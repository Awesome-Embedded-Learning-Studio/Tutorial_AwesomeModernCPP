// join:发起线程的一方等它收工,再继续往下走
#include <chrono>
#include <print>
#include <thread>

void slow_work() {
    std::println("worker  : starting");
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    std::println("worker  : done");
}

int main() {
    std::println("main    : launching a thread");
    std::thread t(slow_work);
    std::println("main    : waiting");
    t.join();
    std::println("main    : thread finished, continuing");
}
