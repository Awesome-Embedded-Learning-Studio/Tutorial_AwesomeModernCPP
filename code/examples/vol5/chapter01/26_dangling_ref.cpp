// 悬垂引用:按引用捕获活不过本函数的局部变量,线程读到的是旧栈上的垃圾
#include <chrono>
#include <cstring>
#include <print>
#include <thread>

void burn_stack() { // 往栈上灌别的数据，看能不能把旧值覆盖出来
    volatile char area[16384];
    std::memset(const_cast<char*>(area), '#', sizeof(area));
}

void spawn() {
    int local_value = 42;
    std::thread t([&local_value] { // 按引用捕获一个活不过本函数的局部变量
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        std::println("thread reads: {}", local_value);
    });
    t.detach();
}

int main() {
    spawn();
    burn_stack();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    std::println("main exits");
}
