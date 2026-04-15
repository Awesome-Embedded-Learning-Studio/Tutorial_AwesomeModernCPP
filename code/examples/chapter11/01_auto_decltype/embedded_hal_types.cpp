// embedded_hal_types.cpp - 嵌入式HAL库类型中的auto应用
#include <iostream>
#include <vector>
#include <map>
#include <memory>
#include <cstdint>
#include <array>

// 模拟嵌入式HAL类型
struct GPIO_TypeDef {
    volatile uint32_t MODER;    // 模式寄存器
    volatile uint32_t OTYPER;   // 输出类型寄存器
    volatile uint32_t OSPEEDR;  // 输出速度寄存器
    volatile uint32_t PUPDR;    // 上拉/下拉寄存器
    volatile uint32_t IDR;      // 输入数据寄存器
    volatile uint32_t ODR;      // 输出数据寄存器
};

struct UART_InitTypeDef {
    uint32_t BaudRate;
    uint32_t WordLength;
    uint32_t StopBits;
    uint32_t Parity;
};

struct UART_HandleTypeDef {
    uint32_t Instance;           // UART基址
    UART_InitTypeDef Init;       // 初始化参数
};

// 模拟HAL寄存器地址
constexpr uint32_t GPIOA_BASE = 0x40000000;
#define GPIOA ((GPIO_TypeDef*) GPIOA_BASE)

// 传感器接口
class ISensor {
public:
    virtual ~ISensor() = default;
    virtual bool read(uint8_t* data, size_t len) = 0;
    virtual uint8_t get_id() const = 0;
};

// 传感器管理器
class SensorManager {
public:
    using SensorMap = std::map<uint8_t, std::unique_ptr<ISensor>>;

    // 传统写法：复杂的返回类型
    // SensorManager::SensorMap::iterator find_sensor(uint8_t id) {
    //     return sensors_.find(id);
    // }

    // 使用auto：简洁清晰
    auto find_sensor(uint8_t id) {
        return sensors_.find(id);
    }

    auto begin() { return sensors_.begin(); }
    auto end() { return sensors_.end(); }

    auto sensors_begin() const { return sensors_.begin(); }
    auto sensors_end() const { return sensors_.end(); }

private:
    SensorMap sensors_;
};

// UART缓冲区管理器
template<size_t BufferSize>
class UARTBufferManager {
public:
    // 使用auto简化迭代器类型
    auto get_tx_buffer() { return tx_buffer_.begin(); }
    auto get_rx_buffer() { return rx_buffer_.begin(); }

    auto tx_buffer_end() { return tx_buffer_.end(); }
    auto rx_buffer_end() { return rx_buffer_.end(); }

private:
    std::array<uint8_t, BufferSize> tx_buffer_{};
    std::array<uint8_t, BufferSize> rx_buffer_{};
};

// 寄存器访问辅助类
template<typename RegType>
class Register {
public:
    explicit Register(uintptr_t address) : address_(address) {}

    // 读取寄存器
    decltype(auto) read() const {
        return *reinterpret_cast<volatile RegType*>(address_);
    }

    // 写入寄存器
    void write(RegType value) const {
        *reinterpret_cast<volatile RegType*>(address_) = value;
    }

    // 位操作
    void set_bits(RegType mask) const {
        auto& reg = *reinterpret_cast<volatile RegType*>(address_);
        reg |= mask;
    }

    void clear_bits(RegType mask) const {
        auto& reg = *reinterpret_cast<volatile RegType*>(address_);
        reg &= ~mask;
    }

private:
    uintptr_t address_;
};

void demonstrate_hal_types() {
    std::cout << "=== HAL类型简化演示 ===\n\n";

    // 1. 复杂的迭代器类型
    std::cout << "1. 复杂迭代器类型的简化:\n";

    // 传统写法（注释）：
    // std::map<uint8_t, std::unique_ptr<ISensor>>::iterator it = manager.find_sensor(5);

    SensorManager manager;
    auto it = manager.find_sensor(5);  // 简洁！

    std::cout << "  使用auto替代复杂的迭代器类型\n";

    // 2. 范围for循环
    std::cout << "\n2. 范围for循环:\n";

    // 嵌入式场景：遍历传感器配置
    std::map<uint8_t, const char*> sensor_config = {
        {1, "Temperature_Sensor"},
        {2, "Humidity_Sensor"},
        {3, "Pressure_Sensor"}
    };

    std::cout << "  传感器配置:\n";
    for (const auto& [id, name] : sensor_config) {
        std::cout << "    ID: " << +id << ", Name: " << name << '\n';
    }

    // 3. 寄存器操作
    std::cout << "\n3. 寄存器访问:\n";

    Register<uint32_t> gpio_moder(GPIOA_BASE + offsetof(GPIO_TypeDef, MODER));
    Register<uint32_t> gpio_odr(GPIOA_BASE + offsetof(GPIO_TypeDef, ODR));

    // 模拟寄存器操作
    std::cout << "  GPIO MODER 地址: 0x" << std::hex << GPIOA_BASE + offsetof(GPIO_TypeDef, MODER) << std::dec << '\n';
    std::cout << "  使用auto简化寄存器操作代码\n";
}

void demonstrate_buffer_operations() {
    std::cout << "\n=== 缓冲区操作演示 ===\n\n";

    UARTBufferManager<256> uart_buffer;

    // 使用auto简化缓冲区操作
    auto tx_begin = uart_buffer.get_tx_buffer();
    auto rx_begin = uart_buffer.get_rx_buffer();

    std::cout << "1. TX缓冲区大小: 256\n";
    std::cout << "2. RX缓冲区大小: 256\n";

    // 模拟填充缓冲区
    std::vector<uint8_t> data = {0x01, 0x02, 0x03, 0x04};
    auto dest = tx_begin;
    for (auto byte : data) {
        *dest++ = byte;
    }

    std::cout << "3. 写入 " << data.size() << " 字节到TX缓冲区\n";
}

// 实际应用：DMA配置结构
struct DMA_Config {
    uint32_t peripheral_address;
    uint32_t memory_address;
    uint32_t direction;
    uint32_t buffer_size;
    uint32_t mode;
};

class DMA_Manager {
public:
    // 使用auto返回配置引用
    auto& get_config(uint8_t channel) {
        return configs_[channel];
    }

    const auto& get_config(uint8_t channel) const {
        return configs_[channel];
    }

private:
    std::array<DMA_Config, 8> configs_{};
};

void demonstrate_dma_config() {
    std::cout << "\n=== DMA配置演示 ===\n\n";

    DMA_Manager dma_manager;

    // 使用auto引用直接修改配置
    auto& config = dma_manager.get_config(0);
    config.peripheral_address = 0x40000000;
    config.memory_address = 0x20000000;
    config.buffer_size = 100;
    config.direction = 1;  // 内存到外设

    std::cout << "DMA通道0配置:\n";
    std::cout << "  外设地址: 0x" << std::hex << config.peripheral_address << std::dec << '\n';
    std::cout << "  内存地址: 0x" << std::hex << config.memory_address << std::dec << '\n';
    std::cout << "  缓冲区大小: " << config.buffer_size << '\n';
}

// 实际应用：中断向量表
using IRQHandler = void(*)();

class IRQManager {
public:
    auto& get_handler(uint8_t irq_num) {
        return handlers_[irq_num];
    }

    void set_handler(uint8_t irq_num, IRQHandler handler) {
        handlers_[irq_num] = handler;
    }

private:
    std::array<IRQHandler, 256> handlers_{};
};

void external_irq_handler() {
    // 模拟中断处理
    std::cout << "  [IRQ] 外部中断触发\n";
}

void demonstrate_irq_handlers() {
    std::cout << "\n=== 中断处理演示 ===\n\n";

    IRQManager irq_manager;

    // 使用auto简化中断注册
    auto handler = external_irq_handler;
    irq_manager.set_handler(10, handler);

    std::cout << "1. 注册外部中断处理程序到IRQ10\n";

    // 获取并检查处理器
    const auto& registered = irq_manager.get_handler(10);
    if (registered != nullptr) {
        std::cout << "2. 中断处理程序已注册\n";
    }
}

int main() {
    demonstrate_hal_types();
    demonstrate_buffer_operations();
    demonstrate_dma_config();
    demonstrate_irq_handlers();

    std::cout << "\n=== 演示结束 ===\n";
    std::cout << "\n总结:\n";
    std::cout << "1. auto简化了复杂的HAL类型声明\n";
    std::cout << "2. 在范围for中使用auto&避免拷贝\n";
    std::cout << "3. auto引用用于直接修改配置\n";
    std::cout << "4. 结合decltype(auto)实现完美的转发语义\n";

    return 0;
}
