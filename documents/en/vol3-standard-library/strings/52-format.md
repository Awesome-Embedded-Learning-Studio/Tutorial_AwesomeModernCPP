---
chapter: 7
cpp_standard:
- 20
- 23
description: 'A deep dive into std::format: compile-time type-safe formatting in the
  style of Python f-strings. We cover format string syntax, compile-time validation
  of `format_string`, why it is safer than `printf`, writing to buffers with `format_to`,
  and a dual-dimension benchmark comparing performance and type safety against `printf`
  and `iostream`. We also explore C++23''s `print` and runtime `width`/`precision`
  parameters.'
difficulty: intermediate
order: 52
platform: host
prerequisites:
- string 深入：SSO、COW 与 resize_and_overwrite
- 迭代器适配器：反向、插入与流，把现成迭代器改出新行为
reading_time_minutes: 16
related:
- print 与 println：直接消费 format 的快捷输出
tags:
- host
- cpp-modern
- intermediate
- 基础
title: 'format: Type-Safe Formatting in C++20'
translation:
  source: documents/vol3-standard-library/strings/52-format.md
  source_hash: 79e59ca94b1cf62b33e51126f6e588bfb8e207e667afbc13af17f4f4418b619a
  translated_at: '2026-06-24T00:49:15.915438+00:00'
  engine: anthropic
  token_count: 3914
---
# format: Type-Safe Formatting in C++20

Combining an `int`, a string, and a floating-point number into a single line of readable text is a task every C++ program must perform. The standard library has historically offered two paths for this, but both have significant drawbacks: `printf` is fast but not type-safe, while `iostream` is safe but slow and verbose. `std::format` (C++20) fills this gap by using Python f-string style placeholder syntax to move type checking to compile time. It retains the expressiveness of `printf` format strings without falling into the pit of runtime undefined behavior.

In this article, we will dissect `std::format` thoroughly: how to write format strings, how it blocks incorrect types at compile time, how to write to buffers, how its performance compares to `printf` and `iostream`, and finally, what features C++23 added. We will leave the deep dive into C++23's `std::print` and `std::println` for the next article, treating them here merely as direct consumers of `std::format`.

## The Problem: Why printf and iostream Fall Short

Using `printf` to assemble a log line is almost muscle memory for all projects, but it has two chronic issues.

The first is a lack of type safety. The compiler **does not enforce alignment** between `printf`'s format string (`%d`, `%s`, `%f`) and the subsequent arguments. If you write `%s` but pass an `int`, it still compiles. Let's try a snippet:

```cpp
// Standard: C++20
#include <cstdio>

int main() {
    // %s 期望 char*，却传了 int —— 编译通过，运行期 UB
    std::printf("value = %s\n", 42);
    return 0;
}
```

When compiling with GCC 16.1.1 using `-Wall -Wextra`, it **only gives one warning** (`format '%s' expects ... but argument has type 'int'`), not an error. However, when we actually run it:

```text
$ ./printf_ub
Segmentation fault (core dumped)   # exit code 139 = SIGSEGV
```

`42` is treated as a pointer and dereferenced, causing an immediate segmentation fault. This is runtime undefined behavior (UB)—the compiler took a look, warned you about it, but if you ignore it, it will compile anyway.

Furthermore, this `-Wformat` warning **only applies to string literals**. Once the format string is constructed at runtime, the compiler cannot even see it, and the warning disappears:

```cpp
// Standard: C++20
#include <cstdio>
#include <string>

int main(int argc, char**) {
    std::string fmt = (argc > 0) ? "value = %s\n" : "value = %d\n";
    std::printf(fmt.c_str(), 42);   // 同样是 UB，这次连 warning 都没有
    return 0;
}
```

Compiling this code with `-Wall -Wextra` yields **a clean slate**, without a single warning. However, if just one log entry in the project concatenates user input into the format string, type checking is completely compromised.

`iostream` is type-safe, but it is both verbose and slow. To construct the string `"id=1 name=alice score=3.14"`:

```cpp
std::ostringstream oss;
oss << "id=" << 1 << " name=" << "alice" << " score=" << 3.14;
std::string s = oss.str();
```

Each value requires a separate `<<` call, operator overloading involves jumping through multiple layers, and `ostringstream` must maintain internal formatting state—this mechanism comes at a cost. We will measure this with a real benchmark later, but for now, keep in mind that it is "widely considered slow."

The goal of `std::format` is to combine the advantages of both approaches: it uses a compact format string like `printf` to express the output intent, but moves the validation of "whether placeholders and argument types match" to compile time. If it doesn't compile, it won't run.

## Getting Started: What Does a Format String Look Like?

Let's start with a minimal example to get a feel for the syntax:

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

`{}` acts as a placeholder, consuming arguments in order. We don't need to worry about types; `std::format` already knows what each argument is at compile time, so at runtime it formats them directly in the correct way.

### Positional Arguments: Using the Same Argument Multiple Times

By default, `{}` consumes arguments sequentially. To reorder the output or reuse an argument, we add a number to the placeholder—`{0}` is the first argument, and `{{1}}` is the second:

```cpp
std::cout << std::format("{1} before {0}\n", "B", "A");
```

```text
A before B
```

The most practical use of positional arguments is internationalization—since word order varies across languages, "{0}'s {1}" and "{1} of {0}" might need to reuse the same set of arguments. With positional arguments, we only need to modify the format string for translation, without touching the call code. Once we use positional arguments, **all** placeholders in the same string must carry an index; we cannot mix automatic and manual indexing.

### Format Specifiers: The stuff following `{:`

What truly brings `std::format` close to the expressiveness of `printf` is the **format specifiers** that can follow `{:`. The complete syntax is `{:fill align width .prec type}`. It looks intimidating, but it becomes clear when we break it down layer by layer.

**Alignment and Fill**: `<` for left alignment, `>` for right alignment, `^` for center alignment. We can also specify a fill character before the `{}`. These are used in conjunction with the width:

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

**Precision and Types**: Use `.N` to specify the number of decimal places for floating-point numbers, and use the type characters `b`/`o`/`x` to specify the integer base:

```cpp
std::cout << std::format("{:.3f}\n", 3.14159);   // 浮点保留 3 位
std::cout << std::format("{:b}\n", 42);          // 二进制
std::cout << std::format("{:#x}\n", 255);        // 带 0x 前缀的十六进制
std::cout << std::format("{:#o}\n", 8);          // 带 0 前缀的八进制
std::cout << std::format("{:c}\n", 65);          // 当字符输出
```

```text
3.142
101010
0xff
010
A
```

We can also combine these format specifiers. The combination order must follow `fill align width .prec type`. The two combinations below are commonly used: left-aligned with `-` padding, and signed with zero padding:

```cpp
std::cout << std::format("[{:-<8}]\n", 42);    // 左对齐，填 '-'
std::cout << std::format("[{:+08}]\n", 42);    // 强制正号 + 零填充
```

```text
[42------]
[+0000042]
```

There are still quite a few formatting specifiers (for example, `{:.5}` truncates strings, `{:e}` uses scientific notation, etc.). We don't need to memorize the entire table—the key is to remember the skeleton of `fill align width .prec type`, and we can look up the rest on cppreference. The crucial part is understanding that **all of these are bound to the argument type; if the type is incorrect, it will be blocked at compile time**. Let's look at how this is achieved.

## Compile-Time Type Checking: How `format_string` Blocks Errors

This is the core difference between `std::format` and `printf`. Going back to the example at the beginning where we used `%s` with an `int`, let's switch to `std::format`:

```cpp
// Standard: C++20
#include <format>
#include <iostream>

int main() {
    std::cout << std::format("{:d}", "not a number");
    return 0;
}
```

This time, GCC 16.1.1 **compilation results in a direct error**, not a warning:

```text
t2_compile.cpp:7:30: error: call to consteval function
  'std::basic_format_string<char, const char (&)[13]>("{:d}")'
  is not a constant expression
...
format:1609:48: error: call to non-'constexpr' function
  'void std::__format::__failed_to_parse_format_spec()'
```

The error message looks intimidating, but the meaning is clear: the format specifier `d` (integer) does not match the argument type `const char[13]` (string), so the format string parsing failed, and **compilation failed**.

The same logic applies if the number of arguments is incorrect. Providing only one argument for two placeholders:

```cpp
std::cout << std::format("{} {}", 1);   // 2 个占位符，1 个参数
```

```text
t3_args.cpp:4:30: error: call to consteval function
  'std::basic_format_string<char, int>("{} {}")' is not a constant expression
format:322:56: error: call to non-'constexpr' function
  'void std::__format::__invalid_arg_id_in_format_string()'
```

It still fails to compile. We should take a closer look at the mechanism here, as it explains why it must be a literal.

### format_string: A consteval Gatekeeper

The type of the first parameter of `std::format` is not `const char*`, but `std::format_string<Args...>`. This type has a key design feature: its constructor is `consteval`—which means **the construction itself must be completed at compile time**.

```cpp
// 大致是这个意思（简化伪码，不是标准库真身）
template <typename... Args>
struct basic_format_string {
    const char* str;

    // consteval 构造函数：编译期就跑格式串解析
    template <typename T>
    consteval basic_format_string(const T& s) : str{s} {
        // 编译期扫描格式串，对每个占位符校验：
        //  - 参数下标越界？报错
        //  - 格式说明对这个参数类型合法吗？不合法报错
        constant_expression_check(s, std::make_format_checker<Args...>());
    }
};
```

The "scan and verify" process inside the constructor acts as a checker that compares the format string against the argument types one by one. Since the entire construction is `consteval`, it can only occur in a compile-time constant context—and string literals happen to be compile-time constants. This effectively forces a runtime bug—a mismatch between the format string and argument types—into a compile-time error.

::: warning The format string must be a literal
The `consteval` constructor of `format_string` dictates that the format string **must be a compile-time constant**. The following code will not compile because `runtime_fmt` is not a constant:

```cpp
std::string runtime_fmt = read_from_config();
std::format(runtime_fmt, 42);   // error: 不是常量表达式
```

True runtime format strings take a different path (via `std::vformat` below), which **has no compile-time checks**. You are responsible for ensuring the types match. This is an intentional trade-off: the standard library provides compile-time checks for the common "fast and safe" path, while reserving a separate "escape hatch" for cases where you genuinely need a runtime string and accept the risk, ensuring the former isn't weighed down by the latter.
:::

### Runtime Format Strings: The `vformat` Escape Hatch

When you truly read a format string from a configuration file or user input, `std::format` cannot be used; you must use `std::vformat`. It skips compile-time checks and parses at runtime:

```cpp
// Standard: C++20
#include <format>
#include <iostream>
#include <string>

int main() {
    std::string runtime_fmt = "x={}, y={}";
    int a = 1, b = 2;
    // vformat：运行期格式串 + make_format_args 打包的参数；没有编译期校验
    std::string s = std::vformat(runtime_fmt, std::make_format_args(a, b));
    std::cout << s << '\n';
    return 0;
}
```

```text
x=1, y=2
```

Note that in C++20, `make_format_args` requires **lvalues** (like `a` and `b`, not literals `1` or `2`). This is a well-known pitfall in the standard—LWG 3631 changed it to `const&` in C++23 to allow rvalues. However, our local tests with GCC 16.1.1 (libstdc++) show that passing rvalues in C++23 mode **still fails to compile**, indicating that this defect report hasn't landed in libstdc++ yet. Therefore, it is safest to stick with passing lvalues for now, and don't be misled by older resources claiming that "C++23 allows passing rvalues."

The `vformat` approach is typically only needed when writing your own internationalized logging framework or a `fmt::runtime`-style interface. For 99% of daily use cases, using a literal format string with `std::format` is sufficient, and type safety comes for free.

## format_to: Writing directly to a buffer

`std::format` returns a `std::string` every time, which implies a heap allocation. If you want to write into an existing buffer and avoid allocation, use `std::format_to`. It writes the result to an output iterator, similar to how `snprintf` works in `printf` ("write to this block of memory").

The most natural pairing is `std::back_inserter`, which we discussed in the previous chapter, to append to a `std::string`:

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

This is another example of the "adapter + algorithm" collaboration in action: `format_to` only recognizes the output iterator interface, while `back_inserter` translates "assignment" into `push_back`. When these two click together, writing to a `string` feels as smooth as writing to a stream.

If the target is a fixed-size `char` array (common in embedded systems where we want to avoid any heap allocation), we can simply pass the array's starting address as an iterator. However, arrays don't automatically expand, so writing past the end leads to an out-of-bounds error. In this case, we use `std::format_to_n`. It accepts an additional maximum character count to guarantee we stay within bounds, and it also tells us if the output was truncated:

```cpp
// Standard: C++20
#include <format>
#include <iostream>

int main() {
    char cbuf[8];
    auto res = std::format_to_n(cbuf, sizeof(cbuf) - 1, "long number {}", 123456789);
    *res.out = '\0';   // res.out 指向写入的末尾，手动补 '\0'
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

`res.size` is the length of the **complete** formatted output (21), while `res.out` is the end position of the actual data written into the buffer. By comparing these two, we can determine if truncation occurred—in this case, 21 is far greater than the buffer capacity of 7, so the result was truncated to `long nu`. When implementing fixed-buffer logging or protocol frame assembly, this `format_to_n_result` serves as the basis for determining whether "this log entry will fit."

By the way, if we only want to know the formatted length without actually writing anything, we can use `std::formatted_size`:

```cpp
std::cout << std::formatted_size("{}-{}\n", 100, 200);   // 7
```

When pre-allocating a buffer, we calculate the capacity once and then write to it using `format_to`. This helps avoid a second internal reallocation in `std::string`.

## Benchmark: `format` vs `printf` vs `iostream`

Talk is cheap. Let's actually run the code. We loop one million times for the same log line (`id=N name=alice score=3.14`), measuring the total time taken by `printf`, `std::format`, `std::format_to` (writing to a fixed `char` buffer), and `iostream` (`ostringstream`). The full benchmark is available at `/tmp/fmt/bench.cpp`, compiled with `g++ -std=c++23 -O2` (local GCC 16.1.1).

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

Here are a few robust conclusions (absolute microsecond values will vary by machine, so we focus only on orders of magnitude and relative relationships):

- **`printf` is the fastest**, because its format string parsing is a hand-written state machine and it uses varargs for arguments, resulting in the lowest overhead. The trade-off is the lack of type safety mentioned earlier.
- **`std::format` / `format_to` follow closely**, at roughly 1.1–1.4 times the cost of `printf`. `format_to` writes to a `char` buffer without heap allocation, making it slightly faster than `std::format`, which returns a `std::string`. In the dimension of "type safety + performance close to printf," `std::format` has a clear advantage.
- **`iostream` is significantly the slowest**, at roughly 2–3 times the cost of `printf`, with high variance (repeated construction and destruction of `ostringstream`, layered jumps through `<<` operators, and maintaining formatting state all drag it down). Using `ostringstream` for string concatenation in a hot logging path is a genuine loss.

The conclusion is clear: **for safety without sacrificing speed, use `std::format`**. `printf` retains value only in corners where type checking is impossible and extreme performance is critical (e.g., ultra-high-frequency compact logging), though in such scenarios, it is usually better to simply eliminate the logging. `iostream` for formatting strings should be phased out in performance-sensitive areas.

## What C++23 Added to `format`

C++23 did two noteworthy things around formatting, both making `std::format` more convenient.

### First: Runtime Width/Precision as Arguments (P2636)

In C++20, width and precision had to be hardcoded in the format string: `{:>10}`, `{:.3f}`. However, in practice, we often need to "align to a specific column width" or "derive precision from configuration," where the width is only known at runtime. The C++20 workaround involved `std::vformat` + manual string concatenation, which was ugly and lost compile-time checks.

P2636 introduced "nested placeholders" to format specs: width and precision positions can now contain another `{}` to take values from subsequent arguments. GCC 16.1.1 already supports this:

```cpp
// Standard: C++23
#include <print>
#include <iostream>

int main() {
    int width = 8;
    int prec = 2;
    std::println("[{:>{}}]", 42, width);          // 宽度从参数取
    std::println("[{:.{}}]", 3.14159265, prec);    // 精度从参数取
    return 0;
}
```

```text
[      42]
[3.1]
```

`{:>{}}` Here, the first `{}` is the placeholder subject, and the second `{}` is the width—`width=8` is filled in, which is equivalent to `{:>8}`. Similarly, `{:.{}}` takes the precision from `prec`. Note that compile-time checking is still preserved: the nested parameter is required to be an integer type; if the types don't match, the code won't compile.

### Item Two: `print` / `println` Directly Consume `format` (The Star of the Next Post)

`std::format` returns a `std::string`, so to output to the terminal, we still need to wrap it with `std::cout << ...`, resulting in an extra copy. C++23's `std::print` / `std::println` (from the `<print>` header) directly accept the format string and arguments of `std::format` and stream them out internally, eliminating the intermediate `std::string`:

```cpp
// Standard: C++23
#include <print>

int main() {
    std::println("Hello {} = {}", "x", 42);   // 自动带换行
    std::print("[no newline]");
    return 0;
}
```

```text
Hello x = 42
[no newline]
```

`println` automatically appends a newline at the end, while `print` does not. Their syntax is fully consistent with `std::format`—they use the same format strings and the same compile-time type checking—only the output destination changes from "returning a string" to "writing directly to a stream." Topics like how `print` selects the target stream, how it interacts with `sync_with_stdio(false)`, and its performance advantages over `cout` will be covered in the next post (dedicated to `std::print`), so we won't expand on them here.

::: warning `print` may be unavailable on older GCC versions
`std::print` and `std::println` require the `<print>` header and a relatively recent libstdc++. They are basically unavailable before GCC 13, and become gradually available starting with GCC 14. On my local machine with GCC 16.1.1, `<print>` is fully functional (including `println`, `print`, and `vprint`). If your project needs to support older toolchains, `std::format` itself (available since GCC 13) has much wider coverage than `std::print` and is more stable across different toolchains. When targeting older environments, the `fmt` library is commonly used as a polyfill—it is the prototype for `std::format` and has an almost identical API.
:::

## Custom formatter: Adding format support for custom types

`std::format` supports built-in types (integers, floating-point numbers, strings, pointers) out of the box. However, it fails to compile by default for custom types—`std::format("{}", my_point)` will error with "no matching formatter." To make your own types compatible with `std::format`, you simply need to write a specialization for `std::formatter`.

Here, we will only touch upon the minimal usage—adding the ability to format a `Point` as `(x, y)`—without expanding on the full implementation of the formatter parser (that topic alone could warrant a separate article). The minimal specialization requires implementing two functions:

```cpp
// Standard: C++20
#include <format>
#include <iostream>
#include <string>

struct Point {
    int x{};
    int y{};
};

// 给 Point 加格式化支持：特化 std::formatter<Point>
template <>
struct std::formatter<Point> {
    // 解析格式串里 {} 之间的说明部分；这里不认任何说明，直接接受
    constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    // 真正输出：把 Point 写成 "(x, y)"
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

- `parse` is responsible for consuming the format specifiers between `{}` (such as `:>10` in `{:>10}`). Since we don't support any specifiers here, we simply return `ctx.begin()` to indicate "nothing consumed." Once you want `Point` to support alignment like `{:>10}`, you will need to parse it in `parse` and apply it in `format`—this is how all standard library formatters are implemented.
- `format` is responsible for writing the value out. It receives `ctx.out()`, which is an output iterator. We can simply reuse `std::format_to` to write `(x, y)`. Note that we can nest `{}` inside `format_to` because `int` is supported out of the box.

The beauty of this pattern is: **once you write a formatter for your own type, it works anywhere that accepts `std::formattable`**—not just `std::format`, but also C++23's `std::print`, `std::format_to`, logging frameworks, and range formatting (`std::formatter<std::range>` in C++23) can all use it directly without changing a single line of code in those components. This is the benefit of a "standardized extension point." Compared to the "every container for itself" approach of implementing `operator<<`, this is much more consistent.

## Common Pitfalls

Let's round up the places where it's easy to crash and burn, each corresponding to the tests above:

::: warning Format strings must be literals
The format string for `std::format` must be a compile-time constant. Strings only known at runtime (from config files or user input) will fail to compile with `std::format`. You must use `std::vformat` for those, but that path **lacks compile-time type checking**—if the types don't match, it's on you.
:::

::: warning make_format_args requires lvalues (C++20)
When using `std::vformat` with `std::make_format_args`, parameters must be lvalues under the C++20 standard. Passing rvalues (like the literal `1` or `"str"`) won't compile. LWG 3631 changed this to `const&` in C++23 to allow rvalues, but testing on GCC 16.1.1 (libstdc++) shows this **is not yet implemented**; passing rvalues in C++23 mode still errors. For now, always pass lvalues to be safe.
:::

::: warning format_to_n's res.size is the full length
The `result.size` returned by `std::format_to_n` is "how long it would be if not truncated," not "how much was actually written." To determine if truncation occurred, use `size > capacity`. Do not use `size` as the written length—if you need the actual write position, look at `result.out`.
:::

::: warning Number thousands separator not implemented in libstdc++
The `,` (thousands separator) in format specs is standardized in C++26 via P2931. libstdc++ 16.1.1 hasn't implemented it yet, so `std::format("{:,}", 1234567)` will **cause a compilation error** (parse failure). If you need localized number grouping, you currently have to post-process yourself or wait for the `L` option in C++26. Don't be misled by old resources suggesting `{:,}` works.
:::

## Summary

The intent of `std::format` can be summed up in one sentence: **combining the expressiveness of printf format strings, the type safety of iostreams, and the concise syntax of Python f-strings.** Here are the key takeaways:

- **Format string syntax:** `{}` placeholders take arguments by order or index `{0}{1}`. After `{:`, comes `fill align width .prec type` to control alignment, width, precision, and radix.
- **Compile-time type checking is the core value:** The `consteval` constructor of `format_string` blocks mismatches between "format spec vs. argument type" and "placeholder count vs. argument count" as compile-time errors, whereas the same errors in `printf` are just UB.
- **Format strings must be literals.** For runtime strings, use `std::vformat` + `make_format_args`, at the cost of losing compile-time checks (and you must pass lvalues in C++20).
- **Writing to buffers:** Use `format_to` (with `back_inserter` for `string` or raw pointers for `char` buffers). Use `format_to_n` for fixed-length buffers to prevent overruns, and `formatted_size` if you only need to know the length.
- **Performance:** `format` / `format_to` closely trail `printf` (about 1.1–1.4x slower), while `iostream` is significantly slower (2–3x). If you want safety without the slowness, choose `format`.
- **New in C++23:** P2636 allows width/precision to be taken from arguments (`{:>{}}`). `std::print` / `std::println` consume format strings and output directly, saving the intermediate `std::string`—the latter is the star of the next post.
- **Custom types:** Specialize `std::formatter` (implement `parse` + `format`), and your type fits into all formattable interfaces without changing the consumer.

In the next post, we will focus on `std::print` / `std::println`—how it consumes `std::format` strings directly, how to choose output targets, and why it outperforms `std::cout`, wrapping up our deep dive into formatting.

## References

- [cppreference: std::format](https://en.cppreference.com/w/cpp/utility/format/format) — Main interface and format string syntax
- [cppreference: std::format_string](https://en.cppreference.com/w/cpp/utility/format/basic_format_string) — Compile-time validation mechanism (consteval constructor)
- [cppreference: std::formatter](https://en.cppreference.com/w/cpp/utility/format/formatter) — Extension point for custom types
- [cppreference: std::format_to_n](https://en.cppreference.com/w/cpp/utility/format/format_to_n) — Fixed-length buffer writing and truncation logic
- [P2636R4](https://wg21.link/p2636) — C++23 runtime width/precision as arguments
- [{fmt} library](https://github.com/fmtlib/fmt) — The prototype of `std::format`, a cross-toolchain polyfill
