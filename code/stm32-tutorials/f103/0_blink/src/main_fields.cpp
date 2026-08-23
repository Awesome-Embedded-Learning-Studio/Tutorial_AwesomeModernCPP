/*
 * 重构阶梯 · 第 3 档:寄存器位域 + 强类型枚举。
 *
 * F1 的引脚配置和 F4 不一样:不是 2bit 的 MODER,而是 CNF[1:0] + MODE[1:0] 共 4bit。
 * 所以这一档的枚举直接编码这 4bit——传一个"不存在的模式"编译期就拦,
 * 代价仍是零指令(对照 objdump)。
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

// F1 每引脚配置 = CNF[1:0] << 2 | MODE[1:0]
enum class PinConfig : std::uint32_t {
    kInputAnalog = 0b0000,     // CNF=00 模拟输入
    kInputFloating = 0b0100,   // CNF=01 浮空输入(复位默认)
    kInputPullUpDown = 0b1000, // CNF=10 上/下拉(方向由 ODR 选)
    kOutputPush2MHz = 0b0010,  // CNF=00 推挽输出 2MHz
    kOutputPush50MHz = 0b0011, // CNF=00 推挽输出 50MHz
    kAfPush2MHz = 0b1010,      // CNF=10 复用推挽 2MHz(UART TX 之类)
};

using RCC_APB2ENR = mmio_reg<std::uint32_t, 0x40021018>;
using GPIOC_CRH = mmio_reg<std::uint32_t, 0x40011004>;
using GPIOC_ODR = mmio_reg<std::uint32_t, 0x4001100C>;

using CrhPc13 = reg_field<20, 4>; // CRH 里 PC13 占 bit 20-23(引脚 13-8 = 第 5 组)

static void delay(unsigned int n) {
    volatile unsigned int i = n;
    while (i--)
        __asm__ volatile("nop");
}

int main() {
    RCC_APB2ENR::value() |= (1u << 4); /* 打开 GPIOC 时钟 */
    CrhPc13::set<GPIOC_CRH>(static_cast<std::uint32_t>(PinConfig::kOutputPush2MHz));

    for (;;) {
        GPIOC_ODR::value() ^= (1u << 13);
        delay(200000);
    }
}
