// join 的错误路径:join 自己、重复 join,各抛一个 system_error
#include <chrono>
#include <print>
#include <system_error>
#include <thread>

std::thread* g_self = nullptr;

void self_joiner() {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    try {
        g_self->join(); // 线程 join 自己
    } catch (const std::system_error& e) {
        std::println("  self join threw: {}", e.code().message());
    }
}

void work() {}

int main() {
    std::thread t(work);
    t.join();
    try {
        t.join(); // 已经 join 过，joinable() 为假
    } catch (const std::system_error& e) {
        std::println("  second join threw: {}", e.code().message());
    }
    std::thread s(self_joiner);
    g_self = &s;
    s.join();
    std::println("main continues");
}
