---
chapter: 10
cpp_standard:
- 23
description: The C++23 expected type and its monadic operations for building elegant
  error-propagation chains
difficulty: intermediate
order: 3
platform: host
prerequisites:
- 'Chapter 10: Evolution of Error Handling: From Error Codes to Type Safety'
- 'Chapter 10: optional for Error Handling'
reading_time_minutes: 11
related:
- 'Error Handling Patterns: A Selection Guide and Best Practices'
tags:
- host
- cpp-modern
- intermediate
- expected
- 类型安全
title: 'std::expected<T, E>: Type-Safe Error Propagation'
translation:
  source: documents/vol2-modern-features/ch10-error-handling/03-expected-error.md
  source_hash: d3b459b84e7ad55481f2adfa556d4c9d03f5b4bab1fdd256b0d77a381ea882df
  translated_at: '2026-09-25T16:45:07+00:00'
  engine: anthropic
  token_count: 4100
---
# std::expected<T, E>: Type-Safe Error Propagation

In the previous article we discussed `std::optional` for error handling and pointed out its limitation — it cannot carry error information. When you need to know "why it failed", `optional` falls short. `std::expected<T, E>`, introduced in C++23, exists precisely to fill this gap: it tells you both "is there a value" and "what is the reason there is no value".

If you have touched Rust, `expected` will feel familiar — its design is cut from the same cloth as Rust's `Result<T, E>`: it holds a value `T` on success and an error `E` on failure. The difference is that C++ has neither the compiler-enforced `must_use` check nor the `?` operator, so we have to lean on monadic operations and coding discipline to compensate.

One caveat up front: `std::expected` is a C++23 feature. If you are currently on C++17 or C++20, this article provides a usable simplified implementation; and since `expected` has no RTTI dependency, it works just fine in embedded settings as well.

------

## The Core Semantics of expected

`std::expected<T, E>` is a template class that holds either a success value of type `T` or an error object of type `E`. Its interface design borrows from `optional` — you can check for success with `operator bool()` or `has_value()`, fetch the value with `value()`, and fetch the error with `error()`:

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
        // Prints Error: 1 (kInvalidCharacter)
    }
}
```

`std::unexpected(E)` is a helper template dedicated to constructing the error branch of an `expected`. It plays the same role for `expected` that `std::nullopt` plays for `optional` — an explicit way to say "this is an error".

How the value and the error are packed into one return type — and how they are taken back out — is captured in an animation. You can play it, pause it, or step through it frame by frame, walking the value channel and the error channel one at a time:

<Anim id="expected-channel" />

------

## Construction and Access

`expected` offers a fairly rich set of construction options. The most basic ones: construct directly from a value to represent success, and construct from `std::unexpected` to represent failure:

```cpp
// Construct from a success value
std::expected<int, std::string> success = 42;

// Construct from an error
std::expected<int, std::string> failure =
    std::unexpected("something went wrong");

// Construct in place
std::expected<std::string, int> in_place_success{
    std::in_place, "hello"};
```

For access, `expected` provides an interface similar to `optional`'s, but adds one crucial member — `error()`:

```cpp
std::expected<int, std::string> result = 42;

// Check
result.has_value();      // true
static_cast<bool>(result);  // true

// Access the value
result.value();          // 42; throws std::bad_expected_access if empty
*result;                 // 42; no check, undefined behavior if empty (like optional's operator*)
result->some_member;     // if T is a struct

// Access the error (only call when !has_value())
std::expected<int, std::string> err =
    std::unexpected("oops");
err.error();             // "oops"

// Safe default value
result.value_or(0);      // returns the value if present, otherwise 0
```

The difference between `value()` and `operator*` is this: the former throws the `std::bad_expected_access<E>` exception when the `expected` is in the error state, while the latter is undefined behavior. So use `*` on paths where you are certain a value is there, and use `value()` — or an explicit `has_value()` check first — on paths where you are not so sure.

------

## Monadic Operations

This is the most powerful part of `expected`. C++23's `expected` natively supports four monadic operations, letting you organize multiple fallible operations as chained calls instead of nesting `if/else` layer upon layer.

### and_then: Chaining Fallible Operations

`and_then` takes a function `f`; `f` receives the value inside the `expected` and returns a new `expected`. If the current `expected` is in the error state, `f` is not called — the error passes straight through to the end of the chain:

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

If `parse_int` returns an error, neither `validate_positive` nor the lambda executes — the error shows up directly in `result.error()`. That is what "errors propagate automatically" means.

### transform: Transforming the Value

The difference between `transform` and `and_then` is that the function you pass in returns a plain value rather than an `expected`. `transform` wraps that return value into a new `expected` for you:

```cpp
auto result = parse_int("42")
    .transform([](int v) { return v * 2; })
    .transform([](int v) { return std::to_string(v); });
// The type of result is std::expected<std::string, ParseError>
```

Here the first `transform` turns an `int` into an `int` (doubled), and the second turns the `int` into a `std::string`. If any step along the way fails, the subsequent `transform` calls do not run.

`transform` suits transformations that cannot fail themselves. If an operation may fail, use `and_then`; if it always succeeds, use `transform`.

### or_else: Handling Errors

`or_else` invokes the function you pass in when the `expected` is in the error state. It is typically used for error recovery, logging, or error enrichment:

```cpp
std::expected<int, std::string> try_cache(int key) {
    return std::unexpected("cache miss for " + std::to_string(key));
}

std::expected<int, std::string> try_database(int key) {
    return key * 100;  // Simulate fetching from the database
}

int main() {
    auto result = try_cache(42)
        .or_else([](const std::string& err) {
            std::cerr << "Cache failed: " << err << ", trying DB\n";
            return try_database(42);
        });

    // result holds 4200
}
```

The function passed to `or_else` must return an `expected` of the same type. This means you can perform error recovery inside `or_else` — if the fallback operation succeeds, the rest of the chain continues down the success path.

### transform_error: Transforming the Error Type

`transform_error` lets you transform the error object while it propagates, without affecting the success path. This is extremely useful for cross-layer error propagation — a lower layer may use one error type while an upper layer needs another:

```cpp
struct AppError {
    int code;
    std::string message;
    std::string context;  // Extra context information
};

auto result = parse_int("abc")
    .transform_error([](ParseError e) -> AppError {
        return AppError{static_cast<int>(e),
                        "Parse error",
                        "in config file line 1"};
    });
// The type of result is std::expected<int, AppError>
```

### A Complete Chained Example

Combine the four operations and you get a complete error-handling pipeline:

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
    // Simplified: assume it always succeeds
    return "host=192.168.1.1\nport=8080\n";
}

std::expected<ServerConfig, ConfigError> parse_config(
    const std::string& content) {
    ServerConfig cfg;
    cfg.host = "localhost";
    cfg.port = 8080;
    // Simplified: actually parse the content
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

This chain reads very clearly: read the file -> parse the config -> validate the config -> convert to a connection string. If any step fails, the subsequent steps are skipped automatically, and the error message is handled uniformly at the end of the chain.

------

## expected vs Exceptions vs optional

We have put together a comparison table to help you choose in real-world scenarios:

| Scenario | Recommended approach | Why |
|----------|----------------------|-----|
| Lookup / caching, failure carries no reason | `optional` | Concise; no error information needed |
| Parsing / I/O, you need to know why it failed | `expected` | Carries error information |
| Multi-step operation chains that need error propagation | `expected` | Monadic operations support chaining |
| Unrecoverable, serious errors | exceptions | Forces a break; RAII cleans up automatically |
| Constructor failure | exceptions | Constructors have no return value |
| Embedded (no exception support) | `expected` or an enum | No RTTI dependency |

A practical rule of thumb: **if the caller needs to do different things depending on the error type (retry, degrade, report), use `expected`; if it only needs to know "success or failure", use `optional`; and if it is a serious error at the program-logic level (impossible to recover from), use exceptions.**

------

## A Simplified Implementation for C++17

If your project is still on C++17, don't worry — you can implement a simplified yet fully functional `expected`. The implementation below covers the core functionality and can be dropped straight into a project:

```cpp
#include <utility>
#include <type_traits>
#include <stdexcept>

/// Helper type: used to construct the error branch
template <typename E>
struct unexpected {
    E value;
    constexpr explicit unexpected(E v) : value(std::move(v)) {}
};

/// A simplified expected<T, E>
template <typename T, typename E>
class expected {
    bool has_value_;
    union {
        T val_;
        E err_;
    } storage_;

public:
    // Construct from a success value
    expected(const T& v) : has_value_(true) {
        new(&storage_.val_) T(v);
    }

    expected(T&& v) : has_value_(true) {
        new(&storage_.val_) T(std::move(v));
    }

    // Construct from an error
    expected(unexpected<E> u) : has_value_(false) {
        new(&storage_.err_) E(std::move(u.value));
    }

    // Destructor
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

    /// and_then: chain an operation that returns an expected
    template <typename F>
    auto and_then(F&& f) -> decltype(f(std::declval<T>())) {
        using ResultType = decltype(f(std::declval<T>()));
        if (has_value_) return f(storage_.val_);
        return ResultType(unexpected<E>{storage_.err_});
    }

    /// transform: transform the value
    template <typename F>
    auto transform(F&& f)
        -> expected<decltype(f(std::declval<T>())), E> {
        using U = decltype(f(std::declval<T>()));
        if (has_value_)
            return expected<U, E>(f(storage_.val_));
        return expected<U, E>(unexpected<E>{storage_.err_});
    }

    /// or_else: handle the error
    template <typename F>
    expected or_else(F&& f) {
        if (has_value_) return *this;
        return f(storage_.err_);
    }
};
```

This implementation omits some details (fine-grained control over copy/move semantics, `constexpr` support, and so on), but its core semantics are completely correct, and it can be used for error handling in production environments.

------

## A Practical Example: A Multi-Layer Parsing Chain

Let's look at an example closer to real-world development — parsing a network address out of a string, involving multiple stages of validation and conversion:

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

This example demonstrates the advantage of `expected` in multi-layer operations: every step returns an `expected`, any step that fails propagates automatically, and everything is handled uniformly at the end of the chain. The error information carries plenty of context — the `detail` field tells you exactly what went wrong.

------

## References

- [cppreference: std::expected](https://en.cppreference.com/w/cpp/utility/expected)
- [P2505R5 - Monadic Functions for std::expected](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2022/p2505r1.html)
- [C++ Stories: std::expected monadic extensions](https://www.cppstories.com/2024/expected-cpp23-monadic/)
