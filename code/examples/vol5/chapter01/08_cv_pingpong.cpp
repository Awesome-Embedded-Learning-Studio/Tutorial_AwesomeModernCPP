// 条件变量乒乓:两个线程互相通知十万次,量一次唤醒往返的开销
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <print>
#include <thread>

int main() {
    std::mutex m;
    std::condition_variable cv;
    int turn = 0;
    constexpr long kRounds = 100000;
    std::thread worker([&] {
        std::unique_lock<std::mutex> lk(m);
        for (long i = 0; i < kRounds; ++i) {
            cv.wait(lk, [&] { return turn == 1; });
            turn = 0;
            cv.notify_one();
        }
    });
    auto start = std::chrono::steady_clock::now();
    {
        std::unique_lock<std::mutex> lk(m);
        for (long i = 0; i < kRounds; ++i) {
            turn = 1;
            cv.notify_one();
            cv.wait(lk, [&] { return turn == 0; });
        }
    }
    auto end = std::chrono::steady_clock::now();
    worker.join();
    const long us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    std::println("rounds = {}, total = {} us, per round trip = {} us", kRounds, us,
                 static_cast<double>(us) / kRounds);
}
