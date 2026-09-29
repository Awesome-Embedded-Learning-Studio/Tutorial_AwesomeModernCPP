// latch 一次性倒计数：归零前 try_wait = 0，归零后 = 1，main 不写一行轮询
#include <iostream>
#include <latch>
#include <thread>
#include <vector>

int main() {
    // latch：main 等四个工人全到齐
    constexpr int n = 4;
    std::latch ready(n);
    std::vector<int> data(n, -1);
    std::cout << "开工前 try_wait = " << ready.try_wait() << "（计数未归零）\n";
    std::vector<std::jthread> workers;
    for (int i = 0; i != n; ++i) {
        workers.emplace_back([i, &ready, &data] {
            data[i] = i * i;    // 各自的准备活
            ready.count_down(); // 报到：计数减一，不等待
        });
    }
    ready.wait(); // main 在这里等计数归零
    std::cout << "归零后 try_wait = " << ready.try_wait() << "，data = [";
    for (int i = 0; i != n; ++i) {
        std::cout << data[i] << (i + 1 == n ? "]\n" : " ");
    }
    return 0;
}
