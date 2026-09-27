---
chapter: 0
cpp_standard:
- 11
- 14
- 17
- 20
description: Pull apart the responsibilities of the Valgrind quintet (memcheck/callgrind/cachegrind/helgrind+drd/massif), compile and run six classic memory errors under ASan for real, and nail down the essential difference between the two routes of dynamic binary translation and compile-time shadow-memory instrumentation.
difficulty: advanced
order: 4
platform: host
prerequisites:
- Dynamic Memory Management (new/delete and smart pointers)
- Dynamic Memory Management in C (malloc/free and a quick valgrind tour)
reading_time_minutes: 27
related:
- 'The ASan tool family and memory safety: shadow memory, Heartbleed, and sanitizer selection'
- Debugging Techniques for Concurrent Programs
- Dynamic Memory Management
tags:
- host
- cpp-modern
- advanced
- 内存安全
- 调试
- 内存管理
title: 'Valgrind vs ASan: JIT interpretation vs compile-time instrumentation'
translation:
  source: documents/vol6-performance/ch00-performance-mindset/04-memory-safety-asan-valgrind.md
  source_hash: 4a96815f8c688c7e6e9aa1058760e641068a3bf284de6778723c5c2652cafcfe
  translated_at: '2026-09-26T05:24:02+00:00'
  engine: anthropic
  token_count: 13000
---
# Valgrind vs ASan: JIT interpretation vs compile-time instrumentation

> PS: this part was migrated from notes I took back in college; every key conclusion has since been re-verified by actually compiling and running on this machine with GCC 16.1.1 + valgrind 3.25.1. If anything is still off, an Issue or PR is welcome.

Let's start with something most of us have done: a piece of C++ code runs perfectly locally, goes to production, and either crashes intermittently or has its memory RSS climb until the OOM Killer takes it out. You go back and read the code — the `new`/`delete` pairing all looks right, the overflow is off by maybe a byte or two — and reading alone tells you nothing. Bugs like this are hopeless to debug by eye; you need a tool to "see" every memory access.

What this article does is split the memory-error-catching tools into two camps by implementation route, and take both apart for a run. One camp is **Valgrind**: the veteran JIT scheme that wraps a "virtual CPU" around your program and interprets it. The other is **AddressSanitizer (ASan)**: a scheme that inserts checking code into your program at compile time and does its bookkeeping with "shadow memory". The original old notes covered only Valgrind and said nothing about ASan — yet that is precisely the route more commonly used in engineering today. This article fills that gap and puts the two routes side by side.

## 1. Two classes of memory errors, and why reading the code never shows them

Before we reach for the tools, let's sort out the "enemies" we're hunting. Memory errors fall roughly into two classes, and catching them differs wildly in difficulty.

**Class one: deterministic out-of-bounds / use-after-free / double-free.** The signature of these errors is "an address was touched that shouldn't have been". Dangerous, but comparatively easy to catch: as long as the tool can mark "which memory is legal and which isn't", the overflow gets reported the moment it happens. An off-by-one like `char buf[8]; buf[8] = 'x';`, a dangling pointer like `free(p); return *p;` — both belong here.

**Class two: uninitialized reads / memory leaks.** These are sneakier. An uninitialized read means "the address is legal, but the value is garbage" — the program doesn't crash, it just quietly computes wrong. A memory leak means "the address stays legal, it just never gets given back" — no crash either, RSS just slowly climbs. You can't catch either of these with a "legal-address table"; you need another mechanism: Valgrind keeps a per-byte "has this value been initialized yet" flag, and ASan's leak detection (LSan) sweeps the heap at program exit to look for blocks that are "allocated but pointed to by no one".

The root reason reading the code doesn't work is that both classes of errors **depend on the runtime memory state**, not on the literal text of the code. Look at `*p` alone and you have no idea whether, at this instant, the memory `p` points to is live or dead, initialized or garbage. That's exactly why we need tools to "record" every allocation, every free, every read and write — turning the runtime memory state into a ledger you can audit after the fact.

And on "recording", Valgrind and ASan take two completely different implementation routes. Conclusion first, teardown after.

| Dimension | Valgrind (memcheck) | AddressSanitizer |
|------|---------------------|------------------|
| How it records | Dynamic binary translation: at runtime, each machine instruction is translated into a checked version | Compile-time instrumentation: at compile time, checking code is inserted around every memory access |
| Recompile needed? | **No** — a stock binary runs as-is | **Yes** — must recompile with `-fsanitize=address` |
| Runtime overhead | 20-50x slower, 2x+ memory (the official wording) | ~2x slower, ~3x memory |
| Platforms | Linux/macOS (FreeBSD/Solaris), x86/ARM, etc. | GCC/Clang/MSVC, all platforms, including Windows |
| Who catches uninitialized reads | memcheck, natively (V-bit) | ASan **cannot** — you additionally need `-fsanitize=memory` (MSan, Clang-only) |
| Catches stack out-of-bounds | Yes (but needs the full `--tool=memcheck` setup) | Stack/global redzones by default; `detect_stack_use_after_return` catches access after the frame returns |

Keep this table in mind for now. Starting from "the pain at the source", let's first see how the Valgrind route works.

## 2. Valgrind: wrap a "virtual CPU" around your program and JIT-interpret it

### 2.1 What it actually does

Valgrind is in essence a **dynamic binary translation (DBT) framework**. It isn't an ordinary detection library — it stuffs your entire program into a "virtual CPU" and runs it there. When you type `valgrind ./myprog`, what really happens is: Valgrind intercepts each of your machine instructions, **just-in-time translates** it into a new sequence of instructions that "does the original work + incidentally records memory state", and only then executes it. So your program isn't running on the CPU directly; it's being "interpreted" inside Valgrind's core.

That's where its famous side effect comes from: **20 to 50 times slower**, with memory usage more than doubled. The official Valgrind manual says it outright:

> Programs running under Valgrind run significantly more slowly, and use much more memory -- e.g. more than twice as much as normal under the Memcheck tool.

Put it in perspective: a program that runs in 1 second might take half a minute inside memcheck. So Valgrind isn't something you keep attached during everyday development; it's for "this program really does have a memory bug, and I'm carving out time specifically to hunt it down".

This JIT-interpreting architecture has one huge advantage, and it's the fundamental reason Valgrind hasn't been obsoleted yet: **no recompilation needed**. You have a ten-year-old binary whose source you can't even fully find, you suspect it leaks — type `valgrind ./old_relic` and it just runs. ASan can't do that; ASan must be recompiled from source. This is the hardest difference between the two routes.

### 2.2 The quintet: one framework, five tools

The essence of Valgrind is "framework + tools". The core handles translation and scheduling; the specifics of "what to record, what to report" go to a pluggable tool. Which one `--tool=<name>` selects is which pair of "checking glasses" you put on. Let's have a look — the manual lists these core tools:

**Memcheck**: the memory error detector, Valgrind's default tool, and the one most people actually run when they say "check memory with Valgrind". Its full catch list (quoted from manual section 4.1): accessing memory you shouldn't (heap block overflow, top-of-stack overflow, access after free), using uninitialized values, invalid frees (double-free, mismatches like `malloc` with `delete`), `memcpy` with overlapping source and destination, "suspicious" negative sizes passed to allocation functions, `realloc` passed 0, alignment values that aren't powers of two, and memory leaks. In one sentence: memcheck nets nearly all the most common memory errors in C/C++ programs.

**Callgrind**: a call-graph + cache/branch-prediction profiler. It needs no special compile-time options from you (though `-g` is recommended); at the end of the run it writes the profile data to a file, which you then convert into human-readable form with `callgrind_annotate`. Use it to pin down "which function gets called how many times, and what the call relationships look like".

**Cachegrind**: a cache profiler. It simulates the CPU's I1/D1/L2 caches, pinpoints exactly where in your program cache misses and hits happen, and can tell you how many misses and how many instructions each line of code, each function, each module produced. Use it when you want to squeeze cache performance.

**Helgrind and DRD**: these two are both **thread error detectors**, catching data races, inconsistent lock ordering, and misuse of the POSIX thread APIs. The original notes described Helgrind as "still experimental" — a claim **long outdated**: in the 2026 official manual, both Helgrind and DRD are formally listed stable tools, each with its own chapter (manual chapters 8 and 9), not experimental features. A side note while we're here: the notes mentioned only Helgrind and **missed DRD** — the two share a goal (catching thread bugs) but use different algorithms, and DRD is usually faster and supports some scenarios better (lots of small objects, Boost.Thread, OpenMP, for instance). I covered hands-on TSan/Helgrind work for thread errors in volume 5's [Debugging Techniques for Concurrent Programs](../../vol5-concurrency/ch08-debug-testing-perf/01-debugging-concurrency.md); this article won't repeat it — just remember "for thread-class bugs reach for helgrind/drd, or the more modern TSan".

**Massif**: a heap profiler. It measures how much memory your program actually eats on the heap, giving you the growth curves of heap blocks, heap management structures, and the stack. Use it to "slim down" a program or find the big RSS consumers.

> **A division of labor that's easy to miss**: memcheck catches "right or wrong" (may this memory be accessed, is it initialized), callgrind/cachegrind/massif catch "fast or slow / much or little" (performance and usage). Newcomers often conflate them and assume Valgrind is a memory-leak checker — but that's just one tool's job, memcheck's. The performance-analysis tools (callgrind/cachegrind/massif) and ASan aren't even in the same race; ASan doesn't touch performance profiling.

### 2.3 memcheck's two-table principle: A-bit and V-bit

How does memcheck earn its grip on so many kinds of memory errors? The key is the two "shadow tables" it maintains, covering the entire process address space. Manual section 4.5 spells it out:

**The Valid-Address table (A-bit).** Every byte of the process address space gets 1 bit recording "may this address currently be read or written". `malloc` a block, and the A-bit marks those bytes "valid"; `free` it, and the mark flips back to "invalid". When an instruction is about to read or write some byte, its A-bit is consulted first; if it says invalid, that's an illegal access and memcheck reports it on the spot. This layer catches: out-of-bounds, use-after-free, and access to unallocated regions.

**The Valid-Value table (V-bit).** Every byte of the process address space gets 8 bits; every CPU register also gets a corresponding bit vector. They record "whether this value has been initialized yet". Freshly `malloc`'d memory has all V-bits "uninitialized"; once an instruction writes a defined value into it, the corresponding bytes' V-bits flip to "initialized". The key design point: **V-bits propagate along with the value**. Read an uninitialized value from memory into a register, and the V-bit moves into the register with it; do arithmetic on it, and the result's V-bit is "uninitialized" too. But memcheck doesn't report the moment it reads an uninitialized value — it reports only at the instant that value "gets used to influence program output, or to compute an address". The delay is deliberate, to avoid a screen full of false positives.

Put the two tables together and it clicks: A-bit governs "is the address legal", V-bit governs "is the value clean". The former catches out-of-bounds/UAF, the latter uninitialized reads. Double-free and alloc-dealloc mismatches are caught by yet another ledger memcheck keeps itself — "which allocator was this memory requested from" — checked at free time.

The cost of this "every byte accounted for" mechanism is the memory doubling mentioned earlier: A-bits and V-bits themselves take up space.

## 3. ASan: compile-time instrumentation + shadow memory

### 3.1 The idea is exactly reversed

ASan's implementation route is exactly the reverse of Valgrind's. It does **not** wrap a virtual CPU around your program; instead, **at compile time** it inserts the checking code into your program. You add `-fsanitize=address`, and the compiler puts a small piece of code around every memory read/write: that code consults a "shadow memory" table, decides whether this access is legal, and if not, reports the error and aborts.

So ASan's checking is "the program checks itself", not "an outside virtual CPU checks on its behalf". That explains the huge gap in overhead between the two routes: ASan spends only a few extra instructions on the instrumented accesses, with no "translate the whole instruction stream" cost, hence only **~2x slower** (Valgrind is 20-50x); the price is a mandatory recompile, and checking that covers only instrumented code — a dynamically loaded third-party .so not built with ASan is out of its reach (Valgrind can handle it, because it intercepts wholesale at the instruction level).

### 3.2 Shadow memory: the 8-byte to 1-byte encoding

ASan's core mechanism is shadow memory (for a full teardown of shadow memory see this volume's [ASan tool family](./03-asan-family-and-memory-safety.md), which also covers how it plugged over-read holes like Heartbleed back in the day). It maps the process's entire address space into a shadow table in 8-byte groups, with every 8 application bytes corresponding to 1 shadow byte. The value of that shadow byte has a precise meaning — here's the legend pasted straight from a run on my machine (the output below is real):

```text
Shadow byte legend (one shadow byte represents 8 application bytes):
  Addressable:           00
  Partially addressable: 01 02 03 04 05 06 07
  Heap left redzone:       fa
  Freed heap region:       fd
  Stack left redzone:      f1
  Stack mid redzone:       f2
  Stack right redzone:     f3
  Stack after return:      f5
  Stack use after scope:   f8
  Global redzone:          f9
  Global init order:       f6
  Poisoned by user:        f7
  Container overflow:      fc
  Array cookie:            ac
  Intra object redzone:    bb
  ASan internal:           fe
  Left alloca redzone:     ca
  Right alloca redzone:    cb
```

Let me unpack what makes this encoding clever:

- Shadow byte `00`: all 8 bytes are accessible;
- `01` through `07`: only the first N bytes are accessible, the rest is out-of-bounds redzone. This is exactly how ASan catches off-by-one: it paves a "redzone" ring around every heap block, stack frame, and global variable, and the redzone's shadow bytes are marked `fa`/`f9` and the like. Step into the redzone, the instrumented code checks the shadow byte, finds it isn't "accessible", and reports immediately;
- `fd`: this memory has been freed; any further access is use-after-free, caught on the spot.

In other words, ASan takes a different path: where memcheck accounts for address legality byte by byte, ASan paves redzones around the legal regions and lets the redzones define the boundaries. This mechanism is extremely effective for out-of-bounds and UAF, but **it has no V-bit**, so ASan cannot catch uninitialized reads. That gap has to be filled by MSan (MemorySanitizer, `-fsanitize=memory`), and MSan exists only in Clang — **GCC as of 16.1.1 still doesn't support `-fsanitize=memory`** (verified on this machine: `unrecognized argument`). That's a genuine shortcoming of the ASan route relative to memcheck.

> **Pitfall warning**: ASan and the other sanitizers are in a "one class at a time" relationship. `-fsanitize=address` and `-fsanitize=thread` (TSan) **cannot be enabled at the same time**: their assumptions about shadow-memory layout differ, and mixing them either errors out outright or behaves erratically. So enable ASan when hunting memory errors, enable TSan separately when hunting concurrency data races — don't try to "all-in-one" it. For how to hunt thread errors, see [the concurrency debugging chapter in volume 5](../../vol5-concurrency/ch08-debug-testing-perf/01-debugging-concurrency.md).

## 4. Hands-on: six classic errors, real ASan output

Theory alone isn't satisfying. Let's write out, as real code, all six classes of classic errors that the original notes presented as "screenshots only, no source", and compile and run them on this machine (GCC 16.1.1) with `g++ -std=c++20 -O0 -g -fsanitize=address,undefined`. Every output chunk below is something I **actually ran** — not hand-crafted.

First, pack all six error classes into one program:

```cpp
// cases.cpp — six classic memory errors, each reproduced with ASan
// Build: g++ -std=c++20 -O0 -g -fsanitize=address,undefined cases.cpp -o cases
// Run:   ./cases <1..6>   with no argument, only the leak runs
#include <cstdio>
#include <cstdlib>

// 1. Using uninitialized memory (ASan can't catch this; needs MSan)
int case_uninit() {
    int* p = (int*)malloc(sizeof(int));   // contents are garbage
    int v = *p;                            // reads a garbage value, but the address is legal
    free(p);
    return v;
}

// 2. use-after-free
int case_uaf() {
    int* p = (int*)malloc(sizeof(int));
    *p = 42;
    free(p);
    return *p;                             // reads freed memory
}

// 3. Heap buffer overflow (tail read/write)
int case_oob() {
    int* a = (int*)malloc(4 * sizeof(int)); // only a[0..3]
    a[4] = 99;                              // the 5th element is out of bounds
    int r = a[4];
    free(a);
    return r;
}

// 4. Memory leak (forgot to free)
void case_leak() {
    int* p = (int*)malloc(sizeof(int));
    *p = 7;                                 // deliberately not freed
}

// 5. malloc paired with delete (alloc/dealloc mismatch)
void case_mismatch() {
    int* p = (int*)malloc(sizeof(int));
    *p = 5;
    delete p;                               // malloc should pair with free
}

// 6. Double free
void case_double_free() {
    int* p = (int*)malloc(sizeof(int));
    free(p);
    free(p);                                // the second free
}

int main(int argc, char** argv) {
    if (argc < 2) { case_leak(); puts("done: leak only"); return 0; }
    switch (atoi(argv[1])) {
        case 1: printf("uninit=%d\n", case_uninit()); break;
        case 2: printf("uaf=%d\n", case_uaf()); break;
        case 3: printf("oob=%d\n", case_oob()); break;
        case 4: case_leak(); puts("done leak"); break;
        case 5: case_mismatch(); puts("done mismatch"); break;
        case 6: case_double_free(); puts("done double-free"); break;
        default: puts("usage: ./cases [1..6]"); break;
    }
    return 0;
}
```

Memorize this build line; every case below uses it: `g++ -std=c++20 -O0 -g -fsanitize=address,undefined cases.cpp -o cases`. `-g` is there so the ASan report carries line numbers; `-O0` keeps the optimizer from optimizing our out-of-bounds access away (at high optimization levels, a "write then immediately read" like `a[4]` may get folded — ASan still catches it, but `-O0` is cleanest while debugging).

### 4.1 Using uninitialized memory — ASan's blind spot

First run case 1 and watch ASan's reaction:

```text
$ ./cases 1
uninit=-1094795586
```

**ASan says nothing**, and the program returns a garbage value (`-1094795586`) normally. This is the shortcoming mentioned earlier: the memory address is legal (it came from `malloc`), ASan's shadow memory has it marked "accessible", and there is no V-bit to judge "has this value been initialized". memcheck catches this error (via V-bit), ASan doesn't; catching it means switching to MSan (`-fsanitize=memory`, Clang-only). This is a **substantive capability difference** between the two routes — not about which is stronger, but about each minding its own patch.

### 4.2 use-after-free — the redzone bites on the spot

Run case 2:

```text
$ ./cases 2
=================================================================
==44083==ERROR: AddressSanitizer: heap-use-after-free on address 0x799329de0010 ...
READ of size 4 at 0x799329de0010 thread T0
    #0 ... in case_uaf() /tmp/asand/cases.cpp:20
    #1 ... in main /tmp/asand/cases.cpp:56
    ...

0x799329de0010 is located 0 bytes inside of 4-byte region [0x799329de0010,0x799329de0014)
freed by thread T0 here:
    #0 ... in free ...
    #1 ... in case_uaf() /tmp/asand/cases.cpp:19
    ...

previously allocated by thread T0 here:
    #0 ... in malloc ...
    #1 ... in case_uaf() /tmp/asand/cases.cpp:17
    ...

SUMMARY: AddressSanitizer: heap-use-after-free /tmp/asand/cases.cpp:20 in case_uaf()
```

(I trimmed the build-id and other irrelevant lines above; every key piece is still there.) Look at the three chunks of information ASan hands you: **where this illegal read happened** (line 20 of `case_uaf()`, the `return *p`), **where this memory was freed** (line 19), and **where it was originally malloc'd** (line 17). Put the three together and the whole "allocate -> free -> access again" causal chain is in full view. That's the credit of the redzone mechanism plus "after `free` the shadow byte flips to `fd`": once freed, that memory is no longer "accessible" as far as ASan is concerned, and the next touch trips a report.

### 4.3 Heap buffer overflow — the tail redzone

Run case 3 (`a[4]` is out of bounds; `a` only holds 4 ints):

```text
$ ./cases 3
=================================================================
==44191==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x7288a7be0020 ...
WRITE of size 4 at 0x7288a7be0020 thread T0
    #0 ... in case_oob() /tmp/asand/cases.cpp:26
    ...

0x7288a7be0020 is located 0 bytes after 16-byte region [0x7288a7be0010,0x7288a7be0020)
allocated by thread T0 here:
    #0 ... in malloc ...
    #1 ... in case_oob() /tmp/asand/cases.cpp:25
    ...
```

`located 0 bytes after 16-byte region`: this block is 16 bytes (4 ints), and the access lands exactly on the **first byte past its end** — that is, the start of the tail redzone. That's the principle behind ASan catching off-by-one: immediately behind the block `malloc` returns is a ring of redzone, whose shadow bytes are `fa` (heap left redzone — really, poison laid around the heap block); `a[4]` falls into the redzone, the instrumented code checks the shadow byte, sees it isn't `00`, and reports on the spot.

> **A point the original notes raise but that's easy to misread**: the notes say "Valgrind doesn't check statically allocated arrays". That's true for old memcheck (stack/global array overflow was historically a memcheck weak spot), but **ASan is not like that**: ASan paves redzones around stack arrays and global variables too (shadow bytes `f1`~`f3` are stack redzones, `f9` is the global redzone), and it catches stack array overflow crisply. So the conclusion "static array overflow can't be caught" holds for Valgrind only, not for ASan. Don't conflate the two tools' limitations.

### 4.4 Memory leak — LSan sweeps the heap at program exit

Run case 4 (`./cases 4`, deliberately doesn't free):

```text
$ ./cases 4

=================================================================
==44296==ERROR: LeakSanitizer: detected memory leaks

Direct leak of 4 byte(s) in 1 object(s) allocated from:
    #0 ... in malloc ...
    #1 ... in case_leak() /tmp/asand/cases.cpp:34
    #2 ... in main /tmp/asand/cases.cpp:58
    ...

SUMMARY: AddressSanitizer: 4 byte(s) leaked in 1 allocation(s).
```

Note that this error comes from **`LeakSanitizer`**, not ASan proper: LSan is the leak detector bundled with ASan by default, and it sweeps the entire heap **when the program exits normally**, pulling out the blocks that are "allocated but pointed to by no pointer at all". What it reports is the "definitely lost" entry in the "still reachable / definitely lost" classification. This is the same leak-detection idea as memcheck's (both sweep the heap at exit); LSan just happens to be part of the ASan toolchain.

> **What about daemons?** LSan by default sweeps only when the program `exit`s, and a long-running daemon/service process doesn't exit on its own. In that case you can signal it to dump mid-run: `ASAN_OPTIONS=abort_on_error=0:detect_leaks=1` combined with `kill`, or use LSan's `__lsan_do_leak_check()` API to trigger a scan actively from inside the code. On the Valgrind side, the corresponding move is to `kill` the memcheck process from another terminal so it prints its output (the original notes mentioned this trick).

### 4.5 malloc paired with delete — alloc/dealloc mismatch

Run case 5:

```text
$ ./cases 5
=================================================================
==44300==ERROR: AddressSanitizer: alloc-dealloc-mismatch (malloc vs operator delete) ...
    #0 ... in operator delete(void*, unsigned long) ...
    #1 ... in case_mismatch() /tmp/asand/cases.cpp:42
    ...

0x71a3249e0010 is located 0 bytes inside of 4-byte region [0x71a3249e0010,0x71a3249e0014)
allocated by thread T0 here:
    #0 ... in malloc ...
    #1 ... in case_mismatch() /tmp/asand/cases.cpp:40
    ...
```

`alloc-dealloc-mismatch (malloc vs operator delete)`: ASan records for every allocation "which function requested it"; at deallocation it compares, `malloc` paired with `delete` doesn't match, reported on the spot. memcheck catches the same class (manual 4.2.5, "freed with an inappropriate deallocation function"); the two sides' capabilities are aligned here.

> **Platform difference note**: this `alloc-dealloc-mismatch` check is **off by default on Windows** (MSVC's ASan, because on Windows `delete` and `free` are often effectively equivalent). On Linux/macOS it's on by default. If you're on Windows and notice this class of error isn't being caught, try `ASAN_OPTIONS=alloc_dealloc_mismatch=1`.

### 4.6 Double free

Run case 6:

```text
$ ./cases 6
=================================================================
==44193==ERROR: AddressSanitizer: attempting double-free on 0x6d0d527e0010 in thread T0:
    #0 ... in free ...
    #1 ... in case_double_free() /tmp/asand/cases.cpp:49
    ...

0x6d0d527e0010 is located 0 bytes inside of 4-byte region [0x6d0d527e0010,0x6d0d527e0014)
freed by thread T0 here:
    #0 ... in free ...
    #1 ... in case_double_free() /tmp/asand/cases.cpp:48
    ...
```

`attempting double-free`: after the first `free`, the shadow byte flips to `fd`; when the same address is `free`'d a second time, ASan sees it's already in the `fd` state (freed) and rules it a double-free outright. It even thoughtfully tells you "the previous free was on line 48".

### 4.7 Bonus: stack use-after-return

ASan can also catch something memcheck historically had great trouble with: **a stack frame being accessed after it returns** (the function has returned, but the caller still holds a pointer to one of its locals). This one has to be enabled explicitly:

```cpp
// suar2.cpp
#include <cstdio>
static int* g = nullptr;
void stash() { int local = 0xc0ffee; g = &local; }  // stash the local's address outward
int main() { stash(); return *g; }                   // local died when stash returned
```

```text
$ g++ -std=c++20 -O0 -g -fsanitize=address suar2.cpp -o suar2
$ ASAN_OPTIONS=detect_stack_use_after_return=1 ./suar2
=================================================================
==44702==ERROR: AddressSanitizer: stack-use-after-return on address 0x6da50b8f0020 ...
READ of size 4 at 0x6da50b8f0020 thread T0
    #0 ... in main /tmp/asand/suar2.cpp:4
    ...

Address 0x6da50b8f0020 is located in stack of thread T0 at offset 32 in frame
    #0 ... in stash() /tmp/asand/suar2.cpp:3

  This frame has 1 object(s):
    [32, 36) 'local' (line 3) <== Memory access at offset 32 is inside this variable
HINT: this may be a false positive if your program uses some custom stack unwind mechanism ...
SUMMARY: AddressSanitizer: stack-use-after-return /tmp/asand/suar2.cpp:4 in main
```

Notice the address `0x6da50b8f0020`: it sits **far forward** in the process address space (not the normal stack region), because with `detect_stack_use_after_return` on, ASan moves the "locals that might be pointed to by escaping pointers" onto a dedicated "fake stack"; when the function returns, that fake-stack region is poisoned, and any further access reports `stack-use-after-return` (shadow byte `f5`). It's off by default because of some overhead and a few false positives (see that HINT). But this kind of "still using stack memory after the function returned" bug is brutally hard to track down, so it's worth knowing the trick exists.

## 5. Using Valgrind: feed those errors to memcheck

With the theory covered, let's stuff the very same `cases.cpp` from section 4 (this time built plainly, without `-fsanitize=`) into valgrind and see how memcheck reports the same batch of errors — the two dialects face to face, since only a side-by-side reads clearly. This machine uses valgrind 3.25.1.

First build a clean version with `-g` (valgrind doesn't need ASan's instrumentation, but it does need `-g` to put line numbers in the report):

```bash
g++ -std=c++20 -g -O0 cases.cpp -o cases_plain

# Most common: full memcheck leak check
valgrind --tool=memcheck --leak-check=full ./cases_plain 4

# Go harder: also list still-reachable blocks + follow child processes
valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --trace-children=yes ./cases_plain
```

A few key parameters: `--leak-check=full` does the full leak check (with line numbers); `--show-leak-kinds=all` lists even the "still reachable" blocks (blocks that still have a pointer to them, that in theory could still be freed — the older `--show-reachable=yes` is an alias for it, still works but is no longer recommended); `--trace-children=yes` follows child processes spawned via `fork`/`exec`. To switch tools, change `--tool=`: `callgrind`, `cachegrind`, `helgrind`, `drd`, `massif`.

### 5.1 The same UAF, memcheck's report

Run case 2 (the very use-after-free from section 4):

```text
$ valgrind --tool=memcheck --leak-check=full ./cases_plain 2
==453796== Memcheck, a memory error detector
...
==453796== Invalid read of size 4
==453796==    at 0x40011E9: case_uaf() (cases.cpp:20)
==453796==    by 0x4001377: main (cases.cpp:56)
==453796==  Address 0x4ee9080 is 0 bytes inside a block of size 4 free'd
==453796==    at 0x48529EF: free (vg_replace_malloc.c:989)
==453796==    by 0x40011E4: case_uaf() (cases.cpp:19)
==453796==  Block was alloc'd at
==453796==    at 0x484F8A8: malloc (vg_replace_malloc.c:446)
==453796==    by 0x40011CA: case_uaf() (cases.cpp:17)
uaf=42
...
==453796== ERROR SUMMARY: 1 errors from 1 contexts (suppressed: 0 from 0)
```

Watch the line numbers: `cases.cpp:20` read, `:19` free, `:17` malloc — **exactly the same** as ASan reported in section 4 (ASan's side also said :20/:19/:17). One bug, both tools locate it to the same lines; only the dialect differs:

- ASan says `heap-use-after-free` + `located 0 bytes inside of 4-byte region`;
- memcheck says `Invalid read of size 4` + `Address ... is 0 bytes inside a block of size 4 free'd`.

memcheck adds one more line, `Block was alloc'd at ... :17`: its A-bit ledger has recorded this memory's entire "life" (where it was requested, where it was freed, and now being read again), handing you the whole causal chain at once — the same idea as ASan's "allocated by / freed by" three-parter, in two different wordings.

### 5.2 Leaks: LEAK SUMMARY lined up against LSan

Run case 4 (deliberately not freed):

```text
$ valgrind --tool=memcheck --leak-check=full ./cases_plain 4
==453446== HEAP SUMMARY:
==453446==     in use at exit: 4 bytes in 1 blocks
==453446==   total heap usage: 3 allocs, 2 frees, 77,828 bytes allocated
==453446== 4 bytes in 1 blocks are definitely lost in loss record 1 of 1
==453446==    at 0x484F8A8: malloc (vg_replace_malloc.c:446)
==453446==    by 0x400123D: case_leak() (cases.cpp:34)
==453446==    by 0x40013B5: main (cases.cpp:58)
==453446== LEAK SUMMARY:
==453446==    definitely lost: 4 bytes in 1 blocks
==453446==    indirectly lost: 0 bytes in 0 blocks
==453446==      possibly lost: 0 bytes in 0 blocks
==453446==    still reachable: 0 bytes in 0 blocks
==453446== ERROR SUMMARY: 1 errors from 1 contexts (suppressed: 0 from 0)
```

`definitely lost: 4 bytes`, lining up against section 4's `Direct leak of 4 byte(s)` from LSan on the ASan side. Both "sweep the heap at program exit"; memcheck just splits leaks into four tiers (`definitely lost / indirectly lost / possibly lost / still reachable` — finer-grained), while LSan by default reports only the `Direct` and `Indirect` tiers. The line number is again `:34`, matching ASan.

> **Stop downloading the source tarball to build by hand.** The install flow the original notes give is `tar -jxvf valgrind-3.12.0.tar.bz2 && ./configure && make && sudo make install`. `3.12.0` is the 2016 release — **ten years ago** — and it handles modern kernels and newer CPU instructions (recent AVX, for instance) poorly, so freshly built programs tend to throw all kinds of errors. These days just use the distro package: Debian/Ubuntu `apt install valgrind`, Fedora/RHEL `dnf install valgrind`, Arch `pacman -S valgrind` — what you get is a 3.2x version (this machine has 3.25.1).

## 6. Choosing between the two routes

After all that: when exactly do you use which? Here's a field-tested decision:

**Default to ASan.** For daily development and the memory-error detector you keep hooked into CI, ASan is the first choice: it's fast (2x slower vs 20-50x, which CI can live with), cross-platform (Windows/macOS/Linux all covered, MSVC included), and its reports are clean. In modern C++ projects, `-fsanitize=address,undefined` is practically the standard debug-build configuration. Volume 1's [Dynamic Memory Management](../../vol1-fundamentals/ch12/02-new-delete.md) covers ASan for catching leaks, and volume 5's concurrency debugging covers TSan — both are tools on this same route.

**These scenarios demand Valgrind:**

1. **Binary only, no source**, or recompilation is too costly (a huge legacy project, say). ASan must recompile; Valgrind runs the stock binary as-is.
2. **You need to catch uninitialized reads but only have GCC**. ASan has no V-bit, and MSan is Clang-only; for a GCC-built project that needs to catch uninitialized reads, memcheck is right there.
3. **You need performance profiling** (callgrind/cachegrind/massif). These tools have no ASan counterpart at all; if you want cache misses, heap growth curves, and call graphs, the Valgrind suite is the only place to get them.
4. **You need wholesale coverage, including third-party libraries not built with ASan**. Valgrind intercepts at the instruction level, catching memory errors even inside a .so with no source; ASan covers only instrumented code.

Conversely, **these are the jobs Valgrind can't do, or does poorly, where you need ASan**: catching stack-array/global-array overflow (ASan's stack/global redzones are a strength), running fast (CI-friendly), the Windows platform (Valgrind basically doesn't support Windows), and catching stack-use-after-return (ASan has a dedicated fake-stack mechanism).

One-sentence summary: **ASan is the "development-phase" standard; Valgrind is the specialist clinic for "weird bugs / performance / legacy binaries".** They aren't a replacement relationship, they're complementary: plenty of teams hang ASan in CI for daily gatekeeping, and turn to Valgrind for a second look when a weird problem ASan can't catch shows up.

## 7. Back to C++: tools are the safety net, RAII is the cure

A whole article on tools, and at the end we must pull the thread back: **however strong these tools are, they "catch bugs after the fact" — they don't "eliminate bugs".** What actually makes memory errors vanish at the root is C++'s RAII and smart pointers.

Look back at those six error classes and you'll find them **all, without exception, built on "raw malloc/free, raw pointers"**:

- Leaks? With `std::unique_ptr` / `std::vector`, the object frees itself when it leaves scope — there's simply no chance to forget the `free`;
- use-after-free? Smart-pointer ownership semantics turn "may this memory still be used" into something the compiler can constrain;
- double-free? `unique_ptr` can't be copied, and a move nulls out the source pointer — a double is physically impossible;
- out-of-bounds? `std::vector` with `.at()` throws, and `std::span` carries its bounds; don't use raw `[]` with a hand-managed length.

C-style `malloc`/`free`/raw pointers throw "when does memory get freed, who may access it" entirely onto the programmer to remember, and the human brain inevitably gets this wrong — which is exactly why "bookkeeping tools" like Valgrind and ASan exist as the safety net. Modern C++'s idea is to **move that bookkeeping into the type system**: a resource's lifetime is bound tightly to an object, and the compiler guarantees the release for you. This is the fundamental leap from "tools catch bugs" to "the language eliminates bugs" — the entire subject of volume 1's [Dynamic Memory Management](../../vol1-fundamentals/ch12/02-new-delete.md).

But this does **not** mean a Modern C++ project can do without ASan/Valgrind. As long as your code still calls C libraries, still uses `new`/`delete`, still touches third-party interfaces without RAII wrappers, memory errors still have a seam to slip through. So the right posture is: **first use RAII to eliminate 99% of memory errors at the moment you write the code; then use ASan to catch the escaped 1% during testing; and finally keep Valgrind as the fallback for the strangest, hardest cases.** Three lines of defense — not one of them optional.
