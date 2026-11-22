// 四条线程各走四步,30 毫秒一步:执行流在核间轮转,进度彼此独立
#include <chrono>
#include <print>
#include <thread>
#include <vector>

using namespace std::chrono_literals; // C++14 起的时间字面量（如 30ms），靠这行 using 声明引入

void work(int id) {
    for (int step = 0; step < 4; ++step) {
        std::println("worker {} step {}", id, step);
        std::this_thread::sleep_for(30ms);
    }
}

int main() {
    std::vector<std::thread> threads;
    for (int id = 1; id <= 4; ++id) {
        threads.emplace_back(work, id);
    }
    for (auto& t : threads) {
        t.join();
    }
}
