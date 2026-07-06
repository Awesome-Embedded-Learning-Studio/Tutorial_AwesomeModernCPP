---
chapter: 7
cpp_standard:
- 17
description: 'This article covers three boundary topics: the runtime cost of dynamic linking/PIC
  (the "symbol resolution + position independence" cost from CSAPP ch7), the optimization differences
  among mainstream compilers GCC/Clang/MSVC (Agner vol1 ch8), and the performance face of compile-time
  metaprogramming (templates/constexpr), which has zero runtime cost once computed at compile time but
  costs compile time and binary size'
difficulty: advanced
order: 3
platform: host
prerequisites:
- LTO, ThinLTO, and the engineering rollout of PGO
- '-O levels and optimization blockers'
reading_time_minutes: 5
related:
- Size optimization: -Os, --gc-sections, and template bloat control
tags:
- host
- cpp-modern
- advanced
- 优化
- 链接器
- 工具链
title: Linking performance, multi-compiler comparison, and compile-time metaprogramming
translation:
  source: documents/vol6-performance/ch07-compiler-and-size/07-03-linking-and-compilers.md
  source_hash: 037748bacea5c92cef655da38c5d80b0162beebf433c5cea79ec39fc054101df
  translated_at: '2026-07-06T00:00:00+00:00'
  engine: manual
  token_count: 2500
---
# Linking performance, multi-compiler comparison, and compile-time metaprogramming

The first two ch07 articles covered `-O` levels and LTO/PGO. This one closes out the remaining three compile/link-related topics: **the runtime cost of dynamic linking, optimization differences among mainstream compilers, and the performance face of compile-time metaprogramming**. All three are "boundary topics", not the lead actors in routine performance work, but they show up in large projects, embedded systems, and extreme-optimization scenarios.

> This article has no local measurements. Dynamic-linking mechanics come from CSAPP ch7, multi-compiler comparison from Agner vol1 §8, and compile-time metaprogramming from Agner vol1 §15; we cover only the performance side and don't reinvent the experiments.

## Dynamic linking and PIC: the cost of position independence

CSAPP Chapter 7, *Linking*, covers static vs dynamic linking. From a performance angle, dynamic linking (`.so`/`.dylib`/`.dll`) carries several runtime costs:

- **Symbol resolution**: the first time you call into a dynamic library, the runtime linker (`ld.so`) has to look up the symbol table and bind the address (lazy binding) or bind everything at startup (now binding). This is a **first-call latency**.
- **PIC (position-independent code)**: dynamic-library code has to be loadable at any address, so it goes through a **GOT (Global Offset Table)** for indirect addressing; every access to a global variable or external function adds one GOT lookup. This is a **constant per-access cost** (a few extra instructions).
- **PLT (Procedure Linkage Table)**: external function calls go through a PLT indirect jump, one extra hop versus a direct call.
- **icache pressure**: dynamic-library code is scattered across different load addresses, and cross-library calls add icache misses.

Each of these costs is **small per-call (nanoseconds)**, but in scenarios with "extremely high-frequency cross-library calls + very short functions" they show up (for example, a tight loop calling a small function in another `.so`). A few counters:

- **Inline the hot-path cross-library calls away** (LTO works within a library, not across; or move the hot function into the main program).
- **`-Wl,-z,now`** (bind everything at startup, so first-call latency doesn't fire mid-run; suitable for long-running services).
- **Statically link hot-path libraries** (trade upgrade independence for performance).

CSAPP ch7 has the full mechanism; vol6 only covers the performance side. For most applications these costs are negligible; **real-time games, HFT, and embedded** are where they get fined-tuned.

## Multi-compiler comparison: GCC vs Clang vs MSVC

The three mainstream compilers' optimization capability is **broadly comparable, with details that differ**. Agner Fog compares them in vol1 §8; a few observations (these shift with versions, check the latest):

- **Optimization strength**: at `-O2`/`-O3`, the overall performance gap across the three is usually within **single-digit percent**, and which one wins depends on the specific code.
- **Vectorization**: GCC has historically been aggressive at vectorization; Clang/LLVM's vectorization framework (led by Nadav Rotem) is also strong and sometimes smarter; MSVC's auto-vectorization is relatively conservative (but hand-written intrinsics are the same).
- **Code generation**: Clang has the best error messages and most accurate diagnostics; GCC has the widest platform support; MSVC is the de facto standard under the Windows ABI.
- **Cross-platform**: GCC/Clang span Linux/macOS/Windows (MinGW/clang-cl); MSVC is mostly Windows.

The practical advice is **don't switch toolchains over "I heard X compiler is faster"**: the gap is small, and shifts with versions. Choose a compiler by **platform support + team familiarity + diagnostic quality**, not by raw performance. If you really want to squeeze, **PGO + LTO is far more effective than switching compilers**. Run benchmarks periodically to confirm your compiler choice has no obvious disadvantage.

> Note: different compilers have **incompatible ABIs** (Itanium ABI vs MSVC ABI), so mixing them (say, a GCC-built library used by MSVC) requires `extern "C"` interface isolation. That's an engineering problem, not a performance one.

## Compile-time metaprogramming: the performance face of templates and constexpr

C++ templates and `constexpr` can push computation to **compile time**: once compiled, it's done, and runtime cost is zero. This is "true zero-overhead abstraction" (at runtime). But the cost has two faces.

### Runtime: zero or near zero

- **`constexpr`/`consteval` functions**: evaluation finishes at compile time, and the runtime result is just a constant. For example, `constexpr int fib(int n)` computes `fib(10)` at compile time; at runtime it's just the literal `55`, **zero runtime cost**.
- **Template computation**: `template<int N> struct Fact { static constexpr int v = N * Fact<N-1>::v; };`, computed at compile time.
- **Type-level computation** (the dispatch for `std::tuple`, `std::variant`) is generated at compile time, and at runtime it's optimized direct code (often inlined away).

So "if it can be computed at compile time, compute it at compile time" is one of C++'s performance principles: **shifting compute from runtime to compile time is free**.

### The cost: compile time and size

- **Compile time**: template instantiation is one of the compiler's heaviest jobs. Heavy-template C++ projects routinely take tens of seconds to minutes to compile. This is a long-standing pain point in the C++ world (modules, since C++20, ease it).
- **Binary size**: templates generate one copy of the code per type (`vector<int>`, `vector<double>`, `vector<string>` are three copies), producing **template bloat**. ch07-04 covers the counters (`extern template`, factoring out shared logic).
- **Readability/error messages**: heavy-template code is notoriously hard to read in its errors.

Practical tradeoff: **for small computations on the hot path, push them to compile time with `constexpr`** (`constexpr int kTable[N] = ...`); **don't over-templatize for the sake of "compile time"** (template bloat + compile time + readability cost). `constexpr` is more restrained and more modern than "template metaprogramming"; prefer `constexpr`/`consteval`.

## References

- CSAPP Chapter 7, *Linking*: static/dynamic linking, GOT/PLT, the mechanics of symbol resolution
- Agner Fog, *Optimizing Software in C++*, §8 "Different C++ compilers" (compiler comparison) and §15 "Metaprogramming" (the size face of compile-time metaprogramming). Local copy
- GCC/Clang/MSVC docs for their respective `-O` behavior, `-fpic`/`-fPIC`, and `constexpr` support
- ch07-04 Size optimization (this volume, counters to template bloat)

Three-part capstone: **dynamic linking has runtime cost** (PIC indirection, symbol resolution, icache), small per-call, only visible under extremely high-frequency short cross-library calls, with mechanics in CSAPP ch7; **the three compilers' performance gap is small** (single-digit percent), don't switch toolchains for "faster", PGO+LTO is more effective; **compile-time metaprogramming is zero-cost at runtime, but the cost is compile time + template size**, prefer `constexpr`/`consteval` over heavy templates. These three are supporting cast in routine optimization, but lead roles in large-project build engineering, embedded, and extreme optimization.
