// 一个 stop_source 停一组线程:一次 request_stop,三条 processor 各自看见
#include <chrono>
#include <print>
#include <stop_token>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

void processor(std::stop_token token, int id) {
    int rounds = 0;
    while (!token.stop_requested()) {
        ++rounds;
        std::this_thread::sleep_for(50ms);
    }
    std::println("processor {} stopped after {} rounds", id, rounds);
}

int main() {
    std::stop_source source;
    std::vector<std::thread> pool;
    for (int id = 1; id <= 3; ++id) {
        pool.emplace_back(processor, source.get_token(), id);
    }
    std::this_thread::sleep_for(150ms);
    std::println("one request_stop() call");
    source.request_stop();
    for (auto& t : pool)
        t.join();
    std::println("all joined");
}
