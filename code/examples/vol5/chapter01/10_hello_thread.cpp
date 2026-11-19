// 三种构造方式之一:函数指针
#include <print>
#include <thread>

void print_hello(int id) {
    std::println("hello from thread {}", id);
}

int main() {
    std::thread t(print_hello, 42);
    t.join();
}
