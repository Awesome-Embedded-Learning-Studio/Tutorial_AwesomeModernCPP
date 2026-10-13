---
title: 'Lab 1: Bounded Queue, Concurrent Cache and Sync Primitives'
description: 'Build a fixed-capacity blocking queue with close semantics and timeouts, plus a sharded-lock concurrent cache; train with mutex, condition_variable, and C++20 synchronization primitives'
chapter: 10
order: 1
tags:
  - host
  - cpp-modern
  - mutex
  - intermediate
difficulty: intermediate
platform: host
reading_time_minutes: 20
cpp_standard: [17, 20]
prerequisites:
  - Mutexes, Condition Variables, and Synchronization Primitives
  - 'Lab 0: Thread Lifecycle'
related:
  - 'mutex and RAII Locks'
  - 'condition_variable and Wait Semantics'
  - 'latch, barrier, and semaphore'
translation:
  source: documents/vol5-concurrency/exercises/01-bounded-queue.md
  source_hash: 2d5b3222401ff2c5b5d79f833d783a8940074148bc9588a893a31dc250cf4617
  translated_at: '2026-09-26T09:22:45+00:00'
  engine: anthropic
  token_count: 8200
---

# Lab 1: Bounded Queue, Concurrent Cache and Sync Primitives

> The runnable companion project for this lab lives at `code/volumn_codes/vol5-labs/templates/lab1_bounded_queue/`. Expect roughly **8–12 hours** of hands-on work (`reading_time_minutes` counts pure reading minutes, not hands-on time).

## Goal

Lab 0 got the multithreaded skeleton running — creating threads, RAII wrappers, passing arguments safely. But those threads all worked in isolation, and the main thread just waited for them to finish. Real concurrent systems don't look like that: threads have to cooperate — producers push data into a queue, consumers take it out, a full queue must apply backpressure, and a closed queue must let everyone exit gracefully.

The core deliverables of this lab are three things:

1. **`BoundedBlockingQueue<T>`** — a fixed-capacity blocking queue with close semantics (built up through MS1-4). **Lab 3's ThreadPool reuses it directly as its task queue**, so the interface has to be settled once, and settled right.
2. **`ConcurrentCache<K, V>`** — a sharded-lock concurrent cache (MS5), for practicing the "coarse-grained lock vs fine-grained lock" trade-off.
3. **C++20 synchronization primitives in practice** (MS6) — implementing three classic concurrency patterns with `std::latch` / `barrier` / `counting_semaphore`.

When you're done, the mutex + condition_variable one-two punch should be muscle memory: you'll handle all four waiting scenarios — **predicate waits, spurious wakeups, lost wakeups, and close-triggered wakeups** — correctly, and you'll understand the performance trade-offs of lock granularity.

## Prerequisites

- **ch02-01** mutex and RAII Locks — `std::mutex`, `lock_guard`, `unique_lock`
- **ch02-03** condition_variable — predicate waits, spurious wakeups, `notify_one` vs `notify_all`
- **ch02-05** latch / barrier / semaphore — C++20 synchronization primitives
- **Lab 0** — `JoiningThread` (this lab's tests and examples rely on it)

## Project Scaffold (Get This Running First)

Every lab ships in two copies under vol5-labs/: **`templates/lab1_bounded_queue/`** is an empty implementation skeleton (copy it and fill it in), while **`examples/lab1_bounded_queue/`** is a reference implementation (⚠️ under development — it currently contains only empty declarations, so no complete reference is available yet; once it's done you can consult it when stuck, but don't copy from it up front). Both are standalone projects. The one you work on is the templates copy:

```text
templates/lab1_bounded_queue/
├── CMakeLists.txt       # standalone: FetchContent Catch2 + INTERFACE library + test
├── include/lab1/        ← you fill in the implementation here
│   ├── bounded_blocking_queue.h   #   MS1-4
│   ├── concurrent_cache.h         #   MS5
│   └── sync_practice.h            #   MS6
└── test/                # tests provided by the tutorial (no changes needed)
    └── test_milestone1.cpp … test_milestone6.cpp
```

**Note that lab1 is C++20** (unlike lab0's C++17), because MS6 needs `std::latch/barrier/counting_semaphore`.

```bash
cd code/volumn_codes/vol5-labs/templates/lab1_bounded_queue
cmake -B build -DCMAKE_BUILD_TYPE=Debug   # Debug enables ThreadSanitizer by default
cmake --build build
```

**Expected result: the build stops at the link stage with `undefined reference to lab1::BoundedBlockingQueue<...>::push(...)`** — that's intentional; `include/lab1/*.h` contains declarations but no implementations. Fill them in milestone by milestone, and the corresponding tests turn from red to green.

## The Final Interface

Before you start, get a clear picture of the target shape (it matches the headers in `include/lab1/` exactly).

### `BoundedBlockingQueue<T>` — the MS1-4 evolution (the interface stays fixed, the internals fill in step by step)

| Method | Signature | Milestone |
|------|------|-----------|
| Constructor | `explicit BoundedBlockingQueue(std::size_t capacity)` | MS1 |
| Blocking push | `void push(T value)` — blocks when full; throws `std::runtime_error` after close | MS1/MS2 |
| Blocking pop | `std::optional<T> pop()` — blocks when empty; returns `nullopt` when closed and empty | MS1/MS2 |
| Close | `void close()` — wakes all blocked threads | MS2 |
| Closed check | `bool is_closed() const noexcept` | MS2 |
| Timed push | `bool try_push_for(T, std::chrono::nanoseconds)` — true on success; false on timeout or close | MS3 |
| Timed pop | `std::optional<T> try_pop_for(std::chrono::nanoseconds)` | MS3 |
| Approximate size | `std::size_t size() const noexcept` | MS4 |

### `ConcurrentCache<K, V, Hash>` — MS5 (sharded locking)

| Method | Signature |
|------|------|
| Constructor | `explicit ConcurrentCache(std::size_t shard_count = 16)` |
| Lookup | `std::optional<V> get(const K&) const` |
| Write | `void put(K key, V value)` |
| Erase | `bool erase(const K&)` |
| Size | `std::size_t size() const noexcept` |

### `sync_practice` — MS6 (three free functions, one C++20 primitive each)

| Function | Primitive used | Why this one |
|------|----------|-----------|
| `fork_join_sum(n, task)` | `std::latch` | one-shot "wait for all N tasks to finish" (countdown to 0) |
| `two_phase_sum(n, val)` | `std::barrier` | multi-phase, synchronizing between phases (reusable) |
| `measure_max_concurrency(n, max)` | `std::counting_semaphore` | a counting semaphore for "allow N in concurrently" |

Now let's break it down milestone by milestone.

## Milestone 1: Blocking push / pop

### Goal

Implement `push` (blocks waiting for room when the queue is full) and `pop` (blocks waiting for data when the queue is empty). First get "multiple threads passing data through a queue" working; close and timeouts come later.

### Why This Step First

This is the most basic form of the mutex + condition_variable combo. Every later milestone (close wakeups, timed waits) adds branches onto this structure, so the skeleton you build here has to be right.

### Implementation Guide

The core is a fixed-capacity ring buffer (or just a `std::queue<T>`) + one `mutex` + two `condition_variable`s (`not_full_` for producers, `not_empty_` for consumers). Two design points:

**First, waits must use a predicate — never a bare `wait()`.** The producer needs to "wait until there's room":

```cpp
std::unique_lock lock(m_);
not_full_.wait(lock, [this] { return queue_.size() < capacity_; });  // ← the predicate
queue_.push(std::move(value));
not_empty_.notify_one();
```

That lambda predicate is your lifeline. If you write `not_full_.wait(lock)` (no predicate), you'll get bitten by **spurious wakeups** and **lost wakeups**: the operating system is allowed to return from `wait` for no reason at all (spurious wakeup), or a notify may fire before you even enter the wait (lost wakeup) — in both cases you'd proceed when there's actually no room and blow past the capacity. A predicate wait **re-checks the condition** after returning, sealing both traps shut.

**Second, `notify_one` or `notify_all`?** For MS1's single-producer single-consumer scenario, `notify_one` is enough (it wakes just one waiter). But once you reach MS2's close, you must switch to `notify_all` (every blocked consumer has to be woken so they can exit). Use `notify_one` for now; we'll change it in MS2.

> **Pitfall warning**: never `notify` while holding the lock — not because it's incorrect, but because "unlock first, then notify" avoids pointless context switches where the woken thread immediately fails to grab the lock and goes back to sleep. That said, the standard form of a predicate wait (`wait` returns while still holding the lock, and the lock is released when it destructs at function exit) already implies the right order — don't gild the lily by manually unlocking, notifying, and re-locking.

### Verification

> **Don't be fooled by the tests**: `test_milestone1` checks "you can push and pop, FIFO order, blocking behavior, multiple producers with no loss or duplication" — **all behavior, never whether you used a predicate wait**. You could perfectly well pass the tests with a bare `wait()` (no predicate), if no spurious wakeup happens to fire. But that's a time bomb: under high concurrency or particular schedulings, it will go off. **The real acceptance criterion: every wait in `push`/`pop` is a predicate wait (`cv.wait(lock, predicate)`); there is no bare `wait()`.** Run MS4's stress test under TSan, and a bare wait will sooner or later surface as a data race or an out-of-bounds access.

`test/test_milestone1.cpp` covers four scenarios: single push/pop, FIFO order, pop blocking until a push arrives, and multiple producers with no loss or duplication under concurrency.

## Milestone 2: close Semantics

### Goal

Implement `close()`: wake every push/pop currently blocked; after close, push throws, and pop returns `nullopt` once the remaining elements are drained.

### Why

MS1's queue has no way to end — a consumer's `while (auto v = q.pop())` would wait forever. A real producer-consumer setup needs a "production complete" signal that consumers use to exit. `close()` is that signal: it makes `pop` return `nullopt` once the queue is drained, and the consumer loop ends naturally.

### Implementation Guide

Inside `close`: take the lock, set `closed_ = true`, and **`notify_all()` on both cvs** (wake every blocked producer and consumer). Then the push and pop predicates both need the `closed_` condition added:

```cpp
// push: waits for "room available", but must also fail immediately (throw) on close
not_full_.wait(lock, [this] { return queue_.size() < capacity_ || closed_; });
if (closed_) throw std::runtime_error("push on closed queue");
// ...

// pop: waits for "data available", but once closed and the queue is empty, returns nullopt right away
not_empty_.wait(lock, [this] { return !queue_.empty() || closed_; });
if (queue_.empty()) return std::nullopt;   // at this point it must be closed_ && empty
// ...
```

**The key point: `close` must `notify_all`** (not `notify_one`). Multiple consumers may be blocked in `pop`, and you have to wake them all — `notify_one` wakes just one, and the rest stay stuck forever (deadlock).

> **Pitfall warning**: after close, pop's semantics are "drain the remaining elements → nullopt", not "nullopt immediately". Elements pushed before the close must still be retrievable by consumers. That's why the pop predicate is `!queue_.empty() || closed_` — serving data comes first; only when the queue is empty does closed come into play.

### Verification

> **Don't be fooled by the tests**: `test_milestone2` checks that push throws after close, pop returns nullopt after draining, and close wakes consumers so they exit. But it **does not verify that close really wakes every blocked thread** — if you slip and write `notify_one`, the test has only one consumer and still passes. **The real acceptance criterion: `close()` calls `notify_all()` on both cvs.** The multi-consumer scenario in MS4's stress test will expose notify_one's deadlock.

## Milestone 3: Timed try_push_for / try_pop_for

### Goal

Add timed versions of push/pop: if the wait doesn't pay off, give up and report failure (push returns false, pop returns nullopt) — no throwing, no waiting forever.

### Why

The blocking versions can wait forever — that's a breeding ground for deadlock (say, a producer and a consumer each waiting on the other). The timed versions provide a "give up and move on" escape hatch, used in real systems for liveness probing, graceful degradation, and avoiding indefinite blocking.

### Implementation Guide

Use `condition_variable::wait_for(lock, timeout, predicate)`. As in MS1, **the predicate is mandatory** — `wait_for` also has spurious wakeups, and without a predicate you'd return early before the timeout expires. The return value tells you whether it returned because the predicate was satisfied (true) or because it timed out (false):

```cpp
bool try_push_for(T value, std::chrono::nanoseconds timeout) {
    std::unique_lock lock(m_);
    bool ok = not_full_.wait_for(lock, timeout,
        [this] { return queue_.size() < capacity_ || closed_; });
    if (!ok || closed_) return false;   // timed out, or already closed
    queue_.push(std::move(value));
    not_empty_.notify_one();
    return true;
}
```

Note that `wait_for` returning false does not mean closed — it only means timed out. That's why you check `closed_` separately afterwards.

### Verification

> **Don't be fooled by the tests**: `test_milestone3` checks timeout return values and timing, **not whether your wait_for carries a predicate**. A bare `wait_for(lock, timeout)` (no predicate) hit by a spurious wakeup returns early and quits before the deadline — but the test's timing assertions have a 10ms tolerance, so it might sneak through. **The real acceptance criterion: `wait_for` takes a predicate, and afterwards you double-check with the return value + `closed_`.**

## Milestone 4: Backpressure and Concurrent Stress

### Goal

Run real MPMC (multiple producers, multiple consumers) stress against a small-capacity queue, verifying that the capacity limit holds, nothing is lost or duplicated, and TSan stays clean.

### Why

The first three milestones are about being "correct at a single point"; MS4 is about being "correct as a system" — when many producers and consumers run truly concurrently, do your locks, cvs, and predicates hold up under pressure? This is the hard threshold BoundedBlockingQueue must clear to be usable.

### Implementation Guide

You should barely need any new code — a correct MS1-3 implementation is enough. The point of this step is **understanding the capacity limit**: `capacity_` is a hard ceiling, and producers **must block** when the queue is full (backpressure) rather than growing it without bound. If your `push` doesn't block and instead expands dynamically, it's no longer a "bounded" queue — the tests' `size()` assertions and MS4's backpressure semantics both break.

Before running the tests, think through the multi-consumer shutdown sequence: all producers finish pushing → the main thread calls `close()` → each consumer's `while (auto v = q.pop())` drains the remainder, receives `nullopt`, and exits. This chain relies on MS2's `notify_all` + nullopt semantics — something MS1's single-consumer tests can't exercise.

### Verification

> **Don't be fooled by the tests**: `test_milestone4`'s MPMC stress checks "no loss or duplication + size tracking". If you secretly make the queue unbounded (push never blocks), no-loss-no-duplication still holds and the tests still pass — but you've lost backpressure, and Lab 3's ThreadPool will OOM when it uses your queue. **The real acceptance criterion: the queue size is always ≤ capacity (`push` really blocks when full), and multi-consumer shutdown happens via MS2's close.** Under TSan this stress test must report zero races.

## Milestone 5: ConcurrentCache (Sharded Locking)

### Goal

Implement a sharded-lock concurrent cache: keys are hashed into one of `shard_count` shards, each shard has its own mutex, and different shards can proceed in parallel.

### Why

This is the classic "coarse-grained lock vs fine-grained lock" trade-off. The naive approach is one lock for the whole cache — every thread serializes, and throughput chokes on lock contention. After sharding, different keys land in different shards, reads and writes genuinely parallelize, and throughput scales linearly with the shard count (until you hit some other bottleneck).

### Implementation Guide

Internally it's a `std::vector<Shard>`, each Shard holding its own `mutex` + `unordered_map`. Locating the shard: `shard_idx = hash(key) % shard_count` (when shard_count is a power of two, you can use the bitwise `& (shard_count - 1)`, which is faster). A `shard_count` of 16 is a good choice (spread out enough, overhead under control).

```cpp
template <typename K, typename V, typename Hash = std::hash<K>>
class ConcurrentCache {
    struct Shard {
        mutable std::mutex m;
        std::unordered_map<K, V> map;
    };
    std::vector<Shard> shards_;
    Hash hash_{};
    // get/put/erase: hash(key) % shards_.size() locates the shard; lock only that one
};
```

> **About `mutable`**: `get` is a `const` method (logically it doesn't change the cache), but it needs to lock — and locking is acquiring a mutex, which changes the mutex's state. Hence the Shard's mutex is `mutable` — modifiable even inside a const method. This is the standard way to write const + concurrency, not laziness.

### Verification

> **Don't be fooled by the tests**: `test_milestone5` checks that concurrent puts lose nothing, gets are correct, and size is right. **But it doesn't check whether you actually sharded** — a single global lock also passes every test (the results are just as correct). The difference is throughput alone: a single lock is far slower under high concurrency. **The real acceptance criterion: internally there are multiple shards, each with its own mutex, and accesses to different keys lock different shards.** Write your own micro-benchmark (single lock vs sharding) and compare throughput to feel the difference — that's the whole point of MS5.

## Milestone 6: C++20 Synchronization Primitives in Practice

### Goal

Implement one classic concurrency pattern each with `std::latch` / `std::barrier` / `std::counting_semaphore` (`fork_join_sum` / `two_phase_sum` / `measure_max_concurrency`).

### Why

ch02-05 covered the concepts of these three primitives, but "knowing they exist" and "knowing which to pick" are worlds apart. The heart of this milestone isn't how much code you write, but **judging "which primitive fits this scenario"** — each of the three functions corresponds to one typical scenario, so as you build them, think carefully about why it's that one.

### Implementation Guide

**`fork_join_sum` (latch)**: dispatch N tasks to threads, with the main thread waiting for all of them to finish. Initialize `std::latch` to N, each task calls `count_down()` when done, and the main thread `wait()`s. Why a latch and not a barrier? Because this is a one-shot "wait for all N to finish" (countdown to 0), whereas a barrier is reusable phase synchronization.

**`two_phase_sum` (barrier)**: several workers each do phase 1 (write their own contribution), synchronize at the barrier (nobody enters phase 2 until all of them finish phase 1), then aggregate. `std::barrier` can take a completion function (run by the last thread to arrive), which suits "aggregating between phases". Why not a latch? Because there may be multiple rounds of phases (a barrier is reusable), and you want to do work at the phase boundary.

**`measure_max_concurrency` (semaphore)**: N threads all want into the critical section, but at most `max_concurrent` may be inside. Initialize `std::counting_semaphore<max>` to max; each thread `acquire()`s to enter and `release()`s to leave, while an atomic records the peak occupancy. Why a semaphore? Because this is the textbook "allow N concurrent" scenario — neither latch nor barrier fits.

> **Pitfall warning**: `counting_semaphore`'s template parameter is the maximum and its constructor argument is the initial value. `std::counting_semaphore<4>` constructed with `(4)` means 4 initial permits, with a ceiling of 4. In `measure_max_concurrency`, observe the peak with an `atomic` compare_exchange, not a plain `int++` (multiple threads writing the same variable is a data race).

### Verification

> **Don't be fooled by the tests**: `test_milestone6` checks the three functions' return values (fork_join_sum's sum, two_phase_sum's product, max_concurrency's ceiling). **But it doesn't check whether you actually used the corresponding primitive** — you could hand-roll the same results with a mutex (for example, simulating a latch with mutex + atomic counter for fork_join). **The real acceptance criterion: the three functions genuinely use `std::latch` / `std::barrier` / `std::counting_semaphore` respectively**, not hand-rolled equivalents. Internalize the feeling of "the standard library hands you the right tool — don't reinvent the wheel".

## Self-Check List

Confirm every item before submitting:

- [ ] MS1 tests pass — push/pop, FIFO, blocking behavior, multiple producers with no loss or duplication
- [ ] MS2 tests pass — push throws after close, pop returns nullopt after draining, close wakes consumers
- [ ] MS3 tests pass — timeout return values and timing are correct
- [ ] MS4 tests pass — MPMC stress with no loss or duplication, size tracks capacity
- [ ] MS5 tests pass — concurrent puts lose nothing, gets are correct, size is right
- [ ] MS6 tests pass — the three synchronization-primitive functions produce correct results
- [ ] **MS1 real acceptance**: every wait in `push`/`pop` is a predicate wait (`cv.wait(lock, predicate)`); there is no bare `wait()`
- [ ] **MS2 real acceptance**: `close()` calls `notify_all()` on both cvs (not `notify_one`)
- [ ] **MS4 real acceptance**: the queue size is always ≤ capacity (backpressure works), and multi-consumer shutdown happens via close
- [ ] **MS5 real acceptance**: internally there are multiple shards, each holding its own mutex (not one global lock)
- [ ] **MS6 real acceptance**: the three functions genuinely use `std::latch` / `std::barrier` / `std::counting_semaphore` respectively
- [ ] **All tests report no data races under TSan** (just run the Debug build)
- [ ] You can explain why a predicate wait guards against both spurious wakeups and lost wakeups
- [ ] You can explain pop's "drain the remainder, then nullopt" semantics after close (rather than nullopt immediately)
- [ ] You can explain sharded locking's throughput advantage over a single lock, and the trade-offs in choosing shard_count

## Extensions (bonus)

- Add `try_push`/`try_pop` to `BoundedBlockingQueue` (non-blocking versions that return success/failure immediately)
- Build a reader-writer-lock version of `ConcurrentCache` with `std::shared_mutex` (better than sharded mutexes when reads heavily outnumber writes), and compare throughput
- Implement a "strictly == max" verification for `measure_max_concurrency` (requires enough callers + a synchronized start)

## Reference Resources

- [`std::condition_variable` — cppreference](https://en.cppreference.com/w/cpp/thread/condition_variable)
- [The predicate overload of `std::condition_variable::wait` — cppreference](https://en.cppreference.com/w/cpp/thread/condition_variable/wait)
- [`std::latch` / `std::barrier` / `std::counting_semaphore — cppreference`](https://en.cppreference.com/w/cpp/thread)
- [ThreadSanitizer — Clang documentation](https://clang.llvm.org/docs/ThreadSanitizer.html)
