---
chapter: 7
cpp_standard:
- 17
- 20
description: A deep dive into why charconv's from_chars/to_chars is the standard library's
  fastest number-string conversion path—no locale, no exceptions, no allocation, errors
  reported through return codes—with real benchmarks charting the order-of-magnitude
  gap against stoi/to_string/snprintf
difficulty: intermediate
order: 51
platform: host
prerequisites:
- 'Deep Dive into std::string: SSO, COW, and resize_and_overwrite'
- 'Iterator Adapters: Reverse, Insertion, and Stream — Teaching Old Iterators New
  Tricks'
reading_time_minutes: 14
related:
- char8_t and UTF-8 Strings
tags:
- host
- cpp-modern
- intermediate
- 优化
title: 'charconv: Zero-Overhead Number-String Conversions'
translation:
  source: documents/vol3-standard-library/strings/51-charconv.md
  source_hash: c3fd8b6c352499fbb9f6c1a3abbca2ccb3da7abbd795624841d531cbfcb2f48c
  translated_at: '2026-09-25T23:50:30+00:00'
  engine: anthropic
  token_count: 9700
---

# charconv: Zero-Overhead Number-String Conversions

Converting between numbers and strings is probably the most everyday corner of the standard library—and the corner most easily written sloppily. Need to format an `int`? Most people's first reflex is `std::to_string`. Need to parse an `int`? First reflex is to reach for `std::stoi` or `std::atoi`. In most scenarios there's nothing wrong with that—the code runs, and it's clear enough. But the moment you shift your gaze somewhere performance-sensitive (high-frequency logging, serialization, protocol parsing, the thousands upon thousands of field conversions inside things like CSV/JSON), these two old pals immediately show their cracks: `to_string` hands you a freshly `new`-ed `std::string` on every call, and `stoi` walks an entire locale-lookup chain and may even throw.

C++17 brought us a set of primitives built precisely for this job: `from_chars` and `to_chars` in `<charconv>`. Their design goal is exactly one thing—**to be the fastest number↔string conversions in the standard library**. To get there, the committee resolutely cut away a pile of "convenience" features: no format-string parsing, no allocation, no locale dependence, no exceptions, not even leading-whitespace skipping. What comes back in exchange is a speedup of several times to more than ten times. In this article we take this set of primitives apart and run it through, focusing on why it is designed the way it is, how to use it without stepping on rakes, and—with real benchmarks—just how much faster it gets.

## Why It Is Fast: Four Things It Refuses to Do

To see why `charconv` is fast, you first have to see what it cut away on our behalf. Compare against the most common `std::stoi("42")`:

- Internally, `stoi` consults the **locale**. Even if you never configure anything, rules like "the decimal point is a period" in the C locale still have to go through a lookup chain, because the standard must allow a German locale to spell the decimal point as a comma.
- `stoi`'s error handling goes through **exceptions**. On a parse failure it throws `std::invalid_argument`; on overflow it throws `std::out_of_range`. The exception machinery itself costs little on the normal path, but it forces the implementation to keep a pile of "how do I clean up after an error" branches alive.
- `stoi` takes a `const std::string&`, and the result needs somewhere to land too—so the parse is often accompanied by temporary constructions.
- `to_string`, in the other direction, **returns a brand-new `std::string` on every call**, which means a heap allocation (short strings get off free via SSO, but an `int` easily converts to a dozen-plus digits and mostly blows past the SSO threshold).

`charconv` cut all four away:

1. **No locale**: `from_chars` / `to_chars` behave identically under every locale in the world and consult no tables. The decimal point is always a period.
2. **No exceptions**: error information is packed into a `std::errc` inside the return value; the normal path never touches the exception machinery at all.
3. **No allocation**: `to_chars` doesn't return a `string`—it writes bytes straight into **the buffer you hand it**; `from_chars` reads the character range you hand it and writes the parsed result into the variable you pass in.
4. **No format string**: there is no `"%d"` or `"%.6f"` to parse. An integer is an integer; the floating-point format (scientific / fixed / hex) is passed as an enum and pinned down at compile time.

With those four cuts, what you get is a "bare conversion"—apart from mapping between a number's binary representation and ASCII characters, it does almost nothing else. The price is that you manage the buffer yourself, check the return code yourself, and deal with leading whitespace yourself. Let's walk through, one by one, how to do each of those things right.

## to_chars: Writing Directly into Your Buffer

Integer version first. The `to_chars` signature looks like this (integers):

```cpp
// Standard: C++17
struct to_chars_result {
    char* ptr;            // one past the last character written (past-the-end pointer)
    std::errc ec;         // {} (i.e., 0) on success
};

to_chars_result to_chars(char* first, char* last, int value, int base = 10);
```

It takes a character buffer **that you allocated**—`[first, last)`—writes `value` into it, and reports where it stopped. Note that the returned `ptr` is a past-the-end pointer: that means you can immediately work out how many bytes were written (`ptr - first`), and it also means **it does not write a null terminator**. This is utterly different from `sprintf`, which appends a `\0`; `to_chars` does not, because it doesn't want to waste that one byte, and it doesn't want to assume you'll use these characters as a C string.

Minimal usage—convert an `int` out and then tuck it back into a `std::string`:

```cpp
// Standard: C++17
#include <charconv>
#include <string>

char buf[16];
int value = 12345678;
auto res = std::to_chars(buf, buf + sizeof(buf), value);
// res.ptr points one past the last character written; no '\0'
std::string out(buf, res.ptr);   // construct from the [buf, res.ptr) range: exactly 8 characters
```

Run it to confirm:

```text
to_chars int: 12345678
```

The first trap `to_chars` springs on people sits precisely on that "no `\0`". If you habitually treat `buf` as a C string right away (`printf("%s", buf)` or `std::string(buf)`), odds are you'll read uninitialized garbage past the end until you happen to hit a stray `\0`. The correct approach is always to delimit the span you just wrote with the `(buf, res.ptr)` pair of pointers.

::: warning to_chars does not write a null terminator
`to_chars` stops once the number is written and appends no `\0`. If you need a C string, write it yourself: `*res.ptr = '\0';` (provided the buffer has at least one byte left); if you need a `std::string`, construct with `std::string(buf, res.ptr)`. Using bare `buf` as a string is a guaranteed rake.
:::

The integer `to_chars` also takes a `base` parameter (2 through 36)—binary, hexadecimal, even base-32, whatever you like:

```cpp
// Standard: C++17
char buf[32];
auto r = std::to_chars(buf, buf + sizeof(buf), 255, 16);   // hexadecimal
// [buf, r.ptr) = "ff"
```

### What If the Buffer Is Too Small

`to_chars`' error handling is admirably restrained: **there is exactly one failure case**—the buffer you hand it cannot hold the result. In that case `ptr` is set to `last` (the buffer's past-the-end) and `ec` to `std::errc::value_too_large`:

```cpp
// Standard: C++17
char buf[3];
auto r = std::to_chars(buf, buf + sizeof(buf), 123456);
// r.ptr == buf + 3 (== last), r.ec == std::errc::value_too_large
```

Let's verify this behavior on GCC 16.1.1:

```text
to_chars 123456 into buf[3]: ec==value_too_large? 1 ptr==last? 1
```

So checking success is a single line: `if (res.ec == std::errc{})` (or the equivalent `if (!res.ec)`). Integers cannot fail on the value itself—any `int` converts—so all you have to guarantee is a buffer big enough. A safe upper bound: an `int` in decimal takes at most 11 characters (sign included), so a buffer sized `std::numeric_limits<int>::digits10 + 2` is absolutely enough for integers.

## from_chars: Reading a Span of Characters into a Variable

The `from_chars` signature in the opposite direction:

```cpp
// Standard: C++17
struct from_chars_result {
    const char* ptr;       // where parsing stopped
    std::errc ec;          // {} on success; invalid_argument or result_out_of_range on failure
};

from_chars_result from_chars(const char* first, const char* last,
                             int& value, int base = 10);
```

It reads the character range `[first, last)`, writes the parsed number into `value`, and reports where it stopped. On success `ptr` points at **the first character not recognized as part of the number**—a very practical design: it means you can take `ptr` and carry on parsing the next field; the parse result and "how much is left unread" are handed to you in one shot.

Minimal usage:

```cpp
// Standard: C++17
std::string s = "42abc";   // note the trailing non-digit characters
int v = 0;
auto res = std::from_chars(s.data(), s.data() + s.size(), v);
// res.ec == {}, v == 42, res.ptr points at 'a' (s.data()+2)
```

Verified:

```text
from_chars '42abc' -> value=42 ptr-offset=2 ec=0
```

`ptr` stops at offset 2—exactly where `'a'` sits: the digit part was consumed, and the rest is left untouched. That's much more convenient than `stoi`'s routine of passing an extra `size_t* pos` for it to tell you where it stopped.

`from_chars` has two classes of failure, each with its own error code:

- **`std::errc::invalid_argument`**: the input isn't a number at all (say `"abc"`, or an empty range).
- **`std::errc::result_out_of_range`**: the input is a valid number, but falls outside the target type's range (say stuffing `"999999999999999999999"` into an `int`).

Verified overflow:

```text
from_chars overflow int -> ec is err=1
```

One more detail worth remembering—**on failure, `value` is left unchanged**. `from_chars` never touches the variable you passed in when parsing fails. This will come in handy again in the pitfalls section below.

### from_chars Does Not Skip Leading Whitespace

This is the biggest behavioral gap from `stoi` / `strtod`, and the most commonly overlooked trap. `from_chars` **does not skip any leading whitespace**—it demands that the very first input character be a digit (or a sign). Spaces up front? Straight to `invalid_argument`.

Let's test `from_chars` and `stoi` side by side on `"   42"`, which carries leading whitespace:

```cpp
// Standard: C++17
std::string s = "   42";
int v = -1;
auto r = std::from_chars(s.data(), s.data() + s.size(), v);
// r.ec == std::errc::invalid_argument, v is still -1 (unchanged)

size_t idx = 0;
int sv = std::stoi(s, &idx);
// sv == 42, idx == 5 — stoi actively skipped the leading whitespace
```

Side-by-side results:

```text
from_chars '   42': ec-ok=0 value=-1 (v unchanged on err)
stoi '   42': value=42 consumed=5 (skips leading ws)
```

`from_chars` reports failure and `v` sits untouched at `-1`; `stoi` cheerfully skips the spaces and parses out 42. This is not a bug in `from_chars`—it's a deliberate trade-off: whitespace skipping is locale-dependent (what counts as whitespace requires consulting the `isspace` table), and skipping would violate the "no locale" design goal. So when parsing user input or file fields with `from_chars`, **trim the leading whitespace off yourself first**, or use `std::find_if` to locate the first non-whitespace character and feed it from there.

::: warning from_chars does not skip leading whitespace
`from_chars` requires the first input character to be a digit or a sign; leading whitespace is judged `invalid_argument` outright. This is the opposite of `stoi` / `strtod`, which skip whitespace on their own initiative. When parsing input that may carry whitespace (user keystrokes, CSV fields), trim it first.
:::

## Measured Performance: Just How Big Is the Gap

That was a lot of "why it's fast"—so how much faster, exactly? In this section we actually run it. Local GCC 16.1.1, compiled with `g++ -std=c++20 -O2`, each case run 5,000,000 times (integers) / 3,000,000 times (floating point), measuring the wall-clock time of the whole pipeline (including writing results out).

First, **integer → string** (`to_chars` vs `std::to_string` vs `std::snprintf("%d")`):

```text
[int->str] to_chars :  46.6 ms
[int->str] to_string:  49.8 ms
[int->str] snprintf : 171.1 ms
```

There's a counterintuitive point here worth singling out: **on the integer path, `to_chars` and `to_string` are nearly identical in speed**. The reason isn't that `to_chars` holds no advantage—it's that in modern libstdc++ (GCC 11 onward), `std::to_string(int)` is **implemented on top of `to_chars`** in the first place. For integer formatting the two are literally the same thing; the only difference is that `to_string` wraps an extra `std::string` construction around it. The real chasm is `snprintf`—it has to parse the `"%d"` format string and deal with locale on top, leaving it more than 3 times slower than the other two.

Flip the direction—**string → integer** (`from_chars` vs `std::stoi` vs `std::atoi`):

```text
[str->int] from_chars:  28.4 ms
[str->int] stoi     :  82.9 ms
[str->int] atoi     :  93.1 ms
```

Now the gap opens up: `from_chars` is close to **3 times faster** than `stoi`, and more than 3 times faster than `atoi`. `stoi`'s slowness sits mostly in locale lookups and the exception-handling path (even if nothing is ever thrown, the branches are still there); `atoi` is slow because it goes through the C `strtod`-family machinery and requires null termination. Here `to_string` cannot be flipped around to serve as a "same implementation" for `from_chars`, so on the parsing side `charconv`'s advantage is the real deal.

Floating point is where `charconv` truly gets to shine. **`double` → string** (`to_chars` vs `std::to_string` vs `std::snprintf("%.17g")`):

```text
[dbl->str] to_chars :  96.7 ms
[dbl->str] to_string: 814.6 ms
[dbl->str] snprintf : 765.7 ms
```

`to_chars` is more than **8 times faster** than `to_string`, and nearly 8 times faster than `snprintf`. At this magnitude we're no longer talking minor tuning—it comes from the modern floating-point formatting algorithms `charconv` uses (the Ryū / Schubfach line of research; `to_chars`' implementation goal is the "shortest round-trippable representation", achieved with zero allocation and zero locale lookups throughout). For any scenario that serializes large volumes of floating-point values (numerical results to disk, metrics export, scientific data), switching to `to_chars` is basically a free order of magnitude.

> The absolute microsecond values will drift across machines and workloads, but **the order-of-magnitude relationships are stable**: on the integer path `to_chars` ties with `to_string` and beats `snprintf` by a wide margin; on the parsing path `from_chars` is about 3 times faster than `stoi`; on the floating-point path `to_chars` is nearly an order of magnitude faster than the traditional tools. I ran three rounds in a row; the conclusions held.

## Floating Point: chars_format and the State of GCC Support

With integers covered, on to floating point. The floating-point versions of `from_chars` / `to_chars` add an `std::chars_format` parameter:

```cpp
// Standard: C++17
enum class chars_format {
    scientific = 0x1,   // 1.234e+05
    fixed      = 0x2,   // 123456.789
    hex        = 0x4,   // 1.8p+1
    general    = scientific | fixed   // picks automatically (to_chars default)
};
```

`to_chars`'s default behavior for `double` (no format passed) is `general`, but it emits the **shortest round-trippable representation**—the fewest characters that still guarantee `from_chars` reads back the exact same `double`. That's smarter than `sprintf`'s `"%.6f"` (fixed number of digits) or `std::to_string(double)` (fixed 6 decimal places):

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

All three formats, verified:

```text
to_chars default: 123456.789
to_chars scientific: 1.23456789e+05
to_chars fixed: 123456.789
```

Floating-point `from_chars` works the other way, reading according to the `chars_format`. The hex format is a bit special: it uses `p` to separate the binary exponent (`1.8p+1` means `1.5 × 2¹ = 3.0`), in the same vein as `%a`:

```cpp
// Standard: C++17
std::string s = "1.8p+1";   // 1.5 * 2^1 = 3.0
double d = 0;
auto r = std::from_chars(s.data(), s.data() + s.size(), d, std::chars_format::hex);
// d == 3.0
```

Verified:

```text
from_chars double hex '1.8p+1' -> d=3
```

### The State of GCC Support (Verified on 16.1.1)

The floating-point part of `<charconv>` carries some historical baggage: **C++17 did define floating-point `from_chars` / `to_chars`, but they were hard to implement, and the major compilers took a long time to catch up**. GCC's floating-point `from_chars` didn't fully land until **11.1** (floating-point `to_chars` came earlier, from 8.1 on), and Clang/libc++ was for a while later still. That's why a lot of older material online claims "floating-point `from_chars` is unavailable"—which is outdated today.

Verified on local GCC 16.1.1: floating-point `from_chars` (all three formats—scientific / fixed / hex) and `to_chars` (including the default shortest representation) are fully available; every example above genuinely ran. Conclusion: **as long as you don't need to stay compatible with GCC 10 or earlier, floating-point charconv is safe to use**. For a cross-platform library that must accommodate older toolchains, probe with the feature-test macro `__cpp_lib_to_chars` at compile time (`#ifdef __cpp_lib_to_chars`); it is defined on GCC 16.1.1. Note that `<charconv>` has no separate `__cpp_lib_charconv` macro—don't get the name wrong.

## A Few Pitfalls You'll Actually Hit

Let's collect in one place the spots where `charconv` most often flips over, every one of them verified by the tests above:

::: warning Ignoring the ec in the return value
Both `from_chars` / `to_chars` report errors through the returned `ec`; nothing is ever thrown. **Forgetting to check `ec` and using `value` directly is the most common trap of all**. The small mercy is that on error `from_chars` leaves `value` alone (it keeps its old value), so what you'll see is "the variable's previous value"—a stealthy bug: the program doesn't crash, but the result is wrong. Build the habit: `if (auto r = std::from_chars(...); r.ec == std::errc{}) { use value }`.

On the `to_chars` side, forgetting `ec` has a more direct consequence: when the buffer is too small nothing was written at all, and constructing a string from `(buf, r.ptr)` hands you uninitialized contents.
:::

::: warning Making the buffer too small
When the `to_chars` buffer is too small, it returns `ptr == last` and `ec == value_too_large`, and **makes no promise about what was written** (it may have written part of the result, or nothing). For integers, a buffer of `std::numeric_limits<T>::digits10 + 2` (decimal digits + sign + slack) is plenty; for floating point with the shortest representation, 32 bytes is generally enough for a `double`. When in doubt, go bigger—`charconv` never complains about a large buffer.
:::

::: warning from_chars does not skip leading whitespace
Emphasized earlier, nailed down once more here: `from_chars` requires the first character to be a digit or a sign; it skips no whitespace and nothing else besides. When parsing external input (user keystrokes, files, network fields), always trim first, or use `std::find_if` + `!std::isspace` to locate the first valid character before feeding it in.
:::

::: warning to_chars does not write a null terminator
`to_chars` stops as soon as the number is written—no `\0` appended. Need a C string? Add it yourself (`*r.ptr = '\0'`). Need a `std::string`? Use `std::string(buf, r.ptr)`. Calling `printf("%s", buf)` on the raw buffer is a guaranteed rake.
:::

::: warning The sign character boundary
Can `from_chars` swallow a leading `-` for unsigned types? **No**—stuffing `"-1"` into an `unsigned` returns `invalid_argument` (see the unsigned example tested above, where `u` stays unchanged). This differs from `strtoul`, which accepts negative input and then converts it to the unsigned value. To support signed notation for unsigned fields, parse as a signed type first, then range-check yourself.
:::

## Summary

`charconv`'s value fits in one sentence—**it is the performance ceiling for number↔string conversion in the standard library**, at the cost of managing buffers, checking return codes, and handling whitespace yourself. The key conclusions, collected:

- Four "don'ts" buy the speed: no locale, no exceptions, no allocation, no format string. `to_chars` writes directly into the buffer you give it; `from_chars` reads directly from the range you give it.
- On the integer path, `to_chars` is nearly as fast as `std::to_string` (libstdc++'s `to_string(int)` is `to_chars` underneath), and both are far faster than `snprintf`.
- On the parsing path, `from_chars` is about 3 times faster than `stoi` (measured 28 ms vs 83 ms, 5 million runs).
- On the floating-point path, `to_chars` is nearly an order of magnitude faster than the traditional tools (measured 97 ms vs 800 ms, 3 million runs), thanks to the modern shortest-round-trip formatting algorithm.
- Floating-point `from_chars` / `to_chars` are fully available on GCC 16.1.1 (all three formats: scientific / fixed / hex); for older toolchains, probe with `__cpp_lib_to_chars`.
- Five high-frequency pitfalls: forgetting to check `ec`, a too-small buffer, no leading-whitespace skipping, `to_chars` not writing `\0`, and unsigned types rejecting `-`.

Next up are `<format>` (C++20) and `<print>` (C++23)—the high-level facilities built on top of these low-level primitives, adding format strings, type safety, and locale support. Convenient, certainly, but the price is that they cannot reach `charconv`'s bare-metal performance. Once you understand the `charconv` layer, looking back at why `format` internally calls `to_chars` will feel completely natural.

## References

- [cppreference: std::to_chars](https://en.cppreference.com/w/cpp/utility/to_chars) — the `to_chars` overload family, `chars_format`, return-value semantics
- [cppreference: std::from_chars](https://en.cppreference.com/w/cpp/utility/from_chars) — the `from_chars` overload family, error codes (`invalid_argument` / `result_out_of_range`)
- [cppreference: std::chars_format](https://en.cppreference.com/w/cpp/utility/chars_format) — the four formats `scientific` / `fixed` / `hex` / `general`
- [P0067R5: Elementary string conversions](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2016/p0067r5.html) — the proposal that brought `charconv` into the standard, covering the "no locale / no exceptions / no allocation" design motivation
