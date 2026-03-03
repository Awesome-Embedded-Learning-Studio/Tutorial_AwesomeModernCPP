// 初始化列表示例：展示初始化列表 vs 成员赋值的差异

#include <iostream>
#include <cstdint>
#include <string>

// ==================== 不推荐的写法：成员赋值 ====================
class Timer_Bad
{
public:
    Timer_Bad(uint32_t period)
    {
        period_ = period;      // 赋值，不是初始化
        enabled_ = false;     // 先默认初始化，再赋值
    }

private:
    uint32_t period_;
    bool     enabled_;
};

// ==================== 推荐的写法：初始化列表 ====================
class Timer_Good
{
public:
    Timer_Good(uint32_t period)
        : period_(period)    // 直接初始化
        , enabled_(false)   // 直接初始化
    {}

private:
    uint32_t period_;
    bool     enabled_;
};

// ==================== const 成员必须使用初始化列表 ====================
class Device
{
public:
    Device(uint32_t id)
        : id_(id)  // const 成员只能初始化一次
    {}

    uint32_t get_id() const { return id_; }

private:
    const uint32_t id_;
};

// ==================== 引用成员必须使用初始化列表 ====================
class GPIO
{
public:
    explicit GPIO(uint32_t pin) : pin_(pin) {}
    uint32_t read() const { return pin_; }

private:
    uint32_t pin_;
};

class Driver
{
public:
    Driver(GPIO& gpio)
        : gpio_(gpio)  // 引用必须初始化
    {}

    void use_gpio() {
        std::cout << "GPIO pin: " << gpio_.read() << std::endl;
    }

private:
    GPIO& gpio_;
};

// ==================== 没有默认构造的成员必须使用初始化列表 ====================
class SpiBus
{
public:
    explicit SpiBus(uint32_t base_addr) : base_addr_(base_addr) {}
    uint32_t get_base() const { return base_addr_; }

private:
    uint32_t base_addr_;
};

class Sensor
{
public:
    Sensor()
        : spi_(0x40013000)  // SpiBus 没有默认构造，必须使用初始化列表
    {}

    uint32_t read_spi() const { return spi_.get_base(); }

private:
    SpiBus spi_;
};

// ==================== 循环缓冲区：展示完整初始化 ====================
class RingBuffer
{
public:
    RingBuffer(uint8_t* buf, size_t size)
        : buffer_(buf)
        , size_(size)
        , head_(0)
        , tail_(0)
    {
        // 空函数体，所有初始化已在列表中完成
    }

    bool write(uint8_t data) {
        size_t next = (head_ + 1) % size_;
        if (next == tail_) return false;  // 缓冲区满
        buffer_[head_] = data;
        head_ = next;
        return true;
    }

    bool read(uint8_t* data) {
        if (head_ == tail_) return false;  // 缓冲区空
        *data = buffer_[tail_];
        tail_ = (tail_ + 1) % size_;
        return true;
    }

private:
    uint8_t* buffer_;
    size_t   size_;
    size_t   head_;
    size_t   tail_;
};

// ==================== 演示主函数 ====================
int main() {
    std::cout << "=== 初始化列表示例 ===" << std::endl;

    // 基本用法对比
    std::cout << "\n--- 基本用法 ---" << std::endl;
    Timer_Bad timer_bad(1000);
    Timer_Good timer_good(1000);

    // const 成员
    std::cout << "\n--- const 成员 ---" << std::endl;
    Device device(42);
    std::cout << "Device ID: " << device.get_id() << std::endl;

    // 引用成员
    std::cout << "\n--- 引用成员 ---" << std::endl;
    GPIO gpio(5);
    Driver driver(gpio);
    driver.use_gpio();

    // 没有默认构造的成员
    std::cout << "\n--- 没有默认构造的成员 ---" << std::endl;
    Sensor sensor;
    std::cout << "SPI Base: 0x" << std::hex << sensor.read_spi() << std::dec << std::endl;

    // 循环缓冲区
    std::cout << "\n--- 循环缓冲区 ---" << std::endl;
    uint8_t buf[16];
    RingBuffer ring_buf(buf, sizeof(buf));

    for (uint8_t i = 0; i < 10; ++i) {
        ring_buf.write(i);
    }

    uint8_t val;
    while (ring_buf.read(&val)) {
        std::cout << "Read: " << int(val) << std::endl;
    }

    std::cout << "\n关键点：" << std::endl;
    std::cout << "1. 成员赋值：先默认初始化，再赋值（两次操作）" << std::endl;
    std::cout << "2. 初始化列表：直接初始化（一次操作）" << std::endl;
    std::cout << "3. const 成员、引用成员、无默认构造成员必须用初始化列表" << std::endl;
    std::cout << "4. 初始化列表让对象在构造完成后处于可用状态" << std::endl;

    return 0;
}
