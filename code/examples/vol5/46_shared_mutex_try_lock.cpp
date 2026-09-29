// shared_mutex 两档配合：两读者同场时独占档 try_lock 失败，读者全撤后才进得去门
#include <atomic>
#include <barrier>
#include <iostream>
#include <mutex>
#include <shared_mutex>
#include <syncstream>
#include <thread>
#include <vector>

int main() {
    // shared_mutex：两读者在场时，独占档拿不进门
    constexpr int n = 2;
    std::shared_mutex m;
    int protected_value = 7;
    std::atomic<int> reading{0};                 // 只做在场读者数的观测
    std::barrier phase(n + 1, []() noexcept {}); // 编排用：两读者加 main

    std::vector<std::jthread> readers;
    for (int id = 0; id != n; ++id) {
        readers.emplace_back([&, id] {
            std::shared_lock lk(m); // 读档：多人可同时在场
            std::osyncstream(std::cout)
                << "读者 " << id << " 进入，在读 " << reading.fetch_add(1) + 1 << '\n';
            phase.arrive_and_wait(); // 第 1 轮：两个读者与 main 都到位
            phase.arrive_and_wait(); // 第 2 轮：等 main 探完独占档再撤
            reading.fetch_sub(1);    // 作用域结束前先撤观测计数
        });                          // 作用域结束：shared_lock 放锁
    }
    phase.arrive_and_wait(); // 此刻两个读者都持着读档
    bool got = m.try_lock();
    if (got) {
        m.unlock();
    }
    std::cout << "两读者在场时独占档 try_lock 拿到 = " << got << "（期望 0）\n";
    phase.arrive_and_wait(); // 放读者们出去，读档全撤
    for (auto& t : readers) {
        t.join();
    }
    bool got2 = m.try_lock(); // 读者全撤：独占档进门
    if (got2) {
        ++protected_value;
        m.unlock();
    }
    std::cout << "读者全撤后独占档 try_lock 拿到 = " << got2
              << "，protected_value = " << protected_value << "（期望 8）\n";
    return 0;
}
