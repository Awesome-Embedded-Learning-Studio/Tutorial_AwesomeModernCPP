---
chapter: 17
difficulty: intermediate
order: 9
platform: stm32f1
reading_time_minutes: 7
tags:
- cpp-modern
- intermediate
- stm32f1
title: 'Part 39: std::expected Error Handling — A Better Choice Than Exceptions in
  Embedded Systems'
translation:
  engine: anthropic
  source: documents/vol8-domains/embedded/03-uart/09-cpp-expected-and-error-handling.md
  source_hash: 3ceccc30709c1f1a003eb607da2588c224478548f7ff7a09157f2bec7ce526ec
  token_count: 1380
  translated_at: '2026-05-26T12:19:16.660521+00:00'
description: ''
---
# Part 39: `std::expected` Error Handling — A Better Choice Than Exceptions in Embedded

> Phase five begins with error handling. Embedded projects disable exceptions, and bare error codes are easily ignored. C++23's `std::expected` fills this gap perfectly.

---

## The Embedded Error Handling Trilemma

In any programming scenario, error handling must solve one problem: a function can succeed or fail, so how does the caller know the result?

In PC-based C++, the standard answer is exceptions. A function throws an exception, and the caller catches it with try/catch. Exceptions cannot be silently ignored — an uncaught exception terminates the program. However, exceptions have a runtime cost (stack unwinding, RTTI information, exception tables), and our CMakeLists.txt explicitly disables them via `-fno-exceptions`. On resource-constrained STM32s, the overhead of exceptions is unacceptable.

The C approach is to return error codes. `HAL_UART_Transmit` returns `HAL_StatusTypeDef` — `HAL_OK`, `HAL_ERROR`, `HAL_BUSY`, or `HAL_TIMEOUT`. This is lightweight, but it has a fatal flaw: **error codes can be silently ignored**. If you write `HAL_UART_Transmit(...)` without checking the return value, the compiler won't complain, and the code compiles fine. When something goes wrong at runtime — data wasn't sent, a timeout occurred, a hardware fault happened — you have no idea what happened.

We need a mechanism that combines the "cannot be ignored" safety of exceptions with the "zero runtime overhead" efficiency of error codes. C++23's `std::expected` is the answer.

---

## UartError: Type-Safe Error Codes

Let's start with our error type definition:

```cpp
enum class UartError {
    Timeout,
    NotInitialized,
    HardwareFault,
    Busy,
};
```

Four error values, each corresponding to a real failure scenario in UART operations:

- **Timeout**: The operation did not complete within the specified time. For example, the timeout parameter of `HAL_UART_Transmit` expired.
- **NotInitialized**: Send or receive was called before the driver was initialized. The current code doesn't explicitly check this state, but the error type reserves this value for future use.
- **HardwareFault**: A low-level hardware failure — a USART peripheral anomaly, a DMA transfer error, and so on.
- **Busy**: The peripheral is busy. For example, calling `send_it` while an interrupt-based transmission is already in progress.

Why use `enum class` instead of a plain enum or `int`? Because we already experienced this in the LED tutorial — `enum class` doesn't implicitly convert to `int`. You can't use a `UartError` as an `int`, and you can't pass an `int` where a `UartError` is expected. The type system enforces this for you.

---

## Basic Usage of std::expected

`std::expected<T, E>` is a "value or error" container. It either holds a success value of type `T`, or an error value of type `E`. You can think of it as a "safer optional" — `std::optional` only tells you "whether there is a value," while `std::expected` tells you "there is a value, or there isn't and the reason is E."

In our code, the return type of the `send` method is:

```cpp
std::expected<size_t, UartError> send(const uint8_t* data, size_t length, uint32_t timeout);
```

On success, it returns the number of bytes sent (`size_t`); on failure, it returns the specific `UartError`.

How the caller uses it:

```cpp
auto result = uart.send(data, size, 100);
if (result) {
    // Success: result.value() is the byte count
} else {
    // Failure: result.error() is the UartError
}
```

Key point: **you cannot use the return value directly without checking it**. `result` is not `size_t`; it is `std::expected<size_t, UartError>`. You must first check whether `result` has a value (via `operator bool` or `has_value()`), and only then can you access the success value through `value()` or `operator*`. If you forget to check and directly call `value()`, it triggers undefined behavior on error (typically a hard fault in a bare-metal environment).

Compare this with C-style error codes. `HAL_UART_Transmit` returns `HAL_StatusTypeDef`. You can completely ignore the return value without the compiler issuing a warning. `std::expected` uses the type system to make it "hard to forget checking" — although you still *can* skip the check, the code's intent is much clearer, and the compiler can work with the `[[nodiscard]]` attribute to emit a warning when the result is unchecked.

---

## Mapping HAL_StatusTypeDef to UartError

Inside the `send` method, we map the HAL return value to our `UartError` domain:

```cpp
std::expected<size_t, UartError> Uart::send(const uint8_t* data, size_t length, uint32_t timeout) {
    HAL_StatusTypeDef status = HAL_UART_Transmit(&huart, const_cast<uint8_t*>(data), length, timeout);
    if (status != HAL_OK) {
        switch (status) {
            case HAL_TIMEOUT: return std::unexpected(UartError::Timeout);
            case HAL_BUSY:    return std::unexpected(UartError::Busy);
            default:          return std::unexpected(UartError::HardwareFault);
        }
    }
    return length;
}
```

`std::unexpected(E)` constructs an `std::expected` object that "contains an error value." This is syntactically symmetric with returning a success value directly (`return length;`) — return the value on success, return `std::unexpected` on failure.

The blocking receive `receive` has exactly the same structure:

```cpp
std::expected<size_t, UartError> Uart::receive(uint8_t* buffer, size_t length, uint32_t timeout) {
    HAL_StatusTypeDef status = HAL_UART_Receive(&huart, buffer, length, timeout);
    if (status != HAL_OK) {
        switch (status) {
            case HAL_TIMEOUT: return std::unexpected(UartError::Timeout);
            case HAL_BUSY:    return std::unexpected(UartError::Busy);
            default:          return std::unexpected(UartError::HardwareFault);
        }
    }
    return length;
}
```

The return types for interrupt-based send and receive are slightly different — there's no data to return on success (it merely "started the interrupt operation"), so they return `std::expected<void, UartError>`. The error mapping also includes the `HAL_BUSY` case:

```cpp
std::expected<void, UartError> Uart::send_it(const uint8_t* data, size_t length) {
    HAL_StatusTypeDef status = HAL_UART_Transmit_IT(&huart, const_cast<uint8_t*>(data), length);
    if (status == HAL_OK) {
        return {};
    }
    switch (status) {
        case HAL_BUSY: return std::unexpected(UartError::Busy);
        default:       return std::unexpected(UartError::HardwareFault);
    }
}
```

`return {}` constructs an `std::expected` that is "successful but valueless." `HAL_BUSY` indicates the peripheral is busy (already sending or receiving), which maps to `UartError::Busy`.

---

## Runtime Cost of std::expected

The memory layout of `std::expected<T, E>` is essentially a tagged union — a discriminant flag (success/failure) plus storage space for either the success value or the error value. `sizeof(std::expected<size_t, UartError>)` typically equals `sizeof(size_t) + 1`, roughly 8 to 12 bytes.

Runtime overhead: constructing and checking `std::expected` takes only a few CPU instructions — a conditional branch to determine success or failure, and a value read. There is practically no difference compared to manually writing `if (status != HAL_OK)`. This is why it suits embedded systems — the type safety comes with almost no runtime cost.

---

## Relationship with std::variant

If you read the `std::variant` event system in the button tutorial, you might think `std::expected` and `std::variant` look somewhat similar. Indeed, the underlying implementation of `std::expected` is very similar to `std::variant` — both are type-safe unions. The difference lies in semantics: `std::expected` explicitly distinguishes between "success" and "failure," whereas `std::variant` is just "one of several types." `std::expected` provides interfaces specifically geared toward error handling, such as `value()`, `error()`, and `operator bool()`, making it more intuitive than the generic `std::variant`.

---

## Summary

This part introduced C++23's `std::expected` as a solution for embedded error handling. It bridges the gap between exceptions (too heavy) and error codes (ignorable) — it forces the caller to handle errors through the type system while maintaining zero runtime overhead. Our `UartError` enum defines four error types, and the four methods `send`, `receive`, `send_it`, and `receive_it` return either a success value or an error value via `std::expected`.

In the next part, we'll zoom out from individual methods to the entire driver class — exploring how the `Uart` template achieves zero-size abstraction and compile-time dispatch.
