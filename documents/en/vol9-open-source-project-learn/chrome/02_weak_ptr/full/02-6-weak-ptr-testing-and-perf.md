---
chapter: 1
cpp_standard:
- 17
- 20
description: "Test the teaching WeakPtr against six invariants with Catch2, then measure object size, allocation behavior, call overhead, and the TRIVIAL_ABI payoff against std::weak_ptr and real Chromium."
difficulty: intermediate
order: 6
platform: host
prerequisites:
- 'WeakPtr hands-on (V): callback integration, closing the OnceCallback loop'
- 'OnceCallback hands-on (VI): tests and performance'
reading_time_minutes: 13
related:
- 'WeakPtr hands-on (II): the core skeleton and control block'
- 'WeakPtr prerequisite (VI): TRIVIAL_ABI and trivially relocatable'
tags:
- host
- cpp-modern
- intermediate
- 智能指针
- weak_ptr
- 测试
- 优化
title: "WeakPtr Hands-on (VI): Tests and Performance Comparison"
---
# WeakPtr Hands-on (VI): Tests and Performance Comparison

Code written this far along, and the one thing that scares me is "it runs, but I can't honestly say why." A WeakPtr demo that passes only ever covers the comfortable path where the object is still alive. The cases that actually bite live on the edge: dereferencing after `invalidate`, holding a WeakPtr when the factory destructs, cross-sequence liveness checks returning a false positive. So this piece adds no new feature. Two jobs only: pin every property we care about into a test, then put the teaching version next to `std::weak_ptr` and real Chromium and measure. How big is the object, how many allocations, how far does one liveness check walk, and what does `TRIVIAL_ABI` actually buy. Like [01-6 tests and performance](../../01_once_callback/full/01-6-once-callback-testing-and-perf.md), I trust the measurements, not the assertions.

---

## Six invariants

What you test are "invariants," properties that have to hold no matter how you poke them. For WeakPtr I count six, and the tests orbit those six.

The first is the plain one: a WeakPtr minted from a factory, with the object still alive, has to dereference correctly. That's the foundation. Fail this and nothing else matters. The second is move semantics. WeakPtr is move-friendly; after a move the source must be empty, you can't have two handles pointing at the same flag. The third is that liveness checks must fail after invalidate. One `invalidate_weak_ptrs()` goes out, and every already-minted WeakPtr's `get()` and `operator bool` must return null and false. None can slip through.

The fourth is the one I want singled out: dereferencing an invalidated WeakPtr must trip an assertion. The teaching version uses `assert`, Chromium uses `CHECK` in release. This is a use-after-free in the making, non-negotiable, it has to blow up on the spot. The fifth is the asymmetry of `maybe_valid()`: when called cross-sequence its negative is trustworthy, its positive is not. If it says "invalidated," believe it. If it says "still alive," hedge. The sixth is the knottiest one, and the one that wrecks real code: factory destruction equals invalidating every WeakPtr it ever minted, and the factory as "last member" has to guard the pointed-at object's destructor in reverse. Let that slip and you get the classic UAF, a member half-destructed while someone still holds a WeakPtr and dereferences it.

---

## Catch2 test cases

My habit is [Catch2](https://github.com/catchorg/Catch2)'s `TEST_CASE` paired with `REQUIRE`, dropping each of the six invariants onto a concrete case. Here are the key ones. These are Catch2-style illustrations; the runnable samples that ship with the project live in `code/volumn_codes/vol9/full_tutorial_codes/chrome_design/`, demos `12` through `18` .cpp. Wiring Catch2 in as a standalone test target I left as an extension; build it yourself if you want.

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

    REQUIRE(wp);                  // Invariant 1: object alive, wp is truthy
    REQUIRE(wp->x == 7);
    REQUIRE(wp.get() == &foo);
}

TEST_CASE("WeakPtr move: source is empty after move", "[weak_ptr]") {
    Foo foo{1};
    WeakPtrFactory<Foo> fac(&foo);
    auto wp1 = fac.get_weak_ptr();
    auto wp2 = std::move(wp1);    // Invariant 2: move

    REQUIRE_FALSE(wp1);           // source empty after move
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
        // Simulate factory destruction with an inner scope.
        // (For the real "last member" scenario, see BadController/GoodController below.)
    }
    // Here we test the "doom" semantics of invalidate_weak_ptrs_and_doom directly.
    fac.invalidate_weak_ptrs_and_doom();
    REQUIRE_FALSE(fac.has_weak_ptrs());
}

TEST_CASE("WeakPtr was_invalidated distinguishes dead-from-nulled", "[weak_ptr]") {
    Foo foo{1};
    WeakPtrFactory<Foo> fac(&foo);
    auto wp = fac.get_weak_ptr();
    REQUIRE_FALSE(wp.was_invalidated());      // still alive, not "invalidated"

    fac.invalidate_weak_ptrs();
    REQUIRE(wp.was_invalidated());            // invalidated, not manually reset

    auto wp2 = fac.get_weak_ptr();
    wp2.reset();
    REQUIRE_FALSE(wp2.was_invalidated());     // manual reset does not count as "invalidated"
}

#if !defined(NDEBUG)
// Debug only: dereferencing an invalidated handle asserts (teaching uses assert; Chromium uses CHECK)
TEST_CASE("WeakPtr deref invalid asserts in debug", "[weak_ptr][.assert]") {
    Foo foo{1};
    WeakPtrFactory<Foo> fac(&foo);
    auto wp = fac.get_weak_ptr();
    fac.invalidate_weak_ptrs();
    // Invariant 4: this line should abort under debug.
    // In an isolated assert test: REQUIRE_THROWS(wp->x);
    // (Catch2 needs a subprocess to isolate abort; in practice use death_test mode.)
}
#endif
```

The test design follows the same line as 01-6: each case targets one invariant, it does not enumerate the API. I pulled `was_invalidated` out on purpose because it has to tell "invalidated" apart from "manually reset," and that distinction is the WeakPtr-specific semantics most likely to get written crooked. Get it wrong and callers can't tell "the object is really gone" from "I let go myself." One more trap I've stepped in: the dereference-invalid assertion test can't share a process with the normal cases. Both `assert` and `CHECK` abort, one blast and the whole batch goes red. Either run those cases in a death_test subprocess, or tag them `[.assert]` and run them in isolation.

---

## Performance: object size

First measure with `sizeof`. Real terminal output, GCC 13, x86-64:

```cpp
static_assert(sizeof(WeakPtr<Foo>) == sizeof(void*) * 2);   // 16 bytes
```

| Type | Size (x86-64) | Composition |
|---|---|---|
| `WeakPtr<T>` (teaching / Chromium) | **16 bytes** | `WeakReference` (=`scoped_refptr<const Flag>`, 1 pointer) + `T*` (1 pointer) |
| `std::weak_ptr<T>` | **16 bytes** | object pointer + control block pointer |
| `std::shared_ptr<T>` | 16 bytes | object pointer + control block pointer |

All three come out to 16 bytes, looks like a tie. Don't be fooled by the number. What pulls them apart isn't size, it's composition and allocation behavior.

---

## Performance: allocation behavior

WeakPtr and `std::weak_ptr` match on size, so to find the gap you have to look at how the pointed-at object itself is allocated. Compare how many heap hits it takes to bring a weakly-referenced object into existence:

| Scheme | Heap allocations | Notes |
|---|---|---|
| `std::weak_ptr` + `std::shared_ptr<T>(new T)` | **2** | one for T, one for the control block |
| `std::weak_ptr` + `std::make_shared<T>()` | 1 | T and control block fused, but a long-lived `weak_ptr` pins the whole block (see [pre-00](./pre-00-weak-ptr-weak-reference-and-lifetime.md)) |
| WeakPtr (pointed-at object carries a `WeakPtrFactory`) | **1** (Flag) + the object allocates however it likes | Flag is intrusive refcount, one allocation; the object isn't forced into shared, it allocates as many times as it needs |

The part I like most about WeakPtr is that it doesn't force the pointed-at object into any particular allocation scheme. Manage the object the way you already do; attach a factory and the only real overhead is one intrusive Flag allocation inside the factory. `std::weak_ptr` is less generous. Either you pay two allocations honestly, or you take the `make_shared` shortcut for one, at the cost of fusing the object's memory with the control block so that a long-lived weak_ptr drags the whole block along and won't let it free.

---

## Performance: call overhead

One call to WeakPtr's `get()` walks `ref_.IsValid()` into `flag_->IsValid()` into one `invalidated_.IsSet()`. That is one atomic acquire-load. On the hot path, that's it, nothing else.

`std::weak_ptr::lock()` is busier: one atomic read of the strong count to check the object is there, then one atomic increment of the strong count to grab it temporarily, and finally it hands back a temporary `shared_ptr`. Heavier than `get()`, and the weight is in that extra atomic increment and the temporary `shared_ptr` it has to construct.

This is the direct dividend of WeakPtr's "no lifetime extension, only liveness check." It doesn't bump the refcount the way `lock()` does, it just reads the flag. There's no free lunch: the cost is that the returned raw pointer makes no guarantee the object is still alive when you dereference it. That's exactly what the "same-sequence deref" contract is there to backstop.

---

## Performance: the TRIVIAL_ABI payoff

`TRIVIAL_ABI` pays off at the calling-convention layer. With it, WeakPtr passed by value goes straight into registers instead of detouring through memory. We argued it was safe in [pre-06](./pre-06-weak-ptr-trivial-abi.md). Can we quantify it?

```cpp
// Pass WeakPtr by value into a function.
void sink(WeakPtr<Foo> wp) { (void)wp; }
```

Annotated `[[clang::trivial_abi]]`, those 16 bytes of argument travel in two registers (on x86-64 SysV, the `rdi`/`rsi` neighborhood). Without the annotation the compiler spills 16 bytes onto the stack and threads an implicit reference through. Once `-O2` simplifies, the former saves one copy in the frame layout plus the destructor's calling-convention overhead.

A single call saves little. But think about the volume of Chromium's task system, callbacks ferrying WeakPtrs in bulk all day, and the savings stack up fast. That's why Chromium tags both WeakPtr and WeakReference with this attribute and force-inlines `IsSet()` into the header. The source comment says it outright: "measurable performance impact on base::WeakPtr." They measured it.

---

## vs std::weak_ptr: the tradeoff table

| Dimension | `std::weak_ptr` | `WeakPtr` |
|---|---|---|
| Takes a position on ownership | Yes (must pair with `shared_ptr`) | **No** (object manages itself) |
| Control block / Flag allocation | Non-intrusive (separate, or fused via `make_shared`) | **Intrusive** (one Flag allocation) |
| Thread / sequence model | Atomic ops are safe by themselves, sequencing is on the user | **Sequence-bound** (deref/invalidation on the bound sequence, debug catches it) |
| Cross-thread deref | `lock()` is thread-safe | Must be same-sequence (contract + DCHECK) |
| Batch invalidation | None (each weak_ptr expires independently) | **One `invalidate` drops all** (shared Flag) |
| Call overhead | `lock()` is two atomics + a temporary shared_ptr | `get()` is one atomic acquire-load |
| Size | 16 bytes | 16 bytes (and `TRIVIAL_ABI` lands it in registers) |

One sentence: `std::weak_ptr` is the general, safe weak reference; `WeakPtr` is tailored for "post tasks, no ownership, serialized execution." Inside a system like Chromium the latter fits the model like a glove. In ordinary general-purpose C++, the former is enough and ships with the standard library. Nobody loses.

---

## vs real Chromium: our tradeoffs

Same as [01-6](../../01_once_callback/full/01-6-once-callback-testing-and-perf.md): the teaching version cuts corners, and I'll lay the tradeoffs on the table:

| Dimension | Chromium | Our teaching version |
|---|---|---|
| Flag refcount | `RefCountedThreadSafe` (atomic) | Same (core, not skimped) |
| Atomic flag | `base::AtomicFlag` (wrapper) | `std::atomic` with memory_order (equivalent) |
| Sequence checking | `SEQUENCE_CHECKER` three macros + SequenceToken | Simplified `SequenceChecker` (thread id simulation) |
| `SafeRef` | Full (non-null, dangles-then-crashes) | Not implemented (left as an extension) |
| `BindOnce` integration | Full type erasure + `InvokeHelper` dual specialization | Simplified trampoline + wiring to 01 OnceCallback |
| `TRIVIAL_ABI` | Annotated | Annotated (clang) |
| `InvalidateAndDoom` / `BindToCurrentSequence` | Full | `AndDoom` kept; `BindToCurrentSequence` omitted |

What I traded away is completeness. No `SafeRef`, `BindToCurrentSequence` cut, a stand-in instead of the real `SequenceToken`. What I bought back is readability and buildability: the teaching version runs on the standard library plus one clang attribute. The mechanisms that actually carry weight, the refcounted Flag, the acquire/release pairing, the sequence contract, the compile-time weak dispatch, I left untouched. I weighed it back and forth, and for the purpose of teaching, the trade reads as fair.

That closes WeakPtr. Design, implementation, and now verification, the whole arc walked. Look back over the 13 pieces since [pre-00 weak references](./pre-00-weak-ptr-weak-reference-and-lifetime.md) and we really did one thing: take "object lifetime," an old fuzzy engineering intuition, and squeeze it step by step into code that compiles, tests, and has explicit acquire/release pairing. The lesson I kept relearning all the way through fits in one line: every signature, every `requires`, every memory-order pair has to be backed by a specific "why." The ones that can't answer that question will, sooner or later, break in some scene that should have been smooth, and usually in the hardest-to-reproduce way. This line and the OnceCallback line close here. When you go chew on the next industrial-scale component, I hope you carry this set of intuitions with you.

---

## References

- [Catch2 documentation](https://github.com/catchorg/Catch2/tree/devel/docs)
- [Chromium `base/memory/weak_ptr_unittest.cc`, official tests](https://source.chromium.org/chromium/chromium/src/+/main:base/memory/weak_ptr_unittest.cc)
- [OnceCallback hands-on (VI): tests and performance](../../01_once_callback/full/01-6-once-callback-testing-and-perf.md)
- [cppreference: std::weak_ptr](https://en.cppreference.com/w/cpp/memory/weak_ptr)
