---
title: "配平中断优先级,再让中断敲门:ThisTask 与第一条唤醒链路"
description: "0e8ce6d(commit 说明自带拼写错误 add this tasl,照实引用)给应用一张当前任务的公共脸:ThisTask::sleep_for/yield/block 走 extern template 单点接线,concept 检查挪到后端完备的实现头;同时打通第一条真中断唤醒链路,USART1 RX 中断进一字节邮箱再到 ready,只 pend 不切;硬知识是 NVIC 优先级必须压到 0x80 才罩得进 BASEPRI 0x40 的临界区,我自己在 Renode 里加了撕裂探针,把优先级改回 0x00 当场复现中断闯进临界区;checkout 后在串口敲字符,睡着的任务立刻醒来接字节"
chapter: 3
order: 4
tags:
  - stm32f1
  - advanced
  - 嵌入式
  - 寄存器
  - concepts
difficulty: advanced
platform: stm32f1
cpp_standard: [23]
reading_time_minutes: 24
prerequisites:
  - "谁最急,谁上场:就绪位图、任务三态与选人仲裁"
  - "在栈顶伪造出生证明:PendSV、SVC 与第一次切换"
related:
  - "睡死那个 bug:丢唤醒、park_for 与让错误用法编不过的建造者链"
current_status:
  title: 中断学会敲门了
  detail: ZerOS的篇章正在打磨ing
  variant: polishing
  icon: 🔔
---

# 配平中断优先级,再让中断敲门:ThisTask 与第一条唤醒链路

咱们本章开张的时候说过,调度器能切了之后还有一件事欠着,就是中断把睡着的任务叫醒的第一条链路。名是咱们开张词点的,机制面那篇在讲 ICSR 的时候又留了一句,说的是延迟切换的设计要在中断唤醒的路径上兑现,今天的这一篇就来兑现它们。上一篇《睡死那个 bug:丢唤醒、park_for 与让错误用法编不过的建造者链》把 `park_for` 和类型状态的防线讲完之后,内核本身已经稳了,可是它还从来没有被真正的外部事件敲过门,叫醒它的要么是时钟,要么是测试桩里一个只会计数的替身。

咱们今天锚定的 commit 是 `0e8ce6d`,subject 写的是 “feat: add this tasl”。您没看错,task 被拼成了 tasl,这是仓库里的原始拼写错误,我照实引用,也不替它做任何的修饰。它交了 9 个文件,加了 246 行、删了 28 行,干的是两件事:一件是给应用代码一张“当前任务”的公共脸 `ThisTask`,另一件是把真中断唤醒的头一条链路接通,USART1 收到了一个字节,睡着的任务立刻醒来把字节接走。路上还有一堂能上生产课的硬知识:USART1 的中断优先级必须压到 0x80,数值大于临界区的 BASEPRI 0x40,这个配平要是缺了,默认的 0 会闯开一切临界区,咱们会在 Renode 里当着它的面闯一次。

## 给应用一张“当前任务”的脸

任务想睡觉的时候,它凭什么知道自己是谁?上一篇之前,咱们要的答案在 `system_sched.current_task()` 里,应用代码直接伸手进了调度器的单例。`0e8ce6d` 把伸手进去的动作收了回去,换上了一张公共的脸。咱们看新交的头文件 `include/ZerOS/this_task.hpp`:

```cpp
#pragma once

// The public face of "the task that is calling me": sleep, yield, block.
// Business code includes ONLY this header — which silicon answers the
// call is wiring's secret (see src/system + src/arch/<target>).

#include <cstdint>

namespace ZerOS {

// plain unit, zero cost, keeps milliseconds from being confused with ticks
struct Milliseconds {
    std::uint32_t count;
};

namespace detail {
struct SystemTaskBackend; // neutral tag; the real one lives in a target .cpp
}

template <class Backend>
struct BasicThisTask {
    BasicThisTask() = delete; // a face, not a thing: no instances, ever

    static void sleep_for(Milliseconds delay) noexcept;
    static void yield() noexcept;
    // raw "sleep until someone readies me" — the real event primitives
    // (semaphores and friends) will grow on top of this
    static void block() noexcept;
};

extern template struct BasicThisTask<detail::SystemTaskBackend>;

using ThisTask = BasicThisTask<detail::SystemTaskBackend>;

} // namespace ZerOS
```

头文件顶上的注释把约定说得干净,业务代码只准 include 的头是它,哪块硅片接的电话,是接线方自己的秘密。`Milliseconds` 是个只有 `count` 一个成员的裸结构体,咱们看它头顶的注释,说的就是零开销这件事,毫秒就是毫秒这样的值,咱们不会把它跟 tick 混在一起。上一站手搓 `task_sleep` 的时候咱们传的是裸的 `std::uint32_t`,到底是毫秒还是节拍数的问题,类型上是看不出来的,现在这个结构体把单位的歧义收走了。

构造函数被 `= delete` 删掉了,注释写的是 “a face, not a thing: no instances, ever”,脸是拿来叫的,没人造得出一副这样的脸。三个成员全都是 `static` 的,谁要是实例化了 `ThisTask`,编译就在他动手的那一行把他拦住了。`block` 头上的注释值得您多看一眼,它说的是裸的“睡到有人来叫我为止”,信号量、互斥量这些真正的事件原语,将来都以它为地基,这一行注释是后面同步原语的种子,咱们结尾还要回到它。

最底下两行是接线的枢纽。`extern template` 是显式实例化的声明,意思是模板的本体住在别的编译单元里,业务代码的 `.o` 文件见到它,生成的只是一个普通的外部调用。`using ThisTask = BasicThisTask<detail::SystemTaskBackend>` 给了应用一个不带模板参数的名字,而 `SystemTaskBackend` 在公共头里只有一句前向声明,连它是 struct 还是 class 这样的底都没有交代,它的真身住在哪个文件里,咱们下一节就看。

## concept 检查落在后端完备的地方

模板的本体不在公共头里,咱们在 `src/system/this_task_impl.hpp` 找到了它,这个文件是私有的,只有目标的 `.cpp` 和内核内部的测试会碰它。私有这件事是带着设计意图的:检查后端的 concept,得等后端的真身可见了才有意义。公共头里的 `SystemTaskBackend` 只有一句前向声明,什么都没交代,检查要是放在那儿的话,concept 就只能判它不合格了,咱们得到的报错跟真问题一点关系都没有。检查挪到了后端完备的实现头里,哪个函数签名写错了、哪个 `noexcept` 漏了,报错就直接落在具体的那一行上。头一截是验收用的 concept:

```cpp
#pragma once

// Private: the template bodies behind the ThisTask face. Only target
// .cpps and internal tests include this; the concept fires HERE, where
// the backend is complete — never on the public declaration.

#include "ZerOS/this_task.hpp"

#include <concepts>

namespace ZerOS {

namespace detail {
template <class B>
concept TaskBackend = requires(Milliseconds delay) {
    { B::sleep_for(delay) } noexcept -> std::same_as<void>;
    { B::yield() } noexcept -> std::same_as<void>;
    { B::block() } noexcept -> std::same_as<void>;
};
} // namespace detail
```

咱们看这个 `TaskBackend`,它要求后端会做的是三件事,而且三个签名都得是 `noexcept` 的。这个取舍跟仲裁面那篇把 concept 当接口文档的思路是同一条路的两半,那篇用 concept 写给人看的合同,这一篇管的是报错报在哪儿。

跟在后面的是三个模板体,写法是同一个模子倒出来的,咱们过一眼:

```cpp
template <class B>
void BasicThisTask<B>::sleep_for(Milliseconds delay) noexcept {
    static_assert(detail::TaskBackend<B>);
    B::sleep_for(delay);
}

template <class B>
void BasicThisTask<B>::yield() noexcept {
    static_assert(detail::TaskBackend<B>);
    B::yield();
}

template <class B>
void BasicThisTask<B>::block() noexcept {
    static_assert(detail::TaskBackend<B>);
    B::block();
}

} // namespace ZerOS
```

`static_assert` 守了一次门,然后咱们把调用转给后端。业务代码调用 `ThisTask::sleep_for` 生成的,就是对外部符号的一次普通调用,头文件里没有任何目标相关的知识。

## 接线与后端:每一个目标一份

真身的家安在 `src/arch/arm_cortex_m3/this_task.cpp` 里。它独占的知识是接线:两个单例住在哪儿、睡着的任务怎么被叫醒,后端要办的三件事全得靠这样的知识串起来,这样的知识一个目标配一份。咱们从头看,头一截是 include 和匿名的唤醒函数:

```cpp
// The Cortex-M3 answer to the ThisTask face: owns the wiring knowledge
// (both singletons + the wake reaction), then stamps the template out.

#include "system/this_task_impl.hpp"

#include "ZerOS/arch/arm_cortex_m3/switch.hpp"
#include "ZerOS/arch/arm_cortex_m3/time_kernel_cm3.hpp"
#include "ZerOS/kernel/clock/clock.hpp"
#include "ZerOS/kernel/sched/task_control_block.hpp"

namespace {

using ZerOS::arch::cortex_m3::system_sched;
using ZerOS::arch::cortex_m3::system_time;

void wake_from_sleep(ZerOS::base::BorrowedPtr<ZerOS::clock::TimeWaiter> waiter) {
    system_sched.ready(ZerOS::sched::TCB::owner_of(waiter));
}

} // namespace
```

`wake_from_sleep` 其实是咱们见过的老朋友,它起初在 example 里是咱们手搓的函数,上一篇把它收进了 arch 层的 `switch.cpp`,这一篇跟着接线知识一起搬进了 `this_task.cpp`,借着 `owner_of` 的指针减法从内嵌的 `TimeWaiter` 反推回 TCB 的活儿没有变,两个单例的知识也让后端接走了。

中间是干后端活的本尊,咱们看它:

```cpp
namespace ZerOS::detail {

struct SystemTaskBackend {
    static void sleep_for(Milliseconds delay) noexcept {
        system_sched.sleep_for(system_sched.current_task(), system_time, delay.count,
                               wake_from_sleep);
    }

    static void yield() noexcept {
        system_sched.yield();
    }

    static void block() noexcept {
        system_sched.block(system_sched.current_task());
    }
};

} // namespace ZerOS::detail
```

咱们看 `sleep_for` 的后端,它调的是调度器自己的 `sleep_for`,也就是上一篇用 `park_for` 把布防和阻塞关进同一个临界区的那个版本,手搓竞态的土壤从应用面上消失了。`yield` 和 `block` 是直通的调用,`block` 拿当前任务亲手交给了调度器,任务睡了下去之后,叫醒它的 `ready` 什么时候来,它也就什么时候回来了。

最后是盖章的一行,咱们看它:

```cpp
namespace ZerOS {
// stamp the face for THIS program; business .o files link plain calls
template struct BasicThisTask<detail::SystemTaskBackend>;
} // namespace ZerOS
```

末尾那行 `template struct BasicThisTask<detail::SystemTaskBackend>;` 是显式实例化的定义,注释里的 “stamp” 用得形象:这个编译单元替本程序把脸盖了一个章,别的 `.o` 文件只管调用,链接器自然就把两半接上了。这套模式的收益在换板子的时候才显得出来,哪天咱们把这个内核搬到第二块板上,新的 `this_task.cpp` 照着同样的结构写一份、换掉两个单例的名字就行,咱们一行应用代码都不用动。

咱们还要看一眼它从哪里搬走的家。这个 commit 对 `switch.hpp` 的改动把 arch 层的 `sleep_for` 声明摘掉了,留下的注释说应用面的 sleep/yield/block 去了 `<ZerOS/this_task.hpp>`(注释里的 D20 是仓库内部的设计文档编号),本层就只剩装配了。arch 层从此只剩 `spawn` 和 `start_scheduler` 这样的装配期动作,睡眠这样的应用面请求归公共的脸管,层次切干净了。

## 把 USART1 的 RX 交给中断

链路的另一头是串口。`uart.hpp` 在这个 commit 里从纯 TX 轮询升级成了 RX 中断接收,咱们把 `uart1_init` 的新版本看一遍:

```cpp
inline void uart1_init() {
    // RCC 在 Renode 为假值(就绪位恒 1),此行主要服务真机
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN | RCC_APB2ENR_IOPAEN;
    // PA9: 复用推挽输出 2MHz;PA10: 浮空输入
    GPIOA->CRH = (GPIOA->CRH & ~(GPIO_CRH_MODE9 | GPIO_CRH_CNF9 | GPIO_CRH_MODE10 | GPIO_CRH_CNF10)) |
                 GPIO_CRH_MODE9_1 | GPIO_CRH_CNF9_1 | GPIO_CRH_CNF10_0;
    USART1->BRR = 72'000'000 / 115'200;
    // RXNE 溢出(ORE)也会借 RXNEIE 拉起中断,handler 内自判 SR
    USART1->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE;

    // USART1 中断优先级必须压到 0x80(数值大于 BASEPRI 0x40):
    // 默认 0 是临界区罩不住的,调度器改队列改到一半会被它打断
    auto* const kNvicIp = reinterpret_cast<volatile std::uint8_t*>(0xE000E400);
    kNvicIp[kUsart1Irq] = 0x80;
    // ISER1 管 IRQ32-63,IRQ37 落 bit5
    auto* const kNvicIsr1 = reinterpret_cast<volatile std::uint32_t*>(0xE000E104);
    *kNvicIsr1 = (1u << (kUsart1Irq - 32));
}
```

PA10 换成了浮空输入(`CNF10_0` 只把 CNF 的最低位置 1,MODE10 保持的是 00),引脚是不推也不拉的,电平交给了对方的 TX,波特率还是原来的 115200。`CR1` 比上一版多了 `RE` 和 `RXNEIE` 两个位,`RE` 开的是接收,`RXNEIE` 的开关是让“收到一个字节”去拉起中断的。注释里那句 ORE 的说明是个容易忽略的角落:接收溢出(上一个字节还没读走、新字节又来了)用的也是 RXNEIE 同一个开关,所以咱们写的处理程序不能看见中断就当有字节,得自己查一遍状态寄存器的 RXNE 位,等咱们写到处理程序的时候,就能看到这个自判落在了哪。

中间那两行是全 commit 最值钱的一课,注释自己把道理写完了,咱们下一节专门展开。NVIC 的优先级寄存器是一字一个中断的数组,基址落在了 0xE000E400,USART1 的中断编号是 37,咱们直接往第 37 个字节写了 0x80。使能位的家在 ISER1,它的管辖区是 IRQ32 到 63,37 减 32 得到了 5,写的是 bit5。这些都是绕开 CMSIS 的裸寄存器写法,上一站那篇拆 CMSIS 移位公式的判断在这里延续着:源码里的数字,就是寄存器里躺着的数字,中间没有人替咱们换算。

向量表也跟着扩了。`startup.cpp` 里的 `vectors` 数组从 16 个槽扩到了 54 个,扩出来的是片上外设的座位,USART1 的中断想在表里有自己的处理程序,表里就得有它的座位。座位要是空着的话,误触发的中断连一句可供排查的话都不给咱们留,所以新扩的槽全挂了同一个兜底的 `fault_handler`。咱们一段一段看扩出来的那部分(注释里的 P4 也是仓库内部的设计文档编号,跟 D20 是同一个系列的记号),头 16 个新座位挂的全是兜底:

```cpp
    // 16-52 片上外设槽:未使能不会来;万一误触发,兜底打印比 nullptr 的 lockup 好查(P4)
    &fault_handler,
    &fault_handler,
    &fault_handler,
    &fault_handler,
    &fault_handler,
    &fault_handler,
    &fault_handler,
    &fault_handler, // 16-23
    &fault_handler,
    &fault_handler,
    &fault_handler,
    &fault_handler,
    &fault_handler,
    &fault_handler,
    &fault_handler,
    &fault_handler, // 24-31
```

从 16 到 52 的 37 个空槽全挂了同一个 `fault_handler`,它做的事不多,打印一行 `[FAULT] halted` 就停机了。注释把理由讲了:没使能的中断不会来,可万一有误触发的话,`nullptr` 槽在 Cortex-M3 上的下场是 lockup(取到一个无效地址,内核也就直接停摆了,什么痕迹都不留),兜底打印好歹给咱们留了一句话查。`fault_handler` 本身是 `extern "C"` 里的一个死循环加 WFI,咱们就不另贴了。

接着的 16 个座位也是兜底,咱们照原样看:

```cpp
    &fault_handler,
    &fault_handler,
    &fault_handler,
    &fault_handler,
    &fault_handler,
    &fault_handler,
    &fault_handler,
    &fault_handler, // 32-39
    &fault_handler,
    &fault_handler,
    &fault_handler,
    &fault_handler,
    &fault_handler,
    &fault_handler,
    &fault_handler,
    &fault_handler, // 40-47
```

座位的算法是统一的,Cortex-M3 的向量表头 16 个槽是内核异常的,片上外设的中断从槽 16 编起,咱们给外设的 IRQ 编号加上 16,才是它在表里的座位号。

表尾的最后几个座位,坐的才是真处理程序,咱们看:

```cpp
    &fault_handler,
    &fault_handler,
    &fault_handler,
    &fault_handler,
    &fault_handler,     // 48-52
    &USART1_IRQHandler, // 53 USART1 全局中断(RX 事件唤醒 demo)
};
```

USART1 的座位是这么算的:它的全局中断编号是 IRQ 37,加上头部的 16 个槽,得的就是 53 号,表尾的这个座位是它的,咱们看最后一行,坐进去的正是 `USART1_IRQHandler`。

## 配平:0x80 与 0x40 的关系

现在咱们把那两行注释的道理展开,咱们回顾一下上一站立下的判定:BASEPRI 写入 0x40 之后,优先级数值大于等于 0x40 的异常全部被挡住了,而优先级 0 的异常永远挡不住,这是 ARMv7-M 架构硬性定下的,BASEPRI 从设计上就不掩蔽最高的优先级。Cortex-M3 的优先级口径是数值越小越急,STM32F1 只做了高 4 位,所以芯片认得的等级是 0x00 到 0xF0 步进 0x10 的十六档。

这个口径落到了中断唤醒链路上,就成了一个必须配平的关系。临界区的引擎是上一站那副 `CortexM3CriticalSection`,锁的开关写的是 BASEPRI 0x40。调度器改的是就绪队列,任务写的是邮箱,靠的都是它,它的隐含假设是“我锁着的时候,没人来动这些共享的东西”。USART1 的中断恰恰会动:它的处理程序要写邮箱,还要调 `ready` 去改它的就绪队列。要是 USART1 的优先级是 0x80,数值大于 0x40 的时候,任务侧一加了锁,BASEPRI 一升到了位,它就被罩住了,只能等锁放掉的那一刻。要是它跑在默认的 0x00,数值比 0x40 还小的话,任何的临界区都罩不住它,调度器把位图和 FIFO 改到了一半,它闯进来改的是同一批数据,改完了就退,留给咱们的是一副谁也没见过的队列。所以优先级的数值得咬合着填,USART1 的 0x80 要大于 0x40,BASEPRI 的罩子才罩得住它。

::: warning
这个配平是每接一个新中断都要重做一遍的算术,一次性的配置管不了它一辈子的。BASEPRI 的屏蔽等级是 0x40,任何要碰内核数据的中断,它的优先级数值就得压到 0x40 到 0xF0 这一段里。要是哪个中断真有理由豁免临界区(比如必须硬实时响应的紧急信号),那它享受豁免的代价是它的处理程序一个内核数据都不能碰。两边的约定要成对出现,单边改了任何一半,咱们撕开的都是同一个口子。
:::

## ISR 只登记,不切换

链路的最后一环在 example 的 `main.cpp` 里,咱们把它的新角色看全。共享的东西有一字节的邮箱和一把锁:

```cpp
// the one-byte mailbox: ISR writes, task C drains; BASEPRI on both hands
constinit ZerOS::arch::cortex_m3::CortexM3CriticalSection mailbox_lock{};
char g_rx_byte = 0;
bool g_rx_pending = false;
ZerOS::sched::TCB* g_task_c = nullptr; // set in main, read by the ISR
```

这个邮箱的配置小得不能再小,里面装了一个字节,一个记着新货到没到的标记,末了再加一把 BASEPRI 锁。注释里的 “BASEPRI on both hands” 是整段的关键,邮箱的两只手(写它的 ISR、读它的任务)都握着同一把锁,咱们马上两边都能看到。`g_task_c` 是 main 里填好、ISR 里读的指针,ISR 要把唤醒登记送到具体的任务头上,它得知道自己叫醒的是谁。

睡着等事件的任务是新的 task_c,咱们看它的循环:

```cpp
// event-sleeper: woken by the RX interrupt, not by the clock
void task_c(void*) {
    for (;;) {
        ThisTask::block();
        char c;
        {
            ZerOS::irq::CriticalGuard guard{mailbox_lock};
            c = g_rx_byte;
            g_rx_pending = false;
        }
        ZerOS::board::print("got '");
        ZerOS::board::uart1_putc(c);
        ZerOS::board::print("'\r\n");
    }
}
```

它的循环跟 task_a、task_b 按钟点睡的节奏完全不同,`ThisTask::block()` 是裸的“睡到有人来叫我”,没有定时器的布防,叫醒它的只能是 `ready`。它醒来之后握了锁、取了字节、清了标记,再打一行 `got 'X'` 这样的回执,然后回去接着睡它的觉。它的优先级是 0,比 task_a 和 task_b 的 1 更急,中断一来它也就绪了,`ready` 里的判断就会请求切换,打印任务立刻让了位,字节就不用等下一拍了。

咱们看处理程序,它是唤醒链路里真正踩在硅上的一双手:

```cpp
// RXNE (and ORE, sharing RXNEIE): hand the byte to task C and book a
// switch — the actual context switch waits until the ISR exits (PendSV)
extern "C" void USART1_IRQHandler() {
    if ((USART1->SR & USART_SR_RXNE) == 0) {
        return;
    }
    const char c = ZerOS::board::uart1_getc();
    {
        ZerOS::irq::CriticalGuard guard{mailbox_lock};
        g_rx_byte = c;
        g_rx_pending = true;
    }
    if (g_task_c != nullptr) {
        system_sched.ready(g_task_c); // pends PendSV, never switches here
    }
}
```

开头那一眼 SR 的 RXNE 就是 init 注释里预告的自判:溢出也借 RXNEIE 拉中断,拉起来了却没有新字节,咱们遇到的这样的空炮弹就直接退回去。`uart1_getc` 读了数据寄存器,硬件顺手把 RXNE 清了。写邮箱的那一段握着 `mailbox_lock`,注释承诺过的两只手在这里合上了。接下去的就是 `ready`,注释里写的是 “pends PendSV, never switches here”,只把 PendSV 挂了起来,绝不在中断的肚子里切换。

这一行就是仲裁面那篇留下的承诺兑现的地方。那篇说过了,调度 API 从不在调用的现场直接切,真正的切换被推迟到了 PendSV,当时咱们看到的还只是测试桩里的计数器,现在 `request_switch` 写 ICSR 的 bit28、PendSV 排队、ISR 退出之后硬件尾链进切换,整套延迟切换的机制跑在了真中断的路径上。为什么非要推迟?咱们想一层:中断处理程序自己是压在 MSP 上的,它退出的时刻硬件还有一整套出入栈的流程要走,在它肚子里换掉了 PSP 和 r4 到 r11,等于是拆了别人正在住的房子。登记完了就退,切换交给专门干这个的 PendSV,顺序就干净了。

main 里的装配咱们点两句就够了:三个任务照工厂链搭好,task_c 的优先级给了 0,`spawn` 逐个入队了之后 `g_task_c = &c`,指针这才交给了 ISR。初始化的次序本身也是个约定,ISR 是使能了才来的,`uart1_init` 排在最开头的位置,指针在 `spawn` 之后才就位的。这个次序有个照实要说的边界:装配期里要是来一个字节的话,`g_task_c` 此刻还是空的,字节进了邮箱,等它的任务还不存在,它就丢了,ISR 开头的空检查只保证系统不崩,字节的丢失它是不知道的,demo 的装配期没有人打字,所以这个洞没有露过馅。

## 把优先级改回 0:现场撕一次

上面说的“默认 0 会闯开一切临界区”,这句话咱们不空口讲,我把 `uart.hpp` 里那一行改了,`kNvicIp[kUsart1Irq] = 0x80` 被我改成了 `0x00`,重新编译了固件,跑给您看。

我照实交代一个一上手就遇到的事实:干净的三任务 demo 里,这个破绽是看不出来的。我把 24 个不同的字符从宿主侧打进了串口,逐个等了回执,优先级 0x00 的固件把 24 个字节全数送达,系统也还是活着的。道理想通了就不奇怪:撕裂的窗口是任务侧握着锁的那几十条指令,在 72MHz 的仿真时间线上是几百纳秒的事,字符到达的时刻要恰好落进这个窗口才有事,概率低到仿真里碰不上的地步。破绽的存在是一回事,和一个具体的负载碰上它,又是另一回事了。

所以我给 demo 加了一个探针,把碰上的概率变成了必然。这是我自己写的实验代码,不属于 `0e8ce6d` 的内容,它的 shape 是下面这样的:

```cpp
// 撕裂探针(我自己的实验,不属于 commit 0e8ce6d):
// 探针任务几乎一直握着一把 BASEPRI 临界区,USART 的 ISR 检查旗标,
// 若它在锁开着的时候被放行,说明这个中断闯穿了 BASEPRI(只有 0x00 做得到)
constinit ZerOS::arch::cortex_m3::CortexM3CriticalSection probe_lock{};
volatile bool g_in_lock = false;
volatile std::uint32_t g_lock_passes = 0;
volatile std::uint32_t g_isr_tear_hits = 0;

void task_probe(void*) {
    for (;;) {
        {
            ZerOS::irq::CriticalGuard guard{probe_lock};
            g_in_lock = true;
            for (volatile int i = 0; i < 2000; ++i) {
            }
            g_in_lock = false;
        }
        ++g_lock_passes;
    }
}
```

探针任务的优先级给了 31(最低的真实档),它在后台一圈一圈地转,几乎每一刻都握的是那把锁,每秒能完整进出的次数有七千多。我在 USART 的 ISR 里加了一行:看见 `g_in_lock` 是真,计数就加了。这个计数器的含义是精确的,它每加了一次,就是 ISR 在临界区开着的时候被放行了一次,`ready` 和它后面的一切就踩在任务侧改到一半的状态旁边。0x80 的配平之下,锁着的每一刻 BASEPRI 都是 0x40,USART 的 0x80 被罩住了,挂起等着放行的机会,这个计数器就应该是恒为零的。改回了 0x00,它就长起来了。task_a 的打印我带上了两个计数,每一秒报一次的节奏。测的载具是 Renode 自带的 robot 测试框架,宿主侧往串口里打了 10 个字符,然后咱们收心跳,每个配置各收了一行,每行是三段的读数。贴出来的 g1 到 g3,是 robot 脚本里三次 Wait 各捕获的一行心跳,task_a 的心跳每隔一秒报一次,三行也就对着三个连续的整秒了:

```text
0x80:g1=A 0 0 g2=A 7122 0 g3=A 14244 0
0x00:g1=A 0 0 g2=A 7122 5 g3=A 14244 10
```

两行的格式是“A 进出次数 撕裂次数”。配平正确的时候,进出次数涨到了一万四,撕裂次数也就始终是 0 了,10 个字符没有一个是在锁里被处理的。把优先级改回了 0x00,咱们用同一个探针、打同样 10 个字符,看到的撕裂次数从 5 涨到了 10,每个字符都在临界区开着的时候闯了进来。0x40 的 BASEPRI 在 0x00 面前就是一张纸,上一站的判定,说的是优先级 0 的异常永远挡不住,在这里变成了一个咱们一眼能看懂的计数器。我看到 5 和 10 的时候是有点爽的,不瞒您。上一篇答应给您看的“队列被撕坏的现场”,这次交出来的是计数器的版本,咱们照实记一笔,把队列真写坏给读者看的验收,要等咱们把固件跑上真正的开发板的那一篇。

::: warning
探针的计数器是“ISR 与任务互相看见了对方”的下界证明,它证明的是 BASEPRI 挡不住 0x00 这个架构事实落在了 Renode 的模型里,它没有证明每一次的撕裂都必然弄坏数据。真实的板上,坏不坏、坏成什么样,取决于撕裂那一刻任务侧改到了哪个字节,可能十次里九次是没事的,而第十次链表断了环。生产代码不能靠大概率没事的假设活着,咱们要是想让配平靠检查把关,至少得有一行注释把数值的关系写明白,`uart.hpp` 里写的就是这个样子。
:::

## 验收

咱们把本篇可查证的事实归拢一遍,一共归出了四件。

第一件要核的是构建。我在 /tmp 的副本里 checkout 到了 `0e8ce6d`(third_party 下的工作树是个 gitfile,`.git` 只是一个指到真实仓库的文件,我拿 `git archive 0e8ce6d` 导出了树,STM32CubeF1 子模块也做了同样的导出),配上了交叉工具链构建:

```shell
cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/arch/arm-none-eabi.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

真实的体积报告如下,咱们看数字:

```text
   FLASH:        2408 B        64 KB      3.67%
     RAM:        1200 B        20 KB      5.86%
   text	   data	    bss	    dec	    hex
    2216	     192	   1008	   3416	    d58
```

比起机制面那篇的 1680 字节,这一版多出的 700 多字节花在了中断的路径上,咱们能数出它们的花销:向量表扩了 38 个槽,RX 的处理程序和 ThisTask 的后端都在里面。预算线还是立项时的核心 flash 6KB、RAM 1KB,咱们离它还远。

第二件要核的是串口敲字符。Renode 1.17.0 认不出 `bluepill.repl` 里的 `I2C.STM32F4_I2C`,跟机制面那篇遇到的一样,我把 i2c1、i2c2 两个节点整块注释掉,demo 不经过 i2c 的路径,所以行为是不受影响的。敲字符我用的是 Renode 的 robot 测试框架(`renode-test`),它给串口提供了一个终端tester,宿主侧的 `Write Char On Uart` 关键字把字符打进了仿真串口,`Wait For Line On Uart` 关键字等的是回执,等效于您在串口窗里拿键盘打字。测试的断言是等 `got 'X'` 和 `got 'h'` 两行回执,咱们看真实的结果:

```text
+++++ Finished test 'wake.Keystroke Wakes Blocked Task C' in 0.98 seconds with status OK
```

tester 记录的两次匹配也贴给您,回执行的原文和时间戳都在里面:

```text
{'Line': "got 'X'", 'Timestamp': 115.38217}
{'Line': "got 'h'", 'Timestamp': 123.08217}
```

两行记录里的 Timestamp 用的是仿真毫秒的口径,X 落在了 115.38,h 落在了 123.08,中间相隔的 7.7 个仿真毫秒里,咱们看着链路走完了一整套事:RX 中断进了 ISR,字节进了邮箱,`ready` 登记了,ISR 退了场,PendSV 接手了切换,task_c 醒来打印了,又睡了回去。链路的每一环都是真的。

第三件就是上一节的撕裂复现,两行计数器原文都贴过了,您要复现的话,改那一行优先级、加上探针、跑同样的 robot 脚本,0x80 之下撕裂的次数是零,0x00 之下它就长起来了。我照实补一句边界:我的探针是在 Renode 的模型里做的,BASEPRI 对外部中断的掩蔽在模型里成立(0x80 之下计数恒零就是证据),它跟真实硅片的行为有没有出入,我手头没有开发板,所以给不出证据。

第四件交代的是边界。host 侧的测试在这个 commit 一个字没动,`test/` 目录里也没有 ThisTask 的测试。`this_task.cpp` 在 CMake 里只挂进了 zeros-bluepill 同一个目标,host 上谁也没有替 `SystemTaskBackend` 盖过这个章的,业务代码要是 include 了 `this_task.hpp` 又想在 host 上链接,得到的会是一个未定义符号。咱们照实说,这就是接线知识每目标恰好一处的另一面:公共的脸是唯一的,后端的章是每个目标自己盖的,host 什么时候盖自己的章,归后面的目标扩展管。

## 下一篇

链路通了,调度器第一次听见了内核之外的世界:字符从宿主的键盘进来,穿过 USART1 的 RXNE、ISR 的邮箱、`ready` 的登记、PendSV 的切换,落进了 task_c 的手里。咱们回过头看 `ThisTask::block` 头上那句注释,“the real event primitives (semaphores and friends) will grow on top of this”,信号量、互斥量、事件组这一整条同步原语的线,地基就是今天这对 `block` 加 `ready` 的原语,后面咱们看着它们一节一节盖起来。咱们写好一篇就挂一篇。
