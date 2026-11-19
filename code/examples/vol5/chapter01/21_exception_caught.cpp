// 异常在线程函数内部接住:main 正常继续
#include <print>
#include <stdexcept>
#include <thread>

void safe_worker() {
    try {
        throw std::runtime_error("oops, something went wrong");
    } catch (const std::exception& e) {
        std::println("worker caught: {}", e.what());
    } catch (...) {
        std::println("worker caught unknown exception");
    }
}

int main() {
    std::thread t(safe_worker);
    t.join();
    std::println("main continues normally");
}
