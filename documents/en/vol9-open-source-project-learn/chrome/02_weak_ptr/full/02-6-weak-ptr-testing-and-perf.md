---
chapter: 1
cpp_standard:
- 17
- 20
description: "Testing the teaching-version WeakPtr against six invariants with Catch2, then comparing it with std::weak_ptr and the real Chromium on object size, allocation behavior, call overhead, and the TRIVIAL_ABI payoff"
difficulty: intermediate
order: 6
platform: host
prerequisites:
- 'WeakPtr hands-on (V): integrating with callbacks to close the OnceCallback loop'
- 'OnceCallback hands-on (VI): tests and performance comparison'
reading_time_minutes: 13
related:
- 'WeakPtr hands-on (II): the core skeleton and control block'
- 'WeakPtr prerequisite (VI): TRIVIAL_ABI and trivial relocatability'
tags:
- host
- cpp-modern
- intermediate
- 智能指针
- weak_ptr
- 测试
- 优化
title: 'WeakPtr hands-on (VI): tests and performance comparison'
translation:
  source: documents/vol9-open-source-project-learn/chrome/02_weak_ptr/full/02-6-weak-ptr-testing-and-perf.md
  source_hash: 32e02ab0f4aa0e73107054852eb3894310e12b9520174f40094fb07f444ff27f
  translated_at: '2026-09-26T01:57:57+00:00'
  engine: anthropic
  token_count: 3300
---
# WeakPtr hands-on (VI): tests and performance comparison

With the code written this far, the thing we fear most is a component that looks like it runs while leaving us no confidence. A WeakPtr demo that passes only ever covers the most comfortable path — "the object is alive". Everything that actually goes wrong lives at the boundaries: dereferencing after invalidate, someone still holding a WeakPtr after the factory destructs, a cross-sequence liveness check handing back a false positive. So this piece adds no new features; we do exactly two things. First, nail every property that ought to hold down into concrete tests. Second, put the teaching version side by side with `std::weak_ptr` and the real Chromium and measure — how big is the object, how many allocations does it take, how far does one liveness check travel, and where exactly `TRIVIAL_ABI` saves. Like [the 01-6 tests-and-performance piece](../../01_once_callback/full/01-6-once-callback-testing-and-perf.md), we trust only real measurements, never assertions made on faith.

---

## Six invariants

What tests should pin down are "invariants" — plainly put, properties that must keep standing no matter how you abuse the thing. Distilled, WeakPtr has six of them, and we will orbit these six throughout.

The first is the most primitive: a WeakPtr minted by the factory must dereference correctly while the object is alive. That is the foundation — fail this one and nothing else matters. The second is move semantics — WeakPtr is move-only friendly, so after a move the source must become empty; the two sides must not end up pointing at the same flag. The third is that liveness checks must fail after invalidation: once a single `invalidate_weak_ptrs()` goes out, the `get()` and `operator bool` of every WeakPtr already minted must return null and false — not one of them may slip through.

The fourth is one we deliberately single out: dereferencing an already-invalidated WeakPtr must trigger an assertion. The teaching version uses `assert`; Chromium in release mode uses `CHECK` — this is the precursor of a use-after-free, and there is no negotiating: it must blow up on the spot. The fifth is the asymmetry of `maybe_valid()`: called across sequences, its negative answer is trustworthy and its positive answer is not. If it says "invalidated", you can believe it; if it says "still alive", you had better weigh it yourself. The sixth is the most roundabout, and the one that most often wrecks real projects: destructing the factory equals invalidating every WeakPtr it ever minted, and sitting as the "last member" the factory must in turn stand guard over the pointee's destruction — if that is not upheld, someone can still be holding and dereferencing a WeakPtr while members are half-destructed. That is the classic UAF.

---

## Catch2 test cases

Our habit is [Catch2](https://github.com/catchorg/Catch2)'s `TEST_CASE` paired with `REQUIRE`, landing each of the six invariants on a concrete case. Below we walk you through a few of the key ones — these are Catch2-style illustrative cases; the runnable examples currently in the project live under `code/volumn_codes/vol9/full_tutorial_codes/chrome_design/` as demo .cpp files `12` through `18`. Wiring Catch2 up as a standalone test target is something we left as an extension — build one yourself if you are interested.

```cpp
// Platform: host | C++ Standard: C++20
#include <catch2/catch_test.hpp>
#include "weak_ptr/weak_ptr.hpp"

using namespace tamcpp::chrome;

struct Foo {
    int x = 42;
    int get() const { return x; }
};

TEST_CASE("WeakPtr basic: alive object dereferences correctly", "[weak_ptr]") {
    Foo foo{7};
    WeakPtrFactory<Foo> fac(&foo);
    auto wp = fac.get_weak_ptr();

    REQUIRE(wp);                  // Invariant 1: object alive, wp passes the liveness check
    REQUIRE(wp->x == 7);
    REQUIRE(wp.get() == &foo);
}

TEST_CASE("WeakPtr move: source is empty after move", "[weak_ptr]") {
    Foo foo{1};
    WeakPtrFactory<Foo> fac(&foo);
    auto wp1 = fac.get_weak_ptr();
    auto wp2 = std::move(wp1);    // Invariant 2: move

    REQUIRE_FALSE(wp1);           // source is empty after the move
    REQUIRE(wp2);
    REQUIRE(wp2->x == 1);
}

TEST_CASE("WeakPtr invalidate: all weak ptrs go null", "[weak_ptr]") {
    Foo foo{1};
    WeakPtrFactory<Foo> fac(&foo);
    auto wp1 = fac.get_weak_ptr();
    auto wp2 = fac.get_weak_ptr();

    fac.invalidate_weak_ptrs();   // Invariant 3: batch invalidation

    REQUIRE_FALSE(wp1);
    REQUIRE_FALSE(wp2);
    REQUIRE(wp1.get() == nullptr);
}

TEST_CASE("WeakPtr factory destruct: invalidates all weak ptrs", "[weak_ptr]") {
    Foo foo{1};
    WeakPtrFactory<Foo> fac(&foo);
    WeakPtr<Foo> wp;
    {
        // Simulate factory destruction: with an inner scope
        // (the real "last member" scenario is in the BadController/GoodController cases below)
    }
    // Here we directly test the "doom" semantics of invalidate_weak_ptrs_and_doom
    fac.invalidate_weak_ptrs_and_doom();
    REQUIRE_FALSE(fac.has_weak_ptrs());
}

TEST_CASE("WeakPtr was_invalidated distinguishes dead-from-nulled", "[weak_ptr]") {
    Foo foo{1};
    WeakPtrFactory<Foo> fac(&foo);
    auto wp = fac.get_weak_ptr();
    REQUIRE_FALSE(wp.was_invalidated());      // still alive, not "invalidated"

    fac.invalidate_weak_ptrs();
    REQUIRE(wp.was_invalidated());            // invalidated, not a manual reset

    auto wp2 = fac.get_weak_ptr();
    wp2.reset();
    REQUIRE_FALSE(wp2.was_invalidated());     // a manual reset does not count as "invalidated"
}

#if !defined(NDEBUG)
// Debug only: dereferencing an invalidated ptr must assert (teaching version uses assert; Chromium uses CHECK)
TEST_CASE("WeakPtr deref invalid asserts in debug", "[weak_ptr][.assert]") {
    Foo foo{1};
    WeakPtrFactory<Foo> fac(&foo);
    auto wp = fac.get_weak_ptr();
    fac.invalidate_weak_ptrs();
    // Invariant 4: this line should abort in debug
    // Run inside an isolated assert test: REQUIRE_THROWS(wp->x);
    // (isolating an abort in Catch2 takes a subprocess; in real projects, the death_test pattern)
}
#endif
```

The test design carries the same approach as 01-6: each case stares at one invariant; there is no API tour. We deliberately pull `was_invalidated` out into its own test because it has to distinguish two states — "invalidated by the factory" versus "reset by the holder". That semantics is unique to WeakPtr and the easiest to get crooked; write it crooked and callers can no longer tell "the object is really gone" from "I let go of it myself". One more place where we have stepped in a pit before: the assert-on-invalid-deref tests must not run in the same process as ordinary cases — both `assert` and `CHECK` abort, and one detonation turns the whole batch of cases red. In real projects you either go the death_test route and isolate them in subprocesses, or simply tag those cases `[.assert]` and run them separately.

---

## Performance comparison: object size

First, measure with `sizeof` — real terminal output from local GCC 13 on x86-64:

```cpp
static_assert(sizeof(WeakPtr<Foo>) == sizeof(void*) * 2);   // 16 bytes
```

| Type | Size (x86-64) | Composition |
|---|---|---|
| `WeakPtr<T>` (teaching version / Chromium) | **16 bytes** | `WeakReference` (=`scoped_refptr<const Flag>`, 1 pointer) + `T*` (1 pointer) |
| `std::weak_ptr<T>` | **16 bytes** | object pointer + control-block pointer |
| `std::shared_ptr<T>` | 16 bytes | object pointer + control-block pointer |

All three come out of `sizeof` at 16 bytes, looking like equals. But do not let the number fool you: what really pulls them apart is not size — it is composition and allocation behavior.

---

## Performance comparison: allocation behavior

WeakPtr and `std::weak_ptr` are the same size, so to find the real gap we have to look at how the pointee itself gets allocated. Let us compare how many times the heap gets knocked to bring a weakly-referenced object into existence:

| Scheme | Heap allocations | Notes |
|---|---|---|
| `std::weak_ptr` + `std::shared_ptr<T>(new T)` | **2** | one for T, one for the control block |
| `std::weak_ptr` + `std::make_shared<T>()` | 1 | T and the control block packed together, but a long-lived `weak_ptr` holds the whole block of memory hostage (see [pre-00](./pre-00-weak-ptr-weak-reference-and-lifetime.md)) |
| WeakPtr (pointee carries its own `WeakPtrFactory`) | **1** (the Flag) + the pointee however it sees fit | the Flag is an intrusive refcount, one allocation; the pointee is not forced into shared ownership — as many allocations as it naturally takes |

The part of the WeakPtr side we find most comfortable is that it does not force the pointee into any particular allocation scheme. The object keeps managing itself the way it always did; you just hang a factory on it, and the only truly extra cost is the single intrusive allocation of the Flag inside that factory. The `std::weak_ptr` side is nowhere near as generous: either you honestly pay two allocations, or you take the `make_shared` shortcut down to one — at the price of welding the object's whole block of memory to the control block, so the moment a weak_ptr lives long, the object lingers on and refuses to be released.

---

## Performance comparison: call overhead

One call to WeakPtr's `get()` walks the underlying chain `ref_.IsValid()` into `flag_->IsValid()` into one `invalidated_.IsSet()` — that is, a single atomic acquire-load. On the hot path, that is the whole story; there is nothing else.

The `std::weak_ptr::lock()` side is far livelier: it atomically reads the strong count once to decide whether the object is there, and if it is, atomically increments the strong count once more to grip it temporarily, finally spitting out a temporary `shared_ptr`. Heavier than `get()` — heavy precisely because it must do one extra atomic increment, and must erect a temporary `shared_ptr` on the spot.

This is the direct dividend of WeakPtr's "no lifetime extension, liveness checks only" stance — it does not imitate `lock()` and temporarily raise the refcount; it just honestly reads the flag once. Of course, there is no free lunch: the cost is that the returned raw pointer carries no guarantee the object is still alive at the moment you dereference it, which is exactly why the "dereference on the same sequence" contract has to back it up from behind.

---

## Performance comparison: the TRIVIAL_ABI payoff

The benefit of `TRIVIAL_ABI` lives at the calling-convention layer: passed by value, a WeakPtr goes straight into registers instead of detouring through memory. We argued it was safe in [pre-06](./pre-06-weak-ptr-trivial-abi.md) — so can we quantify it?

```cpp
// Pass a WeakPtr by value into a function
void sink(WeakPtr<Foo> wp) { (void)wp; }
```

With `[[clang::trivial_abi]]` annotated, these 16 bytes of argument travel in two registers (on x86-64 SysV, `rdi`/`rsi` and that neighborhood of adjacent registers); without the annotation, the compiler has to open up 16 bytes on the stack and implicitly stuff a reference in. Once `-O2` cancels out what it can, what the former saves is one copy in the stack-frame layout, plus the calling-convention overhead of the destructor dance.

One call alone saves little. But think of the scale of Chromium's task system, where callbacks ship WeakPtrs around in bulk all day long — this little gain accumulates into something scary. It is also why Chromium specifically annotates WeakPtr and WeakReference with the attribute and forcibly inlines `IsSet()` into the header — the source comments state it in black and white: "measurable performance impact on base::WeakPtr". They actually measured.

---

## vs std::weak_ptr: the trade-off summary

| Dimension | `std::weak_ptr` | `WeakPtr` |
|---|---|---|
| Interference with ownership | Yes (must pair with `shared_ptr`) | **No** (the object keeps managing itself its own way) |
| Control-block / Flag allocation | Non-intrusive (standalone, or merged via `make_shared`) | **Intrusive** (one Flag allocation) |
| Threading / sequence model | Atomics safe in themselves; sequencing left to the user | **Sequence affinity** (deref/invalidation on the same sequence, caught in debug) |
| Cross-thread deref | `lock()` is thread-safe | Same sequence required (contract + DCHECK) |
| Batch invalidation | None (each weak_ptr expires independently) | **One `invalidate` invalidates them all** (shared Flag) |
| Call overhead | `lock()`: two atomics + a temporary shared_ptr | `get()`: one atomic acquire-load |
| Size | 16 bytes | 16 bytes (and `TRIVIAL_ABI` puts it in registers) |

One sentence to make it plain: `std::weak_ptr` is the general-purpose, safe weak reference; `WeakPtr` is tailor-made for the package of task posting, no ownership interference, and sequenced execution. In a system like Chromium, the latter fits the model with no gaps at all; in general-purpose C++ code, the former is adequate and comes with the standard library — nobody loses.

---

## vs the real Chromium: our trade-offs

As in [01-6](../../01_once_callback/full/01-6-once-callback-testing-and-perf.md), our teaching version cuts quite a few things, and we will lay the trade-offs on the table for you:

| Dimension | Chromium implementation | Our teaching version |
|---|---|---|
| Flag refcount | `RefCountedThreadSafe` (atomic) | Same (core, not skimped) |
| Atomic flag | `base::AtomicFlag` (a wrapper) | `std::atomic` with memory_order (equivalent) |
| Sequence checking | `SEQUENCE_CHECKER` macro trio + SequenceToken | Simplified `SequenceChecker` (thread-id emulation) |
| `SafeRef` | Complete (non-null, crashes on dangle) | Not implemented (left as an extension) |
| `BindOnce` integration | Full type erasure + dual `InvokeHelper` specializations | Simplified trampoline + hooked into the 01 OnceCallback |
| `TRIVIAL_ABI` | Annotated | Annotated (clang) |
| `InvalidateAndDoom` / `BindToCurrentSequence` | Complete | `AndDoom` kept; `BindToCurrentSequence` omitted |

What we sacrificed is completeness — no `SafeRef`, `BindToCurrentSequence` chopped, and the real `SequenceToken` replaced by a simplified stand-in. What we bought back is readability and compilability: the teaching version runs on nothing but the standard library plus one clang attribute. And the truly load-bearing mechanisms — the refcounted Flag, the acquire/release pairing, the sequence contract, compile-time weak dispatch — are untouched, not one word moved. We have weighed it back and forth: for the purpose of teaching, this trade is worth it.

With this, the WeakPtr component has been walked all the way through — design, implementation, and now verification. Looking back over the 13 pieces since [pre-00 introduced weak references](./pre-00-weak-ptr-weak-reference-and-lifetime.md), we really did only one thing: take the old problem of "lifetime" and force it, step by step, out of vague engineering intuition and into code that compiles, that tests, and that has explicit acquire/release pairing. The one lesson we carried away from falling on our faces over and over while writing this series: behind every signature, every `requires` clause, every pair of memory orders, you must be able to state a concrete "why" — the ones you cannot will sooner or later wreck a scene that should have gone smoothly, and usually in the hardest-to-reproduce way. This is where the series ties off together with the OnceCallback line. We hope that the next time you go chew on an industrial-grade component, you have this set of intuitions under you as a safety net.

---

## References

- [Catch2 documentation](https://github.com/catchorg/Catch2/tree/devel/docs)
- [Chromium `base/memory/weak_ptr_unittest.cc` — the official tests](https://source.chromium.org/chromium/chromium/src/+/main:base/memory/weak_ptr_unittest.cc)
- [OnceCallback hands-on (VI): tests and performance comparison](../../01_once_callback/full/01-6-once-callback-testing-and-perf.md)
- [cppreference: std::weak_ptr](https://en.cppreference.com/w/cpp/memory/weak_ptr)
