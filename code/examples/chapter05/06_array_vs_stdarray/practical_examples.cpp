#include <iostream>
#include <array>
#include <algorithm>
#include <cstring>
#include <cstdint>

// std::array 在实际嵌入式场景中的应用

// 示例1：状态机状态表
enum class State {
    Idle,
    Running,
    Paused,
    Error
};

enum class Event {
    Start,
    Stop,
    Pause,
    Resume,
    Error
};

struct StateTransition {
    State current;
    Event event;
    State next;
};

constexpr std::array<StateTransition, 8> state_table = {{
    {State::Idle, Event::Start, State::Running},
    {State::Running, Event::Stop, State::Idle},
    {State::Running, Event::Pause, State::Paused},
    {State::Paused, Event::Resume, State::Running},
    {State::Paused, Event::Stop, State::Idle},
    {State::Error, Event::Stop, State::Idle},
    // 可以添加更多转换...
}};

State get_next_state(State current, Event event) {
    auto it = std::find_if(state_table.begin(), state_table.end(),
        [current, event](const StateTransition& t) {
            return t.current == current && t.event == event;
        });
    return (it != state_table.end()) ? it->next : State::Error;
}

void state_machine_demo() {
    std::cout << "=== State Machine with std::array ===\n\n";

    State state = State::Idle;

    auto print_state = [](State s) {
        switch (s) {
            case State::Idle:    std::cout << "Idle"; break;
            case State::Running: std::cout << "Running"; break;
            case State::Paused:  std::cout << "Paused"; break;
            case State::Error:   std::cout << "Error"; break;
        }
    };

    std::array<Event, 6> events = {
        Event::Start, Event::Pause, Event::Resume,
        Event::Stop, Event::Start, Event::Error
    };

    for (Event e : events) {
        print_state(state);
        std::cout << " + ";
        switch (e) {
            case Event::Start:  std::cout << "Start"; break;
            case Event::Stop:   std::cout << "Stop"; break;
            case Event::Pause:  std::cout << "Pause"; break;
            case Event::Resume: std::cout << "Resume"; break;
            case Event::Error:  std::cout << "Error"; break;
        }
        std::cout << " -> ";
        state = get_next_state(state, e);
        print_state(state);
        std::cout << "\n";
    }
}

// 示例2：环形缓冲区
template<typename T, size_t N>
class RingBuffer {
    std::array<T, N> buffer_;
    size_t head_ = 0;
    size_t tail_ = 0;

    // 要求N是2的幂
    static_assert((N & (N - 1)) == 0, "Size must be power of 2");
    static constexpr size_t mask_ = N - 1;

public:
    bool push(const T& value) {
        size_t next = (head_ + 1) & mask_;
        if (next == tail_) {
            return false;  // Full
        }
        buffer_[head_] = value;
        head_ = next;
        return true;
    }

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

    const std::array<T, N>& data() const { return buffer_; }
};

void ring_buffer_demo() {
    std::cout << "\n=== Ring Buffer with std::array ===\n\n";

    RingBuffer<int, 8> rb;

    // Push values
    for (int i = 0; i < 7; ++i) {
        bool ok = rb.push(i);
        std::cout << "Push " << i << ": " << (ok ? "success" : "failed") << "\n";
    }

    std::cout << "Buffer size: " << rb.size() << "\n";
    std::cout << "Buffer full: " << (rb.full() ? "yes" : "no") << "\n";

    // Pop values
    std::cout << "\nPopping values:\n";
    int val;
    while (rb.pop(val)) {
        std::cout << "  " << val << "\n";
    }

    std::cout << "Buffer empty: " << (rb.empty() ? "yes" : "no") << "\n";
}

// 示例3：硬件寄存器映射（演示用）
class RegisterMap {
    std::array<uint32_t, 16> registers_;

public:
    RegisterMap() { registers_.fill(0); }

    // 读写寄存器
    uint32_t read(size_t index) const {
        if (index < registers_.size()) {
            return registers_[index];
        }
        return 0;
    }

    void write(size_t index, uint32_t value) {
        if (index < registers_.size()) {
            registers_[index] = value;
        }
    }

    // 位操作
    void set_bit(size_t index, size_t bit) {
        if (index < registers_.size() && bit < 32) {
            registers_[index] |= (1U << bit);
        }
    }

    void clear_bit(size_t index, size_t bit) {
        if (index < registers_.size() && bit < 32) {
            registers_[index] &= ~(1U << bit);
        }
    }

    // 导出原始数据（用于DMA等）
    uint32_t* data() { return registers_.data(); }
    const uint32_t* data() const { return registers_.data(); }
};

void register_map_demo() {
    std::cout << "\n=== Register Map with std::array ===\n\n";

    RegisterMap regs;

    // 写入一些寄存器
    regs.write(0, 0x12345678);
    regs.write(1, 0xABCDEF00);
    regs.set_bit(2, 5);
    regs.set_bit(2, 10);

    // 读取并显示
    for (size_t i = 0; i < 4; ++i) {
        std::cout << "Reg[" << i << "] = 0x" << std::hex << regs.read(i) << std::dec << "\n";
    }

    // 与C API互操作
    std::array<uint32_t, 16> copy;
    std::memcpy(copy.data(), regs.data(), sizeof(uint32_t) * 16);

    std::cout << "\nCopied via memcpy, first value: 0x" << std::hex << copy[0] << std::dec << "\n";
}

// 示例4：查表
template<typename T, size_t TABLE_SIZE>
class LookupTable {
    std::array<T, TABLE_SIZE> table_;

public:
    explicit LookupTable(const std::array<T, TABLE_SIZE>& table) : table_(table) {}

    // 线性插值查表
    T lookup(float index) const {
        if (index <= 0) return table_[0];
        if (index >= TABLE_SIZE - 1) return table_[TABLE_SIZE - 1];

        size_t i = static_cast<size_t>(index);
        float frac = index - i;

        return table_[i] + frac * (table_[i + 1] - table_[i]);
    }

    // 直接查表
    const T& operator[](size_t index) const {
        return table_[index];
    }

    size_t size() const { return TABLE_SIZE; }
};

void lookup_table_demo() {
    std::cout << "\n=== Lookup Table with std::array ===\n\n";

    // 正弦表（简化版）
    constexpr std::array<float, 16> sin_table = [] {
        std::array<float, 16> table{};
        for (size_t i = 0; i < 16; ++i) {
            float angle = i * 3.14159f / 8.0f;  // 0 to 2pi
            table[i] = std::sin(angle);
        }
        return table;
    }();

    LookupTable<float, 16> sin_lut(sin_table);

    std::cout << "Sine lookup:\n";
    for (float f = 0.0f; f < 3.2f; f += 0.5f) {
        float sin_val = sin_lut.lookup(f * 5.0f);  // Scale to table index
        std::cout << "  sin(" << f << ") ≈ " << sin_val << "\n";
    }
}

// 示例5：固定大小字符串缓冲区
template<size_t N>
class StaticString {
    std::array<char, N> buffer_;

public:
    StaticString() {
        buffer_[0] = '\0';
    }

    StaticString(const char* str) {
        set(str);
    }

    void set(const char* str) {
        if (str) {
            std::strncpy(buffer_.data(), str, N - 1);
            buffer_[N - 1] = '\0';
        } else {
            buffer_[0] = '\0';
        }
    }

    const char* c_str() const {
        return buffer_.data();
    }

    size_t length() const {
        return std::strlen(buffer_.data());
    }

    bool empty() const {
        return buffer_[0] == '\0';
    }

    // 格式化（简化版）
    int printf(const char* format, ...) {
        va_list args;
        va_start(args, format);
        int result = std::vsnprintf(buffer_.data(), N, format, args);
        va_end(args);
        return result;
    }
};

void static_string_demo() {
    std::cout << "\n=== Static String with std::array ===\n\n";

    StaticString<32> str1("Hello");
    StaticString<64> str2;

    std::cout << "str1: \"" << str1.c_str() << "\" (len=" << str1.length() << ")\n";

    str2.set("World");
    std::cout << "str2: \"" << str2.c_str() << "\"\n";

    str2.printf("Value: %d, Hex: 0x%X", 42, 0xABCD);
    std::cout << "Formatted: \"" << str2.c_str() << "\"\n";
}

// 示例6：传感器数据缓冲
struct SensorData {
    uint32_t timestamp;
    float value;
    uint8_t status;
};

class SensorBuffer {
    std::array<SensorData, 100> buffer_;
    size_t count_ = 0;

public:
    bool add(const SensorData& data) {
        if (count_ >= buffer_.size()) {
            return false;
        }
        buffer_[count_++] = data;
        return true;
    }

    const SensorData* get_latest() const {
        if (count_ == 0) return nullptr;
        return &buffer_[count_ - 1];
    }

    const SensorData* get(size_t index) const {
        if (index >= count_) return nullptr;
        return &buffer_[index];
    }

    size_t count() const { return count_; }

    void clear() { count_ = 0; }

    // 统计
    float average() const {
        if (count_ == 0) return 0.0f;
        float sum = 0.0f;
        for (size_t i = 0; i < count_; ++i) {
            sum += buffer_[i].value;
        }
        return sum / count_;
    }

    float min() const {
        if (count_ == 0) return 0.0f;
        float m = buffer_[0].value;
        for (size_t i = 1; i < count_; ++i) {
            if (buffer_[i].value < m) m = buffer_[i].value;
        }
        return m;
    }

    float max() const {
        if (count_ == 0) return 0.0f;
        float m = buffer_[0].value;
        for (size_t i = 1; i < count_; ++i) {
            if (buffer_[i].value > m) m = buffer_[i].value;
        }
        return m;
    }
};

void sensor_buffer_demo() {
    std::cout << "\n=== Sensor Buffer with std::array ===\n\n";

    SensorBuffer buffer;

    // 添加一些传感器数据
    for (int i = 0; i < 10; ++i) {
        SensorData data;
        data.timestamp = i * 100;
        data.value = 20.0f + i * 0.5f;
        data.status = 0;
        buffer.add(data);
    }

    std::cout << "Collected " << buffer.count() << " samples\n";
    std::cout << "Average: " << buffer.average() << "\n";
    std::cout << "Min: " << buffer.min() << "\n";
    std::cout << "Max: " << buffer.max() << "\n";

    const SensorData* latest = buffer.get_latest();
    if (latest) {
        std::cout << "Latest: " << latest->value << " at t=" << latest->timestamp << "\n";
    }
}

int main() {
    state_machine_demo();
    ring_buffer_demo();
    register_map_demo();
    lookup_table_demo();
    static_string_demo();
    sensor_buffer_demo();

    std::cout << "\n=== Key Takeaways ===\n";
    std::cout << "1. std::array perfect for fixed-size collections\n";
    std::cout << "2. State tables, ring buffers, lookup tables all benefit\n";
    std::cout << "3. Zero overhead, full type safety\n";
    std::cout << "4. Easy integration with C APIs via .data()\n";
    std::cout << "5. Compile-time size enables template optimizations\n";

    return 0;
}
