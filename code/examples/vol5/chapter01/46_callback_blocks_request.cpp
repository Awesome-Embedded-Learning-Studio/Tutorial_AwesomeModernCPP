// 回调里的睡眠会阻塞 request_stop:200ms 的回调,请求方就得等 200ms
#include <chrono>
#include <print>
#include <stop_token>
#include <thread>

using namespace std::chrono_literals;
using clock_type = std::chrono::steady_clock;
static clock_type::time_point t0;

long long ms_since_start() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(clock_type::now() - t0).count();
}

int main() {
    t0 = clock_type::now();
    std::stop_source source;
    std::thread worker([&, token = source.get_token()] {
        {
            std::stop_callback cb(token, [] {
                std::println("  [callback] starts at {} ms", ms_since_start());
                std::this_thread::sleep_for(200ms);
                std::println("  [callback] ends at {} ms", ms_since_start());
            });
            while (!token.stop_requested()) {
                std::this_thread::sleep_for(5ms);
            }
            std::println("[worker] leaving the inner scope at {} ms", ms_since_start());
        }
        std::println("[worker] past the destructor at {} ms", ms_since_start());
    });
    std::this_thread::sleep_for(20ms);
    std::println("[main] request_stop at {} ms", ms_since_start());
    source.request_stop();
    std::println("[main] request_stop returned at {} ms", ms_since_start());
    worker.join();
}
