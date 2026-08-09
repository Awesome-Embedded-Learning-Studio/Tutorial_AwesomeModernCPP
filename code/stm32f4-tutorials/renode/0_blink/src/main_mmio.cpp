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

using RCC_AHB1ENR = mmio_reg<std::uint32_t, 0x40023830>;
using GPIOD_MODER = mmio_reg<std::uint32_t, 0x40020C00>;
using GPIOD_ODR = mmio_reg<std::uint32_t, 0x40020C14>;

static void delay(unsigned int n) {
    volatile unsigned int i = n;
    while (i--)
        __asm__ volatile("nop");
}

int main() {
    RCC_AHB1ENR::value() |= (1u << 3);         /* 打开 GPIOD 时钟            */
    GPIOD_MODER::value() &= ~(3u << (2 * 12)); /* PD12 模式清零              */
    GPIOD_MODER::value() |= (1u << (2 * 12));  /* PD12 = 01 通用输出         */

    for (;;) {
        GPIOD_ODR::value() ^= (1u << 12); /* 翻转 PD12                  */
        delay(500000);
    }
}
