---
chapter: 7
cpp_standard:
- 20
description: 'A deep dive into std::source_location — how current() captures file/line/func/column
  all at once; why a default argument automatically injects the call site; where its
  type safety wins over the __FILE__/__LINE__ macros; how to verify its zero-overhead
  constexpr nature; and the classic pitfall of default argument vs. first line of the
  function body'
difficulty: intermediate
order: 68
platform: host
prerequisites:
- 'expected: Value or Error, C++23''s New Error Handling Paradigm'
- 'optional: Making "Maybe Nothing" a Type'
reading_time_minutes: 12
related:
- 'stacktrace: C++23 Finally Standardizes Call Stack Capture'
tags:
- host
- cpp-modern
- intermediate
- 基础
title: 'source_location: Compile-Time Code Location, a Type-Safe Alternative to __FILE__'
translation:
  source: documents/vol3-standard-library/error-utils/68-source-location.md
  source_hash: d74c82cc035e8645969768ee0c43af5d507e2d067815e3302d0b63b76fed5364
  translated_at: '2026-09-26T01:04:14+00:00'
  engine: anthropic
  token_count: 4300
---
# source_location: Compile-Time Code Location, a Type-Safe Alternative to __FILE__

Anyone who has written logging, assertions, or a test framework has hand-written this kind of macro:

```cpp
#define LOG(msg) std::cout << __FILE__ << ":" << __LINE__ << " " << msg << '\n'
```

It works, but it hurts in a dozen ways. `__FILE__` is a `const char*`, `__LINE__` is an `int`, and `__func__` is yet another beast — if you want to pass "the location of this call" to a function as a whole, you have to stitch three or four macros together by hand, and the types are all bare strings and integers with zero protection. Worse, macros have no column number, cannot form a value object at compile time, and the moment macro calls nest, they point at the wrong location.

C++20 delivers a standardized, type-safe replacement: `std::source_location`. It packs "which file, which line, which function, which column" into a single object you grab in one shot, and it is `constexpr` — evaluated at compile time, zero runtime overhead. In this article we take it apart and run it end to end: first how `current()` captures the location, then a thorough look at the "default argument injection" pattern that dominates real usage, and finally we draw clear boundaries against macros and against the runtime `stacktrace` covered in the next article.

## current(): File / Line / Func / Column in One Shot

The single entry point of `source_location` is a static member function `current()`, which returns a `source_location` object representing "the place of the call". The object exposes four query interfaces:

```cpp
// Standard: C++20
#include <iostream>
#include <source_location>
#include <string_view>

void print_loc(const std::source_location& loc = std::source_location::current()) {
    std::cout << "file_name     : " << loc.file_name() << '\n';
    std::cout << "line          : " << loc.line() << '\n';
    std::cout << "column        : " << loc.column() << '\n';
    std::cout << "function_name : " << loc.function_name() << '\n';
}

int main() {
    print_loc();
}
```

Running it under `g++ -std=c++20 -O2` (GCC 16.1.1 on this machine):

```text
file_name     : /tmp/sloc/basic.cpp
line          : 15
function_name : int main()
column        : 14
```

All four pieces in one shot: file name, line number, column number, and function signature. Note that `function_name()` does not hand you the bare name the way `__func__` does — it gives the **full function signature** (`int main()`). That is especially useful for templates and overloads, and we will put it to the test later.

One detail worth flagging up front: `current()` is `constexpr`, and the object it returns is itself a literal type, so it can appear in constant expressions. This is what makes it "zero overhead" in nature — we will verify that with assembly shortly.

## Why Not Macros: Type Safety and All-at-Once Capture

Put `source_location` next to the traditional macros and the differences are stark:

| Dimension | `__FILE__` / `__LINE__` / `__func__` | `source_location` |
|---|---|---|
| Type | `const char*` / `int` / `const char*`, each its own thing | One object: `string_view` + `unsigned` |
| Completeness | File + line + function name, **no column** | File + line + column + function signature |
| Evaluation time | Preprocessing / compile time | `constexpr`, compile time |
| Passable as a whole | No — you stitch several macros by hand | Yes — just pass one object |
| Sensitive to macro expansion | Nested macro calls point at the wrong location | Determined by the real call site, stable |

Code makes this clearest:

```cpp
// Standard: C++20
#include <iostream>
#include <source_location>

constexpr int line_of(std::source_location loc = std::source_location::current()) {
    return loc.line();
}

// static_assert proves: current()'s result is already fixed at compile time
static_assert(line_of() == __LINE__, "line_of must be usable in constant expression");

#define LEGACY_LOG() \
    std::cout << "[legacy] " << __FILE__ << ":" << __LINE__ \
              << " (no func, no col)\n"

int main() {
    constexpr int here = line_of();
    std::cout << "constexpr line_of() = " << here << '\n';
    LEGACY_LOG();
}
```

Output:

```text
constexpr line_of() = 20
[legacy] /tmp/sloc/constexpr.cpp:23 (no func, no col)
```

Two observations. First, the result of `line_of()` feeds straight into `static_assert`, which means a `source_location` is a value already fixed at compile time — nobody is consulting a symbol table at runtime; the compiler "welds" the location into the constant as it generates the call. Second, the `LEGACY_LOG` line only gives "file:line" — no function name, no column; retrieving those would mean stacking `__func__` and friends on top.

"Welded into the constant" is not a figure of speech — let's check the assembly. Compile the program above with `-O2` and grep for symbol calls related to `source_location`:

```text
$ g++ -std=c++20 -O2 -S -o constexpr.s constexpr.cpp
$ grep -c "current" constexpr.s
0
```

`current` never appears in the assembly — the compiler evaluated it fully at compile time and folded it into a constant. Passing a `source_location` argument costs the same as passing a few `int`s or `string_view`s, with no extra runtime call. That is what "zero overhead" means in practice: not "the overhead is tiny", but "under `-O2` it simply does not exist".

Want to watch `current` get folded away at compile time yourself? Open the online demo below and inspect the assembly (`allow-x86-asm`): `current` is never called — the location was "welded" into constants during compilation:

<OnlineCompilerDemo
  title="Zero Overhead of source_location: current Vanishes in the Assembly"
  source-path="code/examples/vol3/68_source_location.cpp"
  description="static_assert proves current() is evaluated at compile time; inspecting the x86-64 assembly shows current is never called — the location is welded into constants at compile time, zero runtime overhead"
  allow-x86-asm
/>

## Default Argument Injection: The Most Common Pattern

The real killer feature of `source_location` is "sitting in a function's default argument and auto-injecting the call site". It is exactly the pattern from the `print_loc` at the top:

```cpp
void print_loc(const std::source_location& loc = std::source_location::current()) {
    // ...
}
```

The elegance of this design: **default arguments are evaluated at the "call site", not at the "function definition"**. That has always been the semantics of C++ default arguments — every time `print_loc()` is called without arguments, the compiler generates one `source_location::current()` at the call site. So `loc` naturally encodes "who called `print_loc`", without the caller having to pass a location explicitly.

This completely changes how we write logging and assertions. Let's build a `log_info` that carries the location and an `expect` that prints the location on failure:

```cpp
// Standard: C++20
#include <iostream>
#include <source_location>
#include <string_view>

void expect(bool cond, std::string_view msg,
            std::source_location loc = std::source_location::current()) {
    if (!cond) {
        std::cerr << loc.file_name() << ':' << loc.line()
                  << " in " << loc.function_name()
                  << ": CHECK FAILED: " << msg << '\n';
    }
}

void log_info(std::string_view msg,
              std::source_location loc = std::source_location::current()) {
    std::cout << "[INFO " << loc.function_name() << ":" << loc.line()
              << "] " << msg << '\n';
}

int divide(int a, int b) {
    expect(b != 0, "除数不能为零");
    log_info("doing division");
    return a / b;
}

int main() {
    log_info("程序启动");
    std::cout << "10 / 2 = " << divide(10, 2) << '\n';
    divide(10, 0);   // trigger the failing assertion
}
```

Output:

```text
[INFO int main():30] 程序启动
[INFO int divide(int, int):25] doing division
10 / 2 = 5
/tmp/sloc/expect.cpp:24 in int divide(int, int): CHECK FAILED: 除数不能为零
[INFO int divide(int, int):25] doing division
```

Note the details: the caller's single line `log_info("...")` doesn't have to care about the location at all — it is injected automatically; `function_name()` gives the **full signature** — `int divide(int, int)` — which is particularly friendly to overloads and templates; and the failing assertion prints the exact line of the `expect` call inside `divide`, pinning down the bug in one step.

This is the core usage of `source_location`: **turning "I want to know where my caller is" from macro magic into an ordinary default argument**. Where you used to write an eyesore like `EXPECT(cond, msg)`, you now write a plain function — type-safe, overloadable, steppable in the debugger. It does everything the macro could do, without polluting any namespace.

### Why Default Arguments Can Capture the Call Site

This is worth pausing on to get the mechanism straight, or you will step into a trap the moment the context changes.

In the C++ standard, default arguments are evaluated at the "call site", not at the "function definition". The rule was always there; it just never had a serious job before. The standard semantics of `source_location::current()` happen to be "return the location that invokes it" — and when it appears in a default argument, that "location that invokes it" is **the line written by the caller of `print_loc`**.

In other words, automatic injection works because two rules stack: "default arguments are evaluated at the call site" plus "`current()` returns the call site location". Once you understand that, you can predict the classic pitfall coming next.

## The Classic Pitfall: Default Parameter vs. First Line of the Function Body

Write `current()` in different places and the locations you get are completely different. This is where `source_location` flips over most easily, so let's compare them head-to-head:

```cpp
// Standard: C++20
#include <iostream>
#include <source_location>

// Mode A: current() as a default argument — captures the [call site] location
void log_default(std::source_location loc = std::source_location::current()) {
    std::cout << "[A 默认参数] line=" << loc.line()
              << " func=" << loc.function_name() << '\n';
}

// Mode B: call current() on the first line of the body — captures the [current function] location
void log_inline() {
    std::source_location loc = std::source_location::current();
    std::cout << "[B 函数体首行] line=" << loc.line()
              << " func=" << loc.function_name() << '\n';
}

int main() {
    log_default();   // expected: the line number points at this line
    log_inline();    // expected: the line number points inside log_inline
}
```

Output:

```text
[A 默认参数] line=19 func=int main()
[B 函数体首行] line=13 func=void log_inline()
```

The difference is plain at a glance:

- **Mode A** (default argument): `loc` is evaluated at the point of call on line 19 inside `main`, so it reports line 19 of `int main()` — the **call site**.
- **Mode B** (first line of the function body): `current()` is invoked on line 13 inside `log_inline`, so it reports line 13 of `void log_inline()` — **the current function itself**.

Both styles have their uses, but mixing them up breeds bugs. In the vast majority of scenarios you want "who called me" — that is Mode A, no exceptions. If you genuinely want to record "where this function itself is" (a function-entry log, say), then Mode B is the right one. The rule of thumb is simple: **`current()` reports the line it is written on** — a default argument is evaluated at the call site, while a `current()` inside the function body is that body's line.

::: warning current() must sit in a default argument to capture the call site
If you want a logging function that automatically records the caller's location, `current()` has to live in the **default argument**. Slip and write `auto loc = std::source_location::current();` as the first line of the function body, and you will forever capture the logging function's own line instead of the caller's — every log entry points at the same place, completely destroying their locating value. This is the number-one `source_location` trap; anyone who has stepped in it remembers.
:::

## function_name() Returns the Signature, Not Just the Name

We have already seen that `function_name()` returns the full signature. That is especially useful for member functions and templates, so let's verify it separately:

```cpp
// Standard: C++20
#include <iostream>
#include <source_location>

struct Tracker {
    void method(int x,
                std::source_location loc = std::source_location::current()) {
        std::cout << "member func call from: " << loc.function_name()
                  << " @ line " << loc.line() << '\n';
    }
};

template <typename T>
void tpl_func(T,
              std::source_location loc = std::source_location::current()) {
    std::cout << "template func call from: " << loc.function_name()
              << " @ line " << loc.line() << '\n';
}

int main() {
    Tracker t;
    t.method(42);
    tpl_func(7);
}
```

Output:

```text
member func call from: int main() @ line 23
template func call from: int main() @ line 24
```

Note that what is verified here is the behavior in default-argument mode: because `current()` sits in the default argument, `function_name()` reports the **caller** `int main()`, not `method` or `tpl_func` themselves. This confirms the "default argument = call site" rule once more — whether the called function is a plain function, a member function, or a template, what gets injected is the call site.

If you want the called function to record its own signature (entry instrumentation, say), go back to Mode B above and call `current()` inside the function body — only then does `function_name()` give the called function's own signature, such as `void Tracker::method(int)` or `void tpl_func<int>(int)`. The two serve different purposes; don't mix them up.

## Drawing the Line Against stacktrace

At this point we absolutely must separate `source_location` from the runtime `std::stacktrace` (C++23) covered in the next article. Both are "about code location", but they are two completely different levels of thing:

| Dimension | `source_location` (C++20) | `stacktrace` (C++23) |
|---|---|---|
| Granularity | A single point: the line of the call | The entire call stack, many frames |
| Evaluation time | Compile time `constexpr`, zero overhead | Runtime, consults the symbol table |
| Overhead | None — folded into a constant | Real — stack frame unwinding + symbolization |
| Dependencies | None, pure language feature | Needs symbolization support linked in (e.g., libstdc++'s `_GLIBCXX_USE_BACKTRACE`) |
| Typical uses | Logging, assertions, contracts, debug instrumentation | Printing the call chain on crash, deep-stack diagnosis |

One sentence to sum up: `source_location` answers "where is this line", while `stacktrace` answers "how did I get to this line". The former is compile-time, zero-overhead, single-point — right for attaching to every log message; the latter is runtime, costly, whole-stack — right for firing once when something goes wrong. For 99% of everyday "logging/assertions with location" needs, `source_location` is enough; don't roll out the heavyweight `stacktrace`.

## A Few Traps People Actually Hit

::: warning Where current() sits decides everything
`current()` in a default argument → you get the call site; in a function body → you get the current function itself. For auto-injected logging, it must go in the default argument. This is the number-one trap — see the measured comparison above.
:::

::: warning Don't forget the const& default argument
Written as `std::source_location loc = std::source_location::current()`, the default argument passes by value — fine here, because `source_location` is tiny (`sizeof` is only 8 bytes on libstdc++). But if you define a custom wrapper type holding extra state, remember to use a `const std::source_location&` default argument to avoid copies — the standard library's `source_location` itself is trivial, so by value is fine as-is.
:::

::: warning The #line directive rewrites source_location too
Like `__FILE__` and `__LINE__`, `source_location` is affected by the `#line` directive. Generated code (yacc/lex, template generators) is full of `#line` remapping, and the line numbers and file names `source_location` reports follow along. We verified this in practice:

```text
$ #line 100 "fake.cpp" afterwards
source_location line=100 file=fake.cpp
```

This is actually good for debugging generated code (locations point at the source template rather than the generated result), but know that this behavior exists, or a "filename that doesn't exist" will leave you scratching your head.
:::

::: warning current() on MSVC has a history of pitfalls
GCC and Clang support `current()` in default arguments stably. But older MSVC (before VS 2019 16.10) had a bug in the implementation of "calling `current()` inside a default argument", and the captured location came out shifted. If your code has to run cross-platform, make sure the MSVC version is new enough (fixed in VS 2022 17.0+) — otherwise log locations will be scrambled on Windows. This series standardizes on GCC 16.1.1; on Linux there is no such problem.
:::

## Summary

`std::source_location` turns "where is this code" from a pile of macros into one type-safe object. Let's collect the key conclusions:

- `current()` captures all four pieces in one shot: `file_name()` (a `string_view`), `line()`, `column()`, and `function_name()` (the full signature, a `string_view`) — the column number and full signature that the `__FILE__`/`__LINE__`/`__func__` trio lacks.
- The core usage is **default argument injection**: write `std::source_location loc = std::source_location::current()` as the function's last default argument; the caller does nothing, and the location automatically points at the call site.
- `constexpr` + zero overhead: evaluated at compile time; under `-O2`, `current` never appears in the assembly at all — passing a `source_location` is as cheap as passing a few `int`s.
- The number-one trap: `current()` in a default argument gets the call site, in a function body it gets the current function itself — for auto-injected logging, always use the default argument.
- Division of labor with `stacktrace` (C++23): `source_location` is compile-time, single-point, zero-overhead, right for putting a location on every log line; `stacktrace` is runtime, whole-stack, with real overhead, right for crash diagnosis. For everyday logging and assertions, `source_location` is enough.

The typical uses form a single thread — **logging, assertions, contracts, test frameworks**: anywhere you "want to know at runtime which spot in the code this information came from", it should replace your hand-rolled `__FILE__` macros.

In the next article we cover the runtime `std::stacktrace` (C++23): how to capture the complete call chain, and how it cooperates with `source_location` — one handles "single-point, zero-overhead instrumentation", the other "whole-stack backtraces when things go wrong".

## References

- [cppreference: std::source_location](https://en.cppreference.com/w/cpp/utility/source_location) — the standard semantics of `current()` / `file_name()` / `line()` / `column()` / `function_name()` (C++20)
- [cppreference: Default arguments](https://en.cppreference.com/w/cpp/language/default_arguments) — "default arguments are evaluated at the call site" is the mechanism underlying `current()` injection
- [P1208R6: source_location](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p1208r6.pdf) — the proposal that brought `source_location` into C++20, with the design motivation and the original intent of "replacing the macros"
