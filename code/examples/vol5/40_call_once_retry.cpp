// call_once 的重试语义：异常传出不算成功，第 3 轮才落地，后到线程直接通过
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

// call_once 的重试语义：异常不算成功，下一个进来的接着试
int init_attempts = 0;
std::once_flag config_once;

void flaky_init() {
    ++init_attempts;
    std::cout << "初始化第 " << init_attempts << " 次尝试\n";
    if (init_attempts < 3) {
        throw std::runtime_error("依赖还没就绪");
    }
    std::cout << "初始化成功\n";
}

int main() {
    for (int round = 0; round != 3; ++round) {
        try {
            std::call_once(config_once, flaky_init);
            std::cout << "第 " << round + 1 << " 轮：call_once 顺利返回\n";
        } catch (const std::exception& e) {
            std::cout << "第 " << round + 1 << " 轮：异常传出 call_once（" << e.what() << "）\n";
        }
    }
    std::vector<std::jthread> latecomers; // 后到的线程：直接通过
    for (int id = 0; id != 4; ++id) {
        latecomers.emplace_back([] { std::call_once(config_once, flaky_init); });
    }
    std::cout << "后到线程全部通过，总尝试次数 = " << init_attempts << "（期望 3）\n";
    return 0;
}
