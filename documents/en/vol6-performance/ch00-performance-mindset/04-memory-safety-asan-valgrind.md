---
chapter: 0
cpp_standard:
- 11
- 14
- 17
- 20
description: Pull apart the responsibilities of the Valgrind quintet (memcheck/callgrind/cachegrind/helgrind+drd/massif),
  compile and run six classic memory errors under ASan for real, and explain the essential difference between
  the "dynamic binary translation" and "compile-time shadow-memory instrumentation" routes.
difficulty: advanced
order: 4
platform: host
prerequisites:
- Dynamic memory management (new/delete and smart pointers)
- C dynamic memory management (malloc/free and valgrind)
reading_time_minutes: 27
related:
- The ASan tool family and memory safety (shadow memory, Heartbleed, and sanitizer selection)
- Concurrency debugging techniques (TSan / Helgrind in depth)
- Dynamic memory management
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
  translated_at: '2026-07-06T14:30:00+00:00'
  engine: manual
  token_count: 6200
---
# Valgrind vs ASan: JIT interpretation vs compile-time instrumentation

> PS: This part is migrated from my college notes, and every key conclusion has been re-verified by actually compiling and running on this machine with GCC 16.1.1 + valgrind 3.25.1. If anything is still off, an Issue or PR is welcome.

Let's start with something we've probably all done: a piece of C++ code runs fine locally, goes to production, and either crashes intermittently or has its RSS climb until the OOM Killer takes it out. You go back and read the code, the `new`/`delete` pairs all look right, the overflow is only off by a byte or two, and reading the code tells you nothing. This kind of bug has no hope of being caught by eye; you need a tool to "see" every memory access.

What this article does is split the memory-error-catching tools into two camps by their implementation route, and run them both. One camp is **Valgrind**: the old-school JIT scheme that wraps a "virtual CPU" around your program and interprets it. The other is **AddressSanitizer (ASan)**: a scheme that inserts checking code into your program at compile time and accounts for memory with "shadow memory". The original notes only covered Valgrind and didn't mention ASan at all, which is exactly the route more commonly used in engineering today. This article fills that gap and puts the two routes side by side.

## 1. Two classes of memory errors, and "why reading the code doesn't help"

Before we reach for tools, let's sort out the "enemies" we're hunting. Memory errors fall roughly into two classes, and catching them is wildly different in difficulty.

**Class one: deterministic overflow / use-after-free / double-free.** The signature of these errors is "accessed an address that shouldn't be accessed". They're dangerous, but relatively easy to catch: as long as the tool can mark "which memory is legal and which isn't", the overflow is reported the moment it happens. An off-by-one like `char buf[8]; buf[8] = 'x';`, a dangling pointer like `free(p); return *p;`, all belong here.

**Class two: uninitialized reads / memory leaks.** These are sneakier. An uninitialized read is "the address is legal but the value is garbage"; the program doesn't crash, it just silently computes wrong. A memory leak is "the address stays legal, it just never gets returned"; the program doesn't crash either, RSS just climbs slowly. You can't catch these two with a "legal address table", you need another mechanism: Valgrind maintains a "has this value been initialized" flag for every byte, and ASan's leak detection (LSan) sweeps the heap at program exit looking for blocks that are "allocated but pointed to by no one".

The fundamental reason reading the code doesn't work is that both classes of errors **depend on runtime memory state**, not on the literal text of the code. Looking at `*p` alone, you have no idea whether the memory `p` points to at this moment is live or dead, initialized or garbage. That's exactly why we need tools to "record" every allocation, every free, every read/write, turning runtime memory state into an auditable ledger.

"Recording" is something Valgrind and ASan do via two completely different implementation routes. Let's put the conclusion up front and take them apart one by one.

| Dimension | Valgrind (memcheck) | AddressSanitizer |
|------|---------------------|------------------|
| How it records | Dynamic binary translation: at runtime, translates each machine instruction into a checked version | Compile-time instrumentation: at compile time, inserts checking code before and after every memory access |
| Recompile needed? | **No**, runs on a stock binary | **Yes**, must recompile with `-fsanitize=address` |
| Runtime overhead | 20-50x slower, 2x+ memory (official wording) | ~2x slower, ~3x memory |
| Platforms | Linux/macOS (FreeBSD/Solaris), x86/ARM, etc. | GCC/Clang/MSVC, all platforms, including Windows |
| Who catches uninitialized reads | memcheck natively (V-bit) | ASan **cannot**, needs separate `-fsanitize=memory` (MSan, Clang only) |
| Catches stack overflow | Yes (needs full `-tool=memcheck`) | Catches stack/global redzones by default, `detect_stack_use_after_return` catches stack-return-then-access |

Keep this table in mind. Next we start from "the source pain" and see how the Valgrind route works.

## 2. Valgrind: wrap a "virtual CPU" to JIT-interpret your program

### 2.1 What it's actually doing

Valgrind is essentially a **dynamic binary translation (DBT) framework**. It's not an ordinary detection library; it stuffs your entire program into a "virtual CPU" and runs it there. When you type `valgrind ./myprog`, what really happens is: Valgrind intercepts every one of your machine instructions, **just-in-time translates** it into a new sequence that "does the original work + incidentally records memory state", and only then executes. So your program isn't running directly on the CPU; it's being "interpreted" inside Valgrind's core.

That's the source of its famous side effect: **20 to 50 times slower**, and memory usage more than doubled. The official Valgrind manual says it outright:

> Programs running under Valgrind run significantly more slowly, and use much more memory -- e.g. more than twice as much as normal under the Memcheck tool.

Put it in perspective: a program that runs in 1 second might take half a minute under memcheck. So Valgrind isn't something you keep running during daily development; it's for "this program really has a memory bug, I'm setting aside time specifically to hunt it down".

This JIT-interpretation architecture has one huge advantage, and it's the fundamental reason Valgrind hasn't been obsoleted yet: **no recompilation needed**. You've got a binary from ten years ago whose source you can't even find all of, you suspect it leaks, `valgrind ./old_relic` and you're running. ASan can't do this; ASan must recompile from source. That's the hardest difference between the two routes.

### 2.2 The quintet: one framework, five tools

The essence of Valgrind is "framework + tools". The core handles translation and scheduling; the specifics of "what to record, what to report" go to a pluggable tool. `--tool=<name>` picks one, which is to say you pick a pair of "checking glasses". Let's have a look; the manual lists these core tools:

**Memcheck**: the memory error detector, Valgrind's default tool, and the one most people actually mean when they say "use Valgrind to check memory". Its full catch list (quoted from manual section 4.1) is: accessing memory you shouldn't (heap overflow, stack-top overflow, use-after-free), using uninitialized values, wrong frees (double-free, mismatched `malloc` with `delete`), `memcpy` source/dest overlap, passing "suspicious" negative sizes to allocation functions, `realloc` with 0, alignment not a power of two, and memory leaks. In one sentence: memcheck nets nearly all the commonest memory errors in C/C++ programs.

**Callgrind**: call graph + cache/branch prediction profiler. It doesn't need special compile-time options (but `-g` is recommended); at the end of the run it writes analysis data to a file, then you turn it into human-readable form with `callgrind_annotate`. Use it to find "which function gets called how many times, what the call relationships look like".

**Cachegrind**: cache profiler. It simulates the CPU's I1/D1/L2 caches, pinpoints exactly where cache hits and misses happen in your program, and can tell you how many misses and how many instructions each line, each function, each module produced. Use it when you want to press cache performance.

**Helgrind and DRD**: these two are both **thread error detectors**, catching data races, lock-order inconsistencies, and POSIX thread API misuse. The original notes called Helgrind "still experimental", and that claim **has been outdated for a long time**: in the 2026 official manual both Helgrind and DRD are formally listed stable tools, each with its own chapter (manual chapters 7 and 8), not experimental. Worth mentioning too: the notes only mentioned Helgrind and **missed DRD**: the two have the same goal (catching thread bugs) but different algorithms, and DRD is usually faster and handles some scenarios (lots of small objects, Boost.Thread, OpenMP) better. Thread-error hunting is covered in depth with TSan/Helgrind in vol5's [Concurrency debugging techniques](../../vol5-concurrency/ch08-debug-testing-perf/01-debugging-concurrency.md); this article won't repeat it, just remember "for thread bugs reach for helgrind/drd, or the more modern TSan".

**Massif**: heap profiler. It measures how much memory your program actually eats on the heap, giving you the growth curve of heap blocks, heap management structures, and the stack. Use it to "slim down" a program or find the big RSS consumers.

> **An easily missed division of labor**: memcheck catches "right or wrong" (can this memory be accessed, is it initialized), callgrind/cachegrind/massif catch "fast or slow / much or little" (performance and usage). Newcomers often conflate them and think Valgrind is for finding memory leaks, when that's only one tool's job (memcheck's). The performance-analysis tools (callgrind/cachegrind/massif) and ASan aren't even in the same race; ASan doesn't touch performance profiling.

### 2.3 memcheck's dual-table principle: A-bit and V-bit

How does memcheck catch so many kinds of memory errors? The key is that it maintains two "shadow tables" covering the entire process address space. Manual section 4.5 lays this out clearly.

**Valid-Address table (A-bit).** Every byte of the process address space has 1 bit recording "can this address currently be read or written". When you `malloc` a block, A-bit marks those bytes "valid"; when you `free`, it flips back to "invalid". When an instruction is about to read or write a byte, it first checks that byte's A-bit; if it says invalid, that's an illegal access and memcheck reports it on the spot. This layer catches: overflow, use-after-free, accessing unallocated regions.

**Valid-Value table (V-bit).** Every byte of the process address space has 8 bits; every CPU register also has a corresponding bit vector. They record "whether this value has been initialized yet". Freshly `malloc`'d memory has all V-bits "uninitialized"; once an instruction writes a defined value into it, the corresponding bytes' V-bits flip to "initialized". The key design is that **V-bits propagate with the value**: read an uninitialized value from memory into a register and the V-bit moves into the register too; do arithmetic on it and the result's V-bit is "uninitialized" as well. But memcheck doesn't report the moment it reads an uninitialized value; it only reports when that value is "used to affect program output, or used to compute an address". This delay is deliberate, to avoid a screen full of false positives.

Putting the two tables together: A-bit governs "is the address legal", V-bit governs "is the value clean". The former catches overflow/UAF, the latter catches uninitialized reads. Double-free and alloc-dealloc mismatch are caught via a ledger memcheck itself maintains, recording "which allocator was this memory requested from".

The cost of this "every byte accounted for" mechanism is the memory doubling mentioned earlier; A-bit and V-bit themselves take up space.

## 3. ASan: compile-time instrumentation + shadow memory

### 3.1 The idea is exactly reversed

ASan's implementation route is the reverse of Valgrind's. It does **not** wrap a virtual CPU around your program; instead, **at compile time** it inserts checking code into your program. You add `-fsanitize=address`, and the compiler inserts a small piece of code before and after every memory read/write: that code consults a "shadow memory" table, decides whether this access is legal, and if not, reports an error and aborts.

So ASan's checking is "the program checks itself", not "an outside virtual CPU checks for it". That explains the huge gap in overhead between the two routes: ASan only spends a few extra instructions on the instrumented accesses, with no "translate the whole instruction stream" cost, so it's **only about 2x slower** (Valgrind is 20-50x); the price is that you must recompile, and the checking covers only the instrumented code. Dynamically loaded third-party `.so` files not built with ASan are out of its reach (Valgrind can, because it intercepts at the instruction level across the board).

### 3.2 Shadow memory: the 8-byte to 1-byte encoding

ASan's core mechanism is shadow memory (a full teardown of shadow memory is in this volume's [ASan tool family](./03-asan-family-and-memory-safety.md), which also covers how it plugged Heartbleed-style over-read holes). It maps the process's entire address space into a shadow table in 8-byte groups, with every 8 application bytes corresponding to 1 shadow byte. The value of that shadow byte has a precise meaning; I'll paste the legend straight from a real run on my machine (the output that follows is real):

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

Translating the elegance of this encoding:

- Shadow byte `00`: all 8 bytes are addressable;
- `01`-`07`: only the first N bytes are addressable, the rest is overflow redzone. This is exactly how ASan catches off-by-one: it paves a "redzone" around every heap block, stack frame, and global variable, and the redzone's shadow bytes are marked `fa`/`f9` and friends. Step into the redzone, the instrumentation checks the shadow byte, sees it's not "addressable", and reports on the spot;
- `fd`: this memory has been `free`'d; any further access is use-after-free, caught red-handed.

In other words, ASan takes a different path from memcheck's "byte-by-byte address legality accounting": it paves redzones around legal regions and uses redzones to define boundaries. This mechanism is extremely effective for overflow and UAF, but **it has no V-bit**, so ASan cannot catch uninitialized reads. That gap has to be filled by MSan (MemorySanitizer, `-fsanitize=memory`), and MSan is Clang-only; **GCC still doesn't support `-fsanitize=memory` as of 16.1.1** (verified on this machine: `unrecognized argument`). That's a real shortcoming of the ASan route versus memcheck.

> **Pitfall warning**: ASan and the other sanitizers are in a "one class at a time" relationship. `-fsanitize=address` and `-fsanitize=thread` (TSan) **cannot be turned on together**: their assumptions about shadow-memory layout differ, and mixing them either errors out or behaves erratically. So turn on ASan when hunting memory errors, turn on TSan separately when hunting concurrency data races, and don't try to "one-shot" it. For how to hunt thread errors, see [vol5's concurrency debugging article](../../vol5-concurrency/ch08-debug-testing-perf/01-debugging-concurrency.md).

## 4. Run it: six classic errors, real ASan output

Theory alone isn't satisfying. We take the six classes of "screenshots only, no source" classic errors from the original notes, write them all out as real code, and compile and run them on this machine (GCC 16.1.1) with `g++ -std=c++20 -O0 -g -fsanitize=address,undefined`. Every output chunk below is something I **really ran**, not hand-fabricated.

First, pack all six errors into one program:

```cpp
// cases.cpp — six classic memory errors, each reproduced with ASan
// Build: g++ -std=c++20 -O0 -g -fsanitize=address,undefined cases.cpp -o cases
// Run:   ./cases <1..6>   no arg runs only the leak
#include <cstdio>
#include <cstdlib>

// 1. Using uninitialized memory (ASan can't catch this, needs MSan)
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
    return *p;                             // reading freed memory
}

// 3. Heap buffer overflow (tail read/write)
int case_oob() {
    int* a = (int*)malloc(4 * sizeof(int)); // only a[0..3]
    a[4] = 99;                              // 5th element, out of bounds
    int r = a[4];
    free(a);
    return r;
}

// 4. Memory leak (forgot to free)
void case_leak() {
    int* p = (int*)malloc(sizeof(int));
    *p = 7;                                 // deliberately don't free
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
    free(p);                                // second free
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

Note this build line, every case below uses it: `g++ -std=c++20 -O0 -g -fsanitize=address,undefined cases.cpp -o cases`. `-g` makes ASan reports carry line numbers; `-O0` keeps the optimizer from folding away our overflow access (at higher optimization levels, a "write-then-immediately-read" like `a[4]` may get folded; ASan still catches it, but `-O0` is cleanest for debugging).

### 4.1 Using uninitialized memory — ASan's blind spot

Run case 1 and watch ASan's reaction:

```text
$ ./cases 1
uninit=-1094795586
```

**ASan says nothing**, and the program returns a garbage value (`-1094795586`) normally. That's the shortcoming mentioned earlier: this memory address is legal (it came from `malloc`), ASan's shadow memory marks it "addressable", and there's no V-bit to judge "has this value been initialized". memcheck catches this error (via V-bit), ASan doesn't; to catch it you have to switch to MSan (`-fsanitize=memory`, Clang only). That's a **substantive capability gap** between the two routes; it's not about which is stronger, it's that each minds its own patch.

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

(I trimmed the build-id and other irrelevant lines above; all the key info is there.) ASan gives you three pieces: **where this illegal read happened** (line 20 of `case_uaf()`, the `return *p`), **where this memory was freed** (line 19), and **where it was originally `malloc`'d** (line 17). Put together, the whole causal chain of "allocate -> free -> access again" is in full view. That's the credit of the redzone mechanism plus "after `free` the shadow byte flips to `fd`": once freed, that memory is no longer "addressable" to ASan, and any further touch trips a report.

### 4.3 Heap buffer overflow — the tail redzone

Run case 3 (`a[4]` overflows; `a` only holds 4 ints):

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

`located 0 bytes after 16-byte region`: this memory is 16 bytes (4 ints), and the access lands exactly on the **first byte after its end**, i.e., the start of the tail redzone. That's how ASan catches off-by-one: right behind the block returned by `malloc` is a ring of redzone, whose shadow bytes are `fa` (heap left redzone, really poison around the heap block); `a[4]` falls into the redzone, the instrumentation checks the shadow byte, sees it isn't `00`, and reports on the spot.

> **A point the notes raise but is easy to misread**: the notes say "Valgrind doesn't check statically-allocated arrays". That's true for old memcheck (stack/global array overflow was historically a memcheck weak spot), but **ASan is different**: ASan paves redzones around stack arrays and global variables too (shadow bytes `f1`-`f3` are stack redzones, `f9` is the global redzone), and it catches stack array overflow cleanly. So "static array overflow can't be caught" holds for Valgrind, not for ASan. Don't conflate the two tools' limitations.

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

Note that the error is from **`LeakSanitizer`**, not ASan proper: LSan is the leak detector bundled with ASan by default, and it sweeps the entire heap **when the program exits normally**, pulling out blocks that are "allocated but pointed to by no one". What it reports is the "definitely lost" entry in the "still reachable / definitely lost" classification. This is the same leak-detection idea as memcheck (both sweep the heap at exit); LSan just happens to be part of the ASan toolchain.

> **What about daemons?** LSan by default only sweeps when the program `exit`s; a long-running daemon/service doesn't exit on its own. You can signal it to dump mid-run: `ASAN_OPTIONS=abort_on_error=0:detect_leaks=1` combined with `kill`, or use LSan's `__lsan_do_leak_check()` API to trigger a scan from inside the code. On the Valgrind side the equivalent move is to `kill` the memcheck process from another terminal so it prints its output (the notes mentioned this trick).

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

`alloc-dealloc-mismatch (malloc vs operator delete)`: ASan records for every allocation "who requested it", and at deallocation time compares; `malloc` paired with `delete` doesn't match, reported on the spot. memcheck catches the same class (manual 4.2.5 "freed with an inappropriate deallocation function"); the two sides are aligned.

> **Platform difference note**: this `alloc-dealloc-mismatch` check is **off by default on Windows** (MSVC's ASan, because on Windows `delete` and `free` are often effectively equivalent). Linux/macOS turn it on by default. If you're on Windows and notice this class of error isn't caught, check `ASAN_OPTIONS=alloc_dealloc_mismatch=1`.

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

`attempting double-free`: after the first `free`, the shadow byte flips to `fd`; on the second `free` of the same address, ASan sees it's already in `fd` state (freed) and rules it a double-free. It even helpfully tells you "the previous free was on line 48".

### 4.7 Bonus: stack use-after-return

ASan can also catch something memcheck historically had great trouble with: **a stack frame being accessed after it returns** (the function has returned, but the caller still holds a pointer to a local inside it). This has to be turned on explicitly:

```cpp
// suar2.cpp
#include <cstdio>
static int* g = nullptr;
void stash() { int local = 0xc0ffee; g = &local; }  // store a local's address outward
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

Note the address `0x6da50b8f0020`: it sits **very far forward** in the process address space (not the normal stack region), because with `detect_stack_use_after_return` on, ASan moves "locals that might be pointed to by escaping pointers" onto a dedicated "fake stack"; when the function returns, that fake-stack region is poisoned, and any further access reports `stack-use-after-return` (shadow byte `f5`). It's off by default because of some overhead and a few false positives (see that HINT). But this kind of "still using stack memory after the function returned" bug is extremely hard to track down, and it's worth knowing the trick exists.

## 5. Using Valgrind: feed the above errors to memcheck

With theory covered, let's feed the same `cases.cpp` from section 4 (this time built without `-fsanitize=`, plain compile) into valgrind and see how memcheck reports the same batch of errors, the two dialects face to face; only side-by-side comparison reads clearly. This machine uses valgrind 3.25.1.

First, build a clean version with `-g` (valgrind doesn't need ASan's instrumentation, but it does need `-g` to give line numbers in the report):

```bash
g++ -std=c++20 -g -O0 cases.cpp -o cases_plain

# Most common: full memcheck leak check
valgrind --tool=memcheck --leak-check=full ./cases_plain 4

# Go harder: list still-reachable too + follow child processes
valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --trace-children=yes ./cases_plain
```

A few key parameters: `--leak-check=full` does a full leak check (with line numbers); `--show-leak-kinds=all` lists even the "still reachable" blocks (blocks that still have a pointer pointing at them, that in theory could still be freed) (the older `--show-reachable=yes` is an alias, still works but is no longer recommended); `--trace-children=yes` follows child processes from `fork`/`exec`. To switch tools, change `--tool=`: `callgrind`, `cachegrind`, `helgrind`, `drd`, `massif`.

### 5.1 The same UAF, memcheck's report

Run case 2 (the same use-after-free from section 4):

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

Notice the line numbers: `cases.cpp:20` read, `:19` free, `:17` malloc, **exactly the same** as ASan reported in section 4 (ASan's side also had :20/:19/:17). The same bug, both tools locate it to the same lines; only the dialect differs:

- ASan says `heap-use-after-free` + `located 0 bytes inside of 4-byte region`;
- memcheck says `Invalid read of size 4` + `Address ... is 0 bytes inside a block of size 4 free'd`.

memcheck also adds `Block was alloc'd at ... :17`: its A-bit ledger records this memory's "life" (where it was allocated, where freed, now being read again), giving you the whole causal chain at once, the same idea as ASan's "allocated by / freed by" three-part report in two different wordings.

### 5.2 Leaks: LEAK SUMMARY vs LSan

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

`definitely lost: 4 bytes`, matching section 4's LSan `Direct leak of 4 byte(s)` on the ASan side. Both "sweep the heap at program exit"; memcheck just splits leaks into four tiers (`definitely lost / indirectly lost / possibly lost / still reachable`, finer-grained), while LSan by default reports only the `Direct` and `Indirect` tiers. The line number is again `:34`, matching ASan.

> **Don't go download the source tarball and compile it by hand.** The install flow the notes give is `tar -jxvf valgrind-3.12.0.tar.bz2 && ./configure && make && sudo make install`; `3.12.0` is from 2016, **ten years ago**, and it handles modern kernels/new CPU instructions (like recent AVX) poorly, so freshly built programs tend to throw all kinds of errors. These days just use the distro package: Debian/Ubuntu `apt install valgrind`, Fedora/RHEL `dnf install valgrind`, Arch `pacman -S valgrind`; what you get is a 3.2x version (this machine has 3.25.1).

## 6. How to choose between the two routes

With all that said, when do you use which? Here's a field-tested decision:

**Default to ASan.** For daily development and the memory-error detector hung in CI, ASan is the first choice: it's fast (2x slower vs 20-50x, CI can live with that), cross-platform (Windows/macOS/Linux all work, MSVC supports it too), and the reports are clean. In modern C++ projects, `-fsanitize=address,undefined` is almost the standard debug-build config. vol1's [Dynamic memory management](../../vol1-fundamentals/ch12/02-new-delete.md) covers ASan for leak hunting, and vol5's concurrency debugging covers TSan; both are tools on this same route.

**These scenarios demand Valgrind:**

1. **Binary only, no source**, or recompilation is too costly (a huge legacy project, say). ASan must recompile; Valgrind runs on a stock binary.
2. **You need to catch uninitialized reads, but you only have GCC**. ASan has no V-bit, and MSan is Clang-only; for a GCC-built project to catch uninitialized reads, memcheck is right there.
3. **You need performance profiling** (callgrind/cachegrind/massif). These tools have no ASan equivalent at all; cache misses, heap growth curves, and call graphs only come from the Valgrind suite.
4. **You need full coverage, including third-party libraries not built with ASan**. Valgrind intercepts at the instruction level and catches memory errors even in a sourceless `.so`; ASan only covers instrumented code.

Conversely, **these are things Valgrind can't do, or does poorly, and need ASan**: catching stack/global array overflow (ASan's stack/global redzones are a strength), running fast (CI-friendly), the Windows platform (Valgrind basically doesn't support Windows), catching stack-use-after-return (ASan has a dedicated fake-stack mechanism).

One-sentence summary: **ASan is the "development-phase" standard, Valgrind is the "weird bugs / performance / legacy binary" specialist.** They aren't a replacement relationship, they're complementary: many teams hang ASan in CI for daily gatekeeping, and turn to Valgrind for a second look when ASan can't pin down a weird problem.

## 7. Back to C++: tools are the safety net, RAII is the cure

After a whole article on tools, we have to pull the thread back: **no matter how strong these tools are, they "catch bugs after the fact", they don't "eliminate bugs".** What actually makes memory errors vanish at the root is C++'s RAII and smart pointers.

Looking back at those six errors, you'll see they're **all built on "raw malloc/free, raw pointers"**:

- Leaks? With `std::unique_ptr` / `std::vector`, the object frees itself when it leaves scope; there's simply no chance to forget `free`;
- use-after-free? Smart-pointer ownership semantics turn "can this memory still be used" into something the compiler can enforce;
- double-free? `unique_ptr` can't be copied, and after a move the source is nulled out, so a double is physically impossible;
- Overflow? `std::vector` with `.at()` throws an exception, `std::span` carries a bound; don't use raw `[]` with a hand-managed length.

C-style `malloc`/`free`/raw pointers throw "when memory gets freed, who can access it" entirely onto the programmer to remember, and the human brain inevitably gets this wrong, which is exactly why "accounting tools" like Valgrind and ASan exist as a safety net. Modern C++ moves this accounting **into the type system**: a resource's lifetime is bound to an object, and the compiler guarantees release for you. That's the fundamental leap from "tools catch bugs" to "the language eliminates bugs", which vol1's [Dynamic memory management](../../vol1-fundamentals/ch12/02-new-delete.md) is entirely about.

But this **does not** mean a Modern C++ project can do without ASan/Valgrind. As long as your code still calls C libraries, still uses `new`/`delete`, still touches third-party interfaces without RAII wrappers, memory errors still have a seam to slip through. So the right posture is: **first use RAII to eliminate 99% of memory errors at write time, then use ASan to surface the 1% that slips through during testing, and finally keep Valgrind as the safety net for the weirdest cases.** Three layers of defense, none optional.
