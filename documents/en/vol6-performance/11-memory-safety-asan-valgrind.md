---
title: 'Valgrind vs. ASan Comparison: JIT Interpretation vs. Compile-time Instrumentation'
description: Break down the responsibilities of the Valgrind suite (Memcheck, Callgrind, Cachegrind, Helgrind/DRD, and Massif), compile and run six classic memory errors with ASan, and thoroughly explain the essential differences between "dynamic binary translation" and "compile-time shadow memory instrumentation.
chapter: 6
order: 11
platform: host
difficulty: advanced
cpp_standard:
- 11
- 14
- 17
- 20
tags:
- host
- cpp-modern
- advanced
- 内存安全
- 调试
- 内存管理
reading_time_minutes: 28
prerequisites:
- 动态内存管理（new/delete 与智能指针）
- C 语言动态内存（malloc/free 与 valgrind 速览）
related:
- ASan 工具家族与内存安全：shadow memory、Heartbleed 与 sanitizer 选型
- 并发程序调试技巧（TSan / Helgrind 深入）
- 动态内存管理
translation:
  source: documents/vol6-performance/11-memory-safety-asan-valgrind.md
  source_hash: 6cfba1678566f8d5e146ab1d149ee1297c8f9e5a6c0ef64c81c72a73090a073e
  translated_at: '2026-06-25T13:29:35.943066+00:00'
  engine: anthropic
  token_count: 5270
---
# Valgrind vs. ASan: JIT Interpretation vs. Compile-Time Instrumentation

> PS: This content has been migrated from the author's university notes. Key conclusions have been verified on the local machine using GCC 16.1.1 + Valgrind 3.25.1; if there are any omissions, issues or PRs are welcome.

Let's start with a scenario we've likely all encountered: a piece of C++ code runs perfectly fine locally, but crashes sporadically in production, or its memory RSS (Resident Set Size) climbs until it gets killed by the OOM Killer. You review the code, and the `new`/`delete` pairs look correct, and the out-of-bounds access is only off by a byte or two—you can't find the problem just by reading the code. For bugs like this, debugging by eye is hopeless; we must rely on tools to "see" every memory access.

In this article, we categorize memory error detection tools into two camps based on their "implementation route" and break them down. One camp is **Valgrind**—the veteran solution that wraps a "virtual CPU" around your program to interpret execution via JIT (Just-In-Time compilation); the other is **AddressSanitizer (ASan)**—a compile-time solution that inserts check code into your program and uses "shadow memory" for bookkeeping. The original notes only covered Valgrind and didn't mention ASan, yet the latter is the more commonly used path in modern engineering. This article fills that gap and puts the two approaches side by side for comparison.

## 1. Two Types of Memory Errors, and "Why Code Review Can't Find Them"

Before using the tools, let's clarify the "enemies" we are hunting. Memory errors generally fall into two categories, and the difficulty of catching them differs significantly:

**Type 1: Deterministic out-of-bounds / use-after-free / double-free.** The characteristic of these errors is "accessing an address that should not be accessed." They are dangerous but relatively easier to catch—as long as the tool can mark "which memory blocks are legal and which aren't," it can report the error the moment the boundary is crossed. `char buf[8]; buf[8] = 'x';` (off-by-one) and `free(p); return *p;` (dangling pointer) fall into this category.

**Type 2: Uninitialized reads / memory leaks.** These are more insidious. Uninitialized reads mean "the address is legal, but the value is garbage"—the program doesn't crash, it just silently calculates the wrong result. Memory leaks mean "the address is always legal, just never returned"—the program doesn't crash, but RSS slowly increases. You cannot catch these with a "legal address table" alone; you need a different mechanism: Valgrind maintains a "is this value initialized" tag for every byte, while ASan's leak detector (LSan) scans the heap at program exit to see if there are blocks "allocated but not pointed to."

The fundamental reason why code review fails is that both types of errors **depend on the runtime memory state**, not on the literal code syntax. Just looking at `*p`, you have no idea if the memory `p` points to is alive or dead, initialized or garbage. This is exactly why we need tools to "record" every allocation, every free, and every read/write—turning the runtime memory state into an audit-able ledger.

Regarding this "recording" task, Valgrind and ASan take two completely different implementation routes. Let's put the conclusion first, then break it down later.

| Dimension | Valgrind (memcheck) | AddressSanitizer |
|------|---------------------|------------------|
| How it records | Dynamic Binary Translation: Translates every machine instruction into a checked version at runtime | Compile-time instrumentation: Inserts check code before/after every memory access at compile time |
| Needs recompilation? | **No**, runs on existing binaries | **Yes**, must recompile with `-fsanitize=address` |
| Runtime overhead | 20~50x slower, 2x+ memory (official docs) | ~2x slower, ~3x memory |
| Platforms | Linux/macOS (FreeBSD/Solaris), x86/ARM, etc. | GCC/Clang/MSVC, all platforms including Windows |
| Catches uninitialized reads? | Yes, native (V-bit) | **No**, requires `-fsanitize=memory` (MSan, Clang only) |
| Catches stack overflow | Yes (with full `--tool=memcheck` suite) | Catches stack/global redzones by default; `detect_stack_use_after_return` catches stack-use-after-return |

Keep this table in mind. Next, starting from the "source of the pain," let's look at how the Valgrind path works.

## 2. Valgrind: Wrapping a "Virtual CPU" to JIT Interpret Your Program

### 2.1 What is it actually doing?

Valgrind is essentially a **dynamic binary translation (DBT) framework**. It isn't a normal checking library; it puts your entire program inside a "virtual CPU" to run. When you type `valgrind ./myprog`, what actually happens is: Valgrind intercepts every machine instruction of yours, **translates it on-the-fly** into a sequence of new instructions that "does the original work + records memory state," and only then executes it. So your program doesn't run directly on the CPU; it is "interpreted" inside Valgrind's core.

This is the source of its famous side effect—**20 to 50 times slower**, memory usage more than doubled. The Valgrind official manual states:

> Programs running under Valgrind run significantly more slowly, and use much more memory -- e.g. more than twice as much as normal under the Memcheck tool.

In other words: a program that runs in 1 second might take half a minute under memcheck. So Valgrind isn't for hanging onto during daily development; it's for when "this program really has a memory bug, and I'm dedicating a chunk of time to hunt it down."

This JIT interpretation architecture has a huge benefit, and it's the fundamental reason Valgrind hasn't been retired: **no recompilation needed**. You have a binary from ten years ago, where you can't even find all the source code, and you suspect it leaks—just type `valgrind ./old_binary` and it runs. ASan can't do this; ASan must recompile from source. This is the hardest distinction between the two routes.

### 2.2 The Five-Piece Suite: One Framework, Five Tools

The essence of Valgrind is "framework + tools." The core handles translation and scheduling, while the specific "what to record, what to report" is handed off to pluggable tools. Using `--tool=<name>` selects which pair of "checking glasses" to wear. Let's look at the core tools listed in the manual:

**Memcheck**—The memory error detector, Valgrind's default tool, and the one most people actually use when they say "using Valgrind to check memory." It catches the full set (from manual section 4.1): accessing memory that shouldn't be accessed (heap block overflow, stack top overflow, access after free), using uninitialized values, incorrect freeing (double-free, `malloc` with `delete` mismatches), `memcpy` source/destination overlap, passing "suspicious" negative sizes to allocation functions, passing 0 to `realloc`, alignment values that aren't powers of two, and memory leaks. In short: memcheck catches almost all common memory errors in C/C++ programs.

**Callgrind**—Call graph + cache/branch prediction profiler. It doesn't require special compile-time options (though `-g` is recommended) and writes analysis data to a file at the end of the run, which is then converted to a human-readable format using `callgrind_annotate`. Use it to locate "how many times a function was called and what the call graph looks like."

**Cachegrind**—Cache profiler. It simulates the CPU's I1/D1/L2 caches and precisely points out cache misses and hits in your program, telling you how many misses and instructions each line of code, function, or module generated. Use it when squeezing out cache performance.

**Helgrind and DRD**—Both are **thread error detectors**, catching data races, inconsistent lock ordering, and POSIX threads API misuse. The source notes described Helgrind as "still experimental," but this statement is **long outdated**—in the 2026 official manual, both Helgrind and DRD are officially listed as stable tools with independent chapters (Chapters 7 and 8), not experimental features. Incidentally, the source notes only mentioned Helgrind and **missed DRD**—they share the same goal (catching thread bugs) but use different algorithms; DRD is usually faster and has better support for certain scenarios (like many small objects, Boost.Thread, OpenMP). I covered practical TSan/Helgrind usage in Volume 5's [Concurrency Debugging Techniques](../vol5-concurrency/ch08-debug-testing-perf/01-debugging-concurrency.md), so I won't repeat it here. Just remember: "for thread bugs, look to helgrind/drd or the more modern TSan."

**Massif**—Heap profiler. Measures exactly how much heap memory your program consumes, giving you growth curves for heap blocks, management structures, and the stack. Use it to "slim down" a program or find the biggest RSS consumers.

> **An easily overlooked division of labor**: memcheck catches "correctness" (can this memory be accessed, is it initialized), while callgrind/cachegrind/massif catch "speed/quantity" (performance and usage). Newcomers often confuse them, thinking Valgrind is just for memory leaks—actually, that's just one tool's job. The performance profiling tools (callgrind/cachegrind/massif) are in a completely different track from ASan; ASan doesn't touch performance profiling.

### 2.3 Memcheck's Dual-Table Principle: A-bit and V-bit

How does memcheck manage to catch so many types of memory errors? The key lies in the two "shadow tables" it maintains, covering the entire process address space. Manual section 4.5 explains this clearly:

**Valid-Address Table (A-bit).** Every byte in the process address space corresponds to 1 bit, recording "whether this address is currently readable/writable." When memory is malloc'd, the A-bit marks those bytes as "valid"; when freed, the mark flips back to "invalid." When an instruction tries to read/write a byte, it checks the A-bit first—if invalid, it's an illegal access, and memcheck reports the error immediately. This layer catches: out-of-bounds, use-after-free, accessing unallocated areas.

**Valid-Value Table (V-bit).** Every byte in the process address space corresponds to 8 bits; each CPU register also corresponds to a bit vector. They record "whether this value has been initialized." Memory from malloc starts with all V-bits as "uninitialized"; once an instruction writes a definite value to it, the corresponding byte's V-bit flips to "initialized." The key design is: **V-bits "propagate" with the value**—if you read an uninitialized value from memory into a register, the V-bit moves to the register too; if you use it in calculation, the result's V-bit is also "uninitialized." However, memcheck doesn't report as soon as an uninitialized value is read; it only reports when "that value is used to influence program output or generate an address." This delay is intentional—to avoiding flooding the screen with false positives.

Looking at the two tables together: A-bit manages "is the address legal," V-bit manages "is the value clean." The former catches OOB/UAF, the latter catches uninitialized reads. Double-free and alloc-dealloc mismatches are caught by memcheck's own maintained ledger of "what allocator was used for this block."

The cost of this "accounting for every byte" mechanism explains the memory doubling mentioned earlier—the A-bit and V-bit themselves take up space.

## 3. ASan: Compile-Time Instrumentation + Shadow Memory

### 3.1 The Approach is Completely Reversed

ASan's implementation route is the exact opposite of Valgrind's. It **does not** wrap a virtual CPU around the program; instead, it **inserts check code into your program at compile time**. When you add `-fsanitize=address`, the compiler inserts a small piece of code before and after every memory read/write: this code checks a "shadow memory" table to determine if the access is legal, and if not, reports an error and aborts.

So ASan's checking is "the program checking itself," rather than "an external virtual CPU checking for it." This explains the huge difference in overhead between the two routes: ASan only adds a few instructions on the instrumented memory accesses, avoiding the cost of "translating the entire instruction stream," so it is **only about 2 times slower** (Valgrind is 20~50x); the price is that it must be recompiled, and checking only covers instrumented code—dynamically loaded third-party .so files compiled without ASan are out of its scope (Valgrind can handle them, as it intercepts everything at the instruction level).

### 3.2 Shadow Memory: 8 Bytes → 1 Byte Encoding

The core mechanism of ASan is shadow memory (for a full breakdown of shadow memory, see this volume's [ASan Tool Family](./10-asan-family-and-memory-safety.md), which also covers how it plugged vulnerabilities like Heartbleed). It maps the entire process address space into a shadow table in groups of 8 bytes—every 8 application bytes correspond to 1 shadow byte. The value of that shadow byte has a specific meaning; I'll paste the diagram generated locally here (the output below is real):

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

Let's translate the elegance of this encoding scheme:

- Shadow byte is `00`: All 8 bytes are accessible;
- It is `01`~`07`: Only the first N bytes are accessible; the rest are out-of-bounds red zones—this is exactly how ASan catches off-by-one errors. It surrounds every heap block, stack frame, and global variable with a ring of "red zones". The shadow bytes for these red zones are marked `fa`, `f9`, etc. Once you step into a red zone, the instrumentation code checks the shadow byte, sees that it is not "accessible", and reports an error immediately;
- It is `fd`: This memory block has been freed—accessing it again is a use-after-free, and it gets caught on the spot.

In other words, ASan does not account for address validity "byte-by-byte" like Memcheck does. Instead, it "lays out red zones around valid regions and uses them to define boundaries". This mechanism is extremely effective for out-of-bounds and UAF issues, but **it lacks V-bits**—so ASan cannot detect uninitialized reads. This gap must be filled by MSan (MemorySanitizer, `-fsanitize=memory`), and MSan is only implemented in Clang. **GCC up to version 16.1.1 does not support `-fsanitize=memory`** (tested locally, results in `unrecognized argument`). This is a genuine shortcoming of the ASan approach compared to Memcheck.

> **Warning**: ASan and other sanitizers have a "one at a time" relationship. `-fsanitize=address` and `-fsanitize=thread` (TSan) **cannot be enabled simultaneously**—they make different assumptions about shadow memory layout, and mixing them will lead to direct errors or abnormal behavior. Therefore, enable ASan when hunting memory errors, and enable TSan separately when hunting concurrent data races. Don't expect to "catch everything in one go". For how to check threading errors, see [Volume 5: Concurrency Debugging](../vol5-concurrency/ch08-debug-testing-perf/01-debugging-concurrency.md).

## IV. Let's Run It: Six Classic Errors and Real ASan Output

Explaining the theory isn't satisfying enough. We took the six classic errors from the source notes—which were "all screenshots, no source code"—rewrote them entirely in real code, and compiled and ran them locally using `g++ -std=c++20 -O0 -g -fsanitize=address,undefined` on GCC 16.1.1. Every output snippet below was **actually generated by running the code**, not manually fabricated.

First, let's wrap all six error types into a single program:

```cpp
// cases.cpp — 六类经典内存错误，逐个用 ASan 复现
// 编译: g++ -std=c++20 -O0 -g -fsanitize=address,undefined cases.cpp -o cases
// 运行: ./cases <1..6>   不传参则只跑内存泄漏
#include <cstdio>
#include <cstdlib>

// 1. 使用未初始化内存（ASan 抓不到，要 MSan）
int case_uninit() {
    int* p = (int*)malloc(sizeof(int));   // 内容是垃圾
    int v = *p;                            // 读到垃圾值，但地址合法
    free(p);
    return v;
}

// 2. use-after-free
int case_uaf() {
    int* p = (int*)malloc(sizeof(int));
    *p = 42;
    free(p);
    return *p;                             // 读已释放内存
}

// 3. 堆缓冲区越界（尾部读写）
int case_oob() {
    int* a = (int*)malloc(4 * sizeof(int)); // 只有 a[0..3]
    a[4] = 99;                              // 第 5 个元素越界
    int r = a[4];
    free(a);
    return r;
}

// 4. 内存泄漏（忘记 free）
void case_leak() {
    int* p = (int*)malloc(sizeof(int));
    *p = 7;                                 // 故意不 free
}

// 5. malloc 配 delete（分配/释放不匹配）
void case_mismatch() {
    int* p = (int*)malloc(sizeof(int));
    *p = 5;
    delete p;                               // malloc 该配 free
}

// 6. 双重释放
void case_double_free() {
    int* p = (int*)malloc(sizeof(int));
    free(p);
    free(p);                                // 第二次 free
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

Remember this compilation command, as we will use it for every case below: `g++ -std=c++20 -O0 -g -fsanitize=address,undefined cases.cpp -o cases`. We use `-g` so that the ASan report includes line numbers, and `-O0` to prevent the compiler from optimizing away our out-of-bounds accesses (at high optimization levels, a "write-then-read" pattern like `a[4]` might be folded; ASan can still catch it, but `-O0` is cleanest for debugging).

### 4.1 Using Uninitialized Memory — ASan's Blind Spot

Let's run case 1 first to see how ASan reacts:

```text
$ ./cases 1
uninit=-1094795586
```

**ASan stays silent**, and the program simply returns a garbage value (`-1094795586`). This highlights the limitation mentioned earlier: this memory address is valid (allocated via `malloc`), and ASan's shadow memory marks it as "accessible". Since it lacks the V-bit to determine "whether this value has been initialized", ASan misses this error. Memcheck can catch this (using the V-bit), but ASan cannot—to detect this, you would need MSan (`-fsanitize=memory`, Clang only). This represents a **substantive difference in capability** between the two approaches; neither is strictly "stronger"—they simply cover different domains.

### 4.2 use-after-free —— The red zone catches it immediately

Running case 2:

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

(I omitted irrelevant lines like the build-id above, but kept all the key information.) As you can see, ASan provides three pieces of information: **where this illegal read occurred** (`return *p` at line 20 in `case_uaf()`), **where this memory was freed** (line 19), and **where it was originally malloc'd** (line 17). Combined, the entire causal chain of "allocate → free → use" becomes clear at a glance. This is the result of the redzone mechanism combined with "setting shadow bytes to `fd` after free"—to ASan, that memory block is no longer "accessible" after `free`, so touching it triggers a report.

### 4.3 Heap Buffer Overflow — Tail Redzone

Running case 3 (`a[4]` is out of bounds, as `a` was only allocated for four ints):

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

`located 0 bytes after 16-byte region` — This memory region is 16 bytes (four `int`s), and the access point lands exactly on the **first byte immediately following its end**, which is the start of the trailing redzone. This is the principle behind how ASan catches off-by-one errors: a block returned by `malloc` is immediately followed by a ring of redzones. The shadow bytes for these redzones are `fa` (heap left redzone, though actually representing poisoned areas surrounding the heap block). Since `a[4]` falls into this redzone, the instrumentation checks the shadow byte, sees it is not `00`, and reports the error on the spot.

> **A point mentioned in the source notes that is easily misunderstood**: The source notes state that "Valgrind does not check statically allocated arrays." While this was true for older versions of Memcheck (historically, out-of-bounds access on stack/global arrays was a weak point for Memcheck), **this is not the case for ASan**. ASan places redzones around stack arrays and global variables (shadow bytes `f1`~`f3` are stack redzones, `f9` is a global redzone), and it is very effective at catching out-of-bounds access on stack arrays. Therefore, the conclusion that "static array out-of-bounds access cannot be detected" applies only to Valgrind, not ASan. Do not conflate the limitations of these two tools.

### 4.4 Memory Leaks — LSan Scans the Heap at Program Exit

Running case 4 (`./cases 4`, intentionally omitting `free`):

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

Note that this error is reported by **`LeakSanitizer`**, not ASan itself. LSan is the leak detector bundled by default with ASan. It scans the entire heap **when the program exits normally** to identify blocks that were allocated but are no longer pointed to by any pointer. It reports "definitely lost" from the "still reachable / definitely lost" classification. This approach is consistent with Memcheck's leak detection strategy (both scan the heap at exit), except that LSan is part of the ASan toolchain.

> **What about daemons?** By default, LSan only scans when the program calls `exit`. Long-running daemons or service processes do not exit on their own. In this case, you can send a signal to trigger an intermediate dump: use `ASAN_OPTIONS=abort_on_error=0:detect_leaks=1` with `kill`, or call the LSan `__lsan_do_leak_check()` API in your code to actively trigger a scan. The corresponding approach for Valgrind is to `kill` the memcheck process from another terminal to force output (a trick mentioned in the source notes).

### 4.5 malloc with delete — Allocation/Deallocation Mismatch

Running case 5:

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

`alloc-dealloc-mismatch (malloc vs operator delete)` — ASan records "who allocated it" for every allocation. When freeing, it checks the match; `malloc` paired with `delete` is a mismatch, triggering an immediate report. Memcheck detects the same category (manual section 4.2.5, "freed with an inappropriate deallocation function"), so their capabilities are aligned.

> **Platform Note:** This `alloc-dealloc-mismatch` check is **disabled by default** on Windows (MSVC's ASan), because `delete` and `free` are often effectively equivalent on Windows. It is enabled by default on Linux/macOS. If you find these errors are not caught on Windows, check `ASAN_OPTIONS=alloc_dealloc_mismatch=1`.

### 4.6 Double Free

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

`attempting double-free` — After the first `free`, the shadow bytes are set to `fd`. When we attempt to `free` the same address a second time, ASan detects that it is already in the `fd` state (freed) and immediately flags it as a double-free. It also helpfully informs us that "the previous free was on line 48".

### 4.7 Extra Bonus: Use-After-Return on the Stack

ASan can also catch something that has historically been difficult for memcheck to detect — **accessing a stack frame after it has returned** (the function has returned, but the caller still holds a pointer to a local variable inside that function). This feature must be explicitly enabled:

```cpp
// suar2.cpp
#include <cstdio>
static int* g = nullptr;
void stash() { int local = 0xc0ffee; g = &local; }  // 把局部变量地址存出去
int main() { stash(); return *g; }                   // local 已随 stash 返回而消失
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

Note that address `0x6da50b8f0020` is located very **early** in the process address space (not in the normal stack region). This is because with `detect_stack_use_after_return` enabled, ASan moves "local variables that might be targeted by escaped pointers" to a dedicated "fake stack". When the function returns, that region of the fake stack is marked as poisoned, and any subsequent access triggers a `stack-use-after-return` error (shadow byte `f5`). This option is disabled by default due to some overhead and a small number of false positives (see that HINT). However, bugs involving "stack memory still in use after a function returns" are extremely difficult to track down, so it is worth knowing this trick exists.

## 5. How to Use Valgrind: Feeding Those Errors to Memcheck

Now that we have covered the principles, let's take that same `cases.cpp` from Section 4 (this time compiled without `-fsanitize=`, just a normal build) and run it under Valgrind. We will see how Memcheck reports the same batch of errors—comparing the two reporting styles side-by-side makes the differences clear. The local machine uses Valgrind 3.25.1.

First, compile a clean version with `-g` (Valgrind doesn't need ASan's instrumentation, but it requires `-g` to report line numbers):

```bash
g++ -std=c++20 -g -O0 cases.cpp -o cases_plain

# 最常用：memcheck 全量查泄漏
valgrind --tool=memcheck --leak-check=full ./cases_plain 4

# 更狠：连 still reachable 也列出来 + 跟进子进程
valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --trace-children=yes ./cases_plain
```

Here are a few key parameters: `--leak-check=full` performs a full leak check (providing line numbers), `--show-leak-kinds=all` lists even "still reachable" blocks (blocks that still have pointers pointing to them and can theoretically be freed) (the older `--show-reachable=yes` is an alias for this, which still works but is deprecated), and `--trace-children=yes` follows child processes spawned by `fork`/`exec`. To switch tools, modify `--tool=`: options include `callgrind`, `cachegrind`, `helgrind`, `drd`, and `massif`.

### 5.1 Memcheck reports the same UAF like this

Running case 2 (the use-after-free from Section 4):

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

Notice the line numbers—`cases.cpp:20` for the read, `:19` for free, and `:17` for malloc. These are **exactly the same** as those reported by ASan in Section 4 (ASan also reported :20/:19/:17). It is the same bug; both tools pinpoint the exact same lines, just with different wording:

- ASan says `heap-use-after-free` + `located 0 bytes inside of 4-byte region`;
- memcheck says `Invalid read of size 4` + `Address ... is 0 bytes inside a block of size 4 free'd`.

memcheck also adds `Block was alloc'd at ... :17`. It relies on the A-bit ledger to record the "lifetime" of this memory (where it was allocated, where it was freed, and where it is now being read), providing the full causal chain in one go. This shares the same logic as ASan's three-part "allocated by / freed by" structure, just with different phrasing.

### 5.2 Leaks: LEAK SUMMARY vs. LSan

Running case 4 (intentionally not calling free):

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

`definitely lost: 4 bytes` corresponds to the `Direct leak of 4 byte(s)` from LSan (LeakSanitizer) in the previous section. Both tools "scan the heap upon program exit," but Memcheck categorizes leaks into four levels (`definitely lost`, `indirectly lost`, `possibly lost`, `still reachable`), which is more granular, whereas LSan defaults to reporting only `Direct` and `Indirect` leaks. The line number is `:34`, consistent with ASan.

> **Stop manually downloading and compiling source packages.** The installation process in the original notes was `tar -jxvf valgrind-3.12.0.tar.bz2 && ./configure && make && sudo make install`. Version `3.12.0` is from 2016—**ten years ago**. It has poor support for modern kernels and new CPU instructions (like newer AVX), and tends to throw errors when running newly compiled programs. Now, just use the distribution packages: `apt install valgrind` for Debian/Ubuntu, `dnf install valgrind` for Fedora/RHEL, or `pacman -S valgrind` for Arch. You'll get version 3.2x (3.25.1 on my machine).

## 6. How to Choose Between the Two Paths

After all this discussion, when should we actually use which tool? Here is a practical decision matrix:

**Default to ASan.** For daily development and memory error detection in CI pipelines, ASan is the first choice. It is fast (2x slowdown vs. 20–50x, which CI can tolerate), cross-platform (works on Windows, macOS, and Linux, and is supported by MSVC), and provides clean reports. In modern C++ projects, `-fsanitize=address,undefined` is practically the standard configuration for debug builds. We cover ASan catching leaks in [Dynamic Memory Management](../vol1-fundamentals/ch12/02-new-delete.md) in Volume 1, and TSan for concurrency debugging in Volume 5—these are all tools in this ecosystem.

**You must use Valgrind in these scenarios:**

1. **You only have the binary, no source code**, or recompiling is too expensive (e.g., a massive legacy project). ASan requires recompilation, while Valgrind can run on existing binaries.
2. **You need to catch uninitialized reads but only have GCC.** ASan lacks V-bits, and MSan is Clang-only—for GCC projects needing to catch uninitialized reads, Memcheck is the ready solution.
3. **You need performance profiling** (callgrind/cachegrind/massif). ASan has no equivalent for these; if you want to see cache misses, heap growth curves, or call graphs, Valgrind is the only game in town.
4. **You need full coverage, including third-party libraries not compiled with ASan.** Valgrind intercepts at the instruction level, so it can catch memory errors even in `.so` files without source code; ASan only covers instrumented code.

Conversely, **Valgrind can't do these, or does them poorly, so you need ASan**: catching stack/global buffer overflows (ASan's stack/global redzones are its strength), running fast (CI friendly), Windows platform (Valgrind has basically no Windows support), and catching `stack-use-after-return` (ASan has a dedicated fake stack mechanism).

To sum it up in one sentence: **ASan is the standard for "development phase," while Valgrind is the specialist for "hard-to-diagnose issues, performance, and legacy binaries."** They aren't replacements; they are complementary—many teams run ASan in CI as the daily gatekeeper, and only bring in Valgrind for a second opinion when ASan misses a weird bug.

## 7. Back to C++: Tools are a Safety Net, RAII is the Cure

After talking about tools for so long, we must bring the conversation back to this point: **No matter how strong these tools are, they are "post-mortem bug catchers," not "bug eliminators."** What truly makes memory errors disappear from the root is C++'s RAII and smart pointers.

Looking back at those six categories of errors, you will find they are **invariably built on "raw malloc/free and raw pointers"**:

- **Leaks?** Use `std::unique_ptr` / `std::vector`. Objects are automatically released when they go out of scope, so there's no chance to forget `free`.
- **Use-after-free?** The ownership semantics of smart pointers make "whether this memory is still valid" a compile-time constraint.
- **Double-free?** `unique_ptr` cannot be copied and nulls the source after a move, making a double-free physically impossible.
- **Out-of-bounds?** `std::vector` with `.at()` throws exceptions, and `std::span` carries bounds—don't use raw `[]` with manual lengths.

C-style `malloc`/`free` and raw pointers leave "when to release memory" and "who can access it" entirely to the programmer's memory—human brains inevitably fail at this, which is why we need "bookkeeping tools" like Valgrind and ASan as a safety net. The Modern C++ approach moves this bookkeeping **into the type system**: the resource lifecycle is bound to the object, and the compiler guarantees the release for you. This is the fundamental leap from "tools catching bugs" to "the language eliminating bugs," which is exactly what our [Dynamic Memory Management](../vol1-fundamentals/ch12/02-new-delete.md) chapter in Volume 1 is about.

However, this **does not** mean Modern C++ projects don't need ASan/Valgrind. As long as your code calls C libraries, uses `new`/`delete`, or touches third-party interfaces lacking RAII wrappers, there is still room for memory errors to slip in. So the correct approach is: **First, use RAII to eliminate 99% of memory errors while coding. Then, use ASan to catch the remaining 1% during testing. Finally, use Valgrind as the bottom line for the weirdest, hardest problems.** Three lines of defense, each indispensable.
