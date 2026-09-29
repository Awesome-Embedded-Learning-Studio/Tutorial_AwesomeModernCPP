// 块级 static 的并发初始化：Heavy 只构造一次，四个线程全读到 42
#include <atomic>
#include <iostream>
#include <thread>
#include <vector>

std::atomic<int> construction_count{0}; // 只做构造次数的观测

struct Heavy {
    Heavy() {
        std::cout << "Heavy 构造（只应出现一次）\n";
        ++construction_count;
    }
    static Heavy& instance() {
        static Heavy h; // C++11 起：并发进入只有一个线程执行初始化
        return h;
    }
    int value = 42;
};

int main() {
    std::vector<std::jthread> readers;
    for (int id = 0; id != 4; ++id) {
        readers.emplace_back([] {
            Heavy& h = Heavy::instance();
            std::cout << "线程读到 value = " << h.value << '\n';
        });
    }
    for (auto& t : readers) {
        t.join();
    }
    std::cout << "构造次数 = " << construction_count << "（期望 1）\n";
    return 0;
}
