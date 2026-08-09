/*
 * F407 闪灯:翻转 PD12(STM32F4 Discovery 板的 UserLED)。
 *
 * 这是重建后第一条流水线的起点:裸机寄存器写法。后续会一步步重构为
 * 类型安全寄存器封装 → 模板 → C++23,每一步都用 Renode 验证零开销。
 *
 * 寄存器地址(F4 布局,与 F1 不同):
 *   RCC    基址 0x40023800,  AHB1ENR  偏移 0x30  → GPIODEN = bit3
 *   GPIOD  基址 0x40020C00,  MODER    偏移 0x00
 *                          ODR      偏移 0x14
 */
#define RCC_AHB1ENR (*(volatile unsigned int*)0x40023830)
#define GPIOD_MODER (*(volatile unsigned int*)0x40020C00)
#define GPIOD_ODR (*(volatile unsigned int*)0x40020C14)

static void delay(unsigned int n) {
    volatile unsigned int i = n;
    while (i--)
        __asm__ volatile("nop");
}

int main(void) {
    RCC_AHB1ENR |= (1u << 3);         /* 打开 GPIOD 时钟                */
    GPIOD_MODER &= ~(3u << (2 * 12)); /* PD12 模式清零                  */
    GPIOD_MODER |= (1u << (2 * 12));  /* PD12 = 01 通用输出             */

    for (;;) {
        GPIOD_ODR ^= (1u << 12); /* 翻转 PD12 → UserLED 跟着翻     */
        delay(500000);
    }
}
