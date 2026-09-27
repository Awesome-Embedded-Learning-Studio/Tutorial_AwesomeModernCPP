---
chapter: 10
cpp_standard:
- 11
- 14
- 17
- 20
- 23
description: A comprehensive comparison of all error handling approaches with a
  scenario-based selection guide
difficulty: intermediate
order: 4
platform: host
prerequisites:
- 'Chapter 10: Evolution of Error Handling: From Error Codes to Type Safety'
- 'Chapter 10: optional for Error Handling'
- 'Chapter 10: std::expected<T, E>: Type-Safe Error Propagation'
reading_time_minutes: 13
related:
- 'Deep Dive into RAII: The Cornerstone of Resource Management'
tags:
- host
- cpp-modern
- intermediate
- 类型安全
title: 'Error Handling Patterns: A Selection Guide and Best Practices'
translation:
  source: documents/vol2-modern-features/ch10-error-handling/04-error-patterns.md
  source_hash: ab1d0b97afa451f4415ac032c0c0c11091b93e29d98ce1a666e5aad040d8faaf
  translated_at: '2026-09-25T16:45:19+00:00'
  engine: anthropic
  token_count: 2750
---
# Error Handling Patterns: A Selection Guide and Best Practices

With the previous three articles as groundwork, we have discussed the pros and cons of error codes, exceptions, `optional`, and `expected`. This article wraps up the entire error handling topic—we will put all the approaches side by side for a comprehensive comparison, then give you a practical selection guide, plus a set of best practices distilled from real-world pitfalls.

Along the way, we will also fill in a few topics the earlier articles did not expand on: the combinator patterns common in functional error handling, macro-assisted error propagation tricks, and strategies for converting errors at the boundary with C APIs.

------

## Comprehensive Comparison

First, let's put the key metrics of all the approaches in one place. This table matters—consider bookmarking it:

| Metric | Enum/Error Codes | Exceptions | optional | variant | expected |
|--------|------------------|------------|----------|---------|----------|
| **Error information carried** | Enum value | Rich (exception object) | None | Limited (which type is held) | Rich (custom E) |
| **Ignorability** | Easy to ignore | Cannot be ignored | Ignorable | Ignorable | Ignorable |
| **Happy-path overhead** | Zero | Zero | Tiny | Small | Small |
| **Failure-path overhead** | Zero | Heavy | Zero | Zero | Zero |
| **Composability** | Poor (manual propagation) | Good (automatic propagation) | Good (C++23 monadic) | Poor (verbose visit) | Good (native monadic) |
| **Control-flow transparency** | High (explicit checks) | Low (invisible jumps) | High | Medium | High |
| **Usable in embedded** | Fully usable | Usually disabled | Fully usable | Fully usable | Fully usable |
| **Requires RTTI** | No | Yes | No | No | No |
| **C++ standard required** | C++98 | C++98 | C++17 | C++17 | C++23 |

The "Ignorability" row in the table deserves a few extra words. C++ has nothing like Rust's `#[must_use]` compiler enforcement (C++17 does have `[[nodiscard]]`, but the standard library does not apply that attribute to `optional` / `expected`). So in C++, whether you return error codes or an `expected`, the caller can still end up not checking the return value—code review and static analysis tools have to close that gap.

------

## Selection Guide

From real project experience, we have summarized a decision flow; you can follow it to pick the right approach for your specific scenario.

### Decision Tree

**Step 1: Is the error "recoverable"?**

If the error means there is a serious bug in the program's logic (say, a null pointer dereference or an out-of-bounds array access), or the system is in a state it cannot possibly recover from (out of memory, stack overflow), you should use `assert` or terminate the program outright. No "return value" scheme should handle these errors, because the caller could not take any reasonable recovery action anyway.

**Step 2: Are you running in an environment that allows exceptions?**

If the environment allows exceptions (host applications, servers) and errors occur rarely (an "exception" is by definition an "unusual situation"), exceptions are the best choice—clean code, automatic RAII cleanup, and nothing to forget to handle. Embedded environments and performance-sensitive hot paths usually disable exceptions; in that case, move on to Step 3.

**Step 3: Does the caller need to know the reason for the failure?**

If not—for instance, a lookup only cares "is it there or not", and a cache only cares "hit or miss"—use `optional`. Simple, lightweight, semantically clear.

If it does—for instance, file operations need to distinguish "file not found" from "permission denied", and network requests need to distinguish "timeout" from "connection refused"—use `expected`.

**Step 4: Does your compiler support C++23?**

If it does, use `std::expected<T, E>` directly and enjoy the native monadic operations. If you are still on C++17, use your own simplified `expected` implementation, or go with the enum + struct approach.

Draw this decision flow as a tree—ask the four questions top to bottom, and whichever exit you reach tells you which approach to use:

![Decision tree for choosing an error handling approach](./04-error-patterns-choose.drawio)

### Scenario-Based Recommendations

Here is a list of recommendations we have organized by common scenario:

| Scenario | Recommended approach | Rationale |
|----------|----------------------|-----------|
| Lookup/search | `optional` | Only care whether it exists, not the reason |
| Cache hit | `optional` | Same as above |
| User input validation | `expected` | Need to tell the user what went wrong |
| Config file parsing | `expected` | Need to distinguish "file not found" from "format error" |
| Network I/O | `expected` | Need to distinguish timeout, refused, DNS failure, etc. |
| File I/O | `expected` | Need to distinguish not found, permission, disk full, etc. |
| Database query | `expected` | Need to distinguish connection failure, syntax error, no results, etc. |
| Constructor failure | Exceptions | Constructors have no return value |
| Unrecoverable errors | `assert` / terminate | Recovery should not be attempted |
| High-frequency interrupt/signal handling | Error codes | Extremely low overhead, deterministic execution time |
| Cross C/C++ boundary | Error codes | C does not understand C++ types |

------

## Performance Comparison

Performance is what many people worry about. Here is a simplified analysis to help you decide in performance-sensitive scenarios.

Compared with raw error codes, the extra overhead of `expected` comes mainly from two places: first, type construction—an `expected<T, E>` has to store a flag (success/failure) plus storage space for `T` or `E`; second, moves/copies—during error propagation, the error object may be moved several times.

At the `-O2` optimization level, the compiler inlines and optimizes most of that overhead away. A function returning `expected<int, EnumError>` ends up with assembly virtually indistinguishable from a function returning an `int` error code—because the compiler can keep the flag in one register and the error enum value in another.

The scenario where performance really diverges is something like `expected<std::string, std::string>`, where both the value type and the error type can involve heap allocation. In that case, every propagation moves the contents of a `std::string`. If your chain of operations is long (say, more than 5 steps), use a lightweight error type (an enum, a small struct, or a `std::string_view`).

The performance model of exceptions is completely different. On the "happy path", the cost of exceptions is close to zero (modern compilers use the "zero-cost exception handling" model). But when an exception is thrown, stack unwinding is expensive—it requires walking stack frames, searching for the catch block, and destroying local objects. This means exceptions are a poor fit for "failures that are expected to happen frequently"—if 10% of the requests to your HTTP service time out, handling timeouts with exceptions is a bad choice.

------

## Functional Error Handling Patterns

The core idea of functional error handling is: **errors are values, not surprises in the control flow**. With combinator patterns, error propagation and transformation become predictable and composable.

### TRY Macro: Simulating Rust's ? Operator

C++ has no built-in `?` operator, but we can simulate one with a macro. It is remarkably handy in functional-style error handling:

```cpp
/// TRY macro: if the expression returns an error, propagate it upward immediately
/// Uses GCC/Clang statement expression syntax
#define TRY(expr)                                           \
    ({                                                      \
        auto _result = (expr);                              \
        if (!_result) return std::unexpected(_result.error()); \
        std::move(_result.value());                         \
    })

// Usage example
std::expected<std::string, ConfigError> read_file(const std::string& path);
std::expected<Config, ConfigError> parse_config(const std::string& content);
std::expected<Config, ConfigError> validate_config(const Config& cfg);

std::expected<Config, ConfigError> load_config(const std::string& path) {
    auto content = TRY(read_file(path));
    auto config = TRY(parse_config(content));
    auto validated = TRY(validate_config(config));
    return validated;
}
```

Compare that with the manual-check version without the macro:

```cpp
std::expected<Config, ConfigError> load_config(const std::string& path) {
    auto content_result = read_file(path);
    if (!content_result) {
        return std::unexpected(content_result.error());
    }

    auto config_result = parse_config(content_result.value());
    if (!config_result) {
        return std::unexpected(config_result.error());
    }

    auto validated_result = validate_config(config_result.value());
    if (!validated_result) {
        return std::unexpected(validated_result.error());
    }

    return validated_result;
}
```

The macro version is far more concise, and its semantics are clear—`TRY` means "attempt this step, and give up if it fails". One caveat: this macro uses GCC/Clang statement expression syntax, so MSVC needs a different implementation.

For compilers that do not support statement expressions, there is a slightly more verbose but portable version:

```cpp
// Portable version: the caller declares the variable
#define TRY_OUT(result, expr)            \
    auto result = (expr);                \
    if (!result) return std::unexpected(result.error())

// Usage
std::expected<Config, ConfigError> load_config(const std::string& path) {
    TRY_OUT(content, read_file(path));
    TRY_OUT(config, parse_config(content.value()));
    TRY_OUT(validated, validate_config(config.value()));
    return validated;
}
```

### Error Recovery and Retry

Functional style also makes retry logic easy to implement. A generic retry wrapper:

```cpp
#include <chrono>
#include <thread>

/// Retry wrapper with exponential backoff
template <typename F, typename Rep, typename Period>
auto retry(F&& func, unsigned max_attempts,
           std::chrono::duration<Rep, Period> initial_delay)
    -> decltype(func()) {
    using ResultType = decltype(func());
    auto delay = initial_delay;

    for (unsigned attempt = 0; attempt < max_attempts; ++attempt) {
        auto result = func();
        if (result) return result;

        if (attempt == max_attempts - 1) return result;

        std::this_thread::sleep_for(delay);
        delay *= 2;  // exponential backoff
    }
    return ResultType();  // never reached
}

// Usage
auto result = retry(
    []() { return fetch_url("https://example.com"); },
    3,                          // at most 3 attempts
    std::chrono::milliseconds(100)  // initial delay 100ms
);
```

### Error Aggregation

Sometimes you want to collect all the errors and report them together instead of returning at the first one. Take form validation—a user submits a form, several fields may be wrong at once, and telling the user everything in a single pass is far better than making them fix things one by one:

```cpp
#include <vector>
#include <string>
#include <iostream>

struct ValidationError {
    std::string field;
    std::string message;
};

struct ValidationReport {
    std::vector<ValidationError> errors;

    void add(std::string field, std::string message) {
        errors.push_back({std::move(field), std::move(message)});
    }

    bool ok() const { return errors.empty(); }

    void print() const {
        for (const auto& e : errors) {
            std::cerr << "  - " << e.field << ": " << e.message << "\n";
        }
    }
};

void validate_form(const std::string& name,
                   const std::string& email,
                   int age,
                   ValidationReport& report) {
    if (name.empty()) report.add("name", "Name cannot be empty");
    if (name.size() > 100) report.add("name", "Name too long");

    if (email.find('@') == std::string::npos) {
        report.add("email", "Invalid email format");
    }

    if (age < 0 || age > 200) report.add("age", "Age out of range");
}

int main() {
    ValidationReport report;
    validate_form("", "invalid", -1, report);

    if (!report.ok()) {
        std::cerr << "Validation failed:\n";
        report.print();
    }
}
```

------

## Handling the Boundary with C APIs

In embedded development we constantly have to work with C APIs. C APIs usually represent errors as integer error codes, while our C++ code uses `expected`. Convert once at the boundary, then stay fully C++-style inside:

```cpp
// Suppose the C API looks like this
extern "C" {
    int hal_init(void);       // returns 0 on success
    int hal_send(const uint8_t* data, int len);
    int hal_read(uint8_t* buffer, int len);
}

// C++ wrapper layer
enum class HalError {
    kInitFailed,
    kSendFailed,
    kReadFailed,
    kTimeout,
};

std::expected<void, HalError> wrapped_hal_init() {
    int ret = hal_init();
    if (ret != 0) return std::unexpected(HalError::kInitFailed);
    return {};
}

std::expected<void, HalError> wrapped_hal_send(
    const uint8_t* data, int len) {
    int ret = hal_send(data, len);
    if (ret != 0) return std::unexpected(HalError::kSendFailed);
    return {};
}

std::expected<int, HalError> wrapped_hal_read(
    uint8_t* buffer, int len) {
    int ret = hal_read(buffer, len);
    if (ret < 0) return std::unexpected(HalError::kReadFailed);
    return ret;  // returns the number of bytes actually read
}

// Now we can organize this in functional style
std::expected<void, HalError> send_command(const uint8_t* cmd, int len) {
    TRY_OUT(init_result, wrapped_hal_init());
    TRY_OUT(send_result, wrapped_hal_send(cmd, len));
    return {};
}
```

The key principle is: **convert once at the C/C++ boundary, then use C++ style everywhere inside**. This keeps compatibility with the C ecosystem while the C++ code stays clean.

------

## Best Practices

To close out, here are the best practices we have summarized from real projects—every one of them learned the hard way.

### 1. Choose One Approach and Stay Consistent

Mixing multiple error handling styles is the single biggest source of messy code. If the team decides to use `expected`, use `expected` everywhere; if it decides on error codes, use error codes everywhere. Do not have one function return `optional`, another throw exceptions, and a third use output parameters—callers would have to consult the documentation every single time to figure out how errors are reported.

### 2. Keep Error Types Lightweight

The `E` in `expected<T, E>` should be as lightweight as possible—an enum, a small struct, or a `std::string_view`. Avoid `std::string`, or structs containing heap-allocating members, as the error type, because during error propagation the error object may be copied or moved multiple times. If your error type needs to carry complex information, consider an error code plus an error-message lookup table.

### 3. Use [[nodiscard]] to Enforce Return Value Checks

Although the standard library does not mark `optional` and `expected` `[[nodiscard]]`, you can add it to your own return types:

```cpp
struct [[nodiscard]] Result {
    ErrorCode error;
    std::string message;
    constexpr bool ok() const noexcept { return error == ErrorCode::kSuccess; }
};
```

With that in place, the compiler emits a warning if the caller ignores the return value. It is not as strict as Rust's `#[must_use]`, but it beats nothing.

### 4. Do Not Store Exceptions in expected's E

`std::expected<T, std::exception_ptr>` looks tempting—it seems to avoid the overhead of exceptions while keeping their rich information. In practice, it makes the `expected` heavy, and you would have to re-throw the exception at the final handling site to get at that information. A better move is to define a lightweight error type.

### 5. Give Error Handling Layers

Low-level functions use simple error types (enums), middle layers enrich the error information during propagation (attaching context), and the top layer does the final logging and user-facing messages. That way the bottom stays generic while the top gets information rich enough to act on:

```cpp
// Low level: a simple enum
std::expected<int, IoError> read_byte(int fd);

// Middle layer: enrich the error with context
std::expected<Config, AppError> load_config(const std::string& path) {
    auto byte = read_byte(fd)
        .transform_error([path](IoError e) -> AppError {
            return AppError{e, "while reading config: " + path};
        });
    // ...
}
```

### 6. Use Error Codes on Performance-Sensitive Hot Paths

In high-frequency interrupt handling, signal handling, real-time sampling, and similar scenarios, even the (small) construction and move overhead of `expected` may be unacceptable. In these scenarios, use the simplest error codes and a global error state, and squeeze performance to the limit.

### 7. Reserve Assertions for Impossible Situations

`assert` is for checking invariants of your program's logic—if an assertion fires, the code has a bug. Do not use `assert` to check external input (user input, file contents, network data), because external input "can go wrong"; it is not "impossible". The former calls for `expected` / error codes; the latter for `assert`.

------

## Reference Resources

- [cppreference: Error handling](https://en.cppreference.com/w/cpp/error)
- [C++ Core Guidelines: Error handling](https://isocpp.org/wiki/faq/exceptions)
- [P2505R5 - Monadic Functions for std::expected](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2022/p2505r1.html)
