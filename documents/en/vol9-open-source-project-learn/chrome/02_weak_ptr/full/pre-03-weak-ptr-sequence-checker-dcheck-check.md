---
chapter: 0
cpp_standard:
- 11
- 17
- 20
description: "Tell threads and sequences apart, cover the three SEQUENCE_CHECKER macros and why they cost nothing in release, and settle the DCHECK vs CHECK tradeoff — why WeakPtr's operator* uses CHECK"
difficulty: intermediate
order: 3
platform: host
prerequisites:
- 'WeakPtr prerequisite (II): std::atomic and memory_order'
reading_time_minutes: 11
related:
- 'WeakPtr hands-on (IV): sequence affinity and lazy binding'
tags:
- host
- cpp-modern
- intermediate
- atomic
- 并发
- weak_ptr
title: "WeakPtr prerequisite (III): sequences, SEQUENCE_CHECKER, and DCHECK/CHECK"
translation:
  source: documents/vol9-open-source-project-learn/chrome/02_weak_ptr/full/pre-03-weak-ptr-sequence-checker-dcheck-check.md
  source_hash: e73ea921af2f0332ce653455ebd48606760e16f5d453489ca3fd23333461e1a9
  translated_at: '2026-09-26T01:24:08+00:00'
  engine: anthropic
  token_count: 2500
---
# WeakPtr prerequisite (III): sequences, SEQUENCE_CHECKER, and DCHECK/CHECK

[Prerequisite (II)](./pre-02-weak-ptr-atomic-and-memory-order.md) settled the visibility problem: two sequences poking the same flag at once, with acquire/release making sure both sides see it. But WeakPtr carries one more contract, written in the comment at the top of `weak_ptr.h`. We skimmed right past it on the first read and only came back to study it carefully after stepping in a hole — **a weak pointer can be passed across sequences freely, but dereferencing and invalidation must happen on the one sequence it was bound to**.

Atomic operations cannot hold this line. It takes a set of macros called `SEQUENCE_CHECKER`: debug builds catch violations, release builds let them evaporate into zero overhead. In this piece we take that machinery apart, and along the way settle the tradeoff between `DCHECK` and `CHECK` — once those two click, you can read why WeakPtr puts `CHECK` on `operator*` yet only dares to put `DCHECK` on `IsValid`.

## Threads vs sequences: Chromium's concurrency model

First, let's fix a common misunderstanding. When most people hear "thread-safe," the picture in their head is "a bunch of threads pile on at once." Chromium doesn't do the math that way; its concurrency model is built on **sequences**.

What is a sequence? Just think of it as a virtual thread. Tasks inside it run in a queue, and the next one only gets its turn when the previous one finishes. But this virtual thread is not nailed to any one OS thread — it can couch-surf across different physical threads, as long as its tasks never crowd in at the same time.

Why the detour? We wondered too at first — wouldn't plain threads do? It clicked eventually: what the overwhelming majority of objects in a browser actually want is "access me serially; as for which thread serves me, I don't care." Use a real thread, and you have to babysit its TLS, its message pump, its lifecycle. Switch to a sequence, and all that mess is shoved onto the task scheduler; you declare one sentence — "these lumps of code belong to the same sequence" — and the scheduler guarantees they never run concurrently. We covered this line of thinking in [OnceCallback hands-on (I)](../../01_once_callback/full/01-1-once-callback-motivation-and-api-design.md); the one-line takeaway: **message passing over locks, serialization over threads**.

WeakPtr's contract grows on exactly this ground. Hand a weak pointer off wherever you like — toss a `WeakPtr<Controller>` to a thread pool and post a callback back, and nobody stops you. But **the moment you actually dereference, or actually invalidate the factory**, you must land back on the bound sequence. Otherwise deref and invalidate squeeze in from both ends at once, and that is a race.

---

## SEQUENCE_CHECKER: it watches mutual exclusion, not same thread

The name `SequenceChecker` is ambiguous — it reads like it checks "are we on the same thread." What it actually watches is something lower-level: **mutual exclusion**. The first time a `SequenceChecker` is touched, it records the current context. On every later touch, it audits one thing: is the context I am in right now mutually exclusive with the one I recorded?

"Mutually exclusive" has three sources in Chromium, and matching any one passes: the same physical thread, the same sequence (the same `SequencedTaskRunner`), or the same tracked lock. The verdict rides on `SequenceToken`. This thing sits in thread-local storage (TLS); tasks from the same sequence all carry the same token when they get scheduled out, so `SequenceChecker` compares tokens and knows where it stands.

Where does this machinery earn its keep? The most vicious class of concurrency bug is exactly "I thought this code ran serially, and it turned out concurrent" — brutal to reproduce, you can run it a hundred times under the debugger without a single offense, then in production one user click and it falls over. `SequenceChecker` lets you nab that crowd in a debug build at almost no cost.

## The three macros: how to use them

Chromium wraps `SequenceChecker` into three macros, with the whole debug-versus-release difference hidden behind them:

```cpp
class Controller {
public:
    void do_work() {
        DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);  // I must run on the bound sequence
        // ... work ...
    }

private:
    SEQUENCE_CHECKER(sequence_checker_);   // declare a checker member
};
```

`SEQUENCE_CHECKER(name)` declares the member — it can go anywhere, a member or a local variable. `DCHECK_CALLED_ON_VALID_SEQUENCE(name)` is the one doing real work: it bets you are on the bound sequence right now, and if not, a debug build prints the call stack and aborts while a release build plays dead and does nothing. `DETACH_FROM_SEQUENCE(name)` is an explicit unbind — you shout at the checker, "I'm not bound to a sequence right now; bind again on the next `DCHECK_CALLED_ON_VALID_SEQUENCE`." It's for the scenario where the object doesn't yet know which sequence it will serve on when it's constructed, which is quite common.

WeakPtr's `Flag` is the showpiece of this pattern. Its `sequence_checker_` detaches the moment it is constructed — the subtext being "I don't yet know which sequence I belong to." It binds on the first touch by `IsValid`/`Invalidate`, and every access after that has to land on that same sequence. This is WeakPtr's lazy sequence binding; we'll take it apart in its own dedicated piece in 02-4.

## All no-ops in release: where the zero cost comes from

Here is a fact that matters enormously and that plenty of people never notice: **in a release build (where `DCHECK_IS_ON()` is false), all three macros compile down to nothing**.

One at a time. `SEQUENCE_CHECKER(name)` expands to a `static_assert(true, "")` placeholder — not half a byte of member, so `sizeof(Controller)` doesn't grow on its account. `DCHECK_CALLED_ON_VALID_SEQUENCE(name)` expands to a do-nothing placeholder (in real Chromium this is `EAT_CHECK_STREAM_PARAMS(...)`, whose job is to swallow the attached stream parameters and check nothing). `DETACH_FROM_SEQUENCE(name)` likewise leaves nothing behind.

The first time we read this, it stopped us for a beat: debug catches violations free of charge, and in release these checks cost exactly 0 — the code runs full speed with nothing attached. This is the root design of the `DCHECK` system: **catch bugs during development, zero overhead in production**.

But zero cost has a price of its own. Precisely because release builds have zero enforcement, WeakPtr's "sequence contract" in release **rests entirely on developer discipline**. `MaybeValid()` (02-4) is the one query interface deliberately left unbound to a sequence, callable from any sequence, and even its return value is only an "optimistic estimate." Violate the sequence contract in a release build, and the program won't crash for you — it will just hand you a race at some unlucky juncture. So in debug builds, treat every DCHECK seriously; that is close to your only chance to catch this kind of bug, and once it slips past, it's really gone.

## DCHECK vs CHECK: abort in debug only, or in release too

`DCHECK` and `CHECK` are two tiers in Chromium's assertion system, and the difference is a single line.

`DCHECK(expr)` checks `expr` only in debug builds (`DCHECK_IS_ON()`), and aborts when it fails; in release, the whole expression, `expr` included, is never evaluated — it simply evaporates. `CHECK(expr)` isn't picky: debug or release, it checks, and aborts on failure.

Put plainly: `DCHECK` is a "development-time contract," while `CHECK` is a "red line production must also hold." Which one to pick comes down to a single question: after this assertion fails, can you accept the release build continuing to run? If the logic is wrong but running on won't immediately cause a memory-safety problem, use `DCHECK` — catch in debug, let go in release, save the overhead. If a failed assertion means "a use-after-free or nastier undefined behavior is guaranteed to come next," then release has to stop right away too, and that's a job for `CHECK`.

### WeakPtr's tradeoff: CHECK on operator*, DCHECK on IsValid

With that principle in hand, let's look at two key WeakPtr assertions. The two choices point in opposite directions, yet the logic behind them is the same one.

`operator*` and `operator->` take `CHECK`, and release aborts all the same:

```cpp
T& operator*() const {
    CHECK(ref_.IsValid());   // dereferencing after invalidation → aborts in release too
    return *ptr_;
}
```

Why `CHECK` here? Think it through. Dereferencing an already-invalidated WeakPtr means your hand is clutching a pointer to an object that **may have destructed long ago**, and you are about to poke its memory — that is the direct precursor of use-after-free. Let the program keep running with a dangling pointer, and there is no telling what monsters it spits out. A bug like this has to blow up immediately in release too, no wiggle room whatsoever. Hence `CHECK` here: insist on dereferencing an invalidated weak pointer, and whether debug or release, the program halts on the spot and hands you a clean crash instead of an untraceable UAF bad debt.

`IsValid` takes `DCHECK`, caught in debug only:

```cpp
bool Flag::IsValid() const {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);   // sequence violation → caught in debug only
    return !invalidated_.IsSet();
}
```

Why step down one tier here? A sequence violation is a **usage contract** problem, not an "immediate memory safety" problem. Call `IsValid` from the wrong sequence and the answer may be inaccurate — you read a stale state. But the call itself never dereferences a dangling pointer; the genuinely dangerous deref is already locked down tight at the `operator*` layer with `CHECK`. So `IsValid` takes `DCHECK` here: debug catches the sequence violation and helps you drag the bug out, release lets it pass and saves the check. Besides, the atomic operation itself guarantees `IsValid`'s read won't tear, so even if release misses a sequence violation, you won't get data-race-grade UB popping out of it.

Put the two side by side, and WeakPtr's safety layering stands up: **memory safety (dereferencing an invalidated handle) is guarded by `CHECK` — the sky can fall and it still holds; the usage contract (sequence binding) is guarded by `DCHECK` — caught in development, trusted to the developer in production.** Every assertion in the hands-on pieces ahead will bump into this same tradeoff, and you lose nothing by chewing it through early.

## References

- [Chromium `base/sequence_checker.h`](https://source.chromium.org/chromium/chromium/src/+/main:base/sequence_checker.h)
- [Chromium threading & sequences documentation](https://chromium.googlesource.com/chromium/src/+/main/docs/threading_and_tasks.md)
- [cppreference: std::atomic and memory_order](https://en.cppreference.com/w/cpp/atomic/memory_order)
- [Chromium `base/check.h` — CHECK/DCHECK](https://source.chromium.org/chromium/chromium/src/+/main:base/check.h)
