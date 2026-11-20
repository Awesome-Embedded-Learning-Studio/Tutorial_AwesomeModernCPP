// jthread 析构等待:worker 睡满 300ms,析构就等满 300ms
#include <chrono>
#include <print>
#include <thread>

using namespace std::chrono_literals;
using clock_type = std::chrono::steady_clock;
static clock_type::time_point t0;

int main() {
    t0 = clock_type::now();
    {
        std::jthread t([] {
            std::this_thread::sleep_for(300ms);
            std::println("[worker] finished its own work, nobody asked it to");
        });
        std::this_thread::sleep_for(50ms);
        std::println(
            "[main] leaving the scope at {} ms",
            std::chrono::duration_cast<std::chrono::milliseconds>(clock_type::now() - t0).count());
    }
    std::println(
        "[main] destructor returned at {} ms",
        std::chrono::duration_cast<std::chrono::milliseconds>(clock_type::now() - t0).count());
}
