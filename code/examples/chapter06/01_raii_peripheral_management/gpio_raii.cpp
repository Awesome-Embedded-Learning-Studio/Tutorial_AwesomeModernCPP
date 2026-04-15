// GPIO RAII 示例 - 管理单个 GPIO 引脚的生命周期
// 演示如何使用 RAII 自动管理硬件外设资源

#include <cstdint>
#include <cstdio>

// 模拟 HAL API
namespace hal {
    inline void gpio_config(uint8_t pin, bool output, bool level) {
        printf("[HAL] GPIO %d configured as %s, level=%d\n",
               pin, output ? "output" : "input", level);
    }

    inline void gpio_write(uint8_t pin, bool level) {
        printf("[HAL] GPIO %d write %d\n", pin, level);
    }

    inline bool gpio_read(uint8_t pin) {
        printf("[HAL] GPIO %d read\n", pin);
        return false;
    }
}

enum class GPIODir { Input, Output };

class GPIOPin {
public:
    GPIOPin(uint8_t pin, GPIODir dir, bool init_level = false) noexcept
        : pin_(pin), dir_(dir)
    {
        hal::gpio_config(pin_, dir_ == GPIODir::Output, init_level);
        if (dir_ == GPIODir::Output) {
            hal::gpio_write(pin_, init_level);
        }
    }

    // 不可拷贝、可移动
    GPIOPin(const GPIOPin&) = delete;
    GPIOPin& operator=(const GPIOPin&) = delete;
    GPIOPin(GPIOPin&& other) noexcept
        : pin_(other.pin_), dir_(other.dir_), moved_(other.moved_)
    {
        other.moved_ = true;
    }
    GPIOPin& operator=(GPIOPin&&) = delete;

    ~GPIOPin() noexcept {
        if (moved_) return;
        // 将引脚恢复为安全态：输入（高阻）
        hal::gpio_config(pin_, false, false);
    }

    void write(bool v) noexcept {
        if (dir_ == GPIODir::Output) hal::gpio_write(pin_, v);
    }

    bool read() const noexcept { return hal::gpio_read(pin_); }

    uint8_t pin() const noexcept { return pin_; }

    // 手动放弃析构行为（比如资源被移交）
    void release() noexcept { moved_ = true; }

private:
    uint8_t pin_;
    GPIODir dir_;
    bool moved_ = false;
};

// 使用示例
void blink_once() {
    GPIOPin led(13, GPIODir::Output, false);
    printf("LED on\n");
    led.write(true);
    printf("LED off (about to exit scope)\n");
    // 离开作用域时，led 自动恢复为输入（safe state）
}

int main() {
    printf("=== GPIO RAII Example ===\n\n");

    blink_once();

    printf("\n=== Demonstration complete ===\n");
    printf("Notice how GPIO 13 was automatically restored to input mode\n");
    printf("when the GPIOPin object went out of scope.\n");

    return 0;
}
