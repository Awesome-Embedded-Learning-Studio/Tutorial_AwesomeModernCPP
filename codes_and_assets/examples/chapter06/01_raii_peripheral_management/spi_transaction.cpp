// SPI 事务 RAII 示例 - 自动管理片选信号
// 演示如何使用 RAII 确保 SPI 事务的正确开始和结束

#include <cstdint>
#include <cstdio>

// 模拟 SPI HAL
namespace hal {
    inline void spi_begin_transaction() {
        printf("[HAL] SPI transaction started\n");
    }

    inline void spi_end_transaction() {
        printf("[HAL] SPI transaction ended\n");
    }

    inline void spi_transfer(const uint8_t* tx, uint8_t* rx, size_t n) {
        printf("[HAL] SPI transfer: %zu bytes\n", n);
        for (size_t i = 0; i < n; ++i) {
            if (rx) rx[i] = tx[i]; // 简单回环
        }
    }
}

// 模拟 GPIO 用于片选
namespace gpio {
    inline void write(uint8_t pin, bool level) {
        printf("[GPIO] CS pin %d = %d\n", pin, level);
    }
}

class SPIBus {
public:
    void beginTransaction() noexcept {
        hal::spi_begin_transaction();
    }

    void endTransaction() noexcept {
        hal::spi_end_transaction();
    }

    void transfer(const uint8_t* tx, uint8_t* rx, size_t n) noexcept {
        hal::spi_transfer(tx, rx, n);
    }

    void setCS(uint8_t pin, bool level) noexcept {
        gpio::write(pin, level);
    }
};

// Guard: 构造时拉低 CS 并 beginTransaction；析构时拉高 CS 并 endTransaction
class SPITransaction {
public:
    SPITransaction(SPIBus& bus, uint8_t cs_pin) noexcept
        : bus_(bus), cs_pin_(cs_pin), active_(true)
    {
        bus_.beginTransaction();
        bus_.setCS(cs_pin_, false);  // Active low
    }

    SPITransaction(const SPITransaction&) = delete;
    SPITransaction& operator=(const SPITransaction&) = delete;
    SPITransaction(SPITransaction&& other) noexcept
        : bus_(other.bus_), cs_pin_(other.cs_pin_), active_(other.active_)
    {
        other.active_ = false;
    }

    ~SPITransaction() noexcept {
        if (!active_) return;
        bus_.setCS(cs_pin_, true);   // Deassert
        bus_.endTransaction();
    }

    void dismiss() noexcept { active_ = false; }

private:
    SPIBus& bus_;
    uint8_t cs_pin_;
    bool active_;
};

// 使用示例
void read_sensor(SPIBus& spi, uint8_t cs, const uint8_t* tx_buf, uint8_t* rx_buf, size_t len) {
    SPITransaction t(spi, cs);  // 自动拉低 CS
    spi.transfer(tx_buf, rx_buf, len);
    // 任何 return、异常或 early exit 都会正确释放 CS
}

// 演示 early return 的安全性
void read_with_early_return(SPIBus& spi, uint8_t cs) {
    SPITransaction t(spi, cs);

    uint8_t cmd[] = {0x01, 0x02};
    uint8_t resp[2] = {0};
    spi.transfer(cmd, resp, 2);

    // 模拟错误情况
    if (resp[0] == 0xFF) {
        printf("Error detected, returning early\n");
        return;  // SPITransaction 析构函数仍然会被调用！
    }

    printf("Read successful\n");
}

int main() {
    printf("=== SPI Transaction RAII Example ===\n\n");

    SPIBus spi;
    uint8_t cs_pin = 10;

    // 正常事务
    uint8_t tx_data[] = {0xAB, 0xCD};
    uint8_t rx_data[2] = {0};
    read_sensor(spi, cs_pin, tx_data, rx_data, 2);

    printf("\n");

    // 带 early return 的事务
    read_with_early_return(spi, cs_pin);

    printf("\n=== Demonstration complete ===\n");
    printf("Notice how CS is properly deasserted even with early return.\n");

    return 0;
}
