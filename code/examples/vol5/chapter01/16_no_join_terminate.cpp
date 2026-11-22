// 既不 join 也不 detach:线程对象析构时 std::terminate 直接收走整个进程
#include <print>
#include <thread>

void some_work() {
    std::println("working...");
}

int main() {
    std::thread t(some_work);
    // 既不 join 也不 detach
}
