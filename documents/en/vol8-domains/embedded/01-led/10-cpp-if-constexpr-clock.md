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
title: 'Part 15: The Third Refactor — Using `if constexpr` to Automatically Select
  the Right Clock Enable at Compile Time'
translation:
  engine: anthropic
  source: documents/vol8-domains/embedded/01-led/10-cpp-if-constexpr-clock.md
  source_hash: 46566194d11d1983b4a63adaf02f386ff3ba4f1984048233c29a68999773f466
  token_count: 1426
  translated_at: '2026-05-26T12:07:50.338434+00:00'
description: ''
---
# Part 15: The Third Refactoring — Using `if constexpr` to Automatically Select Clock Enable at Compile Time

> Continuing from the previous article: we have the GPIO template skeleton in place, but clock enable remains unsolved. The core problem is that ``__HAL_RCC_GPIOA_CLK_ENABLE()`` and ``__HAL_RCC_GPIOC_CLK_ENABLE()`` are different macros that, when expanded, write to different register bits. We cannot use a single "generic" runtime function to choose between them. The solution is ``if constexpr``—a compile-time conditional branch introduced in C++17.

---

## The Problem: Why We Can't Select Clock Macros at Runtime

You might think, why not just write a ``switch``?

```cpp
void enable_clock(GpioPort port) {
    switch (port) {
        case GpioPort::A: __HAL_RCC_GPIOA_CLK_ENABLE(); break;
        case GpioPort::B: __HAL_RCC_GPIOB_CLK_ENABLE(); break;
        case GpioPort::C: __HAL_RCC_GPIOC_CLK_ENABLE(); break;
        case GpioPort::D: __HAL_RCC_GPIOD_CLK_ENABLE(); break;
        case GpioPort::E: __HAL_RCC_GPIOE_CLK_ENABLE(); break;
    }
}
```

This looks reasonable, but it has two problems. The first problem is waste: ``PORT`` is a template parameter, meaning it is a compile-time constant. Using a runtime ``switch`` to handle a compile-time constant is equivalent to asking the compiler to generate code for branches that "will never be taken." Although the optimizer might eliminate the redundant branches for you, this is not guaranteed—especially when the macro expansion involves ``volatile`` writes.

The second problem is more subtle: the clock enable macros, when expanded, contain write operations to the ``volatile`` register. ``volatile`` tells the compiler "this memory location might be modified by hardware, so do not optimize away accesses to it." When analyzing the ``switch``, the compiler cannot determine that only one ``case`` will be executed—from its perspective, the ``switch`` argument could be any runtime value. Therefore, the compiler might refuse to optimize away those "never-executed" ``volatile`` writes.

``if constexpr``, on the other hand, is completely different. The compiler knows the value of ``PORT`` at compile time and directly discards the non-matching branches. Only the matching branch gets compiled into the final binary.

---

## A Detailed Look at `if constexpr` Syntax

``if constexpr`` is a feature introduced in C++17, with the following syntax:

```cpp
if constexpr (compile_time_condition) {
    // 编译时条件为真时编译这段代码
} else {
    // 编译时条件为假时，这段代码被完全丢弃
}
```

The difference from a regular ``if`` is this: in a regular ``if``, both branches are compiled into the binary, and the runtime selects which one to execute. With ``if constexpr``, only the branch whose condition is true is compiled; the other branch is completely discarded at compile time—leaving no trace of it in the generated binary.

Even more powerfully, the discarded branch doesn't even need to be syntactically valid C++ code (in certain situations)—because the compiler never analyzes it at all. This is known as "compile-time branch discarding."

---

## The Complete GPIOClock Implementation

In ``gpio.hpp``, clock enable is encapsulated as a private nested class. This is the most exquisite part of the entire template design:

```cpp
private:
    class GPIOClock {
      public:
        static inline void enable_target_clock() {
            if constexpr (PORT == GpioPort::A) {
                __HAL_RCC_GPIOA_CLK_ENABLE();
            } else if constexpr (PORT == GpioPort::B) {
                __HAL_RCC_GPIOB_CLK_ENABLE();
            } else if constexpr (PORT == GpioPort::C) {
                __HAL_RCC_GPIOC_CLK_ENABLE();
            } else if constexpr (PORT == GpioPort::D) {
                __HAL_RCC_GPIOD_CLK_ENABLE();
            } else if constexpr (PORT == GpioPort::E) {
                __HAL_RCC_GPIOE_CLK_ENABLE();
            }
        }
    };
```

Let's break down the design intent of this code layer by layer.

First is the nested class design. ``GPIOClock`` is placed in the ``private`` region of the ``GPIO`` class, making it inaccessible from the outside. It is an "internal implementation detail" of the GPIO—users of the GPIO don't need to know how the clock is enabled; they only need to call ``setup()``. This idea of "encapsulating implementation details" is very common in C++, and nested classes are a natural way to achieve it.

Next is the ``static inline`` function. ``static`` means it can be called without an instance of ``GPIOClock``, directly via ``GPIOClock::enable_target_clock()``. ``inline`` suggests that the compiler embed the function body directly at the call site—in embedded development, short functions like this consisting of only a few lines of code will almost always be inlined, avoiding function call overhead.

The most critical part is the condition in ``if constexpr``. ``PORT == GpioPort::A`` is a compile-time constant expression—because ``PORT`` is a template parameter, it is known at compile time. The compiler checks these conditions one by one, keeping only the branch that evaluates to true.

When the template is instantiated as ``GPIO<GpioPort::C, GPIO_PIN_13>``, the compiler sees that ``PORT == GpioPort::C`` is true, so only ``__HAL_RCC_GPIOC_CLK_ENABLE()`` is compiled into the code. The other four branches (A, B, D, E) are completely discarded at compile time. If you use ``arm-none-eabi-objdump`` to disassemble the final ``.elf`` file, you will find only one clock enable call—no conditional jumps, no ``switch`` table, just a single direct register write instruction.

⚠️ Warning: The condition in ``if constexpr`` must be a compile-time constant expression. If you try to use a runtime variable (such as a function parameter) as the condition, the compiler will emit an error. This restriction is actually a good thing—it ensures that the branch decision is made at compile time and won't secretly introduce runtime overhead. If you genuinely need runtime selection, then that falls outside the design goals of this template.

---

## How `setup()` Uses GPIOClock

```cpp
void setup(Mode gpio_mode, PullPush pull_push = PullPush::NoPull, Speed speed = Speed::High) {
    GPIOClock::enable_target_clock();  // 自动使能对应端口的时钟
    GPIO_InitTypeDef init_types{};
    init_types.Pin = PIN;
    init_types.Mode = static_cast<uint32_t>(gpio_mode);
    init_types.Pull = static_cast<uint32_t>(pull_push);
    init_types.Speed = static_cast<uint32_t>(speed);
    HAL_GPIO_Init(native_port(), &init_types);
}
```

``GPIOClock::enable_target_clock()`` is the first line called in ``setup()``. Because ``setup()`` itself is a method of a template class, when the compiler instantiates ``GPIO<GpioPort::C, GPIO_PIN_13>``, it unfolds the entire call chain:

1. ``GPIOClock::enable_target_clock()`` → ``if constexpr (PORT == GpioPort::C)`` → ``__HAL_RCC_GPIOC_CLK_ENABLE()``
2. ``PIN`` → ``GPIO_PIN_13``
3. ``native_port()`` → ``GPIOC``

The final compiled code for ``setup()`` is completely identical to hand-written C code—enable the clock first, then configure the pin, with zero extra overhead.

One more point to emphasize: the condition in ``if constexpr`` must be a compile-time constant expression. If you try to use a runtime variable (such as a function parameter) as the condition, the compiler will directly emit an error. This restriction is actually a good thing—it ensures that the branch decision is made at compile time and won't secretly introduce runtime overhead. If you genuinely need to select a clock at runtime, use a traditional ``switch-case``, but that is not the design goal of this template.

---

## Why Not Use Other Approaches

**Template specialization** is the classic approach, but it requires writing a specialization for each port:

```cpp
template <GpioPort PORT> struct ClockEnabler;
template <> struct ClockEnabler<GpioPort::A> {
    static void enable() { __HAL_RCC_GPIOA_CLK_ENABLE(); }
};
template <> struct ClockEnabler<GpioPort::C> {
    static void enable() { __HAL_RCC_GPIOC_CLK_ENABLE(); }
};
// 还要写B、D、E...
```

This works, but the code is scattered across multiple places—five specializations mean five separate code blocks. ``if constexpr`` centralizes all the logic in one place, letting you see the handling for all ports at a glance. During maintenance, you only need to modify one location.

**Runtime array indexing** is another idea—directly manipulating registers without going through HAL macros:

```cpp
void enable_clock(int port_index) {
    RCC->APB2ENR |= (1 << (port_index + 2));
}
```

But this bypasses the HAL, and the HAL macros might perform additional work (such as memory barriers, waiting for clock stabilization, etc.). Directly manipulating registers might miss these details, potentially causing instability under certain clock configurations. Wherever you can use HAL macros, use them—this is a pragmatic choice in embedded development.

Therefore, ``if constexpr`` is the most elegant solution: logic centralized in one place, determined at compile time, perfectly compatible with HAL macros, and easy to maintain.

---

## Verifying the Compilation Output

We can use ``arm-none-eabi-objdump`` to inspect the compiled code and verify the effects of ``if constexpr``. For the ``GPIO<GpioPort::C, GPIO_PIN_13>`` instance, we should only see the instruction corresponding to ``__HAL_RCC_GPIOC_CLK_ENABLE()`` in ``setup()``—a write to the ``RCC_APB2ENR`` register (address ``0x40021018``) that sets bit4 (IOPCEN) to 1.

```text
; 预期的汇编输出（-O2优化）
MOV.W   R0, #0x10          ; 0x10 = bit4 = IOPCEN
LDR     R1, =0x40021018    ; RCC_APB2ENR地址
STR     R1, [R1]           ; 写入寄存器（简化表示）
```

No conditional jumps, no ``switch`` jump table, no code for other ports. ``if constexpr`` thoroughly eliminates the "redundant" branches at compile time.

---

## Where We Are Now

``if constexpr`` solves the last core problem of the GPIO template—compile-time automatic selection of clock enable. Now the GPIO class is complete: type-safe port and pin (``enum class`` + NTTP), compile-time address conversion (``constexpr native_port()``), and automatic clock enable (``if constexpr``). You can declare a GPIO object using ``GPIO<GpioPort::C, GPIO_PIN_13>``, and calling ``setup(Mode::OutputPP)`` automatically completes all initialization.

Next step: building a dedicated LED template on top of GPIO—encapsulating LED-specific knowledge like "push-pull output, active-low, low-speed" so that users only need a single line of code to declare an LED.
