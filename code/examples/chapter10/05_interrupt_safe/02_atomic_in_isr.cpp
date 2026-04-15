// ISR 中的原子操作 - 演示各种原子模式在中断中的应用
// 本示例展示了完整的、可编译运行的 ISR 安全代码模式

#include <cstdint>
#include <cstdio>
#include <thread>
#include <atomic>
#include <array>
#include <chrono>
#include <vector>

// ============================================================================
// 模式1：数据就绪标志（最常见的模式）
// ============================================================================

class DataReadyFlag {
public:
    DataReadyFlag() noexcept : ready_(false), data_(0) {}

    // ISR 中调用：设置标志和数据
    void set_from_isr(int value) noexcept {
        data_ = value;
        // release 确保数据写入完成后再设置标志
        ready_.store(true, std::memory_order_release);
    }

    // 主线程调用：检查并获取数据
    bool get_from_main_thread(int& out_value) noexcept {
        if (ready_.load(std::memory_order_acquire)) {
            out_value = data_;
            ready_.store(false, std::memory_order_release);
            return true;
        }
        return false;
    }

private:
    std::atomic<bool> ready_;
    int data_;  // 假设 int 的读写是原子的（大多数平台如此）
};

// ============================================================================
// 模式2：原子计数器
// ============================================================================

class InterruptCounter {
public:
    // ISR 中调用：递增计数
    void increment_from_isr() noexcept {
        // relaxed 对计数器足够了
        count_.fetch_add(1, std::memory_order_relaxed);
    }

    // 主线程：获取当前值
    int get() const noexcept {
        return count_.load(std::memory_order_relaxed);
    }

    // 主线程：获取并重置
    int get_and_reset() noexcept {
        return count_.exchange(0, std::memory_order_relaxed);
    }

private:
    std::atomic<int> count_{0};
};

// ============================================================================
// 模式3：多个相关变量的同步
// ============================================================================

struct TimestampedData {
    int value;
    uint32_t timestamp;

    TimestampedData() : value(0), timestamp(0) {}
    TimestampedData(int v, uint32_t t) : value(v), timestamp(t) {}
};

class SynchronizedDataBuffer {
public:
    // ISR 中调用：更新带时间戳的数据
    void update_from_isr(int value, uint32_t timestamp) noexcept {
        value_ = value;
        timestamp_ = timestamp;
        // 发布数据
        ready_.store(true, std::memory_order_release);
    }

    // 主线程：读取数据
    bool try_get_from_main_thread(TimestampedData& out) noexcept {
        if (ready_.load(std::memory_order_acquire)) {
            out.value = value_;
            out.timestamp = timestamp_;
            ready_.store(false, std::memory_order_release);
            return true;
        }
        return false;
    }

private:
    std::atomic<bool> ready_{false};
    int value_;
    uint32_t timestamp_;
};

// ============================================================================
// 模式4：事件标志组
// ============================================================================

class EventFlags {
public:
    enum Flag : uint32_t {
        DataReady     = 1 << 0,
        ErrorOccurred = 1 << 1,
        Timeout       = 1 << 2,
        BufferFull    = 1 << 3,
    };

    // ISR 中调用：设置标志
    void set_from_isr(Flag flag) noexcept {
        // OR 操作是原子的
        flags_.fetch_or(static_cast<uint32_t>(flag), std::memory_order_release);
    }

    // 主线程：检查并清除标志
    bool check_and_clear(Flag flag) noexcept {
        const uint32_t mask = static_cast<uint32_t>(flag);
        const uint32_t current = flags_.load(std::memory_order_acquire);
        if (current & mask) {
            flags_.fetch_and(~mask, std::memory_order_release);
            return true;
        }
        return false;
    }

    // 主线程：等待标志（轮询）
    bool wait_for(Flag flag, int timeout_ms) noexcept {
        auto start = std::chrono::steady_clock::now();
        while (!check_and_clear(flag)) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - start).count();
            if (elapsed >= timeout_ms) {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        return true;
    }

private:
    std::atomic<uint32_t> flags_{0};
};

// ============================================================================
// 模式5：状态机同步
// ============================================================================

enum class DeviceState : uint32_t {
    Idle = 0,
    Busy = 1,
    Error = 2,
    Done = 3
};

class AtomicStateMachine {
public:
    // ISR 中调用：尝试转换状态
    bool transition_from_isr(DeviceState from, DeviceState to) noexcept {
        uint32_t expected = static_cast<uint32_t>(from);
        const uint32_t desired = static_cast<uint32_t>(to);
        // CAS 操作确保状态转换的原子性
        return state_.compare_exchange_strong(expected, desired,
            std::memory_order_acq_rel, std::memory_order_acquire);
    }

    // 主线程：获取当前状态
    DeviceState get_state() const noexcept {
        return static_cast<DeviceState>(state_.load(std::memory_order_acquire));
    }

    // 主线程：等待状态变化
    bool wait_for_state(DeviceState target, int timeout_ms) noexcept {
        auto start = std::chrono::steady_clock::now();
        while (get_state() != target) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - start).count();
            if (elapsed >= timeout_ms) {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return true;
    }

private:
    std::atomic<uint32_t> state_{static_cast<uint32_t>(DeviceState::Idle)};
};

// ============================================================================
// 演示程序
// ============================================================================

void demonstrate_data_ready_flag() {
    printf("\n=== 模式1：数据就绪标志 ===\n");

    DataReadyFlag buffer;
    std::atomic<bool> stop{false};

    // 模拟中断线程
    std::thread isr_thread([&]() {
        for (int i = 0; i < 5 && !stop.load(); ++i) {
            buffer.set_from_isr(i * 100);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });

    // 主线程处理数据
    int received = 0;
    while (received < 5) {
        int value;
        if (buffer.get_from_main_thread(value)) {
            printf("  主线程收到数据: %d\n", value);
            received++;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    stop.store(true);
    isr_thread.join();
}

void demonstrate_atomic_counter() {
    printf("\n=== 模式2：原子计数器 ===\n");

    InterruptCounter counter;

    // 模拟多个中断源
    std::atomic<bool> stop{false};
    std::thread isr1([&]() {
        for (int i = 0; i < 100 && !stop.load(); ++i) {
            counter.increment_from_isr();
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    });
    std::thread isr2([&]() {
        for (int i = 0; i < 150 && !stop.load(); ++i) {
            counter.increment_from_isr();
            std::this_thread::sleep_for(std::chrono::microseconds(150));
        }
    });

    // 主线程定期读取计数
    for (int i = 0; i < 10; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        printf("  计数器值: %d\n", counter.get());
    }

    stop.store(true);
    isr1.join();
    isr2.join();

    printf("  最终计数值: %d\n", counter.get_and_reset());
}

void demonstrate_synchronized_data() {
    printf("\n=== 模式3：多变量同步 ===\n");

    SynchronizedDataBuffer buffer;
    std::atomic<bool> stop{false};

    // 模拟传感器中断
    std::thread sensor_thread([&]() {
        uint32_t timestamp = 0;
        for (int i = 0; i < 5 && !stop.load(); ++i) {
            timestamp += 100;
            buffer.update_from_isr(i * 10 + 25, timestamp);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });

    // 主线程读取数据
    int received = 0;
    while (received < 5) {
        TimestampedData data;
        if (buffer.try_get_from_main_thread(data)) {
            printf("  值=%d, 时间戳=%u\n", data.value, data.timestamp);
            received++;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    stop.store(true);
    sensor_thread.join();
}

void demonstrate_event_flags() {
    printf("\n=== 模式4：事件标志组 ===\n");

    EventFlags events;
    std::atomic<bool> stop{false};

    // 模拟中断源
    std::thread isr_thread([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        events.set_from_isr(EventFlags::DataReady);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        events.set_from_isr(EventFlags::ErrorOccurred);
    });

    // 主线程等待事件
    if (events.wait_for(EventFlags::DataReady, 200)) {
        printf("  收到: DataReady 事件\n");
    }

    if (events.wait_for(EventFlags::ErrorOccurred, 200)) {
        printf("  收到: ErrorOccurred 事件\n");
    }

    stop.store(true);
    isr_thread.join();
}

void demonstrate_state_machine() {
    printf("\n=== 模式5：状态机同步 ===\n");

    AtomicStateMachine device;
    std::atomic<bool> stop{false};

    // 模拟设备操作
    std::thread device_thread([&]() {
        // 启动设备
        if (device.transition_from_isr(DeviceState::Idle, DeviceState::Busy)) {
            printf("  设备: Idle -> Busy\n");
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // 操作完成
        if (device.transition_from_isr(DeviceState::Busy, DeviceState::Done)) {
            printf("  设备: Busy -> Done\n");
        }
    });

    // 主线程等待完成
    if (device.wait_for_state(DeviceState::Done, 200)) {
        printf("  主线程: 设备操作完成\n");
        // 重置状态
        device.transition_from_isr(DeviceState::Done, DeviceState::Idle);
    }

    stop.store(true);
    device_thread.join();
}

// ============================================================================
// 主函数
// ============================================================================

int main() {
    printf("========================================\n");
    printf("  ISR 中的原子操作 - 模式演示\n");
    printf("========================================\n");

    // 检查原子操作是否无锁
    printf("\n平台检查:\n");
    printf("  atomic<bool> lock-free: %s\n",
        std::atomic<bool>{}.is_lock_free() ? "是" : "否");
    printf("  atomic<int> lock-free: %s\n",
        std::atomic<int>{}.is_lock_free() ? "是" : "否");
    printf("  atomic<uint32_t> lock-free: %s\n",
        std::atomic<uint32_t>{}.is_lock_free() ? "是" : "否");

    demonstrate_data_ready_flag();
    demonstrate_atomic_counter();
    demonstrate_synchronized_data();
    demonstrate_event_flags();
    demonstrate_state_machine();

    printf("\n=== 总结 ===\n");
    printf("1. 使用 is_lock_free() 检查原子操作是否真正无锁\n");
    printf("2. ISR 中用 release，主线程用 acquire\n");
    printf("3. 简单计数器可以用 relaxed 内存序\n");
    printf("4. 事件标志组适合多个事件源的场景\n");
    printf("5. 状态机可以用 CAS 操作实现原子转换\n");

    return 0;
}

// 编译命令：
// g++ -std=c++17 -Wall -Wextra -pthread 02_atomic_in_isr.cpp -o 02_atomic_in_isr
