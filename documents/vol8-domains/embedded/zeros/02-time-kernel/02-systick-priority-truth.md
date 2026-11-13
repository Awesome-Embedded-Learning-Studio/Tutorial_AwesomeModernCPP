---
title: "注释写 Lowest,硅片判 Highest:拆掉 CMSIS 的优先级移位"
description: "把 CMSIS 从内核 arch 层拆掉:__get/__set_BASEPRI 换成手写 mrs/msr 内联汇编(写侧带 memory clobber),NVIC_SetPriority + SysTick_Config 换成匿名命名空间里的 volatile 指针直写 CSR/RVR/CVR 与 SCB SHPR3;高潮是与仓库自带的 core_cm3.h 当庭对质——旧调用 NVIC_SetPriority(SysTick_IRQn, 0xF0) 经 (0xF0<<4)&0xFF 算出 0x00,注释的 Lowest Level 落空成硅片上的最高优先级,真正兜住它的是下一行 SysTick_Config 内部的库默认值;验收 = git grep 在 include/ZerOS 与 src/arch 对 stm32f1xx/core_cm3/assert.h 零命中(真实命令与输出)"
chapter: 2
order: 2
tags:
  - stm32f1
  - intermediate
  - 嵌入式
  - 寄存器
difficulty: intermediate
platform: stm32f1
cpp_standard: [23]
reading_time_minutes: 18
prerequisites:
  - "给内核一秒钟·第 1 篇:一秒钟的定义权"
related:
  - "一秒钟的定义权:tick、回绕算术与到期队列"
---

# 临界区换引擎:从 CMSIS 宏到内联汇编

## 临界区换引擎:从 CMSIS 宏到内联汇编

咱们从 `cm3_irq.hpp` 看起,它是临界区的家。上一站咱们用它写出了 BASEPRI 嵌套临界区,锁的开关走的是 CMSIS 的 `__get_BASEPRI` 与 `__set_BASEPRI`,这一篇要把它们换成咱们自己手写的内联汇编。为什么要自己动手?BASEPRI 这样的特殊寄存器,不在普通的内存里待着,`volatile` 指针是够不着它的,摸到它的路子只有一对指令,也就是咱们要自己写的 `mrs` 与 `msr`,而 CMSIS 把这对指令包在了一整棵 ST 的头文件树里,arch 层想跟 ST 断干净的话,就得把它们接到咱们自己手里。咱们把新文件拆成两段,头一段咱们看两个汇编函数:

```cpp
#pragma once
#include "ZerOS/kernel/clock/kernel.hpp"
#include "ZerOS/kernel/irq/critical_section.hpp"
#include <cstdint>

namespace ZerOS::arch::cortex_m3 {

inline std::uint32_t read_basepri() {
    std::uint32_t value = 0;
    asm volatile("mrs %0, basepri" : "=r"(value));
    return value;
}

inline void write_basepri(std::uint32_t value) {
    asm volatile("msr basepri, %0" ::"r"(value) : "memory");
}
```

`mrs` 和 `msr` 就是咱们说的那对专用指令,`mrs` 负责的是读出,而 `msr` 把值写回去,一读一写正好对上 `read_basepri` 与 `write_basepri` 两个函数的名字。约束写法咱们看仔细:`"=r"(value)` 声明的输出交给 `value` 这个变量,`"r"(value)` 声明的输入从 `value` 里取,寄存器都让编译器自己挑它的。

真正值得您停一下的,是写侧末尾的那个 `"memory"`。它的作用是告诉编译器,这段汇编动的是内存,您别把临界区里前后的访存指令重排到锁外面去。读侧为什么不带?因为读 BASEPRI 没有任何可观察的副作用,读的动作只是把一个值搬进通用寄存器,编译器把它挪到哪一步去执行都是允许的,而程序的可见行为一丝不变,所以读侧不需要 clobber 护着。写侧就不一样了,改的对象是“哪些内存访问被挡住”的这个状态,因此谁在锁内、谁在锁外也就跟着变了,所以重排一次,行为也就跟着变一次了,咱们才必须用 `"memory"` 把访存顺序看住。

文件的后半截是临界区结构的本体,咱们接下去看:

```cpp

// For simple and toy, ARM and disarm currently using systicks
struct CortexM3CriticalSection {
    void lock() {
        if (nest_++ == 0) {
            saved_basepri_ = read_basepri();
            write_basepri(kMaskLevel);
        }
    }
    void unlock() {
        if (--nest_ == 0) {
            write_basepri(saved_basepri_);
        }
    }

  private:
    static constexpr std::uint32_t kMaskLevel = 0x40;
    std::uint32_t nest_{0};
    std::uint32_t saved_basepri_{0};
};

static_assert(ZerOS::irq::CriticalSection<CortexM3CriticalSection>);

} // namespace ZerOS::arch::cortex_m3
```

结构跟上一站是一模一样的:嵌套的计数还在,保存恢复的路子也在,`kMaskLevel` 还是 0x40 的老值,一个字也没被咱们动过,动的只有底下的引擎,`lock` 与 `unlock` 里的调用点换成了上面那对函数。include 的变动也在文件头上:旧版拖着 `"stm32f1xx.h"` 和 `<assert.h>`,新版剩下的外来头只有一个 `<cstdint>`。

## 直写三寄存器

咱们接着看 `time_kernel_cm3.cpp`。commit 之前的它是这样的:

```cpp
#include "ZerOS/arch/arm_cortex_m3/time_kernel_cm3.hpp"
#include "core_cm3.h"

namespace ZerOS::arch::cortex_m3 {
constinit SystemTimeKernel system_time{};
}

void ZerOS::arch::cortex_m3::init_time(std::uint32_t cycles_per_tick) {
    NVIC_SetPriority(SysTick_IRQn, 0xF0); // Lowest Level
    SysTick_Config(cycles_per_tick);
}

extern "C" void SysTick_Handler() {
    ZerOS::arch::cortex_m3::system_time.on_elapsed(1);
}
```

雷就埋在这两行的里面,咱们把 `NVIC_SetPriority` 和 `SysTick_Config` 各看一眼,上一篇的伏笔就在这里。commit 之后的同一个函数,换成了直写的版本,新文件咱们拆成两段看,头一段是寄存器地址与位常量的声明区:

```cpp
#include "ZerOS/arch/arm_cortex_m3/time_kernel_cm3.hpp"
#include <cstdint>

namespace ZerOS::arch::cortex_m3 {
constinit SystemTimeKernel system_time{};
}

namespace {
auto* const kSystCsr = reinterpret_cast<volatile std::uint32_t*>(0xE000E010);
auto* const kSystRvr = reinterpret_cast<volatile std::uint32_t*>(0xE000E014);
auto* const kSystCvr = reinterpret_cast<volatile std::uint32_t*>(0xE000E018);
auto* const kScbShpr3 = reinterpret_cast<volatile std::uint32_t*>(0xE000ED20);

constexpr std::uint32_t kSystEnable = 1u;
constexpr std::uint32_t kSystTickInt = 2u;
constexpr std::uint32_t kSystClkSource = 4u;
} // namespace
```

咱们从上往下看这段:四个地址集中在匿名的命名空间里。匿名命名空间给了它们内部链接的身份,编译单元之外的代码谁也看不见它们。`volatile std::uint32_t*` 让每一次读写都真的落到总线上去,编译器不许缓存它们的值、不许合并它们的访问。三个位常量也全都具了名:`ENABLE` 占的是第 0 位,`TICKINT` 占的是第 1 位,`CLKSOURCE` 占的是第 2 位,`0xE000E010` 这个 CSR 寄存器就是靠它们三个开起来的。

地址备齐了,函数本体咱们接着看:

```cpp
void ZerOS::arch::cortex_m3::init_time(std::uint32_t cycles_per_tick) {
    *kScbShpr3 = (*kScbShpr3 & 0x00FFFFFFu) | (0xF0u << 24);
    *kSystRvr = cycles_per_tick - 1;
    *kSystCvr = 0;
    *kSystCsr = kSystEnable | kSystTickInt | kSystClkSource;
}

extern "C" void SysTick_Handler() {
    ZerOS::arch::cortex_m3::system_time.on_elapsed(1);
}
```

`init_time` 的函数体一共四行,咱们一行一行对着看。最后的一行是 `CSR = kSystEnable | kSystTickInt | kSystClkSource`,三个常量合出 0b111 的值,也就是 SysTick 的时钟、中断两个开关全开,时钟源选的处理器时钟,一次就全开到位了。`RVR` 这一行写的是 `cycles_per_tick - 1`,这个减一是硬件的规定,重装的值数到它就回卷,所以给进去的要 N-1。`CVR` 这一行写的是 0,它把当前的计数值清了零,等下一次的时钟沿一来,它就立刻从 RVR 重装了,周期从此就对得整整齐齐了。

最上面那行是这一篇的主角,咱们对着地址表看,它写的是 SCB 的 SHPR3 寄存器,地址落在了 `0xE000ED20`。这个 32 位的寄存器管着系统异常 12 到 15 的优先级,咱们按每 8 位一个字节把它切开,而最高的那个字节,也就是排在第 24 到 31 位的那个,归 SysTick 的优先级所有。咱们的写法是拿 `0x00FFFFFF` 把高字节清干净,跟着就把 `0xF0` 搬到了第 24 位往上,其余三个字节的旧值原样保住。

这行跟被它替换掉的 `NVIC_SetPriority(SysTick_IRQn, 0xF0)` 意图相同,但有个关键区别:0xF0 这个数原样出现在咱们自己的源码里,它落到寄存器的高字节里还是 0xF0,中间没有任何人替咱们换算。而被替换的那行,中间隔着一层 CMSIS 的换算公式。下一节咱们就去看那层公式干了什么。

## 当庭对质:core_cm3.h 第 1639 行

`core_cm3.h` 躺在仓库的 `third_party/STM32CubeF1` 子模块里,完整的路径是 `Drivers/CMSIS/Include/core_cm3.h`,板级的 CMake 把这个目录加进了 include 路径,旧的 `#include "core_cm3.h"` 找的就是它。咱们把它翻到第 1639 行,`__NVIC_SetPriority` 的实现全文在这儿:

```c
__STATIC_INLINE void __NVIC_SetPriority(IRQn_Type IRQn, uint32_t priority)
{
  if ((int32_t)(IRQn) >= 0)
  {
    NVIC->IP[((uint32_t)IRQn)]               = (uint8_t)((priority << (8U - __NVIC_PRIO_BITS)) & (uint32_t)0xFFUL);
  }
  else
  {
    SCB->SHP[(((uint32_t)IRQn) & 0xFUL)-4UL] = (uint8_t)((priority << (8U - __NVIC_PRIO_BITS)) & (uint32_t)0xFFUL);
  }
}
```

SysTick 在 Cortex-M3 的身上属于系统异常,中断号是个负数这件事写在了枚举里(`SysTick_IRQn = -1`),所以它走的是 else 分支,写的是 `SCB->SHP` 数组。`((uint32_t)(-1) & 0xF) - 4` 咱们一算,算出来的是 11,`SHP[11]` 正是 SHPR3 的最高字节,它的地址是 `0xE000ED23`,跟我直写版动的是同一个字节,这一点上两边的看法没有分歧。

分歧全出在换算的公式上:它拿 `priority << (8U - __NVIC_PRIO_BITS)` 去移位,而后再跟 0xFF 做截断。`__NVIC_PRIO_BITS` 的取值是多少,咱们去设备头文件 `third_party/STM32CubeF1/Drivers/CMSIS/Device/ST/STM32F1xx/Include/stm32f103xb.h` 的第 51 行看:

```c
#define __NVIC_PRIO_BITS           4U       /*!< STM32 uses 4 Bits for the Priority Levels    */
```

STM32F1 的 NVIC 只把高 4 位的优先级做进了硬件,低的 4 位它是不接的。CMSIS 的想法是:您给我一个逻辑等级,我把它挪到高位上去对齐硬件。想法听着倒是挺体贴,咱们把旧调用的实参 0xF0 代进去,一步一步地手算:

咱们把 `0xF0` 交给公式,头一眼看的是它自己:

```text
0xF0            = 240        = 0b 1111 0000
```

咱们把它左移 `8 - 4 = 4` 位:

```text
0xF0 << 4       = 0xF00      = 3840      = 0b 1111 0000 0000
```

咱们再跟 0xFF 做截断,留下的只有低 8 位:

```text
0xF00 & 0xFF    = 0x00       = 0
```

咱们算到头,得到的值是 0x00。写进 `SHP[11]` 的就是 0。

咱们在开发用的电脑上随手编了个小程序,把这串算术跑了一遍,免得手一滑算错了:

```cpp
#include <cstdint>
#include <cstdio>
int main() {
    std::uint32_t requested = 0xF0u;
    std::uint32_t written = (requested << (8u - 4u)) & 0xFFu;
    std::printf("requested = 0x%02X\n", requested);
    std::printf("0xF0 << 4      = 0x%03X\n", requested << 4);
    std::printf("(..) & 0xFF    = 0x%02X\n", written);
    std::uint32_t cmsis_default = ((1u << 4) - 1u);
    std::printf("SysTick_Config default ((1<<4)-1)=0x%X -> reg 0x%02X\n", cmsis_default, (cmsis_default << 4) & 0xFF);
    return 0;
}
```

咱们跑出来的输出如下:

```text
requested = 0xF0
0xF0 << 4      = 0xF00
(..) & 0xFF    = 0x00
SysTick_Config default ((1<<4)-1)=0xF -> reg 0xF0
```

跟咱们的手算是一致的。最后一行是我顺手多打的:它把 `SysTick_Config` 内部那个默认的等级 15 过同一道公式,落到寄存器上正好是 0xF0 的值,这行输出在后面的一节还要用到。前两行暴露的问题在这里:Cortex-M3 的优先级语义是数值越小越紧急,0 是最高的一档。我当年写下 `// Lowest Level` 的时候,我心里打的算盘是:“0xF0 在 STM32 的 4 位实现里是 15 级、垫底的等级”,可这个数在 CMSIS 的公式里过了一道,出来的却是 0x00,垫底也就变成了登顶。

那临界区呢?咱们回忆上一站的 BASEPRI 判定:BASEPRI 写入 0x40 之后,优先级数值大于等于 0x40 的异常全部被挡住,而优先级 0 的异常永远挡不住——这是架构硬性定下的判定,BASEPRI 从设计上就不掩蔽最高的优先级。所以如果 SysTick 真的运行在 0x00,那么等它进来的时候,咱们那个嵌套临界区对它形同虚设:您锁得再严,时基中断照样会扎进锁的中间,`on_elapsed` 跟任务侧对时间队列的并发保护整个穿帮。

::: warning
CMSIS 的 `NVIC_SetPriority` 收的是逻辑等级,它替您做 `(priority << (8 - __NVIC_PRIO_BITS)) & 0xFF` 的换算,您想直接写进寄存器的那个字节,得由您自己反推回来。您想直接给寄存器的字节,就不能走这个接口了,您想走这个接口,就得按逻辑等级的口径给数。咱们要是把两边混着理解,得到的值跟您以为的会差上一整圈。
:::

## 那旧代码怎么没炸

讲到这儿您可能要问:既然优先级被写成了 0x00,上一站的板上 ticker 不是跑得好好的吗,临界区要是真的穿了帮,时间队列早该乱了。

好吧，笔者当时也很纳闷。不是哥们咋炸了？答案就在旧函数的下一行。`SysTick_Config` 自己内部也调了一次 `NVIC_SetPriority`,咱们看 `core_cm3.h` 第 1834 行起它的实现,节选关键的优先级与配置那四行,开头几行的越界检查略去:

```c
  SysTick->LOAD  = (uint32_t)(ticks - 1UL);                         /* set reload register */
  NVIC_SetPriority (SysTick_IRQn, (1UL << __NVIC_PRIO_BITS) - 1UL); /* set Priority for Systick Interrupt */
  SysTick->VAL   = 0UL;                                             /* Load the SysTick Counter Value */
  SysTick->CTRL  = SysTick_CTRL_CLKSOURCE_Msk |
                   SysTick_CTRL_TICKINT_Msk   |
                   SysTick_CTRL_ENABLE_Msk;                         /* Enable SysTick IRQ and SysTick Timer */
```

第二行的调用是 `NVIC_SetPriority(SysTick_IRQn, (1 << 4) - 1)`,递进去的实参是 15。15 过的是同一个公式:`(15 << 4) & 0xFF` = `0xF0`。我那行写进去的 0x00,活了不到一条指令的工夫,就被库的默认值改写回 0xF0——这回是真的最低优先级了,BASEPRI 0x40 是挡得住它的,咱们的临界区在工作。
