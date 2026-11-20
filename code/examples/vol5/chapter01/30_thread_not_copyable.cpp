// std::thread 不可拷贝:拷贝构造被删除,编译不过
// 错误写法留在注释里,放开后报错见正文;所有权只能 move
#include <chrono>
#include <print>
#include <thread>

void work() {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

int main() {
    std::thread a(work);
    // std::thread b = a;  // 编译不过
    std::thread b = std::move(a); // 所有权转移
    b.join();
    std::println("a joinable after move: {}", a.joinable());
}
