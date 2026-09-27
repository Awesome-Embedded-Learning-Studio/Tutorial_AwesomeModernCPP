---
chapter: 0
cpp_standard:
- 11
- 14
- 17
- 20
description: Put the user-space flags -fsanitize=address/memory/undefined/thread and the kernel-side
  tools KASAN/KMSAN/UBSAN/KCSAN/KFENCE side by side in one table, and unpack the "compile-time
  instrumentation vs sampling" routes and the "debug vs production" layered defense.
difficulty: advanced
order: 5
platform: host
prerequisites:
- 'The ASan tool family and memory safety: shadow memory, Heartbleed, and sanitizer selection'
- 'Valgrind vs ASan: JIT interpretation vs compile-time instrumentation'
reading_time_minutes: 20
related:
- 'The ASan tool family and memory safety: shadow memory, Heartbleed, and sanitizer selection'
- 'Valgrind vs ASan: JIT interpretation vs compile-time instrumentation'
- Debugging Techniques for Concurrent Programs
- Dynamic Memory Management
tags:
- host
- cpp-modern
- advanced
- 内存安全
- 调试
- 工具链
title: 'The sanitizer toolchain landscape: from -fsanitize to in-kernel KASAN/KFENCE'
translation:
  source: documents/vol6-performance/ch00-performance-mindset/05-sanitizer-toolchain-and-memory-safety.md
  source_hash: ccdb0b754391b0f51c8dad25bc045c3e4139b756fc2087b9e1fa344a76dfccb7
  translated_at: '2026-09-26T05:26:33+00:00'
  engine: anthropic
  token_count: 10000
---
# The sanitizer toolchain landscape: from -fsanitize to in-kernel KASAN/KFENCE

> PS: This part was migrated from my college notes and then re-verified; the user-space sanitizers were actually run on this machine, while the kernel-side tools cannot be run locally, so kernel.org's official documentation is the authority there. If anything is still loose, an Issue or PR is welcome.

In the previous two articles we took user-space ASan / UBSan / MSan / TSan and Valgrind apart in detail: how shadow memory keeps its books, where the two routes of JIT interpretation and compile-time instrumentation differ, and why the five sanitizers are mutually exclusive. But if your gaze stops at "`g++ -fsanitize=address`, just add a flag", you will miss a much bigger picture: **sanitizers are not a user-space monopoly — the kernel has a whole matching set of tools**, and the design trade-offs on the two sides are completely different.

What this article does is flatten the entire sanitizer toolchain into one view. On one side, user-space `-fsanitize=*`; on the other, kernel-space `CONFIG_KASAN / CONFIG_KMSAN / CONFIG_KFENCE`. They hunt the same classes of bugs (out-of-bounds, use-after-free, uninitialized reads, data races), but under completely different constraints: user space can afford to slow the program 2–5x to catch bugs; the kernel cannot — slow the kernel by 5x and the whole machine is toast. So the kernel side evolved the "sampling" route: KFENCE buys "can stay enabled in production forever" with extremely low overhead, coexisting in layers with heavyweight tools like KASAN that can only be enabled during debugging.

## First, let's close out the user-space side

Before walking into the kernel, let's nail down the four user-space sanitizer flags with real reports, so we have something to compare against the kernel side later. The detailed shadow-memory mechanics and the Heartbleed story were covered thoroughly in the previous article; here we keep only the minimal reproducible code and the real terminal output, so it is easy to match "which flag catches which bug".

One-line division of labor for the four flags: `-fsanitize=address` (ASan: out-of-bounds / UAF / leaks), `-fsanitize=undefined` (UBSan: undefined behavior), `-fsanitize=memory` (MSan: uninitialized reads), `-fsanitize=thread` (TSan: data races).

### ASan: three classes of bugs in one pass

Heap overflow, use-after-free, memory leaks — ASan takes all three in one haul. We write the three errors as separate minimal examples (put them in one program and ASan aborts at the first error, so you would never see the other two — hence the split):

```cpp
// uaf.cpp — use-after-free
#include <cstdio>
int main() {
    int* p = new int(7);
    delete p;
    printf("*p = %d\n", *p);   // p is already deleted, dangling
    return 0;
}
```

Compile it with `g++ -std=c++20 -O0 -g -fsanitize=address -fno-omit-frame-pointer uaf.cpp -o uaf`; running it prints:

```text
=================================================================
==118313==ERROR: AddressSanitizer: heap-use-after-free on address 0x72c9e1de0010 at pc 0x5d222d6ed26f bp 0x7ffc31d299a0 sp 0x7ffc31d29990
READ of size 4 at 0x72c9e1de0010 thread T0
    #0 0x5d222d6ed26e in main /tmp/sanit/uaf.cpp:6
    ...
SUMMARY: AddressSanitizer: heap-use-after-free /tmp/sanit/uaf.cpp:6 in main
```

`-g` is what makes the report carry source locations like `uaf.cpp:5` — the make-or-break line for ASan usability: without debug symbols, the report degenerates into a pile of addresses and is basically worthless. It catches stack overflows just as well; swap in a stack buffer that crosses a function boundary:

```cpp
// stack_oob.cpp — stack buffer overflow
#include <cstdio>
void fill(char* p) {                 // cross-function, so detection crosses stack frames
    for (int i = 0; i <= 8; ++i) p[i] = 'A';  // legal indices are 0..7, 8 is out of bounds
}
int main() {
    char buf[8];
    fill(buf);
    printf("done\n");
    return 0;
}
```

Compile with the same flags and run:

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

Notice it does more than say "out of bounds" — it tells you "this memory is the `buf` at offset 40 in `main`'s stack frame": the stack redzone even annotates which stack array a piece of memory belongs to. That is the power of shadow memory, dissected in detail in the previous article; we won't expand on it here.

Memory leaks go through LeakSanitizer (LSan), which ships inside ASan and scans once at process exit:

```cpp
// leak.cpp — forgot the delete
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

ASan's cost is real: the program runs 2–5x slower and eats 3–5x more memory. So **production builds must drop `-fsanitize=address`**; enable it only during debugging and testing. This constraint sounds like a trifle, but on the kernel side the very same "too expensive" problem directly gave birth to a completely different kind of tool — that is where KFENCE later in this article comes from.

### UBSan: the undefined-behavior specialist

ASan asks "may this memory be touched at all"; UBSan asks "is this operation itself legal". Signed integer overflow, out-of-bounds array subscripts, null-pointer dereference, invalid shifts — in the C++ standard these are undefined behavior (UB): they don't necessarily crash, but the results are unpredictable:

```cpp
// ub.cpp — three kinds of UB
#include <cstdio>
#include <cstdint>
int main() {
    int32_t big = 2147483647;   // INT32_MAX
    int32_t sum = big + 1;      // (1) signed addition overflow → UB
    int arr[4] = {0,1,2,3};
    int idx = 10;
    int v = arr[idx];           // (2) out-of-bounds index → UBSan's bounds check
    printf("sum=%d v=%d\n", sum, v);
    return 0;
}
```

Compile with `g++ -std=c++20 -O0 -g -fsanitize=undefined ub.cpp -o ub` (recover is the default, so every UB gets printed and the program keeps going):

```text
ub.cpp:6:13: runtime error: signed integer overflow: 2147483647 + 1 cannot be represented in type 'int'
ub.cpp:9:20: runtime error: index 10 out of bounds for type 'int [4]'
ub.cpp:9:9: runtime error: load of address 0x7fffed140f28 with insufficient space for an object of type 'int'
sum=-2147483648 v=0
```

One real-world trap to flag up front: **UBSan and ASan can be enabled together** (`-fsanitize=address,undefined`), and many people do exactly that, because one guards memory while the other guards arithmetic — they complement each other. But UBSan's default is "print and keep running" (recover); if you want it to abort at the first UB (closer to production behavior), add `-fno-sanitize-recover=all`. The other way around, ASan aborts on contact, and that cannot be changed.

### MSan: uninitialized reads, Clang only

MSan catches "using a value that was never initialized" — a class ASan cannot see: the memory is legal, the access is legal, but the value is garbage. The trap here: **MSan exists only in Clang; GCC does not support this flag at all**:

```cpp
// msan.cpp — using an uninitialized variable
#include <cstdio>
int main() {
    int x;                     // deliberately left uninitialized
    if (x)                     // branching on a garbage value → MSan catches this
        printf("x is truthy\n");
    else
        printf("x is zero\n");
    return 0;
}
```

GCC errors out outright:

```text
$ g++ -std=c++20 -fsanitize=memory msan.cpp -o msan
g++: error: unrecognized argument to '-fsanitize=' option: 'memory'
```

Swap in Clang and it compiles and runs (`clang++ -std=c++20 -O0 -g -fsanitize=memory -fno-omit-frame-pointer msan.cpp -o msan`):

```text
==118932==WARNING: MemorySanitizer: use-of-uninitialized-value
    #0 0x58f3129f5677  (/tmp/sanit/msan+0xd7677)
    ...
SUMMARY: MemorySanitizer: use-of-uninitialized-value
```

> **Pitfall warning**: MSan has a hard restriction — **the entire program, including every library it links against, must be compiled with MSan instrumentation**. Run `clang++ -fsanitize=memory` and link an uninstrumented `libc++` or third-party library, and you will get a flood of false positives, because MSan treats every value the library returns as uninitialized. That is why MSan is rarely used in real projects; a clean run usually requires "rebuilding the entire toolchain with MSan". The previous article covered this; we stress it again here because the kernel-side KMSAN carries a similar "whole-chain instrumentation" requirement.

As for TSan (data races): it is mutually exclusive with ASan, costs 5–15x, and specializes in concurrency bugs. The concurrency volume's "Debugging Techniques for Concurrent Programs" already took it apart completely, so here we only mark its position in the landscape and won't repeat it.

## Now the real question: what about the kernel

With the four user-space flags memorized, we arrive at what this article actually wants to say. **The kernel is C code too — it can go out of bounds, it can UAF, it can data-race. Can we just slap `-fsanitize=address` onto the kernel?**

The answer: **yes — and the kernel really did it, but the cost is so high you can only enable it while debugging**. This is KASAN, the Kernel AddressSanitizer. Under the hood it is the same machinery as user-space ASan (shadow memory + compile-time instrumentation), but the kernel has its own constraints:

1. **The shadow memory takes a large chunk of the kernel's virtual address space**. User-space ASan's shadow is "1/8 of the process address space"; the kernel side simply carves a big segment out of the kernel VAS (`KASAN_SHADOW_START` through `KASAN_SHADOW_END`). On a 64-bit kernel the address space is big enough (128 TB) to absorb it; on 32-bit it is far tighter, which is why early KASAN ran on 64-bit only, until 5.11 brought the slimmed-down ARM-32 version by Linus Walleij.

2. **Every memory access on the whole machine gets instrumented**. The kernel is not a process; it is the substrate all processes share. Enable KASAN and whole-machine performance collapses on the spot — which is why `CONFIG_KASAN` is for debug kernels only, and production kernels never turn it on.

3. **It requires a specific memory allocator**. The kernel uses the SLAB or SLUB allocator, and KASAN has to plant redzones inside the allocator and "poison" freed pages (`KASAN_SANITIZE_*`) to catch UAF/OOB the moment they happen. It is the same idea as user-space ASan intercepting `malloc/free`, just relocated to `kmalloc/kfree`.

The original notes say "KASAN applies to x86_64 and AArch64, 4.x and up" — that version number needs checking. In fact KASAN merged into mainline in **Linux 4.0** (initially x86_64, AArch64 following), and only **5.11** added the optimized ARM-32 version. The mechanism description is fine; just don't remember it as a vague "4.x".

### What a KASAN report looks like (the official format)

What does a KASAN report look like? Following the example structure of the kernel.org dev-tools/kasan document, with the official example's `kmalloc_oob_right` swapped for a fictional `buggy_driver_write` (fields and hierarchy map exactly onto the official report), it goes roughly like this:

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

The structure is almost identical to a user-space ASan report: **first, where it blew up (slab-out-of-bounds, an out-of-bounds write, in which driver function); then the allocation stack (who allocated this memory, in which `kmalloc`)**. The kernel report adds kernel-allocator-specific details like "which slab cache it belongs to (`kmalloc-8`) and which byte inside the object". Once you can read a user-space ASan report, you can basically read a kernel KASAN report too.

## The full comparison table: user space ↔ kernel

With both sides now lined up, this table is the core of the article. The original notes had it as an external PNG; we redraw it in Markdown ourselves.

| Bug caught | User-space flag | Kernel tool | Kernel mainline version | Production-ready? |
|---------|-----------|---------|------------|----------|
| OOB / UAF / double free | `-fsanitize=address` (ASan) | **KASAN** | 4.0 (x86_64) / 5.11 (ARM-32 optimized) | No, debug only |
| Uninitialized reads | `-fsanitize=memory` (MSan, Clang only) | **KMSAN** | usable via patch branch from 5.16, fully usable in mainline from **6.1**; Clang 14.0.6+ only, x86_64 only | No, huge overhead |
| Undefined behavior (overflow / OOB / shifts) | `-fsanitize=undefined` (UBSan) | **UBSAN** | merged in 4.5 | Some checks can ship (see below) |
| Data races | `-fsanitize=thread` (TSan) | **KCSAN** | merged in 5.8, sampling-based | No, debug only |
| Memory leaks | LSan bundled with ASan | **kmemleak** / eBPF `memleak` | kmemleak has existed for a long time | With caution, has false positives |
| Memory errors caught by sampling | (no user-space counterpart) | **KFENCE** | **5.12** | **Yes, on by default** |
| Access-pattern analysis (not bug detection) | (none) | **DAMON** | **5.15** | Yes, purpose-built for production |

A few correspondences in this table are must-knows:

- **ASan ↔ KASAN**: the same shadow-memory idea ported into the kernel; the price is whole-machine performance collapse, so it stays debug-only.
- **MSan ↔ KMSAN**: both Clang-only, both demanding whole-chain instrumentation, both hugely expensive. The KMSAN documentation says it outright: "not intended for production use, because it drastically increases kernel memory footprint and slows the whole system down".
- **UBSan ↔ UBSAN**: kernel UBSAN merged in 4.5, and **part of its checks (such as `CONFIG_UBSAN_BOUNDS`) are enabled by default in modern distribution kernels**, because this subset is cheap — one of the few kernel sanitizers that can "live there permanently".
- **TSan ↔ KCSAN**: note that TSan is full compile-time instrumentation, while KCSAN is different — it is **sampling**-based (watchpoints), so its overhead is controllable; the flip side is that it detects data races by "happening to sample them", not TSan-style "theoretically guaranteed detection". Merged into mainline in 5.8 (the google/kernel-sanitizers repo says it plainly: "in mainline since 5.8").

The original notes mark KMSAN as "6.1 and above" — **that version number is correct**, don't misremember it. KMSAN's patch series was maintained by Google's Alexander Potapenko for years and stayed an out-of-tree patch branch until the end of 2021 (kernel.org's official example report ran on a patched `5.16.0-rc3+`, built from the google/kmsan branch, not mainline); the README of Google's official repo (google/kmsan) states plainly: "Linux 6.1+ contains a fully-working KMSAN implementation which can be used out of the box" — that is, **fully usable in mainline from 6.1**. So KMSAN was the last of this batch of kernel sanitizers to reach mainline. Be careful not to confuse "the 5.16 patch branch runs" with "in mainline as of 6.1" — the most common misreading of this kind of version number.

## KFENCE: the key move that gets a sanitizer into production

KASAN's problem is obvious: debug-only. But what do you do when your company's production kernel hits a memory bug? You can't take a production machine, swap in a KASAN debug kernel, and reproduce there — the business would be long dead by then. What is really missing is a memory-error detector **cheap enough to stay on forever**.

That is KFENCE (Kernel Electric-Fence), **merged into mainline in Linux 5.12**. Its idea is the opposite of KASAN's: stop "checking every access" and switch to **sampling**:

- KFENCE maintains a fixed-size object pool (by default `CONFIG_KFENCE_NUM_OBJECTS=255`; each object occupies 2 pages: 1 page holds the object, 1 page acts as a guard page; object pages and guard pages alternate within the pool, so every object page has guard pages on both sides; under the default configuration the whole pool is about 2 MiB).
- The kernel's slab allocator (`kmalloc`) gets **hooked into the KFENCE pool by a sampling timer**: KFENCE has a sampling interval in milliseconds (boot parameter `kfence.sample_interval`, default configurable via `CONFIG_KFENCE_SAMPLE_INTERVAL`); within each sampling interval, the next `kmalloc` allocation gets "hooked" and handed over to KFENCE to manage.
- Once an allocation enters the KFENCE pool, it is placed between two guard pages; any out-of-bounds read or write steps on a guard page, immediately triggers a page fault, and the kernel reports the precise error plus the allocation stack.
- After the object is freed, KFENCE marks the page "inaccessible"; the next person to touch it commits a use-after-free, reported just as immediately.

The cost of sampling: **the overwhelming majority of allocations never pass through KFENCE**, so it misses most bugs — you have to run long enough, and push enough allocations through the KFENCE pool, to give it a chance to catch something. What you buy is **extremely low overhead** (officially near zero; real production workloads barely notice it), and so it became **the first memory sanitizer that can stay enabled on production kernels**. In fact, wherever the architecture supports it and SLAB or SLUB is enabled, KFENCE is on by default in many distributions.

The original notes said, verbatim, "KFENCE must run for a long time, but its overhead is low enough that it can even run in production environments". The mechanism description is correct; we add the version number (5.12) and the keyword "sampling", and underline the engineering significance of "on by default". It replaced the older `kmemcheck` (deleted back in 4.15 — too expensive, and at odds with KFENCE's approach).

## DAMON: the other "sampling" route, but not for catching bugs

Since "sampling" came up, DAMON (Data Access MONitor) deserves a word too, because philosophically it is the same species as KFENCE — **don't track everything; sample representative samples**. But DAMON is not a sanitizer: it doesn't catch bugs, it **monitors memory access patterns**:

- **Merged into mainline in Linux 5.15**, with the goal of helping developers (and the kernel itself) see "how exactly is this process touching memory", so as to optimize layout and guide reclamation.
- DAMON slices the target process's address space into equal-sized regions, **samples** a handful of representative pages in each region, records access frequency, and builds a histogram. Hot regions get subdivided further — this "smart zoom-in" keeps it cheap to run even on enormous address spaces.
- The kernel component is the "producer" (emitting access patterns); user space (or the kernel) is the "consumer". The consumer can even turn the patterns around into `madvise()` calls that change memory attributes — for example advising the kernel to swap out a data region confirmed cold.

DAMON has three interfaces: the user-space `damo` tool (from awslabs/damo), the sysfs tree under `/sys/kernel/mm/damon/admin/`, and a kernel API for kernel developers. The old debugfs interface is deprecated. Set it beside KFENCE and you can see that, in the 5.12–5.15 wave, the kernel systematically used "sampling" to plug the "full instrumentation is too expensive" hole: KFENCE catches bugs, DAMON watches patterns, and both can go to production.

## Three layers of defense: putting each tool in its place

Put the user-space and kernel sanitizers together, and the memory-safety toolchain turns out to be **defense in depth**, with each layer making a different overhead/coverage trade-off:

::: tip Development: full instrumentation, until the bug is caught
During dev self-testing, CI, and fuzzing, **overhead is not the problem — coverage is everything**. User space enables `-fsanitize=address,undefined` (plus a separate round of `-fsanitize=thread`); kernel debug builds enable `CONFIG_KASAN` + `CONFIG_KCSAN` + `CONFIG_UBSAN`. This layer assumes full instrumentation will catch the bug, at the cost of a several-times-slower program or machine — a cost borne only outside production.
:::

::: tip Testing / pre-production: sampled instrumentation, long-running
Staging, canary rollout, long-duration load tests: **whole-machine collapse is unacceptable, yet only enough running time surfaces the rare bugs**. This layer uses KFENCE — sampling, low overhead, always-on — letting thousands upon thousands of allocations flow through the guard-page pool to catch the out-of-bounds and UAF that "show up once in ten thousand runs". User space currently has no true counterpart at this layer (Valgrind too slow, ASan too heavy), which is exactly why KFENCE's engineering value on the kernel side stands out.
:::

::: tip Production: lightweight always-on checks + post-mortem analysis
A real production kernel **enables only checks with negligible overhead**: KFENCE (on by default), lightweight UBSAN subsets like `CONFIG_UBSAN_BOUNDS`, plus DAMON doing access-pattern analysis to guide optimization. When an incident happens, you lean on post-mortem tools: kernel oops logs, kdump/crash analysis, eBPF's `memleak-bpfcc` tracing unfreed allocations. This layer no longer counts on "catching the bug red-handed"; it counts on "leaving enough evidence to investigate afterwards".
:::

This layering is exactly why the kernel keeps both KASAN and KFENCE, two seemingly redundant tools: **the same bug (UAF, say) is caught by KASAN during development and by KFENCE in production** — the tools don't duplicate; the scenarios don't overlap. In user space, only the first layer (development-time instrumentation) works comfortably today; the second and third layers have nothing as mature as the kernel's tools. Which is also why "completely nailing memory safety in C++ user space" is harder than in the kernel: the kernel at least has KFENCE backing up production, while a user-space production UAF often means waiting for the crash and then going to read the core dump.

## By the way: static analysis and post-mortem tools

Besides the runtime sanitizers above, both the kernel and user space have another set of tools that **don't run the code — they read it, or read its logs**. The original notes mention them too; we close with a quick roundup, without going deep:

- **Static analysis**: on the kernel side, `sparse`, `smatch`, `Coccinelle`, `checkpatch.pl`; on the user-space side, `clang-tidy`, `cppcheck`. They never run the code and add zero runtime overhead, but they only catch the "obviously wrong code pattern" class — they cannot see UAF/OOB that only surfaces at runtime. They complement sanitizers rather than replace them: static analysis catches conventions, sanitizers catch runtime behavior.
- **Post-mortem analysis**: kernel oops/panic logs, the `kdump`/`crash` tools dissecting dumps, `[K]GDB` debugging. These are forensic tools for after the bug has already blown up — a different stage from the sanitizers' "catch it early".

We have already met C++ user-space post-mortem analysis twice: in "Dynamic Memory Management" we used `-fsanitize=address` to report leaks at exit, and in "Debugging Techniques for Concurrent Programs" we used TSan to locate concurrency bugs after the fact. The whole toolchain is a pipeline — **development-time sanitizers → production-time lightweight checks → post-mortem analysis** — and whichever link is missing, the corresponding class of bug will keep biting you at that stage.

## References

The resources below are Linux-centric: my main work machine runs Linux, and it is the system I know best. Windows folks, please forgive me — I will keep adding to this list over time.

- [kernel.org: Kernel Address Sanitizer (KASAN)](https://www.kernel.org/doc/html/latest/dev-tools/kasan.html) — KASAN mechanics, config options, and example reports
- [kernel.org: Kernel Memory Sanitizer (KMSAN)](https://www.kernel.org/doc/html/latest/dev-tools/kmsan.html) — KMSAN requires Clang 14.0.6+, x86_64 only, explicitly "not for production"
- [kernel.org: Kernel Electric-Fence (KFENCE)](https://www.kernel.org/doc/html/latest/dev-tools/kfence.html) — KFENCE's sampling mechanism, `CONFIG_KFENCE_NUM_OBJECTS`, and its production-ready positioning
- [kernel.org: UndefinedBehaviorSanitizer (UBSAN)](https://www.kernel.org/doc/html/latest/dev-tools/ubsan.html) — the kernel UBSAN sub-checks and their overhead
- [kernel.org: Kernel Concurrency Sanitizer (KCSAN)](https://www.kernel.org/doc/html/latest/dev-tools/kcsan.html) — KCSAN's watchpoint-based sampled race detection
- [kernel.org: DAMON](https://www.kernel.org/doc/html/latest/admin-guide/mm/damon/usage.html) — DAMON's sysfs/schemes interfaces and access-pattern monitoring
- [Clang: UndefinedBehaviorSanitizer](https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html) — the list of user-space UBSan sub-checks
- [Clang: MemorySanitizer](https://clang.llvm.org/docs/MemorySanitizer.html) — MSan's whole-chain instrumentation requirement and usage
