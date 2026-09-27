---
title: "时钟门控：灯不亮，从时钟查起"
description: "每个外设一条独立时钟闸门，APB2ENR 的 bit4 管 GPIOC 的时钟开关：从那行开钟代码在反汇编里的三条读改写看起，再拆 HAL 宏里多出来的那口假读是干什么用的；然后是本篇正题——时钟没开时写寄存器，手册说不算数、真正的开发板不给报错、GDB 强写也白搭，而 Renode 实测发现它的 GPIO 模型压根不模拟门控，模拟器的保真度边界就此现形；最后把 64MHz 时钟树算清楚：HSI 分频倍频、两条 APB 各自的限额——所有读数与指令均真跑可复现"
chapter: 1
order: 3
tags:
  - stm32f1
  - beginner
  - 嵌入式
  - 寄存器
difficulty: beginner
platform: stm32f1
---

# 时钟门控：灯不亮，从时钟查起

上一篇结尾给您留了个问题：`main` 里开时钟的那一行为什么非写不可，那行不写会是什么光景。这一篇咱们就来把它说清。

**外设不工作了，第一个要查的永远是时钟有没有开**。这真的不是夸大。新手遇到的那类"代码完全正确、编译没有警告、运行没有报错、灯就是不亮"的问题，一多半就是栽在这一行上了，而且栽得无声无息：没有任何错误码、任何异常、任何提示，写进去的值不生效，也没有谁告诉您它没生效。咱们管它叫"无声拒绝"。

## 为什么外设的时钟默认全关

咱们看数字电路这一侧：它干活全靠时钟信号。一个触发器什么时候接收输入、什么时候翻转输出，全跟着时钟的节拍走。一旦时钟停了，整个模块也就停摆了。它其实还在那儿，不过对写入没有任何响应。

那 ST 为什么不让所有外设始终有时钟？为了省电。STM32F103C8T6 里集成了几十个外设：五个 GPIO 端口、四五个定时器、三个串口，SPI、I2C、ADC 又各配了两个，外加的 DMA、USB、CAN。全开的话，哪怕您只用一个脚点灯，没用的外设个个都在耗电。电池供电的设备第一个不答应。ST 的方案是**时钟门控**（clock gating）：每个外设的时钟线路上装一道闸门，闸门的开合由软件控制。用谁开谁的闸，不用的就关着，复位后默认全是关着的。

管这些闸门的模块叫 **RCC**（Reset and Clock Control：复位与时钟控制），它的基址就是 `0x40021000`，上一篇反汇编字面量池里出现过的两个地址之一。RCC 手里的活儿三件：选时钟源、管分频倍频、管各路外设时钟的通断。今天咱们只动它的闸门。

## APB2ENR，好一排电闸

闸门寄存器按总线分开：APB2 总线的外设归 `RCC_APB2ENR`（偏移 `0x18`，地址 `0x40021018`），APB1 的归 `RCC_APB1ENR`。全系列的 GPIO 都挂 APB2，所以咱们给 GPIOC 开钟，动的就是它：

```cpp
RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;  // 给 GPIOC 合闸
```

咱们去设备头文件里找 `IOPCEN` 的定义（`stm32f103xb.h` L1287-L1289）：

```cpp
#define RCC_APB2ENR_IOPCEN_Pos   (4U)
#define RCC_APB2ENR_IOPCEN_Msk   (0x1UL << RCC_APB2ENR_IOPCEN_Pos)  // 0x00000010
#define RCC_APB2ENR_IOPCEN       RCC_APB2ENR_IOPCEN_Msk             // I/O port C clock enable
```

咱们要找的就是 bit 4。同一排电闸上的邻居们：bit 2 是 GPIOA、bit 3 是 GPIOB，而 bit 12 是 SPI1、bit 14 是 USART1。上一篇见过的三条指令，谜底现在凑齐了，干的就是读、改、写：`ldr r3, [r2, #24]`、`orr.w r3, r3, #16`、`str r3, [r2, #24]`，bit 4 立了起来。咱们把固件跑起来后，让 Renode 去读这个地址，报的正是 `0x00000010`：全寄存器就一位站着，别的电闸都关着。

## HAL 的开钟宏：多出来的一口假读

咱们再来看 HAL 怎么做同一件事：靠的是这样一个宏（`stm32f1xx_hal_rcc.h` L517-L523，咱们直接从原文复制过来）：

```c
#define __HAL_RCC_GPIOC_CLK_ENABLE()   do { \
                                        __IO uint32_t tmpreg; \
                                        SET_BIT(RCC->APB2ENR, RCC_APB2ENR_IOPCEN);\
                                        /* Delay after an RCC peripheral clock enabling */\
                                        tmpreg = READ_BIT(RCC->APB2ENR, RCC_APB2ENR_IOPCEN);\
                                        UNUSED(tmpreg); \
                                      } while(0U)
```

`SET_BIT` 展开的还是那套读改写，跟咱们手写的 `|=` 没有本质区别。有意思的是后头那口"假读"：`tmpreg` 读出来就扔，`UNUSED` 处理掉了未使用警告，看着倒是纯属多余。答案就写在宏里的注释上：`Delay after an RCC peripheral clock enabling`。手册的要求是合闸之后隔上几个时钟周期，再去访问外设的寄存器，好让外设那边的时钟信号站稳。读一次 APB2ENR 的耗时，刚好把这个延迟吃掉了。官方库连这个时序缓冲都替您算好了，这就是"包装纸"里实打实有价值的一层，拆 HAL 那篇咱们再细看。

<Anim id="f103-clock-gate-sequence" />

## 关掉时钟做实验：手册、开发板与模拟器的三种答案

现在咱们正式做这个实验：时钟没开时往 GPIOC 的寄存器写值，会发生什么？

咱们挨个听。**手册的答案**：写进去的值不算数。没有时钟的外设，内部时序逻辑根本接收不了写入。ST 的 STM32F1 参考手册对这类行为的规定是明确的，这也是"无声拒绝"在手册里的明文依据。

**开发板的答案**：写进去没反应、不报错。最阴的场景是拿调试器排障：您 GDB 连上去，拿 `set *(int*)0x40011004 = 0x00200000` 把 CRH 强写了，敲了回车，读回来看到的还是复位值 `0x44444444`。您以为自己地址错了、语法错了、调试器坏了，折腾了半小时，其实是 GPIOC 压根没合闸。调试器明明能直接读写这些地址，但 GPIOC 的时钟没开，写进去的照样不算数。

**模拟器的答案**：这就出好戏了。咱们在 Renode 里复现这个实验。机器加载了固件，但停在了复位状态（一条用户指令都没跑，APB2ENR 还是复位时的 `0x00000000`），然后咱们手工替固件把活干了：

```text
=== EXP-1: machine halted, GPIOC clock is OFF (reset state) ===
--- APB2ENR (expect 0x00000000):
0x00000000
--- manual write 0x33333333 to CRH(0x40011004), then read back:
0x33333333        ← 写进去了？？
--- manual write 0x00002000 to BSRR(0x40011010), ODR(0x4001100C) after:
0x00002000        ← ODR 还真翻了
```

时钟关着的状态下，咱们写 CRH，读回来的正是 `0x33333333`，写入生效了。写 BSRR 的时候，ODR 倒是照翻不误。咱们再手动合上 bit 4 重来一遍，行为真的一模一样，**Renode 的 STM32F1 GPIO 模型，其实压根不模拟时钟门控**。

这个偏差来得倒是时候，给咱们提了个醒。模拟器是咱们这一卷的主力观测工具，但它其实是个**行为模型**，不是电路的等价复刻：没实现的细节，它就一副"看起来能跑"的样子。时钟门控属于电气层的约定，Renode 的 GPIO 模型选择不管这一层，于是在开发板上直接挂死的代码，居然在模拟器里欢快地闪灯。反过来还有更值钱的一条：**模拟器里跑通的代码，不保证到了真正的开发板上也认**。逻辑上的对错，Renode 说了算。时序和电气上的约定，手册说了算。两边的说法都要听，这个习惯咱们从今天养起。

## 电从哪来：64MHz 怎么算出来的

闸门后面流的电也就是时钟信号本身，从哪来的、几伏几赫，咱们也该看一眼。`01_blinky` 里 `SystemClock_Config` 是这么配的（`main.cpp` 节选，五条注释是工程里的原文，另两条是笔者补的说明）：

```cpp
osc.OscillatorType   = RCC_OSCILLATORTYPE_HSI;      // 片内 8MHz RC 振荡器
osc.PLL.PLLSource    = RCC_PLLSOURCE_HSI_DIV2;      // 8M/2 = 4M 进 PLL
osc.PLL.PLLMUL       = RCC_PLL_MUL16;               // 4M × 16 = 64M
clk.SYSCLKSource     = RCC_SYSCLKSOURCE_PLLCLK;     // PLL 输出当系统主钟
clk.AHBCLKDivider    = RCC_SYSCLK_DIV1;             // HCLK  = 64M
clk.APB1CLKDivider   = RCC_HCLK_DIV2;               // APB1  = 32M
clk.APB2CLKDivider   = RCC_HCLK_DIV1;               // APB2  = 64M
```

咱们把整条链路画成图：

![咱们项目配置下的简化时钟树：HSI 8MHz 经 2 分频进 PLL 16 倍频得 64MHz，AHB 不分频，APB1 二分频到 32MHz，APB2 直通 64MHz](./03-clock-tree.drawio)

咱们分三步算。

第一步：8MHz 的 HSI（片内 RC 振荡器，不用焊外部晶振）对 Cortex-M3 来说太慢了，咱们得靠 PLL（锁相环：本质是个倍频器）把它抬到 64MHz。怎么抬？除 2 再乘 16，`8 / 2 × 16 = 64`，F103 手册的上限是 72MHz，64 留了安全余量。

第二步：SYSCLK 出来了，走 AHB 分频器得 HCLK（CPU 与总线矩阵的节拍），咱们不分频、HCLK = 64MHz。

第三步：咱们把 HCLK 再分给两条 APB 外设总线。而 APB2 直通 64MHz，GPIO、USART1、SPI1、TIM1 这些要跑高速的，全挂在它上面了。APB1 除以 2 得到的是 32MHz，因为 USART2/3、TIM2-4、I2C、SPI2/3 这些 APB1 上的外设，手册的限额是 36MHz，64 直接怼上去是要出事的，32 刚好是安全的。

所以咱们"给 GPIOC 开时钟"，开的其实是一条从 HSI 出发、经 PLL 倍频，一路 AHB 直通、APB2 分发的 64MHz 节拍信号，进 GPIOC 模块前还有一道只听 RCC 话的闸门。咱们上一篇写下的 `BSRR` 单指令写入，就是踩着这个节拍走进触发器的。时钟树的全貌（HSE、CSS、RTC、看门狗这些分支）以后用到的站再展开，今天讲的链路，够咱们用了。

## 欸欸！自查一下再走

- 外设时钟默认全关，是为了什么？您觉得这个设计省的是谁的电？
- `RCC_APB2ENR` 的 bit 4 对应谁？bit 14 呢？请您查头文件验证。
- HAL 宏里那口 `tmpreg` 假读，您还记得它解决的是什么问题吗？
- "模拟器里跑通了"和"开发板上能用"之间，还隔着哪几类差异？今天这篇咱们占了哪一类？

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
