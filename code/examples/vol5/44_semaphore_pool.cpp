// counting_semaphore 资源池限流：2 个座位 4 个用户，同时在库峰值压在 2
#include <atomic>
#include <chrono>
#include <iostream>
#include <semaphore>
#include <syncstream>
#include <thread>
#include <vector>

int main() {
    // semaphore：2 个座位，4 个用户，限流由计数本身承担
    constexpr int permits = 2;
    constexpr int users = 4;
    std::counting_semaphore<permits> seats{permits};
    std::atomic<int> in_use{0}; // 只做在场人数的观测，原子的正式规则归 第 3 章
    std::atomic<int> peak{0};

    std::vector<std::jthread> holders;
    for (int id = 0; id != users; ++id) {
        holders.emplace_back([id, &seats, &in_use, &peak] {
            seats.acquire(); // 没空位就阻塞，不烧 CPU
            int now = in_use.fetch_add(1) + 1;
            int seen = peak.load();
            while (now > seen && !peak.compare_exchange_weak(seen, now)) {
            }
            std::osyncstream(std::cout) << "用户 " << id << " 入座，在场 " << now << '\n';
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            in_use.fetch_sub(1);
            seats.release(); // 离开设卡：空位 +1，等的人被唤醒
            std::osyncstream(std::cout) << "用户 " << id << " 离开\n";
        });
    }
    for (auto& t : holders) {
        t.join();
    }
    std::cout << "同时在库峰值 = " << peak << "（上限 " << permits << "）\n";
    return 0;
}
