---
chapter: 7
cpp_standard:
- 17
- 20
description: We explain why `charconv`'s `from_chars`/`to_chars` is the fastest path
  for number-to-string conversions in the standard library—no locale, no exceptions,
  no allocations, and errors reported via return codes—and use real benchmarks to
  demonstrate the order-of-magnitude difference compared to `stoi`/`to_string`/`snprintf`.
difficulty: intermediate
order: 51
platform: host
prerequisites:
- string 深入：SSO、COW 与 resize_and_overwrite
- 迭代器适配器：反向、插入与流，把现成迭代器改出新行为
reading_time_minutes: 14
related:
- char8_t 与 UTF-8 字符串
tags:
- host
- cpp-modern
- intermediate
- 优化
title: 'charconv: Zero-Overhead Number-String Conversions'
translation:
  source: documents/vol3-standard-library/strings/51-charconv.md
  source_hash: c3fd8b6c352499fbb9f6c1a3abbca2ccb3da7abbd795624841d531cbfcb2f48c
  translated_at: '2026-06-24T00:48:46.864581+00:00'
  engine: anthropic
  token_count: 3028
---
# charconv: Zero-Overhead Number ↔ String Conversion

Number-to-string conversion is likely the most routine, yet easily botched, corner of the standard library. To format an `int`, the first instinct is `std::to_string`; to parse an `int`, the instinct is to reach for `std::stoi` or `std::atoi`. In most scenarios, this is fine—it works and is clear enough. But once we move into performance-sensitive areas (high-frequency logging, serialization, protocol parsing, or the thousands of field conversions in CSV/JSON), these old friends immediately show their true colors: `to_string` performs a `new` for a `std::string` every time, and `stoi` not only traverses a full locale lookup chain but can also throw exceptions.

C++17 delivers a set of primitives dedicated to this task: `from_chars` and `to_chars` in `<charconv>`. Their design goal is singular—**to be the fastest number-to-string conversion in the standard library**. To achieve this, the committee ruthlessly cut a bunch of "convenience" features: no format strings, no allocation, no locale dependency, no exceptions, and it doesn't even skip leading whitespace for you. In exchange, we get speedups ranging from several times to an order of magnitude. In this post, we will break down these primitives, focusing on why they are designed this way, how to use them without stepping on landmines, and quantifying exactly how much faster they are using real benchmarks.

## Why It Is Fast: Four "Don'ts"

To understand the speed of `charconv`, we must first look at what was cut. Let's use the most common `std::stoi("42")` as a comparison:

- `stoi` internally checks **locale** (regional settings). Even if you configure nothing, the rule that "the decimal point is a period" in the C locale still requires a lookup chain, because the standard must allow for the German locale to write the decimal point as a comma.
- `stoi` handles errors via **exceptions**. On failure, it `throw`s `std::invalid_argument`; on overflow, it `throw`s `std::out_of_range`. While the exception mechanism itself has low overhead on the happy path, it forces the implementation to retain numerous branches for "how to clean up errors."
- `stoi` accepts a `const std::string&`, which implies the result must land somewhere—parsing often involves temporary construction.
- Conversely, `to_string` **returns a new `std::string` every time**, implying a heap allocation (short strings might benefit from SSO, but converting an `int` often results in a string exceeding the SSO threshold).

`charconv` cuts all four of these:

1. **No Locale**: The behavior of `from_chars` / `to_chars` is identical across all locales worldwide; no tables are consulted. The decimal point is always a period.
2. **No Exceptions**: Error information is packed into a `std::errc` in the return value; the happy path never touches the exception mechanism.
3. **No Allocation**: `to_chars` doesn't return a `string`; it writes bytes directly into **a buffer you provide**. `from_chars` reads directly from a character span you provide and writes the result into a variable you pass in.
4. **No Format Strings**: There are no `"%d"` or `"%.6f"` strings to parse. Integers are just integers; floating-point formats (scientific, fixed, or hex) are passed via an enum and determined at compile time.

With these four cuts, we get "raw conversion"—aside from mapping the binary representation of a number to ASCII characters, it does almost nothing else. The price is that you must manage the buffer yourself, check return codes, and handle leading whitespace yourself. Next, we will look at how to do these things correctly, one by one.

## to_chars: Writing Directly to Your Buffer

Let's look at the integer version first. The signature of `to_chars` looks like this (for integers):

```cpp
// Standard: C++17
struct to_chars_result {
    char* ptr;            // 写完后的下一个位置（尾后指针）
    std::errc ec;         // 成功时为 {}（即 0）
};

to_chars_result to_chars(char* first, char* last, int value, int base = 10);
```

It takes a character buffer `[first, last)` that **you allocated**, writes the `value` into it, and returns where the write ended. Note that the returned `ptr` is a past-the-end pointer—this means you can immediately calculate how many bytes were written (`ptr - first`), and it implies that **it does not write a null terminator**. This is fundamentally different from `sprintf`, which appends a `\0`. `to_chars` does not, because it doesn't want to waste that single byte, nor does it assume you intend to use this character buffer as a C string.

Here is the minimal usage: converting an `int` and putting it back into a `std::string`:

```cpp
// Standard: C++17
#include <charconv>
#include <string>

char buf[16];
int value = 12345678;
auto res = std::to_chars(buf, buf + sizeof(buf), value);
// res.ptr 指向写完后的下一个位置；没有 '\0'
std::string out(buf, res.ptr);   // 用 [buf, res.ptr) 这段构造，恰好 8 个字符
```

Let's run it to verify:

```text
to_chars int: 12345678
```

The first pitfall with `to_chars` is that it "doesn't write a `\0`". If you habitually use `buf` directly as a C string (e.g., `printf("%s", buf)` or `std::string(buf)`), you will likely read uninitialized garbage until you accidentally hit a `\0`. The correct approach is to always use the `(buf, res.ptr)` pair of iterators/pointers to define the range you just wrote.

::: warning to_chars Does Not Write a Null Terminator
`to_chars` stops after writing the digits and does not append a `\0`. If you need a C string, manually add `*res.ptr = '\0';` (assuming there is at least one byte left in the buffer). If you need a `std::string`, use the `std::string(buf, res.ptr)` constructor. Using `buf` directly as a string is guaranteed to cause trouble.
:::

Integer `to_chars` also supports a `base` parameter (from 2 to 36), allowing you to use binary, hexadecimal, or even base-32:

```cpp
// Standard: C++17
char buf[32];
auto r = std::to_chars(buf, buf + sizeof(buf), 255, 16);   // 十六进制
// [buf, r.ptr) = "ff"
```

### What if the buffer is not large enough

The error handling in `to_chars` is quite restrained: **there is only one error condition**, which occurs when the buffer you provided is too small. In this case, `ptr` is set to `last` (one past the end of the buffer), and `ec` is set to `std::errc::value_too_large`:

```cpp
// Standard: C++17
char buf[3];
auto r = std::to_chars(buf, buf + sizeof(buf), 123456);
// r.ptr == buf + 3 (== last), r.ec == std::errc::value_too_large
```

Let's test the behavior of GCC 16.1.1:

```text
to_chars 123456 into buf[3]: ec==value_too_large? 1 ptr==last? 1
```

So checking for success is a one-liner: `if (res.ec == std::errc{})` (or the equivalent `if (!res.ec)`). Integers won't fail due to their value—any `int` can be converted—so you only need to ensure the buffer is large enough. A safe upper bound: a decimal `int` is at most 11 characters (including the sign), so a buffer of `std::numeric_limits<int>::digits10 + 2` is definitely sufficient for integers.

## from_chars: Read a sequence of characters, populate a variable

The signature for the reverse operation, `from_chars`, is:

```cpp
// Standard: C++17
struct from_chars_result {
    const char* ptr;       // 解析停下的位置
    std::errc ec;          // 成功为 {}; 失败为 invalid_argument 或 result_out_of_range
};

from_chars_result from_chars(const char* first, const char* last,
                             int& value, int base = 10);
```

It reads characters from the range `[first, last)`, parses the number into `value`, and returns the stopping position. On success, `ptr` points to the **first character that was not recognized as part of the number**—this is a practical design, as it allows us to continue parsing the next field using `ptr`, providing both the parsing result and the remaining unread data in one go.

Minimal usage:

```cpp
// Standard: C++17
std::string s = "42abc";   // 注意后面跟了非数字
int v = 0;
auto res = std::from_chars(s.data(), s.data() + s.size(), v);
// res.ec == {}, v == 42, res.ptr 指向 'a'（s.data()+2）
```

Testing:

```text
from_chars '42abc' -> value=42 ptr-offset=2 ec=0
```

`ptr` stops at offset two, which is exactly where `'a'` is located—the numeric part was consumed, and the rest remains as is. This feels much smoother than the `stoi` approach of "passing a `size_t* pos` to tell you where it stopped."

`from_chars` has two types of failure, corresponding to two error codes:

- **`std::errc::invalid_argument`**: The input is not a number at all (for example, `"abc"`, or an empty range).
- **`std::errc::result_out_of_range`**: The input is a valid number, but it exceeds the range of the target type (for example, trying to stuff `"999999999999999999999"` into an `int`).

Testing overflow in practice:

```text
from_chars overflow int -> ec is err=1
```

Here is another detail worth remembering—**`value` remains unchanged on error**. `from_chars` does not modify the variable you passed in when parsing fails. We will leverage this behavior later in the "Common Pitfalls" section.

### `from_chars` Does Not Skip Leading Whitespace

This is the single biggest behavioral difference from `stoi` / `strtod`, and it is the most commonly overlooked pitfall. `from_chars` **does not skip any leading whitespace**—it requires that the first character of the input be a digit (or a sign). Leading spaces? It immediately returns `invalid_argument`.

Let's compare `from_chars` and `stoi` using the string `"   42"`, which contains leading whitespace:

```cpp
// Standard: C++17
std::string s = "   42";
int v = -1;
auto r = std::from_chars(s.data(), s.data() + s.size(), v);
// r.ec == std::errc::invalid_argument, v 仍是 -1（没被改）

size_t idx = 0;
int sv = std::stoi(s, &idx);
// sv == 42, idx == 5 —— stoi 主动跳过了前导空白
```

Benchmark:

```text
from_chars '   42': ec-ok=0 value=-1 (v unchanged on err)
stoi '   42': value=42 consumed=5 (skips leading ws)
```

`from_chars` returns failure, leaving `v` unchanged at `-1`, while `stoi` happily skips the whitespace and parses out `42`. This isn't a bug in `from_chars`, but a deliberate trade-off: skipping whitespace is locale-dependent (what counts as whitespace requires checking `isspace` tables), and doing so would violate the "locale-independent" design goal. Therefore, when using `from_chars` to parse user input or file fields, **you must `trim` the leading whitespace yourself**, or use `std::find_if` to locate the first non-whitespace character before passing it to the function.

::: warning from_chars Does Not Skip Leading Whitespace
`from_chars` requires the first character of the input to be a digit or a sign; leading whitespace immediately results in an `invalid_argument` error. This is the opposite of `stoi` / `strtod`, which actively skip whitespace. When parsing input containing whitespace (user input, CSV fields), you must trim it first.
:::

## Performance Benchmarking: How Big is the Difference?

We've discussed a lot about "why it's fast," but how much faster is it really? In this section, we will actually run the code. Using GCC 16.1.1 locally, compiled with `g++ -std=c++20 -O2`, each test case runs 5,000,000 times (integers) / 3,000,000 times (floating-point), measuring the wall-clock time of the entire pipeline (including writing the results to memory).

First, let's look at **Integer → String** (`to_chars` vs `std::to_string` vs `std::snprintf("%d")`):

```text
[int->str] to_chars :  46.6 ms
[int->str] to_string:  49.8 ms
[int->str] snprintf : 171.1 ms
```

Here is a counterintuitive point worth mentioning on its own: **`to_chars` and `to_string` are almost identical in speed for integer paths**. This isn't because `to_chars` lacks an advantage, but because modern libstdc++ (since GCC 11+) implements `std::to_string(int)` **using `to_chars` under the hood**. Therefore, for integer formatting, they are essentially the same thing; the only difference is the extra `std::string` constructor wrapper in `to_string`. The real performance gap lies with `snprintf`—it must parse the `"%d"` format string and handle locale, making it more than three times slower than the other two.

If we reverse the direction to **string → integer** (`from_chars` vs `std::stoi` vs `std::atoi`):

```text
[str->int] from_chars:  28.4 ms
[str->int] stoi     :  82.9 ms
[str->int] atoi     :  93.1 ms
```

This time, the gap is significant: `from_chars` is nearly **3x faster** than `stoi`, and over 3x faster than `atoi`. `stoi` is slower mainly due to locale lookups and exception handling paths (even if no exception is thrown, the branching logic is still there), while `atoi` is slower because it relies on the C `strtod` family and requires null termination. Since `to_string` cannot serve as the "same underlying implementation" for `from_chars`, the performance advantage of `charconv` in parsing is substantial.

Floating-point operations are where `charconv` truly shines. **`double` to string** (`to_chars` vs `std::to_string` vs `std::snprintf("%.17g")`):

```text
[dbl->str] to_chars :  96.7 ms
[dbl->str] to_string: 814.6 ms
[dbl->str] snprintf : 765.7 ms
```

`to_chars` is over **8 times faster** than `to_string`, and nearly 8 times faster than `snprintf`. This isn't just a minor optimization; it stems from the modern floating-point formatting algorithms used in `charconv` (inspired by the Ryū / Schubfach papers). The goal of `to_chars` is to provide the "shortest round-trip representation," and it achieves this without any memory allocation or locale queries. For any scenario involving serializing large amounts of floating-point data (saving numerical results to disk, exporting metrics, or scientific data), switching to `to_chars` is basically a free order-of-magnitude performance gain.

> The absolute microsecond values will vary across different machines and loads, but the **order-of-magnitude relationships are robust**: for integers, `to_chars` is on par with `to_string` and far faster than `snprintf`; for parsing, `from_chars` is about 3 times faster than `stoi`; for floating-point numbers, `to_chars` is nearly an order of magnitude faster than traditional methods. I ran three consecutive rounds, and the conclusion was consistent.

## Floating-Point: `chars_format` and GCC Support Status

Now that we've covered integers, let's look at floating-point numbers. The floating-point versions of `from_chars` and `to_chars` add a `std::chars_format` parameter:

```cpp
// Standard: C++17
enum class chars_format {
    scientific = 0x1,   // 1.234e+05
    fixed      = 0x2,   // 123456.789
    hex        = 0x4,   // 1.8p+1
    general    = scientific | fixed   // 自动挑（to_chars 默认）
};
```

The default behavior of `to_chars` for `double` (when no format is specified) is `general`, but it outputs the **shortest round-trip representation**. This means it uses the fewest characters possible to ensure that `from_chars` can read back the exact same `double` value. This is smarter than `sprintf`'s `"%.6f"` (fixed precision) or `std::to_string(double)` (fixed six decimal places):

```cpp
// Standard: C++17
char buf[64];
double d = 123456.789;
auto r1 = std::to_chars(buf, buf + sizeof(buf), d);                 // "123456.789"
auto r2 = std::to_chars(buf, buf + sizeof(buf), d,
                        std::chars_format::scientific);             // "1.23456789e+05"
auto r3 = std::to_chars(buf, buf + sizeof(buf), d,
                        std::chars_format::fixed);                  // "123456.789"
```

Here is the translation of the provided text.

```text
Testing three formats:
```

```text
to_chars default: 123456.789
to_chars scientific: 1.23456789e+05
to_chars fixed: 123456.789
```

Conversely, floating-point `from_chars` parses strings according to the specified `chars_format`. The hex format is slightly unique: it uses `p` to separate the binary exponent (e.g., `1.8p+1` represents `1.5 × 2¹ = 3.0`), consistent with `%a`:

```cpp
// Standard: C++17
std::string s = "1.8p+1";   // 1.5 * 2^1 = 3.0
double d = 0;
auto r = std::from_chars(s.data(), s.data() + s.size(), d, std::chars_format::hex);
// d == 3.0
```

Actual measurement:

```text
from_chars double hex '1.8p+1' -> d=3
```

### GCC Support Status (Tested on 16.1.1)

The floating-point portion of `<charconv>` carries some historical baggage: **although C++17 defined floating-point `from_chars` / `to_chars`, implementation was difficult, and it took major compilers a long time to catch up**. GCC's floating-point `from_chars` only landed completely in **11.1** (floating-point `to_chars` arrived earlier, in 8.1), and Clang/libc++ lagged even longer. This led to many older online resources stating "floating-point `from_chars` is unavailable"—which is now outdated.

Local testing on GCC 16.1.1: floating-point `from_chars` (scientific / fixed / hex formats) and `to_chars` (including the default shortest representation) are fully available; all examples above actually run. So: **as long as you don't need to support GCC 10 or earlier, you can safely use floating-point `charconv`**. If you are writing a cross-platform library or need to support older toolchains, detect support at compile time using the feature macro `__cpp_lib_to_chars` (`#ifdef __cpp_lib_to_chars`), which is defined on GCC 16.1.1. Note that `<charconv>` does not have a separate `__cpp_lib_charconv` macro, so don't use the wrong name.

## Common Pitfalls

Let's consolidate the places where `charconv` is most likely to bite you; every point here has been verified by the tests above:

::: warning Ignoring ec in the return value
`from_chars` / `to_chars` rely on the returned `ec` for error reporting and do not throw exceptions. **Forgetting to check `ec` and using `value` directly is the most common pitfall**. Fortunately, when an error occurs, `from_chars` leaves `value` untouched (retaining its original value), so what you see is "stale data"—a subtle bug where the program doesn't crash, but the result is wrong. Make it a habit: `if (auto r = std::from_chars(...); r.ec == std::errc{}) { use value }`.

For `to_chars`, the consequence of ignoring `ec` is more direct: when the buffer is insufficient, it writes nothing, so constructing a string with `(buf, r.ptr)` yields uninitialized contents.
:::

::: warning Buffer too small
When the `to_chars` buffer is too small, it returns `ptr == last` and `ec == value_too_large`, and **does not guarantee what was written** (it might have written part of it or nothing at all). For integers, a buffer of `std::numeric_limits<T>::digits10 + 2` (decimal digits + sign + margin) is sufficient. For floating-point using the shortest representation, 32 bytes is generally enough for `double`. If unsure, make it larger; `charconv` doesn't mind a big buffer.
:::

::: warning from_chars does not skip leading whitespace
As emphasized earlier, let's nail this down: `from_chars` requires the first character to be a digit or sign; it does not skip whitespace or anything other than the sign. When parsing external input (user-typed, files, network fields), be sure to `trim` first, or use `std::find_if` + `!std::isspace` to locate the first valid character before feeding it to the function.
:::

::: warning to_chars does not write a null terminator
`to_chars` stops immediately after writing the digits and does not append `\0`. If you need a C string, add it yourself (`*r.ptr = '\0'`); if you need a `std::string`, use `std::string(buf, r.ptr)`. Calling `printf("%s", buf)` directly will blow up.
:::

::: warning Sign bit boundaries
Can `from_chars` consume a leading `-` for unsigned types? **No**—stuffing `"-1"` into an `unsigned` returns `invalid_argument` (see the unsigned example above; `u` remains unchanged). This differs from `strtoul`, which accepts negative numbers and converts them to unsigned values. To support signed notation for unsigned fields, you must first parse as signed, then check the range.
:::

## Summary

The value of `charconv` can be summed up in one sentence—**it is the performance ceiling for standard library number-to-string conversions**, at the cost of managing buffers, checking return codes, and handling whitespace yourself. Here are the key takeaways:

- Four "don'ts" buy speed: no locale, no exceptions, no allocation, no format strings. `to_chars` writes directly to the buffer you give it; `from_chars` reads directly from the range you give it.
- For integers, `to_chars` is almost as fast as `std::to_string` (libstdc++'s `to_string(int)` uses `to_chars` internally), and both are far faster than `snprintf`.
- For parsing, `from_chars` is about 3x faster than `stoi` (measured 28 ms vs 83 ms over 5 million iterations).
- For floating-point, `to_chars` is nearly an order of magnitude faster than traditional methods (measured 97 ms vs 800 ms over 3 million iterations), thanks to modern shortest-roundtrip formatting algorithms.
- Floating-point `from_chars` / `to_chars` are fully available in GCC 16.1.1 (including scientific / fixed / hex formats); use `__cpp_lib_to_chars` to detect support for older toolchains.
- Five high-frequency pitfalls: forgetting to check `ec`, buffer too small, not skipping leading whitespace, `to_chars` doesn't write `\0`, and unsigned types reject `-`.

The next post covers `<format>` (C++20) and `<print>` (C++23)—these are high-level facilities built on top of these low-level primitives that "add format strings, type safety, and locale support." They are certainly more convenient, but the cost is that they can't match the raw performance of `charconv`. Once you understand the `charconv` layer, it becomes very natural to see why `<format>` internally calls `to_chars`.

## References

- [cppreference: std::to_chars](https://en.cppreference.com/w/cpp/utility/to_chars) — `to_chars` overloads, `chars_format`, return value semantics
- [cppreference: std::from_chars](https://en.cppreference.com/w/cpp/utility/from_chars) — `from_chars` overloads, error codes (`invalid_argument` / `result_out_of_range`)
- [cppreference: std::chars_format](https://en.cppreference.com/w/cpp/utility/chars_format) — four formats: `scientific` / `fixed` / `hex` / `general`
- [P0067R5: Elementary string conversions](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2016/p0067r5.html) — The proposal that brought `charconv` into the standard, discussing the design motivation of "no locale / no exceptions / no allocation"
