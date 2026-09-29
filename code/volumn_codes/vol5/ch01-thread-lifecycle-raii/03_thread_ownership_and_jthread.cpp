// 03_thread_ownership_and_jthread.cpp
// 《线程所有权与 jthread/stop_token》配套代码
// 对应篇章:documents/vol5-concurrency/ch01-thread-lifecycle-raii/
//          03-thread-ownership-and-jthread.md
//
// 演示按正文小节顺序走:
//   1. thread 的 move-only 所有权(move 前后 joinable、工厂返回、sink 接收)
//   2. ThreadGuard:持引用的中间形态(异常路径上自动 join)
//   3. JoiningThread:按值收编的自动 join 类(招牌代码,含 join 抛异常的务实析构)
//   4. jthread 一行替换(析构 = request_stop() 然后 join(),P0660R10)
//   5. 探测式的 token 注入(首参收 token 自动注入;不收 token 原样调用)
//   6. 三件套 stop_source/stop_token(request_stop 幂等计次、晚派生 token、
//      stop_possible 为假的几种情况)
//   7. 轮询:每圈看一眼旗子(正文 polling_worker 里的 process_batch 调用点在此补全)
//   8. stop_callback:停止瞬间的收尾动作(回调同步跑在举旗的线程上)
//   9. 组控制:一个 source 停一组线程(token 排后参,给自动注入让路)
//  10. vector<jthread> 与 parallel_for_each(与串行 std::for_each 对拍验证正确性)
//
// 用法:./03_thread_ownership_and_jthread
// 编译(正文口径,jthread/stop_token 需 -std=c++20,GCC 10 起):
//   g++ -std=c++20 -Wall -Wextra -pedantic -pthread 03_thread_ownership_and_jthread.cpp
//
// 正文的三个练习(可选 detach 的 cancel_join / 组控制装回调 / parallel_accumulate)
// 留给读者,不在本演示里。

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

void section(const std::string& title)
{
    std::cout << "\n===== " << title << " =====\n";
}

//----------------------------------------------------------------------
// 1. thread 是一份只能移动的所有权
//----------------------------------------------------------------------

void worker()
{
    std::cout << "worker running\n";
}

void background_task(int id)
{
    std::cout << "background_task " << id << " done\n";
}

// 工厂函数:把线程造出来交还调用方。返回语句对右值走 move,
// 对具名局部对象走 NRVO 或 move,两条路都绕开拷贝(正文原文)。
std::thread make_worker(int id)
{
    return std::thread(background_task, id);
}

// sink:按值收参,调用方 std::move 把所有权交进来,从这以后线程的生死归这个函数管。
// 正文只给了签名;收下了就管到底,这里补上 join。
void own_the_thread(std::thread t)
{
    t.join();
}

void demo_ownership_move()
{
    std::thread t1(worker);
    std::cout << "t1 joinable: " << t1.joinable() << "\n";  // 1

    std::thread t2 = std::move(t1);  // 所有权从 t1 移交给 t2
    std::cout << "t1 joinable: " << t1.joinable() << "\n";  // 0
    std::cout << "t2 joinable: " << t2.joinable() << "\n";  // 1

    t2.join();  // 此后能收尾的只有 t2

    // 往调用方流:工厂返回
    std::thread from_factory = make_worker(7);
    from_factory.join();

    // 往函数里流:sink 接收,move 出去的一方变空、接手的一方变实
    std::thread t3(background_task, 8);
    own_the_thread(std::move(t3));  // 交出去之后,t3 就别再碰了
    std::cout << "t3 joinable after sink: " << t3.joinable() << "\n";  // 0
}

//----------------------------------------------------------------------
// 2. ThreadGuard:持引用的中间形态
//----------------------------------------------------------------------

// 教学上的中间台阶(正文原样代码):
// 别扭之处是 thread 对象必须活在 guard 外面、还得活得比 guard 长。
class ThreadGuard {
public:
    explicit ThreadGuard(std::thread& t) : thread_(t) {}
    ~ThreadGuard()
    {
        if (thread_.joinable()) {
            thread_.join();
        }
    }
    ThreadGuard(const ThreadGuard&) = delete;
    ThreadGuard& operator=(const ThreadGuard&) = delete;
private:
    std::thread& thread_;  // 持引用:thread 对象必须在外面活着
};

void guard_worker()
{
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::cout << "guard worker done\n";
}

// 中间某一步抛异常:写在函数末尾的 join 成了永远执行不到的代码,
// guard 的析构在栈展开里把 join 补上。
void demo_thread_guard()
{
    std::thread t(guard_worker);  // t 先声明:析构时晚于 guard,顺序正好
    ThreadGuard guard(t);
    throw std::runtime_error("mid-function failure");
}

//----------------------------------------------------------------------
// 3. JoiningThread:按值收编的自动 join
//----------------------------------------------------------------------

// 本文招牌代码(正文原样)。析构函数取的是「join() 自己也会抛异常」一节里
// 的务实版本:join 失败就地吞下、stderr 留痕,不让异常逃出 noexcept 的析构。
class JoiningThread {
public:
    JoiningThread() noexcept = default;

    // 接受任意可调用对象与参数,直接起线程
    template <typename Callable, typename... Args>
    explicit JoiningThread(Callable&& func, Args&&... args)
        : thread_(std::forward<Callable>(func),
                  std::forward<Args>(args)...)
    {}

    // 从现成的 std::thread 接管所有权(按值收参,move 进来)
    explicit JoiningThread(std::thread t) noexcept
        : thread_(std::move(t))
    {}

    JoiningThread(JoiningThread&& other) noexcept
        : thread_(std::move(other.thread_))
    {}

    JoiningThread& operator=(JoiningThread&& other) noexcept
    {
        if (this != &other) {
            if (joinable()) {
                join();  // 在接手新线程以前,把手里的旧线程处理掉
            }
            thread_ = std::move(other.thread_);
        }
        return *this;
    }

    JoiningThread& operator=(std::thread other) noexcept
    {
        if (joinable()) {
            join();      // 同上:旧的收了尾,再来接新的
        }
        thread_ = std::move(other);
        return *this;
    }

    ~JoiningThread()
    {
        if (joinable()) {
            try {
                join();  // 全部的卖点就在这两行
            }
            catch (const std::system_error& e) {
                // 析构函数不允许把异常抛出去,只能吞下并留痕
                std::fprintf(stderr, "JoiningThread: join() failed: %s\n",
                             e.what());
            }
        }
    }

    void join() { thread_.join(); }
    void detach() { thread_.detach(); }
    [[nodiscard]] bool joinable() const noexcept
    {
        return thread_.joinable();
    }
    std::thread& get() noexcept { return thread_; }
    const std::thread& get() const noexcept { return thread_; }

    JoiningThread(const JoiningThread&) = delete;
    JoiningThread& operator=(const JoiningThread&) = delete;

private:
    std::thread thread_;
};

void task(int n)
{
    std::cout << "task " << n << " done\n";
}

void demo_joining_thread()
{
    {
        JoiningThread t1(task, 42);             // 模板构造直接起线程
        JoiningThread t2(std::thread(task, 7)); // 从现成 thread 接管
        // 不需要写任何 join——作用域结束,两个析构函数各自收尾
    }

    // move 赋值:旧线程先了结、新线程再接手(operator= 里那步 join 不能漏)
    JoiningThread a(task, 1);
    JoiningThread b(task, 2);
    b = std::move(a);  // b 的旧线程(task 2)在这里被 join,然后接手 a 的线程
    std::cout << "move 赋值完成:a 成空壳(joinable = " << a.joinable() << ")\n";  // 0
}  // b 析构:join 收尾

//----------------------------------------------------------------------
// 4. jthread:标准补上的自动收尾(一行替换)
//----------------------------------------------------------------------

// 正文里这个函数也叫 worker,这里避开与第 1 节重名。
void jthread_worker()
{
    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::cout << "worker done\n";
}

void demo_jthread_drop_in()
{
    std::jthread t(jthread_worker);  // 类名一换,join 那行删掉,其余原样
    // 作用域结束,t 析构。P0660R10 对 ~jthread() 的措辞:
    //   If joinable() is true, calls request_stop() and then join().
    // 相比手写 JoiningThread,标准版在 join 以前多做了 request_stop()。
}

//----------------------------------------------------------------------
// 5. 探测式的 token 注入
//----------------------------------------------------------------------

void modern_worker(std::stop_token token)  // 首参收 token:自动注入
{
    // jthread 析构以前,它 token 的 stop_possible() 恒为真(内部 source 活着)
    std::cout << "modern_worker: 注入 token 的 stop_possible = "
              << token.stop_possible() << "\n";  // 1
}

void old_worker(int id)  // 不收 token:原样调用
{
    std::cout << "old_worker: id = " << id << ",没收到 token\n";
}

void demo_token_injection_probe()
{
    std::jthread a(modern_worker);  // token 被注入,可取消
    std::jthread b(old_worker, 42); // 退化成"自动 join 线程"
    // 作用域结束:a、b 各自析构:request_stop() + join()
}

//----------------------------------------------------------------------
// 6. 三件套:stop_source、stop_token
//----------------------------------------------------------------------

void demo_stop_trio()
{
    std::stop_source source;             // 默认构造:分配一份停止状态
    std::stop_token token = source.get_token();

    std::cout << "token.stop_requested() = " << token.stop_requested() << "\n";  // 0:还没人举旗

    const bool first  = source.request_stop();  // true:请求由这次调用发出
    const bool second = source.request_stop();  // false:旗子早举着了

    std::cout << "request_stop first/second = " << first << " " << second << "\n";  // 1 0
    std::cout << "token.stop_requested() = " << token.stop_requested() << "\n";    // 1:token 同步看见

    // 正文「实验回填」点名的验证:request_stop 之后再派生的 token 也看得见旗子
    std::stop_token late = source.get_token();
    std::cout << "晚派生 token 的 stop_requested() = " << late.stop_requested() << "\n";  // 1

    // stop_possible() 为假的情况一:token 没关联任何停止状态(默认构造)
    std::stop_token stateless;
    std::cout << "无状态 token 的 stop_possible() = " << stateless.stop_possible() << "\n";  // 0

    // 情况二:状态还在,但既没有请求、世上也不再有任何存活的 stop_source——旗杆拆了
    std::stop_token orphan;
    {
        std::stop_source s;
        orphan = s.get_token();
    }  // s 在这里析构
    std::cout << "旗杆已拆 token 的 stop_possible() = " << orphan.stop_possible() << "\n";  // 0

    // 不需要停止能力时:nostopstate 构造,不分配,noexcept
    std::stop_source no_state(std::nostopstate);
    std::cout << "nostopstate token 的 stop_possible() = "
              << no_state.get_token().stop_possible() << "\n";  // 0
}

//----------------------------------------------------------------------
// 7. 轮询:每圈看一眼旗子
//----------------------------------------------------------------------

// 正文 polling_worker 里只留了这一处调用点(「完整版见代码仓」)。
// 每圈真正要干的活与取消机制无关,给个最小占位,最能看清停止的节奏。
void process_batch(int /*iteration*/)
{
}

void polling_worker(std::stop_token token)
{
    int iteration = 0;
    while (!token.stop_requested()) {  // 每圈看一眼旗子
        process_batch(iteration);      // 完整版见代码仓(就是上面那个占位)
        ++iteration;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::cout << "processed " << iteration << " batches\n";
}

void demo_polling()
{
    std::jthread t(polling_worker);  // token 自动注入
    std::this_thread::sleep_for(std::chrono::seconds(1));
    t.request_stop();  // 举手:下一圈循环条件就过不去了
    // t 析构:再举一次旗(幂等,无害),然后 join
}

//----------------------------------------------------------------------
// 8. 回调:停止瞬间的收尾动作
//----------------------------------------------------------------------

// 正文里这个函数也叫 worker,这里避开重名。
void cb_worker(std::stop_token token)
{
    int counter = 0;
    std::stop_callback cb(token, [&counter] {
        // 注意:这段代码跑在调用 request_stop 的线程上(本例是 main),
        // 不在 worker 自己的线程上——所以下面把线程 id 一起打出来对照
        std::cout << "callback fired, counter = " << counter
                  << " (on thread " << std::this_thread::get_id() << ")\n";
    });

    while (!token.stop_requested()) {
        ++counter;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    std::cout << "worker exits\n";
}

void demo_stop_callback()
{
    // 对照行:回调打出来的线程 id 应与 main 的完全一致
    std::cout << "main thread id = " << std::this_thread::get_id() << "\n";

    std::jthread t(cb_worker);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    t.request_stop();  // 回调在这里同步执行完毕,这一行才返回
}

//----------------------------------------------------------------------
// 9. 组控制:一个 source 停一组线程
//----------------------------------------------------------------------

// 正文示例带了 using namespace std::chrono_literals;(200ms/1s 字面量)
using namespace std::chrono_literals;

void worker_fun(int id, std::stop_token stoken)
{
    // token 排在形参表后面:给自动注入让路的机关就在这里——
    // 注入形式是 f(token, id, 外部token) 三个实参,与本签名对不上,
    // 探测退到"原样调用",咱们显式传的 token 畅通无阻
    while (!stoken.stop_requested()) {
        std::cout << "worker " << id << " is working\n";
        std::this_thread::sleep_for(200ms);
    }
    std::cout << "worker " << id << " exits\n";
}

void demo_group_control()
{
    std::stop_source source;  // 外部控场的 source
    std::jthread threads[4];

    for (int i = 0; i < 4; ++i) {
        // token 作为尾参显式传入:四个线程共享同一份停止状态
        threads[i] = std::jthread(worker_fun, i + 1, source.get_token());
    }

    std::this_thread::sleep_for(1s);
    source.request_stop();  // 一次请求,四个线程一起看见
    // 函数返回,数组析构:每个 jthread 各自收尾(内部 source 的旗没人听,
    // 等于白举一次;join 逐个照做)
}

//----------------------------------------------------------------------
// 10. vector<jthread> 与 parallel_for_each
//----------------------------------------------------------------------

// 正文原样代码:分块并行 + 最后一块留给调用方自己算,收尾全靠 vector 析构。
template <typename Iterator, typename Func>
void parallel_for_each(Iterator first, Iterator last, Func func,
                       unsigned thread_count)
{
    const std::size_t length = std::distance(first, last);
    if (length == 0) {
        return;
    }
    if (thread_count == 0) {
        // hardware_concurrency() 只是提示值,讲法见 ch01/01
        thread_count = std::thread::hardware_concurrency();
    }
    if (thread_count == 0) {
        // 查询也会失手(返回 0):兜成 1,不然下面的
        // thread_count - 1 在无符号数上回绕成巨值
        thread_count = 1;
    }

    const std::size_t block_size = length / thread_count;
    std::vector<std::jthread> threads;
    threads.reserve(thread_count);  // 容量一次给足,扩容搬移的动静省了

    Iterator block_start = first;
    for (unsigned i = 0; i < thread_count - 1; ++i) {
        Iterator block_end = block_start;
        std::advance(block_end, block_size);
        threads.emplace_back([block_start, block_end, &func] {
            std::for_each(block_start, block_end, func);
        });
        block_start = block_end;
    }

    std::for_each(block_start, last, func);  // 最后一块调用方自己算,
                                             // 少开一个线程
    // 函数返回,vector 析构:逐个元素析构,逐个 join,零手动收尾
}

void demo_parallel_for_each()
{
    std::vector<int> data(1000);
    std::iota(data.begin(), data.end(), 0);
    std::vector<int> expected = data;

    parallel_for_each(data.begin(), data.end(), [](int& v) { v *= 2; }, 4);
    for (int& v : expected) {
        v *= 2;  // 串行参照
    }

    std::cout << "parallel_for_each 与串行结果一致: " << (data == expected) << "\n";  // 1
}

}  // namespace

int main()
{
    section("1. thread 是一份只能移动的所有权");
    demo_ownership_move();

    section("2. ThreadGuard:持引用的中间形态");
    try {
        demo_thread_guard();
    }
    catch (const std::exception& e) {
        std::cout << "caught: " << e.what() << "\n";
    }

    section("3. JoiningThread:按值收编的自动 join");
    demo_joining_thread();

    section("4. jthread:标准补上的自动收尾(一行替换)");
    demo_jthread_drop_in();

    section("5. 探测式的 token 注入");
    demo_token_injection_probe();

    section("6. 三件套:stop_source / stop_token");
    demo_stop_trio();

    section("7. 轮询:每圈看一眼旗子");
    demo_polling();

    section("8. 回调:停止瞬间的收尾动作");
    demo_stop_callback();

    section("9. 组控制:一个 source 停一组线程");
    demo_group_control();

    section("10. vector<jthread> 与 parallel_for_each");
    demo_parallel_for_each();

    std::cout << "\n全部演示结束\n";
    return 0;
}
