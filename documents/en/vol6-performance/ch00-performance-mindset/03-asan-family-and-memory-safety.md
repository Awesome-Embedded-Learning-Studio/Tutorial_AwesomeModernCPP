---
chapter: 0
cpp_standard:
- 11
- 14
- 17
- 20
description: Starting from Heartbleed, this article dissects AddressSanitizer's shadow-memory trio, catches OOB/UAF/global-overflow bugs and UBSan errors in real runs, and sorts out the responsibilities of — and mutual exclusivity among — the five siblings ASan/LSan/MSan/TSan/UBSan.
difficulty: advanced
order: 3
platform: host
prerequisites:
- Dynamic Memory Management (new/delete and smart pointers)
- Debugging Techniques for Concurrent Programs
reading_time_minutes: 24
related:
- Dynamic Memory Management (new/delete and smart pointers)
- Debugging Techniques for Concurrent Programs
- C Dynamic Memory Management (malloc/free and valgrind)
tags:
- host
- cpp-modern
- advanced
- 内存安全
- 调试
- 内存管理
title: 'The ASan tool family and memory safety: shadow memory, Heartbleed, and sanitizer selection'
translation:
  source: documents/vol6-performance/ch00-performance-mindset/03-asan-family-and-memory-safety.md
  source_hash: 35b9df09fd3e640c1f80403410197c03d7fa48738f483bb3927990618e98ebc6
  translated_at: '2026-09-26T05:25:15+00:00'
  engine: anthropic
  token_count: 11500
---
# The ASan tool family and memory safety: shadow memory, Heartbleed, and sanitizer selection

> PS: This part is a set of notes I migrated from my college days, verified only by limited searching. If you spot a technical claim that is sloppy — or outright wrong — please file an Issue, or send a fix PR straight away!

After writing C/C++ for a while, you have most likely been tortured repeatedly by a few classes of problems: reading one element past the end of an array, a freed pointer getting used again by somebody else, the same `delete` called twice. What these errors share is a uniquely nasty property: they are **undefined behavior** (the infamous Undefined Behavior). They don't necessarily crash — the program runs just fine in a Debug build, then explodes randomly in Release or on a different machine. Worse, the crash site is usually miles away from the code that actually went wrong, and the stack trace may well point at some innocent library function.

Why does this happen? Because this class of bug corrupts the memory manager's own metadata, and it only blows up when the next `malloc`/`free` walks over the trampled spot. The earlier parts of this volume are about performance; in this article we switch to another dimension: how to use tools to drag the bug out into the daylight before it turns into a production incident. The protagonist is AddressSanitizer (ASan) and the whole sanitizer tool family behind it.

Don't be too quick to dismiss ASan as some small "just add a flag and done" utility. The design behind it (shadow memory, compile-time instrumentation) is actually one of the most important pieces of engineering progress in C/C++ memory safety over the past decade-plus, and it was originally invented to plug a hole that made the entire internet's blood run cold. We start with that hole.

## Where it all began: Heartbleed and the buffer over-read

In April 2014, CVE-2014-0160 was disclosed, code-named Heartbleed. It was a hole hiding inside a harmless-sounding OpenSSL feature (the TLS heartbeat extension). The protocol is simple: the client sends over an arbitrary chunk of data and tells the server "this data is N bytes long, read it back to me verbatim", as a way of checking that the connection is still alive.

The bug: the server **trusted the length N reported by the client, without validating that N actually stayed within the real length of the data it was holding**. So an attacker only had to report a huge N (say, 64KB), and the server would "read back" 64KB out of its own process memory to the attacker. What came back could be another session's TLS private keys, user passwords, session tokens — anything in process memory adjacent to that buffer, all of it leaked.

The essence of this bug is an out-of-bounds **read** (a buffer over-read), not an out-of-bounds **write**. An OOB write at least corrupts data and tends to expose itself; an over-read is far quieter — the process itself doesn't crash, and the data just quietly flows out. ASan got brought up over and over back then precisely because it is one of the few tools that can **reliably detect over-reads**: as long as the out-of-bounds memory touches a redzone ASan planted, a single read trips the alarm immediately.

Let's reproduce a Heartbleed-shaped bug in a few dozen lines of Modern C++, and then let ASan catch it in the act. This is this article's flagship demo, and we will come back to it again and again.

```cpp
// oob_read.cpp — an out-of-bounds read in the shape of Heartbleed
// Platform: host    Standard: C++20
// Build: g++ -std=c++20 -O1 -fsanitize=address -g oob_read.cpp -o oob_read
#include <array>
#include <cstdio>
#include <string>

// Heartbeat echo: the client says "give me n bytes back". The server obliges, but never validates the upper bound of n.
std::string read_back(const std::array<char, 8>& buf, int n)
{
    return std::string(buf.data(), n);   // n can be far larger than 8
}

int main()
{
    std::array<char, 8> buf{'H', 'i', '!', 0, 0, 0, 0, 0};
    // Only 8 bytes were authorized, yet we ask to "read back" 64 — the classic over-read
    auto leaked = read_back(buf, 64);
    std::printf("读到 %zu 字节: %.8s...\n", leaked.size(), leaked.c_str());
}
```

Compiled and run without ASan, this code will most likely "look normal": the `std::string` constructor dutifully copies 64 bytes starting from `buf.data()` at the length you gave it, reading off all the unrelated bytes further up the stack, and the program doesn't crash. That is exactly what makes over-reads scary.

Add `-fsanitize=address` and run again, and the picture changes completely. Here's what it produces on my machine with GCC 16.1.1:

```text
=================================================================
==37023==ERROR: AddressSanitizer: stack-buffer-overflow on address 0x72175e1f0028 at pc 0x761760d29ac2 ...
READ of size 64 at 0x72175e1f0028 thread T0
    #0 0x... in memcpy (/usr/lib/libasan.so.8+0x129ac1)
    ...
    #6 0x... in read_back[abi:cxx11](std::array<char, 8ul> const&, int) oob_read.cpp:11
    #7 0x... in main oob_read.cpp:18
    ...

  This frame has 2 object(s):
    [32, 40) 'buf' (line 16)
    [64, 96) 'leaked' (line 18) <== Memory access at offset 40 partially underflows this variable
SUMMARY: AddressSanitizer: stack-buffer-overflow oob_read.cpp:11 in read_back
```

Note two details. First, the error type is `stack-buffer-overflow`, occurring at line 11 of `read_back` — the `return std::string(buf.data(), n);` line — pinpointing the exact source location, which is exactly why you must compile with `-g`. Second, ASan even tells us the stack frame holds two objects: `buf` occupies `[32, 40)` and `leaked` occupies `[64, 96)`, and the out-of-bounds read (offset 40) lands exactly between them. This level of crime-scene information is what fundamentally separates ASan from the "sprinkle some asserts and hunt slowly" approach.

## So what exactly does ASan do to make this happen

### Piece one: compile-time instrumentation (CTI)

ASan **rewrites your code at compile time**; it is not a profiler that analyzes after the fact. When you add `-fsanitize=address`, the compiler (GCC or Clang, either works) inserts extra checking instructions around every memory access (every `*p`, every array subscript, every `memcpy`). This technique is called **compile-time instrumentation** (CTI), also known as static instrumentation.

Let's first verify that it really does "touch your code". Compile the `oob_read.cpp` above twice — once without ASan, once with — and compare the two **code-section (.text) sizes**, i.e. the machine instructions actually packed into the binary:

```text
Plain build .text:   2792 bytes
ASan build .text:    5736 bytes   (+105%)
```

(My machine, GCC 16.1.1, `g++ -std=c++20 -O1 -g`, `.text` inspected with `size`.) That extra doubling is the checking instructions the compiler stuffed in around every memory access. Watch out for a trap here: **don't compare whole-binary file sizes**. ASan's runtime library `libasan.so.8` is **dynamically linked** (you can see it with `ldd`) and is not baked into the executable, so the overall file size only grows by about 5%; what genuinely reflects the amount of instrumentation is the `.text` code section — that is where the doubling happens. The cost is bigger binaries and slower runs, but next to the bugs it catches, that overhead is nearly negligible at the development stage. CTI is decided at **compile time**, so you must pass `-fsanitize=address` **when compiling**, and **when linking too**. If you add it only when compiling the main program but not when linking some third-party `.a` library, the memory accesses inside that library were never instrumented, and ASan is blind to that part of the code. The full pipeline:

```bash
g++ -std=c++20 -O1 -fsanitize=address -g -c a.cpp -o a.o     # compile with it
g++ -std=c++20 -O1 -fsanitize=address -g main.cpp a.o -o app  # link with it too
```

`-fsanitize=address` must appear in both the compile and the link step; miss either one and it's all for nothing.

### Piece two: shadow memory

Instrumentation alone isn't enough. The checks it inserts need a "ledger" that can answer "is this address actually accessible right now". That ledger is **shadow memory**.

The core idea is an elegantly simple design: **1 byte of shadow memory records the accessibility state of 8 bytes of real memory**. That is, ASan maps the entire process address space, in groups of 8 bytes, onto a contiguous shadow region, at a ratio of 1:8. Checking whether an address is legal then takes nothing more than computing its shadow byte and reading it — no complicated hash tables to maintain.

ASan prints the shadow-byte values verbatim at the end of its report. Here's the legend from a real run:

```text
Shadow byte legend (one shadow byte represents 8 application bytes):
  Addressable:           00
  Partially addressable: 01 02 03 04 05 06 07
  Heap left redzone:       fa
  Freed heap region:       fd
  Stack left redzone:      f1
  Stack mid redzone:       f2
```

That is the entire semantics of the 1:8 mapping. `00` means all 8 bytes are accessible; `01`–`07` mean only the first few bytes are legal (for example, `03` means the first 3 bytes are accessible and the last 5 are not — used for partially addressable regions at alignment tails); `fa` is the redzone around heap allocations: ASan secretly plants a ring of "keep out" zones around every block you `new`, so the moment you read an `fa`, you have a heap out-of-bounds; `fd` is memory that has already been `free`d — touch it and it's use-after-free; `f1`/`f2` are the redzones for stack objects.

Look back at the shadow dump in the earlier report:

```text
=>0x72175e1f0000: f1 f1 f1 f1 00[f2]f2 f2 00 00 00 00 f3 f3 f3 f3
```

The `00` is `buf` itself (8 bytes, 1 shadow byte), and the `[f2]` right after it is the mid redzone between stack objects. The address of our out-of-bounds read lands exactly on that `f2`, and ASan spots it at a glance. This is why the shadow-memory mechanism can be precise down to the byte.

### Piece three: the runtime library + quarantine

Instrumentation and a shadow region still aren't enough — somebody has to **fill this ledger in**. The ASan runtime library (`libasan`) replaces `new`/`delete` and `malloc`/`free` wholesale with its own versions. Every time memory is allocated, the runtime paints redzones for it in the shadow region; every time it is freed, the corresponding shadow region gets marked `fd`.

There is another key design here called **quarantine**. Memory that has been `free`d is not immediately handed back to the system for reuse; ASan first tosses it into a quarantine queue to sit for a while. Why? Because with use-after-free bugs, if the memory is handed out to somebody else right after the `free`, its shadow state flips back to `00`, and later mistaken reads go undetected. Holding it in quarantine for a while guarantees that the "already freed" state is still there for later bad accesses to run into.

The quarantine isn't unlimited, though: the queue has a cap, and once it fills, the oldest freed memory is genuinely reclaimed in FIFO order. So ASan's use-after-free detection is not 100% either — if the quarantine window has already slid past and the memory has been reallocated, that particular bad read escapes. But combined with adequate test coverage, the vast majority of UAFs get caught.

### The cost: 2-4x overhead, and why it's still worth it

Add the three pieces together and ASan's typical overhead is **a 2-4x runtime slowdown and a 3-5x memory overhead** (the shadow region takes 1/8, plus redzones and quarantine). Sounds like a lot — but compared to what?

The traditional memory-checking tool Valgrind (Memcheck) uses **dynamic binary instrumentation** (DBI): instead of recompiling your program, it translates every machine instruction at runtime into its own intermediate representation, analyzes each one, then executes it. High precision and no recompilation — but at the price of a 20-50x slowdown. A test that normally takes 1 second keeps you waiting half a minute under Valgrind, which often rules it out of daily CI entirely.

ASan **front-loads the analysis cost into compile time** (CTI); at runtime it only does table lookups, which is how it squeezes overhead down to 2-4x. At that magnitude you can keep ASan **permanently** on in development and CI, running the full test suite, instead of manually invoking Valgrind once in a blue moon. That is ASan's most fundamental advantage over Valgrind: **you can afford it**.

::: warning No Valgrind on this machine
Every ASan/UBSan output in this article comes from real runs on my machine (GCC 16.1.1 / Clang 22, WSL2). Valgrind isn't installed in my environment (`which valgrind` → not found), so this article doesn't paste any Valgrind output. If you need Valgrind, `apt install valgrind` on Debian/Ubuntu does it; usage is covered in the valgrind section of [C dynamic memory management](../../vol1-fundamentals/c_tutorials/14-dynamic-memory.md) in vol1. Burn in the essential difference between the two commands: **ASan is compile-time `-fsanitize=address` (CTI); Valgrind is runtime `valgrind ./prog` (DBI)**.
:::

## The tool family: five sanitizers, one job each

ASan is really just one member of a family. These tools started out as patches for GCC and Clang written by Google engineers, and later became standard equipment in mainstream compilers. The family has five members in all, each watching for one specific class of error:

| Tool | Compile flag | What it catches | Typical overhead |
|------|---------|--------|---------|
| **ASan** (AddressSanitizer) | `-fsanitize=address` | Out-of-bounds reads/writes, use-after-free, double-free, stack/global overflows | 2-4x slowdown |
| **LSan** (LeakSanitizer) | `-fsanitize=leak` | Memory leaks (heap memory left unfreed at program exit) | Nearly zero overhead |
| **MSan** (MemorySanitizer) | `-fsanitize=memory` | Reads of uninitialized memory (use of uninitialized value) | ~3x slowdown |
| **TSan** (ThreadSanitizer) | `-fsanitize=thread` | Data races, deadlocks | 5-15x slowdown |
| **UBSan** (UndefinedBehaviorSanitizer) | `-fsanitize=undefined` | Undefined behavior (signed overflow, null-pointer dereference, out-of-range shifts, etc.) | Configurable; most subchecks are cheap |

Of the five siblings, ASan is the workhorse, all but mandatory in day-to-day development; LSan gets enabled by default alongside ASan (on GCC/Clang in supported environments); MSan is only fully available on Clang, and **the entire program** must be built as an MSan version (even libc has to be an MSan build, or you drown in false positives); TSan watches concurrency specifically — we covered it in [Debugging Techniques for Concurrent Programs](../../vol5-concurrency/ch08-debug-testing-perf/01-debugging-concurrency.md) in vol5; and UBSan is the "finisher" — cheap, and combinable with the others.

### ASan and TSan are mutually exclusive: an iron rule

These five tools don't combine arbitrarily. The most important constraint: **ASan and TSan cannot be enabled at the same time**. ASan wants its own shadow-memory layout, TSan wants its own, and the two mechanisms fight. The compiler rejects you outright at compile time:

```text
$ g++ -std=c++20 -fsanitize=address,thread -g conflict.cpp -o conflict
cc1plus: error: '-fsanitize=thread' is incompatible with '-fsanitize=address'
```

The error message is blunt. The engineering consequence of this constraint: in a project's CI, memory-error detection and data-race detection need **two separate, independent builds** — one with ASan, one with TSan — each running the tests. The vol5 TSan article covered this "dual build" practice in detail; here we just keep the conclusion.

As for MSan, it is incompatible with both ASan and TSan (it requires all code to run "cleanly" through its own uninitialized-value tracking), and it only supports Clang, so it sees the least real-world use. LSan and UBSan are the two "goes-with-everything" picks: LSan is nearly zero-overhead and can stay on permanently, and most of UBSan's subchecks can run alongside ASan.

## Hands-on: ASan catches three classic error classes

Principles alone aren't satisfying. Let's write one example each of the three memory errors C++ code most easily trips over, and let ASan catch them one by one. All three outputs below are from real runs on my machine.

### Heap use-after-free

Smart pointers block most UAFs, but as long as the project still has raw pointers and C-style APIs, this hole can never be fully plugged. A minimal example: after releasing a `unique_ptr`, keep reading through the raw pointer it once handed out:

```cpp
// uaf.cpp — use-after-free
// Platform: host    Standard: C++20
// Build: g++ -std=c++20 -O1 -fsanitize=address -g uaf.cpp -o uaf
#include <cstdio>
#include <memory>

int main()
{
    auto p = std::make_unique<int>(42);
    int* raw = p.get();     // grab the raw pointer
    p.reset();              // freed here — raw instantly becomes dangling
    std::printf("悬空指针读到的值: %d\n", *raw);   // use-after-free
}
```

Run it with ASan:

```text
=================================================================
==37082==ERROR: AddressSanitizer: heap-use-after-free on address 0x7a948abe0010 ...
READ of size 4 at 0x7a948abe0010 thread T0
    #0 0x... in main uaf.cpp:12

0x7a948abe0010 is located 0 bytes inside of 4-byte region [0x7a948abe0010,0x7a948abe0014)
freed by thread T0 here:
    #0 0x... in operator delete(void*, unsigned long) (/usr/lib/libasan.so.8+0x12e4c1)
    ...
    #4 0x... in main uaf.cpp:11

previously allocated by thread T0 here:
    #0 0x... in operator new(unsigned long) (/usr/lib/libasan.so.8+0x12d341)
    ...
    #2 0x... in main uaf.cpp:9

SUMMARY: AddressSanitizer: heap-use-after-free uaf.cpp:12 in main
```

This report is where ASan is at its most valuable. It doesn't just tell you "the read at line 12 is a use-after-free"; it simultaneously hands you **the two-part history of that memory**: allocated by `make_unique` at `uaf.cpp:9`, freed by `reset` at `uaf.cpp:11`. Stare at those two lines and the bug's causal chain is complete. This is precisely the payoff of the quarantine + redzone design: freed memory is marked `fd` instead of being reclaimed immediately, so later bad reads still run into it.

The `[fd]` in the shadow dump is the smoking gun:

```text
=>0x7a948abe0000: fa fa[fd]fa fa fa fa fa ...
```

`fd` = freed heap region. Credit goes to ASan's "ledger".

### Global buffer overflow

Global/static variables get redzone protection too. An out-of-bounds access into a global array gets caught all the same:

```cpp
// global_oob.cpp — out-of-bounds on a global array
// Build: g++ -std=c++20 -O1 -fsanitize=address -g global_oob.cpp -o global_oob
#include <cstdio>
int g[4] = {1, 2, 3, 4};
int main() { std::printf("g[5] = %d\n", g[5]); }
```

```text
==38356==ERROR: AddressSanitizer: global-buffer-overflow on address 0x63ca65acd074 ...
SUMMARY: AddressSanitizer: global-buffer-overflow global_oob.cpp:5 in main
```

The error type is clearly labeled `global-buffer-overflow`. ASan distinguishes the three kinds of regions — stack, heap, global — with different redzone encodings (`f1`/`f2` for stack, `fa` for heap, `f9` for globals), so you can see at a glance which class of storage the overflow happened in.

::: warning On the claim that "global OOB detection requires Clang 11"
Some older material says "detecting out-of-bounds accesses on global variables with ASan requires Clang 11 or newer". The historical background: early ASan's redzone support for globals was incomplete, and it took improvements introduced in Clang 11 — the ODR indicator (`-fsanitize-address-use-odr-indicator`) and friends — to make global detection solid. But **today** (GCC 8.3+ / mainstream Clang versions), global-overflow detection is on by default and works out of the box; the example above was caught in one shot on my GCC 16.1.1 with the default configuration. This "version threshold" is obsolete for current toolchains — don't let old articles lead you astray.
:::

### Leaks: LSan wraps up at exit

Finally, memory leaks. LSan works differently from the previous ones: it waits until `main` returns and the program is about to exit, then scans every heap allocation still "alive" and flags the ones nothing references and that were never freed. A minimal deliberate leak:

```cpp
// leak.cpp — a deliberate leak
// Platform: host    Standard: C++20
// Build: g++ -std=c++20 -O1 -fsanitize=address -g leak.cpp -o leak
#include <cstdlib>
#include <cstdio>
int main()
{
    int* p = (int*)std::malloc(sizeof(int) * 4);  // take some heap memory
    p[0] = 42;
    std::printf("ptr = %p\n", (void*)p);  // let the pointer escape, so the optimizer can't delete the whole thing
    // no free: the memory p points to leaks when the program exits
}
```

Run it with ASan (GCC 16.1.1 / WSL2, with LSan enabled by default alongside ASan):

```text
ptr = 0x730c4cbe0010
=================================================================
==364484==ERROR: LeakSanitizer: detected memory leaks

Direct leak of 16 byte(s) in 1 object(s) allocated from:
    #0 0x... in malloc (/usr/lib/libasan.so.8+0x12c161)
    #1 0x... in main leak.cpp:8
    ...

SUMMARY: AddressSanitizer: 16 byte(s) leaked in 1 allocation(s).
```

Note: the report is printed **after** `main` returns — that is exactly LSan's "wrap up at exit" way of working. [Dynamic Memory Management](../../vol1-fundamentals/ch12/02-new-delete.md) in vol1 also gives an equivalent example worth cross-checking.

::: warning LSan's "silent exit" trap
On mainstream Linux (GCC 16.1.1 / Clang 22), LSan is enabled by default alongside ASan, and the example above is caught reliably on my machine. But watch out for a real trap: **leaks are only scanned when the process exits normally**. If your program gets taken out by `SIGKILL`, or calls `_exit` to bypass the `atexit` hooks, or runs in a container/sandbox where LSan's exit hook never fires, the leak report **silently disappears**: the program looks "error-free", but the truth is LSan never got the chance to scan.

How to chase it down: confirm the process exits via a normal return; force detection on explicitly with `ASAN_OPTIONS=detect_leaks=1` when needed; and for long-running services (the ones that never exit at all), LSan's "wrap up at exit" model simply doesn't apply — switch to Valgrind massif or heap sampling instead. Never assume that "no LSan report means no leak".
:::

## UBSan: turning silent undefined behavior into errors

With the main force of the ASan family covered, we turn to the finisher, UBSan. C/C++ has a blood-pressure-spiking property: **undefined behavior (UB)**. The compiler's attitude toward UB is "the standard doesn't say what happens, so I'll assume it never happens and optimize freely". The consequence: signed integer overflow, out-of-range shifts, null-pointer dereferences — programs **often look like they run just fine**, until one day `-O2` is on or the compiler version changes, the optimizer makes aggressive transformations based on the assumption "this can't overflow", and the program suddenly computes nonsense.

UBSan's approach: plant a runtime check next to every operation that can produce UB, and the moment one actually happens, print a `runtime error: ...` report immediately (by default the program is not aborted, though that is configurable). The overhead is small, and many of its subchecks can stay on permanently alongside ASan.

A minimal example, stuffing in three classic UBs at once:

```cpp
// ubsan.cpp — UBSan catching undefined behavior
// Platform: host    Standard: C++20
// Build: g++ -std=c++20 -O1 -fsanitize=undefined -g ubsan.cpp -o ubsan
#include <cstdio>
#include <limits>

int main()
{
    int arr[4]{1, 2, 3, 4};
    int idx = 10;
    std::printf("越界下标 arr[10] = %d\n", arr[idx]);   // out-of-bounds subscript

    int max = std::numeric_limits<int>::max();
    std::printf("有符号溢出: %d\n", max + 1);           // signed integer overflow

    int shift = 32;
    std::printf("左移 32 位: %d\n", 1 << shift);        // shift amount >= width
}
```

Run it with UBSan:

```text
ubsan.cpp:11:55: runtime error: index 10 out of bounds for type 'int [4]'
ubsan.cpp:11:16: runtime error: load of address 0x7ffe8a0525c8 with insufficient space for an object of type 'int'
ubsan.cpp:14:16: runtime error: signed integer overflow: 2147483647 + 1 cannot be represented in type 'int'
ubsan.cpp:17:42: runtime error: shift exponent 32 is too large for 32-bit type 'int'
```

All three UBs caught, precise down to `file:line:column`. The list of UBs UBSan covers is long; the common ones include:

- **Arithmetic**: signed integer overflow/underflow, division by zero;
- **Shifts**: negative or >= width shift amounts, left shifts that clobber the sign bit;
- **Memory/pointers**: null-pointer dereference, misaligned memory access, object-size mismatch (accessing through a pointer of the wrong type);
- **Arrays**: out-of-bounds subscripts (`-fsanitize=bounds`; this overlaps with ASan's OOB detection but with a different focus: ASan watches redzones, UBSan checks array sizes known at compile time).

UBSan's overhead depends on which subchecks you enable. `-fsanitize=undefined` is a bundle of default subchecks, most of them light; the genuinely expensive one is `-fsanitize=integer` (unsigned overflow counts as an error too — heavy overhead, lots of false positives, use with care in production). The everyday recommendation: enable `-fsanitize=undefined` together with ASan — low cost, high yield.

## Choosing: which tool to reach for when facing a memory bug

By now all five siblings have taken the stage. The question: when you are actually sitting in front of a weird bug, in what order do you reach for the tools? Let's triage by "symptom":

- **Crashes immediately / segfault / intermittent crashes**: turn on ASan first and run a reproducing test. Out-of-bounds, UAF, and double-free are the three most common causes of segfaults, and ASan sweeps them up in one pass.
- **Intermittently wrong results / bizarre values crossing function boundaries**: suspect UAF or a data race. Rule out UAF with ASan first; if ASan reports nothing, build a separate TSan version to hunt the data race (remember the two are mutually exclusive — never on at the same time).
- **Nonsensical computed values / behavior changes under `-O2`**: you can all but lock in UB — go straight to UBSan.
- **Reading garbage values that "look normal", behavior depending on uninitialized data**: MSan (note: Clang only, and the whole program must be built with it).
- **Process eating more and more memory / suspected leak**: LSan (reported at exit), or for long-running services, Valgrind massif / heap sampling.

One engineering practice: **keep two builds permanently in CI** — one `ASan+UBSan`, one `TSan` — run on every commit. The overhead is acceptable (ASan+UBSan sits in the 2-4x range), and what you buy is pinning down the most expensive class of bug, the "random crash after launch", before it ever leaves the building.

::: warning ASan is not a silver bullet
ASan is powerful, but it has a few unavoidable limitations you must keep in mind.

First, **it only catches paths that actually execute**. CTI is runtime detection; code that never runs never triggers a check. If your test coverage is thin and some out-of-bounds path has never been exercised, ASan can't catch it — which is exactly why ASan should be paired with good test cases, or even fuzzing: the fuzzer's job is to drag rare paths out into the open, and ASan's job is to report the moment one of those paths goes wrong.

Second, **it only catches memory-class errors**. Logic errors (wrong results), concurrency errors (data races), and UB such as integer overflow are not ASan's business: the latter goes to UBSan, the former to TSan. Don't expect one flag to solve every problem.

Third, **don't enable it in production**. A 2-4x slowdown plus extra memory is a disaster under production load. ASan/UBSan/TSan are **development / testing / CI stage** tools; make sure these flags are stripped from release builds.

Fourth, **it has false-positive edges**. Certain custom stack-unwinding mechanisms (`swapcontext`, `vfork`) make ASan's shadow-region judgment go wrong and report false positives. That line in the report — `HINT: this may be a false positive if your program uses some custom stack unwind mechanism` — is warning you about exactly this.
:::

Real memory safety is held up by RAII, smart pointers, `std::span`, range-`for` — the tools that **make out-of-bounds and dangling accesses unwritable at the syntax level**; those are the subjects of vol1 and vol3. The value of the ASan toolchain lies in the transition period: while you haven't yet replaced every raw pointer and the third-party C libraries aren't yet wrapped in modern façades, it is that "last line of defense", making latent memory bugs take shape at the development stage instead of detonating in production at 3 a.m. to greet you.

## References

- [AddressSanitizer · google/sanitizers Wiki](https://github.com/google/sanitizers/wiki/AddressSanitizer) — the official ASan write-up; the authoritative source for the shadow-memory mechanism, the 1:8 mapping, and the 2x overhead
- [Clang: AddressSanitizer](https://clang.llvm.org/docs/AddressSanitizer.html) — the Clang-side ASan documentation, covering the evolution of global detection such as `-fsanitize-address-use-odr-indicator`
- [Clang: ThreadSanitizer](https://clang.llvm.org/docs/ThreadSanitizer.html) — the TSan documentation, source for the ASan↔TSan mutual exclusion (see the vol5 concurrency-debugging article)
- [Clang: UndefinedBehaviorSanitizer](https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html) — the inventory of UBSan subchecks and their overheads
- [Valgrind User Manual](https://valgrind.org/docs/manual/manual.html) — the DBI approach and Memcheck/Helgrind, for the 20-50x overhead comparison
