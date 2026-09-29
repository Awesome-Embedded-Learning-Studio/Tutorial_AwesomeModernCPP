// 02_thread_arguments_and_lifetime.cpp
// 《线程参数与生命周期陷阱》配套代码
//
// 用法：
//   ./02_thread_arguments_and_lifetime              # 安全演示全跑
//   ./02_thread_arguments_and_lifetime dangling     # 故意的悬垂引用，配 ASan 抓现行
//   ./02_thread_arguments_and_lifetime bad-worker   # detach 后对象先死（裸 bool 版，另有 data race）
//   ./02_thread_arguments_and_lifetime bad-worker-atomic  # 换 atomic 仍悬垂：看似修好的坏例
//
// 编译（文章口径，GCC 16.2.1 验证零警告）：
//   g++ -std=c++20 -Wall -Wextra -pedantic -pthread 02_thread_arguments_and_lifetime.cpp
// ASan 镜头（抓 dangling / bad-worker-atomic 两个模式）：
//   g++ -std=c++20 -fsanitize=address -g -pthread 02_thread_arguments_and_lifetime.cpp
// TSan 镜头（对照：悬垂不归它管，bad-worker 的裸 bool 才归它管）：
//   g++ -std=c++20 -fsanitize=thread -g -O2 -pthread 02_thread_arguments_and_lifetime.cpp
//
// 两个"编译不过"的片段（decay-copy 拒绝引用、unique_ptr 拒绝拷贝）见文末注释，
// 它们进不了任何可执行文件，报错原文在文章里。

#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

//----------------------------------------------------------------------
// 第一幕：decay-copy 的传参正路
//----------------------------------------------------------------------

void update_value(int& x)
{
    x = 42;
}

// 演示 1：decay-copy 按值拷贝，想改原件必须 std::ref 包一层。
//   std::thread t(update_value, value);  // 编译不过，报错原文见文末注释 A
void demo_decay_and_ref()
{
    int value = 0;
    std::thread t(update_value, std::ref(value));
    t.join();
    std::cout << "demo_decay_and_ref: value = " << value << '\n';  // 42
}

void append_suffix(std::string& str, const std::string& suffix)
{
    str += suffix;
}

// 演示 2：引用参数的完整形态——可变的用 ref，只读的用 cref。
void demo_ref_cref()
{
    std::string message = "Hello";
    std::string suffix = " World";
    std::thread t(append_suffix, std::ref(message), std::cref(suffix));
    t.join();
    std::cout << "demo_ref_cref: message = " << message << '\n';  // Hello World
}

void process_data(std::unique_ptr<int> data)
{
    std::cout << "demo_move_only: *data = " << *data << '\n';
}

// 演示 3：move-only 参数，所有权显式移进线程。
//   std::thread t(process_data, p);  // 编译不过，报错原文见文末注释 B
void demo_move_only()
{
    auto p = std::make_unique<int>(42);
    std::thread t(process_data, std::move(p));  // move 之后 p 是空指针，别再碰
    t.join();
}

struct Greeter {
    void greet(const std::string& name) const
    {
        std::cout << "demo_member_fn: greeter says: " << name << '\n';
    }
};

// 演示 4：成员函数做入口。注意 &g 本身也是个参数——decay-copy 拷的是指针的值，
// 拷不走对象本身，对象得自己活过线程（本例靠 t.join() 兜底）。
void demo_member_fn()
{
    Greeter g;
    std::thread t(&Greeter::greet, &g, "hello member fn");
    t.join();
}

struct Counter {
    int n = 0;
    void operator()() { ++n; }
};

// 演示 5：functor 传值，线程跑的是拷贝。
void demo_functor_copy()
{
    Counter c;
    std::thread t(c);  // decay-copy：闭包里存的是 c 的副本
    t.join();
    std::cout << "demo_functor_copy: c.n = " << c.n << '\n';  // 0：动的是副本
}

#if defined(SHOW_DEPRECATION)
// 演示 6（选编译）：[=] 隐式捕获 this 已在 C++20 弃用（P0806R2）。
// 加 -DSHOW_DEPRECATION 编译可以看到 GCC 的警告：
//   warning: implicit capture of 'this' via '[=]' is deprecated in C++20 [-Wdeprecated]
//     note: add explicit 'this' or '*this' capture
// 默认构建不带这个宏，保持零警告。
class ImplicitThisDemo {
public:
    void run()
    {
        std::thread t([=] { std::cout << "implicit this: tag_ = " << tag_ << '\n'; });
        t.join();
    }

private:
    int tag_ = 7;
};
#endif  // SHOW_DEPRECATION

// 演示 6b：两种替换写法。[=, this] 还是指针捕获，[=, *this] 拷的是整个对象副本。
class CaptureDemo {
public:
    void run()
    {
        int local = 1;
        {
            std::thread t([=, this] {  // 警告消失，语义同旧：this 仍是指针
                std::cout << "capture [=, this]: counter_ = " << counter_
                          << ", local = " << local << '\n';
            });
            t.join();
        }
        {
            std::thread t([=, *this]() mutable {  // 对象副本进闭包
                ++counter_;
                std::cout << "capture [=, *this]: counter_ (copy) = " << counter_
                          << '\n';
            });
            t.join();
        }
        std::cout << "capture original: counter_ = " << counter_ << '\n';  // 仍是 7
    }

private:
    int counter_ = 7;
};

void demo_capture()
{
#if defined(SHOW_DEPRECATION)
    ImplicitThisDemo demo;
    demo.run();
#endif
    CaptureDemo capture_demo;
    capture_demo.run();
}

//----------------------------------------------------------------------
// 第二幕：修复三板斧
//----------------------------------------------------------------------

// 板斧一：值捕获。闭包里是 message 的副本，detach 之后外面的生死与线程无关。
void demo_value_capture()
{
    std::string message = "Hello from parent";
    std::thread t([message] {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::cout << "demo_value_capture: Thread sees: " << message << '\n';
    });
    t.detach();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));  // 等线程把话说完
}

// 板斧二：shared_ptr 延长生命周期。控制块的引用计数是原子的，
// 线程闭包攥着最后一个引用，atomic<bool> 活到线程退出之后才释放。
class SharedFlagWorker {
public:
    SharedFlagWorker() : running_(std::make_shared<std::atomic<bool>>(false)) {}

    void start()
    {
        running_->store(true);
        auto running = running_;  // 拷一份 shared_ptr，计数 +1
        std::thread t([running] {
            while (running->load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            std::cout << "demo_shared_ptr: worker exiting cleanly\n";
        });
        t.detach();
    }

    void stop() { running_->store(false); }

private:
    std::shared_ptr<std::atomic<bool>> running_;
};

void demo_shared_ptr()
{
    SharedFlagWorker worker;
    worker.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    worker.stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));  // 等线程收尾
}

// 板斧三：不 detach，join 收尾。析构 = 写停止标志 + 等线程结束，
// 成员销毁排在线程之后，this 从头到尾没有悬垂的机会。
class JoiningWorker {
public:
    void start()
    {
        running_ = true;
        thread_ = std::thread([this] {
            while (running_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            std::cout << "demo_join: worker exiting cleanly\n";
        });
    }

    ~JoiningWorker()
    {
        running_ = false;
        if (thread_.joinable()) {
            thread_.join();
        }
    }

private:
    // 这里用 atomic 图的是停止标志的可见性；它管不了生命周期，文章里有专节。
    std::atomic<bool> running_ = false;
    std::thread thread_;
};

void demo_join_worker()
{
    JoiningWorker worker;
    worker.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    // 作用域结束：析构写 false 并 join，线程一定先于成员死亡
}

// [&, i] 批量捕获的"眼下能活"版：安全全押在末尾那几行 join 上，
// join 一改成 detach 就是批量悬垂，文章第三场事故讲的就是它。
void parallel_square(const std::vector<int>& input, std::vector<int>& output)
{
    std::vector<std::thread> threads;
    for (std::size_t i = 0; i < input.size(); ++i) {
        threads.emplace_back([&, i] {  // input、output 引用捕获，i 值捕获
            output[i] = input[i] * input[i];
        });
    }
    for (auto& t : threads) {
        t.join();
    }
}

void demo_parallel_square()
{
    std::vector<int> input{1, 2, 3, 4, 5, 6, 7, 8};
    std::vector<int> output(input.size(), 0);
    parallel_square(input, output);
    std::cout << "demo_parallel_square:";
    for (int v : output) {
        std::cout << ' ' << v;
    }
    std::cout << '\n';
}

//----------------------------------------------------------------------
// 事故现场：只在不带参数的时候不跑，配 ASan/TSan 使用
//----------------------------------------------------------------------

// 事故一：detach + [&local_value]。faulty_function 返回后栈帧拆除，
// 线程 100 毫秒后醒来读一块已归还的栈内存。ASan 报 stack-use-after-return。
void faulty_function()
{
    // 一个看起来无害的局部变量
    int local_value = 42;

    std::thread t([&local_value] {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::cout << "Value: " << local_value << '\n';  // local_value 已死
    });
    t.detach();
}

// 事故二：detach 之后对象先死。裸 bool 版还叠着一层 data race（TSan 也管得着）。
// 循环里保留打印：一来与文章里的坏例同形，二来纯睡觉的循环会被 -O2 优化
// 吊走读操作（data race 本身就是 UB，编译器有这个权利），TSan 就没得抓了。
class BadWorkerPlain {
public:
    void start()
    {
        running_ = true;
        std::thread t([this] {
            while (running_) {
                std::cout << "Working...\n";
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            std::cout << "BadWorkerPlain: exiting\n";
        });
        t.detach();
    }

    void stop() { running_ = false; }

private:
    bool running_ = false;
};

// 事故二的"看似修好"版：race 确实修掉了，生命周期没有——
// 成员随对象一起销毁，线程下一圈读的是死内存上的原子量。
class BadWorkerAtomic {
public:
    void start()
    {
        running_.store(true);
        std::thread t([this] {
            while (running_.load()) {
                std::cout << "Working...\n";
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            std::cout << "BadWorkerAtomic: exiting\n";
        });
        t.detach();
    }

    void stop() { running_.store(false); }

private:
    std::atomic<bool> running_ = false;
};

}  // namespace

int main(int argc, char* argv[])
{
    const std::string mode = (argc > 1) ? argv[1] : "";

    if (mode == "dangling") {
        faulty_function();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        std::cout << "main done\n";
        return 0;
    }
    if (mode == "bad-worker") {
        {
            BadWorkerPlain worker;
            worker.start();
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            worker.stop();  // 与循环里的读并发、无同步：TSan 镜头下的 data race 现行
            // 对象多活一会儿，让竞态读落在活内存上，TSan 才抓得稳；
            // 紧跟着析构的话，UB 之下的编译器优化可能把循环读搅得没谱
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
        return 0;
    }
    if (mode == "bad-worker-atomic") {
        {
            BadWorkerAtomic worker;
            worker.start();
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            worker.stop();  // 可见性有了，对象还是要死
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
        return 0;
    }

    demo_decay_and_ref();
    demo_ref_cref();
    demo_move_only();
    demo_member_fn();
    demo_functor_copy();
    demo_capture();
    demo_value_capture();
    demo_shared_ptr();
    demo_join_worker();
    demo_parallel_square();
    std::cout << "main done\n";
    return 0;
}

//----------------------------------------------------------------------
// 注释 A：decay-copy 拒绝引用参数（GCC 16.2.1 逐字）
//
//   std::thread t(update_value, value);   // update_value 要 int&，传的是 int
//
//   error: static assertion failed: std::thread arguments must be
//   invocable after conversion to rvalues
//
// 随错误摆出的 _Invoker<std::tuple<void (*)(int&), int>> 里能读出
// decay 之后的实参类型：int& 已经变成了 int。
//
// 注释 B：unique_ptr 拒绝拷贝（GCC 16.2.1 逐字）
//
//   auto p = std::make_unique<int>(42);
//   std::thread t(process_data, p);       // 少一个 std::move
//
//   error: no matching function for call to 'std::tuple<void (*)(std::unique_ptr<int>),
//   std::unique_ptr<int> >::tuple(void (&)(std::unique_ptr<int>),
//   std::unique_ptr<int>&)'
//
// 线程内部要拿 tuple 把函数与参数 decay-copy 存起来，unique_ptr 的拷贝
// 被删了、又只给了个左值，tuple 的构造函数找不出能对上的重载。
//----------------------------------------------------------------------
