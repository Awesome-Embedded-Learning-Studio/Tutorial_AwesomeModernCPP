---
chapter: 1
cpp_standard:
- 17
- 20
description: Understand the semantic boundaries of borrowing, observation, and non-owning pointers in C++, and hand-roll Borrowed<T> and ObserverPtr<T>
difficulty: intermediate
order: 1
platform: host
prerequisites:
- 'Volume 2 · Chapter 1: Deep Dive into RAII: The Cornerstone of Resource Management'
- 'Volume 2 · Chapter 1: weak_ptr and Circular References: Breaking the Ownership Deadlock'
reading_time_minutes: 13
related:
- 'WeakPtr Anti-Pattern: The Fatal Trap of T* + raw Flag*'
tags:
- host
- cpp-modern
- intermediate
- 智能指针
- 内存管理
title: 'Non-Owning Pointers: A Panorama from T* to Borrowed to ObserverPtr'
translation:
  source: documents/vol8-domains/cpp-deep-dives/pointer-semantics/01-non-owning-pointer-overview.md
  source_hash: 03111f3329d1f66c018ac3f0b937826d767efb2f816966e09316f6c7edc12d0d
  translated_at: '2026-09-27T02:43:50+00:00'
  engine: anthropic
  token_count: 6900
---
# Non-Owning Pointers: A Panorama from T* to Borrowed to ObserverPtr

I'm curious — has anyone else been through this? You pick up a project, open whichever function you happen to need, and there in the parameter list sits `T* ptr`, and you start muttering to yourself: does this pointer *own* the object, or is it just *borrowing* it? Is the caller supposed to check for nullptr? Will the object still be alive after the function returns?

A raw pointer `T*` could be anything and promises nothing. It might be the owner (say, in that instant after `new` but before the object is handed off to a smart pointer), a borrower (passed into a function for a quick use), or a dangling pointer (the object is long gone while the pointer lingers on). The compiler won't help you tell them apart, and comments aren't necessarily reliable either (for all you know, the comments were written by an AI).

R.3 in the C++ Core Guidelines puts it bluntly: **a raw pointer (a `T*` that is not an `owner<T>`) should be used only to express non-owning observation or borrowing**. Yet in real code, when we're handed a `T*`, we simply cannot tell which semantics it is meant to convey.

So today's task is clear: survey the various ways C++ can express "does not own the object," then hand-roll two semantically explicit types — `Borrowed<T>` and `ObserverPtr<T>` — and let the code speak for itself.

Conclusion first: non-owning does not mean safe, and nullable does not mean able to test for liveness. Each of these types has its own niche, and using the wrong one digs you into a deeper hole than a raw pointer would.

## Core Concept: A Four-Layer Semantic Model

Before writing any code, we need to sort one thing out — how many distinct semantics "not owning" actually covers in C++. We'll split it into four layers:

**Layer 1: Borrowing.** `T*` and `T&` are the most primitive form of borrowing. You take a pointer or reference, use it, hand it back, and neither manage the object's lifetime nor care when it gets destroyed. This suits "brief, synchronous use" scenarios such as function parameters — but whatever you do, don't store it away for later. After all, the resource is under no obligation to inform you, "our resource here has just blown up; please seek employment elsewhere."

**Layer 2: Explicit Observation.** From here on, we get more semantic clarity. What I mean is — when we hold an `ObserverPtr<T>`, all we're trying to say is: yes, it's persisted, but we don't own it in the slightest, and we have no way of knowing whether it has expired. "I'm just observing it. I acknowledge the thing exists. But I don't own it, and I can offer zero guarantees about whether it's still usable." The difference from a raw pointer is **readability** (which sounds a bit underwhelming, haha): when you see `ObserverPtr<T>`, you know this is a pure observation relationship. But just like `T*`, it cannot test for liveness — if the object is destroyed while you're still holding the ObserverPtr, dereferencing it is UB.

**Layer 3: Non-owning weak reference.** This is the layer where `WeakPtr<T>` enters the stage. Its core difference from ObserverPtr: after the object is destroyed, you can safely detect the expiration. For that, it needs a control block, independent of the object, that records "is the object still alive." But — you say — I also want to lock it and extend its lifetime. Er, no can do.

**Layer 4: Weak reference into shared ownership.** That's `std::weak_ptr<T>`. It differs from layer 3 in that it relies on the control block of `std::shared_ptr<T>`, and calling `lock()` temporarily extends the object's lifetime.

Now let's compare the four layers in a table:

| Feature | T* | T& | Borrowed\<T\> | ObserverPtr\<T\> | WeakPtr\<T\> | std::weak_ptr\<T\> |
|---------|----|----|---------------|-----------------|-------------|-------------------|
| Nullable | Yes | No | No (by design) | Yes | Yes | Yes |
| Owns the object | No | No | No | No | No | No |
| Extends lifetime | No | No | No | No | No | Temporarily via lock() |
| Safe null-check after object destruction | No | No | No | No | **Yes** | **Yes** |
| Suited to function parameters | Yes | Yes | **Recommended** | Acceptable | Too heavyweight | Too heavyweight |
| Suited to class members | Possible, but unclear | Possible | Not recommended | **Recommended** | Recommended | Recommended |
| Suited to async callbacks | **Dangerous** | **Dangerous** | **Dangerous** | **Dangerous** | Yes | Yes |

⚠️ Look closely at that row — "safe null-check after object destruction." The first four types (T*, T&, Borrowed, ObserverPtr) all fail it. Only a WeakPtr that truly owns an independent control block can do it. We'll unpack this in part 2; for now, just remember the conclusion.

## Hand-Rolling Borrowed\<T\>: Making Borrow Semantics Explicit

The problem `Borrowed<T>` wants to solve is simple: when a function parameter is `const T&` or `T*`, neither the caller nor the reader can see at a glance that "this is merely a borrow." We need a type that nails "non-null, non-owning, short-term use" down in the type system.

The C++ Core Guidelines' `gsl::not_null<T>` does something similar — it constrains the pointer to be non-null, but expresses no borrowing semantics. Our `Borrowed<T>` goes one step further: it is non-null, it is non-owning, and it **forbids construction from temporaries** — because you cannot "borrow" something that is about to be destroyed.

First, the core implementation:

```cpp
// borrowed.h
// Teaching-version Borrowed<T>: explicit non-null borrowing semantics
// Note: this is not a production-grade implementation; it is for teaching

#pragma once

#include <type_traits>
#include <utility>

template <typename T>
class Borrowed {
public:
    // Construct from an lvalue reference — the most ordinary usage
    explicit Borrowed(T& ref) noexcept : ptr_(&ref) {}

    // Forbid construction from temporaries
    Borrowed(T&&) = delete;

    // Forbid construction from nullptr (the T* overload accepts only non-null pointers)
    Borrowed(std::nullptr_t) = delete;

    // Construct from a raw pointer; the caller must guarantee non-null
    explicit Borrowed(T* ptr) noexcept : ptr_(ptr)
    {
        assert(ptr != nullptr && "Borrowed<T> requires a non-null pointer");
    }

    // Default copy and move — a borrow is transferable
    Borrowed(const Borrowed&) = default;
    Borrowed& operator=(const Borrowed&) = default;
    Borrowed(Borrowed&&) = default;
    Borrowed& operator=(Borrowed&&) = default;

    // Access interface
    T& get() const noexcept { return *ptr_; }
    T* operator->() const noexcept { return ptr_; }
    T& operator*() const noexcept { return *ptr_; }

private:
    T* ptr_;
};

// Helper: create a Borrowed from a reference, sparing you the explicit constructor
template <typename T>
Borrowed<T> borrow(T& ref) noexcept
{
    return Borrowed<T>(ref);
}
```

Obviously, a few questions come up:

**Why forbid construction from temporaries?** This is the most crucial difference between `Borrowed<T>` and a raw reference. Consider this scenario:

```cpp
std::string get_name();

// If construction from temporaries were allowed, this would happen:
// Borrowed<std::string> b(get_name());  // the temporary is destroyed at the end of the expression
// By this point the object returned by get_name has been destroyed,
// and accessing the held reference here means stepping on a landmine
// b.get();  // dangling reference!
```

With `T&&` marked `= delete`, the compiler rejects this usage outright at compile time. This is the closest imitation of Rust's borrow checker that C++ can offer — not as comprehensive as Rust's, but it at least plugs the most common pitfall.

**Why are the constructors explicit?** To prevent implicit conversions. You don't want a function taking `Borrowed<Foo>` to be implicitly callable from a `Foo&` — the act of borrowing should be deliberate.

**Why is there a `borrow()` helper?** Purely for convenience. Since the constructor is explicit, writing `Borrowed<Foo>(foo)` every time is a bit of a mouthful; `borrow(foo)` is cleaner. The standard library has similar designs, such as `std::make_pair` and `std::make_shared`.

**Why not forbid it as a class member?** Technically it's doable (say, via `static_assert` plus SFINAE), but in practice that's over-engineering. Agreeing in documentation and convention that "Borrowed should not be stored as a class member" is enough. Between compiler enforcement and team convention, we choose the latter — because C++'s type system was never good at expressing lifetime constraints in the first place (otherwise, why would we be sitting down to have this conversation, expressing ourselves in such clumsy ways?), and forcing it tends to introduce unnecessary complexity.

A typical correct usage:

```cpp
void process_data(Borrowed<const std::vector<int>> data)
{
    // The caller guarantees data is non-null; use it directly
    for (const auto& item : data.get()) {
        // ...
    }
}

int main()
{
    std::vector<int> v{1, 2, 3};
    process_data(borrow(v));  // Clear: I'm borrowing v
}
```

Compared with using `const std::vector<int>&` directly, the `Borrowed` version's advantage is not in runtime behavior (the generated code is nearly identical) but in **readability** — the function signature tells you outright, "this is a borrow."

## Hand-Rolling ObserverPtr\<T\>: A Nullable Non-Owning Observer

If `Borrowed<T>` is meant for function parameters, then `ObserverPtr<T>` is meant for class members. Its semantics are "I observe this object, but I don't own it, and I'm not responsible for its lifetime."

In fact, the C++ standards committee once proposed a remarkably similar type: `std::experimental::observer_ptr<W>`, included in Library Fundamentals TS v2. Its definition:

> A non-owning pointer, or observer. The observer stores a pointer to a second object, known as the watched object. An observer_ptr may also have no watched object.

Unfortunately, as of C++26 (26, I believe — I haven't found any newer news, and if I've got it wrong again, feel free to flame me), `observer_ptr` still has not been formally adopted into the standard and remains at the TS stage. But its design is very clean and worth studying. Our teaching version simplifies on top of it:

```cpp
// observer_ptr.h
// Teaching-version ObserverPtr<T>: a nullable, non-owning observation pointer
// Modeled on std::experimental::observer_ptr (Library Fundamentals TS v2)

#pragma once

#include <cstddef>

template <typename T>
class ObserverPtr {
public:
    // Default construction: observing nothing
    ObserverPtr() noexcept : ptr_(nullptr) {}

    // Construct from nullptr: observing nothing
    ObserverPtr(std::nullptr_t) noexcept : ptr_(nullptr) {}

    // Construct from a raw pointer: start observing
    explicit ObserverPtr(T* ptr) noexcept : ptr_(ptr) {}

    // Copy and move
    ObserverPtr(const ObserverPtr&) = default;
    ObserverPtr& operator=(const ObserverPtr&) = default;
    ObserverPtr(ObserverPtr&&) = default;
    ObserverPtr& operator=(ObserverPtr&&) = default;

    // Rebind the watched object
    void reset(T* ptr = nullptr) noexcept { ptr_ = ptr; }

    // Release the observation relationship, returning the original pointer
    T* release() noexcept
    {
        T* old = ptr_;
        ptr_ = nullptr;
        return old;
    }

    // Access
    T* get() const noexcept { return ptr_; }
    T& operator*() const noexcept { return *ptr_; }
    T* operator->() const noexcept { return ptr_; }

    // Check whether there is a watched object
    explicit operator bool() const noexcept { return ptr_ != nullptr; }

    // Swap
    void swap(ObserverPtr& other) noexcept
    {
        T* tmp = ptr_;
        ptr_ = other.ptr_;
        other.ptr_ = tmp;
    }

private:
    T* ptr_;
};

// Equality comparison
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

// Helper function
template <typename T>
ObserverPtr<T> make_observer(T* ptr) noexcept
{
    return ObserverPtr<T>(ptr);
}
```

**What separates ObserverPtr from Borrowed?** The core difference comes down to one word: **nullability**. Borrowed expresses "a borrow I guarantee to be non-null"; ObserverPtr expresses "an observation that may be null." The former suits function parameters (the caller guarantees non-null); the latter suits persisted class members or storage members (the watched object may not have been set yet, or may have been set to null).

**Why isn't ObserverPtr a WeakPtr?** That's the most common misconception. The difference between ObserverPtr and WeakPtr is not what the API looks like (both have `get()`, `operator->`, `operator bool()`) but **what happens after the object is destroyed**. Inside, ObserverPtr is just a raw pointer; when the object is destroyed it knows nothing about it, and dereferencing is UB. A true WeakPtr needs a control block independent of the object to record liveness — and that's a topic for articles I plan to submit to other Q&As and columns later.

A typical correct usage — a class-member observation relationship:

```cpp
class Logger;

class Service {
public:
    void set_logger(Logger* log) { logger_.reset(log); }

    void do_work()
    {
        if (logger_) {
            // Log only if there is a Logger; otherwise skip it
            // ...
        }
    }

private:
    ObserverPtr<Logger> logger_;  // I observe the Logger, but I don't own it
};
```

A typical wrong usage — async callbacks:

```cpp
// Wrong! ObserverPtr cannot guarantee the object is still alive
void Service::async_task()
{
    // If Service is destroyed before the callback executes, logger_ is dangling
    // This callback captures logger_; running it may be UB
    auto callback = [this]() {
        if (logger_) { // Folks, this kind of thing is dangerous
            // The Logger that logger_'s ptr_ points to may no longer exist
            // operator bool only checks whether ptr_ is nullptr
            // If Logger was destroyed but ptr_ was never reset, this is UB
        }
    };
    // post_callback(callback);  // Don't do this
}
```

## How Borrowed, ObserverPtr, and Raw Pointers Relate

Now let's look back and spell out how these three types relate to raw pointers.

`Borrowed<T>` is essentially a type-safe wrapper around `T&`. Over `T&`, it adds the constraint "no construction from temporaries"; over `T*`, it adds the "non-null" guarantee. Its overhead is zero — after compiler optimization it is identical to a raw reference. And its limitation is the same as a raw reference's: **it cannot test for liveness**.

`ObserverPtr<T>` is essentially a semantic annotation on `T*`. Its runtime behavior is identical to a raw pointer's; the only difference is readability — when you see a member variable of type `ObserverPtr<Logger>`, you don't need to guess whether it owns that Logger; the type name has already answered for you. But likewise, **it cannot test for liveness**.

The problem with a raw pointer `T*` is not that it is "unsafe" but that it "takes no position" — handed a `T*`, you don't know whether it is owning or non-owning, nullable or guaranteed non-null, short-lived or long-lived. What `Borrowed` and `ObserverPtr` fix is precisely this refusal to commit.

## Summary

Let's sum up the key points of this installment:

- **T\*** and **T&** are C++'s most primitive borrowing mechanisms; by themselves they express no ownership semantics
- **Borrowed\<T\>** expresses a non-null borrow; suited to function parameters, forbids construction from temporaries, and does not extend lifetime
- **ObserverPtr\<T\>** expresses a nullable, non-owning observation; suited to class members, and provides no ability to test for liveness
- **Non-owning does not mean safe** — neither Borrowed nor ObserverPtr can safely detect expiration after the object is destroyed
- Their core value is **semantic expression**, not runtime safety — let the code speak for itself and cut down ambiguity

At this point we have only covered two semantic layers: "borrowing" and "observation." The real trouble is "weak references" — when you need to safely hold a reference to an object in a world where it may be destroyed at any moment, Borrowed and ObserverPtr alone won't cut it.

In the next installment, we'll dissect something that looks a lot like a WeakPtr but isn't: `T* + raw Flag*`.

## References

- [C++ Core Guidelines - R.3: A raw pointer (a T\*) is non-owning](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#Rr-ptr)
- [std::experimental::observer_ptr - cppreference](https://en.cppreference.com/cpp/experimental/observer_ptr)
- [GSL: Guidelines Support Library (Microsoft)](https://github.com/microsoft/GSL) — `gsl::not_null` and `gsl::span`
- [C++ Core Guidelines - F.7: For general use, take T\* or T\& arguments rather than smart pointers](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#Rf-smartptrref)
