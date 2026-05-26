---
title: 'Error Handling Patterns Summary: A Selection Guide and Best Practices'
description: A comprehensive comparison of all error handling approaches, providing
  scenario-based selection guidelines.
chapter: 10
order: 4
tags:
- host
- cpp-modern
- intermediate
- 类型安全
difficulty: intermediate
platform: host
cpp_standard:
- 11
- 14
- 17
- 20
- 23
reading_time_minutes: 20
prerequisites:
- 'Chapter 10: 错误处理的演进'
- 'Chapter 10: optional 用于错误处理'
- 'Chapter 10: std::expected'
related:
- RAII 深入理解
translation:
  source: documents/vol2-modern-features/ch10-error-handling/04-error-patterns.md
  source_hash: aae379f59aac125a1c2944b75ff76e575a081758ec0299b6a74ed718a12cff68
  translated_at: '2026-05-26T11:36:02.080864+00:00'
  engine: anthropic
  token_count: 2747
---
# Error Handling Patterns Summary: A Selection Guide and Best Practices

Building on the previous three articles, we discussed the pros and cons of error codes, exceptions, `optional`, and `expected`. This article wraps up our entire error handling topic — we will put all approaches together for a comprehensive comparison, provide a practical selection guide, and share best practices learned from real-world pitfalls.

Additionally, this article covers topics we didn't explore earlier: combinator patterns commonly used in functional error handling, macro-assisted error propagation techniques, and error conversion strategies at C API boundaries.

------

## Comprehensive Comparison

Let's put the key metrics of all approaches side by side. This table is important — consider bookmarking it:

| Metric | Enum/Error Code | Exception | optional | variant | expected |
|--------|-----------------|-----------|----------|---------|----------|
| **Error information** | Enum value | Rich (exception object) | None | Limited (which type is held) | Rich (custom E) |
| **Ignorability** | Easy to ignore | Cannot be ignored | Can be ignored | Can be ignored | Can be ignored |
| **Happy path overhead** | Zero | Zero | Negligible | Small | Small |
| **Failure path overhead** | Zero | Heavy | Zero | Zero | Zero |
| **Composability** | Poor (manual propagation) | Good (automatic propagation) | Good (C++23 monadic) | Poor (verbose visit) | Good (native monadic) |
| **Control flow transparency** | High (explicit checks) | Low (invisible jumps) | High | Medium | High |
| **Embedded viability** | Fully viable | Usually disabled | Fully viable | Fully viable | Fully viable |
| **Requires RTTI** | No | Yes | No | No | No |
| **C++ standard requirement** | C++98 | C++98 | C++17 | C++17 | C++23 |

The "ignorability" metric in the table deserves extra mention. C++ lacks a Rust-like `#[must_use]` compiler-enforced check (although C++17 has `[[nodiscard]]`, the standard library doesn't apply this attribute to `optional` / `expected`). So in C++, whether using error codes or `expected`, callers might skip checking the return value — we need to rely on code reviews and static analysis tools to fill this gap.

------

## Selection Guide

Based on real project experience, we've summarized a decision flow to help you choose the right approach for your specific scenario.

### Decision Tree

**Step 1: Is the error "recoverable"?**

If the error indicates a serious logic bug (like null pointer dereference, array out-of-bounds), or the system is in an unrecoverable state (out of memory, stack overflow), we should use `assert` or terminate the program directly. Such errors should not be handled with any "return value" approach, because the caller simply cannot make a reasonable recovery action.

**Step 2: Are you running in an environment that allows exceptions?**

If the environment allows exceptions (host applications, servers), and the error frequency is very low ("exceptions" are, after all, "exceptional situations"), exceptions are the best choice — clean code, automatic RAII cleanup, and no forgotten error handling. Embedded environments or performance-sensitive hot paths typically disable exceptions, in which case move to step three.

**Step 3: Does the caller need to know the reason for failure?**

If not — for example, a lookup operation only cares about "found or not", a cache only cares about "hit or miss" — use `optional`. Simple, lightweight, and clear semantics.

If yes — for example, file operations need to distinguish "file not found" from "permission denied", network requests need to distinguish "timeout" from "connection refused" — use `expected`.

**Step 4: Does your compiler support C++23?**

If yes, use `std::expected<T, E>` directly and enjoy native monadic operations. If you're still on C++17, use a simplified self-implemented `expected`, or use an enum + struct approach.

### Scenario-Based Recommendations

We've put together a recommended list organized by common scenarios:

| Scenario | Recommended Approach | Rationale |
|----------|---------------------|-----------|
| Lookup/search | `optional` | Only care about presence, no reason needed |
| Cache hit | `optional` | Same as above |
| User input validation | `expected` | Need to tell the user what went wrong |
| Config file parsing | `expected` | Need to distinguish "file not found" from "format error" |
| Network IO | `expected` | Need to distinguish timeout, refused, DNS failure, etc. |
| File IO | `expected` | Need to distinguish not found, permission, disk full, etc. |
| Database query | `expected` | Need to distinguish connection failure, syntax error, no results, etc. |
| Constructor failure | Exception | Constructors have no return value |
| Unrecoverable errors | `assert` / terminate | Should not attempt recovery |
| High-frequency interrupt/signal handling | Error code | Extremely low overhead, deterministic execution time |
| Crossing C/C++ boundaries | Error code | C doesn't understand C++ types |

------

## Performance Comparison

Performance is a concern for many. We'll provide a simplified analysis to help you make decisions in performance-sensitive scenarios.

Compared to bare error codes, the extra overhead of `expected` mainly comes from two aspects: first, type construction — `expected<T, E>` needs to store a flag (success/failure) and the storage space for `T` or `E`; second, move/copy — during error propagation, the error object might be moved multiple times.

At the `-O2` optimization level, most of this overhead gets inlined and optimized away by the compiler. The optimized assembly of a function returning `expected<int, EnumError>` is virtually indistinguishable from one returning an `int` error code — because the compiler can optimize the flag into one register and the error enum value into another.

The scenario with a real performance difference is things like `expected<std::string, std::string>` — where both the value type and error type might involve heap allocation. In this case, each propagation step moves the contents of `std::string`. If your operation chain is long (say, more than five steps), we recommend using lightweight error types (enums, small structs, `std::string_view`).

The performance model for exceptions is completely different. On the "happy path," exception overhead is near zero (modern compilers use the "zero-cost exception handling" model). But when throwing an exception, the overhead of stack unwinding is massive — it needs to walk the stack frames, locate catch blocks, and destroy local objects. This means exceptions are not suitable for "failures expected to occur frequently" — if 10% of your HTTP service requests time out, using exceptions to handle timeouts is a poor choice.

------

## Functional Error Handling Patterns

The core idea behind functional error handling is: **errors are values, not control flow accidents**. Through combinator patterns, error propagation and transformation become predictable and composable.

### TRY Macro: Simulating Rust's ? Operator

C++ doesn't have a built-in `?` operator, but we can simulate it with a macro. This macro is extremely handy in functional-style error handling:

```cpp
/// TRY 宏：如果表达式返回错误，直接向上传播
/// 使用 GCC/Clang 的 statement expression 语法
#define TRY(expr)                                           \
    ({                                                      \
        auto _result = (expr);                              \
        if (!_result) return std::unexpected(_result.error()); \
        std::move(_result.value());                         \
    })

// 使用示例
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

Compare this to the manual checking version without the macro:

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

The macro version is much cleaner, with clear semantics — `TRY` means "try this step, bail out if it fails." But note that this macro uses GCC/Clang's statement expression syntax, so MSVC requires a different implementation.

For compilers that don't support statement expressions, we can use a slightly more verbose but portable version:

```cpp
// 可移植版本：需要调用方声明变量
#define TRY_OUT(result, expr)            \
    auto result = (expr);                \
    if (!result) return std::unexpected(result.error())

// 使用
std::expected<Config, ConfigError> load_config(const std::string& path) {
    TRY_OUT(content, read_file(path));
    TRY_OUT(config, parse_config(content.value()));
    TRY_OUT(validated, validate_config(config.value()));
    return validated;
}
```

### Error Recovery and Retry

The functional style also makes it easy to implement retry logic. A generic retry wrapper:

```cpp
#include <chrono>
#include <thread>

/// 带指数退避的重试包装器
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
        delay *= 2;  // 指数退避
    }
    return ResultType();  // 不会到这里
}

// 使用
auto result = retry(
    []() { return fetch_url("https://example.com"); },
    3,                          // 最多 3 次
    std::chrono::milliseconds(100)  // 初始延迟 100ms
);
```

### Error Aggregation

Sometimes we want to collect all errors and report them together, rather than returning on the first failure. For example, form validation — a user submits a form, and multiple fields might have issues simultaneously. Telling the user about everything at once is much better than fixing them one by one:

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

## Boundary Handling with C APIs

In embedded development, we frequently need to interact with C APIs. C APIs typically use integer error codes, while our C++ code uses `expected`. We do a one-time conversion at the boundary, then use C++ style exclusively internally:

```cpp
// 假设 C API 长这样
extern "C" {
    int hal_init(void);       // 返回 0 表示成功
    int hal_send(const uint8_t* data, int len);
    int hal_read(uint8_t* buffer, int len);
}

// C++ 包装层
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
    return ret;  // 返回实际读取的字节数
}

// 现在可以用函数式风格组织
std::expected<void, HalError> send_command(const uint8_t* cmd, int len) {
    TRY_OUT(init_result, wrapped_hal_init());
    TRY_OUT(send_result, wrapped_hal_send(cmd, len));
    return {};
}
```

The key principle is: **do a one-time conversion at the C/C++ boundary, use C++ style exclusively internally**. This maintains compatibility with the C ecosystem while keeping the C++ code clean.

------

## Best Practices

Finally, here are some best practices we've summarized from real projects — every single one learned the hard way.

### 1. Choose One Approach and Stay Consistent

Mixing multiple error handling styles is the biggest source of code confusion. If the team decides to use `expected`, use `expected` everywhere; if the decision is error codes, use error codes everywhere. Don't have one function return `optional`, another throw an exception, and yet another use output parameters — the caller has to check the documentation every time to know how to handle errors.

### 2. Keep Error Types Lightweight

The `E` in `expected<T, E>` should be as lightweight as possible — enums, small structs, or `std::string_view`. Avoid using `std::string` or structs with heap-allocated members as error types, because during error propagation, the error object might be copied or moved multiple times. If your error type needs to carry complex information, consider using an error code plus an error message lookup table.

### 3. Use [[nodiscard]] to Enforce Return Value Checks

Although the standard library doesn't add `[[nodiscard]]` to `optional` and `expected`, you can add it to your own return types:

```cpp
struct [[nodiscard]] Result {
    ErrorCode error;
    std::string message;
    constexpr bool ok() const noexcept { return error == ErrorCode::kSuccess; }
};
```

This way, if the caller ignores the return value, the compiler will issue a warning. While not as strict as Rust's `#[must_use]`, it's better than nothing.

### 4. Don't Store Exceptions in expected's E

`std::expected<T, std::exception_ptr>` looks tempting — it avoids exception overhead while preserving the rich information of exceptions. But in practice, it makes `expected` cumbersome, and you need to rethrow the exception at the final handling point to extract the information. A better approach is to define a lightweight error type.

### 5. Error Handling Should Have Layers

Low-level functions use simple error types (enums), the middle layer enriches error information during propagation (adding context), and the top layer does the final logging and user notification. This keeps the low level generic while giving the top level sufficiently rich information:

```cpp
// 底层：简单的枚举
std::expected<int, IoError> read_byte(int fd);

// 中间层：增强错误信息
std::expected<Config, AppError> load_config(const std::string& path) {
    auto byte = read_byte(fd)
        .transform_error([path](IoError e) -> AppError {
            return AppError{e, "while reading config: " + path};
        });
    // ...
}
```

### 6. Use Error Codes for Performance-Sensitive Hot Paths

In scenarios like high-frequency interrupt handling, signal handling, and real-time sampling, the construction and move overhead of `expected` (though small) might still be unacceptable. In these scenarios, use the simplest error codes and global error states to squeeze out every last bit of performance.

### 7. Use Assertions for Impossible Situations

`assert` is for checking program logic invariants — if an assertion fails, it means the code has a bug. Don't use `assert` to check external inputs (user input, file contents, network data), because external inputs "might fail" — they aren't "impossible." Use `expected` / error codes for the former, and `assert` for the latter.

------

## Summary

There is no silver bullet for error handling. Error codes are simple and brute-force, exceptions are elegant but heavy, `optional` is lightweight but carries no information, and `expected` is currently the most balanced approach but requires C++23 (or a custom implementation). When choosing an approach, we need to consider environment constraints (can we use exceptions?), performance requirements (are there hot paths?), and team preferences (is the style consistent?).

Our recommended strategy is: **default to `expected`, use `optional` for lookup/cache scenarios, use exceptions/termination for constructors and unrecoverable errors, and do a one-time conversion at C API boundaries**. The toolbox can hold many tools, but we need to know when to use which.

With this, ch10 Error Handling is fully covered. In the next article, we'll move on to ch11 and discuss user-defined literals — an interesting mechanism that makes code more intuitive and safer.

## References

- [cppreference: Error handling](https://en.cppreference.com/w/cpp/error)
- [C++ Core Guidelines: Error handling](https://isocpp.org/wiki/faq/exceptions)
- [P2505R5 - Monadic Functions for std::expected](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2022/p2505r1.html)
