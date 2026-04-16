---
title: "第11篇：HAL_GPIO_WritePin与TogglePin —— 让引脚动起来"
description: ""
tags:
  - beginner
  - cpp-modern
  - stm32f1
difficulty: beginner
platform: stm32f1
---
# 第11篇：HAL_GPIO_WritePin与TogglePin —— 让引脚动起来

> 承接上一篇：引脚配置好了，时钟开了，推挽输出就绪。现在就差最后一步——告诉引脚"输出高电平"或"输出低电平"。这就是 `HAL_GPIO_WritePin()` 和 `HAL_GPIO_TogglePin()` 的工作。

---

## 我们的目标

经过前面几篇的努力，GPIOC的时钟已经使能了，PC13也配好了推挽输出模式。引脚现在已经"站好军姿"等待命令了。但我们还没给它下达过任何指令——所以LED到现在还是不亮的。这一篇我们就来解决最后一步：怎么让引脚输出我们想要的电平。

---

## HAL_GPIO_WritePin —— 直接控制引脚电平

这是HAL库提供的最基本的引脚控制函数，我们先看它的完整签名：

```c
void HAL_GPIO_WritePin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin, GPIO_PinState PinState);
```

三个参数我们在前面的文章中都已经见过，现在我们把它们放在一起理解。第一个参数 `GPIO_TypeDef *GPIOx` 是端口指针，告诉HAL你要操作哪个端口——GPIOA、GPIOB还是GPIOC。第二个参数 `uint16_t GPIO_Pin` 是引脚位掩码，指出具体哪个引脚。第三个参数 `GPIO_PinState PinState` 只有两种取值：`GPIO_PIN_SET`（高电平，值为1）和 `GPIO_PIN_RESET`（低电平，值为0）。

对于我们的Blue Pill板载LED（PC13，低电平有效），点亮LED需要输出低电平，熄灭LED需要输出高电平：

```c
// 点亮LED —— PC13输出低电平
HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);

// 熄灭LED —— PC13输出高电平
HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
```

这里要注意一个容易搞混的地方：我们说"点亮LED"对应的是 `GPIO_PIN_RESET`（低电平），而不是直觉上的 `GPIO_PIN_SET`。这是因为Blue Pill的PC13 LED电路是低电平有效的——这在第3篇（推挽、开漏与PC13）中已经详细分析过了。如果你顺手把SET和RESET写反了，LED的行为就会完全反过来——"亮"变成"灭"，"灭"变成"亮"。不过话说回来，这不影响程序运行，只是逻辑上的颠倒。

---

## BSRR寄存器——原子操作的幕后功臣

`HAL_GPIO_WritePin` 的底层实现非常精巧，值得我们深入看一看。它操作的不是ODR（Output Data Register），而是BSRR（Bit Set/Reset Register）。BSRR的设计是ARM Cortex-M系列的一大亮点：

```c
// HAL_GPIO_WritePin 的实现（简化版）
void HAL_GPIO_WritePin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin, GPIO_PinState PinState)
{
    if (PinState != GPIO_PIN_RESET) {
        GPIOx->BSRR = GPIO_Pin;                    // 低16位：设置
    } else {
        GPIOx->BSRR = (uint32_t)GPIO_Pin << 16U;   // 高16位：清除
    }
}
```

BSRR是一个32位只写寄存器，它的设计非常巧妙。低16位（bit0到bit15）用来设置对应的ODR位——写1到bit13就是把ODR的bit13设为1（输出高电平）。高16位（bit16到bit31）用来清除对应的ODR位——写1到bit29（即bit13左移16位）就是把ODR的bit13清为0（输出低电平）。

以PC13为例，`GPIO_PIN_13` 的值是 `0x2000`（第13位为1）。当我们需要输出高电平时，写 `GPIOC->BSRR = 0x2000`，这会设置ODR的第13位为1。当我们需要输出低电平时，写 `GPIOC->BSRR = 0x2000 << 16 = 0x20000000`，这会清除ODR的第13位为0。

为什么不用ODR直接写？因为ODR是16位可读写寄存器，如果用"读-改-写"的方式修改某一个位，在读取和写回之间如果发生了中断，中断处理函数可能也修改了同一个端口的另一位——写回时就会覆盖中断的修改。BSRR通过"写1生效"的设计避免了这个问题：设置和清除是两个独立的位域，写操作是原子的，不需要读-改-写三步。这意味着即使多个中断同时操作同一个端口的不同引脚，也不会互相干扰。

---

## HAL_GPIO_TogglePin —— 翻转引脚电平

有时候我们不需要关心当前电平是什么，只需要把它翻转——高变低、低变高。这时候用 `HAL_GPIO_TogglePin` 更方便：

```c
void HAL_GPIO_TogglePin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin);
```

它只有两个参数——端口和引脚，不需要指定目标电平。底层实现也很直接：

```c
void HAL_GPIO_TogglePin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin)
{
    GPIOx->ODR ^= GPIO_Pin;   // 异或操作翻转对应位
}
```

异或操作（XOR）的特性是：与0异或保持不变，与1异或翻转。所以 `ODR ^= GPIO_PIN_13` 只会翻转ODR的第13位，其他位不受影响。

⚠️ 注意：与BSRR不同，TogglePin的"读-改-写"操作不是原子的。如果在读取ODR和写回之间发生了中断，而中断处理函数也修改了同一个端口的其他引脚，理论上可能会出问题。不过对于LED闪烁这种简单场景，完全不用担心——LED不需要原子性保证。

---

## HAL_Delay —— 时间的来源

LED闪烁需要延时，我们用的是 `HAL_Delay()`：

```c
HAL_Delay(500);   // 延时500毫秒
```

`HAL_Delay` 的实现依赖于SysTick定时器。SysTick是Cortex-M3内核内置的24位递减计数器，它的时钟源是HCLK（在我们的配置中是64MHz）。`HAL_Init()` 会把SysTick配置为每1ms产生一次中断，每次中断时一个名为 `uwTick` 的全局计数器加1。`HAL_Delay()` 就是通过查询这个计数器来判断是否经过了指定的毫秒数。

这就是为什么 `main.cpp` 中必须先调用 `HAL_Init()`——没有它，SysTick没有被配置，`HAL_Delay()` 根本不工作，你的程序会卡在延时函数里永远不出来。

---

## 完整的C风格LED闪烁程序

现在我们把前面所有HAL API组合起来，写一个完整的C风格LED闪烁程序。这是整个系列中"纯HAL方式"的完整展示，也是后续C++重构的起点：

```c
#include "stm32f1xx_hal.h"

/* 时钟配置：HSI -> PLL -> 64MHz */
void SystemClock_Config(void) {
    RCC_OscInitTypeDef osc = {0};
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    osc.HSIState = RCC_HSI_ON;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSI_DIV2;
    osc.PLL.PLLMUL = RCC_PLL_MUL16;
    HAL_RCC_OscConfig(&osc);

    RCC_ClkInitTypeDef clk = {0};
    clk.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                    RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV2;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2);
}

/* LED初始化：使能时钟 + 配置PC13为推挽输出 */
void led_init(void) {
    __HAL_RCC_GPIOC_CLK_ENABLE();

    GPIO_InitTypeDef g = {0};
    g.Pin   = GPIO_PIN_13;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &g);
}

/* LED点亮：PC13输出低电平 */
void led_on(void) {
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
}

/* LED熄灭：PC13输出高电平 */
void led_off(void) {
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
}

int main(void) {
    HAL_Init();
    SystemClock_Config();
    led_init();

    while (1) {
        led_on();
        HAL_Delay(500);
        led_off();
        HAL_Delay(500);
    }
}
```

我们来逐段理解这个程序。首先是 `SystemClock_Config()`，它配置系统时钟到64MHz——HSI（8MHz内部振荡器）经过PLL倍频（/2 × 16 = 64MHz）作为SYSCLK，然后AHB不分频、APB1二分频到32MHz、APB2不分频保持64MHz。这段代码对应我们项目中 `system/clock.cpp` 的 `setup_system_clock()` 方法。

接下来是 `led_init()`，它做了两件事：先调用 `__HAL_RCC_GPIOC_CLK_ENABLE()` 唤醒GPIOC的时钟（这是第4篇讲过的第一大坑），然后把PC13配置为推挽输出、无上下拉、低速。这个函数和我们项目中 `gpio.hpp` 的 `setup()` 方法做的是完全一样的事情。

最后是 `led_on()` 和 `led_off()`，分别调用 `HAL_GPIO_WritePin` 输出低电平和高电平。注意 `led_on()` 传的是 `GPIO_PIN_RESET`（低电平），因为Blue Pill的PC13 LED是低电平有效的。

主函数 `main()` 的逻辑很直接：初始化HAL库和时钟，初始化LED引脚，然后在无限循环中交替点亮和熄灭LED，每次间隔500ms。

---

## 编译和烧录

如果你是跟着env_setup系列一路走来的，编译和烧录应该已经很熟悉了：

```bash
mkdir build && cd build
cmake ..
make
make flash
```

如果你用的是我们项目中的CMakeLists.txt，编译完成后会自动显示固件大小：

```text
   text    data     bss     dec     hex filename
   1234     120       4    1358     54e stm32_demo.elf
```

烧录成功后，你应该看到Blue Pill板上的LED以1秒为周期（500ms亮+500ms灭）稳定闪烁。

如果LED完全没有反应，排查顺序是：第一，确认ST-Link连接正常（SWDIO、SWCLK、GND三线）；第二，确认时钟配置正确（用调试器读RCC_CFGR寄存器）；第三，确认GPIOC时钟已使能（读RCC_APB2ENR的bit4）；第四，确认PC13已配置为输出（读GPIOC_CRH的[23:20]位）。

---

## 我们走到了哪一步

到这里，HAL库的三个核心GPIO API我们都掌握了：`__HAL_RCC_GPIOx_CLK_ENABLE()` 开时钟、`HAL_GPIO_Init()` 配引脚、`HAL_GPIO_WritePin()`/`HAL_GPIO_TogglePin()` 控电平。用这三个API已经足够控制LED闪烁了。

但如果你回头看看上面的代码，会发现一个问题：这段代码跟PC13硬绑定了。`GPIOC`、`GPIO_PIN_13`、`__HAL_RCC_GPIOC_CLK_ENABLE()` 这三个常量分散在三个不同的函数中。如果要把LED换到PA0上，你需要改三个地方——而且必须三个都改对，漏一个就不工作。

下一篇我们就来分析这种C风格写法的问题，看看它是怎么一步步走到"难以维护"的，为后续的C++重构做铺垫。
