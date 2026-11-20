// 异常逃出线程函数:main 的 try 接不住,std::terminate 收走整个进程
#include <print>
#include <stdexcept>
#include <thread>

void unsafe_worker() {
    throw std::runtime_error("oops, something went wrong");
}

int main() {
    try {
        std::thread t(unsafe_worker);
        t.join();
    } catch (const std::exception& e) {
        std::println("caught: {}", e.what());
    }
}
