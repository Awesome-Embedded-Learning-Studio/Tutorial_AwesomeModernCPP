---
chapter: 6
cpp_standard:
- 17
description: Since C++17, return value optimization (RVO/NRVO) makes "return a large object by value" nearly free — the return value is constructed directly in the caller's stack frame, zero copies and zero moves. This article uses a Tracked type whose copy/move constructors count (a demonstration the compiler can't punch through) to show that URVO/NRVO is 0 copies 0 moves, that return std::move(local) is an anti-pattern (it disables NRVO and adds 1 move), and that move is about an order of magnitude cheaper than copy
difficulty: advanced
order: 5
platform: host
prerequisites:
- std::function's small buffer optimization
- Benchmark methodology reference card
reading_time_minutes: 6
related:
- The cost cheat sheet for C++ abstractions
tags:
- host
- cpp-modern
- advanced
- 优化
- 移动语义
title: 'The real cost of RVO, NRVO, and move'
translation:
  source: documents/vol6-performance/ch06-cpp-abstraction-cost/06-05-rvo-move.md
  source_hash: df61a891aa680946e318fe0137e288b6eb627bc81fcd528ca7c1383bf422b090
  translated_at: '2026-07-06T00:00:00+00:00'
  engine: manual
  token_count: 3600
---
# The real cost of RVO, NRVO, and move

## "Returning a large object by value" used to be expensive, now it's free

In early C++, "returning a large object by value" (say `vector<string> make()`) was repeatedly warned against: it meant copying the whole object, absurdly expensive. So for a long time C++ circles had old dogma like "to return a large object use a pointer/reference" or "use an out-parameter `void make(T& out)`".

This dogma **is mostly obsolete in modern C++**. Since C++17, **return value optimization (RVO/NRVO) is mandatory** (URVO) or effectively standard (NRVO): the return value is **constructed directly in the caller's stack frame**, neither copied nor moved, zero cost. Let's see this clearly with a method the compiler can't punch through.

## Seeing it with a copy/move counter (the compiler can't punch through)

There's a pitfall in demonstrating RVO: **timing it lets the compiler's cross-iteration optimization punch through** (in my first timing version, "copy" was actually faster than "RVO", because the compiler folded the whole loop). The right approach is a type whose **copy/move constructors count**: it **directly counts** how many copies and how many moves happened, and no matter how smart the compiler is, it can't rewrite your counter:

```cpp
struct Tracked {
    int v;
    static int64_t copies, moves;
    Tracked(int x) : v(x) {}
    Tracked(const Tracked& o) : v(o.v) { ++copies; }              // count copies
    Tracked(Tracked&& o) noexcept : v(o.v) { ++moves; o.v = -1; } // count moves
};
```

Then measure four "return" styles, watching the copies/moves counts:

```text
===== Copy/move counts for RVO/NRVO/move/copy =====
  URVO return Tracked(1):     copies=0  moves=0  → zero copies zero moves
  NRVO return t (named local):   copies=0  moves=0  → zero copies zero moves
  return std::move(t):        copies=0  moves=1  → forced 1 move (you disabled NRVO)
  return g_global (lvalue):    copies=1  moves=0  → 1 copy (RVO not eligible)
```

This table is the gold standard for teaching RVO (it doesn't depend on timing, the compiler can't punch through):

- **URVO (returning an unnamed temporary)**: `return Tracked(1);`. Since C++17 this is **guaranteed copy elision**: the returned prvalue is initialized directly in the caller, neither copied nor moved. **0 copies 0 moves**.
- **NRVO (returning a named local)**: `Tracked t(...); return t;`. For returning a named local, the compiler **in effect** elides it (it was common practice before C++17; C++17 guarantees more scenarios). **0 copies 0 moves**.
- **`return std::move(t)` (anti-pattern)**: your hand-written `std::move` **forces it to an rvalue**, which **disables NRVO** (NRVO requires an lvalue), and the compiler is forced into the move constructor. **0 copies 1 move**, one avoidable move. That's why "``return std::move(local)`" is a famous **anti-pattern** in C++: it can only make code slower, never faster.
- **Returning an lvalue (global/parameter)**: `return g_global;`. An lvalue isn't RVO/move-eligible (doesn't qualify), so it's copied. **1 copy 0 moves**. This is the genuinely "expensive" case: returning an externally-named object, you have to copy it.

## Seeing RVO turned off with -fno-elide-constructors

GCC has a flag `-fno-elide-constructors` that turns off copy elision (used to reproduce old C++ behavior or for debugging). Rebuild with it:

```text
(with -fno-elide-constructors:)
  URVO return Tracked(1):     copies=0  moves=0   ← C++17 guaranteed, can't turn off
  NRVO return t (named local):   copies=0  moves=1   ← NRVO off, degenerates to 1 move
  return std::move(t):        copies=0  moves=1
  return g_global:            copies=1  moves=0
```

Two things to read off:

- **URVO is mandatory in C++17, and `-fno-elide` can't turn it off** (prvalue initialization rules, not an optimization). So `return T(args)` is always zero-cost.
- **NRVO is an "in-effect optimization"; turn it off and it degenerates into one move**. Move is cheaper than copy (covered below), so even if NRVO doesn't kick in you only pay one move's cost, not a copy. This is the safety net for C++'s "value semantics is safe": the worst case is one cheap move.

## How much cheaper is move than copy

`std::move` itself does nothing (it's just a cast to an rvalue); the real work is done by the **move constructor**. For types that manage dynamic memory like `vector`/`string`, move is a **pointer swap** (O(1)), and copy is a **deep copy** (O(n)):

```cpp
// vector's move constructor: O(1) pointer swap (note the parameter is vector&&, not const vector&& -- move needs to modify the source)
vector(vector&& o) noexcept : data_(o.data_), size_(o.size_) { o.data_ = nullptr; o.size_ = 0; }
// vector's copy constructor: O(n) deep copy
vector(const vector& o) : data_(new T[o.size()]) { copy o.data_ → data_; }
```

For a 4 KB `vector<int>` (1000 elements), move is a few pointer assignments (nanosecond-level), and copy is allocate 4KB plus memcpy (tens to hundreds of nanoseconds, depending on the allocator). **Move is more than an order of magnitude cheaper than copy**, and the gap widens as the element count grows.

But move isn't free; it's still "construct a new object plus destroy the gutted source". So **cheapest is RVO/NRVO (0 times), then move (1 time), most expensive copy (deep copy)**.

## Practical rules

Compressed into a few memorizable ones:

1. **Return by value, write it with confidence**. Both `return Tracked(args)` (URVO, mandatory-free since C++17) and `T t(...); return t;` (NRVO, effectively free) are zero-cost.
2. **`return std::move(local)` is an anti-pattern, don't write it**. It disables NRVO and can only slow things down. The compiler will turn the return of a local into an rvalue automatically as needed; you don't have to do it.
3. **Move isn't free, but it's about an order of magnitude cheaper than copy** (for big objects). `std::vector`/`std::string` moving is O(1).
4. **Use `std::move` where you "clearly want to turn something into an rvalue"**: for example stuffing a local into a container `v.push_back(std::move(elem))`, or transferring `unique_ptr` ownership. Don't use it on a `return`.
5. **Returning an external lvalue (global, parameter, member) still copies**; in that case consider a `const&` return or an explicit `std::move` (if you really do want to transfer ownership).

These cover "how modern C++ returns objects" completely. The old dogma "return by value is slow" basically doesn't hold under modern C++ plus RVO: **write naturally, write value semantics, and the compiler elides copies for you.**

In one sentence: RVO/NRVO makes return-by-value zero-cost (URVO is mandatory since C++17 at 0/0, NRVO is an in-effect optimization at 0/0), and the compiler can't punch through it; you have to verify it with a copy/move counter; `return std::move(local)` is an anti-pattern that disables NRVO and adds one move (0 copies 1 move), don't write it; move is about an order of magnitude cheaper than copy (O(1) pointer swap vs O(n) deep copy), but isn't free; returning an external lvalue is the only case that still copies, and there you consider a reference or an explicit transfer. ch06 ends here: virtual functions, exceptions, std::function, the cheat sheet, RVO/move — the costs of the major C++ abstractions have all been measured.

## References

- cppreference *Copy elision* (the C++17 guaranteed-elision rules)
- Meyer, S. *Effective Modern C++ Item 25* (reverse: overloading on rvalue vs reference qualification) — the engineering use of move semantics
- Agner Fog, *Optimizing software in C++* §7.16 *Returning objects* (local)
- The measurement code for this article: `code/volumn_codes/vol6-performance/ch06/rvo_move.cpp` (includes the -fno-elide-constructors comparison)
