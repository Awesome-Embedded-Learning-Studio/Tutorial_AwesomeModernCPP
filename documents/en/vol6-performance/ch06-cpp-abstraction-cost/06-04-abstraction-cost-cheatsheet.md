---
chapter: 6
cpp_standard:
- 20
description: Rolls up the C++ abstraction costs measured across this chapter (the sizeof and call/construction cost of virtual functions, exceptions, std::function, optional/variant/span, and friends) into a cheat sheet, plus three supplementary entries (variable storage types, bitfields, and the zero cost of enum class), for desk reference while coding
difficulty: advanced
order: 4
platform: host
prerequisites:
- Virtual functions and devirtualization
- The zero-cost model of exceptions
- std::function's small buffer optimization
reading_time_minutes: 5
related:
- The real cost of RVO, NRVO, and move
- The performance cost of C++ abstractions (chapter front page)
tags:
- host
- cpp-modern
- advanced
- 优化
- 字面量
- enum_class
title: The cost cheat sheet for C++ abstractions
translation:
  source: documents/vol6-performance/ch06-cpp-abstraction-cost/06-04-abstraction-cost-cheatsheet.md
  source_hash: 399bcd8bc9a5eb4d6e20ba901eed029b7a1ba3246bf4bd06050454e2215b552d
  translated_at: '2026-07-06T00:00:00+00:00'
  engine: manual
  token_count: 3500
---
# The cost cheat sheet for C++ abstractions

This article is ch06's reference card: it **rolls the C++ abstraction costs measured in earlier articles into a cheat sheet**, then adds three small entries that didn't get their own article (variable storage types, bitfields, enum class). When you hit "is this abstraction expensive" while coding, look it up here.

## What was measured: the cost cheat sheet

| Abstraction | Main cost | Measured number (this machine) | When you care |
|---|---|---|---|
| **Virtual function** (via pointer)| vtable lookup + indirect jump + blocks inlining | 0.55 ns, **2.5x** CRTP | A hot virtual call that hasn't been devirtualized |
| **Devirtualization** | Often free when the compiler can prove the type | Direct object 0.23 ns ≈ CRTP | Most of the time the compiler does it for you |
| **Exception** (normal path)| Table-driven zero cost | **0.25 ns (same as a pure function)** | Almost never |
| **Exception** (throwing path)| EH table lookup + stack unwind | **857 ns, ~3400x** | Keep the exception path out of hot loops |
| **`std::function`** call | Type-erased indirect call | 1.61 ns, **6x** a direct lambda | A million calls per frame |
| **`std::function`** construction | Small hits SBO, big heap-allocates | SBO 2.3 ns / heap 19.6 ns (**8.5x**)| Repeated construction + big capture on the hot path |
| **RVO/NRVO** | Return value constructed directly in the caller | **0 copies, 0 moves** | Don't write std::move on a return of a local |
| **`return std::move(local)`** | Disables NRVO, forces a move | 0 copies + 1 move (one extra) | **Anti-pattern, don't write it** |

This table is the measurement roll-up of ch06-01/02/03/05; see each article for mechanism and experiment. The thesis (Carruth *No Zero-Cost Abstractions*): **every C++ abstraction maps to a hardware cost**, but "has a cost" doesn't equal "happens every time", and the compiler often eliminates it for you (devirtualization, zero-cost exceptions, RVO). **Measure first, then decide whether to hand-write around it.**

## Supplementary entries

### 1. Variable storage types: register / static / thread_local

A variable's **storage type** affects where it lives and how fast it is to access (Agner vol1 §7.1):

- **Automatic variables (stack)**: the default. Fastest access (on a stack that fits in L1), and the compiler can put it in a register. The `register` keyword is meaningless on modern compilers (the compiler allocates registers itself); it's a deprecated/removed keyword since C++17, don't use it.
- **Static variables (`static`/global)**: fixed address, fixed initialization (constant initialization is zero-cost; dynamic initialization has a startup cost). In multithreaded code, the initialization of a static local is thread-safe (magic statics), but **there's a runtime cost to thread-safe initialization** (an atomic check on first entry).
- **`thread_local`**: one per thread. Access is slightly more expensive (has to look up the thread-local storage area, usually a few extra instructions), but avoids sharing under multithreading. Useful for "per-thread context objects".

In practice: hot-path variables should be automatic where possible (let the compiler put them in registers); `static` global constants are free; `thread_local` is for per-thread context (its initialization and destruction cost has to be counted into the thread lifecycle).

### 2. Bitfields

A **bitfield** packs several small fields into one integer, saving space:

```cpp
struct Flags { unsigned a : 1; unsigned b : 1; unsigned c : 6; };  // 8 bits total
```

The upside: small `sizeof` (compact), cache-friendly. The cost: **bit operations** — reading and writing a bitfield member is "read the whole byte plus bitmask plus bit operation", a few more instructions than reading and writing a plain `int`. So bitfields **save memory, spend instructions**. They fit "lots of flag bits, memory is the bottleneck" (protocol headers, flag sets); they don't fit "a single field read/written at high frequency, compute is the bottleneck". Agner vol1 §7.27 has the detailed tradeoffs.

### 3. enum class: zero overhead

**`enum class`** (the C++11 strongly-typed enum) is "type-safe enum", and **zero overhead**: underneath it's just an `int` (or whatever underlying type you specify), as fast to access as a plain `int`, **and the type safety is at compile time, zero cost at runtime**. So:

- Prefer `enum class` over bare `int` constants (type safety, readability, free).
- Don't worry about its performance; it's the same as `int`.
- Specifying the underlying type (`enum class Color : uint8_t`) controls sizeof and saves space.

This is one of the few cases where "zero-cost abstraction" actually holds (an exception to Carruth's thesis: not every abstraction has a cost; `enum class`/`optional` on the normal path is nearly zero-cost).

## sizeof cheat sheet (measured on this machine, libstdc++ C++20)

```text
sizeof:
  int                              = 4
  std::optional<int>               = 8   (int 4B + has-value flag + padding)
  std::variant<int,double>         = 16  (double 8B + index + padding)
  std::variant<int,char,double,str>= 40  (string 32B + index + padding)
  std::span<int>                   = 16  (pointer + length, zero ownership)
  std::string_view                 = 16  (pointer + length, no \0 guarantee)
  std::shared_ptr<int>             = 16  (2 pointers: object + control block)
  std::unique_ptr<int>             = 8   (1 pointer)
  std::string                      = 32  (includes SSO buffer)
  std::vector<int>                 = 24  (3 pointers)
```

How to read it: the extra bytes in `optional`/`variant` are the "has-value" flag and the index; `span`/`string_view` are lightweight "pointer plus length" views (zero ownership, nearly free); `string`'s 32 bytes include the SSO small buffer (the SSO mechanism is covered in vol3).

How to use this table: when coding, prefer zero- or near-zero-cost abstractions (`enum class`, `span`/`string_view`, `optional`/`variant` on the normal path); they make code safer and cost almost nothing in performance. What really deserves attention is virtual function calls (via pointer, not devirtualized), `std::function` with repeated construction plus a big capture, and exceptions entering a hot loop; these have real cost and often need manual optimization. Always measure before optimizing: an abstraction that "sounds expensive" may already be eliminated by the compiler, and one that "sounds free" (constructing a `std::function`) may be hiding a heap allocation.

The next article is ch06's last, on RVO/NRVO and move. It isn't really "abstraction cost", it's "the mechanism for returning large objects under value semantics", and it's often misunderstood.

## References

- Agner Fog, *Optimizing software in C++* §7 *Variables / objects / containers* (variable storage types, bitfields, enum) (local)
- Carruth, *There Are No Zero-Cost Abstractions* (CppCon 2019) — the "no zero-cost abstractions" thesis
- ch06-01/02/03/05 (this volume; the measurement source for each cost)
- The sizeof program for this article: `code/volumn_codes/vol6-performance/ch06/abstraction_sizeof.cpp`
