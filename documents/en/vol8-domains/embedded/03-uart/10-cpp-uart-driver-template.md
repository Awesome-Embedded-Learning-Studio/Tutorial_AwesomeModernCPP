# Part 40: UART Driver Template — Zero-Size Abstraction and Compile-Time Dispatch

> The LED tutorial uses templates to select ports and pins, and the button tutorial uses templates to select pull-up/pull-down and active levels. The dimension of the UART driver template is the USART instance—but the implementation technique is more elegant than the previous two series.

---

## The Full Picture of the UartDriver Template

`UartDriver<UartInstance>` is the core of the entire UART driver. It is a class template where the template parameter is a `UartInstance` enum—selecting which USART peripheral to use. Let's look at its complete declaration:

```cpp
// 来源: code/stm32f1-tutorials/3_uart_logger/device/uart/uart_driver.hpp
template <UartInstance INSTANCE> class UartDriver {
  public:
    void init(const UartConfig& config);
    template <UartGpioInitializer F> static void set_gpio_init(F fn) noexcept;
    void deinit();

    // Blocking API
    auto send(std::span<const std::byte> data, uint32_t timeout_ms)
        -> std::expected<size_t, UartError>;
    auto receive(std::span<std::byte> buffer, uint32_t timeout_ms)
        -> std::expected<size_t, UartError>;

    // Convenience API
    void send_string(std::string_view str);

    // Interrupt API
    auto send_it(std::span<const std::byte> data) -> std::expected<void, UartError>;
    auto receive_it(std::span<std::byte> buffer) -> std::expected<void, UartError>;
    void enable_interrupt();

    // Callback registration
    using RxCallback = void (*)(std::span<const std::byte>);
    using TxCallback = void (*)();
    void set_rx_callback(RxCallback cb);
    void set_tx_callback(TxCallback cb);

    // Manager access
    static auto native_handle() -> UART_HandleTypeDef*;

  private:
    static constexpr USART_TypeDef* native_instance() noexcept;
    static inline void enable_clock();
    static inline UART_HandleTypeDef huart_{};
    static inline void (*gpio_init_)() = nullptr;
    static inline RxCallback rx_callback_ = nullptr;
    static inline TxCallback tx_callback_ = nullptr;
};
```

Note a key characteristic: this class **has no instance data members**. All data is `static inline`. What does this mean? It means `sizeof(UartDriver<UartInstance::Usart1>)` equals 1—the size of an empty class. The class itself occupies no RAM.

---

## Zero-Size Empty Class Optimization

The C++ standard dictates that the size of any complete object type is at least one byte (even if it has no data members), because every object must have a unique address. So `sizeof(UartDriver<Usart1>)` is 1, not 0.

But this one byte is only the overhead of the object itself. The real state—the HAL handle, callback function pointers—is entirely stored in `static inline` members. These members do not belong to the object instance, but rather to the template specialization. `UartDriver<Usart1>` and `UartDriver<Usart2>` each have their own independent set of static members, stored in the BSS segment.

The beauty of this design is that we can create instances of `UartDriver` in our code (for example, through static instances returned by `UartManager::driver()`), but the instances themselves take up almost no space. The state is stripped from the object to the template specialization level—each USART instance has only one copy of the state, not one per object. If we write `auto& drv1 = UartManager<Usart1>::driver();` ten times in our code, there won't be ten copies of `huart_`, only one.

---

## static inline Members: The C++17 Singleton Weapon

Before C++17, a class's `static` members needed to be defined separately in a `.cpp` file:

```cpp
// uart_driver.hpp
template <UartInstance INSTANCE>
class UartDriver {
    static UART_HandleTypeDef huart_;
};

// uart_driver.cpp（需要一个专门的 .cpp）
template <>
UART_HandleTypeDef UartDriver<UartInstance::Usart1>::huart_{};
```

This was cumbersome—every template specialization required a line of definition, it was easy to miss one, and it required an additional `.cpp` file.

C++17 introduced `static inline` members: we can define and initialize them directly in the header file, without needing a `.cpp` file.

```cpp
static inline UART_HandleTypeDef huart_{};
```

The compiler guarantees that each template specialization has only one instance of the `huart_`, automatically handling duplicate definition issues at link time. For template classes, this is the perfect singleton pattern—no need for `extern`, no need for a `.cpp` file, and no need to worry about ODR (One Definition Rule) violations.

In our code, the four `static inline` members each have their own responsibilities:

- `huart_{}` — The HAL handle, storing USART configuration and runtime state (BSS segment, zero-initialized)
- `gpio_init_` = nullptr — GPIO initialization callback (function pointer)
- `rx_callback_` = nullptr — Receive complete callback (function pointer)
- `tx_callback_` = nullptr — Transmit complete callback (function pointer)

All are stored in the BSS segment, occupying no heap space and requiring no dynamic allocation.

---

## if constexpr: Compile-Time Dispatch

We first saw `if constexpr` in the LED tutorial—used to select different GPIO port clock enable macros at compile time. In the UART driver, `if constexpr` appears three times, all following the same pattern: selecting different hardware operations based on the template parameter `INSTANCE`.

### enable_clock()

```cpp
static inline void enable_clock() {
    if constexpr (INSTANCE == UartInstance::Usart1) {
        __HAL_RCC_USART1_CLK_ENABLE();
    } else if constexpr (INSTANCE == UartInstance::Usart2) {
        __HAL_RCC_USART2_CLK_ENABLE();
    } else if constexpr (INSTANCE == UartInstance::Usart3) {
        __HAL_RCC_USART3_CLK_ENABLE();
    }
}
```

`INSTANCE` is a compile-time constant (NTTP), so `if constexpr` determines which branch to take at compile time. After compilation, `UartDriver<Usart1>::enable_clock()` is left with only the `__HAL_RCC_USART1_CLK_ENABLE();` statement—the code in the other two branches is completely discarded and does not appear in the binary.

### enable_interrupt()

```cpp
void enable_interrupt() {
    if constexpr (INSTANCE == UartInstance::Usart1) {
        HAL_NVIC_SetPriority(USART1_IRQn, 0, 0);
        HAL_NVIC_EnableIRQ(USART1_IRQn);
    } else if constexpr (INSTANCE == UartInstance::Usart2) {
        HAL_NVIC_SetPriority(USART2_IRQn, 0, 0);
        HAL_NVIC_EnableIRQ(USART2_IRQn);
    } else if constexpr (INSTANCE == UartInstance::Usart3) {
        HAL_NVIC_SetPriority(USART3_IRQn, 0, 0);
        HAL_NVIC_EnableIRQ(USART3_IRQn);
    }
}
```

The same pattern—selecting the corresponding NVIC configuration based on the USART instance.

### Why Not Virtual Functions?

Virtual functions can also achieve "selecting different behavior based on type." But virtual functions have a runtime cost—each object needs a vtable pointer (4 bytes), and every virtual function call requires indirect addressing through the vtable (an extra memory access). On a 72 MHz Cortex-M3, this could mean a few extra clock cycles.

More importantly, the selection with virtual functions happens at runtime—the compiler doesn't know which implementation will actually be called, so it cannot perform inline optimization. With `if constexpr`, the selection happens at compile time—the compiler knows exactly what to call, can inline it, and can eliminate dead code.

In embedded scenarios, the USART instance is determined at compile time—our code either uses USART1 or USART2, and doesn't switch at runtime. So `if constexpr` is the absolutely correct choice: determined at compile time, zero runtime overhead, and the compiler can perform maximum optimization.

---

## native_instance(): From Enum to Register Pointer

```cpp
static constexpr USART_TypeDef* native_instance() noexcept {
    return reinterpret_cast<USART_TypeDef*>(
        static_cast<uintptr_t>(INSTANCE));
}
```

This single line performs a two-step conversion: `INSTANCE` (`UartInstance` enum) → `uintptr_t` (integer value) → `USART_TypeDef*` (pointer).

The underlying value of `UartInstance::Usart1` is `USART1_BASE` (0x40013800), which is the base address of the USART1 peripheral in the STM32 memory map. STM32 peripheral registers are memory-mapped—accessing address 0x40013800 is equivalent to accessing the first register of USART1. The field layout of the `USART_TypeDef` struct corresponds one-to-one with the physical layout of the USART register group, so casting the base address to a `USART_TypeDef*` allows us to access all registers through struct members.

Is `reinterpret_cast` legal here? In the general C++ standard, `reinterpret_cast` an arbitrary integer to a pointer is "implementation-defined behavior"—the standard does not guarantee the result. But in embedded C++, this is the standard way to access memory-mapped peripherals, and all mainstream ARM compilers (GCC, Clang, ARM Compiler) support it and optimize it well.

---

## The init() Method: Initialization Pipeline

`init()` strings all the components discussed above into an initialization pipeline:

```cpp
void init(const UartConfig& config) {
    enable_clock();              // 1. 使能 USART 时钟
    if (gpio_init_) {
        gpio_init_();            // 2. 调用用户注册的 GPIO 初始化
    }
    huart_.Instance = native_instance();  // 3. 设置 USART 基地址
    huart_.Init.BaudRate = config.baud_rate;
    huart_.Init.WordLength = static_cast<uint32_t>(config.word_length);
    huart_.Init.StopBits = static_cast<uint32_t>(config.stop_bits);
    huart_.Init.Parity = static_cast<uint32_t>(config.parity);
    huart_.Init.Mode = static_cast<uint32_t>(config.mode);
    huart_.Init.HwFlowCtl = static_cast<uint32_t>(config.hw_flow);
    huart_.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&huart_);      // 4. 调用 HAL 初始化
}
```

Four steps: enable clock → configure GPIO (via callback) → populate the HAL initialization struct → call HAL initialization. The order of each step cannot be swapped—we can't configure registers if the clock isn't enabled, pin signals won't reach the USART if GPIO isn't configured, and HAL initialization must be called after all parameters are in place.

`static_cast<uint32_t>(config.word_length)` these conversions convert our `enum class` values back to the `uint32_t` constants expected by the HAL library. The underlying type of `enum class` is `uint32_t` (declared as `enum class WordLength : uint32_t` in `uart_config.hpp`), so `static_cast` is safe and has zero overhead.

---

## Summary

This post broke down the core design of the `UartDriver<UartInstance>` template: zero-size empty class optimization (the object itself occupies no RAM), `static inline` members (one copy of BSS storage per specialization, no .cpp definition needed), `if constexpr` compile-time dispatch (selecting different clock enables and NVIC configurations), and the `reinterpret_cast` pointer mapping of `native_instance()`.

The next post is the final one on C++ abstractions: how Concepts constrain the GPIO initialization callback, and how `UartManager` manages the driver's lifecycle.
