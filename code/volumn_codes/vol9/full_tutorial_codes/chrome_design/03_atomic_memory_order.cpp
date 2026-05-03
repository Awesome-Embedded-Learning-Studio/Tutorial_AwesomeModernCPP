// std::atomic 与 memory_order
// 来源：OnceCallback 前置知识速查 (pre-00)
// 编译：g++ -std=c++17 -Wall -Wextra 03_atomic_memory_order.cpp -o 03_atomic_memory_order -pthread

#include <atomic>
#include <iostream>
#include <thread>

int main() {
    std::cout << "=== std::atomic 基本操作 ===\n";
    {
        std::atomic<bool> flag{true};
        std::cout << "  initial: " << flag.load() << "\n";

        flag.store(false, std::memory_order_release);
        std::cout << "  after store: " << flag.load(std::memory_order_acquire) << "\n";
    }

    std::cout << "\n=== 线程间 acquire/release 同步 ===\n";
    {
        std::atomic<bool> ready{false};
        int data = 0;

        // 线程 A：写入数据，然后设 ready = true
        std::thread producer([&]() {
            data = 42;
            ready.store(true, std::memory_order_release);
        });

        // 线程 B：等待 ready，然后读取数据
        std::thread consumer([&]() {
            while (!ready.load(std::memory_order_acquire)) {
                // 自旋等待
            }
            std::cout << "  consumer sees data = " << data << "\n";
        });

        producer.join();
        consumer.join();
    }

    std::cout << "\n=== enum class ===\n";
    {
        enum class Status : unsigned char {
            kEmpty,
            kValid,
            kConsumed
        };
        Status s = Status::kValid;
        // int y = s;  // 编译错误：不可隐式转换
        std::cout << "  Status value (cast) = " << static_cast<int>(s) << "\n";
    }

    return 0;
}
