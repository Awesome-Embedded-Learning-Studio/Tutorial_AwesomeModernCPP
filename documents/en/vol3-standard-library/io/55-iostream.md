---
chapter: 7
cpp_standard:
- 11
- 17
- 20
description: Thoroughly explains the iostream stream hierarchy and `streambuf` buffering,
  the default buffering differences between `cin`/`cout`/`cerr`/`clog`, why `sync_with_stdio`
  and `cin.tie` slow down real-world benchmarks by an order of magnitude, why streams
  are slow (locale lookups, virtual functions, sentries, and synchronization with
  C stdio), and the `failbit`/`badbit`/`eofbit` state machine; and demonstrates the
  speed gap between `cin`, `scanf`, and `from_chars` by benchmarking reading one million
  integers.
difficulty: intermediate
order: 55
platform: host
prerequisites:
- 迭代器适配器：反向、插入与流，把现成迭代器改出新行为
- charconv：零开销的数字与字符串互转
reading_time_minutes: 16
related:
- charconv：零开销的数字与字符串互转
- print：C++23 的直接输出与 iostream 解耦
- format：C++20 的类型安全格式化
- fstream：文件流读写、RAII 与它的可移植性坑
tags:
- host
- cpp-modern
- intermediate
- 基础
title: 'iostream: Stream Abstraction and Why It Is So Slow'
translation:
  source: documents/vol3-standard-library/io/55-iostream.md
  source_hash: 1d2cc3d94d6fcbbbdcb4908dea9978daad288a98d57b914a1fe987ecd12f4202
  translated_at: '2026-06-24T00:43:10.267351+00:00'
  engine: anthropic
  token_count: 3954
---
# iostream: Stream Abstraction and Why It Is So Slow

Most C++ developers have likely heard this piece of "advice": `cin` and `cout` are slow, so when solving algorithmic problems, turn off `sync_with_stdio` first, otherwise you won't pass the test cases with large datasets. While this statement is technically correct, it compresses a topic worth explaining thoroughly into a mere mantra—where exactly does `iostream` slow down, why does disabling synchronization make it fast, and what pitfalls remain after that speedup? In this article, we will dissect the `<iostream>` stream abstraction: first, we clarify its hierarchy and buffering design, then we use real benchmarks to measure that "order of magnitude" gap, and finally, we explain exactly when to use it and when to avoid it.

We will repeatedly return to the same specific task—**reading one million integers from standard input and summing them**. This task is small enough to show the full code, yet substantial enough to expose the overhead of every layer of the stream abstraction. The local environment is GCC 16.1.1, compiled with `g++ -std=c++20 -O2`. The numbers are real results; absolute values may vary by machine, but we only care about the order of magnitude conclusions.

## Clarifying the Hierarchy of Stream Abstraction

Many developers' mental model of `iostream` stops at "`cin` is for input, `cout` is for output." However, once you open the `<iostream>` header, you see a complete inheritance hierarchy. Let's lay it out from bottom to top, because when we discuss "why it is slow" later, every layer contributes a portion of the overhead:

```text
ios_base          ← 所有流的公共基类：格式标志、locale、状态位
  └─ ios          ← 加上 streambuf 指针和错误处理
       ├─ istream ← 输入：operator>>、get、getline
       └─ ostream ← 输出：operator<<、put、write
            └─ iostream ← 多继承自 istream 和 ostream
```

The actual work is done by the `streambuf` pointer held within `ios`. The `istream` / `ostream` classes themselves merely handle "formatting and dispatching"—they translate `>>` / `<<` operations into character read/write requests, and then pass these requests to the underlying `streambuf`. It is `streambuf` that manages buffering and interfaces with the actual I/O channels (terminals, files, or memory blocks). You can visualize this relationship as follows:

```text
你的代码  ──>>/<<──►  istream/ostream(格式化 + sentry + locale)
                          │
                          ▼  把字符请求委托下去
                      streambuf(缓冲、实际读写)
                          │
                          ▼
                    真正的 I/O 通道(stdin / 文件 / string)
```

This chain is the source of `iostream`'s abstraction power—the same `<<` / `>>` code can seamlessly switch between the screen, files, and memory simply by swapping the `streambuf`. However, this is also one of the reasons it is "slow": **every `<<` must traverse the entire dispatch chain**. We will see just how expensive this chain is in our benchmarks later on.

The `<iostream>` header provides us with four predefined standard stream objects, corresponding to `stdin` / `stdout` / `stderr`:

- `std::cin` — bound to `stdin`, an `istream`;
- `std::cout` — bound to `stdout`, an `ostream`, **buffered**;
- `std::cerr` — bound to `stderr`, an `ostream`, **unbuffered**, flushing immediately on every `<<`;
- `std::clog` — also bound to `stderr`, but **buffered**, accumulating writes just like `cout`.

The fact that `cerr` is unbuffered is critical. Let's verify this directly. The code below deliberately inserts a `cerr` output and a `sleep` between two `cout` outputs to observe how the buffering behavior manifests:

```cpp
// Standard: C++20
#include <chrono>
#include <iostream>
#include <thread>

int main() {
    std::cout << "[cout] 这一串会先在 cout 的缓冲里待着";
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    // cerr 不缓冲：哪怕 cout 还没 flush，cerr 立刻出去
    std::cerr << "[cerr] 我不缓冲，立刻打到 stderr\n";
    std::cout << " (cout 这一段补完才一起 flush)\n";
    return 0;
}
```

When we merge stdout and stderr into the same terminal, the output order looks like this:

```text
[cout] 这一串会先在 cout 的缓冲里待着[cerr] 我不缓冲，立刻打到 stderr
 (cout 这一段补完才一起 flush)
```

Notice the first line—the `[cout]` sequence should have appeared first, yet it is squeezed onto the same line as `[cerr]`; meanwhile, the `cerr` message appears on screen **before** the second half of the `cout` output. This is living proof that "`cerr` is unbuffered, `cout` is buffered": `cout` held `"这一串..."` in its buffer, while `cerr` immediately pierced through to `stderr`. Finally, when the program exited, `cout` flushed everything, including `(cout 这一段...)`. This is why diagnostic messages default to `cerr`—**even if the program crashes on the next line, the error message has already been flushed out**, so it won't be stuck in `cout`'s buffer and go down with the ship.

## `sync_with_stdio` and `cin.tie`: Two Switches That Slow Down Real-World I/O

Now that we've clarified the hierarchy, let's get straight to the most practical part of this article. By default, `iostream` enables two mechanisms that "prioritize safety over speed," and that common competitive programming tip to "turn off `sync_with_stdio`" is referring to these two.

The first is `std::ios_base::sync_with_stdio`, which defaults to `true`. It forces `cin` / `cout` / `cerr` to **synchronize** with the C standard library's `stdin` / `stdout` / `stderr`—ensuring that if you mix `std::cin` with `scanf`, or `std::cout` with `printf`, the order of reads and writes remains consistent as if you were using only one set of streams. The cost of this guarantee is that the standard library implementation must make `cin` / `cout` share the same buffers and positions as C's `FILE*`. The most common implementation effectively **degrades `cin` / `cout` to reading character-by-character through C stdio**. Once you go character-by-character, buffering is effectively wasted.

The second is `std::cin.tie(&std::cout)`, which binds `cin` to `cout` by default. The semantics of this binding are: **before every read from `cin`, flush the bound `cout` first**. This is another safeguard for interactive program correctness—a typical scenario is `cout << "Enter x: "` to show a prompt, followed by `cin >> x` to read input. With the tie, we don't have to worry that the prompt is still stuck in the buffer while the user is already blocked waiting for input. The cost is: **every read operation incurs an extra, gratuitous flush of `cout`**. When doing heavy I/O, this is pure overhead.

How much do these two switches together impact "reading heavily from `cin`"? Let's measure it directly using the task mentioned at the beginning. The following small program reads one million `int`s from standard input and sums them up. If `argv[1]` is `0`, it takes the default path; if it is `1`, it turns off both switches:

```cpp
// Standard: C++20
#include <chrono>
#include <cstdio>
#include <iostream>

int main(int argc, char** argv) {
    const bool fast = (argc > 1 && argv[1][0] == '1');
    if (fast) {
        std::ios_base::sync_with_stdio(false);
        std::cin.tie(nullptr);
    }
    auto t0 = std::chrono::high_resolution_clock::now();
    long acc = 0;
    int x;
    while (std::cin >> x) acc += x;
    auto t1 = std::chrono::high_resolution_clock::now();
    std::fprintf(stderr, "mode=%s  time=%.1f ms  sum=%ld\n",
                 fast ? "fast(sync off)" : "default(sync on)",
                 std::chrono::duration<double, std::milli>(t1 - t0).count(), acc);
    return 0;
}
```

We fed it the same 7.5 MiB data file containing one million integers, running it three times:

```text
=== default cin (sync on, tied) ===
mode=default(sync on)  time=176.6 ms  sum=3499993500000
mode=default(sync on)  time=177.8 ms  sum=3499993500000
mode=default(sync on)  time=176.8 ms  sum=3499993500000
=== fast cin (sync off + untie) ===
mode=fast(sync off)  time=41.4 ms  sum=3499993500000
mode=fast(sync off)  time=39.8 ms  sum=3499993500000
mode=fast(sync off)  time=39.8 ms  sum=3499993500000
```

**A drop from 177 ms down to 40 ms—over a 4x speedup**—that is the empirical evidence behind that statement. The two curves align perfectly: the default runs consistently hit 176–178 ms, while the fast runs consistently hit 39–42 ms. The conclusion is rock solid.

Even more interesting is the number 40 ms itself. Remember the dispatch chain diagram from earlier? By default, `cin` is forced to synchronize with C stdio, compelling it to traverse `FILE*` almost character-by-character, which is why it is slow. Once synchronization is disabled, `cin`'s own `streambuf` layer can finally cut loose and use its own buffer for bulk reading, allowing its speed to catch up immediately—matching, or even slightly beating, the `scanf` we will test shortly. In other words, **disabling `sync_with_stdio` isn't magic; it simply untangles the dispatch chain that was being bogged down by synchronization**.

### Two Pitfalls Left Behind After Disabling Synchronization

The speedup is real, but making this cut severs two connections that can trip you up if you aren't careful. Let's examine them one by one.

::: warning Don't Mix cin/cout with scanf/printf
After disabling `sync_with_stdio`, `cin` / `cout` use their own buffers, while `scanf` / `printf` use C's `FILE*` buffers. These two buffering mechanisms **are unaware of each other**, so the order of output is no longer guaranteed. In the code below, the source order is `printf 1`, `cout 2`, `printf 3`, `cout 4`:

```cpp
// Standard: C++20
#include <cstdio>
#include <iostream>

int main(int argc, char** argv) {
    if (argc > 1) std::ios_base::sync_with_stdio(false);  // 传参 = 关同步
    std::printf("[printf] 1\n");
    std::cout << "[cout]   2\n";
    std::printf("[printf] 3\n");
    std::cout << "[cout]   4\n";
    return 0;
}
```

Running with synchronous mode (default), the four lines are printed strictly in source code order:

```text
[printf] 1
[cout]   2
[printf] 3
[cout]   4
```

Running with synchronization disabled (consistent results across multiple runs), the order is completely scrambled—the two sets of buffers accumulate and flush independently:

```text
[cout]   2
[cout]   4
[printf] 1
[printf] 3
```

The pattern is straightforward: the two `cout` lines are grouped together by its own buffer, and the two `printf` lines are grouped together by the C buffer. Whichever buffer fills up or is flushed first goes out first. Therefore, the golden rule is—**after disabling `sync_with_stdio`, use either `cin`/`cout` exclusively or `scanf`/`printf` exclusively throughout the program. Do not mix them**. If you really need to mix them and are concerned about ordering, C++23's `std::print(std::cout, ...)` offers a clean solution (see Chapter [53-print](../strings/53-print.md) in this volume).
:::

::: warning Interactive prompts require manual flushing after untying
`cin.tie(nullptr)` disables the "automatic flush of `cout` before reading." In batch processing scenarios, this is a pure gain—there are no prompts to print, so flushing before every read is a waste. However, if you are writing an interactive program and habitually write code like this:

```cpp
std::cout << "Enter x: ";   // 提示没换行，也不手 flush
std::cin >> x;
```

By default, `tie` causes `cin >> x` to flush `cout` first, ensuring the user sees `Enter x:` before typing. However, if you disable this with `cin.tie(nullptr)` to "speed things up," that automatic flush disappears. The prompt might get stuck in the `cout` buffer, leaving the user staring at a blank screen waiting for input, which ruins the user experience. The conclusion: **whether to disable `tie` depends on whether you actually have a `cout` prompt that needs flushing before reading**. Disable it for pure data throughput, but keep it for interactive sessions.
:::

## Seeing the Full Picture: Why iostream is Actually Slow

So far, we have focused on `sync` and `tie`, but even with both of these switches turned off, `cin` and `cout` are still slower than raw `from_chars`. Let's point out **every expensive step** in this dispatch chain so you understand why `iostream` can't be fast, even when "optimized":

**Locale lookups.** `>>` and `<<` default to formatting based on the current locale—for example, thousands separators in integers, decimal points in floating-point numbers, and the text representation of `bool` values (`true` / `false`) are all locale-dependent. Even if you don't configure anything, it still has to check the C locale every time. We compared this in detail in the [51-charconv](../strings/51-charconv.md) chapter; `charconv` can be several times faster by cutting out locale, and that overhead is hidden right here.

**Virtual function dispatch.** `istream` and `ostream` implement `>>` and `<<` as calls to `streambuf` virtual functions (like `sputc`, `sbumpc`, `xsputn`, etc.). Since `streambuf` is an abstract class, the specific implementation is determined at runtime. Compilers struggle to inline this entire chain away, so every `<<` carries the cost of an indirect call.

**The sentry object.** This is a layer many people don't know about. The standard requires that for every call to `>>` or `<<`, a `sentry` object is constructed first. It checks the stream state, locks the `streambuf` (ensuring a single `<<` is atomic in multi-threaded environments), and performs preparation, then wraps things up upon destruction. This means **every `<< x` you see corresponds to a sentry construction and destruction underneath**. Once or twice doesn't matter, but in a loop of a million iterations, this is real overhead. This is also why "combining multiple `<<` into one call" (e.g., using `std::format` to build a string and then outputting it with a single `<<`) is faster than "writing ten `<<` statements in a row"—fewer sentry constructions.

**Synchronization with C stdio.** As discussed in the `sync_with_stdio` section, this is enabled by default and forces standard streams to process character-by-character through C `FILE*`, representing the single largest performance gap.

**Format parsing.** `>>` and `<<` aren't just moving bytes; they have to perform a full suite of parsing tasks: "skip leading whitespace, identify signs, truncate by width, and assemble integers." `<<` does the reverse, formatting integers into characters. This is necessary work, but `iostream` bundles it with the locale lookups, virtual functions, and sentry objects mentioned above, running the full gamut for every single number read.

When you add all this up, the slowness of `iostream` is no mystery—**it's not that one specific point is slow, but rather that every layer adds a bit of overhead**. The benefits are tangible: type safety (the compiler knows at compile-time that you are `<<`-ing an `int`, avoiding the undefined behavior you get with `printf` type mismatches), automatic extensibility (overload `operator<<` for custom types to feed them into any `ostream`), and seamless integration with the exception and RAII systems. This is why it won't be—and shouldn't be—"optimized away"—it pays the price of abstraction, and the bill for abstraction always comes due.

## Putting the Three Approaches Together: cin vs scanf vs from_chars

Now we arrive at the most important question: for a job like "reading one million integers," which method should we use? Let's run all three paths on the same dataset: default `cin`, `cin` with synchronization disabled, C's `scanf`, and `fread` to slurp the entire file into memory followed by `from_chars` for parsing. The latter is the most "brutal" fast path—bypassing all stream abstractions to read bytes and parse directly.

The core code for the `scanf` and `fread + from_chars` paths looks like this:

```cpp
// Standard: C++20
// 路径 A：scanf，直接走 FILE* 缓冲
long acc = 0;
int x;
while (std::scanf("%d", &x) == 1) acc += x;

// 路径 B：fread 把 stdin 整块读进内存，再 from_chars 逐个解析
std::vector<char> buf;
{ char chunk[1 << 16]; size_t n;
  while ((n = std::fread(chunk, 1, sizeof(chunk), stdin)) > 0)
      buf.insert(buf.end(), chunk, chunk + n); }
const char* first = buf.data();
const char* last  = buf.data() + buf.size();
long acc2 = 0;
while (first < last) {
    while (first < last && (*first == ' ' || *first == '\n')) ++first;  // from_chars 不跳前导空白，自己跳
    if (first >= last) break;
    int y;
    auto r = std::from_chars(first, last, y);
    if (r.ec != std::errc{}) break;
    acc2 += y;
    first = r.ptr;
}
```

All four paths process the same data file containing one million integers. The time taken represents the minimum value across multiple runs (absolute values vary by machine, so we focus on the order of magnitude):

```text
cin   (sync on,  默认)     ~177 ms
scanf                      ~59 ms
cin   (sync off + untie)   ~40 ms
fread + from_chars         ~18 ms
```

Putting these numbers together, the conclusion is clear:

- **Default `cin` is the slowest of the four**—because it synchronizes with C stdio, processes characters via `FILE*`, and is even slower than `scanf`.
- **`scanf` is approximately 59 ms**, three times faster than default `cin`. It uses C's `FILE*` buffering directly, avoiding the `iostream` dispatch chain and the overhead of sentry objects.
- **`cin` with synchronization disabled is approximately 40 ms**, slightly beating `scanf`. This shows that the `iostream` dispatch chain itself is **not slower than C stdio**—once the "synchronization" shackle is removed, its own `streambuf` buffering is equally efficient.
- **`fread + from_chars` is approximately 18 ms**, more than doubling the speed again. This path minimizes overhead for both buffering (`fread` reads a large chunk at once) and parsing (`from_chars` has no locale, no exceptions, and no allocation), making it the correct destination for performance-sensitive scenarios. For a detailed breakdown of why `from_chars` is so fast, see [51-charconv](../strings/51-charconv.md).

::: warning A Misleading Comparison
Some might compare "`std::stringstream >>` on in-memory strings" with "`sscanf` on in-memory strings" to conclude that `iostream` is faster or slower than `scanf`. Be careful here: **`sscanf` performs extremely poorly on memory strings** (local tests show it can slow down to tens of seconds), because some implementations re-scan the remaining buffer, which is completely different from its behavior when using `FILE*`. Therefore, please treat "reading from standard input" as the fair battleground—that is, the table above—and don't use in-memory `sscanf` as a representative, as that leads to misleading conclusions.
:::

To wrap it up in one sentence: **`sync_with_stdio(false) + cin.tie(nullptr)` allows `cin` / `cout` to match the `scanf` / `printf` tier; but to truly squeeze out performance, the fast path is `from_chars` (for input) and `std::print` / `std::format_to` (for output)—the overhead of the `iostream` layer is always there**.

## Stream State Machine: failbit / badbit / eofbit

Having discussed performance, let's thoroughly explain another mechanism in `iostream` that often trips people up—its error states. Internally, every stream has three state bits:

- `goodbit` (actually 0) — everything is normal;
- `failbit` — the last operation **failed due to format reasons** (e.g., trying to read an `int` but encountering `"hello"`); the stream itself is not broken, and clearing the state allows it to continue;
- `badbit` — the stream has **a real problem** (underlying I/O error, buffer corruption, etc.); this is usually unrecoverable;
- `eofbit` — reached the end of file.

The most critical thing to understand is: **once `failbit` or `badbit` is set, all subsequent `>>` / `<<` operations become no-ops**—the stream refuses to work until you `clear()` the state. Let's run through this state machine with a code snippet, reading `int`, `int`, `int` from a string stream, but with a `"hello"` sandwiched in the middle:

```cpp
// Standard: C++20
#include <iostream>
#include <sstream>
#include <string>

int main() {
    std::istringstream iss("42  hello  99");
    int x;

    iss >> x;   // 正常读到 42
    std::cout << "读到 " << x
              << "  good=" << iss.good() << " fail=" << iss.fail()
              << " eof=" << iss.eof() << " bool(iss)=" << static_cast<bool>(iss) << '\n';

    iss >> x;   // 想读 int，却碰到 hello —— failbit 置位，x 不变
    std::cout << "格式不匹配后: good=" << iss.good()
              << " fail=" << iss.fail()
              << " bool(iss)=" << static_cast<bool>(iss) << '\n';

    int y = -999;
    iss >> y;   // 流处于 fail 状态，这次 >> 是空操作，y 不变
    std::cout << "y 还是 " << y << "，因为流在 fail 状态下 >> 被忽略\n";

    iss.clear();   // 清掉 failbit，"hello" 仍在缓冲里等着
    std::string s;
    iss >> s;      // 用 string 把 "hello" 消化掉
    iss >> x;      // 继续读到 99
    std::cout << "clear() 之后: s=" << s << " x=" << x << '\n';

    // 读到末尾再读：eofbit 和 failbit 一起置位
    iss >> x;
    std::cout << "读到末尾后: eof=" << iss.eof()
              << " fail=" << iss.fail() << '\n';
    return 0;
}
```

The observed state changes:

```text
读到 42  good=1 fail=0 eof=0 bool(iss)=1
格式不匹配后: good=0 fail=1 bool(iss)=0
y 还是 -999，因为流在 fail 状态下 >> 被忽略
clear() 之后: s=hello x=99
读到末尾后: eof=1 fail=1
```

This state machine has several practical key points:

**`operator bool` (and `operator!`) is the unified entry point for checking if a stream is usable.** The standard library provides an implicit conversion from a stream to `bool`, which is equivalent to `!fail()`—meaning it evaluates to `true` as long as neither `failbit` nor `badbit` is set. This is the foundation of the idiom often seen in loops:

```cpp
while (iss >> x) sum += x;   // >> 返回流本身，流再转 bool
```

`>> x` returns an `istream&` (the stream itself), which is implicitly converted to `bool`: the loop continues if valid data is read, and exits if it hits the end of the file (`eofbit` is set alongside `failbit`) or encounters a format error. This pattern is much cleaner and safer than "read first with `>>`, then check `eof()`" — **checking `eof()` alone is a classic pitfall**. It is only set after "reading past the end," meaning the last read data might be incomplete.

::: warning clear() leaves "bad characters" in the buffer
`clear()` only resets the state flags; **it does not remove the character in the buffer that caused the failure**. So, in the example above, after `clear()`, `"hello"` remains stuck at the stream's read position, and the next `>> int` will fail immediately. The solution is to either consume it with a `std::string` as shown, or use `iss.ignore(...)` to skip a section. Many people find they "still can't read" after `clear()`, and this is almost always the reason.
:::

**Distinguish clearly between `badbit` and `failbit`.** `failbit` means "this read failed to produce an `int`, but you can recover by clearing the state"; `badbit` means "the stream is broken, give up." When encountering bad data during interactive parsing, the correct routine is usually: `clear()` + `ignore()` to skip the bad field and continue reading. Low-level errors like terminal or pipe disconnection trigger `badbit`, in which case you should usually exit directly.

## When to use iostream, and when not to use it

After listing so many criticisms of iostream, let's be fair. It's not a tool that should be eliminated, but rather one that should be used in the right scenarios.

**Scenarios where you SHOULD use `iostream`:**

- **Simple interaction, command-line utilities.** A few lines of `cout << "..." << x` paired with `cin >> x` offer type safety and readability. Overloading `<<` for custom types allows direct printing. In these cases, development efficiency is far more important than I/O overhead.
- **Debug logging.** Especially using `std::cerr` / `std::clog` — error and diagnostic information need to be "flushed immediately" and "not swallowed by buffering," which is exactly the design intent of `cerr`'s unbuffered nature. Performance is not the primary concern here.
- **Places needing type safety without the risks of `printf`'s undefined behavior.** If the type of `x` doesn't match in `printf("%d", x)`, it is undefined behavior, and the compiler might not warn you. With `std::cout << x`, a type mismatch results in a direct compilation error.

**Scenarios where you SHOULD NOT use `iostream`:**

- **Performance-sensitive reading/writing of large amounts of numbers.** Protocol parsing, serialization, CSV / JSON parsing, or competitive programming problems with large datasets. The correct destination for this path is `from_chars` / `to_chars` ([51-charconv](../strings/51-charconv.md)). A difference of tens of times is not a trivial optimization.
- **Output requiring both type safety and the expressiveness of format strings.** Since C++20, there is a better answer for this — `std::format` ([52-format](../strings/52-format.md)) and C++23's `std::print` / `std::println` ([53-print](../strings/53-print.md)). `print` writes directly to the stream, bypassing the `<<` dispatch chain. Volume 53 tested its order-of-magnitude advantage over `cout`.
- **Binary, random-access, or mmap for large file I/O.** This is the job of file streams, covered in [56-fstream](56-fstream.md). This article focuses on standard streams, but it's worth mentioning: `fstream` is also not a performance tool for large random access file operations; for real speed, switch to `mmap` or C's `stdio`.

A key decision principle: **iostream is the "safe and convenient" default, not the "fast" default.** Once you start writing workarounds for its speed (turning off sync, untying, using `<< '\n'` instead of `endl`), it usually means you should switch tools rather than continue squeezing performance out of this abstraction layer.

## Summary

Let's wrap up the key conclusions from our tour of `<iostream>`:

- **Hierarchy**: `ios_base` → `ios` → `istream` / `ostream` → `iostream`; the real work and buffering is done by the attached `streambuf`, while `<<` / `>>` only handle formatting and dispatch requests.
- **Four standard streams**: `cout` / `clog` are buffered, `cerr` is unbuffered (flushes immediately on every `<<`) — so error diagnostics go to `cerr` by default to avoid getting stuck in a buffer.
- **Two performance switches**: `sync_with_stdio(false)` breaks synchronization with C stdio (default drags `cin` to walk `FILE*` character by character), `cin.tie(nullptr)` saves the `cout` flush before every read. Measured reading one million integers, dropping from 177 ms to 40 ms, **approximately a 4x speedup**.
- **Cost of turning off sync**: Don't mix `cin` / `cout` with `scanf` / `printf` anymore (order will be chaotic, tested `printf 1 cout 2` outputting `cout 2 / cout 4 / printf 1 / printf 3`); interactive prompts need manual flushing.
- **Why it's slow**: Locale lookup + virtual function dispatch + sentry construction on every `<<` + synchronization with C stdio + format parsing. Each layer adds a bit; the cost lies in the abstraction, not a single point.
- **Horizontal comparison (reading 1 million int, local GCC 16.1.1)**: `cin` default ~177 ms, `scanf` ~59 ms, `cin` sync off ~40 ms, `fread + from_chars` ~18 ms. `cin` with sync off ≈ `scanf`, but `from_chars` is more than twice as fast.
- **State machine**: `goodbit` / `failbit` / `badbit` / `eofbit`; once `fail` or `bad` is set, subsequent `>>` / `<<` are no-ops. You must `clear()` to recover, but `clear()` doesn't remove the bad character in the buffer (must `ignore` or read it away).
- **Selection**: Simple interaction, debug logs, small tools prioritizing type safety — use `iostream`; large number reading/writing — `charconv`; type-safe output needing format string expressiveness — `format` / `print`; binary large files — `fstream` / `mmap`.

In the next article, we will dive into file streams — the three types of file streams in `fstream`, `open` modes, the RAII automatic `close` lifecycle pitfalls, and why you should also switch tools for large file reading and writing.

## References

- [cppreference: iostream](https://en.cppreference.com/w/cpp/header/iostream) — Overview of standard stream objects `cin` / `cout` / `cerr` / `clog` and header files
- [cppreference: std::ios_base::sync_with_stdio](https://en.cppreference.com/w/cpp/io/ios_base/sync_with_stdio) — Semantics of the synchronization switch and "order not guaranteed after turning off"
- [cppreference: std::basic_streambuf](https://en.cppreference.com/w/cpp/io/basic_streambuf) — Low-level buffering abstraction
- [cppreference: std::basic_istream::sentry](https://en.cppreference.com/w/cpp/io/basic_istream/sentry) — The sentry object constructed on every `>>`
- [cppreference: std::basic_ios](https://en.cppreference.com/w/cpp/io/basic_ios) — `fail` / `bad` / `eof` / `clear` / `operator bool` state machine
