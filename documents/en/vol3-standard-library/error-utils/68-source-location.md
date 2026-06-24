---
chapter: 7
cpp_standard:
- 20
description: 'A deep dive into `std::source_location`: how `current()` captures file,
  line, function, and column all at once; why using it as a default argument automatically
  injects the call site; where its type safety lies compared to the `__FILE__` and
  `__LINE__` macros; how to verify its zero-overhead `constexpr` nature; and the classic
  pitfall between default arguments and the first line of a function body.'
difficulty: intermediate
order: 68
platform: host
prerequisites:
- expected：值或错误，C++23 的错误处理新范式
- optional：把「可能没有」做成类型
reading_time_minutes: 12
related:
- stacktrace：运行时调用栈与符号化（C++23）
tags:
- host
- cpp-modern
- intermediate
- 基础
title: 'source_location: Compile-time code location, a type-safe alternative to `__FILE__`'
translation:
  source: documents/vol3-standard-library/error-utils/68-source-location.md
  source_hash: d74c82cc035e8645969768ee0c43af5d507e2d067815e3302d0b63b76fed5364
  translated_at: '2026-06-24T04:09:08.293854+00:00'
  engine: anthropic
  token_count: 2881
---
# source_location: Compile-Time Code Location, A Type-Safe Alternative to `__FILE__`

Anyone who has written logging, assertion, or testing frameworks has likely manually written macros similar to these:

```cpp
#define LOG(msg) std::cout << __FILE__ << ":" << __LINE__ << " " << msg << '\n'
```

It works, but it comes with plenty of rough edges. `__FILE__` is a `const char*`, `__LINE__` is an `int`, and `__func__` is something else entirely. If you want to pass the "location of a single call" to a function as a whole, you have to manually stitch together three or four macros. The types are nothing but bare strings and integers, offering zero type safety. Even worse, macros lack a "column number," cannot form a value object at compile time, and point to the wrong location the moment you nest them.

C++20 provides a standardized, type-safe alternative: `std::source_location`. It bundles "which file, which line, which function, and which column" into a single object, capturing everything at once. It is also `constexpr`—evaluable at compile time with zero runtime overhead. In this post, we will break it down and run through it: first, we look at how `current()` retrieves location information; then, we dive deep into the most common "default argument injection" pattern; finally, we distinguish its boundaries from macros and from runtime `stacktrace`, which we will cover in the next post.

## current()：Capture file / line / func / column all at once

The entry point for `source_location` is a single static member function `current()`, which returns a `source_location` object representing the "call site." This object exposes four query interfaces:

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

`g++ -std=c++20 -O2` (native GCC 16.1.1) yields:

```text
file_name     : /tmp/sloc/basic.cpp
line          : 15
function_name : int main()
column        : 14
```

Four things delivered in one go: file name, line number, column number, and function signature. Note that `function_name()` doesn't just give you the bare name like `__func__` does; it provides the **full function signature** (e.g., `int main()`). This is particularly useful for templates and overloads, and we will test this later.

Let's highlight a detail right away: `current()` is `constexpr`, and the object it returns is also a literal type, so it can be used in constant expressions. This property determines its "zero-overhead" nature, and we will verify this with assembly later.

## Why not macros: Type safety + All-in-one capture

Let's compare `source_location` with traditional macros side-by-side; the differences are clear:

| Dimension | `__FILE__` / `__LINE__` / `__func__` | `source_location` |
|---|---|---|
| Type | `const char*` / `int` / `const char*`, separate types | A single object, `string_view` + `unsigned` |
| Completeness | File + Line + Function name, **no column number** | File + Line + Column + Function signature |
| Evaluation time | Preprocessor / Compile-time | `constexpr`, compile-time |
| Can be passed as a whole? | No, must manually stitch multiple macros | Yes, pass a single object |
| Affected by macro expansion | Nested macro calls point to the wrong location | Determined by the actual call site, stable |

The code makes this most obvious:

```cpp
// Standard: C++20
#include <iostream>
#include <source_location>

constexpr int line_of(std::source_location loc = std::source_location::current()) {
    return loc.line();
}

// static_assert 证明: current() 的结果在编译期就是确定的
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

Here is the output:

```text
Running...
```

```text
constexpr line_of() = 20
[legacy] /tmp/sloc/constexpr.cpp:23 (no func, no col)
```

Two observations: First, the result of `line_of()` can be passed directly to `static_assert`, which proves that `source_location` is determined at compile time—this isn't a runtime symbol table lookup; the compiler "bakes" the location into a constant when generating the call. Second, the `LEGACY_LOG` line only provides "file:line", missing the function name and column number; retrieving those would require stacking macros like `__func__`.

"Baking into a constant" isn't just a figure of speech; let's look at the assembly. Compile the program above with `-O2` and grep for `source_location` related symbol calls:

```text
$ g++ -std=c++20 -O2 -S -o constexpr.s constexpr.cpp
$ grep -c "current" constexpr.s
0
```

`current` never appears in the assembly—the compiler fully evaluated and folded it into a constant at compile time. The overhead of passing `source_location` parameters is the same as passing a few `int`s or `string_view`s, with no additional runtime calls. This is the practical meaning of "zero-overhead": it's not "very small overhead", it's "non-existent at `-O2`".

Want to see `current` folded at compile time for yourself? Open the online demo below and check the assembly (enable `allow-x86-asm`). You'll see that `current` is never called—the location is "baked" into the constants at compile time:

<OnlineCompilerDemo
  title="Zero Overhead of source_location: current Disappears in Assembly"
  source-path="code/examples/vol3/68_source_location.cpp"
  description="static_assert proves current() is evaluated at compile time; checking the x86-64 assembly reveals current is never called—the location is baked into constants at compile time, resulting in zero runtime overhead"
  allow-x86-asm
/>

## Default Argument Injection: The Most Common Pattern

The real killer feature of `source_location` is "acting as a function default parameter to automatically inject the call site". This is exactly the pattern used in the `print_loc` example at the beginning:

```cpp
void print_loc(const std::source_location& loc = std::source_location::current()) {
    // ...
}
```

The brilliance of this design lies in this: **default arguments are evaluated at the "call site," not at the "function definition."** This is the inherent semantics of C++ default arguments—every time `print_loc()` is called without arguments, the compiler generates a `source_location::current()` at the call site. Consequently, `loc` naturally represents "who called `print_loc`," without requiring the caller to explicitly pass the location.

This fundamentally changes the way we write logging and assertions. Let's implement a `log_info` with location information and an `expect` that prints the location upon failure:

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
    divide(10, 0);   // 触发失败断言
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

Note a few things: the caller line `log_info("...")` doesn't need to care about the location at all; the location is injected automatically. `function_name()` provides the **full signature**—`int divide(int, int)`—which is particularly friendly to overloading and templates. The failing assertion prints the location precisely at that `expect` call line inside `divide`, pinpointing the bug in one step.

This is the core usage of `source_location`: **transforming the need to "know the caller's location" from macro magic into a normal default parameter**. Previously, you had to write disgusting macros like `EXPECT(cond, msg)`. Now, you can just write a normal function. It is type-safe, overloadable, and debuggable—it does everything macros can do without polluting the namespace.

### Why default parameters can capture the call site

It is worth pausing here to clarify the underlying mechanism, otherwise, you might fall into a trap in a different scenario.

In the C++ standard, the evaluation point of default parameters is the "call site," not the "function definition." This rule has always existed, but it didn't have much practical use before. The standard semantics of `source_location::current()` happen to be "return the location where it is called"—when it appears in a default parameter, this "location where it is called" is the **line written by the caller of `print_loc`**.

In other words, it is the combination of two rules—"default parameters are evaluated at the call site" and "`current()` returns the call site location"—that enables automatic injection. Once you understand this, you can predict the following classic pitfall.

## Classic Pitfall: Default Parameter vs. First Line of Function Body

Placing `current()` in different locations yields completely different results. This is where `source_location` is most prone to errors, so let's compare them directly:

```cpp
// Standard: C++20
#include <iostream>
#include <source_location>

// 模式A: current() 作默认参数 —— 拿到【调用点】位置
void log_default(std::source_location loc = std::source_location::current()) {
    std::cout << "[A 默认参数] line=" << loc.line()
              << " func=" << loc.function_name() << '\n';
}

// 模式B: 在函数体首行调 current() —— 拿到【当前函数】位置
void log_inline() {
    std::source_location loc = std::source_location::current();
    std::cout << "[B 函数体首行] line=" << loc.line()
              << " func=" << loc.function_name() << '\n';
}

int main() {
    log_default();   // 期望: 行号指向这一行
    log_inline();    // 期望: 行号指向 log_inline 内部
}
```

**Run it:**

```text
[A 默认参数] line=19 func=int main()
[B 函数体首行] line=13 func=void log_inline()
```

The difference is clear at a glance:

- **Mode A** (Default parameter): `loc` is evaluated at the call site in `main` on line 19, so it captures line 19 of `int main()` — the **call site**.
- **Mode B** (First line of function body): `current()` is called inside `log_inline` on line 13, so it captures line 13 of `void log_inline()` — the **function itself**.

Both approaches have their uses, but mixing them up leads to bugs. In most scenarios, you want to know "who called me," so you must use Mode A. If you truly intend to record "where this function is" (e.g., a function entry log), then Mode B is correct. The rule of thumb is simple: **wherever `current()` is written, that's the line it reports**. Default parameters are evaluated at the call site, while `current()` in the function body reports the line inside the body.

::: warning current() must be a default parameter to capture the call site
To implement "logging functions automatically record the caller's location," `current()` must be placed in the **default parameter**. If you mistakenly write `auto loc = std::source_location::current();` as the first line of the function body, you will always get the location of the logging function itself, not the caller — all your logs will point to the same place, rendering them useless for debugging. This is the number one pitfall of `source_location`; anyone who has fallen for it remembers it well.
:::

## function_name() returns the signature, not just the name

As seen above, `function_name()` returns the full signature. This is particularly useful for member functions and templates. Let's verify this separately:

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

**Run it:**

```text
member func call from: int main() @ line 23
template func call from: int main() @ line 24
```

Note that this verifies the behavior in default parameter mode: because `current()` is in the default parameter, `function_name()` reports the **caller** `int main()`, rather than `method` or `tpl_func` themselves. This confirms the rule that "default parameter = call site"—regardless of whether the called function is a regular function, a member function, or a template, what gets injected is the call site.

If you want the called function to record its own signature (for example, instrumentation at the function entry), revert to Mode B above and call `current()` inside the function body. In that case, `function_name()` will yield the signature of the called function itself, such as `void Tracker::method(int)` or `void tpl_func<int>(int)`. The two serve different purposes, so do not confuse them.

## Distinguishing from `stacktrace`

At this point, it is crucial to distinguish `source_location` from the runtime `std::stacktrace` (C++23) covered in the next article. Both are "related to code location," but they operate at completely different levels:

| Dimension | `source_location` (C++20) | `stacktrace` (C++23) |
|---|---|---|
| Granularity | Single point: the specific line of the call | The entire call stack, multiple frames |
| Evaluation Time | Compile-time `constexpr`, zero-overhead | Runtime, requires symbol table lookup |
| Overhead | None, folded into a constant | Yes, requires stack unwinding + symbolization |
| Dependencies | None, pure language feature | Requires linking symbolization support (e.g., `_GLIBCXX_USE_BACKTRACE` in libstdc++) |
| Typical Use Cases | Logging, assertions, contracts, instrumentation | Printing call chains on crashes, deep stack diagnostics |

To summarize in one sentence: `source_location` answers "where is this line?", while `stacktrace` answers "how did I get here?". The former is compile-time, zero-overhead, and single-point, making it suitable for attaching to every log message; the latter is runtime, has overhead, and covers the whole stack, making it suitable for one-time printing upon error. For 99% of daily "logging with location / assertion" needs, `source_location` is sufficient; avoid the heavyweight `stacktrace` unless necessary.

## Common Pitfalls

::: warning The location of `current()` is everything
If `current()` appears in a default parameter → it captures the call site; if it appears in a function body → it captures the current function itself. To achieve automatic log injection, you must place it in the default parameter. This is the number one pitfall, as demonstrated by the comparison above.
:::

::: warning Don't forget `const&` for default parameters
Writing a default parameter as `std::source_location loc = std::source_location::current()` passes by value. Since `source_location` is small (`sizeof` is only 8 bytes in libstdc++), passing by value is fine. However, if you define a custom wrapper type that holds significant state, remember to use `const std::source_location&` for the default parameter to avoid copies—the standard library's `source_location` itself is trivial, so pass-by-value is acceptable.
:::

::: warning The `#line` directive also affects `source_location`
Like `__FILE__` and `__LINE__`, `source_location` is affected by the `#line` directive. In generated code (yacc/lex, template generators), `#line` remapping is common, and the line numbers and filenames reported by `source_location` will change accordingly. We have verified this experimentally:

```text
$ #line 100 "fake.cpp" 之后
source_location line=100 file=fake.cpp
```

This is beneficial for debugging generated code (the location points to the source template rather than the generated result), but be aware of this behavior to avoid confusion when seeing "non-existent filenames."
:::

::: warning Historical issues with `current()` on MSVC
GCC and Clang have stable support for `current()` in default arguments. However, older versions of MSVC (prior to VS 2019 16.10) had a bug in the implementation of calling `current()` within default arguments, resulting in incorrect location information. If your code needs to run cross-platform, ensure your MSVC version is new enough (fixed in VS 2022 17.0+); otherwise, log locations will be incorrect on Windows. This series uses GCC 16.1.1 as the standard, so this issue does not occur on Linux.
:::

## Summary

`std::source_location` transforms the concept of "where is the code" from a collection of macros into a type-safe object. Let's review the key takeaways:

- `current()` captures all four pieces of information at once: `file_name()` (as a `string_view`), `line()`, `column()`, and `function_name()` (full signature, as a `string_view`). This provides the column number and full signature that are missing from the `__FILE__`/`__LINE__`/`__func__` trio.
- The core usage is **default argument injection**: by writing `std::source_location loc = std::source_location::current()` as the last default parameter of a function, the caller doesn't need to do anything, and the location automatically points to the call site.
- `constexpr` + zero-overhead: Evaluation happens at compile time. Under `-O2`, `current` completely disappears from the assembly; passing a `source_location` is as cheap as passing a few `int`s.
- The major pitfall: `current()` inside a default argument captures the call site, while inside a function body it captures the function itself. To achieve automatic log injection, you must use default arguments.
- Division of labor with `stacktrace` (C++23): `source_location` is compile-time, single-point, and zero-overhead, making it suitable for attaching locations to individual logs. `stacktrace` is runtime, full-stack, and has overhead, making it suitable for crash diagnostics. For daily logging and assertions, `source_location` is sufficient.

The typical use case forms a single line: **logging, assertions, contracts, and testing frameworks**—anywhere you want to know at runtime where a piece of information originated in the code, you should use this to replace hand-rolled `__FILE__` macros.

In the next article, we will discuss runtime `std::stacktrace` (C++23), looking at how to capture the complete call chain and how it cooperates with `source_location`—one handles "single-point zero-overhead instrumentation," while the other handles "full stack backtraces on error."

## References

- [cppreference: std::source_location](https://en.cppreference.com/w/cpp/utility/source_location) — Standard semantics for `current()` / `file_name()` / `line()` / `column()` / `function_name()` (C++20)
- [cppreference: Default arguments](https://en.cppreference.com/w/cpp/language/default_arguments) — "Default arguments are evaluated at the call site" is the underlying basis for the `current()` injection mechanism
- [P1208R6: source_location](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p1208r6.pdf) — The proposal for adding `source_location` to C++20, covering design motivation and the original intent of "replacing macros"
