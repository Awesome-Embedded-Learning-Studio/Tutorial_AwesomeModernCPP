/*
 * 重构阶梯 · 第 2 档:类型安全寄存器封装(mmio_reg 模板)。
 *
 * 第 1 档(main.c)用裸的 *(volatile unsigned int*)0x... 强转,地址和类型各走各的,
 * 写错地址、用错类型编译器都不拦。这一档把"地址 + 类型"绑成一个 mmio_reg<类型, 地址>,
 * 用错类型(例如把 32 位寄存器当 16 位读写)编译期就抓。
 *
 * 目标:零开销 —— 跟第 1 档生成同一份汇编(本档末尾对照 objdump 验证)。
 */
#include <cstdint>

template <typename T, std::uintptr_t Addr> struct mmio_reg {
    static volatile T& value() noexcept { return *reinterpret_cast<volatile T*>(Addr); }
};

using RCC_APB2ENR = mmio_reg<std::uint32_t, 0x40021018>;
using GPIOC_CRH = mmio_reg<std::uint32_t, 0x40011004>;
using GPIOC_ODR = mmio_reg<std::uint32_t, 0x4001100C>;

static void delay(unsigned int n) {
    volatile unsigned int i = n;
    while (i--)
        __asm__ volatile("nop");
}

int main() {
    RCC_APB2ENR::value() |= (1u << 4);        /* 打开 GPIOC 时钟            */
    GPIOC_CRH::value() &= ~(0xFu << (4 * 5)); /* PC13 配置位清零            */
    GPIOC_CRH::value() |= (0x2u << (4 * 5));  /* PC13 = 推挽输出 2MHz       */

    for (;;) {
        GPIOC_ODR::value() ^= (1u << 13); /* 翻转 PC13                  */
        delay(200000);
    }
}
