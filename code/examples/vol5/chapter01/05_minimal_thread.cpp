// 最小的 std::thread 程序:构造即启动,join 等待
#include <print>
#include <thread>

void work() {
    std::println("working");
}

int main() {
    std::thread t(work); // 这一行执行完，系统里多了一条执行流
    t.join();
}
