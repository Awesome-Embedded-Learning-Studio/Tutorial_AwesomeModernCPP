---
chapter: 7
cpp_standard:
- 23
description: A deep dive into std::expected — promoting errors from exceptions/return
  codes into a type, the everyday construction and access patterns, how and_then/transform/or_else
  chain "operations that may fail" into one short-circuiting pipeline, its relationship
  with optional/variant, and the measured performance gap against exceptions at different
  failure rates
difficulty: advanced
order: 64
platform: host
prerequisites:
- 'optional: Making "Maybe Nothing" a Type'
- 'variant: Type-Safe Unions and visit'
related:
- 'filesystem: C++17 Cross-Platform Filesystem Operations'
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
  translated_at: '2026-09-26T00:53:28+00:00'
  engine: anthropic
  token_count: 5600
---
# expected: Value or Error, C++23's New Error Handling Paradigm

At this point we already have `optional` for "might not be there" and `variant` for "might be A or B". But there is an even more common scenario we can't dodge: **a function either returns a value, or returns an error explaining "why it didn't work out"**. C++ has historically handled this rather awkwardly, so let's put the pain points on the table first.

The three most common approaches each carry a real defect. The first is throwing exceptions: `throw std::runtime_error(...)` — control flies away instantly, and at the call site you can't tell this function throws at all. Exceptions are "implicit control flow"; when you read the code, they aren't written in the signature. And even when nothing is ever thrown, the exception machinery itself carries potential overhead on some implementations — table registration, stack unwinding — and compilers optimize exception-bearing code more conservatively. The second is return codes: `int rc = foo(); if (rc < 0) ...`. Explicit, sure, but the error information and the return value get squeezed through the same channel: whether the `int` you got is a "result" or an "error code" depends entirely on convention, and **it is far too easy to forget to check** — one call site missing an `if`, and the error silently slips away. The third is output parameters, `bool foo(int& out)`: a reference gets shoehorned into the signature, the return-value slot is spent on a boolean, and chaining is flatly impossible.

C++23 delivers a clean answer: **make "value or error" a type**. `std::expected<T, E>` holds either an expected value `T` or an unexpected value `E`. In this article we take it apart completely — from construction and access, to C++23's monadic chaining (expected's real killer feature), to measured performance comparisons against exceptions. One conclusion up front to set the stage: **expected is not about "replacing exceptions" — it is about promoting errors from implicit control flow to explicit types, so that the compiler forces you to deal with them**.

## A Minimal Example: Parse or Fail

Let's go straight to the classic `expected` example from cppreference — parsing a string into a number, returning a `double` on success, or an enum explaining the reason on failure:

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
        return std::unexpected(parse_error::kInvalidInput);   // failure: wrap it in unexpected
    if (std::isinf(retval))
        return std::unexpected(parse_error::kOverflow);

    str.remove_prefix(end - begin);
    return retval;                                            // success: return the value directly
}
```

Two things matter when reading this code. **On success, just `return retval;`** — `expected` has an implicit constructor, so a `T` can be wrapped directly into a successful `expected<T,E>`. **On failure, `return std::unexpected(error);`** — `std::unexpected` is an explicit wrapper whose sole job is telling `expected` "this is an error, not a value". This is deliberate: inside `expected<double, parse_error>`, both `double` and `parse_error` can be implicitly constructed, so if you could also `return parse_error{...}` directly, the compiler would have no way to tell success from failure. The standard library therefore forces you to wrap the error in `std::unexpected`, eliminating the ambiguity.

How does the caller use it? Let's run it (GCC 16.1.1, `-std=c++23 -O2`):

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

Compare this against the return-code style and notice where the difference lies: **the return type `expected<double, parse_error>` writes "this can fail, and here are the possible failure reasons" directly into the signature**. When a caller receives an `expected` and uses the value without checking `has_value()`, the compiler can't save you much either (more on that below) — but at least the type is sitting right there, telling you this is "something that needs a check", unlike a bare `int` return code cosplaying as an ordinary result. The fact that `"42abc"` parses as 42 is because `strtod` does prefix parsing: it reads one number and returns, leaving the rest in the string — that has nothing to do with expected; it's `strtod`'s semantics.

## Construction and Access: The Common Patterns

First, a concentrated pass over the construction and access patterns you'll use day to day, so the monadic discussion later doesn't stall on basic syntax.

### Construction

A successful `expected` takes the value directly; a failed one wraps it in `std::unexpected`:

```cpp
// Standard: C++23
std::expected<int, std::string> ok = 7;                       // success
std::expected<int, std::string> bad = std::unexpected("disk full");  // failure
```

Note the `bad` line — `std::unexpected("disk full")` holds a string literal, but the eventual `E` is `std::string`; this goes through `std::unexpected`'s implicit constructor, and the literal gets converted to `std::string`. This matters: `unexpected` itself forwards whatever you pass in to `E`'s constructor, so you don't have to hand-write `std::string("...")` every time.

### Access: `operator*` / `value()` / `value_or()`

There are three ways to get the value, each with different semantics — on this point it is nearly identical to `optional`:

```cpp
// Standard: C++23
std::expected<int, std::string> ok = 7;
std::expected<int, std::string> bad = std::unexpected("disk full");

std::cout << "*ok = " << *ok << '\n';              // dereference: unchecked, UB on failure
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

The difference between the three, in one sentence: **`*x` means "I guarantee it has a value, hand it over" (undefined behavior on failure, zero overhead); `x.value()` means "I hope it has a value, otherwise throw" (a failed one throws `bad_expected_access<E>`); `x.value_or(default)` means "take it if present, otherwise use the default" (never throws — the right choice when a sensible fallback exists)**.

::: warning Dereference Doesn't Check — Don't Use It on a Failed State
`operator*` and `operator->` are **unchecked**. Calling `*x` on a failed `expected` is undefined behavior — in the standard library, it is literally "I trust you, here's the internally stored value". If you aren't sure whether it holds a value, check `has_value()` first, use the throwing `value()`, or use the fallback `value_or()`. On this point `optional` and `expected` are exactly alike — step on it once and the lesson sticks.
:::

The exception type `value()` throws deserves a dedicated note — it's not `std::runtime_error` but `std::bad_expected_access<E>`, an exception class that carries the error value `E` along. So after you `catch` it, you can still extract the error value from the exception:

```cpp
// Standard: C++23
std::expected<int, std::string> bad = std::unexpected("disk full");
try {
    (void)bad.value();                          // failure -> throws
} catch (const std::bad_expected_access<std::string>& e) {
    std::cout << "value() threw, error=" << e.error() << '\n';
}
```

```text
value() threw, error=disk full
```

This is a rather elegant bridge between expected and exceptions: on ordinary days `value()` walks the happy path at zero cost, and when things genuinely go wrong you still get structured error information instead of a lone `what()` string.

### The Error-Side Symmetry: `error()` and `error_or()`

Nearly everything the value side has, the error side has too. `error()` fetches the error value (UB on a successful state), and `error_or(default)` supplies a default error in the success state. This symmetry comes up again and again once we start writing monadic code:

```cpp
// Standard: C++23
std::expected<int, int> ok = 5;
std::expected<int, int> bad = std::unexpected(7);
std::cout << "ok.error_or(-99) = " << ok.error_or(-99) << '\n';   // -99 (success state)
std::cout << "bad.error_or(-99) = " << bad.error_or(-99) << '\n'; // 7
```

```text
ok.error_or(-99) = -99
bad.error_or(-99) = 7
```

## C++23 Monadic Operations: Chaining Operations That May Fail

At this point you might still be thinking: writing `if (has_value()) ... else ...` over and over is pretty verbose — how is it any better than checking return codes? What truly lets expected pull away from return codes is the four monadic operations C++23 gave it — `and_then` / `transform` / `or_else` / `transform_error`. They let you chain a sequence of "every step may fail" operations into one pipeline, where **any failing step short-circuits automatically, everything after it never runs, and the error travels all the way to the end**.

This is expected's core power, so let's treat it seriously.

### A Real Chain: Parse → Convert → Format

Suppose we need to take a user-input string, first parse it into a dollar amount, then convert that to cents (an integer, avoiding floating point), then format it into a display string. Every step can fail: parse failure, negative amount, formatting... The traditional style is `if` nested inside `if`:

```cpp
// Traditional style: layer upon layer of nested ifs
auto s = std::string_view{"42.5"};
auto parsed = parse_number(s);
if (!parsed) return error(parsed.error());
auto cents = to_cents(*parsed);
if (!cents) return error(cents.error());
auto text = format_amount(*cents);
```

Three steps is tolerable — what about five, or ten? This is the synchronous edition of the classic "callback hell". Chained with `and_then`, it becomes one linear expression:

```cpp
// Standard: C++23
auto to_cents(double dollars) -> std::expected<long, parse_error> {
    if (dollars < 0)
        return std::unexpected(parse_error::kInvalidInput);  // negative amount -> failure
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

run("42.5");   // success flows all the way through
run("meow");   // parse fails; the next two steps never run at all
run("-1");     // parse succeeds (=-1), but to_cents rejects the negative -> failure
```

```text
"42.5" -> OK: 42.50 USD
"meow" -> ERR: 0
"-1" -> ERR: 0
```

Two things to internalize. **First, types flow naturally along the chain**: `parse_number` returns `expected<double,E>`; `and_then(to_cents)` takes a `double` and returns `expected<long,E>`, so the whole expression's type becomes `expected<long,E>`; then `transform(format_amount)` turns the inner `long` into a `string`, yielding `expected<string,E>`. The value type changes at each step, but the error type `E` stays the same throughout — which is exactly why the error can travel all the way to the end. **Second, the short-circuit is automatic**: in `run("meow")`, `parse_number` fails; `and_then` and `transform` see that the `expected` they received is in the failure state and pass the error through untouched — `to_cents` and `format_amount` are never called at all. This "continue on success, bypass on failure" semantics is precisely what you used to hand-write with a pile of `if`s, now compressed into one chain.

### What Each of the Four Operations Actually Does

Keep this semantic comparison of the four operations in mind and you'll never get lost choosing between them:

- **`and_then(f)`** — `f` takes the value and returns a **new `expected`**. This is for chaining "the next step may fail too" operations (`to_cents` above). `f`'s return type must be `expected<U, E>`, with the same E.
- **`transform(f)`** — `f` takes the value and returns a **plain value** (not an expected). This is for chaining "only transforms the value, cannot fail" operations (`format_amount` above). The result is `expected<U, E>`. Note that the difference from `and_then` is exactly `f`'s return type: if it can fail, use `and_then`; if it can't, use `transform`.
- **`or_else(f)`** — the reverse of `and_then`: `f` is invoked **only on failure**; it receives the error and returns a new `expected`. On success, the value passes through unchanged. This is for "remediate on failure / fall back".
- **`transform_error(f)`** — the reverse of `transform`: `f` is invoked **only on failure**; it receives the error and returns a **new error value** (which can be a new type). On success, the value passes through unchanged. This is for "rewriting / translating error messages".

A one-line mnemonic: **`and_then`/`transform` take the success branch (`and_then` returns an expected, `transform` returns a value); `or_else`/`transform_error` take the failure branch (`or_else` returns an expected, `transform_error` returns an error value)**. Two axes: which path you take × what you return.

### `or_else` and `transform_error`: The Failure-Side Chain

The success side is easy to grasp; these two on the failure side deserve a dedicated look, because in traditional error handling, "fallback on failure" and "error message massaging" are the most verbose parts.

`or_else` is the fallback — when failure hits, a fresh `expected` steps in:

```cpp
// Standard: C++23
auto with_fallback = parse_number("meow")
    .or_else([](parse_error) {
        return std::expected<double, parse_error>(0.0);   // parsing failed, so fall back to 0
    });
std::cout << "fallback value = " << *with_fallback << '\n';
```

```text
fallback value = 0
```

`transform_error` rewrites the error — on failure it converts the error `E` into another (usually more readable) form, and `E` in the return type changes accordingly. Below we translate the enum error into a numbered string, and along the way demonstrate that `E`'s type can change along the chain:

```cpp
// Standard: C++23
auto reworded = parse_number("meow")
    .transform([](double d) { return d + 1000.0; })       // success branch, but currently in the failure state -> not executed
    .transform_error([](parse_error e) {
        return std::string("bad number, code=") +
               std::to_string(static_cast<int>(e));
    });
// note: at this point E has changed from parse_error to std::string
std::cout << "reworded error = " << reworded.error() << '\n';
```

```text
reworded error = bad number, code=0
```

That `transform` looks a bit redundant — the input is in the failure state, so of course it doesn't run. It's there to make one thing clear: **each step in the chain decides whether to execute based on the current `expected`'s state, so you can string success-side and failure-side operations together and they won't interfere with each other**. The success-side `transform` is transparent in the failure state, and the failure-side `transform_error` is transparent in the success state.

## The Relationship with optional and variant

Put expected back into the standard library's family of type tools and its position becomes clear. Remember how, covering `optional`, we said "a value that might be absent", and, covering `variant`, "a type-safe union"? expected sits precisely between the two.

### expected ≈ optional + Error Information

`optional<T>` only tells you "present or not" — and says nothing at all when it's not. `expected<T, E>` stuffs in an extra `E` when absent, telling you **why it's absent**. Storage-wise, `expected<T,E>` is roughly "an `optional<T>` plus an error slot". When `E` is a lightweight type (an enum, an `int`), expected and optional cost nearly the same — let's measure it (GCC 16.1.1, `-std=c++23 -O2`):

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

All three are 8 bytes. Why so tidy? Because underneath they are all "one value + one discriminant bit" structures. `optional<int>` is an `int` plus a `bool` (implemented via some impossible value of the `int`, or an extra byte — implementations typically have `optional<int>` borrow one high byte, so it stays 8). `expected<int,int>` and `variant<int,int>` are both "two mutually exclusive members + one tag" — the two `int`s share one storage (union semantics), plus a tiny tag distinguishing which one is currently stored, so `int + int` is still 8. **Only when `E` is a large type needing its own storage (say, `std::string`) does expected grow bigger than optional**:

```cpp
// Standard: C++23
std::cout << "sizeof expected<double,std::string> = " << sizeof(std::expected<double,std::string>) << '\n';
std::cout << "sizeof expected<int,std::string>    = " << sizeof(std::expected<int,std::string>) << '\n';
```

```text
sizeof expected<double,std::string> = 40
sizeof expected<int,std::string>    = 40
```

40 bytes, because `std::string` (libstdc++'s SSO implementation) is itself 32 bytes, and adding the `double`/`int` slot and the tag fills out the alignment. The lesson here: **mind the size of `E` when choosing it**. Using `std::string` as the error type is information-rich, but every `expected` then hauls around a string's worth of extra bulk — a small, focused error type is the key to using expected economically.

### expected Is a Semantic Specialization of `variant<T,E>`

One level deeper, `expected<T,E>` is structurally just `variant<T, E>` — except it **assigns semantics to the two members**: the first is "the expected value", the second is "the unexpected error". `variant` is the neutral "A or B"; `expected` is the opinionated "success or failure". That semantic difference buys a whole interface tailored to error handling: the asymmetry of `value()`/`error()` (value access throws on failure, error access doesn't), the `value_or`/`error_or` fallbacks, and the monadic suite above. `variant` has none of this — to do the same things with `variant`, you'd have to write a stretch of `visit` code yourself.

One more structural detail worth flagging: **`expected` is never "valueless"**. cppreference's exact words are "expected is never valueless". `variant`, in certain extreme cases (a throw while in a value-carrying state, combined with no nothrow move constructor), can in theory enter the `valueless_by_exception` state. Because expected only ever holds a value or an error and doesn't depend on a type list, it simply doesn't have this pitfall.

### `expected<void, E>`: Error Only, No Value

There's a class of operations that return no value at all — all we care about is "did it work": closing a file, flushing a buffer, committing a transaction. Using `expected<T,E>` here is awkward — what do you put in for `T`? The standard library provides a partial specialization, `expected<void, E>`, dedicated to "succeeded (no value) or failed (with error)":

```cpp
// Standard: C++23
std::expected<void, int> vok;                              // success
std::expected<void, int> vbad = std::unexpected(42);       // failure, error 42
std::cout << "vok.has_value = " << vok.has_value()
          << "  vbad.has_value = " << vbad.has_value()
          << "  vbad.error = " << vbad.error() << '\n';
```

```text
vok.has_value = 1  vbad.has_value = 0  vbad.error = 42
```

The `void` specialization has no `operator*` / `value()` (there's no value to fetch), but `has_value()`, `error()`, and the monadic suite are all there — it feels just like an ordinary expected in use.

## Performance Against Exceptions: Is It Really Zero Overhead

One slogan you can't avoid when discussing expected is "zero overhead". But what "zero overhead" concretely means needs unpacking. What we mean is: **on the expected-success path (the happy path), expected doesn't introduce the exception machinery of table registration / stack unwinding — the control flow is ordinary branches and returns**. Whether that actually holds can't be settled by hand-waving; let's run it.

::: warning A Note on the Experiment Setup
The microbenchmarks below measure the overhead of the error-handling mechanism itself. To keep that signal from drowning, the function bodies are deliberately featherweight (one multiply-add) and marked `[[gnu::noinline]]` so the compiler can't optimize the whole loop away or flatten costs across call boundaries. Compilation flags: `-std=c++23 -O2 -funwind-tables` (exception unwind tables on — the default ABI configuration on most distributions). Absolute numbers will vary with machine, frequency, and cache state; what we're watching is **the relative gap between the two at different failure rates** — that trend is stable.
:::

First, the happy path — when **everything succeeds and not a single error occurs**, which style is faster:

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
// Each runs 200'000'000 times, always with non-negative input (never fails / never throws)
```

Measured (representative value from three runs; absolute values differ across machines — watch the trend):

```text
expected happy : 343 ms
throw    happy : 350 ms
```

The two are nearly tied, with the difference sitting inside measurement noise — expected carries slightly more branch checking, and under modern "zero-cost exception table" implementations, throw adds almost no extra instructions when nothing is thrown. So **on the happy path, expected is no more expensive than throw; they are in the same class** — that much is confirmed.

The real gap is in **failure frequency**. An expected failure is just an ordinary "return an unexpected value" — the cost is no different from a normal return. A throw failure, though, goes through "throw → consult the exception table → stack unwinding → destroy objects along the way → enter the catch", a sequence far more expensive than one ordinary return, and **the more frequent the failures, the more exaggerated the gap**. Keeping that lightweight happy-path function fixed and artificially controlling the failure rate, here is what comes out:

```text
--- 1/100000 fail rate (rare failures) ---
expected fail-every-100000: 273 ms
throw    fail-every-100000: 216 ms
--- 1/1000 fail rate (moderate) ---
expected fail-every-1000: 225 ms
throw    fail-every-1000: 469 ms
--- 1/10 fail rate (frequent failures) ---
expected fail-every-10: 491 ms
throw    fail-every-10: 38037 ms
```

Three ranges, three takeaways:

1. **Rare failures (one in a hundred thousand)**: the two are comparable, with throw even slightly faster — almost nothing is thrown, so the exception-table machinery never kicks in, while expected pays one extra `has_val` branch. This is why the old saying "exceptions are for truly exceptional cases" holds up: for errors that almost never happen, exceptions genuinely cost nothing on the happy path.
2. **Moderate failures (one in a thousand)**: throw starts lagging noticeably — the stack-unwinding cost of each throw accumulates to twice expected's total.
3. **Frequent failures (one in ten)**: throw explodes outright — 38037 ms versus 491 ms, **nearly two orders of magnitude slower**. This is exactly where expected belongs: when "failure" is the routine case rather than the exceptional one (parsing user input, network retries, cache misses), the exception model's failure cost simply can't keep up, while expected replaces the whole unwinding dance with one ordinary return — and the gap blows wide open.

So the conclusion is not "expected is always faster than exceptions" — on the happy path they tie, and with rare failures exceptions lose nothing either. The conclusion is that **the "frequency" of the error decides which to use**: for things that almost never happen, use exceptions (their potential unwinding cost buys you freedom from layer-after-layer of checks while writing code); for things that happen all the time, use expected (each failure costs a constant-factor single return). Get this judgment straight, and you'll know in which scenarios expected genuinely helps you — and in which it's just extra typing.

## How to Choose the Error Type E

One last practical question: what goes in `E`? Earlier examples used an enum, `std::string`, and `std::error_code`. There's a simple hierarchy to the choice.

**Lightest: `enum class`**. An enum value is typically 4 bytes, with extra information attached only if you need it. It suits scenarios where "there are only a few error kinds and no attached data is needed" — protocol parsing, state machines:

```cpp
// Standard: C++23
enum class io_error { kOk, kTimeout, kClosed, kAgain };
std::expected<int, io_error> read(int fd);
```

**Most standard-library-friendly: `std::error_code`**. This is C++'s standard vehicle for error codes, and it plugs into `<system_error>`, the filesystem API, and platform errors. Article 66 covers the `error_code` machinery in depth (how to test, how categories work, how it cooperates with `system_category`/`errc`); here all you need to know is: using `error_code` as `E` means your errors plug directly into the standard library's and system calls' whole existing body of error codes. Construct one with `std::make_error_code(std::errc::...)`:

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

That `110` is POSIX's `ETIMEDOUT` error number, and `message()` supplies a readable description — this is the benefit of `error_code` interfacing directly with system error codes: you don't have to maintain your own number-to-string mapping.

**Richest information: a custom error type**. When errors need to carry structured information (error code + human-readable message + context), define a struct to serve as `E`. The cost is bulk, but if error information genuinely proves useful on your function's return path, that overhead is worth it:

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

The trade-off boils down to one sentence: **the smaller E is, the faster (every expected carries one E's worth of bulk); the richer E is, the more convenient (the caller gets more debugging information)**. For local utilities, embedded work, and hot paths, prefer an enum or `int`; for cross-module boundaries, public APIs, and anything that must interface with system errors, prefer `error_code`; for structured errors with context, use a custom type.

## A Few Pitfalls People Actually Hit

Here's a consolidated list of the places where this journey tends to go wrong:

::: warning Don't Forget `std::unexpected`
A failed `expected` must be constructed by wrapping with `std::unexpected(e)` — you can't just `return e;`. The reason was covered earlier: both `T` and `E` are implicitly constructible, so returning an `E` directly leaves the compiler unable to tell value from error — either it won't compile, or the semantics land wrong. Remember the symmetric mnemonic: "success returns the bare value; failure wraps in unexpected".
:::

::: warning `operator*` Doesn't Check
`*x` and `x->` are undefined behavior on a failed state — they are the "I trust you" zero-overhead access. Don't use them when unsure whether there's a value; use `value()` (throws `bad_expected_access<E>` on failure) or `value_or(default)` (never throws) instead.
:::

::: warning `and_then`'s Callback Must Return an expected
`and_then(f)` requires `f` to return an `expected`, because what it expresses is "the next step may fail too". If your `f` can't fail and only transforms the value, use `transform(f)`, whose `f` returns a plain value. Get it backwards — a value-returning lambda inside `and_then`, or an expected-returning lambda inside `transform` — and it fails to compile. That's the type system screening for you; you just need to read the error message.
:::

::: warning E's Size Seeps Into Every expected
When `E` is a large type like `std::string`, every `expected<T, std::string>` hauls an extra string's worth of bulk. Line up large arrays of expecteds on a hot path and this overhead amplifies. Accept it if you can (rich information); if not, switch to a smaller E.
:::

::: warning The Monadic E Must Line Up
Every `expected` in an `and_then` chain must have the same `E` (or one implicitly convertible), or the types won't connect. When crossing subsystems with different error types, either unify E, or use `transform_error` at the seam to explicitly translate E into the type the next stage expects.
:::

## Summary

`std::expected<T, E>` makes "value or error" a type. Its core value is promoting errors from implicit control flow (exceptions) or an ambiguous channel (return codes) into **values that are visible at compile time, type-safe, and explicitly handled**. A few key conclusions to collect:

- Construction: success returns the bare `T` (implicitly wrapped); failure must use `std::unexpected(e)` (eliminating the success/failure ambiguity). Access: `*x`/`x->` are unchecked (UB on failure); `x.value()` throws `bad_expected_access<E>` on failure; `x.value_or(default)` never throws; `x.error_or(default)` is the error-side fallback.
- The four C++23 monadic operations are the real killer feature: `and_then` (the next step may fail), `transform` (only transforms the value, never fails), `or_else` (failure fallback), and `transform_error` (rewrites the error) — compressing "layer upon layer of failure-checking ifs" into one **automatically short-circuiting** chain. Mnemonic: the success branch is `and_then`/`transform`, the failure branch is `or_else`/`transform_error`; the expected-returning ones are `and_then`/`or_else`, the plain-value-returning ones are `transform`/`transform_error`.
- Relationship with optional/variant: `expected` ≈ `optional` + error information; structurally it is a "success/failure" semantic specialization of `variant<T,E>`, and it never enters a valueless state; the `expected<void,E>` partial specialization serves operations that "return no value and only care whether it worked".
- Performance (measured on GCC 16.1.1): on the happy path expected ties with throw, both in zero-overhead territory; **the gap is decided by failure frequency** — with rare failures exceptions lose nothing, while with frequent failures expected is nearly two orders of magnitude faster. The more "routine" the error, the more expected is the right tool.
- Choosing `E` is a small-versus-rich trade-off: enums are lightest, `error_code` interfaces with the standard library and system errors (detailed in article 66), and custom types carry the most information. Prefer a small E on hot paths; prefer `error_code` across modules and public APIs.

In the next article we follow the `error_code` thread — taking apart the `category`/`errc`/`system_category` machinery behind an `E` that is `error_code`, and seeing how the standard library packs "error code + category + readable message" into one lightweight object.
