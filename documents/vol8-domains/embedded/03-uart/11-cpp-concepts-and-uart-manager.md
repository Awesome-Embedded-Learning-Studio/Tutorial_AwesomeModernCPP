---
title: "第41篇：Concepts 约束 GPIO 初始化 + UartManager —— 类型安全的组装"
description: ""
tags:
  - cpp-modern
  - intermediate
  - stm32f1
difficulty: intermediate
platform: stm32f1
chapter: 17
order: 11
---
# 第41篇：Concepts 约束 GPIO 初始化 + UartManager —— 类型安全的组装

> 按钮教程用 Concepts 约束回调函数的签名。UART 教程用它约束 GPIO 初始化回调。同一个机制，不同的场景—— Concepts 的价值在于"让编译器帮你检查接口契约"。

---

## UartGpioInitializer Concept

在讲 Concepts 之前，先看问题。`UartDriver` 的 `set_gpio_init()` 方法接受一个可调用对象——用户注册的 GPIO 初始化函数。在纯模板编程中（没有 Concepts），这个函数的签名可能是：

```cpp
template <typename F> static void set_gpio_init(F fn) { gpio_init_ = fn; }
```

`F` 可以是任何类型。如果你传了一个带参数的函数（比如 `void gpio_init(int pin)`），编译时不会报错——错误要到 `init()` 内部调用 `gpio_init_()` 时才会爆出来，报错信息是一大段模板实例化的调用栈，根本看不懂。

Concepts 改变了这一点。我们的代码定义了一个 `UartGpioInitializer` Concept：

```cpp
// 来源: code/stm32f1-tutorials/3_uart_logger/device/uart/uart_driver.hpp
template <typename F>
concept UartGpioInitializer =
    std::invocable<F> && std::is_nothrow_invocable_v<F>;
```

这个 Concept 要求 `F` 满足两个条件：

1. **`std::invocable&lt;F&gt;`**：`F` 可以无参数调用（`f()`）。不接受带参数的函数。
2. **`std::is_nothrow_invocable_v&lt;F&gt;`**：调用 `F` 不会抛出异常。

然后在 `set_gpio_init()` 中使用这个 Concept 作为约束：

```cpp
template <UartGpioInitializer F>
static void set_gpio_init(F fn) noexcept { gpio_init_ = fn; }
```

`UartGpioInitializer F` 告诉编译器："`F` 必须满足 `UartGpioInitializer` Concept 的所有要求。"如果你传了一个不符合要求的可调用对象，编译器在 `set_gpio_init()` 调用点就会报错——错误信息会清楚地告诉你"不满足约束 `UartGpioInitializer`"，而不是一大段模板实例化栈。

### 为什么要求 nothrow？

我们的项目通过 `-fno-exceptions` 禁用了异常。如果 GPIO 初始化函数允许抛出异常，而 `init()` 在内部调用它时异常被触发，程序会调用 `std::terminate()` 直接终止——因为没有异常处理机制来捕获它。

`std::is_nothrow_invocable_v&lt;F&gt;` 在编译时检查：如果 `F` 的 `operator()` 或函数签名没有 `noexcept` 声明，Concept 检查可能仍然通过（因为编译器在禁用异常时不会严格区分 nothrow 和可能抛出）。但明确声明 Concept 约束至少表达了设计意图："GPIO 初始化不应该抛出异常。"

在我们的代码中，`usart1_gpio_init()` 确实被声明为 `noexcept`：

```cpp
static void usart1_gpio_init() noexcept { ... }
```

---

## UartManager：不可实例化的生命周期管理器

`UartManager` 是一个纯静态工具类——它的全部作用是提供对 `UartDriver` 的单例访问和 HAL 句柄的桥梁。你不应该也不能创建它的实例：

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

### 删除所有构造函数

五个 `= delete` 声明确保了这个类不可能被实例化、拷贝或移动。任何尝试创建 `UartManager&lt;Usart1&gt; mgr;` 的代码都会编译报错。这不是过度防御——因为 `UartManager` 没有实例状态（`UartDriver` 的状态都在 `static inline` 成员中），创建实例没有任何意义。

### driver()：Meyer's Singleton

`driver()` 是一个静态方法，内部使用了 Meyers' Singleton 模式：

```cpp
static auto driver() -> Driver& {
    static Driver drv;
    return drv;
}
```

`static Driver drv` 是一个函数内的静态局部变量。C++ 保证它只被初始化一次（第一次调用 `driver()` 时），后续调用直接返回已有的实例。而且初始化是线程安全的（C++11 保证）——虽然在我们的 bare-metal 环境中没有多线程，但这个保证也没有运行时代价。

由于 `Driver`（即 `UartDriver&lt;INSTANCE&gt;`）没有实例数据成员，`sizeof(Driver)` 是 1。`static Driver drv` 占用 1 字节的 BSS 空间——几乎可以忽略。

### handle()：extern "C" 的桥梁

```cpp
static auto handle() -> UART_HandleTypeDef* {
    return Driver::native_handle();
}
```

`handle()` 返回底层 HAL 句柄的指针。这个方法主要用于需要 C 链接的代码——`printf_redirect.cpp` 和 `uart_irq.cpp`。这些文件中的函数在 `extern "C"` 块中，它们需要 `UART_HandleTypeDef*` 来调用 HAL 函数，但它们不能直接访问 C++ 命名空间中的 `static inline` 成员。

`handle()` 就是一座桥梁：C 链接的代码通过这个方法获取句柄指针，而不需要知道 `UartDriver` 的内部结构。

这替代了传统的全局变量模式：

```cpp
// 传统做法（C 风格）
UART_HandleTypeDef huart1;  // 全局变量，任何地方都能访问和修改

// 我们的做法（C++ 风格）
auto* huart = UartManager<UartInstance::Usart1>::handle();  // 只读访问
```

传统做法中 `huart1` 是一个全局变量——任何代码都可以读写它的任何字段。我们的做法中，`handle()` 只返回指针，不提供对 `UartDriver` 内部状态的可修改访问。虽然拿到指针后理论上还是能通过指针修改内容，但至少访问路径是明确的、可追溯的。

---

## 初始化管线：从调用者视角看

把所有组装在一起，`main.cpp` 中的初始化代码看起来像这样：

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

五步初始化管线，每一步的职责清晰，顺序不可调换。从调用者的角度看，这是一套声明式的接口——"告诉驱动你要什么"，而不是"手动配置寄存器"。底层所有的硬件细节（时钟、GPIO、HAL 句柄、NVIC）都被封装在模板和 Concepts 约束的背后。

---

## 与 LED/Button 系列的单例模式对比

如果你还记得 LED 教程中的 `ClockConfig`，它使用了一个 `SimpleSingleton` 基类来保证全局唯一实例：

```cpp
class ClockConfig : public base::SimpleSingleton<ClockConfig> { ... };
```

`UartManager` 的单例实现不同——它通过删除所有构造函数 + 静态 `driver()` 方法来实现。为什么不也用 `SimpleSingleton`？

因为 `ClockConfig` 有实例状态（时钟配置参数），它确实需要唯一的实例来管理这些状态。而 `UartManager` 没有任何实例状态——`UartDriver` 的所有状态都在 `static inline` 成员中。`UartManager` 只是一个访问接口，不是一个状态持有者。删除构造函数比继承 `SimpleSingleton` 更直接地表达了"我不需要实例"的语义。

---

## 小结

这一篇讲了两个设计工具：Concepts 约束 GPIO 初始化回调的签名（`invocable + nothrow`），`UartManager` 通过删除构造函数 + Meyers' Singleton 管理驱动生命周期。`handle()` 方法是 C 链接代码访问 HAL 句柄的桥梁，替代了传统的全局变量模式。

下一篇是 C++ 抽象的收官之作——`main.cpp` 的完整走读。所有之前讲过的零件——LED、Button、UART 驱动、printf 重定向、中断接收、命令处理器——全部在这里汇合。
