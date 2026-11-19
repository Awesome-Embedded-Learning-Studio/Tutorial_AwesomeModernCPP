// jthread 自动请求停止:离开作用域,worker 的循环看得见
#include <chrono>
#include <print>
#include <stop_token>
#include <thread>

using namespace std::chrono_literals;

void worker(std::stop_token token) {
    std::stop_callback cb(
        token, [] { std::println("[callback] running on thread {}", std::this_thread::get_id()); });
    int ticks = 0;
    while (!token.stop_requested()) {
        ++ticks;
        std::this_thread::sleep_for(20ms);
    }
    std::println("[worker] saw the request after {} ticks, thread {}", ticks,
                 std::this_thread::get_id());
}

int main() {
    std::println("[main] thread {}", std::this_thread::get_id());
    {
        std::jthread t(worker);
        std::this_thread::sleep_for(150ms);
        std::println("[main] leaving the scope, no request_stop call");
    }
    std::println("[main] destructor returned");
}
