// 03_thread_pool.cpp —— 卷五 ch05/03《线程池（正源）》配套完整代码
//
// 内容：
//   1. ThreadPool17 —— C++17 裸版：std::thread + mutex + cv + bool stop_
//   2. ThreadPool20 —— C++20 版：jthread + 池持 stop_source_ + cv_any 三参 wait
//      worker_loop 是全卷正源（化简版：一行判 wait 返回值编码 drain 语义）
//      析构函数在函数体内显式 workers_.clear()：join 必须发生在其余成员死亡之前
//
// 本机验证记录（GCC 16.2.1, x86-64 Linux）：
//   - 300 轮 × 4 worker × 100 任务 drain 完整性 + 干净退出：PASS
//   - 析构仅 request_stop（去掉 notify_all）：300 + 3×1000 轮 + TSan(-O1 -g) 全部干净退出
//   - 异常经 future 重抛 / 停止后 submit 拒收 / 停止前最后任务仍执行：PASS
//   - 教训现场：若析构依赖成员逆序销毁去 join（workers_ 声明在最前、最后才析构），
//     析构时队列尚有存货会 SIGSEGV / terminate(std::future_error: No associated state)
//
// 编译运行：
//   g++ -std=c++20 -Wall -Wextra -pedantic -pthread -O2 -o 03_thread_pool 03_thread_pool.cpp
//   ./03_thread_pool
// TSan 走查：
//   g++ -std=c++20 -Wall -Wextra -pthread -O1 -g -fsanitize=thread -o 03_thread_pool_tsan 03_thread_pool.cpp
//   ./03_thread_pool_tsan

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <stop_token>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// C++17 裸版：手动 stop_ 标志 + 手动 notify_all + 手动 join
// ---------------------------------------------------------------------------
class ThreadPool17 {
public:
    explicit ThreadPool17(std::size_t num_threads) {
        for (std::size_t i = 0; i < num_threads; ++i) {
            workers_.emplace_back([this] { worker_loop(); });
        }
    }

    ~ThreadPool17() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;                // ① 持锁置位
        }
        cv_.notify_all();                // ② 解锁后唤醒
        for (auto& w : workers_) {       // ③ 最后 join（顺序反了是死锁）
            w.join();
        }
    }

    ThreadPool17(const ThreadPool17&) = delete;
    ThreadPool17& operator=(const ThreadPool17&) = delete;

    template <typename F, typename... Args>
    auto submit(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>> {
        using R = std::invoke_result_t<F, Args...>;
        auto task = std::make_shared<std::packaged_task<R()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...));
        std::future<R> fut = task->get_future();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stop_) {
                throw std::runtime_error("submit on stopped ThreadPool17");
            }
            tasks_.push([task] { (*task)(); });
        }
        cv_.notify_one();
        return fut;
    }

private:
    void worker_loop() {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
                if (stop_ && tasks_.empty()) {
                    return;               // drain 语义：停且空才退
                }
                task = std::move(tasks_.front());
                tasks_.pop();
            }                             // 锁内取任务……
            task();                       // ……锁外执行
        }
    }

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stop_ = false;
};

// ---------------------------------------------------------------------------
// C++20 版：池持 stop_source_，worker 收 token，cv_any 三参 wait
// ---------------------------------------------------------------------------
class ThreadPool20 {
public:
    explicit ThreadPool20(std::size_t num_threads) {
        for (std::size_t i = 0; i < num_threads; ++i) {
            // token 显式捕获传入：全体 worker 共享池持有的这一份停止状态，
            // jthread 自动注入的内部 token 刻意不用（每线程一个 source，停不到全体）
            workers_.emplace_back(
                [this, st = stop_source_.get_token()] { worker_loop(st); });
        }
    }

    ~ThreadPool20() {
        stop_source_.request_stop();     // 规范上即会唤醒三参 wait 的等待者
        cv_any_.notify_all();            // 廉价双保险（实验：去掉也能干净退出）
        workers_.clear();                // join 必须发生在其余成员死亡之前！
    }                                    // 依赖成员逆序销毁去 join 是真实翻车点

    ThreadPool20(const ThreadPool20&) = delete;
    ThreadPool20& operator=(const ThreadPool20&) = delete;

    template <typename F, typename... Args>
    auto submit(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>> {
        using R = std::invoke_result_t<F, Args...>;
        auto task = std::make_shared<std::packaged_task<R()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...));
        std::future<R> fut = task->get_future();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stop_source_.stop_requested()) {
                throw std::runtime_error("submit on stopped ThreadPool20");
            }
            tasks_.push([task] { (*task)(); });
        }
        cv_any_.notify_one();
        return fut;
    }

    void stop() { stop_source_.request_stop(); }  // 提前关门（测试用）

private:
    void worker_loop(std::stop_token st) {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                // 返回 false ⟺ stop 已请求且此刻队列已空（drain 完成）
                // 返回 true  ⟺ 谓词成立（队列非空）
                if (!cv_any_.wait(lock, st, [this] { return !tasks_.empty(); })) {
                    return;
                }
                task = std::move(tasks_.front());
                tasks_.pop();
            }
            task();
        }
    }

    std::vector<std::jthread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex mutex_;
    std::condition_variable_any cv_any_;
    std::stop_source stop_source_;
};

// ---------------------------------------------------------------------------
// 演示（main 验证的是 C++20 化简版，C++17 裸版在上方供对照阅读）：
//   ① 300 轮 drain 完整性：每轮 4 worker × 100 任务，不做 get 直接拆池
//   ② 异常经 future 重抛
//   ③ 停止后拒收 + 停止前最后一次提交仍执行
// 输出与正文引用的 PASS 三行一一对应。
// ---------------------------------------------------------------------------
int main() {
    // 1) 300 轮 drain：每轮核对 100 个任务全部执行、线程全部干净退出
    bool drain_ok = true;
    for (int round = 0; round < 300; ++round) {
        std::atomic<int> executed{0};
        {
            ThreadPool20 pool(4);
            for (int i = 0; i < 100; ++i) {
                (void)pool.submit([&executed, i] {
                    ++executed;
                    return i;
                });
            }
            // 不 get：析构开始时队列大概率还有存货，正好检验 drain
        }
        drain_ok = drain_ok && executed.load() == 100;
    }
    std::cout << "drain+clean-exit: " << (drain_ok ? "PASS" : "FAIL")
              << " (300 rounds)\n";

    // 2) 异常经 future 重抛（packaged_task 把异常捕进共享状态）
    bool exc_ok = false;
    {
        ThreadPool20 pool(1);
        auto f = pool.submit([]() -> int { throw std::invalid_argument("boom"); });
        try {
            (void)f.get();
        } catch (const std::invalid_argument&) {
            exc_ok = true;
        }
    }
    std::cout << "exception-via-future: " << (exc_ok ? "PASS" : "FAIL") << "\n";

    // 3) 停止后拒收；停止前最后一次提交仍要执行完
    bool reject_ok = false;
    bool last_ok = false;
    {
        ThreadPool20 pool(2);
        auto f = pool.submit([] { return 7; });
        pool.stop();
        try {
            (void)pool.submit([] { return 8; });
        } catch (const std::runtime_error&) {
            reject_ok = true;
        }
        last_ok = f.get() == 7;
    }
    std::cout << "submit-after-stop rejected: " << (reject_ok ? "PASS" : "FAIL")
              << ", drained task ran: " << (last_ok ? "PASS" : "FAIL") << "\n";
    return 0;
}
