---
chapter: 7
cpp_standard:
- 20
- 23
description: A thorough walkthrough of std::format—Python f-string-style formatting with
  compile-time type safety, covering format-string syntax, why format_string's compile-time
  validation is safer than printf, writing into buffers with format_to, benchmarked
  performance and type-safety comparisons against printf/iostream, plus C++23's print
  and runtime width/precision arguments
difficulty: intermediate
order: 52
platform: host
prerequisites:
- 'Deep Dive into std::string: SSO, COW, and resize_and_overwrite'
- 'Iterator Adapters: Reverse, Insert, and Stream — Repurposing Existing Iterators
  with New Behaviors'
reading_time_minutes: 16
related:
- 'print: Direct Output in C++23 and Decoupling from iostream'
tags:
- host
- cpp-modern
- intermediate
- 基础
title: 'format: Type-Safe Formatting in C++20'
translation:
  source: documents/vol3-standard-library/strings/52-format.md
  source_hash: 79e59ca94b1cf62b33e51126f6e588bfb8e207e667afbc13af17f4f4418b619a
  translated_at: '2026-09-26T00:00:05+00:00'
  engine: anthropic
  token_count: 9800
---

# format: Type-Safe Formatting in C++20

Turning an `int`, a string, and a floating-point number into one line of readable text is a chore every C++ program has to do. The standard library has offered two routes for it, and both sting: `printf` is fast but not type-safe; `iostream` is safe but slow and verbose. `std::format` (C++20) exists to fill that gap—with Python f-string-style placeholder syntax, it moves the type checking to compile time, keeping `printf`'s format-string expressiveness without falling into the pit of runtime undefined behavior.

In this article we take `std::format` apart and run it all the way through: how exactly to write a format string, how it manages to reject wrong types at compile time, how to write straight into a buffer, how it really stacks up against `printf` and `iostream` in speed, and finally what C++23 added on top. `std::print` / `std::println` from C++23 is a story of its own that we save for the next article; here it only gets a passing mention as a direct consumer of `std::format`.

## First, the Pain Points: Where printf and iostream Fall Short

Assembling a log line with `printf` is practically muscle memory in every project, but it has two chronic flaws.

The first is type unsafety. Between `printf`'s format string (`%d` / `%s` / `%f`) and the arguments that follow, the compiler **enforces no correspondence**—write `%s` and pass an `int`, and it still compiles. Let's try a snippet:

```cpp
// Standard: C++20
#include <cstdio>

int main() {
    // %s expects char*, but we pass an int — compiles fine, runtime UB
    std::printf("value = %s\n", 42);
    return 0;
}
```

Compiled with GCC 16.1.1 under `-Wall -Wextra`, it produces **only a warning** (`format '%s' expects ... but argument has type 'int'`), not an error. And when you actually run it:

```text
$ ./printf_ub
Segmentation fault (core dumped)   # exit code 139 = SIGSEGV
```

`42` gets treated as a pointer and dereferenced—instant segfault. This is runtime UB: the compiler glanced at your code, dropped a reminder, and if you ignore it, it compiles anyway.

What's more, this `-Wformat` reminder **only fires for string literals**. Once the format string is assembled at runtime, the compiler never even sees it, and the warning vanishes:

```cpp
// Standard: C++20
#include <cstdio>
#include <string>

int main(int argc, char**) {
    std::string fmt = (argc > 0) ? "value = %s\n" : "value = %d\n";
    std::printf(fmt.c_str(), 42);   // still UB, and this time not even a warning
    return 0;
}
```

Under `-Wall -Wextra` this snippet is **squeaky clean**—not a single diagnostic. As soon as one log call site anywhere in the project splices user input into a format string, type checking has completely lost the gate.

`iostream`, for its part, is type-safe, but pays for it with verbosity and slowness. Assembling that same `"id=1 name=alice score=3.14"` line:

```cpp
std::ostringstream oss;
oss << "id=" << 1 << " name=" << "alice" << " score=" << 3.14;
std::string s = oss.str();
```

Every value needs its own `<<`, each one hops through layers of operator overloads, and `ostringstream` internally keeps formatting state alive—we'll put a real number on this machinery's cost with a benchmark later; for now just file away the "commonly known to be slow" verdict.

`std::format`'s founding idea is to fuse the strengths of both sides: express output intent through a compact format string the way `printf` does, but move the "do the placeholder and argument types match" check to compile time—if it doesn't compile, it doesn't get to run.

## Getting Started: What the Format String Looks Like

Let's run a minimal one first to get a feel for the syntax:

```cpp
// Standard: C++20
#include <format>
#include <iostream>

int main() {
    std::cout << std::format("Hello {} {} {}\n", "world", 42, 3.14);
    return 0;
}
```

```text
Hello world 42 3.14
```

`{}` is a placeholder that consumes the following arguments in order. You don't have to think about types—`std::format` already knows at compile time what each argument is, and at runtime it simply formats each one the correct way.

### Positional Arguments: Using the Same Argument Multiple Times

By default `{}` consumes arguments in order. To reorder them, or to use one argument several times, give the placeholder a number—`{0}` is the first argument, `{1}` the second:

```cpp
std::cout << std::format("{1} before {0}\n", "B", "A");
```

```text
A before B
```

The most practical use of positional arguments is internationalization—word order differs between languages, a Chinese "{0} 的 {1}" and an English "{1} of {0}" may need to reuse the same set of arguments, and translators can then touch only the format string, never the call site. Once you use positional arguments, **every** placeholder in that string must carry a number; you can't mix positional and automatic numbering.

### Format Specifiers: That Chunk After `{:`

What truly brings `std::format` close to `printf`'s expressiveness is the **format specifier** that can follow the `{:`. The full syntax is `{:fill align width .prec type}`—intimidating at a glance, but clear once you peel it apart layer by layer.

**Alignment and fill**: `<` left-aligns, `>` right-aligns, `^` centers, and a fill character may sit in front of the alignment. Combined with a width:

```cpp
std::cout << std::format("[{:>10}]\n", "right");
std::cout << std::format("[{:<10}]\n", "left");
std::cout << std::format("[{:^10}]\n", "center");
std::cout << std::format("[{:*^10}]\n", "x");
```

```text
[     right]
[left      ]
[  center  ]
[****x*****]
```

**Precision and type**: `.N` sets how many decimal places a floating-point value keeps, and type characters `b`/`o`/`x` set the base of an integer:

```cpp
std::cout << std::format("{:.3f}\n", 3.14159);   // 3 decimal places for the float
std::cout << std::format("{:b}\n", 42);          // binary
std::cout << std::format("{:#x}\n", 255);        // hexadecimal with the 0x prefix
std::cout << std::format("{:#o}\n", 8);          // octal with the 0 prefix
std::cout << std::format("{:c}\n", 65);          // printed as a character
```

```text
3.142
101010
0xff
010
A
```

These specifiers also compose, in the order `fill align width .prec type`. Two common combos: left-aligned padded with `-`, and signed with zero padding:

```cpp
std::cout << std::format("[{:-<8}]\n", 42);    // left-aligned, padded with '-'
std::cout << std::format("[{:+08}]\n", 42);    // forced plus sign + zero padding
```

```text
[42------]
[+0000042]
```

The specifier grammar has plenty more corners (`{:.5}` applied to a string truncates its length, `{:e}` gives scientific notation, and so on), but there's no need to memorize the whole table—keep the `fill align width .prec type` skeleton in mind and look the rest up on cppreference. The key thing to understand: **every one of these is bound to the argument's type, and a mismatch is rejected outright at compile time**. Let's see right now how that works.

## Compile-Time Type Checking: How format_string Blocks Mistakes

This is the part of `std::format` that most sets it apart from `printf`. Back to the opening example of `%s` paired with an `int`, now written with `std::format`:

```cpp
// Standard: C++20
#include <format>
#include <iostream>

int main() {
    std::cout << std::format("{:d}", "not a number");
    return 0;
}
```

This time GCC 16.1.1 **fails to compile outright**—no warning:

```text
t2_compile.cpp:7:30: error: call to consteval function
  'std::basic_format_string<char, const char (&)[13]>("{:d}")'
  is not a constant expression
...
format:1609:48: error: call to non-'constexpr' function
  'void std::__format::__failed_to_parse_format_spec()'
```

The error message looks frightening, but its meaning is crisp: the specifier `d` (integers) doesn't fit the argument type `const char[13]` (a string), the format string fails to parse, and **compilation stops right there**.

A wrong argument count fails the same way. Two placeholders, one argument:

```cpp
std::cout << std::format("{} {}", 1);   // 2 placeholders, 1 argument
```

```text
t3_args.cpp:4:30: error: call to consteval function
  'std::basic_format_string<char, int>("{} {}")' is not a constant expression
format:322:56: error: call to non-'constexpr' function
  'void std::__format::__invalid_arg_id_in_format_string()'
```

Compilation fails again. The mechanism here deserves a closer look, because it explains "why must it be a literal".

### format_string: A consteval Gatekeeper

The first parameter of `std::format` is not a `const char*`; it is a `std::format_string<Args...>`. This type carries one key design decision: its constructor is `consteval`—which means **constructing it at all must be doable at compile time**.

```cpp
// Roughly the idea (simplified pseudo-code, not the real standard library)
template <typename... Args>
struct basic_format_string {
    const char* str;

    // consteval constructor: format-string parsing runs at compile time
    template <typename T>
    consteval basic_format_string(const T& s) : str{s} {
        // Scan the format string at compile time; for each placeholder, check:
        //  - Argument index out of range? Error.
        //  - Is the specifier legal for this argument's type? If not, error.
        constant_expression_check(s, std::make_format_checker<Args...>());
    }
};
```

That "scan + validate" process inside the constructor is precisely the checker that compares the format string against the argument types one by one. Because the whole construction is `consteval`, it can only happen in a compile-time constant context—and string literals happen to be compile-time constants. So a "format string doesn't match argument types" runtime bug gets forcibly turned into a compile-time error.

::: warning The format string must be a literal
The `consteval` construction of `format_string` dictates that the format string **must be a compile-time constant**. The following won't compile, because `runtime_fmt` is not a constant:

```cpp
std::string runtime_fmt = read_from_config();
std::format(runtime_fmt, 42);   // error: not a constant expression
```

A genuinely runtime format string has to take a different route (`std::vformat`, below), and that route **has no compile-time validation**—you are on your own to keep the types matching. This is a deliberate trade-off: the standard library builds the "fast and safe" everyday path with compile-time validation, and reserves a separate escape hatch for "I truly need a runtime string and accept the risk", so the former isn't dragged down by the latter.
:::

### Runtime Format Strings: The vformat Escape Hatch

When you really do read a format string from a config file or from user input, `std::format` is off the table and you turn to `std::vformat`. It skips compile-time validation and parses at runtime:

```cpp
// Standard: C++20
#include <format>
#include <iostream>
#include <string>

int main() {
    std::string runtime_fmt = "x={}, y={}";
    int a = 1, b = 2;
    // vformat: runtime format string + arguments packed by make_format_args; no compile-time validation
    std::string s = std::vformat(runtime_fmt, std::make_format_args(a, b));
    std::cout << s << '\n';
    return 0;
}
```

```text
x=1, y=2
```

Note that in C++20, `make_format_args` must be passed **lvalues** (`a`, `b`—you can't write `1`, `2` directly). This is a widely criticized pothole in the standard—LWG 3631 already changed it to `const&` in C++23, allowing rvalues. But when we tested on our local GCC 16.1.1 (libstdc++), passing rvalues in C++23 mode **still fails to compile**, which means this defect report has not landed in the current libstdc++. So for now the safest move is to keep passing lvalues honestly; don't get led astray by older articles claiming "C++23 accepts rvalues now".

The `vformat` route is something you only touch when writing your own internationalized logging framework or a `fmt::runtime`-style interface. For the everyday 99% of cases, a literal format string through `std::format` is enough, and the type safety comes free.

## format_to: Writing Straight into a Buffer

Each call to `std::format` returns a `std::string`, which means a heap allocation. If you want to write into an existing buffer and dodge the allocation, use `std::format_to`—it writes the result to an output iterator, much closer to `printf`-world's `snprintf` style of "write into this memory".

The most natural pairing is the `std::back_inserter` from the previous article, appending into a `std::string`:

```cpp
// Standard: C++20
#include <format>
#include <iostream>
#include <string>
#include <iterator>

int main() {
    std::string buf;
    std::format_to(std::back_inserter(buf), "a={} ", 1);
    std::format_to(std::back_inserter(buf), "b={} ", 2);
    std::cout << "buf = [" << buf << "]\n";
    return 0;
}
```

```text
buf = [a=1 b=2 ]
```

This is yet another example of the "adapter + algorithm" cooperation: `format_to` only knows the output-iterator interface, `back_inserter` translates "assignment" into `push_back`, and once the two mesh, writing a `string` flows as smoothly as writing a stream.

If the destination is a fixed-size `char` array (common in embedded, when you want to avoid any heap allocation), just pass the array's base address in as the iterator. But arrays don't grow on their own, and writing past the end is an overrun—that's when you reach for `std::format_to_n`, which additionally takes a maximum character count, guarantees it stays in bounds, and tells you whether the output was truncated:

```cpp
// Standard: C++20
#include <format>
#include <iostream>

int main() {
    char cbuf[8];
    auto res = std::format_to_n(cbuf, sizeof(cbuf) - 1, "long number {}", 123456789);
    *res.out = '\0';   // res.out points at the end of the written output; append the '\0' by hand
    std::cout << "cbuf = [" << cbuf << "]\n";
    std::cout << "total size = " << res.size
              << ", truncated = " << std::boolalpha
              << (res.size > static_cast<int>(sizeof(cbuf)) - 1) << '\n';
    return 0;
}
```

```text
cbuf = [long nu]
total size = 21, truncated = true
```

`res.size` is the length of the **fully** formatted output (21), and `res.out` is the end position of what actually landed in the buffer. Comparing the two tells you whether truncation happened—here 21 far exceeds the buffer capacity of 7, and the result got cut down to `long nu`. When building fixed-buffer logging or protocol-frame assembly, this `format_to_n_result` is what you consult to decide "does this log line fit".

Incidentally, if you only want to know how long the formatted output would be, without actually writing it, there's `std::formatted_size`:

```cpp
std::cout << std::formatted_size("{}-{}\n", 100, 200);   // 7
```

When pre-allocating a buffer, use it to compute the capacity once and then `format_to` into it—this spares `std::string` a second round of internal growth.

## Measured: format vs printf vs iostream

Talk is cheap, so let's actually run it. The same log line (`id=N name=alice score=3.14`) looped one million times, using `printf` / `std::format` / `std::format_to` (writing into a fixed-size `char` buffer) / `iostream` (`ostringstream`) respectively, measuring total time. The full benchmark lives in `/tmp/fmt/bench.cpp`, compiled with `g++ -std=c++23 -O2` (local GCC 16.1.1).

```text
--- run 1 ---
printf    : 129121 us   (0.13 us/iter)
format    : 164247 us   (0.16 us/iter)
format_to : 154787 us   (0.15 us/iter)
iostream  : 304199 us   (0.30 us/iter)
(sink=0)
--- run 2 ---
printf    : 135027 us   (0.14 us/iter)
format    : 180146 us   (0.18 us/iter)
format_to : 152233 us   (0.15 us/iter)
iostream  : 442312 us   (0.44 us/iter)
(sink=0)
```

A few robust conclusions (absolute microsecond values jitter from machine to machine—look only at the orders of magnitude and the relative relationships):

- **`printf` is the fastest**, because its format-string parsing is a hand-written state machine and the arguments travel through varargs—minimal overhead, at the price of exactly the type unsafety described above.
- **`std::format` / `format_to` follow close behind**, at roughly 1.1–1.4x of `printf`. `format_to` writes into a `char` buffer with no heap allocation, making it even slightly faster than `std::format`, which returns a `std::string`. On the "type-safe + near-printf performance" axis, `std::format` holds a clear advantage.
- **`iostream` is clearly the slowest**, roughly 2–3x of `printf`, and with heavy jitter (the repeated construction and destruction of `ostringstream`, layer upon layer of `<<` operator hops, and the maintained formatting state all drag it down). Assembling strings with `ostringstream` on a logging hot path is a genuine loss.

So the conclusion is clear: **if you want safety without the slowdown, use `std::format`**. Only in corners already walled in by type checking and obsessively concerned with that last sliver of performance (say, ultra-high-frequency compact logging) does `printf` still earn its keep—and those scenarios usually deserve the harder question of whether to cut the logging entirely. `iostream` for assembling formatted strings should be retired from performance-sensitive code.

## What C++23 Added to format

Around formatting, C++23 did two things worth mentioning, and both make `std::format` smoother to use.

### First: Runtime width / precision as Arguments (P2636)

In C++20, width and precision must be hard-coded into the format string: `{:>10}`, `{:.3f}`. In practice, though, you constantly want to "align to some column width" or "take the precision from configuration"—the width is only known at runtime. The C++20 workaround is `std::vformat` plus building the string yourself, which is ugly and throws away compile-time checking.

P2636 opened up "nested placeholders" in format specifiers: at the width and precision positions you can write another `{}` that pulls its value from the following arguments. GCC 16.1.1 already supports it:

```cpp
// Standard: C++23
#include <print>
#include <iostream>

int main() {
    int width = 8;
    int prec = 2;
    std::println("[{:>{}}]", 42, width);          // width comes from an argument
    std::println("[{:.{}}]", 3.14159265, prec);    // precision comes from an argument
    return 0;
}
```

```text
[      42]
[3.1]
```

In `{:>{}}`, the first `{}` is the placeholder proper and the second `{}` is the width—`width=8` gets filled in, equivalent to `{:>8}`. `{:.{}}` works the same way, taking precision from `prec`. Note that compile-time checking is still intact: the nested-in argument is required to be an integer type, and a mismatch won't compile.

### Second: print / println Consuming format Directly (Star of the Next Article)

`std::format` returns a `std::string`, and getting it to the terminal means another layer of `std::cout << ...`—one extra copy. C++23's `std::print` / `std::println` (the `<print>` header) directly accept `std::format`'s format string and arguments and stream the output internally, eliminating the intermediate `std::string`:

```cpp
// Standard: C++23
#include <print>

int main() {
    std::println("Hello {} = {}", "x", 42);   // newline included automatically
    std::print("[no newline]");
    return 0;
}
```

```text
Hello x = 42
[no newline]
```

`println` appends a trailing newline, `print` doesn't, and the syntax is identical to `std::format`—same format strings, same compile-time type checking; only the output target changes from "return a string" to "write the stream directly". How `print` picks its target stream, how it cooperates with `sync_with_stdio(false)`, and where it beats `cout` in performance are all material for the next article (the `std::print` special), so we won't expand on them here.

::: warning print may be missing on older GCC
`std::print` / `std::println` need the `<print>` header and a reasonably new libstdc++. Before GCC 13 they are essentially absent; from GCC 14 on they become gradually usable. On our local GCC 16.1.1, `<print>` is fully available in practice (`println`, `print`, `vprint` are all there). If your project has to support older toolchains, `std::format` itself (available since GCC 13) has far broader coverage than `std::print` and is more stable across toolchains. When targeting legacy environments, the `fmt` library is the usual polyfill—it is literally the prototype of `std::format`, with a nearly identical API.
:::

## Custom formatters: Adding Format Support to Your Own Types

Out of the box, `std::format` supports the built-in types (integers, floating point, strings, pointers). Custom types, by default, don't compile—`std::format("{}", my_point)` reports "no matching formatter". To let your own types slot into `std::format`, just write a specialization of `std::formatter`.

Here we only point at a minimal usage—giving a `Point` the ability to "format as `(x, y)`"—without unfolding the full formatter-parser implementation (that alone could be its own article). The minimal specialization only needs two functions:

```cpp
// Standard: C++20
#include <format>
#include <iostream>
#include <string>

struct Point {
    int x{};
    int y{};
};

// Give Point formatting support: specialize std::formatter<Point>
template <>
struct std::formatter<Point> {
    // Parse the spec part between {} in the format string; we recognize none here, so accept as-is
    constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    // The real output: write the Point as "(x, y)"
    auto format(const Point& p, std::format_context& ctx) const {
        return std::format_to(ctx.out(), "({}, {})", p.x, p.y);
    }
};

int main() {
    Point p{3, 4};
    std::cout << std::format("point = {}\n", p);
    std::cout << std::format("two points: {} and {}\n", Point{1, 1}, Point{9, 9});
    return 0;
}
```

```text
point = (3, 4)
two points: (1, 1) and (9, 9)
```

The division of labor between the two functions is clear:

- `parse` consumes the format specifier between the `{}` (for example, the `:>10` in `{:>10}`). Here we support no specifiers and simply return `ctx.begin()` to say "nothing consumed". The moment you want `Point` to honor an alignment like `{:>10}`, you have to parse it in `parse` and apply it in `format`—that is exactly how the standard library's built-in formatters are implemented.
- `format` writes the value out. The `ctx.out()` it receives is an output iterator, so we just reuse `std::format_to` to write `(x, y)` into it. Note that `{}` still works nested inside `format_to` here, because `int` is supported out of the box.

The beauty of this pattern: **once you write a formatter for your own type, it works anywhere that accepts `std::formattable`**—not just `std::format`, but C++23's `std::print`, `std::format_to`, logging frameworks, and the formatting of ranges (C++23's `std::formatter<std::range>`) can all consume it directly, without changing a single line of those components. That is the dividend of a standardized extension point—far more convergent than the everyone-for-themselves approach of "writing `operator<<` for your containers".

## A Few Pitfalls You'll Actually Hit

Let's gather in one place the spots where this journey tends to flip over, each mapping back to what we tested above:

::: warning The format string must be a literal
A `std::format` format string must be a compile-time constant. Strings known only at runtime (config files, user input) fail to compile through `std::format`; you have to use `std::vformat`, but that route **has no compile-time type checking**—the blame for mismatched types lands back on your own head.
:::

::: warning make_format_args takes lvalues (C++20)
When pairing `std::vformat` with `std::make_format_args`, under the C++20 standard the arguments must be lvalues; passing rvalues (literals like `1`, `"str"`) fails to compile. LWG 3631 changed this to `const&` in C++23 to allow rvalues, but on our local GCC 16.1.1 (libstdc++) the fix **has not landed yet**—passing rvalues in C++23 mode still errors out. For now, passing lvalues across the board is the safest bet.
:::

::: warning format_to_n's res.size is the full length
The `result.size` returned by `std::format_to_n` is "how long the output would be if not truncated", not "how much was actually written". To decide whether truncation happened, compare `size > capacity`; never use `size` as the written length—and if you truly need the actual write position, look at `result.out`.
:::

::: warning The digit-grouping separator is not implemented in libstdc++ yet
The `,` in format specifiers (the thousands separator) was only standardized by P2931 in C++26, and libstdc++ 16.1.1 has not implemented it—`std::format("{:,}", 1234567)` **fails to compile** (a parse failure). If you need localized digit grouping, your current options are post-processing the string yourself or waiting for C++26's `L` option to land. Don't be misled by older articles claiming `{:,}` works.
:::

## Summary

`std::format`'s founding idea fits in one sentence: **printf's format-string expressiveness, iostream's type safety, and Python f-string's concise syntax, fused into one**. The key conclusions, collected:

- Format-string syntax: `{}` placeholders, consuming arguments in order or by position via `{0}{1}`; after `{:` comes the `fill align width .prec type` specifier, controlling alignment, width, precision, and base.
- Compile-time type checking is the core value: `format_string`'s `consteval` construction turns every mismatch—"specifier vs argument type", "placeholder count vs argument count"—into a compile-time error, while the same mistakes in `printf` are merely runtime UB.
- The format string must be a literal; when you truly need a runtime string, go through `std::vformat` + `make_format_args`, at the cost of no compile-time checking (and in C++20 you must also pass lvalues).
- For buffer writing use `format_to` (with `back_inserter` into a `string`, or a bare pointer into a `char` buffer), use `format_to_n` for fixed lengths with overrun protection, and `formatted_size` when you only want the length.
- Performance: `format` / `format_to` stay right behind `printf` (roughly 1.1–1.4x), while `iostream` is clearly the slowest (2–3x). If you want safety without the slowdown, pick `format`.
- New in C++23: P2636 lets width/precision come from arguments (`{:>{}}`); and `std::print` / `std::println` consume the format string and output directly, skipping the intermediate `std::string`—the latter being the star of the next article.
- Custom types: specialize `std::formatter` (implement `parse` + `format`), and the type enters every formattable interface, with no changes needed on the consumer side.

In the next article we cover `std::print` / `std::println` specifically—how it directly consumes `std::format`'s format strings, how to choose the output target, and where it beats `std::cout` in performance—wrapping up the formatting thread.

## References

- [cppreference: std::format](https://en.cppreference.com/w/cpp/utility/format/format) — the main interface and format-string syntax
- [cppreference: std::format_string](https://en.cppreference.com/w/cpp/utility/format/basic_format_string) — the compile-time validation mechanism (consteval construction)
- [cppreference: std::formatter](https://en.cppreference.com/w/cpp/utility/format/formatter) — the extension point for custom types
- [cppreference: std::format_to_n](https://en.cppreference.com/w/cpp/utility/format/format_to_n) — writing into fixed-size buffers and detecting truncation
- [P2636R4](https://wg21.link/p2636) — C++23's runtime width/precision as arguments
- [The {fmt} library](https://github.com/fmtlib/fmt) — the prototype of `std::format`, and a cross-toolchain polyfill
