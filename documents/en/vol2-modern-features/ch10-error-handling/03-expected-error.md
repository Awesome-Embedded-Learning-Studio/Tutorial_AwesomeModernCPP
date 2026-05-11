---
title: 'std::expected<T, E>: Type-safe error propagation'
description: C++23's expected type and monadic operations, implementing elegant error
  propagation chains
chapter: 10
order: 3
tags:
- host
- cpp-modern
- intermediate
- expected
- 类型安全
difficulty: intermediate
platform: host
cpp_standard:
- 23
reading_time_minutes: 18
prerequisites:
- 'Chapter 10: 错误处理的演进'
- 'Chapter 10: optional 用于错误处理'
related:
- 错误处理模式总结
---
# std::expected<T, E>: Type-Safe Error Propagation

In the previous article, we discussed `std::optional` for error handling and pointed out its limitation—it cannot carry error information. When you need to know *why* something failed, `std::optional` falls short. `std::expected`, introduced in C++23, fills this gap: it tells you both whether a value exists and why it doesn't.

If you have experience with Rust, the design philosophy behind `std::expected` is identical to Rust's `Result`—it holds a value `T` on success and an error `E` on failure. The difference is that C++ lacks compiler-enforced `match` checks and the `?` operator, so we rely on monadic operations and coding discipline to bridge the gap.

A quick note: `std::expected` is a C++23 feature. If you are currently using C++17 or C++20, we provide a usable simplified implementation later in this article. In embedded scenarios, since there is no RTTI dependency, `std::expected` works perfectly fine.

------

## Core Semantics of expected

`std::expected` is a template class that holds either a success value of type `T` or an error object of type `E`. Its interface design borrows from `std::optional`—you can use `has_value()` or the boolean conversion operator to check for success, `value()` to get the value, and `error()` to get the error:

```cpp
#include <expected>
#include <string>
#include <iostream>

enum class ParseError {
    kEmptyInput,
    kInvalidCharacter,
    kOutOfRange,
};

std::expected<int, ParseError> parse_int(const std::string& s) {
    if (s.empty()) {
        return std::unexpected(ParseError::kEmptyInput);
    }

    try {
        std::size_t pos = 0;
        int value = std::stoi(s, &pos);
        if (pos != s.size()) {
            return std::unexpected(ParseError::kInvalidCharacter);
        }
        return value;
    } catch (...) {
        return std::unexpected(ParseError::kOutOfRange);
    }
}

int main() {
    auto r1 = parse_int("42");
    if (r1) {
        std::cout << "Value: " << r1.value() << "\n";  // 42
    }

    auto r2 = parse_int("42abc");
    if (!r2) {
        std::cout << "Error: " << static_cast<int>(r2.error()) << "\n";
        // 输出 Error: 1（kInvalidCharacter）
    }
}
```

`std::unexpected` is a helper template used specifically to construct the error branch of `std::expected`. Its role is similar to `std::nullopt` for `std::optional`—it explicitly expresses "this is an error."

------

## Construction and Access

`std::expected` offers several ways to be constructed. The most basic approach: construct directly with a value to indicate success, or use `std::unexpected` to indicate failure:

```cpp
// 成功值构造
std::expected<int, std::string> success = 42;

// 错误构造
std::expected<int, std::string> failure =
    std::unexpected("something went wrong");

// 就地构造
std::expected<std::string, int> in_place_success{
    std::in_place, "hello"};
```

For access, `std::expected` provides an interface similar to `std::optional`, but adds a crucial member—`operator*`:

```cpp
std::expected<int, std::string> result = 42;

// 检查
result.has_value();      // true
static_cast<bool>(result);  // true

// 访问值
result.value();          // 42，如果为空则抛出 std::bad_expected_access
*result;                 // 42，未定义行为检查（类似 optional 的 operator*）
result->some_member;     // 如果 T 是结构体

// 访问错误（仅在 !has_value() 时调用）
std::expected<int, std::string> err =
    std::unexpected("oops");
err.error();             // "oops"

// 安全默认值
result.value_or(0);      // 如果有值返回值，否则返回 0
```

The difference between `value()` and `operator*` is that the former throws a `std::bad_expected_access` exception when `std::expected` is in an error state, while the latter results in undefined behavior. Therefore, use `operator*` on paths where you are certain a value exists, and use `value()` or check `has_value()` first on paths where you are less certain.

------

## Monadic Operations

This is the most powerful part of `std::expected`. C++23's `std::expected` natively supports four monadic operations, allowing you to chain multiple potentially failing operations without deeply nesting `if/else` blocks.

### and_then: Chaining Potentially Failing Operations

`and_then` takes a function `f`, which accepts the value inside `std::expected` and returns a new `std::expected`. If the current `std::expected` is in an error state, `f` is not called, and the error passes straight through to the end of the chain:

```cpp
#include <expected>
#include <string>
#include <iostream>

std::expected<int, std::string> validate_positive(int value) {
    if (value > 0) return value;
    return std::unexpected("Value must be positive");
}

std::expected<double, std::string> safe_divide(int num, int denom) {
    if (denom == 0) {
        return std::unexpected("Division by zero");
    }
    return static_cast<double>(num) / denom;
}

int main() {
    std::string input = "42";

    auto result = parse_int(input)
        .and_then(validate_positive)
        .and_then([](int v) {
            return safe_divide(v, 2);
        });

    if (result) {
        std::cout << "Result: " << *result << "\n";  // 21.0
    } else {
        std::cout << "Error: " << result.error() << "\n";
    }
}
```

If `parse_int` returns an error, the subsequent `validate_range` and `to_hex_string` will not execute, and the error appears directly in `result`. This is what we mean by "automatic error pass-through."

### transform: Transforming the Value

The difference between `transform` and `and_then` is that the provided function returns a plain value instead of an `std::expected`. `transform` automatically wraps the return value in a new `std::expected`:

```cpp
auto result = parse_int("42")
    .transform([](int v) { return v * 2; })
    .transform([](int v) { return std::to_string(v); });
// result 的类型是 std::expected<std::string, ParseError>
```

Here, the first `transform` turns an `int` into an `int` (doubling it), and the second turns an `int` into a `std::string`. If any step fails, subsequent `transform` calls will not execute.

`transform` is suited for operations that "cannot fail themselves." If an operation might fail, use `and_then`; if it is guaranteed to succeed, use `transform`.

### or_else: Handling Errors

`or_else` calls the provided function when `std::expected` is in an error state. It is typically used for error recovery, logging, or error enrichment:

```cpp
std::expected<int, std::string> try_cache(int key) {
    return std::unexpected("cache miss for " + std::to_string(key));
}

std::expected<int, std::string> try_database(int key) {
    return key * 100;  // 模拟从数据库获取
}

int main() {
    auto result = try_cache(42)
        .or_else([](const std::string& err) {
            std::cerr << "Cache failed: " << err << ", trying DB\n";
            return try_database(42);
        });

    // result 持有 4200
}
```

The function inside `or_else` must return the same type of `std::expected`. This means you can perform error recovery within `or_else`—if the fallback operation succeeds, the subsequent parts of the chain will continue down the success path.

### transform_error: Transforming the Error Type

`transform_error` allows you to transform the error object as it passes through, without affecting the success path. This is extremely useful for cross-layer error propagation—the lower layer might use one error type, while the upper layer requires another:

```cpp
struct AppError {
    int code;
    std::string message;
    std::string context;  // 额外的上下文信息
};

auto result = parse_int("abc")
    .transform_error([](ParseError e) -> AppError {
        return AppError{static_cast<int>(e),
                        "Parse error",
                        "in config file line 1"};
    });
// result 的类型是 std::expected<int, AppError>
```

### Complete Chaining Example

Combining all four operations gives us a complete error-handling pipeline:

```cpp
#include <expected>
#include <string>
#include <iostream>
#include <charconv>
#include <system_error>

enum class ConfigError {
    kFileNotFound,
    kParseError,
    kValidationError,
};

struct ServerConfig {
    std::string host;
    int port;
};

std::expected<std::string, ConfigError> read_file(
    const std::string& path) {
    // 简化：假设总是成功
    return "host=192.168.1.1\nport=8080\n";
}

std::expected<ServerConfig, ConfigError> parse_config(
    const std::string& content) {
    ServerConfig cfg;
    cfg.host = "localhost";
    cfg.port = 8080;
    // 简化：实际解析内容
    return cfg;
}

std::expected<ServerConfig, ConfigError> validate_config(
    ServerConfig cfg) {
    if (cfg.port < 1 || cfg.port > 65535) {
        return std::unexpected(ConfigError::kValidationError);
    }
    return cfg;
}

int main() {
    auto result = read_file("server.cfg")
        .and_then(parse_config)
        .and_then(validate_config)
        .transform([](const ServerConfig& cfg) -> std::string {
            return cfg.host + ":" + std::to_string(cfg.port);
        })
        .transform_error([](ConfigError e) -> std::string {
            switch (e) {
                case ConfigError::kFileNotFound:
                    return "Config file not found";
                case ConfigError::kParseError:
                    return "Config parse error";
                case ConfigError::kValidationError:
                    return "Config validation failed";
            }
            return "Unknown error";
        });

    if (result) {
        std::cout << "Server: " << *result << "\n";
    } else {
        std::cerr << "Failed: " << result.error() << "\n";
    }
}
```

This chain reads very clearly: read file -> parse config -> validate config -> convert to connection string. If any step fails, subsequent steps are automatically skipped, and the error information is handled uniformly at the end of the chain.

------

## expected vs. Exceptions vs. optional

We have put together a comparison table to help you make choices in real-world scenarios:

| Scenario | Recommended Approach | Reason |
|------|---------|------|
| Lookup/cache, failure has no specific reason | `std::optional` | Concise, no error information needed |
| Parsing/IO, need to know the reason for failure | `std::expected` | Carries error information |
| Multi-step operation chains, need error propagation | `std::expected` | Monadic operations support chaining |
| Unrecoverable critical errors | Exceptions | Forced interruption, automatic RAII cleanup |
| Constructor failure | Exceptions | Constructors have no return value |
| Embedded (no exception support) | `std::expected` or enums | No RTTI dependency |

A practical rule of thumb: **if the caller needs to do different things based on the error type (retry, degrade, report), use `std::expected`; if you only need to know "success or failure," use `std::optional`; if it is a severe program-logic error (impossible to recover from), use exceptions.**

------

## Simplified Implementation for C++17 Environments

If your project is still on C++17, don't worry—you can implement a fully functional, simplified version of `std::expected`. The following implementation covers the core features and can be dropped directly into your project:

```cpp
#include <utility>
#include <type_traits>
#include <stdexcept>

/// 辅助类型：用于构造错误分支
template <typename E>
struct unexpected {
    E value;
    constexpr explicit unexpected(E v) : value(std::move(v)) {}
};

/// 简化版 expected<T, E>
template <typename T, typename E>
class expected {
    bool has_value_;
    union {
        T val_;
        E err_;
    } storage_;

public:
    // 成功值构造
    expected(const T& v) : has_value_(true) {
        new(&storage_.val_) T(v);
    }

    expected(T&& v) : has_value_(true) {
        new(&storage_.val_) T(std::move(v));
    }

    // 错误构造
    expected(unexpected<E> u) : has_value_(false) {
        new(&storage_.err_) E(std::move(u.value));
    }

    // 析构
    ~expected() {
        if (has_value_) storage_.val_.~T();
        else storage_.err_.~E();
    }

    constexpr bool has_value() const noexcept { return has_value_; }
    constexpr explicit operator bool() const noexcept {
        return has_value_;
    }

    T& value() {
        if (!has_value_)
            throw std::runtime_error("bad expected access");
        return storage_.val_;
    }

    const T& value() const {
        if (!has_value_)
            throw std::runtime_error("bad expected access");
        return storage_.val_;
    }

    const E& error() const {
        if (has_value_)
            throw std::runtime_error("no error present");
        return storage_.err_;
    }

    T& operator*() { return storage_.val_; }
    T* operator->() { return &storage_.val_; }

    T value_or(T default_val) const {
        return has_value_ ? storage_.val_ : default_val;
    }

    /// and_then：链接返回 expected 的操作
    template <typename F>
    auto and_then(F&& f) -> decltype(f(std::declval<T>())) {
        using ResultType = decltype(f(std::declval<T>()));
        if (has_value_) return f(storage_.val_);
        return ResultType(unexpected<E>{storage_.err_});
    }

    /// transform：对值做变换
    template <typename F>
    auto transform(F&& f)
        -> expected<decltype(f(std::declval<T>())), E> {
        using U = decltype(f(std::declval<T>()));
        if (has_value_)
            return expected<U, E>(f(storage_.val_));
        return expected<U, E>(unexpected<E>{storage_.err_});
    }

    /// or_else：处理错误
    template <typename F>
    expected or_else(F&& f) {
        if (has_value_) return *this;
        return f(storage_.err_);
    }
};
```

This implementation omits some details (fine-grained control of copy/move semantics, `std::unexpect_t` support, etc.), but the core semantics are completely correct and suitable for production-environment error handling.

------

## Practical Example: Multi-Layer Parsing Chain

Let's look at an example closer to real-world development—parsing a network address from a string, which involves multiple steps of validation and conversion:

```cpp
#include <string>
#include <string_view>
#include <expected>
#include <iostream>
#include <charconv>

struct AddressError {
    enum Code {
        kEmptyInput,
        kMissingPort,
        kInvalidHost,
        kInvalidPort,
        kPortOutOfRange,
    } code;
    std::string detail;
};

struct NetworkAddress {
    std::string host;
    int port;
};

std::expected<std::string, AddressError> validate_input(
    std::string_view input) {
    if (input.empty()) {
        return std::unexpected(AddressError{
            AddressError::kEmptyInput, "Input is empty"});
    }
    return std::string(input);
}

std::expected<NetworkAddress, AddressError> split_address(
    std::string input) {
    auto colon = input.rfind(':');
    if (colon == std::string::npos) {
        return std::unexpected(AddressError{
            AddressError::kMissingPort,
            "No port specified: " + input});
    }

    NetworkAddress addr;
    addr.host = input.substr(0, colon);
    if (addr.host.empty()) {
        return std::unexpected(AddressError{
            AddressError::kInvalidHost, "Host is empty"});
    }

    auto port_str = input.substr(colon + 1);
    int port = 0;
    auto [ptr, ec] = std::from_chars(
        port_str.data(), port_str.data() + port_str.size(), port);
    if (ec != std::errc{} || ptr != port_str.data() + port_str.size()) {
        return std::unexpected(AddressError{
            AddressError::kInvalidPort,
            "Port is not a number: " + std::string(port_str)});
    }
    if (port < 1 || port > 65535) {
        return std::unexpected(AddressError{
            AddressError::kPortOutOfRange,
            "Port out of range: " + std::to_string(port)});
    }
    addr.port = port;
    return addr;
}

int main() {
    auto result = validate_input("192.168.1.1:8080")
        .and_then(split_address)
        .transform([](const NetworkAddress& a) -> std::string {
            return a.host + ":" + std::to_string(a.port);
        })
        .or_else([](const AddressError& e) -> std::expected<std::string, AddressError> {
            std::cerr << "Error: " << e.detail << "\n";
            return std::unexpected(e);
        });

    if (result) {
        std::cout << "Address: " << *result << "\n";
    }
}
```

This example demonstrates the advantage of `std::expected` in multi-layer operations: each step returns an `std::expected`, and if any step fails, it automatically passes through, ultimately handled uniformly at the end of the chain. The error information carries sufficient context—the `message` field tells you exactly what went wrong.

------

## Summary

`std::expected` is C++23's core tool for type-safe error handling. It provides more information than `std::optional`, is better suited for performance-sensitive and embedded scenarios than exceptions, and its monadic operations make error propagation chains elegant. If you are still on C++17, a simplified `std::expected` implementation can cover most of your needs.

In the next article, we will comprehensively compare all error-handling approaches and provide a scenario-based selection guide.

## Reference Resources

- [cppreference: std::expected](https://en.cppreference.com/w/cpp/utility/expected)
- [P2505R5 - Monadic Functions for std::expected](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2022/p2505r1.html)
- [C++ Stories: std::expected monadic extensions](https://www.cppstories.com/2024/expected-cpp23-monadic/)
