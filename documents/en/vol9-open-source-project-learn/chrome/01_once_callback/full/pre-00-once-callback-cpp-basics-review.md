---
chapter: 0
cpp_standard:
- 11
- 14
- 17
- 20
description: A quick review of all the essential C++ features required for the OnceCallback
  series—including move semantics, perfect forwarding, variadic templates, smart pointers,
  atomics, lambdas, type traits, and more—to prepare for the upcoming deep dive.
difficulty: intermediate
order: 0
platform: host
prerequisites:
- 卷一 C++ 基础入门
reading_time_minutes: 14
related:
- OnceCallback 前置知识（一）：函数类型与模板偏特化
- OnceCallback 前置知识（三）：Lambda 高级特性
tags:
- host
- cpp-modern
- intermediate
- 基础
- 入门
title: 'OnceCallback Prerequisites: A Quick Review of C++11/14/17 Core Features'
translation:
  source: documents/vol9-open-source-project-learn/chrome/01_once_callback/full/pre-00-once-callback-cpp-basics-review.md
  source_hash: 5513768d040062a8299d0647724bce9cbfb7972e824859ac8da0697c07a50b6d
  translated_at: '2026-06-16T04:13:41.649148+00:00'
  engine: anthropic
  token_count: 2741
---
# OnceCallback Prerequisite Cheat Sheet: A Review of C++11/14/17 Core Features

## Introduction

Let's be honest, this isn't a "from zero" tutorial—if you are completely unfamiliar with concepts like move semantics and smart pointers, I suggest going back to Volume Two and finishing the relevant chapters before returning. The role of this article is a **cheat sheet**: we run through all the C++ features that will be used repeatedly in the OnceCallback series. For each feature, we cover only three things—"what it is", "how to use it", and "where it appears in OnceCallback". The goal is to ensure you don't get stuck on syntax details while reading the subsequent articles.

> **Learning Objectives**
>
> - Quickly review all C++11/14/17 fundamental features required for the OnceCallback series.
> - Understand the specific application of each feature in the design of OnceCallback.
> - Establish the knowledge baseline needed for deep learning later on.

---

## Move Semantics and std::move

Move semantics are the foundation of the entire OnceCallback—it is a move-only type, and its core design relies entirely on move semantics. Let's quickly review the core concepts.

### Rvalue References and Move Constructors

C++11 introduced rvalue references `T&&`, which can bind to temporary objects (rvalues). The semantic of a move constructor is to **steal** resources from the source `other`, rather than making a copy. After stealing, `other` enters a "valid but unspecified" state—usually, it is emptied.

```cpp
struct MoveOnly {
    int* data;
    // Move constructor
    MoveOnly(MoveOnly&& other) noexcept : data(other.data) {
        other.data = nullptr; // Steal resources, leave source empty
    }
};
```

### The Essence of std::move

`std::move` actually doesn't move anything—it is just a `static_cast<T&&>` that unconditionally converts the input object into an rvalue reference. The ones actually performing the "move" are the move constructor or move assignment operator. The role of `std::move` is to tell the compiler: "I agree to treat this object as an rvalue; you can steal resources from it."

### Application in OnceCallback

The invocation method of OnceCallback is `cb(args...)`. `std::move(cb)` converts `cb` to an rvalue, and `operator()` uses deducing this (a C++23 feature, covered in a dedicated article later) to detect that this is an rvalue call, executes the callback, and marks the state of `*this` as "consumed". Any subsequent access to `cb` is illegal. The entire design idea is: **using the type system to enforce the "call-once-then-invalid" semantics**.

OnceCallback simultaneously deletes the copy constructor and copy assignment (`= delete`), retaining only move operations. This means a OnceCallback object has exactly one owner at any given time—you cannot copy it, you can only transfer ownership via `std::move`.

---

## Perfect Forwarding and std::forward

Perfect forwarding solves this problem: you write a function template that accepts parameters and passes them verbatim to another function. "Verbatim" means preserving the value category (lvalue vs. rvalue) and const qualifiers of the arguments.

### Forwarding References and Deduction Rules

When a function template's parameter is `T&&` and `T` is a template parameter, `T&&` is not a normal rvalue reference, but a **forwarding reference** (also known as a universal reference). The compiler deduces `T` based on the value category of the passed argument:

- Pass an lvalue `t` (type `U&`) → `T` deduced to `U&`, `T&&` collapses to `U&`
- Pass an rvalue `t` (type `U`) → `T` deduced to `U`, `T&&` is `U&&`

### The Role of std::forward

`std::forward<T>` decides whether to return an lvalue reference or an rvalue reference based on the template parameter `T`:

```cpp
template<typename T>
void wrapper(T&& arg) {
    // Forward arg to target, preserving lvalue/rvalue
    target(std::forward<T>(arg));
}
```

If you pass `arg` directly without `std::forward`, then `arg` is always an lvalue inside the function (because named variables are lvalues), and the rvalue information is lost.

### Application in OnceCallback

Perfect forwarding appears many times in OnceCallback. The `bind` function template uses it to preserve the value category of bound arguments—`std::forward<Args>(args)` ensures that passed rvalues remain rvalues, and passed lvalues remain lvalues. The deducing this implementation of `operator()` also uses `std::forward<Self>` to perfectly forward the value category of `*this` to the internal lambda.

---

## Variadic Templates and Parameter Pack Expansion

Variadic templates allow you to write a function or class that accepts an arbitrary number of arguments of arbitrary types. OnceCallback's template signature `OnceCallback<R(Args...)>` uses parameter packs.

### Basic Syntax

```cpp
template<typename... Ts> // Template parameter pack
void print_all(Ts... args) { // Function parameter pack
    // sizeof...(args) returns the number of arguments
}
```

`Ts...` is called a parameter pack; it can contain zero or more types. `args...` is a function parameter pack, expanded at the call site. `sizeof...(args)` is a compile-time constant returning the number of elements in the pack.

### Expansion Positions

Parameter packs can be expanded in multiple places: function parameter lists, template parameter lists, initializer lists, capture lists (since C++20), etc. In OnceCallback, the most critical expansion position is the lambda's capture list—this feature was only introduced in C++20 and will be covered in a dedicated article.

### Application in OnceCallback

`Args...` in `OnceCallback<R(Args...)>` is a parameter pack; it appears repeatedly throughout the class's implementation—the constructor's parameter types, `operator()`'s parameter types, and the internal storage's signature all come from this pack. `BoundArgs...` in `bind<BoundArgs...>` is another parameter pack, expanded into the lambda's capture list and the call arguments of `target`.

---

## Smart Pointer Cheat Sheet

OnceCallback uses two types of smart pointers internally; let's quickly review their respective roles.

### std::unique_ptr: Exclusive Ownership

`std::unique_ptr` is an exclusive smart pointer—only one `unique_ptr` points to the object at any given time. It is not copyable, only movable. The creation method is `std::make_unique`.

```cpp
auto ptr = std::make_unique<int>(42);
```

In OnceCallback, the significance of `std::unique_ptr` is not that we use it directly, but that OnceCallback must support lambdas that capture move-only objects—if a lambda captures a `std::unique_ptr`, then the `std::function`-like storage (OnceCallback's internal storage) containing that lambda must also be move-only. This is something `std::function` cannot do, and it is one of the reasons we chose `std::unique_ptr`.

### std::shared_ptr: Shared Ownership

`std::shared_ptr` manages an object's lifetime through reference counting. All `shared_ptr`s pointing to the same object share the same reference count; when the last `shared_ptr` is destroyed, the object is also destroyed.

```cpp
auto ptr1 = std::make_shared<int>(42);
auto ptr2 = ptr1; // Both share ownership
```

In OnceCallback, `std::shared_ptr` is used to manage the cancellation token `CancellationState`. The token needs to be shared between the OnceCallback object and an external controller—the external controller calls `cancel()` to invalidate the token, and OnceCallback checks the token status via its held `std::shared_ptr` copy before executing the callback. The reference counting of `std::shared_ptr` ensures that the underlying `CancellationState` object is not destroyed as long as someone holds the token.

---

## std::atomic and memory_order

The internal implementation of the cancellation token uses `std::atomic` and `memory_order`.

### Atomic Operations

`std::atomic` provides atomic access to variables of type `T`—reads and writes cannot be interrupted by operations from other threads. Basic operations are `load` (read) and `store` (write), and you can specify the memory order.

```cpp
std::atomic<bool> flag{false};
flag.store(true, std::memory_order_release);
bool value = flag.load(std::memory_order_acquire);
```

### acquire/release Semantics

`memory_order_acquire` and `memory_order_release` are a pair of matching memory orders. Simply put: a `release` store guarantees that all writes before the store are visible to other threads; an `acquire` load guarantees that all reads after the load see the writes before the release store. In OnceCallback's cancellation token, `cancel()` uses a `release` store to set `canceled` to `true`, and `is_canceled()` uses an `acquire` load to read `canceled`—this guarantees that if `is_canceled()` returns `true`, all states related to the token are visible to the current thread.

---

## enum class

`enum class` is a scoped enumeration introduced in C++11, solving the name pollution and implicit conversion problems of old-style `enum`.

```cpp
enum class State { Empty, Ready, Consumed };
State s = State::Ready; // Must use scope prefix
```

OnceCallback uses `enum class` to distinguish three states of the callback. Specifying the underlying type as `uint8_t` saves memory—the entire enum occupies only one byte.

---

## Lambda Basics

Lambdas are ubiquitous in OnceCallback—constructing callbacks, `bind`, and the internal implementation of `operator()` all rely on lambdas. Here is a quick review of the basic syntax.

```cpp
[capture](parameters) -> return_type { body }
```

The `operator()` of the closure class generated by a lambda is `const` by default—this means you cannot modify value-captured variables inside the lambda unless you add the `mutable` keyword. In OnceCallback's `bind` and `operator()` implementation, the lambda must be declared as `mutable` because the internals need to call `consume()` to modify the state of `*this`. We will expand on this detail in the article on advanced lambda features.

Generic lambdas (since C++14) allow parameters to use `auto`:

```cpp
auto generic = [](auto&& x) {
    // x is a forwarding reference
};
```

The lambda inside `OnceCallback::operator()` uses `auto&&` to accept runtime arguments—here `auto&&` is a forwarding reference (because `auto` is equivalent to a template parameter).

---

## Type Traits

Type traits are tools for compile-time querying and manipulating type information. OnceCallback uses several key traits; let's quickly review them.

```cpp
std::is_same_v<T, U>       // Check if T and U are the same type
std::is_lvalue_reference_v<T> // Check if T is an lvalue reference
std::remove_reference_t<T>  // Remove reference from T
```

In OnceCallback, `std::is_same_v` and `std::remove_reference_t` are used for the `NotSameAsThis` concept—it checks "whether the template parameter, after decay, is the same type as `This` itself", used to prevent the template constructor from hijacking move constructor calls. `std::is_lvalue_reference_v` is used in the deducing this implementation of `operator()`—it detects if the caller passed an lvalue, and if so, triggers a `static_assert` error. `std::is_void_v` is used for compile-time branching to distinguish between `void` and non-`void` return types in `bind` and `operator()`.

---

## if constexpr

`if constexpr` is a compile-time conditional branch introduced in C++17. The difference between it and a normal `if` is: the condition must be a compile-time constant expression, and **the unselected branch will not be compiled**—not even syntax checked. This feature is particularly useful when handling `void` return types.

```cpp
template<typename T>
auto get_value() {
    if constexpr (std::is_void_v<T>) {
        return; // Only compiled if T is void
    } else {
        return T{}; // Only compiled if T is not void
    }
}
```

Without `if constexpr` and using a normal `if`, both branches would be compiled. In that case, the `return T{}` in the `void` branch would cause an error immediately—`void` is not a type that can be assigned. `if constexpr` guarantees that the `void` case only generates the code for `return;`, and the non-`void` case only generates the code for `return T{}`.

In OnceCallback, `if constexpr` appears in two places: the callback execution logic of `operator()` and the chaining composition logic of `then`. Both places face the same problem—`void` return types cannot be assigned and returned in the常规 way.

---

## decltype(auto)

`decltype(auto)` is a return type deduction method introduced in C++14. The difference between it and `auto` lies in the handling of references: `auto` drops references and top-level const, while `decltype(auto)` preserves them.

```cpp
int x = 42;
int& foo() { return x; }
auto a = foo();     // a is int (copy)
decltype(auto) b = foo(); // b is int& (reference)
```

In OnceCallback, the lambdas in `bind` and `operator()` use `decltype(auto)` as a trailing return type. The purpose is to perfectly forward the return value of the callable object—if the called function returns `T&`, `decltype(auto)` will also return `T&`, preserving the value category information.

---

## [[nodiscard]] Attribute

`[[nodiscard]]` is an attribute standardized in C++17, telling the compiler "the return value of this function should not be ignored". If the caller writes `is_canceled()` but doesn't use the return value, the compiler will issue a warning.

```cpp
[[nodiscard]] bool is_canceled() const;
```

All three query methods of OnceCallback are marked with `[[nodiscard]]`. The reason is simple—calling these methods is for getting the return value for judgment, and calls that ignore the return value are likely typos (e.g., writing `is_canceled()` instead of `cancel()`). The `[[nodiscard]]` on the `explicit operator bool` plays a similar role—preventing unexpected behavior caused by implicit conversion to `bool`.

---

## Ref-qualified Member Functions

C++11 allows reference qualifiers (ref-qualifiers) for non-static member functions, annotating with `&` or `&&` after the function parameter list. `&` means it can only be called on an lvalue, `&&` means it can only be called on an rvalue.

```cpp
struct RefQualified {
    void foo() & { /* Called on lvalue */ }
    void foo() && { /* Called on rvalue */ }
};
```

In OnceCallback, the `operator()` method is declared as `R operator()(Args... args) &&`—the trailing `&&` means `operator()` can only be called on an rvalue (`std::move(cb)` or `cb` on a temporary object). This is another way to express consume semantics—unlike the C++23 `operator()` which uses deducing this to distinguish between lvalue and rvalue to give different error messages, the C++11 version uses a ref-qualifier directly, which is more concise.

---

## Summary

In this article, we quickly ran through all the basic C++ features that will be used in the OnceCallback series. For each feature, we clarified three points: what it is, how to use it, and where it appears in OnceCallback. If you are unfamiliar with any feature, I suggest going back to the corresponding chapters in the earlier volumes to study systematically—subsequent articles will not re-explain these basic syntax rules.

Next, we are entering the deep dive section. The first stop is "Function Types and Template Partial Specialization"—this is the key to understanding the weird syntax `OnceCallback<R(Args...)>` and the entry point for building our entire template skeleton.

## Reference Resources

- [cppreference: Move semantics and rvalue references](https://en.cppreference.com/w/cpp/language/reference)
- [cppreference: std::forward](https://en.cppreference.com/w/cpp/utility/forward)
- [cppreference: Variadic templates](https://en.cppreference.com/w/cpp/language/parameter_pack)
- [cppreference: std::shared_ptr](https://en.cppreference.com/w/cpp/memory/shared_ptr)
- [cppreference: std::atomic](https://en.cppreference.com/w/cpp/atomic/atomic)
- [cppreference: if constexpr](https://en.cppreference.com/w/cpp/language/if)
- [cppreference: Type traits](https://en.cppreference.com/w/cpp/header/type_traits)
