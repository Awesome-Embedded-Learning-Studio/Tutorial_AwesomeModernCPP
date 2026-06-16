---
chapter: 1
cpp_standard:
- 23
description: Deep dive into the design of CancelableToken—implementing a lightweight
  cancellation mechanism using `shared_ptr` + `atomic<bool>`, and how it integrates
  into the execution flow of `OnceCallback`.
difficulty: beginner
order: 4
platform: host
prerequisites:
- OnceCallback 实战（二）：核心骨架搭建
- OnceCallback 前置知识速查：C++11/14/17 核心特性回顾
reading_time_minutes: 8
related:
- OnceCallback 实战（五）：then 链式组合
- OnceCallback 实战（六）：测试与性能对比
tags:
- host
- cpp-modern
- beginner
- 回调机制
- atomic
- 智能指针
- 引用计数
title: 'OnceCallback in Practice (Part 4): Designing a Cancellation Token'
translation:
  source: documents/vol9-open-source-project-learn/chrome/01_once_callback/full/01-4-once-callback-cancellation-token.md
  source_hash: c84ec06e9d73d6d11a77f352e48074daa2488703747abdb92727d138e24a1de2
  translated_at: '2026-06-16T04:13:09.609635+00:00'
  engine: anthropic
  token_count: 1517
---
# OnceCallback in Practice (Part 4): Cancellation Token Design

## Introduction

A very common requirement in asynchronous programming is that external conditions change after a callback is created but before it is executed, rendering the callback meaningless—for example, the object bound to the callback is destroyed, or the task is cancelled. In these cases, we want the callback to check "should I still execute?" before running, rather than blindly executing.

This is the purpose of a cancellation token. In this post, we will implement a simplified cancellation token and see how it integrates into the execution flow of OnceCallback.

> **Learning Objectives**
>
> - Understand the concept and motivation behind cancellation tokens.
> - Understand the implementation of `CancelableToken` line by line.
> - Understand how the cancellation mechanism integrates into `OnceCallback`.
> - Understand the different behaviors of void and non-void callbacks upon cancellation.

---

## The Concept of Cancellation Tokens

You can think of a cancellation token as a "pass." When a callback is created, we give it a pass marked "valid." At some point, external conditions change (e.g., the bound object is destroyed), and external code says "the pass is voided" (calling `Invalidate()`). Afterward, all callbacks holding this pass will find it "invalid" upon checking before execution and skip the run.

In Chromium, this "pass" is the control block inside `WeakPtr`—when the object pointed to by `WeakPtr` is destroyed, the flag in the control block is cleared, and all callbacks bound to this `WeakPtr` are automatically cancelled. Our simplified version doesn't need to be as complex as `WeakPtr`; we only need a simple "valid/invalid" flag.

### Core Requirements

A cancellation token needs to meet three conditions: multiple callbacks can share the same token (one `Invalidate()` invalidates all callbacks simultaneously), the token must be copyable and movable (convenient for holding a copy inside `OnceCallback` and one outside), and the invalidation check must be thread-safe (an external thread might call `Invalidate()` in one thread while the callback checks `IsValid()` in another).

---

## Complete Implementation of CancelableToken

The entire cancellation token is only 18 lines of code, but every line has its purpose.

```cpp
class CancelableToken {
 public:
  CancelableToken() : flag_(std::make_shared<Flag>()) {}

  void Invalidate() {
    flag_->valid.store(false, std::memory_order_release);
  }

  bool IsValid() const {
    return flag_->valid.load(std::memory_order_acquire);
  }

 private:
  struct Flag {
    std::atomic<bool> valid{true};
  };

  std::shared_ptr<Flag> flag_;
};
```

### Why Use a Nested `Flag` Struct?

You might wonder—why not just put an `atomic<bool>` directly in `CancelableToken`? The reason is that `shared_ptr` manages a heap object. If we put `atomic<bool>` directly in `CancelableToken`, `shared_ptr` would manage the `CancelableToken` itself—but `CancelableToken` has its own `shared_ptr` member, which creates a cycle where `shared_ptr` contains a `shared_ptr`.

By using a nested `Flag` struct to isolate the state that needs to be shared, `shared_ptr` directly manages `Flag`. The copying and moving of `CancelableToken` are automatically handled through the reference counting of `shared_ptr`—simple and correct. Another benefit is that the `Flag` struct is easy to extend later—if we need to add more atomic flags (like a cancellation reason code), we can just add them to `Flag`.

### The Sharing Mechanism of `shared_ptr`

The copy constructor and copy assignment of `CancelableToken` are compiler-generated defaults—they simply copy the `shared_ptr<Flag>`, incrementing the reference count. All token copies created via copying share the same `Flag` object. When any copy calls `Invalidate()`, it modifies the same `Flag`, and all copies will see `false` on their next call to `IsValid()`.

```cpp
// Copying a token shares the underlying Flag
CancelableToken token2 = token1; // Both point to the same Flag
token1.Invalidate();             // Modifies the shared Flag
assert(!token2.IsValid());       // token2 sees the change
```

### `memory_order_acquire`/`release` Pairing

`Invalidate()` uses `memory_order_release` to store `false`, and `IsValid()` uses `memory_order_acquire` to load. This is a pair of memory orders. The release store guarantees that all writes before the store (including any state modifications before calling `Invalidate()`) are visible to other threads. The acquire load guarantees that all reads after the load see the writes preceding the release store.

In our scenario, this means if one thread calls `Invalidate()`, another thread subsequently calling `IsValid()` is guaranteed to see `false`—there will be no "I just invalidated it but `is_valid` still returns true" situation. This is the guarantee of thread safety.

---

## Integration into OnceCallback

The cancellation token is set into `OnceCallback` via the `set_cancel_token` method:

```cpp
void set_cancel_token(CancelableToken token) {
  cancel_token_ = std::move(token);
}
```

`cancel_token_` is of type `CancelableToken`, defaulting to an empty value (cancellation disabled). After setting, ownership of the cancellation token is transferred into `OnceCallback`.

### Complete Logic of `is_cancelled()`

```cpp
bool is_cancelled() const {
  if (state_ != State::kValid) {
    return true;
  }
  if (cancel_token_ && !cancel_token_->IsValid()) {
    return true;
  }
  return false;
}
```

Two layers of checks. First layer: if the state is not `kValid`, return true—empty callbacks (`kEmpty`) and consumed callbacks (`kConsumed`) both count as "cancelled." This makes sense—empty callbacks have nothing to execute, and consumed callbacks have already run. Second layer: if there is a cancellation token and the token is invalid, also return true.

### Cancellation Check in `impl_run()`

```cpp
ReturnType impl_run(Args&&... args) {
  if (is_cancelled()) {
    state_ = State::kConsumed;
    func_.reset(); // Release resources
    if constexpr (std::is_void_v<ReturnType>) {
      return;
    } else {
      throw CallbackCancelledException("Callback was cancelled");
    }
  }
  // ... execute the callable
}
```

The cancellation check happens **before** executing the callable object. If cancelled, the callback is consumed directly without execution—`state_` is set to `kConsumed`, and `func_` is reset to `nullptr` (destroying the internal callable object and releasing resources).

---

## Difference in Cancellation Behavior Between void and Non-void Callbacks

There is a design decision here worth expanding on—when a void callback is cancelled, it simply returns (no execution, no error), whereas when a non-void callback is cancelled, it throws a `CallbackCancelledException`.

The reason is the caller's expectations differ. The caller of a void callback does not expect a return value—after calling `Run()`, it's done, regardless of whether the callback actually executed. So, skipping execution of a cancelled void callback is transparent to the caller.

The caller of a non-void callback expects a return value—`Run()` returns something. If the callback is cancelled, we cannot provide a meaningful return value. Returning a default value (like 0) might mask errors—the caller thinks the callback executed normally, but actually nothing was done. Throwing an exception seems aggressive, but it explicitly tells the caller "something went wrong," which is safer than silently returning an error value.

Chromium chooses to terminate the program directly (`CHECK` failure) here, reasoning that in Chrome's architecture, cancelled callbacks should not be called—the caller should check `IsCancelled()` before calling. We chose exceptions to make it easier to catch and verify in tests, rather than crashing the program directly.

---

## Usage Example

```cpp
// Example 1: Normal execution
auto token = std::make_shared<CancelableToken>();
OnceCallback<void()> cb = [token]() {
  if (token->IsValid()) {
    std::cout << "Executing task..." << std::endl;
  }
};
cb(); // Prints "Executing task..."

// Example 2: Cancellation
auto token2 = std::make_shared<CancelableToken>();
OnceCallback<void()> cb2 = [token2]() {
  std::cout << "This will not print" << std::endl;
};
token2->Invalidate();
cb2.set_cancel_token(*token2);
cb2(); // Lambda does not execute, callback consumed
```

Note in the second example—`Run()` is called, but the lambda inside the callback does not execute. `impl_run()` detects the token is invalid before execution, consumes the callback, and returns.

---

## Summary

In this post, we implemented a cancellation token and integrated it into `OnceCallback`. `CancelableToken` uses `shared_ptr` + `atomic` to implement a lightweight cancellation mechanism—all token copies share the same `Flag` object, and one `Invalidate()` invalidates all copies simultaneously. The integration checks the token status before `impl_run()` executes—if cancelled, the callback is consumed without execution. Void callbacks return directly, while non-void callbacks throw `CallbackCancelledException`, a difference stemming from the caller's different expectations regarding return values.

In the next post, we will look at `ThenCallback` chaining—the most intricate ownership design among the four `OnceCallback` features.

## References

- [cppreference: std::shared_ptr](https://en.cppreference.com/w/cpp/memory/shared_ptr)
- [cppreference: std::atomic](https://en.cppreference.com/w/cpp/atomic/atomic)
- [Chromium WeakPtr Documentation](https://chromium.googlesource.com/chromium/src/+/main/docs/memory_model/weak_ptr.md)
