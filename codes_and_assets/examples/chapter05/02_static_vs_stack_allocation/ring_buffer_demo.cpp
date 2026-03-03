#include <iostream>
#include <cstdint>
#include <array>

// 环形缓冲区实现 - 静态分配的经典应用

template<typename T, size_t N>
class RingBuffer {
    static_assert(N > 0 && (N & (N - 1)) == 0,
                  "Size must be power of 2 for efficient masking");

    std::array<T, N> buffer_;
    size_t head_ = 0;
    size_t tail_ = 0;
    size_t mask_ = N - 1;

public:
    RingBuffer() = default;

    // 非阻塞push
    bool push(const T& value) {
        size_t next = (head_ + 1) & mask_;
        if (next == tail_) {
            return false;  // Full
        }
        buffer_[head_] = value;
        head_ = next;
        return true;
    }

    // 非阻塞pop
    bool pop(T& out) {
        if (head_ == tail_) {
            return false;  // Empty
        }
        out = buffer_[tail_];
        tail_ = (tail_ + 1) & mask_;
        return true;
    }

    bool empty() const { return head_ == tail_; }
    bool full() const { return ((head_ + 1) & mask_) == tail_; }
    size_t size() const { return (head_ - tail_) & mask_; }
    size_t capacity() const { return N - 1; }

    void clear() { head_ = tail_ = 0; }
};

// 字节型环形缓冲区
template<size_t N>
class ByteRingBuffer {
    std::array<uint8_t, N> buffer_;
    size_t head_ = 0;
    size_t tail_ = 0;

public:
    // 写入数据
    size_t write(const uint8_t* data, size_t len) {
        size_t written = 0;
        for (size_t i = 0; i < len; ++i) {
            size_t next = (head_ + 1) % N;
            if (next == tail_) break;  // Full
            buffer_[head_] = data[i];
            head_ = next;
            ++written;
        }
        return written;
    }

    // 读取数据
    size_t read(uint8_t* data, size_t len) {
        size_t read_count = 0;
        for (size_t i = 0; i < len; ++i) {
            if (head_ == tail_) break;  // Empty
            data[i] = buffer_[tail_];
            tail_ = (tail_ + 1) % N;
            ++read_count;
        }
        return read_count;
    }

    size_t size() const {
        if (head_ >= tail_) return head_ - tail_;
        return N - tail_ + head_;
    }

    size_t available() const { return N - size() - 1; }
    bool empty() const { return head_ == tail_; }
    bool full() const { return ((head_ + 1) % N) == tail_; }
};

void ring_buffer_demo() {
    std::cout << "=== Ring Buffer Demo ===\n\n";

    RingBuffer<int, 8> rb;

    std::cout << "--- Initial State ---\n";
    std::cout << "Empty: " << rb.empty() << "\n";
    std::cout << "Full: " << rb.full() << "\n";

    std::cout << "\n--- Pushing 7 elements ---\n";
    for (int i = 0; i < 7; ++i) {
        bool ok = rb.push(i);
        std::cout << "Push " << i << ": " << (ok ? "success" : "failed")
                  << ", size: " << rb.size() << "\n";
    }

    std::cout << "\n--- Popping 3 elements ---\n";
    for (int i = 0; i < 3; ++i) {
        int val;
        bool ok = rb.pop(val);
        std::cout << "Pop: " << (ok ? std::to_string(val) : "failed")
                  << ", size: " << rb.size() << "\n";
    }

    std::cout << "\n--- Pushing 2 more elements ---\n";
    for (int i = 7; i < 9; ++i) {
        bool ok = rb.push(i);
        std::cout << "Push " << i << ": " << (ok ? "success" : "failed")
                  << ", size: " << rb.size() << "\n";
    }

    std::cout << "\n--- Try to overflow ---\n";
    bool overflow = rb.push(999);
    std::cout << "Push 999: " << (overflow ? "unexpected success" : "correctly rejected") << "\n";
}

void byte_ring_buffer_demo() {
    std::cout << "\n=== Byte Ring Buffer Demo ===\n\n";

    ByteRingBuffer<32> buf;

    const char* msg1 = "Hello, ";
    const char* msg2 = "World!";

    size_t written = buf.write(reinterpret_cast<const uint8_t*>(msg1), 7);
    std::cout << "Written: " << written << " bytes\n";
    std::cout << "Buffer size: " << buf.size() << "\n";

    written = buf.write(reinterpret_cast<const uint8_t*>(msg2), 6);
    std::cout << "Written: " << written << " bytes\n";
    std::cout << "Buffer size: " << buf.size() << "\n";

    std::cout << "\n--- Reading back ---\n";
    uint8_t read_buf[32];
    size_t read_count = buf.read(read_buf, sizeof(read_buf));
    std::cout << "Read: " << read_count << " bytes\n";
    std::cout << "Content: ";
    for (size_t i = 0; i < read_count; ++i) {
        std::cout << static_cast<char>(read_buf[i]);
    }
    std::cout << "\n";
}

// UART缓冲区示例
class UARTRxBuffer {
    static constexpr size_t BUFFER_SIZE = 256;
    ByteRingBuffer<BUFFER_SIZE> buffer_;

public:
    // 模拟ISR中调用
    void isr_receive_byte(uint8_t byte) {
        buffer_.write(&byte, 1);
    }

    // 主循环中调用
    size_t read(uint8_t* data, size_t len) {
        return buffer_.read(data, len);
    }

    size_t available() const { return buffer_.size(); }
};

void uart_buffer_demo() {
    std::cout << "\n=== UART Buffer Example ===\n\n";

    UARTRxBuffer uart;

    // 模拟接收数据
    for (uint8_t c = 'A'; c <= 'Z'; ++c) {
        uart.isr_receive_byte(c);
    }

    std::cout << "Received: " << uart.available() << " bytes\n";

    // 读取数据
    uint8_t data[32];
    size_t read = uart.read(data, sizeof(data));
    std::cout << "Read: " << read << " bytes\n";
    std::cout << "Content: ";
    for (size_t i = 0; i < read; ++i) {
        std::cout << static_cast<char>(data[i]);
    }
    std::cout << "\n";
}

int main() {
    ring_buffer_demo();
    byte_ring_buffer_demo();
    uart_buffer_demo();

    std::cout << "\n=== Key Takeaways ===\n";
    std::cout << "1. Ring buffer is a fixed-size static allocation pattern\n";
    std::cout << "2. Perfect for ISR-to-main communication\n";
    std::cout << "3. O(1) push/pop with power-of-2 size\n";
    std::cout << "4. No fragmentation, deterministic timing\n";

    return 0;
}
