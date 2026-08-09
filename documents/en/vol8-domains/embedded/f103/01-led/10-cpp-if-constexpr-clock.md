---
chapter: 15
difficulty: beginner
order: 10
platform: stm32f1
reading_time_minutes: 8
tags:
- beginner
- cpp-modern
- stm32f1
title: 'Part 15: Third Refactor — `if constexpr` Enables Compile-Time Selection of
  Clock Enable'
description: ''
translation:
  source: documents/vol8-domains/embedded/01-led/10-cpp-if-constexpr-clock.md
  source_hash: 0884f936930fc60cacb09e3b7d53507d67bad107aaa4e4435856fe3af9388768
  translated_at: '2026-06-16T04:09:59.631209+00:00'
  engine: anthropic
  token_count: 1432
---
# Part 15: The Third Refactor — `if constexpr` Automates Clock Enable at Compile Time

> Following the previous post: The GPIO template skeleton is ready, but clock enable remains unsolved. The core issue is that `__HAL_RCC_GPIOA_CLK_ENABLE()` and `__HAL_RCC_GPIOB_CLK_ENABLE()` are different macros; they expand to write to different register bits. We cannot use a "generic" runtime function to select between them. The solution is `if constexpr`—compile-time conditional branching introduced in C++17.

---

## Problem: Why We Can't Select Clock Macros at Runtime

You might think, why not just write a `switch` statement?

```cpp
void enable_clock(Port port) {
    switch (port) {
        case Port::A: __HAL_RCC_GPIOA_CLK_ENABLE(); break;
        case Port::B: __HAL_RCC_GPIOB_CLK_ENABLE(); break;
        // ...
    }
}
```

This looks reasonable, but it has two problems. The first is waste: `Port` is a template parameter, a compile-time constant. Handling a compile-time constant with a runtime `switch` forces the compiler to generate code for branches that are "never taken." While the optimizer might eliminate the extra branches, you can't guarantee this—especially when the macro expansion contains `volatile` writes.

The second problem is more subtle: the clock enable macros expand to include write operations to the `RCC` register. `volatile` tells the compiler, "This memory location might be modified by hardware, so do not optimize accesses to it." When analyzing the `switch`, the compiler cannot determine that only one branch will be executed—from its perspective, the `port` parameter could be any runtime value. Therefore, the compiler may refuse to optimize away those "never executed" `volatile` writes.

`if constexpr` is completely different. The compiler knows the value of `Port` at compile time and directly discards the non-matching branches. Only the matching branch is compiled into the final binary.

---

## Deep Dive into `if constexpr` Syntax

`if constexpr` is a feature introduced in C++17. Its syntax is:

```cpp
if constexpr (condition) {
    // Branch A
} else {
    // Branch B
}
```

The difference from a normal `if` statement is this: both branches of a normal `if` are compiled into the binary, and the CPU selects which one to execute at runtime based on the condition. With `if constexpr`, only the branch satisfying the condition is compiled; the other branch is completely discarded at compile time—leaving no trace of it in the generated binary.

Even more powerfully, the discarded branch doesn't even need to be syntactically valid C++ code (in some cases)—because the compiler never analyzes it. This is known as "compile-time branch discarding."

---

## Complete Implementation of `GPIOClock`

In the `GPIO` class, clock enable is encapsulated as a private nested class. This is the most ingenious part of the entire template design:

```cpp
class GPIO {
    // ... (Port and Pin definitions)

    class GPIOClock {
    public:
        static void enable() {
            if constexpr (Port == Port::A) {
                __HAL_RCC_GPIOA_CLK_ENABLE();
            } else if constexpr (Port == Port::B) {
                __HAL_RCC_GPIOB_CLK_ENABLE();
            } else if constexpr (Port == Port::C) {
                __HAL_RCC_GPIOC_CLK_ENABLE();
            } else if constexpr (Port == Port::D) {
                __HAL_RCC_GPIOD_CLK_ENABLE();
            } else if constexpr (Port == Port::H) {
                __HAL_RCC_GPIOH_CLK_ENABLE();
            }
        }
    };

public:
    // ... (setup method)
};
```

Let's unpack the design intent of this code layer by layer.

First is the nested class design. `GPIOClock` is placed in the `private` section of the `GPIO` class, so it cannot be called directly from outside. It is an "internal implementation detail" of GPIO—users of GPIO don't need to know how the clock is enabled, they just need to call `setup()`. This idea of "encapsulating implementation details" is very common in C++, and nested classes are a natural way to achieve it.

Next is the `enable()` function. `static` means it can be called without an instance of `GPIOClock`, accessed directly via `GPIOClock::enable()`. `[[maybe_unused]]` suggests the compiler embed the function body directly at the call site—in embedded development, such short functions of only a few lines are almost always inlined, avoiding function call overhead.

The core is the condition of `if constexpr`. `Port == Port::A` is a compile-time constant expression—because `Port` is a template parameter, it is known at compile time. The compiler checks these conditions one by one, keeping only the branch that evaluates to true.

When the template is instantiated as `GPIO<Port::C, Pin5>`, the compiler sees that `Port == Port::C` is true, so only `__HAL_RCC_GPIOC_CLK_ENABLE()` is compiled into the code. The other four branches (A, B, D, H) are completely discarded at compile time. If you use `objdump` to disassemble the final `.elf` file, you will find only one clock enable call—no conditional jumps, no `switch` jump table, just a direct register write instruction.

⚠️ **Note:** The condition in `if constexpr` must be a compile-time constant expression. If you try to use a runtime variable (like a function parameter) as the condition, the compiler will error. This limitation is actually a good thing—it ensures the branch decision is fixed at compile time and won't secretly introduce runtime overhead. If you genuinely need runtime selection, then templates aren't the right design tool.

---

## How `setup()` Uses `GPIOClock`

```cpp
void setup() {
    GPIOClock::enable();  // 1. Enable clock first
    // ... (configure mode, speed, etc.)
}
```

`GPIOClock::enable()` is the first line called in `setup()`. Because `setup()` itself is a method of a template class, when the compiler instantiates `GPIO<Port::C, Pin5>`, it expands the entire call chain:

1. `GPIO<Port::C, Pin5>::setup()` → `GPIOClock::enable()` → `__HAL_RCC_GPIOC_CLK_ENABLE()`
2. `GPIO<Port::C, Pin5>::setup()` → `MODER` configuration
3. `GPIO<Port::C, Pin5>::setup()` → `OSPEEDR` configuration

The final compiled code for `GPIO<Port::C, Pin5>` is identical to hand-written C code—clock on, configure pin, zero extra overhead.

Another point to emphasize: the condition in `if constexpr` must be a compile-time constant expression. If you try to use a runtime variable (like a function parameter) as the condition, the compiler will error directly. This restriction is actually beneficial—it ensures branch decisions are made at compile time, preventing the introduction of hidden runtime costs. If you really need runtime clock selection, use the traditional `switch`, but that is not the design goal of templates.

---

## Why Not Other Solutions

**Template specialization** is a classic approach, but it requires writing a specialization for each port:

```cpp
template<> struct GPIOClock<Port::A> {
    static void enable() { __HAL_RCC_GPIOA_CLK_ENABLE(); }
};
template<> struct GPIOClock<Port::B> { /* ... */ };
```

This works, but the code is scattered across multiple places—five specializations mean five separate code blocks. `if constexpr` centralizes all logic in one place, allowing you to see how every port is handled at a glance. Maintenance requires changing only one spot.

**Runtime array indexing** is another idea—manipulating registers directly without HAL macros:

```cpp
constexpr uint32_t rcc_bases[5] = { /* ... */ };
*RCC_BASE[port] |= (1 << bit);
```

But this bypasses the HAL, and HAL macros might do extra work (like memory barriers, waiting for clock stabilization, etc.). Direct register manipulation might miss these details, potentially causing instability in certain clock configurations. Where you can use HAL macros, use them—this is the pragmatic choice in embedded development.

Therefore, `if constexpr` is the most elegant solution: logic centralized in one place, determined at compile time, works perfectly with HAL macros, and easy to maintain.

---

## Verifying the Compilation Output

We can use `objdump` to check the compiled code and verify the effect of `if constexpr`. For a `GPIO<Port::C, Pin5>` instance, in the disassembly we should see only the instruction corresponding to Port C—a write to the `AHB1ENR` register (address `0x40023830`) setting bit 4 (`IOPCEN`) to 1.

```text
; Disassembly of GPIO<Port::C, Pin5>::setup()
ldr  r3, [pc, #offset]  ; Load RCC base address
ldr  r3, [r3, #0x30]    ; Read AHB1ENR
orr  r3, r3, #0x10      ; Set bit 4 (IOPCEN)
str  r3, [r1, #0x30]    ; Write back
; ... (MODER configuration follows)
```

No conditional jumps, no `switch` jump tables, and no code for other ports. `if constexpr` completely eliminated the "superfluous" branches at compile time.

---

## Where We Are Now

`if constexpr` solved the final core problem of the GPIO template—compile-time automatic selection of clock enable. The GPIO class is now complete: type-safe ports and pins (`enum class` + NTTP), compile-time address translation (`constexpr`), and automatic clock enable (`if constexpr`). You can declare a GPIO object using `GPIO<Port::C, Pin5>`, and calling `setup()` automatically completes all initialization.

Next step: Build a dedicated LED template on top of GPIO—encapsulating LED-specific knowledge like "push-pull output, active low, low speed," so users can declare an LED with just one line of code.
