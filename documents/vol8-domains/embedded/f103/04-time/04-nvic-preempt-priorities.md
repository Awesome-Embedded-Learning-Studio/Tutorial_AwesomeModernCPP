---
title: "谁打断谁：NVIC 抢占等级怎么排"
description: "tim.hpp 的 TimBase 与 TimOneShot 走 CMSIS 单参 NVIC_SetPriority，exti.hpp 与 uart_irq.hpp 走 HAL 双参且子优先级恒 0，同一块固件里两条路线共存，库取态 16 级全给抢占。RM0008 只实现 4 位共 16 级，出界的 16 经 (16<<4)&0xFF 回卷成最高级 0。Renode 三幕探针 11_prio_probe 贴出高打低嵌套、同级不嵌套加挂起追发、IRQ 编号决序、16 写入读回 0 四段日志。分组阶梯照 NVIC_EncodePriority 源码逐组复算，复位默认 PRIGROUP=0 下两条路线写出的 IP 相同，改了分组就岔开。SysTick 被 HAL 排最低 0x0F，ISR 里忙等节拍就是自锁，修法是时钟重配后 NVIC_SetPriority(SysTick_IRQn, 0U)"
chapter: 4
order: 4
tags:
  - stm32f1
  - intermediate
  - 嵌入式
  - 寄存器
  - 外设管理
difficulty: intermediate
platform: stm32f1
cpp_standard: [20, 23]
reading_time_minutes: 12
prerequisites:
  - "TIM 家族与确认式消抖：从 1Hz 心跳到 20ms 确认窗"
  - "量程的边界：16 位乘 16 位能配多长"
related:
  - "把日志用起来：两种写法与两个观看面"
---

# 谁打断谁：NVIC 抢占等级怎么排

## 引言：咱们照着填的那些数字

[《量程的边界：16 位乘 16 位能配多长》](03-period-range-boundary.md)收尾的时候留了话，咱们原样搬过来：

> 多个定时器都要打断 CPU 的那一刻，谁排在前头、谁的中断又得让路，下一篇咱们拆解的就是 NVIC 的抢占等级。01 篇里咱们其实已经用过它，09 的 `init(3)`、10 号示例里 Touch=2、Window=3、SlowTick=4 的排法，当时咱们只是照着填，它的规则咱们还没拆过。

NVIC 的全称是嵌套向量中断控制器（Nested Vectored Interrupt Controller），也就是 Cortex-M3 内核里管中断排队与放行的那块电路。抢占等级就是您填给 `init(3)` 的那个 3，数值越小的中断越有资格打断别人。本篇咱们只扛一个问题：这个数字是怎么决定谁打断谁的，填成同级的时候硬件怎么排，填出界了硬件又怎么接。

素材还是 01 到 03 篇同源的那套，咱们用的子模块固定在提交 8026841。实验咱们照旧在 Renode 里跑，流水线的纪律咱们不重讲。

## 同一块固件里的两条路线

TimBase 的 init 落到 NVIC 上的两行，咱们从 tim.hpp 里逐字搬出来：

```cpp
        NVIC_SetPriority(HW::irq_number(), preempt);
        NVIC_EnableIRQ(HW::irq_number());
```

`NVIC_SetPriority` 是 CMSIS（ARM 给 Cortex-M 内核写的寄存器访问层，Cortex Microcontroller Software Interface Standard）的函数，参数表里收的只有 IRQ 号和级别。TimOneShot 的 init 是同款两行，咱们不重贴。

exti.hpp 与 uart_irq.hpp 走的又是另一条路，咱们也贴出来：

```cpp
HAL_NVIC_SetPriority(irq, preempt, 0);
HAL_NVIC_SetPriority(irq_number(), preempt, 0);
```

上一行是 exti.hpp 的写法，下一行是 uart_irq.hpp 的。HAL 的函数多收了一个子级别参数，两条路都把它填成了 0。这个参数管什么用，第五节拆分组的时候咱们再说清。

10 号示例里两条路线住的是同一块固件：Touch 用的 Exti 走 HAL 路线，Window 用的 TimOneShot 与 SlowTick 用的 TimBase 走 CMSIS 路线，抢占等级排的是 2、3、4。库的取态在源码里看得很直白：16 级全给了抢占，子级别是不用的，`HAL_NVIC_SetPriorityGrouping` 咱们一次也没见它被调过。不过这个取态成立的根据，还有一件咱们没查的事：复位之后分组寄存器里放的是什么。这件事咱们也留到第五节，数字是怎么写进硬件的，咱们接着往下看。

## 写进硬件的只有高四位

CMSIS 的 `NVIC_SetPriority` 最底下真正干活的那行写入，咱们从 core_cm3.h 里逐字搬出来：

```c
    NVIC->IP[((uint32_t)IRQn)]               = (uint8_t)((priority << (8U - __NVIC_PRIO_BITS)) & (uint32_t)0xFFUL);
```

IP 寄存器（Interrupt Priority registers）是 NVIC 里给每个中断源配的级别槽位，一个中断源占一个字节的位置。负的 IRQn 是系统异常的编号，SysTick 的编号是 −1，它们走的就是 `SCB->SHP` 那个分支，机制跟上面的相同，咱们提一句就过。

移多少位是由 `__NVIC_PRIO_BITS` 决定的，它在 F1 的器件头文件 stm32f103xb.h 里是这么写的，咱们看<RefLink :id="4" preview="ARM Limited, stm32f103xb.h" />：

```c
#define __NVIC_PRIO_BITS           4U       /*!< STM32 uses 4 Bits for the Priority Levels    */
```

咱们把它代进写入行，算式就成了 `(priority << 4) & 0xFF`：您填的数左移四位，落进那个字节的最高四位。讲 NVIC 的第 10 章在 RM0008 的页 197，特性页的原句是<RefLink :id="1" preview="STMicroelectronics, RM0008" />：

```text
16 programmable priority levels (4 bits of interrupt priority are used)
```

四个位撑起的是十六级，数值越小的中断越有资格打断别人。咱们代两个数进去看：`preempt=2` 的时候，`2<<4` 给出的是 `0x20`。`preempt=15` 的时候，得到的就是 `0xF0`。那 `preempt=16` 呢？这个数咱们放到第五节单独拆。

RM0008 的 10.1.2（页 198）是向量表。外设中断那一栏的 Priority 列写着 settable，也就是您在 IP 寄存器里配的那些。核心异常的那一栏写的是负数、fixed，级别在架构里定死了，IP 寄存器是够不着的。TIM2 的位置是 28，表里给的地址是 0x0000_00B0，TIM3 的位置是 29，TIM4 的位置是 30。器件头文件里的 `TIM2_IRQn = 28`，跟向量表的 28 是同一个 28。这个编号眼下对咱们还只是身份信息，到下一节它会决定同时挂起的时候谁排在头一个。

## 三幕探针：嵌套、挂起与出界

探针是笔者为这一篇新搭的 examples/11_prio_probe，搭在与子模块同一个提交 8026841 的克隆树上，咱们没把它收进子模块。跑的还是 01 篇用的 Renode 流水线。您在下面的代码里会看到 handler 直接调了 Log::info，01 篇沿用的老边界是中断里别调它，ISR 里动的只有标志，打日志的活回主循环，那是给工程代码立的。本实验量的就是毫秒级时序本身，时间戳必须在事情发生的现场打，存了标志、回了主循环再打的话，打出来的就是排空时刻不是事发时刻，三幕的对照就散了。logger 的发送是有界等待，最坏的情况丢一行，机器是锁不死的。日志时间戳的零点在 HAL_Init，咱们只看相对间隔。固件的形状：Tim3 的周期是 100ms、抢占级 3，它的中断服务进门打 `T3+`，等 40ms 制造一个可被打断的窗口，出门的时候打 `T3-`。Tim2 的周期是 25ms，咱们在三幕里改它的抢占级，它的 handler 进门只打一条 `T2`。咱们把 Tim3 的 handler 逐字贴在下面。等待用的是 `WFI`（Wait For Interrupt，让内核睡到下一个中断来的指令），靠的是 1ms 的 SysTick 节拍醒来看表：

```cpp
// 慢拍: 进 ISR 先报到, 等 40ms 制造可被打断的窗口, 再退场。
// 等,用的是 WFI(靠 1ms 的 SysTick 节拍醒来看表), 不用空转——
// Renode 里空转会按指令数慢慢磨, WFI 能让虚拟时间直接跳到下一个中断
struct OnSlow {
    static void operator()() {
        Log::info(estdx::logger::Tag("ev"), "T3+");
        std::uint32_t t0 = HAL_GetTick();
        while (HAL_GetTick() - t0 < 40) {
            __WFI();
        }
        Log::info(estdx::logger::Tag("ev"), "T3-");
    }
};
```

main 里初始化的关键段是幕 A 的底子，SysTick 开头那三行注释咱们留到后面拆翻车的时候细说：

```cpp
    HAL_Init();
    SystemClock_Config();
    // 库把 SysTick 排在 TICK_INT_PRIORITY(15, 最低)。本探针要驱动 ISR 忙等,
    // 得把节拍提到最高级(放在时钟重配之后, 否则会被 HAL_InitTick 盖回去):
    // 否则忙等里等 HAL_GetTick 会把自己锁死, 被 Tim3 压住的 SysTick 永远走不了
    NVIC_SetPriority(SysTick_IRQn, 0U);
    Serial::init();
    Slow::init(3);  // 低抢占级(数值大)
    Fast::init(1);  // 高抢占级(数值小)
```

幕 B 的拉平靠的是运行时的 `NVIC_SetPriority(TIM2_IRQn, 3)`，咱们不用贴它的调用行。决序与幕 C 用的是这一段：

```cpp
    // 关中断窗口里同时挂起两个更新事件, 开闸瞬间两个同级 pending,
    // 先被服务的是 IRQ 编号小的 TIM2(28), 编号大的 TIM3(29) 排后面
    __disable_irq();
    TIM3->SR = TIM_SR_UIF;
    TIM2->SR = TIM_SR_UIF;
    Log::info(estdx::logger::Tag("ph"), "arm");
    __enable_irq();
    wait_for(500);

    NVIC_SetPriority(TIM2_IRQn, 16);  // 出界: 高 4 位以外全被移走
    Log::info(estdx::logger::Tag("ph"), "C");
    Log::info(estdx::logger::Tag("ph"), NVIC_GetPriority(TIM2_IRQn));
```

幕 A 里咱们给 Tim2 配的抢占级是 1，Tim3 拿的是 3，数值小的那个管打断：

```text
[100ms][Info ][ev] T3+
[125ms][Info ][ev] T2
[140ms][Info ][ev] T3-
```

咱们把三条日志的间隔念出来：T3+ 在 100ms 到 T3- 在 140ms 的窗口里，中间的 125ms 插进来一条 T2。硬件此刻做的事咱们也能说清：CPU 正在 Tim3 的 handler 里等着，抢占级更高的 Tim2 到期，NVIC 就直接把 CPU 从 handler 里借走了，跑完 T2 才把控制权还了回去。这就是咱们说的嵌套，01 篇咱们填数字的时候，靠的正是它。

幕 B 咱们用运行时的 `NVIC_SetPriority(TIM2_IRQn, 3)` 把 Tim2 拉平成同级 3，拉平之后的样子是：

```text
[2400ms][Info ][ev] T3+
[2440ms][Info ][ev] T3-
[2440ms][Info ][ev] T2
```

咱们看窗口里 2400 到 2440 之间，没有 T2 的日志。25ms 网格上那一拍其实在 2425ms 就到期了，但是同级之间不嵌套，T2 到期就被记进了挂起位（pending：中断已经发生、排进队里等 CPU 的状态，硬件给每个中断源都留了一个挂起位）。2440ms Tim3 的 handler 一出门，这笔挂起就立刻被放了行，所以 T3- 和追发的 T2 打的是同一个毫秒戳。挂起的请求不会丢，只是被推迟了。

决序的那段在幕 B 的尾巴上，就是代码里 `__disable_irq` 到 `__enable_irq` 之间的那个窗口。咱们在关中断的空当里把 TIM3、TIM2 的更新标志都置上，补了一条 `arm` 日志，然后才开了闸。开闸的一瞬间两个同级中断同时挂起，谁走在头一个？

```text
[2440ms][Info ][ph] arm
[2450ms][Info ][ev] T2
[2475ms][Info ][ev] T2
[2500ms][Info ][ev] T3+
```

`arm` 之后头一个被服务的是 IRQ 编号 28 的 TIM2：手动挂起的那笔 2450ms 被服务，网格上真实到期的那笔 2475ms 又跑了一遍，TIM2 连着被服务了两次，2500ms 才轮上编号 29 的 TIM3。同级同时挂起的时候，编号小的走在前面。连开闸之后真实到期的 T2，也照样插在了 TIM3 的前面。这不是 Renode 的巧合，ST 在 hal_cortex.c 开头的注释里把决序规则写成了三层，咱们把原句贴出来：

```text
     -@- IRQ priority order (sorted by highest to lowest priority):
        (+@) Lowest preemption priority
        (+@) Lowest sub priority
        (+@) Lowest hardware priority (IRQ number)
```

头一层管的是嵌套，能不能打断看的就是抢占级。头一层平了，接手的就是子级别，它排的是同时挂起的中断谁在前面。再往下连子级别也平了的时候，IRQ 编号说了算。探针里抢占级拉平了，库又把子级别恒填成了 0，于是定夺的差事落在了第三层的编号上。还有一句要提醒您：子级别管的只是同时挂起时的次序，它产生不了嵌套，想让别的中断打断您，能改的只有抢占级。

幕 C 轮到的是出界。咱们把 `NVIC_SetPriority(TIM2_IRQn, 16)` 写进去，紧接着又把级别读了回来：

```text
[2940ms][Info ][ph] C
[2940ms][Info ][ph] 0
[3000ms][Info ][ev] T3+
[3025ms][Info ][ev] T2
[3040ms][Info ][ev] T3-
```

`C` 之后那个 `0` 就是 `NVIC_GetPriority(TIM2_IRQn)` 的读回：咱们写进去的是 16，读回来的却是 0。行为也跟着变了，3025ms 的 T2 又插回 T3 的窗口里了，嵌套恢复了，跟幕 A 的样子又对上了。16 变 0 的算式，咱们在下一节一步步走。

跑完之后笔者把判定脚本 check_probe.py 也跑了两遍，五组断言把每一段日志都查到了，回回给的都是 PASS<RefLink :id="2" preview="Awesome-Embedded-Learning-Studio, libestdx" />：

```text
prio probe check: PASS (高打低嵌套 / 同级不嵌套+追发 / IRQ号决序 / 16回卷成0并恢复嵌套)
```

## 出界的 16 与分组的切法

回卷的算式，咱们把 core_cm3.h 那行代入数字慢慢走。咱们已经知道 `__NVIC_PRIO_BITS` 的值是 4，把它代进了写入行，算式也就成了 `(priority << 4) & 0xFF`：

```text
preempt=2   →  2<<4   = 0x20
preempt=15  →  15<<4  = 0xF0
preempt=16  →  16<<4  = 0x100  →  &0xFF  →  0x00
```

0x00 是数值最小的级别，而数值最小就是最高级。写 16 的效果等同于写 0，本想让 Tim2 当最低的，结果成了最高的。咱们在幕 C 里算是亲手验过了：CMSIS 这边是没有 bounds 检查的，读回的 0 与恢复的嵌套就是它跑出来的证据。

HAL 双参的那三步咱们再看一遍，源码咱们逐字贴在下面（stm32f1xx_hal_cortex.c）：

```c
void HAL_NVIC_SetPriority(IRQn_Type IRQn, uint32_t PreemptPriority, uint32_t SubPriority)
{
  uint32_t prioritygroup = 0x00U;

  /* Check the parameters */
  assert_param(IS_NVIC_SUB_PRIORITY(SubPriority));
  assert_param(IS_NVIC_PREEMPTION_PRIORITY(PreemptPriority));

  prioritygroup = NVIC_GetPriorityGrouping();

  NVIC_SetPriority(IRQn, NVIC_EncodePriority(prioritygroup, PreemptPriority, SubPriority));
}
```

三步的次序值得咱们念一遍。头一步是 `NVIC_GetPriorityGrouping()` 从 AIRCR（SCB 里的应用中断与复位控制寄存器，分组的 PRIGROUP 字段就在它里面）把运行时的分组实况读回来。第二件的活儿归 `NVIC_EncodePriority`，它按这个分组的规则，把 preempt 与 sub 两个数拼成 4 位的 IP 值。第三步还是落回 CMSIS 的 `NVIC_SetPriority`。两条路线到最底层就汇合了，差别的只是拼不拼。至于开头的两个 assert_param 在默认档下都是空操作，02 篇贴过它的两档展开，出界检查在这里是指望不上的。

分组这个词咱们到现在还没拆过。它拆的就是那 4 个级别位，Cortex-M3 允许咱们在运行时把它们切成两段。前一段当的是抢占位，决定的正是谁打断谁。后一段当的是子级别，只在抢占位相同的时候给同时挂起的中断排次序，它本身产生不了嵌套。切法由 AIRCR 的 PRIGROUP 字段定，HAL 给常用的切法起了宏名，宏值就是 PRIGROUP 的原值。`NVIC_PRIORITYGROUP_4` 的宏值是 0x00000003U，管的是 4 位抢占加 0 位子。`NVIC_PRIORITYGROUP_3` 的宏值是 0x00000004U，切的是 3 位抢占加 1 位子。`NVIC_PRIORITYGROUP_0` 的宏值是 0x00000007U，4 个位全给了子级别。

同一个数在各分组下落进 IP 的模样，咱们照着 `NVIC_EncodePriority` 的源码一组一组算：

| group(AIRCR.PRIGROUP) | HAL 宏名 | 抢占位 | 子位 | (2,0) 的 IP | (2,1) 的 IP | CMSIS 单参 2 的 IP |
|---|---|---|---|---|---|---|
| 0(复位默认) | —(无宏) | 4 | 0 | 0x20 | 0x20(sub 被扔) | 0x20 |
| 3 | NVIC_PRIORITYGROUP_4 | 4 | 0 | 0x20 | 0x20(sub 被扔) | 0x20 |
| 4 | NVIC_PRIORITYGROUP_3 | 3 | 1 | 0x40 | 0x50 | 0x20 |
| 7 | NVIC_PRIORITYGROUP_0 | 0 | 4 | 0x00(preempt 被扔) | 0x10 | 0x20 |

头两行咱们放在一起看。复位默认的 PRIGROUP=0 与 GROUP_4 的 PRIGROUP=3 在 4 位实现上是逐位等价的，咱们翻遍库树也没找到一处对 `HAL_NVIC_SetPriorityGrouping` 的调用，AIRCR 保持的就是复位值，于是两条路线（HAL 那边 sub 恒 0）写出的 IP 相同。前提就是咱们守着这个默认，一旦有谁改了分组，两边写出的 IP 就岔开了。

第三行是 `NVIC_PRIORITYGROUP_3`（3 位抢占 1 位子）的分组。CMSIS 单参路线写下的还是 `preempt<<4`，但在这个分组下读回的抢占级得按 `IP>>5` 算。咱们写 2 进去，IP 就成了 0x20，读回的抢占级是 1。写的还是同一个 2，分组换了一次，含义从抢占级 2 漂成了抢占级 1 加子级别 0。数字本身是一个都没变的，变的只是硬件拿哪几位当抢占位。

末行的 `NVIC_PRIORITYGROUP_0`（0 位抢占 4 位子）最极端：抢占位一个都没剩，preempt 参数整个被扔掉了，`(2,0)` 写进去的 IP 是 0x00，全系统的所有中断都成了同级，嵌套是一次也不会有的。这不是咱们推断出来的，HAL 的注释原句摆在那里：

```text
@note When the NVIC_PriorityGroup_0 is selected, IRQ preemption is no more possible. The pending IRQ priority will be managed only by the subpriority.
```

阶梯表是咱们照源码一组一组算出来的，ARM 与 ST 当年为什么这么设计，咱们不代言。寄存器与分组的字段级细节，RM0008 第 10 章只指了个方向，正文里写这些细节的是 PM0056 编程手册，本篇没下到它的真身，咱们一个字都没引，卡片咱们放在文末。

## SysTick 的一次翻车：等一个被自己压住的节拍

hal_cortex.c 文件开头的使用说明里也这么写，咱们把原句贴出来：

```text
Configures the SysTick IRQ priority to the lowest value 0x0F.
```

库那边也是照此办理的，kernel.hpp 的第 10 行咱们逐字贴出来：

```c
#define TICK_INT_PRIORITY 0x0FU /* SysTick 优先级给最低 */
```

0x0F 是十六级的最后一级，谁来了都能压它。取材的时候笔者在这里真的翻过车：初版探针的 OnSlow 在 handler 里忙等 HAL_GetTick，跑起来的日志是这个长相：T3+ 之后 T2 一条接一条地连发了几十条，时间戳全落在了同一个毫秒上，节拍从此就停了。因果链倒是不复杂：抢占级 3 的 Tim3 一进 handler，SysTick 的 15 就被压住了。HAL_GetTick 读的正是 SysTick 喂的那个计数，它停了，忙等循环的条件就永远差着 40ms，退不出去了。抢占级更高的 Tim2 倒是照常打断这个卡死的循环，所以几十条 T2 挤在了同一个时间戳上。

级别在 NVIC 里排成的是一条全序，任何两个中断源咱们都排得出谁压谁。在 ISR 里等一个被自己压住的更低级资源，这样等下去的结果就是自锁。更常见的变体是在 ISR 里调 HAL_Delay，它卡死的原因一模一样，数的也是同一个计数器。修法咱们在探针代码里已经贴过了，就是 SystemClock_Config 之后的那句 `NVIC_SetPriority(SysTick_IRQn, 0U)`，作用是把节拍提到最高的级别。位置不能放早了，放到了时钟重配之前，就会被后面跑的 HAL_InitTick 盖回去。

## 收尾：下一篇拆 SysTick

HAL 的时基里住着 SysTick，HAL_Init 里配的那个 1ms 节拍，HAL_GetTick 与 HAL_Delay 数的都是它。刚拆完的那次翻车里也有它，它被 HAL 排到了最低级，又正好是忙等循环要看的那块表。它还留着一条咱们没用上的线索：RM0008 的 10.1.1 里写着校准值 9000，9MHz 的参考时钟拿它出 1ms。这个数怎么用、1ms 的节拍到底是谁在哪个时钟上配出来的，下一篇咱们拆的就是 SysTick 时基。

## 注意事项与常见错误

| 症状 | 原因 | 解法 |
|---|---|---|
| 抢占等级填了 16 或更大，本想配最低，行为反而成了最高、到处打断别人 | (16<<4)&0xFF=0x00，出界值回卷，CMSIS 与 HAL 都没有 bounds 检查 | 只用 0 到 15；配完拿 NVIC_GetPriority 读回对数 |
| HAL 双参配的数与 CMSIS 单参的读回对不上 | HAL 按运行时 AIRCR 分组编码，CMSIS 单参恒 preempt<<4，分组一改两边含义就分岔 | 用 NVIC_GetPriorityGrouping 查实况，对照分组阶梯表重算 |
| 从别的工程搬来的抢占等级数字，行为全变了 | 原工程调过 HAL_NVIC_SetPriorityGrouping，分组不同，同一个 IP 读回的抢占级不同 | 按当前分组重排数字；本库不调分组，靠的是复位默认 |
| ISR 里调 HAL_Delay 或忙等 HAL_GetTick，卡死在里面 | SysTick 被 HAL 排最低 0x0F，抢占级更高的 ISR 把节拍压住，计数不走 | 时钟重配后 NVIC_SetPriority(SysTick_IRQn, 0U)；长活儿移出 ISR |
| 想靠子优先级让一个中断打断另一个 | 子优先级只排同时挂起的先后，不产生嵌套，嵌套只看抢占位 | 要嵌套就给不同的抢占级 |
| 同级中断的长 handler 期间，另一个中断迟到一拍才跑，担心丢 | 同级不嵌套，到期进挂起位，handler 出门后追发，一次到期一次服务 | 不丢只迟；窗口长的活儿别放 ISR |

## 参考资源

<ReferenceCard title="参考文献">
  <ReferenceItem
    :id="1"
    author="STMicroelectronics"
    title="RM0008 Reference Manual — STM32F101/102/103/105/107（Rev 21）"
    :year="2021"
    url="https://www.st.com/resource/en/reference_manual/rm0008-stm32f101xx-stm32f102xx-stm32f103xx-stm32f105xx-and-stm32f107xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf"
    chapter="10 Nested vectored interrupt controller (NVIC); 10.1.1 SysTick calibration value register; 10.1.2 Interrupt and exception vectors"
  />
  <ReferenceItem
    :id="2"
    author="Awesome-Embedded-Learning-Studio"
    title="libestdx（include/libestdx/boards/stm32f1/ 的 tim.hpp、exti.hpp、uart_irq.hpp 与 hal/kernel.hpp，子模块固定在提交 8026841）"
    :year="2026"
    url="https://github.com/Awesome-Embedded-Learning-Studio/libestdx"
    chapter="TimBase/TimOneShot 的 init、HAL 双参调用、TICK_INT_PRIORITY; 探针 examples/11_prio_probe 为本篇新增，未进子模块，库本体仍 pin 在 8026841"
  />
  <ReferenceItem
    :id="3"
    author="STMicroelectronics"
    title="stm32f1xx_hal_cortex.c 与 stm32f1xx_hal_cortex.h（vendored 源）"
    chapter="HAL_NVIC_SetPriority、三层决序注释、NVIC_PriorityGroup_0 的 @note、文件头使用说明里的 SysTick 0x0F 一句、分组宏值; 文本冻在 libestdx 子模块 third_party/STM32F1/Drivers/STM32F1xx_HAL_Driver/，stm32f1xx_hal_cortex.c 在 Src/ 子目录，stm32f1xx_hal_cortex.h 在 Inc/ 子目录"
  />
  <ReferenceItem
    :id="4"
    author="ARM Limited / STMicroelectronics"
    title="CMSIS core_cm3.h 与 stm32f103xb.h（vendored 源）"
    chapter="__NVIC_SetPriority 的写入行、__NVIC_PRIO_BITS=4、TIM2_IRQn=28；文本冻在 libestdx 子模块 third_party/STM32F1/Drivers/CMSIS/"
  />
  <ReferenceItem
    :id="5"
    author="STMicroelectronics"
    title="PM0056 Programming Manual — STM32F10xxx/20xxx/21xxx/L1xxxx Cortex-M3 programming manual"
    url="https://www.st.com/resource/en/programming_manual/pm0056-stm32f10xxx20xxx21xxxl1xxxx-cortexm3-programming-manual-stmicroelectronics.pdf"
    chapter="编程手册; NVIC 寄存器与优先级分组的字段级细节在此，本篇正文未引其原文"
  />
</ReferenceCard>
