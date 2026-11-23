---
title: "Balancing Interrupt Priorities, Letting Interrupts Knock: ThisTask and the First Wake-up Path"
description: "0e8ce6d(commit description has a spelling error add this tasl, quoted as is) gives the application a public face of the current task: ThisTask::sleep_for/yield/block goes through extern template single-point wiring, concept checks moved to the backend implementation header; at the same time, the first real interrupt wake-up path is connected, USART1 RX interrupt enters a byte mailbox and then goes to ready, only pend no switch; the hard knowledge is that NVIC priority must be pressed to 0x80 to cover the BASEPRI 0x40 critical section, I added a tear probe in Renode myself, changed the priority back to 0x00 and reproduced the interrupt entering the critical section on the spot; checkout and type characters on the serial port, sleeping tasks wake up immediately to receive bytes"
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
  - "Who is the most urgent, who steps up: Ready Bitmap, Task Three States and Selection Arbitration"
  - "Faking a Birth Certificate at the Stack Top: PendSV, SVC and the First Switch"
related:
  - "The Bug of Sleeping: Lost Wake-up, park_for and the Builder Chain That Prevents Compilation of Incorrect Usage"
current_status:
  title: Interrupts Have Learned to Knock
  detail: The ZerOS chapter is currently being polished
  variant: polishing
  icon: 🔔
translation:
  source: documents/vol8-domains/embedded/zeros/03-first-scheduler/04-thistask-and-irq-wakeup.md
  source_hash: be18d2af0da2f3d9b0cf3dbb5d3cf5fdaf2a254cbeb94c5f1e73eec0baf32437
  translated_at: '2026-11-22T13:37:26+00:00'
  engine: qwen3:14b
  token_count: 18196
---
# Balancing Interrupt Priorities, Letting Interrupts Knock: ThisTask and the First Wake-up Path

We mentioned at the beginning of this chapter that the scheduler can switch tasks, but there is still one thing left to do: the first path for interrupts to wake up sleeping tasks. We mentioned it in the opening words, and the mechanism article mentioned it again when talking about ICSR, saying that the delayed switching design needs to be realized on the interrupt wake-up path. Today's article will realize them. After the previous article "The Bug of Sleeping: Lost Wake-up, park_for and the Builder Chain That Prevents Compilation of Incorrect Usage" finished explaining `park_for` and the type state defense, the kernel itself is now stable, but it has never been knocked on by a real external event. What wakes it up is either the clock or a placeholder in the test that only counts.

Today's commit is `0e8ce6d`, and the subject is "feat: add this tasl". You didn't miss it, task is spelled as tasl, which is an original spelling error in the repository, and I quote it as is, without any embellishment. It touched 9 files, added 246 lines, deleted 28 lines, and did two things: one was to give the application code a public face of "current task" `ThisTask`, and the other was to connect the first real interrupt wake-up path. When USART1 receives a byte, the sleeping task wakes up immediately to receive the byte. Along the way, there is a hard knowledge that can be applied in production: the interrupt priority of USART1 must be set to 0x80, which is greater than the critical section's BASEPRI 0x40. If this balancing is missing, the default 0 will break into all critical sections. We will demonstrate this in Renode in front of it.

## Giving the Application a "Current Task" Face

When a task wants to sleep, how does it know who it is? Before the previous article, the answer was in `system_sched.current_task()`, and the application code directly reached into the scheduler's singleton. `0e8ce6d` took back the action of reaching in and replaced it with a public face. Let's look at the new header file `include/ZerOS/this_task.hpp`:

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

The comments at the top of the header file clearly state the agreement, and the business code is only allowed to include this header. Which silicon chip receives the call is the secret of the wiring party. `Milliseconds` is a bare structure with only one member, `count`. Looking at the comment on top, it says zero overhead, milliseconds are just milliseconds. We will not mix it with ticks. Before, when we manually wrote `task_sleep`, we passed a bare `std::uint32_t`, and the question of whether it was milliseconds or tick counts was not visible in the type. Now this structure removes the ambiguity of the unit.

The constructor was deleted by `= delete`, and the comment says "a face, not a thing: no instances, ever". A face is used to call, and no one can create such a face. All three members are `static`, and if anyone instantiates `ThisTask`, the compiler will stop them at the line where they do it. The comment on top of `block` is worth a closer look, it says "bare 'sleep until someone calls me'", and the real event primitives such as semaphores and mutexes will be built on top of it in the future. This line of comment is the seed for the synchronization primitives we will see later, and we will return to it at the end.

The last two lines are the hub of the wiring. `extern template` is a declaration of explicit instantiation, meaning that the body of the template lives in another compilation unit. When the business code's `.o` file sees it, it generates only a regular external call. `using ThisTask = BasicThisTask<detail::SystemTaskBackend>` gives the application a name without template parameters, and `SystemTaskBackend` is only a forward declaration in the public header, not even telling whether it is a struct or class. Its real body lives in which file, we will see in the next section.

## Concept Checks Fall in the Backend's Complete Place

The body of the template is not in the public header. We found it in `src/system/this_task_impl.hpp`, which is private and only the target's `.cpp` and internal kernel tests will touch it. This privacy is intentional: checking the backend's concept is only meaningful when the backend's real body is visible. The public header's `SystemTaskBackend` only has a forward declaration, and nothing else is mentioned. If the check is placed there, the concept can only be judged as不合格, and the error we get has nothing to do with the real problem. Moving the check to the backend's complete implementation header, if any function signature is written wrong or any `noexcept` is missing, the error will directly appear on the specific line. The first part is the acceptance concept:

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

We look at this `TaskBackend`, which requires the backend to do three things, and all three signatures must be `noexcept`. This choice is on the same road as the one in the arbitration article that uses concept as an interface document. That article used concept to write a contract for people to see, and this one manages where the error is reported.

Following are three template bodies, written in the same mold. Let's take a quick look:

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

`static_assert` guarded the door once, and then we passed the call to the backend. The business code calling `ThisTask::sleep_for` generates a regular call to an external symbol, and there is no target-related knowledge in the header file.

## Wiring and Backend: One per Target

The real body is located in `src/arch/arm_cortex_m3/this_task.cpp`. Its exclusive knowledge is wiring: where the two singletons live, how sleeping tasks are woken up, and the three things the backend needs to do all rely on such knowledge. Such knowledge is one per target. Let's start from the beginning, the first part is include and anonymous wake-up function:

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

`wake_from_sleep` is actually an old friend we've seen before. It was initially a function we manually wrote in the example, and in the previous article, it was moved into the `switch.cpp` of the arch layer. This article followed the wiring knowledge into `this_task.cpp`, and the work of deducing back to the TCB from the embedded `TimeWaiter` via the `owner_of` pointer subtraction has not changed, and the knowledge of the two singletons has also been taken over by the backend.

In the middle is the real body doing the backend work. Let's look at it:

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

We look at the backend of `sleep_for`, which calls the scheduler's own `sleep_for`, which is the version used in the previous article with `park_for` to put the defense and blocking in the same critical section. The soil for hand-crafted race conditions has disappeared from the application side. `yield` and `block` are direct calls, and `block` hands the current task directly to the scheduler. After the task goes to sleep, the `ready` that wakes it up comes when it does.

Finally, there is a line for stamping, let's look at it:

```cpp
namespace ZerOS {
// stamp the face for THIS program; business .o files link plain calls
template struct BasicThisTask<detail::SystemTaskBackend>;
} // namespace ZerOS
```

The last line `template struct BasicThisTask<detail::SystemTaskBackend>;` is the explicit instantiation definition. The comment's use of "stamp" is vivid: this compilation unit has stamped a face for this program, other `.o` files only need to call, and the linker naturally connects the two halves. The benefit of this pattern becomes apparent when switching boards. If we move this kernel to another board one day, the new `this_task.cpp` can be written in the same structure, and we just need to change the names of the two singletons, and we don't need to change a single line of application code.

We also need to take a look at where it moved from. The changes to `switch.hpp` in this commit removed the declaration of the arch layer's `sleep_for`, and the comment said that the application side's sleep/yield/block went to `<ZerOS/this_task.hpp>` (the D20 in the comment is the internal design document number), and this layer only has assembly left. The arch layer now only has assembly-time actions such as `spawn` and `start_scheduler`, and application-side requests such as sleep are managed by the public face, and the layers are cleanly separated.

## Handing USART1's RX to Interrupts

The other end of the link is the serial port. `uart.hpp` in this commit has been upgraded from pure TX polling to RX interrupt reception. Let's take a look at the new version of `uart1_init`:

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

PA10 has been changed to floating input (`CNF10_0` only sets the lowest bit of CNF to 1, and MODE10 remains 00), the pin is neither pushed nor pulled, and the level is handed over to the other side's TX. The baud rate is still the original 115200. `CR1` has two more bits compared to the previous version, `RE` and `RXNEIE`. `RE` opens the reception, and `RXNEIE` is the switch that lets "receiving a byte" trigger an interrupt. The comment's explanation of ORE is an easy-to-ignore corner: overflow (the previous byte hasn't been read yet, and a new byte comes) also uses the same switch `RXNEIE`, so our written handler cannot assume that an interrupt means a byte is present, we need to check the RXNE bit in the status register ourselves. When we write to the handler, we will see this self-judgment in the code.

The two lines in the middle are the most valuable lesson in the entire commit, and the comment itself has already explained the reason. We will expand on it in the next section. The NVIC priority register is an array of one interrupt per byte, with the base address at 0xE000E400. USART1's interrupt number is 37, and we directly wrote 0x80 to the 37th byte. The enable bit is in ISER1, which is responsible for IRQ32 to 63. 37 minus 32 gives 5, and we wrote bit5. These are all raw register writes that bypass CMSIS. The judgment from the previous article that dissects the CMSIS shift formula is continued here: the numbers in the source code are the numbers in the register, and no one is replacing them for us.

The vector table has also been expanded. The `vectors` array in `startup.cpp` has been expanded from 16 slots to 54, with the expanded slots being the seats of on-chip peripherals. USART1's interrupt wants to have its own handler in the table, so the table must have its seat. If the seat is empty, an erroneously triggered interrupt won't even leave us a line for debugging, so all the newly expanded slots are hung on the same `fault_handler`. We'll look at the expanded part one by one (the P4 in the comment is also an internal design document number, and it's part of the same series as D20), the first 16 new seats are all hung on the same backup:

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

From 16 to 52, the 37 empty slots are all hung on the same `fault_handler`, which does little more than print a line `[FAULT] halted` and stop. The comment explains the reason: unenabled interrupts won't come, but if there's a mis-triggered one, the `nullptr` slot on Cortex-M3 will result in a lockup (fetching an invalid address, the core will directly stop, leaving no trace), and the backup print at least leaves us a line to check. `fault_handler` itself is a dead loop plus WFI in `extern "C"`, and we won't paste it separately.

The next 16 seats are also backups, and we'll look at them as they are:

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

The algorithm for the seats is uniform. The first 16 slots in the Cortex-M3 vector table are for kernel exceptions, and on-chip peripheral interrupts start from slot 16. We add 16 to the IRQ number of the peripheral to get its seat number in the table.

The last few seats at the end of the table are for the real handlers. Let's look at them:

```cpp
    &fault_handler,
    &fault_handler,
    &fault_handler,
    &fault_handler,
    &fault_handler,     // 48-52
    &USART1_IRQHandler, // 53 USART1 全局中断(RX 事件唤醒 demo)
};
```

USART1's seat is calculated like this: its global interrupt number is IRQ 37, adding the first 16 slots gives 53, which is its seat in the table. Looking at the last line, it's exactly `USART1_IRQHandler` that's sitting there.

## Balancing: The Relationship Between 0x80 and 0x40

Now we'll expand on the reasoning in those two lines of comments. Let's review the judgment we made in the previous article: after writing 0x40 to BASEPRI, all exceptions with priority values greater than or equal to 0x40 are blocked, while exceptions with priority 0 are always unblocked. This is a hard rule set by the ARMv7-M architecture, and BASEPRI is designed not to mask the highest priority. The Cortex-M3's priority scale is smaller values mean higher urgency, and STM32F1 only uses the high 4 bits, so the chip recognizes 16 levels from 0x00 to 0xF0 in steps of 0x10.

This scale falls on the interrupt wake-up path, forming a required balancing relationship. The critical section engine is the `CortexM3CriticalSection` from the previous article, with the lock's switch written as BASEPRI 0x40. The scheduler modifies the ready queue, and the task writes to the mailbox, both relying on it. Its implicit assumption is "when I'm locked, no one will touch these shared things." USART1's interrupt will touch: its handler writes to the mailbox and calls `ready` to modify its ready queue. If USART1's priority is 0x80, which is greater than 0x40, when the task side adds a lock, BASEPRI rises to the level, and it is covered, only to wait until the lock is released. If it runs at the default 0x00, which is smaller than 0x40, no critical section can cover it. The scheduler modifies the bitmap and FIFO halfway, and it breaks in, modifies the same data, and then retreats, leaving us with a queue that no one has ever seen. So the priority values must be aligned: USART1's 0x80 must be greater than 0x40, and only then can the BASEPRI cover it.

::: warning
This balancing is an arithmetic that must be redone every time a new interrupt is connected. A one-time configuration can't cover it for life. The BASEPRI shielding level is 0x40, and any interrupt that touches kernel data must have its priority value pressed into the range of 0x40 to 0xF0. If any interrupt really has a reason to be exempt from the critical section (such as an emergency signal that must be hard real-time responsive), the price it pays is that its handler cannot touch any kernel data. The agreements on both sides must appear in pairs. If either side changes any half, we're tearing the same hole.
:::

## ISR Only Registers, Doesn't Switch

The last link of the chain is in the `main.cpp` of the example. Let's look at its new role in full. The shared items are a one-byte mailbox and a lock:

```cpp
// the one-byte mailbox: ISR writes, task C drains; BASEPRI on both hands
constinit ZerOS::arch::cortex_m3::CortexM3CriticalSection mailbox_lock{};
char g_rx_byte = 0;
bool g_rx_pending = false;
ZerOS::sched::TCB* g_task_c = nullptr; // set in main, read by the ISR
```

This mailbox configuration is as small as it can be, containing one byte, a marker indicating whether new goods have arrived, and finally a BASEPRI lock. The comment's "BASEPRI on both hands" is the key to the entire section. The mailbox's two hands (the ISR that writes it and the task that reads it) both hold the same lock. We'll see both sides soon. `g_task_c` is a pointer filled in main and read by the ISR. The ISR needs to deliver the wake-up registration to the specific task, and it must know who it's waking up.

The sleeping task waiting for events is the new task_c. Let's look at its loop:

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

Its loop is completely different from the clock-sleeping rhythm of task_a and task_b. `ThisTask::block()` is a bare "sleep until someone calls me", with no timer defense. Only `ready` can wake it up. After waking up, it grabs the lock, takes the byte, clears the marker, and writes a receipt like "got 'X'", then goes back to sleep. Its priority is 0, more urgent than task_a and task_b's 1. When the interrupt comes, it becomes ready, and the `ready` judgment requests a switch, the printing task immediately yields, and the byte doesn't have to wait for the next beat.

Let's look at the handler, which is the real pair of hands on the silicon in the wake-up chain:

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

The first look at SR's RXNE is the self-judgment预告ed in the init comment: overflow also uses RXNEIE to trigger an interrupt, but if there's no new byte, we just return. `uart1_getc` reads the data register, and the hardware clears RXNE. The section that writes to the mailbox holds `mailbox_lock`, and the promise made in the comment is fulfilled here. The next is `ready`, and the comment says "pends PendSV, never switches here", only PendSV is queued, and no switching is done inside the interrupt.

This line is where the promise left in the arbitration article is fulfilled. That article said that the scheduling API never directly switches on the call site, and the real switch is postponed to PendSV. At that time, we only saw the counter in the test stub. Now `request_switch` writes to ICSR's bit28, PendSV queues, and after the ISR exits, the hardware tail chain switches. The entire set of delayed switching mechanisms runs on the real interrupt path. Why delay? Let's think of one layer: the interrupt handler itself is on the MSP, and when it exits, the hardware has a whole set of push and pop processes to go through. If it changes the PSP and r4 to r11 inside it, it's like tearing down someone else's house. After registration, it retreats, and the switch is handed over to PendSV, which specializes in this. The order is clean.

We'll just mention a few points about the assembly in main: three tasks are set up in factory chains, task_c's priority is given 0, `spawn` queues them one by one, and `g_task_c = &c` after that, the pointer is handed over to the ISR. The order of initialization itself is an agreement. The ISR comes only after being enabled, and `uart1_init` is placed at the very beginning, and the pointer is only in place after `spawn`. This order has a boundary that must be stated honestly: during the setup phase, if a byte comes in, `g_task_c` is still empty at that moment, the byte enters the mailbox, but the task that should receive it doesn't exist yet, and it's lost. The empty check at the beginning of the ISR only ensures the system doesn't crash, but the loss of the byte is unknown to it. No one types during the setup phase of the demo, so this hole hasn't exposed itself.

## Changing the Priority Back to 0: Tearing It Once on Site

The phrase "default 0 will break into all critical sections" is not spoken in vain. I changed the line in `uart.hpp`, changing `kNvicIp[kUsart1Irq] = 0x80` to `0x00`, recompiled the firmware, and ran it for you to see.

I'll honestly admit a fact I encountered right at the start: in a clean three-task demo, this flaw is not visible. I sent 24 different characters from the host side into the serial port, waiting for receipts one by one. The firmware with priority 0x00 delivered all 24 bytes, and the system was still alive. It's not strange once the idea is clear: the torn window is the dozens of instructions on the task side holding the lock, which is a few hundred nanoseconds on the 72MHz simulation timeline. The character must arrive exactly in this window to be an issue, and the probability is so low that it's not encountered in the simulation. The existence of the flaw is one thing, and the probability of it encountering a specific load is another.

So I added a probe to the demo, turning the probability of encountering it into a certainty. This is my own experimental code, not part of `0e8ce6d`, and its shape is as follows:

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

The probe task is given a priority of 31 (the lowest real level), and it spins in the background, almost every moment holding the lock. It can complete the number of entries per second more than 7,000 times. I added a line in the USART's ISR: if `g_in_lock` is true, the counter is incremented. The meaning of this counter is precise: every time it increments, it means that the ISR was allowed to pass while the critical section was open, and `ready` and everything after it were executed while the task side was in the middle of modifying. Under the 0x80 balance, every moment the lock is held, BASEPRI is 0x40, USART's 0x80 is covered, and it's suspended, waiting for the chance to be allowed. This counter should be zero. Changing it back to 0x00, it grows. I added two counters to task_a's print, reporting once every second. The test vehicle is the robot test framework that comes with Renode. The host side sent 10 characters into the serial port, and we received heartbeats, each configuration received one line, each line with three readings. The g1 to g3 posted are three lines of heartbeat captured by the robot script's three Waits. Task_a's heartbeat reports once every second, so the three lines correspond to three consecutive seconds:

```text
0x80:g1=A 0 0 g2=A 7122 0 g3=A 14244 0
0x00:g1=A 0 0 g2=A 7122 5 g3=A 14244 10
```

The format of the two lines is "A entry count tear count". When the balance is correct, the entry count rises to 14,000, and the tear count remains zero, with no character being processed while the lock is held. Changing the priority back to 0x00, we use the same probe and send the same 10 characters, and the tear count rises from 5 to 10, with each character breaking in while the critical section is open. The BASEPRI of 0x40 is just a piece of paper in front of 0x00. The judgment from the previous article, which said that priority 0 exceptions are always unblocked, becomes a counter we can see at a glance here. When I saw 5 and 10, I was a bit happy, I won't hide it. The "scene where the queue is torn" that I promised to show you in the previous article is now in the form of a counter, and we'll honestly record it, and the actual verification of the queue being truly written bad for the readers will have to wait until we run the firmware on a real development board.

::: warning
The counter in the probe is the lower bound proof that "ISR and task see each other", it proves that the BASEPRI cannot block the 0x00 architectural fact in the Renode model, but it does not prove that every tear necessarily corrupts the data. On a real board, whether it's bad or how bad it is depends on which byte the task side was modifying at the moment of the tear. It may be fine in nine out of ten times, and the tenth time the list is broken. Production code cannot live on the assumption of high probability of being fine. If we want the balance to be checked, at least we need a line of comment that clearly states the relationship between the numerical values, which is what is written in `uart.hpp`.
:::

## Acceptance

Let's summarize the verifiable facts from this article, totaling four.

The first thing to verify is the build. I checked out to `0e8ce6d` in the /tmp copy (the working tree under third_party is a gitfile, and `.git` is just a file pointing to the real repository, I exported the tree with `git archive 0e8ce6d`, and the STM32CubeF1 submodule also did the same export), and built it with the cross-toolchain:

```shell
cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/arch/arm-none-eabi.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The real volume report is as follows, let's look at the numbers:

```text
   FLASH:        2408 B        64 KB      3.67%
     RAM:        1200 B        20 KB      5.86%
   text	   data	    bss	    dec	    hex
    2216	     192	   1008	   3416	    d58
```

Compared to the 1680 bytes of the mechanism article, this version added more than 700 bytes to the interrupt path. We can count the cost: the vector table was expanded by 38 slots, and the RX handler and ThisTask backend are inside. The budget line is still the core flash 6KB and RAM 1KB from the project's launch, and we are still far away from it.

The second thing to verify is typing characters on the serial port. Renode 1.17.0 doesn't recognize `bluepill.repl`'s `I2C.STM32F4_I2C`, just like the mechanism article encountered. I commented out the i2c1 and i2c2 nodes entirely, and the demo doesn't go through the i2c path, so the behavior is unaffected. I used Renode's robot test framework (`renode-test`) to type characters, it provided a terminal tester for the serial port, and the host-side `Write Char On Uart` keyword sent characters into the simulated serial port, and the `Wait For Line On Uart` keyword waited for the receipt, equivalent to you typing on the serial port window with your keyboard. The test's assertion was to wait for the two lines of receipt `got 'X'` and `got 'h'`, let's look at the real result:

```text
+++++ Finished test 'wake.Keystroke Wakes Blocked Task C' in 0.98 seconds with status OK
```

The tester's two matches are also posted for you, with the original text and timestamps inside:

```text
{'Line': "got 'X'", 'Timestamp': 115.38217}
{'Line': "got 'h'", 'Timestamp': 123.08217}
```

The two lines of records have Timestamps in the simulation millisecond scale, X falls at 115.38, h falls at 123.08, and in the 7.7 simulation milliseconds between them, we see the entire chain of events: RX interrupt enters the ISR, the byte enters the mailbox, `ready` registers, the ISR exits, PendSV takes over the switch, task_c wakes up to print, and then goes back to sleep. Every link in the chain is real.

The third thing is the tear reproduction from the previous section, the two lines of counter original text have already been posted. If you want to reproduce it, change that line of priority, add the probe, and run the same robot script. The number of tears under 0x80 is zero, and it grows under 0x00. I honestly add a boundary: my probe was done in the Renode model, and the masking of BASEPRI to external interrupts is valid in the model (the evidence that the count is always zero under 0x80). Whether it differs from the behavior of real silicon, I don't have a development board on hand, so I can't provide evidence.

The fourth thing is to explain the boundary. The host-side test hasn't changed a single word in this commit, and there is no ThisTask test in the `test/` directory. `this_task.cpp` is only linked to the same target as `zeros-bluepill` in CMake, and no one on the host has stamped this seal for `SystemTaskBackend`. If the business code includes `this_task.hpp` and wants to link on the host, it will get an undefined symbol. We honestly say that this is the other side of the fact that the wiring knowledge is exactly one per target: the public face is unique, and the backend's seal is stamped by each target itself. When the host stamps its own seal, it's up to the target extension to manage.

## Next Article

The path is open, and the scheduler has heard the world outside the kernel for the first time: characters come in from the host's keyboard, through USART1's RXNE, the ISR's mailbox, `ready`'s registration, PendSV's switch, and fall into the hands of task_c. Looking back at the comment on top of `ThisTask::block`, "the real event primitives (semaphores and friends) will grow on top of this", the entire line of synchronization primitives such as semaphores, mutexes, and event groups has this pair of `block` plus `ready` primitives as its foundation. We will write one article at a time and post it.
