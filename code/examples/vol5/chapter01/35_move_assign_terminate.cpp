// move 赋值给已关联线程的对象:std::terminate 直接收场,本示例预期崩溃
#include <chrono>
#include <print>
#include <thread>

void slow_work() {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
}

int main() {
    std::println("starting two threads, then move-assign over a joinable one");
    std::thread t1(slow_work);
    std::thread t2(slow_work);
    t2 = std::move(t1); // t2 还关联着线程
}
