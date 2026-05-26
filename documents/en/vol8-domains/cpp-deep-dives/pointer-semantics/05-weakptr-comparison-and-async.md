---
title: '`std::weak_ptr` Comparison and Practical Async Callbacks'
description: 'Comparing `std::weak_ptr` with Chrome WeakPtr: a safety analysis of
  six asynchronous callback capture patterns'
chapter: 1
order: 5
tags:
- host
- cpp-modern
- advanced
- 智能指针
- 异步编程
- 回调机制
difficulty: advanced
platform: host
reading_time_minutes: 8
prerequisites:
- Chrome-like WeakPtr：引用计数控制块与 WeakPtrFactory
- 卷二 · 第一章：weak_ptr 与循环引用
related:
- 跨线程安全、性能取舍与设计原则总结
cpp_standard:
- 17
- 20
translation:
  source: documents/vol8-domains/cpp-deep-dives/pointer-semantics/05-weakptr-comparison-and-async.md
  source_hash: e71aa17f860345c3ebdc18ef482f08e7eec3aa5e6fc918fc53381f07184bd56d
  translated_at: '2026-05-26T11:56:10.444261+00:00'
  engine: anthropic
  token_count: 1619
---
# std::weak_ptr vs. Chrome WeakPtr and Async Callback Patterns in Practice

## Introduction

In the previous four articles, we built a non-owning pointer type from scratch—ranging from Borrowed to ObserverPtr to various WeakPtr implementations. Now it is time to bring everything together for a side-by-side comparison.

In this article, we will do two things: first, we will place `std::weak_ptr<T>` and Chrome-like `WeakPtr<T>` side by side to clarify their core differences; second, we will use six async callback capture patterns for a practical comparison, giving you an intuitive feel for the difference between "incorrect capture" and "correct capture."

## Core Differences Between std::weak_ptr and Chrome WeakPtr

Let us start with a frequently overlooked fact: **`std::weak_ptr<T>` and Chrome-like `WeakPtr<T>` do not solve the same problem.**

`std::weak_ptr<T>` solves the problem of "weak references in a shared ownership model." It relies on the `std::shared_ptr<T>` control block. After a successful call to `lock()`, you obtain a `shared_ptr<T>`, thereby **temporarily extending the object's lifetime**. This means that as long as your `lock()` succeeds, the object is guaranteed not to be destroyed while your `shared_ptr` is alive.

Chrome-like `WeakPtr<T>` solves the problem of "weak references on objects not managed by shared_ptr." It does not rely on `shared_ptr`. Calling `get()` does not extend the object's lifetime—it simply returns a pointer. The object might be destroyed at any time, and the pointer you receive might be invalid before you even use it. It only guarantees that you can **safely detect invalidation**, not that the object will still be alive after you obtain the pointer.

These are two completely different lifetime strategies:

| Feature | Chrome-like WeakPtr\<T\> | std::weak_ptr\<T\> |
|---------|-------------------------|-------------------|
| Depends on shared_ptr | No | Yes |
| Extends lifetime when acquiring reference | **No** | **Yes** (lock returns shared_ptr) |
| Safe null check after object destruction | Yes | Yes |
| Suitable for non-shared_ptr managed objects | **Yes** | No |
| Naturally thread-safe | No (sequence-bound) | Partial (lock() is atomic, but accessing T requires synchronization) |
| Control block overhead | Small (custom ref count) | Larger (shared_ptr control block) |

**When should we use `std::weak_ptr`?** When the object is already managed by `shared_ptr`, and you need to safely observe it in asynchronous scenarios, potentially requiring a temporary lifetime extension.

**When should we use Chrome-like WeakPtr?** When the object is not managed by `shared_ptr` (stack objects, `unique_ptr`, framework-managed objects), and you need to safely detect invalidation in asynchronous callbacks.

**When should we NOT use `std::weak_ptr`?** When you forcibly change an object to `shared_ptr` management just to use `weak_ptr`. This introduces unnecessary reference counting overhead and easily causes performance bottlenecks in multithreaded environments (atomic reference count contention).

## Six Async Callback Capture Patterns

Next, we will use actual code to compare six ways of capturing object references in asynchronous callbacks. For each pattern, we will analyze: where the danger lies, what happens after the object is destroyed, and whether it constitutes UB.

### Pattern 1: Capturing a raw `this` — Dangerous

```cpp
class NetworkClient {
public:
    void start_request()
    {
        // 错误！lambda 捕获了裸 this
        timer_.schedule(1000ms, [this]() {
            process_response();  // 如果 NetworkClient 已析构，this 是悬垂指针
        });
    }

    void process_response() { /* ... */ }

private:
    Timer timer_;
};

// 使用场景
void test()
{
    auto client = std::make_unique<NetworkClient>();
    client->start_request();
    // client 在这里析构
}  // 1 秒后回调执行 → this 悬垂 → UB
```

**Problem**: `this` is just a raw pointer that carries no lifetime information. After the object is destroyed, the `this` in the callback is a dangling pointer, and any member access is UB. This is the most common crash source in C++ asynchronous programming.

### Pattern 2: Capturing `T*` — Equally Dangerous

```cpp
void start_request()
{
    auto* raw_ptr = this;
    timer_.schedule(1000ms, [raw_ptr]() {
        raw_ptr->process_response();  // 同样的悬垂问题
    });
}
```

**Problem**: There is no fundamental difference from capturing a raw `this`. `T*` does not provide any lifetime guarantees. The only difference is that it "looks" like a conscious decision to capture a pointer, but it is practically no safer than a raw `this`.

### Pattern 3: Capturing `ObserverPtr<T>` — Still Dangerous

```cpp
void start_request()
{
    auto obs = make_observer(this);
    timer_.schedule(1000ms, [obs]() {
        if (obs) {
            obs->process_response();  // ObserverPtr::operator bool 只检查是否为 nullptr
        }                            // 对象销毁后 obs.get() 仍非 nullptr → 悬垂解引用
    });
}
```

**Problem**: The `operator bool()` of `ObserverPtr` only checks whether the internal pointer is `nullptr`. After the object is destroyed, the internal pointer is not `nullptr` (it is dangling), so `if (obs)` will pass, and then the dangling pointer gets dereferenced. UB.

### Pattern 4: Capturing `UnsafeWeakPtr<T>` — UB

```cpp
void start_request()
{
    auto weak = get_unsafe_weak_ptr();
    timer_.schedule(1000ms, [weak]() {
        if (weak.is_valid()) {  // 访问已销毁的 Flag → UB！
            // ...
        }
    });
}
```

**Problem**: As analyzed in detail in the second article, the `Flag*` accessed by `is_valid()` might already be a dangling pointer. The null-check action itself is UB. This is the most insidious danger among the six patterns—it appears to have a "liveness check" mechanism, but even the check itself is unsafe.

### Pattern 5: Capturing Chrome-like `WeakPtr<T>` — Correct

```cpp
class NetworkClient {
public:
    void start_request()
    {
        auto weak = factory_.get_weak_ptr();
        timer_.schedule(1000ms, [weak]() {
            if (auto* self = weak.get()) {
                self->process_response();  // 安全：get() 先检查 control block
            }                             // 失效时返回 nullptr，不会解引用
        });
    }

private:
    Timer timer_;
    WeakPtrFactory<NetworkClient> factory_{this};
};
```

**Analysis**: `weak.get()` first checks `WeakFlag::is_valid()`. Since `WeakFlag` is reference-counted, as long as `weak` is alive, `WeakFlag` is guaranteed to exist, so `is_valid()` will not be UB. After the object is destroyed, the Factory's destructor will invalidate `WeakFlag`, `get()` returns `nullptr`, and the callback safely skips execution.

**But there is a prerequisite**: The callback's execution and the object's destruction must be on the same sequence. If crossing sequences, after `get()` returns non-null but before actually using `self`, another sequence might be destroying the object—this is a TOCTOU race condition.

### Pattern 6: Capturing `std::weak_ptr<T>` — Correct

```cpp
class NetworkClient : public std::enable_shared_from_this<NetworkClient> {
public:
    void start_request()
    {
        auto weak = weak_from_this();  // C++17
        timer_.schedule(1000ms, [weak]() {
            if (auto self = weak.lock()) {
                self->process_response();  // lock() 成功 → shared_ptr 延长生命周期
            }                             // 在 self 的作用域内，对象不会被析构
        });
    }

private:
    Timer timer_;
};

// 使用时必须用 shared_ptr 管理
auto client = std::make_shared<NetworkClient>();
client->start_request();
```

**Analysis**: `weak.lock()` is an atomic operation—it either returns a valid `shared_ptr` (while incrementing the reference count by one) or returns empty. If it returns a valid `shared_ptr`, the object is guaranteed not to be destroyed while your `self` variable is alive. This is safer than Chrome WeakPtr—it not only detects invalidation but also prevents the object from being destroyed between the check and actual use.

**But the cost is**: The object must be managed by `shared_ptr`, and `lock()` introduces atomic reference count operations. In high-frequency asynchronous scenarios, these atomic operations can become a performance bottleneck.

## Summary of the Six Patterns

| Pattern | Liveness Check | Behavior After Object Destruction | UB? | Suitable Scenario |
|---------|----------------|-----------------------------------|-----|-------------------|
| Raw `this` | None | Dangling pointer access | Yes | None—never capture a raw this in async callbacks |
| `T*` | None | Dangling pointer access | Yes | None—same as above |
| `ObserverPtr<T>` | None | `operator bool` passes but pointer is dangling | Yes | Synchronous observation, not for async callbacks |
| `UnsafeWeakPtr<T>` | Fake | Null check itself is UB | Yes | None—should not be used |
| Chrome `WeakPtr<T>` | Yes (control block) | Safely returns nullptr | No (single sequence) | Async callbacks for non-shared_ptr objects |
| `std::weak_ptr<T>` | Yes (shared_ptr control) | Safely returns empty shared_ptr | No | Async callbacks for shared_ptr managed objects |

## Summary

- `std::weak_ptr<T>` relies on `shared_ptr`, and `lock()` temporarily extends the object's lifetime
- Chrome-like `WeakPtr<T>` does not rely on `shared_ptr`, does not extend the object's lifetime, and only detects invalidation
- Do not forcibly change an object to `shared_ptr` management just to use `weak_ptr`
- Never capture a raw `this`, raw `T*`, `ObserverPtr`, or `UnsafeWeakPtr` in asynchronous callbacks
- Chrome `WeakPtr` is suitable for non-`shared_ptr` scenarios, but be mindful of sequence binding
- `std::weak_ptr` is suitable for `shared_ptr` scenarios, and `lock()` provides stronger safety guarantees

## Resources

- [std::weak_ptr - cppreference](https://en.cppreference.com/w/cpp/memory/weak_ptr)
- [std::enable_shared_from_this - cppreference](https://en.cppreference.com/w/cpp/memory/enable_shared_from_this)
- [C++ Core Guidelines - CP.51: Do not use capturing lambdas that are coroutines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines)
- [Chromium WeakPtr Design Document](https://www.chromium.org/developers/weak-ptrs-in-chromium/)
