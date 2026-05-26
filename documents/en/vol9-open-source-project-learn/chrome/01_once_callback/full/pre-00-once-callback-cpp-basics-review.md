---
title: 'OnceCallback Prerequisites at a Glance: A Review of Core C++11/14/17 Features'
description: A quick review of all the fundamental C++ features required for the OnceCallback
  series—move semantics, perfect forwarding, variadic templates, smart pointers, atomic
  operations, lambda expressions, type traits, and more—preparing us for the deep
  dive ahead.
chapter: 0
order: 0
tags:
- host
- cpp-modern
- intermediate
- 基础
- 入门
difficulty: intermediate
platform: host
cpp_standard:
- 11
- 14
- 17
- 20
reading_time_minutes: 25
prerequisites:
- 卷一 C++ 基础入门
related:
- OnceCallback 前置知识（一）：函数类型与模板偏特化
- OnceCallback 前置知识（三）：Lambda 高级特性
translation:
  source: documents/vol9-open-source-project-learn/chrome/01_once_callback/full/pre-00-once-callback-cpp-basics-review.md
  source_hash: 36ab223da7e08fe1f83ab09fe810ea3204c6f75675fbb239a06b2e33b445b543
  translated_at: '2026-05-26T12:26:53.872398+00:00'
  engine: anthropic
  token_count: 2747
---
# OnceCallback Prerequisite Quick Reference: Core C++11/14/17 Features Review

## Introduction

Let's be honest—this article isn't meant to teach you from scratch. If you're completely unfamiliar with concepts like move semantics and smart pointers, we recommend going back to Volume 2 and working through the relevant chapters before returning here. The role of this article is a **quick reference manual**: we pull out all the C++ features that the OnceCallback series will use repeatedly, and for each feature, we cover exactly three things—"what it is", "how to use it", and "where it appears in OnceCallback". The goal is to prevent you from getting stuck on a syntax detail when reading the upcoming articles.

> **Learning Objectives**
>
> - Quickly review all the C++11/14/17 fundamental features required for the OnceCallback series
> - Understand where each feature is specifically applied in the OnceCallback design
> - Establish the knowledge baseline needed for deeper learning ahead

---

## Move Semantics and std::move

Move semantics are the foundation of the entire OnceCallback—it is a move-only type, and its core design relies entirely on move semantics. Let's quickly go through the core concepts.

### Rvalue References and Move Construction

C++11 introduced rvalue references `T&&`, which can bind to temporary objects (rvalues). The semantics of a move constructor `T(T&& other)` are to "steal" resources from `other` rather than making a copy. After stealing, `other` enters a "valid but unspecified" state—typically emptied out.

```cpp
// 一个最简单的移动语义示例
class Buffer {
    int* data_;
    std::size_t size_;
public:
    // 普通构造
    Buffer(std::size_t n) : data_(new int[n]), size_(n) {}
    // 移动构造：偷走 other 的资源
    Buffer(Buffer&& other) noexcept
        : data_(other.data_), size_(other.size_) {
        other.data_ = nullptr;   // 清空源对象
        other.size_ = 0;
    }
    ~Buffer() { delete[] data_; }
};

Buffer a(100);          // a 拥有 100 个 int
Buffer b = std::move(a); // b 偷走了 a 的资源，a 变空
```

### The Essence of std::move

`std::move` doesn't actually move anything—it is simply a `static_cast<T&&>` that unconditionally casts the passed object to an rvalue reference. What actually performs the "move" is the move constructor or move assignment operator. The role of `std::move` is to tell the compiler, "I agree to treat this object as an rvalue; you may steal resources from it."

### Application in OnceCallback

OnceCallback is invoked as `std::move(cb).run(args...)`—`std::move` converts `cb` to an rvalue, and `run()` uses deducing this (a C++23 feature covered in a dedicated article later) to detect that this is an rvalue invocation, executes the callback, and marks `cb`'s state as "consumed." Any subsequent access to `cb` is illegal. The entire design philosophy is: **enforcing the "invoke-once-then-invalidate" semantics through the type system**.

OnceCallback also deletes its copy constructor and copy assignment operator (`= delete`), keeping only move operations. This means a OnceCallback object has exactly one owner at any given time—you can't copy it, you can only transfer ownership via `std::move`.

---

## Perfect Forwarding and std::forward

Perfect forwarding solves this problem: you write a function template that accepts parameters and passes them verbatim to another function. "Verbatim" means preserving the value category (lvalue or rvalue) and const qualification of the parameters.

### Forwarding References and Deduction Rules

When a function template's parameter is `T&&` and `T` is a template parameter, `T&&` is not a plain rvalue reference, but rather a **forwarding reference** (also known as a universal reference). The compiler deduces `T` based on the value category of the passed argument:

- Passing an lvalue `x` (type `int`) → `T = int&`, `T&&` collapses to `int&`
- Passing an rvalue `42` (type `int`) → `T = int`, `T&&` is simply `int&&`

### The Role of std::forward

`std::forward<T>(arg)` decides whether to return an lvalue reference or an rvalue reference based on the type of the template parameter `T`:

```cpp
template<typename T>
void wrapper(T&& arg) {
    // std::forward 保持 arg 的原始值类别
    target(std::forward<T>(arg));
}

int x = 10;
wrapper(x);    // arg 是左值引用，forward 返回左值引用
wrapper(10);   // arg 是右值引用，forward 返回右值引用
```

If you don't use `std::forward` and pass `arg` directly, then `arg` is always an lvalue inside the function (because named variables are lvalues), and the rvalue information is lost.

### Application in OnceCallback

Perfect forwarding appears many times in OnceCallback. The `bind_once` function template uses it to preserve the value category of bound parameters—`std::forward<BoundArgs>(args)...` ensures that passed rvalues remain rvalues, and passed lvalues remain lvalues. The deducing this implementation of the `run()` method also uses `std::forward<Self>(self)` to perfectly forward the value category of `self` to the internal `impl_run`.

---

## Variadic Templates and Parameter Pack Expansion

Variadic templates allow you to write functions or classes that accept any number of arguments of any type. The template signature of OnceCallback, `OnceCallback<R(Args...)>`, uses a parameter pack.

### Basic Syntax

```cpp
template<typename... Types>  // Types 是参数包
void print_all(Types... args) {
    // args... 在这里展开
    // sizeof...(Types) 返回参数数量
}
```

`Types...` is called a parameter pack, which can contain zero or more types. `args...` is a function parameter pack, expanded at the call site. `sizeof...(Types)` is a compile-time constant that returns the number of elements in the pack.

### Expansion Locations

Parameter packs can be expanded in multiple places: function parameter lists, template parameter lists, initializer lists, capture lists (since C++20), and more. The most critical expansion location in OnceCallback is the lambda capture list—a feature introduced only in C++20, which we cover in a dedicated article later.

### Application in OnceCallback

`OnceCallback<R(Args...)>`'s `Args...` is a parameter pack that appears repeatedly throughout the class's implementation—the parameter types of the constructor, the parameter types of `run()`, and the signature of the internal `func_` all come from this pack. `bind_once`'s `BoundArgs...` is another parameter pack, expanded into the lambda's capture list and the call arguments of `std::invoke`.

---

## Smart Pointer Quick Reference

OnceCallback internally uses two types of smart pointers; let's quickly go over their respective roles.

### std::unique_ptr: Exclusive Ownership

`unique_ptr` is an exclusive smart pointer—only one `unique_ptr` can point to an object at any given time. It is not copyable, only movable. It is created using `std::make_unique<T>(args...)`.

```cpp
auto p = std::make_unique<int>(42);
// auto p2 = p;             // 编译错误：不可拷贝
auto p3 = std::move(p);    // OK：移动转移所有权
// 此后 p 为 nullptr
```

In OnceCallback, the significance of `unique_ptr` isn't that we use it directly, but rather that OnceCallback must support lambdas that capture move-only objects—if a lambda captures a `unique_ptr`, then the `std::move_only_function` containing this lambda (OnceCallback's internal storage) must also be move-only. This is something `std::function` cannot achieve, and it is one of the reasons we chose `std::move_only_function`.

### std::shared_ptr: Shared Ownership

`shared_ptr` manages an object's lifetime through reference counting. All `shared_ptr` instances pointing to the same object share a single reference count, and the object is destroyed when the last `shared_ptr` is destroyed.

```cpp
auto p1 = std::make_shared<int>(42);
auto p2 = p1;   // OK：拷贝，引用计数 +1
// p1 和 p2 都指向同一个 int
```

In OnceCallback, `shared_ptr` is used to manage the cancellation token `CancelableToken`. The token needs to be shared between the OnceCallback object and an external controller—the external controller calls `invalidate()` to invalidate the token, and OnceCallback checks the token's state via its own held `shared_ptr` copy before executing the callback. The reference counting of `shared_ptr` guarantees that as long as someone still holds the token, the underlying `Flag` object will not be destroyed.

---

## std::atomic and memory_order

The internal implementation of the cancellation token uses `std::atomic<bool>` and `memory_order_acquire/release`.

### Atomic Operations

`std::atomic<T>` provides atomic access to `T` type variables—reads and writes cannot be interrupted by other threads' operations. The basic operations are `load()` (read) and `store()` (write), which can specify a memory order.

```cpp
std::atomic<bool> flag{true};

// 线程 A：写入
flag.store(false, std::memory_order_release);

// 线程 B：读取
if (flag.load(std::memory_order_acquire)) {
    // flag 仍然为 true
}
```

### acquire/release Semantics

`memory_order_release` and `memory_order_acquire` are a paired set of memory orders. Simply put: an `release` store guarantees that all writes before the store are visible to other threads; an `acquire` load guarantees that all reads after the load can see the writes preceding the release store. In OnceCallback's cancellation token, `invalidate()` uses an `release` store to set `valid` to `false`, and `is_valid()` uses an `acquire` load to read `valid`—this guarantees that if `is_valid()` returns `true`, all state related to the token is visible to the current thread.

---

## enum class

`enum class` is the scoped enumeration introduced in C++11, solving the name pollution and implicit conversion problems of the old-style `enum`.

```cpp
// 老式 enum：名字污染全局命名空间，可以隐式转成 int
enum Color { Red, Green, Blue };
int x = Red;  // OK，隐式转换

// enum class：名字被限定在枚举作用域内，不可隐式转换
enum class Status : uint8_t {
    kEmpty,    // 从未被赋值
    kValid,    // 持有有效的可调用对象
    kConsumed  // 已被 run() 消费
};
Status s = Status::kValid;
// int y = s;  // 编译错误：不可隐式转换
```

OnceCallback uses `enum class Status` to distinguish between three states of the callback. Specifying the underlying type as `uint8_t` saves memory—the entire enumeration takes up only one byte.

---

## Lambda Basics

Lambdas are everywhere in OnceCallback—constructing callbacks, `bind_once`, and the internal implementations of `then()` all rely on lambdas. Here is a quick review of the basic syntax.

```cpp
auto add = [](int a, int b) { return a + b; };
// add 的类型是编译器生成的唯一闭包类

int x = 10;
// 值捕获：拷贝 x
auto f1 = [x]() { return x; };
// 引用捕获：引用 x（注意生命周期）
auto f2 = [&x]() { return x; };
// 初始化捕获（C++14）：可以移动捕获
auto f3 = [p = std::make_unique<int>(42)]() { return *p; };
```

The `operator()` of the closure class generated by a lambda is `const` by default—this means you cannot modify value-captured variables inside the lambda unless you add the `mutable` keyword. In OnceCallback's `bind_once` and `then()` implementations, the lambda must be declared as `mutable` because it internally needs to call `std::move(self).run()` to modify `self`'s state. We will expand on this detail in the article on advanced lambda features.

Generic lambdas (since C++14) allow parameters to use `auto`:

```cpp
auto generic = [](auto x, auto y) { return x + y; };
// 编译器为 operator() 生成模板版本
```

The lambda inside `bind_once` uses `(auto&&... call_args)` to accept runtime arguments—here, `auto&&` is a forwarding reference (because `auto` is equivalent to a template parameter).

---

## Type Traits

Type traits are tools for querying and manipulating type information at compile time. OnceCallback uses several key traits; let's quickly go through them.

```cpp
#include <type_traits>

// std::decay_t<T>：去掉 T 上的引用、const/volatile 限定符，数组变指针，函数变函数指针
using T1 = std::decay_t<const int&>;       // T1 = int
using T2 = std::decay_t<OnceCallback&&>;   // T2 = OnceCallback（去掉引用）

// std::is_same_v<A, B>：A 和 B 是否是同一类型
static_assert(std::is_same_v<int, int>);           // 通过
static_assert(!std::is_same_v<int, double>);       // 通过

// std::is_lvalue_reference_v<T>：T 是否是左值引用类型
static_assert(std::is_lvalue_reference_v<int&>);      // 通过
static_assert(!std::is_lvalue_reference_v<int>);      // 通过
static_assert(!std::is_lvalue_reference_v<int&&>);    // 通过

// std::is_void_v<T>：T 是否是 void
static_assert(std::is_void_v<void>);            // 通过
static_assert(!std::is_void_v<int>);            // 通过
```

In OnceCallback, `std::decay_t` and `std::is_same_v` are used in the `not_the_same_t` concept—it checks "whether the template parameter, after decay, is the same type as `OnceCallback` itself," used to prevent the template constructor from hijacking calls to the move constructor. `std::is_lvalue_reference_v` is used in the deducing this implementation of `run()`—it detects whether the caller passed an lvalue, and if so, triggers a `static_assert` error. `std::is_void_v` is used in `impl_run()` and `then()` to distinguish compile-time branches between void and non-void return types.

---

## if constexpr

`if constexpr` is the compile-time conditional branch introduced in C++17. The difference between it and a regular `if` is that the condition must be a compile-time constant expression, and **the unselected branch is not compiled**—not even syntax checking is performed. This feature is particularly useful when handling void return types.

```cpp
template<typename R>
R do_something() {
    if constexpr (std::is_void_v<R>) {
        // void 返回：执行操作，不 return
        perform_action();
        return;  // void return
    } else {
        // 非 void 返回：执行操作，return 结果
        return perform_action();
    }
}
```

Without `if constexpr`, using a regular `if` means both branches would be compiled. In this case, the `return result` in the void branch would directly cause an error—void is not a type that can be assigned. `if constexpr` guarantees that the void case only generates the `return;` code, and the non-void case only generates the `return result;` code.

In OnceCallback, `if constexpr (std::is_void_v<ReturnType>)` appears in two places: the callback execution logic of `impl_run()`, and the chained composition logic of `then()`. Both places deal with the same issue—void return types cannot be assigned and returned in the usual way.

---

## decltype(auto)

`decltype(auto)` is the return type deduction method introduced in C++14. The difference between it and `auto` lies in how references are handled: `auto` drops references and top-level const, while `decltype(auto)` preserves them.

```cpp
int x = 10;
int& ref = x;

auto f1() { return ref; }           // 返回 int（丢掉了引用）
decltype(auto) f2() { return ref; } // 返回 int&（保留了引用）
```

In OnceCallback, the lambdas in `bind_once` and `then()` use `-> decltype(auto)` as a trailing return type. The purpose of this is to perfectly forward the callable object's return value—if the called function returns `int&&`, `decltype(auto)` will also return `int&&`, without losing value category information.

---

## [[nodiscard]] Attribute

`[[nodiscard]]` is an attribute standardized in C++17 that tells the compiler "the return value of this function should not be ignored." If the caller writes `cb.is_cancelled();` without using the return value, the compiler will issue a warning.

```cpp
[[nodiscard]] bool is_cancelled() const noexcept;
[[nodiscard]] bool maybe_valid() const noexcept;
[[nodiscard]] bool is_null() const noexcept;
```

All three query methods of OnceCallback are annotated with `[[nodiscard]]`. The reason is simple—calling these methods is specifically to get the return value for a check, and calls that ignore the return value are most likely typos (for example, writing `if (!cb.is_cancelled())` instead of `cb.is_cancelled();`). The `explicit` of `explicit operator bool()` serves a similar purpose—preventing unintended behavior caused by implicit conversion to `bool`.

---

## Ref-qualified Member Functions

C++11 allows non-static member functions to be ref-qualified, using `&` or `&&` annotated after the function's parameter list. `&` means it can only be called through an lvalue, and `&&` means it can only be called through an rvalue.

```cpp
class Widget {
public:
    void process() & {
        // 只能通过左值调用：Widget w; w.process();
    }
    void process() && {
        // 只能通过右值调用：Widget().process(); 或 std::move(w).process();
    }
};
```

In OnceCallback, the `then()` method is declared as `auto then(Next&& next) &&`—the trailing `&&` means `then()` can only be called through an rvalue (via `std::move(cb).then(next)` or on a temporary object). This is another way to express consume semantics—unlike `run()` which uses deducing this to differentiate between lvalues and rvalues to provide different error messages, `then()` doesn't need to distinguish between them, making the ref-qualifier approach more concise.

---

## Summary

In this article, we quickly went through all the fundamental C++ features that the OnceCallback series will use. For each feature, we clarified three points: what it is, how to use it, and where it will appear in OnceCallback. If you feel unfamiliar with any feature, we recommend going back to the corresponding chapter in the earlier volumes for a systematic study—upcoming articles will not re-explain these basic syntax elements.

Next, we are entering the deep-dive section. The first stop is "Function Types and Template Partial Specialization"—this is the key to understanding the peculiar syntax of `OnceCallback<R(Args...)>`, and it is the entry point for building our entire template skeleton.

## References

- [cppreference: Move Semantics and Rvalue References](https://en.cppreference.com/w/cpp/language/reference)
- [cppreference: std::forward](https://en.cppreference.com/w/cpp/utility/forward)
- [cppreference: Variadic Templates](https://en.cppreference.com/w/cpp/language/parameter_pack)
- [cppreference: std::shared_ptr](https://en.cppreference.com/w/cpp/memory/shared_ptr)
- [cppreference: std::atomic](https://en.cppreference.com/w/cpp/atomic/atomic)
- [cppreference: if constexpr](https://en.cppreference.com/w/cpp/language/if)
- [cppreference: Type Traits](https://en.cppreference.com/w/cpp/header/type_traits)
