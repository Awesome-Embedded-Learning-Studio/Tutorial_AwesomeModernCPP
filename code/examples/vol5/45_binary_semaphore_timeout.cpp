// binary_semaphore 跨线程信号：release/acquire 两端不同线程，try_acquire_for(50ms) 空等超时
#include <chrono>
#include <iostream>
#include <semaphore>
#include <thread>

int main() {
    // binary_semaphore 初值 0：release 的发信号，acquire 的收信号
    std::binary_semaphore signal{0};
    std::jthread producer([&signal] {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        signal.release(); // 发信号的是这个线程
    });
    signal.acquire(); // 收信号的是 main：跨线程合法
    std::cout << "main 收到了信号\n";
    auto start = std::chrono::steady_clock::now();
    bool got = signal.try_acquire_for(std::chrono::milliseconds(50));
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now() - start)
                          .count();
    std::cout << "try_acquire_for(50ms) 拿到 = " << got << "，实际等了 " << elapsed_ms << "ms\n";
    return 0;
}
