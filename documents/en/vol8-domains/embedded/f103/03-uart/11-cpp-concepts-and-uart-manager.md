---
chapter: 17
difficulty: intermediate
order: 11
platform: stm32f1
reading_time_minutes: 7
tags:
- cpp-modern
- intermediate
- stm32f1
title: '**Part 41: Concepts-Constrained GPIO Initialization + UartManager — Type-Safe
  Assembly**'
description: ''
translation:
  source: documents/vol8-domains/embedded/03-uart/11-cpp-concepts-and-uart-manager.md
  source_hash: 4bb8de12ceaa1a78c670828f03568d2feb3778de8f61d7e3256c6893011350f1
  translated_at: '2026-06-16T04:12:14.195386+00:00'
  engine: anthropic
  token_count: 1344
---
# Part 41: Concept-Constrained GPIO Initialization + UartManager — Type-Safe Assembly

> The button tutorial used Concepts to constrain callback function signatures. The UART tutorial uses them to constrain GPIO initialization callbacks. The same mechanism, different scenarios — the value of Concepts lies in "letting the compiler check interface contracts for you."

---

## UartGpioInitializer Concept

Before discussing Concepts, let's look at the problem. The `UartDriver`'s `init_gpio` method accepts a callable object — a user-registered GPIO initialization function. In pure template programming (without Concepts), the function signature might look like this:

```cpp
template <typename TFunc>
void init_gpio(TFunc&& func);
```

`TFunc` can be any type. If you pass a function with parameters (e.g., `void init(int x)`), it won't error at compile time — the error only explodes when `init_gpio` internally calls `func()`, resulting in a massive template instantiation call stack that is impossible to understand.

Concepts change this. Our code defines a `UartGpioInitializer` Concept:

```cpp
template <typename T>
concept UartGpioInitializer = requires(T t) {
    { t() } -> std::same_as<void>;
    { t() } noexcept;
};
```

This Concept requires `T` to satisfy two conditions:

1. **Callable with no arguments**: `t` can be called with no arguments (`t()`). Functions with parameters are not accepted.
2. **No-throw guarantee**: Calling `t` must not throw an exception.

Then, we use this Concept as a constraint in `UartDriver`:

```cpp
template <UartGpioInitializer TFunc>
void init_gpio(TFunc&& func);
```

`template <UartGpioInitializer TFunc>` tells the compiler: "`TFunc` must satisfy all requirements of the `UartGpioInitializer` Concept." If you pass a callable object that doesn't meet the requirements, the compiler will error at the `init_gpio` call site — the error message will clearly tell you "constraints not satisfied," rather than dumping a massive template instantiation stack.

### Why require `nothrow`?

Our project disables exceptions via `-fno-exceptions`. If the GPIO initialization function were allowed to throw exceptions, and `init_gpio` triggered one internally, the program would call `std::terminate` and exit immediately — because there is no exception handling mechanism to catch it.

`noexcept` checks this at compile time: If `TFunc`'s `operator()` or function signature lacks a `noexcept` declaration, the Concept check might still pass (because the compiler doesn't strictly distinguish nothrow from potentially throwing when exceptions are disabled). However, explicitly declaring the Concept constraint at least expresses the design intent: "GPIO initialization should not throw exceptions."

In our code, `uart1_gpio_init` is indeed declared as `noexcept`:

```cpp
void uart1_gpio_init() noexcept;
```

---

## UartManager: A Non-Instantiable Lifecycle Manager

`UartManager` is a pure static utility class — its sole purpose is to provide singleton access to `UartDriver` and act as a bridge to HAL handles. You should not, and cannot, create an instance of it:

```cpp
class UartManager {
    UartManager() = delete;
    // ...
};
```

### Deleting All Constructors

Five `= delete` declarations ensure this class cannot be instantiated, copied, or moved. Any attempt to create a `UartManager` instance will result in a compilation error. This isn't excessive defense — because `UartManager` has no instance state (all state for `UartDriver` is in the `UartDriver` member), creating an instance is meaningless.

### driver(): Meyer's Singleton

`driver()` is a static method that internally uses the Meyers' Singleton pattern:

```cpp
static UartDriver& driver() {
    static UartDriver instance;
    return instance;
}
```

`instance` is a function-local static variable. C++ guarantees it is initialized only once (on the first call to `driver()`), and subsequent calls return the existing instance. Furthermore, initialization is thread-safe (guaranteed by C++11) — although we don't have multithreading in our bare-metal environment, this guarantee comes with no runtime cost.

Since `UartDriver` (i.e., `instance`) has no instance data members, `sizeof(UartDriver)` is 1. `instance` occupies 1 byte of BSS space — effectively negligible.

### handle(): The extern "C" Bridge

```cpp
static UART_HandleTypeDef* handle();
```

`handle()` returns a pointer to the underlying HAL handle. This method is primarily used for code requiring C linkage — `stm32f4xx_it.c` and `stm32f4xx_hal_conf.h`. Functions in these files are within `extern "C"` blocks; they need `UART_HandleTypeDef*` to call HAL functions but cannot directly access C++ namespace `UartManager` members.

`handle()` serves as the bridge: C-linked code uses this method to obtain the handle pointer without needing to know the internal structure of `UartManager`.

This replaces the traditional global variable pattern:

```cpp
// Traditional approach
UART_HandleTypeDef huart1; // Global variable
```

In the traditional approach, `huart1` is a global variable — any code can read or write any of its fields. In our approach, `handle()` only returns a pointer and does not provide modifiable access to `UartDriver`'s internal state. Although theoretically one could still modify content through the pointer, at least the access path is explicit and traceable.

---

## The Initialization Pipeline: From the Caller's Perspective

Putting it all together, the initialization code in `main.cpp` looks like this:

```cpp
int main() {
    HAL_Init();
    SystemClock_Config();

    UartManager::driver().init_gpio(uart1_gpio_init);
    UartManager::driver().init_peripheral();
    UartManager::driver().enable();

    printf("System ready.\r\n");
    vTaskStartScheduler();
}
```

A five-step initialization pipeline, where each step has a clear responsibility and the order is non-negotiable. From the caller's perspective, this is a declarative interface — "tell the driver what you want," rather than "manually configure registers." All underlying hardware details (clocks, GPIO, HAL handles, NVIC) are encapsulated behind templates and Concept constraints.

---

## Comparison with the LED/Button Singleton Pattern

If you remember the `LedManager` from the LED tutorial, it used a `Singleton` base class to ensure a globally unique instance:

```cpp
class LedManager : public Singleton<LedManager> { ... };
```

`UartManager`'s singleton implementation is different — it achieves this by deleting all constructors + a static `driver()` method. Why not use `Singleton` here?

Because `LedManager` has instance state (clock configuration parameters), it genuinely needs a unique instance to manage that state. `UartManager`, however, has no instance state — all state for `UartDriver` is in the `UartDriver` member. `UartManager` is purely an access interface, not a state holder. Deleting constructors expresses the "I don't need instances" semantics more directly than inheriting from `Singleton`.

---

## Summary

This post covered two design tools: Concepts to constrain the GPIO initialization callback signature (`UartGpioInitializer`), and `UartManager` managing the driver lifecycle via deleted constructors + Meyers' Singleton. The `handle()` method acts as a bridge for C-linked code to access the HAL handle, replacing the traditional global variable pattern.

The next post is the grand finale of our C++ abstraction — a complete walkthrough of `UartShell`. All the components discussed previously — LED, Button, UART driver, printf redirection, interrupt reception, command processor — converge here.
