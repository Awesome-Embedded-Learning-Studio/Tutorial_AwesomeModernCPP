// jthread:不写 join,析构自动等
#include <chrono>
#include <print>
#include <thread>

using namespace std::chrono_literals;

void worker() {
    std::this_thread::sleep_for(100ms);
    std::println("worker done");
}

int main() {
    std::jthread t(worker);
    // 不写 join，析构时自动等
}
