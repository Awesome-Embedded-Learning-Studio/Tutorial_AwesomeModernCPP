---
chapter: 1
cpp_standard:
- 23
description: "A close look at CancelableToken's design: a lightweight cancellation mechanism built from shared_ptr + atomic<bool>, and how it slots into OnceCallback's execution flow"
difficulty: beginner
order: 4
platform: host
prerequisites:
- 'OnceCallback in Practice (II): the core skeleton'
- 'OnceCallback prerequisites: a refresher on C++11/14/17 essentials'
reading_time_minutes: 8
related:
- 'OnceCallback in Practice (V): then chaining'
- 'OnceCallback in Practice (VI): tests and performance'
tags:
- host
- cpp-modern
- beginner
- 回调机制
- atomic
- 智能指针
- 引用计数
title: 'OnceCallback in Practice (IV): the cancellation token'
---
# OnceCallback in Practice (IV): the cancellation token

A callback usually gets bound at one moment and actually runs at another, with a task queue sitting in between. A lot can happen in that gap: the bound object is gone, the upper layer cancels the task, the user has already closed the tab. When the callback finally gets its turn, that "do I still need to run?" check at the front is the cancellation token, and it is what this post builds.

Chromium turns this into the whole `WeakPtr` machinery under `//base`: when the object destructs, every callback hanging off it is voided at once. That system is a lot to take in at once, so here we build a minimum viable version first: a lightweight flag that several parties can share, that one invalidate drops for everyone, and that you can check across threads. Once this one runs, going back to Chromium's `WeakPtr` will feel a lot smoother.

## One flag, shared how

The core tension a cancellation token has to solve is one sentence: invalidation happens outside the callback, the check happens inside it, and the two sides are probably not in the same place, maybe not even on the same thread. So the token itself has to be something both sides can each hold a copy of, while pointing at the same shared state.

That means two things have to hold at once. The token has to be copyable, so the callback holds one copy internally and the code that wants to cancel it holds another, both pointing at the same state. And the reads and writes of that shared state have to be thread-safe: an external thread calling `invalidate()` and the callback thread checking `is_valid()` must not collide into a torn read.

Let us look at the implementation first, then come back to why it is shaped this way.

## The full CancelableToken

The whole cancellation token is 18 lines, but every line earns its keep.

```cpp
#pragma once
#include <atomic>
#include <memory>

namespace tamcpp::chrome {
class CancelableToken {
    struct Flag {
        std::atomic<bool> valid{true};
    };
    std::shared_ptr<Flag> flag_;

public:
    CancelableToken() : flag_(std::make_shared<Flag>()) {}

    void invalidate() {
        flag_->valid.store(false, std::memory_order_release);
    }

    bool is_valid() const {
        return flag_->valid.load(std::memory_order_acquire);
    }
};
} // namespace tamcpp::chrome
```

### Why a nested Flag struct at all

The first time I wrote this, my hand moved faster than my head: I dropped an `std::atomic<bool> valid` straight into `CancelableToken` and figured that was it. The moment it compiled and I went to share it, the problem jumped out. `shared_ptr` cannot manage that `valid` on its own. To share it, you need the `shared_ptr` to point at an object that *contains* `valid`. If that object is `CancelableToken` itself, then `CancelableToken` has a `shared_ptr` member, and you are staring at a `shared_ptr<CancelableToken>` wrapping `shared_ptr<Flag>` nesting doll.

The window paper only tears once you peel out the bit of state you actually want to share, drop it into a `Flag` struct, and let `shared_ptr` manage that directly. At that point you do not have to think about `CancelableToken`'s copy and move semantics anymore: the compiler-generated copy constructor just shallow-copies the inner `shared_ptr<Flag>`, the reference count ticks up, and every copy naturally lands on the same `Flag`. Thinking about it later, the struct has a side benefit: the day you want to add something to it, a cancellation reason code say, you just add a field to `Flag` and nothing outside moves.

### How the sharing actually lands

Saying "shared on copy" can still feel abstract. Look at this snippet. `token2` is a copy of `token1`, and the `shared_ptr<Flag>` inside both points at the same memory. When `token1` calls `invalidate()`, it mutates the `valid` in that shared memory, and the next time `token2` reads `is_valid()` it reads the same cell and naturally sees `false`:

```cpp
auto token1 = std::make_shared<CancelableToken>();
auto token2 = token1;  // shares the same Flag

token1->invalidate();
assert(!token2->is_valid());  // token2 sees the invalidation too
```

One caution here: the example wraps the outer layer in another `shared_ptr<CancelableToken>` purely to keep the snippet short. In real use you copy `CancelableToken` by value, since it already shares state through its internal `shared_ptr`. No need to wrap it in another smart pointer.

### The acquire/release pair

`invalidate()` stores `false` with `memory_order_release`, and `is_valid()` loads with `memory_order_acquire`. That is not a random pick. The release store guarantees that every write before it, say the mutations to object state that happened before invalidation, becomes visible to any thread that reads the new value through this store. The acquire load guarantees that after reading that new value, every subsequent read sees the writes that preceded the release.

Dropping that into our scenario: one thread calls `invalidate()`, another thread checks `is_valid()` right after, and as long as the latter reads `false`, every bit of work the former did before invalidating is visible to it. You will not get hit with "I just called invalidate, why does is_valid still say true." That is the confidence that lets it be used across threads. Flip both to `memory_order_relaxed` and the visibility guarantee evaporates; whether the flag flipped and when other threads see it becomes a coin flip, and that is the spot where beginners most often slip.

## Slotting it into OnceCallback

The cancellation token only earns its keep once it is attached to a callback. The attachment point is a `set_token()`:

```cpp
void set_token(std::shared_ptr<CancelableToken> token) {
    token_ = std::move(token);
}
```

`token_` defaults to an empty `shared_ptr`, which reads as "this callback does not participate in cancellation." The moment `set_token` is called, the token moves inside the callback and lives and dies with it. Note that we deliberately use `shared_ptr<CancelableToken>` rather than a bare `CancelableToken` here. OnceCallback is move-only, so whatever it holds either has to move wholesale or be a cheaply copyable handle, and `shared_ptr` is exactly the latter.

### is_cancelled() looks at two places

```cpp
[[nodiscard]] bool is_cancelled() const noexcept {
    if (status_ != Status::kValid) return true;
    if (token_ && !token_->is_valid()) return true;
    return false;
}
```

This check does not look at the token alone. It first glances at the callback's own `status_`: an empty callback (`kEmpty`) and an already-run callback (`kConsumed`) both count as "cancelled" outright. That is reasonable, since an empty callback has nothing inside to execute and a consumed callback must not run again. Only after the `status_` gate passes does the token get its turn: if there is a token and it has been invalidated, that also counts as cancelled. Both gates have to be there. Look at the token alone and the empty and consumed callbacks slip through.

### The gate inside impl_run()

```cpp
ReturnType impl_run(FuncArgs... args) {
    assert(status_ == Status::kValid);

    // Cancellation check before execution
    if (token_ && !token_->is_valid()) {
        status_ = Status::kConsumed;
        func_ = nullptr;
        if constexpr (std::is_void_v<ReturnType>) {
            return;
        } else {
            throw std::bad_function_call{};
        }
    }

    // Normal consumption flow...
}
```

The cancellation check sits **before** the callable is executed, and the position matters. The moment the check hits, the callback is marked `kConsumed` on the spot. `func_` is nulled along with it, and the lambda inside it, plus whatever it captured, gets released right away. From the outside, this `run()` looks like it consumed the callback, except the function body never actually ran. That "consumed but not executed" semantics is the root of the void versus non-void split below.

## Why void and non-void callbacks differ on cancellation

This is the one spot in the whole design I think is worth stopping on. Both hit the cancellation check, but a void callback just `return`s and reports nothing, while a non-void callback throws `std::bad_function_call`. At first glance that looks inconsistent. Sit with it a second and you see it is forced.

The caller of a void callback is not expecting a return value at all. They write `std::move(cb).run();` and walk away; they have no idea whether you executed or not. So skipping on cancel is fully transparent to them, no problem.

A non-void callback is the awkward one. The caller wrote `int result = std::move(cb).run();` and they are staring at that return value. The callback got cancelled, so what do you hand back? Pick 0 at random? Then the caller gets a 0, assumes the callback ran cleanly and handed them a 0, and the downstream logic just keeps rolling on 0. That kind of "looks like success, did nothing" bug is harder to chase than a crash. So here we would rather throw, and tell the caller plainly: this one did not run, deal with it.

Chromium's choice is rougher still: it `CHECK`-fails and terminates the process outright. The reasoning is that under their architecture, the caller is supposed to check `is_cancelled()` themselves before calling `run()`. If you checked, then charged in anyway, that is a bug, and they crash it in your face. We take the exception path here mostly so tests stay writable; a unit test can assert with `REQUIRE_THROWS` instead of taking the whole process down with it. Neither choice is right or wrong. It depends on how harsh the environment you are targeting with this callback is.

## A usage example

```cpp
using namespace tamcpp::chrome;

// Create the token and the callback
auto token = std::make_shared<CancelableToken>();
bool executed = false;

OnceCallback<void()> cb([&executed] { executed = true; });
cb.set_token(token);

// With a valid token, the callback runs normally
assert(!cb.is_cancelled());
std::move(cb).run();
assert(executed);  // the callback ran

// Build another callback, this time invalidate the token first
executed = false;
auto cb2 = OnceCallback<void()>([&executed] { executed = true; });
cb2.set_token(token);
token->invalidate();  // void the token

assert(cb2.is_cancelled());
std::move(cb2).run();  // a cancelled void callback does not run and does not throw
assert(!executed);     // the callback did not run
```

Read the second example carefully. `cb2.run()` does get called, but the lambda inside never executes a single line. `impl_run()` sees the invalidated token before execution, consumes the callback on the spot, and `return`s. `executed` is still `false`. That is the transparent semantics of a cancelled void callback.

## References

- [cppreference: std::shared_ptr](https://en.cppreference.com/w/cpp/memory/shared_ptr)
- [cppreference: std::atomic](https://en.cppreference.com/w/cpp/atomic/atomic)
- [Chromium WeakPtr documentation](https://chromium.googlesource.com/chromium/src/+/main/docs/memory_model/weak_ptr.md)
