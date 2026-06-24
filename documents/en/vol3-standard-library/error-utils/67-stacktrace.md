---
chapter: 7
cpp_standard:
- 23
description: 'Here is the translation of the description, optimized for a technical
  tutorial context:


  "A deep dive into `std::stacktrace`: how to capture runtime call stacks, linking
  pitfalls with `libstdc++exp`, symbolization differences between stripped and unstripped
  binaries, and when to use it versus compile-time `source_location`.'
difficulty: intermediate
order: 67
platform: host
prerequisites:
- error_code：错误码体系与自定义 category
- expected：值或错误，C++23 的错误处理新范式
reading_time_minutes: 14
related:
- source_location：编译期代码位置，__FILE__ 的类型安全替代
tags:
- host
- cpp-modern
- intermediate
- 基础
title: 'stacktrace: C++23 Finally Standardizes Call Stack Collection'
translation:
  source: documents/vol3-standard-library/error-utils/67-stacktrace.md
  source_hash: 17120e14d29a16ed047ce71a28dab6d0083a479c0bce5cbacc100a5aa6af3ac3
  translated_at: '2026-06-24T00:41:42.720827+00:00'
  engine: anthropic
  token_count: 4289
---
# stacktrace: C++23 Finally Standardizes Call Stack Capture

If you have written server-side or large-scale applications, you have likely hit this pitfall: the program crashes in a specific error branch, and the logs only contain "Processing failed." As for "who called whom and which path led here"—you know nothing. During debugging, you have to guess the call relationships or temporarily scatter `__FILE__` / `__LINE__` points throughout the code.

Before C++23, capturing the call stack (backtrace) at runtime required platform-specific tricks: on Linux, you wrestled with the libc interfaces `backtrace()` / `backtrace_symbols()`, on Windows you used `CaptureStackBackTrace` + `SymFromAddr`, or you simply used `boost::stacktrace` for cross-platform compatibility. Each of these solutions has its own drawbacks—the libc approach does not demangle symbols and requires hooking into `abi::__cxa_demangle`; the Windows symbol engine requires separate initialization. C++23 standardizes this process with the `<stacktrace>` header, providing a cross-platform, type-safe interface for call stack capture. In this article, we will break it down thoroughly and clarify two hard obstacles you will inevitably encounter in real-world projects: **linking libraries** and **symbol dependencies**.

## Establishing Intuition in One Sentence

`std::stacktrace` is a **runtime snapshot of the call stack**: at a specific moment during program execution, it records "all functions on the current path that have not yet returned" in call order, providing the function name, source file, and line number for each frame. Its typical usage is just one line:

```cpp
// Standard: C++23
auto st = std::stacktrace::current();   // 在此刻拍一张栈快照
std::cout << std::to_string(st);        // 打印成 gdb 风格的多行文本
```

Note a key design choice here: `current()` merely **captures the addresses** (program counter PC + frame info); it does **not** perform symbolization. Symbolization (via `description()`, `source_file()`, and `source_line()`) happens on-demand only when you access a specific `stacktrace_entry`. This design of "decoupling capture from symbolization" directly dictates the performance differences discussed later—capturing is cheap, while symbolization is expensive.

## basic_stacktrace and stacktrace_entry: A Two-Layer Structure

The standard library provides two classes with clear responsibilities:

- `std::basic_stacktrace<Allocator>` — A "sequence of frames" that behaves like a `vector`, supporting `size()`, subscript access, and iteration. `std::stacktrace` is an alias for `basic_stacktrace<std::allocator<stacktrace_entry>>`.
- `std::stacktrace_entry` — Represents a single stack frame, corresponding to "one invocation of a function". It is lightweight by design, storing only a program counter internally (`native_handle()`), while symbolic information is calculated on demand when queried.

`stacktrace_entry` has only three member functions that actually retrieve data:

```cpp
// Standard: C++23
std::string description() const;     // demangle 后的可读描述，如 "foo(int)"
std::string source_file() const;     // 源文件路径，无调试符号时为空
std::uint_least32_t source_line() const;  // 源文件行号，无调试符号时为 0
```

Let's run a minimal example to print the members of each frame and see the results:

```cpp
// Standard: C++23
#include <stacktrace>
#include <iostream>
#include <string>

void inspect(int x) {
    auto st = std::stacktrace::current();
    std::cout << "depth = " << st.size() << '\n';
    for (std::size_t i = 0; i < st.size(); ++i) {
        const auto& e = st[i];
        std::cout << "--- entry " << i << " ---\n";
        std::cout << "  native_handle : " << e.native_handle() << '\n';
        std::cout << "  bool(e)       : " << (e ? "true" : "false") << '\n';
        std::cout << "  description   : [" << e.description() << "]\n";
        std::cout << "  source_file   : [" << e.source_file() << "]\n";
        std::cout << "  source_line   : " << e.source_line() << '\n';
    }
}

void caller_a(int v) { inspect(v); }

int main() {
    caller_a(7);
    return 0;
}
```

Compile and run with `g++ -std=c++23 -O0 -g` (local GCC 16.1.1)—note the `-lstdc++exp` at the end. This is the most critical pitfall in this article; we will dedicate the next section to it. For now, let's just get it running:

```text
depth = 6
--- entry 0 ---
  native_handle : 109511442723472
  bool(e)       : true
  description   : [inspect(int)]
  source_file   : [/tmp/st_members.cpp]
  source_line   : 6
--- entry 1 ---
  native_handle : 109511442724282
  bool(e)       : true
  description   : [caller_a(int)]
  source_file   : [/tmp/st_members.cpp]
  source_line   : 20
--- entry 2 ---
  native_handle : 109511442724299
  bool(e)       : true
  description   : [main]
  source_file   : [/tmp/st_members.cpp]
  source_line   : 23
--- entry 3 ---
  native_handle : 123497777100608
  bool(e)       : true
  description   : []
  source_file   : []
  source_line   : 0
--- entry 4 ---
  native_handle : 123497777100920
  bool(e)       : true
  description   : [__libc_start_main]
  source_file   : []
  source_line   : 0
--- entry 5 ---
  native_handle : 109511442723204
  bool(e)       : true
  description   : [_start]
  source_file   : []
  source_line   : 0
```

Here are a few points worth noting. First, the top of the stack (entry 0) is **the function executing `current()` itself**. Below that are the successive callers, all the way down to `_start` (the actual entry point of the program) and `__libc_start_main` (the C runtime). Second, the further down we go, the more "unknowable" it becomes—libc and `_start` lack debug symbols, so their `source_file` and `source_line` fields are empty. There is even a completely `<unknown>` frame sandwiched in between (entry 3, usually an internal trampoline in libc). This reflects the reality of stack collection: **frames for our own code yield full information, but the deeper we go into the runtime, the more it becomes a black box**. Do not expect every frame to be complete.

## The First Real Hurdle: The `libstdc++exp` Library

Now let's look back at that `-lstdc++exp`. This is the first hurdle for beginners, and almost everyone gets tripped up by it. If you compile directly using your usual habits:

```text
$ g++ -std=c++23 -O2 -g st_members.cpp -o st_members
/usr/bin/ld: .../stacktrace:209:(.text+0x4a):
  undefined reference to `std::__stacktrace_impl::_S_current(...)'
/usr/bin/ld: .../stacktrace:167:(.text._ZStlsRSoRKSt16stacktrace_entry+0xc1):
  undefined reference to `std::stacktrace_entry::_Info::_M_populate(unsigned long)'
collect2: error: ld returned 1 exit status
```

The compilation succeeded, but linking failed. The error reports two missing symbols: `_S_current` (stack trace collection implementation) and `_M_populate` (symbolization implementation). This is because libstdc++ **does not** include the `<stacktrace>` implementation in the default `libstdc++.so` linked by default. Since collection and symbolization involve platform-specific low-level logic (backtrace / dladdr / DWARF parsing), which adds significant size, the standard library splits it into a separate library that must be linked explicitly by the user.

::: warning Explicit linking of the experimental library is required
The libstdc++ `<stacktrace>` implementation resides in the **experimental library**, which is not linked by default. The GCC toolchain convention is as follows:

- **GCC 16 and later** (tested with local GCC 16.1.1): The library name is `libstdc++exp`, and the link flag is `-lstdc++exp` (note that it is `exp`, **without an underscore**).
- **Early documentation for GCC 14 / 15** often writes it as `-lstdc++_exp` (with an underscore). If your toolchain is an older version, keep the underscore; for newer versions, remove it.

Local testing shows that `-lstdc++_exp` directly reports `cannot find -lstdc++_exp: No such file or directory`, and it only works when changed to `-lstdc++exp`. The difference between the two commands is just a single character, but it can stall you for a long time.

Additionally, on your machine, this library **only has a static version `libstdc++exp.a`, not a `.so`**. Therefore, the stacktrace implementation is **statically linked into your binary**, which does not increase runtime dynamic dependencies—this is good for deployment, but the trade-off is that the binary size increases by a few dozen KB.
:::

A complete compilation command looks like this:

```text
g++ -std=c++23 -O2 -g your_code.cpp -o your_app -lstdc++exp
```

If we use CMake, the equivalent is:

```cmake
target_link_libraries(your_app PRIVATE stdc++exp)
```

Note that `-lstdc++exp` must be placed **after** the source files. The GCC linker processes dependencies in order, so the library must appear after the "object that needs it," otherwise symbols will fail to resolve. This is a classic linking order pitfall.

## The Second Hard Pitfall: Debug Symbols Determine What You Get

The linking is done, and it runs—but you will soon discover: why are `source_file` and `source_line` sometimes empty? This section answers that question.

The key is that the information `<stacktrace>` can provide comes from **two distinct data sources**, each relying on different things:

| Information | Data Source | Depends On |
|------|--------|----------|
| Function name (`description`) | Runtime symbol table (`.symtab` / `.dynsym`) | Symbols not stripped, or exported via `-rdynamic` |
| Source file + line number (`source_file` / `source_line`) | DWARF debug info (`.debug_*` sections) | Compiled with `-g` |

Function names come from the symbol table, while source files and line numbers come from debug information—these are two independent things. Let's perform a comparative experiment with the same program compiled in three different ways:

**With `-g` (has debug info)**: The output above shows `source_file` and `source_line` are fully present.

**Without `-g` (no debug info, but the symbol table remains)**:

```text
--- entry 0 ---
  native_handle : 95551312581264
  description   : [inspect(int)]
  source_file   : []
  source_line   : 0
```

We can retrieve the function names, but the source file and line numbers are completely empty—because the `.debug_line` section is missing, addresses cannot be mapped back to source locations.

**Stripping the symbol table** (`g++ ... -g` followed by `strip`): the `description` for all frames is completely empty, leaving only bare addresses:

```text
--- entry 0 ---
  native_handle : 111239407198864
  description   : []
  source_file   : []
  source_line   : 0
```

`strip` removes the `.symtab` section, so function names cannot be resolved. However, if we add `-rdynamic` during linking (which exports symbols to the `.dynsym` dynamic symbol table, which `strip` does not remove), the function names will be available again:

```text
--- entry 0 ---
  description   : [inspect(int)]    # strip 后, 但链接时带了 -rdynamic
  source_file   : []                # 调试信息还是没了, 行号拿不到
```

This is a real-world engineering trade-off. Our advice is straightforward:

::: warning To get complete stack information, prepare the data source at compile time

- **To get source file + line numbers**: You must compile with `-g` (or `-g3` for more detail). For release builds where you want to keep localization capabilities, you can use `objcopy --only-keep-debug` to save debug information into a separate file, and use `addr2line -e app <addr>` for post-mortem analysis.
- **To get function names (after stripping)**: Add `-rdynamic` during linking to place symbols in `.dynsym`. The cost is a larger binary and visible symbols (weigh this if you are concerned about information leakage).
- **Minimal stack information for production**: At least include `-rdynamic`. This way, even without debug info or even if stripped, `description()` can still provide function names, avoiding a wall of `<unknown>`.
:::

### Raw mangled symbols vs. description: Why we need to demangle

Here is a point that beginners often confuse. Because C++ has function overloading and namespaces, the compiler "mangles" function names into an internal representation. For example, `my_lib::compute_value(int, int)` is actually stored in the symbol table as `_ZN6my_lib13compute_valueEii`—which is completely unreadable to humans.

`stacktrace_entry::description()` has already **demangled** the output for you, returning human-readable text directly. Let's compare this with `dladdr` (the libc address-to-symbol query interface) to see the difference between "raw" and "demangled":

```cpp
// Standard: C++23
#include <stacktrace>
#include <iostream>
#include <dlfcn.h>      // dladdr
#include <cxxabi.h>     // abi::__cxa_demangle
#include <cstdlib>

namespace my_lib {
    int compute_value(int a, int b) {
        auto st = std::stacktrace::current();
        auto e = st[0];
        // stacktrace_entry 已经 demangle 过的描述
        std::cout << "description            : " << e.description() << '\n';

        // 用 dladdr 拿原始 mangled 符号做对比
        Dl_info info{};
        dladdr(reinterpret_cast<void*>(e.native_handle()), &info);
        std::cout << "dladdr dli_sname(原始) : " << (info.dli_sname ? info.dli_sname : "<null>") << '\n';

        // 手动 demangle 原始符号
        int status = 0;
        char* demangled = abi::__cxa_demangle(info.dli_sname, nullptr, nullptr, &status);
        std::cout << "手动 demangle          : " << (demangled ? demangled : "<null>") << '\n';
        std::free(demangled);
        return a + b;
    }
}

int main() {
    return my_lib::compute_value(1, 2) - 3;
}
```

Running `g++ -std=c++23 -O0 -g -rdynamic ... -lstdc++exp -ldl` produces:

```text
description            : my_lib::compute_value(int, int)
dladdr dli_sname(原始) : _ZN6my_lib13compute_valueEii
手动 demangle          : my_lib::compute_value(int, int)
```

The difference is obvious. `_ZN6my_lib13compute_valueEii` is the compiler's internal mangled name (starting with `_ZN`, which is the g++ C++ name identifier, followed by the encoded namespace, function name, and parameter types), making it practically unreadable to the human eye. Internally, `stacktrace_entry::description()` uses the `abi::__cxa_demangle` routine to directly provide `my_lib::compute_value(int, int)`. Therefore, in daily use of `<stacktrace>`, you don't need to demangle names yourself—it handles this for you. You only need `dladdr` to fetch the raw mangled name when performing specific operations on the "raw symbol string" (such as with certain symbol matching tools).

## `to_string` vs `operator<<`: Two Printing Methods

There are two built-in ways to print an entire stack trace.

The first is `std::to_string(stacktrace)`—note that this is a **free function**, not a member of `stacktrace` (writing `st.to_string()` will fail to compile). It returns a multi-line string in a GDB-style format:

```cpp
// Standard: C++23
#include <stacktrace>
#include <iostream>
void level3() { auto st = std::stacktrace::current(); std::cout << std::to_string(st); }
void level2() { level3(); }
void level1() { level2(); }
int main() { level1(); }
```

The output looks like this:

```text
   0#  level3() at /tmp/st_tostring.cpp:3 [0x57a7e83ed2cd]
   1#  level2() at /tmp/st_tostring.cpp:4 [0x57a7e83ed373]
   2#  level1() at /tmp/st_tostring.cpp:5 [0x57a7e83ed37f]
   3#  main at /tmp/st_tostring.cpp:6 [0x57a7e83ed38b]
   4#  <unknown> [0x7fe05d227740]
   5#  __libc_start_main [0x7fe05d227878]
   6#  _start [0x57a7e83ed1c4]
```

`序号#` + function + `at file:line` + `[address]`, which reads almost exactly like a GDB backtrace output. This is a format intentionally aligned by the standard library. If you need to stuff an entire stack into a log, this is the most convenient approach.

The second method is `operator<<`—which has two overloads: one outputs a single `stacktrace_entry` on a single line, and the other for the entire `basic_stacktrace` is equivalent to `to_string`. The output format for a single entry is " function at file:line [address]" (note the leading space, as mandated by the standard):

```text
 foo(int) at /tmp/st_basic.cpp:6 [0x5b11f3c25e90]
```

`to_string` is suitable when we need a complete block to stuff into a log, while `operator<<` is better for streaming output or custom formatting. Under the hood, both rely on the same symbolization logic and produce identical output; they simply differ in their level of encapsulation.

## Real-world: Capturing a Stack Trace in a Crash Handler

The true value of `<stacktrace>` shines in crash diagnostics. When a program receives a fatal signal like `SIGSEGV`, capturing a snapshot of the stack inside the signal handler is far better than a silent crash where nothing is logged.

```cpp
// Standard: C++23
#include <stacktrace>
#include <iostream>
#include <csignal>
#include <cstdlib>

void log_stacktrace() {
    auto st = std::stacktrace::current();
    std::cerr << "=== stacktrace on crash ===\n";
    std::cerr << std::to_string(st);
}

void broken(int* p) {
    *p = 42;   // 故意空指针解引用, 触发 SIGSEGV
}

void outer(int n) {
    if (n == 0) broken(nullptr);
    outer(n - 1);
}

int main() {
    std::signal(SIGSEGV, [](int) {
        log_stacktrace();
        std::_Exit(1);   // 用 _Exit 避免析构链再出问题
    });
    outer(3);
    return 0;
}
```

Running `g++ -std=c++23 -O0 -g ... -lstdc++exp` yields:

```text
=== stacktrace on crash ===
   0#  log_stacktrace() at /tmp/st_crash.cpp:7 [0x5a3f0683c2ce]
   1#  operator() at /tmp/st_crash.cpp:23 [0x5a3f0683c3d9]
   2#  _FUN at /tmp/st_crash.cpp:25 [0x5a3f0683c3fd]
   3#  <unknown> [0x7b645b63e8ef]
   4#  broken(int*) at /tmp/st_crash.cpp:13 [0x5a3f0683c391]
   5#  outer(int) at /tmp/st_crash.cpp:17 [0x5a3f0683c3b4]
   6#  outer(int) at /tmp/st_crash.cpp:18 [0x5a3f0683c3c1]
   7#  outer(int) at /tmp/st_crash.cpp:18 [0x5a3f0683c3c1]
   8#  outer(int) at /tmp/st_crash.cpp:18 [0x5a3f0683c3c1]
   9#  main at /tmp/st_crash.cpp:26 [0x5a3f0683c44a]
  10#  <unknown> [0x7b645b627740]
  11#  __libc_start_main [0x7b645b627878]
  12#  _start [0x5a3f0683c1c4]
```

This stack trace directly tells us the crash occurred in `broken`, having been called recursively from `main` through `outer`—making it easy to pinpoint the issue during troubleshooting. Here are a few points to note for real-world engineering:

- The top few frames are the **signal handler itself** (`log_stacktrace`, the lambda's `operator()`, `_FUN`, and the kernel's `sigreturn` trampoline `<unknown>`). The actual crash point is in the `broken` frame **below** them. When reading a crash stack, remember to skip the handler's own frames first.
- The signal handler runs in an **asynchronous signal context**, not a normal function call. `std::to_string` allocates memory internally (`new` / `malloc`), so strictly speaking, calling non-async-signal-safe functions within a signal handler is risky. This example uses `std::_Exit` (async-signal-safe) to exit, reducing the risk; for absolutely critical scenarios, a more robust approach is to only set a flag in the handler and collect the trace in the main loop, or use `sigaltstack` with a dedicated handling stack. However, as a lightweight "crash evidence" solution, the code above is widely sufficient in practice.
- To ensure the stack frames in the handler include line numbers, the crashed binary must also be compiled with `-g`; otherwise, the `broken` frame will show only the function name.

## Performance: Capture is cheap, symbolization is expensive

Earlier, we left a hint: `current()` only captures addresses, while symbolization happens only when the entry is accessed. This means the overhead is split into two phases with vastly different orders of magnitude. Let's run a benchmark—capturing after 5 levels of recursion (depth 6), measuring "capture only" versus "capture + full `to_string` symbolization," running each 100,000 times:

```cpp
// Standard: C++23
#include <stacktrace>
#include <iostream>
#include <chrono>
#include <string>

void deep(int n) {
    if (n == 0) {
        auto st = std::stacktrace::current();
        volatile auto sz = st.size();   // 防止被优化掉, 但不触发符号化
        (void)sz;
        return;
    }
    deep(n - 1);
}

int main() {
    constexpr int kIters = 100000;
    for (int i = 0; i < 1000; ++i) deep(5);   // 预热

    // 只采集
    auto t1 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < kIters; ++i) deep(5);
    auto t2 = std::chrono::high_resolution_clock::now();
    double ns_capture =
        std::chrono::duration<double, std::nano>(t2 - t1).count() / kIters;

    // 采集 + 全符号化
    int sink = 0;
    t1 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < kIters; ++i) {
        auto st = std::stacktrace::current();
        std::string s = std::to_string(st);
        sink += static_cast<int>(s.size());
    }
    t2 = std::chrono::high_resolution_clock::now();
    double ns_full =
        std::chrono::duration<double, std::nano>(t2 - t1).count() / kIters;

    std::cout << "depth=6, iters=" << kIters << '\n';
    std::cout << "capture-only       : " << ns_capture << " ns/call\n";
    std::cout << "capture + to_string: " << ns_full << " ns/call\n";
    std::cout << "sink=" << sink << '\n';
    return 0;
}
```

`g++ -std=c++23 -O2 -g ... -lstdc++exp`, run twice to check stability:

```text
depth=6, iters=100000
capture-only       : 841.43 ns/call
capture + to_string: 2145.12 ns/call
sink=15900000

depth=6, iters=100000
capture-only       : 852.22 ns/call
capture + to_string: 1954.18 ns/call
sink=15900000
```

The numbers speak for themselves. On the local machine (x86-64, GCC 16.1.1, `-O2`), the orders of magnitude are:

- **Capture only**: Approximately **0.8 µs / call**. With a depth of only six, the overhead is mainly in traversing stack frames and reading return addresses. Taking a snapshot occasionally on a hot path is perfectly acceptable.
- **Capture + Full Symbolization**: Approximately **2 µs / call**, which is two to three times the cost of capture only. The extra overhead comes from demangling, string concatenation, and memory allocation (`std::string`). The deeper the stack and the longer the symbols, the more this cost increases.

::: warning Absolute values vary by machine; orders of magnitude are stable
The above measurements were taken on an idle local machine. Absolute values fluctuate with CPU load, stack depth, and symbol length (the symbolization time varied by nearly 10% between two runs). However, the **order of magnitude relationship is stable**: symbolization costs several times more than pure capture, and both are far more expensive than a normal function call (nanosecond scale). The conclusion is—**don't casually call `to_string` in a hot loop**; only symbolize on error paths or diagnostic paths.
:::

This split—"capture is cheap, symbolization is expensive"—is the design motivation behind the standard library's decoupling of the two. You can first use `current()` to obtain a lightweight stack snapshot and store it (almost zero cost), and only perform `to_string` when you actually need to diagnose. For example, you can push the captured `stacktrace` object into a logging queue and let a background thread slowly symbolize it without blocking the business logic. If symbolization and capture were tightly bound from the start, this deferred symbolization would be impossible.

## Comparison with `source_location`: When to use which

`<stacktrace>` has a very similar sibling—C++20's `std::source_location` (Volume 68). Both can tell you "where the code is," but their positioning is completely different. **Don't mix them up**:

| Dimension | `std::stacktrace` (C++23) | `std::source_location` (C++20) |
|-----------|--------------------------|-------------------------------|
| What it provides | Runtime **entire call chain** (multiple frames) | Compile-time **single point** (current function/file/line) |
| When determined | Captured at runtime | Fixed at compile time |
| Overhead | Microsecond level (capture + symbolization) | **Zero overhead** (compile-time constant) |
| Typical use cases | Crash diagnostics, error logging, debug tracing | Logging points, assertions, "where am I" in default parameters |

The most intuitive difference is overhead and granularity. `source_location` is a compile-time constant; the compiler directly fills in `__FILE__`, `__LINE__`, and the function name. Accessing it at runtime is just reading a few constants, which is zero-cost, so it can be safely used in every log line and every assertion. `stacktrace` is captured live at runtime with microsecond-level overhead and should only be used where "something went wrong and it's worth the cost to understand the full context."

A common engineering combination: **Use `source_location` for routine logging** to carry the current function and line number (zero overhead, sufficient for locating a single point); **Switch to `stacktrace` on error/crash paths** to capture the entire call chain (expensive but comprehensive information, worth it). We will cover `source_location` specifically in the next article; for now, establishing this division of intuition is enough.

## Summary

The core of `std::stacktrace` boils down to these points:

- **Two-layer structure**: `basic_stacktrace` (sequence of frames, supports `size()` / indexing / iteration) + `stacktrace_entry` (single frame, query `description()` / `source_file()` / `source_line()` on demand). Capture and symbolization are decoupled—`current()` only grabs addresses, and symbolization happens only when accessing the entry.
- **Linking the library is the first pitfall**: libstdc++'s `<stacktrace>` implementation is in the experimental library and is not linked by default. GCC 16 uses `-lstdc++exp` (no underscore), while older GCC 14/15 documentation used `-lstdc++_exp` (with underscore). If you don't link, you get `undefined reference`; if you link the wrong name, you get `cannot find`. This library only has a static version `libstdc++exp.a`, which will be statically linked into the binary.
- **Debug symbols are the second pitfall**: Function names rely on the symbol table (requires unstripped binary or `-rdynamic`), while source file/line numbers rely on DWARF debug information (requires `-g`). After stripping the symbol table, only raw addresses remain. In production, at least keep `-rdynamic` to preserve function names.
- **`description` is demangled**: For mangled names like `_ZN6my_lib13compute_valueEii`, `description()` automatically restores them to `my_lib::compute_value(int, int)`. You don't need to call `abi::__cxa_demangle` yourself in daily use.
- **Two printing methods**: The free function `std::to_string(st)` produces a gdb-style multi-line string; `operator<<` handles single entries (single line) and the whole stack.
- **Performance**: Capture is about 0.8 µs, symbolization about 2 µs (local machine, depth 6, `-O2`). In terms of orders of magnitude, symbolization is several times more expensive—capture only on hot paths, symbolize only on error paths.
- **Division of labor with `source_location`**: `stacktrace` is runtime full-stack with overhead, used for crashes/diagnostics; `source_location` is compile-time single-point with zero overhead, used for logging points/assertions. They are used together, not as replacements.

At this point, C++23 has finally standardized "runtime call stack capture," a task previously handled differently across platforms. In the next article, we will turn to its zero-overhead brother, `source_location`—to see how the compile-time approach obtains code locations at zero cost.

## Reference Resources

- [cppreference: std::basic_stacktrace (C++23)](https://en.cppreference.com/w/cpp/utility/basic_stacktrace) — `current()` / `size()` / iteration interfaces and the `std::stacktrace` alias
- [cppreference: std::stacktrace_entry (C++23)](https://en.cppreference.com/w/cpp/utility/stacktrace_entry) — Semantics of `description` / `source_file` / `source_line` / `native_handle` (the standard does not have a `symbol()` member)
- [cppreference: std::to_string (stacktrace)](https://en.cppreference.com/w/cpp/utility/basic_stacktrace/to_string) — Output format of the free function `to_string` in gdb style
- [cppreference: `__cpp_lib_stacktrace`](https://en.cppreference.com/w/cpp/feature_test) — Feature test macro, measured value `202011` on local GCC 16.1.1
- [GCC libstdc++ C++23 status](https://gcc.gnu.org/onlinedocs/libstdc++/manual/status.html#iso.2023) — `<stacktrace>` implementation status and experimental library linking conventions
