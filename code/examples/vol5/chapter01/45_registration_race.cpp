// stop_callback 的注册竞态:请求先到,注册时就地执行;在线沙箱限线程数,收窄到 50 轮
// 正文读数来自本地 2000 轮实测;线程创建失败(配额)的轮次跳过
#include <atomic>
#include <chrono>
#include <print>
#include <stop_token>
#include <thread>

using namespace std::chrono_literals;

int main() {
    constexpr int kRounds = 50;
    std::atomic<int> racy{0};
    std::atomic<int> alive{0};
    std::atomic<int> gone{0};

    // A：注册与请求互抢，回调对象活到工作函数返回前
    int a_rounds = 0;
    for (int i = 0; i < kRounds; ++i) {
        std::stop_source source;
        try {
            std::thread worker([&, i] {
                std::this_thread::sleep_for(std::chrono::microseconds(i % 30));
                std::stop_callback cb(source.get_token(), [&] { racy.fetch_add(1); });
                std::this_thread::yield();
            });
            ++a_rounds;
            source.request_stop();
            worker.join();
        } catch (const std::system_error&) {
            // 在线沙箱线程配额有限,跳过这一轮
        }
    }

    // B：回调对象活到请求之后
    for (int i = 0; i < kRounds; ++i) {
        std::stop_source source;
        std::stop_callback cb(source.get_token(), [&] { alive.fetch_add(1); });
        source.request_stop();
    }

    // C：请求晚于回调注销
    for (int i = 0; i < kRounds; ++i) {
        std::stop_source source;
        {
            std::stop_callback cb(source.get_token(), [&] { gone.fetch_add(1); });
        }
        source.request_stop();
    }

    std::println("A: {} rounds, {} invocations", a_rounds, racy.load());
    std::println("B: live callback at request -> {}/{}", alive.load(), kRounds);
    std::println("C: request after deregistration -> {}/{}", gone.load(), kRounds);
}
