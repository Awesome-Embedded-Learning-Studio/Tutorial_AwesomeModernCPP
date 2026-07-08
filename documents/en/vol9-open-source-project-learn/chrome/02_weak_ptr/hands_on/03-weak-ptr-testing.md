---
chapter: 1
cpp_standard:
- 17
- 20
description: "WeakPtr's test strategy: design cases around six invariants, then quantify object size, allocation, and call cost against std::weak_ptr and real Chromium to see where the teaching version saves and what it gives up"
difficulty: advanced
order: 3
platform: host
prerequisites:
- weak_ptr design guide (II): step-by-step implementation
- OnceCallback design guide (III): test strategy and performance
reading_time_minutes: 7
related:
- weak_ptr design guide (I): motivation, API, and the control block
- WeakPtr hands-on (VI): tests and performance
tags:
- host
- cpp-modern
- advanced
- 智能指针
- weak_ptr
- 测试
- 优化
title: "weak_ptr Design Guide (III): Test Strategy and Performance"
---
# weak_ptr Design Guide (III): Test Strategy and Performance

We finished the implementation in the last piece, and honestly we weren't entirely at ease with it. Code that compiles is one thing; correct semantics is another. WeakPtr is exactly the kind of thing that "looks like it runs": you test a happy path, it goes green, and the real traps hide on the boundaries. UAF, destruction races, the source-object state after a move. This piece pins the six invariants we promised last time back into actual test cases, and then puts real numbers next to `std::weak_ptr` and real Chromium to see where the teaching version saves and what it sacrifices. The approach is the same one we took in [OnceCallback design guide (III)](../../01_once_callback/hands_on/03-once-callback-testing.md): invariants drive the cases, numbers do the talking, no hand-waving.

## Six invariants into a test matrix

| # | Invariant | Assertion that must hold |
|---|---|---|
| 1 | Basic usability | While the object is alive, `wp` is truthy and `get()` returns the real address |
| 2 | Move semantics | After move, the source is empty (`operator bool == false`) |
| 3 | Invalidated on demand | After `invalidate_weak_ptrs()`, every minted `wp.get()==nullptr` |
| 4 | CHECK-on-deref | Dereferencing an invalidated `wp` trips an assertion (debug assert / release CHECK) |
| 5 | maybe_valid asymmetry | Negative is trusted (false ⇒ definitely invalidated), positive is not |
| 6 | Factory destruction invalidates | After the factory destructs, all wp are invalid; the last member guards during member destruction |

## Key cases (Catch2 style)

Six invariants sound abstract. On the ground they collapse to the boundaries that blow up the moment you get them wrong. We pick three that lock down the semantics hardest: collective invalidation through the shared Flag, `was_invalidated` separating invalidated from manually reset, and the destruction order of the last member. The runnable demos in `code/volumn_codes/vol9/full_tutorial_codes/chrome_design/` are `12` through `18`; wiring the Catch2 test target is left as an extension. For now, here is what the cases look like:

```cpp
// Platform: host | C++ Standard: C++20
#include <catch2/catch_test.hpp>
#include "weak_ptr/weak_ptr.hpp"
using namespace tamcpp::chrome;

struct Foo { int x = 42; };

TEST_CASE("invalidate kills all weak ptrs sharing the flag", "[weak_ptr]") {
    Foo foo;  WeakPtrFactory<Foo> fac(&foo);
    auto wp1 = fac.get_weak_ptr();
    auto wp2 = fac.get_weak_ptr();          // same factory → shared Flag
    fac.invalidate_weak_ptrs();
    REQUIRE_FALSE(wp1);                     // invariant 3: collective invalidation
    REQUIRE_FALSE(wp2);
}

TEST_CASE("was_invalidated vs reset", "[weak_ptr]") {
    Foo foo;  WeakPtrFactory<Foo> fac(&foo);
    auto wp_a = fac.get_weak_ptr();
    fac.invalidate_weak_ptrs();
    REQUIRE(wp_a.was_invalidated());        // invalidated
    auto wp_b = fac.get_weak_ptr();
    wp_b.reset();
    REQUIRE_FALSE(wp_b.was_invalidated());  // manual reset is not invalidation
}

// invariant 6: last member guards during member destruction
struct Good {                                  // ✓ factory declared last
    std::vector<int> buf_;
    WeakPtrFactory<Good> fac_{this};
};
struct Bad {                                   // ✗ factory declared first → destructs last
    WeakPtrFactory<Bad> fac_{this};
    std::vector<int> buf_;
};
TEST_CASE("last-member idiom: destruction order", "[weak_ptr][.death]") {
    // Good: fac_ destructs first → WeakPtr invalidates → buf_ destructs
    // Bad: buf_ destructs first → fac_ destructs after → window where WeakPtr is still
    //      valid (a dangling deref is possible)
    // Verify with TSan/AddressSanitizer in an isolated death test that Good does not UAF
}
```

All three stare at semantic boundaries, not API surface. The shared-Flag case checks whether "one invalidate, everyone drops" actually holds. The `was_invalidated` case is finer: a manual `reset()` should not count as invalidation, and we deliberately contrast `wp_b` with `wp_a` so the two flavors of "became empty" don't blur together. The last-member case is the destruction-order crux, and we treat it on its own.

Invariants 4 (CHECK-on-deref) and 6 (destruction order) have a snag: they abort. Drop them into an ordinary TESTCASE and the whole binary goes down with them. So they have to be isolated as death tests that crash in a child process. This is the same trick 01-6 used for OnceCallback's single-consumption assertion; we already walked through it there.

## Performance: object size

Start with the most direct question: how many bytes does a `WeakPtr<T>` actually cost? We pin it with `static_assert`; if it doesn't compile, it's wrong:

```cpp
static_assert(sizeof(WeakPtr<Foo>) == sizeof(void*) * 2);   // 16 bytes (x86-64)
```

| Type | sizeof | Composition |
|---|---|---|
| `WeakPtr<T>` | 16 | `WeakReference` (scoped_refptr, 1 ptr) + `T*` (1 ptr) |
| `std::weak_ptr<T>` | 16 | object pointer + control block pointer |

The sizeof comes out equal, and we were a little surprised at first. `std::weak_ptr` has a reputation; we expected it to be tighter. But once you think about it the symmetry is obvious: both are two pointers, one to the object and one to the control block or Flag, structurally identical. The real gap is in allocation behavior (table below), not size.

There's also a gain `sizeof` can't show. `TRIVIAL_ABI` lets WeakPtr travel entirely in registers when passed by value (two registers is enough), which `std::weak_ptr` cannot do. That's an ABI-layer effect a benchmark won't necessarily catch, but on a hot path it saves real stack traffic.

## Performance: allocation and calls

Same size, and the real gap opens up in allocation count and liveness-check cost. We lined the two up side by side:

| Dimension | `std::weak_ptr` | `WeakPtr` |
|---|---|---|
| Pointed-at object allocation | `shared_ptr(new T)` 2 allocs; `make_shared` 1 but fuses the memory | Not forced: Flag is 1 intrusive alloc + the object allocates its own way |
| Liveness check cost | `lock()`: atomic read of strong count + if alive, inc + build a temporary shared_ptr | `get()`: 1 atomic acquire-load, returns a raw pointer |
| Cross-sequence deref | `lock()` is thread-safe | Same-sequence only (contract + DCHECK) |
| Batch invalidation | None | **One invalidate drops all** (shared Flag) |

The row worth sitting with is the liveness-check cost. `get()` is lighter than `lock()` for a real reason, not by luck. `lock()` has to atomically read the strong count, increment it if the object is alive, and then hand you back a temporary `shared_ptr` (which has to decrement again on the way out). That's several atomic round trips. `get()` is a single acquire-load that returns a raw pointer and is done. The price is real too: what you get back is a raw pointer, nobody synchronizes for you, and the "same sequence" contract is what backs it up. We think the trade is worth it. The contract is enforced by a DCHECK in debug, so a real violation blows up during development instead of leaking to production.

## vs real Chromium: teaching-version tradeoffs

| Dimension | Chromium | Teaching version |
|---|---|---|
| Flag refcount | `RefCountedThreadSafe` | Same |
| Atomic flag | `base::AtomicFlag` | `std::atomic` + memory_order (equivalent) |
| Sequence checking | `SEQUENCE_CHECKER` + SequenceToken | Simplified (thread id as a stand-in) |
| `SafeRef` | Full | Not implemented |
| `BindOnce` integration | Full `InvokeHelper` dual specialization | Simplified trampoline |
| `InvalidateAndDoom` | Full | Kept |
| `BindToCurrentSequence` | Full | Omitted |

The tradeoff logic for the teaching version goes like this. Trim what's peripheral: `SafeRef`, `BindToCurrentSequence`, and friends get cut; sequence checking stands in with a thread id. But the core mechanism stays untouched. Refcounted Flag, the acquire/release pairing, the sequence contract, compile-time weak dispatch stay exactly as they should be. The reasoning is simple. The periphery is engineering convenience; the core is the correctness load-bearing wall. Cutting the periphery makes the thing awkward to use. Cutting the core means it isn't WeakPtr anymore.

That closes the design, implementation, and verification trilogy for the WeakPtr component. Looking back, it's a sibling piece to OnceCallback: the cancellation token we threw in for convenience back in 01-4 has its industrial-strength answer in this WeakPtr system, and the loop is now closed.

## References

- [Chromium `base/memory/weak_ptr_unittest.cc`](https://source.chromium.org/chromium/chromium/src/+/main:base/memory/weak_ptr_unittest.cc)
- [Catch2 documentation](https://github.com/catchorg/Catch2/tree/devel/docs)
- [weak_ptr design guide (I): motivation, API, and the control block](./01-weak-ptr-design.md)
- [OnceCallback design guide (III): test strategy and performance](../../01_once_callback/hands_on/03-once-callback-testing.md)
