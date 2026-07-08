---
chapter: 0
cpp_standard:
- 11
- 17
- 20
description: "Drawing the line between threads and sequences, the three SEQUENCE_CHECKER macros, why they vanish to zero cost in release, and the DCHECK vs CHECK tradeoff that explains why WeakPtr's operator* uses CHECK."
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
---
# WeakPtr prerequisite (III): sequences, SEQUENCE_CHECKER, and DCHECK/CHECK

[Prerequisite (II)](./pre-02-weak-ptr-atomic-and-memory-order.md) settled the visibility problem: two sequences poking a flag at the same time, acquire/release makes sure both see it. But WeakPtr carries one more contract, written in the comment at the top of `weak_ptr.h`. I skimmed right past it the first time, only came back to read it carefully after it bit me. **A weak pointer can be handed around across sequences freely, but dereferencing it and invalidating it must happen on the sequence it was bound to.**

Atomic operations cannot enforce this. It takes a set of macros called `SEQUENCE_CHECKER`: debug builds catch violations, release builds let it evaporate to zero overhead. This piece walks through that machinery and, along the way, makes the `DCHECK` and `CHECK` tradeoff clear. Once those two click, you can read why WeakPtr puts a `CHECK` on `operator*` but only dares put a `DCHECK` on `IsValid`.

## Threads vs sequences: Chromium's concurrency model

First, a common misunderstanding. The moment people hear "thread-safe," most picture "several threads pile on at once." Chromium does not think about it that way. Its concurrency model is built on **sequences**.

What is a sequence? Think of it as a virtual thread. Tasks inside it run one after another, the next one only starts when the previous one finishes. But the virtual thread itself is not nailed to any OS thread; it can hop between physical threads, as long as two of its tasks never overlap.

Why the detour? I wondered the same thing at first, why not just use threads. It clicked eventually. The thing most objects in a browser actually want is "I want to be accessed serially, and I don't care which thread serves me." Use a real thread and you inherit its TLS, its message pump, its lifecycle. Use a sequence and all of that becomes the task scheduler's problem; you only say "this batch of code belongs to one sequence" and the scheduler guarantees they don't overlap. We covered this in [OnceCallback hands-on (I)](../../01_once_callback/full/01-1-once-callback-motivation-and-api-design.md). The one-line summary: **message passing over locks, serialization over threads.**

WeakPtr's contract sits on this ground. Pass a weak pointer around as much as you like, hand a `WeakPtr<Controller>` to a thread pool and post a callback back, nobody stops you. But **the moment you actually dereference it, or tell the factory to invalidate**, you must be back on the bound sequence. Otherwise deref and invalidate collide head-on, and that is a race.

---

## SEQUENCE_CHECKER: it watches "mutual exclusion," not "same thread"

The name `SequenceChecker` is misleading, it reads like it checks "are we on the same thread." It actually watches something lower-level, **mutual exclusion**. The first time a `SequenceChecker` is touched it remembers the current context. Every later touch, it checks one thing: is the context I'm in now mutually exclusive with the one I recorded?

"Mutually exclusive" in Chromium has three sources, any one is enough: same physical thread, same sequence (same `SequencedTaskRunner`), or the same tracked lock. The judgment uses `SequenceToken`, which lives in thread-local storage. Tasks from the same sequence carry the same token when they get scheduled out, so `SequenceChecker` compares tokens and knows.

Where this pays off: the nastiest class of concurrency bug is "I thought this ran serially, but it ran concurrently." Hard to reproduce, you can hit it a hundred times in the debugger with no crash, then a user clicks once in production and it blows up. `SequenceChecker` lets you catch that crowd almost for free in a debug build.

## The three macros: how to use them

Chromium wraps `SequenceChecker` into three macros. The debug/release difference hides entirely behind them:

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

`SEQUENCE_CHECKER(name)` declares the member, you can put it anywhere, member or local variable. `DCHECK_CALLED_ON_VALID_SEQUENCE(name)` is the one that actually works; it bets you're on the bound sequence right now. If not, a debug build prints the call stack and aborts, a release build plays dead and does nothing. `DETACH_FROM_SEQUENCE(name)` is an explicit unbind; you tell the checker "I'm not bound to any sequence yet, bind on the next `DCHECK_CALLED_ON_VALID_SEQUENCE`." It's for the case where the object doesn't know which sequence it'll serve when constructed, which is common.

WeakPtr's `Flag` is the textbook example of this pattern. Its `sequence_checker_` detaches the moment it's constructed, meaning "I don't know which sequence I belong to yet." It binds the first time `IsValid` or `Invalidate` touches it, and every access after that has to land on the same sequence. That is WeakPtr's lazy sequence binding, and I'll take it apart properly in 02-4.

## All no-ops in release: where zero cost comes from

Here is a fact that's easy to miss and matters a lot: **in a release build (`DCHECK_IS_ON()` false), all three macros compile to nothing.**

One by one. `SEQUENCE_CHECKER(name)` expands to a `static_assert(true, "")` placeholder, not half a byte of member, `sizeof(Controller)` doesn't grow because of it. `DCHECK_CALLED_ON_VALID_SEQUENCE(name)` expands to a no-op placeholder (in real Chromium this is `EAT_CHECK_STREAM_PARAMS(...)`, its job is to swallow the attached stream parameters and check nothing). `DETACH_FROM_SEQUENCE(name)` likewise leaves nothing behind.

I paused the first time I read that. Debug catches violations for free, release pays exactly zero for these checks, the code runs full speed with nothing in the way. That's the whole point of the `DCHECK` system: **catch bugs in development, zero overhead in production.**

But zero cost has a price. Because release builds have no enforcement at all, WeakPtr's "sequence contract" in release **rests entirely on developer discipline**. `MaybeValid()` (02-4) is the one query interface deliberately unbound to a sequence, callable from anywhere, but its return value is an "optimistic estimate" at best. Violate the contract in release and the program won't crash for you. It'll hand you a race at some inconvenient moment instead. So in a debug build, every DCHECK matters. That's close to your only shot at catching this kind of bug. Let one slide and it's gone.

## DCHECK vs CHECK: abort in debug only, or abort in release too

`DCHECK` and `CHECK` are two tiers of Chromium's assertion system. The difference is one line.

`DCHECK(expr)` checks `expr` only in a debug build (`DCHECK_IS_ON()`), aborts if it fails. In release, the whole expression including `expr` is never evaluated, it evaporates. `CHECK(expr)` doesn't care, debug or release both check it, both abort on failure.

Put it plainly: `DCHECK` is a "development contract," `CHECK` is a "red line production also holds." Which to pick comes down to one question: if this assertion fails, can you tolerate release keeping on running. Logic is wrong but continuing won't immediately cause a memory-safety problem, use `DCHECK`, catch in debug, let go in release, save the cost. If a failed assertion means "next thing that happens is a use-after-free or worse undefined behavior," then release has to stop right now too, and that's `CHECK`.

### WeakPtr's call: CHECK on operator*, DCHECK on IsValid

With that principle in hand, look at two key WeakPtr assertions. They point opposite ways, but the logic behind them is the same one.

`operator*` and `operator->` use `CHECK`, release aborts too:

```cpp
T& operator*() const {
    CHECK(ref_.IsValid());   // invalidated and still dereferenced → release also aborts
    return *ptr_;
}
```

Why `CHECK` here? Think it through. Dereferencing an already-invalidated WeakPtr means you're holding a pointer to an object that **may have destructed long ago**, and you're about to touch its memory. That's the direct predecessor of use-after-free; letting the program keep running with a dangling pointer can produce any kind of garbage. This one has to blow up in release too, no slack. So `CHECK`: dereference an invalidated weak pointer, debug or release, the program stops right there, you get a clean crash instead of an untraceable UAF mess.

`IsValid` uses `DCHECK`, debug only:

```cpp
bool Flag::IsValid() const {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);   // sequence violation → debug only
    return !invalidated_.IsSet();
}
```

Why step down a tier here? A sequence violation is a **usage contract** problem, not an "immediate memory safety" problem. Call `IsValid` from the wrong sequence and the result may be stale, you read a state that's behind. But it doesn't itself dereference a dangling pointer; the genuinely dangerous deref is already locked down at the `operator*` layer with `CHECK`. So `IsValid` uses `DCHECK` here: catch the sequence violation in debug to help you find the bug, let it go in release to save the check. And besides, the atomic operations themselves guarantee `IsValid`'s read won't tear, so even if release misses a sequence violation, you won't get a data-race-level UB out of it.

Put the two side by side and WeakPtr's safety layering stands up: **memory safety (deref of an invalidated handle) uses `CHECK`, the sky can fall and it still holds; usage contract (sequence binding) uses `DCHECK`, caught in development, trusted to the developer in production.** Every assertion in the hands-on pieces ahead will run into this same call, so get it clear in your head early.

## References

- [Chromium `base/sequence_checker.h`](https://source.chromium.org/chromium/chromium/src/+/main:base/sequence_checker.h)
- [Chromium threading & sequences documentation](https://chromium.googlesource.com/chromium/src/+/main/docs/threading_and_tasks.md)
- [cppreference: std::atomic and memory_order](https://en.cppreference.com/w/cpp/atomic/memory_order)
- [Chromium `base/check.h` — CHECK/DCHECK](https://source.chromium.org/chromium/chromium/src/+/main:base/check.h)
