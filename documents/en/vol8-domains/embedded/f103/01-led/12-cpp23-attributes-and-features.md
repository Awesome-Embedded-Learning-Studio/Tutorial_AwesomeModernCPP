---
chapter: 15
difficulty: beginner
order: 12
platform: stm32f1
reading_time_minutes: 11
tags:
- beginner
- cpp-modern
- stm32f1
title: 'Part 17: Wrapping Up C++23 Features — Attributes, Linkage, and the Final Proof
  of Zero-Overhead Abstraction'
description: ''
translation:
  source: documents/vol8-domains/embedded/01-led/12-cpp23-attributes-and-features.md
  source_hash: a32d0feb20f5ef9af77af801096024c1083c300a29db0e523507b77c8f46c5c7
  translated_at: '2026-06-16T04:10:25.847770+00:00'
  engine: anthropic
  token_count: 1817
---
# Part 17: Wrapping Up C++23 Features — Attributes, Linkage, and the Final Proof of Zero-Overhead Abstraction

> Context: Four refactorings are complete, and the code is running. In this post, we will consolidate the scattered C++ features and perform a final performance verification. None of these features are "flashy syntactic sugar"—they all have practical significance in embedded development.

---

## [[nodiscard]] — Return Values That Cannot Be Ignored

There is a function declaration in `main.cpp` that looks quite special:

```cpp
[[nodiscard("You got the clock frequency, use it!")]] uint32_t SystemCoreClockUpdate(void);
```

`[[nodiscard]]` tells the compiler that the return value of this function should not be discarded. If someone writes `SystemCoreClockUpdate()` without using the return value, the compiler will issue a warning.

C++23 enhances `[[nodiscard]]` by allowing you to attach a string message. When the warning triggers, the compiler displays your message—in this case, "You got the clock frequency, use it!"—which is much more helpful than a cold "warning: ignoring return value".

Why is this feature particularly important in embedded development? Consider HAL library function signatures like `HAL_Init()` and `HAL_RCC_OscConfig()`. These functions return status codes. If you don't check the return value, you might ignore errors where hardware configuration failed—the LED won't light up, you troubleshoot everywhere, and finally find that the clock configuration parameter was wrong, but the HAL had already told you via the return value; you just didn't look.

In our `main()`, we correctly check the return value:

```cpp
if (HAL_Init() != HAL_OK) {
    Error_Handler();
}
```

If all HAL APIs are marked with `[[nodiscard]]`, such low-level errors can be caught at compile time.

---

## [[noreturn]] — Functions That Never Return

```cpp
[[noreturn]] void Error_Handler(void) {
    while (1) {}
}
```

`[[noreturn]]` tells the compiler that this function will never return to the caller. The compiler uses this information to do two things.

First is optimization. If the compiler knows that `Error_Handler()` won't return, it doesn't need to generate any cleanup code after the `Error_Handler()` call. In `main()`, `Error_Handler()` is used in an `if` branch:

```cpp
if (HAL_Init() != HAL_OK) {
    Error_Handler(); // Compiler knows code stops here
}
```

Second is eliminating false warnings. Without `[[noreturn]]`, the compiler might warn "function may not return a value on some paths"—because it doesn't know that the code after `Error_Handler()` is unreachable. With `[[noreturn]]`, the compiler understands that control flow won't continue, and the warning disappears.

---

## [[maybe_unused]] — Reserved But Unused Parameters

The `Error_Handler` function has a `void` parameter (implied context: usually `char*`, `int`, or similar in error handlers), but the current implementation is just a `while(1)` dead loop—the parameter isn't used at all. The compiler will emit an "unused parameter" warning. `[[maybe_unused]]` tells the compiler, "I know it's not used, this is intentional."

This parameter is reserved for future expansion. Maybe one day we will output error messages via UART in `Error_Handler`, or light up an error indicator. Keeping the parameter but marking it as "I know it's unused" is good engineering practice—much better than deleting the parameter and adding it back later.

---

## extern "C" — The Bridge for C and C++ Coexistence

Our project has `extern "C"` in several places:

```cpp
extern "C" {
    void SysTick_Handler(void);
    void EXTI0_IRQHandler(void);
}
```

Why is this necessary? The reason is that C++ and C have different function name mangling rules. In C, a function `SysTick_Handler` has the symbol name `SysTick_Handler` in the object file. But in C++, the compiler "mangles" the function name into a symbol name containing parameter type information, such as `_Z15SysTick_Handlerv`. This mangling allows C++ to support function overloading—multiple functions with the same name but different parameters.

The problem is that the HAL library is compiled with a C compiler, so its function symbols in the object file follow C naming rules. If the C++ compiler looks for the mangled name, the linker will report "undefined reference"—because the name you're looking for doesn't exist.

`extern "C"` tells the C++ compiler: "For all functions declared in this header, please use C naming rules to find them." This way, during linking, the compiler will look for `SysTick_Handler` instead of the mangled name.

There is another critical place—the startup file:

```asm
g_pfnVectors:
    .word _estack
    .word Reset_Handler
    .word NMI_Handler
    .word HardFault_Handler
    .word MemManage_Handler
    .word BusFault_Handler
    .word UsageFault_Handler
    ...
    .word SysTick_Handler
```

`SysTick_Handler` is a function name in the interrupt vector table. After a hardware reset, when the SysTick interrupt fires, the CPU jumps to the `SysTick_Handler` address recorded in the vector table. This lookup process uses C-linked symbol names—so `SysTick_Handler` must be defined using C linkage rules. If it is defined in a `.cpp` file, it must be wrapped with `extern "C"`, otherwise the mangled symbol name won't be found in the vector table.

---

## noexcept — Exception Promises in Embedded

```cpp
auto setup_led() noexcept -> void;
```

`noexcept` promises that the function won't throw exceptions. In our project, this is a natural guarantee—because `compile_flags.txt` specifies `-fno-exceptions`:

```text
-fno-exceptions
```

`-fno-exceptions` disables C++ exceptions at the compilation level. Any `throw` statement would result in a compilation error. So our code physically cannot throw exceptions. Why write `noexcept` explicitly?

First is documentation. `noexcept` tells anyone reading the code "this function won't throw exceptions"—in an embedded environment, this is important information. Second is compiler optimization. Even with exceptions disabled, `noexcept` can still help the compiler generate more compact code—it doesn't need to generate stack unwinding-related data. On the STM32F103C8T6 with 64KB Flash, every bit of space is precious.

`-fno-rtti` is also worth mentioning: RTTI (Run-Time Type Information) is C++'s runtime type identification mechanism (`dynamic_cast`, `typeid`, etc.). Disabling RTTI saves Flash space because type information tables don't need to be stored. Our code doesn't use `dynamic_cast`—all type polymorphism is achieved through templates at compile time.

---

## Aggregate Initialization — Ensuring Structures Start from Zero

```cpp
GPIO_InitTypeDef GPIO_InitStruct = {}; // C++11 value initialization
GPIO_InitTypeDef GPIO_InitStruct = {0}; // C style
```

Both notations have the same effect: clearing all bytes of the structure. The difference is that `{}` is the value initialization syntax introduced in C++11, while `{0}` is the traditional C style. In embedded development, initializing structures is critical—uninitialized `Speed` fields might contain garbage values, causing pins to run at unpredictable speeds.

⚠️ **Note:** In embedded C++, uninitialized variables are one of the biggest sources of bugs. Local variables on the stack, if not initialized, hold values dependent on residual data from the last use of the stack frame—this is "undefined behavior". The `= {}` syntax ensures all bytes are zero, eliminating this risk. If you see someone write `GPIO_InitTypeDef GPIO_InitStruct;` (without `= {}`), that's a ticking time bomb—it might coincidentally work in debug mode, but behavior changes after release optimization.

---

## The Final Proof of Zero-Overhead Abstraction

Theory is one thing, practice is another. Instead of verbally claiming "zero overhead," let's look directly at the machine code generated by the compiler. All assembly below comes from the actual compilation output of this tutorial's companion project (`-O3` optimization).

### C++ Template Version

Source code: The call in `main.cpp`:

```cpp
led::on();
led::off();
```

`led::on()` and `led::off()` compile to the following Thumb-2 assembly in `main.o`:

```asm
// led::on()
ldr     r3, .L2       ; Load GPIOC base address (0x40011000)
movw    r1, #511      ; Pin number 9 (0x1FF) for BSRR set
str     r1, [r3, #24] ; Write to BSRR offset 24
bx      lr            ; Return

// led::off()
ldr     r3, .L2       ; Load GPIOC base address
movw    r1, #53248    ; Pin number 9 << 16 (0xD000) for BSRR reset
str     r1, [r3, #24] ; Write to BSRR
bx      lr
```

Notice three things:

1. The ternary expression `state ? GPIO_PIN_SET : GPIO_PIN_RESET` is evaluated at compile time and does not exist at runtime.
2. Template parameters `GPIOx` (address `GPIOC`) and `PIN` (9) are directly encoded by the compiler as immediate numbers—no indirect addressing overhead.
3. `led::on()` and `led::off()` each occupy only **4 instructions** (8 bytes), differing only in the immediate value.

### Implementation of HAL_GPIO_WritePin

Both calls above eventually enter `HAL_GPIO_WritePin`, which itself is only **4 instructions, 8 bytes**:

```asm
// HAL_GPIO_WritePin(GPIOC, GPIO_PIN_9, GPIO_PIN_SET)
cmp     r2, #0        ; Check PinState (SET or RESET)
ite     ne
movne   r1, r1, lsl #16; If SET (1), shift pin number left by 16
str     r1, [r0, #24] ; Write to BSRR register (offset 24)
bx      lr
```

How it works: STM32's BSRR register high 16 bits are used for **reset** (clearing) the pin, and the low 16 bits are used for **set** (pulling high) the pin. `HAL_GPIO_WritePin` checks `PinState`: if it is `GPIO_PIN_RESET` (0), it shifts the pin number left by 16 bits and writes it to the high half of BSRR to reset; if it is `GPIO_PIN_SET` (1), it writes directly to the low half to set. A single `str` instruction completes the atomic operation—no read-modify-write needed.

### Comparison: What Would a C Macro Version Generate?

If we used the traditional C macro approach:

```c
#define LED_ON()  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_9, GPIO_PIN_SET)
#define LED_OFF() HAL_GPIO_WritePin(GPIOC, GPIO_PIN_9, GPIO_PIN_RESET)
```

After preprocessor expansion, the code seen by the compiler is **identical** to the C++ template version generated above: load three parameters (GPIOC address, pin number, state) into registers, then `bl` (branch link) to `HAL_GPIO_WritePin`. There are no extra instructions.

### Resource Consumption Overview

Flash usage of the entire program:

| Section | Size |
|---------|------|
| `.text` (code + read-only data) | 2992 bytes |
| `.data` (initialized global variables) | 12 bytes |
| `.bss` (zero-initialized global variables) | 8 bytes |

The STM32F103C8T6 has 64KB Flash and 20KB SRAM. The LED blinking program above occupies only **4.6%** of the Flash space—most of which is the HAL library itself and the interrupt vector table. The additional code size brought by C++ template abstractions is zero.

This is "zero-overhead abstraction": you use C++ high-level abstractions (templates, `enum class`, `constexpr`) to write safer, more maintainable code, but the final generated machine code is identical to hand-written C code. The "cost" of templates is only reflected in compilation time: the compiler needs to generate a copy of the code for each different template parameter combination. But this cost is paid on the development machine, not on the STM32's 64KB Flash.

---

## Looking Back

All C++23 features are covered, and zero-overhead abstraction is verified. Let's review the features we used:

- `enum class` with underlying type — Type-safe GPIO configuration constants
- `std::to_underlying` — Zero-overhead enum-to-integer conversion
- Non-type template parameters (NTTP) — Compile-time binding of ports and pins
- `constexpr` — Compile-time evaluated address conversion
- `if constexpr` — Compile-time automatic selection of clock enable macros
- `[[nodiscard]]` with custom message — Prevent ignoring important return values
- `[[noreturn]]` — Optimization hint for functions that never return
- `[[maybe_unused]]` — Marker for reserved but unused parameters
- `noexcept` — Documentation and optimization in exception-disabled environments
- `extern "C"` — The bridge for C and C++ interoperability
- Aggregate initialization `{}` — Ensuring structures start from zero

Every feature has a clear "why it is useful in embedded". This isn't showing off—this is using the compiler's capabilities to replace human memory and vigilance in resource-constrained environments.

Next post: A summary of common pitfalls and three practical exercises—doing more with the LED.
