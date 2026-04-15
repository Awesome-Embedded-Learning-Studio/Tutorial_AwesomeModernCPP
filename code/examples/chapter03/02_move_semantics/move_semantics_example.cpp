// 移动语义示例：资源所有权转移

#include <iostream>
#include <array>
#include <cstdint>
#include <utility>
#include <vector>

// ==================== 简单的 DMA 缓冲区 ====================
class DMABuffer_Simple
{
public:
    DMABuffer_Simple(size_t len) : length_(len) {}

    // 拷贝构造（深拷贝）
    DMABuffer_Simple(const DMABuffer_Simple& other) = default;

    // 移动构造（对于 std::array，移动等价于拷贝）
    DMABuffer_Simple(DMABuffer_Simple&& other) noexcept
        : length_(other.length_)
    {
        other.length_ = 0;
    }

    size_t get_length() const { return length_; }

private:
    std::array<uint8_t, 256> data;
    size_t length_;
};

// ==================== 带动态资源的 DMA 缓冲区 ====================
class DMABuffer_Dynamic
{
public:
    DMABuffer_Dynamic(size_t cap)
        : data_(new uint8_t[cap])
        , length_(0)
        , capacity_(cap)
    {}

    ~DMABuffer_Dynamic() {
        delete[] data_;
    }

    // 禁用拷贝
    DMABuffer_Dynamic(const DMABuffer_Dynamic&) = delete;
    DMABuffer_Dynamic& operator=(const DMABuffer_Dynamic&) = delete;

    // 移动构造
    DMABuffer_Dynamic(DMABuffer_Dynamic&& other) noexcept
        : data_(other.data_)
        , length_(other.length_)
        , capacity_(other.capacity_)
    {
        other.data_ = nullptr;
        other.length_ = 0;
        other.capacity_ = 0;
    }

    // 移动赋值
    DMABuffer_Dynamic& operator=(DMABuffer_Dynamic&& other) noexcept {
        if (this != &other) {
            delete[] data_;
            data_ = other.data_;
            length_ = other.length_;
            capacity_ = other.capacity_;

            other.data_ = nullptr;
            other.length_ = 0;
            other.capacity_ = 0;
        }
        return *this;
    }

    size_t get_length() const { return length_; }
    size_t get_capacity() const { return capacity_; }

private:
    uint8_t* data_;
    size_t length_;
    size_t capacity_;
};

// ==================== SPI 总线 RAII 类 ====================
class SPIBus
{
public:
    SPIBus(uint32_t base_addr, uint8_t tx_ch, uint8_t rx_ch)
        : peripheral_(reinterpret_cast<volatile uint32_t*>(base_addr))
        , tx_ch_(tx_ch)
        , rx_ch_(rx_ch)
    {
        std::cout << "SPI initialized at 0x" << std::hex << base_addr << std::dec << std::endl;
    }

    ~SPIBus() {
        if (peripheral_) {
            std::cout << "SPI deinitialized" << std::endl;
        }
    }

    // 禁用拷贝：硬件资源不能共享
    SPIBus(const SPIBus&) = delete;
    SPIBus& operator=(const SPIBus&) = delete;

    // 允许移动：所有权可以转移
    SPIBus(SPIBus&& other) noexcept
        : peripheral_(other.peripheral_)
        , tx_ch_(other.tx_ch_)
        , rx_ch_(other.rx_ch_)
    {
        other.peripheral_ = nullptr;
    }

    SPIBus& operator=(SPIBus&& other) noexcept {
        if (this != &other) {
            if (peripheral_) {
                // 释放当前资源
            }
            peripheral_ = other.peripheral_;
            tx_ch_ = other.tx_ch_;
            rx_ch_ = other.rx_ch_;
            other.peripheral_ = nullptr;
        }
        return *this;
    }

    bool is_valid() const { return peripheral_ != nullptr; }

private:
    volatile uint32_t* peripheral_;
    uint8_t tx_ch_;
    uint8_t rx_ch_;
};

// ==================== 工厂函数 ====================
SPIBus create_spi() {
    return SPIBus(0x40013000, 1, 2);  // 返回临时对象
}

// ==================== 完美转发示例 ====================
template<typename T>
void factory_demo(T&& arg) {
    (void)arg;
    std::cout << "Factory called with rvalue: " << std::is_rvalue_reference_v<T> << std::endl;
}

// ==================== 演示主函数 ====================
int main() {
    std::cout << "=== 移动语义示例 ===" << std::endl;

    // 1. 简单缓冲区的移动
    std::cout << "\n--- 简单缓冲区 ---" << std::endl;
    DMABuffer_Simple buf1(128);
    std::cout << "buf1 length: " << buf1.get_length() << std::endl;

    DMABuffer_Simple buf2 = std::move(buf1);
    std::cout << "buf2 length (moved): " << buf2.get_length() << std::endl;
    std::cout << "buf1 length (after move): " << buf1.get_length() << std::endl;

    // 2. 动态资源的零拷贝转移
    std::cout << "\n--- 动态资源零拷贝 ---" << std::endl;
    DMABuffer_Dynamic dyn_buf(1024);
    std::cout << "dyn_buf capacity: " << dyn_buf.get_capacity() << std::endl;

    DMABuffer_Dynamic moved_buf = std::move(dyn_buf);
    std::cout << "moved_buf capacity: " << moved_buf.get_capacity() << std::endl;
    std::cout << "dyn_buf capacity (after move): " << dyn_buf.get_capacity() << std::endl;

    // 3. SPI 总线所有权转移
    std::cout << "\n--- SPI 总线所有权 ---" << std::endl;
    SPIBus spi = create_spi();  // 移动构造
    std::cout << "SPI valid: " << spi.is_valid() << std::endl;

    // 4. 容器与移动
    std::cout << "\n--- 容器与移动 ---" << std::endl;
    std::vector<DMABuffer_Dynamic> buffers;
    buffers.reserve(3);

    DMABuffer_Dynamic b1(256);
    DMABuffer_Dynamic b2(512);
    DMABuffer_Dynamic b3(1024);

    buffers.push_back(std::move(b1));
    buffers.push_back(std::move(b2));
    buffers.push_back(std::move(b3));

    std::cout << "Buffers in vector: " << buffers.size() << std::endl;

    // 5. 完美转发
    std::cout << "\n--- 完美转发 ---" << std::endl;
    DMABuffer_Dynamic temp(64);
    factory_demo(temp);           // 左值
    factory_demo(std::move(temp)); // 右值
    factory_demo(DMABuffer_Dynamic(32)); // 临时对象

    std::cout << "\n关键点：" << std::endl;
    std::cout << "1. 移动语义转移资源所有权，避免拷贝" << std::endl;
    std::cout << "2. 硬件资源应禁用拷贝、启用移动" << std::endl;
    std::cout << "3. std::move 只转换类型，不立即销毁对象" << std::endl;
    std::cout << "4. 容器扩容时优先使用移动构造" << std::endl;
    std::cout << "5. 返回局部对象时直接 return，编译器会优化" << std::endl;

    return 0;
}
