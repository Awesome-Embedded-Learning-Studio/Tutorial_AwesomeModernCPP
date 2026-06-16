---
chapter: 1
cpp_standard:
- 23
description: 'Building the OnceCallback Class Skeleton in Five Steps: Template Partial
  Specialization, Data Members, Constructor Constraints, `run()` Consumption Semantics,
  and Query Interface'
difficulty: beginner
order: 2
platform: host
prerequisites:
- OnceCallback 实战（一）：动机与接口设计
- OnceCallback 前置知识（一）：函数类型与模板偏特化
- OnceCallback 前置知识（四）：Concepts 与 requires 约束
- OnceCallback 前置知识（五）：std::move_only_function
- OnceCallback 前置知识（六）：Deducing this
reading_time_minutes: 9
related:
- OnceCallback 实战（三）：bind_once 实现
- OnceCallback 实战（四）：取消令牌设计
tags:
- host
- cpp-modern
- beginner
- 回调机制
- 函数对象
- 模板
title: 'OnceCallback in Practice (Part 2): Building the Core Skeleton'
translation:
  source: documents/vol9-open-source-project-learn/chrome/01_once_callback/full/01-2-once-callback-core-skeleton.md
  source_hash: bbacc2606374047c704315c92e0161ed381c557428b26bbcfcb6e6e970346f7b
  translated_at: '2026-06-16T04:13:01.048920+00:00'
  engine: anthropic
  token_count: 2281
---
# OnceCallback in Practice (Part 2): Building the Core Skeleton

## Introduction

In the previous post, we clarified "why we need OnceCallback" and "what the target API looks like." Now, let's write some code. The task here is to build the class skeleton for OnceCallback from scratch—not implementing all features at once, but in five steps, each layer building on the last. Once the skeleton is complete, we can add components like `bind_once`, cancellation tokens, and `then()` on top of it.

We have already covered all the prerequisite knowledge in the first seven articles. This post is pure practice—we will map every design decision directly into code by examining the actual source.

> **Learning Objectives**
>
> - Build the complete class skeleton for `OnceCallback<R(Args...)>` from scratch.
> - Understand the responsibilities of each data member and method.
> - Master the deducing this implementation for `run()` and the consumption logic for `impl_run()`.

---

## Step 1: Primary Template and Partial Specialization

In Prerequisite (1), we discussed the pattern of "function type + template partial specialization." Now, let's apply it directly to OnceCallback.

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

When you write `OnceCallback<int(int, int)>`, the compiler matches `int(int, int)` to the primary template's `FuncSignature`. It then discovers that the partial specialization can decompose it into `ReturnType = int` and `FuncArgs = {int, int}`, so it selects the specialized version. `FuncSig` is a type alias that stores the complete function signature—we will use this later when declaring `std::move_only_function<FuncSig>`.

---

## Step 2: Data Members—Three Core Storages

Now, let's add data members to the specialized class. OnceCallback needs three things to manage its state.

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

`func_` is the core of type erasure—it wraps various forms of callable objects (lambdas, function pointers, functors) into a unified invocation interface with a `FuncSig` signature. Regardless of what you pass in, `func_` can invoke it using the same `operator()`.

`status_` is a three-state enumeration, distinguishing "never assigned," "ready to call," and "already invoked." Why can't we rely solely on the null check of `func_`? Because the `std::move_only_function`'s `operator bool()` can only distinguish between "null" and "non-null," and the state after moving is unspecified—we covered this in detail in Prerequisite (5).

`token_` is an optional cancellation token used to check if execution should be aborted before the callback runs. It defaults to a null pointer (cancellation disabled) and is set via the `set_token()` method. We will cover this in a dedicated post later.

---

## Step 3: Constructors and requires Constraints

Next, we add the constructors. The key point here is that the template constructor must use a `requires` constraint to prevent it from hijacking the move constructor—we discussed this issue in Prerequisite (4).

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

The **template constructor** is the most commonly used—this is what gets called when you write `OnceCallback<int(int)>([](int x) { return x; })`. `Functor` is deduced as the lambda's closure type. `requires not_the_same_t` ensures that when the input is an `OnceCallback` itself, this template is excluded (letting the move constructor handle it). `std::move(function)` moves the incoming callable object into `func_`, and `status_` is set to `kValid`.

The **default constructor** creates an empty OnceCallback—`status_` is `kEmpty` (determined by the member initializer's default value), and both `func_` and `token_` are empty.

The **move constructor** steals everything from another OnceCallback—`func_` and `token_` are transferred via `std::move`, and `status_` is copied over as well. The key point is that we actively set the source object to `kEmpty` after the move, rather than relying on the unspecified post-move state of `std::move_only_function`.

---

## Step 4: The deducing this Implementation of run()

This step is the soul of the entire skeleton. `run()` uses deducing this to intercept lvalue calls at compile time, while forwarding rvalue calls to the internal `impl_run()`.

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

When the caller writes `cb.run(args)`, `Self` is deduced as `OnceCallback&` (an lvalue reference). `static_assert` triggers, and the error message directly tells the caller what to do. When writing `std::move(cb).run(args)`, `Self` is deduced as `OnceCallback` (non-reference), compilation succeeds, and it forwards to `impl_run`.

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

Several key details are worth noting.

First, look at the consumption order—`impl_run` first moves `func_` out into a local variable `functor`. Then, it nullifies `func_`, sets `status_` to kConsumed, and finally executes `functor`. This order is critical: secure the callable object and mark the state before execution. Even if the callable object throws an exception internally, `status_` is already `kConsumed`, ensuring the callback doesn't end up in an inconsistent state.

Next, look at `if constexpr`—void return types cannot be assigned or returned in the conventional way. `if constexpr (std::is_void_v<ReturnType>)` selects the branch at compile time: the void case takes the "call but don't assign" path, while the non-void case takes the "call and assign to return" path. This is the standard pattern we discussed in the cheat sheet.

Finally, the cancellation check—we verify the cancellation token before execution. If cancelled, we consume the callback but do not execute it. For void returns, we simply `return`; for non-void returns, we throw `std::bad_function_call`. Throwing an exception for non-void might seem aggressive, but the reasoning is solid: the caller expects a return value, but we cannot provide a meaningful one, so throwing an exception is safer than returning an undefined value.

---

## Step 5: Query Interfaces

Finally, let's add a set of query methods so the caller can check the callback's status before execution.

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

The logic for `is_cancelled()` is: return true if the state is not kValid (both empty and consumed callbacks count as "cancelled"), or if there is a token and it is expired. `maybe_valid()` is currently just `!is_cancelled()`. `is_null()` only checks if it was never assigned. `operator bool()` combines both empty and cancelled conditions.

All query methods are marked with `[[nodiscard]]`—calling these methods is intended for judgment based on the return value, so ignoring the return value is likely a mistake. The `explicit` keyword prevents implicit conversion to `bool`.

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

If these four scenarios pass—constructing a callback yields the correct return value, void callbacks execute normally, resources are released after a callback capturing `unique_ptr` is used, and the source becomes empty while the target is valid after a move—then the skeleton is solid.

---

## Summary

In this post, we built the core skeleton of OnceCallback in five steps. Template partial specialization `OnceCallback<R(Args...)>` decomposes the function type via pattern matching. Three data members each have their duties—`func_` handles type erasure, `status_` manages the three-state logic, and `token_` handles the cancellation mechanism. The constructors use `requires not_the_same_t` to protect the move constructor from being hijacked. `run()` uses deducing this to intercept lvalue calls at compile time, and `impl_run()` guarantees exception safety for consumption semantics via the "move func_ out first, then execute" order.

In the next post, we will add the first component to the skeleton—`bind_once()`—to implement argument binding.

## Reference Resources

- [Chromium callback.h source](https://chromium.googlesource.com/chromium/src/+/HEAD/base/functional/callback.h)
- [cppreference: std::move_only_function](https://en.cppreference.com/w/cpp/utility/functional/move_only_function)
- [P0847R7 - Deducing this proposal](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2021/p0847r7.html)
