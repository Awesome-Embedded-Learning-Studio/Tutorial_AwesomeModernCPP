---
title: "Comments Write Lowest, Silicon Judges Highest: Dismantling CMSIS Priority Shifting"
description: "Removing CMSIS from the kernel arch layer: replacing __get/__set_BASEPRI with handwritten inline assembly (with memory clobber on the write side), replacing NVIC_SetPriority + SysTick_Config with volatile pointers directly writing CSR/RVR/CVR and SCB SHPR3 in an anonymous namespace; the climax is a confrontation with the core_cm3.h in the repository —— the old call to NVIC_SetPriority(SysTick_IRQn, 0xF0) calculates 0x00 via (0xF0<<4)&0xFF, making the comment's 'Lowest Level' fall empty on the silicon chip's highest priority, the real one that holds it is the default value inside the next line of SysTick_Config in the library; verification = git grep in include/ZerOS and src/arch for stm32f1xx/core_cm3/assert.h with zero hits (real command and output)"
chapter: 2
order: 2
tags:
  - stm32f1
  - intermediate
  - embedded
  - registers
difficulty: intermediate
platform: stm32f1
cpp_standard: [23]
reading_time_minutes: 18
prerequisites:
  - "Give the Kernel a Second: Part 1: Defining a Second"
related:
  - "Defining a Second: ticks, wrap-around arithmetic, and expiration queues"
translation:
  source: documents/vol8-domains/embedded/zeros/02-time-kernel/02-systick-priority-truth.md
  source_hash: f8df9e4cadec6194b506e3716cb2e565da80a95629c370ff5633b859e2441412
  translated_at: '2026-11-22T13:28:58+00:00'
  engine: qwen3:14b
  token_count: 9174
---
# Critical Section Engine Swap: From CMSIS Macros to Inline Assembly

## Critical Section Engine Swap: From CMSIS Macros to Inline Assembly

Let's start with `cm3_irq.hpp`, the home of the critical section. In the previous stop, we used it to write the BASEPRI nested critical section, with the lock's on/off controlled by CMSIS's `__get_BASEPRI` and `__set_BASEPRI`. In this article, we'll replace them with our own handwritten inline assembly. Why do it ourselves? Special registers like BASEPRI don't reside in regular memory, and `volatile` pointers can't reach them. The only way to touch them is through a pair of instructions, `mrs` and `msr`, which we'll write ourselves. CMSIS wraps this pair of instructions in an entire tree of ST header files, and if the arch layer wants to break clean from ST, it must take them into our own hands. We'll split the new file into two parts, the first part showing two assembly functions:

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

`mrs` and `msr` are the dedicated instructions we've been talking about. `mrs` is responsible for reading, and `msr` writes the value back. One read and one write exactly match the names of the two functions, `read_basepri` and `write_basepri`. Let's look closely at the constraint syntax: `"=r"(value)` declares the output to be assigned to the variable `value`, and `"r"(value)` declares the input to be taken from `value`. The compiler is free to choose the registers.

What's truly worth pausing on is the `"memory"` at the end of the write side. Its purpose is to tell the compiler that this assembly is touching memory, and not to reorder memory access instructions inside the critical section to outside the lock. Why isn't it on the read side? Because reading BASEPRI has no observable side effects. The read action is simply moving a value into a general-purpose register, and the compiler is allowed to execute it at any step, with the program's visible behavior remaining unchanged. Therefore, the read side doesn't need `"memory"` to protect it. The write side is different. It modifies the state of "which memory accesses are blocked," so who is inside the lock and who is outside also changes accordingly. Therefore, every time we reorder, the behavior changes, and we must use `"memory"` to ensure the memory access order is preserved.

The second part of the file is the body of the critical section structure, which we'll look at next:

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

The structure is the same as the previous stop: the nested count is still there, the save and restore path is still there, and `kMaskLevel` is still the old value of 0x40, not a single character has been changed. Only the engine underneath has been changed, with the call points in `lock` and `unlock` replaced by the pair of functions above. The include changes are also at the top of the file: the old version dragged `"stm32f1xx.h"` and `<assert.h>`, while the new version only has one external header left, `<cstdint>`.

## Direct Write to Three Registers

Let's continue with `time_kernel_cm3.cpp`. Before the commit, it looked like this:

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

The trap is buried in these two lines. Let's take a look at `NVIC_SetPriority` and `SysTick_Config`, and the setup from the previous article is here. After the commit, the same function has been replaced with a direct write version. We'll split the new file into two parts for viewing, the first part being the declaration area for register addresses and bit constants:

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

Let's look at this section from top to bottom: four addresses are concentrated in an anonymous namespace. The anonymous namespace gives them internal linkage, and code outside the compilation unit can't see them. `volatile std::uint32_t*` ensures that every read and write actually goes onto the bus, and the compiler is not allowed to cache their values or merge their accesses. The three bit constants are also all named: `ENABLE` occupies the 0th bit, `TICKINT` occupies the 1st bit, and `CLKSOURCE` occupies the 2nd bit. The CSR register at `0xE000E010` is opened by these three.

With the addresses ready, let's continue with the function body:

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

The body of `init_time` has four lines, and we'll look at them one by one. The last line is `CSR = kSystEnable | kSystTickInt | kSystClkSource`, combining three constants to produce a value of 0b111, which turns on both the clock and interrupt switches for SysTick, selecting the processor clock as the clock source, all in one go. The `RVR` line writes `cycles_per_tick - 1`, which is a hardware requirement. The reload value counts up to it before wrapping, so the value given in must be N-1. The `CVR` line writes 0, clearing the current count value. The next clock edge will immediately reload it from RVR, making the period perfectly aligned.

The top line is the main character of this article. Looking at the address table, it writes the SCB's SHPR3 register at `0xE000ED20`. This 32-bit register controls the priority of system exceptions 12 to 15. We split it into bytes, with the highest byte, from bit 24 to 31, belonging to SysTick's priority. Our writing method is to use `0x00FFFFFF` to clear the high byte, then move `0xF0` to bits 24 and above, preserving the old values of the other three bytes.

This line has the same intention as the one it replaced, `NVIC_SetPriority(SysTick_IRQn, 0xF0)`, but there's a key difference: the number 0xF0 appears directly in our own source code, and it ends up in the high byte of the register as 0xF0, with no one in between replacing it for us. The replaced line had a CMSIS conversion formula in between. In the next section, we'll look at what that formula did.

## Confrontation in Court: Line 1639 of core_cm3.h

`core_cm3.h` is located in the `third_party/STM32CubeF1` submodule of the repository, with the full path being `Drivers/CMSIS/Include/core_cm3.h`. The board-level CMake adds this directory to the include path, and the old `#include "core_cm3.h"` refers to it. We'll flip to line 1639 of it, and the full implementation of `__NVIC_SetPriority` is here:

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

SysTick is a system exception on the Cortex-M3, and its interrupt number being a negative number is written in the enumeration (`SysTick_IRQn = -1`), so it goes through the else branch, writing to the `SCB->SHP` array. Calculating `((uint32_t)(-1) & 0xF) - 4`, we get 11, and `SHP[11]` is the highest byte of SHPR3, with the address at `0xE000ED23`, which is the same byte as the direct write version. There's no disagreement on this point between the two sides.

The disagreement comes entirely from the conversion formula: it shifts `priority << (8U - __NVIC_PRIO_BITS)` and then truncates it with 0xFF. Let's look at the value of `__NVIC_PRIO_BITS` in the device header file `third_party/STM32CubeF1/Drivers/CMSIS/Device/ST/STM32F1xx/Include/stm32f103xb.h` at line 51:

```c
#define __NVIC_PRIO_BITS           4U       /*!< STM32 uses 4 Bits for the Priority Levels    */
```

STM32F1's NVIC only implements the high 4 bits of priority in hardware, and it doesn't support the lower 4 bits. CMSIS's idea is: you give me a logical level, and I'll align it to the hardware's high bits for you. The idea sounds quite considerate, but let's substitute the old call's argument of 0xF0 into it and do the calculation manually:

We give `0xF0` to the formula, and the first thing we see is itself:

```text
0xF0            = 240        = 0b 1111 0000
```

We shift it left by `8 - 4 = 4` bits:

```text
0xF0 << 4       = 0xF00      = 3840      = 0b 1111 0000 0000
```

We then truncate it with 0xFF, keeping only the low 8 bits:

```text
0xF00 & 0xFF    = 0x00       = 0
```

We calculate all the way to the end and get the value 0x00. What's written into `SHP[11]` is 0.

On our development computer, we quickly wrote a small program to run this arithmetic, just to avoid miscalculating:

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

The output we got is as follows:

```text
requested = 0xF0
0xF0 << 4      = 0xF00
(..) & 0xFF    = 0x00
SysTick_Config default ((1<<4)-1)=0xF -> reg 0xF0
```

It's consistent with our manual calculation. The last line is an extra one I added on the fly: it runs the default level of 15 inside `SysTick_Config` through the same formula, resulting in the value 0xF0 in the register. This line of output will be used in the next section. The first two lines expose the problem here: the priority semantics of Cortex-M3 are that the smaller the number, the more urgent it is, and 0 is the highest level. When I wrote `// Lowest Level` earlier, I was thinking: "0xF0 in STM32's 4-bit implementation is level 15, the lowest level," but this number, after going through the CMSIS formula, comes out as 0x00, turning the lowest into the highest.

What about the critical section? Let's recall the BASEPRI judgment from the previous stop: after writing 0x40 into BASEPRI, all exceptions with priority numbers greater than or equal to 0x40 are blocked, while exceptions with priority 0 are never blocked — this is a hard judgment set by the architecture, and BASEPRI is designed not to mask the highest priority. So if SysTick is really running at 0x00, then when it comes in, our nested critical section is as good as nothing: no matter how tight you lock, the time-based interrupt will still pierce through the middle of the lock, and the concurrent protection of the time queue between `on_elapsed` and the task side is completely compromised.

::: warning
CMSIS's `NVIC_SetPriority` receives a logical level, and it does the calculation `(priority << (8 - __NVIC_PRIO_BITS)) & 0xFF` for you. The byte you want to write directly into the register must be deduced by you. If you want to write directly into the register's byte, you can't use this interface. If you want to use this interface, you must provide numbers according to the logical level's standard. If we mix up the understanding of both sides, the value we get will differ by a full circle from what we expect.
:::

## Why Didn't the Old Code Explode

At this point, you might be wondering: since the priority was written as 0x00, why did the ticker on the board run well in the previous stop? If the critical section was really compromised, the time queue should have been messed up.

Well, I was also very confused at the time. Why didn't it explode? The answer is in the next line of the old function. `SysTick_Config` itself also calls `NVIC_SetPriority` internally. Let's look at its implementation starting from line 1834 of `core_cm3.h`, excerpting the four lines of priority and configuration, skipping the first few lines of out-of-bounds checks:

```c
  SysTick->LOAD  = (uint32_t)(ticks - 1UL);                         /* set reload register */
  NVIC_SetPriority (SysTick_IRQn, (1UL << __NVIC_PRIO_BITS) - 1UL); /* set Priority for Systick Interrupt */
  SysTick->VAL   = 0UL;                                             /* Load the SysTick Counter Value */
  SysTick->CTRL  = SysTick_CTRL_CLKSOURCE_Msk |
                   SysTick_CTRL_TICKINT_Msk   |
                   SysTick_CTRL_ENABLE_Msk;                         /* Enable SysTick IRQ and SysTick Timer */
```

The second line's call is `NVIC_SetPriority(SysTick_IRQn, (1 << 4) - 1)`, with the argument being 15. 15 goes through the same formula: `(15 << 4) & 0xFF` = `0xF0`. The 0x00 I wrote in, didn't last for even a single instruction, and was overwritten back to 0xF0 by the library's default value — this time it's really the lowest priority, and BASEPRI 0x40 can block it, and our critical section is working.
