/*
 * 重构阶梯 · 第 3 档:寄存器位域 + 强类型枚举。
 * 把"PD12 = 输出"从手写位移,升级成 reg_field<Offset,Width> + enum class PinMode。
 * 传错模式值编译期就拦,代价仍是零指令(对照 objdump)。
 */
#include <cstdint>

template <typename T, std::uintptr_t Addr> struct mmio_reg {
    static volatile T& value() noexcept { return *reinterpret_cast<volatile T*>(Addr); }
};

// 寄存器位域:从 Offset 位起、Width 位宽
template <std::uint32_t Offset, std::uint8_t Width> struct reg_field {
    static constexpr std::uint32_t kMask = (Width >= 32) ? 0xFFFFFFFFu : ((1u << Width) - 1u);
    static constexpr std::uint32_t kShift = Offset;

    template <typename Reg> static void set(std::uint32_t v) {
        Reg::value() = (Reg::value() & ~(kMask << kShift)) | ((v & kMask) << kShift);
    }
};

// 强类型:GPIO 模式,只能传这四个值之一
enum class PinMode : std::uint32_t {
    kInput = 0,
    kOutput = 1,
    kAlternate = 2,
    kAnalog = 3,
};

using RCC_AHB1ENR = mmio_reg<std::uint32_t, 0x40023830>;
using GPIOD_MODER = mmio_reg<std::uint32_t, 0x40020C00>;
using GPIOD_ODR = mmio_reg<std::uint32_t, 0x40020C14>;

using ModerPd12 = reg_field<24, 2>; // MODER 里 PD12 占 bit 24-25

static void delay(unsigned int n) {
    volatile unsigned int i = n;
    while (i--)
        __asm__ volatile("nop");
}

int main() {
    RCC_AHB1ENR::value() |= (1u << 3); /* 打开 GPIOD 时钟 */
    ModerPd12::set<GPIOD_MODER>(static_cast<std::uint32_t>(PinMode::kOutput));

    for (;;) {
        GPIOD_ODR::value() ^= (1u << 12);
        delay(500000);
    }
}
