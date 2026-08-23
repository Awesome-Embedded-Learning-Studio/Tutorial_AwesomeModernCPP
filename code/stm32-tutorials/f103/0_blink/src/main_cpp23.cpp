/*
 * 重构阶梯 · 第 5 档:C++23 收尾。
 * consteval 把引脚号校验提到编译期(非法引脚在 static_assert 里直接编译失败);
 * [[nodiscard]] 标注不可丢弃的访问器;constinit 让配置常量在编译期完成初始化。
 * 仍是零开销——这些都是编译期约束,不产生运行时代码。
 */
#include <cstdint>

template <typename T, std::uintptr_t Addr> struct mmio_reg {
    static volatile T& value() noexcept { return *reinterpret_cast<volatile T*>(Addr); }
};

enum class PinConfig : std::uint32_t {
    kInputFloating = 0b0100,
    kInputPullUpDown = 0b1000,
    kOutputPush2MHz = 0b0010,
    kAfPush2MHz = 0b1010,
};

using RCC_APB2ENR = mmio_reg<std::uint32_t, 0x40021018>;

template <std::uintptr_t BaseAddr, std::uint32_t ClockBit> struct GpioPort {
    static void enable_clock() { RCC_APB2ENR::value() |= (1u << ClockBit); }

    template <std::uint8_t Pin> static void set_config(PinConfig cfg) {
        const std::uint32_t v = static_cast<std::uint32_t>(cfg);
        if constexpr (Pin < 8) {
            constexpr std::uint32_t shift = 4 * Pin;
            auto& crl = mmio_reg<std::uint32_t, BaseAddr + 0x00>::value();
            crl = (crl & ~(0xFu << shift)) | (v << shift);
        } else {
            constexpr std::uint32_t shift = 4 * (Pin - 8);
            auto& crh = mmio_reg<std::uint32_t, BaseAddr + 0x04>::value();
            crh = (crh & ~(0xFu << shift)) | (v << shift);
        }
    }

    template <std::uint8_t Pin> static void toggle() {
        mmio_reg<std::uint32_t, BaseAddr + 0x0C>::value() ^= (1u << Pin);
    }
};

// consteval:编译期校验函数,非法引脚(>=16)在 static_assert 里触发编译失败
consteval bool valid_pin(std::uint8_t pin) noexcept {
    return pin < 16;
}

template <typename Port, std::uint8_t Pin> struct Led {
    static_assert(valid_pin(Pin), "pin must be 0-15");
    static void init() {
        Port::enable_clock();
        Port::template set_config<Pin>(PinConfig::kOutputPush2MHz);
    }
    static void toggle() { Port::template toggle<Pin>(); }
    [[nodiscard]] static consteval std::uint8_t pin() noexcept { return Pin; }
};

using GpioC = GpioPort<0x40011000, 4>;
using UserLed = Led<GpioC, 13>;

// constinit:编译期完成初始化的配置常量
constinit const std::uint32_t kBlinkDelay = 200000;

static void delay(unsigned int n) {
    volatile unsigned int i = n;
    while (i--)
        __asm__ volatile("nop");
}

int main() {
    UserLed::init();
    for (;;) {
        UserLed::toggle();
        delay(kBlinkDelay);
    }
}
