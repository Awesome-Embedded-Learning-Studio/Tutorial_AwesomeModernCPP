// ISR 基础示例 - 演示中断与主线程的数据竞争问题
// 本示例模拟 ISR 执行环境，展示常见的陷阱

#include <cstdint>
#include <cstdio>
#include <thread>
#include <atomic>
#include <vector>

// 模拟硬件寄存器
namespace hal {
    // 模拟 UART 数据寄存器（volatile 确保每次都访问硬件）
    volatile uint32_t UART_DR = 0;
    volatile uint32_t UART_SR = 0;

    constexpr uint32_t UART_SR_RXNE = 0x0020;  // 接收数据非空

    // 模拟中断使能
    inline void enable_uart_irq() { /* 实际硬件操作 */ }
}

// ============================================================================
// 反面教材：有数据竞争的代码
// ============================================================================

namespace bad_example {
    // ❌ 问题：非原子的共享变量
    int shared_counter = 0;
    uint8_t received_byte = 0;
    bool data_ready = false;

    // 模拟 UART 接收中断（ISR）
    void uart_rx_isr() {
        // 检查接收标志
        if (hal::UART_SR & hal::UART_SR_RXNE) {
            received_byte = static_cast<uint8_t>(hal::UART_DR);
            shared_counter++;      // ❌ 数据竞争！
            data_ready = true;     // ❌ 数据竞争！
        }
    }

    // 主循环
    void main_loop() {
        if (data_ready) {          // ❌ 数据竞争！
            printf("Received: 0x%02X, count: %d\n", received_byte, shared_counter);
            data_ready = false;
        }
    }
}

// ============================================================================
// 正确做法：使用原子操作
// ============================================================================

class ISRUARTDriver {
public:
    // 编译期检查：确保原子操作是无锁的
    static_assert(std::atomic<int>::is_always_lock_free,
                  "atomic<int> must be lock-free for ISR use!");

    ISRUARTDriver() noexcept
        : shared_counter(0)
        , received_byte(0)
        , data_ready(false)
    {}

    // 模拟 UART 接收中断（ISR）
    void uart_rx_isr() noexcept {
        if (hal::UART_SR & hal::UART_SR_RXNE) {
            received_byte.store(static_cast<uint8_t>(hal::UART_DR),
                               std::memory_order_relaxed);
            shared_counter.fetch_add(1, std::memory_order_relaxed);
            // release 确保之前的写入都完成
            data_ready.store(true, std::memory_order_release);
        }
    }

    // 主循环
    bool main_loop() {
        // acquire 确保能看到 ISR 中的所有写入
        if (data_ready.load(std::memory_order_acquire)) {
            uint8_t data = received_byte.load(std::memory_order_relaxed);
            int count = shared_counter.load(std::memory_order_relaxed);
            printf("Received: 0x%02X, count: %d\n", data, count);
            data_ready.store(false, std::memory_order_release);
            return true;
        }
        return false;
    }

private:
    std::atomic<int> shared_counter;
    std::atomic<uint8_t> received_byte;
    std::atomic<bool> data_ready;
};

namespace good_example {
    // ✅ 使用原子变量的示例（参考上面的类实现）
}

// ============================================================================
// ISR 禁区演示
// ============================================================================

namespace isr_restriction_demo {
    std::atomic<bool> error_flag{false};

    // ❌ 错误示例：在 ISR 中使用互斥锁
    // class BadISR {
    //     std::mutex mtx;
    //     void isr() {
    //         std::lock_guard<std::mutex> lock(mtx);  // ❌ 可能死锁！
    //     }
    // };

    // ❌ 错误示例：在 ISR 中分配内存
    // void bad_isr() {
    //     int* p = new int;  // ❌ 可能阻塞，可能抛异常
    //     delete p;
    // }

    // ❌ 错误示例：在 ISR 中进行长时间操作
    // void bad_isr() {
    //     for (volatile int i = 0; i < 1000000; ++i) {  // ❌ 阻塞其他中断
    //         // 浪费 CPU 时间
    //     }
    // }

    // ✅ 正确示例：ISR 只做最少的工作
    class GoodISR {
        std::atomic<bool> data_ready{false};
        uint8_t buffer;

    public:
        void isr() noexcept {
            // 只读取数据并设置标志
            buffer = static_cast<uint8_t>(hal::UART_DR);
            data_ready.store(true, std::memory_order_release);
        }

        bool get_data(uint8_t& data) noexcept {
            if (data_ready.load(std::memory_order_acquire)) {
                data = buffer;
                data_ready.store(false, std::memory_order_release);
                return true;
            }
            return false;
        }
    };
}

// ============================================================================
// 主函数：模拟中断环境
// ============================================================================

int main() {
    printf("=== ISR 基础示例 ===\n\n");

    // 演示正确做法
    printf("正确示例：使用原子操作\n");
    printf("------------------------------\n");

    ISRUARTDriver isr_system;

    // 模拟 10 次中断
    for (int i = 0; i < 10; ++i) {
        // 模拟硬件接收到数据
        hal::UART_SR = hal::UART_SR_RXNE;
        hal::UART_DR = 0x40 + i;

        // 模拟中断触发
        isr_system.uart_rx_isr();

        // 清除硬件状态
        hal::UART_SR = 0;
    }

    // 主循环处理数据
    int processed = 0;
    while (processed < 10) {
        if (isr_system.main_loop()) {
            processed++;
        }
    }

    printf("\n=== ISR 设计原则 ===\n");
    printf("1. ISR 中不能使用互斥锁（可能死锁）\n");
    printf("2. ISR 中不能分配内存（可能阻塞/抛异常）\n");
    printf("3. ISR 执行时间要短（影响响应性）\n");
    printf("4. 访问共享数据必须使用原子操作\n");
    printf("5. 用 release/acquire 内存序确保可见性\n");

    return 0;
}

// 编译命令：
// g++ -std=c++17 -Wall -Wextra -pthread 01_isr_basics.cpp -o 01_isr_basics
