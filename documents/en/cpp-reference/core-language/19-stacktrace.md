---
chapter: 99
cpp_standard:
- 23
description: Capture and print the call stack — the standard tool for program introspection
  and crash diagnostics
difficulty: intermediate
order: 19
reading_time_minutes: 3
tags:
- host
- cpp-modern
- intermediate
- 调试
title: std::stacktrace
translation:
  source: documents/cpp-reference/core-language/19-stacktrace.md
  source_hash: a4b390d03037a00423cb685d9ff2be5e2901c2ae5fd4d04d7d23fab2c4d84f94
  translated_at: '2026-09-25T09:22:53+00:00'
  engine: anthropic
  token_count: 680
---
<!--
Reference card: std::stacktrace (C++23). Tested locally with GCC 16.1.1 -std=c++23.
Critical pitfall: GCC's stacktrace symbols are not in the main library; you must link the experimental library separately. GCC 16+ uses -lstdc++exp (no underscore), GCC 12-15 uses -lstdc++_exp (with underscore). Missing the link produces: undefined reference to std::__stacktrace_impl::_S_current.
-->

# std::stacktrace (C++23)

## One-Liner

Capture the current call stack anywhere in your code and get each frame's function name, source file, and line number — the standard tool for program introspection and crash diagnostics, replacing clumsy approaches like the `backtrace()` family.

## Header

`#include <stacktrace>`

::: warning GCC linking pitfall (tested)
GCC's `<stacktrace>` symbols are not in the main library; you must link the experimental library separately: **GCC 16+ uses `-lstdc++exp` (note: no underscore), GCC 12–15 uses `-lstdc++_exp` (with underscore)**. In CMake, write `target_link_libraries(target PRIVATE stdc++exp)`. Miss the link and you get `undefined reference to std::__stacktrace_impl::_S_current`.
:::

## Core API Cheat Sheet

| Operation | Signature | Description |
|------|------|------|
| Capture current stack | `std::stacktrace::current()` | Returns a snapshot of the current call stack |
| With skipped frames | `stacktrace::current(skip, max_depth)` | Skips the first `skip` frames, takes at most `max_depth` |
| Stack depth | `st.size()` | Number of frames |
| Get one frame | `st[i]` | Yields a `stacktrace_entry` |
| Frame description | `entry.description()` | Human-readable string with the function name, address, and more |
| Frame source location | `entry.source_file()` / `source_line()` | Source file name and line number (requires debug symbols) |
| Stream output | `std::cout << st` | Prints the whole stack directly |

## Minimal Example

```cpp
// Standard: C++23
// Build: g++ -std=c++23 demo.cpp -lstdc++exp     (GCC 16+)
#include <stacktrace>
#include <iostream>

void inner() {
    auto st = std::stacktrace::current();
    std::cout << "depth=" << st.size() << "\n";
    for (std::size_t i = 0; i < st.size(); ++i)
        std::cout << "  #" << i << " " << st[i] << "\n";
}
void outer() { inner(); }
int main() { outer(); }
```

Actual output (GCC 16.1.1, `-std=c++23 -lstdc++exp`):

```text
depth=6
  #0  inner() [0x5ca9fa0aa28d]
  #1  outer() [0x5ca9fa0aa3d4]
  #2  main [0x5ca9fa0aa3e0]
  #3  <unknown> [0x7f1709227740]
  #4  __libc_start_main [0x7f1709227878]
  #5  _start [0x5ca9fa0aa184]
```

Function names for our own code (inner/outer/main) resolved fine; the dynamic-library frames showing `<unknown>` is normal — those addresses were never symbolized.

## Embedded Applicability: Low

- Depends on the symbol table and runtime stack unwinding — usually unavailable on bare metal and RTOS, or requires a dedicated port
- Symbol information bloats the image; after `strip` (`-s`) only addresses remain
- Embedded work more commonly relies on hardware breakpoints, ITM, SEGGER RTT, or custom crashdump backtraces
- It does come in handy on the host side: tooling, test stubs, and CI failure diagnostics

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 12 | 16 | 19.34 |

## See Also

- [cppreference: std::stacktrace](https://en.cppreference.com/w/cpp/header/stacktrace)

---

*Part of the content referenced from [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
