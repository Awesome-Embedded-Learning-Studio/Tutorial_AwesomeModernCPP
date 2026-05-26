---
title: 'A Panorama of Non-Owning Pointers: From T* to Borrowed to ObserverPtr'
description: Understanding the semantic boundaries of borrowing, observing, and non-owning
  pointers in C++, implementing `Borrowed<T>` and `ObserverPtr<T>` from scratch
chapter: 1
order: 1
tags:
- host
- cpp-modern
- intermediate
- 智能指针
- 内存管理
difficulty: intermediate
platform: host
reading_time_minutes: 20
prerequisites:
- 卷二 · 第一章：RAII 深入理解
- 卷二 · 第一章：weak_ptr 与循环引用
related:
- WeakPtr 反模式：T* + raw Flag* 的致命陷阱
cpp_standard:
- 17
- 20
translation:
  source: documents/vol8-domains/cpp-deep-dives/pointer-semantics/01-non-owning-pointer-overview.md
  source_hash: 8c3bc7304a09ebee4c8298b6323195b5156a7c9a74dbaf611519bc5a57509c4b
  translated_at: '2026-05-26T11:56:01.036850+00:00'
  engine: anthropic
  token_count: 2432
---
# The Non-Owning Pointer Landscape: From T* to Borrowed to ObserverPtr

## Introduction

I wonder if anyone else has had this experience: you open a project, navigate to a function as needed, and see ``T* ptr`` staring back at you in the parameter list. Then the second-guessing begins—does this pointer *own* the object, or is it just *borrowing* it? Does the caller need to check for `nullptr`? Is the object still alive after the function returns?

A raw pointer ``T*`` could be anything, and it promises nothing. It might be an owner (like that brief moment after ``new`` before handing it off to a smart pointer), a borrower (passed to a function for temporary use), or even a dangling pointer (the object is long gone, but the pointer remains). The compiler won't help you distinguish between these cases, and comments aren't always reliable (they might have been written by AI, after all).

C++ Core Guidelines rule R.3 puts it bluntly: **A raw pointer (a non-``owner<T>`` ``T*``) should only be used to indicate non-owning observation or borrowing**. But in real-world code, when we encounter a ``T*``, we have no way to tell what semantic intent it's supposed to convey.

So our goal today is clear: we will map out the various ways C++ expresses "not owning an object," and then we will hand-roll two types with explicit semantics—``Borrowed<T>`` and ``ObserverPtr<T>``—to let the code speak for itself.

Let's state the conclusion upfront: non-owning does not equal safe, and nullable does not equal able-to-check-liveness. Each of these types has its own applicable scenarios, and using the wrong one is worse than using a raw pointer.

## Core Concept: The Four-Layer Semantic Model

Before writing any code, we need to clarify one thing—how many distinct "non-owning" semantics actually exist in C++. We break them down into four layers:

**Layer 1: Borrowing.** ``T*`` and ``T&`` are the most primitive forms of borrowing. You receive a pointer or reference, use it, and give it back. You don't manage the object's lifetime, nor do you care when it gets destroyed. This is suitable for "short-lived, synchronous use" scenarios like function parameters, but you should never store them for later use. After all, a resource has no obligation to notify you when it blows up.

**Layer 2: Explicit Observation.** This is where more semantic clarity emerges. What we mean is—when we hold an ``ObserverPtr<T>``, we are simply saying: it is persisted, but we don't own it at all, and we have no way to know whether it has become invalid. "I am merely observing it; I know it exists. But I don't own it, and I can't guarantee whether it's actually usable." The difference from a raw pointer lies in **readability** (which might sound a bit underwhelming, ha): when you see ``ObserverPtr<T>``, you immediately know this is a pure observation relationship. However, just like ``T*``, it cannot check liveness—if the object is destroyed and you still hold the ObserverPtr, dereferencing it is UB.

**Layer 3: Non-owning Weak Reference.** This is the layer where ``WeakPtr<T>`` enters the picture. Its core difference from ObserverPtr is that after the object is destroyed, you can safely detect the invalidation. To achieve this, it requires a control block independent of the object to record "whether the object is still alive." But if you say, "I want to lock it to extend its lifetime"—well, you can't.

**Layer 4: Shared-ownership Weak Reference.** This is ``std::weak_ptr<T>``. The difference from Layer 3 is that it relies on the control block of a ``std::shared_ptr<T>``, and calling ``lock()`` temporarily extends the object's lifetime.

Now let's compare these four layers in a table:

| Feature | T* | T& | Borrowed\<T\> | ObserverPtr\<T\> | WeakPtr\<T\> | std::weak_ptr\<T\> |
|---------|----|----|---------------|-----------------|-------------|-------------------|
| Nullable | Yes | No | No (by design) | Yes | Yes | Yes |
| Owns object | No | No | No | No | No | No |
| Extends lifetime | No | No | No | No | No | lock() temporarily extends |
| Safe null check after destruction | No | No | No | No | **Yes** | **Yes** |
| Suitable for function parameters | Yes | Yes | **Recommended** | Okay | Too heavy | Too heavy |
| Suitable for class members | Okay but ambiguous | Okay | Not recommended | **Recommended** | Recommended | Recommended |
| Suitable for async callbacks | **Dangerous** | **Dangerous** | **Dangerous** | **Dangerous** | Yes | Yes |

⚠️ Pay close attention to this row—"Safe null check after destruction." The first four types (T*, T&, Borrowed, ObserverPtr) all fail at this. Only a WeakPtr with a truly independent control block can do it. We will dive into this in the second article; for now, just remember this conclusion.

## Hand-Rolling Borrowed\<T\>: Making Borrowing Semantics Explicit

The problem ``Borrowed<T>`` aims to solve is simple: when ``const T&`` or ``T*`` appears in a function parameter, callers and readers cannot tell at a glance that "this is just a borrow." We need a type to nail down the "non-null, non-owning, short-term use" semantics directly into the type system.

The ``gsl::not_null<T>`` in C++ Core Guidelines does something similar—it constrains the pointer to be non-null, but it doesn't express borrowing semantics. Our ``Borrowed<T>`` goes a step further: it is non-null, it is non-owning, and it **prohibits construction from temporaries**—because you cannot "borrow" something that is about to be destroyed.

Let's look at the core implementation:

```cpp
// borrowed.h
// 教学版 Borrowed<T>：显式非空借用语义
// 注意：这不是生产级实现，用于教学演示

#pragma once

#include <type_traits>
#include <utility>

template <typename T>
class Borrowed {
public:
    // 从左值引用构造——这是最正常的用法
    explicit Borrowed(T& ref) noexcept : ptr_(&ref) {}

    // 禁止从临时对象构造
    Borrowed(T&&) = delete;

    // 禁止从 nullptr 构造（T* 重载只接受非空指针）
    Borrowed(std::nullptr_t) = delete;

    // 从裸指针构造，但调用者需保证非空
    explicit Borrowed(T* ptr) noexcept : ptr_(ptr)
    {
        assert(ptr != nullptr && "Borrowed<T> requires a non-null pointer");
    }

    // 默认拷贝和移动——借用是可以传递的
    Borrowed(const Borrowed&) = default;
    Borrowed& operator=(const Borrowed&) = default;
    Borrowed(Borrowed&&) = default;
    Borrowed& operator=(Borrowed&&) = default;

    // 访问接口
    T& get() const noexcept { return *ptr_; }
    T* operator->() const noexcept { return ptr_; }
    T& operator*() const noexcept { return *ptr_; }

private:
    T* ptr_;
};

// 辅助函数：从引用创建 Borrowed，省去写 explicit 构造
template <typename T>
Borrowed<T> borrow(T& ref) noexcept
{
    return Borrowed<T>(ref);
}
```

Naturally, we will have a few questions:

**Why prohibit construction from temporaries?** This is the most critical difference between ``Borrowed<T>`` and a raw reference. Look at this scenario:

```cpp
std::string get_name();

// 如果允许从临时对象构造，就会出这种事：
// Borrowed<std::string> b(get_name());  // 临时对象在表达式结束时销毁
// 到这里，get_name返回的对象就被销毁掉了，这个时候访问持有的引用就是踩到地雷了
// b.get();  // 悬垂引用！
```

Once ``T&&`` is marked as ``= delete``, the compiler will outright reject this usage at compile time. This is the closest we can get in C++ to simulating Rust's borrow checker—though not as comprehensive as Rust's, it at least plugs the most common pitfall.

**Why is the constructor explicit?** To prevent implicit conversions. You wouldn't want a function accepting ``Borrowed<Foo>`` to be implicitly called from a ``Foo&``—the act of borrowing should be deliberate.

**Why is there a ``borrow()`` helper function?** Purely for convenience. Because the constructor is explicit, writing ``Borrowed<Foo>(foo)`` every time is a bit verbose, so ``borrow(foo)`` is cleaner. The standard library has similar designs, such as ``std::make_pair`` and ``std::make_shared``.

**Why not prohibit using it as a class member?** Technically it's possible (for example, through ``static_assert`` combined with SFINAE), but in practice, that's over-engineered. It's sufficient to establish a convention in our documentation and team norms that "Borrowed should not be stored as a class member." Between compiler enforcement and team conventions, we choose the latter—because C++'s type system is inherently bad at expressing lifetime constraints (otherwise, why would we be sitting here talking about this, using clumsy ways to express what we mean?). Forcing it would likely introduce unnecessary complexity.

A typical correct usage:

```cpp
void process_data(Borrowed<const std::vector<int>> data)
{
    // 调用者保证 data 非空，我们直接用
    for (const auto& item : data.get()) {
        // ...
    }
}

int main()
{
    std::vector<int> v{1, 2, 3};
    process_data(borrow(v));  // 清晰：我在借用 v
}
```

Compared to directly using ``const std::vector<int>&``, the advantage of the ``Borrowed`` version isn't in runtime behavior (they generate almost identical code), but in **readability**—the function signature directly tells you "this is a borrow."

## Hand-Rolling ObserverPtr\<T\>: A Nullable Non-Owning Observer

If ``Borrowed<T>`` is designed for function parameters, then ``ObserverPtr<T>`` is designed for class members. Its semantic meaning is "I observe this object, but I don't own it, and I'm not responsible for its lifetime."

In fact, the C++ standard committee once proposed a very similar type: ``std::experimental::observer_ptr<W>``, included in Library Fundamentals TS v2. Its definition is:

> A non-owning pointer, or observer. The observer stores a pointer to a second object, known as the watched object. An observer_ptr may also have no watched object.

Unfortunately, as of C++26 (I think it's 26, I haven't seen any new updates—if I'm wrong again, feel free to flame me), ``observer_ptr`` still hasn't been officially incorporated into the standard and remains at the TS stage. However, its design is very clear and worth referencing. Our teaching version will be a simplified take on it:

```cpp
// observer_ptr.h
// 教学版 ObserverPtr<T>：可空非拥有观察指针
// 参考了 std::experimental::observer_ptr (Library Fundamentals TS v2)

#pragma once

#include <cstddef>

template <typename T>
class ObserverPtr {
public:
    // 默认构造：空观察
    ObserverPtr() noexcept : ptr_(nullptr) {}

    // 从 nullptr 构造：空观察
    ObserverPtr(std::nullptr_t) noexcept : ptr_(nullptr) {}

    // 从裸指针构造：开始观察
    explicit ObserverPtr(T* ptr) noexcept : ptr_(ptr) {}

    // 拷贝和移动
    ObserverPtr(const ObserverPtr&) = default;
    ObserverPtr& operator=(const ObserverPtr&) = default;
    ObserverPtr(ObserverPtr&&) = default;
    ObserverPtr& operator=(ObserverPtr&&) = default;

    // 重新绑定观察对象
    void reset(T* ptr = nullptr) noexcept { ptr_ = ptr; }

    // 释放观察关系，返回原指针
    T* release() noexcept
    {
        T* old = ptr_;
        ptr_ = nullptr;
        return old;
    }

    // 访问
    T* get() const noexcept { return ptr_; }
    T& operator*() const noexcept { return *ptr_; }
    T* operator->() const noexcept { return ptr_; }

    // 检查是否有观察对象
    explicit operator bool() const noexcept { return ptr_ != nullptr; }

    // 交换
    void swap(ObserverPtr& other) noexcept
    {
        T* tmp = ptr_;
        ptr_ = other.ptr_;
        other.ptr_ = tmp;
    }

private:
    T* ptr_;
};

// 相等比较
template <typename T, typename U>
bool operator==(const ObserverPtr<T>& a, const ObserverPtr<U>& b) noexcept
{
    return a.get() == b.get();
}

template <typename T>
bool operator==(const ObserverPtr<T>& a, std::nullptr_t) noexcept
{
    return !a;
}

// 辅助函数
template <typename T>
ObserverPtr<T> make_observer(T* ptr) noexcept
{
    return ObserverPtr<T>(ptr);
}
```

**What is the difference between ObserverPtr and Borrowed?** The core difference comes down to two words: **nullable**. Borrowed expresses "I guarantee a non-null borrow," while ObserverPtr expresses "I might be a null observation." The former is suited for function parameters (where the caller guarantees non-null), and the latter is suited for persisted class members or storage members (where the observed object might not be set yet, or might be set to null).

**Why isn't ObserverPtr a WeakPtr?** This is the most common misconception. The difference between ObserverPtr and WeakPtr isn't about what their APIs look like (they both have ``get()``, ``operator->``, ``operator bool()``), but about **what happens after the object is destroyed**. Internally, ObserverPtr is just a raw pointer; if the object is destroyed, it knows nothing about it, and dereferencing it is UB. A true WeakPtr requires a control block independent of the object to record its alive status—but that's a topic for a future article I plan to submit to other Q&A sites and columns!

A typical correct usage—an observation relationship as a class member:

```cpp
class Logger;

class Service {
public:
    void set_logger(Logger* log) { logger_.reset(log); }

    void do_work()
    {
        if (logger_) {
            // 有 Logger 才记录，没有就算了
            // ...
        }
    }

private:
    ObserverPtr<Logger> logger_;  // 我观察 Logger，但不拥有它
};
```

A typical incorrect usage—an async callback:

```cpp
// 错误！ObserverPtr 不能保证对象还活着
void Service::async_task()
{
    // 如果 Service 在回调执行前被销毁，logger_ 就是悬垂的
    // 这个 callback 捕获了 logger_，执行时可能 UB
    auto callback = [this]() {
        if (logger_) { // 孩子们，这种东西很危险
            // logger_ 的 ptr_ 指向的 Logger 可能已经不存在了
            // operator bool 只检查 ptr_ 是否为 nullptr
            // 如果 Logger 被销毁但 ptr_ 没被 reset，这里就是 UB
        }
    };
    // post_callback(callback);  // 别这么做
}
```

## The Relationship Between Borrowed, ObserverPtr, and Raw Pointers

Now let's step back and clarify the relationship between these three types and raw pointers.

``Borrowed<T>`` is essentially a type-safe wrapper around ``T&``. It adds the "prohibits construction from temporaries" constraint compared to ``T&``, and adds the "non-null" guarantee compared to ``T*``. Its overhead is exactly zero—after compiler optimization, it is identical to a raw reference. Its limitations are also the same as a raw reference: **it cannot check liveness**.

``ObserverPtr<T>`` is essentially a semantic annotation on ``T*``. Its runtime behavior is completely identical to a raw pointer, and the only difference lies in readability—when you see a member variable of type ``ObserverPtr<Logger>``, you don't need to guess whether it owns that Logger; the type name has already answered for you. But likewise, **it cannot check liveness**.

The problem with a raw pointer ``T*`` isn't that it's "unsafe," but that it "doesn't state its intent"—when you receive a ``T*``, you don't know if it's owning or non-owning, nullable or guaranteed non-null, short-term or long-term. ``Borrowed`` and ``ObserverPtr`` solve this "doesn't state its intent" problem.

## Summary

Let's summarize the key takeaways from this article:

- **T\*** and **T&** are C++'s most primitive borrowing mechanisms and do not inherently express ownership semantics
- **Borrowed\<T\>** expresses a non-null borrow, is suitable for function parameters, prohibits construction from temporaries, and does not extend lifetimes
- **ObserverPtr\<T\>** expresses a nullable non-owning observation, is suitable for class members, and does not provide liveness-checking capabilities
- **Non-owning does not equal safe**—neither Borrowed nor ObserverPtr can safely detect invalidation after an object is destroyed
- Their core value lies in **semantic expression**, not runtime safety—letting the code speak for itself and reducing ambiguity

So far, we have only addressed the "borrowing" and "observation" semantic layers. The real trouble comes with "weak references"—when you need to safely hold a reference to an object that might be destroyed at any time, Borrowed and ObserverPtr simply aren't enough.

In the next article, we will dissect something that looks a lot like WeakPtr but actually isn't: ``T* + raw Flag*``.

## References

- [C++ Core Guidelines - R.3: A raw pointer (a T\*) is non-owning](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#Rr-ptr)
- [std::experimental::observer_ptr - cppreference](https://en.cppreference.com/cpp/experimental/observer_ptr)
- [GSL: Guidelines Support Library (Microsoft)](https://github.com/microsoft/GSL) — ``gsl::not_null`` and ``gsl::span``
- [C++ Core Guidelines - F.7: For general use, take T\* or T\& arguments rather than smart pointers](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#Rf-smartptrref)
