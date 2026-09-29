/*
 * 演示：std::thread 基础 —— 构造的三种入口、join 与 detach、joinable、
 *       线程身份证、hardware_concurrency，以及手动 join 的 parallel_for_each
 *
 * 背景：文章 vol5 ch01 "std::thread 基础" 涵盖——
 *       1. 构造的三种形态：函数指针、lambda、函数对象（函数对象会被拷一份进线程）
 *       2. join：阻塞到线程执行完毕；detach：放线程单飞
 *       3. 什么都不做（不 join 不 detach）→ 析构时 std::terminate（见文末注释块）
 *       4. joinable 的四种空壳状态；get_id / this_thread::get_id
 *       5. hardware_concurrency：提示值，查不出来返回 0（本骨架故意不兜底）
 *       6. 最费解解析：std::thread t(Accumulator()) 被解析成函数声明（见文末注释块）
 *       7. parallel_for_each：vector<std::thread> + 手动 join 的并行骨架
 *
 * 预期结果：
 *   依次打印三种入口的输出、join/detach 的时序、线程 id（数字部分每次运行不同）、
 *   hardware_concurrency（本机 20），最后打印 sum = 999000。
 *
 * 编译命令：
 *   g++ -std=c++20 -Wall -Wextra -pedantic -pthread 01_std_thread.cpp -o 01_std_thread
 *   ./01_std_thread
 *
 * 编译器：GCC 12+ | Clang 15+ | MSVC 19.3+
 * 平台：x86-64 Linux / macOS / Windows
 * C++ 标准：C++11（示例用 C++20 编译只为对齐全卷口径）
 */

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
// 1. 入口形态一：普通函数
// ---------------------------------------------------------------------------
void print_hello(int id)
{
    std::cout << "Hello from thread " << id << "\n";
}

// ---------------------------------------------------------------------------
// 2. 入口形态三：函数对象（带状态，重载 operator()）
//    注意：std::thread 会把它拷一份带进线程（decay-copy），细节见 ch01/02
// ---------------------------------------------------------------------------
class Accumulator {
public:
    Accumulator(const std::vector<int>& data, int& result)
        : data_(data), result_(result) {}

    void operator()() const
    {
        int local_sum = 0;
        for (int v : data_) {
            local_sum += v;
        }
        result_ = local_sum;
    }

private:
    const std::vector<int>& data_;
    int& result_;
};

// ---------------------------------------------------------------------------
// 3. join 的示例入口
// ---------------------------------------------------------------------------
void slow_work()
{
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    std::cout << "worker: done\n";
}

// ---------------------------------------------------------------------------
// 4. detach 的示例入口（主线程很快退出，进程多半等不到它打印）
// ---------------------------------------------------------------------------
void background_cleanup()
{
    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::cout << "cleanup: done\n";
}

// ---------------------------------------------------------------------------
// 5. 身份证示例的入口
// ---------------------------------------------------------------------------
void worker()
{
    std::cout << "worker 自己的 id: "
              << std::this_thread::get_id() << "\n";
}

// ---------------------------------------------------------------------------
// 6. 收尾骨架：parallel_for_each
//    - thread_count 传 0 表示“您看着办”，骨架去问 hardware_concurrency()
//    - 最后一块留给调用方自己啃，省一条线程
//    - 故意不兜 hardware_concurrency() == 0：这个隐患留给 ch01/03 的
//      jthread 版骨架去堵（届时还会加上自动 join）
// ---------------------------------------------------------------------------
template <typename Iterator, typename Func>
void parallel_for_each(Iterator first, Iterator last, Func func,
                       unsigned thread_count)
{
    const std::size_t length = std::distance(first, last);
    if (length == 0) {
        return;
    }
    if (thread_count == 0) {
        thread_count = std::thread::hardware_concurrency();
    }

    const std::size_t block_size = length / thread_count;
    std::vector<std::thread> threads;
    threads.reserve(thread_count);

    Iterator block_start = first;
    for (unsigned i = 0; i < thread_count - 1; ++i) {
        Iterator block_end = block_start;
        std::advance(block_end, block_size);
        threads.emplace_back([block_start, block_end, &func] {
            std::for_each(block_start, block_end, func);
        });
        block_start = block_end;
    }

    std::for_each(block_start, last, func);  // 最后一块，调用方自己啃

    for (std::thread& t : threads) {
        t.join();                            // 一串手动 join，一个都不能少
    }
}

int main()
{
    // --- 1. 函数指针入口 -----------------------------------------------
    std::thread t1(print_hello, 42);
    t1.join();

    // --- 2. lambda 入口（引用捕获的味道：寿命对得上才安全） -------------
    {
        std::vector<int> data = {1, 2, 3, 4, 5};
        long sum = 0;

        std::thread t([&data, &sum] {
            for (int v : data) {
                sum += v;
            }
        });

        t.join();
        std::cout << "sum = " << sum << "\n";   // 15
    }

    // --- 3. 函数对象入口（线程里跑的是 acc 的拷贝） ----------------------
    {
        std::vector<int> data(10000, 1);
        int result = 0;

        Accumulator acc(data, result);
        std::thread t(acc);

        t.join();
        std::cout << "result = " << result << "\n";  // 10000
    }

    // --- 4. join：等它跑完 ------------------------------------------------
    {
        std::thread t(slow_work);
        std::cout << "main: waiting\n";
        t.join();
        std::cout << "main: joined, worker is done\n";
    }

    // --- 5. detach：放它单飞（进程多半等不到 cleanup 的输出） -------------
    {
        std::thread t(background_cleanup);
        t.detach();
        std::cout << "main: detached, leaving now\n";
    }

    // --- 6. 线程的身份证 ---------------------------------------------------
    {
        std::thread t(worker);
        std::cout << "main 的 id: " << std::this_thread::get_id() << "\n";
        std::cout << "t 的 id:     " << t.get_id() << "\n";

        t.join();
        std::cout << "join 之后 t 的 id: " << t.get_id() << "\n";
    }

    // --- 7. hardware_concurrency：提示值，查不出来返回 0 -------------------
    std::cout << "hardware_concurrency = "
              << std::thread::hardware_concurrency() << "\n";  // 本机 20

    // --- 8. parallel_for_each：每个元素翻倍，与串行版本对拍 -----------------
    {
        std::vector<int> data(1000);
        for (std::size_t i = 0; i < data.size(); ++i) {
            data[i] = static_cast<int>(i);
        }

        parallel_for_each(data.begin(), data.end(),
                          [](int& v) { v *= 2; },
                          0);

        long sum = 0;
        for (int v : data) {
            sum += v;
        }
        std::cout << "sum = " << sum << "\n";  // 期望 999000
    }

    return 0;
}

// ---------------------------------------------------------------------------
// 附 A. 什么都不做，会怎样（放开注释后程序以 SIGABRT 终止）
//
// int main()
// {
//     std::thread t(some_work);
//     // 既没有 join()，也没有 detach()
//     return 0;
// }
//
// 本机运行输出（退出码 134，即 SIGABRT）：
//   terminate called without an active exception
//
// ---------------------------------------------------------------------------
// 附 B. 最费解解析（放开注释后编译失败，与文章抄录的报错一致）
//
// std::thread t(Accumulator());   // 您以为在定义线程，其实在声明函数
// t.join();
//
// GCC 16.2.1 原文：
//   mvp_true.cpp:26:18: warning: parentheses were disambiguated as a function
//                          declaration [-Wvexing-parse]
//   mvp_true.cpp:26:18: note: replace parentheses with braces to declare a
//                          variable
//   mvp_true.cpp:27:7: error: request for member 'join' in 't', which is of
//                          non-class type 'std::thread(Accumulator (*)())'
//
// 解读：Accumulator() 恰好读得成一个形参（“返回 Accumulator 的无参函数”
// 做形参时会被调整成函数指针），于是整句话成了函数声明，t 根本不是对象。
// 带实参的 std::thread t(Accumulator(data, result)); 不受影响，它编得过。
// 修法：统一花括号 —— std::thread t{Accumulator(data, result)};
// （注意 std::thread t{Accumulator()}; 会报 no matching function for call to
//  'Accumulator::Accumulator()'：这个类没有默认构造。）
// ---------------------------------------------------------------------------
