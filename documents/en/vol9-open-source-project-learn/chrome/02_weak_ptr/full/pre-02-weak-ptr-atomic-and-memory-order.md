---
chapter: 0
cpp_standard:
- 11
- 17
- 20
description: "Starting from a data race, walks through the six memory_order values
  of std::atomic, zeroes in on the acquire/release pairing, and lands on the release-Set
  and acquire-IsSet pair inside WeakPtr/AtomicFlag."
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 'WeakPtr prerequisite (I): intrusive reference counting and scoped_refptr'
reading_time_minutes: 13
related:
- 'WeakPtr hands-on (II): the core skeleton and control block'
- 'WeakPtr prerequisite (III): sequences, SEQUENCE_CHECKER, and DCHECK/CHECK'
tags:
- host
- cpp-modern
- intermediate
- atomic
- memory_order
- 并发
- weak_ptr
title: "WeakPtr prerequisite (II): std::atomic and memory_order"
translation:
  source: documents/vol9-open-source-project-learn/chrome/02_weak_ptr/full/pre-02-weak-ptr-atomic-and-memory-order.md
  source_hash: de265b5bfe21f39dd3549f481eac3163538830893ba97d50345bd92e28d16aa8
  translated_at: '2026-09-26T01:23:38+00:00'
  engine: anthropic
  token_count: 5100
---
# WeakPtr prerequisite (II): std::atomic and memory_order

In the last piece we hand-rolled that minimal refcounting base class, stuffing `memory_order_relaxed` into `add_ref` and `memory_order_acq_rel` into `release`. At the time we waved it off in one sentence and left a loose thread: why not use one uniform order for everything? What are these `memory_order_*` things actually in charge of?

That thread has to be picked up. The most elegant part of the whole WeakPtr mechanism is that it takes no locks: leaning on a single `release`/`acquire` pair of atomic operations, it achieves "one sequence destructs the object, and a WeakPtr held on another sequence can never deref the destroyed object". To digest that guarantee you have to chew through memory order first — it is the foundation the entire deref chain later stands on. This piece takes the topic apart, walking from data races all the way to that one pairing inside WeakPtr.

---

## Why atomic operations exist: the data race

Start with the smallest scenario that actually breaks: two threads poking an ordinary `int` at the same time:

```cpp
int counter = 0;
// Thread A
counter++;
// Thread B (at the same time)
counter++;
```

`counter++` looks like one statement, but at runtime it is three steps: read `counter`, add 1, write it back. Once the two threads' three steps interleave — A reads 0, B also reads 0, A writes 1, B writes 1 — the final result is 1, not 2. That is a data race. Under the C++ standard, the moment you hit a data race the program's behavior is outright undefined. It is not just a wrong result: the compiler is entitled to assume "this program has no data races" and improvise freely on that assumption, so what you end up with may be a binary you never saw coming.

`std::atomic` governs two things. One is atomicity: it squeezes those three steps into one indivisible step that nobody can slip into. The other is visibility and ordering: under what conditions one thread's writes can be seen by another thread, and whether the compiler and the CPU may reorder the reads and writes. Most people have a working intuition for the atomicity half; the half that is genuinely hard — and that genuinely decides whether your concurrency is correct — is the second one, namely `memory_order`.

---

## std::atomic basics

`std::atomic<T>` provides atomic `load`/`store`/`exchange`/`compare_exchange` (CAS), plus read-modify-write operations such as `fetch_add`/`fetch_sub`:

```cpp
std::atomic<int> a{0};
a.store(1, std::memory_order_release);   // atomic write
int v = a.load(std::memory_order_acquire);   // atomic read
a.fetch_add(1, std::memory_order_relaxed);   // atomic +1
```

Every operation can take a `memory_order` parameter; if you leave it off, the default is `std::memory_order_seq_cst`, the strongest and the most expensive. C++ offers six in total, laid out from weakest to strongest:

---

## The six memory orders

| memory_order | Applicable operations | Semantics |
|---|---|---|
| `relaxed` | load/store/RMW | Atomicity only; establishes no synchronization, restricts no reordering |
| `consume` | load | Data-dependency flavored acquire (in practice close to acquire; de-emphasized by the standard, don't use it) |
| `acquire` | load | Stops later reads/writes from being reordered before this load; pairs with a release to establish synchronization |
| `release` | store | Stops earlier reads/writes from being reordered after this store; pairs with an acquire to establish synchronization |
| `acq_rel` | RMW (fetch_*) | Both acquire and release at once |
| `seq_cst` | all | Additionally guarantees a single global total order; strongest, the default, most expensive |

One piece of advice before we go on: don't try to carve all six into your brain in one pass. Real engineering churns through only three combinations: `relaxed` (just counting, no information passed), `acquire`+`release` (synchronization for single-writer/multi-reader), and `seq_cst` (brought in only when a globally consistent order is needed). Below we take `acquire`/`release` apart in detail, because that is exactly the pair WeakPtr uses.

---

## relaxed: atomicity only, no information passed

`relaxed` is the least of your worries: it only makes this one operation atomic. It puts no constraints at all on compiler or CPU reordering, and it gives no guarantee that other reads and writes around this operation become visible to other threads.

In our experience, the most natural home for `relaxed` is a pure counter — you only care that the number adds up right, not about where it sits relative to other memory operations:

```cpp
std::atomic<int> hits{0};
// Multiple threads all execute:
hits.fetch_add(1, std::memory_order_relaxed);   // only wants to count the total right
```

The trap hides here too. `relaxed` cannot carry a signal like "the data is ready"; if you cut corners and write this:

```cpp
// Thread A
data = 42;                                   // ordinary write
ready.store(true, std::memory_order_relaxed);

// Thread B
while (!ready.load(std::memory_order_relaxed));
assert(data == 42);   // Not guaranteed to pass! May read a stale value of data
```

`relaxed` neither blocks the "write `ready` before `data`" reordering, nor guarantees that when B reads `ready==true`, `data==42` is visible to it. Whether the assert fires is pure luck. To carry that kind of signal, you have to upgrade to `release`/`acquire`.

---

## acquire / release: establishing happens-before

This is the pair you use most in day-to-day work, and it is the vital gate of concurrent correctness. There are just two rules — let us recite them.

When a thread performs a `release` store, none of the reads and writes it did before this store may be reordered after it; when a thread performs an `acquire` load, none of the reads and writes it does after this load may be reordered before it.

Pair the two ends up and it gets delightful: thread A finishes a pile of writes and stores a flag with `release`; thread B reads that flag with `acquire`. The moment B's acquire reads the value written by A's release, every write A did before the release becomes visible to B. That edge is called happens-before.

```cpp
// Platform: host | C++ Standard: C++17
#include <atomic>
#include <thread>
#include <cassert>

int data = 0;
std::atomic<bool> ready{false};

void producer() {
    data = 42;                                   // (1) ordinary write
    ready.store(true, std::memory_order_release); // (2) release: "publishes" (1)
}

void consumer() {
    while (!ready.load(std::memory_order_acquire)) { // (3) acquire: waits for the release
        // spin
    }
    assert(data == 42);                          // (4) guaranteed to pass!
}

int main() {
    std::thread t1(producer);
    std::thread t2(consumer);
    t1.join(); t2.join();
    return 0;
}
```

Why is the `assert` guaranteed to pass? Because the acquire at (3) read the value written by the release at (2), which makes (1) — which sits before (2) — visible to (4), which sits after (3). That is all the release/acquire pairing does: it hands the "the object has been written" signal, together with the data that was written, to the other thread atomically and in order.

### Back to the acq_rel in release()

In [prerequisite (I)](./pre-01-weak-ptr-intrusive-refcount-and-scoped-refptr.md), the refcount's `release` used `memory_order_acq_rel` rather than a plain `release`. The reason is simple: `fetch_sub` is a read-modify-write — it reads the old value and writes the new one. `acq_rel` lets it cover both ends: the acquire half gets to see other threads' latest writes to that counter, and the release half publishes this thread's writes to the object to whichever thread takes over `delete` next. When the refcount hits zero, that handoff is exactly enough.

---

## Back to WeakPtr: the release/acquire pairing in AtomicFlag

With the tools in hand, we can finally zoom in precisely on WeakPtr's liveness mechanism. Chromium's `base::AtomicFlag` is a semantically narrowed-down wrapper over `std::atomic<uint_fast8_t>`, and WeakPtr uses it with remarkable restraint: `Set()` with release, `IsSet()` with acquire — that is the whole pair:

```cpp
// Simplified equivalent of base::AtomicFlag (our teaching version uses std::atomic directly)
class AtomicFlag {
public:
    void Set() {
        flag_.store(1, std::memory_order_release);
    }
    bool IsSet() const {
        return flag_.load(std::memory_order_acquire) != 0;
    }
private:
    std::atomic<uint_fast8_t> flag_{0};
};
```

Mapped onto WeakPtr's two actions: the invalidation chain is `WeakReferenceOwner::Invalidate()` calling all the way down to `Flag::Invalidate()`, finally landing on `invalidated_.Set()` — one release-store. It fires at the moment the factory puts the object into an unusable state, and there are two typical scenarios: either the very start of `WeakPtrFactory`'s destruction (the "last member" idiom — see [02-3](./02-3-weak-ptr-factory-and-last-member.md)), or an explicit call by you to `InvalidateWeakPtrs()`. One ordering detail deserves a special callout: the factory's destructor first calls Invalidate to invalidate every WeakPtr, and only then do the other members get their turn to be destroyed. That is precisely how the "last member" guards the member-destruction phase — not the other way around, "destroy everything first, then Invalidate".

The liveness-check chain runs the other way: `WeakPtr::get()` calls `WeakReference::IsValid()`, which calls `Flag::IsValid()` — essentially `!invalidated_.IsSet()`, one acquire-load.

Drop these two ends into the producer/consumer model from the previous section and you will find they are the same pattern pair. Thread A is the sequence the factory lives on: before destructing the object it calls `Invalidate` — a release-store — publishing the "the object is unusable" state along with all earlier writes. Thread B holds the WeakPtr and, before dereferencing, calls `IsValid` — an acquire-load. If B reads invalidated, then A's release has already taken effect: every write A's sequence did before the release, including the operations that put the object into the unusable state, is visible to B, so B knows full well it must not deref, and `get()` dutifully returns `nullptr`. Conversely, if B reads not-yet-invalidated, A has not released yet, the object is still breathing at this instant, and B's deref is safe.

The entire secret of WeakPtr's "safe to deref without locks" lives in this release/acquire pair. It blocks nobody and waits for nobody; it simply erects the happens-before edge "saw the invalidation flag ⇒ saw every state the object went through", and leaves the rest to the atomic read itself.

### Why not relaxed, and why not seq_cst

`relaxed` needs no deliberation at all — the `data`/`ready` counterexample earlier has already convicted it. Atomicity without synchronization means the acquire side may read `ready==true` while failing to see the object's pre-destruction state, and WeakPtr outright misses the invalidation.

`seq_cst` does work, but we find the price not worth paying. It requires all `seq_cst` operations to line up into one globally consistent total order, which on x86 needs stronger instructions to hold it up: ordinary acquire/release on x86 is nearly just a plain load/store, while a `seq_cst` store has to carry an `MFENCE` or a `LOCK` prefix. WeakPtr's deref is a hot path — every `get()` reads the flag once — so switching to `seq_cst` would amplify the overhead by a notch for no good reason. acquire/release is the exactly-enough choice here: the synchronization gets established, and you do not foot the bill for `seq_cst`'s global total order. This is the root of why Chromium's source comments specifically stress that "IsSet must be inlined and has a measurable performance impact on WeakPtr".

### Why MaybeValid is also an acquire

In 02-4 we will run into `MaybeValid()`, which offers cross-sequence callers an "optimistic liveness check". You see the maybe in the name and your first reaction might be "just fudge it with `relaxed`, then". No dice: internally it is `!invalidated_.IsSet()`, and it still has to be an acquire read. The reasoning is identical to the above — once it reads "invalidated", the caller has to be able to pound their chest and say "every write by which the other sequence put the object into the invalidated state, I have seen them all". The "maybe" in `MaybeValid` is not about memory order; it is about not checking sequence affinity. You can call it from any sequence, but it only guarantees that the negative result is trustworthy — reading invalidated means truly invalidated — while a positive result (reading not-yet-invalidated) carries no promise that your subsequent operations are still safe. We will unpack that distinction in 02-4.

---

That takes memory order apart thoroughly. One last piece of the concurrency foundation remains to be laid: sequences and `SEQUENCE_CHECKER`, plus the dividing line between `DCHECK` and `CHECK`. Once all three foundation stones are in place, we can roll up our sleeves in 02-2 and build Flag, WeakReference, and WeakPtr up level by level.

## References

- [cppreference: std::atomic](https://en.cppreference.com/w/cpp/atomic/atomic)
- [cppreference: std::memory_order](https://en.cppreference.com/w/cpp/atomic/memory_order)
- [Herb Sutter — `atomic<>` Weapons`](https://channel9.msdn.com/Shows/Going+Deep/Cpp-and-Beyond-2012-Herb-Sutter-atomic-Weapons-1-of-2)
- [Chromium `base/synchronization/atomic_flag.h`](https://source.chromium.org/chromium/chromium/src/+/main:base/synchronization/atomic_flag.h)
