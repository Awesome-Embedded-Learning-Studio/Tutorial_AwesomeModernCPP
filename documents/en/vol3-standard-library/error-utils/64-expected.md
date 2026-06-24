---
chapter: 7
cpp_standard:
- 23
description: Thoroughly explains `std::expected`—elevating errors from exceptions/return
  codes to a type, various construction and access patterns, how `and_then`, `transform`,
  and `or_else` chain "fallible operations" into a short-circuiting chain, its relationship
  with `optional`/`variant`, and the real-world performance gap compared to exceptions
  at different failure rates.
difficulty: advanced
order: 64
platform: host
prerequisites:
- optional：把「可能没有」做成类型
- variant：类型安全的联合体与 visit
related:
- filesystem：路径、目录与跨平台文件操作
tags:
- host
- cpp-modern
- advanced
- 类型安全
- expected
title: 'expected: Value or Error, C++23''s New Error Handling Paradigm'
translation:
  source: documents/vol3-standard-library/error-utils/64-expected.md
  source_hash: a39b8f18277237fd686faac3e79bc0e9313ac24b6fe7cf8988eb87599b507164
  translated_at: '2026-06-24T00:39:58.387799+00:00'
  engine: anthropic
  token_count: 4559
---
# `expected`: Value or Error, C++23's New Error Handling Paradigm

We have covered `optional` for "values that might not be there" and `variant` for "values that might be A or B". However, there is one even more common scenario we cannot avoid: **a function either returns a value or returns an error explaining "why it failed"**. Historically, C++ has handled this somewhat awkwardly. Let's first lay out the pain points.

There are three common approaches, each with significant drawbacks. The first is throwing exceptions: `throw std::runtime_error(...)`. Control flow instantly vanishes; at the call site, you cannot tell that this function throws—exceptions are "implicit control flow" that are not visible in the signature. Moreover, even if exceptions are never thrown, the mechanism itself carries potential overhead for table registration and stack unwinding in some implementations, and compilers tend to be more conservative when optimizing code with exceptions. The second is error codes: `int rc = foo(); if (rc < 0) ...`. This is explicit, but error information and return values are squeezed into the same channel. Whether the `int` you receive is a "result" or an "error code" relies entirely on convention, and **it is far too easy to forget to check**—a call site without an `if` silently drops the error. The third is output parameters like `bool foo(int& out)`, which forces a reference into the signature and occupies the return value slot with a boolean, making chaining completely impossible.

C++23 provides a clean answer: **make "value or error" a type**. `std::expected<T, E>` either holds an expected value `T` or an unexpected value `E`. In this article, we will break it down completely—from construction and access to C++23's monadic chaining (which is `expected`'s real killer feature) and finally to performance benchmarks comparing it with exceptions. Let's start with a conclusion to set the stage: **`expected` is not about "replacing exceptions," but rather "promoting errors from implicit control flow to explicit types," forcing the compiler to make you handle them**.

## A Minimal Example: Parse or Fail

Let's jump straight into a classic example from cppreference for `expected`—parsing a string into a number, returning a `double` on success, or an enum explaining the reason for failure on error:

```cpp
// Standard: C++23
#include <cmath>
#include <expected>
#include <iostream>
#include <string_view>

enum class parse_error {
    kInvalidInput,
    kOverflow
};

auto parse_number(std::string_view& str) -> std::expected<double, parse_error>
{
    const char* begin = str.data();
    char* end;
    double retval = std::strtod(begin, &end);

    if (begin == end)
        return std::unexpected(parse_error::kInvalidInput);   // 失败:包成 unexpected
    if (std::isinf(retval))
        return std::unexpected(parse_error::kOverflow);

    str.remove_prefix(end - begin);
    return retval;                                            // 成功:直接返回值
}
```

Reading this code, there are two key takeaways. **On success, simply `return retval;`** — `expected` has an implicit constructor, so `T` is automatically wrapped into a successful `expected<T,E>`. **On failure, `return std::unexpected(error);`** — `std::unexpected` is an explicit wrapper designed to signal to `expected` that "this is an error, not a value." This design is intentional: in `expected<double, parse_error>`, both `double` and `parse_error` can be implicitly constructed. If errors could be returned directly via `return parse_error{...};`, the compiler would be unable to distinguish between success and failure. Therefore, the Standard Library requires you to wrap errors in `std::unexpected` to eliminate ambiguity.

How do we use this on the caller side? Let's run it (GCC 16.1.1, `-std=c++23 -O2`):

```cpp
auto process = [](std::string_view str) {
    std::cout << "str: \"" << str << "\", ";
    if (const auto num = parse_number(str); num.has_value())
        std::cout << "value: " << *num << '\n';
    else if (num.error() == parse_error::kInvalidInput)
        std::cout << "error: invalid input\n";
    else if (num.error() == parse_error::kOverflow)
        std::cout << "error: overflow\n";
};

for (auto src : {"42", "42abc", "meow", "inf"})
    process(src);
```

```text
str: "42", value: 42
str: "42abc", value: 42
str: "meow", error: invalid input
str: "inf", error: overflow
```

Compare this with the return code approach. What is the difference? **The return type `expected<double, parse_error>` directly writes "it might fail" and "what the failure reasons are" into the signature**. When the caller receives an `expected`, the compiler cannot help much if they use the value without checking `has_value()` (we will discuss this later), but at least the type is right there. You know this is "something that needs checking," unlike a bare `int` return code which pretends to be a normal result. The fact that `"42abc"` parses to 42 is because `strtod` performs prefix parsing; it returns once it reads a number, leaving the rest in the string. This has nothing to do with `expected`, it is the semantics of `strtod`.

## Construction and Access: Several Patterns

Let us quickly review the common construction and access methods we will use daily, so we do not get stuck on basic syntax later when discussing monadic operations.

### Construction

For a successful `expected`, just put the value in. For a failure, wrap it with `std::unexpected`:

```cpp
// Standard: C++23
std::expected<int, std::string> ok = 7;                       // 成功
std::expected<int, std::string> bad = std::unexpected("disk full");  // 失败
```

Note the `bad` line—`std::unexpected("disk full")` contains a string literal, yet `E` is `std::string`. This invokes the implicit constructor of `std::unexpected`, converting the literal to a `std::string`. This is important: `unexpected` forwards the argument to the constructor of `E`, so we do not need to explicitly write `std::string("...")` every time.

### Access: `operator*` / `value()` / `value_or()`

There are three ways to access the value, each with different semantics. This is almost identical to `optional`:

```cpp
// Standard: C++23
std::expected<int, std::string> ok = 7;
std::expected<int, std::string> bad = std::unexpected("disk full");

std::cout << "*ok = " << *ok << '\n';              // 解引用:不检查,失败时是 UB
std::cout << "ok.value_or(-1) = " << ok.value_or(-1) << '\n';   // 7

std::cout << "bad.value_or(-1) = " << bad.value_or(-1) << '\n'; // -1
std::cout << "bad.error() = " << bad.error() << '\n';           // disk full
```

```text
*ok = 7
ok.value_or(-1) = 7
bad.value_or(-1) = -1
bad.error() = disk full
```

To summarize the differences between the three in one sentence: **`*x` means "I guarantee it has a value, take it directly" (failure is undefined behavior, zero overhead); `x.value()` means "I expect it to have a value, otherwise throw an exception" (failure throws `bad_expected_access<E>`); `x.value_or(default)` means "take it if available, otherwise use the default" (never throws, suitable for scenarios with a reasonable fallback).**

::: warning Dereference does not check; do not use on a failed state
`operator*` and `operator->` are **unchecked**. Calling `*x` on a failed `expected` is undefined behavior. In the standard library, this implies "I trust you, here is the address of the internally stored value." If you are unsure whether it contains a value, either check `has_value()` first, use `value()` which throws an exception, or use the fallback `value_or()`. This behavior is identical in `optional` and `expected`—once you trip over it, you'll remember.
:::

The exception type thrown by `value()` is worth noting separately—it is not `std::runtime_error`, but `std::bad_expected_access<E>`, an exception class that carries the error value `E` with it. Therefore, after `catch`-ing it, we can extract the error value from the exception:

```cpp
// Standard: C++23
std::expected<int, std::string> bad = std::unexpected("disk full");
try {
    (void)bad.value();                          // 失败 -> 抛异常
} catch (const std::bad_expected_access<std::string>& e) {
    std::cout << "value() threw, error=" << e.error() << '\n';
}
```

```text
value() threw, error=disk full
```

This highlights a clever bridge between `expected` and exceptions: we can use `value()` for the zero-overhead happy path, yet still retrieve structured error information when things go wrong, rather than being limited to a single `what()` string.

### Symmetry on the error side: `error()` and `error_or()`

Almost everything available for the value side has a counterpart on the error side. `error()` retrieves the error value (calling this while in the success state is UB), while `error_or(default)` provides a default error when in the success state. We will rely on this symmetry repeatedly when we discuss monadic operations later:

```cpp
// Standard: C++23
std::expected<int, int> ok = 5;
std::expected<int, int> bad = std::unexpected(7);
std::cout << "ok.error_or(-99) = " << ok.error_or(-99) << '\n';   // -99(成功态)
std::cout << "bad.error_or(-99) = " << bad.error_or(-99) << '\n'; // 7
```

```text
ok.error_or(-99) = -99
bad.error_or(-99) = 7
```

## C++23 Monadic Operations: Chaining "Operations That May Fail"

At this point, you might still be thinking: writing `if (has_value()) ... else ...` repeatedly is just as verbose as checking return codes. What truly allows `expected` to leave return codes behind are the four monadic operations added in C++23—`and_then`, `transform`, `or_else`, and `transform_error`. They allow you to chain a series of operations where "each step might fail" into a pipeline. **If any step fails, it automatically short-circuits, skipping all subsequent steps, and the error propagates to the end.**

This is the core power of `expected`, so let's dive in.

### A Real-World Chain: Parse → Convert → Format

Let's say we need to take a user input string, parse it into a dollar amount, convert it to "cents" (an integer to avoid floating-point issues), and finally format it into a display string. Each step can fail: parsing errors, negative amounts, formatting errors, etc. The traditional approach involves nesting layers of `if` statements:

```cpp
// 传统写法:层层嵌套 if
auto s = std::string_view{"42.5"};
auto parsed = parse_number(s);
if (!parsed) return error(parsed.error());
auto cents = to_cents(*parsed);
if (!cents) return error(cents.error());
auto text = format_amount(*cents);
```

Three steps are fine, but what about five or ten? This is the classic synchronous version of "callback hell". We can chain them using `and_then` to form a linear expression:

```cpp
// Standard: C++23
auto to_cents(double dollars) -> std::expected<long, parse_error> {
    if (dollars < 0)
        return std::unexpected(parse_error::kInvalidInput);  // 金额为负 -> 失败
    return static_cast<long>(dollars * 100.0);
}

auto format_amount(long cents) -> std::string {
    return std::to_string(cents / 100) + "." +
           (cents % 100 < 10 ? "0" : "") + std::to_string(cents % 100) + " USD";
}

auto run = [](std::string_view s) {
    std::cout << "\"" << s << "\" -> ";
    auto result = parse_number(s)
        .and_then(to_cents)         // expected<double,E> -> expected<long,E>
        .transform(format_amount);  // expected<long,E>   -> expected<string,E>
    if (result)
        std::cout << "OK: " << *result << '\n';
    else
        std::cout << "ERR: " << static_cast<int>(result.error()) << '\n';
};

run("42.5");   // 成功一路走到底
run("meow");   // parse 失败,后面两步根本不执行
run("-1");     // parse 成功(=-1),但 to_cents 拒绝负数 -> 失败
```

```text
"42.5" -> OK: 42.50 USD
"meow" -> ERR: 0
"-1" -> ERR: 0
```

Focus on two key observations. **First, types flow naturally through the chain**: `parse_number` returns `expected<double, E>`, `and_then(to_cents)` takes a `double` and returns `expected<long, E>`, so the whole expression becomes `expected<long, E>`. Then `transform(format_amount)` turns the inner `long` into a `string`, yielding `expected<string, E>`. The value type changes at each step, but the error type `E` remains consistent throughout, allowing errors to propagate all the way down. **Second, short-circuiting is automatic**: inside `run("meow")`, `parse_number` fails. `and_then` and `transform` see that the received `expected` is in a failure state, so they transparently pass the error along. `to_cents` and `format_amount` are never even called. This "continue on success, bypass on failure" semantics replaces what you would have manually written with a pile of `if` statements, compressing it into a single chain.

### What the Four Operations Actually Do

Memorize the semantics of these four operations so you don't get confused when choosing:

- **`and_then(f)`** — `f` takes the value and returns a **new `expected`**. Use this to chain operations that might also fail (like `to_cents` above). The return type of `f` must be `expected<U, E>`, and `E` must match.
- **`transform(f)`** — `f` takes the value and returns a **plain value** (not an `expected`). Use this to chain operations that only modify the value and cannot fail (like `format_amount` above). It returns `expected<U, E>`. Note the distinction from `and_then` lies in the return type of `f`: use `and_then` if it can fail, use `transform` if it cannot.
- **`or_else(f)`** — The inverse of `and_then`. `f` is called **only on failure**, receiving the error and returning a new `expected`. On success, it passes through unchanged. This is used for "failure recovery/fallbacks".
- **`transform_error(f)`** — The inverse of `transform`. `f` is called **only on failure**, receiving the error and returning a **new error value** (which can be a new type). On success, it passes through unchanged. This is used for "rewriting/translating error messages".

To remember in one sentence:**`and_then`/`transform` take the success path (`and_then` returns expected, `transform` returns value); `or_else`/`transform_error` take the failure path (`or_else` returns expected, `transform_error` returns error value)**. Two axes: which path to take × what to return.

### `or_else` and `transform_error`: Chaining the Failure Side

The success side is easy to understand, but the two failure-side operations deserve a closer look, because "failure fallbacks" and "error message processing" are the most verbose parts of traditional error handling.

`or_else` is the fallback — when failing, swap in a new `expected` to take over:

```cpp
// Standard: C++23
auto with_fallback = parse_number("meow")
    .or_else([](parse_error) {
        return std::expected<double, parse_error>(0.0);   // 解析失败就给 0
    });
std::cout << "fallback value = " << *with_fallback << '\n';
```

```text
fallback value = 0
```

`transform_error` rewrites errors—converting the error `E` into another (usually more readable) form upon failure, and the `E` type in the return type changes accordingly. The following example translates an enumeration error into a numbered string, and incidentally demonstrates how the type of `E` can change along the chain:

```cpp
// Standard: C++23
auto reworded = parse_number("meow")
    .transform([](double d) { return d + 1000.0; })       // 成功分支,但现在是失败态 -> 不执行
    .transform_error([](parse_error e) {
        return std::string("bad number, code=") +
               std::to_string(static_cast<int>(e));
    });
// 注意:到这里 E 已经从 parse_error 变成了 std::string
std::cout << "reworded error = " << reworded.error() << '\n';
```

```text
reworded error = bad number, code=0
```

That `transform` might look a bit redundant—since the input is in a failure state, it won't execute anyway. We included it to clarify one thing: **each step in the chain decides whether to run based on the current state of the `expected`. You can chain operations for both the success path and the failure path; they do not interfere with each other.** A success-path `transform` is transparent when in a failure state, and a failure-path `transform_error` is transparent when in a success state.

## Relationship with optional and variant

When we place `expected` back into the standard library's type utility family, its position becomes clear. Remember when we discussed `optional` as "a value that might be absent" and `variant` as a "type-safe union"? `expected` fits right in between these two.

### expected ≈ optional + Error Information

`optional<T>` only tells you "if it exists," but says nothing when it is absent. `expected<T, E>` stuffs an `E` in there to tell you **why** it is absent. From a storage perspective, `expected<T, E>` is roughly equivalent to "an `optional<T>` plus an error slot." When `E` is a lightweight type (like an enum or `int`), the overhead of `expected` is almost identical to `optional`—verified by a quick test (GCC 16.1.1, `-std=c++23 -O2`):

```cpp
// Standard: C++23
std::cout << "sizeof optional<int>     = " << sizeof(std::optional<int>) << '\n';
std::cout << "sizeof expected<int,int> = " << sizeof(std::expected<int,int>) << '\n';
std::cout << "sizeof variant<int,int>  = " << sizeof(std::variant<int,int>) << '\n';
```

```text
sizeof optional<int>     = 8
sizeof expected<int,int> = 8
sizeof variant<int,int>  = 8
```

All three are 8 bytes. Why are they so uniform? Because their underlying structure is essentially "one value + one discriminator bit". `optional<int>` consists of an `int` plus a `bool` (implemented using either an impossible value for `int` or an extra byte; in practice, `optional<int>` usually borrows a high-order byte, so it remains 8 bytes). Both `expected<int,int>` and `variant<int,int>` follow a "two mutually exclusive members + one tag" pattern—two `int`s share the same storage (union semantics), plus a tiny tag to distinguish which one is currently active, so `int + int` still totals 8 bytes. **Only when `E` is a large type requiring independent storage (like `std::string`) does `expected` become larger than `optional`**:

```cpp
// Standard: C++23
std::cout << "sizeof expected<double,std::string> = " << sizeof(std::expected<double,std::string>) << '\n';
std::cout << "sizeof expected<int,std::string>    = " << sizeof(std::expected<int,std::string>) << '\n';
```

```text
sizeof expected<double,std::string> = 40
sizeof expected<int,std::string>    = 40
```

40 bytes, because `std::string` (libstdc++'s SSO implementation) is 32 bytes by itself. Adding the `double`/`int` storage and the tag completes the alignment. The lesson here is: **pay attention to the size of `E`**. While `std::string` is informative as an error type, every `expected` instance carries the weight of a string—keeping the error type small and lean is key to using `expected` efficiently.

### `expected` is a Semantic Specialization of `variant<T, E>`

Looking a layer deeper, `expected<T, E>` is structurally just `variant<T, E>`, but it **assigns semantics to the two members**: the first is the "expected value," and the second is the "unexpected error." `variant` is a neutral "A or B," whereas `expected` takes a stance: "success or failure." This semantic distinction brings a full suite of interfaces tailored for error handling: the asymmetry of `value()`/`error()` (value throws on failure, error does not), the fallbacks provided by `value_or`/`error_or`, and the monadic operations mentioned above. These are features `variant` lacks—to achieve the same with `variant`, you would have to write a chain of `visit` calls yourself.

There is another structural detail worth noting: **`expected` is never "valueless"**. As cppreference states, "expected is never valueless." In contrast, `variant` can theoretically enter a `valueless_by_exception` state in extreme cases (such as when a value-holding state throws an exception and the type lacks a `nothrow` move constructor). Since `expected` only holds a value or an error and doesn't rely on a type list, it is immune to this pitfall by design.

### `expected<void, E>`: Errors Only, No Value

There is a class of operations that return no value and only care about "success or failure": closing files, flushing buffers, or committing transactions. Using `expected<T, E>` here is awkward—what do you put for `T`? The standard library provides a partial specialization, `expected<void, E>`, specifically to represent "success (with no value) or failure (with an error)":

```cpp
// Standard: C++23
std::expected<void, int> vok;                              // 成功
std::expected<void, int> vbad = std::unexpected(42);       // 失败,错误 42
std::cout << "vok.has_value = " << vok.has_value()
          << "  vbad.has_value = " << vbad.has_value()
          << "  vbad.error = " << vbad.error() << '\n';
```

```text
vok.has_value = 1  vbad.has_value = 0  vbad.error = 42
```

The `void` specialization lacks `operator*` / `value()` (as there is no value to retrieve), but `has_value()`, `error()`, and the monadic interface are all available, making its usage consistent with the standard `expected`.

## Performance Comparison with Exceptions: Is It Really Zero-Overhead?

A slogan inseparable from `expected` is "zero-overhead abstraction." However, we need to clarify exactly what "zero-overhead" means. We mean that **on the expected success path (happy path), `expected` does not introduce the exception registration/stack unwinding mechanisms found in exceptions; the control flow consists of standard branches and returns**. We cannot simply assume this holds true; we must run the code to verify.

::: warning Benchmark Methodology
The micro-benchmarks below measure the overhead of the "error handling mechanism itself." To ensure this signal isn't drowned out, the function body is intentionally kept extremely lightweight (a single multiply-add operation), and marked with `[[gnu::noinline]]` to prevent the compiler from optimizing away the entire loop or amortizing costs across call boundaries. Compiler flags: `-std=c++23 -O2 -funwind-tables` (unwind tables are enabled, which is the default ABI configuration for most distributions). Absolute numbers will fluctuate based on the machine, frequency, and cache state. We are focusing on the **relative performance gap between the two under different failure rates**, and the trend is robust.
:::

First, let's look at the happy path—when **all calls succeed and no errors occur**—which style is faster:

```cpp
// Standard: C++23
[[gnu::noinline]] std::expected<int, int> compute_expected(int x) {
    if (x < 0) return std::unexpected(-1);
    return x * 7 + 3;
}
[[gnu::noinline]] int compute_throw(int x) {
    if (x < 0) throw std::runtime_error("neg");
    return x * 7 + 3;
}
// 各跑 200'000'000 次,全部传非负数(永不失败/永不抛)
```

Actual measurements (three samples taken as representative values; absolute values vary by machine, observe the trend):

```text
expected happy : 343 ms
throw    happy : 350 ms
```

The two are nearly neck and neck—the difference is lost in measurement noise. `expected` involves slightly more branching, but with modern "zero-cost exception table" implementations, `throw` adds virtually no instructions when not thrown. Therefore, **on the happy path, `expected` is no more expensive than `throw`; they are in the same performance tier**, and this is confirmed.

The real difference lies in the **failure frequency**. A failure in `expected` is just a normal "return an unexpected value," costing the same as a standard return. In contrast, a failure with `throw` requires "throwing → looking up the exception table → stack unwinding → destroying objects along the way → entering the catch block." This process is far more expensive than a normal return, and **the more frequent the failures, the more dramatic the gap becomes**. Keeping the lightweight happy-path function fixed and artificially controlling the failure frequency yields:

```text
--- 1/100000 fail rate (罕见失败) ---
expected fail-every-100000: 273 ms
throw    fail-every-100000: 216 ms
--- 1/1000 fail rate (中等) ---
expected fail-every-1000: 225 ms
throw    fail-every-1000: 469 ms
--- 1/10 fail rate (频繁失败) ---
expected fail-every-10: 491 ms
throw    fail-every-10: 38037 ms
```

Three scenarios, covering three distinct situations:

1. **Rare failures (1 in 100,000)**: Both are comparable, with `throw` being slightly faster—because exceptions are rarely thrown, the exception handling mechanism isn't triggered at all, whereas `expected` incurs an extra `has_val` branch. This validates the old adage that "exceptions are for truly exceptional conditions": for errors that rarely occur, exceptions impose no overhead on the happy path.
2. **Moderate failures (1 in 1,000)**: `throw` starts to lag significantly—the cumulative cost of stack unwinding makes it twice as slow as `expected`.
3. **Frequent failures (1 in 10)**: `throw` completely blows up, with 38,037 ms versus 491 ms—**nearly two orders of magnitude slower**. This is the scenario where `expected` shines: when "failure" is a regular occurrence rather than an exceptional one (e.g., parsing user input, network retries, or cache misses), the cost model of exceptions collapses. `expected` replaces the entire stack unwinding process with a simple return, creating a massive performance gap.

The conclusion isn't that "`expected` is always faster than exceptions"—on the happy path, they are tied, and for rare failures, exceptions aren't a disadvantage. The conclusion is that the **frequency of errors** determines which tool to use: use exceptions for rare occurrences (trading the potential cost of stack unwinding for cleaner code without manual error checking at every layer), and use `expected` for frequent occurrences (where the cost of failure is a constant-time return). Understanding this distinction helps us recognize where `expected` truly helps and where it just adds extra typing.

## How to choose the Error Type `E`

One final practical question: what should we use for `E`? Previous examples used enums, `std::string`, and `std::error_code`. There is a simple hierarchy for selection.

**Lightest: `enum class`**. An enumerator value is typically 4 bytes, carrying extra information only if you need it. Suitable for scenarios where "error types are limited and no additional data is needed," such as protocol parsing or state machines:

```cpp
// Standard: C++23
enum class io_error { kOk, kTimeout, kClosed, kAgain };
std::expected<int, io_error> read(int fd);
```

**Standard Library Friendly: `std::error_code`**. This is the standard error code carrier in C++, capable of integrating seamlessly with `<system_error>`, filesystem APIs, and platform errors. We will cover the mechanism of `error_code` in detail in Chapter 66 (how to check, how to categorize, and how it works with `system_category`/`errc`). For now, you just need to know: using `error_code` as `E` means your errors can directly interface with the extensive existing error codes of the standard library and system calls. Construct it using `std::make_error_code(std::errc::...)`:

```cpp
// Standard: C++23
std::expected<int, std::error_code> read_with_ec(bool ok) {
    if (!ok)
        return std::unexpected(std::make_error_code(std::errc::timed_out));
    return 42;
}
auto r = read_with_ec(false);
if (!r) std::cout << r.error().value() << ": " << r.error().message() << '\n';
```

```text
110: Connection timed out
```

That `110` is the POSIX `ETIMEDOUT` error number, and `message()` provides a human-readable description. This demonstrates the benefit of `error_code` directly interfacing with system error codes: you don't need to maintain your own mapping of numbers to strings.

**Most informative: custom error types**. When an error needs to carry structured information (error code + human-readable message + context), define a struct to serve as `E`. The cost is size, but if error information is truly useful along your function's return path, this overhead is worth it:

```cpp
// Standard: C++23
struct AppError {
    int code;
    std::string msg;
};
std::expected<int, AppError> read_app(bool ok) {
    if (!ok) return std::unexpected(AppError{5003, "connection reset"});
    return 42;
}
```

The trade-off in selection boils down to one sentence: **the smaller `E` is, the faster (every `expected` carries the size of `E`); the richer `E` is, the more usable (callers get more debugging info)**. For local tools, embedded systems, or hot paths, prioritize enums or `int`; for cross-module, public APIs, or system error integration, prioritize `error_code`; for structured errors requiring context, use custom types.

## Common Pitfalls

Let's consolidate the common places where things go wrong:

::: warning Don't Forget `std::unexpected`
A failed `expected` must be wrapped with `std::unexpected(e)`, you cannot directly `return e;`. As discussed earlier: since both `T` and `E` can be implicitly constructed, returning `E` directly leaves the compiler unable to distinguish whether you intend a value or an error, leading to either compilation failures or semantic ambiguity. Remember the symmetric rule: "return raw value for success, wrap in `unexpected` for failure."
:::

::: warning `operator*` Does Not Check
`*x` and `x->` invoke undefined behavior on a failed state; they are "I trust you" zero-overhead accessors. If you aren't sure whether a value exists, don't use them. Use `value()` (throws `bad_expected_access<E>` on failure) or `value_or(default)` (never throws).
:::

::: warning `and_then` Callbacks Must Return `expected`
`and_then(f)` requires `f` to return `expected`, as it expresses "the next step might also fail." If your `f` cannot fail and only transforms the value, use `transform(f)`, where `f` returns a normal value. Getting this backwards—putting a lambda returning a normal value in `and_then`, or a lambda returning `expected` in `transform`—will result in a compilation error. This is the type system guarding you; just understand the error message.
:::

::: warning The Size of `E` Bleeds into Every `expected`
When `E` is a large type like `std::string`, every `expected<T, std::string>` carries the overhead of a string's size. On hot paths where `expected` objects are stored in large arrays, this overhead is amplified. Accept it if you need rich information, or switch to a smaller `E` if you can't.
:::

::: warning Monadic `E` Types Must Align
All `expected` instances in an `and_then` chain must have the same `E` type (or be implicitly convertible), otherwise the types won't connect. When crossing subsystems where error types differ, either unify `E` or use `transform_error` at the boundary to explicitly translate `E` into the type expected by the next stage.
:::

## Summary

`std::expected<T, E>` turns "value or error" into a type. Its core value is elevating errors from implicit control flow (exceptions) or vague channels (return codes) into **compile-time visible, type-safe, explicitly handled values**. Here are the key takeaways:

- **Construction**: Return `T` raw for success (implicit wrap), and `std::unexpected(e)` for failure (eliminates success/failure ambiguity). **Access**: `*x`/`x->` are unchecked (failure is UB), `x.value()` throws `bad_expected_access<E>` on failure, `x.value_or(default)` never throws, and `x.error_or(default)` is the safety net for the error side.
- **C++23 Monadic Operations**: These are the real killer features: `and_then` (next step might fail), `transform` (change value, cannot fail), `or_else` (failure fallback), and `transform_error` (rewrite error). They compress layers of "if-fail" checks into an **auto-short-circuiting** chain. Mnemonic: success branch uses `and_then`/`transform`, failure branch uses `or_else`/`transform_error`; return `expected` for `and_then`/`or_else`, return normal values for `transform`/`transform_error`.
- **Relationship with `optional`/`variant`**: `expected` ≈ `optional + error info`. Structurally, it is a "success/failure" semantic specialization of `variant<T,E>` that never enters a valueless state. `expected<void,E>` is a partial specialization specifically for operations that "don't return a value, only care about success."
- **Performance (Empirical GCC 16.1.1)**: On the happy path, `expected` is on par with `throw`, both at zero-overhead levels. **The gap is determined by failure frequency**—for rare failures, exceptions don't lose much; for frequent failures, `expected` is nearly two orders of magnitude faster. The more "routine" the error, the more you should use `expected`.
- **Choosing `E`**: It's a trade-off between size and richness. Enums are lightest, `error_code` interfaces with the standard library and system errors (detailed in Post 66), and custom types carry the most info. Prioritize small `E` for hot paths, and `error_code` for cross-module/public APIs.

In the next post, we will dive into the `error_code` lineage—unpacking the mechanisms of `category`/`errc`/`system_category` behind `E = error_code`, and seeing how the standard library bundles "error code + category + readable message" into a lightweight object.
