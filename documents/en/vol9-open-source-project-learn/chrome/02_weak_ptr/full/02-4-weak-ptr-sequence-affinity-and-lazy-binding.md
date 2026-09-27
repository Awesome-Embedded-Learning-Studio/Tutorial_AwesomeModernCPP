---
chapter: 1
cpp_standard:
- 17
- 20
description: "State WeakPtr's sequence contract head-on — deref and invalidation must
  land on the bound sequence — plus the Flag's lazy sequence-binding mechanism (release
  relies on discipline), and pin down exactly how IsValid and MaybeValid differ"
difficulty: intermediate
order: 4
platform: host
prerequisites:
- 'WeakPtr hands-on (III): WeakPtrFactory and the last-member idiom'
- 'WeakPtr prerequisite (III): sequences, SEQUENCE_CHECKER, and DCHECK/CHECK'
reading_time_minutes: 12
related:
- 'WeakPtr hands-on (V): integrating with callbacks to close the OnceCallback loop'
- 'WeakPtr prerequisite (II): std::atomic and memory_order'
tags:
- host
- cpp-modern
- intermediate
- 智能指针
- weak_ptr
- 并发
title: "WeakPtr hands-on (IV): sequence affinity and lazy binding"
translation:
  source: documents/vol9-open-source-project-learn/chrome/02_weak_ptr/full/02-4-weak-ptr-sequence-affinity-and-lazy-binding.md
  source_hash: 6f4f745966f17db353ced3e2728c5070def4a180942420103e6bf252aa8c3f25
  translated_at: '2026-09-26T01:46:24+00:00'
  engine: anthropic
  token_count: 7300
---
# WeakPtr hands-on (IV): sequence affinity and lazy binding

In the previous pieces we walked you through standing up the WeakPtr skeleton — minting, dereferencing, invalidation on destruction — and functionally it runs. But there is one thread we have deliberately not touched, the one Chromium singles out in a long comment at the top of the source: a `WeakPtr` may be passed across sequences, but dereferencing and invalidation **must happen on the sequence it is bound to**. That is the thread we want to pull apart head-on in this piece, because two easy-to-confuse things hang from it: one is the Flag's lazy sequence binding, the other is the `IsValid`/`MaybeValid` pair of queries that look alike but mean completely different things. Once those two are clear, using WeakPtr across multiple sequences truly lands.

---

## The sequence contract: why deref and invalidation must stay on one sequence

First, the contract in its own words (`weak_ptr.h:50-54`):

> Weak pointers may be passed safely between sequences, but must always be dereferenced and invalidated on the same SequencedTaskRunner otherwise checking the pointer would be racey.

In one sentence: a weak pointer can be passed safely across sequences, but its dereference and invalidation must always land on the same `SequencedTaskRunner` — otherwise the very act of "checking this pointer" is itself a race.

You might object: isn't `invalidated_` atomic, so how can there be a race? Atomic operations themselves indeed never tear; the problem lives in the window where "`get()` returns non-null → the caller takes the `T*` and accesses it". Picture sequence A holding a `WeakPtr`: `get()` has just read "valid" once and is about to deref; meanwhile sequence B calls `Invalidate()`, and right after that the owner destroys the object. The `T*` in A's hand looks fine, but actually accessing it may step into a half-destroyed object, or even memory that has already been reclaimed. Atomicity only guarantees that reads and writes do not tear; it cannot guarantee that nobody touches the object during the deref window. The cleanest constraint is simply this — make deref and invalidation run on the same sequence, so the window never exists in the first place.

As for passing across sequences, that is allowed. You can hand a `WeakPtr<Controller>` off from sequence A to sequence B (say, drop it into the thread pool and post a task back to A); the pass itself only moves data and never touches the Flag. Only when B wants to genuinely "use" it (deref) or "kill" it do you step onto the contract's turf.

---

## Lazy binding: the Flag does not bind to a sequence at construction

With the contract in place, the next unavoidable question: how does the Flag know which sequence the "bound sequence" actually is? Chromium's answer: it does not bind at construction at all — the binding settles only the first time someone "touches" it. This is lazy binding.

Look at `Flag`'s constructor — it does exactly one thing, `DETACH_FROM_SEQUENCE` (`weak_ptr.cc:15-20`):

```cpp
WeakReference::Flag::Flag() {
    // Flags only become bound when checked for validity, or invalidated,
    // so that we can check that later validity/invalidation operations
    // on the same Flag take place on the same sequenced thread.
    DETACH_FROM_SEQUENCE(sequence_checker_);
}
```

`DETACH_FROM_SEQUENCE` means "I am not bound to any sequence yet, don't check me". At this point the Flag is in the unbound state. When someone first touches it by calling `IsValid` or `Invalidate`, `DCHECK_CALLED_ON_VALID_SEQUENCE` records the current sequence; from then on the Flag is loyal to it alone, and every subsequent `IsValid`/`Invalidate` must run on that sequence:

```cpp
bool WeakReference::Flag::IsValid() const {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);   // first touch → bind; afterwards → verify
    return !invalidated_.IsSet();
}
```

Why must it be lazy? Because huge numbers of objects in Chromium are "constructed on sequence A, but used on sequence B" — at the moment of construction you have no idea which sequence they will end up running on. If the Flag were hard-bound to the constructing sequence the instant it is built, all those objects would become outright unusable. Lazy binding makes "commit to nothing at construction, adopt a sequence on first real use" a workable path, and the barrier to using the thing drops away at once.

There is an even finer opening hidden in here. If a Flag currently has no WeakPtr holding it (`!HasRefs()`), `WeakReferenceOwner::GetRef` detaches it again (`weak_ptr.cc:91-101`):

```cpp
WeakReference WeakReferenceOwner::GetRef() const {
#if DCHECK_IS_ON()
    DCHECK(flag_);
    if (!HasRefs()) {
        flag_->DetachFromSequence();   // no holders → unbind; can bind to another sequence next time
    }
#endif
    return WeakReference(flag_);
}
```

This block only takes effect under `DCHECK_IS_ON()`, but it opens the door to "once every WeakPtr from a factory is gone, keep using the factory on a different sequence" — the next WeakPtr it mints will, on first touch, bind to the new sequence. The source comment calls this out explicitly (`weak_ptr.h:63-65`): once all WeakPtrs are destroyed or invalidated, the factory becomes unbound from its sequence; it can be destroyed on another sequence, or mint fresh WeakPtrs again.

---

## Release builds: zero overhead + discipline

Here we should line this up against the key fact from [prerequisite (III)](./pre-03-weak-ptr-sequence-checker-dcheck-check.md): all of the lazy-binding-plus-sequence-checking logic is wrapped in `DCHECK_IS_ON()`, and release builds compile it away to nothing.

In other words, the release build of WeakPtr carries not a shred of runtime sequence checking. In release, the sequence contract rests entirely on developer discipline: violate it and the program will not crash on the spot — a race will simply pop up at some unlucky moment. That is why taking every DCHECK seriously in debug builds matters so much: it is pretty much your only chance to catch a sequence violation.

---

## IsValid vs MaybeValid: exact on the bound sequence vs a cross-sequence hint

Lazy binding piles the whole "sequence contract check" onto `IsValid`, yet WeakPtr also throws out another query, `MaybeValid`. The two look alike but their semantics are far apart; fail to keep them straight and you will step in a hole sooner or later.

Take `IsValid` first: it performs a sequence assertion — you must call it on the bound sequence, and its return value is 100% accurate. `true` genuinely means still valid; `false` genuinely means invalidated. `WeakPtr::get()` and `operator bool` take exactly this path, which is why "check liveness, then deref" is the safe routine. There is one more side effect: it is precisely this `IsValid` call that triggers lazy binding (if it has not bound before).

```cpp
bool WeakReference::Flag::IsValid() const {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);   // same-sequence contract
    return !invalidated_.IsSet();
}
```

`MaybeValid` is different: it carries no sequence assertion whatsoever and can be called from any sequence. The Chromium source comment states its boundaries very plainly (`weak_ptr.h:266-283`):

> Returns false if the WeakReference is confirmed to be invalid. This call is safe to make from any thread, e.g. to optimize away unnecessary work, but `RefIsValid()` must always be called, on the correct sequence, before actually using the pointer.

Burn the asymmetry of its return value into memory. A `false` return is trustworthy — an acquire read observed the invalidation bit written under release, so it is invalidated beyond any doubt. A `true` return, though, cannot be taken at face value; it only counts as "maybe" still valid: you might have just read `true`, and then, before your deref, the bound sequence goes ahead and invalidates it. So `MaybeValid` has exactly one genuinely appropriate use — a speculative "can I skip this" decision made from another sequence. For example, a message loop can glance at it before dispatching a task: a `false` tells you the task is definitively pointless, so skip it and save one cross-sequence hop. But the moment you actually want to touch the pointer, you must return to the bound sequence and pass `IsValid` again. Treat a positive result as a deref license and sooner or later you will crash.

```cpp
bool WeakReference::Flag::MaybeValid() const {
    return !invalidated_.IsSet();   // no sequence assertion; callable from any sequence
}
```

A quick comparison table, for when you want to look things back up:

| Query | Sequence constraint | Triggers lazy binding | Trustworthiness of the result |
|---|---|---|---|
| `IsValid()` | must be the bound sequence | yes | 100% accurate |
| `MaybeValid()` | any sequence | no | trustworthy when negative, not when positive |

This distinction becomes a major tool in the next piece (02-5, the BindOnce integration) — you will see that Chromium's callback cancellation goes through `IsValid` (exact, same-sequence), while `MaybeValid` is a separate, independent "scheduler speculation query" channel.

---

## Adding sequence checks to the teaching version

Next, let us retrofit the `Flag` from 02-2 with sequence checks. The teaching version uses a simplified `SequenceChecker` that records the thread id in debug builds:

```cpp
// Platform: host | C++ Standard: C++17  (debug-only checks)
#if defined(NDEBUG)
// release: all no-op, zero bytes, zero overhead
class SequenceChecker {
public:
    void detach_from_sequence() noexcept {}
    bool called_on_valid_sequence() const noexcept { return true; }
};
#else
#include <thread>
// debug: record the bound thread, abort on violation
class SequenceChecker {
public:
    void detach_from_sequence() noexcept { bound_thread_ = std::thread::id{}; }
    bool called_on_valid_sequence() const noexcept {
        if (bound_thread_ == std::thread::id{}) {
            bound_thread_ = std::this_thread::get_id();   // lazy binding
            return true;
        }
        return bound_thread_ == std::this_thread::get_id();
    }
private:
    mutable std::thread::id bound_thread_;
};
#endif
```

You can map this against Chromium's three macros: `detach_from_sequence` corresponds to `DETACH_FROM_SEQUENCE`, `called_on_valid_sequence` to `DCHECK_CALLED_ON_VALID_SEQUENCE`, and under release everything is a no-op. The teaching version simulates a sequence with a thread id; real Chromium uses the finer-grained `SequenceToken`, but the shape of the lazy binding is identical.

Then `Flag` wires it in:

```cpp
class Flag : public RefCountedThreadSafe {
public:
    Flag() { seq_.detach_from_sequence(); }                 // construction: unbound

    void Invalidate() noexcept {
        // DCHECK: same sequence, or only one ref left (may be destroyed cross-thread)
        assert(seq_.called_on_valid_sequence() || has_one_ref());
        invalidated_.Set();
    }
    bool IsValid() const noexcept {
        assert(seq_.called_on_valid_sequence());            // first touch → bind
        return !invalidated_.IsSet();
    }
    bool MaybeValid() const noexcept {
        return !invalidated_.IsSet();                       // does not touch seq_, does not bind
    }
private:
    // ...
    mutable SequenceChecker seq_;
    AtomicFlag invalidated_;
};
```

With this wiring, if you call `IsValid` on the wrong thread in a debug build, the assert will catch the sequence violation; in release, these asserts vanish along with the `SequenceChecker` itself — zero overhead.

---

This piece has taken the sequence contract apart head-on: a weak pointer can travel across sequences, but deref and invalidation must land on the same bound sequence, otherwise the check itself is a race. The Flag's lazy binding — detach at construction, bind on first touch, unbind and reuse once no references remain — makes "no sequence pinned at construction" workable, which fits the crowd of Chromium objects that are "constructed in one place, used in another". The entire check is wrapped in `DCHECK_IS_ON()`: zero overhead in release, with the contract resting on discipline alone. And the query pair `IsValid` (same-sequence, exact, triggers binding) versus `MaybeValid` (any sequence, optimistic, trustworthy when negative but not when positive) absolutely must be kept apart — the former is the hard gate before a deref, the latter is merely a hint for the scheduler's speculative skips.

The real "eye" of the whole series — the pivotal move — comes in the next piece: we wire WeakPtr into the callback system and watch `BindOnce` use `IsValid` to turn a callback into an automatic no-op once its object dies, finally tying off the loose end left behind by the hand-rolled cancellation token in 01-4.

## References

- [Chromium `base/memory/weak_ptr.h` — the thread-safety comment at the top (lines 50-69)](https://source.chromium.org/chromium/chromium/src/+/main:base/memory/weak_ptr.h)
- [Chromium `base/memory/weak_ptr.cc` — Flag/WeakReferenceOwner](https://source.chromium.org/chromium/chromium/src/+/main:base/memory/weak_ptr.cc)
- [WeakPtr prerequisite (III): sequences, SEQUENCE_CHECKER, and DCHECK/CHECK](./pre-03-weak-ptr-sequence-checker-dcheck-check.md)
- [Chromium `base/sequence_checker.h`](https://source.chromium.org/chromium/chromium/src/+/main:base/sequence_checker.h)
