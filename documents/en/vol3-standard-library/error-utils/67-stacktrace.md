---
chapter: 7
cpp_standard:
- 23
description: A deep dive into std::stacktrace — how it captures the runtime call stack,
  the libstdc++exp linking pitfall, how symbolization differs with and without debug
  symbols and after strip, and where stacktrace versus compile-time source_location
  each belongs
difficulty: intermediate
order: 67
platform: host
prerequisites:
  - 'error_code: The Error Code System and Custom Categories'
  - 'expected: Value or Error, C++23''s New Error Handling Paradigm'
reading_time_minutes: 14
related:
  - 'source_location: Compile-Time Code Location, a Type-Safe Alternative to __FILE__'
tags:
  - host
  - cpp-modern
  - intermediate
  - 基础
title: 'stacktrace: C++23 Finally Standardizes Call Stack Capture'
translation:
  source: documents/vol3-standard-library/error-utils/67-stacktrace.md
  source_hash: 17120e14d29a16ed047ce71a28dab6d0083a479c0bce5cbacc100a5aa6af3ac3
  translated_at: '2026-09-26T01:06:30+00:00'
  engine: anthropic
  token_count: 6100
---
# stacktrace: C++23 Finally Standardizes Call Stack Capture

If you've written server-side code, or any application with a bit of scale, you've definitely hit this pit: the program dies down some error branch, and the log carries a single line "processing failed" — who called whom, which path led here, all unknown. During the post-mortem you're left guessing at call relationships, or hastily scattering `__FILE__` / `__LINE__` markers all over the code.

Before C++23, grabbing the call stack (a backtrace) at runtime meant everyone improvised their own solution: on Linux you wrestled the libc interfaces `backtrace()` / `backtrace_symbols()`; on Windows you reached for `CaptureStackBackTrace` + `SymFromAddr`; for cross-platform code you simply adopted `boost::stacktrace`. Each of these came with its own traps — the libc pair doesn't demangle, so you had to wire up `abi::__cxa_demangle` yourself; on Windows the symbol engine needs separate initialization. C++23 standardizes the whole affair: the `<stacktrace>` header, a cross-platform, type-safe interface for call stack capture. In this article we take it apart completely, and along the way we deal with the two hard pits you are guaranteed to hit in real projects — **the library you must link** and **the symbol dependencies**.

## The Intuition in One Sentence

`std::stacktrace` is a **runtime snapshot of the call stack**: at some instant during execution, it records "every function on the current path that hasn't returned yet", in call order, and each frame hands you a function name, a source file, and a line number. Its typical usage is a single line:

```cpp
// Standard: C++23
auto st = std::stacktrace::current();   // snapshot the stack at this instant
std::cout << std::to_string(st);        // print as gdb-style multi-line text
```

Note the key design choice here: `current()` only **captures addresses** (the program counter PC plus frame information); it does **not** symbolize. Symbolization (`description()` / `source_file()` / `source_line()`) happens on demand, only when you access a given `stacktrace_entry`. This decoupling of capture and symbolization directly drives the performance differences we'll measure later — capture is cheap, symbolization is what costs.

## basic_stacktrace and stacktrace_entry: A Two-Layer Structure

The standard library gives us two classes with a clear division of labor:

- `std::basic_stacktrace<Allocator>` — a "sequence of frames" that, much like `vector`, supports `size()`, indexing, and iteration. `std::stacktrace` is an alias for `basic_stacktrace<std::allocator<stacktrace_entry>>`.
- `std::stacktrace_entry` — a single stack frame, representing "one invocation of some function". It is itself very lightweight — internally it stores just a program counter (via `native_handle()`); symbol information is computed on demand at query time.

Of `stacktrace_entry`'s query interface, only three members actually fetch data:

```cpp
// Standard: C++23
std::string description() const;     // human-readable demangled description, e.g. "foo(int)"
std::string source_file() const;     // source file path; empty when there are no debug symbols
std::uint_least32_t source_line() const;  // source line number; 0 when there are no debug symbols
```

Let's run a minimal example that prints every member of every frame:

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

Compile and run with `g++ -std=c++23 -O0 -g` (our local GCC 16.1.1) — mind the `-lstdc++exp` at the end of the command; it is the single most important pit of this article, and the next section is devoted to it. For now, just build with it and get things running:

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

Several things worth noting. First, the top of the stack (entry 0) is **the function currently executing `current()`** itself; below it come the callers, level by level, all the way down to `_start` (the program's real entry point) and `__libc_start_main` (the C runtime). Second, the deeper you go, the more "unknowable" it gets — libc and `_start` carry no debug symbols, so their `source_file` / `source_line` come back empty, and one frame in between is entirely `<unknown>` (entry 3, usually a trampoline inside libc). This is simply what stack capture really looks like: **your own code's frames come with full information, while the deeper into the runtime you go, the more of a black box it becomes** — don't expect every frame to be complete.

## The First Hard Pit: Linking libstdc++exp

Now let's go back to that `-lstdc++exp`. It's the first hurdle for newcomers — almost everyone trips on it. If you compile the way you always do:

```text
$ g++ -std=c++23 -O2 -g st_members.cpp -o st_members
/usr/bin/ld: .../stacktrace:209:(.text+0x4a):
  undefined reference to `std::__stacktrace_impl::_S_current(...)'
/usr/bin/ld: .../stacktrace:167:(.text._ZStlsRSoRKSt16stacktrace_entry+0xc1):
  undefined reference to `std::stacktrace_entry::_Info::_M_populate(unsigned long)'
collect2: error: ld returned 1 exit status
```

Compilation succeeds; linking fails. The errors say two symbols can't be found: `_S_current` (the stack-capture implementation) and `_M_populate` (the symbolization implementation). The reason is that libstdc++ does **not** compile the `<stacktrace>` implementation into the `libstdc++.so` linked by default — capture and symbolization involve platform-specific low-level machinery (backtrace / dladdr / DWARF parsing), which is not small, so the standard library splits it out into a separate library: whoever uses it, links it.

::: warning You must explicitly link the experimental library
libstdc++'s `<stacktrace>` implementation lives in the **experimental library**, which is not linked by default. The GCC toolchain convention:

- **GCC 16 and later** (verified on our local GCC 16.1.1): the library is named `libstdc++exp`, link flag `-lstdc++exp` (note it's `exp`, **no underscore**).
- **Early GCC 14 / 15 documentation** often wrote it as `-lstdc++_exp` (with an underscore). If your toolchain is still an older version, keep the underscore as before; newer versions switched to the form without it.

Verified locally: `-lstdc++_exp` fails outright with `cannot find -lstdc++_exp: No such file or directory`; switching to `-lstdc++exp` makes it pass. The two commands differ by exactly one character, yet that character can cost you half a day.

Also, on this machine the library ships **only as the static `libstdc++exp.a`, with no `.so`**. So the stacktrace implementation gets **statically linked into your binary** and adds no runtime dynamic dependency — good news for deployment, at the price of a binary some tens of KB larger.
:::

A complete compile command looks like this:

```text
g++ -std=c++23 -O2 -g your_code.cpp -o your_app -lstdc++exp
```

If you use CMake, the equivalent is:

```cmake
target_link_libraries(your_app PRIVATE stdc++exp)
```

Note that `-lstdc++exp` must come **after** the source files — GCC's linker resolves dependencies in order, so a library must appear after "the objects that need it", or the symbols still won't resolve. This is another classic link-order pit.

## The Second Hard Pit: Debug Symbols Decide What You Get

Linking is sorted, the program runs — and soon you'll wonder: why are `source_file` and `source_line` sometimes empty? This section answers that.

The key point: the information `<stacktrace>` can give you comes from **two layers of data sources**, each with its own dependency:

| Information | Data source | What it depends on |
|------|--------|----------|
| Function name (`description`) | Runtime symbol table (`.symtab` / `.dynsym`) | Symbols not stripped, or exported via `-rdynamic` |
| Source file + line number (`source_file` / `source_line`) | DWARF debug info (`.debug_*` sections) | Compiled with `-g` |

Function names come from the symbol table; source files and line numbers come from debug info — two independent things. Let's just run the comparison experiment, compiling the same program three different ways:

**With `-g` (debug info present)**: the output above — `source_file` / `source_line` all present.

**Without `-g` (no debug info, but the symbol table still there)**:

```text
--- entry 0 ---
  native_handle : 95551312581264
  description   : [inspect(int)]
  source_file   : []
  source_line   : 0
```

The function name still comes through, but source file and line are completely empty — the `.debug_line` section doesn't exist, so addresses can't be mapped back to source locations.

**Stripping the symbol table** (`g++ ... -g` followed by `strip`): every frame's `description` goes empty too, leaving bare addresses:

```text
--- entry 0 ---
  native_handle : 111239407198864
  description   : []
  source_file   : []
  source_line   : 0
```

`strip` deleted `.symtab`, so function names can no longer be resolved. At this point, if you add `-rdynamic` at link time (exporting symbols into the `.dynsym` dynamic symbol table, which strip does not delete), the function names come back:

```text
--- entry 0 ---
  description   : [inspect(int)]    # stripped, but linked with -rdynamic
  source_file   : []                # debug info is still gone, no line number
```

This is the real engineering trade-off. Our advice is blunt:

::: warning Want a complete stack, prepare the data sources at compile time

- **To get source file + line number**: you must compile with `-g` (or `-g3` for more detail). If a release build should keep the ability to map addresses back to source locations, you can use `objcopy --only-keep-debug` to store the debug info in a separate file, then resolve addresses after the fact with `addr2line -e app <addr>`.
- **To get function names (after strip)**: add `-rdynamic` at link time so symbols land in `.dynsym`. The cost is a bigger binary and externally visible symbols (weigh this if information leakage is a concern).
- **Minimal stack info in production**: at least carry `-rdynamic`, so that even without debug info, even after strip, `description()` still gives you function names instead of a wall of `<unknown>`.
:::

### Raw Mangled Symbols vs. description: Why Demangle

Here's a point beginners easily confuse. Because C++ has overloading and namespaces, the compiler "mangles" function names into an internal representation (the mangled name). For example, `my_lib::compute_value(int, int)` is actually stored in the symbol table as `_ZN6my_lib13compute_valueEii` — something the human eye simply cannot read.

`stacktrace_entry::description()` **demangles for you** and returns plain human-readable text. Let's use `dladdr` (libc's address-to-symbol lookup interface) for comparison, to see the difference between "raw" and "demangled":

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
        // the description, already demangled by stacktrace_entry
        std::cout << "description            : " << e.description() << '\n';

        // fetch the raw mangled symbol via dladdr for comparison
        Dl_info info{};
        dladdr(reinterpret_cast<void*>(e.native_handle()), &info);
        std::cout << "dladdr dli_sname(原始) : " << (info.dli_sname ? info.dli_sname : "<null>") << '\n';

        // manually demangle the raw symbol
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

Built with `g++ -std=c++23 -O0 -g -rdynamic ... -lstdc++exp -ldl`, this prints:

```text
description            : my_lib::compute_value(int, int)
dladdr dli_sname(原始) : _ZN6my_lib13compute_valueEii
手动 demangle          : my_lib::compute_value(int, int)
```

The difference is plain to see. `_ZN6my_lib13compute_valueEii` is the compiler's internal mangled name (the leading `_ZN` is g++'s marker for a C++ name; what follows encodes the namespace, the function name, and the parameter types) — essentially unreadable to the human eye. `stacktrace_entry::description()` uses exactly the `abi::__cxa_demangle` machinery internally and hands you `my_lib::compute_value(int, int)` directly. So in day-to-day use of `<stacktrace>` you never demangle yourself — it's already done for you. Only when you need the "raw symbol string" for some other processing (certain symbol-matching tools, say) do you need `dladdr` to fish out the mangled name directly.

## to_string and operator<<: Two Ways to Print

There are two ready-made ways to print an entire stack.

The first is `std::to_string(stacktrace)` — note that it's a **free function**, not a member of `stacktrace` (writing `st.to_string()` fails to compile). It returns a gdb-style multi-line string:

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

`index#` + function + `at file:line` + `[address]` — it reads almost exactly like gdb's backtrace output, a format the standard library deliberately aligned with. If you want to stuff a whole stack into a log, this is the least effort.

The second is `operator<<` — it comes in two overloads: for a single `stacktrace_entry` it prints one line, and for a whole `basic_stacktrace` it is equivalent to `to_string`. A single entry's output format is "function at file:line [address]" (note the leading space — that's mandated by the standard):

```text
 foo(int) at /tmp/st_basic.cpp:6 [0x5b11f3c25e90]
```

`to_string` suits "I want one whole blob to drop into the log"; `operator<<` suits "I want stream output, or to assemble my own format". Underneath, both go through the same symbolization logic, their output content is identical — only the granularity of the packaging differs.

## In Practice: Printing the Stack in a Crash Handler

Nowhere does `<stacktrace>` show its value more than crash diagnosis. When the program receives a fatal signal such as `SIGSEGV`, taking a stack snapshot inside the signal handler beats "the program vanished and left nothing behind" by a mile.

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
    *p = 42;   // deliberate null-pointer dereference, triggers SIGSEGV
}

void outer(int n) {
    if (n == 0) broken(nullptr);
    outer(n - 1);
}

int main() {
    std::signal(SIGSEGV, [](int) {
        log_stacktrace();
        std::_Exit(1);   // use _Exit to avoid trouble from the destructor chain
    });
    outer(3);
    return 0;
}
```

Built with `g++ -std=c++23 -O0 -g ... -lstdc++exp`, this prints:

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

This stack tells you outright that the crash happened in `broken`, reached from `main` through repeated recursive calls to `outer` — one glance localizes it during a post-mortem. A few points that matter in real engineering:

- The top few frames are **the signal handler itself** (`log_stacktrace`, the lambda's `operator()`, `_FUN`, and the kernel's `sigreturn` trampoline `<unknown>`). The real crash point is the `broken` frame **below** them. When reading a crash stack, remember to skip the handler's own frames first.
- A signal handler runs in an **asynchronous signal context** — it is not an ordinary function call. `std::to_string` allocates memory internally (`new` / `malloc`), and strictly speaking, calling functions that are not async-signal-safe inside signal handling carries risk. This example exits via `std::_Exit` (async-signal-safe) to reduce the risk; for absolutely rigorous settings, the sturdier approach is to only set a flag in the handler and capture later in the main loop, or to use `sigaltstack` with a dedicated handler stack. But as a lightweight "leave evidence behind on crash" scheme, the code above is broadly sufficient in engineering practice.
- For the stack inside the handler to carry line numbers too, the crashing binary must likewise be built with `-g`; otherwise even the `broken` frame is down to a bare function name.

## Performance: Capture Is Cheap, Symbolization Is Expensive

We planted a seed earlier: `current()` only captures addresses, and symbolization happens when an entry is accessed. That splits the cost into two stages whose orders of magnitude differ a lot. Let's measure it — capturing after 5 levels of recursion (depth 6), timing "capture only" and "capture + `to_string` full symbolization" separately, 100,000 runs each:

```cpp
// Standard: C++23
#include <stacktrace>
#include <iostream>
#include <chrono>
#include <string>

void deep(int n) {
    if (n == 0) {
        auto st = std::stacktrace::current();
        volatile auto sz = st.size();   // prevent it being optimized away, but don't trigger symbolization
        (void)sz;
        return;
    }
    deep(n - 1);
}

int main() {
    constexpr int kIters = 100000;
    for (int i = 0; i < 1000; ++i) deep(5);   // warm-up

    // capture only
    auto t1 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < kIters; ++i) deep(5);
    auto t2 = std::chrono::high_resolution_clock::now();
    double ns_capture =
        std::chrono::duration<double, std::nano>(t2 - t1).count() / kIters;

    // capture + full symbolization
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

Built with `g++ -std=c++23 -O2 -g ... -lstdc++exp`, run twice in a row to check stability:

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

The numbers speak for themselves. On this machine (x86-64, GCC 16.1.1, `-O2`), the orders of magnitude:

- **Capture only**: about **0.8 µs per call**. Depth is just 6, and the main cost is walking the stack frames and reading return addresses. Snapping an occasional capture on a hot path is perfectly acceptable.
- **Capture + full symbolization**: about **2 µs per call**, 2–3× capture alone. The extra cost is demangling, string concatenation, and memory allocation (`std::string`). The deeper the stack and the longer the symbols, the more this part grows.

::: warning Absolute numbers vary by machine; the orders of magnitude are what hold
The measurements above were taken on an idle machine; absolute values fluctuate with CPU load, stack depth, and symbol length (the symbolization time differed by nearly 10% between the two runs). But the **order-of-magnitude relationship is stable**: symbolization costs several times pure capture, and both are far more expensive than an ordinary function call (nanoseconds). The conclusion: **don't casually `to_string` inside hot loops** — symbolize only on error paths and diagnostic paths.
:::

This split — cheap capture, expensive symbolization — is precisely the design motivation for the standard library decoupling the two. You can `current()` first and keep the lightweight stack snapshot around (nearly zero cost), then `to_string` when you actually need to diagnose — for example, push the captured `stacktrace` object into a log queue and let a background thread symbolize it at its leisure, without blocking the business logic. Had symbolization been welded to capture from the start, this kind of deferred symbolization would be impossible.

## stacktrace and source_location: When to Use Which

`<stacktrace>` has a sibling with a similar temperament — C++20's `std::source_location` (article 68 in this volume). Both can tell you "where the code is", but their positions are entirely different — **don't mix them up**:

| Dimension | `std::stacktrace` (C++23) | `std::source_location` (C++20) |
|------|--------------------------|-------------------------------|
| What you get | The **entire call chain** at runtime (many frames) | A **single point** at compile time (current function/file/line) |
| When it's determined | Captured at runtime | Fixed at compile time |
| Overhead | Microsecond level (capture + symbolization) | **Zero overhead** (compile-time constants) |
| Typical scenarios | Crash diagnosis, error logs, debug tracing | Log markers, assertions, "where am I" in default arguments |

The most intuitive differences are overhead and granularity. `source_location` is a compile-time constant: the compiler fills in `__FILE__` / `__LINE__` / the function name directly, so reading it at runtime is just reading a few constants — zero cost — which is why it can be used without worry on every log line and in every assertion. `stacktrace` is a genuine runtime capture with microsecond-level overhead, and belongs only where "something has gone wrong and it's worth paying that price to understand how we got here".

A common engineering pairing: **for everyday logging, use `source_location`** to carry the current function and line number (zero overhead, enough to pin down a single point); **on error/crash paths, bring in `stacktrace`** to capture the whole call chain (pricey but information-complete — worth it). The next article covers `source_location` in depth; building this division-of-labor intuition here is enough.

## Summary

The core of `std::stacktrace` boils down to just these points:

- **A two-layer structure**: `basic_stacktrace` (a sequence of frames supporting `size()` / indexing / iteration) plus `stacktrace_entry` (a single frame, queried on demand via `description()` / `source_file()` / `source_line()`). Capture and symbolization are decoupled — `current()` only grabs addresses; symbolization happens when an entry is accessed.
- **The library to link is the first pit**: libstdc++'s `<stacktrace>` implementation lives in the experimental library, not linked by default. GCC 16 uses `-lstdc++exp` (no underscore); older GCC 14/15 documentation says `-lstdc++_exp` (with an underscore). Don't link and you get `undefined reference`; link the wrong name and you get `cannot find`. The library ships only as the static `libstdc++exp.a` and gets statically linked into your binary.
- **Debug symbols are the second pit**: function names rely on the symbol table (must be unstripped or `-rdynamic`); source file/line rely on DWARF debug info (needs `-g`). Strip the symbol table and only bare addresses remain. In production, at least carry `-rdynamic` to keep function names.
- **description is already demangled**: for mangled names like `_ZN6my_lib13compute_valueEii`, `description()` automatically restores `my_lib::compute_value(int, int)` — day to day you never wire up `abi::__cxa_demangle` yourself.
- **Two ways to print**: the free function `std::to_string(st)` gives a gdb-style multi-line string; `operator<<` comes in single-entry (one line) and whole-stack flavors.
- **Performance**: capture is about 0.8 µs, with symbolization about 2 µs (this machine, depth 6, `-O2`). By order of magnitude, symbolization costs several times more — capture only on hot paths, symbolize on error paths.
- **Division of labor with source_location**: `stacktrace` is the whole runtime stack, with overhead, for crash/diagnosis; `source_location` is a compile-time single point, zero overhead, for log markers/assertions. They work in tandem; neither replaces the other.

With this, C++23 has finally standardized "runtime call stack capture", something every platform used to improvise in its own way. In the next article we turn to its zero-overhead sibling `source_location` — how the compile-time approach gets your code location at zero cost.

## References

- [cppreference: std::basic_stacktrace (C++23)](https://en.cppreference.com/w/cpp/utility/basic_stacktrace) — the `current()` / `size()` / iteration interface and the `std::stacktrace` alias
- [cppreference: std::stacktrace_entry (C++23)](https://en.cppreference.com/w/cpp/utility/stacktrace_entry) — semantics of `description` / `source_file` / `source_line` / `native_handle` (there is no `symbol()` member in the standard)
- [cppreference: std::to_string (stacktrace)](https://en.cppreference.com/w/cpp/utility/basic_stacktrace/to_string) — the gdb-style output format of the free function `to_string`
- [cppreference: `__cpp_lib_stacktrace`](https://en.cppreference.com/w/cpp/feature_test) — the feature-test macro; the measured value on our local GCC 16.1.1 is `202011`
- [GCC libstdc++ C++23 status](https://gcc.gnu.org/onlinedocs/libstdc++/manual/status.html#iso.2023) — `<stacktrace>` implementation status and the experimental-library linking convention
