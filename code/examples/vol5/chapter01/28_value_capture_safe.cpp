// 值捕获修复悬垂:线程拿自己的副本,父函数先走也不怕
#include <chrono>
#include <print>
#include <string>
#include <thread>

void safe_version() {
    std::string message = "Hello from parent";
    std::thread t([message] { // 值捕获：线程拿自己的副本
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::println("thread sees: {}", message);
    });
    t.detach();
}

int main() {
    safe_version();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    std::println("main exits");
}
