---
chapter: 6
cpp_standard:
- 17
description: 'std::function type-erases any callable object, and the price is an indirect call plus a possible heap allocation. Measured here: a call is about 6x slower than a direct lambda (1.61 ns vs 0.25 ns), and at construction a small capture hits SBO (2.3 ns) while a large capture triggers a heap allocation (19.6 ns, 8.5x). Watch out for repeatedly constructing a function with a large capture on the hot path; the fixes are a template parameter (compile-time polymorphism) or a fixed-signature function pointer'
difficulty: advanced
order: 3
platform: host
prerequisites:
- 'Virtual functions and devirtualization: don''t rush to rewrite virtuals as templates'
- 'Cachelines and locality: the 64-byte minimum unit of transfer'
reading_time_minutes: 4
related:
- 'C++ abstraction costs: a cheat sheet'
- 'The real cost of RVO, NRVO, and move'
tags:
- host
- cpp-modern
- advanced
- 优化
- std_function
title: 'std::function''s small buffer optimization: the cost of type erasure'
translation:
  source: documents/vol6-performance/ch06-cpp-abstraction-cost/06-03-std-function-sbo.md
  source_hash: 1ea24a87c0bb47090b772b47ce372dbf02b03c3a6a833ddd255cda61696f2fda
  translated_at: '2026-09-26T06:54:32+00:00'
  engine: anthropic
  token_count: 1200
---
# std::function's small buffer optimization: the cost of type erasure

## The convenience and the cost of type erasure

`std::function` is C++'s most convenient "hold any callable" container: function pointers, lambdas, function objects, bind expressions — as long as the signature matches, anything goes in. Its implementation relies on **type erasure**: instead of baking the callable's type into a template parameter, it calls through a uniform internal interface (usually a virtual function or a table of function pointers).

That convenience has a cost. Let's measure it (on this machine):

```text
===== std::function SBO =====
调用:
  调用 - 函数指针:               0.26 ns
  调用 - function+小lambda(SBO): 1.61 ns
  调用 - 直接 lambda(对照):     0.25 ns

构造 1000000 次:
  function 装函数指针:     2.0 ns/次
  function 装小lambda(SBO): 2.3 ns/次
  function 装大lambda(堆分配): 19.6 ns/次 ← 堆分配开销

sizeof(std::function<int(int)>) = 32
```

Two costs:

**1. The call is indirect, about 6x slower than a direct lambda**. A direct lambda (0.25 ns) is as fast as a function pointer (0.26 ns) — both are direct calls the compiler can inline; `std::function` (1.61 ns) has to go through the type-erased indirect call (vtable/function-pointer lookup plus a jump), **about 6x**. Same story as the virtual functions in 06-01: indirect calls block inlining.

**2. Construction may heap-allocate**. When `std::function` holds a callable, it has to store that callable's state. Most implementations have **SBO (Small Buffer Optimization)**: a small buffer is reserved inside the `std::function` object itself (on this machine, libstdc++'s `std::function<int(int)>` is 32 bytes). Captures with small state (≤ the SBO threshold, usually 16-24 bytes) are stored inline, no heap allocation; captures with large state (over the threshold) have no option but to `new` a block on the heap.

Measured construction cost: a small lambda (SBO hit) takes 2.3 ns per construction, **a large lambda (over SBO, heap-allocated) takes 19.6 ns**, **8.5x**. That gap mostly comes from the cost of a single `new`/`delete` heap allocation (on the order of tens of nanoseconds).

## When these two costs bite

**The 6x call cost**: for a callback invoked "once in a while", it doesn't matter (the callback isn't on the hot path); for a callback invoked "a million times per frame", 6x is real money. Take an event dispatcher: if every dispatch goes through `std::function`, the call overhead can become the bottleneck, and switching to a template (compile-time polymorphism) or a function pointer is much faster.

**The construction heap-allocation cost**: this is the easier trap to fall into. Consider code like this:

```cpp
// Hot path: repeatedly constructing a function with a big capture
for (auto& item : items) {
    std::function<void(int)> f = [item, ctx](int x) { /* big capture */ };
    dispatch(f);
}
```

Every loop iteration constructs a `std::function`, and if the capture is big (over SBO), **every iteration does a `new`/`delete`**: heap allocation + cache misses + possible malloc lock contention (under multithreading). This pattern of "repeatedly constructing a function on the hot path" is a performance black hole. A few common fixes:

- **Use a template parameter (compile-time polymorphism)**: make the callback type a template parameter, eliminating type erasure. The cost is that call sites must know the type at compile time.
- **A fixed-signature function pointer**: if the callback captures nothing, just use `void(*)(int)` — zero overhead.
- **Reuse the `std::function` object**: construct it once outside the loop, and only mutate its state inside the loop (though mutating state may still heap-allocate).
- **Avoid unnecessary captures**: the less a lambda captures, the more likely it is to hit SBO.

## SBO is the same idea as string's SSO

SBO is the same idea as `std::string`'s **SSO (Small String Optimization)**: both keep a small buffer inside the object — small stays inline, only big goes to the heap. Both resolve the tension between "type erasure / dynamic size" and "avoiding heap allocation on the hot path". The mechanics of SBO/SSO (why the threshold is 16-24 bytes, how it cooperates with the ABI) belong to vol3/vol4; vol6 only covers the layer of "it affects the heap-allocation cost of hot-path construction".

The sizeof of `std::function` varies by implementation (libstdc++ 32 bytes, libc++ 48 bytes, MSVC different again), and the SBO threshold varies with it. So "will this lambda of mine trigger a heap allocation" is a question for `sizeof` or for reading the implementation — but **the general advice is: don't depend on hitting SBO on the hot path; big captures belong in templates**.

One sentence to wrap up: `std::function` has two costs — the call is indirect (about 6x slower than a direct lambda), and construction may heap-allocate (triggered by a big capture, about 8.5x more expensive than SBO); SBO lets small captures (≤16-24B) live inside the object with no heap allocation, while big captures heap-allocate; on the hot path, avoid repeatedly constructing `std::function` with a big capture — that is a heap-allocation black hole, and the fixes are a template parameter, a function pointer, reusing the object, or cutting captures; SBO shares its idea with string's SSO, and the mechanics belong to vol3/vol4.

## References

- cppreference *std::function* — type-erasure semantics, SBO notes
- Sutter/Stroustrup CppCoreGuidelines *F.50* — when to use function vs template vs function pointer
- Agner Fog, *Optimizing software in C++*, object/container overhead. Local copy
- The measurement code for this article: `code/volumn_codes/vol6-performance/ch06/function_sbo.cpp`
