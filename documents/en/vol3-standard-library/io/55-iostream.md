---
chapter: 7
cpp_standard:
- 11
- 17
- 20
description: A thorough walkthrough of the iostream class hierarchy and streambuf buffering,
  the default buffering differences among cin/cout/cerr/clog, why sync_with_stdio and
  cin.tie drag real benchmarks down by an order of magnitude, why streams are slow at
  all (locale lookups, virtual dispatch, sentries, synchronization with C stdio), plus
  the failbit/badbit/eofbit state machine — benchmarked by reading one million ints
  to chart the speed gap between cin, scanf, and from_chars
difficulty: intermediate
order: 55
platform: host
prerequisites:
- 'Iterator Adapters: Reverse, Insertion, and Stream — Teaching Old Iterators New
  Tricks'
- 'charconv: Zero-Overhead Number-String Conversions'
reading_time_minutes: 16
related:
- 'charconv: Zero-Overhead Number-String Conversions'
- 'print: Direct Output in C++23 and Decoupling from iostream'
- 'format: Type-Safe Formatting in C++20'
- 'fstream: File Stream I/O, RAII, and Its Portability Pitfalls'
tags:
- host
- cpp-modern
- intermediate
- 基础
title: 'iostream: Stream Abstraction and Why It''s So Slow'
translation:
  source: documents/vol3-standard-library/io/55-iostream.md
  source_hash: 6d928b858c8b3e124c02c2932a450c2d80217b59a7ea1e10dd69237a2b547c50
  translated_at: '2026-09-26T00:41:29+00:00'
  engine: anthropic
  token_count: 5700
---
# iostream: Stream Abstraction and Why It's So Slow

Anyone who writes C++ has probably heard the "advice": `cin` / `cout` are slow; turn off `sync_with_stdio` before grinding competitive-programming problems, or the big test cases won't pass. The advice itself isn't wrong — but it compresses something genuinely worth understanding into a mnemonic chant: where exactly is `iostream` slow, why does turning off sync make it fast, and what traps are left behind once it's fast. In this article we take `<iostream>`'s stream abstraction apart and run it through its paces: first get a clear view of its layering and buffering design, then use a real benchmark to measure that "order of magnitude" gap, and finally nail down which scenarios should use it and which should route around it.

We'll keep coming back to one concrete task: **read one million integers from standard input and sum them**. It's small enough to paste in full, yet heavy enough to expose the overhead of every layer of the stream abstraction. Local machine, GCC 16.1.1, `g++ -std=c++20 -O2`; every number below was actually measured. Absolute values will drift from machine to machine — we only care about the order-of-magnitude conclusions.

## First, Get the Stream Abstraction's Layers Straight

Many people's mental model of `iostream` stops at "`cin` is input, `cout` is output". But the moment you open the `<iostream>` header, you're looking at a whole inheritance hierarchy. Let's lay it out from bottom to top, because when we later ask "why is it slow", every layer contributes a share of the overhead:

```text
ios_base          ← common base of all streams: format flags, locale, state bits
  └─ ios          ← adds the streambuf pointer and error handling
       ├─ istream ← input: operator>>, get, getline
       └─ ostream ← output: operator<<, put, write
            └─ iostream ← multiply inherits from istream and ostream
```

The one actually doing the work is the `streambuf` pointer hanging off `ios`. `istream` / `ostream` themselves only do "formatting and dispatch" — they translate `>>` / `<<` into read/write requests for characters, then hand those requests down to the underlying `streambuf`. `streambuf` is the layer that manages buffering and interfaces with the real I/O channel (terminal, file, memory block). You can picture the relationship like this:

```text
your code  ──>>/<<──►  istream/ostream(formatting + sentry + locale)
                          │
                          ▼  delegates character requests downward
                      streambuf(buffering, actual reads/writes)
                          │
                          ▼
                    real I/O channel(stdin / file / string)
```

This chain is where `iostream`'s abstraction power comes from — the same `<<` / `>>` code switches seamlessly between screen, file, and memory just by swapping the `streambuf`. But it's also one of the roots of its "slowness": **every single `<<` walks the entire dispatch chain**. Our measurements later will show just how expensive that chain is.

The `<iostream>` header predefines four standard stream objects for us, corresponding to `stdin` / `stdout` / `stderr`:

- `std::cin` — tied to `stdin`, an `istream`;
- `std::cout` — tied to `stdout`, an `ostream`, **buffered**;
- `std::cerr` — tied to `stderr`, an `ostream`, **unbuffered** — every `<<` goes out immediately;
- `std::clog` — also tied to `stderr`, but **buffered**, accumulating writes like `cout` does.

The fact that `cerr` is unbuffered matters a lot, so let's verify it hands-on. The code below deliberately wedges a `cerr` output and a `sleep` between two `cout` outputs, to see exactly how the buffering behavior shows up:

```cpp
// Standard: C++20
#include <chrono>
#include <iostream>
#include <thread>

int main() {
    std::cout << "[cout] 这一串会先在 cout 的缓冲里待着";
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    // cerr is unbuffered: even though cout hasn't flushed, cerr goes out immediately
    std::cerr << "[cerr] 我不缓冲，立刻打到 stderr\n";
    std::cout << " (cout 这一段补完才一起 flush)\n";
    return 0;
}
```

With stdout and stderr merged into the same terminal, the output order looks like this:

```text
[cout] 这一串会先在 cout 的缓冲里待着[cerr] 我不缓冲，立刻打到 stderr
 (cout 这一段补完才一起 flush)
```

Notice the first line — the `[cout]` string should have happened first, yet it ended up squeezed onto the same line as `[cerr]`; and the `cerr` message hit the screen **before** the second half of the `cout` output. This is living proof of "cerr unbuffered, cout buffered": `cout` kept its first string parked in its buffer, while the `cerr` message pierced straight through to `stderr` immediately; only at program exit did `cout` finally flush, bringing its trailing segment along. So there's a solid reason error diagnostics default to `cerr` — **even if the program crashes on the very next line, the error message is already out**, not left trapped in `cout`'s buffer to die with it.

## sync_with_stdio and cin.tie: Two Switches That Slow Down Real I/O

With the layering straight, let's jump straight into the most hands-on part of this article. `iostream` ships with two "slowed down for safety" mechanisms enabled by default — and the competitive-programming chant "turn off `sync_with_stdio` first" is turning off exactly these two.

The first is `std::ios_base::sync_with_stdio`, `true` by default. It keeps `cin` / `cout` / `cerr` **synchronized** with the C standard library's `stdin` / `stdout` / `stderr` — guaranteeing that if you mix `std::cin` with `scanf`, or `std::cout` with `printf`, the order of reads and writes stays consistent with "using only one side". The price of that guarantee: the standard library implementation must let `cin` / `cout` share the same buffering and position state with C's `FILE*`, and the most common way to do that is to **degrade `cin` / `cout` into going through C stdio essentially character by character**. Once you're per-character, half the point of buffering is gone.

The second is `std::cin.tie(&std::cout)` — by default, `cin` is tied to `cout`. The semantics of tying: **before every read from `cin`, the `cout` it's tied to gets flushed first**. Again this exists for interactive-program correctness — the classic scene is printing a prompt with `cout << "Enter x: "` and then reading with `cin >> x`; being tied means you never face the prompt still stuck in the buffer while the user is already blocked at the input. The cost: **every read operation comes with a free extra flush of `cout`**, and under heavy reading that's pure waste.

How much do these two switches combined hurt "reading a lot from `cin`"? Let's measure it directly with the task from the beginning. The little program below reads one million `int`s from standard input and sums them; `argv[1]` of `0` takes the default path, `1` turns both switches off:

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

Feeding it the same 7.5 MiB data file of one million integers, three runs in a row:

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

**From 177 ms down to 40 ms, more than a 4x speedup** — that's the entire empirical content of that one-line advice. The two groups line up cleanly: the three default runs all land at 176–178 ms, the three fast runs at 39–42 ms. A rock-solid conclusion.

What's more interesting is the number 40 ms itself. Remember the dispatch-chain diagram from earlier? In the default state, `cin` — because it must stay synchronized with C stdio — is forced to track the `FILE*` position essentially character by character, hence the slowness. The moment sync is off, `cin`'s own `streambuf` layer can finally stretch out and read in bulk through its own buffer, and the speed catches up immediately — on par with the `scanf` we're about to measure, even slightly ahead. In other words, **turning off `sync_with_stdio` works no magic; it merely unshackles the dispatch chain that synchronization was dragging down**.

### The Two Traps Left Behind After Turning Sync Off

The speedup is real, but the same cut also severs two things, and you'll step on them if you're not watching. Let's take them one by one.

::: warning Stop mixing cin/cout with scanf/printf
After `sync_with_stdio` is turned off, `cin` / `cout` use their own buffers while `scanf` / `printf` use C's `FILE*` buffers; the two buffer sets **don't know the other exists**, and the ordering of output is no longer guaranteed. In the code below, the source order is `printf 1`, `cout 2`, `printf 3`, `cout 4`:

```cpp
// Standard: C++20
#include <cstdio>
#include <iostream>

int main(int argc, char** argv) {
    if (argc > 1) std::ios_base::sync_with_stdio(false);  // passing an argument = turn sync off
    std::printf("[printf] 1\n");
    std::cout << "[cout]   2\n";
    std::printf("[printf] 3\n");
    std::cout << "[cout]   4\n";
    return 0;
}
```

With sync on (the default), the four lines come out dutifully in source order:

```text
[printf] 1
[cout]   2
[printf] 3
[cout]   4
```

Turn sync off and run again (multiple runs, same result every time), and the order is completely scrambled — each buffer set accumulates and flushes on its own:

```text
[cout]   2
[cout]   4
[printf] 1
[printf] 3
```

The pattern is plain: `cout`'s two lines get batched together by its own buffer, `printf`'s two lines get batched together by the C buffer, and whichever buffer fills first / gets flushed first goes out first. Hence the iron rule — **after turning off `sync_with_stdio`, the whole program uses either `cin` / `cout` exclusively or `scanf` / `printf` exclusively; never mix**. If you genuinely need to mix and fear the scrambling, C++23's `std::print(std::cout, ...)` is a clean way out (see [53-print](../strings/53-print.md) in this volume).
:::

::: warning After untying cin, interactive prompts must flush on their own
What `cin.tie(nullptr)` removes is "automatically flushing `cout` before each read". In batch-processing scenarios that's pure profit — there's no prompt to print, and flushing before every read is nothing but waste. But if you're writing an interactive program and habitually do this:

```cpp
std::cout << "Enter x: ";   // prompt has no newline, and no manual flush
std::cin >> x;
```

Under the default `tie`, `cin >> x` flushes `cout` first, so the user sees `Enter x:` before typing. But the moment you casually add `cin.tie(nullptr)` "for speed", that automatic flush is gone: the prompt may sit in `cout`'s buffer and stubbornly refuse to appear, and the user stares at a blank screen waiting to type — an awful experience. Conclusion: **whether to untie `cin` depends on whether you really have a `cout` prompt that needs flushing before reads**. Pure data throughput — untie; interactive — keep the tie.
:::

## The Full Picture: Why iostream Is Slow in the First Place

So far we've been leaning on `sync` / `tie` as the whole story, yet even with both switches off, `cin` / `cout` still trail bare `from_chars` by a solid margin. Let's now point out **every expensive spot** along that dispatch chain — you'll understand why `iostream`, even "optimized", can't get truly fast:

**Locale lookups.** `>>` / `<<` format according to the current locale by default — the thousands separator in integers, the decimal point of floating-point numbers, the `true` / `false` text for booleans are all locale-dependent. Even if you never configure anything, the C locale still has to be consulted. We compared this in detail in [51-charconv](../strings/51-charconv.md) in this volume: `charconv` gets several times faster after cutting locale out, and this is exactly where that cost was hiding.

**Virtual dispatch.** `istream` / `ostream` implement `>>` / `<<` as calls to `streambuf` virtual functions (`sputc` / `sbumpc` / `xsputn` and friends), and `streambuf` is an abstract class — which concrete implementation runs is decided at runtime. The compiler can rarely inline this chain away entirely, so every `<<` carries a layer of indirect calls on its back.

**The sentry object.** This is a layer many people don't know about. The standard requires that every invocation of `>>` / `<<` construct a `sentry` object on entry — it checks the stream state, locks the `streambuf` (so that one `<<` is atomic under multithreading), and does the upfront preparation, then wraps up in its destructor. In other words, **every `<< x` you see corresponds to one sentry construction + destruction underneath**. Once or twice doesn't matter; across a loop of a million, that's real, honest overhead. It's also why "stitching several `<<` into one call" (say, building the string with `std::format` first and `<<`-ing it once) beats "ten `<<` in a row" — fewer sentry constructions.

**Synchronization with C stdio.** That's the `sync_with_stdio` section from earlier: on by default, forcing the standard streams to go through C's `FILE*` character by character — the single biggest cut in magnitude.

**Format parsing.** `>>` / `<<` doesn't just move bytes; it also runs the whole parsing pipeline of "skip leading whitespace, recognize the sign, cut off at the field width, assemble an integer", and `<<` in turn has to format an integer into characters. That work is inherently necessary — but `iostream` bundles it together with the locale, virtual dispatch, and sentry from above, so every number read pays for the full tour.

Add it all up and the slowness of `iostream` stops being mysterious — **it isn't one slow spot; it's every layer chipping in a little**. What you buy with it is just as tangible: type safety (the compiler knows at compile time that you're `<<`-ing an `int`, unlike `printf` where a mismatched type is undefined behavior), automatic extensibility (overload `operator<<` for your own type and it plugs into any `ostream`), and seamless cooperation with exceptions/RAII. That's why it won't — and shouldn't — be "optimized away": its cost is the cost of abstraction, and someone always has to pay that bill.

## Putting the Three Approaches Together: cin vs scanf vs from_chars

With all that said, the question that most deserves an answer arrives: for a job like "read one million ints", which one should we actually use? Let's put all the paths on the same data in one go: default `cin`, sync-off `cin`, C's `scanf`, and `fread` slurping the whole file into memory followed by `from_chars` parsing. The last one is the most "brute-force" fast path — it bypasses every stream abstraction, reading raw bytes and parsing them directly.

The cores of the `scanf` and `fread + from_chars` paths look like this, respectively:

```cpp
// Standard: C++20
// Path A: scanf, going straight through the FILE* buffer
long acc = 0;
int x;
while (std::scanf("%d", &x) == 1) acc += x;

// Path B: fread slurps all of stdin into memory, then from_chars parses it number by number
std::vector<char> buf;
{ char chunk[1 << 16]; size_t n;
  while ((n = std::fread(chunk, 1, sizeof(chunk), stdin)) > 0)
      buf.insert(buf.end(), chunk, chunk + n); }
const char* first = buf.data();
const char* last  = buf.data() + buf.size();
long acc2 = 0;
while (first < last) {
    while (first < last && (*first == ' ' || *first == '\n')) ++first;  // from_chars doesn't skip leading whitespace, so we do it ourselves
    if (first >= last) break;
    int y;
    auto r = std::from_chars(first, last, y);
    if (r.ec != std::errc{}) break;
    acc2 += y;
    first = r.ptr;
}
```

All four paths got the same one-million-integer data file; times are the minimum across multiple runs (absolute values drift per machine — look only at the order of magnitude):

```text
cin   (sync on,  default)   ~177 ms
scanf                       ~59 ms
cin   (sync off + untie)    ~40 ms
fread + from_chars          ~18 ms
```

Put side by side, these numbers make the conclusion crystal clear:

- **Default `cin` is the slowest of the four** — because it must stay synchronized with C stdio and walk the `FILE*` character by character, it can't even keep up with `scanf`.
- **`scanf` at roughly 59 ms**, 3x faster than default `cin`. It uses C's `FILE*` buffering directly — no `iostream` dispatch chain, and no sentry to pay for.
- **Sync-off `cin` at roughly 40 ms**, edging just past `scanf`. This shows that `iostream`'s dispatch chain itself **is not slower than C stdio** — once the "synchronization" shackle comes off, its own `streambuf` buffering is every bit as efficient.
- **`fread + from_chars` at roughly 18 ms**, better than twice as fast again. This path drives both the buffering (`fread` pulling a big chunk at a time) and the parsing (`from_chars`: no locale, no exceptions, no allocation) down to minimal overhead — the rightful destination for performance-sensitive scenarios. For a dedicated breakdown of why `from_chars` can be this fast, see [51-charconv](../strings/51-charconv.md).

::: warning A comparison that is easy to misread
Some people compare "in-memory `std::stringstream >>`" against "in-memory `sscanf`" and conclude that `iostream` is faster/slower than `scanf`. Careful here: **`sscanf` performs terribly on in-memory strings** (on this machine it can degrade to the tens-of-seconds level), because some of its implementations repeatedly rescan the remaining buffer — a completely different behavior from when it goes through `FILE*`. So keep "reading standard input" as the fair battleground — the table above — and don't hold up in-memory `sscanf` as the representative; that leads to misleading conclusions.
:::

One sentence to close it out: **`sync_with_stdio(false) + cin.tie(nullptr)` lets `cin` / `cout` catch up with the `scanf` / `printf` tier; but if you're truly squeezing performance, the fast paths are `from_chars` (input) and `std::print` / `std::format_to` (output) — the overhead of the `iostream` layer never goes away**.

## The Stream State Machine: failbit / badbit / eofbit

With performance out of the way, let's thoroughly cover the other `iostream` mechanism that regularly trips people up — its error state. Inside every stream are three state bits:

- `goodbit` (which is really 0) — all is well;
- `failbit` — the last operation **failed for format reasons** (say you wanted an `int` but ran into `"hello"`); the stream itself isn't broken, and clearing the state lets you keep using it;
- `badbit` — the stream is **genuinely in trouble** (an underlying I/O error, a corrupted buffer, that kind of thing); usually unrecoverable;
- `eofbit` — the end was reached.

The most crucial realization: **once `failbit` or `badbit` is set, all subsequent `>>` / `<<` become no-ops** — the stream refuses to work until you `clear()` the state back. Let's run this state machine live with a piece of code that reads `int`, `int`, `int` in sequence from a string stream, with a `"hello"` wedged in the middle:

```cpp
// Standard: C++20
#include <iostream>
#include <sstream>
#include <string>

int main() {
    std::istringstream iss("42  hello  99");
    int x;

    iss >> x;   // reads 42 cleanly
    std::cout << "读到 " << x
              << "  good=" << iss.good() << " fail=" << iss.fail()
              << " eof=" << iss.eof() << " bool(iss)=" << static_cast<bool>(iss) << '\n';

    iss >> x;   // wants an int but hits hello — failbit gets set, x unchanged
    std::cout << "格式不匹配后: good=" << iss.good()
              << " fail=" << iss.fail()
              << " bool(iss)=" << static_cast<bool>(iss) << '\n';

    int y = -999;
    iss >> y;   // stream is in fail state, this >> is a no-op, y unchanged
    std::cout << "y 还是 " << y << "，因为流在 fail 状态下 >> 被忽略\n";

    iss.clear();   // clears failbit; "hello" is still sitting in the buffer waiting
    std::string s;
    iss >> s;      // use a string to digest "hello"
    iss >> x;      // goes on to read 99
    std::cout << "clear() 之后: s=" << s << " x=" << x << '\n';

    // read past the end: eofbit and failbit get set together
    iss >> x;
    std::cout << "读到末尾后: eof=" << iss.eof()
              << " fail=" << iss.fail() << '\n';
    return 0;
}
```

The state transitions it prints:

```text
读到 42  good=1 fail=0 eof=0 bool(iss)=1
格式不匹配后: good=0 fail=1 bool(iss)=0
y 还是 -999，因为流在 fail 状态下 >> 被忽略
clear() 之后: s=hello x=99
读到末尾后: eof=1 fail=1
```

A few practical takeaways from this state machine:

**`operator bool` (and `operator!`) is the single entry point for checking whether a stream is usable.** The standard library gives streams an implicit conversion to `bool`, equivalent to `!fail()` — true as long as neither `failbit` nor `badbit` is set. That's precisely the foundation of the classic loop idiom:

```cpp
while (iss >> x) sum += x;   // >> returns the stream itself, which then converts to bool
```

`>> x` returns the `istream&` (the stream itself), which then implicitly converts to `bool`: keep looping while valid data arrives, exit on end-of-file (`eofbit` gets set together with `failbit`) or on a format error. This style is far cleaner — and safer — than "first `>>`, then check `eof()`" — **checking `eof()` alone is the classic trap**, because it's only set after you've read past the end, so the last value read may be a half-built one.

::: warning After clear(), the "bad characters" are still in the buffer
`clear()` only resets the state bits — **it doesn't touch the character in the buffer that caused the failure**. So in the example above, after `clear()`, `"hello"` is still stuck at the stream's read position, and the next `>> int` will fail immediately all over again. The remedies: either read it away with a `std::string` as the example does, or skip a stretch with `iss.ignore(...)`. When people find "it still won't read" after `clear()`, nine times out of ten this is why.
:::

**Keep `badbit` and `failbit` distinct.** `failbit` says "couldn't read an `int` this time, but clear the state and it's salvageable"; `badbit` says "the stream is broken, stop struggling". When interactive parsing hits bad data, the right routine is usually `clear()` + `ignore()` to skip the bad field and keep reading onward. Only underlying errors like a severed terminal/pipe truly land in `badbit`, and those cases usually call for exiting outright.

## When to Use iostream, and When Not To

After all this criticism of iostream, fairness is due. It isn't a tool that should be eradicated; it's a tool that belongs in the right scenarios.

**Scenarios where `iostream` is the right choice:**

- **Simple interaction, small command-line tools.** A few lines of `cout << "..." << x` paired with `cin >> x`: type-safe, readable, and printing your own types is one `<<` overload away. In such settings, development efficiency matters far more than that bit of I/O overhead.
- **Debug logging.** Especially via `std::cerr` / `std::clog` — error and diagnostic messages want "flushed immediately" and "not swallowed by a buffer", which is exactly the design intent of `cerr` being unbuffered; performance is hardly the concern here.
- **Places that need type safety but don't want `printf`'s undefined-behavior risk.** In `printf("%d", x)`, a mismatched `x` type is undefined behavior and the compiler won't necessarily flag it; with `std::cout << x`, a type error is a compile failure, period.

**Scenarios where `iostream` is the wrong choice:**

- **Performance-sensitive bulk numeric I/O.** Protocol parsing, serialization, CSV/JSON parsing, the big test cases in competitive programming. The rightful destination on this path is `from_chars` / `to_chars` ([51-charconv](../strings/51-charconv.md)) — a tens-of-times gap is not something you shave a little off.
- **Output that needs both type safety and format-string expressiveness.** Since C++20 this desire has a better answer — `std::format` ([52-format](../strings/52-format.md)) and C++23's `std::print` / `std::println` ([53-print](../strings/53-print.md)). `print` writes to the stream directly without passing through the `<<` dispatch chain; article 53 in this volume measured its order-of-magnitude advantage over `cout`.
- **Binary, random-access, mmap-style big-file I/O.** That's file-stream territory and belongs to [56-fstream](56-fstream.md); this article focuses on the standard streams, so one sentence only: `fstream` is no performance tool for random access on large files either — for real speed you switch to `mmap` or C's `stdio`.

One decision through-line: **iostream is the "safe and convenient" default, not the "fast" default**. The moment you find yourself writing work-arounds for its speed (turning off sync, untying, `<< '\n'` instead of `endl`), that usually means it's time to switch tools — not to keep squeezing performance out of this abstraction layer.

## Summary

Let's gather the key conclusions from this trip through `<iostream>`:

- **Layering**: `ios_base` → `ios` → `istream` / `ostream` → `iostream`; the one doing the real work and managing buffering is the attached `streambuf`, while `<<` / `>>` only handle formatting and dispatching the requests downward.
- **The four standard streams**: `cout` / `clog` buffered, `cerr` unbuffered (every `<<` flushes immediately) — hence error diagnostics default to `cerr`, unafraid of dying inside a buffer.
- **The two performance switches**: `sync_with_stdio(false)` unshackles the C stdio synchronization (which by default drags `cin` into walking the `FILE*` character by character), and `cin.tie(nullptr)` saves the `cout` flush before every read. Measured on reading one million ints: 177 ms down to 40 ms, **roughly a 4x speedup**.
- **The cost of turning sync off**: stop mixing `cin` / `cout` with `scanf` / `printf` (the order scrambles — in our run, source `printf 1 cout 2` printed as `cout 2 / cout 4 / printf 1 / printf 3`); interactive prompts must be flushed by hand.
- **Why it's slow**: locale lookups + virtual dispatch + a sentry construction per `<<` + C stdio synchronization + format parsing — every layer contributes a little; the cost lives in the abstraction, not in any single spot.
- **Head-to-head (reading 1 million ints, local GCC 16.1.1)**: default `cin` ~177 ms, `scanf` ~59 ms, sync-off `cin` ~40 ms, `fread + from_chars` ~18 ms. Sync-off `cin` ≈ `scanf`, but `from_chars` is better than twice as fast again.
- **State machine**: `goodbit` / `failbit` / `badbit` / `eofbit`; once `fail` or `bad` is set, all subsequent `>>` / `<<` are no-ops and only `clear()` restores operation — but `clear()` leaves the bad characters in the buffer (you must `ignore` them or read them away).
- **Selection**: simple interaction, debug logging, small tools that prioritize type safety — use `iostream`; bulk numeric I/O — `charconv`; type-safe output wanting format-string expressiveness — `format` / `print`; binary large files — `fstream` / `mmap`.

Next article we move to file streams — `fstream`'s three kinds of file streams, `open` modes, the lifecycle traps of RAII-automatic `close`, and why large-file I/O should also switch tools.

## References

- [cppreference: iostream](https://en.cppreference.com/w/cpp/header/iostream) — the standard stream objects `cin` / `cout` / `cerr` / `clog` and a header overview
- [cppreference: std::ios_base::sync_with_stdio](https://en.cppreference.com/w/cpp/io/ios_base/sync_with_stdio) — the semantics of the sync switch and "ordering is not guaranteed once it's off"
- [cppreference: std::basic_streambuf](https://en.cppreference.com/w/cpp/io/basic_streambuf) — the underlying buffering abstraction
- [cppreference: std::basic_istream::sentry](https://en.cppreference.com/w/cpp/io/basic_istream/sentry) — the sentry object constructed on every `>>`
- [cppreference: std::basic_ios](https://en.cppreference.com/w/cpp/io/basic_ios) — the `fail` / `bad` / `eof` / `clear` / `operator bool` state machine
