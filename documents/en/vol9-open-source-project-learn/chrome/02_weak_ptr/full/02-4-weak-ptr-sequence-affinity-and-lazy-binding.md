---
chapter: 1
cpp_standard:
- 17
- 20
description: "Lay out WeakPtr's sequence contract head-on: deref and invalidation must
  land on the bound sequence, and the Flag's lazy sequence-binding mechanism (release
  relies on developer discipline), and pin down the difference between IsValid and MaybeValid."
difficulty: intermediate
order: 4
platform: host
prerequisites:
- 'WeakPtr hands-on (III): WeakPtrFactory and the last-member idiom'
- 'WeakPtr prerequisite (III): sequences, SEQUENCE_CHECKER, and DCHECK/CHECK'
reading_time_minutes: 12
related:
- 'WeakPtr hands-on (V): callback integration, closing the OnceCallback loop'
- 'WeakPtr prerequisite (II): std::atomic and memory_order'
tags:
- host
- cpp-modern
- intermediate
- 智能指针
- weak_ptr
- 并发
title: "WeakPtr Hands-on (IV): Sequence Affinity and Lazy Binding"
---
# WeakPtr Hands-on (IV): Sequence Affinity and Lazy Binding

In the previous pieces we put the WeakPtr skeleton on its feet. Minting, dereferencing, destruction-driven invalidation, it all runs. But one thread we have not touched, the one Chromium calls out in a long comment at the top of the source: a `WeakPtr` may travel between sequences, but dereference and invalidation **must happen on the sequence it was bound to**. We want to take that contract apart head-on in this piece, because two easy-to-confuse things hang off it. One is the Flag's lazy sequence binding. The other is the pair `IsValid` and `MaybeValid`, which look alike and mean completely different things. Once those two are clear, using WeakPtr across sequences actually lands.

---

## The sequence contract: why deref and invalidation share a sequence

First the contract, in its own words (`weak_ptr.h:50-54`):

> Weak pointers may be passed safely between sequences, but must always be dereferenced and invalidated on the same SequencedTaskRunner otherwise checking the pointer would be racey.

One sentence: a weak pointer can be passed safely across sequences, but dereferencing and invalidating it must always land on the same `SequencedTaskRunner`, otherwise the act of "checking this pointer" is itself a race.

You might object: isn't `invalidated_` atomic, how is it a race. The atomic operation itself does not tear, yes. The problem is the window between "`get()` returns non-null" and "the caller takes that `T*` and uses it". Picture sequence A holding a `WeakPtr`. `get()` reads "valid" once and is about to deref. Sequence B calls `Invalidate()`, then the owner destructs the object. The `T*` in A's hand, which looked fine, now points at a half-built object or already-freed memory if A goes through with the access. Atomicity only guarantees the read/write does not tear. It cannot guarantee nobody touches the object during the deref window. The clean fix is to serialize deref and invalidation onto the same sequence, so the window never opens.

Passing the pointer across sequences is allowed. You hand a `WeakPtr<Controller>` from sequence A to sequence B (drop it into a thread pool, then post a task back to A). The handoff itself is just moving bytes, it never touches Flag. Only when B actually wants to "use" it (deref) or "void" it does it step onto the contract's territory.

---

## Lazy binding: Flag does not bind a sequence at construction

The contract is set. The next question is unavoidable: how does Flag know which sequence is "the bound one"? Chromium's answer: it does not bind at construction. It waits until the first time someone "touches" it, and only then does it commit. This is lazy binding.

Look at `Flag`'s constructor. It does exactly one thing, `DETACH_FROM_SEQUENCE` (`weak_ptr.cc:15-20`):

```cpp
WeakReference::Flag::Flag() {
    // Flags only become bound when checked for validity, or invalidated,
    // so that we can check that later validity/invalidation operations
    // on the same Flag take place on the same sequenced thread.
    DETACH_FROM_SEQUENCE(sequence_checker_);
}
```

`DETACH_FROM_SEQUENCE` means "I am not bound to any sequence yet, do not check". At this point Flag is unbound. The first time someone calls `IsValid` or `Invalidate` and touches it, `DCHECK_CALLED_ON_VALID_SEQUENCE` records the current sequence, and from then on Flag is loyal to it. Every later `IsValid`/`Invalidate` must run on that sequence:

```cpp
bool WeakReference::Flag::IsValid() const {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);   // first touch → bind; after → verify
    return !invalidated_.IsSet();
}
```

Why does it have to be lazy? Because in Chromium a lot of objects are "constructed on sequence A, used on sequence B", and at construction you do not yet know which sequence they will end up running on. If Flag pinned itself to the constructing sequence the moment it was built, none of those objects could use it. Lazy binding opens the path of "no opinion at construction, commit to a sequence the first time you actually use it", and the barrier to use drops right down.

There is a finer seam hiding here. If a Flag currently has no WeakPtr holding it (`!HasRefs()`), `WeakReferenceOwner::GetRef` detaches it again (`weak_ptr.cc:91-101`):

```cpp
WeakReference WeakReferenceOwner::GetRef() const {
#if DCHECK_IS_ON()
    DCHECK(flag_);
    if (!HasRefs()) {
        flag_->DetachFromSequence();   // no holders → unbind, can rebind to another sequence
    }
#endif
    return WeakReference(flag_);
}
```

This block only takes effect under `DCHECK_IS_ON()`, but it opens a door: after all of a factory's WeakPtrs are gone, the factory can pick up on a different sequence. The next WeakPtr it mints will rebind to the new sequence on first touch. The source comment calls this out directly (`weak_ptr.h:63-65`): once all WeakPtrs are destroyed or invalidated, the factory is unbound from its sequence, may be destroyed on a different sequence, and may mint fresh WeakPtrs there.

---

## Release: zero overhead, and self-discipline

Line this up against the key fact from [prerequisite (III)](./pre-03-weak-ptr-sequence-checker-dcheck-check.md): all of the lazy binding and sequence checking is wrapped in `DCHECK_IS_ON()`, and in a release build it compiles away to nothing.

Put differently, a release WeakPtr does zero runtime sequence checking. The sequence contract in release rests entirely on developer discipline. Violate it and your program will not crash on the spot. It will surface a race at some inconvenient moment. That is why taking every DCHECK seriously in a debug build matters so much. It is close to your only chance to catch a sequence violation.

---

## IsValid vs MaybeValid: same-sequence truth vs cross-sequence hint

Lazy binding piles all of the "sequence contract checking" onto `IsValid`, yet WeakPtr throws in a second query, `MaybeValid`. The two look alike. Their semantics are far apart. Mix them up and you step in it.

Start with `IsValid`. It runs a sequence assertion. You must call it on the bound sequence, and its return value is fully trustworthy: true means really still valid, false means really invalidated. `WeakPtr::get()` and `operator bool` go through this path, which is why "check liveness, then deref" is safe. There is a side effect too: this `IsValid` call is what triggers lazy binding (if it has not been bound yet).

```cpp
bool WeakReference::Flag::IsValid() const {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);   // same-sequence contract
    return !invalidated_.IsSet();
}
```

`MaybeValid` is different. It carries no sequence assertion, any sequence can call it. The Chromium source comment states its boundary plainly (`weak_ptr.h:266-283`):

> Returns false if the WeakReference is confirmed to be invalid. This call is safe to make from any thread, e.g. to optimize away unnecessary work, but `RefIsValid()` must always be called, on the correct sequence, before actually using the pointer.

Memorize the asymmetry of its return value. A false return is trustworthy: an acquire read caught the invalidation bit a release wrote, the pointer is invalidated, full stop. A true return you cannot fully trust. It only counts as "maybe" still valid. You might read true, and right before you deref, the bound sequence invalidates it. So `MaybeValid` has exactly one fitting use: a speculative "can I skip this" check from another sequence. For example, a message loop can glance at it before dispatching a task, see false, and know the task is pointless, skip it, save a cross-sequence post. But the moment you want to actually touch the pointer, you must go back to the bound sequence and run `IsValid` again. Treat a positive result as a deref pass and you will crash sooner or later.

```cpp
bool WeakReference::Flag::MaybeValid() const {
    return !invalidated_.IsSet();   // no sequence assertion, any sequence may call
}
```

A comparison table for reference:

| Query | Sequence constraint | Triggers lazy binding | Result trust |
|---|---|---|---|
| `IsValid()` | must be the bound sequence | yes | 100% accurate |
| `MaybeValid()` | any sequence | no | negative trusted / positive not trusted |

This distinction pays off in the next piece (02-5, BindOnce integration). You will see Chromium's callback cancellation goes through `IsValid` (same-sequence, trustworthy), while `MaybeValid` is a separate "scheduler speculative query" channel.

---

## Adding the sequence check to the teaching version

Now we add the sequence check to the `Flag` from 02-2. The teaching version uses a simplified `SequenceChecker` that records a thread id in debug:

```cpp
// Platform: host | C++ Standard: C++17  (debug-only check)
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

You can map it onto Chromium's three macros: `detach_from_sequence` lines up with `DETACH_FROM_SEQUENCE`, `called_on_valid_sequence` with `DCHECK_CALLED_ON_VALID_SEQUENCE`, and in release they are all no-ops. The teaching version models a sequence with a thread id; real Chromium uses the finer `SequenceToken`, but the shape of lazy binding is the same.

Now `Flag` plugs it in:

```cpp
class Flag : public RefCountedThreadSafe {
public:
    Flag() { seq_.detach_from_sequence(); }                 // construct: unbound

    void Invalidate() noexcept {
        // DCHECK: same sequence, or only this ref left (cross-thread destruct ok)
        assert(seq_.called_on_valid_sequence() || has_one_ref());
        invalidated_.Set();
    }
    bool IsValid() const noexcept {
        assert(seq_.called_on_valid_sequence());            // first touch → bind
        return !invalidated_.IsSet();
    }
    bool MaybeValid() const noexcept {
        return !invalidated_.IsSet();                       // never touches seq_, never binds
    }
private:
    // ...
    mutable SequenceChecker seq_;
    AtomicFlag invalidated_;
};
```

Wire it up this way and in debug, if you call `IsValid` on the wrong thread, the assert catches the sequence violation. In release, these asserts along with `SequenceChecker` all disappear, zero overhead.

---

This piece took the sequence contract apart head-on. A weak pointer can travel across sequences, but deref and invalidation must land on the same bound sequence, otherwise the check itself is a race. Flag's lazy binding (detach at construction, bind on first touch, unbind when no refs remain so it can be reused) is what makes "no sequence specified at construction" workable, and it fits the many Chromium objects built as "constructed in one place, used in another". All of this checking lives inside `DCHECK_IS_ON()`, zero overhead in release, the contract carried by self-discipline. And `IsValid` (same sequence, trustworthy, triggers binding) versus `MaybeValid` (any sequence, optimistic, negative trusted and positive not) is a pair you must keep straight. The first is the hard gate before a deref. The second is only a scheduler's skip hint.

The real "eye" of the series is next. We plug WeakPtr into the callback system and watch `BindOnce` lean on `IsValid` so a callback turns into a no-op once its object dies, closing the tail the hand-rolled cancellation token from 01-4 left open.

## References

- [Chromium `base/memory/weak_ptr.h` — top-of-file thread-safety comment (lines 50-69)](https://source.chromium.org/chromium/chromium/src/+/main:base/memory/weak_ptr.h)
- [Chromium `base/memory/weak_ptr.cc` — Flag / WeakReferenceOwner](https://source.chromium.org/chromium/chromium/src/+/main:base/memory/weak_ptr.cc)
- [WeakPtr prerequisite (III): sequences, SEQUENCE_CHECKER, and DCHECK/CHECK](./pre-03-weak-ptr-sequence-checker-dcheck-check.md)
- [Chromium `base/sequence_checker.h`](https://source.chromium.org/chromium/chromium/src/+/main:base/sequence_checker.h)
