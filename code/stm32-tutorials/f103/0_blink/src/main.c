/*
 * F103 闪灯:翻转 PC13(Blue Pill 板载 LED,低电平点亮)。
 *
 * 这是第一幕的起点:裸机寄存器写法。后续会一步步重构为类型安全寄存器封装 →
 * 位域 → 模板 → C++23,每一步都用 Renode 验证零开销。
 *
 * 寄存器地址(F1 布局,与 F4 不同):
 *   RCC    基址 0x40021000,  APB2ENR  偏移 0x18 → IOPCEN = bit4
 *   (F1 的 GPIO 挂在 APB2 总线上,不是 F4 的 AHB1)
 *   GPIOC  基址 0x40011000,  CRH      偏移 0x04(引脚 8-15,每引脚 4bit:CNF+MODE)
 *                          ODR      偏移 0x0C
 */
#define RCC_APB2ENR (*(volatile unsigned int*)0x40021018)
#define GPIOC_CRH (*(volatile unsigned int*)0x40011004)
#define GPIOC_ODR (*(volatile unsigned int*)0x4001100C)

static void delay(unsigned int n) {
    volatile unsigned int i = n;
    while (i--)
        __asm__ volatile("nop");
}

int main(void) {
    RCC_APB2ENR |= (1u << 4);        /* 打开 GPIOC 时钟                  */
    GPIOC_CRH &= ~(0xFu << (4 * 5)); /* PC13 配置位清零(CRH 里第 5 组)  */
    GPIOC_CRH |= (0x2u << (4 * 5));  /* PC13 = 0010:推挽输出 2MHz       */

    for (;;) {
        GPIOC_ODR ^= (1u << 13); /* 翻转 PC13 → LED 反着跟着闪        */
        delay(200000);
    }
}
