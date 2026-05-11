---
title: "第39篇：std::expected 错误处理 —— 嵌入式中比异常更好的选择"
description: ""
tags:
  - cpp-modern
  - intermediate
  - stm32f1
difficulty: intermediate
platform: stm32f1
chapter: 17
order: 9
---
# 第39篇：std::expected 错误处理 —— 嵌入式中比异常更好的选择

> 阶段五从错误处理开始。嵌入式项目禁用异常，裸错误码又容易被忽略。C++23 的 `std::expected` 正好填补了这个空白。

---

## 嵌入式错误处理的三难困境

在任何编程场景中，错误处理都需要解决一个问题：函数可能成功也可能失败，调用方怎么知道结果？

在 PC 端 C++ 中，标准答案是异常。函数抛出异常，调用方用 try/catch 捕获。异常不能被默默忽略——未捕获的异常会导致程序终止。但异常有运行时代价（栈展开、RTTI 信息、异常表），我们的 CMakeLists.txt 通过 `-fno-exceptions -fno-rtti` 明确禁用了它们。在资源受限的 STM32 上，异常的开销不可接受。

C 语言的方案是返回错误码。`HAL_UART_Transmit()` 返回 `HAL_StatusTypeDef`——`HAL_OK`、`HAL_ERROR`、`HAL_BUSY` 或 `HAL_TIMEOUT`。这很轻量，但有一个致命问题：**错误码可以被默默忽略**。如果你写了 `HAL_UART_Transmit(&huart, data, len, timeout);` 而不检查返回值，编译器不会报错，代码照常编译通过。等到运行时出了问题——数据没发出去、超时了、硬件故障了——你完全不知道发生了什么。

我们需要一种机制，既有异常"不能被忽略"的安全性，又有错误码"零运行时开销"的效率。C++23 的 `std::expected<T, E>` 就是这个答案。

---

## UartError：类型安全的错误码

先看我们的错误类型定义：

```cpp
// 来源: code/stm32f1-tutorials/3_uart_logger/device/uart/uart_error.hpp
namespace device::uart {

enum class UartError {
    Timeout,
    NotInitialized,
    HardwareFault,
    Busy,
};

} // namespace device::uart
```

四个错误值，每一个都对应 UART 操作中真实可能发生的故障场景：

- **Timeout**：操作在指定时间内未完成。比如 `HAL_UART_Transmit()` 的超时参数到期。
- **NotInitialized**：驱动未初始化就调用了发送/接收。目前代码中没有显式检查这个状态，但错误类型预留了这个值供将来使用。
- **HardwareFault**：底层硬件故障——USART 外设异常、DMA 传输错误等。
- **Busy**：外设正在忙。比如已经在进行一次中断发送时又调用了 `send_it()`。

为什么用 `enum class` 而不是普通 enum 或 `int`？因为在 LED 教程中我们就已经体会到了——`enum class` 不会隐式转换为 `int`，你不能把 `UartError::Timeout` 当成 `0` 来用，也不能把 `3` 当成 `UartError` 来用。类型系统帮你把关。

---

## std::expected 的基本用法

`std::expected&lt;T, E&gt;` 是一个"值或错误"的容器。它要么持有一个成功值 `T`，要么持有一个错误值 `E`。你可以把它理解为"更安全的 optional"——`std::optional&lt;T&gt;` 只告诉你"有没有值"，`std::expected&lt;T, E&gt;` 告诉你"有值，或者没有值且原因是 E"。

在我们的代码中，`send()` 方法的返回类型是：

```cpp
auto send(std::span<const std::byte> data, uint32_t timeout_ms)
    -> std::expected<size_t, UartError>
```

成功时返回发送的字节数（`size_t`），失败时返回具体的 `UartError`。

调用端的使用方式：

```cpp
auto result = driver.send(data, 1000);
if (result) {
    // 成功，*result 是发送的字节数
    size_t sent = *result;
} else {
    // 失败，result.error() 是 UartError
    UartError err = result.error();
    if (err == UartError::Timeout) {
        // 处理超时
    }
}
```

关键点：**你不能直接使用返回值而不检查**。`result` 不是 `size_t`，它是 `std::expected&lt;size_t, UartError&gt;`。你必须先检查 `result` 是否有值（通过 `if (result)` 或 `result.has_value()`），然后才能通过 `*result` 或 `result.value()` 访问成功值。如果你忘记检查直接 `*result`，在出错时会触发未定义行为（裸机环境中通常是一个 hard fault）。

和 C 风格的错误码对比一下。`HAL_UART_Transmit()` 返回 `HAL_StatusTypeDef`。你可以完全不检查返回值，编译器不报警告。`std::expected` 通过类型系统让你"不容易忘记检查"——虽然你仍然可以不检查，但代码的意图更清晰，编译器可以配合 `[[nodiscard]]` 属性在未检查时发出警告。

---

## HAL_StatusTypeDef 到 UartError 的映射

`send()` 方法内部把 HAL 的返回值映射到我们的 `UartError` 域：

```cpp
// 来源: code/stm32f1-tutorials/3_uart_logger/device/uart/uart_driver.hpp
auto send(std::span<const std::byte> data, uint32_t timeout_ms)
    -> std::expected<size_t, UartError> {
    auto* ptr = reinterpret_cast<const uint8_t*>(data.data());
    HAL_StatusTypeDef result = HAL_UART_Transmit(&huart_, ptr, data.size(), timeout_ms);
    if (result == HAL_OK)
        return data.size();
    if (result == HAL_TIMEOUT)
        return std::unexpected(UartError::Timeout);
    return std::unexpected(UartError::HardwareFault);
}
```

`std::unexpected(UartError::Timeout)` 构造一个"包含错误值"的 `expected` 对象。和直接返回成功值（`return data.size()`）的语法对称——成功返回值，失败返回 `std::unexpected(错误值)`。

阻塞式接收 `receive()` 的结构完全相同：

```cpp
auto receive(std::span<std::byte> buffer, uint32_t timeout_ms)
    -> std::expected<size_t, UartError> {
    auto* ptr = reinterpret_cast<uint8_t*>(buffer.data());
    HAL_StatusTypeDef result = HAL_UART_Receive(&huart_, ptr, buffer.size(), timeout_ms);
    if (result == HAL_OK)
        return buffer.size();
    if (result == HAL_TIMEOUT)
        return std::unexpected(UartError::Timeout);
    return std::unexpected(UartError::HardwareFault);
}
```

中断式发送和接收的返回类型稍有不同——成功时没有数据需要返回（只是"启动了中断操作"），所以返回 `std::expected&lt;void, UartError&gt;`。错误映射也多了 `HAL_BUSY` 的情况：

```cpp
auto send_it(std::span<const std::byte> data) -> std::expected<void, UartError> {
    auto* ptr = reinterpret_cast<const uint8_t*>(data.data());
    HAL_StatusTypeDef result = HAL_UART_Transmit_IT(&huart_, ptr, data.size());
    if (result == HAL_OK)
        return {};
    if (result == HAL_BUSY)
        return std::unexpected(UartError::Busy);
    return std::unexpected(UartError::HardwareFault);
}
```

`return {}` 构造一个"成功但无值"的 `std::expected&lt;void, UartError&gt;`。`HAL_BUSY` 表示外设正在忙（已经在发送或接收中），映射到 `UartError::Busy`。

---

## std::expected 的运行时代价

`std::expected` 的内存布局本质上是一个 tagged union——一个判别标志（成功/失败）加上成功值或错误值的存储空间。`sizeof(std::expected&lt;size_t, UartError&gt;)` 通常等于 `sizeof(size_t) + sizeof(UartError) + 少量对齐填充`，大约 8-12 字节。

运行时开销：构造和检查 `std::expected` 都是几个 CPU 指令——一个条件分支判断成功/失败，一个值读取。和手动写 `if (result == HAL_OK)` 几乎没有区别。这就是为什么它适合嵌入式——类型安全性几乎不带来运行时代价。

---

## 和 std::variant 的关系

如果你读过按钮教程的 `std::variant&lt;Pressed, Released&gt;` 事件系统，可能会觉得 `std::expected` 和 `std::variant` 有点像。确实，`std::expected&lt;T, E&gt;` 在底层实现上和 `std::variant&lt;T, E&gt;` 很相似——都是一个类型安全的联合体。区别在于语义：`std::expected` 明确区分"成功"和"失败"，而 `std::variant` 只是"多种类型之一"。`std::expected` 提供了 `has_value()`、`value()`、`error()` 等专门面向错误处理的接口，比通用 `std::visit` 更直观。

---

## 小结

这一篇引入了 C++23 的 `std::expected` 作为嵌入式错误处理的解决方案。它弥补了异常（太重）和错误码（可忽略）之间的空白——通过类型系统强制调用方处理错误，同时保持零运行时开销。我们的 `UartError` 枚举定义了四种错误类型，`send()`/`receive()`/`send_it()`/`receive_it()` 四个方法通过 `std::expected` 返回成功值或错误值。

下一篇我们把视角从单个方法拉远到整个驱动类——`UartDriver&lt;UartInstance&gt;` 模板是如何实现零大小抽象和编译时分发的。
