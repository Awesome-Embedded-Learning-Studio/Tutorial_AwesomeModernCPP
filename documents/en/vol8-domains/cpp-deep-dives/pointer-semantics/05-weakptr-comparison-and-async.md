---
chapter: 1
cpp_standard:
- 17
- 20
description: Comparing std::weak_ptr with Chrome WeakPtr, plus a safety analysis of
  six async callback capture patterns
difficulty: advanced
order: 5
platform: host
prerequisites:
- 'Chrome-like WeakPtr: Reference-Counted Control Block and WeakPtrFactory'
- 'weak_ptr and Circular References: Breaking the Ownership Deadlock'
reading_time_minutes: 7
related:
- 'Series Wrap-Up: Cross-Thread Safety, Performance Trade-offs, and Design Principles'
tags:
- host
- cpp-modern
- advanced
- 智能指针
- 异步编程
- 回调机制
title: 'std::weak_ptr Compared: Async Callbacks in Practice'
translation:
  source: documents/vol8-domains/cpp-deep-dives/pointer-semantics/05-weakptr-comparison-and-async.md
  source_hash: 31762d4848b3e0f1d3533a8cef070f681c0b8482f51c4ca0903d8b80ad48bb03
  translated_at: '2026-09-27T02:43:50+00:00'
  engine: anthropic
  token_count: 3700
---
# std::weak_ptr Compared: Async Callbacks in Practice

Over the previous four posts we built a family of non-owning pointer types from scratch — from `Borrowed` to `ObserverPtr` to various flavors of `WeakPtr`. Now it is time to pull everything together and compare.

This post does two things: first, it puts `std::weak_ptr<T>` and Chrome-like `WeakPtr<T>` side by side and spells out their core differences; second, it walks through six async callback capture patterns in practice, so you can see first-hand what separates a "wrong capture" from a "correct capture".

## Core Differences Between std::weak_ptr and Chrome WeakPtr

Let's start with a fact that often gets overlooked: **`std::weak_ptr<T>` and Chrome-like `WeakPtr<T>` do not solve the same problem.**

`std::weak_ptr<T>` solves "weak references under a shared ownership model". It relies on the control block of `std::shared_ptr<T>`: once `lock()` succeeds you hold a `shared_ptr<T>`, which **temporarily extends the object's lifetime**. This means that as long as your `lock()` succeeded, the object is guaranteed not to be destroyed while your `shared_ptr` is alive.

Chrome-like `WeakPtr<T>` solves "weak references to objects that are not managed by shared_ptr". It does not depend on `shared_ptr`, and calling `get()` does not extend the object's lifetime — it merely returns a pointer. The object can be destroyed at any moment, and the pointer you obtain may be invalidated before you use it. It only guarantees that you can **safely detect invalidation**; it does not guarantee the object is still alive after you obtain the pointer.

These are two completely different lifetime strategies:

| Feature | Chrome-like WeakPtr\<T\> | std::weak_ptr\<T\> |
|------|-------------------------|-------------------|
| Depends on shared_ptr | No | Yes |
| Extends the lifetime when acquiring a reference | **No** | **Yes** (lock returns a shared_ptr) |
| Safe null check after the object is destroyed | Yes | Yes |
| Suitable for objects not managed by shared_ptr | **Yes** | No |
| Naturally cross-thread safe | No (sequence-bound) | Partially (lock() is atomic, but access to T still needs synchronization) |
| Control block overhead | Small (custom ref count) | Larger (shared_ptr's control block) |

**When should you use `std::weak_ptr`?** When the object is already managed by a `shared_ptr`, you need to observe it safely in async scenarios, and you may need to temporarily extend its lifetime.

**When should you use Chrome-like WeakPtr?** When the object is not managed by `shared_ptr` (stack objects, `unique_ptr`, framework-managed objects) and you need to safely detect invalidation in async callbacks.

**When should you not use `std::weak_ptr`?** When you would have to force the object into `shared_ptr` management just to use a `weak_ptr`. That introduces unnecessary reference-counting overhead, and under multithreading it easily becomes a performance bottleneck (atomic refcount contention).

## Six Async Callback Capture Patterns

Next, let's use real code to compare six ways of capturing a reference to an object inside an async callback. For each one we will analyze where the danger lies, what happens after the object is destroyed, and whether it is UB.

### Pattern 1: Capturing raw `this` — Dangerous

```cpp
class NetworkClient {
public:
    void start_request()
    {
        // Wrong! The lambda captures raw this
        timer_.schedule(1000ms, [this]() {
            process_response();  // If NetworkClient has been destroyed, this is dangling
        });
    }

    void process_response() { /* ... */ }

private:
    Timer timer_;
};

// Usage example
void test()
{
    auto client = std::make_unique<NetworkClient>();
    client->start_request();
    // client is destroyed here
}  // 1 second later the callback fires → this dangles → UB
```

**The problem**: `this` is just a raw pointer that carries no lifetime information. After the object is destroyed, the `this` inside the callback is a dangling pointer, and any member access through it is UB. This is the most common source of crashes in C++ asynchronous programming.

### Pattern 2: Capturing `T*` — Just as Dangerous

```cpp
void start_request()
{
    auto* raw_ptr = this;
    timer_.schedule(1000ms, [raw_ptr]() {
        raw_ptr->process_response();  // Same dangling problem
    });
}
```

**The problem**: there is no fundamental difference from capturing `this`. A `T*` provides no lifetime guarantees whatsoever. The only difference is that it "looks" like a deliberately captured pointer, but in reality it is no safer than a raw `this`.

### Pattern 3: Capturing `ObserverPtr<T>` — Still Dangerous

```cpp
void start_request()
{
    auto obs = make_observer(this);
    timer_.schedule(1000ms, [obs]() {
        if (obs) {
            obs->process_response();  // ObserverPtr::operator bool only checks for nullptr
        }                            // After the object is destroyed, obs.get() is still non-null → dangling dereference
    });
}
```

**The problem**: `ObserverPtr`'s `operator bool()` only checks whether the internal pointer is `nullptr`. After the object is destroyed, the internal pointer is not `nullptr` (it is dangling), so `if (obs)` passes, and you then dereference a dangling pointer. UB.

### Pattern 4: Capturing `UnsafeWeakPtr<T>` — UB

```cpp
void start_request()
{
    auto weak = get_unsafe_weak_ptr();
    timer_.schedule(1000ms, [weak]() {
        if (weak.is_valid()) {  // Accessing a destroyed Flag → UB!
            // ...
        }
    });
}
```

**The problem**: as the second post in this series analyzed in detail, the `Flag*` that `is_valid()` accesses may already be a dangling pointer. The very act of checking for validity is UB. This is the sneakiest danger of the six patterns — it looks like it has a liveness check, but even the liveness check itself is unsafe.

### Pattern 5: Capturing Chrome-like `WeakPtr<T>` — Correct

```cpp
class NetworkClient {
public:
    void start_request()
    {
        auto weak = factory_.get_weak_ptr();
        timer_.schedule(1000ms, [weak]() {
            if (auto* self = weak.get()) {
                self->process_response();  // Safe: get() checks the control block first
            }                             // Returns nullptr when invalidated — no dangling dereference
        });
    }

private:
    Timer timer_;
    WeakPtrFactory<NetworkClient> factory_{this};
};
```

**Analysis**: `weak.get()` checks `WeakFlag::is_valid()` first. Because `WeakFlag` is reference-counted, as long as `weak` is alive the `WeakFlag` is guaranteed to exist, so `is_valid()` cannot be UB. After the object is destroyed, the Factory's destructor invalidates the `WeakFlag`, `get()` returns `nullptr`, and the callback safely skips the work.

**But there is one precondition**: the callback must execute on the same sequence as the object's destruction. Across sequences, after `get()` returns non-null but before you actually use `self`, another sequence could be destroying the object — that is a TOCTOU race.

### Pattern 6: Capturing `std::weak_ptr<T>` — Correct

```cpp
class NetworkClient : public std::enable_shared_from_this<NetworkClient> {
public:
    void start_request()
    {
        auto weak = weak_from_this();  // C++17
        timer_.schedule(1000ms, [weak]() {
            if (auto self = weak.lock()) {
                self->process_response();  // lock() succeeds → the shared_ptr extends the lifetime
            }                             // Within self's scope, the object cannot be destroyed
        });
    }

private:
    Timer timer_;
};

// The object must be managed with shared_ptr
auto client = std::make_shared<NetworkClient>();
client->start_request();
```

**Analysis**: `weak.lock()` is an atomic operation — it either returns a valid `shared_ptr` (with the reference count incremented at the same time) or returns empty. If it returned a valid `shared_ptr`, the object is guaranteed not to be destroyed for as long as your `self` variable is alive. This is safer than Chrome WeakPtr — it does not just detect invalidation, it also prevents the object from being destroyed between the check and the use.

**But the cost is**: the object must be managed by `shared_ptr`, and `lock()` adds an atomic reference-count operation. In high-frequency async scenarios, these atomic operations can become a performance bottleneck.

## Summary of the Six Patterns

| Pattern | Liveness check | Behavior after the object is destroyed | UB? | Suitable scenarios |
|------|---------|----------------|------|---------|
| Raw `this` | None | Dangling pointer access | Yes | None — never capture raw this in an async callback |
| `T*` | None | Dangling pointer access | Yes | None — same as above |
| `ObserverPtr<T>` | None | `operator bool` passes but the pointer dangles | Yes | Synchronous observation; not for async callbacks |
| `UnsafeWeakPtr<T>` | Fake | The validity check itself is UB | Yes | None — should not be used |
| Chrome `WeakPtr<T>` | Yes (control block) | Safely returns nullptr | No (single sequence) | Async callbacks on non-shared_ptr objects |
| `std::weak_ptr<T>` | Yes (shared_ptr control) | Safely returns an empty shared_ptr | No | Async callbacks on shared_ptr-managed objects |

## Key Takeaways

- `std::weak_ptr<T>` depends on `shared_ptr`; `lock()` temporarily extends the object's lifetime
- Chrome-like `WeakPtr<T>` does not depend on `shared_ptr`, does not extend the object's lifetime, and only detects invalidation
- Do not force an object into `shared_ptr` management just so you can use a `weak_ptr`
- Never capture a raw `this`, a raw `T*`, an `ObserverPtr`, or an `UnsafeWeakPtr` in an async callback
- Chrome `WeakPtr` fits non-`shared_ptr` scenarios, but mind its sequence binding
- `std::weak_ptr` fits `shared_ptr` scenarios; `lock()` provides a stronger safety guarantee

## References

- [std::weak_ptr - cppreference](https://en.cppreference.com/w/cpp/memory/weak_ptr)
- [std::enable_shared_from_this - cppreference](https://en.cppreference.com/w/cpp/memory/enable_shared_from_this)
- [C++ Core Guidelines - CP.51: Do not use capturing lambdas that are coroutines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines)
- [Chromium WeakPtr design document](https://www.chromium.org/developers/weak-ptrs-in-chromium/)
