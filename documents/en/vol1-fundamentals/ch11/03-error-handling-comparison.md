---
chapter: 11
cpp_standard:
- 11
- 14
- 17
- 20
description: 'A comparison of error handling strategies: exceptions, error codes, optional, and expected'
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
  source: documents/vol1-fundamentals/ch11/03-error-handling-comparison.md
  source_hash: 522e4f1153a3e6511889778616ad5e3b60bb6fa80bc39cde66c7c23bd4b87c0f
  translated_at: '2026-09-27T04:12:30+00:00'
  engine: anthropic
  token_count: 3500
---
# Comparing Error Handling Approaches: With This Many Tools, You'd Better Know How to Use Them

The error handling toolbox C++ gives us is bigger than in most languages. In the C era we had nothing but return values and `errno`; Java and C# lean almost entirely on exceptions; Rust hands you `Result<T, E>` and the `?` operator. And C++? **It has all of them: error codes, exceptions, `std::optional`, `std::expected`. Having plenty of options isn't a bad thing, but if we don't understand the design intent and the trade-offs behind each tool, mixed-style code is an easy trap to fall into**: within the same project, one function returns `-1`, another throws, a third returns `std::nullopt`, and the caller has to dig through the docs every single time to figure out how errors are supposed to be handled. (I really need to vent here: the project I took over for maintenance was something else—a hundred different error handling styles, my brain endlessly switching gears on "how does this spot get handled again" =.=)

Strictly speaking, this topic would fit best in the later software engineering chapters. But after some thought—error handling is a fundamental skill every C++er needs—so I went ahead and decided to put it here anyway! Since we are touching on software engineering philosophy, let me state my position up front: the wording below is more an opinion than a fact (yes, C++ really is that free; it is genuinely hard to point your finger at a piece of syntax and declare it flat-out wrong—that kind of absolutism does not exist here.)

Our goal is not to argue over "which one is best" (those arguments are usually pointless and quickly degenerate into religious sects quarreling), but to **get clear on which approach fits which scenarios, which it does not, and how to make the choice in a real project**. We start from the oldest of them all, error codes, walk all the way to C++23's `std::expected`, and close with a practical decision guide.

## Starting with Error Codes: Simple but Unsafe

Error codes are a legacy of the C era, and the first error handling approach every C++ programmer runs into. The principle is dead simple: the function tells us success or failure through its return value—usually `0` for success and a negative number for error, or a set of `#define`s or an `enum` to tell different error types apart.

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

The strength of error codes is their **predictability**—control flow never jumps away without warning, every line executes in order, and we can read straight off the function signature which errors it might return. On top of that, the extra cost is zero: no exception tables, no stack unwinding, no runtime support of any kind.

But error codes have one fatal flaw: **the caller can choose to ignore them**. The `divide` function above returns an `int`; if the caller never checks the return value at all, the compiler will not complain and the program still runs—only now the results may be wrong. In a large project, missed checks on error codes are practically guaranteed to happen. Worse still, an error code can only convey "which error happened", never rich context (a file path, the argument values that failed)—unless we define extra structs or use output parameters, at which point the code bloats beyond recognition.

When our function returns an error code and the caller never checks it, the error is **silently swallowed**. Bugs of this kind are brutal to track down: the program does not crash, does not report anything, it just quietly produces wrong results. Take embedded systems as an example: **a "silent error" like this can make the hardware misbehave, and we have no idea where the problem lies.**

## Exceptions: Impossible to Ignore, but Not Cheap

C++'s exception mechanism solves the "errors get ignored" problem at the language level. A `throw` statement interrupts the normal flow of execution and climbs up the call stack looking for a matching `catch` block. If we do not catch it, the program goes straight to `std::terminate`—there is no pretending we did not see it.

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

The strong suit of exceptions is that they bind the error information and the control flow together—we cannot catch an exception and then simply not deal with it. Exceptions can also carry arbitrarily rich information (through classes derived from `std::exception`): a low-level function deep in the call stack throws, the top level catches and handles it uniformly, and the layers in between do not need to care at all.

But exceptions come with a few problems we cannot overlook. **Performance cost** is the first: while the cost of the "happy path" (in other words, when no exception is thrown) is already tiny on modern compilers—approaching zero—once an exception is actually thrown, the cost of stack unwinding is considerable: local objects have to be destroyed frame by frame while the runtime hunts for a matching catch block.

> I'd like to share a scene from an interview I once had with a cloud storage company, chatting with a senior interviewer. He said: we do not allow exceptions on extremely hot paths—any nanosecond-level cost gets evaluated over and over. If an exception fired there, the latency it would cause for the services upstream would be a disaster.
>
> On my own project, I have also reviewed code on a hot path—for a place that is extremely hot yet very easy to get wrong, exceptions should be sentenced to death outright. Genuinely, do not use them.

**Opaque control flow** is another: from the function signature alone, we have no idea whether it throws, or what it throws. C++11 once introduced `throw()` and `noexcept`, but dynamic exception specifications like `throw(std::invalid_argument)` were removed in C++17, and only the single keyword `noexcept` remains—all it can tell us is "this function guarantees it will not throw"; as for "which exceptions might it throw", the language imposes no constraint whatsoever.

We also run into the most practical problem of all: **many embedded toolchains have no plans whatsoever to enable exception support!**. GCC's and Clang's `-fno-exceptions` flag disables the exception machinery entirely; the moment a `throw` statement appears, the link step errors out. On severely resource-constrained MCUs, the code size cost of exceptions (exception tables, RTTI) is often unacceptable. The result is a split landscape: desktop and server-side C++ uses exceptions heavily, while embedded C++ barely touches them—same language, two styles.

## std::optional: Just Whether It's There or Not

C++17 introduced `std::optional<T>`, which expresses a very plain idea: this value **might exist, or it might not**. Unlike error codes, `optional` is part of the type system—the signature `std::optional<int> divide(int a, int b)` tells us explicitly "the return value may be absent", and the caller has to face that fact.

```cpp
#include <optional>

std::optional<int> safe_divide(int a, int b) {
    if (b == 0) {
        return std::nullopt;  // Division by zero, return empty
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

The appeal of `std::optional` is that it is **lightweight and explicit**. It forces the caller, at the type level, to deal with "the value is not there"—if we call `.value()` directly without checking `has_value()`, an empty value throws `std::bad_optional_access` (right, internally it still uses exceptions). We can also use `*result` to skip the check and access the value directly, but when the value is empty, that is undefined behavior.

The problem with `std::optional` is that it can only tell us "it failed", but **not why it failed**. Division by zero is one failure, overflow is another, an illegal argument yet a third—but `std::optional` treats them all alike and returns `std::nullopt` for every one. Once we need to tell different error types apart, `optional` is no longer enough.

Where `optional` fits: there is exactly one kind of error ("not found", "does not exist"), and the caller does not need to know the specific reason. For example, searching a container for an element: `std::find_if` returns `end()` when it finds nothing, but if we design our API to return `std::optional`, the semantics become crystal clear—a value means found, empty means not, plain and simple.

## std::expected: Both the Value and the Reason

`std::expected<T, E>` is a type introduced in C++23, and an error handling approach I am genuinely fond of.

> Fun fact: back when I was discussing this thing with someone, my brain glitched and I said it was a C++20 feature. A bro immediately jumped up saying I had never written code, that this is C++23. I teared up a little QAQ.

It combines the type safety of `std::optional` with the rich error information of exceptions. Simply put, an `expected<T, E>` holds either a success value `T` or an error `E`—and that error can be any type at all, defined entirely by you.

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
    // Simplified: overflow not handled for now
    return a / b;
}

// Caller
auto result = checked_divide(10, 0);
if (result.has_value()) {
    std::cout << "Result: " << result.value() << "\n";
} else {
    // Different handling per error type becomes possible
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

Look at the biggest difference between `std::expected` and `std::optional`: when failure happens, `expected` can tell us **why it failed**. The error type `E` can be an enum, a struct, a `std::string`—any type that carries enough information. This lets the caller adopt different recovery strategies for different error types, instead of staring at a hollow "it failed".

C++23 also provides a set of monadic operations for `std::expected`, letting us chain multiple operations that may each fail: `and_then` continues to the next step on success, `transform` converts the value's type on success, `or_else` attempts recovery on failure. When an error occurs, these operations automatically skip the remaining steps and propagate the error value directly—same idea as Rust's `?` operator, just not as syntactically slick.

That said, `std::expected` has its costs too. Before the C++23 standard officially landed, mainstream compiler support was still incomplete (GCC 12+ and MSVC 19.34+ support the basic functionality; Clang's support lagged relatively behind). If your project is still on C++17 or an earlier standard, you can use a third-party library (such as `tl::expected`) as a substitute—the interface is essentially identical, the migration cost is tiny, and there is a very real chance the whole migration is just you running a project-wide search-and-replace from `tl::expected` to `std::expected` (a joy).

The `value()` method of `std::expected` throws a `std::bad_expected_access<E>` exception when the value is absent. If your whole reason for picking `expected` was "no exceptions", then by all means check with `has_value()` first, or dereference with `*` (UB when empty, but it does not throw). Mixing `expected` and exception handling is an all-too-easy-to-miss style inconsistency.

## The Four Strategies Go Head to Head

"Clap clap!" (the sound of palms meeting)—alright! Come back from all those LLM sites and cppreference now! Let's sum up!

| Feature | Error Codes | Exceptions | `std::optional` | `std::expected` |
| --- | --- | --- | --- | --- |
| Can be ignored | Yes (the biggest problem) | No | Yes (but the type system reminds you) | Yes (but the type system reminds you) |
| Carries error info | Needs extra machinery | Supported natively | No (present/absent only) | Yes, error type is user-defined |
| Performance cost | Zero | Stack unwinding costs | Minimal | Minimal |
| Embedded usability | Fully usable | Mostly disabled | Fully usable | Fully usable (C++23) |
| Stack unwinding | None | Yes | None | None |
| Standard required | Plain C suffices | C++ (must be enabled) | C++17 | C++23 |

From this table we can see a clean dividing line. The essential difference between exceptions and the other three approaches is the **control flow model**: exceptions are a non-local jump, while error codes / `optional` / `expected` are all local value passing. That difference determines where each of them fits.

In a real project, our selection logic goes roughly like this:

1. Does the project allow exceptions? If it does, **for "unrecoverable, unexpected" errors, we use exceptions**. These tend to be desktop, backend, even other so-called high-performance scenarios—but on paths that are **extremely low-frequency yet redundantly complex, exceptions are acceptable!** If the team forbids it, or you simply cannot use it, give it up, buddy, and see below!
2. For "expected errors the caller must handle", use `expected` or `optional`
3. On a very old C++, we stick to error codes plus `optional` / `expected`, and make sure every error path carries explicit handling logic.
4. **The worst case is mixing several approaches with no unified convention**—that turns the entire codebase's error handling into one giant mess; please, whatever you do, do not let your project become this hell. **And if you are a Vibe Coder, please keep this problem reined in too! Do not let AI abuse error handling paradigms!**

## In Practice: Three Ways to Write Safe Division

Now let's use one complete example program to put the three "no exceptions" error handling approaches side by side: the same functionality (safe integer division), implemented with error codes, `std::optional`, and `std::expected` respectively, then tested together in `main`.

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

The complete code sits right below—hit "Try It Yourself" to run it directly, no terminal needed (the compile options are already set to C++23, which `std::expected` requires):

<OnlineCompilerDemo
  title="In Practice: Three Ways to Write Safe Division (error_cmp.cpp)"
  source-path="code/examples/vol1/22_error_comparison.cpp"
  description="Run the comparison of error codes, optional, and expected online. Try adding an IntegerOverflow value to MathError—all three functions will have to change accordingly."
  run-options="-O2 -std=c++23"
  allow-run
/>

If your local compiler does not fully support `std::expected` yet, you can temporarily switch the standard to C++20 and substitute the `tl::expected` header-only library. On GCC 13+ and MSVC 19.34+, the code above compiles as is.

Three test cases, three implementations, identical results—but "identical" is only the surface. Look closely at the `10 / 0` error case: the error code version prints the string `"division by zero"`, the `optional` version can only say `"no value"`, and the `expected` version produces the concrete `DivisionByZero` enum value. In an example this simple the difference hardly matters, but imagine the function had a few more distinct failure modes—`optional` would be completely helpless there. It has no way to tell us which failure occurred.

Among the three versions above, the error code `divide_error_code` hides an easy-to-miss trap: if the caller uses `result_code` without checking the return value, then on the error path `result_code` is uninitialized (we did initialize it with `= 0`, but that is just how the test code is written; in real code, output parameters are forgotten all the time). `optional` and `expected` are safer on this front: calling `.value()` without checking `has_value()` throws immediately or leads to UB—at least you are not carrying a garbage value onward.

## Exercises

### Exercise 1: Extending the Error Types

Add an `IntegerOverflow` error type to the `error_cmp.cpp` above. Hint: in `checked_divide`, `a == INT_MIN && b == -1` causes overflow under two's complement representation (the result falls outside the range of `int`). Handle this extra error condition in each of the three implementations, and add the corresponding test cases.

### Exercise 2: Error Handling for File Reading

Suppose you have a function `std::string read_file(const std::string& path)` that can fail for three reasons: the file does not exist, insufficient permissions, or a read timeout. Design this function's interface twice, once with `std::optional` and once with `std::expected` (no need to implement the actual logic—just design the signatures and error types), and compare the difference in expressive power between the two schemes.

### Exercise 3: An Error Propagation Chain

Use `std::expected` to implement a simple parsing chain: `read_file` -> `parse_config` -> `validate_config`, with each function returning a `std::expected`. Write the complete call chain in `main`, making sure a failure at any step propagates correctly to the top level and produces a clear error message.
