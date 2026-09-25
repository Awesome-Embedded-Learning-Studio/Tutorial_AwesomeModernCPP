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
- Exception Safety
reading_time_minutes: 15
tags:
- cpp-modern
- host
- intermediate
- 进阶
title: Comparing Error Handling Approaches
translation:
  source: documents/vol1-fundamentals/ch10/03-error-handling-comparison.md
  source_hash: c5a0ed3995a5a85d4a14efdbd2f0e4d1dfa3127b67b2da6a4c3c2d8dbbd9cbc5
  translated_at: '2026-09-25T11:50:28+00:00'
  engine: anthropic
  token_count: 3000
---
# Comparing Error Handling Approaches: A Fuller Toolbox Demands More Skill

The error handling toolbox C++ hands us is larger than in most languages. In the C era we had only return values and `errno`; Java and C# lean almost entirely on exceptions; Rust gave us `Result<T, E>` and the `?` operator. And C++? It has all of them: error codes, exceptions, `std::optional`, `std::expected`. Having plenty isn't a bad thing, but if we don't understand the design intent and trade-offs behind each tool, mixed-style code comes easy: in the same project, one function returns `-1`, another throws, a third returns `std::nullopt`, and the caller has to dig through the docs every single time to figure out how errors should be handled.

In this article we step back for a wider view and put C++'s major error handling strategies side by side. Our goal is not to argue over "which one is best" (that debate is usually pointless), but to work out which approach fits which scenarios, which it doesn't, and how to choose in a real project. We start with the oldest of them all—error codes—walk all the way up to C++23's `std::expected`, and close with a practical decision guide.

## Starting with Error Codes: Simple but Unsafe

Error codes are a solution inherited from the C era, and the first error handling style every C++ programmer meets. The idea is dead simple: the function tells us through its return value whether it succeeded or failed—usually `0` for success and a negative number for failure, or a set of `#define`s or an `enum` to tell different error types apart.

```cpp
int divide(int a, int b, int* result) {
    if (b == 0) {
        return -1;  // Error code: division by zero
    }
    *result = a / b;
    return 0;       // Success
}

// Caller
int quotient = 0;
if (divide(10, 3, &quotient) != 0) {
    // Handle the error
}
```

The strength of error codes is their **predictability**—control flow never suddenly jumps away, every line executes in order, and we can see at a glance from the function signature which errors it might return. On top of that, the extra cost is zero: no exception tables, no stack unwinding, no runtime support of any kind.

But error codes have one fatal flaw: **the caller can simply ignore them**. The `divide` function above returns an `int`; if the caller never checks the return value, the compiler won't complain and the program still runs—only the result may be wrong. In a large project, a missed error-code check is practically guaranteed to happen at some point. Worse, an error code can only convey *what* error happened; it cannot carry rich context (the file path, the argument values that failed) unless we define an extra struct or use output parameters—and then the code bloats.

If our function returns an error code and the caller doesn't check it, the error is **silently swallowed**. Bugs of this kind are extremely hard to trace: the program doesn't crash and doesn't report anything—it just quietly produces wrong results. In an embedded system, such "silent errors" can make the hardware misbehave, and we have no clue where things went wrong.

## Exceptions: Impossible to Ignore, but Not Cheap

C++ exceptions solve the "ignored error" problem at the language level. A `throw` statement interrupts the normal flow of execution and climbs the call stack looking for a matching `catch` block. If we don't catch it, the program goes straight to `std::terminate`—there is no pretending we didn't see it.

```cpp
int divide(int a, int b) {
    if (b == 0) {
        throw std::invalid_argument("division by zero");
    }
    return a / b;
}

// The caller must handle it, or the exception keeps propagating
try {
    int result = divide(10, 0);
} catch (const std::invalid_argument& e) {
    std::cout << "Error: " << e.what() << "\n";
}
```

The strength of exceptions is that they bind the error information and the control flow together—we cannot catch an exception and leave it unhandled. Exceptions can also carry arbitrarily rich information (through classes derived from `std::exception`): a low-level function deep in the call stack throws, the top level catches and handles it uniformly, and the layers in between don't need to care at all.

Still, exceptions have several problems we cannot overlook. **Performance cost** is the first: although the cost of the "happy path" (when no exception occurs) is already tiny on modern compilers (the zero-cost model), once an exception is actually thrown, the cost of stack unwinding is considerable—local objects must be destructed frame by frame and the matching `catch` block located. **Opaque control flow** is another: looking at a function signature alone, we have no way to know whether it throws, or what it throws. C++11 did bring us `throw()` and `noexcept`, but dynamic exception specifications like `throw(std::invalid_argument)` were removed in C++17, leaving only the single keyword `noexcept`—it can only tell us "this function guarantees it won't throw," and imposes no language-level constraint at all on "which exceptions might be thrown."

Then we run into the most practical problem of all: **many embedded toolchains don't support exceptions at all**. GCC's and Clang's `-fno-exceptions` flag disables the exception machinery entirely; the moment a `throw` statement appears, the link fails. On severely resource-constrained MCUs, the code-size cost of exceptions (exception tables, RTTI) is often unacceptable. The result is a split reality: desktop and server C++ uses exceptions heavily, while embedded C++ barely uses them—the same language, two different styles.

## std::optional: A Value or No Value

C++17 introduced `std::optional<T>`, which expresses a very plain idea: this value **may exist, or may not**. Unlike error codes, `optional` is part of the type system—the signature `std::optional<int> divide(int a, int b)` tells us explicitly that "the return value may be absent," and the caller has to face that fact.

```cpp
#include <optional>

std::optional<int> safe_divide(int a, int b) {
    if (b == 0) {
        return std::nullopt;  // Division by zero: return empty
    }
    return a / b;
}

// Caller
auto result = safe_divide(10, 0);
if (result.has_value()) {
    std::cout << "Result: " << result.value() << "\n";
} else {
    std::cout << "Division by zero!\n";
}
```

The strength of `std::optional` is that it is **lightweight and explicit**. It forces the caller, at the type level, to deal with the "value absent" case—if we call `.value()` without checking `has_value()` and the value is empty, we get a `std::bad_optional_access` exception (yes, internally it still uses exceptions). We can also skip the check and access the value directly with `*result`, but when the value is empty that is undefined behavior.

The problem with `std::optional` is that it can only tell us "it failed"—it **cannot tell us why it failed**. Division by zero is one failure, overflow is another, an invalid argument a third—but `std::optional` treats them all alike and returns `std::nullopt` for every one of them. Once we need to distinguish different error types, `optional` is no longer enough.

Where `optional` fits: there is exactly one kind of error ("not found", "doesn't exist"), and the caller doesn't need to know the specific reason. Think of looking up an element in a container: `std::find_if` returns `end()` when nothing matches, but if we design our API to return a `std::optional`, the semantics become crystal clear—a value means found, empty means not found. Simple and clean.

## std::expected: Both the Value and the Reason

`std::expected<T, E>`, introduced in C++23, combines the type safety of `std::optional` with the rich error information of exceptions. Put simply, an `expected<T, E>` holds either a success value `T` or an error `E`—and that error can be any type, entirely up to you.

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
    // Simplified: overflow left unhandled for now
    return a / b;
}

// Caller
auto result = checked_divide(10, 0);
if (result.has_value()) {
    std::cout << "Result: " << result.value() << "\n";
} else {
    // Different error types can be handled differently
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

Look at the biggest difference between `std::expected` and `std::optional`: when failure happens, `expected` can tell us **why it failed**. The error type `E` can be an enum, a struct, a `std::string`—any type that carries enough information. That lets the caller pick different recovery strategies for different error types, instead of staring at a hollow "failed".

C++23 also gave `std::expected` a set of monadic operations that let us chain multiple fallible steps together: `and_then` continues to the next step on success, `transform` converts the value's type on success, and `or_else` attempts recovery on failure. On error, these operations automatically skip the remaining steps and propagate the error value directly—much like Rust's `?` operator in spirit, just less syntactically neat.

`std::expected` has its costs, though. Before the C++23 standard was officially finalized, mainstream compiler support was still incomplete (GCC 12+ and MSVC 19.34+ covered the basic functionality; Clang lagged relatively behind). If your project is still on C++17 or an earlier standard, a third-party library such as `tl::expected` can stand in—the interface is nearly identical, so migration costs little.

`std::expected`'s `value()` method throws a `std::bad_expected_access<E>` exception when the value is absent. If "no exceptions" was the whole reason you chose `expected`, remember to check with `has_value()` first, or dereference with `*` (UB when the value is empty, but no exception). Mixing `expected` with exception handling is a style inconsistency that is easy to miss.

## The Four Strategies Head to Head

Let's put the key properties of the four error handling approaches side by side. The table below is the core reference for making the choice:

| Property | Error codes | Exceptions | `std::optional` | `std::expected` |
|------|--------|------|------------------|------------------|
| Can be ignored? | Yes (the biggest problem) | No | Yes (but the type system reminds you) | Yes (but the type system reminds you) |
| Carries error info | Needs extra machinery | Natively supported | No (only present/absent) | Yes, custom error type |
| Performance cost | Zero | Stack unwinding costs | Tiny | Tiny |
| Embedded usability | Fully usable | Mostly disabled | Fully usable | Fully usable (C++23) |
| Stack unwinding | None | Yes | None | None |
| Standard required | Plain C suffices | C++ (must be enabled) | C++17 | C++23 |

From this table a clear divide emerges. The essential difference between exceptions and the other three lies in the **control flow model**: exceptions are a non-local jump, while error codes / `optional` / `expected` are all local value passing. That distinction decides where each of them fits.

In a real project, the selection logic goes roughly like this. When the project allows exceptions (desktop/server applications), use exceptions for "unrecoverable, unexpected" errors and `expected` or `optional` for "anticipated errors the caller needs to handle"; when the project bans exceptions (embedded systems, game engines, real-time systems), stick to error codes and `optional` / `expected` only, and make sure every error path has explicit handling logic. **The worst case is mixing several styles with no unified convention**—that turns the whole codebase's error handling into a mess.

## In Practice: Three Ways to Write Safe Division

Now let's take one complete sample program and put the three exception-free error handling approaches together: the same functionality (safe integer division) implemented with error codes, `std::optional`, and `std::expected` respectively, then exercised uniformly in `main`.

```cpp
// error_cmp.cpp
// Comparing three error handling approaches: error codes, optional, expected

#include <cstdio>
#include <optional>
#include <expected>
#include <string>

// ========== Approach 1: error codes ==========

constexpr int kErrDivisionByZero = -1;
constexpr int kErrSuccess = 0;

int divide_error_code(int a, int b, int* out) {
    if (b == 0) {
        return kErrDivisionByZero;
    }
    *out = a / b;
    return kErrSuccess;
}

// ========== Approach 2: std::optional ==========

std::optional<int> divide_optional(int a, int b) {
    if (b == 0) {
        return std::nullopt;
    }
    return a / b;
}

// ========== Approach 3: std::expected ==========

enum class MathError {
    DivisionByZero,
};

std::expected<int, MathError> divide_expected(int a, int b) {
    if (b == 0) {
        return std::unexpected(MathError::DivisionByZero);
    }
    return a / b;
}

// ========== Test ==========

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

        // Error code version
        int result_code = 0;
        int err = divide_error_code(tc.a, tc.b, &result_code);
        if (err == kErrSuccess) {
            std::printf("  [ErrorCode]  result = %d\n", result_code);
        } else {
            std::printf("  [ErrorCode]  error: division by zero\n");
        }

        // optional version
        auto result_opt = divide_optional(tc.a, tc.b);
        if (result_opt.has_value()) {
            std::printf("  [Optional]   result = %d\n", result_opt.value());
        } else {
            std::printf("  [Optional]   error: no value\n");
        }

        // expected version
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

If your compiler doesn't fully support `std::expected` yet, you can temporarily switch the standard to C++20 and substitute the `tl::expected` header-only library. On GCC 13+ and MSVC 19.34+, the code above compiles as-is.

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

Three test cases, three implementations, completely identical results—yet "identical" is only the surface. Look closely at the `10 / 0` error case: the error code version printed a `"division by zero"` string, the `optional` version could only say `"no value"`, and the `expected` version gave the concrete `DivisionByZero` enum value. In an example this simple the difference is minor, but imagine the function having a few more failure modes: `optional` would be completely helpless—it has no way to tell us which failure occurred.

Of the three versions above, the error code version `divide_error_code` hides an easily missed trap: if the caller skips checking the return value and uses `result_code` anyway, then on the error path the value of `result_code` is uninitialized (we did initialize it with `= 0`, but that is just the test code's style; in real code, output parameters are frequently forgotten). `optional` and `expected` are safer on this front: calling `.value()` without checking `has_value()` throws immediately or triggers UB—at the very least, we never carry on running with a garbage value.

## Exercises

### Exercise 1: Extending the Error Types

Add an `IntegerOverflow` error type to the `error_cmp.cpp` above. Hint: in `checked_divide`, `a == INT_MIN && b == -1` overflows under two's-complement representation (the result falls outside the range of `int`). Handle this extra error condition in each of the three implementations, and add matching test cases.

### Exercise 2: Error Handling for Reading a File

Suppose you have a function `std::string read_file(const std::string& path)` that can fail for three reasons: the file doesn't exist, insufficient permissions, or a read timeout. Design this function's interface twice, once with `std::optional` and once with `std::expected` (no need to implement the actual logic—just design the signatures and error types), and compare the difference in expressive power between the two.

### Exercise 3: The Error Propagation Chain

Build a simple parsing chain with `std::expected`: `read_file` -> `parse_config` -> `validate_config`, with each function returning a `std::expected`. Write the full call chain in `main`, making sure a failure at any step propagates correctly to the top with a clear error message.
