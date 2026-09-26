---
title: 'print: Direct Output in C++23 and Decoupling from iostream'
description: A thorough walkthrough of how std::print/std::println bypass cout's sync_with_stdio
  and locale overhead to write straight to the stream, why mixing them with cout after
  sync is turned off scrambles the output order (and how print(cout,...) rescues it
  in one move), the order-of-magnitude gap of print vs cout/printf under a real benchmark,
  and the engineering trade-offs of the FILE*/ostream overload pair and Unicode output
chapter: 7
order: 53
cpp_standard:
  - 23
difficulty: intermediate
platform: host
reading_time_minutes: 14
prerequisites:
  - 'Iterator Adapters: Reverse, Insertion, and Stream — Teaching Old Iterators New Tricks'
  - 'char8_t and UTF-8 Strings'
related:
  - 'Container Selection Guide: Choosing the Right Container Based on Operations, Memory, and Invalidation Rules'
tags:
  - host
  - cpp-modern
  - intermediate
  - 基础
translation:
  source: documents/vol3-standard-library/strings/53-print.md
  source_hash: 97459e32ff8bc7f9723c65bea233c8acbd5b6b50d6c55099b0218f995b1a17ee
  translated_at: '2026-09-26T00:01:26+00:00'
  engine: anthropic
  token_count: 6900
---
# print: Direct Output in C++23 and Decoupling from iostream

`std::format` (C++20) solved the problem of "how do I stitch data into a string", but it left an awkward tail behind: to get the finished string onto the screen, you still had to hand it back to `std::cout << std::format(...)`. That detour hands back the overhead `format` worked so hard to save — `cout` carries iostream's entire synchronization-and-locale apparatus on its back, so every `<<` costs real money. `std::print` / `std::println` (C++23) exist to close out that tail: formatting and output merged into a single call that writes straight to the stream, no longer routed through iostream's `<<` chain.

This article focuses on the **output semantics** of `print` — what entitles it to be faster than `cout`, the classic ordering trap that springs when you mix it with `cout`, the orders of magnitude under a real benchmark, and how to choose between the `FILE*` and `ostream` overload sets. Format-string syntax (`{}`, `{:x}`, `{:.3f}` and friends) belongs to the previous article on `std::format`, so we won't repeat it here; all we care about is "how the stitched-together result gets out, how fast it gets out, and whether it fights with the other output paths".

## Get It Running First

`print` looks almost exactly like `format`; the only difference is that it writes the result straight to stdout instead of returning a string:

```cpp
// Standard: C++23
#include <print>
#include <cstdio>

int main()
{
    // Overload (1): writes directly to stdout
    std::print("Hello, {}\n", "world");
    std::print("{2} {1}{0}!\n", 23, "C++", "Hello");   // manual indexing, any order you like

    // println: appends the newline for you, no need to add \n yourself
    std::println("一行带换行: {}", 42);

    // A format string with format-specs (syntax details belong to the format article)
    std::println("十六进制: {:x}  浮点: {:.3f}", 255, 3.14159265);

    // Escaping braces
    std::println("字典字面量: {{key: {}}}", "value");

    // print to stderr (stderr is unbuffered, the line won't sit around)
    std::println(stderr, "这条进 stderr");
    return 0;
}
```

Run it with `g++ -std=c++23 -O2` (local GCC 16.1.1):

```text
这条进 stderr
Hello, world
Hello C++23!
一行带换行: 42
十六进制: ff  浮点: 3.142
字典字面量: {key: value}
```

Notice the first line — the `stderr` content actually shows up first. That is not a bug; it is precisely the core mechanism we are here to discuss: `println(stderr, ...)` writes to unbuffered `stderr` and lands immediately, while the `stdout` that `print` writes to is block-buffered in this setting (when redirected to a pipe or an editor's output pane) and waits for the program to end before being flushed out in one batch. The two streams each run their own buffer, and whoever doesn't buffer lands first. That "each running its own buffer" is exactly the root of the ordering trap coming later.

## Why print Can Beat cout: Skipping Two Layers of Overhead

To understand what motivated `print`'s design, you first have to see why `cout` is slow. A statement like `std::cout << "i=" << i << '\n'` drags two things along on every `<<`:

1. **Synchronization with C stdio** (`sync_with_stdio`, on by default). To guarantee that `std::cout` and `std::printf` come out in a consistent order, libstdc++ has to coordinate with C's `FILE*` buffering on every single `<<` — a very real runtime cost.
2. **Locale awareness**. iostream's formatting (numbers, currency, dates) consults the locale; even if you have never set a locale, that check path is still there.

`std::print` skips both layers. It calls C's `FILE*` writing directly (`stdout` goes through the `fwrite` machinery), does its formatting with `std::format`'s compile-time parsing results, and **never touches iostream at all**. cppreference describes it in one line — "equivalent to `std::print(stdout, fmt, args...)`" — with the bottom layer landing on stream-writing functions such as `vprint_unicode` / `vprint_nonunicode`.

Put differently, of the baggage `cout` carries — "synchronization + locale + one function call per operator" — `print` shoulders none of it. That is the literal meaning of that "decouple from iostream" line in its design goals.

But there is a **counterintuitive** point to puncture first: `print` is **not** "the fastest output method, always". Its value is not being number one in absolute speed; it is "speed close to `printf` without touching sync or locale, while keeping `format`'s type safety". The next section's benchmark will make that clear — don't get carried away by the "print is fast" slogan.

## Measured: print vs cout, print vs printf

Saying "it skips the overhead" is not enough — let's just run a benchmark. We write 2 million short lines to `/dev/null` using `cout` (with sync on and off), `printf`, and `print` respectively (excluding terminal I/O noise so we measure only the formatting and buffering logic itself), timed to the microsecond:

```cpp
// Standard: C++23
#include <cstdio>
#include <iostream>
#include <print>
#include <chrono>

constexpr int kIterations = 2'000'000;

static void report(const char* name, std::chrono::steady_clock::time_point t0,
                   std::chrono::steady_clock::time_point t1)
{
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    std::fprintf(stderr, "%-22s %lld us\n", name, (long long)us);
}

void bench_cout_sync()
{
    std::ios::sync_with_stdio(true);   // the default
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kIterations; ++i) {
        std::cout << "i=" << i << " sq=" << i * 2 << '\n';
    }
    report("cout (sync=true)", t0, std::chrono::steady_clock::now());
}

void bench_cout_nosync()
{
    std::ios::sync_with_stdio(false);  // the usual "speed up cout" move
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kIterations; ++i) {
        std::cout << "i=" << i << " sq=" << i * 2 << '\n';
    }
    report("cout (sync=false)", t0, std::chrono::steady_clock::now());
}

void bench_printf()
{
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kIterations; ++i) {
        std::printf("i=%d sq=%d\n", i, i * 2);
    }
    report("printf", t0, std::chrono::steady_clock::now());
}

void bench_print()
{
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kIterations; ++i) {
        std::print("i={} sq={}\n", i, i * 2);
    }
    report("print", t0, std::chrono::steady_clock::now());
}

int main()
{
    std::freopen("/dev/null", "w", stdout);   // exclude terminal I/O noise
    bench_cout_sync();
    bench_printf();
    bench_print();
    bench_cout_nosync();
    return 0;
}
```

Local GCC 16.1.1, `-std=c++23 -O2`, three runs in a row (absolute microseconds drift with load; we only look at orders of magnitude and relative positions):

```text
cout (sync=true)       180151 us
printf                 128323 us
print                  176287 us
cout (sync=false)      150366 us
cout (sync=true)       179359 us
printf                 133809 us
print                  171167 us
cout (sync=false)      155003 us
cout (sync=true)       191023 us
printf                 133643 us
print                  176044 us
cout (sync=false)      171143 us
```

Want to run it yourself? Open the online demo below (compiled with `-std=c++23`; timings go to stderr):

<OnlineCompilerDemo
  title="cout / printf / std::print Formatting Performance Face-Off"
  source-path="code/examples/vol3/53_print_benchmark.cpp"
  description="2 million short lines written to /dev/null, comparing formatting time across cout (sync on/off), printf, and std::print — print gets type-safe speed in the printf tier without touching sync at all"
  allow-run
  run-options="-O2 -std=c++23"
/>

Let's straighten out the order-of-magnitude conclusions:

- `printf` is the fastest (around 130 ms), but the price is C-style variadics — not type-safe, and a format string that disagrees with its arguments can only blow up at runtime.
- `cout(sync=false)` comes next (around 155–170 ms), but that is what you get **by manually turning synchronization off** — and once it is off, the standard library no longer guarantees its output order relative to `printf`/`print` (the trap in the next section).
- `print` sits steadily at 170–180 ms, reaching that speed **without any sync switch**, and it carries `format`'s type safety.
- `cout(sync=true)` (the default) is the slowest at around 180–190 ms — this is the `cout` performance most people actually get when they change nothing.

So the honest conclusion: `print` is **not the absolute speed champion** (`printf` and a sync-disabled `cout` can match or beat it), but when you "don't want to touch sync yet still want type-safe formatting", it is the best value in that tier. If your code is already full of `cout` and you turned sync off for performance, swapping a few hot-path lines over to `print` won't necessarily buy you anything — `print`'s home turf is new code and situations where you want to shake off iostream entirely.

## The Real Trap: Mixing print and cout Scrambles the Order

Performance is `print`'s selling point, but what trips people up most in day-to-day work is **synchronization**. `print` writes straight into C's `stdout` buffer, while `cout` (in libstdc++) has its own streambuf. The two are coordinated through `sync_with_stdio(true)` by default, so under default settings the order is correct:

```cpp
// Standard: C++23
#include <iostream>
#include <print>

int main()
{
    // sync_with_stdio defaults to true: the order is correct
    std::cout << "第一行(cout)\n";
    std::print("第二行(print)\n");
    std::cout << "第三行(cout)\n";
    std::print("第四行(print)\n");
    return 0;
}
```

```text
第一行(cout)
第二行(print)
第三行(cout)
第四行(print)
```

But plenty of people write `std::ios::sync_with_stdio(false)` for performance. The moment that line goes in, the coordination between `cout` and C stdio is severed, and the two buffers flush on their own — the order scrambles immediately:

```cpp
// Standard: C++23
#include <iostream>
#include <print>

int main()
{
    std::ios::sync_with_stdio(false);   // the usual "speed up cout" move

    std::cout << "第一行(cout)\n";
    std::print("第二行(print)\n");
    std::cout << "第三行(cout)\n";
    std::print("第四行(print)\n");

    std::cout.flush();   // without the flush, cout's leftovers may never show up at all
    return 0;
}
```

Redirect the output to a pipe (block-buffered mode), and it reproduces every time:

```text
第一行(cout)
第三行(cout)
第二行(print)
第四行(print)
```

Both `cout` lines jumped to the front while the two `print` lines got squeezed behind — clearly not the order in the source code. The reason is exactly what we described above: with sync off, `cout`'s streambuf and the C `stdout` buffer that `print` writes to have no knowledge of each other; whoever fills up first or gets flushed first lands first. Here `cout` is flushed in one batch when the program ends, so its content got shoved to the back as a whole block (though its internal order is still correct).

::: warning Once sync is off, stop mixing print(stdout) with cout
`sync_with_stdio(false)` is a "global switch" — once it is off, it changes the relationship between `cout`/`cin` and C stdio for the entire program. If you turned sync off in some performance hotspot and then use both `cout` and `print` elsewhere (`print` goes to stdout by default), the output order is no longer guaranteed. On a terminal (line-buffered) the interleave may happen to look fine; redirect to a file or a pipe (block-buffered) and it reliably falls apart. Debugging this kind of bug is agonizing, because "the order is correct on my machine".
:::

### One Move to Rescue It: print(cout, ...) Takes the Same Buffer

This trap has a very clean fix — provided you know that `print` can write to more than a `FILE*`: C++23 gave it an **`ostream` overload**. Switch `print`'s target from the default `stdout` to `cout` itself, and the output goes through `cout`'s streambuf, sharing the same buffer as `<<`; the order falls right back into place:

```cpp
// Standard: C++23
#include <iostream>
#include <print>

int main()
{
    std::ios::sync_with_stdio(false);
    std::cout << "A(cout)\n";
    std::print(std::cout, "B(print→cout)\n");   // goes through cout's own buffer
    std::cout << "C(cout)\n";
    std::print(std::cout, "D(print→cout)\n");

    std::cout.flush();
    return 0;
}
```

```text
A(cout)
B(print→cout)
C(cout)
D(print→cout)
```

The order is completely correct. Putting the two runs side by side makes it obvious:

```text
print(stdout) interleaved with cout (sync=false):  → A C B D  (scrambled)
print(cout)   sharing one buffer with << (sync=false): → A B C D  (correct)
```

The mechanism in one sentence: `print(FILE*)` and `cout` are two independent buffers — with sync off, each flushes on its own; `print(ostream)` reuses `cout`'s streambuf and lives or dies with `<<`. Hence a simple engineering rule: **the moment your program turns sync off, always use `print(std::cout, ...)` when mixing, never bare `print(...)`**. That way you keep `print`'s formatting convenience without picking a fight with `cout`.

## print's Two Faces: FILE* and ostream

The fix in the previous section works because `print` has two sets of stream-writing overloads. That fact is easy to overlook, and it deserves to be pulled out and made explicit, because it directly determines whether you can "take a different road around the pit".

```cpp
// Standard: C++23
#include <iostream>
#include <sstream>
#include <print>

int main()
{
    // One face: FILE* (stdout / stderr / files you fopen yourself)
    std::print(stdout, "写 stdout: {}\n", 1);
    std::println(stderr, "写 stderr: {}", 2);   // stderr is unbuffered: a good fit for logs and errors

    // The other face: ostream (cout / cerr / stringstream / any ostream)
    std::ostringstream os;
    std::print(os, "写 stringstream: {} = {}\n", "x", 3.14);
    std::println(os, "第二行");
    std::print("{}", os.str());   // dump what was accumulated to stdout in one shot

    // And of course you can feed it cout directly
    std::print(std::cout, "直接 print 到 cout: {}\n", 7);
    return 0;
}
```

```text
写 stderr: 2
写 stdout: 1
写 stringstream: x = 3.14
第二行
直接 print 到 cout: 7
```

The two overload sets correspond to two different underlying paths:

- `print(FILE*, ...)` goes through C's `fwrite` and the C stdio buffer. `stdout` is block-buffered (when redirected) / line-buffered (on a terminal); `stderr` is unbuffered.
- `print(ostream&, ...)` goes through the ostream's `streambuf`, sharing the buffer with `<<`. Write to an `ostringstream` and you accumulate a string in memory; write to `cout` and you share `<<`'s buffer (the rescue from the previous section).

How to choose in practice? Three sentences:

- **Pure new code, performance first**: use `print(...)` / `println(...)` directly; it goes to `stdout` — cleanest and fastest.
- **Mixing with existing `cout` code while sync is off**: use `print(std::cout, ...)` to keep the order guaranteed.
- **Accumulating formatted results in memory** (building log entries or serializing, say): write into an `ostringstream` with `print(oss, ...)` — one fewer intermediate string construction than `oss << std::format(...)`.

## print to stderr and Buffering Semantics

`print` writes to `stdout` by default, but logs and error messages more often head for `stderr`. `print` supports that directly, and with a built-in convenience: `stderr` is **unbuffered**, every write lands immediately — so when you log with `println(stderr, ...)`, you never worry about log lines still sitting unflushed in a buffer when the program crashes.

But note that this "unbuffered" benefit holds only on `stderr`. When `print` writes to `stdout` it is **block-buffered** (when redirected to a file or pipe), and it **does not flush just because it ran into a `\n`** — different semantics from `std::endl` (which does flush). A quick comparison makes it vivid:

```cpp
// Standard: C++23
#include <print>
#include <cstdlib>

int main()
{
    std::print("第一行(带换行)\n");
    std::print("第二行没换行就崩了");
    std::abort();   // simulate a crash
}
```

Run it directly on a terminal, and through a pipe — the results differ:

```text
=== terminal (line-buffered) output ===
[exit=134]                          # nothing came out at all: the buffer wasn't flushed before abort
=== redirected to a pipe (block-buffered) output ===
[done]                              # likewise, nothing
```

In both cases, everything `print` had staged in the buffer was lost to `abort()` (which does not flush user-space buffers — it goes straight to `_exit`) — including that first line with the `\n`. Which goes to show: on `stdout`, `print` **does not flush on newline**; it relies on the program exiting normally for the unified flush. If your program can die abnormally (a crash, `_exit`, killed by a signal) and you need critical logs to land, either write to `stderr` (unbuffered) or call `std::fflush(stdout)` manually at the critical points.

::: warning print does not flush on newline the way endl does
The `endl` in `std::cout << ... << std::endl` flushes as a side effect, so many people assume "printing a newline flushes the buffer". `print` **has no such behavior** — its `stdout` writes are block-buffered, and `\n` is just an ordinary character. When you need a forced flush, `std::flush` the stream yourself, or simply toss the critical output onto unbuffered `stderr`. Bugs of the "the crash ate my logs" kind trace back here nine times out of ten.
:::

## Compiler Support and Feature-Test Macros

`std::print` / `std::println` are C++23 features, living in the header `<print>`. The local GCC 16.1.1 supports them fully — one glance at the feature-test macros says so:

```cpp
// Standard: C++23
#include <print>

int main()
{
#ifdef __cpp_lib_print
    std::println("__cpp_lib_print = {}", __cpp_lib_print);
#endif
#ifdef __cpp_lib_format
    std::println("__cpp_lib_format = {}", __cpp_lib_format);
#endif
    // The no-argument println() overload officially lands in C++26, but
    // cppreference notes "all known implementations make them available in C++23 mode"
    std::print("password");
    std::println();   // prints just a newline
    return 0;
}
```

```text
__cpp_lib_print = 202406
__cpp_lib_format = 202304
password
```

A few points worth flagging:

- `__cpp_lib_print` is `202406` on GCC 16, and that value actually corresponds to a C++23 defect report (DR) that backported "formatted output without intermediate buffering" and support for more formattable types into C++23. So seeing `202406` does not mean you must compile as C++26 — `-std=c++23` already gets you this.
- The no-argument `std::println()` (prints only a newline) was added by the standard in C++26, but the mainstream implementations (GCC/Clang/MSVC) already provide it in C++23 mode; tested locally, `g++ -std=c++23` compiles and runs it fine. For strict portability, write `std::print("\n")` and don't lean on that detail.
- Older GCC (before 13) has no `<print>`. If your code must build on old toolchains, either wrap it in `#ifdef __cpp_lib_print` and fall back to `std::cout << std::format(...)`, or bring in the {fmt} library.

## Unicode Output: The Extra Chore print Does

`print` also takes on a job `printf` wants nothing to do with: Unicode terminal accommodation. Per cppreference, its equivalent implementation splits into two routes — if the ordinary literal encoding is UTF-8, it goes through `vprint_unicode`; otherwise `vprint_nonunicode`. This split is not decorative: the Windows console historically defaulted to a non-UTF-8 code page (GBK/CP437 in the old days), and `print` does the conversion internally so UTF-8 content still displays correctly in a Windows terminal instead of coming out as mojibake. Linux/macOS terminals are natively UTF-8, and that path is essentially a pass-through.

Tried on this machine (where the ordinary literal encoding is UTF-8):

```cpp
// Standard: C++23
#include <print>

int main()
{
    std::println("中文测试: 你好世界 π ≈ 3.14");
    std::println("emoji: 🚀 ✓ ★");
    return 0;
}
```

```text
中文测试: 你好世界 π ≈ 3.14
emoji: 🚀 ✓ ★
```

This matters quite a bit for cross-platform code — to output the same Unicode, `printf` on Windows makes you call `SetConsoleOutputCP(CP_UTF8)` and fiddle around yourself, whereas `print` has that layer wrapped up for you. But note the precondition: **the source file's literals really must be UTF-8-encoded** (only then is the `vprint_unicode` route selected at compile time); if your source file is GBK and you want the Unicode route, keeping the encoding consistent is on you — `print` cannot "turn" non-UTF-8 literals into UTF-8.

## Summary

Let's wrap up the whole `print` story:

- **Positioning**: `print`/`println` are the "direct output" layer C++23 paired with `format` — they skirt iostream's `sync_with_stdio` and locale overhead and write straight to the C streams.
- **Performance**: measured with 2 million short lines to `/dev/null`, `printf` is fastest at about 130 ms, `print` about 175 ms, `cout(sync=false)` about 155–170 ms, and `cout(sync=true)` — the default — slowest at about 185 ms. `print` is not the absolute champion, but in the "no sync games, with type safety" tier it is the best deal.
- **The sync trap**: `print(stdout)` and `cout` are coordinated through `sync_with_stdio(true)` by default, so the order is correct; the moment `sync_with_stdio(false)` runs, the two buffers stop talking to each other and the output order scrambles (reliably reproduced when redirected to a pipe or file). The fix: when mixing, switch to `print(std::cout, ...)` — it reuses `cout`'s streambuf and the order comes back.
- **Two overloads**: `print(FILE*, ...)` rides the C stdio buffer; `print(ostream&, ...)` rides the ostream's streambuf. Logs go to `stderr` (unbuffered); accumulating strings goes to an `ostringstream`.
- **Buffering semantics**: `print` on `stdout` is block-buffered and **does not flush on `\n`** (unlike `endl`); an abnormal exit (abort/_exit/signal) loses whatever is still unflushed in the buffer. Critical logs belong on `stderr`, or get flushed manually.
- **Compiler support**: GCC 16.1.1 `-std=c++23` supports it fully, with `__cpp_lib_print = 202406` (a C++23 DR). The no-argument `println()` is C++26 by the letter of the standard, but the mainstream implementations ship it in C++23 mode. Old toolchains (GCC<13) lack `<print>` — fall back to `cout << format(...)`.
- **Unicode**: with UTF-8 literal encoding it takes the `vprint_unicode` route and converts automatically for non-UTF-8 Windows terminals — cross-platform Unicode output with less heartache than `printf`.

In the next article we switch to another thread of text processing and go deeper into the `format` library — formatter specializations for custom types, formatting ranges/pair/tuple — pushing the `print`/`format` toolkit from "usable" to "customizable".

## References

- [cppreference: std::print (C++23)](https://en.cppreference.com/w/cpp/io/print) — the `FILE*` overloads of `print`, the equivalence with `stdout`/`vprint_unicode`, and the feature-test macro
- [cppreference: std::println (C++23)](https://en.cppreference.com/w/cpp/io/println) — `println`'s various overloads, and how the no-argument version relates to C++26
- [cppreference: std::print(std::ostream) (C++23)](https://en.cppreference.com/w/cpp/io/basic_ostream/print) — the `ostream` overload: reuses the streambuf, shares the buffer with `<<`
- [cppreference: sync_with_stdio](https://en.cppreference.com/w/cpp/io/ios_base/sync_with_stdio) — the semantics and performance impact of the iostream/C stdio synchronization switch
