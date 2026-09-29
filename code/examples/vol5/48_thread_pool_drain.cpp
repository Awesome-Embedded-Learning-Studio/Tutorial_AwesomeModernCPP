// 线程池化简版（C++20：jthread + stop_token + 三参 wait）的正确性验收：
// 300 轮 drain+干净退出、异常经 future 重抛、关门后拒收且已入队任务照跑
#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

class ThreadPool {
  public:
    explicit ThreadPool(std::size_t num_threads) {
        for (std::size_t i = 0; i < num_threads; ++i) {
            // stop_source_ 的 token 随 worker 一起发下去
            workers_.emplace_back([this](std::stop_token st) { worker_loop(st); });
        }
    }

    ~ThreadPool() {
        stop_source_.request_stop();
        cv_any_.notify_all(); // 廉价的双保险
        workers_.clear();     // join 必须发生在其余成员死亡之前
    }

    void stop() {
        stop_source_.request_stop();
        cv_any_.notify_all();
    }

    template <typename F, typename... Args> auto submit(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>> {
        using R = std::invoke_result_t<F, Args...>;
        auto task = std::make_shared<std::packaged_task<R()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...));
        std::future<R> fut = task->get_future();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stop_source_.stop_requested()) { // 关门期间拒收新任务
                throw std::runtime_error("submit on stopped ThreadPool");
            }
            tasks_.push([task] { (*task)(); });
        }
        cv_any_.notify_one(); // 一个新任务叫醒一个人
        return fut;
    }

  private:
    void worker_loop(std::stop_token st) {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                // 返回 false：stop 已请求，且队列已空（drain 完成）
                // 返回 true ：队列非空，取任务执行
                if (!cv_any_.wait(lock, st, [this] { return !tasks_.empty(); })) {
                    return;
                }
                task = std::move(tasks_.front());
                tasks_.pop();
            }
            task(); // 锁外执行
        }
    }

    std::vector<std::jthread> workers_; // 排在成员表最前面
    std::stop_source stop_source_;
    std::mutex mutex_;
    std::condition_variable_any cv_any_;
    std::queue<std::function<void()>> tasks_;
};

int main() {
    // 1) drain + 干净退出：4 worker、100 任务，反复建拆 300 轮
    constexpr int kRounds = 300;
    bool drain_ok = true;
    for (int round = 0; round < kRounds; ++round) {
        std::atomic<int> done{0};
        {
            ThreadPool pool(4);
            for (int i = 0; i < 100; ++i) {
                pool.submit([&done] { done.fetch_add(1, std::memory_order_relaxed); });
            }
        } // 析构：request_stop + drain + join
        if (done.load() != 100) {
            drain_ok = false;
        }
    }
    std::cout << "drain+clean-exit: " << (drain_ok ? "PASS" : "FAIL") << " (" << kRounds
              << " rounds)\n";

    // 2) 任务抛出的异常经 future 重抛给调用方
    bool exception_ok = false;
    {
        ThreadPool pool(2);
        auto fut = pool.submit([] {
            throw std::logic_error("boom");
            return 0;
        });
        try {
            (void)fut.get();
        } catch (const std::logic_error&) {
            exception_ok = true;
        }
    }
    std::cout << "exception-via-future: " << (exception_ok ? "PASS" : "FAIL") << '\n';

    // 3) stop 之后 submit 被拒；stop 之前入队的任务照常跑完（drain）
    bool rejected_ok = false;
    bool drained_ok = false;
    {
        ThreadPool pool(2);
        std::promise<void> ran;
        auto ran_fut = ran.get_future();
        pool.submit([&ran] { ran.set_value(); });
        pool.stop();
        try {
            (void)pool.submit([] { return 1; });
        } catch (const std::runtime_error&) {
            rejected_ok = true;
        }
        ran_fut.get(); // 任务真跑过，set_value 才会发生
        drained_ok = true;
    }
    std::cout << "submit-after-stop rejected: " << (rejected_ok ? "PASS" : "FAIL")
              << ", drained task ran: " << (drained_ok ? "PASS" : "FAIL") << '\n';
}
