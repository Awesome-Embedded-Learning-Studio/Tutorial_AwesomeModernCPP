---
title: 'ASan Tool Family and Memory Safety: Shadow Memory, Heartbleed, and Sanitizer Selection'
description: 'Starting with Heartbleed, we break down the shadow memory trio of AddressSanitizer, test OOB, UAF, global out-of-bounds, and UBSan, and clarify the responsibilities and mutual exclusivity of the five brothers: ASan, LSan, MSan, TSan, and UBSan.'
chapter: 6
order: 10
platform: host
difficulty: advanced
cpp_standard:
- 11
- 14
- 17
- 20
reading_time_minutes: 22
prerequisites:
- 动态内存管理(new/delete 与智能指针)
- 并发程序调试技巧(ThreadSanitizer)
related:
- 动态内存管理(new/delete 与智能指针)
- 并发程序调试技巧(ThreadSanitizer)
- C 语言动态内存管理(malloc/free 与 valgrind)
tags:
- host
- cpp-modern
- advanced
- 内存安全
- 调试
- 内存管理
translation:
  source: documents/vol6-performance/10-asan-family-and-memory-safety.md
  source_hash: 8062233844a7990d1e7112dd028dffa5f17ee3f2c46a0f517645d984738e3f8b
  translated_at: '2026-06-25T13:28:09.799801+00:00'
  engine: anthropic
  token_count: 4210
---
# The ASan Tool Family and Memory Safety: Shadow Memory, Heartbleed, and Sanitizer Selection

> PS: This content was migrated from notes I took during college. It has undergone limited verification through research. If you find any technical inaccuracies or outright errors, please report an issue or submit a pull request!

After writing C/C++ for a while, you have likely been tortured by these classes of problems repeatedly: reading one element past the end of an array, using a pointer after it has been freed, or calling `delete` twice on the same pointer. These errors share a nasty characteristic—they are **undefined behavior** (the infamous UB). It's not that they "always crash," but rather "they run fine in a Debug build, then randomly explode in Release or on a different machine." Even worse, the crash location is often far removed from the actual buggy code, and the stack trace might point to some innocent library function.

Why does this happen? Because these bugs corrupt the memory manager's own metadata, and the explosion only happens during the next `malloc`/`free` operation that touches that corrupted location. Previous volumes focused on performance; this time, we switch dimensions to another topic: how to use tools to catch bugs before they cause production incidents. The protagonists are AddressSanitizer (ASan) and the entire family of sanitizer tools behind it.

Don't rush to treat ASan as just a "add a flag and you're done" utility. The design behind it—shadow memory and compile-time instrumentation—is actually one of the most significant engineering advancements in C/C++ memory safety over the last decade. Furthermore, it was originally invented to plug a hole that terrified the entire internet. We start with that hole.

## The Origin: Heartbleed and Buffer Over-reads

In April 2014, CVE-2014-0160 was disclosed under the name Heartbleed. This was a vulnerability hidden in a seemingly harmless feature of the OpenSSL implementation—the TLS heartbeat extension. The protocol is simple: the client sends arbitrary data and tells the server, "This data is N bytes long, please read it back verbatim," to verify the connection is still alive.

The vulnerability lay in the fact that the server **trusted the length N reported by the client without verifying that N did not exceed the actual length of the data it held**. Consequently, an attacker simply needed to report a large N (for example, 64 KB), and the server would "read back" 64 KB from its own process memory to the attacker. What was read could be the TLS private keys of other sessions, user passwords, or session tokens—everything adjacent to that buffer in process memory would be leaked.

The nature of this bug was not an out-of-bounds **write**, but an out-of-bounds **read** (buffer over-read). Write overflows corrupt data and are easily exposed; over-reads are much quieter, as the process itself does not crash, and data simply silently leaks out. ASan was frequently cited back then because it was one of the few tools capable of **reliably detecting over-reads**—as long as that out-of-bounds memory touched the redzones planted by ASan, a single read would trigger an immediate error.

We will use a few dozen lines of Modern C++ to recreate a Heartbleed-shaped bug, and then have ASan catch it in the act. This is the flagship demo for this article, and we will refer to it repeatedly.

```cpp
// oob_read.cpp —— 复刻 Heartbleed 形状的越界读
// Platform: host    Standard: C++20
// 编译: g++ -std=c++20 -O1 -fsanitize=address -g oob_read.cpp -o oob_read
#include <array>
#include <cstdio>
#include <string>

// 心跳回显:客户端说"还给我 n 字节"。服务器照办,但不校验 n 上界。
std::string read_back(const std::array<char, 8>& buf, int n)
{
    return std::string(buf.data(), n);   // n 可能远大于 8
}

int main()
{
    std::array<char, 8> buf{'H', 'i', '!', 0, 0, 0, 0, 0};
    // 只授权了 8 字节,却要求"读回" 64 字节 —— 经典 over-read
    auto leaked = read_back(buf, 64);
    std::printf("读到 %zu 字节: %.8s...\n", leaked.size(), leaked.c_str());
}
```

If we compile and run this without ASan, the code will likely "appear to work correctly": the `std::string` constructor dutifully copies 64 bytes starting from `buf.data()`, reading the irrelevant bytes on the stack. The program won't crash. This is exactly what makes over-reads so dangerous.

However, if we add `-fsanitize=address` and run it again, the situation changes drastically. Here is the output from the local GCC 16.1.1:

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

Pay attention to two details. First, the error type is `stack-buffer-overflow`, occurring on line 11 of `read_back`—specifically the `return std::string(buf.data(), n);` line. This precision in source location is exactly why compiling with `-g` is essential. Second, ASan even tells us that there are two objects on the stack: `buf` occupies `[32, 40)` and `leaked` occupies `[64, 96)`. The out-of-bounds read location (offset 40) falls right between them. This level of forensic detail is what fundamentally distinguishes ASan from "adding an assertion and hunting slowly."

## So, what exactly does ASan do to achieve this?

### Part One: Compile-Time Instrumentation (CTI)

ASan is not a post-mortem profiler; rather, it **rewrites your code at compile time**. When you add `-fsanitize=address`, the compiler (whether GCC or Clang) inserts extra check instructions before and after every memory access—every `*p`, every array subscript, every `memcpy`. This technique is called **compile-time instrumentation (CTI)**, also known as static instrumentation.

Let's first verify that it actually "touches your code." Compile the `oob_read.cpp` from above without ASan, then again with ASan, and compare the **size of their code segments (.text)**—that is, the actual machine instructions packed into the binary:

```text
普通构建 .text:   2792 字节
ASan 构建 .text:  5736 字节   (+105%)
```

(On the local machine with GCC 16.1.1, `g++ -std=c++20 -O1 -g`, checking the `.text` section with `size`.) The doubled size represents the check instructions the compiler inserts before and after every memory access. Note a pitfall here: **don't compare the total binary file size**—ASan's runtime library, `libasan.so.8`, is **dynamically linked** (visible via `ldd`) and not baked into the executable, so the total file size only increases by about 5%. The `.text` code section, which truly reflects the amount of instrumentation, is where the doubling occurs. The cost is increased size and slower execution—but compared to the bugs it can catch, this overhead is negligible during development. CTI is determined at **compile time**, so you must include `-fsanitize=address` during **compilation**, and also during **linking**. If you add it only when compiling the main program but omit it when linking a third-party `.a` library, memory accesses inside that library won't be instrumented, and ASan will be blind to that code. The complete workflow is:

```bash
g++ -std=c++20 -O1 -fsanitize=address -g -c a.cpp -o a.o     # 编译带
g++ -std=c++20 -O1 -fsanitize=address -g main.cpp a.o -o app  # 链接也带
```

The `-fsanitize=address` flag must be present in **both** the compilation and linking stages. If it is missing from either one, it will not work.

### The Second Piece: Shadow Memory

Instrumentation alone is not enough. The check code inserted by instrumentation needs a "ledger" to answer the question: "Is this address accessible right now?" This ledger is the **shadow memory**.

The core idea is an elegant design: **use one byte of shadow memory to record the accessibility state of eight bytes of actual memory**. In other words, AddressSanitizer maps the entire process address space to a contiguous shadow region in 8-byte chunks, with a ratio of 1:8. This way, checking if an address is valid simply involves calculating its corresponding shadow byte and reading it; there is no need to maintain complex hash tables.

AddressSanitizer prints the values of the shadow bytes at the end of its report. Let's look at the legend in the actual output:

```text
Shadow byte legend (one shadow byte represents 8 application bytes):
  Addressable:           00
  Partially addressable: 01 02 03 04 05 06 07
  Heap left redzone:       fa
  Freed heap region:       fd
  Stack left redzone:      f1
  Stack mid redzone:       f2
```

This covers the complete semantics of the 1:8 mapping. `00` indicates that all eight bytes are accessible; `01` through `07` indicate that only the first few bytes are valid (for example, `03` means the first three bytes are accessible and the last five are not, used for partially accessible regions at the end of alignments); `fa` is the redzone around heap allocations—ASan secretly surrounds every block of memory you `new` with a "no-entry zone", so if you read `fa`, you have a heap buffer overflow; `fd` is memory that has already been `free`'d, so touching it is a use-after-free; `f1`/`f2` are redzones for stack objects.

Looking back at the shadow dump in the error report above:

```text
=>0x72175e1f0000: f1 f1 f1 f1 00[f2]f2 f2 00 00 00 00 f3 f3 f3 f3
```

`00` represents the body of `buf` (8 bytes, plus 1 shadow byte). The immediately following `[f2]` is the mid redzone between stack objects. The address of our out-of-bounds read landed exactly on this `f2`—which ASan spotted immediately. This is why the shadow memory mechanism can achieve byte-level precision.

### The Third Piece: Runtime Library + Quarantine

Instrumentation and shadow regions alone aren't enough; someone needs to **keep the ledger**. The ASan runtime library (`libasan`) completely replaces functions like `new`/`delete` and `malloc`/`free` with its own versions. Every time memory is allocated, the runtime paints redzones in the shadow region for it; every time it is freed, the corresponding shadow memory is marked as `fd`.

A key design here is called **quarantine**. When memory is `free`'d, ASan doesn't return it to the system for reallocation immediately. Instead, it tosses it into a quarantine queue to "cool off." Why? Because for bugs like use-after-free, if you `free` memory and immediately reallocate it to someone else, the shadow state of that block resets to `00`, and subsequent erroneous reads won't be caught. By quarantining it for a while, we ensure that the "freed" state persists long enough to collide with any subsequent invalid accesses.

However, quarantine isn't unlimited—the queue has a cap. Once full, the oldest freed memory is recycled via FIFO. So, ASan's detection of use-after-free isn't 100%—if the quarantine window has passed and the memory has been reallocated, that specific invalid read might slip through. But with sufficient test coverage, the vast majority of UAFs will be caught.

### The Cost: 2-4x Overhead, and Why It's Worth It

With this three-piece combo, ASan typically incurs **2-4x runtime slowdown and 3-5x memory overhead** (shadow memory takes 1/8, plus redzones and quarantine). That sounds like a lot, but it depends on who you compare it to.

Traditional memory checking tools like Valgrind (Memcheck) use **Dynamic Binary Instrumentation** (DBI)—they don't recompile your program. Instead, at runtime, they translate every machine instruction into their own intermediate representation, analyzing and executing them one by one. High precision and no recompilation needed, but the cost is 20-50x slowdown. A test that takes 1 second normally might take half a minute with Valgrind, making it often impossible to include in daily CI.

ASan shifts the analysis cost **upfront to compile time** (CTI). At runtime, it only performs table lookups, keeping the overhead down to 2-4x. This magnitude means you can **permanently** enable ASan in development and CI to run full test suites, rather than remembering to manually run Valgrind occasionally. This is ASan's fundamental advantage over Valgrind—it's not that it's more accurate, but that it's **affordable**.

::: warning No Valgrind Installed Locally
All ASan/UBSan outputs in this article were run locally on real hardware (GCC 16.1.1 / Clang 22, WSL2). Valgrind is not installed in the local environment (`which valgrind` → not found), so actual Valgrind output is not included here. Students who need Valgrind can run `apt install valgrind` on Debian/Ubuntu. For usage, see the Valgrind section in vol1's [C Dynamic Memory Management](../vol1-fundamentals/c_tutorials/14-dynamic-memory.md). Remember the essential difference between the two commands: **ASan is compile-time `-fsanitize=address` (CTI), while Valgrind is runtime `valgrind ./prog` (DBI)**.
:::

## The Tool Family: Five Sanitizers, Each for a Category

ASan is actually just one member of a family. This set of tools was originally implemented by Google engineers as patches for GCC and Clang, later becoming standard in mainstream compilers. The family has five members, each watching for a specific class of errors:

| Tool | Flag | What it Catches | Typical Overhead |
|------|------|-----------------|------------------|
| **ASan** (AddressSanitizer) | `-fsanitize=address` | OOB read/write, use-after-free, double-free, stack/global overflow | 2-4x slowdown |
| **LSan** (LeakSanitizer) | `-fsanitize=leak` | Memory leaks (heap memory not freed at exit) | Near zero overhead |
| **MSan** (MemorySanitizer) | `-fsanitize=memory` | Reading uninitialized memory (use of uninitialized value) | ~3x slowdown |
| **TSan** (ThreadSanitizer) | `-fsanitize=thread` | Data races, deadlocks | 5-15x slowdown |
| **UBSan** (UndefinedBehaviorSanitizer) | `-fsanitize=undefined` | Undefined behavior (signed overflow, null pointer dereference, out-of-bounds shift, etc.) | Configurable, most sub-checks have low overhead |

Among the five, ASan is the workhorse and almost essential for daily development. LSan is enabled by default with ASan (in GCC/Clang on supported environments). MSan is only fully available on Clang and requires **the entire program** to be compiled as MSan (even libc needs a MSan version, or false positives will be rampant). TSan specifically monitors concurrency; we covered it in detail in vol5's [Debugging Concurrency](../vol5-concurrency/ch08-debug-testing-perf/01-debugging-concurrency.md). UBSan acts as a "finisher," with low overhead that allows it to be combined with others.

### ASan and TSan are Mutually Exclusive: An Iron Rule

These five tools cannot be combined arbitrarily. The most important constraint is: **ASan and TSan cannot be enabled simultaneously**. ASan needs its own shadow memory layout, and TSan needs its own; the two mechanisms will conflict. The compiler will reject you outright at compile time:

```text
$ g++ -std=c++20 -fsanitize=address,thread -g conflict.cpp -o conflict
cc1plus: error: '-fsanitize=thread' is incompatible with '-fsanitize=address'
```

The error message is straightforward. The engineering consequence of this constraint is that within a project's CI, memory error detection and data race detection require **two separate builds**—one with ASan and one with TSan—each running the test suite independently. Volume 5's chapter on TSan covered this "dual build" practice in detail, so we will just focus on the conclusion here.

As for MSan, it is incompatible with both ASan and TSan (it requires all code to be "clean" and run through its own uninitialized memory tracking), and it only supports Clang. Consequently, it is the least used in real-world projects. LSan and UBSan are the two "versatile" options—LSan has almost zero overhead and can be a permanent fixture, while most of UBSan's sub-options can also be enabled alongside ASan.

## In Practice: ASan Catches Three Typical Errors

Just talking about principles isn't satisfying. Let's write code for the three types of memory errors that最容易 trip us up in C++, and let ASan catch them one by one. The following three sections are based on actual local runs.

### Heap Use-After-Free

Smart pointers can prevent most use-after-free (UAF) issues, but as long as a project still contains raw pointers or C-style APIs, this hole cannot be completely plugged. Here is a minimal example—we release a `unique_ptr`, but then hold onto the raw pointer it previously returned to read data:

```cpp
// ... code block would follow ...
```

```cpp
// uaf.cpp —— use-after-free
// Platform: host    Standard: C++20
// 编译: g++ -std=c++20 -O1 -fsanitize=address -g uaf.cpp -o uaf
#include <cstdio>
#include <memory>

int main()
{
    auto p = std::make_unique<int>(42);
    int* raw = p.get();     // 拿到裸指针
    p.reset();              // 这里释放 —— raw 立刻变成悬空指针
    std::printf("悬空指针读到的值: %d\n", *raw);   // use-after-free
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

This report is the most valuable part of ASan. It doesn't just tell you "the read on line 12 is a use-after-free"; it simultaneously presents **two phases of this memory's history**: allocated by `make_unique` at `uaf.cpp:9` and freed by `reset` at `uaf.cpp:11`. Looking at these two lines, the causal chain of the bug becomes complete—this is exactly the value of the quarantine and redzone mechanisms: freed memory is marked as `fd` instead of being immediately reclaimed, so subsequent erroneous reads can collide with it.

That `[fd]` in the shadow dump is the smoking gun:

```text
=>0x7a948abe0000: fa fa[fd]fa fa fa fa fa ...
```

`fd` = freed heap region. This is the result of ASan's "bookkeeping" capabilities.

### Global Buffer Overflow

Global and static variables are also protected by redzones. ASan will reliably detect out-of-bounds access to global arrays as well:

```cpp
// global_oob.cpp —— 全局数组越界
// 编译: g++ -std=c++20 -O1 -fsanitize=address -g global_oob.cpp -o global_oob
#include <cstdio>
int g[4] = {1, 2, 3, 4};
int main() { std::printf("g[5] = %d\n", g[5]); }
```

```text
==38356==ERROR: AddressSanitizer: global-buffer-overflow on address 0x63ca65acd074 ...
SUMMARY: AddressSanitizer: global-buffer-overflow global_oob.cpp:5 in main
```

The error type is explicitly marked as `global-buffer-overflow`. ASan uses different redzone encodings to distinguish between stack, heap, and global regions (`f1`/`f2` for stack, `fa` for heap, `f9` for global), so we can see at a glance which type of memory the out-of-bounds access occurred on.

::: warning Regarding the claim that "Global OOB requires Clang 11"
Some older resources might claim that "ASan requires Clang 11 or higher to detect global variable out-of-bounds access." The historical context for this is that early ASan support for global variable redzones was incomplete; Clang 11 introduced improvements like the ODR indicator (`-fsanitize-address-use-odr-indicator`) to solidify global detection. However, **today**—with GCC 8.3+ and current Clang versions—global out-of-bounds detection is enabled by default and works out of the box. The example above was caught immediately with default settings on the local GCC 16.1.1. Therefore, this "version threshold" is obsolete for modern toolchains, so don't be misled by outdated documentation.
:::

### Leaks: LSan Wraps Up at Exit

Finally, let's look at memory leaks. LSan operates differently than the previous sanitizers—it doesn't report errors during execution. Instead, when `main` returns and the program is about to exit, it scans all "live" heap allocations and flags those that are no longer referenced or have not been freed. Let's write a minimal example that intentionally leaks:

```cpp
// leak.cpp —— 故意泄漏
// Platform: host    Standard: C++20
// 编译: g++ -std=c++20 -O1 -fsanitize=address -g leak.cpp -o leak
#include <cstdlib>
#include <cstdio>
int main()
{
    int* p = (int*)std::malloc(sizeof(int) * 4);  // 拿了堆内存
    p[0] = 42;
    std::printf("ptr = %p\n", (void*)p);  // 让指针逃逸,防止整段被优化器删掉
    // 没有 free,程序退出时 p 指向的内存泄漏
}
```

Running with ASan (GCC 16.1.1 / WSL2, LSan is enabled by default with ASan):

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

Note that the report is printed **after** `main` returns. This is how LSan's "cleanup on exit" mechanism works. Volume 1's [Dynamic Memory Management](../vol1-fundamentals/ch12/02-new-delete.md) also provided an equivalent example for comparison.

::: warning The "Silent Exit" Trap of LSan
In mainstream Linux environments (GCC 16.1.1 / Clang 22), LSan is enabled by default alongside ASan, and the example above can be caught reliably on a local machine. However, be wary of a real-world pitfall: **leaks are only scanned when the process exits normally**. If your program is killed by `SIGKILL`, calls `_exit` to bypass `atexit` hooks, or runs in certain containers/sandboxes where LSan's exit hooks don't execute, the leak report will **silently vanish**. The program appears to have "no errors," but in reality, LSan never had a chance to scan.

Troubleshooting steps: Confirm the process exits via a normal `return`. If necessary, explicitly force it on with `ASAN_OPTIONS=detect_leaks=1`. Long-running services (which never exit) cannot use LSan's "cleanup on exit" model; you must switch to Valgrind Massif or heap sampling instead. Do not assume that "no report from LSan means no leaks."
:::

## UBSan: Turning "Undefined Behavior" from Silent Failures into Errors

Having covered the mainstays of the ASan family, let's look at UBSan, the finisher. C/C++ has a blood-pressure-raising feature: **Undefined Behavior (UB)**. The compiler's attitude toward UB is: "Since the standard doesn't specify what happens, I'll assume it doesn't happen and optimize freely." The consequence is that errors like signed integer overflow, out-of-bounds shifts, and null pointer dereferences often **appear to run perfectly fine**—until one day you turn on `-O2` or switch compiler versions. The optimizer, relying on the assumption that "this won't overflow," performs aggressive transformations, and the program suddenly produces outrageous results.

UBSan's strategy is to insert a runtime check next to every operation that could produce UB. If UB actually occurs, it immediately prints a `runtime error: ...` report (by default, it does not abort the program, but this can be configured). The overhead is low, and many sub-features can coexist with ASan.

Let's look at a minimal example that packs three classic types of UB into one:

```cpp
// ubsan.cpp —— UBSan 捕获未定义行为
// Platform: host    Standard: C++20
// 编译: g++ -std=c++20 -O1 -fsanitize=undefined -g ubsan.cpp -o ubsan
#include <cstdio>
#include <limits>

int main()
{
    int arr[4]{1, 2, 3, 4};
    int idx = 10;
    std::printf("越界下标 arr[10] = %d\n", arr[idx]);   // 下标越界

    int max = std::numeric_limits<int>::max();
    std::printf("有符号溢出: %d\n", max + 1);           // 有符号整数溢出

    int shift = 32;
    std::printf("左移 32 位: %d\n", 1 << shift);        // 位移量 >= 位宽
}
```

Run with UBSan:

```text
ubsan.cpp:11:55: runtime error: index 10 out of bounds for type 'int [4]'
ubsan.cpp:11:16: runtime error: load of address 0x7ffe8a0525c8 with insufficient space for an object of type 'int'
ubsan.cpp:14:16: runtime error: signed integer overflow: 2147483647 + 1 cannot be represented in type 'int'
ubsan.cpp:17:42: runtime error: shift exponent 32 is too large for 32-bit type 'int'
```

All three UBs were caught, pinpointed exactly to `file:line:column`. The list of UBs covered by UBSan is extensive. Common ones include:

- **Arithmetic**: Signed integer overflow/underflow, division by zero.
- **Shift**: Shift amount negative or greater than/equal to bit width, left shift changing the sign bit.
- **Memory/Pointer**: Null pointer dereference, misaligned memory access, object size mismatch (accessing via wrong pointer type).
- **Array**: Out-of-bounds subscripts (`-fsanitize=bounds`). This overlaps with ASan's OOB detection but focuses differently—ASan looks at redzones, while UBSan looks at array sizes known at compile time.

The overhead of UBSan depends on which sub-options you enable. `-fsanitize=undefined` is a collection of default sub-options, most of which are lightweight. The truly expensive one is `-fsanitize=integer` (which treats unsigned overflow as an error, high overhead, many false positives, use with caution in production). Recommendation for daily use: enable `-fsanitize=undefined` alongside ASan. It has a low cost and high yield.

## Selection: Which Tool to Use for a Memory Bug

Now that all five siblings have been introduced. The question arises—when you are face-to-face with a weird bug, in what order should you select your tools? We locate them by "symptom":

- **Crashes on run / Segfault / Intermittent crashes**: Start with ASan and run reproduction tests. OOB, UAF, and double-free are the three most common causes of segfaults; ASan catches them all.
- **Intermittent incorrect results / Weird values across functions**: Suspect UAF or data race. First, use ASan to rule out UAF. If ASan is silent, build a separate TSan version to check for data races (remember, the two are mutually exclusive and cannot be enabled simultaneously).
- **Calculated values are ridiculous / Behavior changes under `-O2`**: Almost certainly a UB lock; go straight to UBSan.
- **Reading "looks normal" garbage values / Behavior depends on uninitialized data**: MSan (Note: Clang only, requires full program compilation).
- **Process eats more and more memory / Suspected leak**: LSan (reports on exit), or for long-running services, use Valgrind massif / heap sampling.

A sound engineering practice is to **have two builds resident in CI**—one set with `ASan+UBSan` and one with `TSan`, running on every commit. The overhead is acceptable (ASan+UBSan is in the 2-4x range), and it buys you the ability to catch the most expensive bugs—like "intermittent crashes after deployment"—before they leave the building.

::: warning ASan Is Not a Silver Bullet
ASan is powerful, but it has unavoidable limitations that you must keep in mind.

First, **it only catches paths that are "actually executed"**. CTI is runtime detection; if code isn't run, the check won't trigger. If your test coverage is insufficient and a certain OOB path is never triggered, ASan won't catch it—this is exactly why ASan should be paired with good test cases, or even fuzzing. Fuzzing is responsible for running out rare paths, and ASan is responsible for reporting the moment an error occurs on those paths.

Second, **it only catches memory errors**. Logic errors (calculation mistakes), concurrency errors (data races), and integer overflow UB are not ASan's concern—the latter belongs to UBSan, the former to TSan. Don't expect one flag to solve all problems.

Third, **do not enable in production**. 2-4x slowdown and extra memory are a disaster under production load. ASan/UBSan/TSan are tools for the **development/testing/CI stages**; these flags must be removed in release builds.

Fourth, **it has false positive boundaries**. Certain custom stack unwinding mechanisms (`swapcontext`, `vfork`) can cause ASan's shadow region logic to fail and report false positives. The line `HINT: this may be a false positive if your program uses some custom stack unwind mechanism` in the report is a reminder of this.
:::

## Summary

Starting from the Heartbleed over-read vulnerability that terrified the world, we've dissected the ASan tool family in this article. Let's wrap up with a few key conclusions:

- **The ASan Trio**: Compile-time instrumentation (CTI) rewrites every memory access; shadow memory uses a 1:8 shadow byte to record the accessibility state of every 8 bytes of application memory; a runtime library replaces `new`/`delete` and uses quarantine to isolate freed memory. Overhead is 2-4x slowdown, far lower than Valgrind's 20-50x, making it viable for permanent CI residence.
- **Shadow Memory Encoding**: `00` accessible, `01`-`07` partially accessible, `fa` heap redzone, `fd` freed, `f1`/`f2` stack redzone, `f9` global redzone. The shadow dump at the end of an ASan report is a visual representation of this encoding.
- **Tool Family**: ASan (OOB/UAF), LSan (leaks), MSan (uninitialized reads, Clang only), TSan (data races), UBSan (UB). **ASan and TSan are mutually exclusive**, so maintain separate builds in CI.
- **UBSan is the Finisher**: Low overhead, can coexist with ASan, turning "silent UBs" like signed overflow, shift out-of-bounds, and null pointer dereferences into explicit `runtime error`s.
- **Selection Mnemonic**: For segfaults, use ASan first. For intermittent errors, rule out UAF with ASan then check races with TSan. For ridiculous arithmetic, use UBSan. For memory growth, check LSan. Always disable in production environments.

True memory safety isn't achieved by catching bugs with tools, but by guarding against them at the syntactic level using RAII, smart pointers, `std::span`, and range `for` loops—mechanisms that make it physically impossible to write OOB or dangling accesses. Those are the topics of vol1 and vol3. The value of the ASan toolset lies in the transition period—before you've replaced every raw pointer and wrapped third-party C libraries. It acts as the "last line of defense," forcing latent memory bugs to reveal themselves during development rather than detonating at 3 AM in production.

## Reference Resources

- [AddressSanitizer · google/sanitizers Wiki](https://github.com/google/sanitizers/wiki/AddressSanitizer) — Official ASan documentation, the authoritative source for shadow memory mechanisms, 1:8 mapping, and 2x overhead.
- [Clang: AddressSanitizer](https://clang.llvm.org/docs/AddressSanitizer.html) — Clang-specific ASan docs, including global detection evolution like `-fsanitize-address-use-odr-indicator`.
- [Clang: ThreadSanitizer](https://clang.llvm.org/docs/ThreadSanitizer.html) — TSan documentation, source for ASan↔TSan mutual exclusion (see vol5 Concurrency Debugging).
- [Clang: UndefinedBehaviorSanitizer](https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html) — List of UBSan sub-options and overhead.
- [Valgrind User Manual](https://valgrind.org/docs/manual/manual.html) — DBI methodology and Memcheck/Helgrind, reference for the 20-50x overhead comparison.
