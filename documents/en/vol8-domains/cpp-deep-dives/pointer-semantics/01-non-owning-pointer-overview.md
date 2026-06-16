---
chapter: 1
cpp_standard:
- 17
- 20
description: Understanding the semantic boundaries of borrowing, observation, and
  non-owning pointers in C++, and implementing `Borrowed<T>` and `ObserverPtr<T>`
  from scratch
difficulty: intermediate
order: 1
platform: host
prerequisites:
- 卷二 · 第一章：RAII 深入理解
- 卷二 · 第一章：weak_ptr 与循环引用
reading_time_minutes: 13
related:
- WeakPtr 反模式：T* + raw Flag* 的致命陷阱
tags:
- host
- cpp-modern
- intermediate
- 智能指针
- 内存管理
title: 'Non-owning pointers panorama: From T* to Borrowed to ObserverPtr'
translation:
  source: documents/vol8-domains/cpp-deep-dives/pointer-semantics/01-non-owning-pointer-overview.md
  source_hash: bfc5024ee5a944b12488b05dc846b6e79b2abe5f47f8f404f5e5aa6465fd1d62
  translated_at: '2026-06-16T04:08:25.629721+00:00'
  engine: anthropic
  token_count: 2425
---
# Non-Owning Pointers Panorama: From T* to Borrowed to ObserverPtr

## Introduction

I wonder if anyone has had this experience: you pick up a project, open a function as needed, and see ``T* ptr`` prominently written in the parameter list. Then, you start muttering—does this pointer actually "own" the object, or is it just "borrowing" it? Does the caller need to check for `nullptr`? Is the object still alive after the function returns?

A raw pointer ``T*`` can be anything and promises nothing. It might be an owner (like that split second after ``new`` before it is handed to a smart pointer), a borrower (passed to a function for a quick use), or a dangling pointer (the object is long gone, but the pointer remains). The compiler won't help you distinguish, and comments aren't necessarily reliable (maybe the comment was written by an AI, after all).

There is a rule, R.3, in the C++ Core Guidelines that puts it very bluntly: **A raw pointer (a `T*` that is not ``owner<T>``) should only be used to indicate non-owning observation or borrowing**. However, in actual code, when we get a ``T*``, we simply cannot distinguish what semantics it is supposed to express.

So, our goal today is clear: we will sort out the various ways to express "not owning an object" in C++, and then hand-roll two types with clear semantics—``Borrowed<T>`` and ``ObserverPtr<T>``—to let the code speak for itself.

Let's put the conclusion first: non-owning does not equal safety, nullable does not mean you can determine if it's alive. Each type has its own use cases, and using them incorrectly is worse than using raw pointers.

## Core Concepts: The Four-Layer Semantic Model

Before writing code, we need to clarify one thing—how many semantics does "not owning" actually have in C++? Here, we divide it into four layers:

**Layer 1: Borrowing.** ``T*`` and ``T&`` are the most primitive forms of borrowing. You get a pointer or reference, use it, and give it back. You don't manage the object's lifecycle, nor do you care when it is destroyed. This is suitable for scenarios like function parameters where usage is "brief and synchronous," but never save it for later use. After all, the resource is under no obligation to tell you when it blows up—please look elsewhere.

**Layer 2: Explicit Observation.** Starting here, we have more semantic clarity. What I mean is—when we hold a ``ObserverPtr<T>``, we are simply saying—although it is persisted, we don't own it at all, and we may not even know if it has become invalid. "I am just observing it; I know it exists. But I don't own it, or rather, I can't guarantee whether it is usable." The difference from a raw pointer lies in **readability** (sounds a bit useless, haha): seeing ``ObserverPtr<T>`` tells you this is a pure observation relationship. However, like ``T*``, it cannot determine liveness—if the object is destroyed and you are still holding an ObserverPtr, dereferencing it is UB.

**Layer 3: Non-owning Weak Reference.** This is where ``WeakPtr<T>`` comes in. Its core difference from ObserverPtr is: after the object is destroyed, you can safely detect the failure. To do this, it needs a control block independent of the object to record "whether the object is still alive." However, if you want to `lock` it and extend its lifecycle, well, you can't.

**Layer 4: Shared Ownership Weak Reference.** This is ``std::weak_ptr<T>``. The difference from the third layer is that it relies on the ``std::shared_ptr<T>`` control block, and calling ``lock()`` temporarily extends the object's lifecycle.

Now, let's use a table to compare these four layers:

| Feature | T* | T& | Borrowed\<T\> | ObserverPtr\<T\> | WeakPtr\<T\> | std::weak_ptr\<T\> |
|---------|----|----|---------------|-----------------|-------------|-------------------|
| Nullable | Yes | No | No (by design) | Yes | Yes | Yes |
| Owns Object | No | No | No | No | No | No |
| Extend Lifecycle | No | No | No | No | No | lock() temporarily extends |
| Safe Null Check After Destruction | No | No | No | No | **Yes** | **Yes** |
| Suitable for Function Parameters | Yes | Yes | **Recommended** | Okay | Too Heavy | Too Heavy |
| Suitable for Class Members | Okay but ambiguous | Okay | Not Recommended | **Recommended** | Recommended | Recommended |
| Suitable for Async Callbacks | **Dangerous** | **Dangerous** | **Dangerous** | **Dangerous** | Yes | Yes |

⚠️ Note this row—"Safe Null Check After Destruction". The first four types (T*, T&, Borrowed, ObserverPtr) cannot do this. Only a WeakPtr with a truly independent control block can. We will expand on this in the second article; for now, just remember this conclusion.

## Hand-rolling Borrowed\<T\>: Making Borrowing Semantics Explicit

The problem ``Borrowed<T>`` wants to solve is simple: when ``const T&`` or ``T*`` appears in function parameters, the caller and the reader cannot immediately tell that "this is just a borrow." We need a type to nail the semantics of "non-null, non-owning, short-term use" into the type system.

The ``gsl::not_null<T>`` in C++ Core Guidelines does something similar—it constrains the pointer to be non-null but doesn't express borrowing semantics. Our ``Borrowed<T>`` goes a step further: it is non-null, it is non-owning, and it **prohibits construction from temporary objects**—because you cannot "borrow" something that is about to be destroyed.

Let's look at the core implementation first:

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

Obviously, we will have these questions:

**Why prohibit construction from temporary objects?** This is the most critical difference between ``T&&`` and a raw reference. Look at this scenario:

```cpp
std::string get_name();

// 如果允许从临时对象构造，就会出这种事：
// Borrowed<std::string> b(get_name());  // 临时对象在表达式结束时销毁
// 到这里，get_name返回的对象就被销毁掉了，这个时候访问持有的引用就是踩到地雷了
// b.get();  // 悬垂引用！
```

After ``T&&`` is marked as ``= delete``, the compiler will refuse this usage at compile time. This is the closest simulation we can get in C++ to Rust's borrow checker—although not as comprehensive as Rust, it at least blocks the most common pitfall.

**Why is the constructor explicit?** To prevent implicit conversion. You wouldn't want a function accepting ``Borrowed<Foo>`` to be called implicitly from ``Foo&``—the act of borrowing should be conscious.

**Why is there a ``borrow()`` helper function?** Purely for convenience. Since the constructor is `explicit`, writing ``Borrowed<Foo>(foo)`` every time is a bit verbose, and ``borrow(foo)`` is cleaner. The standard library has similar designs, such as ``std::make_pair`` and ``std::make_shared``.

**Why not prohibit it as a class member?** Technically it can be done (e.g., via ``static_assert`` plus SFINAE), but practically it is over-engineering. It is sufficient for us to agree in documentation and convention that "Borrowed should not be saved as a class member." Between compiler enforcement and team norms, we choose the latter—because C++'s type system is not good at expressing lifetime constraints anyway (otherwise, why would we sit here and talk about this, using clumsy ways to express our meaning?), and forcing it tends to introduce unnecessary complexity.

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

Compared to directly using ``const std::vector<int>&``, the advantage of the ``Borrowed`` version lies not in runtime behavior (they generate almost identical code), but in **readability**—the function signature tells you directly "this is a borrow."

## Hand-rolling ObserverPtr\<T\>: A Nullable Non-Owning Observer

If ``Borrowed<T>`` is for function parameters, then ``ObserverPtr<T>`` is for class members. Its semantics are "I am observing this object, but I don't own it, and I am not responsible for its lifecycle."

In fact, the C++ Standard Committee once proposed a very similar type: ``std::experimental::observer_ptr<W>``, included in Library Fundamentals TS v2. Its definition is:

> A non-owning pointer, or observer. The observer stores a pointer to a second object, known as the watched object. An observer_ptr may also have no watched object.

Unfortunately, as of C++26 (seems to be 26, I haven't found new news, if I got it wrong, feel free to roast me), ``observer_ptr`` has not yet been officially incorporated into the standard and remains at the TS stage. However, its design is very clear and worth referencing. Our teaching version will be a simplification based on it:

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

**What is the difference between ObserverPtr and Borrowed?** The core difference lies in two words: **nullable**. Borrowed expresses "I guarantee a non-null borrow," while ObserverPtr expresses "I might be a nullable observation." The former is suitable for function parameters (the caller guarantees non-null), while the latter is suitable for persisted class members or storage members (the observed object might not be set yet, or might be set to null).

**Why isn't ObserverPtr a WeakPtr?** This is the most common misunderstanding. The difference between ObserverPtr and WeakPtr is not what the API looks like (they both have `get()`, `reset()`, `operator->`), but **what happens after the object is destroyed**. Inside ObserverPtr is just a raw pointer; when the object is destroyed, it knows nothing, and dereferencing is UB. A true WeakPtr needs a control block independent of the object to record the liveness state—this is something the author plans to submit to other questions and columns in future articles!

Typical correct usage—class member observation relationship:

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

Typical incorrect usage—asynchronous callback:

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

Now, looking back, let's clarify the relationship between these three types and raw pointers.

``Borrowed<T>`` is essentially a type-safe wrapper for ``T&``. It adds the constraint of "prohibiting construction from temporary objects" compared to ``T&``, and adds the guarantee of "non-null" compared to ``T*``. Its overhead is zero—after compiler optimization, it is exactly the same as a raw reference. Its limitations are also the same as a raw reference: **it cannot determine liveness**.

``ObserverPtr<T>`` is essentially a semantic label for ``T*``. Its runtime behavior is identical to a raw pointer; the difference is only readability—when you see a member variable of type ``ObserverPtr<Logger>``, you don't need to guess whether it owns that Logger; the type name has already answered for you. But similarly, **it cannot determine liveness**.

The problem with the raw pointer ``T*`` is not that it is "unsafe," but that it is "non-committal"—when you get a ``T*``, you don't know if it is owning or non-owning, nullable or guaranteed non-null, short-term or long-term. ``Borrowed`` and ``ObserverPtr`` solve this "non-committal" problem.

## Summary

Let's summarize the key points of this article:

- **T\*** and **T&** are C++'s most primitive borrowing mechanisms and do not express ownership semantics themselves.
- **Borrowed\<T\>** expresses non-null borrowing, is suitable for function parameters, prohibits construction from temporary objects, and does not extend the object's lifecycle.
- **ObserverPtr\<T\>** expresses nullable non-owning observation, is suitable for class members, and does not provide the ability to check for liveness.
- **Non-owning does not equal safety**—Borrowed and ObserverPtr cannot safely detect failure after the object is destroyed.
- Their core value is **semantic expression**, not runtime safety—let the code speak for itself and reduce ambiguity.

Here, we have only solved the two semantic layers of "borrowing" and "observation." The real trouble is "weak reference"—when you need to safely hold a reference to an object that might be destroyed at any time, relying solely on Borrowed and ObserverPtr is not enough.

In the next article, we will dissect something that looks like WeakPtr but actually isn't: ``T* + raw Flag*``.

## Reference Resources

- [C++ Core Guidelines - R.3: A raw pointer (a T\*) is non-owning](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#Rr-ptr)
- [std::experimental::observer_ptr - cppreference](https://en.cppreference.com/cpp/experimental/observer_ptr)
- [GSL: Guidelines Support Library (Microsoft)](https://github.com/microsoft/GSL) — ``gsl::not_null`` and ``gsl::span``
- [C++ Core Guidelines - F.7: For general use, take T\* or T\& arguments rather than smart pointers](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#Rf-smartptrref)
