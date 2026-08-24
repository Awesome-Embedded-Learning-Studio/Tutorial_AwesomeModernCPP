/*
 * 重构阶梯 · 第 4 档:模板化 GPIO 端口 + LED。
 *
 * F1 的引脚配置拆在 CRL(引脚 0-7)/ CRH(引脚 8-15)两份寄存器里,每引脚 4bit——
 * 模板里用 if constexpr 在编译期选寄存器,运行期没有任何分派开销。
 * main 里只剩 UserLed::init() / UserLed::toggle(),零开销(objdump 验)。
 */
#include <cstdint>

template <typename T, std::uintptr_t Addr> struct mmio_reg {
    static volatile T& value() noexcept { return *reinterpret_cast<volatile T*>(Addr); }
};

// F1 每引脚配置 = CNF[1:0] << 2 | MODE[1:0](含义见第 3 档)
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
        if constexpr (Pin < 8) { // 低 8 个引脚在 CRL
            constexpr std::uint32_t shift = 4 * Pin;
            auto& crl = mmio_reg<std::uint32_t, BaseAddr + 0x00>::value();
            crl = (crl & ~(0xFu << shift)) | (v << shift);
        } else { // 高 8 个引脚在 CRH
            constexpr std::uint32_t shift = 4 * (Pin - 8);
            auto& crh = mmio_reg<std::uint32_t, BaseAddr + 0x04>::value();
            crh = (crh & ~(0xFu << shift)) | (v << shift);
        }
    }

    template <std::uint8_t Pin> static void toggle() {
        mmio_reg<std::uint32_t, BaseAddr + 0x0C>::value() ^= (1u << Pin);
    }
};

using GpioC = GpioPort<0x40011000, 4>; // 基址 0x40011000,时钟使能位 APB2ENR bit4

template <typename Port, std::uint8_t Pin> struct Led {
    static void init() {
        Port::enable_clock();
        Port::template set_config<Pin>(PinConfig::kOutputPush2MHz);
    }
    static void toggle() { Port::template toggle<Pin>(); }
};

using UserLed = Led<GpioC, 13>; // PC13,Blue Pill 板载 LED

static void delay(unsigned int n) {
    volatile unsigned int i = n;
    while (i--)
        __asm__ volatile("nop");
}

int main() {
    UserLed::init();
    for (;;) {
        UserLed::toggle();
        delay(200000);
    }
}
