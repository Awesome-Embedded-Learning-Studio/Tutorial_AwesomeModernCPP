/*
 * 重构阶梯 · 第 4 档:模板化 GPIO 端口 + LED。
 * 把端口基址、时钟位、引脚号都做成模板参数,编译期绑定。
 * main 里只剩 UserLed::init() / UserLed::toggle(),零开销(objdump 验)。
 */
#include <cstdint>

template <typename T, std::uintptr_t Addr> struct mmio_reg {
    static volatile T& value() noexcept { return *reinterpret_cast<volatile T*>(Addr); }
};

enum class PinMode : std::uint32_t { kInput = 0, kOutput = 1, kAlternate = 2, kAnalog = 3 };

using RCC_AHB1ENR = mmio_reg<std::uint32_t, 0x40023830>;

template <std::uintptr_t BaseAddr, std::uint32_t ClockBit> struct GpioPort {
    static void enable_clock() { RCC_AHB1ENR::value() |= (1u << ClockBit); }
    template <std::uint8_t Pin> static void set_mode(PinMode mode) {
        constexpr std::uint32_t shift = 2 * Pin;
        auto& moder = mmio_reg<std::uint32_t, BaseAddr>::value();
        moder = (moder & ~(3u << shift)) | ((static_cast<std::uint32_t>(mode) & 3u) << shift);
    }
    template <std::uint8_t Pin> static void toggle() {
        mmio_reg<std::uint32_t, BaseAddr + 0x14>::value() ^= (1u << Pin);
    }
};

using GpioD = GpioPort<0x40020C00, 3>; // 基址 0x40020C00,时钟使能位 bit3

template <typename Port, std::uint8_t Pin> struct Led {
    static void init() {
        Port::enable_clock();
        Port::template set_mode<Pin>(PinMode::kOutput);
    }
    static void toggle() { Port::template toggle<Pin>(); }
};

using UserLed = Led<GpioD, 12>; // PD12,Discovery 板的 UserLED

static void delay(unsigned int n) {
    volatile unsigned int i = n;
    while (i--)
        __asm__ volatile("nop");
}

int main() {
    UserLed::init();
    for (;;) {
        UserLed::toggle();
        delay(500000);
    }
}
