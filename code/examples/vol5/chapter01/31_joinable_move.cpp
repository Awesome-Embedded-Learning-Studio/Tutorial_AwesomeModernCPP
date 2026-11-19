// joinable 与 move:转移之后,源线程不再关联执行流
#include <print>
#include <thread>

void work() {}

int main() {
    std::thread t1(work);
    std::println("t1 joinable: {}", t1.joinable());
    std::thread t2 = std::move(t1);
    std::println("after move: t1 joinable {}, t2 joinable {}", t1.joinable(), t2.joinable());
    t2.join();
}
