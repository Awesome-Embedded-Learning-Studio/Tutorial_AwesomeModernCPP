---
title: 'OnceCallback in Practice (Part 2): Building the Core Skeleton'
description: Building the OnceCallback Class Skeleton in 5 Steps from Scratch — Template
  Partial Specialization, Data Members, Constructor Constraints, run() Consumption
  Semantics, Query Interface
chapter: 1
order: 2
tags:
- host
- cpp-modern
- beginner
- 回调机制
- 函数对象
- 模板
difficulty: beginner
platform: host
cpp_standard:
- 23
reading_time_minutes: 13
prerequisites:
- OnceCallback 实战（一）：动机与接口设计
- OnceCallback 前置知识（一）：函数类型与模板偏特化
- OnceCallback 前置知识（四）：Concepts 与 requires 约束
- OnceCallback 前置知识（五）：std::move_only_function
- OnceCallback 前置知识（六）：Deducing this
related:
- OnceCallback 实战（三）：bind_once 实现
- OnceCallback 实战（四）：取消令牌设计
---
# OnceCallback in Practice (Part 2): Building the Core Skeleton

## Introduction

In the previous article, we clarified "why we need OnceCallback" and "what the target API should look like." Now we dive into writing the actual code. The task for this article is to build the class skeleton of OnceCallback from scratch—not by writing all the functionality at once, but in five steps, each layering on top of the previous one. Once the skeleton is complete, subsequent `bind_once`, cancellation tokens, and `then()` are all just components added onto this framework.

We have thoroughly covered all the prerequisite knowledge in the previous seven articles. This article is pure hands-on practice—we directly reference the actual source code and translate every design decision into implementation.

> **Learning Objectives**
>
> - Build the complete class skeleton of `OnceCallback<R(Args...)>` from scratch
> - Understand the responsibility of each data member and method
> - Master the deducing this implementation of `run()` and the consumption logic of `impl_run()`

---

## Step 1: Primary Template and Partial Specialization

In Prerequisite Knowledge (Part 1), we discussed the "function type + template partial specialization" pattern. Now we apply it directly to OnceCallback.

```cpp
namespace tamcpp::chrome {

// 主模板：只有声明，没有定义
// 如果有人写了 OnceCallback<int>（传了非函数类型），编译器会报错
template<typename FuncSignature>
class OnceCallback;

// 偏特化：FuncSignature 是 R(Args...) 形式的函数类型时匹配
template<typename ReturnType, typename... FuncArgs>
class OnceCallback<ReturnType(FuncArgs...)> {
    // 所有真正的代码都在这个偏特化里
public:
    using FuncSig = ReturnType(FuncArgs...);
    // ...
};

} // namespace tamcpp::chrome
```

When you write `OnceCallback<int(int, int)>`, the compiler matches `int(int, int)` to the `FuncSignature` of the primary template, then discovers that the partial specialization can decompose it into `ReturnType = int` and `FuncArgs = {int, int}`, so it selects the partially specialized version. `FuncSig` is a type alias that preserves the complete function signature—which will be used later when declaring `std::move_only_function<FuncSig>`.

---

## Step 2: Data Members — Three Core Storages

Now we add data members to the partially specialized class. OnceCallback needs three things to manage its state.

```cpp
template<typename ReturnType, typename... FuncArgs>
class OnceCallback<ReturnType(FuncArgs...)> {
public:
    using FuncSig = ReturnType(FuncArgs...);

private:
    enum class Status : uint8_t {
        kEmpty,     // 从未被赋值（默认构造）
        kValid,     // 持有有效的可调用对象
        kConsumed   // 已被 run() 调用过
    } status_ = Status::kEmpty;

    std::move_only_function<FuncSig> func_;          // 类型擦除的可调用对象
    std::shared_ptr<CancelableToken> token_;         // 可选的取消令牌
};
```

`func_` is the core of type erasure—it uniformly wraps callable objects of various forms (lambdas, function pointers, functors) into a calling interface with a `FuncSig` signature. Regardless of what you pass in, `func_` can invoke it using the same `operator()`.

`status_` is a tri-state enum, distinguishing between "never assigned," "ready to call," and "already called." Why can't we rely solely on the null check of `func_`? Because the `operator bool()` of `std::move_only_function` can only distinguish between "null" and "non-null" states, and the state after a move is unspecified—as we detailed in Prerequisite Knowledge (Part 5).

`token_` is an optional cancellation token, used to check whether execution should be canceled before the callback runs. It defaults to a null pointer (cancellation mechanism disabled) and is set via the `set_token()` method. We will dedicate a future article to this topic.

---

## Step 3: Constructors and requires Constraints

Next, we add the constructors. The key point here is that the template constructor must use `requires` constraints to prevent it from hijacking the move constructor—we covered this problem in Prerequisite Knowledge (Part 4).

```cpp
// not_the_same_t concept：F 退化后不是 T
template<typename F, typename T>
concept not_the_same_t = !std::is_same_v<std::decay_t<F>, T>;

template<typename ReturnType, typename... FuncArgs>
class OnceCallback<ReturnType(FuncArgs...)> {
    // ... 数据成员 ...

    // 禁止拷贝
    OnceCallback(const OnceCallback&) = delete;
    OnceCallback& operator=(const OnceCallback&) = delete;

public:
    // 模板构造函数：接受任意可调用对象
    template<typename Functor>
        requires not_the_same_t<Functor, OnceCallback>
    explicit OnceCallback(Functor&& function)
        : status_(Status::kValid), func_(std::move(function)) {}

    // 默认构造：创建空回调
    explicit OnceCallback() = default;

    // 移动构造
    OnceCallback(OnceCallback&& other) noexcept
        : status_(other.status_),
          func_(std::move(other.func_)),
          token_(std::move(other.token_)) {
        other.status_ = Status::kEmpty;
    }

    // 移动赋值
    OnceCallback& operator=(OnceCallback&& other) noexcept {
        if (this != &other) {
            status_ = other.status_;
            func_ = std::move(other.func_);
            token_ = std::move(other.token_);
            other.status_ = Status::kEmpty;
        }
        return *this;
    }
};
```

Let's understand these constructors one by one.

The **template constructor** is the most commonly used—it is called when you write `OnceCallback<int(int)>([](int x) { return x; })`. `Functor` is deduced as the closure type of the lambda, and `requires not_the_same_t` ensures that when the input is a `OnceCallback` itself, the template is excluded (letting the move constructor handle it). `std::move(function)` moves the passed callable object into `func_`, and `status_` is set to `kValid`.

The **default constructor** creates an empty OnceCallback—`status_` is `kEmpty` (determined by the default value of the member initializer), and both `func_` and `token_` are empty.

The **move constructor** steals everything from another OnceCallback—`func_` and `token_` are transferred via `std::move`, and `status_` is copied over as well. The key point is that the source object is set to `kEmpty` after the move—this is something we do explicitly, rather than relying on the post-move state of `std::move_only_function`.

---

## Step 4: The deducing this Implementation of run()

This step is the soul of the entire skeleton. `run()` uses deducing this to intercept lvalue calls at compile time, forwarding rvalue calls to the internal `impl_run()`.

```cpp
// 声明（在类体内）
template<typename Self>
auto run(this Self&& self, FuncArgs&&... args) -> ReturnType;

// 实现（在类体外，once_callback_impl.hpp 中）
template<typename ReturnType, typename... FuncArgs>
template<typename Self>
auto OnceCallback<ReturnType(FuncArgs...)>::run(this Self&& self, FuncArgs&&... args)
    -> ReturnType {
    static_assert(!std::is_lvalue_reference_v<Self>,
        "once_callback::run() must be called on an rvalue. "
        "Use std::move(cb).run(...) instead.");
    return std::forward<Self>(self).impl_run(std::forward<FuncArgs>(args)...);
}
```

When the caller writes `cb.run(args)`, `Self` is deduced as `OnceCallback&` (an lvalue reference), `static_assert` triggers, and the error message directly tells the caller what to do. When writing `std::move(cb).run(args)`, `Self` is deduced as `OnceCallback` (non-reference), compilation succeeds, and it forwards to `impl_run`.

`impl_run` is where the callback is actually executed:

```cpp
template<typename ReturnType, typename... FuncArgs>
ReturnType OnceCallback<ReturnType(FuncArgs...)>::impl_run(FuncArgs... args) {
    assert(status_ == Status::kValid);

    // 取消检查：消费但不执行
    if (token_ && !token_->is_valid()) {
        status_ = Status::kConsumed;
        func_ = nullptr;
        if constexpr (std::is_void_v<ReturnType>) {
            return;
        } else {
            throw std::bad_function_call{};
        }
    }

    // 消费：先把 func_ 拿出来，再更新状态，最后执行
    auto functor = std::move(func_);
    func_ = nullptr;
    status_ = Status::kConsumed;

    if constexpr (std::is_void_v<ReturnType>) {
        functor(std::forward<FuncArgs>(args)...);
    } else {
        return functor(std::forward<FuncArgs>(args)...);
    }
}
```

There are a few key details worth noting.

First, look at the consumption order—`impl_run` first moves `func_` out as a local variable `functor`, then sets `func_` to null, sets `status_` to kConsumed, and finally executes `functor`. This order is critical: extract the callable object first, mark the state, and then execute. Even if the callable object throws an exception internally, `status_` is already `kConsumed`, so the callback will not be left in an inconsistent state.

Next, look at `if constexpr`—a void return type cannot be assigned and returned in the常规 way. `if constexpr (std::is_void_v<ReturnType>)` selects the branch at compile time: the void case takes the "invoke but don't assign" path, while the non-void case takes the "invoke and assign to return" path. This is the standard pattern we discussed in the cheat sheet article.

Finally, look at the cancellation check—it checks the cancellation token before execution. If canceled, it consumes the callback without executing it. For a void return, it simply `return`; for a non-void return, it throws `std::bad_function_call`. The exception-throwing behavior for non-void might seem aggressive, but the reasoning is solid: the caller expects a return value, but we cannot provide a meaningful one, so throwing an exception is safer than returning an undefined value.

---

## Step 5: Query Interfaces

Finally, we add a set of query methods so the caller can check the callback's state before execution.

```cpp
[[nodiscard]] bool is_cancelled() const noexcept {
    if (status_ != Status::kValid) return true;
    if (token_ && !token_->is_valid()) return true;
    return false;
}

[[nodiscard]] bool maybe_valid() const noexcept {
    return !is_cancelled();
}

[[nodiscard]] bool is_null() const noexcept {
    return status_ == Status::kEmpty;
}

explicit operator bool() const noexcept {
    return !is_null() && !is_cancelled();
}

void set_token(std::shared_ptr<CancelableToken> token) {
    token_ = std::move(token);
}
```

The logic of `is_cancelled()` is: return true if the state is not kValid (both empty and consumed callbacks count as "canceled"), and also return true if a token exists and the token is invalid. `maybe_valid()` is simply `!is_cancelled()` for now. `is_null()` only checks if it has never been assigned. `operator bool()` combines both the empty and canceled conditions.

All query methods are annotated with `[[nodiscard]]`—calling these methods is specifically to get a return value for conditional logic, so ignoring the return value is most likely a typo. The `explicit` keyword prevents implicit conversion to `bool`.

---

## Verifying the Core Skeleton

The skeleton is built. Let's quickly verify a few basic scenarios:

```cpp
#include "once_callback/once_callback.hpp"
#include <cassert>
#include <memory>

int main() {
    using namespace tamcpp::chrome;

    // 1. 非 void 返回
    OnceCallback<int(int, int)> add([](int a, int b) { return a + b; });
    assert(std::move(add).run(3, 4) == 7);

    // 2. void 返回
    bool called = false;
    OnceCallback<void()> side_effect([&called] { called = true; });
    std::move(side_effect).run();
    assert(called);

    // 3. move-only 捕获
    auto ptr = std::make_unique<int>(42);
    OnceCallback<int()> capture_move([p = std::move(ptr)] { return *p; });
    assert(std::move(capture_move).run() == 42);

    // 4. 移动语义
    OnceCallback<int()> movable([] { return 1; });
    OnceCallback<int()> moved_to = std::move(movable);
    assert(movable.is_null());            // 源对象变空
    assert(std::move(moved_to).run() == 1);  // 目标对象有效

    return 0;
}
```

If all four scenarios pass—constructing a callback yields the correct return value, a void callback executes normally, resources are released after a callback capturing `unique_ptr` is consumed, and the source object becomes empty while the target object remains valid after a move—the skeleton is solid.

---

## Summary

In this article, we built the core skeleton of OnceCallback in five steps. The template partial specialization `OnceCallback<R(Args...)>` decomposes function types through pattern matching. Three data members each have their own duties—`func_` handles type erasure, `status_` manages the tri-state, and `token_` handles the cancellation mechanism. The constructors use `requires not_the_same_t` to protect the move constructor from being hijacked. `run()` uses deducing this to intercept lvalue calls at compile time, and `impl_run()` guarantees the exception safety of consumption semantics through the order of "moving func_ out first, then executing."

In the next article, we will add the first component to the skeleton—`bind_once()`, to implement argument binding.

## References

- [Chromium callback.h source code](https://chromium.googlesource.com/chromium/src/+/HEAD/base/functional/callback.h)
- [cppreference: std::move_only_function](https://en.cppreference.com/w/cpp/utility/functional/move_only_function)
- [P0847R7 - Deducing this proposal](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2021/p0847r7.html)
