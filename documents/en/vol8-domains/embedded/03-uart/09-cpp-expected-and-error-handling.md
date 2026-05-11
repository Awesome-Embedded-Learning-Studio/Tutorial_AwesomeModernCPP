# Part 39: std::expected Error Handling — A Better Choice Than Exceptions in Embedded

> Phase five begins with error handling. Embedded projects disable exceptions, and bare error codes are easily ignored. C++23's `std::expected` fills this gap perfectly.

---

## The Embedded Error Handling Trilemma

In any programming scenario, error handling must solve one problem: a function can succeed or fail, so how does the caller know the result?

In PC-based C++, the standard answer is exceptions. A function throws an exception, and the caller catches it with try/catch. Exceptions cannot be silently ignored — an uncaught exception terminates the program. However, exceptions have a runtime cost (stack unwinding, RTTI information, exception tables), and our CMakeLists.txt explicitly disables them via `-fno-exceptions -fno-rtti`. On resource-constrained STM32s, the overhead of exceptions is unacceptable.

The C approach is to return error codes. `HAL_UART_Transmit()` returns `HAL_StatusTypeDef` — `HAL_OK`, `HAL_ERROR`, `HAL_BUSY`, or `HAL_TIMEOUT`. This is lightweight, but it has a fatal flaw: **error codes can be silently ignored**. If you write `HAL_UART_Transmit(&huart, data, len, timeout);` without checking the return value, the compiler won't complain, and the code compiles fine. When something goes wrong at runtime — data wasn't sent, a timeout occurred, a hardware fault happened — you have no idea what happened.

We need a mechanism that combines the "cannot be ignored" safety of exceptions with the "zero runtime overhead" efficiency of error codes. C++23's `std::expected<T, E>` is the answer.

---

## UartError: Type-Safe Error Codes

Let's first look at our error type definition:

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

Four error values, each corresponding to a real failure scenario in UART operations:

- **Timeout**: The operation did not complete within the specified time. For example, the timeout parameter of `HAL_UART_Transmit()` expired.
- **NotInitialized**: A send/receive was called before the driver was initialized. The code doesn't explicitly check this state yet, but the error type reserves this value for future use.
- **HardwareFault**: A low-level hardware failure — a USART peripheral anomaly, a DMA transfer error, and so on.
- **Busy**: The peripheral is busy. For example, calling `send_it()` while an interrupt-based transmission is already in progress.

Why use `enum class` instead of a plain enum or `int`? Because we already experienced this in the LED tutorial — `enum class` does not implicitly convert to `int`. You cannot use `UartError::Timeout` as a `0`, nor can you use `3` as a `UartError`. The type system enforces this for you.

---

## Basic Usage of std::expected

`std::expected<T, E>` is a "value or error" container. It either holds a success value `T` or an error value `E`. You can think of it as a "safer optional" — `std::optional<T>` only tells you "whether there is a value," while `std::expected<T, E>` tells you "there is a value, or there is no value because of reason E."

In our code, the return type of the `send()` method is:

```cpp
auto send(std::span<const std::byte> data, uint32_t timeout_ms)
    -> std::expected<size_t, UartError>
```

On success, it returns the number of bytes sent (`size_t`); on failure, it returns the specific `UartError`.

How the caller uses it:

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

Key point: **you cannot use the return value directly without checking it**. `result` is not a `size_t`; it is a `std::expected<size_t, UartError>`. You must first check whether `result` has a value (via `if (result)` or `result.has_value()`) before you can access the success value through `*result` or `result.value()`. If you forget to check and directly call `*result`, it triggers undefined behavior (typically a hard fault in a bare-metal environment) when an error occurs.

Compare this with C-style error codes. `HAL_UART_Transmit()` returns `HAL_StatusTypeDef`. You can completely ignore the return value without the compiler issuing a warning. `std::expected` uses the type system to make it "hard to forget checking" — although you still *can* skip the check, the code's intent is much clearer, and the compiler can work with the `[[nodiscard]]` attribute to emit a warning when the result is unchecked.

---

## Mapping HAL_StatusTypeDef to UartError

Inside the `send()` method, we map HAL's return values to our `UartError` domain:

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

`std::unexpected(UartError::Timeout)` constructs an `expected` object that "contains an error value." This syntax is symmetric with directly returning a success value (`return data.size()`) — return the value on success, return `std::unexpected(错误值)` on failure.

The blocking receive `receive()` has exactly the same structure:

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

The return types for interrupt-based send and receive are slightly different — there is no data to return on success (it merely "started the interrupt operation"), so they return `std::expected<void, UartError>`. The error mapping also includes the `HAL_BUSY` case:

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

`return {}` constructs an `std::expected<void, UartError>` that is "successful but has no value." `HAL_BUSY` indicates the peripheral is busy (already sending or receiving), which maps to `UartError::Busy`.

---

## Runtime Cost of std::expected

The memory layout of `std::expected` is essentially a tagged union — a discriminant flag (success/failure) plus storage space for either the success value or the error value. `sizeof(std::expected<size_t, UartError>)` is typically equal to `sizeof(size_t) + sizeof(UartError) + 少量对齐填充`, roughly eight to twelve bytes.

Runtime overhead: constructing and checking `std::expected` takes only a few CPU instructions — a conditional branch to determine success or failure, and a value read. There is practically no difference compared to manually writing `if (result == HAL_OK)`. This is why it suits embedded systems — type safety comes with almost no runtime cost.

---

## Relationship with std::variant

If you read the `std::variant<Pressed, Released>` event system in the button tutorial, you might think `std::expected` and `std::variant` look somewhat similar. Indeed, the underlying implementation of `std::expected<T, E>` is very similar to `std::variant<T, E>` — both are type-safe unions. The difference lies in semantics: `std::expected` explicitly distinguishes between "success" and "failure," whereas `std::variant` is just "one of several types." `std::expected` provides interfaces specifically geared toward error handling, such as `has_value()`, `value()`, and `error()`, making it more intuitive than the generic `std::visit`.

---

## Summary

This part introduced C++23's `std::expected` as a solution for embedded error handling. It bridges the gap between exceptions (too heavy) and error codes (ignorable) — it forces the caller to handle errors through the type system while maintaining zero runtime overhead. Our `UartError` enum defines four error types, and the four methods `send()`/`receive()`/`send_it()`/`receive_it()` return either a success value or an error value via `std::expected`.

In the next part, we will zoom out from individual methods to the entire driver class — exploring how the `UartDriver<UartInstance>` template achieves zero-size abstraction and compile-time dispatch.
