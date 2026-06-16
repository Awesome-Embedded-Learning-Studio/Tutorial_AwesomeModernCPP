---
chapter: 10
cpp_standard:
- 11
- 14
- 17
- 20
description: 'Comparing error handling strategies: exceptions, error codes, optional,
  and expected'
difficulty: intermediate
order: 3
platform: host
prerequisites:
- 异常安全
reading_time_minutes: 15
tags:
- cpp-modern
- host
- intermediate
- 进阶
title: Comparison of Error Handling Approaches
translation:
  source: documents/vol1-fundamentals/ch10/03-error-handling-comparison.md
  source_hash: 4b1df1b29e50a5f938cc7c639dce92b8e57c6096c72f16d0b9f4f1caa627d30b
  translated_at: '2026-06-16T03:46:32.005530+00:00'
  engine: anthropic
  token_count: 2669
---
# Comparing Error Handling Strategies

The C++ language provides us with more error handling tools than most languages. In the C era, we only had return values and `errno`; Java and C# rely almost entirely on exceptions; Rust provides `Result` and `?` operators. C++? It has them all. Error codes, exceptions, `std::optional`, `std::expected`—the toolbox is fully stocked. Having many options isn't a bad thing, but if we don't understand the design intent and trade-offs of each tool, it's easy to write code with mixed styles: in the same project, some functions return `std::error_code`, some throw exceptions, and some return `std::optional`. The caller has to consult the documentation every time to know how to handle errors.

In this article, we take a high-level perspective to compare several major error handling strategies in C++. Our goal is not to argue about "which is best"—that debate is usually meaningless—but to clarify which scenarios suit which method, which don't, and how to make choices in actual projects. We will start from the oldest error codes, work our way through to C++23's `std::expected`, and finally provide a practical decision guide.

## Starting with Error Codes: Simple but Unsafe

Error codes are a legacy solution from the C language era and are the first error handling method every C++ programmer encounters. The principle is very direct: a function tells you if it succeeded or failed through its return value, usually using `0` to indicate success and a negative number for an error, or using a set of `enum` or `std::error_code` to distinguish different error types.

```cpp
// Traditional C style error code
int divide(int a, int b, int& result) {
    if (b == 0) {
        return -1; // Error: Division by zero
    }
    result = a / b;
    return 0; // Success
}
```

The advantage of error codes lies in their **predictability**—the control flow doesn't suddenly jump away; every line of code executes in order, and you can see at a glance from the function signature which errors it might return. Moreover, it has zero overhead: no exception tables, no stack unwinding, and no runtime support required.

But error codes have a fatal problem: **the caller can choose to ignore it**. The `divide` function above returns an `int`. If the caller doesn't check the return value at all, the compiler won't complain, and the program will still run—only the result might be wrong. In a large project, failing to check error codes is almost inevitable. Even worse, error codes can only convey "what error happened" and cannot carry rich contextual information (like file paths, failed argument values) unless you define extra structures or use output parameters, which makes the code bloated.

> **Warning**: If your function returns an error code but the caller doesn't check it, the error is **silently swallowed**. This type of bug is extremely hard to track—the program won't crash, won't report an error, it will just silently produce an incorrect result. In embedded systems, this kind of "silent error" can cause abnormal hardware behavior, and you won't have any idea where the problem is.

## Exceptions: Can't be Ignored but Comes at a Cost

C++ exceptions solve the "error ignored" problem at the language level. A `throw` statement interrupts the normal execution flow and searches up the call stack for a matching `catch` block. If you don't catch it, the program calls `std::terminate`—you cannot pretend you didn't see it.

```cpp
// Exception handling
double divide(int a, int b) {
    if (b == 0) {
        throw std::invalid_argument("Division by zero");
    }
    return static_cast<double>(a) / b;
}

void try_divide() {
    try {
        auto res = divide(10, 0);
        std::cout << "Result: " << res << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
}
```

The strength of exceptions is that they bind "error information" with "control flow"—you cannot catch an exception and then not handle it (without re-throwing). Also, exceptions can carry arbitrarily rich information (via derived classes of `std::exception`). After a low-level function throws an exception, the top level can uniformly catch and handle it, while intermediate layers don't need to care.

But exceptions also have several non-negligible issues. The first is **performance overhead**: although the overhead of the "Happy path" (when no exception occurs) is very small on modern compilers (zero-cost model), once an exception is thrown, the overhead of stack unwinding is considerable—local objects must be destructed frame by frame, and matching `catch` blocks must be located. The second is **opaque control flow**: looking at the function signature, you have no idea if it will throw an exception or what it might throw. C++11 introduced `noexcept` and `throw()`, but dynamic exception specifications like `throw(type)` were removed in C++17. Now only the `noexcept` keyword remains—it only tells you "this function guarantees not to throw," but there is no language-level constraint for "what might be thrown."

The third, and most practical issue: **many embedded toolchains simply do not support exceptions**. The `-fno-exceptions` option in GCC and Clang completely disables the exception mechanism; if a `throw` statement is present, the linker will error out. On resource-constrained MCUs, the code size overhead of exceptions (exception tables, RTTI) is often unacceptable. This leads to a fragmented status quo: desktop and server-side C++ use exceptions heavily, while embedded C++ basically doesn't—same language, two different styles.

## std::optional: Is it There or Not?

C++17 introduced `std::optional`, which expresses a very simple concept: this value **might exist, or it might not**. Unlike error codes, `std::optional` is part of the type system—the function signature `std::optional<int> find_id(...)` explicitly tells you "the return value might be missing," and the caller must face this fact.

```cpp
#include <optional>
#include <iostream>

std::optional<int> safe_divide(int a, int b) {
    if (b == 0) {
        return std::nullopt; // Indicate failure
    }
    return a / b;
}

void test_optional() {
    auto result = safe_divide(10, 0);
    if (result.has_value()) {
        std::cout << "Result: " << result.value() << std::endl;
    } else {
        std::cout << "Division failed." << std::endl;
    }
}
```

The benefit of `std::optional` is that it is **lightweight and explicit**. It forces the caller to handle the "value is missing" case at the type level—if you directly call `.value()` without checking `.has_value()`, it will throw `std::bad_optional_access` (yes, it still uses exceptions internally). You can also use `*result` to skip the check and access directly, but if the value is empty, that is undefined behavior.

The problem with `std::optional` is: it can only tell you "it failed," but **not why it failed**. Division by zero is one kind of failure, overflow is another, and invalid arguments are a third—but `std::optional` treats all these the same, returning `std::nullopt` for everything. If you need to distinguish between different error types, `std::optional` isn't enough.

Scenarios suitable for `std::optional` are those where there is only one kind of error ("not found", "does not exist"), and the caller doesn't need to know the specific reason. For example, finding an element in a container: `find` returns `std::nullopt` if not found. If your API is designed to return `std::optional<T>`, the semantics are clear—found means the value, not found means empty, simple and clear.

## std::expected: Wanting Both Value and Reason

`std::expected` is a type introduced in C++23 that combines the type safety of `std::optional` with the rich error information of exceptions. Simply put, `std::expected<T, E>` either contains a successful value `T` or an error `E`—and this error can be any type you define.

```cpp
#include <expected>
#include <iostream>

enum class MathError {
    DivisionByZero,
    Overflow
};

std::expected<int, MathError> safe_divide(int a, int b) {
    if (b == 0) {
        return std::unexpected(MathError::DivisionByZero);
    }
    // Overflow check omitted for brevity
    return a / b;
}

void test_expected() {
    auto result = safe_divide(10, 0);
    if (result) {
        std::cout << "Result: " << result.value() << std::endl;
    } else {
        switch (result.error()) {
            case MathError::DivisionByZero:
                std::cout << "Error: Division by zero" << std::endl;
                break;
            case MathError::Overflow:
                std::cout << "Error: Integer overflow" << std::endl;
                break;
        }
    }
}
```

The biggest difference between `std::expected` and `std::optional` is that when failure occurs, `std::expected` can tell you **why it failed**. The error type `E` can be an enum, a struct, `std::string`—any type that carries enough information. This allows the caller to adopt different recovery strategies based on the error type, instead of facing a hollow "it failed."

C++23 also provides a set of monadic operations for `std::expected`, allowing us to chain multiple operations that might fail: `.and_then()` continues to the next step on success, `.transform()` converts the value type on success, and `.or_else()` attempts recovery on failure. These operations automatically skip subsequent steps on error, directly propagating the error value—similar to Rust's `?` operator, though the syntax is not as concise.

However, `std::expected` also has its costs. Before the C++23 standard is fully finalized, support in mainstream compilers is not yet complete (GCC 12+, MSVC 19.34+ support basic features, Clang's support is lagging). If your project is still using C++17 or earlier, you can use a third-party library (like `tl::expected`) as a substitute—the interface is basically the same, and migration costs are very low.

> **Warning**: The `.value()` method of `std::expected` throws a `std::bad_expected_access` exception when the value is empty. If your reason for choosing `std::expected` is "no exceptions," remember to check with `.has_value()` first, or use `*` to dereference (UB if empty, but won't throw). Mixing `std::expected` with exception handling is a subtle style inconsistency that is easily overlooked.

## A Head-to-Head Comparison of Four Strategies

Let's compare the key attributes of the four error handling methods. The table below is our core reference when making choices:

| Feature | Error Codes | Exceptions | `std::optional` | `std::expected` |
|---------|-------------|------------|----------------|-----------------|
| Can be ignored | Yes (Biggest issue) | No | Yes (Type system warns you) | Yes (Type system warns you) |
| Carries error info | Needs extra mechanism | Native support | None (Just has/has not) | Yes, custom error type |
| Performance overhead | Zero | Stack unwinding cost | Minimal | Minimal |
| Embedded availability | Fully available | Mostly disabled | Fully available | Fully available (C++23) |
| Call stack unwinding | No | Yes | No | No |
| Standard requirement | C language | C++ (Must enable) | C++17 | C++23 |

From this table, we can see a clear divide. The fundamental difference between exceptions and the other three methods lies in the **control flow model**: exceptions are non-local jumps, while error codes / `std::optional` / `std::expected` are all local value passing. This distinction determines their applicable scenarios.

In actual projects, our choice logic is roughly this: if the project allows exceptions (desktop/server applications), use exceptions for "unrecoverable, unexpected" errors, and use `std::expected` or `std::optional` for "expected, caller-needs-to-handle" errors. If the project disables exceptions (embedded, game engines, real-time systems), then use only error codes and `std::optional` / `std::expected`, and ensure explicit handling logic exists on all error paths. **The worst situation is mixing multiple methods without a unified convention**—that makes the error handling of the entire codebase a mess.

## In Action: Three Ways to Write Safe Division

Now let's use a complete example program to put the three "non-exception" error handling methods together—same functionality (safe integer division), implemented with error codes, `std::optional`, and `std::expected` respectively, and tested uniformly in `main`.

```cpp
#include <iostream>
#include <optional>
#include <expected>

// 1. Error Code Implementation
enum class DivErrCode {
    OK,
    DivByZero
};

DivErrCode divide_ec(int a, int b, int& out) {
    if (b == 0) return DivErrCode::DivByZero;
    out = a / b;
    return DivErrCode::OK;
}

// 2. std::optional Implementation
std::optional<int> divide_opt(int a, int b) {
    if (b == 0) return std::nullopt;
    return a / b;
}

// 3. std::expected Implementation
enum class DivError {
    DivByZero
};

std::expected<int, DivError> divide_exp(int a, int b) {
    if (b == 0) return std::unexpected(DivError::DivByZero);
    return a / b;
}

int main() {
    // Test Case 1: Success
    int a = 10, b = 2;

    // Error Code
    int res_ec;
    auto ec = divide_ec(a, b, res_ec);
    if (ec == DivErrCode::OK) std::cout << "EC Result: " << res_ec << std::endl;
    else std::cout << "EC Error" << std::endl;

    // Optional
    auto res_opt = divide_opt(a, b);
    if (res_opt) std::cout << "Opt Result: " << *res_opt << std::endl;
    else std::cout << "Opt Error" << std::endl;

    // Expected
    auto res_exp = divide_exp(a, b);
    if (res_exp) std::cout << "Exp Result: " << *res_exp << std::endl;
    else std::cout << "Exp Error: " << static_cast<int>(res_exp.error()) << std::endl;

    std::cout << "---" << std::endl;

    // Test Case 2: Failure (Divide by zero)
    int c = 10, d = 0;

    // Error Code
    int res_ec2;
    auto ec2 = divide_ec(c, d, res_ec2);
    if (ec2 == DivErrCode::OK) std::cout << "EC Result: " << res_ec2 << std::endl;
    else std::cout << "EC Error: DivByZero" << std::endl;

    // Optional
    auto res_opt2 = divide_opt(c, d);
    if (res_opt2) std::cout << "Opt Result: " << *res_opt2 << std::endl;
    else std::cout << "Opt Error: nullopt" << std::endl;

    // Expected
    auto res_exp2 = divide_exp(c, d);
    if (res_exp2) std::cout << "Exp Result: " << *res_exp2 << std::endl;
    else std::cout << "Exp Error: " << static_cast<int>(res_exp2.error()) << std::endl;

    return 0;
}
```

Compile and run:

```bash
g++ -std=c++23 -o error_demo error_demo.cpp
./error_demo
```

If your compiler doesn't fully support `std::expected` yet, you can temporarily switch the standard to C++20 and use the `tl/expected.hpp` header library as a substitute. On GCC 13+ and MSVC 19.34+, the code above compiles directly.

Expected output:

```text
EC Result: 5
Opt Result: 5
Exp Result: 5
---
EC Error: DivByZero
Opt Error: nullopt
Exp Error: 0
```

Three test cases, three implementation methods, results are completely consistent—but "consistent" is only on the surface. Notice the error case `divide(10, 0)`: the error code version outputs a string "DivByZero", the `std::optional` version can only say "nullopt", while the `std::expected` version gives the specific `DivError` enum value. In such a simple example, the difference isn't huge, but imagine if a function had five different failure modes—`std::optional` would be completely powerless—it can't tell you which failure occurred.

> **Warning**: In the three methods above, the error code version `divide_ec` has a trap that is easily overlooked—if the caller doesn't check the return value and uses `out` directly, the value of `out` on the error path is uninitialized (we initialized it in the test code, but in real code, output parameters are often forgotten to be initialized). `std::optional` and `std::expected` are safer in this regard: if you don't check `has_value()` and call `value()`, it throws an exception or causes UB, at least preventing you from continuing with a garbage value.

## Exercises

### Exercise 1: Extend Error Types

Add an `Overflow` error type to the `std::expected` version above. Hint: In `divide`, if `a == INT_MIN` and `b == -1`, it causes overflow in two's complement representation (result exceeds the range of `int`). Handle this additional error condition in all three implementations and add corresponding test cases.

### Exercise 2: File Reading Error Handling

Assume you have a function `read_config`, which might fail for three reasons: file not found, permission denied, or read timeout. Design the interface for this function using `std::optional` and `std::expected` respectively (no need to implement the logic, just design the signature and error types), and compare the expressive power of the two solutions.

### Exercise 3: Error Propagation Chain

Use `std::expected` to implement a simple parsing chain: `parse_header` -> `validate_checksum` -> `deserialize_payload`. Each function returns `std::expected<T, Error>`. Write a complete call chain in `main` to ensure any failure in any step is correctly propagated to the top level with clear error information.

## Summary

At this point, we have gone through four mainstream error handling methods in C++—error codes, exceptions, `std::optional`, and `std::expected`. Error codes are the oldest and simplest but too easily ignored; exceptions guarantee "errors cannot be ignored" at the language level but at the cost of runtime overhead and unavailability in embedded scenarios; `std::optional` is lightweight and elegant but can only express "has or has not," unable to convey "why not"; `std::expected` is currently the most comprehensive solution, offering type-safe value passing and rich error information, though it requires C++23 support.

There is no absolute right or wrong in choosing which method, the key is to maintain consistency at the project level. In desktop and server projects where exceptions are allowed, exceptions handle "unexpected, unrecoverable" errors, `std::expected` handles "expected, needs recovery" errors, and `std::optional` handles simple "not found, doesn't exist" cases. In embedded projects where exceptions are disabled, error codes are used for minimal scenarios and high-frequency paths, while `std::optional` and `std::expected` take on most error handling duties. Regardless of the choice, **the most important thing is for the whole team to reach a consensus on "when to use what"**, rather than letting everyone choose based on intuition.

Chapter 10 concludes here. We discussed the basic mechanism of exceptions, the four levels of exception safety, the RAII guard pattern, and today's grand comparison of error handling strategies. With this knowledge, we have a solid error handling toolbox. Next, in Chapter 11, we will enter a brand new domain—the Standard Template Library (STL). Starting with `std::vector`, we will gradually get to know a series of powerful containers and algorithms provided by the C++ standard library, allowing us to stop reinventing the wheel.
