---
chapter: 1
cpp_standard:
- 23
description: "A close look at the design of CancelableToken — a lightweight cancellation mechanism built from shared_ptr + atomic<bool> — and how it slots into OnceCallback's execution flow"
difficulty: beginner
order: 4
platform: host
prerequisites:
- 'OnceCallback hands-on (II): building the core skeleton'
- 'OnceCallback prerequisite cheat sheet: a review of C++11/14/17 core features'
reading_time_minutes: 8
related:
- 'OnceCallback hands-on (V): chaining with then'
- 'OnceCallback hands-on (VI): tests and performance comparison'
tags:
- host
- cpp-modern
- beginner
- 回调机制
- atomic
- 智能指针
- 引用计数
title: 'OnceCallback hands-on (IV): designing the cancellation token'
translation:
  source: documents/vol9-open-source-project-learn/chrome/01_once_callback/full/01-4-once-callback-cancellation-token.md
  source_hash: 0635aad9ae5b7bd7184cc93c2191513f47a3caaccfcb5e558f4aeb8fbfee1c31
  translated_at: '2026-09-26T00:30:44+00:00'
  engine: anthropic
  token_count: 2243
---
# OnceCallback hands-on (IV): designing the cancellation token

A callback gets bound at one moment and actually runs at another, with a stretch of task-queue scheduling in between. Just about anything can happen in that gap: the bound object is gone, a higher layer has revoked the task, the user has already closed the tab. When the callback finally gets its turn to execute, that "do I still need to run?" check sitting in front of it is the cancellation token — and building that token is what this piece is about.

Chromium turns this job into the whole `WeakPtr` machinery under `//base`: the object destructs, and every callback hanging off it is voided in one stroke. That system is a lot to cover in full, so here we build a minimum viable version first: a lightweight flag that several parties can share, where a single invalidate drops it for everyone, and that can be checked across threads. Once this one is running, going back to read Chromium's `WeakPtr` will feel a lot smoother.

## One flag, shared how

The core tension a cancellation token has to resolve fits in one sentence: invalidation happens outside the callback, the check happens inside it, and the two sides are most likely not in the same place — possibly not even on the same thread. So the token itself has to be something both sides can each hold a copy of, while both copies point at one shared piece of state.

That means two things must hold at the same time. The token has to be copyable, so the callback holds one copy internally and the code that wants to cancel it holds another, with both copies landing on the same state. And the reads and writes of that shared state have to be thread-safe: an external thread calling `invalidate()` and the callback thread checking `is_valid()` must never collide into a torn read.

Let's look at the implementation first, then circle back to why it is shaped the way it is.

## The full CancelableToken implementation

The whole cancellation token is only 18 lines of code, but every line has a reason to be there.

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

### Why we need a nested Flag struct at all

The first time I wrote this thing, my hand moved faster than my head: I dropped a `std::atomic<bool> valid` straight into `CancelableToken` and figured that settled it. Only after finishing did it feel wrong — now `shared_ptr` has no way to manage that `valid`. To share it, you have to point the `shared_ptr` at an object that *contains* `valid`. But if that object is `CancelableToken` itself, then `CancelableToken` carries a `shared_ptr` member inside, and you have wound yourself into a `shared_ptr<CancelableToken>` wrapping `shared_ptr<Flag>` nesting doll.

Pull out exactly the bit of state you want to share, drop it into a `Flag` struct, and let `shared_ptr` manage that directly — that is when the penny finally drops. `CancelableToken`'s copying and moving no longer need any babysitting from you: the compiler-generated copy constructor just shallow-copies the inner `shared_ptr<Flag>`, the reference count ticks up, and every copy naturally points at the same `Flag`. Thinking it over later, the struct turns out to have an unexpected bonus too: the day you genuinely want to add something to it — a cancellation reason code, say — you just add a field inside `Flag`, and not one line outside moves.

### How the sharing works in practice

Just saying "shared on copy" may still feel abstract. Look at the snippet below: `token2` is a copy of `token1`, and the `shared_ptr<Flag>` inside each points at the same memory. When `token1` calls `invalidate()`, it mutates the `valid` in that memory, and when `token2` next checks `is_valid()` it reads the same cell — so it naturally sees `false`:

```cpp
auto token1 = std::make_shared<CancelableToken>();
auto token2 = token1;  // shares the same Flag

token1->invalidate();
assert(!token2->is_valid());  // token2 sees the invalidation too
```

One caution is worth sounding here: the example wraps an extra `shared_ptr<CancelableToken>` around the outside purely to keep the snippet short. The real usage is to copy `CancelableToken` itself by value (it already shares state via its internal `shared_ptr`); there is no need to wrap another layer of smart pointer around it.

### The acquire/release pair

`invalidate()` stores `false` with `memory_order_release`, and `is_valid()` loads with `memory_order_acquire`. That is not a pair picked at random. The release store guarantees that the writes before it (say, the modifications to object state made before invalidation) all become visible to any thread that reads the new value through this store. The acquire load guarantees that once that new value has been read, every subsequent read sees the batch of writes that preceded the release.

Landed in our scenario: one thread calls `invalidate()`, another thread checks `is_valid()` right after, and as long as the latter reads `false`, all the work the former did before invalidating is visible to it. You will not run into that nonsense of "I just finished invalidating, so why does is_valid still say true." That is the confidence that lets it be used across threads. Swap both orderings for `memory_order_relaxed`, and the visibility guarantee evaporates: whether the flag flipped, and when other threads get to see it, is all down to luck. This is exactly where beginners' hands slip most easily.

## Integrating it into OnceCallback

The cancellation token only has room to prove itself once it hangs off a callback. The attachment point is a `set_token()`:

```cpp
void set_token(std::shared_ptr<CancelableToken> token) {
    token_ = std::move(token);
}
```

`token_` defaults to an empty `shared_ptr`, which amounts to "this callback does not take part in the cancellation mechanism." The moment `set_token` comes in, the token is moved inside the callback and lives and dies with it. Note that we deliberately used `shared_ptr<CancelableToken>` rather than a bare `CancelableToken`, because OnceCallback is move-only: whatever it holds either has to be movable away wholesale, or be a cheaply copyable handle — and `shared_ptr` is exactly the latter.

### is_cancelled() looks in two places

```cpp
[[nodiscard]] bool is_cancelled() const noexcept {
    if (status_ != Status::kValid) return true;
    if (token_ && !token_->is_valid()) return true;
    return false;
}
```

This check does not look at the token alone. It first glances at the callback's own `status_`: an empty callback (`kEmpty`) and an already-run callback (`kConsumed`) both count outright as "cancelled." That is fair — an empty callback has nothing inside to execute, and a callback that has already run must not run again. Only once the `status_` gate passes does the token get its turn: if there is a token and it has been invalidated, that also counts as cancelled. Both gates have to be there; with the token alone, empty and consumed callbacks would slip through the net.

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

The cancellation check sits **before** the callable is executed, and that position matters. Once the check hits, the callback is marked `kConsumed` on the spot — `func_` is nulled along with it, and the lambda inside, together with the resources it captured, is released right away. From the outside, this `run()` looks like it consumed the callback, except the function body never actually executed. That "consumed but not executed" semantics is the root of the void versus non-void behavior split that follows.

## Why void and non-void callbacks behave differently on cancellation

This is the one spot in the whole design I think is most worth stopping on. Both hit the same cancellation check, yet a void callback simply `return`s and reports nothing, while a non-void callback throws `std::bad_function_call`. At first glance that looks inconsistent; think it through and you see it is forced.

The caller of a void callback is not expecting a return value at all — they write `std::move(cb).run();` and are done with it; they have no idea whether you executed or not. So skipping silently on cancel is completely transparent to the caller. Nothing wrong there.

A non-void callback is the awkward one. The caller wrote `int result = std::move(cb).run();` and is staring right at that return value. The callback got cancelled — what do you hand back? Just fill in a 0? Then the caller takes that 0, assumes the callback ran to completion and produced a 0, and the downstream logic carries on from that 0 — and a "looks like success but did nothing" bug of that kind is harder to hunt down than a crash. So here we would rather throw an exception and tell the caller plainly: this run did not happen, deal with it as you see fit.

Chromium's choice is harsher still: it `CHECK`-fails and terminates the process outright. The logic goes: in this architecture of ours, the caller was supposed to check `is_cancelled()` themselves before calling `run()`; if you already checked and still charged in, that is a bug, and they crash it in your face. We take the exception road here mostly to keep tests easy to write — a unit test can assert the throw with `REQUIRE_THROWS`, instead of the whole process dying the moment one case runs. Neither choice is right or wrong; it comes down to how harsh the environment is where you plan to use this callback machinery.

## Usage example

```cpp
using namespace tamcpp::chrome;

// Create the token and the callback
auto token = std::make_shared<CancelableToken>();
bool executed = false;

OnceCallback<void()> cb([&executed] { executed = true; });
cb.set_token(token);

// With a valid token, the callback executes normally
assert(!cb.is_cancelled());
std::move(cb).run();
assert(executed);  // the callback executed

// Create another callback, this time invalidate the token first
executed = false;
auto cb2 = OnceCallback<void()>([&executed] { executed = true; });
cb2.set_token(token);
token->invalidate();  // void the token

assert(cb2.is_cancelled());
std::move(cb2).run();  // a cancelled void callback does not execute and does not throw
assert(!executed);     // the callback did not execute
```

Read the second example carefully: `cb2.run()` really was called, but not a single line of the lambda inside ran. `impl_run()` ran into the invalidated token before executing, consumed the callback on the spot, and `return`ed — so `executed` is still `false`. That is the transparent semantics of a cancelled void callback.

## References

- [cppreference: std::shared_ptr](https://en.cppreference.com/w/cpp/memory/shared_ptr)
- [cppreference: std::atomic](https://en.cppreference.com/w/cpp/atomic/atomic)
- [Chromium WeakPtr documentation](https://chromium.googlesource.com/chromium/src/+/main/docs/memory_model/weak_ptr.md)
