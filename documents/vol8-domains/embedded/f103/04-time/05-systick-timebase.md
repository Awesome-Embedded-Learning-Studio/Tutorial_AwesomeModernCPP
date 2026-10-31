---
title: "一毫秒谁在数：SysTick 时基"
description: "HAL 的 1ms 节拍由 HAL_InitTick 拿 SystemCoreClock 现算重装值，本板 64MHz 下 LOAD=63999、CTRL=7，CLKSOURCE=1 走处理器时钟，翻遍 HAL Src 也没有一处读 SysTick->CALIB。CALIB 的 TENMS 按架构定义是 10ms 重装值，ST 在 F1 装的 9000 按 9MHz 算却是 1ms，泛义对照 10ms@9MHz 该是 90000。Renode 四幕探针 12_systick_probe 开机快照读到 TENMS=640000、NOREF=1，写 CLKSOURCE=0 被顶回读回 7，DDI 0403 的 forced-to-1 规则当场应验；幕 B 拍长 10ms 兑现泛义值，幕 B2 把 9000 原样搬到 64MHz 一拍只剩 0.140625ms，uwTick 快 7.11 倍，HAL_Delay(100) 实际只等约 14ms，幕 C 现算配法还原 1ms。手算四条算式收拢：真铁推演 9000@8MHz=1.125ms 慢 12.5%，时钟频率现算重装值就是正解"
chapter: 4
order: 5
tags:
  - stm32f1
  - intermediate
  - 嵌入式
  - 寄存器
  - 外设管理
difficulty: intermediate
platform: stm32f1
cpp_standard: [20, 23]
reading_time_minutes: 11
prerequisites:
  - "谁打断谁：NVIC 抢占等级怎么排"
related:
  - "把日志用起来：两种写法与两个观看面"
---

# 一毫秒谁在数：SysTick 时基

## 引言：门外路过三回的 SysTick

[《谁打断谁：NVIC 抢占等级怎么排》](04-nvic-preempt-priorities.md)收尾的时候留了话，咱们原样搬过来：

> HAL 的时基里住着 SysTick，HAL_Init 里配的那个 1ms 节拍，HAL_GetTick 与 HAL_Delay 数的都是它。刚拆完的那次翻车里也有它，它被 HAL 排到了最低级，又正好是忙等循环要看的那块表。它还留着一条咱们没用上的线索：RM0008 的 10.1.1 里写着校准值 9000，9MHz 的参考时钟拿它出 1ms。这个数怎么用、1ms 的节拍到底是谁在哪个时钟上配出来的，下一篇咱们拆的就是 SysTick 时基。

SysTick 的全称是系统定时器（System Timer），它是 Cortex-M3 内核自带的一个 24 位递减计数器。它数到了 0 就发一次中断，发完了再从重装值接着数。重装值（reload value）就是计数器数到头重新装载的起点数，咱们后面要跟它打不少交道。这个名字您已经见过三回：00 站的[《为什么是 C++，凭什么？》](../00-env-setup/00-why-cpp.md)里，它是 HAL_Init 配出来的 1ms 节拍的供货方。同站的[《Renode 观测课：没有板子，谁说了算》](../00-env-setup/02-renode-observatory.md)里，它是模型的参考频率，咱们得拿 systickFrequency 这个参数，把它的值对齐到 64MHz。到了 04 篇，它又是被 HAL 排到 0x0F 最低级的那个中断源，拆过的那次翻车里也有它。三回咱们都只从门外路过，它的寄存器一个没拆过。本篇咱们只扛一个问题：1ms 的节拍到底是谁配的，配在了哪口钟上，用的又是哪个数。04 留下的两个问号，9000 这个数怎么用、节拍由谁来配，咱们各给一个带实测的答案。

素材还是 01 到 04 篇用的那套，咱们用的子模块固定在提交 8026841。探针是笔者为这篇新搭的 examples/12_systick_probe，搭在与子模块同一个提交的克隆树上（没收进子模块）。实验咱们照旧在 Renode 里跑，流水线的纪律咱们不重讲。

## 节拍链：1ms 是拿 SystemCoreClock 现算的

链条的头在 kernel.hpp，咱们把开头的两行宏逐字搬出来<RefLink :id="1" preview="Awesome-Embedded-Learning-Studio, libestdx" />：

```c
#define USE_RTOS 0U             /* 不上 RTOS,SysTick 时基归 HAL */
#define TICK_INT_PRIORITY 0x0FU /* SysTick 优先级给最低 */
```

USE_RTOS 填的是 0，SysTick 的时基就归 HAL 管。TICK_INT_PRIORITY 的 0x0F 您在 04 篇拆过了，那是十六级的最后一级。HAL_Init 拿着它调了 HAL_InitTick。这个函数咱们 04 篇没拆过，那边见过的只是它重跑时把抢占级的改动盖回去。真正把数配进硬件的是 HAL_InitTick 的函数体，咱们从 stm32f1xx_hal.c 逐字贴出来<RefLink :id="2" preview="STMicroelectronics, stm32f1xx_hal.c" />：

```c
__weak HAL_StatusTypeDef HAL_InitTick(uint32_t TickPriority)
{
  /* Configure the SysTick to have interrupt in 1ms time basis*/
  if (HAL_SYSTICK_Config(SystemCoreClock / (1000U / uwTickFreq)) > 0U)
  {
    return HAL_ERROR;
  }

  /* Configure the SysTick IRQ priority */
  if (TickPriority < (1UL << __NVIC_PRIO_BITS))
  {
    HAL_NVIC_SetPriority(SysTick_IRQn, TickPriority, 0U);
    uwTickPrio = TickPriority;
  }
  else
  {
    return HAL_ERROR;
  }

  /* Return function status */
  return HAL_OK;
}
```

咱们把第一个 if 里的算式慢慢看。uwTickFreq 在 hal.c 的 L81，类型旁的注释写着 `/* 1KHz */`，它的值是 1，于是 1000U 除完它得到的还是 1000，喂下去的数就是 SystemCoreClock / 1000。中间经手的 HAL_SYSTICK_Config，它的函数体只有一行，咱们原样念：`return SysTick_Config(TicksNumb);`。活儿全转给了 CMSIS 的 SysTick_Config，真正写硬件的是四行，咱们从 core_cm3.h 逐字搬出来<RefLink :id="3" preview="ARM Limited, core_cm3.h" />：

```c
  SysTick->LOAD  = (uint32_t)(ticks - 1UL);                         /* set reload register */
  NVIC_SetPriority (SysTick_IRQn, (1UL << __NVIC_PRIO_BITS) - 1UL); /* set Priority for Systick Interrupt */
  SysTick->VAL   = 0UL;                                             /* Load the SysTick Counter Value */
  SysTick->CTRL  = SysTick_CTRL_CLKSOURCE_Msk |
                   SysTick_CTRL_TICKINT_Msk   |
                   SysTick_CTRL_ENABLE_Msk;                         /* Enable SysTick IRQ and SysTick Timer */
```

第一行写的是重装值，您留意那个减一：计数器从 LOAD 到 0 的一整轮是 LOAD+1 拍，所以想要 64000 拍的时候，写进 LOAD 的就得是 63999。第二行调的就是 04 篇拆过的那个 NVIC_SetPriority，只不过它这次是内嵌在 SysTick_Config 里的，把 SysTick 的抢占级写死在最低的 15。第三行把当前值清成了 0，让它从满装的地方起数。第四行开了三个开关：管启停的 ENABLE，管中断的 TICKINT，还有管选钟的 CLKSOURCE，置 1 选的是处理器时钟。函数的开头还有一道检查，ticks-1 出了 24 位的界就返回 1，HAL_InitTick 的第一个 if 收到的就是它。

数到了 0 之后，是谁在给毫秒数加一？咱们看中断向量那边，examples 的 stm32f1xx_it.c 里管这事的是一个三行的函数：

```c
void SysTick_Handler(void) {
    HAL_IncTick();
}
```

HAL_IncTick 的函数体只有一句 `uwTick += uwTickFreq;`，HAL_GetTick 的函数体是 `return uwTick;`。链条到这里就通了：SysTick 数到了 0 就发中断，收中断的 handler 给 uwTick 加一，咱们调 HAL_GetTick 的时候，拿到手的就是这个计数。tick.hpp 把它包成了 logger 用的钟，咱们连注释一起搬上来<RefLink :id="1" preview="Awesome-Embedded-Learning-Studio, libestdx" />：

```cpp
// Satisfies logger::LogClock. HAL SysTick at 1 kHz (HAL_Init + the
// firmware's SysTick_Handler). uint32 wraps at ~49.7 days: display-only.
struct HalTickClock {
    static std::uint32_t now_ms() { return HAL_GetTick(); }
};
```

答案到这里能立一半了。本板的时钟咱们在 01 篇的 09 示例里配的是 HCLK=64MHz，代进算式得到的正是 64000/64MHz=1ms：HAL 的 1ms 拿 SystemCoreClock 现算，跟 CALIB 是没有关系的。这个说法的根据，笔者是把 HAL Src/ 全目录翻了一遍：stm32f1xx_hal.c 里 SysTick->CALIB 一次都没出现过。目录里带 CALIB 字样的宏还有几处，归 RCC、RTC、ADC 的驱动管，没有一处读的是 SysTick 的校准值。读校准值的活儿，HAL 是从头到尾都没干过的。咱们只按 HAL 的范围说，别的库不在这句话的管辖里。

## 四件寄存器与两口钟

SysTick 在内核的存储器映射里占四个寄存器，CMSIS 的 SysTick_Type 咱们逐字搬：

```c
typedef struct
{
  __IOM uint32_t CTRL;                   /*!< Offset: 0x000 (R/W)  SysTick Control and Status Register */
  __IOM uint32_t LOAD;                   /*!< Offset: 0x004 (R/W)  SysTick Reload Value Register */
  __IOM uint32_t VAL;                    /*!< Offset: 0x008 (R/W)  SysTick Current Value Register */
  __IM  uint32_t CALIB;                  /*!< Offset: 0x00C (R/ )  SysTick Calibration Register */
} SysTick_Type;
```

前三件都是可读可写的，CALIB 却是只读的，注释里那个 R/ 的后面是空的。LOAD 与 VAL 的宽度都是 24 位，上一节那道出界检查查的正是这个上限。CTRL 里有四个位咱们要认：bit0 的 ENABLE，bit1 的 TICKINT，bit2 的 CLKSOURCE，还有 bit16 的 COUNTFLAG。COUNTFLAG 是给轮询数拍用的，咱们走中断，它是用不上的。三个低位都开了的话，读回来的值就是 7，这个数咱们在探针的快照里马上要见到。

CLKSOURCE 管的是选钟，F1 上可选的有两口。RM0008 时钟树那页（页 93）的原句咱们搬上来<RefLink :id="4" preview="STMicroelectronics, RM0008" />：

```text
The RCC feeds the Cortex® System Timer (SysTick) external clock with the AHB clock (HCLK) divided by 8. The SysTick can work either with this clock or with the Cortex® clock (HCLK), configurable in the SysTick Control and Status register.
```

咱们把原句翻成白话：RCC 给 SysTick 的外部钟喂的是 HCLK 的八分频，SysTick 走的可以是这口钟，也可以是处理器自己的 HCLK，选法是写在 CTRL 里的。头一种在架构手册里的名字是外部参考钟，白话讲就是独立于处理器时钟、专门给 SysTick 预备的钟，F1 把它实现成了 HCLK/8。F1 的最大 HCLK 是 72MHz，八分频下来的恰好是 9MHz，9000 那句话的时钟前提就是它。

## 校准值 CALIB：十毫秒的名分，一毫秒的数

CALIB（校准寄存器）是由三个字段拼成的：低 24 位的 TENMS 装的是校准值本身，bit31 的 NOREF 报的是芯片有没有独立参考钟，读出 1 对应的就是没实现，0 对应的才是实现了，bit30 的 SKEW 标记的则是校准值准不准。TENMS 按架构的定义该装什么，咱们去问 DDI 0403（ARM 的 ARMv7-M 架构手册），SYST_CALIB 页的原句是<RefLink :id="5" preview="ARM Limited, DDI 0403" />：

```text
Optionally, holds a reload value to be used for 10ms (100Hz) timing, subject to system clock skew errors. If this field is zero, the calibration value is not known.
```

字段该装什么，这句话说得明白：TENMS 装的是配 10ms（100Hz）用的重装值，字段读到 0 的意思就是厂家没给。ST 在 F1 上装的是什么，RM0008 的 10.1.1（页 197）原句咱们搬上来<RefLink :id="4" preview="STMicroelectronics, RM0008" />：

```text
The SysTick calibration value is set to 9000, which gives a reference time base of 1 ms with the SysTick clock set to 9 MHz (max HCLK/8).
```

咱们把两句并排着看。架构给这个字段派的名分是 10ms 的重装值，ST 装进去的 9000，按 9MHz 算出来的却是 1ms。泛义的对照也好算：10ms@9MHz 对应的该是 90000，ST 装的只有它的十分之一。您要是不知道这层关系，把 9000 当 10ms 的值去用，或者当成放哪都灵的 1ms 去搬，结果就会连钟带数一起配错。

咱们再看 NOREF，连在它身上的还有一条硬规则，DDI 0403 的原句<RefLink :id="5" preview="ARM Limited, DDI 0403" />：

```text
When this bit is 1, the CLKSOURCE bit of the SYST_CSR register is forced to 1 and cannot be cleared to 0.
```

咱们把 CLKSOURCE 自己的定义也搬来，它就在同一份手册的 SYST_CSR 页<RefLink :id="5" preview="ARM Limited, DDI 0403" />：

```text
0 = SysTick uses the implementation defined external reference clock. 1 = SysTick uses the processor clock. If no external clock is provided, this bit reads as 1 and ignores writes.
```

咱们连起来读：芯片没做外部参考钟（NOREF=1）的时候，CLKSOURCE 就被顶在了 1 上，您写 0 进去它不理会，读回来的还是 1。SKEW 咱们本篇只报它的实测值，别的就不展开了。

咱们再换到真铁上看，三个字段该是什么样？咱们按手册推演着走。RM0008 的 RCC 句说明了 F1 供着 HCLK/8 的外部钟，DDI 0403 的定义又写着 NOREF 等于 0 的时候，独立参考钟是实现了的。咱们按定义推下来：真铁的 NOREF 该是 0，TENMS 装的该是 9000，CLKSOURCE 也是该容得下 0 的。不过您得留意措辞：这些说法的属性是推演，读数咱们一个都没有。F1 真铁的 CALIB 读值本篇没有实测过，咱们手头跑的是仿真，下文里真铁侧的说法都停在手册推演的档位上。

## 四幕探针：把 9000 原样搬进 LOAD 会发生什么

探针的量法跟 00 站[《Renode 观测课：没有板子，谁说了算》](../00-env-setup/02-renode-observatory.md)的试验三同宗，拿的是两个独立的钟互相对拍。当参考钟的是 Tim3，TimBase 装配的 100ms 周期拍，抢占级给的是 3，装配的细节 01 篇拆过。它的 handler 只干写一个数组格子的活，写完了就退场，日志全是回了主循环再打的。04 篇的探针在 ISR 里打日志，那是量时序本身的测量特权，这边量的是 SysTick，连特权都是不需要的。参考钟的代码咱们逐字搬出来：

```cpp
// 参考钟: 100ms 一拍, ISR 里只记拍号和当时的 HAL 毫秒数, 不打日志
volatile std::uint32_t beats = 0;
volatile std::uint32_t ring[128];
struct OnGate {
    static void operator()() {
        ring[beats & 127u] = HAL_GetTick();
        beats = beats + 1;
    }
};
```

主循环这边的活儿是等参考钟走满 20 个间隔，合 2000ms 的真值，咱们取的是首尾两个样本的 HAL 毫秒差：

```cpp
// 等参考钟再走 n 拍(n 个样本, n-1 个间隔), 返回首尾样本的毫秒差
// 退出时 beats == b0+n, 写过的槽是 b0 .. b0+n-1
std::uint32_t span_over_beats(std::uint32_t n) {
    std::uint32_t b0 = beats;
    while (beats - b0 < n) {
        __WFI();
    }
    return ring[(b0 + n - 1) & 127u] - ring[b0 & 127u];
}
```

SysTick 的抢占级咱们保持 HAL 默认的 15，给参考钟配的抢占级是 3。04 篇里翻过车的长窗口，这边是没有的，两边的 ISR 干的都是一两件事的小活，15 号也是照常跑的。开机头一件做的事，是把 HAL 配完之后的四个寄存器各读一遍，CALIB 再分头报出它的三个字段，实抓的日志咱们原样贴：

```text
[0ms][Info ][reg] 64000000
[0ms][Info ][reg] 7
[0ms][Info ][reg] 63999
[0ms][Info ][reg] 2148123648
[0ms][Info ][reg] 640000
[0ms][Info ][reg] 1
[0ms][Info ][reg] 0
```

七行咱们挨个念下来：头一行的 64000000 是 SystemCoreClock，第二行的 7 是 CTRL，三个低位全开了。第三行的 63999 是 LOAD，正是 SysTick_Config(64M/1000) 的直接证据。它的来路是两跳：HAL_Init 跑的时候时钟还没配，8MHz 底下写下的 LOAD 是 7999。等到 SystemClock_Config 把时钟配成了 64MHz，HAL_RCC_ClockConfig 的尾段重调 HAL_InitTick，才把 63999 换了进去。时钟重配之后重装值是会自己跟上的，快照里的这个数就是现场证据。第四行的 2148123648 是 CALIB 的原始值，它的十六进制是 0x8009_C400。后三行是分头报的字段：TENMS 字段读到的是 640000，NOREF 的读数是 1，SKEW 的读数是 0。640000 的来路恰好是 64000000/100，模型是严格按泛义的 10ms@64M 给的值，算法上它走的是 systickFrequency/100。这是咱们的推断，行文咱们也按推断来说。

模型给的值跟真铁该有的样子对不上，咱们把两边并排摆开，真铁那一列全是手册推演的口径，您别当读数看：

| | 真铁（手册推演，未实测） | Renode 模型（实测） |
|---|---|---|
| NOREF | 0（RCC 供了 HCLK/8，按定义推） | 1 |
| TENMS | 9000（RM0008 原句） | 640000（=64M/100） |
| TENMS 装进 LOAD 的周期 | @9MHz=1ms，本板 8MHz 下 1.125ms | @64MHz=10ms |
| 写 CLKSOURCE=0 | 按 RCC 句可写，走 8MHz | 写 3 读回 7，被顶回 |

四幕里改的都是 SysTick 自己，参考钟是从头到尾没动的。幕 A 是什么都不改的，量的是 HAL 的现状。幕 B 走的是校准路线，咱们按泛义用法配：CLKSOURCE 写 0 换的是外部参考钟，LOAD 装的是 TENMS。固件里还备了一道兜底：TENMS 为 0 的时候按 9000 上。模型给的值是 640000，兜底也用不到了。配置的四行咱们逐字贴：

```cpp
    SysTick->CTRL = 0;
    SysTick->LOAD = tenms - 1;
    SysTick->VAL = 0;
    SysTick->CTRL = SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk;  // CLKSOURCE=0
```

幕 B2 走的是典型错法，也是幕 B 的对照组：把 RM0008 念成了 9000 就是 1ms，重装值是原样搬的 8999。固件同样把 CLKSOURCE 清成了 0，模型照旧顶了回去，钟还是 64MHz 的处理器钟。配置咱们逐字贴：

```cpp
    SysTick->CTRL = 0;
    SysTick->LOAD = 9000 - 1;
    SysTick->VAL = 0;
    SysTick->CTRL = SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk;
```

幕 C 用一句 `SysTick_Config(SystemCoreClock / 1000u)` 就还原了 HAL 的配法。四幕是连着跑的，日志咱们原样贴，时间戳的零点在 HAL_Init：

```text
[0ms][Info ][ph] A
[2100ms][Info ][d] 2000
[2100ms][Info ][reg] 7
[2100ms][Info ][reg] 639999
[2100ms][Info ][ph] B
[2309ms][Info ][d] 200
[2309ms][Info ][ph] B2
[17243ms][Info ][d] 14223
[17243ms][Info ][ph] C
[19342ms][Info ][d] 2000
[19343ms][Info ][ph] done
```

幕 A 量出来的 d 是 2000：20 个间隔撑起 2000ms 的真值，咱们量出来的差分毫不差，HAL 现状下的滴答周期就是 1.000ms。

幕 B 的日志有两处可看。一处是 B 之前的那两行 reg：CTRL 读回的是 7，咱们写进去的是 3，固件分明把 CLKSOURCE 清成了 0，模型给顶回了 1。NOREF=1 时 forced-to-1 的规则，在模型上是当场应验的。另一处看的是 d=200：LOAD 换成了 640000，钟还是 64MHz 的处理器钟，一拍的长度成了 10.00ms。泛义的值兑现了，它确实是 10ms 的，但钟是没换成的。您再看时间戳：窗口是同样的 2000ms 真值，时间戳却只从 2100 走到了 2309，走的是 209 个单位，uwTick 慢了十倍，日志自己把这事记了下来。

幕 B2 的 d 是 14223。9000 配在 64MHz 上的话，一拍就只剩了 0.140625ms，算下来的理论值是 2000/0.140625=14222.2，采样差的是 1，是对得上的。后果算给您看：uwTick 快了 7.11 倍，HAL_Delay(100) 实际等到的只有约 14ms。报信的还有时间戳，这一幕里它从 2309 走到了 17243。

咱们把四幕的时间戳步幅单拎出来看：每幕的窗口都是同样的 2000ms 真值，第一幕里时间戳从 0 走到了 2100，第二幕只走了 209 个单位，第三幕里它从 2309 一路走到了 17243，第四幕又回到了 2000 上下。时间戳数的正是 uwTick，uwTick 的快慢又是咱们正在测的东西，所以步幅本身就是配置生效的自证，是不靠别的仪器的。

跑完之后笔者把判定脚本 check_probe.py 也跑了两遍：四档断言（A 在 1995 到 2005、B 在 190 到 210、B2 在 14100 到 14400、C 在 1995 到 2005）加上开机快照的三项与相位序列，回回给的都是 PASS<RefLink :id="1" preview="Awesome-Embedded-Learning-Studio, libestdx" />，两次的日志逐字节一致。判定器收尾打的这行咱们原样贴：

```text
systick probe check: PASS (快照 reg/幕A ≈2000/幕B ≈200/幕B2 ≈14222/幕C 还原 ≈2000)
```

## 手算：真铁侧的算式逐步走

四幕量完了，咱们把真铁侧的算式亲手走上一遍，中间的步骤一步都不跳：

```text
9000 的本意   →  9MHz ÷ 9000 = 1ms          (9MHz = 72MHz max HCLK 的八分频)
本板走外部钟  →  64MHz/8 = 8MHz, 9000@8MHz = 1.125ms  (慢 12.5%)
9000 原样搬   →  9000@64MHz = 0.140625ms     (幕 B2 实测互证)
泛义对照      →  10ms@9MHz = 90000           (ST 装的是 9000)
HAL 的配法    →  64000@64MHz = 1ms           (SystemCoreClock 现算,不读 CALIB)
```

算式摆完了，整篇的答案也就能收拢成几句话。在咱们这块 64MHz 的板上，校准路线是出不了准 1ms 的：真铁按手册推演的结果是 1.125ms，慢了 12.5%，模型上的做法更干脆，CLKSOURCE 直接被顶了回去，外部钟是根本挂不上的。HAL 的配法就是正解，它拿的是时钟频率现算的重装值。CALIB 按架构立下的名分，是不用知道时钟频率也能配出的 10ms 基准，ST 在 F1 却把它改成了 1ms@9MHz 的特供值。这层名实不符的地方，就是把数字原样搬走会翻车的根。

## 收尾：时间站正篇五篇走完

咱们写到这里，时间站的正篇算是走完了：从 01 篇的 TIM 家族与消抖，到配置进类型、量程的边界、NVIC 抢占等级，再到本篇的 SysTick 时基，五篇都落了地。咱们回过头看，有一条线就更清楚了：01 篇咱们拿 HalTickClock 当 logger 的钟用，当时说的是不重开战场。本篇总算把战场开了，咱们顺着它包着的 HAL_GetTick，一路拆到了 LOAD 里的那个 63999。00 站的试验三里咱们拿 uwTick 跟虚拟时间对拍，信的是 uwTick。本篇把角色换了过来，信的是 Tim3 的参考拍，受测的成了 uwTick。

还有一件小事要交代给您：uwTick 是个 32 位的无符号整数，回绕一圈的时间约是 49.7 天，tick.hpp 的注释里写着 display-only。[《把日志用起来：两种写法与两个观看面》](../06-patterns/07-logger-dual-syntax-usage.md)里讲过的就是这件事，两条日志的时间戳别直接做减法当间隔，碰上回绕得出来的就是错的。

时间站到这里就告一段落了。PWM 呼吸灯的那一篇，等库侧的素材成熟了咱们再续。下一批咱们回按键站，补那里的续篇。篇名要等落了地再报，这里就不占了。

## 注意事项与常见错误

| 症状 | 原因 | 解法 |
|---|---|---|
| 把 RM0008 的 9000 当重装值原样搬，全板时间快了 7 倍 | 9000 按 9MHz 才是 1ms，配在 64MHz 处理器钟上一拍只有 0.140625ms | 学 HAL 现算：SysTick_Config(SystemCoreClock / 1000) |
| 写 CLKSOURCE=0 想换外部钟，读回却是 7 | Renode 模型 NOREF=1，按 DDI 0403 的 forced-to-1 规则被顶回；真铁按手册推演可写 | 读 CALIB.NOREF 再定路线；模型上按处理器钟配 |
| 拿 CALIB.TENMS 当通用值搬到别的板子 | TENMS 的架构名分是 10ms 重装值，F1 装的 9000 是 9MHz 下的特供，换板换钟都得重算 | 算清钟再选数；拿参考钟对拍验收 |
| 以为 HAL 会自己读校准值兜底 | HAL Src 全目录没有一处读 SysTick->CALIB，重装值只从 SystemCoreClock 现算 | 时钟重配后 HAL_RCC_ClockConfig 末尾会重调 HAL_InitTick，重装值自动跟上；别的库另查 |

## 参考资源

<ReferenceCard title="参考文献">
  <ReferenceItem
    :id="1"
    author="Awesome-Embedded-Learning-Studio"
    title="libestdx（include/libestdx/boards/stm32f1/ 的 tick.hpp 与 hal/kernel.hpp，子模块固定在提交 8026841）"
    :year="2026"
    url="https://github.com/Awesome-Embedded-Learning-Studio/libestdx"
    chapter="HalTickClock、USE_RTOS 与 TICK_INT_PRIORITY; 探针 examples/12_systick_probe 为本篇新增，未进子模块，库本体仍 pin 在 8026841"
  />
  <ReferenceItem
    :id="2"
    author="STMicroelectronics"
    title="stm32f1xx_hal.c 与 stm32f1xx_hal_cortex.c（vendored 源）"
    chapter="HAL_InitTick 函数体、HAL_SYSTICK_Config 的一行转交、HAL_IncTick/HAL_GetTick、uwTickFreq 的 1KHz 默认; 文本冻在 libestdx 子模块 third_party/STM32F1/Drivers/STM32F1xx_HAL_Driver/，两个文件都在 Src/ 子目录"
  />
  <ReferenceItem
    :id="3"
    author="ARM Limited / STMicroelectronics"
    title="CMSIS core_cm3.h（vendored 源）"
    chapter="SysTick_Type 四件寄存器、CTRL 位定义与 SysTick_Config 的四行写入; 文本冻在 libestdx 子模块 third_party/STM32F1/Drivers/CMSIS/"
  />
  <ReferenceItem
    :id="4"
    author="STMicroelectronics"
    title="RM0008 Reference Manual — STM32F101/102/103/105/107（Rev 21）"
    :year="2021"
    url="https://www.st.com/resource/en/reference_manual/rm0008-stm32f101xx-stm32f102xx-stm32f103xx-stm32f105xx-and-stm32f107xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf"
    chapter="10.1.1 SysTick calibration value register (页 197); RCC 章时钟树里给 SysTick 喂 HCLK/8 外部钟的那句 (页 93)"
  />
  <ReferenceItem
    :id="5"
    author="ARM Limited"
    title="ARMv7-M Architecture Reference Manual（DDI 0403）"
    url="https://support.arm.com/documentation/ddi0403/d"
    chapter="SYST_CSR.CLKSOURCE 的定义页; SYST_CALIB 页 TENMS 与 NOREF 的字段定义、forced-to-1 规则"
  />
</ReferenceCard>
