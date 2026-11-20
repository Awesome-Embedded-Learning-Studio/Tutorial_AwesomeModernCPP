// 交错输出:三条线程同时醒来各自连打六个标记,输出顺序每次都不同
#include <chrono>
#include <print>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

void chatter(int id) {
    std::this_thread::sleep_for(50ms); // 让三个线程几乎同时醒来
    for (int step = 0; step < 6; ++step) {
        std::print("[w{}:{}]", id, step);
    }
    std::println("");
}

int main() {
    std::vector<std::thread> threads;
    for (int id = 1; id <= 3; ++id) {
        threads.emplace_back(chatter, id);
    }
    for (auto& t : threads) {
        t.join();
    }
}
