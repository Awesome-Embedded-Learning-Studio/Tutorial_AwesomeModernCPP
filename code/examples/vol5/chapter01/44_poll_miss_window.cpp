// 轮询错过窗口:标志在 sleep 中间置位,wait_for 还得睡满整个超时
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <print>
#include <thread>

using namespace std::chrono_literals;

int main() {
    std::atomic<bool> stop_flag{false};
    std::mutex m;
    std::condition_variable cv;

    std::thread worker([&] {
        std::unique_lock<std::mutex> lock(m);
        auto t0 = std::chrono::steady_clock::now();
        bool woke = cv.wait_for(lock, 500ms, [&] { return stop_flag.load(); });
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - t0)
                      .count();
        std::println("wait_for returned {} after {} ms", woke, ms);
    });

    std::this_thread::sleep_for(50ms);
    stop_flag.store(true);
    std::println("flag set to true at 50 ms, nobody notifies");
    worker.join();
}
