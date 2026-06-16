---
chapter: 10
cpp_standard:
- 23
description: The C++23 `expected` type and monadic operations, implementing elegant
  error propagation chains
difficulty: intermediate
order: 3
platform: host
prerequisites:
- 'Chapter 10: 错误处理的演进'
- 'Chapter 10: optional 用于错误处理'
reading_time_minutes: 11
related:
- 错误处理模式总结
tags:
- host
- cpp-modern
- intermediate
- expected
- 类型安全
title: 'std::expected<T, E>: Type-Safe Error Propagation'
translation:
  source: documents/vol2-modern-features/ch10-error-handling/03-expected-error.md
  source_hash: 31bfe8489ee19196b3a58f3e8405c538be69c40c97f6ba7c801dffe390b724fe
  translated_at: '2026-06-16T03:59:29.226676+00:00'
  engine: anthropic
  token_count: 3394
---
# std::expected<T, E>: Type-Safe Error Propagation

In the previous post, we discussed the application of `std::optional` in error handling and pointed out its limitation—it cannot carry error information. When you need to know "why it failed," `std::optional` falls short. The `std::expected` introduced in C++23 fills this gap: it tells you both "whether there is a value" and "the reason why there isn't."

If you have used Rust, the design philosophy of `std::expected` is identical to Rust's `Result`—holding a value `T` on success and an error `E` on failure. The difference is that C++ lacks compiler-enforced `panic` checks and the `?` operator, so we rely on monadic operations and coding discipline to bridge the gap.

First, a note: `std::expected` is a C++23 feature. If you are currently using C++17 or C++20, this article provides a usable simplified implementation; in embedded scenarios, since there is no dependency on RTTI, `std::expected` works perfectly fine.

------

## Core Semantics of expected

`std::expected` is a template class that either holds a success value of type `T` or an error object of type `E`. Its interface design borrows from `std::optional`—you can use `has_value()` or operator `bool` to check for success, use `value()` to get the value, and `error()` to get the error:

```cpp
std::expected<int, std::string> parse_int(std::string_view str) {
    if (str.empty()) return std::unexpected("empty string"); // Error
    // ... parsing logic ...
    return 42; // Success
}
```

`std::unexpected` is a helper template specifically used to construct the error branch of `std::expected`. Its role is similar to `std::nullopt`之于 `std::optional`—it explicitly expresses "this is an error."

------

## Construction and Access

`std::expected` offers rich construction methods. The most basic ones: construct directly with a value to indicate success, or use `std::unexpected` to indicate failure:

```cpp
std::expected<int, ErrorCode> result1 = 42;             // Success
std::expected<int, ErrorCode> result2 = std::unexpected(ErrorCode::InvalidInput); // Failure
```

Regarding access, `std::expected` provides an interface similar to `std::optional`, but adds a key member—`error()`:

```cpp
if (result.has_value()) {
    int val = result.value(); // Safe access
} else {
    ErrorCode err = result.error(); // Get error
}

// Or use the dereference operator (throws on error)
int val = *result;
```

The difference between `value()` and `operator*` is: the former throws a `std::bad_expected_access` exception when `std::expected` is in an error state, while the latter results in undefined behavior. So, use `operator*` on paths where "you are sure there is a value," and use `value()` or check `has_value()` first on paths where "you are not sure."

------

## Monadic Operations

This is the most powerful part of `std::expected`. C++23's `std::expected` natively supports four monadic operations, allowing you to organize multiple potentially failing operations using chained calls without nesting `if` statements layer by layer.

### and_then: Chaining Potentially Failing Operations

`and_then` accepts a function `f`, where `f` accepts the value inside `std::expected` and returns a new `std::expected`. If the current `std::expected` is in an error state, `f` will not be called, and the error propagates directly to the end of the chain:

```cpp
// Read file -> Parse config -> Validate config
auto result = read_file("config.json")
    .and_then(parse_json)      // If read fails, parse_json is skipped
    .and_then(validate_config); // If parse fails, validate is skipped
```

If `read_file` returns an error, subsequent `parse_json` and `validate_config` will not execute, and the error appears directly in `result`. This is the meaning of "automatic error propagation."

### transform: Transforming the Value

The difference between `transform` and `and_then` is that the passed function returns a normal value instead of an `std::expected`. `transform` automatically wraps the return value into a new `std::expected`:

```cpp
std::expected<int, Error> get_value();
auto result = get_value()
    .transform([](int v) { return v * 2; })  // int -> int
    .transform([](int v) { return std::to_string(v); }); // int -> string
```

Here, the first `transform` turns `int` into `int` (doubling), and the second turns `int` into `string`. If any step in the middle fails, subsequent `transform`s will not execute.

`transform` is suitable for transformation operations that "cannot fail themselves." If an operation might fail, use `and_then`; if it is guaranteed to succeed, use `transform`.

### or_else: Handling Errors

`or_else` calls the passed function when `std::expected` is in an error state, usually used for error recovery, logging, or error enrichment:

```cpp
auto result = risky_operation()
    .or_else([](Error err) {
        log_error(err);
        return try_backup_operation(); // Must return std::expected
    });
```

The function in `or_else` must return the same type of `std::expected`. This means you can perform error recovery inside `or_else`—if the alternative operation succeeds, the subsequent part of the chain will continue executing the success path.

### transform_error: Transforming Error Types

`transform_error` allows you to transform the error object during error propagation without affecting the success path. This is very useful when propagating errors across layers—the lower layer might use one error type, while the upper layer needs another:

```cpp
auto result = low_level_io()
    .transform_error([](IoError err) {
        return AppError::IoFailed; // Convert IoError to AppError
    });
```

### Complete Chaining Example

Combining the four operations creates a complete error handling pipeline:

```cpp
auto conn_str = read_file("config.txt")
    .and_then(parse_config)
    .and_then(validate_config)
    .transform(to_connection_string)
    .or_else([](auto err) {
        log_error(err);
        return std::unexpected("config init failed");
    });
```

This chain reads very clearly: read file -> parse config -> validate config -> convert to connection string. If any step fails, subsequent steps are automatically skipped, and the error information is handled uniformly at the end of the chain.

------

## expected vs Exceptions vs optional

I have compiled a comparison table to help you make choices in actual scenarios:

| Scenario | Recommended Approach | Reason |
|------|---------|------|
| Lookup/Cache, failure without reason | `std::optional` | Concise, no error info needed |
| Parsing/IO, need to know failure reason | `std::expected` | Carries error information |
| Multi-step operation chain, need error propagation | `std::expected` | Monadic operations support chaining |
| Unrecoverable critical errors | Exceptions | Forced interruption, RAII automatic cleanup |
| Constructor failure | Exceptions | Constructors have no return value |
| Embedded (no exception support) | `std::expected` or enum | No RTTI dependency |

A practical judgment method is: **If the caller needs to do different things based on the error type (retry, degrade, report), use `std::expected`; if you only need to know "success or failure," use `std::optional`; if it is a serious program logic error (impossible to recover), use exceptions.**

------

## Simplified Implementation for C++17 Environments

If your project is still on C++17, don't worry, you can implement a functionally complete simplified version of `std::expected`. The implementation below covers core functionality and can be used directly in your project:

```cpp
#include <variant>
#include <optional>

template<typename T, typename E>
class [[nodiscard]] expected {
    std::variant<T, E> v_;

public:
    // Construct from value (success)
    expected(T&& val) : v_(std::move(val)) {}

    // Construct from error (failure)
    expected(E&& err) : v_(std::move(err)) {}

    // Check if it holds a value
    bool has_value() const { return std::holds_alternative<T>(v_); }
    explicit operator bool() const { return has_value(); }

    // Get value (undefined behavior if error)
    const T& operator*() const { return std::get<T>(v_); }
    T& operator*() { return std::get<T>(v_); }

    // Get error (undefined behavior if value)
    const E& error() const { return std::get<E>(v_); }
    E& error() { return std::get<E>(v_); }

    // Monadic operations (simplified)
    template<typename F>
    auto and_then(F&& f) -> decltype(f(std::declval<T>())) {
        if (has_value()) return f(std::get<T>(v_));
        return decltype(f(std::declval<T>()))(std::get<E>(v_));
    }

    template<typename F>
    auto transform(F&& f) -> expected<decltype(f(std::declval<T>())), E> {
        if (has_value()) return f(std::get<T>(v_));
        return std::get<E>(v_);
    }
};

template<typename E>
class unexpected {
    E val_;
public:
    unexpected(E&& val) : val_(std::move(val)) {}
    const E& error() const { return val_; }
};
```

This implementation omits some details (fine-grained control of copy/move semantics, `std::unexpected` support, etc.), but the core semantics are entirely correct and can be used for error handling in production environments.

------

## General Example: Multi-Layer Parsing Chain

Let's look at an example closer to actual development—parsing a network address from a string, involving multi-step validation and conversion:

```cpp
enum class AddrError { InvalidFormat, InvalidPort, UnknownProtocol };

using AddrResult = std::expected<SocketAddress, AddrError>;

AddrResult parse_address(std::string_view input) {
    // 1. Validate format
    if (input.empty() || input.find(':') == std::string_view::npos) {
        return std::unexpected(AddrError::InvalidFormat);
    }

    // 2. Split protocol and address
    auto [proto, addr] = split_proto_and_addr(input);

    // 3. Validate protocol
    if (proto != "tcp" && proto != "udp") {
        return std::unexpected(AddrError::UnknownProtocol);
    }

    // 4. Parse port
    auto port = parse_port(addr);
    if (!port.has_value()) {
        return std::unexpected(AddrError::InvalidPort);
    }

    return SocketAddress{proto, addr, *port};
}

// Usage
auto result = parse_address("tcp:192.168.1.1:8080")
    .and_then([](const SocketAddress& addr) {
        return bind_socket(addr); // Returns std::expected<Socket, AddrError>
    })
    .transform([](const Socket& sock) {
        return sock.get_handle(); // Returns int
    });
```

This example demonstrates the advantage of `std::expected` in multi-layer operations: each step returns `std::expected`, and any failure automatically propagates, ultimately handled uniformly at the end of the chain. The error information carries sufficient context—the `AddrError` field tells you specifically what went wrong.

------

## Summary

`std::expected` is C++23's core tool for type-safe error handling. It provides more error information than `std::optional`, is better suited for performance-sensitive and embedded scenarios than exceptions, and monadic operations make error propagation chains elegant. If you are still on C++17, a simplified `std::expected` implementation covers most needs.

In the next post, we will comprehensively compare all error handling schemes and provide a scenario-based selection guide.

## Reference Resources

- [cppreference: std::expected](https://en.cppreference.com/w/cpp/utility/expected)
- [P2505R5 - Monadic Functions for std::expected](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2022/p2505r1.html)
- [C++ Stories: std::expected monadic extensions](https://www.cppstories.com/2024/expected-cpp23-monadic/)
