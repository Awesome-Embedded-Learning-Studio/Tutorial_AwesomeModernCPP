---
title: "时钟门控：灯不亮，第一嫌疑人是不供电"
description: "每个外设一条独立时钟闸门，APB2ENR 的 bit4 管 GPIOC 的生死：先看那行开钟代码在反汇编里的三条读改写，再拆 HAL 宏里多出来的一口假读是替谁站岗；然后是本篇重头戏——断钟时写寄存器，手册说不算数、真机不给报错、GDB 强写也白搭，而 Renode 实测发现它的 GPIO 模型压根不模拟门控，模拟器的保真度边界就此现形；最后把 64MHz 时钟树的账算清：HSI 分频倍频、两条 APB 各自的限额——所有读数与指令均真跑可复现"
chapter: 1
order: 2
tags:
  - stm32f1
  - beginner
  - 嵌入式
  - 寄存器
difficulty: beginner
platform: stm32f1
---

# 时钟门控：灯不亮，第一嫌疑人是不供电

上一篇结尾留了个钩子：`main` 里第一行为什么是开时钟，那行不写会是什么光景。这一篇就来结这个案。

先把结论摆在桌面上：**外设不工作，第一嫌疑人永远是时钟没开**。这不是夸大。新手坟场里躺着的"代码完全正确、编译没有警告、运行没有报错、灯就是不亮"的尸体，一多半是被这一行干的。它的可怕之处在于无声——没有任何错误码、任何异常、任何提示，你的写入像投进深井的石头，连个回声都没有。

## 为什么外设默认是睡死的

数字电路靠时钟信号干活。一个触发器什么时候接收输入、什么时候翻转输出，全听时钟拍的节拍；时钟一停，整个模块就是一坨睡死的硅——它在，但它不应。

那 ST 为什么不让所有外设始终有时钟？为了省电。STM32F103C8T6 上躺着几十个外设：五个 GPIO 端口、四五个定时器、三个串口、三个 SPI、两个 I2C、两个 ADC，外加 DMA、USB、CAN。全开的话，哪怕你只用一个脚点灯，没用的外设个个都在耗电；电池供电的设备第一个不答应。ST 的方案是**时钟门控**（clock gating）：每个外设的时钟线路上装一道闸门，软件控制开合。用谁开谁，不用就关着，复位后默认全关。

这些闸门的管理员叫 **RCC**（Reset and Clock Control，复位与时钟控制），住在 `0x40021000`——上一篇反汇编字面量池里那两个地址之一。RCC 手里的活儿三件套：选时钟源、管分频倍频、当总闸门的看门人。今天只动它的闸门。

## APB2ENR：一排电闸

闸门寄存器按总线分家：APB2 总线的外设归 `RCC_APB2ENR`（偏移 `0x18`，地址 `0x40021018`），APB1 的归 `RCC_APB1ENR`。GPIO 全系列挂 APB2，所以咱们的开钟行写的是：

```cpp
RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;  // 给 GPIOC 合闸
```

`IOPCEN` 在设备头文件里的定义（`stm32f103xb.h` L1287-L1289）：

```cpp
#define RCC_APB2ENR_IOPCEN_Pos   (4U)
#define RCC_APB2ENR_IOPCEN_Msk   (0x1UL << RCC_APB2ENR_IOPCEN_Pos)  // 0x00000010
#define RCC_APB2ENR_IOPCEN       RCC_APB2ENR_IOPCEN_Msk             // I/O port C clock enable
```

bit 4。同一排电闸上的邻居们：bit 2 是 GPIOA、bit 3 是 GPIOB、bit 12 是 SPI1、bit 14 是 USART1。上一篇那三条指令的谜底现在完整了——`ldr r3, [r2, #24]`、`orr.w r3, r3, #16`、`str r3, [r2, #24]`，读、改、写，把 bit 4 立起来。咱们的固件跑起来后，Renode 读这个地址，报的正是 `0x00000010`：全寄存器就这一位站着，别的电闸都关着。

## HAL 的开钟宏：多出来的一口假读

HAL 版本的同一件事，是这样一个宏（`stm32f1xx_hal_rcc.h` L517-L523，原文照抄）：

```c
#define __HAL_RCC_GPIOC_CLK_ENABLE()   do { \
                                        __IO uint32_t tmpreg; \
                                        SET_BIT(RCC->APB2ENR, RCC_APB2ENR_IOPCEN);\
                                        /* Delay after an RCC peripheral clock enabling */\
                                        tmpreg = READ_BIT(RCC->APB2ENR, RCC_APB2ENR_IOPCEN);\
                                        UNUSED(tmpreg); \
                                      } while(0U)
```

`SET_BIT` 展开还是那套读改写，跟咱们手写的 `|=` 没有本质区别。有意思的是后头那口"假读"：`tmpreg` 读出来就扔，`UNUSED` 处理掉未使用警告，看着纯属多余。注释自己招了——*Delay after an RCC peripheral clock enabling*。手册要求合闸之后、访问外设寄存器之前，得留出几个时钟周期的缓冲，让时钟信号在外设那边稳定站稳；读一次 APB2ENR 的耗时刚好把这个延迟吃掉。官方库连时序的账都替你算了，这就是"包装纸"里实打实有价值的一层，拆 HAL 那篇咱们再细算总账。

## 断钟现场：手册、真机与模拟器的三种口径

现在正式开庭。断钟时往 GPIOC 的寄存器写值，会发生什么？

**手册的口径**：不算数。外设没有时钟，内部时序逻辑根本无法接收写入，RM0008 对这类行为的规定是明确的——这也是"无声拒绝"的制度依据。

**真机的口径**：写进去，没反应，不报错。最阴的场景是拿调试器排障：你 GDB 连上去，`set *(int*)0x40011004 = 0x00200000` 强写 CRH，回车，读回来一看——还是复位值 `0x44444444`。你以为自己地址错了、语法错了、调试器坏了，折腾半小时，其实是 GPIOC 压根没合闸。咱们在 00 站说过调试器是"开了天眼"，但天眼也怼不过配电房。

**模拟器的口径**：这就出好戏了。咱们在 Renode 里复现这个实验——机器加载固件但停在复位状态（一条用户指令都没跑，APB2ENR 还是 `0x00000000`），然后手工替固件把活干了：

```text
=== EXP-1: machine halted, GPIOC clock is OFF (reset state) ===
--- APB2ENR (expect 0x00000000):
0x00000000
--- manual write 0x33333333 to CRH(0x40011004), then read back:
0x33333333        ← 写进去了？？
--- manual write 0x00002000 to BSRR(0x40011010), ODR(0x4001100C) after:
0x00002000        ← ODR 还真翻了
```

断钟状态下写 CRH，读回 `0x33333333`，写入生效了；写 BSRR，ODR 照翻不误。再手动合上 bit 4 重来一遍（EXP-2），行为一模一样——**Renode 的 STM32F1 GPIO 模型压根不模拟时钟门控**。

这针清醒剂来得正是时候。模拟器是咱们这一卷的主力观测工具，但它是个**行为模型**，不是电路的等价复刻：没实现的细节，它就"看起来能跑"。时钟门控属于电气层的约定，Renode 的 GPIO 模型选择不care，于是真机上死给你看的代码在模拟器里欢快地闪灯。反过来的坑更值钱：**模拟器里跑通的代码，不保证真机买账**——逻辑对不对，Renode 说了算；时序和电气约定，手册说了算。两边的证词都要听，这个习惯从今天养起。

## 电从哪来：64MHz 的账

闸门后面流的电——时钟信号本身——从哪来、几伏几赫，也该看一眼。咱们的 `01_blinky` 里 `SystemClock_Config` 是这么配的（`main.cpp` 节选，注释是原文）：

```cpp
osc.OscillatorType   = RCC_OSCILLATORTYPE_HSI;      // 片内 8MHz RC 振荡器
osc.PLL.PLLSource    = RCC_PLLSOURCE_HSI_DIV2;      // 8M/2 = 4M 进 PLL
osc.PLL.PLLMUL       = RCC_PLL_MUL16;               // 4M × 16 = 64M
clk.SYSCLKSource     = RCC_SYSCLKSOURCE_PLLCLK;     // PLL 输出当系统主钟
clk.AHBCLKDivider    = RCC_SYSCLK_DIV1;             // HCLK  = 64M
clk.APB1CLKDivider   = RCC_HCLK_DIV2;               // APB1  = 32M
clk.APB2CLKDivider   = RCC_HCLK_DIV1;               // APB2  = 64M
```

整条链路画成图：

![咱们项目配置下的简化时钟树：HSI 8MHz 经 2 分频进 PLL 16 倍频得 64MHz，AHB 不分频，APB1 二分频到 32MHz，APB2 直通 64MHz](./02-clock-tree.drawio)

三笔账。第一笔：8MHz 的 HSI（片内 RC 振荡器，不用焊外部晶振）对 Cortex-M3 来说太慢，过一遍 PLL（锁相环，本质是个倍频器）抬到 64MHz——先除 2 再乘 16，`8 / 2 × 16 = 64`，F103 手册上限 72MHz，64 留了安全余量。第二笔：SYSCLK 出来先走 AHB 分频器得 HCLK（CPU 与总线矩阵的节拍），咱们不分频，HCLK = 64MHz。第三笔：HCLK 再分给两条 APB 外设总线——APB2 直通 64MHz，GPIO、USART1、SPI1、TIM1 这些快枪手挂这条线；APB1 除以 2 得 32MHz，因为这条线上的外设（USART2/3、TIM2-4、I2C、SPI2/3）手册限额 36MHz，64 直接怼上去会出事，32 刚好安全。

所以"给 GPIOC 开时钟"开的是：一条从 HSI 出发、经 PLL 倍频、AHB 直通、APB2 分发的 64MHz 节拍信号，进 GPIOC 模块前还有一道只听 RCC 话的闸门。咱们的 `BSRR` 那条单指令写入，就是踩着这个节拍走进触发器的。时钟树的全貌（HSE、CSS、RTC、看门狗这些岔路）以后用到的站再展开，今天这条主干道够用了。

## 您来动手

1. Renode 里加载 `register_led`，`pause` 之后 `sysbus ReadDoubleWord 0x40021018`，看 bit 4 是不是已经立着；
2. 手工把 `0x40021018` 写回 `0x00000000`（拆闸），再用 `sysbus WriteDoubleWord 0x40011010 0x00002000` 写 BSRR，读 ODR——在 Renode 里它照样翻（模型不模拟门控），体会一下"模拟器口径"；
3. 找出 `01_blinky` 的 ELF 里 `__HAL_RCC_GPIOC_CLK_ENABLE` 展开后的指令（`arm-none-eabi-objdump -d` 搜 `HAL_GPIO_Init` 调用点附近），数数比手写 `|=` 多了几条；
4. 想想第 2 步里，同一串操作烧到真 C8T6 上会是什么结局。

## 欸欸！自查一下再走

- 外设时钟默认全关，是为了什么？这个设计省的是谁的电？
- `RCC_APB2ENR` 的 bit 4 对应谁？bit 14 呢？查头文件验证；
- HAL 宏里那口 `tmpreg` 假读，解决的是什么问题？
- "模拟器里跑通了"和"真机能用"之间，隔着哪几类坑？今天这篇占了哪一类？

<ReferenceCard title="参考文献">
  <ReferenceItem
    :id="1"
    author="STMicroelectronics"
    title="RM0008 Reference Manual — STM32F101/102/103/105/107"
    :year="2021"
    url="https://www.st.com/resource/en/reference_manual/rm0008-stm32f101xx-stm32f102xx-stm32f103xx-stm32f105xx-and-stm32f107xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf"
    chapter="6 Reset and clock control (RCC); 6.3.7 APB2 peripheral clock enable register"
  />
  <ReferenceItem
    :id="2"
    author="STMicroelectronics"
    title="stm32f1xx_hal_rcc.h — HAL RCC Driver Header"
    :year="2016"
    url="https://github.com/STMicroelectronics/stm32f1xx_hal_driver/blob/master/Inc/stm32f1xx_hal_rcc.h"
    chapter="L517-L523 __HAL_RCC_GPIOC_CLK_ENABLE"
  />
  <ReferenceItem
    :id="3"
    author="Antmicro"
    title="Renode Documentation — STM32 Family Support"
    :year="2026"
    url="https://renode.readthedocs.io/en/latest/"
    chapter="Platform description; known model limitations"
  />
  <ReferenceItem
    :id="4"
    author="STMicroelectronics"
    title="STM32F103x8/xB Datasheet (DS5319)"
    :year="2015"
    url="https://www.st.com/resource/en/datasheet/stm32f103c8.pdf"
    chapter="5.3.22 Electrical characteristics; maximum frequencies"
  />
</ReferenceCard>
