---
chapter: 6
cpp_standard:
- 17
description: Since C++17, return value optimization (RVO/NRVO) has made "returning a large object by value" nearly free — the return value is constructed directly in the caller's stack frame, zero copies, zero moves. This article uses a Tracked type whose copy/move constructors count (a demonstration the compiler can't punch through) to see it clearly — URVO/NRVO is 0 copies 0 moves, return std::move(local) is an anti-pattern (it disables NRVO and costs 1 extra move), and move is an order of magnitude cheaper than copy
difficulty: advanced
order: 5
platform: host
prerequisites:
- 'std::function''s small buffer optimization: the cost of type erasure'
- Benchmark methodology reference card
reading_time_minutes: 6
related:
- 'C++ abstraction costs: a cheat sheet'
tags:
- host
- cpp-modern
- advanced
- 优化
- 移动语义
title: The real cost of RVO, NRVO, and move
translation:
  source: documents/vol6-performance/ch06-cpp-abstraction-cost/06-05-rvo-move.md
  source_hash: df61a891aa680946e318fe0137e288b6eb627bc81fcd528ca7c1383bf422b090
  translated_at: '2026-09-26T06:55:20+00:00'
  engine: anthropic
  token_count: 3300
---
# The real cost of RVO, NRVO, and move

## "Returning a large object by value" used to be expensive — now it's free

In the early days of C++, "returning a large object by value" (say, `vector<string> make()`) was something you were warned against again and again: it meant copying the entire object, which was absurdly expensive. So the C++ world long circulated old dogma like "return big objects through a pointer/reference" or "use an out-parameter `void make(T& out)`".

That dogma **is essentially obsolete in modern C++**. Since C++17, **return value optimization (RVO/NRVO) is mandatory** (URVO) or happens as a matter of fact (NRVO): the return value is **constructed directly in the caller's stack frame**, neither copied nor moved — zero cost. Let's see this clearly with a method the compiler can't punch through.

## Seeing it with a copy/move counter (the compiler can't punch through)

RVO demos hide a pitfall: **if you measure with timers, the compiler's cross-iteration optimization punches right through** (in the first version of my timing program, "copy" actually came out faster than "RVO", because the compiler folded the whole loop away). The right approach is a type **whose copy/move constructors count**: it **counts directly** how many copies and how many moves happened, and however smart the compiler is, it can't touch your counter:

```cpp
struct Tracked {
    int v;
    static int64_t copies, moves;
    Tracked(int x) : v(x) {}
    Tracked(const Tracked& o) : v(o.v) { ++copies; }              // counts copies
    Tracked(Tracked&& o) noexcept : v(o.v) { ++moves; o.v = -1; } // counts moves
};
```

Then measure four ways of "returning", checking the copies/moves count for each:

```text
===== Copy/move counts for RVO/NRVO/move/copy =====
  URVO return Tracked(1):      copies=0  moves=0  → zero copies, zero moves
  NRVO return t (named local): copies=0  moves=0  → zero copies, zero moves
  return std::move(t):         copies=0  moves=1  → forced into 1 move (you disabled NRVO)
  return g_global (lvalue):    copies=1  moves=0  → 1 copy (RVO not possible)
```

This table is the gold standard for teaching RVO (no timers involved, nothing for the compiler to punch through):

- **URVO (returning an unnamed temporary)**: `return Tracked(1);`. Since C++17 this is **guaranteed copy elision**: the returned prvalue initializes directly in the caller, neither copied nor moved. **0 copies, 0 moves**.
- **NRVO (returning a named local)**: `Tracked t(...); return t;`. Returning a named local variable is elided **as a matter of fact** by compilers (widely done even before C++17; in the standard NRVO has always been permitted, never mandatory — what C++17 actually guarantees is elision of prvalue returns). **0 copies, 0 moves**.
- **`return std::move(t)` (anti-pattern)**: your hand-written `std::move` **force-converts the result to an rvalue**, which **disables NRVO** (NRVO requires an lvalue), so the compiler is forced down the move constructor. **0 copies, 1 move** — one avoidable move too many. That's why "`return std::move(local)`" is a famous **anti-pattern** in C++: it can only make code slower, never faster.
- **Returning an lvalue (a global or a parameter)**: `return g_global;`. An lvalue isn't RVO/move-eligible (doesn't qualify), so it takes the copy path. **1 copy, 0 moves**. This is the genuinely "expensive" case: returning an externally named object forces a copy of it.

## Using -fno-elide-constructors to see RVO switched off

GCC has a flag, `-fno-elide-constructors`, that turns copy elision off (used to reproduce old C++ behavior or for debugging). Rebuild with it added:

```text
(with -fno-elide-constructors:)
  URVO return Tracked(1):      copies=0  moves=0   ← C++17 guaranteed, can't be turned off
  NRVO return t (named local): copies=0  moves=1   ← NRVO off, degrades to 1 move
  return std::move(t):         copies=0  moves=1
  return g_global:             copies=1  moves=0
```

Two things to read off:

- **URVO is mandatory in C++17 — even `-fno-elide` can't switch it off** (it's the prvalue initialization rule, not an optimization). So `return T(args)` is always zero-cost.
- **NRVO is a "de facto optimization"; switch it off and it degrades to one move**. A move is cheaper than a copy (covered below), so even when NRVO doesn't kick in, you pay one move, not a copy. That's the safety net under C++'s "value semantics are safe": the worst case is one cheap move.

## How much cheaper is move than copy

`std::move` itself does nothing (it's just a cast to an rvalue); the real work is done by the **move constructor**. For types that manage dynamic memory, like `vector`/`string`, a move is a **pointer swap** (O(1)), while a copy is a **deep copy** (O(n)):

```cpp
// vector's move constructor: O(1) pointer swap (note the parameter is vector&&, not const vector&& — move needs to modify the source)
vector(vector&& o) noexcept : data_(o.data_), size_(o.size_) { o.data_ = nullptr; o.size_ = 0; }
// vector's copy constructor: O(n) deep copy
vector(const vector& o) : data_(new T[o.size()]) { copy o.data_ → data_; }
```

For a 4 KB `vector<int>` (1000 elements), a move is a few pointer assignments (nanoseconds), while a copy is a 4 KB allocation + memcpy (tens to hundreds of nanoseconds, depending on the allocator). **Move is more than an order of magnitude cheaper than copy**, and the gap widens as the element count grows.

But move isn't free: it's still "construct a new object + destroy the gutted source object". So **cheapest is RVO/NRVO (0 operations), next is move (1), most expensive is copy (the deep copy)**.

## Practical rules

Distilled into a few lines you can memorize:

1. **Return by value — write it with confidence**. Both `return Tracked(args)` (URVO, mandatorily free since C++17) and `T t(...); return t;` (NRVO, effectively free) cost nothing.
2. **`return std::move(local)` is an anti-pattern — don't write it**. It disables NRVO and can only slow things down. When returning a local variable, the compiler converts it to an rvalue automatically as needed; you don't have to do anything.
3. **Move isn't free, but it's an order of magnitude cheaper than copy** (for large objects). Moving a `std::vector`/`std::string` is O(1).
4. **Use `std::move` where you explicitly want an rvalue**: stuffing a local into a container (`v.push_back(std::move(elem))`), transferring `unique_ptr` ownership. Not on a `return`.
5. **Returning an external lvalue (a global, a parameter, a member) still copies**. In that case, consider returning by `const&`, or an explicit `std::move` (if you genuinely want to transfer ownership).

These rules cover the whole story of "how modern C++ returns objects". The old dogma "returning by value is slow" basically doesn't hold under modern C++ + RVO: **write naturally, write value semantics, and the compiler elides the copies for you**.

One-sentence wrap-up: RVO/NRVO makes returning by value zero-cost (URVO is mandatory 0/0 in C++17, NRVO is a de facto 0/0 optimization) — and since the compiler can't punch through it no matter how smart, you verify with a copy/move counter; `return std::move(local)` is an anti-pattern that disables NRVO and adds one move (0 copies, 1 move) — don't write it; move is an order of magnitude cheaper than copy (O(1) pointer swap vs O(n) deep copy), but not free; only returning an external lvalue copies, and there you consider a reference or an explicit transfer. That closes out ch06: virtual functions, exceptions, std::function, the cheat sheet, and RVO/move — the costs of C++'s major abstractions, all measured.

## References

- cppreference, *Copy elision* (the C++17 guaranteed-elision rules)
- Meyer, S., *Effective Modern C++* Item 25 (the reverse: overloading on rvalue references vs reference qualification) — engineering usage of move semantics
- Agner Fog, *Optimizing software in C++*, §7.16 *Returning objects*. Local copy.
- This article's measurement code: `code/volumn_codes/vol6-performance/ch06/rvo_move.cpp` (includes the -fno-elide-constructors comparison)
