# Part 17: Wrapping Up C++23 Features — Attributes, Linkage, and the Final Proof of Zero-Overhead Abstraction

> Picking up where we left off: four refactors are done, and the code is running. In this part, we round up the scattered C++ features for a consolidated review, followed by a final performance verification. None of these features are "flashy syntactic sugar" — they all have practical significance in embedded development.

---

## [[nodiscard]] — Return Values That Cannot Be Ignored

`clock.h` contains a function declaration that looks a bit special:

```cpp
[[nodiscard("You should accept the clock frequency, it's what you request!")]]
uint64_t clock_freq() const noexcept;
```

`[[nodiscard]]` tells the compiler that the return value of this function should not be discarded. If someone writes `clock.clock_freq();` without using the return value, the compiler will issue a warning.

C++23 enhanced `[[nodiscard]]` by allowing you to attach a string message. When the warning triggers, the compiler displays your custom message — here, we wrote "You got the clock frequency, please use it!", which is far more helpful than a cold "warning: ignoring return value".

Why is this feature especially important in embedded development? Consider the function signatures in the HAL library: `HAL_StatusTypeDef HAL_RCC_OscConfig(RCC_OscInitTypeDef *RCC_OscInitStruct)` and `HAL_StatusTypeDef HAL_GPIO_Init(GPIO_TypeDef *GPIOx, GPIO_InitTypeDef *GPIO_Init)`. These functions all return status codes. If you don't check the return value, you might overlook a hardware configuration failure — the LED doesn't light up, you troubleshoot everywhere, and finally discover the clock configuration parameters were wrong. The HAL had already told you via the return value, but you didn't look.

In our `clock.cpp`, we correctly check the return value:

```cpp
const auto result = HAL_RCC_OscConfig(&osc);
if (result != HAL_OK) {
    system::dead::halt("Clock Configurations Failed");
}
```

If all HAL APIs were marked with `[[nodiscard]]`, such low-level errors could be caught at compile time.

---

## [[noreturn]] — Functions That Never Return

```cpp
// system/dead.hpp
[[noreturn]] inline void halt(const char* raw_message [[maybe_unused]]) {
    while (1) {
    }
}
```

`[[noreturn]]` tells the compiler that this function will never return to the caller. The compiler uses this information to do two things.

First is optimization. If the compiler knows `halt()` won't return, it doesn't need to generate any cleanup code after the `halt()` call. In `clock.cpp`, `halt()` is used inside an if branch:

```cpp
if (result != HAL_OK) {
    system::dead::halt("Clock Configurations Failed");
}
// 编译器知道：如果执行到了halt()，就不会到达这里
// 所以不需要在if之后生成"函数可能没有返回值"的警告
```

Second is eliminating false warnings. Without `[[noreturn]]`, the compiler might warn "function may not return a value on some paths" — because it doesn't know the code after `halt()` is unreachable. With `[[noreturn]]`, the compiler understands that control flow won't continue, and the warning naturally disappears.

---

## [[maybe_unused]] — Reserved but Unused Parameters

The `halt()` function has a `const char* raw_message` parameter, but the current implementation only has a `while(1) {}` infinite loop — the parameter isn't used at all. The compiler will issue an "unused parameter" warning. `[[maybe_unused]]` tells the compiler "I know it's not being used, and that's intentional."

This parameter is reserved for future expansion. Maybe someday we'll output error messages via UART in `halt()`, or light up an error indicator LED. Keeping the parameter but marking it as "I know it's unused" is good engineering practice — much better than deleting the parameter and adding it back later.

---

## extern "C" — The Bridge for Peaceful C and C++ Coexistence

Our project has several places where `extern "C"` appears:

```cpp
// gpio.hpp
extern "C" {
#include "stm32f1xx_hal.h"
}

// clock.cpp
extern "C" {
#include "stm32f1xx_hal.h"
}

// main.cpp
extern "C" {
#include "stm32f1xx_hal.h"
}
```

Why do we need this? The reason is that C++ and C have different name mangling rules. In C, the symbol name of function `HAL_GPIO_Init` in the object file is simply `HAL_GPIO_Init`. But in C++, the compiler "mangles" the function name into a symbol name that includes parameter type information, such as `_Z12HAL_GPIO_InitP11GPIO_TypeDefP15GPIO_InitTypeDef`. This mangling is what enables C++ function overloading — multiple functions with the same name but different parameters.

The problem is that the HAL library is compiled with a C compiler, so its function symbols in the object files use C-style names. If the C++ compiler looks for mangled names, the linker will report "undefined reference" — because the name you're looking for doesn't exist.

`extern "C"` tells the C++ compiler: "For all functions declared in this header file, please use C naming rules to find them." This way, during linking, the compiler will look for `HAL_GPIO_Init` instead of a mangled name.

There's another critical place — `hal_mock.c`:

```c
void SysTick_Handler(void) {
    HAL_IncTick();
}
```

`SysTick_Handler` is a function name in the interrupt vector table. After a hardware reset, when the SysTick interrupt triggers, the CPU jumps to the `SysTick_Handler` address recorded in the vector table. This lookup process uses C-linked symbol names — so `SysTick_Handler` must be defined using C linkage rules. If it's defined in a `.cpp` file, it must be wrapped with `extern "C"`, otherwise the mangled symbol won't be found in the vector table.

---

## noexcept — Exception Guarantees in Embedded Systems

```cpp
// gpio.hpp
static constexpr GPIO_TypeDef* native_port() noexcept { ... }

// clock.h
uint64_t clock_freq() const noexcept;
```

`noexcept` guarantees that the function won't throw exceptions. In our project, this is a natural guarantee — because `CMakeLists.txt` specifies `-fno-exceptions`:

```cmake
add_compile_options(
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
)
```

`-fno-exceptions` disables C++ exceptions at the compilation level. Any `throw` statement will result in a compilation error. So our code physically cannot throw exceptions. Then why do we still explicitly write `noexcept`?

The first reason is documentation. `noexcept` tells anyone reading the code "this function won't throw exceptions" — in an embedded environment, this is important information. The second reason is compiler optimization. Even with exceptions disabled, `noexcept` can still help the compiler generate more compact code — it doesn't need to generate stack unwinding-related data. On the STM32F103C8T6 with 64KB Flash, every bit of space is precious.

`-fno-rtti` is also worth mentioning: RTTI (Run-Time Type Information) is C++'s runtime type identification mechanism (`dynamic_cast`, `typeid`, etc.). Disabling RTTI saves Flash space because type information tables don't need to be stored. Our code doesn't use `dynamic_cast` — all type polymorphism is achieved through templates at compile time.

---

## Aggregate Initialization — Ensuring Structs Start from Zero

```cpp
// gpio.hpp
GPIO_InitTypeDef init_types{};   // C++风格的值初始化

// clock.cpp
RCC_OscInitTypeDef osc = {0};    // C风格的零初始化
RCC_ClkInitTypeDef clk = {0};
```

Both approaches have the same effect: clearing all bytes of the struct to zero. The difference is that `{}` is the value initialization syntax introduced in C++11, while `{0}` is the traditional C language approach. In embedded development, initializing structs is crucial — uninitialized `Speed` fields might contain garbage values, causing pins to run at unpredictable speeds.

⚠️ Warning: In embedded C++, uninitialized variables are one of the biggest sources of bugs. If local variables on the stack aren't initialized, their values depend on residual data from the last time the stack frame was used — this is undefined behavior (UB). The `GPIO_InitTypeDef init{}` syntax ensures all bytes are zero, eliminating this risk. If you see someone write `GPIO_InitTypeDef init;` (without `{}`), that's a ticking time bomb — it might happen to work in debug mode, but behavior changes after Release optimizations.

---

## The Final Proof of Zero-Overhead Abstraction

Reading about it on paper only goes so far. Rather than just claiming "zero overhead," let's look directly at the machine code generated by the compiler. All the assembly below comes from the actual compilation output of the companion project for this tutorial (`arm-none-eabi-g++ -O2 -mcpu=cortex-m3 -mthumb -std=gnu++23`).

### C++ Template Version

Source code: the calling convention in `main.cpp`:

```cpp
device::LED<device::gpio::GpioPort::C, GPIO_PIN_13> led;
// ...
led.on();   // 点亮
led.off();  // 熄灭
```

The Thumb-2 assembly generated by compiling `LED::on()` and `LED::off()` in `main()` is as follows:

```asm
; led.on()  →  编译器将模板参数全部在编译期折叠为立即数
 8000164:  movs   r2, #1            ; GPIO_PIN_SET   = 1
 8000166:  mov.w  r1, #8192         ; GPIO_PIN_13    = 0x2000
 800016a:  ldr    r0, [pc, #16]     ; GPIOC 基地址   = 0x40011000
 800016c:  bl     8000564           ; 调用 HAL_GPIO_WritePin

; led.off() →  仅 r2 的立即数不同
 8000150:  movs   r2, #0            ; GPIO_PIN_RESET = 0
 8000152:  mov.w  r1, #8192         ; GPIO_PIN_13    = 0x2000
 8000156:  ldr    r0, [pc, #36]     ; GPIOC 基地址   = 0x40011000
 8000158:  bl     8000564           ; 调用 HAL_GPIO_WritePin
```

Notice three things:

1. The ternary expression `LEVEL == ActiveLevel::Low ? ... : ...` is fully evaluated at compile time and doesn't exist at all at runtime
2. The template parameters `GpioPort::C` (address `0x40011000`) and `GPIO_PIN_13` (`0x2000`) are directly encoded by the compiler as immediates — there is no indirect addressing overhead
3. `on()` and `off()` each take only **4 instructions** (8 bytes), and the only difference is the immediate value `r2`

### The Implementation of HAL_GPIO_WritePin

Both calls above ultimately enter `HAL_GPIO_WritePin`, which itself is only **4 instructions, 8 bytes**:

```asm
08000564 <HAL_GPIO_WritePin>:
 8000564:  cbnz   r2, 8000568       ; r2 != 0 (SET)?   跳过移位
 8000566:  lsls   r1, r1, #16       ; r2 == 0 (RESET): 引脚号左移 16 位
 8000568:  str    r1, [r0, #16]     ; 写入 GPIOx->BSRR (偏移 0x10)
 800056a:  bx     lr                ; 返回
```

How it works: on the STM32, the upper 16 bits of the BSRR register are used to **reset** (clear to zero) pins, and the lower 16 bits are used to **set** (pull high) pins. `cbnz` checks `r2` (PinState): if it's `RESET` (0), it shifts the pin number left by 16 bits and writes to the upper half of BSRR to perform a reset; if it's `SET` (1), it writes directly to the lower half to perform a set. A single `str` instruction completes the atomic operation — no read-modify-write is needed.

### Comparison: What Would a C Macro Version Generate?

If we used the traditional C macro approach:

```c
#define LED_ON()  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET)
#define LED_OFF() HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET)
```

After preprocessor expansion, the code seen by the compiler is **exactly identical** to what the C++ template version above generates: loading three parameters (GPIOC address, pin number, state) into `r0/r1/r2`, then a `bl` call to `HAL_GPIO_WritePin`. There are no extra instructions.

### Resource Consumption Overview

Flash usage for the entire program:

| Section | Size |
|----|------|
| `.text` (code + read-only data) | 2992 bytes |
| `.data` (initialized global variables) | 12 bytes |
| `.bss` (zero-initialized global variables) | 8 bytes |

The STM32F103C8T6 has 64KB Flash and 20KB SRAM. The LED blink program above only uses **4.6%** of the Flash space — and the vast majority of that is the HAL library itself and the interrupt vector table. The extra code overhead introduced by the C++ template abstraction is exactly zero.

This is "zero-overhead abstraction": you used C++'s high-level abstractions (templates, enum class, constexpr) to write safer, more maintainable code, but the final generated machine code is completely identical to hand-written C code. The "cost" of templates only manifests in compilation time: the compiler needs to generate a copy of the code for each unique combination of template parameters. But this cost is paid on the development machine, not on the STM32's 64KB Flash.

---

## Looking Back

All C++23 features have been covered, and zero-overhead abstraction has been verified. Let's review all the features we used:

- `enum class` with underlying types — type-safe GPIO configuration constants
- `static_cast` — zero-overhead enum-to-integer conversion
- Non-type template parameters (NTTP) — compile-time binding of ports and pins
- `constexpr` — compile-time evaluated address conversion
- `if constexpr` — compile-time automatic selection of clock enable macros
- `[[nodiscard]]` with custom messages — preventing important return values from being ignored
- `[[noreturn]]` — optimization hints for functions that never return
- `[[maybe_unused]]` — marking reserved but unused parameters
- `noexcept` — documentation and optimization in exception-disabled environments
- `extern "C"` — the bridge for C and C++ interoperability
- Aggregate initialization `{}` — ensuring structs start from zero

Every feature has a clear "why it's useful in embedded systems." This isn't showing off — this is using the compiler's capabilities to replace human memory and vigilance in resource-constrained environments.

Next up: a roundup of common pitfalls and three hands-on exercises — taking the LED to the next level.
