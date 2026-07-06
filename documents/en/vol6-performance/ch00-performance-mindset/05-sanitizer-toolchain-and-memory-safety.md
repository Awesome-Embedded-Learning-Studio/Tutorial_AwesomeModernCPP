---
chapter: 0
cpp_standard:
- 11
- 14
- 17
- 20
description: Lay user-space -fsanitize=address/memory/undefined/thread and kernel-side
  KASAN/KMSAN/UBSAN/KCSAN/KFENCE out in one table; make sense of the "compile-time instrumentation
  vs sampling" routes and the "debug vs production" layered defense.
difficulty: advanced
order: 5
platform: host
prerequisites:
- The ASan tool family and memory safety (shadow memory, Heartbleed, and sanitizer selection)
- Valgrind vs ASan (JIT interpretation vs compile-time instrumentation)
reading_time_minutes: 20
related:
- The ASan tool family and memory safety (shadow memory, Heartbleed, and sanitizer selection)
- Valgrind vs ASan (JIT interpretation vs compile-time instrumentation)
- Concurrency debugging techniques (ThreadSanitizer)
- Dynamic memory management (new/delete and smart pointers)
tags:
- host
- cpp-modern
- advanced
- 内存安全
- 调试
- 工具链
title: 'The sanitizer toolchain panorama: from -fsanitize to in-kernel KASAN/KFENCE'
translation:
  source: documents/vol6-performance/ch00-performance-mindset/05-sanitizer-toolchain-and-memory-safety.md
  source_hash: fe551848e8d6a94c9ba4297b7baeedb42e5ed5ee646b6182ef1b9350893bee66
  translated_at: '2026-07-06T14:45:00+00:00'
  engine: manual
  token_count: 5400
---
# The sanitizer toolchain panorama: from -fsanitize to in-kernel KASAN/KFENCE

> PS: This part is migrated from my college notes and fact-checked; the user-space sanitizers were really run on this machine, while the kernel-side tools can't run here and rest on kernel.org's official docs. If anything is off, an Issue or PR is welcome.

The previous two articles took user-space ASan / UBSan / MSan / TSan and Valgrind apart in fine detail: how shadow memory does its accounting, where the JIT-interpretation and compile-time-instrumentation routes differ, why the five sanitizers are mutually exclusive. But if you stop at "add a `g++ -fsanitize=address` flag", you'll miss a bigger picture: **sanitizers aren't a user-space monopoly; the kernel has a whole parallel set of tools**, and the two sides' design tradeoffs are completely different.

What this article does is flatten the entire sanitizer toolchain and look at it as a whole. User-space `-fsanitize=*` on one side, kernel-space `CONFIG_KASAN / CONFIG_KMSAN / CONFIG_KFENCE` on the other; they hunt the same classes of bugs (overflow, use-after-free, uninitialized reads, data races), but under utterly different constraints. User-space can afford to slow a program 2-5x to catch bugs; the kernel can't, and once the kernel slows 5x the whole machine is toast. So the kernel side evolved the "sampling" route: KFENCE trades extremely low overhead for "can stay on in production all the time", coexisting in layers with the heavy "debug-only" KASAN.

## First, tie off the user-space side

Before walking into the kernel, let's nail down user-space sanitizers' four flags with real reports, so we can contrast them with the kernel later. The detailed shadow-memory mechanics and the Heartbleed story were taken apart in the previous article; here we just show the smallest reproducible code and real terminal output, handy for "which flag maps to which bug".

The four flags in one sentence each: `-fsanitize=address` (ASan, overflow/UAF/leaks), `-fsanitize=undefined` (UBSan, undefined behavior), `-fsanitize=memory` (MSan, uninitialized reads), `-fsanitize=thread` (TSan, data races).

### ASan: three errors in one pass

Heap overflow, use-after-free, memory leaks, ASan collects all three. Let's write each as a minimal example (in one program, ASan would abort at the first error and you'd miss the other two, so we split them):

```cpp
// uaf.cpp — use-after-free
#include <cstdio>
int main() {
    int* p = new int(7);
    delete p;
    printf("*p = %d\n", *p);   // p is deleted, dangling
    return 0;
}
```

Build with `g++ -std=c++20 -O0 -g -fsanitize=address -fno-omit-frame-pointer uaf.cpp -o uaf`, run it:

```text
=================================================================
==118313==ERROR: AddressSanitizer: heap-use-after-free on address 0x72c9e1de0010 at pc 0x5d222d6ed26f bp 0x7ffc31d299a0 sp 0x7ffc31d29990
READ of size 4 at 0x72c9e1de0010 thread T0
    #0 0x5d222d6ed26e in main /tmp/sanit/uaf.cpp:6
    ...
SUMMARY: AddressSanitizer: heap-use-after-free /tmp/sanit/uaf.cpp:6 in main
```

`-g` is what lets the report carry source locations like `uaf.cpp:5`; that's the watershed for whether ASan is usable at all: without debug symbols the report is just a string of addresses, basically useless. It catches stack overflow just the same; let's switch to a cross-function stack buffer:

```cpp
// stack_oob.cpp — stack buffer overflow
#include <cstdio>
void fill(char* p) {                 // cross-function, so detection crosses frames
    for (int i = 0; i <= 8; ++i) p[i] = 'A';  // legal indices 0..7, 8 overflows
}
int main() {
    char buf[8];
    fill(buf);
    printf("done\n");
    return 0;
}
```

Build with the same flags and run:

```text
=================================================================
==119120==ERROR: AddressSanitizer: stack-buffer-overflow on address 0x6ec9ef2f0028 at pc 0x5f38ab644200 bp 0x7fff6db78e20 sp 0x7fff6db78e10
WRITE of size 1 at 0x6ec9ef2f0028 thread T0
    #0 0x5f38ab6411ff in fill(char*) /tmp/sanit/stack_oob.cpp:4
    #1 0x5f38ab6429d in main /tmp/sanit/stack_oob.cpp:8
    ...
Address 0x6ec9ef2f0028 is located in stack of thread T0 at offset 40 in frame
    #0 0x5f38ab644220 in main /tmp/sanit/stack_oob.cpp:6
```

Notice it doesn't just tell you there's an overflow; it tells you "this memory is the `buf` at offset 40 in `main`'s stack frame": the stack redzone even labels which stack array the memory belongs to. That's the power of shadow memory, taken apart in detail in the previous article; we won't re-expand here.

Memory leaks go through ASan's bundled LeakSanitizer (LSan), which sweeps once at exit:

```cpp
// leak.cpp — forgot to delete
#include <cstdio>
int main() {
    int* leak = new int(99);
    *leak = 100;
    printf("leak = %d (deliberately not deleted)\n", *leak);
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

ASan's cost is real and tangible: the program runs 2-5x slower and uses 3-5x more memory. So **production builds absolutely must strip `-fsanitize=address`**; turn it on only for debug and test. This constraint sounds innocuous, but on the kernel side the same "overhead too high" directly forced a completely different tool into existence, which is where KFENCE comes from.

### UBSan: the UB specialist

ASan minds "can this memory be touched"; UBSan minds "is this operation itself legal". Signed integer overflow, array subscript out of bounds, null pointer dereference, misaligned shifts: these are undefined behaviors (UB) under the C++ standard, not necessarily crashing, but the results are unpredictable:

```cpp
// ub.cpp — three flavors of UB
#include <cstdio>
#include <cstdint>
int main() {
    int32_t big = 2147483647;   // INT32_MAX
    int32_t sum = big + 1;      // (1) signed addition overflow → UB
    int arr[4] = {0,1,2,3};
    int idx = 10;
    int v = arr[idx];           // (2) subscript out of bounds → UBSan's bounds check
    printf("sum=%d v=%d\n", sum, v);
    return 0;
}
```

Build with `g++ -std=c++20 -O0 -g -fsanitize=undefined ub.cpp -o ub` (recover by default, prints all UB and continues):

```text
ub.cpp:6:13: runtime error: signed integer overflow: 2147483647 + 1 cannot be represented in type 'int'
ub.cpp:9:20: runtime error: index 10 out of bounds for type 'int [4]'
ub.cpp:9:9: runtime error: load of address 0x7fffed140f28 with insufficient space for an object of type 'int'
sum=-2147483648 v=0
```

A real pitfall to flag here: **UBSan and ASan can be turned on together** (`-fsanitize=address,undefined`), and many people do, since one minds memory and the other minds arithmetic, complementary. But UBSan by default "prints and continues" (recover); if you want it to abort on the first UB (closer to production behavior), add `-fno-sanitize-recover=all`. ASan, by contrast, aborts on contact and you can't change that.

### MSan: uninitialized reads, Clang only

MSan catches "using an uninitialized value", a class ASan can't catch: the memory is legal, the access is legal, but the value is garbage. The catch is that **MSan is Clang-only; GCC doesn't support this flag at all**:

```cpp
// msan.cpp — using an uninitialized variable
#include <cstdio>
int main() {
    int x;                     // deliberately uninitialized
    if (x)                     // branch on a garbage value → MSan catches
        printf("x is truthy\n");
    else
        printf("x is zero\n");
    return 0;
}
```

GCC errors out:

```text
$ g++ -std=c++20 -fsanitize=memory msan.cpp -o msan
g++: error: unrecognized argument to '-fsanitize=' option: 'memory'
```

Switch to Clang and it compiles and runs (`clang++ -std=c++20 -O0 -g -fsanitize=memory -fno-omit-frame-pointer msan.cpp -o msan`):

```text
==118932==WARNING: MemorySanitizer: use-of-uninitialized-value
    #0 0x58f3129f5677  (/tmp/sanit/msan+0xd7677)
    ...
SUMMARY: MemorySanitizer: use-of-uninitialized-value
```

> **Pitfall warning**: MSan has a hard constraint, **the entire program (including every library it links) must be compiled with MSan instrumentation**. If you just `clang++ -fsanitize=memory` and link an uninstrumented `libc++` or third-party library, you get a flood of false positives, because MSan treats every value returned by the library as uninitialized. So MSan sees little real-project use; it usually takes "rebuild the entire toolchain with MSan" to run clean. The previous article covered this; we emphasize it again here because the kernel-side KMSAN has the same "whole-chain instrumentation" requirement.

As for TSan (data races), it's mutually exclusive with ASan, with 5-15x overhead, and is specifically for concurrency bugs; the "Concurrency debugging techniques" chapter in the concurrency volume already took it apart, so here we just mark where it sits on the panorama and don't repeat it.

## Now the question: what about the kernel?

With user-space's four flags memorized, next is what this article really wants to cover. **The kernel is C code too; it overflows, it use-after-frees, it has data races too; can we just slap `-fsanitize=address` onto the kernel?**

The answer: **yes, and the kernel does exactly that, but the cost is so high you can only turn it on for debugging**. That's KASAN, the Kernel AddressSanitizer. Under the hood it's the same machinery as user-space ASan (shadow memory + compile-time instrumentation), but the kernel has its own constraints:

1. **Shadow memory eats a big chunk of kernel virtual address space**. User-space ASan's shadow memory is "1/8 of the process address space"; the kernel just carves out a big segment of kernel VAS (`KASAN_SHADOW_START` to `KASAN_SHADOW_END`). On 64-bit kernels the address space is huge (128 TB), so it holds up; on 32-bit it's much tighter, which is why early KASAN only ran on 64-bit, until 5.11 when Linus Walleij landed a trimmed version for ARM-32.

2. **Every memory access in the machine gets instrumented**. The kernel isn't a process; it's the substrate shared by all processes. Once KASAN is on, whole-machine performance falls off a cliff, which is exactly why `CONFIG_KASAN` is only for debug kernels; production kernels never turn it on.

3. **It needs a specific allocator**. The kernel uses the SLAB or SLUB allocator, and KASAN has to plant redzones in the allocator and poison freed pages (`KASAN_SANITIZE_*`) to nab UAF/OOB on the spot. It's the same idea as user-space ASan intercepting `malloc/free`, just moved to `kmalloc/kfree`.

The original notes said "KASAN applies to x86_64 and AArch64, 4.x and above"; that version number needs checking. In fact KASAN landed in the mainline at **Linux 4.0** (initially x86_64), with AArch64 following, and **5.11** added the trimmed ARM-32 version. The mechanism is right, but don't remember it as a vague "4.x".

### What a KASAN report looks like (official style)

What does a KASAN report look like? Following the structure of the kernel.org dev-tools/kasan example, swapping its `kmalloc_oob_right` for a virtual `buggy_driver_write` (fields and hierarchy map exactly to the official report), it looks roughly like this:

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

It's almost identical in structure to a user-space ASan report: **first it says where it blew up (slab-out-of-bounds, an out-of-bounds write, in which driver function), then it gives the allocation stack (who allocated this memory, in which `kmalloc`)**. The kernel report adds kernel-allocator-specific info like "which slab cache it belongs to (`kmalloc-8`), at which byte of the object". Once you can read a user-space ASan report, you can basically read a kernel KASAN report.

## The panorama table: user-space ↔ kernel

Now align the two sides; this table is the heart of this article. The original notes had it as an external PNG; we redraw it in Markdown:

| Bug hunted | User-space flag | Kernel tool | Kernel version | Production-ok? |
|---------|-----------|---------|------------|----------|
| Overflow / UAF / double-free | `-fsanitize=address` (ASan) | **KASAN** | 4.0 (x86_64) / 5.11 (ARM-32 trimmed) | No, debug only |
| Uninitialized reads | `-fsanitize=memory` (MSan, Clang only) | **KMSAN** | usable on patch branches from 5.16, fully in mainline from **6.1**, Clang 14.0.6+ only, x86_64 only | No, huge overhead |
| Undefined behavior (overflow/oob/shift) | `-fsanitize=undefined` (UBSan) | **UBSAN** | landed in 4.5 | Some sub-checks yes (see below) |
| Data races | `-fsanitize=thread` (TSan) | **KCSAN** | landed in 5.8, sampling-based | No, debug only |
| Memory leaks | ASan bundles LSan | **kmemleak** / eBPF `memleak` | kmemleak has long existed | Cautiously, has false positives |
| Sampled memory errors | (no user-space equivalent) | **KFENCE** | **5.12** | **Yes, on by default** |
| Access-pattern analysis (not bug detection) | (none) | **DAMON** | **5.15** | Yes, designed for production |

A few correspondences in this table you must remember:

- **ASan ↔ KASAN**: the same shadow-memory idea moved into the kernel, at the cost of whole-machine performance collapsing; debug-only.
- **MSan ↔ KMSAN**: both Clang-only, both need whole-chain instrumentation, both with huge overhead. The KMSAN docs say outright: "not intended for production use, because it drastically increases kernel memory footprint and slows the whole system down".
- **UBSan ↔ UBSAN**: kernel UBSAN landed in 4.5, and **part of its checks (like `CONFIG_UBSAN_BOUNDS`) are on by default in modern distro kernels**, because that part is very cheap to run, one of the few kernel sanitizers that can "stay resident".
- **TSan ↔ KCSAN**: note that TSan is full compile-time instrumentation, while KCSAN is different, it's **sampling-based** (watchpoints), with controllable overhead; correspondingly, it detects races by "happening to sample them", not TSan's "theoretically guaranteed to detect". Landed in 5.8 in the mainline (the google/kernel-sanitizers repo says outright "in mainline since 5.8").

The original notes marked KMSAN as "6.1 and above", and **that version number is correct**, don't misremember. KMSAN's patch series was maintained by Google's Alexander Potapenko for years; until the end of 2021 it was still only a branch patch, not in the mainline (the kernel.org official sample report runs on a patched `5.16.0-rc3+`, using the google/kmsan branch, not mainline); Google's official repo (google/kmsan) README states plainly "Linux 6.1+ contains a fully-working KMSAN implementation which can be used out of the box", i.e., **fully usable in mainline from 6.1 on**. So KMSAN is the last of this batch of kernel sanitizers to make it into the mainline. Be careful not to confuse "the 5.16 patch branch can run it" with "6.1 brought it into the mainline"; that's the commonest misread of this kind of version number.

## KFENCE: the key move that puts a sanitizer into production

KASAN's problem is too obvious: it can only be on for debugging, but what happens when your company's online kernel hits a memory bug? You can't exactly swap a production machine for a KASAN-debug kernel to reproduce it; the business would already be dead. What's really missing is a **memory-error detector with overhead low enough to leave on**.

That's KFENCE (Kernel Electric-Fence), **landed in Linux 5.12 in the mainline**. Its idea is entirely different from KASAN's; it no longer "checks every access", it switches to **sampling**:

- KFENCE maintains a fixed-size object pool (default `CONFIG_KFENCE_NUM_OBJECTS=255`; each object takes 2 pages, 1 page for the object and 1 as a guard page, with object pages and guard pages interleaved in the pool, so each object page has guard pages on both sides; under default config the whole pool is about 2 MiB).
- The kernel's slab allocator (`kmalloc`) is **hooked into the KFENCE pool by a sampling timer**: KFENCE has a sampling interval in milliseconds (boot param `kfence.sample_interval`, configurable via `CONFIG_KFENCE_SAMPLE_INTERVAL`), and within each sampling interval the next `kmalloc` is "hooked" and handed off to KFENCE.
- Once in the KFENCE pool, that allocation sits between two guard pages, and any out-of-bounds read/write hits a guard page, immediately triggers a page fault, and the kernel reports a precise error and allocation stack.
- After being freed, KFENCE marks that page "inaccessible"; anyone touching it again is use-after-free, reported on the spot.

The cost of sampling is that **the vast majority of allocations never pass through KFENCE**, so it misses most bugs; you have to run long enough, with enough allocations flowing through the KFENCE pool, to have a chance at catching one. In exchange it has **extremely low overhead** (officially near zero; real production workloads barely feel it), and so it became **the first memory sanitizer that can stay on in a production kernel**. In fact, as long as the architecture supports it and SLAB or SLUB is on, KFENCE is on by default in many distros.

The original notes said "KFENCE must run for a long time, but overhead is low enough that it can even run in production"; the mechanism is right, we just add the version (5.12) and the keyword "sampling", and emphasize the engineering significance of "on by default". It replaces the older `kmemcheck` (deleted back in 4.15 because overhead was too high and the idea clashed with KFENCE's).

## DAMON: another "sampling" route, but not for catching bugs

Mentioning "sampling", we should bring up DAMON (Data Access MONitor) too, because philosophically it's the same family as KFENCE: **no full tracking, just sampling representative samples**. But DAMON isn't a sanitizer; it doesn't catch bugs, it **monitors memory-access patterns**:

- **Landed in Linux 5.15 in the mainline**, aimed at letting developers (and the kernel itself) see "how is a process actually accessing its memory", to optimize layout and guide reclaim.
- DAMON slices the target process's address space into equally-sized regions, **sampling** a handful of representative pages in each region, recording access frequency, and forming histograms. If a region is hot, it gets subdivided further; this "smart amplification" lets it run cheaply even on enormous address spaces.
- The kernel component is the "producer" (producing access patterns); user-space (or the kernel) is the "consumer". A consumer can even feed access patterns back into `madvise()` to change memory attributes, suggesting the kernel swap out regions confirmed cold.

DAMON has three interfaces: the user-space `damo` tool (from awslabs/damo), sysfs under `/sys/kernel/mm/damon/admin/`, and a kernel API for kernel developers. The old debugfs interface is deprecated. Looking at it next to KFENCE, you can see that across the 5.12-5.15 wave the kernel systematically used "sampling" to close the "full instrumentation too expensive" gap: KFENCE catches bugs, DAMON watches patterns, and both can go to production.

## Three-layer defense: place tools by scenario

Putting user-space and kernel-space sanitizers together, the memory-safety toolchain is actually a **layered defense in depth**, where each layer makes a different overhead/coverage tradeoff:

::: tip Development: full instrumentation, catch until you drop
Self-testing, CI, and fuzzing: **overhead isn't the issue, coverage is**. User-space turns on `-fsanitize=address,undefined` (and a separate round of `-fsanitize=thread`); kernel debug builds turn on `CONFIG_KASAN` + `CONFIG_KCSAN` + `CONFIG_UBSAN`. This layer assumes bugs can always be caught by full instrumentation; the price is the program or the whole machine running several times slower, borne only outside production.
:::

::: tip Test / pre-prod: sampled instrumentation, long-running
Pre-prod, gray release, long load tests: **can't accept whole-machine collapse, but have to run long enough to surface rare bugs**. This layer uses KFENCE: sampled, low overhead, can stay on, letting tens of thousands of allocations flow through the guard-page pool to nab the "once in ten thousand runs" overflow and UAF. User-space has no real equivalent at this layer (Valgrind is too slow, ASan too heavy), which is exactly why KFENCE stands out so much on the kernel side.
:::

::: tip Production: always-on lightweight checks + post-hoc analysis
A real online kernel **turns on only checks with negligible overhead**: KFENCE (on by default), lightweight UBSAN subsets like `CONFIG_UBSAN_BOUNDS`, plus DAMON for access-pattern analysis to guide optimization. After an incident you rely on post-hoc tools: kernel oops logs, `kdump`/`crash` analysis, and eBPF's `memleak-bpfcc` to track unreleased allocations. This layer no longer expects to "catch bugs on the spot"; it's about "leave enough evidence to investigate afterward".
:::

This layering is why the kernel maintains both KASAN and KFENCE, two seemingly redundant tools: **the same bug (say UAF), caught by KASAN during development and by KFENCE in production**; the tools don't overlap, the scenarios don't overlap. User-space today only really has the first layer (dev-time instrumentation) working smoothly; the second and third layers aren't as mature as the kernel's, which is also why "completely nailing memory safety in C++ user-space" is harder than in the kernel: the kernel at least has KFENCE backing it up in production; when user-space hits an online UAF, you often just wait for it to crash and then go look at the core dump.

## Side note: static analysis and post-hoc tools

Beyond the runtime sanitizers, both kernel and user-space have another set of **tools that don't run code, they read code or read logs**; the notes mentioned them too, so let's close that thread without expanding:

- **Static analysis**: on the kernel side there's `sparse`, `smatch`, `Coccinelle`, `checkpatch.pl`; on the user-space side there's `clang-tidy`, `cppcheck`. They don't run code and have zero overhead, but they only catch the "obviously fishy in the code pattern" class, and miss runtime-only UAF/OOB. They complement sanitizers rather than replace them: static analysis catches conventions, sanitizers catch runtime behavior.
- **Post-hoc analysis**: kernel oops/panic logs, `kdump`/`crash` for dump analysis, `[K]GDB` debugging. These are forensics for after a bug has already blown up, in a different phase from the "catch bugs early" sanitizers.

User-space C++ post-hoc analysis appeared in the "Dynamic memory management" chapter, where `-fsanitize=address` reported a leak at exit, and in "Concurrency debugging techniques", where TSan did post-hoc concurrency-bug localization. The whole toolchain runs **dev-time sanitizers → production-time lightweight checks → post-hoc analysis** end to end; whichever link is missing, the corresponding class of bug keeps biting you in that phase.

## References

- [kernel.org: Kernel Address Sanitizer (KASAN)](https://www.kernel.org/doc/html/latest/dev-tools/kasan.html) — KASAN mechanism, config options, and sample reports
- [kernel.org: Kernel Memory Sanitizer (KMSAN)](https://www.kernel.org/doc/html/latest/dev-tools/kmsan.html) — KMSAN requires Clang 14.0.6+, x86_64 only, explicitly "not for production"
- [kernel.org: Kernel Electric-Fence (KFENCE)](https://www.kernel.org/doc/html/latest/dev-tools/kfence.html) — KFENCE sampling mechanism, `CONFIG_KFENCE_NUM_OBJECTS`, production-ready positioning
- [kernel.org: UndefinedBehaviorSanitizer (UBSAN)](https://www.kernel.org/doc/html/latest/dev-tools/ubsan.html) — kernel UBSAN sub-checks and overhead
- [kernel.org: Kernel Concurrency Sanitizer (KCSAN)](https://www.kernel.org/doc/html/latest/dev-tools/kcsan.html) — KCSAN's watchpoint-based sampling race detection
- [kernel.org: DAMON](https://www.kernel.org/doc/html/latest/admin-guide/mm/damon/usage.html) — DAMON sysfs/schemes interface and access-pattern monitoring
- [Clang: UndefinedBehaviorSanitizer](https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html) — user-space UBSan sub-check list
- [Clang: MemorySanitizer](https://clang.llvm.org/docs/MemorySanitizer.html) — MSan whole-chain instrumentation requirements and usage
