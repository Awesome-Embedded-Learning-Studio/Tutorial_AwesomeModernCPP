---
chapter: 1
cpp_standard:
- 17
- 20
description: "WeakPtr's test strategy: design cases around the six invariants, quantify object size, allocation, and call overhead, and weigh the trade-offs against std::weak_ptr and real Chromium"
difficulty: advanced
order: 3
platform: host
prerequisites:
- 'weak_ptr Design Guide (II): step-by-step implementation'
- 'once_callback Design Guide (III): test strategy and performance comparison'
reading_time_minutes: 7
related:
- 'weak_ptr Design Guide (I): motivation, API, and the control block'
- 'WeakPtr hands-on (VI): tests and performance comparison'
tags:
- host
- cpp-modern
- advanced
- 智能指针
- weak_ptr
- 测试
- 优化
title: "weak_ptr Design Guide (III): test strategy and performance comparison"
translation:
  source: documents/vol9-open-source-project-learn/chrome/02_weak_ptr/hands_on/03-weak-ptr-testing.md
  source_hash: 9a647a5bd9efa338e1f76ced6d07e8164df0cb9d0bc1eed40050c7acfe7b3321
  translated_at: '2026-09-26T01:58:06+00:00'
  engine: anthropic
  token_count: 3900
---
# weak_ptr Design Guide (III): test strategy and performance comparison

We knocked out the implementation in the last piece, and honestly we did not feel all that reassured — code that compiles is one thing, whether the semantics are right is quite another. WeakPtr is exactly the kind of thing whose scariest failure mode is "looks like it runs": you test one happy path, it goes green, while the traps — UAF, destruction races, the moved-from state of the source object — all lurk on the boundaries. So in this piece we nail the six invariants promised last time back into tests, one by one, and while we are at it we line up real numbers against `std::weak_ptr` and real Chromium to see exactly where the teaching version saves and what it gives up. The approach runs straight through [once_callback Design Guide (III)](../../01_once_callback/hands_on/03-once-callback-testing.md): invariants drive the cases, the data does the talking, no hand-waving.

## Six invariants → a test matrix

| # | Invariant | Assertion that must hold |
|---|---|---|
| 1 | Basic usability | While the object is alive, `wp` tests live and `get()` returns the true address |
| 2 | Move semantics | After the move, the source object is empty (`operator bool == false`) |
| 3 | Invalidated after invalidate | After `invalidate_weak_ptrs()`, every minted `wp.get()==nullptr` |
| 4 | CHECK-on-deref | Dereferencing an invalidated `wp` trips an assertion (debug assert / release CHECK) |
| 5 | maybe_valid asymmetry | Negative is trustworthy (false ⇒ certainly invalidated), positive is not |
| 6 | Factory destruction invalidates | After the factory destructs, every wp is invalid; the last member guards the member-destruction window |

## Key test cases (Catch2 style)

Six invariants sound abstract; on the ground, testing them comes down to picking the boundaries that are guaranteed to blow up the moment you get them wrong. Here we pick the three that lock the semantics down hardest — collective invalidation through the shared Flag, `was_invalidated` telling invalidation apart from a manual reset, and the destruction order of the last member. What is currently runnable in the project is the handful of demo .cpp files numbered `12` through `18` under `code/volumn_codes/vol9/full_tutorial_codes/chrome_design/`; wiring in the Catch2 test target is left as an extension, so for now let us just look at what the cases look like:

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
    REQUIRE(wp_a.was_invalidated());        // was invalidated
    auto wp_b = fac.get_weak_ptr();
    wp_b.reset();
    REQUIRE_FALSE(wp_b.was_invalidated());  // a manual reset does not count as invalidated
}

// invariant 6: the last member guards the member-destruction window
struct Good {                                  // ✓ factory declared last
    std::vector<int> buf_;
    WeakPtrFactory<Good> fac_{this};
};
struct Bad {                                   // ✗ factory declared first → destructed last
    WeakPtrFactory<Bad> fac_{this};
    std::vector<int> buf_;
};
TEST_CASE("last-member idiom: destruction order", "[weak_ptr][.death]") {
    // Good: fac_ destructs first → WeakPtr goes invalid → only then does buf_ destruct
    // Bad: buf_ destructs first → fac_ destructs later → in that window WeakPtr is still valid (dangling deref possible)
    // Verify in an isolated death test with TSan/AddressSanitizer that Good has no UAF
}
```

All three keep their eyes on semantic boundaries, not on the API surface. The shared Flag's collective invalidation tests whether the promise "one invalidate and everybody dies together" actually holds. The `was_invalidated` case is finer still — a manual `reset()` must not count as having been invalidated, which is why, when writing it, we deliberately set `wp_b` against `wp_a`: we did not want the two flavors of "becoming empty" blurred into one. The last-member case is the vital point of destruction order, and we pull it out for its own discussion.

Invariants 4 (CHECK-on-deref) and 6 (destruction order) share one nuisance: they abort. Run them straight inside an ordinary TEST_CASE and the whole binary goes down with them. So they have to be isolated as death tests that crash in a subprocess — the same routine 01-6 used for OnceCallback's consume-once assertion, and we already walked that road in that piece.

## Performance: object size

Start with the most tangible number — how many bytes does a `WeakPtr<T>` actually eat? We pin it down with a `static_assert`: if it does not compile, it is wrong:

```cpp
static_assert(sizeof(WeakPtr<Foo>) == sizeof(void*) * 2);   // 16 bytes (x86-64)
```

| Type | sizeof | Composition |
|---|---|---|
| `WeakPtr<T>` | 16 | `WeakReference` (scoped_refptr, 1 ptr) + `T*` (1 ptr) |
| `std::weak_ptr<T>` | 16 | object pointer + control block pointer |

When the sizeofs printed out the same we were mildly surprised at first — `std::weak_ptr` has a famous name, and we had expected it to be tighter. But think it through once more and it lines up: both sides carry two pointers, one to the object and one to the control block / Flag, so structurally they are symmetric. The real difference is in allocation behavior (see the table below), not in size.

And there is one gain `sizeof` cannot show: `TRIVIAL_ABI` lets a WeakPtr passed by value ride entirely in registers (two registers are enough), which `std::weak_ptr` cannot do. This is an ABI-level affair that a benchmark will not necessarily capture, but on hot paths it genuinely saves stack traffic.

## Performance: allocation and calls

With size tied, what really pulls the two apart is allocation count and liveness-check overhead. We lined the two up item by item:

| Dimension | `std::weak_ptr` | `WeakPtr` |
|---|---|---|
| Pointee allocation | `shared_ptr(new T)`: 2 allocations; `make_shared`: 1, but object and control block are fused into one allocation | Not imposed: 1 intrusive allocation for the Flag + the object allocated its own way |
| Liveness-check overhead | `lock()`: atomic read of the strong count + inc if alive + construct a temporary shared_ptr | `get()`: 1 atomic acquire-load, returns a raw pointer |
| Cross-sequence deref | `lock()` is thread-safe | Same sequence required (contract + DCHECK) |
| Bulk invalidation | None | **one invalidate invalidates them all** (shared Flag) |

The row most worth chewing on in that table is the liveness-check overhead. That `get()` is lighter than `lock()` is no coincidence — `lock()` must first atomically read the strong count, inc it once more after confirming the object is alive, and finally hand you a temporary `shared_ptr` (which then has to dec back down on the way out): several back-and-forth trips of atomic traffic. `get()` is one acquire-load returning a raw pointer, done. Of course there is a price: what comes back is a raw pointer, nobody manages synchronization for you, and the "same sequence" contract is what catches the fallout. We consider that a good bargain — the contract is what DCHECKs enforce in debug builds, so a genuine violation detonates during development instead of riding into production.

## vs real Chromium: the teaching version's trade-offs

| Dimension | Chromium | Teaching version |
|---|---|---|
| Flag refcount | `RefCountedThreadSafe` | Same |
| Atomic flag | `base::AtomicFlag` | `std::atomic` + memory_order (equivalent) |
| Sequence checking | `SEQUENCE_CHECKER` + SequenceToken | Simplified (thread id as a stand-in) |
| `SafeRef` | Full | Not implemented |
| `BindOnce` integration | Full `InvokeHelper` dual specialization | Simplified trampoline |
| `InvalidateAndDoom` | Full | Kept |
| `BindToCurrentSequence` | Full | Omitted |

Our trade-off logic for the teaching version went like this: save whatever is peripheral — `SafeRef`, `BindToCurrentSequence`, and the like got cut, and sequence checking is faked with a thread id — but the core mechanisms did not move an inch: the refcounted Flag, the acquire/release pairing, the sequence contract, and compile-time weak dispatch stay exactly what they should be. The reasoning is simple: the periphery is engineering convenience, while the core is where correctness lives. Cut the periphery and at worst the thing is awkward to use; cut the core and it is no longer WeakPtr.

With this, the three pieces on the WeakPtr component — design, implementation, and verification — are complete. Looking back, it and OnceCallback form a pair of sister series: the cancellation token we tossed out for convenience back in 01-4 finds its industrial-grade answer in this WeakPtr system, and the loop has now closed.

## References

- [Chromium `base/memory/weak_ptr_unittest.cc`](https://source.chromium.org/chromium/chromium/src/+/main:base/memory/weak_ptr_unittest.cc)
- [Catch2 documentation](https://github.com/catchorg/Catch2/tree/devel/docs)
- [weak_ptr Design Guide (I): motivation, API, and the control block](./01-weak-ptr-design.md)
- [once_callback Design Guide (III): test strategy and performance comparison](../../01_once_callback/hands_on/03-once-callback-testing.md)
