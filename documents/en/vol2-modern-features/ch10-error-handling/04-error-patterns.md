# Error Handling Patterns Summary: A Selection Guide and Best Practices

Building on the previous three articles, we discussed the pros and cons of error codes, exceptions, `optional`, and `variant`. This article serves as the conclusion to our error handling topic—we will put all approaches together for a comprehensive comparison, provide a practical selection guide, and share best practices learned from real-world pitfalls.

Additionally, this article covers topics we didn't explore earlier: combinator patterns commonly used in functional error handling, macro-assisted error propagation techniques, and error conversion strategies at the C API boundary.

------

## Comprehensive Comparison

Let's start by putting the key metrics of all approaches side by side. This table is important—consider bookmarking it:

| Metric | Enum/Error Code | Exception | optional | variant | expected |
|--------|-----------------|-----------|----------|---------|----------|
| **Carries error info** | Enum value | Rich (exception object) | None | Limited (which type is held) | Rich (custom E) |
| **Ignorability** | Easy to ignore | Cannot be ignored | Can be ignored | Can be ignored | Can be ignored |
| **Happy path overhead** | Zero | Zero | Negligible | Small | Small |
| **Failure path overhead** | Zero | Heavy | Zero | Zero | Zero |
| **Composability** | Poor (manual propagation) | Good (automatic propagation) | Good (C++23 monadic) | Poor (verbose visit) | Good (native monadic) |
| **Control flow transparency** | High (explicit checks) | Low (invisible jumps) | High | Medium | High |
| **Embedded viability** | Fully viable | Usually disabled | Fully viable | Fully viable | Fully viable |
| **Requires RTTI** | No | Yes | No | No | No |
| **C++ standard requirement** | C++98 | C++98 | C++17 | C++17 | C++23 |

The "ignorability" metric in the table deserves a closer look. C++ lacks Rust's `#[must_use]` compiler-enforced checks (although C++17 introduced `[[nodiscard]]`, the standard library doesn't apply this attribute to `optional` or `expected`). Therefore, in C++, whether using error codes or `expected`, callers might simply not check the return value—this gap must be filled by code reviews and static analysis tools.

------

## Selection Guide

Based on real-world project experience, we've summarized a decision flow to help you choose the right approach for your specific scenario.

### Decision Tree

**Step 1: Is the error "recoverable"?**

If the error indicates a serious logic bug in the program (such as null pointer dereference, array out-of-bounds), or if the system is in an unrecoverable state (out of memory, stack overflow), you should use `assert` or terminate the program directly. Such errors should not be handled by any "return value" approach, because the caller simply cannot make a reasonable recovery attempt.

**Step 2: Are you running in an environment that allows exceptions?**

If the environment allows exceptions (host applications, servers) and the error frequency is very low ("exceptions" are, by definition, "abnormal situations"), exceptions are the best choice—clean code, automatic RAII cleanup, and no forgotten error handling. Embedded environments or performance-sensitive hot paths typically disable exceptions, in which case move to step three.

**Step 3: Does the caller need to know the reason for failure?**

If not—for example, a lookup operation only cares about "found or not", or a cache only cares about "hit or miss"—use `optional`. It's simple, lightweight, and semantically clear.

If yes—for example, file operations need to distinguish between "file not found" and "permission denied", or network requests need to distinguish between "timeout" and "connection refused"—use `expected`.

**Step 4: Does your compiler support C++23?**

If yes, use `expected` directly and enjoy native monadic operations. If you're still on C++17, use a simplified, self-implemented version of `expected`, or use an enum + struct approach.

### Scenario-Based Recommendations

We've organized a recommendation list based on common scenarios:

| Scenario | Recommended Approach | Rationale |
|----------|---------------------|-----------|
| Lookup/Search | `optional` | Only care about presence, no reason needed |
| Cache hit | `optional` | Same as above |
| User input validation | `expected` | Need to tell the user what went wrong |
| Config file parsing | `expected` | Need to distinguish "file not found" from "format error" |
| Network IO | `expected` | Need to distinguish timeout, refusal, DNS failure, etc. |
| File IO | `expected` | Need to distinguish not found, permissions, disk full, etc. |
| Database query | `expected` | Need to distinguish connection failure, syntax error, no results, etc. |
| Constructor failure | Exception | Constructors have no return value |
| Unrecoverable errors | `assert` / Terminate | Should not attempt recovery |
| High-frequency interrupts/signal handling | Error codes | Extremely low overhead, deterministic execution time |
| Crossing C/C++ boundaries | Error codes | C doesn't understand C++ types |

------

## Performance Comparison

Performance is a concern for many. We provide a simplified analysis to help you make decisions in performance-sensitive scenarios.

Compared to raw error codes, the extra overhead of `expected` mainly comes from two aspects: first, type construction—`expected` needs to store a flag bit (success/failure) and the storage space for `T` or `E`; second, move/copy operations—the error object might be moved multiple times during error propagation.

At the `-O2` optimization level, most of this overhead is inlined and optimized away by the compiler. The optimized assembly code of a function returning `expected<T, E>` is virtually indistinguishable from a function returning an `enum` error code—because the compiler can optimize the flag bit into one register and the error enum value into another.

The scenario with a real performance difference is something like `expected<std::string, std::string>`—where both the value type and error type might involve heap allocation. In this case, every propagation step moves the contents of a `std::string`. If your operation chain is long (say, more than five steps), we recommend using lightweight error types (enums, small structs, `std::string_view`).

The performance model for exceptions is completely different. On the "happy path," exception overhead is near zero (modern compilers use the "zero-cost exception handling" model). But when an exception is thrown, the overhead of stack unwinding is massive—it requires traversing stack frames, searching for catch blocks, and destroying local objects. This means exceptions are not suitable for "failures expected to occur frequently"—if 10% of your HTTP service requests time out, using exceptions to handle timeouts is a terrible choice.

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

The macro version is much cleaner, and the semantics are clear—`TRY` simply means "try this step, bail out if it fails." Note, however, that this macro uses GCC/Clang's statement expression syntax, so MSVC requires a different implementation.

For compilers that don't support statement expressions, you can use a slightly more verbose but portable version:

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

Sometimes you want to collect all errors and report them together, rather than returning on the first failure. Form validation is a great example—a user submits a form, and multiple fields might have issues simultaneously. Telling the user about everything at once is much better than fixing them one by one:

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

In embedded development, we frequently interact with C APIs. C APIs typically use integer error codes, while our C++ code uses `expected`. We perform a one-time conversion at the boundary, then use C++ style exclusively internally:

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

The key principle is: **perform a one-time conversion at the C/C++ boundary, and use C++ style exclusively internally**. This maintains compatibility with the C ecosystem while keeping the C++ code clean and clear.

------

## Best Practices

Finally, here are some best practices we've summarized from real-world projects. Every single one was learned the hard way.

### 1. Choose One Approach and Stay Consistent

Mixing multiple error handling styles is the biggest root cause of code chaos. If the team decides to use `expected`, use `expected` everywhere; if the decision is error codes, use error codes everywhere. Don't have one function return `expected`, another throw an exception, and yet another use output parameters—callers would have to check the documentation every time just to know how to handle errors.

### 2. Keep Error Types Lightweight

The `E` in `expected<T, E>` should be as lightweight as possible—enums, small structs, or `std::string_view`. Avoid using `std::string` or structs containing heap-allocated members as error types, because the error object might be copied or moved multiple times during propagation. If your error type needs to carry complex information, consider using an error code paired with an error message lookup table.

### 3. Use [[nodiscard]] to Enforce Return Value Checks

Although the standard library doesn't apply `[[nodiscard]]` to `optional` and `expected`, you can add it to your own custom return types:

```cpp
struct [[nodiscard]] Result {
    ErrorCode error;
    std::string message;
    constexpr bool ok() const noexcept { return error == ErrorCode::kSuccess; }
};
```

This way, if the caller ignores the return value, the compiler will issue a warning. While not as strict as Rust's `#[must_use]`, it's better than nothing.

### 4. Don't Store Exceptions in expected's E

`expected<T, std::exception_ptr>` looks tempting—it avoids the overhead of exceptions while retaining their rich information. In reality, it makes `expected` clumsy, and you need to rethrow the exception at the final handling point just to extract the information. A better approach is to define a lightweight error type.

### 5. Layer Your Error Handling

Low-level functions should use simple error types (enums), intermediate layers should enrich error information during propagation (adding context), and the top level should handle final logging and user prompts. This keeps the low level generic while giving the top level sufficiently rich information:

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

In scenarios like high-frequency interrupt handling, signal handling, and real-time sampling, the construction and move overhead of `expected` (though small) might still be unacceptable. In these scenarios, use the simplest error codes and global error states to push performance to the absolute limit.

### 7. Use Assertions for Impossible Situations

`assert` is for checking program logic invariants—if an assertion fails, it means the code has a bug. Don't use `assert` to check external inputs (user input, file contents, network data), because external inputs "might fail," they aren't "impossible." Use `expected` / error codes for the former, and `assert` for the latter.

------

## Summary

There is no silver bullet for error handling. Error codes are simple and brute-force, exceptions are elegant but heavy, `optional` is lightweight but carries no information, and `expected` is currently the most balanced approach but requires C++23 (or a custom implementation). When choosing an approach, consider environmental constraints (can exceptions be used?), performance requirements (are there hot paths?), and team preferences (is the style consistent?).

Our recommended strategy is: **default to `expected`, use `optional` for lookup/cache scenarios, use exceptions/termination for constructors and unrecoverable errors, and perform a one-time conversion at C API boundaries**. You can fit multiple tools in your toolbox, but you need to know when to use which.

With this, ch10 Error Handling is fully covered. In the next article, we'll move on to ch11 and discuss user-defined literals—an interesting mechanism that makes code more intuitive and safer.

## Reference Resources

- [cppreference: Error handling](https://en.cppreference.com/w/cpp/error)
- [C++ Core Guidelines: Error handling](https://isocpp.org/wiki/faq/exceptions)
- [P2505R5 - Monadic Functions for std::expected](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2022/p2505r1.html)
