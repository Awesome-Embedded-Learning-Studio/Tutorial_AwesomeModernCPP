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
title: 第40篇：UART 驱动模板 —— 零大小抽象与编译时分发
description: ''
---
# 第40篇：UART 驱动模板 —— 零大小抽象与编译时分发

> LED 教程用模板选端口和引脚，按钮教程用模板选上下拉和有效电平。UART 驱动模板的维度是 USART 实例——但实现手法比前两个系列更精妙。

---

## UartDriver 模板的全貌

`UartDriver<UartInstance>` 是整个 UART 驱动的核心。它是一个类模板，模板参数是 `UartInstance` 枚举——选择使用哪个 USART 外设。让我们看它的完整声明：

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

注意一个关键特征：这个类**没有实例数据成员**。所有数据都是 `static inline` 的。这意味着什么？意味着 `sizeof(UartDriver<UartInstance::Usart1>)` 等于 1——空类大小。这个类本身不占用任何 RAM。

---

## 零大小空类优化

C++ 标准规定，任何完整对象类型的大小至少为 1 字节（即使它没有数据成员），因为每个对象都需要有唯一的地址。所以 `sizeof(UartDriver<Usart1>)` 是 1，不是 0。

但这个 1 字节只是对象本身的开销。真正的状态——HAL 句柄、回调函数指针——全部存储在 `static inline` 成员中。这些成员不属于对象实例，而是属于模板特化。`UartDriver<Usart1>` 和 `UartDriver<Usart2>` 各有自己独立的一套 static 成员，存储在 BSS 段中。

这个设计的妙处在于：你可以在代码中创建 `UartDriver` 的实例（比如通过 `UartManager::driver()` 返回的静态实例），但实例本身几乎不占空间。状态被从对象剥离到了模板特化级别——每个 USART 实例只有一份状态，而不是每个对象一份。如果你的代码中写了 `auto& drv1 = UartManager<Usart1>::driver();` 十次，不会有十份 `huart_`，只有一份。

---

## static inline 成员：C++17 的单例利器

在 C++17 之前，类的 `static` 成员需要在 `.cpp` 文件中单独定义：

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

这很麻烦——每个模板特化都需要一行定义，容易遗漏，而且需要一个额外的 `.cpp` 文件。

C++17 引入了 `static inline` 成员：在头文件中直接定义并初始化，不需要 `.cpp` 文件。

```cpp
static inline UART_HandleTypeDef huart_{};
```

编译器保证每个模板特化只有一份 `huart_` 实例，自动处理链接时的重复定义问题。对于模板类来说，这是完美的单例模式——不需要 `extern`、不需要 `.cpp` 文件、不需要担心 ODR（One Definition Rule）违反。

在我们的代码中，四个 `static inline` 成员各司其职：

- `huart_{}` — HAL 句柄，存储 USART 配置和运行时状态（BSS 段，零初始化）
- `gpio_init_` = nullptr — GPIO 初始化回调（函数指针）
- `rx_callback_` = nullptr — 接收完成回调（函数指针）
- `tx_callback_` = nullptr — 发送完成回调（函数指针）

全部存储在 BSS 段中，不占用堆空间，不需要动态分配。

---

## if constexpr：编译时分发

LED 教程中我们第一次见到 `if constexpr`——用于在编译时选择不同 GPIO 端口的时钟使能宏。UART 驱动中 `if constexpr` 出现了三次，都是同样的模式：根据模板参数 `INSTANCE` 选择不同的硬件操作。

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

`INSTANCE` 是编译时常量（NTTP），所以 `if constexpr` 在编译时就确定了走哪个分支。`UartDriver<Usart1>::enable_clock()` 编译后只剩 `__HAL_RCC_USART1_CLK_ENABLE();` 一条语句——其他两个分支的代码被完全丢弃，不出现在二进制中。

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

同样的模式——根据 USART 实例选择对应的 NVIC 配置。

### 为什么不用虚函数？

虚函数也能做到"根据类型选择不同的行为"。但虚函数有运行时代价——每个对象需要一个 vtable 指针（4 字节），每次虚函数调用需要通过 vtable 间接寻址（多一次内存访问）。在 72 MHz 的 Cortex-M3 上，这可能意味着几个额外的时钟周期。

更重要的是，虚函数的选择发生在运行时——编译器不知道具体会调用哪个实现，所以不能做内联优化。而 `if constexpr` 的选择发生在编译时——编译器完全知道要调用什么，可以内联、可以消除死代码。

在嵌入式场景中，USART 实例在编译时就是确定的——你的代码要么用 USART1，要么用 USART2，不会在运行时切换。所以 `if constexpr` 是完全正确的选择：编译时确定、零运行时开销、编译器可以做最大程度的优化。

---

## native_instance()：从枚举到寄存器指针

```cpp
static constexpr USART_TypeDef* native_instance() noexcept {
    return reinterpret_cast<USART_TypeDef*>(
        static_cast<uintptr_t>(INSTANCE));
}
```

这一行做了两步转换：`INSTANCE`（`UartInstance` 枚举）→ `uintptr_t`（整数值）→ `USART_TypeDef*`（指针）。

`UartInstance::Usart1` 的底层值是 `USART1_BASE`（0x40013800），这是 USART1 外设在 STM32 内存映射中的基地址。STM32 的外设寄存器映射到内存地址空间——访问地址 0x40013800 就是访问 USART1 的第一个寄存器。`USART_TypeDef` 结构体的字段布局和 USART 寄存器组的物理布局一一对应，所以把基地址转换成 `USART_TypeDef*` 就能通过结构体成员访问所有寄存器。

`reinterpret_cast` 在这里合法吗？在通用 C++ 标准中，`reinterpret_cast` 一个任意整数到指针是"实现定义行为"——标准不保证结果。但在嵌入式 C++ 中，这是访问内存映射外设的标准做法，所有主流 ARM 编译器（GCC、Clang、ARM Compiler）都支持并且优化得很好。

---

## init() 方法：初始化流水线

`init()` 把前面讲的所有零件串成一条初始化流水线：

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

四步：使能时钟 → 配置 GPIO（通过回调）→ 填充 HAL 初始化结构体 → 调用 HAL 初始化。每一步的顺序都不能调换——时钟没开就配不了寄存器，GPIO 没配好引脚信号就到不了 USART，HAL 初始化必须在所有参数就位后调用。

`static_cast<uint32_t>(config.word_length)` 这些转换把我们的 `enum class` 值转回 HAL 库期望的 `uint32_t` 常量。`enum class` 的底层类型是 `uint32_t`（在 `uart_config.hpp` 中声明为 `enum class WordLength : uint32_t`），所以 `static_cast` 是安全的、零开销的。

---

## 小结

这一篇拆解了 `UartDriver<UartInstance>` 模板的核心设计：零大小空类优化（对象本身不占 RAM）、`static inline` 成员（每特化一份 BSS 存储，不需要 .cpp 定义）、`if constexpr` 编译时分发（选择不同的时钟使能和 NVIC 配置）、`native_instance()` 的 `reinterpret_cast` 指针映射。

下一篇是 C++ 抽象的最后一篇：Concepts 如何约束 GPIO 初始化回调，以及 `UartManager` 如何管理驱动的生命周期。
