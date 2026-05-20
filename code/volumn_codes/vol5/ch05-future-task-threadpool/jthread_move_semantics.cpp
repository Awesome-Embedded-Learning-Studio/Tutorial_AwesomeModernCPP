/*
 * 验证：moved-from jthread 析构时是否会调用 request_stop()
 *
 * 背景：文章原稿声称 vector 扩容时移动元素导致旧 jthread 析构，
 *       会触发自带的 request_stop()。需要验证 moved-from jthread
 *       的析构行为。
 *
 * 预期结果：moved-from jthread 的 joinable()==false，
 *           析构时不调用 request_stop()，不影响其他线程。
 *           只有显式调用 request_stop() 时 stop_callback 才被触发。
 *
 * 编译命令：
 *   g++ -std=c++20 -O2 -pthread -o /tmp/jthread_move_test jthread_move_semantics.cpp
 *
 * 运行：
 *   /tmp/jthread_move_test
 *
 * 参考资料：
 *   - https://en.cppreference.com/cpp/thread/jthread
 *   - jthread 析构函数：若 joinable() 为 true，调用 request_stop() 然后 join()
 *   - jthread 移动后：joinable() 为 false
 *
 * 编译器：GCC 12+
 */

#include <atomic>
#include <chrono>
#include <iostream>
#include <stop_token>
#include <thread>

std::atomic<bool> g_stop_callback_called{false};

int main() {
    std::jthread outer; // 默认构造，joinable()==false

    {
        std::jthread inner([&](std::stop_token st) {
            std::stop_callback cb(st, [&] {
                g_stop_callback_called = true;
                std::cout << "[stop_callback] request_stop() 被调用了！\n";
            });
            while (!st.stop_requested()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            std::cout << "[worker] 收到 stop 请求，退出\n";
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        std::cout << "inner joinable: " << inner.joinable() << "\n";

        // 模拟 vector 扩容：将 inner move 到 outer
        outer = std::move(inner);

        std::cout << "move 后 inner joinable: " << inner.joinable() << "\n";
        std::cout << "move 后 outer joinable: " << outer.joinable() << "\n";

        // inner 在此作用域结束时析构（moved-from 状态）
        std::cout << "inner 即将析构（moved-from）...\n";
    }

    std::cout << "inner 已析构，stop_callback_called = " << g_stop_callback_called.load() << "\n";

    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    std::cout << "300ms 后，stop_callback_called = " << g_stop_callback_called.load() << "\n";

    if (!g_stop_callback_called.load()) {
        std::cout << "结论：moved-from jthread 析构不会触发 request_stop()\n";
    }

    // 现在正常请求停止
    outer.request_stop();
    outer.join();
    std::cout << "outer 已 join，stop_callback_called = " << g_stop_callback_called.load() << "\n";

    return 0;
}
