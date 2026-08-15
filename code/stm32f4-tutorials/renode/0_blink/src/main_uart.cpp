/*
 * 闪灯 + printf 走 UART2(PA2,AF7,115200)。
 * 用途:在 Renode 里 showAnalyzer sysbus.usart2,开一个串口窗口实时看输出,
 *      比"读寄存器看十六进制"直观得多,也更接近真实开发里盯串口的体验。
 * 寄存器(F407):
 *   RCC_AHB1ENR 0x40023830 bit0  = GPIOA 时钟
 *   RCC_APB1ENR 0x40023840 bit17 = USART2 时钟
 *   GPIOA 基址 0x40020000:MODER@0x00(PA2 位 4-5 → AF),AFRL@0x20(PA2 位 8-11 → AF7)
 *   USART2 基址 0x40004400:SR@0x00(TXE=bit7),DR@0x04,CR1@0x0C(UE=bit13,TE=bit3),BRR@0x08
 */
#include <cstdint>

template <typename T, std::uintptr_t Addr> struct mmio_reg {
    static volatile T& value() noexcept { return *reinterpret_cast<volatile T*>(Addr); }
};

using RCC_AHB1ENR = mmio_reg<std::uint32_t, 0x40023830>;
using RCC_APB1ENR = mmio_reg<std::uint32_t, 0x40023840>;
using GPIOA_MODER = mmio_reg<std::uint32_t, 0x40020000>;
using GPIOA_AFRL = mmio_reg<std::uint32_t, 0x40020020>;
using GPIOD_MODER = mmio_reg<std::uint32_t, 0x40020C00>;
using GPIOD_ODR = mmio_reg<std::uint32_t, 0x40020C14>;
using USART2_SR = mmio_reg<std::uint32_t, 0x40004400>;
using USART2_DR = mmio_reg<std::uint32_t, 0x40004404>;
using USART2_BRR = mmio_reg<std::uint32_t, 0x40004408>;
using USART2_CR1 = mmio_reg<std::uint32_t, 0x4000440C>;

static void uart_init() {
    RCC_AHB1ENR::value() |= (1u << 0);  /* GPIOA 时钟            */
    RCC_APB1ENR::value() |= (1u << 17); /* USART2 时钟           */
    GPIOA_MODER::value() = (GPIOA_MODER::value() & ~(3u << 4)) | (2u << 4); /* PA2 = AF */
    GPIOA_AFRL::value() = (GPIOA_AFRL::value() & ~(0xFu << 8)) | (7u << 8); /* AF7=USART2 */
    USART2_BRR::value() = 0x111;                  /* ~115200(模拟器里只要有个值) */
    USART2_CR1::value() = (1u << 13) | (1u << 3); /* UE | TE   */
}

static void uart_putc(char c) {
    while (!(USART2_SR::value() & (1u << 7))) { /* 等 TXE */
    }
    USART2_DR::value() = static_cast<std::uint8_t>(c);
}

static void uart_puts(const char* s) {
    while (*s)
        uart_putc(*s++);
}

static void delay(unsigned int n) {
    volatile unsigned int i = n;
    while (i--)
        __asm__ volatile("nop");
}

int main() {
    uart_init();
    uart_puts("\r\n=== F407 blink + UART2 ready ===\r\n");

    RCC_AHB1ENR::value() |= (1u << 3);                                        /* GPIOD 时钟 */
    GPIOD_MODER::value() = (GPIOD_MODER::value() & ~(3u << 24)) | (1u << 24); /* PD12 输出 */

    unsigned int n = 0;
    for (;;) {
        GPIOD_ODR::value() ^= (1u << 12);
        uart_puts("toggle #");
        /* 简陋十进制打印,够 demo 用 */
        char buf[11];
        unsigned int v = ++n, i = 10;
        buf[i] = '\0';
        do {
            buf[--i] = '0' + (v % 10);
            v /= 10;
        } while (v);
        uart_puts(&buf[i]);
        uart_puts("\r\n");
        delay(3000000);
    }
}
