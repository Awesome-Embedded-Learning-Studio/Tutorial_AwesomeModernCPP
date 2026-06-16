---
chapter: 10
cpp_standard:
- 11
- 14
- 17
- 20
- 23
description: 'Error codes, exceptions, optional, expected: the evolution and selection
  of error handling strategies'
difficulty: intermediate
order: 1
platform: host
prerequisites:
- 'Chapter 4: std::optional'
- 'Chapter 4: std::variant'
reading_time_minutes: 13
related:
- optional 用于错误处理
- std::expected
tags:
- host
- cpp-modern
- intermediate
- 类型安全
title: 'Evolution of Error Handling: From Error Codes to Type Safety'
translation:
  source: documents/vol2-modern-features/ch10-error-handling/01-error-handling-evolution.md
  source_hash: 9c83ba70a048ce336a53546deb895c6c5e5814214aa29c41a0169545c8b65852
  translated_at: '2026-06-16T03:59:23.433667+00:00'
  engine: anthropic
  token_count: 2460
---
# Evolution of Error Handling: From Error Codes to Type Safety

Having written C++ for many years, one thing stands out the most: **error handling is always the hardest part to get right in a project**. It's not because it's complex—precisely because it looks too simple. Many people think `errno` or `try-catch` is enough, but when you reach the maintenance phase, you find unhandled errors everywhere, swallowed exceptions, and function calls failing for unknown reasons.

In this chapter, we will thoroughly review the evolution of C++ error handling: from C-style error codes to C++ exceptions, then to C++17's `std::optional` / `std::variant`, and finally to C++23's `std::expected`. Only by understanding what problems each solution solves and what new problems it introduces can we make reasonable choices when facing specific scenarios.

------

## The Starting Point: C-Style Error Codes

If you have written C, or maintained large C legacy projects, the following code will look familiar:

```cpp
FILE* fp = fopen("log.txt", "r");
if (!fp) {
    // Handle error
}

char buffer[1024];
if (fgets(buffer, sizeof(buffer), fp) == NULL) {
    // Handle error, but wait, did we fclose(fp) here?
}
```

The problem with this style isn't "can it be used," but **can the code written with it run reliably**.

The first problem is **ignorability**. An error code is a normal `int`. The caller can completely ignore the return value, and the compiler won't give any warning. I have seen too much code where a function returns an error code, the caller ignores it and continues execution, and finally the program crashes in a weird way—and the location of the error might be a dozen function calls away from the crash.

The second problem is **lack of information**. What can an `int` tell you? File not found? Permission denied? Disk full? You have to look at the documentation or macro definitions in the header file, and pray that the function's documentation is the latest version. Worse, different modules might use the same integer to represent different meanings; `-1` might mean "file not found" in module A, but "timeout" in module B.

The third problem is **global state dependency**. The classic C standard library `errno` mechanism is an example—it is a global variable. If you forget to save `errno` between two function calls, its value is overwritten. In multi-threaded environments, this is a disaster. Although modern implementations use thread-local storage, the mental burden remains significant.

The fourth problem is **risk of resource leaks**. The code above has only one step, so the placement of `fclose` is relatively clear. But if you have five steps that might fail, each step must correctly clean up resources allocated previously before exiting—the `goto cleanup` pattern was born for this. While it works, the code reads like spaghetti.

------

## Phase Two: C++ Exception Mechanism

C++ introduced the exception mechanism to solve the core pain points of error codes—separating error handling from control flow, so that "happy path" code is not interrupted by error checks:

```cpp
void process_data(const std::string& path) {
    std::ifstream file(path); // May throw std::ifstream::failure
    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>()); // May throw

    auto result = parse_json(content); // May throw
    // ... more operations
}
```

Exceptions solve many problems: the happy path code becomes clear, errors are not silently ignored (uncaught exceptions terminate the program), and RAII配合 stack unwinding can automatically clean up resources. In application layer development, exceptions are a quite handy tool.

But exceptions also have their problems, and some are fatal in specific scenarios.

The first is **performance uncertainty**. The performance overhead of exceptions on the "happy path" (when no exception is thrown) is almost zero—this is the design goal of zero-overhead abstraction. But once an exception is thrown, the overhead of stack unwinding is huge, involving stack frame traversal, destructor calls, exception object copying, etc. This isn't an issue for "occasional errors," but if your network service handles 100,000 requests per second and 5% fail, using exceptions to handle these "expected failures" isn't appropriate.

The second is **opaque control flow**. Looking at the `process_data` code above, can you tell at a glance what exceptions `ifstream` constructor or `parse_json` might throw? Probably not, unless you read the documentation or function implementation carefully. C++ exceptions are "invisible"—function signatures don't annotate what they might throw (the `exception specification` specification was removed in C++17, `noexcept` acts as a specifier to promise not to throw, but cannot annotate what types might be thrown).

The third, and most critical point—**embedded environments usually disable exceptions**. The exception mechanism requires runtime support (stack unwinding information, RTTI, etc.), which increases binary size. On many embedded platforms, `-fno-exceptions` is the default option, meaning you can't use `try` / `catch` at all. The GNU ARM toolchain generates code with exception support that is 50KB to 200KB larger than code without it. On an MCU with only 64KB of Flash, this overhead is fatal.

Finally, there is the complexity of **exception safety**. Writing exception-safe code requires a deep understanding of RAII, strong exception guarantees, basic exception guarantees, etc. If an exception is thrown in a constructor, the object might be in a semi-constructed state; if an `iterator` throws, the container might be in a semi-modified state. This isn't the fault of the exception mechanism, but it does increase the mental burden.

------

## Phase Three: Error Codes + Enums Improvement

Since exceptions are unavailable in some scenarios, we return to the error code approach, but use the C++ type system to make up for its shortcomings:

```cpp
enum class ErrorCode {
    Ok,
    FileNotFound,
    PermissionDenied,
    // ...
};

struct Result {
    ErrorCode code;
    std::string message; // Heap allocation!
};

Result open_file(const std::string& path) {
    if (!exists(path)) {
        return { ErrorCode::FileNotFound, "File missing" };
    }
    // ...
}

auto res = open_file("data.txt");
// Oops, forgot to check res.code!
```

Using `enum class` instead of macros or naked `int` to represent error codes is already a significant improvement—type safety, namespace isolation, and IDE completion friendly. Adding `std::string` for additional information, the caller can finally know exactly what went wrong.

But the core problem remains: **the compiler does not force you to check the return value**. `Result` is still a normal struct. If you don't call `.code`, the program will continue running, using the uninitialized `res` object for subsequent operations. Also, `std::string` in `Result` implies heap allocation, which in embedded environments might not be what you want.

------

## Phase Four: Type-Safe Error Types

C++17 introduced `std::optional` and `std::variant`, and C++23 introduced `std::expected`. They re-examine error handling from the type system level. The core idea is: **make "possible failure" part of the type, and let the compiler check it, rather than relying on programmer discipline**.

### std::optional: Success or No Value

```cpp
std::optional<User> find_user(UserID id);

auto user = find_user(42);
if (user) {
    // Success
} else {
    // Failed, but we don't know why
}
```

`std::optional` is suitable for expressing simple scenarios where "success returns a value, failure returns no value." Its advantage is clear semantics—`std::optional` makes it clear at a glance that "there might be no value here," which is much clearer than returning a pointer or error code.

But `std::optional` cannot carry the cause of the error. When `find_user` returns `std::nullopt`, you only know "not found," but you don't know if it's because the ID doesn't exist, the database connection is broken, or permissions are insufficient.

### std::variant: Multi-State Expression

```cpp
using Result = std::variant<Value, Error, Timeout>;

Result fetch_data();
```

`std::variant` can express multiple error types and is more expressive than `std::optional`. But the usage experience is not ideal—every access requires `std::get_if` or `std::visit` plus `std::overloaded`, making the code verbose. Also, error types and success types are mixed in the same `variant`, which is semantically less intuitive than "value or error."

### std::expected: Value or Error

```cpp
std::expected<User, Error> find_user(UserID id);

auto user = find_user(42);
if (user) {
    // Use *user or user.value()
} else {
    // Use user.error()
}
```

`std::expected` has very direct semantics: **success holds a value of type `T`, failure holds an error of type `E`**. It has the simplicity of `std::optional` and can carry error information like `std::variant`. Moreover, C++23's `std::expected` comes with monadic operations (`and_then`, `transform`, `or_else`, etc.), which can elegantly chain multiple operations that might fail—we will cover this in detail in future articles.

------

## Evolution Timeline

Let's use a timeline to summarize the evolution of C++ error handling solutions:

**C Era (1970s)**: Error codes + `errno`. Simple and crude, ignorable, little information.

**C++98 (1998)**: Exception mechanism. Elegant but heavy, requires RTTI support, opaque control flow.

**C++11 (2011)**: `enum class` standardization, providing a more standardized framework for error codes. The `<system_error>` header introduced a cross-platform error classification mechanism.

**C++17 (2017)**: `std::optional` represents "possibly no value," `std::variant` represents "multiple possible types." This is the first step toward type-safe error handling, but neither is specialized enough.

**C++23 (2023)**: `std::expected` officially enters the standard, accompanied by monadic operations. This is the C++ Committee's official endorsement of the "type-safe error handling" path.

------

## Solution Comparison

I have compiled a comparison table to view the characteristics of the four mainstream solutions together:

| Feature | Error Code/Enum | Exception | optional | expected |
|---------|------------------|-----------|----------|----------|
| **Ignorability** | Easy to ignore | Unignorable (uncaught terminates) | Ignorable | Ignorable |
| **Error Info** | Limited (int/enum) | Rich (exception object) | None (only presence) | Rich (custom E) |
| **Performance (Happy Path)** | Almost zero overhead | Almost zero overhead | Almost zero overhead | Almost zero overhead |
| **Performance (Failure Path)** | Zero overhead | Heavy (stack unwinding) | Zero overhead | Zero overhead |
| **Composability** | Poor (manual propagation) | Good (automatic propagation) | Medium | Good (monadic ops) |
| **Code Bloat** | None | Potentially large | Minimal | Small |
| **Embedded Available** | Fully available | Usually disabled | Fully available | Fully available |
| **Compiler Enforced Check** | No | No | No | No |
| **Needs RTTI** | No | Yes | No | No |

A fact worth noting: in C++, standard library types (like `std::optional` and `std::expected`) **are not enforced by the compiler by default, unlike Rust's `Result`**. Rust's `#[must_use]` attribute makes the compiler emit a warning when the caller ignores the `Result`; C++'s `[[nodiscard]]` has similar functionality, but the standard library hasn't added this attribute to these types (this is also a topic of community discussion, see [P2422R1](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p2422r1.html)). However, you can add `[[nodiscard]]` to return types in your own project to get compiler-enforced checking.

------

## Special Considerations for Embedded Scenarios

In embedded development, the choice of error handling is often not a question of "which is better," but "which is usable."

**Disabled exceptions** is the most common constraint in embedded development. The default configuration of ARM compilers is usually `-fno-exceptions`, meaning `try` / `catch` simply cannot compile. So if you are writing embedded code, error codes, `std::optional`, and `std::expected` are basically your main choices.

**Deterministic error handling** is another key requirement. In real-time systems, you cannot accept "uncertain error handling time"—the stack unwinding time of exceptions is unpredictable, which is unacceptable in hard real-time systems. Return value schemes (error codes, `std::optional`, `std::expected`) have deterministic execution times and are more suitable for real-time scenarios.

**Memory overhead** also needs consideration. `std::expected` typically occupies `sizeof(T) + sizeof(E)` plus some alignment padding space. If `E` is a simple enum, the extra overhead is only a few bytes; if `E` contains `std::string`, it introduces heap allocation. On an MCU with only a few dozen KB of RAM, these overheads need careful weighing.

**Practical advice**: For embedded projects, I recommend using lightweight error types (enums or small structs) with `std::expected` semantics, implementing a simplified version of `std::expected` yourself (available in C++17), or directly using the return struct method. In extremely resource-constrained scenarios, you can even revert to enum error codes—but establish team discipline to "always check return values."

------

## Summary

In this chapter, we reviewed the evolution of C++ error handling: from C error codes to C++ exceptions, to C++17/23 type-safe solutions. Each solution has its reasons for existence; there is no silver bullet. In the next three articles, we will dive deep into `std::optional` for error handling, the usage of `std::expected`, and a comprehensive selection guide to help you make the right decisions in actual projects.

## Reference Resources

- [cppreference: Error handling](https://en.cppreference.com/w/cpp/error)
- [P0786R1 - std::expected proposal](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2021/p0786r1.html)
- [C++ Core Guidelines: Error handling](https://isocpp.org/wiki/faq/exceptions)
