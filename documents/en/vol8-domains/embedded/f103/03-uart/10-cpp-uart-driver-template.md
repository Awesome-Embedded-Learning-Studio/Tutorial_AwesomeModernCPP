---
chapter: 17
difficulty: intermediate
order: 10
platform: stm32f1
reading_time_minutes: 8
tags:
- cpp-modern
- intermediate
- stm32f1
title: 'Part 40: UART Driver Template — Zero-Size Abstraction and Compile-Time Dispatch'
description: ''
translation:
  source: documents/vol8-domains/embedded/03-uart/10-cpp-uart-driver-template.md
  source_hash: 9424dc48fb4959b6f36a4b732d07a5ec41c6c5286412c84de7674e782e4acb5a
  translated_at: '2026-06-16T04:12:15.401361+00:00'
  engine: anthropic
  token_count: 1753
---
# Part 40: UART Driver Template — Zero-Size Abstraction and Compile-Time Dispatching

> The LED tutorial used templates to select ports and pins, and the button tutorial used templates to select pull-up/pull-down and active levels. The UART driver template's dimension is the USART instance—but the implementation technique is more elegant than the previous two series.

---

## The Full Picture of the UartDriver Template

`UartDriver` is the core of the entire UART driver. It is a class template, and the template parameter is a `UsartInstance` enum—selecting which USART peripheral to use. Let's look at its full declaration:

```cpp
template<UsartInstance inst> class UartDriver {
public:
    // ... methods ...

private:
    static inline UART_HandleTypeDef huart;  // HAL handle
    static inline Gpio::InitCallback gpio_init_cb = nullptr;
    static inline ReceiveCallback receive_cb = nullptr;
    static inline TransmitCallback transmit_cb = nullptr;
};
```

Notice a key characteristic: this class **has no instance data members**. All data is `static inline`. What does this mean? It means `sizeof(UartDriver<...>)` equals 1—the empty class size. The class itself occupies no RAM.

---

## Empty Base Optimization (EBO)

The C++ standard specifies that the size of any complete object type is at least 1 byte (even if it has no data members), because each object must have a unique address. So `sizeof(UartDriver<UsartInstance::Usart1>)` is 1, not 0.

But this 1 byte is just the overhead of the object itself. The real state—the HAL handle, callback function pointers—is all stored in `static` members. These members do not belong to the object instance, but to the template specialization. `UartDriver<Usart1>` and `UartDriver<Usart2>` each have their own independent set of static members, stored in the BSS segment.

The beauty of this design is: you can create instances of `UartDriver` in your code (for example, via a static instance returned by `native_instance()`), but the instance itself takes up almost no space. The state is stripped from the object to the template specialization level—there is only one state per USART instance, not per object. If you write `UartDriver<Usart1>` ten times in your code, there won't be ten `huart`s, only one.

---

## static inline Members: C++17's Singleton Tool

Before C++17, a class's `static` member needed to be defined separately in the `.cpp` file:

```cpp
// .cpp file
template<UsartInstance inst>
UART_HandleTypeDef UartDriver<inst>::huart;  // Definition required
```

This was cumbersome—every template specialization required a definition line, it was easy to miss, and it required an extra `.cpp` file.

C++17 introduced `inline` members: define and initialize directly in the header file, without needing a `.cpp` file.

```cpp
// .hpp file
template<UsartInstance inst>
class UartDriver {
    static inline UART_HandleTypeDef huart;  // Definition and declaration
};
```

The compiler guarantees that there is only one instance of `huart` per template specialization, automatically handling duplicate definition issues during linking. For template classes, this is the perfect singleton pattern—no `new`, no `.cpp` file, and no worries about ODR (One Definition Rule) violations.

In our code, four `static inline` members each perform their duties:

- `huart` — HAL handle, stores USART configuration and runtime state (BSS segment, zero-initialized)
- `gpio_init_cb` = nullptr — GPIO initialization callback (function pointer)
- `receive_cb` = nullptr — Receive complete callback (function pointer)
- `transmit_cb` = nullptr — Transmit complete callback (function pointer)

All are stored in the BSS segment, occupy no heap space, and require no dynamic allocation.

---

## if constexpr: Compile-Time Dispatching

We saw `if constexpr` for the first time in the LED tutorial—used to select clock enable macros for different GPIO ports at compile time. In the UART driver, `if constexpr` appears three times, all following the same pattern: selecting different hardware operations based on the template parameter `inst`.

### enable_clock()

```cpp
void enable_clock() const {
    if constexpr (inst == UsartInstance::Usart1) {
        __HAL_RCC_USART1_CLK_ENABLE();
    } else if constexpr (inst == UsartInstance::Usart2) {
        __HAL_RCC_USART2_CLK_ENABLE();
    } else if constexpr (inst == UsartInstance::Usart3) {
        __HAL_RCC_USART3_CLK_ENABLE();
    }
}
```

`inst` is a compile-time constant (NTTP), so `if constexpr` determines which branch to take at compile time. After compilation, `enable_clock()` for `Usart1` results in only `__HAL_RCC_USART1_CLK_ENABLE()`—the code for the other two branches is completely discarded and does not appear in the binary.

### enable_interrupt()

```cpp
void enable_interrupt() const {
    if constexpr (inst == UsartInstance::Usart1) {
        HAL_NVIC_SetPriority(USART1_IRQn, 0, 0);
        HAL_NVIC_EnableIRQ(USART1_IRQn);
    } else if constexpr (inst == UsartInstance::Usart2) {
        HAL_NVIC_SetPriority(USART2_IRQn, 0, 0);
        HAL_NVIC_EnableIRQ(USART2_IRQn);
    } else if constexpr (inst == UsartInstance::Usart3) {
        HAL_NVIC_SetPriority(USART3_IRQn, 0, 0);
        HAL_NVIC_EnableIRQ(USART3_IRQn);
    }
}
```

The same pattern—selecting the corresponding NVIC configuration based on the USART instance.

### Why not use virtual functions?

Virtual functions can also achieve "different behaviors based on type". But virtual functions have runtime costs—each object needs a vtable pointer (4 bytes), and each virtual function call requires indirection through the vtable (an extra memory access). On a 72 MHz Cortex-M3, this might mean a few extra clock cycles.

More importantly, the choice of virtual functions happens at runtime—the compiler doesn't know which implementation will be called, so it cannot inline. With `if constexpr`, the choice happens at compile time—the compiler knows exactly what to call, can inline, and can eliminate dead code.

In embedded scenarios, the USART instance is determined at compile time—your code uses either USART1 or USART2, it doesn't switch at runtime. So `if constexpr` is the correct choice: determined at compile time, zero runtime overhead, and allows for maximum compiler optimization.

---

## native_instance(): From Enum to Register Pointer

```cpp
static USART_TypeDef* native_instance() {
    return reinterpret_cast<USART_TypeDef*>(static_cast<std::underlying_type_t<UsartInstance>>(inst));
}
```

This line performs a two-step conversion: `inst` (`UsartInstance` enum) → integer value → `USART_TypeDef*` (pointer).

The underlying value of `UsartInstance::Usart1` is `0x40013800`, which is the base address of the USART1 peripheral in the STM32 memory map. STM32 peripheral registers are mapped to memory address space—accessing address 0x40013800 is accessing the first register of USART1. The field layout of the `USART_TypeDef` structure corresponds one-to-one with the physical layout of the USART register group, so casting the base address to `USART_TypeDef*` allows access to all registers via structure members.

Is `reinterpret_cast` legal here? In general C++ standards, `reinterpret_cast`ing an arbitrary integer to a pointer is "implementation-defined behavior"—the standard doesn't guarantee the result. But in embedded C++, this is the standard way to access memory-mapped peripherals, and all mainstream ARM compilers (GCC, Clang, ARM Compiler) support it and optimize it well.

---

## init() Method: Initialization Pipeline

`init()` strings all the components discussed above into an initialization pipeline:

```cpp
void init(uint32_t baud_rate) {
    enable_clock();
    if (gpio_init_cb) gpio_init_cb();
    huart.Instance = native_instance();
    huart.Init.BaudRate = baud_rate;
    huart.Init.WordLength = UART_WORDLENGTH_8B;
    huart.Init.StopBits = UART_STOPBITS_1;
    huart.Init.Parity = UART_PARITY_NONE;
    huart.Init.Mode = UART_MODE_TX_RX;
    huart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart) != HAL_OK) {
        // Error handling
    }
    enable_interrupt();
}
```

Four steps: enable clock → configure GPIO (via callback) → fill HAL initialization structure → call HAL initialization. The order of every step cannot be swapped—you can't configure registers if the clock isn't on, the pin signal won't reach the USART if GPIO isn't configured, and HAL initialization must be called after all parameters are in place.

These conversions convert our `UsartInstance` value back to the `USART_TypeDef*` constant expected by the HAL library. The underlying type of `UsartInstance` is `uint32_t` (declared as `enum class UsartInstance : uint32_t`), so `static_cast` is safe and zero-overhead.

---

## Summary

This article broke down the core design of the `UartDriver` template: Empty Base Optimization (the object itself occupies no RAM), `static inline` members (one BSS storage per specialization, no .cpp definition needed), `if constexpr` compile-time dispatching (selecting different clock enables and NVIC configurations), and the `reinterpret_cast` register pointer mapping.

The next article is the final one on C++ abstraction: how Concepts constrain GPIO initialization callbacks, and how `std::unique_ptr` manages the driver's lifecycle.
