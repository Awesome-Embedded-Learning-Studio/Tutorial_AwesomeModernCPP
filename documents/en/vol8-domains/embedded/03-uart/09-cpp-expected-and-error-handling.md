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
title: 'Part 39: std::expected Error Handling — A Better Choice Than Exceptions for
  Embedded Systems'
description: ''
translation:
  source: documents/vol8-domains/embedded/03-uart/09-cpp-expected-and-error-handling.md
  source_hash: 72a482072bb8f7922e1bd06dc8e3f305aa2ad4bf38cbec87ba426c8350042091
  translated_at: '2026-06-16T04:12:14.729844+00:00'
  engine: anthropic
  token_count: 1384
---
# Part 39: Error Handling with std::expected — A Better Choice Than Exceptions for Embedded Systems

> Stage five begins with error handling. Embedded projects disable exceptions, and bare error codes are easily ignored. C++23's `std::expected` fills this gap perfectly.

---

## The Embedded Error Handling Trilemma

In any programming scenario, error handling must address a fundamental question: a function can succeed or fail, so how does the caller know the result?

In PC-based C++, the standard answer is exceptions. A function throws an exception, and the caller catches it with `try`/`catch`. Exceptions cannot be silently ignored—an uncaught exception terminates the program. However, exceptions have a runtime cost (stack unwinding, RTTI information, exception tables), and our `CMakeLists.txt` explicitly disables them via `-fno-exceptions`. On resource-constrained STM32s, the overhead of exceptions is unacceptable.

The C language approach is to return error codes. `HAL_UART_Transmit` returns `HAL_StatusTypeDef`—`HAL_OK`, `HAL_ERROR`, `HAL_BUSY`, or `HAL_TIMEOUT`. This is lightweight, but it has a fatal flaw: **error codes can be silently ignored**. If you write `HAL_UART_Transmit(...)` without checking the return value, the compiler won't complain, and the code compiles successfully. It's only when something goes wrong at runtime—data wasn't sent, a timeout occurred, a hardware fault happened—that you realize you have no idea what went wrong.

We need a mechanism that offers the "cannot be ignored" safety of exceptions, combined with the "zero runtime overhead" efficiency of error codes. C++23's `std::expected` is the answer.

---

## UartError: Type-Safe Error Codes

First, let's look at our error type definition:

```cpp
enum class UartError {
    Timeout,
    NotInitialized,
    HardwareFault,
    Busy
};
```

Four error values, each corresponding to a real-world failure scenario in UART operations:

- **Timeout**: The operation did not complete within the specified time. For example, the timeout parameter of `HAL_UART_Transmit` expired.
- **NotInitialized**: Send/Receive was called before the driver was initialized. Currently, the code doesn't explicitly check this state, but the error type reserves this value for future use.
- **HardwareFault**: A low-level hardware failure—such as a USART peripheral anomaly or a DMA transfer error.
- **Busy**: The peripheral is busy. For example, `write` is called while an interrupt transmission is already in progress.

Why use `enum class` instead of a plain `enum` or `int`? As we saw in the LED tutorial—`enum class` does not implicitly convert to `int`. You cannot use an `LedState` where an `int` is expected, nor can you use a `UartError` where an `int` is expected. The type system guards you against mistakes.

---

## Basic Usage of std::expected

`std::expected<T, E>` is a "value-or-error" container. It either holds a success value `T` or an error value `E`. You can think of it as a "safer `std::optional`"—`std::optional` only tells you "is there a value?", while `std::expected` tells you "there is a value, or there is no value because of error E".

In our code, the return type of the `write` method is:

```cpp
std::expected<size_t, UartError>
```

On success, it returns the number of bytes sent (`size_t`); on failure, it returns the specific `UartError`.

How the caller uses it:

```cpp
auto result = uart.write(buffer, size);
if (!result) {
    // Handle error: result.error() gives the UartError
    return result.error();
}
// Handle success: result.value() gives the size_t
bytes_sent = result.value();
```

Key point: **You cannot use the return value directly without checking it.** `result` is not `size_t`; it is `std::expected<size_t, UartError>`. You must first check if `result` has a value (via `operator bool()` or `has_value()`) before you can access the success value via `operator*` or `value()`. If you forget to check and call `value()` directly on an error, it triggers undefined behavior (usually a hard fault in a bare-metal environment).

Compare this to C-style error codes. `HAL_UART_Transmit` returns `HAL_StatusTypeDef`. You can completely ignore the return value, and the compiler won't warn you. `std::expected` uses the type system to make it "hard to forget to check"—while you *can* still ignore it, the intent is clearer, and the compiler can cooperate with the `[[nodiscard]]` attribute to warn if it is unchecked.

---

## Mapping HAL_StatusTypeDef to UartError

Inside the `write` method, we map the HAL return value to our `UartError` domain:

```cpp
auto hal_status = HAL_UART_Transmit(&huart, data, size, timeout);
if (hal_status == HAL_OK) {
    return size;
} else if (hal_status == HAL_TIMEOUT) {
    return std::unexpected(UartError::Timeout);
} else if (hal_status == HAL_BUSY) {
    return std::unexpected(UartError::Busy);
} else {
    return std::unexpected(UartError::HardwareFault);
}
```

`std::unexpected` constructs an `std::expected` object "containing an error value". This syntax is symmetric with directly returning a success value (`return size`)—success returns the value, failure returns `std::unexpected`.

The structure for blocking receive `read` is identical:

```cpp
auto hal_status = HAL_UART_Receive(&huart, data, size, timeout);
if (hal_status == HAL_OK) {
    return size;
} else if (hal_status == HAL_TIMEOUT) {
    return std::unexpected(UartError::Timeout);
} else if (hal_status == HAL_BUSY) {
    return std::unexpected(UartError::Busy);
} else {
    return std::unexpected(UartError::HardwareFault);
}
```

The return types for interrupt-based send and receive are slightly different—since there is no data to return on success (just "interrupt operation started"), they return `std::expected<void, UartError>`. The error mapping also includes the `HAL_BUSY` case:

```cpp
auto hal_status = HAL_UART_Transmit_IT(&huart, data, size);
if (hal_status == HAL_OK) {
    return {}; // Success with void
} else if (hal_status == HAL_BUSY) {
    return std::unexpected(UartError::Busy);
} else {
    return std::unexpected(UartError::HardwareFault);
}
```

`return {}` constructs an `std::expected` that is "successful but valueless". `HAL_BUSY` indicates the peripheral is busy (already transmitting or receiving), which maps to `UartError::Busy`.

---

## Runtime Cost of std::expected

The memory layout of `std::expected` is essentially a tagged union—a discriminant flag (success/failure) plus storage space for either the success value or the error value. `sizeof(std::expected<T, E>)` is typically `sizeof(T) + sizeof(E)`, approximately 8-12 bytes.

Runtime overhead: constructing and checking `std::expected` takes just a few CPU instructions—a conditional branch to judge success/failure and a value read. There is virtually no difference compared to manually writing error code checks. This is why it fits embedded systems perfectly—type safety brings almost no runtime cost.

---

## Relationship with std::variant

If you've read the `Button` event system in the button tutorial, you might think `std::expected` and `std::variant` are somewhat similar. Indeed, the underlying implementation of `std::expected` is very similar to `std::variant`—both are type-safe unions. The difference lies in semantics: `std::expected` explicitly distinguishes "success" from "failure", whereas `std::variant` is just "one of many types". `std::expected` provides interfaces like `error()`, `value()`, and `operator->` specifically for error handling, making it more intuitive than the generic `std::variant`.

---

## Summary

This part introduced C++23's `std::expected` as a solution for embedded error handling. It bridges the gap between exceptions (too heavy) and error codes (ignorable)—forcing the caller to handle errors through the type system while maintaining zero runtime overhead. Our `UartError` enum defines four error types, and the `write`/`read`/`write_async`/`read_async` methods return success values or errors via `std::expected`.

In the next part, we will zoom out from individual methods to the entire driver class—exploring how the `Uart` template implements zero-overhead abstraction and compile-time polymorphism.
