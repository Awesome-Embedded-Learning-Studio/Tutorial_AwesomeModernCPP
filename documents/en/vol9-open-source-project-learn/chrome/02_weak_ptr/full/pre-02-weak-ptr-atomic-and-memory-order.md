---
chapter: 0
cpp_standard:
- 11
- 17
- 20
description: "Starts from a data race and works through the six std::memory_order values, focusing on how acquire/release pair up and landing on the release-Set / acquire-IsSet pair inside WeakPtr and AtomicFlag."
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 'WeakPtr prerequisite (I): intrusive refcount and scoped_refptr'
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
---
# WeakPtr prerequisite (II): std::atomic and memory_order

In the last piece we hand-rolled a minimal refcount base class, and `add_ref` carried `memory_order_relaxed` while `release` carried `memory_order_acq_rel`. We waved past it at the time and left a loose thread: why not one uniform order? What are these `memory_order_*` things actually doing?

That thread has to be picked up. The clever part of the whole WeakPtr mechanism is that it takes no lock. It leans on a single `release`/`acquire` pair of atomic operations to guarantee that when one sequence destructs the object, a WeakPtr held on another sequence can never dereference the destroyed object. To absorb that guarantee you need memory order first. It is the foundation the entire deref chain stands on, so this piece walks from data races all the way up to that one pair inside WeakPtr.

---

## Why atomic operations exist: the data race

Start with a smallest scenario that breaks. Two threads poke an ordinary `int` at the same time:

```cpp
int counter = 0;
// Thread A
counter++;
// Thread B (same time)
counter++;
```

`counter++` looks like one statement. At runtime it is three: read `counter`, add 1, write it back. Once the two threads' three steps interleave, you can get A reads 0, B reads 0, A writes 1, B writes 1. The final result is 1, not 2. That is a data race. By the C++ standard, a program with a data race is undefined behavior right away, not just a wrong answer. The compiler is allowed to reason from the assumption that your program has no data race and optimize freely, so the binary you get can be something you never expected.

`std::atomic` covers two things. One is atomicity: it squeezes those three steps into one indivisible step nobody can interrupt. The other is visibility and ordering, which governs under what conditions one thread's write becomes visible to another thread, and whether the compiler or CPU is allowed to reorder reads and writes. Most developers have a feel for the atomicity half. The hard half, the half that decides whether your concurrency is actually correct, is the second one. That is `memory_order`.

---

## std::atomic basics

`std::atomic<T>` gives you atomic `load`/`store`/`exchange`/`compare_exchange` (CAS) plus read-modify-write operations like `fetch_add`/`fetch_sub`:

```cpp
std::atomic<int> a{0};
a.store(1, std::memory_order_release);       // atomic write
int v = a.load(std::memory_order_acquire);   // atomic read
a.fetch_add(1, std::memory_order_relaxed);   // atomic +1
```

Each operation can take a `memory_order` argument. Leave it off and you get `std::memory_order_seq_cst`, the strongest and most expensive. C++ defines six orders, weakest to strongest:

---

## The six memory orders

| memory_order | Applies to | Semantics |
|---|---|---|
| `relaxed` | load/store/RMW | Atomic only; no synchronization, no reorder constraint |
| `consume` | load | Data-dependency acquire (in practice close to acquire; the standard is de-emphasizing it, don't use it) |
| `acquire` | load | Stops later reads/writes from moving before this load; pairs with release to synchronize |
| `release` | store | Stops earlier reads/writes from moving after this store; pairs with acquire to synchronize |
| `acq_rel` | RMW (fetch_*) | Both acquire and release at once |
| `seq_cst` | all | Adds a single global total order; strongest, default, most expensive |

One piece of advice before we go further: don't try to carve all six into your head in one pass. Real engineering bounces between three combinations: `relaxed` (just counting, no information carried), `acquire`+`release` (single-writer multi-reader synchronization), and `seq_cst` (when you need one globally consistent order). The rest of this piece zooms in on `acquire`/`release`, because that is exactly the pair WeakPtr uses.

---

## relaxed: atomic, but carries no information

`relaxed` is the lowest-effort order. It makes the operation atomic and nothing else. It puts no constraint on compiler or CPU reordering, and it gives no guarantee about whether surrounding reads and writes become visible to other threads.

In our experience the natural home for `relaxed` is a pure counter, where you only care that the number adds up and you don't care how it orders against other memory operations:

```cpp
std::atomic<int> hits{0};
// Multiple threads each do:
hits.fetch_add(1, std::memory_order_relaxed);   // just want the total count right
```

The trap hides here too. `relaxed` cannot carry a "the data is ready" signal. If you cut corners and write:

```cpp
// Thread A
data = 42;                                   // plain write
ready.store(true, std::memory_order_relaxed);

// Thread B
while (!ready.load(std::memory_order_relaxed));
assert(data == 42);   // NOT guaranteed! May read a stale value of data
```

`relaxed` neither blocks the "write `ready` first, then `data`" reordering, nor guarantees that when B reads `ready==true`, `data==42` is visible to it. Whether the assert fires is luck. To carry this kind of signal you have to step up to `release`/`acquire`.

---

## acquire / release: building happens-before

This is the pair you reach for daily, and it is the hinge concurrency correctness swings on. Two rules, read them once.

When a thread does a `release` store, every read and write it did before this store is forbidden from being reordered after the store. When a thread does an `acquire` load, every read and write it does after this load is forbidden from being reordered before the load.

Mate the two ends and it clicks. Thread A finishes a pile of writes, then stores a flag with `release`. Thread B reads that flag with `acquire`. The moment B's acquire reads the value A's release wrote, every write A did before the release becomes visible to B. That edge is called happens-before.

```cpp
// Platform: host | C++ Standard: C++17
#include <atomic>
#include <thread>
#include <cassert>

int data = 0;
std::atomic<bool> ready{false};

void producer() {
    data = 42;                                   // (1) plain write
    ready.store(true, std::memory_order_release); // (2) release: "publish" (1)
}

void consumer() {
    while (!ready.load(std::memory_order_acquire)) { // (3) acquire: wait for release
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

Why is the `assert` guaranteed to pass? Because the acquire at (3) read what the release at (2) wrote, so (1), which happened before (2), is visible to (4), which runs after (3). That is all the release/acquire pair does. It hands "the object is fully written" plus the written data to another thread, atomically and in order.

### Back to release()'s acq_rel

The refcount `release` in [prerequisite (I)](./pre-01-weak-ptr-intrusive-refcount-and-scoped-refptr.md) used `memory_order_acq_rel`, not plain `release`. The reason is simple. `fetch_sub` is a read-modify-write: it reads the old value and writes the new one. `acq_rel` makes it do both. The acquire half sees the latest write any other thread made to this counter, and the release half publishes this thread's writes to the object out to whoever takes over the `delete`. When the refcount hits zero, that handoff is exactly enough.

---

## Back to WeakPtr: AtomicFlag's release/acquire pair

Tools in hand, we can finally stare precisely at WeakPtr's liveness mechanism. Chromium's `base::AtomicFlag` is a semantically narrowed wrapper around `std::atomic<uint_fast8_t>`. Inside WeakPtr it is used with restraint: `Set()` is release, `IsSet()` is acquire, and that is the whole pair:

```cpp
// base::AtomicFlag simplified equivalent (our teaching version uses std::atomic directly)
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

Map it onto WeakPtr's two actions. The invalidation chain runs from `WeakReferenceOwner::Invalidate()` down to `Flag::Invalidate()`, landing on `invalidated_.Set()`, one release-store. It fires at the moment the factory moves the object into an unusable state. There are two typical scenes: either at the start of `WeakPtrFactory`'s own destructor (the "last member" idiom, see [02-3](./02-3-weak-ptr-factory-and-last-member.md)), or when you call `InvalidateWeakPtrs()` explicitly. One ordering detail worth flagging: the factory destructor calls Invalidate first to drop all WeakPtrs, and only then do the other members get destructed. That is how the "last member" idiom guards member-destruction time. It is not the other way around, "destruct first, Invalidate after."

The liveness-check chain runs the opposite direction. `WeakPtr::get()` calls into `WeakReference::IsValid()`, then `Flag::IsValid()`, which boils down to `!invalidated_.IsSet()`, one acquire-load.

Drop those two ends into the producer/consumer model from the previous section and you see they are the same pair. Thread A is the factory's sequence. Before destructing the object it calls `Invalidate`, a release-store, publishing "the object is unusable" along with every prior write. Thread B holds a WeakPtr. Before dereferencing it calls `IsValid`, an acquire-load. If B reads invalidated, A's release has taken effect, and every write A's sequence made before that release, including the moves that took the object to unusable, is visible to B. B knows not to deref, and `get()` returns `nullptr`. If B reads not-invalidated, A has not released yet, the object is still breathing at this instant, and B's deref is safe.

The full secret of "safe deref with no lock" is that one release/acquire pair. It blocks nothing and waits for nothing. It only stands up the happens-before edge "see the invalidation bit implies see every state the object was changed through," and leaves the rest to the atomic read itself.

### Why not relaxed, and why not seq_cst

`relaxed` is dead on arrival. The `data`/`ready` counterexample earlier already killed it: atomic without synchronization means the acquire side can read `ready==true` yet miss the state the object was in before it got destructed. WeakPtr would leak the check.

`seq_cst` does work, but the cost is not worth it. It requires every `seq_cst` operation to agree on one globally consistent total order. On x86 that calls for stronger instructions. Plain acquire/release on x86 are essentially ordinary loads and stores, but a `seq_cst` store has to carry an `MFENCE` or a `LOCK` prefix. WeakPtr's deref is a hot path; every `get()` reads the flag once. Swapping in `seq_cst` blows that cost up for no reason. acquire/release is the "exactly enough" choice here: the synchronization stands up, and you skip the bill for `seq_cst`'s global total order. The Chromium source comment emphasizes "IsSet must stay inline, it has measurable perf impact on WeakPtr" for exactly this reason.

### Why MaybeValid is also acquire

In 02-4 we will meet `MaybeValid()`, a "optimistic liveness" query offered to cross-sequence callers. You see the name "maybe" and your first thought might be "fine, slack off with `relaxed`." No. Internally it is still `!invalidated_.IsSet()`, still an acquire read, for exactly the same reason as above. The moment it reads "invalidated," the caller has to be able to swear that every write the other sequence made to push the object into the invalidated state is visible on this side. The "maybe" in MaybeValid is not about memory order. It is about not checking sequence binding. You can call it from any sequence, and it only promises the negative result is trustworthy: reading invalidated means really invalidated. The positive result (reading not-invalidated) does not promise your subsequent operations are still safe. That distinction we save for 02-4.

---

That covers memory order. One last piece of concurrency foundation is still missing: sequences and `SEQUENCE_CHECKER`, plus the line between `DCHECK` and `CHECK`. Once those three foundations are down, in 02-2 we can start stacking Flag, WeakReference, and WeakPtr up layer by layer.

## References

- [cppreference: std::atomic](https://en.cppreference.com/w/cpp/atomic/atomic)
- [cppreference: std::memory_order](https://en.cppreference.com/w/cpp/atomic/memory_order)
- [Herb Sutter — `atomic<>` Weapons](https://channel9.msdn.com/Shows/Going+Deep/Cpp-and-Beyond-2012-Herb-Sutter-atomic-Weapons-1-of-2)
- [Chromium `base/synchronization/atomic_flag.h`](https://source.chromium.org/chromium/chromium/src/+/main:base/synchronization/atomic_flag.h)
