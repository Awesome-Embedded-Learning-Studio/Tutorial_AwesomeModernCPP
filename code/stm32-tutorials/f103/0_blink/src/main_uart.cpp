/*
 * 闪灯 + printf 走 USART1(PA9 TX / PA10 RX,115200)。
 * 用途:在 Renode 里 showAnalyzer sysbus.usart1,开一个串口窗口实时看输出,
 *      比"读寄存器看十六进制"直观得多,也更接近真实开发里盯串口的体验。
 * 寄存器(F103):
 *   RCC_APB2ENR 0x40021018:bit2 = GPIOA,bit4 = GPIOC,bit14 = USART1
 *               (F1 的 GPIO 和 USART1 都挂 APB2,不像 F4 分 AHB1/APBx)
 *   GPIOA 基址 0x40010800:CRH@0x04(PA9 位 4-7 复用推挽,PA10 位 8-11 浮空输入)
 *   GPIOC 基址 0x40011000:CRH@0x04(PC13 推挽输出),ODR@0x0C
 *   USART1 基址 0x40013800:SR@0x00(TXE=bit7),DR@0x04,BRR@0x08,CR1@0x0C(UE=bit13,TE=bit3)
 *   波特率:复位后内核跑内部 HSI 8MHz(没开 PLL),USART1 时钟 = 8MHz → BRR = 8M/115200 ≈ 69
 */
#include <cstdint>

template <typename T, std::uintptr_t Addr> struct mmio_reg {
    static volatile T& value() noexcept { return *reinterpret_cast<volatile T*>(Addr); }
};

using RCC_APB2ENR = mmio_reg<std::uint32_t, 0x40021018>;
using GPIOA_CRH = mmio_reg<std::uint32_t, 0x40010804>;
using GPIOC_CRH = mmio_reg<std::uint32_t, 0x40011004>;
using GPIOC_ODR = mmio_reg<std::uint32_t, 0x4001100C>;
using USART1_SR = mmio_reg<std::uint32_t, 0x40013800>;
using USART1_DR = mmio_reg<std::uint32_t, 0x40013804>;
using USART1_BRR = mmio_reg<std::uint32_t, 0x40013808>;
using USART1_CR1 = mmio_reg<std::uint32_t, 0x4001380C>;

static void uart_init() {
    RCC_APB2ENR::value() |= (1u << 2) | (1u << 14); /* GPIOA + USART1 时钟       */
    GPIOA_CRH::value() = (GPIOA_CRH::value() & ~((0xFu << 4) | (0xFu << 8))) |
                         (0xAu << 4)              /* PA9  = 1010:复用推挽 2MHz */
                         | (0x4u << 8);           /* PA10 = 0100:浮空输入      */
    USART1_BRR::value() = 69;                     /* 8MHz / 115200 ≈ 69        */
    USART1_CR1::value() = (1u << 13) | (1u << 3); /* UE | TE                   */
}

static void uart_putc(char c) {
    while (!(USART1_SR::value() & (1u << 7))) { /* 等 TXE */
    }
    USART1_DR::value() = static_cast<std::uint8_t>(c);
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
    uart_puts("\r\n=== F103 blink + UART1 ready ===\r\n");

    RCC_APB2ENR::value() |= (1u << 4);                                        /* GPIOC 时钟 */
    GPIOC_CRH::value() = (GPIOC_CRH::value() & ~(0xFu << 20)) | (0x2u << 20); /* PC13 输出 */

    unsigned int n = 0;
    for (;;) {
        GPIOC_ODR::value() ^= (1u << 13);
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
        delay(200000);
    }
}
