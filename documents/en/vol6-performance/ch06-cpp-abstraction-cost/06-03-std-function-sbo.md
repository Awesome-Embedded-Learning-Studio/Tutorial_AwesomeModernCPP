---
chapter: 6
cpp_standard:
- 17
description: 'std::function type-erases any callable object, and the price is call indirection plus possible heap allocation. This article measures it: a call is about 6x slower than a direct lambda (1.61 ns vs 0.25 ns), and at construction a small capture hits SBO (2.3 ns) while a large capture triggers heap allocation (19.6 ns, 8.5x). Repeatedly constructing a function plus a large capture on the hot path is what to watch for; the fixes are a template parameter (compile-time polymorphism) or a fixed-signature function pointer'
difficulty: advanced
order: 3
platform: host
prerequisites:
- Virtual functions and devirtualization
- Cachelines and locality - the 64-byte minimum unit of transfer
reading_time_minutes: 4
related:
- The cost cheat sheet for C++ abstractions
- The real cost of RVO, NRVO, and move
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
  translated_at: '2026-07-06T00:00:00+00:00'
  engine: manual
  token_count: 3200
---
# std::function's small buffer optimization: the cost of type erasure

## The convenience and cost of type erasure

`std::function` is C++'s most convenient "stuffs any callable" container: function pointers, lambdas, function objects, bind expressions, as long as the signature matches you can stuff them in. Its implementation relies on **type erasure**: it doesn't pin the callable's type as a template parameter, instead it uses a uniform internal interface (usually a virtual function or a function-pointer table) to do the call.

That convenience has a cost. Let's measure (this machine):

```text
===== std::function SBO =====
Calls:
  call - function pointer:               0.26 ns
  call - function+small lambda (SBO): 1.61 ns
  call - direct lambda (control):     0.25 ns

Construct 1000000 times:
  function holding function pointer:     2.0 ns/time
  function holding small lambda (SBO): 2.3 ns/time
  function holding large lambda (heap alloc): 19.6 ns/time ← heap-allocation cost

sizeof(std::function<int(int)>) = 32
```

Two costs:

**1. The call is indirect, about 6x slower than a direct lambda**. A direct lambda (0.25 ns) and a function pointer (0.26 ns) are equally fast (both are direct calls plus inlinable); `std::function` (1.61 ns) has to go through the type-erased indirect call (vtable/function-pointer lookup plus jump), **about 6x**. Same logic as the virtual functions in 06-01: indirect calls block inlining.

**2. Construction may heap-allocate**. When `std::function` holds a callable, it has to store the callable's state. Most implementations have **SBO (Small Buffer Optimization)**: a small buffer is reserved inside the `std::function` object (on this machine's libstdc++ an `std::function<int(int)>` is 32 bytes); captures with small state (≤ the SBO threshold, usually 16-24 bytes) are stored inline without heap allocation; captures with large state (over the threshold) have to `new` a heap block.

Measured construction cost: a small lambda (SBO hit) is 2.3 ns/time, **a large lambda (over SBO, heap allocated) is 19.6 ns/time**, **8.5x**. That gap mostly comes from the cost of one `new`/`delete` heap allocation (tens-of-nanoseconds level).

## When these two costs bite

**The 6x call cost**: for "occasional" callbacks it doesn't matter (the callback isn't on the hot path); for "called a million times per frame" callbacks, 6x is real money. For example an event dispatcher: if every dispatch goes through `std::function`, the call overhead can become the bottleneck; switching to a template (compile-time polymorphism) or a function pointer is much faster.

**The construction heap-allocation cost**: this is the easier pit to fall into. Consider this code:

```cpp
// Hot path repeatedly constructing a function plus a big capture
for (auto& item : items) {
    std::function<void(int)> f = [item, ctx](int x) { /* big capture */ };
    dispatch(f);
}
```

Every loop iteration constructs a `std::function`, and if the capture is big (over SBO), **every iteration does `new`/`delete`**: heap allocation plus cache miss plus possible malloc-lock contention (multithreaded). This "repeatedly constructing a function on the hot path" is a performance black hole; a few common fixes:

- **Use a template parameter (compile-time polymorphism)**: write the callback type as a template parameter, eliminating type erasure. The cost is that the call site has to know the type at compile time.
- **Fixed-signature function pointer**: if the callback has no capture, use `void(*)(int)` directly, zero overhead.
- **Reuse the `std::function` object**: construct once outside the loop, only mutate its state inside the loop (though mutating state may still heap-allocate).
- **Avoid unnecessary captures**: the less a lambda captures, the more likely it hits SBO.

## SBO is the same idea as string's SSO

SBO is the same idea as `std::string`'s **SSO (Small String Optimization)** (both reserve a small buffer inside the object; small goes inline, big heap-allocates). Both solve the contradiction "type erasure / dynamic size plus avoid hot-path heap allocation". The mechanism of SBO/SSO (why the threshold is 16-24 bytes, how it cooperates with the ABI) belongs to vol3/vol4; vol6 only covers "it affects the heap-allocation cost of hot-path construction".

The sizeof of `std::function` varies by implementation (libstdc++ 32 bytes, libc++ 48 bytes, MSFC different again), and the SBO threshold varies with it. So "will this lambda trigger a heap allocation" needs a `sizeof` or a look at the implementation; the **general advice is: don't rely on hitting SBO on the hot path; switch big captures to templates**.

In one sentence: `std::function` has two costs, an indirect call (about 6x slower than a direct lambda) and possible heap allocation on construction (triggered by a big capture, about 8.5x more expensive than SBO); SBO lets small captures (≤16-24B) stay inside the object without heap allocation while big captures heap-allocate; on the hot path avoid repeatedly constructing `std::function` plus a big capture, that's a heap-allocation black hole, and the fixes are a template parameter, a function pointer, reusing the object, or cutting captures; SBO is the same idea as string's SSO, and the mechanism belongs to vol3/vol4.

## References

- cppreference *std::function* — type-erasure semantics, SBO notes
- Stepov/Stroustrup CppCoreGuidelines *F.50* — when to use function vs template vs function pointer
- Agner Fog, *Optimizing software in C++*, object/container overhead (local)
- The measurement code for this article: `code/volumn_codes/vol6-performance/ch06/function_sbo.cpp`
