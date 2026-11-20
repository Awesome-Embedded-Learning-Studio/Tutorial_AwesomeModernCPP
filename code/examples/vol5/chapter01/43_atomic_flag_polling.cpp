// 停止协议的前身:atomic 标志加轮询,20ms 一问
#include <atomic>
#include <chrono>
#include <print>
#include <thread>

using namespace std::chrono_literals;

int main() {
    std::atomic<bool> stop_flag{false};
    auto t0 = std::chrono::steady_clock::now();

    std::thread worker([&] {
        long polls = 0;
        while (!stop_flag.load()) {
            ++polls;
            std::this_thread::sleep_for(20ms);
        }
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - t0)
                      .count();
        std::println("worker noticed at {} ms after {} polls", ms, polls);
    });

    std::this_thread::sleep_for(50ms);
    stop_flag.store(true);
    std::println("flag set at 50 ms");
    worker.join();
}
