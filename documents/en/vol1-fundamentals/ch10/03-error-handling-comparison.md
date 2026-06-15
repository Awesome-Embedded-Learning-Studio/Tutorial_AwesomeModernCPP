---
chapter: 10
cpp_standard:
- 11
- 14
- 17
- 20
description: Comparing exception, error code, optional, and expected error handling
  strategies
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
title: Error Handling Comparison
translation:
  engine: anthropic
  source: documents/vol1-fundamentals/ch10/03-error-handling-comparison.md
  source_hash: aa664f3bed2f905c812a2a0565402e5f2655a3faf1a8baf020bd65934c133b5f
  token_count: 2673
  translated_at: '2026-05-26T10:57:57.860385+00:00'
---
# Comparing Error Handling Strategies

C++ gives us more error handling tools than most languages. In the C era, we only had return values and ``errno``; Java and C# rely almost entirely on exceptions; Rust gives us ``Result<T, E>`` and the ``?`` operator. And C++? It has all of them. Error codes, exceptions, ``std::optional``, ``std::expected``—the toolbox is packed. Having more options isn't a bad thing, but if we don't understand the design intent and trade-offs of each tool, we easily end up with inconsistent code: one function in a project returns ``-1``, another throws an exception, and yet another returns ``std::nullopt``, forcing the caller to consult the documentation every time to figure out how to handle errors.

In this chapter, we take a higher-level perspective and compare the major error handling strategies in C++. Our goal isn't to debate "which one is best"—that kind of debate is usually pointless—but to clarify which scenarios each approach fits, which it doesn't, and how to make choices in real projects. We start with the oldest approach, error codes, work our way up to C++23's ``std::expected``, and finish with a practical decision guide.

## Starting with Error Codes: Simple but Unsafe

Error codes are a legacy from the C era, and they are the first error handling approach every C++ programmer encounters. The principle is straightforward: a function tells you whether it succeeded or failed through its return value, typically using ``0`` for success and negative numbers for errors, or using a set of ``#define``s or ``enum``s to distinguish different error types.

```cpp
int divide(int a, int b, int* result) {
    if (b == 0) {
        return -1;  // 错误码：除零
    }
    *result = a / b;
    return 0;       // 成功
}

// 调用
int quotient = 0;
if (divide(10, 3, &quotient) != 0) {
    // 处理错误
}
```

The advantage of error codes lies in their **predictability**—control flow doesn't suddenly jump away, every line of code executes in sequence, and you can see at a glance from the function signature what errors it might return. Moreover, it has zero overhead: no exception tables, no stack unwinding, and no runtime support required.

But error codes have a fatal flaw: **the caller can choose to ignore them**. The ``divide`` function above returns a ``int``, but if the caller doesn't check the return value at all, the compiler won't complain, and the program will still run—just with potentially incorrect results. In a large project, missing an error code check is almost inevitable. What's worse is that error codes can only convey *what* went wrong, without carrying rich context (like file paths or failed parameter values), unless you define extra structs or use output parameters, which makes the code bloated and unwieldy.

> **Pitfall Warning**: If your function returns an error code but the caller doesn't check it, the error is **silently swallowed**. This type of bug is extremely hard to track down—the program doesn't crash, doesn't report an error, it just silently produces incorrect results. In embedded systems, these "silent errors" can cause abnormal hardware behavior, and you'll have no idea where the problem lies.

## Exceptions: Unignorable but Costly

C++'s exception mechanism solves the "ignored error" problem at the language level. A ``throw`` statement interrupts normal execution flow and searches up the call stack for a matching ``catch`` block. If you don't catch it, the program simply calls ``std::terminate``—you can't pretend you didn't see it.

```cpp
int divide(int a, int b) {
    if (b == 0) {
        throw std::invalid_argument("division by zero");
    }
    return a / b;
}

// 调用者必须处理，否则异常会继续传播
try {
    int result = divide(10, 0);
} catch (const std::invalid_argument& e) {
    std::cout << "Error: " << e.what() << "\n";
}
```

The strength of exceptions is that they bind "error information" and "control flow" together—you can't catch an exception without handling it. Furthermore, exceptions can carry arbitrarily rich information (through derived classes of ``std::exception``). When a low-level function in a deep call stack throws an exception, the top level can catch and handle it uniformly, while the intermediate layers don't need to care at all.

But exceptions also have several issues that cannot be ignored. The first is **performance overhead**: although the "happy path" (when no exception occurs) overhead is already very small on modern compilers (zero-cost model), once an exception is thrown, the overhead of stack unwinding is quite significant—local objects must be destructed frame by frame, and matching `catch` blocks must be located. The second is **opaque control flow**: just by looking at a function signature, you have no idea whether it will throw an exception or what it might throw. C++11 once introduced ``throw()`` and ``noexcept``, but dynamic exception specifications like ``throw(std::invalid_argument)`` were removed in C++17, leaving only the ``noexcept`` keyword—it only tells you "this function guarantees it won't throw," and there is no language-level constraint for "what exceptions it might throw."

The third, and most practical, issue is that **many embedded toolchains don't support exceptions at all**. The ``-fno-exceptions`` option in GCC and Clang completely disables the exception mechanism; once there's a ``throw`` statement, the linker will report an error. On extremely resource-constrained MCUs, the code size overhead of exceptions (exception tables, RTTI) is often unacceptable. This leads to a fragmented status quo: desktop and server C++ heavily uses exceptions, while embedded C++ basically doesn't—the same language, two different styles.

## std::optional: There or Not There

C++17 introduced ``std::optional<T>``, which expresses a very simple concept: this value **might exist, or it might not**. Unlike error codes, ``optional`` is part of the type system—a function signature like ``std::optional<int> divide(int a, int b)`` explicitly tells you "the return value might be absent," and the caller must face this reality.

```cpp
#include <optional>

std::optional<int> safe_divide(int a, int b) {
    if (b == 0) {
        return std::nullopt;  // 除零，返回空
    }
    return a / b;
}

// 调用
auto result = safe_divide(10, 0);
if (result.has_value()) {
    std::cout << "Result: " << result.value() << "\n";
} else {
    std::cout << "Division by zero!\n";
}
```

The benefit of ``std::optional`` is that it is **lightweight and explicit**. It forces the caller at the type level to handle the "value is absent" case—if you directly call ``.value()`` without checking ``has_value()``, it throws a ``std::bad_optional_access`` when the value is empty (yes, it still uses exceptions internally). You can also use ``*result`` to skip the check and access the value directly, but if the value is empty, that is undefined behavior.

The problem with ``std::optional`` is that it can only tell you "it failed," but **not *why* it failed**. Division by zero is one kind of failure, overflow is another, and an invalid parameter is a third—but ``std::optional`` treats all these cases the same, returning ``std::nullopt`` for all of them. If you need to distinguish between different error types, ``optional`` is not enough.

Scenarios suitable for ``optional`` are those where there is only one kind of error ("not found," "does not exist"), and the caller doesn't need to know the specific reason. For example, finding an element in a container: ``std::find_if`` returns ``end()`` when not found, but if your API is designed to return ``std::optional``, the semantics are very clear—found means the value, not found means empty, simple and straightforward.

## std::expected: Value and Reason

``std::expected<T, E>`` is a type introduced in C++23 that combines the type safety of ``std::optional`` with the rich error information of exceptions. Simply put, ``expected<T, E>`` either contains a successful value ``T`` or an error ``E``—and this error can be of any type, entirely defined by you.

```cpp
#include <expected>
#include <string>

enum class DivideError {
    DivisionByZero,
    IntegerOverflow
};

std::expected<int, DivideError> checked_divide(int a, int b) {
    if (b == 0) {
        return std::unexpected(DivideError::DivisionByZero);
    }
    // 简化：暂不处理溢出
    return a / b;
}

// 调用
auto result = checked_divide(10, 0);
if (result.has_value()) {
    std::cout << "Result: " << result.value() << "\n";
} else {
    // 可以根据错误类型做不同处理
    switch (result.error()) {
        case DivideError::DivisionByZero:
            std::cout << "Cannot divide by zero!\n";
            break;
        case DivideError::IntegerOverflow:
            std::cout << "Integer overflow occurred!\n";
            break;
    }
}
```

The biggest difference between ``std::expected`` and ``std::optional`` is that when a failure occurs, ``expected`` can tell you **why it failed**. The error type ``E`` can be an enum, a struct, a ``std::string``—any type that can carry sufficient information. This allows the caller to adopt different recovery strategies based on different error types, instead of facing a hollow "it failed."

C++23 also provides a set of monadic operations for ``std::expected``, allowing us to chain-combine multiple operations that might fail: ``and_then`` continues to the next step on success, ``transform`` transforms the value type on success, and ``or_else`` attempts recovery on failure. These operations automatically skip subsequent steps when an error occurs, directly propagating the error value—similar in concept to Rust's ``?`` operator, just not as syntactically concise.

However, ``std::expected`` also has its costs. Before the C++23 standard was officially finalized, support in mainstream compilers was incomplete (GCC 12+ and MSVC 19.34+ support basic features, while Clang's support lags relatively behind). If your project is still using C++17 or earlier standards, you can use a third-party library (like ``tl::expected``) as a replacement—the interface is basically identical, and the migration cost is very low.

> **Pitfall Warning**: The ``value()`` method of ``std::expected`` throws a ``std::bad_expected_access<E>`` exception when the value is empty. If your original reason for choosing ``expected`` was "not using exceptions," then remember to check with ``has_value()`` first, or use ``*`` to dereference (it's UB when empty, but won't throw an exception). Mixing ``expected`` and exception handling is an easily overlooked style inconsistency.

## A Head-to-Head Comparison of the Four Strategies

Let's put the key attributes of the four error handling approaches side by side. The table below is our core reference when making choices:

| Feature | Error Codes | Exceptions | ``std::optional`` | ``std::expected`` |
|---------|-------------|------------|--------------------|--------------------|
| Can be ignored | Yes (this is the biggest problem) | No | Yes (but the type system reminds you) | Yes (but the type system reminds you) |
| Carries error info | Requires extra mechanism | Natively supported | None (only has/has not) | Supported, custom error type |
| Performance overhead | Zero | Stack unwinding has overhead | Minimal | Minimal |
| Embedded usability | Fully usable | Mostly disabled | Fully usable | Fully usable (C++23) |
| Call stack unwinding | None | Yes | None | None |
| Standard requirement | C is sufficient | C++ (must be enabled) | C++17 | C++23 |

From this table, we can see a clear divide. The fundamental difference between exceptions and the other three approaches lies in the **control flow model**: exceptions are non-local jumps, while error codes / ``optional`` / ``expected`` are all local value passing. This difference determines their respective suitable scenarios.

In real projects, our decision logic generally goes like this: if the project allows exceptions (desktop/server applications), use exceptions for "unrecoverable, unexpected" errors, and use ``expected`` or ``optional`` for "expected, caller-must-handle" errors. If the project disables exceptions (embedded systems, game engines, real-time systems), then only use error codes and ``optional`` / ``expected``, and ensure that all error paths have explicit handling logic. **The worst-case scenario is mixing multiple approaches without a unified convention**—that makes the error handling of the entire codebase a complete mess.

## In Practice: Three Ways to Write Safe Division

Now let's use a complete example program to put three "no-exception" error handling approaches together—the same functionality (safe integer division), implemented with error codes, ``std::optional``, and ``std::expected`` respectively, and then tested uniformly in ``main``.

```cpp
// error_cmp.cpp
// 对比三种错误处理方式：错误码、optional、expected

#include <cstdio>
#include <optional>
#include <expected>
#include <string>

// ========== 方式一：错误码 ==========

constexpr int kErrDivisionByZero = -1;
constexpr int kErrSuccess = 0;

int divide_error_code(int a, int b, int* out) {
    if (b == 0) {
        return kErrDivisionByZero;
    }
    *out = a / b;
    return kErrSuccess;
}

// ========== 方式二：std::optional ==========

std::optional<int> divide_optional(int a, int b) {
    if (b == 0) {
        return std::nullopt;
    }
    return a / b;
}

// ========== 方式三：std::expected ==========

enum class MathError {
    DivisionByZero,
};

std::expected<int, MathError> divide_expected(int a, int b) {
    if (b == 0) {
        return std::unexpected(MathError::DivisionByZero);
    }
    return a / b;
}

// ========== 测试 ==========

int main() {
    struct TestCase {
        int a;
        int b;
        const char* label;
    };

    TestCase cases[] = {
        {10, 3,  "10 / 3"},
        {10, 0,  "10 / 0 (error)"},
        {7,  2,  "7 / 2"},
    };

    for (const auto& tc : cases) {
        std::printf("--- Test: %s ---\n", tc.label);

        // 错误码版本
        int result_code = 0;
        int err = divide_error_code(tc.a, tc.b, &result_code);
        if (err == kErrSuccess) {
            std::printf("  [ErrorCode]  result = %d\n", result_code);
        } else {
            std::printf("  [ErrorCode]  error: division by zero\n");
        }

        // optional 版本
        auto result_opt = divide_optional(tc.a, tc.b);
        if (result_opt.has_value()) {
            std::printf("  [Optional]   result = %d\n", result_opt.value());
        } else {
            std::printf("  [Optional]   error: no value\n");
        }

        // expected 版本
        auto result_exp = divide_expected(tc.a, tc.b);
        if (result_exp.has_value()) {
            std::printf("  [Expected]   result = %d\n", result_exp.value());
        } else {
            switch (result_exp.error()) {
                case MathError::DivisionByZero:
                    std::printf("  [Expected]   error: DivisionByZero\n");
                    break;
            }
        }
    }

    return 0;
}
```

Compile and run:

```bash
g++ -std=c++23 -Wall -Wextra error_cmp.cpp -o error_cmp && ./error_cmp
```

If your compiler doesn't fully support ``std::expected`` yet, you can temporarily change the standard to C++20 and use the ``tl::expected`` header library as a replacement. On GCC 13+ and MSVC 19.34+, the code above can be compiled directly.

Expected output:

```text
--- Test: 10 / 3 ---
  [ErrorCode]  result = 3
  [Optional]   result = 3
  [Expected]   result = 3
--- Test: 10 / 0 (error) ---
  [ErrorCode]  error: division by zero
  [Optional]   error: no value
  [Expected]   error: DivisionByZero
--- Test: 7 / 2 ---
  [ErrorCode]  result = 3
  [Optional]   result = 3
  [Expected]   result = 3
```

Three test cases, three implementations, the results are completely identical—but "identical" is only on the surface. Notice the ``10 / 0`` error case: the error code version outputs a string ``"division by zero"``, the ``optional`` version can only say ``"no value"``, while the ``expected`` version gives a specific ``DivisionByZero`` enum value. In such a simple example, the difference isn't large, but imagine if the function had five different failure modes—``optional`` would be completely powerless—it can't tell you which failure actually occurred.

> **Pitfall Warning**: Among the three approaches above, the error code version's ``divide_error_code`` has an easily overlooked trap—if the caller doesn't check the return value and directly uses ``result_code``, the value of ``result_code`` on the error path is uninitialized (we initialized it with ``= 0``, but that's just how the test code is written; in real code, output parameters are often forgotten to be initialized). ``optional`` and ``expected`` are safer in this regard: if you call ``.value()`` without checking ``has_value()``, it will either throw an exception directly or lead to UB, but at least it won't let you keep running with a garbage value.

## Exercises

### Exercise 1: Extending Error Types

Add an ``IntegerOverflow`` error type to the ``error_cmp.cpp`` above. Hint: in ``checked_divide``, if ``a == INT_MIN && b == -1``, it causes overflow in two's complement representation (the result exceeds the range of ``int``). Handle this additional error condition in all three implementations, and add corresponding test cases.

### Exercise 2: Error Handling for File Reading

Suppose you have a function ``std::string read_file(const std::string& path)`` that might fail for three reasons: file does not exist, insufficient permissions, or read timeout. Design this function's interface using ``std::optional`` and ``std::expected`` respectively (no need to implement the actual logic, just design the signatures and error types), and compare the expressive power of the two approaches.

### Exercise 3: Error Propagation Chain

Use ``std::expected`` to implement a simple parsing chain: ``read_file`` -> ``parse_config`` -> ``validate_config``, where each function returns ``std::expected``. Write a complete call chain in ``main``, ensuring that a failure at any step is correctly propagated to the top level with a clear error message.

## Summary

Here, we have gone through all four mainstream error handling approaches in C++—error codes, exceptions, ``std::optional``, and ``std::expected``. Error codes are the oldest and simplest, but too easily ignored; exceptions guarantee "errors cannot be ignored" at the language level, but at the cost of runtime overhead and unavailability in embedded scenarios; ``std::optional`` is lightweight and elegant, but can only express "whether it exists," not "why it doesn't exist"; ``std::expected`` is currently the most comprehensive solution, offering both type-safe value passing and the ability to carry rich error information, though it requires C++23 support.

There is no absolute right or wrong in which approach to choose; the key is to maintain consistency at the project level. In desktop and server projects that allow exceptions, exceptions handle "unexpected, unrecoverable" errors, ``expected`` handles "expected, needs recovery" errors, and ``optional`` handles simple absence cases like "not found, does not exist." In embedded projects that disable exceptions, error codes are used for minimal scenarios and high-frequency paths, while ``optional`` and ``expected`` shoulder most of the error handling responsibilities. Regardless of which you choose, **the most important thing is that the entire team reaches a consensus on "what to use when,"** rather than letting everyone choose based on intuition.

This concludes Chapter 10 entirely. We discussed the basic mechanisms of exceptions, the four levels of exception safety, the RAII guard pattern, and today's grand comparison of error handling strategies. With this knowledge, we now have a solid error handling toolbox. Next, in Chapter 11, we enter a brand-new domain—the Standard Template Library (STL). Starting with ``std::vector``, we will gradually get to know a series of powerful containers and algorithms provided by the C++ standard library, which will save us from reinventing the wheel.
