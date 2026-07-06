---
chapter: 0
cpp_standard:
- 11
- 14
- 17
- 20
description: Starting from Heartbleed, this article unpacks AddressSanitizer's shadow-memory trio, measures OOB/UAF/global-overflow and UBSan in real runs, and sorts out the responsibilities and mutual exclusivity of the five siblings ASan/LSan/MSan/TSan/UBSan.
difficulty: advanced
order: 3
platform: host
prerequisites:
- Dynamic memory management (new/delete and smart pointers)
- Concurrency debugging techniques (ThreadSanitizer)
reading_time_minutes: 24
related:
- Dynamic memory management (new/delete and smart pointers)
- Concurrency debugging techniques (ThreadSanitizer)
- C dynamic memory management (malloc/free and valgrind)
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
  translated_at: '2026-07-05T13:35:00+00:00'
  engine: manual
  token_count: 5500
---
# The ASan tool family and memory safety: shadow memory, Heartbleed, and sanitizer selection

> PS: This part is a set of notes I migrated back in college, only lightly search-verified. If you spot a technical claim that's loose or outright wrong, please file an Issue or send a fix PR!

After writing C/C++ for a while, you've most likely been tortured repeatedly by a few classes of problems: reading one element past the end of an array, a freed pointer getting used again by someone else, the same `delete` called twice. These errors share a nasty property: they are **undefined behavior** (the infamous Undefined Behavior). It doesn't necessarily crash; in Debug builds it runs fine, and then in Release or on a different machine it explodes randomly. Worse, the crash site is usually miles away from the code that actually went wrong, and the stack trace may point at some innocent library function.

Why? Because these bugs corrupt the memory manager's own metadata, and the damage only triggers the next time `malloc`/`free` walks over the trampled spot. The rest of this volume is about performance; this article switches to a different dimension: how to catch these bugs with tools before they cause an online incident. The protagonist is AddressSanitizer (ASan) and the whole sanitizer family behind it.

Don't be too quick to dismiss ASan as "just a flag you add". Its design (shadow memory, compile-time instrumentation) is one of the most important engineering advances in C/C++ memory safety over the past decade, and it was originally invented to plug a hole that made the whole internet sweat. We start there.

## The starting point: Heartbleed and buffer over-read

In April 2014, CVE-2014-0160 was disclosed, codenamed Heartbleed. It was a hole hiding in an innocent-sounding feature of OpenSSL—the TLS heartbeat extension. The protocol is simple: the client sends some arbitrary data and tells the server "this data is N bytes, read it back to me as-is", just to verify the connection is still alive.

The bug: the server **trusted the length N the client reported, but never checked whether N actually stayed within the real length of the data it held**. So an attacker only had to report a huge N (say 64KB), and the server would "read back" 64KB from its own process memory to the attacker. What got read could be another session's TLS private keys, user passwords, session tokens—anything in process memory adjacent to that buffer, all leaked.

The essence of this bug is an out-of-bounds **read**, not a write (buffer over-read / over-read). Out-of-bounds writes at least corrupt data and tend to expose themselves; over-reads are far quieter, the process doesn't crash, and the data just silently flows out. ASan was brought up repeatedly back then precisely because it's one of the few tools that can **reliably detect over-read**: as long as the out-of-bounds memory touches a redzone ASan planted, a single read triggers an error.

Let's reproduce a Heartbleed-shaped bug in a few dozen lines of Modern C++, and let ASan catch it red-handed. This is the headline demo of this article; we'll reuse it throughout.

```cpp
// oob_read.cpp — reproduce a Heartbleed-shaped out-of-bounds read
// Platform: host    Standard: C++20
// Build: g++ -std=c++20 -O1 -fsanitize=address -g oob_read.cpp -o oob_read
#include <array>
#include <cstdio>
#include <string>

// Heartbeat echo: the client says "give me back n bytes". The server complies,
// but never checks the upper bound of n.
std::string read_back(const std::array<char, 8>& buf, int n)
{
    return std::string(buf.data(), n);   // n may be far larger than 8
}

int main()
{
    std::array<char, 8> buf{'H', 'i', '!', 0, 0, 0, 0, 0};
    // Only 8 bytes authorized, but we ask to "read back" 64 — a classic over-read
    auto leaked = read_back(buf, 64);
    std::printf("Read %zu bytes: %.8s...\n", leaked.size(), leaked.c_str());
}
```

Compiled and run without ASan, this code most likely "looks fine": the `std::string` constructor dutifully copies 64 bytes starting from `buf.data()`, reading off all the unrelated bytes behind it on the stack, and the program doesn't crash. That's exactly what makes over-read terrifying.

Add `-fsanitize=address` and run it again—the picture changes. Run on my GCC 16.1.1:

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

Two details to notice. First, the error type is `stack-buffer-overflow`, occurring in `read_back` at line 11, which is exactly the `return std::string(buf.data(), n);` line, pinned to the source location—that's why you must compile with `-g`. Second, ASan even tells us the stack frame has two objects, `buf` occupying `[32, 40)` and `leaked` occupying `[64, 96)`, and the out-of-bounds read (offset 40) lands right between them. This level of detail at the scene is what fundamentally separates ASan from "sprinkle in some asserts and hunt slowly".

## So, what exactly does ASan do to pull this off?

### First: compile-time instrumentation (CTI)

ASan isn't a post-hoc profiler; it **rewrites your code at compile time**. When you add `-fsanitize=address`, the compiler (GCC or Clang) inserts extra check instructions around every memory access (every `*p`, every array subscript, every `memcpy`). This technique is called **compile-time instrumentation** (CTI), also known as static instrumentation.

Let's first verify it really "touches your code". Compile the `oob_read.cpp` above once without ASan and once with, and compare the **code segment (.text) size**, the actual machine instructions baked into the binary:

```text
Plain build  .text:   2792 bytes
ASan build   .text:  5736 bytes   (+105%)
```

(My GCC 16.1.1, `g++ -std=c++20 -O1 -g`, using `size` to read the `.text` segment.) That doubled size is the check code the compiler stuffs in around every memory access. One trap here: **don't compare the whole binary file size**—ASan's runtime library `libasan.so.8` is **dynamically linked** (`ldd` shows it) and isn't baked into the executable, so the whole file actually only grows about 5%; what really reflects the instrumentation volume is the `.text` code segment, and that's what doubles. The cost is larger size and slower execution, but next to the bugs it catches, that overhead is negligible during development. CTI is decided at **compile time**, so you must bring `-fsanitize=address` at **compile time**, and **at link time too**. If you add it only when compiling main but not when linking some third-party `.a`, the memory accesses inside that library go uninstrumented, and ASan is blind to that part of the code. The full flow:

```bash
g++ -std=c++20 -O1 -fsanitize=address -g -c a.cpp -o a.o     # compile with it
g++ -std=c++20 -O1 -fsanitize=address -g main.cpp a.o -o app  # link with it too
```

`-fsanitize=address` must appear in both compile and link phases; miss one and it does nothing.

### Second: shadow memory

Instrumentation alone isn't enough. The checks it inserts need a "ledger" to answer "can this address actually be accessed right now". That ledger is **shadow memory**.

The core idea is an elegant design: **use 1 byte of shadow memory to record the accessibility state of 8 bytes of real memory**. That is, ASan maps the entire process address space onto a contiguous shadow region in 8-byte groups, at a 1:8 ratio. To check whether an address is valid, you just compute its shadow byte and read it—no complex hash table to maintain.

The shadow byte values are printed directly at the end of ASan's reports. Look at the legend from a real run:

```text
Shadow byte legend (one shadow byte represents 8 application bytes):
  Addressable:           00
  Partially addressable: 01 02 03 04 05 06 07
  Heap left redzone:       fa
  Freed heap region:       fd
  Stack left redzone:      f1
  Stack mid redzone:       f2
```

That's the full semantics of the 1:8 mapping. `00` means all 8 bytes are accessible; `01`–`07` mean only the first few bytes are valid (e.g. `03` means the first 3 bytes are accessible and the last 5 aren't, used for partially-accessible tail regions after alignment); `fa` is the redzone around heap allocations—ASan secretly tucks a ring of "no-go zone" around every block of memory you `new`, and the moment you read into `fa`, that's a heap out-of-bounds; `fd` is already-`free`'d memory, and touching it is use-after-free; `f1`/`f2` are redzones for stack objects.

Look back at the shadow dump from the earlier error:

```text
=>0x72175e1f0000: f1 f1 f1 f1 00[f2]f2 f2 00 00 00 00 f3 f3 f3 f3
```

`00` is the body of `buf` (8 bytes, 1 shadow byte), and the `[f2]` right after it is the mid redzone between stack objects. The address of our out-of-bounds read lands exactly on this `f2`—ASan spots it at a glance. This is why the shadow memory mechanism is precise down to the byte.

### Third: runtime library + quarantine

Instrumentation and a shadow region still aren't enough; someone has to **fill this ledger**. The `new`/`delete`, `malloc`/`free` functions, the ASan runtime library (`libasan`) replaces them wholesale, with its own versions. On every allocation, the runtime paints redzones in the shadow region; on every free, it marks the corresponding shadow region as `fd`.

There's another key design here called **quarantine**. Freed memory isn't immediately returned to the system for reuse; ASan tosses it into a quarantine queue to sit for a while. Why? Because with use-after-free, if you `free` and it's immediately handed back out to someone else, that memory's shadow state flips back to `00`, and later mistaken reads won't be caught. Quarantine holds it for a while, ensuring the "freed" state can be hit by subsequent mistaken accesses.

Quarantine isn't unbounded, though—the queue has a cap, and once full the oldest freed memory is truly reclaimed in FIFO order. So ASan's detection of use-after-free isn't 100% either: if the quarantine window has already slid past and the memory has been reallocated, that particular mistaken read won't be caught. But combined with adequate test coverage, the vast majority of UAFs get nailed.

### The cost: 2-4x overhead, why it's still worth it

Add the three pieces up and ASan's typical overhead is **2-4x slowdown in runtime, 3-5x in memory** (the shadow region takes 1/8, plus redzones and quarantine). Sounds like a lot, but it depends on what you compare against.

The traditional memory-checking tool Valgrind (Memcheck) uses **dynamic binary instrumentation** (DBI): it doesn't recompile your program, instead at runtime it translates every machine instruction into its own intermediate representation, analyzes each one, then executes. High precision, no recompile, but the cost is a 20-50x slowdown. A test that originally took 1 second takes half a minute under Valgrind, which often makes it impossible to fold into daily CI.

ASan **front-loads the analysis cost to compile time** (CTI); at runtime it only does table lookups, so it can keep overhead at 2-4x. That magnitude means you can **leave ASan on permanently** in development and CI to run the full test suite, instead of remembering to manually fire Valgrind occasionally. This is ASan's most fundamental advantage over Valgrind: **it's affordable**.

::: warning Valgrind isn't installed on this machine
All the ASan/UBSan output in this article was really run on my machine (GCC 16.1.1 / Clang 22, WSL2). Valgrind isn't installed in this environment (`which valgrind` → not found), so this article doesn't show Valgrind output. If you need it, on Debian/Ubuntu just `apt install valgrind`; usage is in the valgrind section of vol1's [C dynamic memory management](../../vol1-fundamentals/c_tutorials/14-dynamic-memory.md). Nail down the essential difference between the two: **ASan is compile-time `-fsanitize=address` (CTI), Valgrind is runtime `valgrind ./prog` (DBI)**.
:::

## The tool family: five sanitizers, each minding one class

ASan is really just one member of a family. The toolset was first implemented by Google engineers as patches to GCC and Clang, and later became standard in mainstream compilers. The family has five members, each watching for a specific class of error:

| Tool | Compile switch | What it catches | Typical overhead |
|------|---------|--------|---------|
| **ASan** (AddressSanitizer) | `-fsanitize=address` | Out-of-bounds read/write, use-after-free, double-free, stack/global overflow | 2-4x slowdown |
| **LSan** (LeakSanitizer) | `-fsanitize=leak` | Memory leaks (unfreed heap memory at program exit) | Nearly zero overhead |
| **MSan** (MemorySanitizer) | `-fsanitize=memory` | Reads of uninitialized memory (use of uninitialized value) | ~3x slowdown |
| **TSan** (ThreadSanitizer) | `-fsanitize=thread` | Data races, deadlocks | 5-15x slowdown |
| **UBSan** (UndefinedBehaviorSanitizer) | `-fsanitize=undefined` | Undefined behavior (signed overflow, null-pointer dereference, shift out of range, etc.) | Configurable; most sub-checks are cheap |

Of the five, ASan is the workhorse and is almost mandatory for daily development; LSan is enabled by default together with ASan (on supported GCC/Clang environments); MSan is only fully usable under Clang and must be built with the **entire program** compiled as the MSan version (libc included, otherwise you drown in false positives); TSan is specifically for concurrency, covered in vol5's [Concurrency debugging techniques](../../vol5-concurrency/ch08-debug-testing-perf/01-debugging-concurrency.md); UBSan is "the finisher", cheap and composable with others.

### ASan and TSan are mutually exclusive: one iron rule

These five tools don't combine freely. The most important constraint: **ASan and TSan cannot be enabled at the same time**. ASan needs its own shadow-memory layout, TSan needs its own, and the two mechanisms clash. The compiler rejects you outright at compile time:

```text
$ g++ -std=c++20 -fsanitize=address,thread -g conflict.cpp -o conflict
cc1plus: error: '-fsanitize=thread' is incompatible with '-fsanitize=address'
```

The error is blunt. The engineering consequence: in a project's CI, memory-error detection and data-race detection need **two separate builds**—one with ASan, one with TSan—each running the tests once. vol5's TSan article covers this "dual build" practice in detail; here we just remember the conclusion.

As for MSan, it's incompatible with both ASan and TSan (it requires all code to "cleanly" go through its own uninitialized-tracking), and it only supports Clang, so it sees the least real-world use. LSan and UBSan are the two "all-match": LSan is nearly zero-overhead and can stay on permanently, and most UBSan sub-checks can run alongside ASan.

## Hands-on: ASan catches three classic errors

Theory alone isn't satisfying. Let's write one minimal example for each of the three classes of memory errors that trip up C++ the most, and let ASan catch them one by one. All three below are real runs from my machine.

### Heap use-after-free

Smart pointers block most UAFs, but as long as a project still has raw pointers and C-style APIs, you can never fully close this hole. A minimal example—free a `unique_ptr` and then read through the raw pointer it previously handed out:

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
    p.reset();              // free it here — raw immediately becomes a dangling pointer
    std::printf("Value read through dangling pointer: %d\n", *raw);   // use-after-free
}
```

Run with ASan:

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

This report is where ASan is most valuable. It doesn't just tell you "the read at line 12 is a use-after-free"; it also gives you **the two-history of that memory**: allocated by `make_unique` at `uaf.cpp:9`, freed by `reset` at `uaf.cpp:11`. Stare at those two lines and the bug's causal chain is complete, which is exactly the value of the quarantine + redzone mechanism: freed memory is marked `fd` instead of being reclaimed immediately, so later mistaken reads can hit it.

That `[fd]` in the shadow dump is the smoking gun:

```text
=>0x7a948abe0000: fa fa[fd]fa fa fa fa fa ...
```

`fd` = freed heap region. That's the payoff of ASan's "ledger".

### Global buffer overflow

Global/static variables get redzone protection too. An out-of-bounds access on a global array, ASan catches it just the same:

```cpp
// global_oob.cpp — global array out-of-bounds
// Build: g++ -std=c++20 -O1 -fsanitize=address -g global_oob.cpp -o global_oob
#include <cstdio>
int g[4] = {1, 2, 3, 4};
int main() { std::printf("g[5] = %d\n", g[5]); }
```

```text
==38356==ERROR: AddressSanitizer: global-buffer-overflow on address 0x63ca65acd074 ...
SUMMARY: AddressSanitizer: global-buffer-overflow global_oob.cpp:5 in main
```

The error type is clearly `global-buffer-overflow`. ASan encodes the three regions—stack, heap, global—with different redzones (`f1`/`f2` stack, `fa` heap, `f9` global), so you can tell at a glance which kind of storage the overflow happened on.

::: warning On the "Clang 11 needed for global OOB" claim
Some older material says "detecting global-variable overflow requires Clang 11 or later". The history: early ASan had incomplete redzone support for globals, and Clang 11 introduced ODR indicators (`-fsanitize-address-use-odr-indicator`) and other improvements that solidified global detection. But **today** (GCC 8.3+ / mainstream Clang) detection of global overflow is on by default and works out of the box; the example above on my GCC 16.1.1 was caught in one shot with default config. So this "version threshold" is obsolete for current toolchains—don't be misled by old material.
:::

### Leaks: LSan finalizes at exit

Finally, memory leaks. LSan works differently from the previous ones: it doesn't report errors during execution; instead, when `main` returns and the program is about to exit, it scans all the still-"alive" heap allocations and flags the ones with no references and no matching free. A minimal example that leaks on purpose:

```cpp
// leak.cpp — intentional leak
// Platform: host    Standard: C++20
// Build: g++ -std=c++20 -O1 -fsanitize=address -g leak.cpp -o leak
#include <cstdlib>
#include <cstdio>
int main()
{
    int* p = (int*)std::malloc(sizeof(int) * 4);  // grab some heap memory
    p[0] = 42;
    std::printf("ptr = %p\n", (void*)p);  // let the pointer escape, prevent the whole thing being optimized away
    // no free; the memory p points to leaks when the program exits
}
```

Run with ASan (GCC 16.1.1 / WSL2, LSan enabled by default with ASan):

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

Note: the report is printed **after** `main` returns—that's exactly LSan's "finalize at exit" behavior. vol1's [Dynamic memory management](../../vol1-fundamentals/ch12/02-new-delete.md) has an equivalent example you can compare against.

::: warning LSan's "silent exit" trap
On mainstream Linux (GCC 16.1.1 / Clang 22), LSan is enabled by default with ASan, and the example above is stably caught on my machine. But watch out for a real trap: **leaks are only scanned when the process exits normally**. If your program is killed by `SIGKILL`, or calls `_exit` bypassing the `atexit` hooks, or in some containers/sandboxes where LSan's exit hooks don't fire, the leak report **silently disappears**: the program looks "fine", but actually LSan never got the chance to scan.

How to deal: confirm the process exited via normal return; force it on with `ASAN_OPTIONS=detect_leaks=1` if needed; long-running services (that never exit) can't use LSan's "finalize at exit" model and need Valgrind massif or heap sampling instead. Don't assume "no report means no leak" from LSan.
:::

## UBSan: turning "undefined behavior" from silence into reports

With the ASan family's workhorse covered, let's look at the finisher, UBSan. C/C++ has a feature that spikes the blood pressure: **undefined behavior (UB)**. The compiler's attitude toward UB is "since the standard doesn't say what happens, I'll assume it doesn't happen and optimize freely". The consequence is that signed integer overflow, shift out of range, null-pointer dereference—these errors **often look like they're running fine**, until one day you enable `-O2`, switch compiler versions, and the optimizer, reasoning from "this won't overflow", makes an aggressive transformation and the program suddenly computes an absurd result.

UBSan's idea: insert a runtime check next to every operation that could produce UB; the moment one actually happens, print a `runtime error: ...` report immediately (by default it doesn't abort; you can configure it to). The overhead is small, and many sub-checks can live alongside ASan permanently.

A minimal example, cramming three classic UBs in at once:

```cpp
// ubsan.cpp — UBSan catches undefined behavior
// Platform: host    Standard: C++20
// Build: g++ -std=c++20 -O1 -fsanitize=undefined -g ubsan.cpp -o ubsan
#include <cstdio>
#include <limits>

int main()
{
    int arr[4]{1, 2, 3, 4};
    int idx = 10;
    std::printf("Out-of-bounds subscript arr[10] = %d\n", arr[idx]);   // subscript out of bounds

    int max = std::numeric_limits<int>::max();
    std::printf("Signed overflow: %d\n", max + 1);           // signed integer overflow

    int shift = 32;
    std::printf("Left shift by 32: %d\n", 1 << shift);        // shift amount >= width
}
```

Run with UBSan:

```text
ubsan.cpp:11:55: runtime error: index 10 out of bounds for type 'int [4]'
ubsan.cpp:11:16: runtime error: load of address 0x7ffe8a0525c8 with insufficient space for an object of type 'int'
ubsan.cpp:14:16: runtime error: signed integer overflow: 2147483647 + 1 cannot be represented in type 'int'
ubsan.cpp:17:42: runtime error: shift exponent 32 is too large for 32-bit type 'int'
```

All three UBs caught, pinned to `file:line:column`. The list of UBs UBSan covers is long; the common ones include:

- **Arithmetic**: signed integer overflow/underflow, divide by zero;
- **Shift**: shift amount negative or greater than/equal to width, left shift changing the sign bit;
- **Memory/pointer**: null-pointer dereference, misaligned memory access, object size mismatch (accessing through a pointer of the wrong type);
- **Array**: subscript out of bounds (`-fsanitize=bounds`, this overlaps with ASan's overflow detection but with a different focus: ASan looks at redzones, UBSan looks at compile-time-known array sizes).

UBSan's overhead depends on which sub-checks you enable. `-fsanitize=undefined` is a set of default sub-checks, most very light; the expensive one is `-fsanitize=integer` (unsigned overflow counts too, big overhead, many false positives, use cautiously in production). Daily recommendation: turn on `-fsanitize=undefined` together with ASan—low cost, high payoff.

## Choosing: facing a memory bug, which tool?

By now all five siblings have made an appearance. The question: when you're actually sitting in front of a weird bug, what order do you pick tools in? Let's locate by "symptom":

- **Crashes on first run / segfault / intermittent crashes**: turn on ASan first and reproduce the test. Overflow, UAF, double-free are the most common causes of segfaults, and ASan catches them all in one pass.
- **Intermittent wrong results / bizarre values crossing functions**: suspect UAF or a data race. Rule out UAF with ASan first; if ASan reports nothing, build a separate TSan version to check for data races (remember the two are mutually exclusive and can't run together).
- **Absurd computed values / behavior changes under `-O2`**: almost certainly UB, go straight to UBSan.
- **Reads "normal-looking" garbage values, behavior depends on uninitialized memory**: MSan (note: Clang only, requires whole-program compilation).
- **Process eats more and more memory / suspect a leak**: LSan (report at exit), or for long-running services use Valgrind massif / heap sampling.

One engineering practice: **keep two builds resident in CI**—`ASan+UBSan` in one, `TSan` in the other, run on every commit. The overhead is acceptable (ASan+UBSan is in the 2-4x range), and in exchange you pin down the most expensive class of bugs—"intermittent crashes after launch"—before they ever leave the door.

::: warning ASan isn't a silver bullet
ASan is powerful, but it has several unavoidable limitations you must keep in mind.

First, **it only catches paths that are actually executed**. CTI is runtime detection; code that doesn't run doesn't trigger checks. If your test coverage is insufficient and some overflow path is never triggered, ASan won't catch it, which is exactly why ASan needs to be combined with good test cases and even fuzzing: fuzzing is responsible for flushing out rare paths, and ASan is responsible for reporting errors on those paths the moment they occur.

Second, **it only catches memory-class errors**. Logic errors (wrong computation), concurrency errors (data races), integer-overflow-type UB—ASan doesn't touch: the last goes to UBSan, the second to TSan. Don't expect one flag to solve everything.

Third, **don't enable it in production**. A 2-4x slowdown and extra memory are disastrous under production load. ASan/UBSan/TSan are all **dev/test/CI-stage** tools; release builds must strip these flags.

Fourth, **it has false-positive boundaries**. Some custom stack-unwinding mechanisms (`swapcontext`, `vfork`) make ASan's shadow-region judgment go wrong and report false positives. The line `HINT: this may be a false positive if your program uses some custom stack unwind mechanism` in the report is a reminder of exactly this.
:::

Real memory safety is held by RAII, smart pointers, `std::span`, range-based `for`—the means that **make overflow and dangling pointers un-writable at the language level**; those are the topics of vol1 and vol3. The value of the ASan toolset is for the transition period: before you've replaced every raw pointer, before third-party C libraries are wrapped by modern facades, it's that "last line of defense" that surfaces lurking memory bugs at the development stage instead of letting them blow up at 3am in production.

## References

- [AddressSanitizer · google/sanitizers Wiki](https://github.com/google/sanitizers/wiki/AddressSanitizer) — official ASan writeup; the authoritative source for the shadow-memory mechanism, the 1:8 mapping, and the 2x overhead
- [Clang: AddressSanitizer](https://clang.llvm.org/docs/AddressSanitizer.html) — Clang-side ASan docs, including `-fsanitize-address-use-odr-indicator` and other global-detection evolution
- [Clang: ThreadSanitizer](https://clang.llvm.org/docs/ThreadSanitizer.html) — TSan docs; the source of ASan↔TSan mutual exclusivity (see vol5's concurrency debugging article)
- [Clang: UndefinedBehaviorSanitizer](https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html) — UBSan sub-check list and overhead
- [Valgrind User Manual](https://valgrind.org/docs/manual/manual.html) — DBI method and Memcheck/Helgrind, the 20-50x overhead comparison
