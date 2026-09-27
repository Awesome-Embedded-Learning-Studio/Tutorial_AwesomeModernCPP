---
chapter: 6
cpp_standard:
- 20
description: 'Rolls the C++ abstraction costs measured across this vol6 chapter — sizeof plus call and construction overhead of virtual functions, exceptions, std::function, optional/variant/span, and friends — into one cheat sheet, plus three supplementary entries (variable storage types covering register/static/thread_local, bitfields, and the zero cost of enum class), for desk reference while coding'
difficulty: advanced
order: 4
platform: host
prerequisites:
- 'Virtual functions and devirtualization: don''t rush to rewrite virtuals as templates'
- 'The zero-cost model of exceptions: free on the normal path, expensive on the exception path'
- 'std::function''s small buffer optimization: the cost of type erasure'
reading_time_minutes: 5
related:
- 'The real cost of RVO, NRVO, and move'
- 'The performance cost of C++ abstractions'
tags:
- host
- cpp-modern
- advanced
- 优化
- 字面量
- enum_class
title: 'C++ abstraction costs: a cheat sheet'
translation:
  source: documents/vol6-performance/ch06-cpp-abstraction-cost/06-04-abstraction-cost-cheatsheet.md
  source_hash: 399bcd8bc9a5eb4d6e20ba901eed029b7a1ba3246bf4bd06050454e2215b552d
  translated_at: '2026-09-26T06:55:14+00:00'
  engine: anthropic
  token_count: 1100
---
# C++ abstraction costs: a cheat sheet

This article is ch06's reference card: it **rolls the C++ abstraction costs measured in the earlier articles into a single cheat sheet**, then adds three short entries that never got an article of their own (variable storage types, bitfields, enum class). Next time you're coding and wonder "is this abstraction expensive?", look it up here.

## What we measured earlier: the cost cheat sheet

| Abstraction | Main cost | Measured numbers (this machine) | When to care |
|---|---|---|---|
| **Virtual function** (via pointer)| vtable lookup + indirect jump + blocks inlining | 0.55 ns, **2.5×** CRTP | A hot virtual call that hasn't been devirtualized |
| **Devirtualization** | Often free when the compiler can prove the type | Direct object 0.23 ns ≈ CRTP | Most of the time the compiler does it for you |
| **Exception** (normal path)| Table-driven zero cost | **0.25 ns (same as a pure function)** | Almost never a concern |
| **Exception** (throwing path)| EH table lookup + stack unwinding | **857 ns, ~3400×** | Keep the exception path out of hot loops |
| **`std::function`** call | Type-erased indirect call | 1.61 ns, **6×** a direct lambda | A million calls per frame |
| **`std::function`** construction | Small capture takes SBO, big one heap-allocates | SBO 2.3 ns / heap 19.6 ns (**8.5×**)| Repeated construction + big capture on the hot path |
| **RVO/NRVO** | Return value constructed directly in the caller | **0 copies, 0 moves** | Don't write std::move when returning a local |
| **`return std::move(local)`** | Disables NRVO, forces a move | 0 copies + 1 move (one extra) | **Anti-pattern, don't write it** |

This table is the measurement roll-up of ch06-01/02/03/05; see each article for the mechanism and the experiment. The overarching thesis (Carruth, *No Zero-Cost Abstractions*): **every C++ abstraction maps to a hardware cost**, but "has a cost" doesn't mean "pays every time", and the compiler often eliminates it for you (devirtualization, zero-cost exceptions, RVO). **Measure first, then decide whether to hand-write around it.**

## Supplementary entries

### 1. Variable storage types: register / static / thread_local

A variable's **storage type** affects where it lives and how fast it is to access (Agner vol 1 §7.1):

- **Automatic variables (stack)**: the default. Fastest to access (a stack that hits in L1), and the compiler can put them in registers. The `register` keyword is meaningless on modern compilers (they allocate registers themselves); it's been a deprecated/removed keyword since C++17 — don't use it.
- **Static variables (`static`/global)**: fixed address, fixed initialization (constant initialization is zero-cost; dynamic initialization has a startup cost). Under multithreading, initialization of a static local variable is thread-safe (magic statics), but **thread-safe initialization has a runtime cost** (an atomic check on first entry).
- **`thread_local`**: one copy per thread. Access is slightly more expensive (it has to look up the thread-local storage area through TLS, usually a few extra instructions), but it avoids sharing under multithreading. Useful for "per-thread context objects".

In practice: keep hot-path variables automatic where possible (let the compiler put them in registers); `static` global constants are free; use `thread_local` for per-thread context (and count its initialization and destruction cost into the thread's lifecycle).

### 2. Bitfields

A **bitfield** packs several small fields into a single integer, saving space:

```cpp
struct Flags { unsigned a : 1; unsigned b : 1; unsigned c : 6; };  // 8 bits total
```

The upside: small `sizeof` (compact) and cache-friendly. The cost is **bit manipulation**: reading or writing a bitfield member is "read the whole byte + bitmask + bit ops", a few more instructions than reading or writing a plain `int`. So bitfields **save memory but spend instructions**. They fit "lots of flag bits, memory is the bottleneck" (protocol headers, flag sets); they don't fit "a single field read and written at high frequency, compute is the bottleneck". Agner vol 1 §7.27 has the detailed tradeoffs.

### 3. enum class: zero overhead

**`enum class`** (the C++11 strongly-typed enum) is an enum "with type safety attached", and it's **zero overhead**: underneath it's just an `int` (or whatever underlying type you specify), as fast to access as a plain `int`, **and the type safety is compile-time — zero runtime cost**. So:

- Prefer `enum class` over bare `int` constants (type safety and readability, for free).
- Don't worry about its performance; it's the same as `int`.
- Specifying the underlying type (`enum class Color : uint8_t`) controls sizeof and saves space.

This is one of the few cases where "zero-cost abstraction" genuinely holds (an exception to Carruth's thesis: not every abstraction has a cost — `enum class`/`optional` on the normal path is near zero cost).

## sizeof quick reference (measured on this machine, libstdc++ C++20)

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

How to read it: the extra bytes in `optional`/`variant` are the "has a value" flag and the index; `span`/`string_view` are lightweight "pointer + length" views (zero ownership, nearly free); and `string`'s 32 bytes include the SSO small buffer (the SSO mechanism belongs to vol3).

How to use this table: when writing code, reach first for the zero- or near-zero-cost abstractions (`enum class`, `span`/`string_view`, `optional`/`variant` on the normal path) — they make the code safer and cost almost nothing in performance. What actually deserves your attention is the short list of virtual calls (via pointer, not devirtualized), `std::function` repeatedly constructed with a big capture, and exceptions entering a hot loop: those have real cost and often need manual optimization. And always measure before optimizing — an abstraction that "sounds expensive" may have been eliminated by the compiler long ago, while one that "sounds free" (constructing a `std::function`) may be hiding a heap allocation.

The next article is ch06's last, on RVO/NRVO and move. It isn't an "abstraction cost" so much as the mechanism for "returning large objects under value semantics" — and it's often misunderstood.

## References

- Agner Fog, *Optimizing software in C++*, §7 *Variables / objects / containers* (variable storage types, bitfields, enum) — local copy
- Carruth, *There Are No Zero-Cost Abstractions* (CppCon 2019) — the "no zero-cost abstractions" thesis
- ch06-01/02/03/05 (this volume; where each cost was measured)
- The sizeof program for this article: `code/volumn_codes/vol6-performance/ch06/abstraction_sizeof.cpp`
