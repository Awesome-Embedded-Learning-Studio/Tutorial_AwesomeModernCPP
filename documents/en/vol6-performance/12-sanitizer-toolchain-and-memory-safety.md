---
title: 'Sanitizer Toolchain Overview: From -fsanitize to Kernel KASAN/KFENCE'
description: Comparing user-space `-fsanitize=address/memory/undefined/thread` with kernel-space KASAN/KMSAN/UBSAN/KCSAN/KFENCE side-by-side, we thoroughly explain the two approaches of "compile-time instrumentation vs. sampling" and the layered defense strategy of "debugging vs. production".
chapter: 6
order: 12
platform: host
difficulty: advanced
cpp_standard:
- 11
- 14
- 17
- 20
reading_time_minutes: 22
prerequisites:
- ASan 工具家族与内存安全:shadow memory、Heartbleed 与 sanitizer 选型
- Valgrind 与 ASan 对照:JIT 解释 vs 编译期插桩
related:
- ASan 工具家族与内存安全:shadow memory、Heartbleed 与 sanitizer 选型
- Valgrind 与 ASan 对照:JIT 解释 vs 编译期插桩
- 并发程序调试技巧(ThreadSanitizer)
- 动态内存管理(new/delete 与智能指针)
tags:
- host
- cpp-modern
- advanced
- 内存安全
- 调试
- 工具链
translation:
  source: documents/vol6-performance/12-sanitizer-toolchain-and-memory-safety.md
  source_hash: 6bb4d06235bfda3a14d218ee235d1291d222170d30d403fcd494c82997c838c2
  translated_at: '2026-06-25T13:30:42.897270+00:00'
  engine: anthropic
  token_count: 3783
---
# Sanitizer Toolchain Panorama: From `-fsanitize` to Kernel KASAN/KFENCE

> PS: This content has been migrated from notes taken during my university years and has been verified and cross-checked; user-mode sanitizers were run locally, while kernel-mode tools could not be run locally and are based on official kernel.org documentation. If there are any omissions, issues or PRs are welcome.

In the previous two articles, we dissected user-mode ASan / UBSan / MSan / TSan and Valgrind in great detail—how shadow memory keeps accounts, the differences between JIT interpretation and compile-time instrumentation, and why the five sanitizers are mutually exclusive. However, if you only focus on "just adding a flag with `g++ -fsanitize=address`", you will miss the bigger picture: **sanitizers are not exclusive to user mode; there is a full set of corresponding tools in the kernel**, and their design trade-offs are completely different.

In this article, we will look at the entire sanitizer toolchain on a level playing field. On one side we have user-mode `-fsanitize=*`, and on the other, kernel-mode `CONFIG_KASAN / CONFIG_KMSAN / CONFIG_KFENCE`—they catch the same class of bugs (out-of-bounds, use-after-free, uninitialized, data races), but under completely different constraints: user mode can slow down a program by 2-5x to catch bugs, but the kernel cannot. If the kernel slows down by 5x, the whole machine is effectively dead. Therefore, the kernel has evolved the path of "sampling"—KFENCE uses extremely low overhead to enable "always-on in production," coexisting in a layered fashion with heavy tools like KASAN that are "only enabled during debugging."

## Closing the Loop on User Mode

Before moving to the kernel, let's nail down the four user-mode sanitizer flags with real reports, so we can easily compare them with the kernel tools later. The detailed shadow memory principles and the Heartbleed story were thoroughly explained in the previous article; here, we only provide minimal reproducible code and real terminal outputs to facilitate matching "which flag corresponds to each bug."

The division of labor for the four flags in one sentence: `-fsanitize=address` (ASan, out-of-bounds/UAF/leaks), `-fsanitize=undefined` (UBSan, undefined behavior), `-fsanitize=memory` (MSan, uninitialized reads), `-fsanitize=thread` (TSan, data races).

### ASan: Catching Three Types of Errors at Once

Heap out-of-bounds, use-after-free, and memory leaks—ASan catches them all. We write minimal examples for each of the three errors separately (if we put them in one program, ASan will abort at the first error, so we won't see the latter two; hence, we split them up):

```cpp
// uaf.cpp —— 释放后使用(use-after-free)
#include <cstdio>
int main() {
    int* p = new int(7);
    delete p;
    printf("*p = %d\n", *p);   // p 已 delete,悬空
    return 0;
}
```

Compile with `g++ -std=c++20 -O0 -g -fsanitize=address -fno-omit-frame-pointer uaf.cpp -o uaf`, and run it:

```text
=================================================================
==118313==ERROR: AddressSanitizer: heap-use-after-free on address 0x72c9e1de0010 at pc 0x5d222d6ed26f bp 0x7ffc31d299a0 sp 0x7ffc31d29990
READ of size 4 at 0x72c9e1de0010 thread T0
    #0 0x5d222d6ed26e in main /tmp/sanit/uaf.cpp:6
    ...
SUMMARY: AddressSanitizer: heap-use-after-free /tmp/sanit/uaf.cpp:6 in main
```

`-g` enables source code locations like `uaf.cpp:5` in the report. This is the deciding factor for whether ASan is usable—without debug symbols, the report is just a string of addresses, rendering it useless. It also detects out-of-bounds access on the stack. Let's switch to a cross-function stack buffer:

```cpp
// stack_oob.cpp —— 栈缓冲越界
#include <cstdio>
void fill(char* p) {                 // 跨函数,检测能跨栈帧
    for (int i = 0; i <= 8; ++i) p[i] = 'A';  // 合法下标 0..7,8 越界
}
int main() {
    char buf[8];
    fill(buf);
    printf("done\n");
    return 0;
}
```

Running the binary built with the same flags:

```text
=================================================================
==119120==ERROR: AddressSanitizer: stack-buffer-overflow on address 0x6ec9ef2f0028 at pc 0x5f38ab644200 bp 0x7fff6db78e20 sp 0x7fff6db78e10
WRITE of size 1 at 0x6ec9ef2f0028 thread T0
    #0 0x5f38ab6441ff in fill(char*) /tmp/sanit/stack_oob.cpp:4
    #1 0x5f38ab64429d in main /tmp/sanit/stack_oob.cpp:8
    ...
Address 0x6ec9ef2f0028 is located in stack of thread T0 at offset 40 in frame
    #0 0x5f38ab644220 in main /tmp/sanit/stack_oob.cpp:6
```

Note that it doesn't just tell you that the bounds were exceeded; it also tells you that "this memory is the `buf` located at offset 40 within the `main` stack frame." The stack redzone even marks the ownership of the array on the stack. This demonstrates the power of shadow memory, which we deconstructed in detail in the previous article, so we won't expand on it here.

Memory leaks are handled by LeakSanitizer (LSan), which comes bundled with ASan. It performs a scan when the program exits:

```cpp
// leak.cpp —— 忘记 delete
#include <cstdio>
int main() {
    int* leak = new int(99);
    *leak = 100;
    printf("leak = %d (故意不 delete)\n", *leak);
    return 0;
}
```

```text
=================================================================
==118322==ERROR: LeakSanitizer: detected memory leaks

Direct leak of 4 byte(s) in 1 object(s) allocated from:
    #0 0x7c2b9dd2d341 in operator new(unsigned long) (/usr/lib/libasan.so.8+0x12d341)
    #1 0x609649f361ba in main /tmp/sanit/leak.cpp:4
```

The cost of ASan is substantial: the program runs two to five times slower, and memory usage increases by three to five times. Therefore, **we must remove `-fsanitize=address` for production builds**, enabling it only during debugging and testing. This constraint might sound trivial, but in the kernel world, the same "too much overhead" issue led to the creation of completely different tools—this is the origin of KFENCE, which we will discuss later.

### UBSan: Specialized for Undefined Behavior

ASan manages "whether this memory can be touched," while UBSan manages "whether this operation itself is valid." Signed integer overflow, out-of-bounds array access, null pointer dereference, and misaligned shifts are undefined behaviors (UB) in the C++ standard. They might not necessarily crash, but the results are unpredictable:

```cpp
// ub.cpp —— 三种 UB
#include <cstdio>
#include <cstdint>
int main() {
    int32_t big = 2147483647;   // INT32_MAX
    int32_t sum = big + 1;      // (1) 有符号加法溢出 → UB
    int arr[4] = {0,1,2,3};
    int idx = 10;
    int v = arr[idx];           // (2) 下标越界 → UBSan 的 bounds 检查
    printf("sum=%d v=%d\n", sum, v);
    return 0;
}
```

Use `g++ -std=c++20 -O0 -g -fsanitize=undefined ub.cpp -o ub` (defaults to recover, printing all instances of undefined behavior and then continuing):

```text
ub.cpp:6:13: runtime error: signed integer overflow: 2147483647 + 1 cannot be represented in type 'int'
ub.cpp:9:20: runtime error: index 10 out of bounds for type 'int [4]'
ub.cpp:9:9: runtime error: load of address 0x7fffed140f28 with insufficient space for an object of type 'int'
sum=-2147483648 v=0
```

Here is a real pitfall to watch out for first: **UBSan and ASan can be enabled together** (`-fsanitize=address,undefined`). Many people do this because one handles memory and the other handles arithmetic, making them complementary. However, UBSan defaults to "print and continue" (recover). If you prefer to abort on the first instance of UB (which is closer to production behavior), add `-fno-sanitize-recover=all`. Conversely, ASan aborts immediately upon detection, and this behavior cannot be changed.

### MSan: Uninitialized Reads, Clang Only

MSan detects "use of uninitialized values," a class of errors ASan cannot catch—the memory is valid, the access is valid, but the value is garbage. The catch is: **MSan is implemented only in Clang; GCC does not support this flag at all**:

```cpp
// msan.cpp —— 用了没初始化的变量
#include <cstdio>
int main() {
    int x;                     // 故意不初始化
    if (x)                     // 拿垃圾值做分支判断 → MSan 抓
        printf("x is truthy\n");
    else
        printf("x is zero\n");
    return 0;
}
```

GCC reports an error directly:

```text
$ g++ -std=c++20 -fsanitize=memory msan.cpp -o msan
g++: error: unrecognized argument to '-fsanitize=' option: 'memory'
```

Switching to Clang allows it to compile and run (`clang++ -std=c++20 -O0 -g -fsanitize=memory -fno-omit-frame-pointer msan.cpp -o msan`):

```text
==118932==WARNING: MemorySanitizer: use-of-uninitialized-value
    #0 0x58f3129f5677  (/tmp/sanit/msan+0xd7677)
    ...
SUMMARY: MemorySanitizer: use-of-uninitialized-value
```

> **Warning**: MSan has a hard constraint—**the entire program (including all linked libraries) must be compiled with MSan instrumentation**. If you directly link a non-instrumented `libc++` or a third-party library using `clang++ -fsanitize=memory`, you will get a bunch of false positives, because MSan treats values returned by libraries as uninitialized. Therefore, MSan is rarely used in practical projects; it usually requires "rebuilding the entire toolchain with MSan" to run cleanly. This was mentioned in the previous article, but I'm emphasizing it here because KMSAN on the kernel side has similar "full-link instrumentation" requirements.

As for TSan (ThreadSanitizer, for data races), it is mutually exclusive with ASan, has a 5–15x performance overhead, and specializes in catching concurrency bugs. The "Concurrent Programming Debugging Techniques" section in the concurrency volume has already covered this thoroughly, so we will just mark its position in the big picture here and avoid repetition.

## So, what about the kernel?

Once you have memorized the four user-space flags, here comes the real point of this article. **The kernel is also C code; it can also have out-of-bounds access, UAF, and data races. Can we just slap `-fsanitize=address` onto the kernel?**

The answer is: **Yes, and the kernel actually does this, but the cost is so high that you can only enable it during debugging.** This is KASAN—Kernel AddressSanitizer. Its underlying mechanism is the same as user-space ASan (shadow memory + compile-time instrumentation), but the kernel has its own constraints:

1. **Shadow memory occupies a large chunk of the kernel's virtual address space**. User-space ASan's shadow memory takes up "1/8 of the process address space", but the kernel carves out a significant segment of the kernel VAS directly (from `KASAN_SHADOW_START` to `KASAN_SHADOW_END`). On a 64-bit kernel, the address space is large enough (128 TB) to handle this; on 32-bit, it is much tighter, so early KASAN could only run on 64-bit, until Linus Walleij created a streamlined version for ARM-32 in 5.11.

2. **Every memory access in the system is instrumented**. The kernel is not a process; it is the underlying layer shared by all processes. Once KASAN is enabled, system-wide performance collapses immediately—which is why `CONFIG_KASAN` is only used for debugging the kernel and is never enabled in production kernels.

3. **It requires specific memory allocators**. The kernel uses SLAB or SLUB allocators. KASAN needs to plant red zones in allocators and mark freed pages as "poisoned" (`KASAN_SANITIZE_*`) to catch UAF/OOB immediately. This shares the same logic as user-space ASan intercepting `malloc/free`, just swapped for `kmalloc/kfree`.

The source notes say "KASAN applies to x86_64 and AArch64, 4.x and above", so we need to verify that version number. In reality, KASAN was merged into the mainline in **Linux 4.0** (initially supporting x86_64), with AArch64 following shortly after. **5.11** added the optimized version for ARM-32. The mechanism is correct, but don't just remember it vaguely as "4.x".

### What does KASAN look like? (Official report style)

What does a KASAN report look like? Based on the example structure in the kernel.org dev-tools/kasan documentation, replacing the official example's `kmalloc_oob_right` with a virtual `buggy_driver_write` (where fields and hierarchy correspond exactly to the official report), it looks roughly like this:

```text
==================================================================
BUG: KASAN: slab-out-of-bounds in buggy_driver_write+0x3e/0x60 [buggy]
Write of size 1 at addr ffff888006c42185 by task cat/1234

CPU: 0 PID: 1234 Comm: cat Tainted: G    B
Call Trace:
 dump_stack_lvl+0x49/0x63
 print_report+0x171/0x486
 kasan_report+0xb1/0x130
 buggy_driver_write+0x3e/0x60 [buggy]
 ...

Allocated by task 1234:
 kasan_save_stack+0x1e/0x40
 __kasan_kmalloc+0x81/0xa0
 kmalloc_trace+0x21/0x30
 buggy_driver_init+0x2a/0x60 [buggy]
 ...

The buggy address belongs to the object at ffff888006c42180
 which belongs to the cache kmalloc-8 of size 8
The buggy address is located 5 bytes inside of
 8-byte region [ffff888006c42180, ffff88800642188)
```

The report structure is nearly identical to userspace ASan: **it first states where it blew up (slab-out-of-bounds, out-of-bounds write, which driver function), then provides the allocation stack (who allocated this memory, which `kmalloc` call)**. The kernel report adds kernel-allocator-specific details like "which slab cache (`kmalloc-8`)" and "offset within the object." Once you understand userspace ASan reports, you can basically read kernel KASAN reports too.

## Panoramic Comparison: Userspace ↔ Kernel

Let's align both sides now. This table is the core of this article—the original note was an external PNG, so we've recreated it here in Markdown:

| Bug Caught | Userspace Flag | Kernel Tool | Kernel Merged Version | Production Ready? |
|-----------|----------------|------------|-----------------------|-------------------|
| OOB / UAF / Double Free | `-fsanitize=address` (ASan) | **KASAN** | 4.0 (x86_64) / 5.11 (ARM-32 optimization) | No, debug only |
| Uninitialized Read | `-fsanitize=memory` (MSan, Clang only) | **KMSAN** | Patches available from 5.16, **6.1** mainline fully usable, Clang 14.0.6+ only, x86_64 only | No, huge overhead |
| Undefined Behavior (overflow/shift/OOB) | `-fsanitize=undefined` (UBSan) | **UBSAN** | Merged in 4.5 | Partially (see below) |
| Data Race | `-fsanitize=thread` (TSan) | **KCSAN** | Merged in 5.8, sampling-based | No, debug only |
| Memory Leak | ASan includes LSan | **kmemleak** / eBPF `memleak` | kmemleak has existed for a long time | Careful, false positives |
| Sampling Memory Errors | (No userspace equivalent) | **KFENCE** | **5.12** | **Yes, on by default** |
| Access Pattern Analysis (not bug detection) | (None) | **DAMON** | **5.15** | Yes, designed for production |

There are a few key relationships in this table you must remember:

- **ASan ↔ KASAN**: The same shadow memory concept moved to the kernel, but the cost is a total system performance collapse, so it's debug-only.
- **MSan ↔ KMSAN**: Both are Clang-only, both require full instrumentation, and both have huge overhead. The KMSAN docs explicitly state "not intended for production use, because it drastically increases kernel memory footprint and slows the whole system down."
- **UBSan ↔ UBSAN**: Kernel UBSAN was merged in 4.5, and **some of its checks (like `CONFIG_UBSAN_BOUNDS`) are enabled by default in modern distro kernels** because the overhead is low—this is one of the few kernel sanitizers that can "stay on."
- **TSan ↔ KCSAN**: Note that TSan uses compile-time full instrumentation, while KCSAN is different—it is **sampling-based** (watchpoints), so the overhead is manageable. However, it detects data races by "chance sampling," not TSan's "theoretically guaranteed detection." Merged in mainline in 5.8 (the google/kernel-sanitizers repo explicitly says "in mainline since 5.8").

The original note marks KMSAN as "6.1 and above"—**this version number is correct**, don't get it wrong. KMSAN was maintained as a patch series by Google's Alexander Potapenko for years; by the end of 2021, it was still just a patch branch not in mainline (official kernel.org examples ran on a patched `5.16.0-rc3+` using the google/kmsan branch, not mainline). The Google official repo (google/kmsan) README states: "Linux 6.1+ contains a fully-working KMSAN implementation which can be used out of the box," meaning **fully usable in mainline from 6.1 onwards**. So KMSAN is the latest of this batch of kernel sanitizers to hit mainline. Don't confuse "5.16 patch branch works" with "6.1 mainline"—this is the most common misinterpretation of these version numbers.

## KFENCE: The Key Move to Production

KASAN's problem is obvious—it's debug-only. But what do you do when your company's production kernel hits a memory bug? You can't swap a production machine for a KASAN-enabled debug kernel to reproduce it; your service would be down long before that. What's really missing is a **memory error detector with low enough overhead to stay on all the time**.

Enter KFENCE (Kernel Electric-Fence), **merged into mainline in Linux 5.12**. Its approach is totally different from KASAN: instead of "checking every access," it uses **sampling**:

- KFENCE maintains a fixed-size object pool (default `CONFIG_KFENCE_NUM_OBJECTS=255`, each object uses 2 pages—one for the object, one as a guard page; object pages and guard pages are interleaved in the pool, so every object page is surrounded by guard pages; the whole pool is about 2 MiB by default).
- The kernel slab allocator (`kmalloc`) is **hooked by a sampling timer into the KFENCE pool**: KFENCE has a millisecond sampling interval (boot param `kfence.sample_interval`, configurable via `CONFIG_KFENCE_SAMPLE_INTERVAL`). The next `kmalloc` after each interval is "hooked" and handed to KFENCE.
- Once in the KFENCE pool, the allocation is placed between two guard pages—any out-of-bounds read/write hits a guard page, immediately triggering a page fault, and the kernel reports the precise error and allocation stack.
- On free, KFENCE marks the page as "inaccessible"; touching it again is a use-after-free, which also triggers an immediate report.

The cost of sampling is: **the vast majority of allocations never touch KFENCE**, so it misses most bugs—you have to run for a long time and let enough allocations flow through the pool to catch one. But the trade-off is **extremely low overhead** (officials say near-zero, production workloads barely notice it), making it the **first memory sanitizer that can stay on in a production kernel**. In fact, as long as the architecture supports it and SLAB/SLUB is on, KFENCE is on by default in many distros.

The original note said "KFENCE must run for a long time, but the overhead is low enough to run in production"—the mechanism description is correct. We've added the version number (5.12) and the keyword "sampling," and emphasized the engineering significance of "on by default." It replaces the older `kmemcheck` (which was deleted in 4.15 because the overhead was too high and conflicted with KFENCE's philosophy).

## DAMON: Another "Sampling" Path, But Not for Bugs

Speaking of "sampling," we should mention DAMON (Data Access MONitor), because philosophically it's in the same category as KFENCE—**don't track everything, sample representative data**. But DAMON isn't a sanitizer; it doesn't catch bugs, it **monitors memory access patterns**:

- **Merged into mainline in Linux 5.15**, its goal is to help developers (and the kernel itself) see "how the process is actually accessing memory," to optimize layout and guide reclamation.
- DAMON splits the target process's address space into equal-sized regions, **samples** several representative pages in each region, records access frequency, and forms a histogram. If a region is hot, it subdivides—this "smart zoom" allows it to run cheaply even on huge address spaces.
- The kernel component is the "producer" (producing access patterns), and userspace (or the kernel) is the "consumer." The consumer can even call `madvise()` based on patterns to change memory attributes—like advising the kernel to swap out confirmed cold data.

DAMON has three interfaces: the userspace `damo` tool (from awslabs/damo), sysfs under `/sys/kernel/mm/damon/admin/`, and a kernel API for kernel developers. The old debugfs interface is deprecated. Looking at KFENCE and DAMON together, you'll see that in the 5.12~5.15 wave, the kernel systematically used "sampling" to plug the gap left by "full instrumentation is too expensive"—KFENCE catches bugs, DAMON watches patterns, both can go to production.

## Three Layers of Defense: Placing Tools by Scenario

Putting userspace and kernel sanitizers together, the memory safety toolchain is actually a **layered defense in depth**, each with different overhead/coverage trade-offs:

::: tip Development Phase: Full Instrumentation, Catch Everything
For self-testing, CI, and fuzzing, **overhead is not an issue, coverage is paramount**. Userspace turns on `-fsanitize=address,undefined` (plus a separate run of `-fsanitize=thread`), and kernel debug builds turn on `CONFIG_KASAN` + `CONFIG_KCSAN` + `CONFIG_UBSAN`. This layer assumes bugs will be caught by full instrumentation, at the cost of slowing the program/system down several times—only acceptable in non-production environments.
:::

::: tip Testing/Staging: Sampling Instrumentation, Long Runs
For pre-production, canary releases, and long load tests, **we can't accept total system collapse, but need to run long enough to expose rare bugs**. This layer uses KFENCE—sampling, low overhead, always on, letting thousands of allocations flow through the guard page pool to catch those "once in ten thousand runs" OOB and UAF bugs. Userspace lacks a direct equivalent here (Valgrind is too slow, ASan too heavy), so KFENCE's engineering value on the kernel side is particularly prominent.
:::

::: tip Production: Default Lightweight Checks + Post-Mortem
For real production kernels, **only enable negligible-overhead checks**: KFENCE (on by default), lightweight UBSAN subsets like `CONFIG_UBSAN_BOUNDS`, plus DAMON for access pattern analysis. When accidents happen, rely on post-mortem tools—kernel oops logs, kdump/crash analysis, and eBPF's `memleak-bpfcc` to track unreleased allocations. This layer stops expecting "catching bugs on the spot" and focuses on "keeping enough evidence to investigate later."
:::

This layering explains why the kernel maintains both KASAN and KFENCE, which seem redundant—**the same bug (e.g., UAF) is caught by KASAN during development and by KFENCE in production**. The tools aren't redundant; the scenarios don't overlap. Userspace currently only has the first layer (development instrumentation) working well; the second and third layers lack tools as mature as the kernel's. This is why "totally solving memory safety in C++ userspace" is harder than in the kernel—the kernel has KFENCE as a production safety net; when userspace has a production UAF, often the only option is to wait for it to crash and inspect the core dump.

## By the Way: Static Analysis and Post-Mortem Tools

Besides these runtime sanitizers, both kernel and userspace have a set of tools that **don't run the code, but read code or logs**, mentioned in the original notes. We'll wrap them up briefly here without diving deep:

- **Static Analysis**: Kernel side has `sparse`, `smatch`, `Coccinelle`, `checkpatch.pl`; userspace has `clang-tidy`, `cppcheck`. They don't run code and have zero overhead, but can only catch "obviously problematic patterns," missing runtime-only UAF/OOB. They complement, not replace, sanitizers—static analysis enforces rules, sanitizers catch runtime issues.
- **Post-Mortem**: Kernel oops/panic logs, `kdump`/`crash` tools for dump analysis, `[K]GDB` debugging. These are forensic methods after the bug has exploded, a different stage from sanitizers that "catch bugs early."

Userspace C++ post-mortem analysis was seen once in the "Dynamic Memory Management" chapter using `-fsanitize=address` to report leaks on exit, and in "Concurrent Debugging Tips" using TSan to locate concurrent bugs post-mortem. The whole toolchain is a **one-stop shop: Development Sanitizer → Production Lightweight Checks → Post-Mortem Analysis**. If you miss any link, the corresponding class of bugs will bite you repeatedly at that stage.

## Summary

This article pulled the sanitizer toolchain from userspace to kernel space. Here are the key takeaways:

- Userspace has four flags with clear division: ASan (OOB/UAF/Leak), UBSan (Undefined Behavior), MSan (Uninitialized Read, Clang only), TSan (Data Race). Most are mutually exclusive (ASan/MSan/TSan can't run together), but UBSan can stack with ASan. Local GCC 16.1.1 / Clang 22 all ran successfully.
- Kernel has fully corresponding tools: KASAN (↔ASan, 4.0), KMSAN (↔MSan, 6.1+ mainline fully usable), UBSAN (↔UBSan, 4.5), KCSAN (↔TSan, 5.8). Mechanisms are similar, but constrained by the kernel, most are debug-only.
- **KFENCE (5.12) is the watershed**: Using "sampling + guard pages" to crush memory error detection overhead to production levels, on by default, filling the production gap left by KASAN.
- **DAMON (5.15)** follows the same sampling path but doesn't catch bugs; it monitors access patterns to guide memory optimization.
- The whole toolchain is a three-layer defense: Dev full instrumentation (KASAN/ASan) → Staging sampling (KFENCE) → Production lightweight checks + post-mortem (UBSAN subset/kdump).
- Two version numbers from the source notes have been verified: KFENCE version = 5.12 (source didn't label it, added); KMSAN source said "6.1 and above" is actually correct, verified against Google's official repo README confirming 6.1+ mainline usability—patches ran on 5.16 branch, but mainline merge was 6.1.

Next, we continue on the performance vs. correctness line to see how compiler optimization makes code faster without changing semantics—and when it "secretly" changes semantics, making your carefully crafted concurrent code run differently than you expected.

## Reference Resources

- [kernel.org: Kernel Address Sanitizer (KASAN)](https://www.kernel.org/doc/html/latest/dev-tools/kasan.html) — KASAN mechanisms, config options, and example reports
- [kernel.org: Kernel Memory Sanitizer (KMSAN)](https://www.kernel.org/doc/html/latest/dev-tools/kmsan.html) — KMSAN requires Clang 14.0.6+, x86_64 only, explicitly "not for production"
- [kernel.org: Kernel Electric-Fence (KFENCE)](https://www.kernel.org/doc/html/latest/dev-tools/kfence.html) — KFENCE sampling mechanism, `CONFIG_KFENCE_NUM_OBJECTS`, production readiness
- [kernel.org: UndefinedBehaviorSanitizer (UBSAN)](https://www.kernel.org/doc/html/latest/dev-tools/ubsan.html) — Kernel UBSAN sub-checks and overhead
- [kernel.org: Kernel Concurrency Sanitizer (KCSAN)](https://www.kernel.org/doc/html/latest/dev-tools/kcsan.html) — KCSAN watchpoint-based sampling race detection
- [kernel.org: DAMON](https://www.kernel.org/doc/html/latest/admin-guide/mm/damon/usage.html) — DAMON sysfs/schemes interfaces and access pattern monitoring
- [Clang: UndefinedBehaviorSanitizer](https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html) — Userspace UBSan sub-check list
- [Clang: MemorySanitizer](https://clang.llvm.org/docs/MemorySanitizer.html) — MSan full instrumentation requirements and usage
