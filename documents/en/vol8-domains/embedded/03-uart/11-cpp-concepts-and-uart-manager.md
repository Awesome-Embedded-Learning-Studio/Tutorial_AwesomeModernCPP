---
chapter: 17
difficulty: intermediate
order: 11
platform: stm32f1
reading_time_minutes: 6
tags:
- cpp-modern
- intermediate
- stm32f1
title: 'Part 41: Concepts-Constrained GPIO Initialization + UartManager — Type-Safe
  Assembly'
translation:
  engine: anthropic
  source: documents/vol8-domains/embedded/03-uart/11-cpp-concepts-and-uart-manager.md
  source_hash: 4e378aa94dc28c0c50d94786d7dc29c0ecf48aa948e48195eaf0d0902ffd5fcf
  token_count: 1340
  translated_at: '2026-05-26T12:17:54.927783+00:00'
description: ''
---
# Part 41: Concepts-Constrained GPIO Initialization + UartManager — Type-Safe Assembly

> The button tutorial uses Concepts to constrain callback function signatures. The UART tutorial uses it to constrain GPIO initialization callbacks. The same mechanism, different scenarios — the value of Concepts lies in "letting the compiler check your interface contracts for you."

---

## UartGpioInitializer Concept

Before diving into Concepts, let's look at the problem. The `set_gpio_init()` method of `UartDriver` accepts a callable — the user-registered GPIO initialization function. In pure template programming (without Concepts), this function's signature might be:

```cpp
template <typename F> static void set_gpio_init(F fn) { gpio_init_ = fn; }
```

`F` can be any type. If you pass a function with parameters (like `void gpio_init(int pin)`), the compiler won't report an error — the error only explodes when `gpio_init_()` is called inside `init()`, dumping a massive template instantiation call stack that is completely incomprehensible.

Concepts change this. Our code defines a `UartGpioInitializer` Concept:

```cpp
// 来源: code/stm32f1-tutorials/3_uart_logger/device/uart/uart_driver.hpp
template <typename F>
concept UartGpioInitializer =
    std::invocable<F> && std::is_nothrow_invocable_v<F>;
```

This Concept requires `F` to satisfy two conditions:

1. **`std::invocable<F>`**: `F` can be called with no arguments (`f()`). Functions with parameters are not accepted.
2. **`std::is_nothrow_invocable_v<F>`**: Calling `F` does not throw exceptions.

We then use this Concept as a constraint in `set_gpio_init()`:

```cpp
template <UartGpioInitializer F>
static void set_gpio_init(F fn) noexcept { gpio_init_ = fn; }
```

`UartGpioInitializer F` tells the compiler: "`F` must satisfy all requirements of the `UartGpioInitializer` Concept." If you pass a callable that doesn't meet the requirements, the compiler will report an error right at the `set_gpio_init()` call site — the error message will clearly state "constraint `UartGpioInitializer` not satisfied," rather than dumping a massive template instantiation stack.

### Why require nothrow?

Our project disables exceptions via `-fno-exceptions`. If the GPIO initialization function is allowed to throw exceptions, and an exception is triggered when `init()` calls it internally, the program will call `std::terminate()` and terminate immediately — because there is no exception handling mechanism to catch it.

`std::is_nothrow_invocable_v<F>` checks at compile time: if the `operator()` or function signature of `F` lacks a `noexcept` declaration, the Concept check might still pass (because the compiler doesn't strictly distinguish between nothrow and potentially-throwing when exceptions are disabled). However, explicitly declaring the Concept constraint at least expresses the design intent: "GPIO initialization should not throw exceptions."

In our code, `usart1_gpio_init()` is indeed declared as `noexcept`:

```cpp
static void usart1_gpio_init() noexcept { ... }
```

---

## UartManager: A Non-Instantiable Lifecycle Manager

`UartManager` is a purely static utility class — its entire purpose is to provide singleton access to `UartDriver` and act as a bridge to the HAL handle. You should not, and cannot, create instances of it:

```cpp
// 来源: code/stm32f1-tutorials/3_uart_logger/device/uart/uart_manager.hpp
template <UartInstance INSTANCE>
class UartManager {
  public:
    using Driver = UartDriver<INSTANCE>;

    static auto driver() -> Driver& {
        static Driver drv;
        return drv;
    }

    static auto handle() -> UART_HandleTypeDef* {
        return Driver::native_handle();
    }

    UartManager()                        = delete;
    UartManager(const UartManager&)      = delete;
    UartManager(UartManager&&)           = delete;
    UartManager& operator=(const UartManager&) = delete;
    UartManager& operator=(UartManager&&)      = delete;
};
```

### Deleting All Constructors

Five `= delete` declarations ensure that this class cannot be instantiated, copied, or moved. Any attempt to create a `UartManager<Usart1> mgr;` will result in a compilation error. This isn't over-defensive — because `UartManager` has no instance state (the state of `UartDriver` lives in the `static inline` member), creating an instance makes no sense.

### driver(): Meyer's Singleton

`driver()` is a static method that uses the Meyers' Singleton pattern internally:

```cpp
static auto driver() -> Driver& {
    static Driver drv;
    return drv;
}
```

`static Driver drv` is a function-level static local variable. C++ guarantees it is initialized only once (on the first call to `driver()`), and subsequent calls return the existing instance. Furthermore, the initialization is thread-safe (guaranteed by C++11) — although we don't have multithreading in our bare-metal environment, this guarantee comes with no runtime cost.

Since `Driver` (i.e., `UartDriver<INSTANCE>`) has no instance data members, `sizeof(Driver)` is 1. `static Driver drv` occupies 1 byte of BSS space — practically negligible.

### handle(): The extern "C" Bridge

```cpp
static auto handle() -> UART_HandleTypeDef* {
    return Driver::native_handle();
}
```

`handle()` returns a pointer to the underlying HAL handle. This method is primarily used by code that requires C linkage — `printf_redirect.cpp` and `uart_irq.cpp`. The functions in these files are inside an `extern "C"` block; they need `UART_HandleTypeDef*` to call HAL functions, but they cannot directly access `static inline` members in a C++ namespace.

`handle()` acts as a bridge: C-linked code uses this method to obtain the handle pointer without needing to know the internal structure of `UartDriver`.

This replaces the traditional global variable pattern:

```cpp
// 传统做法（C 风格）
UART_HandleTypeDef huart1;  // 全局变量，任何地方都能访问和修改

// 我们的做法（C++ 风格）
auto* huart = UartManager<UartInstance::Usart1>::handle();  // 只读访问
```

In the traditional approach, `huart1` is a global variable — any code can read or write any of its fields. In our approach, `handle()` only returns a pointer and does not provide modifiable access to the internal state of `UartDriver`. While it's theoretically possible to modify the contents through the pointer once obtained, at least the access path is explicit and traceable.

---

## Initialization Pipeline: From the Caller's Perspective

Putting it all together, the initialization code in `main.cpp` looks like this:

```cpp
// 来源: code/stm32f1-tutorials/3_uart_logger/main.cpp
using Logger = device::uart::UartManager<device::uart::UartInstance::Usart1>;

// 1. 注册 GPIO 初始化回调
Logger::driver().set_gpio_init(usart1_gpio_init);
// 2. 初始化 USART（内部：使能时钟 → 调用 GPIO 回调 → HAL init）
Logger::driver().init(device::uart::UartConfig{.baud_rate = 115200});
// 3. 使能中断（配置 NVIC）
Logger::driver().enable_interrupt();
// 4. 发送欢迎信息
Logger::driver().send_string("UART Logger Ready!\r\n");
// 5. 启动中断接收
uart_start_receive();
```

A five-step initialization pipeline, where each step has a clear responsibility and the order is non-negotiable. From the caller's perspective, this is a declarative interface — "tell the driver what you want," rather than "manually configure registers." All the underlying hardware details (clocks, GPIO, HAL handles, NVIC) are encapsulated behind templates and Concept constraints.

---

## Comparison with the Singleton Pattern in the LED/Button Series

If you remember `ClockConfig` from the LED tutorial, it uses a `SimpleSingleton` base class to guarantee a globally unique instance:

```cpp
class ClockConfig : public base::SimpleSingleton<ClockConfig> { ... };
```

`UartManager`'s singleton implementation is different — it achieves this by deleting all constructors + using a static `driver()` method. Why not use `SimpleSingleton` here as well?

Because `ClockConfig` has instance state (clock configuration parameters), and it genuinely needs a unique instance to manage this state. `UartManager`, on the other hand, has no instance state at all — all of `UartDriver`'s state lives in the `static inline` member. `UartManager` is purely an access interface, not a state holder. Deleting the constructors expresses the "I don't need an instance" semantics more directly than inheriting from `SimpleSingleton`.

---

## Summary

This part covered two design tools: using Concepts to constrain the GPIO initialization callback signature (`invocable + nothrow`), and `UartManager` managing the driver lifecycle through deleted constructors + Meyers' Singleton. The `handle()` method serves as a bridge for C-linked code to access the HAL handle, replacing the traditional global variable pattern.

The next part is the grand finale of our C++ abstractions — a complete walkthrough of `main.cpp`. All the components we've covered previously — LED, Button, UART driver, printf redirection, interrupt-driven reception, and the command processor — all come together here.
