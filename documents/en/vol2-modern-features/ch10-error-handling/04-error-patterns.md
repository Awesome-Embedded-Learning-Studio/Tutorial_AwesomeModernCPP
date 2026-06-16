---
chapter: 10
cpp_standard:
- 11
- 14
- 17
- 20
- 23
description: Comprehensive comparison of all error handling approaches, providing
  scenario-based selection guides
difficulty: intermediate
order: 4
platform: host
prerequisites:
- 'Chapter 10: 错误处理的演进'
- 'Chapter 10: optional 用于错误处理'
- 'Chapter 10: std::expected'
reading_time_minutes: 13
related:
- RAII 深入理解
tags:
- host
- cpp-modern
- intermediate
- 类型安全
title: 'Error Handling Patterns Summary: Selection Guide and Best Practices'
translation:
  source: documents/vol2-modern-features/ch10-error-handling/04-error-patterns.md
  source_hash: 989837e583a83323be37f971a50093d0aa2ab9ab6bef794e47518c6b2e2bd0a7
  translated_at: '2026-06-16T04:00:06.057709+00:00'
  engine: anthropic
  token_count: 2743
---
# Error Handling Patterns Summary: A Selection Guide and Best Practices

Building on the previous three articles, we have discussed the pros and cons of error codes, exceptions, `optional`, and `variant`. This article serves as the conclusion to our error handling theme—we will put all the approaches together for a comprehensive comparison, provide a practical selection guide, and share some best practices summarized from real-world "pitfalls."

Additionally, we will cover some topics that weren't fully expanded upon earlier: combinator patterns commonly used in functional error handling, macro-assisted error propagation techniques, and error conversion strategies at the C API boundary.

------

## Comprehensive Comparison

Let's put the key metrics of all approaches side-by-side. This table is important—consider bookmarking it:

| Metric | Enum/Error Codes | Exceptions | optional | variant | expected |
|--------|------------------|------------|----------|---------|----------|
| **Error Information** | Enum value | Rich (exception object) | None | Limited (holds which type) | Rich (custom E) |
| **Ignorability** | Easy to ignore | Hard to ignore | Ignorable | Ignorable | Ignorable |
| **Happy Path Overhead** | Zero | Zero | Minimal | Small | Small |
| **Failure Path Overhead** | Zero | Heavy | Zero | Zero | Zero |
| **Composability** | Poor (manual propagation) | Good (automatic propagation) | Good (C++23 monadic) | Poor (verbose `visit`) | Good (native monadic) |
| **Control Flow Transparency** | High (explicit check) | Low (invisible jumps) | High | Medium | High |
| **Embedded Availability** | Fully available | Usually disabled | Fully available | Fully available | Fully available |
| **Requires RTTI** | No | Yes | No | No | No |
| **C++ Standard Requirement** | C++98 | C++98 | C++17 | C++17 | C++23 |

The "Ignorability" metric in the table deserves a closer look. Unlike Rust's `#[must_use]` compiler enforcement (although C++17 has the `[[nodiscard]]` attribute, the standard library doesn't apply it to `std::optional` or `std::expected`), C++ does not enforce checking return values. Therefore, whether using error codes or `expected`, callers might ignore the return value—this must be mitigated through code review and static analysis tools.

------

## Selection Guide

Based on actual project experience, I have summarized a decision flow to help you choose the appropriate scheme for specific scenarios.

### Decision Tree

**Step 1: Is the error "recoverable"?**

If the error implies a serious bug in the program logic (like null pointer dereference, array out-of-bounds), or the system is in an unrecoverable state (memory exhaustion, stack overflow), we should use `assert` or terminate the program directly. Such errors should not be handled by any "return value" scheme, as the caller cannot reasonably recover.

**Step 2: Are you running in an environment that allows exceptions?**

If the environment allows exceptions (host applications, servers), and the error frequency is very low (exceptions are, by definition, "unusual situations"), exceptions are the best choice—clean code, automatic RAII cleanup, and impossible to forget to handle. Embedded environments or performance-sensitive hot paths usually disable exceptions, so proceed to Step 3.

**Step 3: Does the caller need to know the reason for failure?**

If not—for example, a lookup operation only cares "found or not", or a cache only cares "hit or miss"—use `optional`. It's simple, lightweight, and semantically clear.

If yes—for example, file operations need to distinguish between "file not found" and "permission denied", or network requests need to distinguish between "timeout" and "connection refused"—use `expected`.

**Step 4: Does your compiler support C++23?**

If yes, use `std::expected` directly and enjoy native monadic operations. If you are still on C++17, use a simplified self-implemented version of `expected`, or use an enum + struct approach.

### Scenario-Based Recommendations

I have organized a recommendation list based on common scenarios:

| Scenario | Recommendation | Rationale |
|----------|----------------|-----------|
| Lookup/Search | `optional` | Only care about existence, not the reason |
| Cache Hit | `optional` | Same as above |
| User Input Validation | `expected` | Need to tell the user what went wrong |
| Config File Parsing | `expected` | Need to distinguish "file not found" vs "format error" |
| Network IO | `expected` | Need to distinguish timeout, refused, DNS failure, etc. |
| File IO | `expected` | Need to distinguish not found, permission, disk full, etc. |
| Database Query | `expected` | Need to distinguish connection failure, syntax error, no results, etc. |
| Constructor Failure | Exception | Constructors have no return values |
| Unrecoverable Errors | `assert` / Terminate | Should not attempt recovery |
| High-Frequency Interrupt/Signal Handling | Error Codes | Extremely low overhead, deterministic execution time |
| Cross C/C++ Boundary | Error Codes | C doesn't understand C++ types |

------

## Performance Comparison

Performance is a concern for many. Here is a simplified analysis to help you make decisions in performance-sensitive scenarios.

Compared to raw error codes, the additional overhead of `expected` mainly comes from two sources: type construction—`expected` needs to store a flag (success/failure) and storage space for `T` or `E`; and moving/copying—the error object might be moved multiple times during propagation.

At `-O2` optimization levels, most of this overhead is inlined and optimized away by the compiler. The assembly of a function returning `expected` is virtually indistinguishable from one returning an enum error code—because the compiler can optimize the flag into one register and the error enum value into another.

The real performance difference comes with types like `expected<std::string, std::string>`—where both value and error types might involve heap allocation. In this case, every propagation moves the `std::string`'s contents. If your operation chain is long (e.g., more than 5 steps), it is recommended to use lightweight error types (enums, small structs, `std::string_view`).

The performance model for exceptions is completely different. On the "happy path," exception overhead is near zero (modern compilers use the "zero-cost exception handling" model). However, when throwing an exception, the cost of stack unwinding is huge—requiring traversal of stack frames, searching for catch blocks, and destroying local objects. This means exceptions are not suitable for "failures that are expected to occur frequently"—if 10% of your HTTP service requests time out, using exceptions to handle timeouts is a terrible choice.

------

## Functional Error Handling Patterns

The core idea of functional error handling is: **errors are values, not control flow surprises**. Through combinator patterns, error propagation and transformation become predictable and composable.

### TRY Macro: Simulating Rust's `?` Operator

C++ doesn't have a built-in `?` operator, but we can simulate it with a macro. This macro is very useful in functional-style error handling:

```cpp
#define TRY(x) \
    ({ \
        auto _res = (x); \
        if (!_res) return std::unexpected(_res.error()); \
        std::move(*_res); \
    })

Result<FileContent> read_file(const std::string& path) {
    auto file = TRY(open_file(path));
    auto size = TRY(get_file_size(file));
    return read_content(file, size);
}
```

Compare this to the manual check version without the macro:

```cpp
Result<FileContent> read_file(const std::string& path) {
    auto file = open_file(path);
    if (!file) return std::unexpected(file.error());

    auto size = get_file_size(*file);
    if (!size) return std::unexpected(size.error());

    return read_content(*file, *size);
}
```

The macro version is much more concise and semantically clear—`TRY` means "try this step, give up if it fails." Note that this macro uses GCC/Clang's statement expression syntax; MSVC requires a different implementation.

For compilers that do not support statement expressions, a slightly more verbose but portable version can be used:

```cpp
#define TRY(x) \
    auto _res_##__LINE__ = (x); \
    if (!_res_##__LINE__) return std::unexpected(_res_##__LINE__.error()); \
    *_res_##__LINE__
```

### Error Recovery and Retry

Functional style also makes it easy to implement retry logic. A generic retry wrapper:

```cpp
template <typename F, typename E>
auto retry(int times, F func) -> std::invoke_result_t<F> {
    using ResultType = std::invoke_result_t<F>;

    for (int i = 0; i < times - 1; ++i) {
        auto res = func();
        if (res) return res; // Success
    }
    return func(); // Last attempt
}
```

### Error Aggregation

Sometimes you want to collect all errors to report together, rather than returning on the first one. For example, form validation—a user submits a form, and multiple fields might have issues simultaneously. It's much better to tell the user everything at once rather than making them fix it one by one:

```cpp
std::vector<Error> validate_form(const Form& form) {
    std::vector<Error> errors;

    if (!valid_email(form.email)) errors.push_back(Error::InvalidEmail);
    if (!valid_phone(form.phone)) errors.push_back(Error::InvalidPhone);
    if (!valid_age(form.age))   errors.push_back(Error::InvalidAge);

    return errors;
}
```

------

## Boundary Handling with C APIs

In embedded development, we often need to work with C APIs. C APIs typically use integer error codes, while our C++ code uses `expected`. Perform a one-time conversion at the boundary, then use C++ style internally:

```cpp
// C API wrapper
expected<FileHandle, Error> open_file(const char* path) {
    int fd = c_api_open(path);
    if (fd < 0) {
        return std::unexpected(map_errno_to_error(errno));
    }
    return FileHandle{fd};
}
```

The key principle is: **perform a one-time conversion at the C/C++ boundary, use C++ style internally**. This maintains compatibility with the C ecosystem while keeping the C++ code clean.

------

## Best Practices

Finally, here are some best practices summarized from actual projects—every one learned the hard way.

### 1. Choose One Scheme and Stick to It

Mixing multiple error handling styles is the biggest source of code confusion. If the team decides to use `expected`, use `expected` everywhere; if you decide on error codes, use error codes everywhere. Don't have one function return `expected`, another throw exceptions, and a third use output parameters—the caller has to check the documentation every time to know how to handle errors.

### 2. Keep Error Types Lightweight

The `E` in `expected<T, E>` should be as lightweight as possible—enums, small structs, or `std::string_view`. Avoid using `std::string` or structs containing heap-allocated members as error types, because during error propagation, the error object might be copied or moved multiple times. If your error type needs to carry complex information, consider using error codes + an error message lookup table.

### 3. Use `[[nodiscard]]` to Enforce Return Value Checking

Although the standard library doesn't add `[[nodiscard]]` to `std::optional` or `std::expected`, you can add it to your own return types:

```cpp
template <typename T, typename E>
class [[nodiscard]] expected { /* ... */ };
```

This way, if the caller ignores the return value, the compiler will issue a warning. While not as strict as Rust's `#[must_use]`, it's better than nothing.

### 4. Don't Store Exceptions in `expected`'s `E`

`expected<T, std::exception_ptr>` looks tempting—it avoids exception overhead while retaining rich exception information. However, this actually makes `expected` heavy, and you need to re-throw the exception at the final handling point to extract the info. A better approach is to define a lightweight error type.

### 5. Error Handling Should Have Layers

Low-level functions use simple error types (enums), middle layers enhance error information during propagation (adding context), and the top layer does final logging and user prompting. This keeps the low level generic and the top level informative:

```cpp
// Low level
enum class FsError { NotFound, PermissionDenied, DiskFull };

// Middle layer
expected<Data, Error> read_config() {
    auto file = TRY(open_file("config.toml"));
    // ... adds context like "while reading config"
}

// Top level
if (auto res = read_config(); !res) {
    log_error("Failed to read config: {}", res.error().message);
}
```

### 6. Use Error Codes for Performance-Sensitive Hot Paths

In scenarios like high-frequency interrupt handling, signal processing, or real-time sampling, the construction and movement overhead of `expected` (though small) might be unacceptable. In these cases, use the simplest error codes and global error states to push performance to the limit.

### 7. Use Assertions for Impossible Situations

`assert` is for checking program logic invariants—if an assertion fails, it indicates a bug in the code. Don't use `assert` to check external input (user input, file content, network data), because external input is "expected to fail," not "impossible." Use `expected` / error codes for the former, and `assert` for the latter.

------

## Summary

There is no silver bullet for error handling. Error codes are simple and crude; exceptions are elegant but heavy; `optional` is lightweight but information-free; `expected` is currently the most balanced solution but requires C++23 (or self-implementation). When choosing a scheme, consider environmental constraints (can exceptions be used?), performance requirements (are there hot paths?), and team preference (is the style unified?).

My recommended strategy is: **default to `expected`, use `optional` for lookup/cache scenarios, use exceptions/termination for constructors and unrecoverable errors, and perform one-time conversion at C API boundaries**. You can keep multiple tools in your toolbox, but you must know when to use which one.

With this, Chapter 10 on Error Handling is complete. In the next article, we enter Chapter 11 to discuss user-defined literals—an interesting mechanism that makes code more intuitive and safe.

## Reference Resources

- [cppreference: Error handling](https://en.cppreference.com/w/cpp/error)
- [C++ Core Guidelines: Error handling](https://isocpp.org/wiki/faq/exceptions)
- [P2505R5 - Monadic Functions for std::expected](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2022/p2505r1.html)
