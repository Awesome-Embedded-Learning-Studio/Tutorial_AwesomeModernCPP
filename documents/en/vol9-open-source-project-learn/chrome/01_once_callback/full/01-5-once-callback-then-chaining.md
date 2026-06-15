---
chapter: 1
cpp_standard:
- 23
description: A line-by-line breakdown of the ownership chain design in `then()` —
  from pipeline thinking to `void`/non-`void` branch handling, understanding the most
  elegant ownership management in `OnceCallback`.
difficulty: beginner
order: 5
platform: host
prerequisites:
- OnceCallback 实战（二）：核心骨架搭建
- OnceCallback 前置知识（二）：std::invoke 与统一调用协议
- OnceCallback 前置知识（三）：Lambda 高级特性
reading_time_minutes: 8
related:
- OnceCallback 实战（六）：测试与性能对比
tags:
- host
- cpp-modern
- beginner
- 回调机制
- 函数对象
- 模板
title: 'OnceCallback in Practice (Part 5): Chaining with `then`'
translation:
  engine: anthropic
  source: documents/vol9-open-source-project-learn/chrome/01_once_callback/full/01-5-once-callback-then-chaining.md
  source_hash: f5dd3bfba9d0e474bf30a0bb13f7b8a1d98951a7bef96793ec9db952dd6d2034
  token_count: 1629
  translated_at: '2026-05-26T12:25:30.907930+00:00'
---
# OnceCallback in Practice (Part 5): Chaining with `then`

## Introduction

`then()` allows us to compose two callbacks into a pipeline — the output of the first callback becomes the input of the second. It sounds simple, but it has the most intricate ownership design of the four `OnceCallback` features. Because `OnceCallback` is move-only, `then()` must transfer the original callback's ownership entirely into the new callback, without any sharing or leaking.

In this article, we start from a pipeline mindset and break down the `then()` implementation line by line, focusing on the ownership chain and the handling of void and non-void branches.

> **Learning Objectives**
>
> - Understand the pipeline semantics and ownership chain design of `then()`
> - Understand the complete implementation of `then()` line by line
> - Understand the special handling for void-prefix callbacks
> - Compare the choice of using `&&` qualification in `then()` versus deducing this in `run()`

---

## Pipeline Thinking: The Semantics of `then()`

If you have used Unix pipes, the semantics of `then()` are quite intuitive:

```bash
# Unix 管道：cmd1 的输出是 cmd2 的输入
echo "hello" | tr 'h' 'H' | wc -c
```

`then()` does the exact same thing — the output of callback A is the input of callback B. Expressed in code:

```cpp
auto pipeline = OnceCallback<int(int, int)>([](int a, int b) {
    return a + b;          // 第一步：3 + 4 = 7
}).then([](int sum) {
    return sum * 2;        // 第二步：7 * 2 = 14
});

int result = std::move(pipeline).run(3, 4);  // result == 14
```

`then()` chains two independent callbacks into a new callback. When we invoke the new callback, the entire A → B flow executes automatically.

---

## Ownership Is the Core Challenge of `then()`

The new chained callback must hold **ownership** of both the original and the subsequent callbacks — otherwise, the original callback might be consumed prematurely elsewhere, breaking the pipeline. Since `OnceCallback` is move-only, `then()` must consume `*this` (the original callback) and `next` (the subsequent callback), transferring both ownerships into a new lambda closure.

The entire ownership chain looks like this:

```mermaid
graph LR
    A["新 OnceCallback"] --> B["move_only_function"] --> C["lambda 闭包"] --> D["原回调 + 后续回调"]
```

Each layer transfers ownership via move semantics, without any sharing or copying. This is the complete embodiment of move-only semantics in `then()`.

---

## Line-by-Line Breakdown of the Complete `then()` Implementation

```cpp
template<typename ReturnType, typename... FuncArgs>
template<typename Next>
auto OnceCallback<ReturnType(FuncArgs...)>::then(Next&& next) && {
    using NextType = std::decay_t<Next>;

    if constexpr (std::is_void_v<ReturnType>) {
        using NextRet = std::invoke_result_t<NextType>;
        return OnceCallback<NextRet(FuncArgs...)>(
            [self = std::move(*this),
             cont = std::forward<Next>(next)]
            (FuncArgs... args) mutable -> NextRet {
                std::move(self).run(std::forward<FuncArgs>(args)...);
                return std::invoke(std::move(cont));
            });
    } else {
        using NextRet = std::invoke_result_t<NextType, ReturnType>;
        return OnceCallback<NextRet(FuncArgs...)>(
            [self = std::move(*this),
             cont = std::forward<Next>(next)]
            (FuncArgs... args) mutable -> NextRet {
                auto mid = std::move(self).run(std::forward<FuncArgs>(args)...);
                return std::invoke(std::move(cont), std::move(mid));
            });
    }
}
```

### Function Signature: Rvalue Qualification

```cpp
auto then(Next&& next) &&
```

The trailing `&&` makes this an rvalue-qualified member function — it can only be called on an `std::move(cb).then(next)` or a temporary object `.then(next)`. If the caller writes `cb.then(next)` (an lvalue call), the compiler directly reports "no matching overloaded function." This is another way to express consume semantics — unlike `run()` which uses deducing this, `then()` does not need to distinguish between lvalues and rvalues to provide different error messages; using a ref-qualifier is more concise.

### `std::decay_t<Next>`: Decaying to Remove References

```cpp
using NextType = std::decay_t<Next>;
```

`Next` might be `SomeLambda&&` (an rvalue reference) or `SomeLambda&` (an lvalue reference). `std::decay_t` strips the reference to get the bare lambda type. We then use `NextType` for type queries.

### The Two Branches of `if constexpr`

The core difference in `then()` is whether the original callback's return type is void.

**Non-void branch**: The original callback returns a value, and this value needs to be passed to the subsequent callback.

```cpp
using NextRet = std::invoke_result_t<NextType, ReturnType>;
```

`std::invoke_result_t<NextType, ReturnType>` deduces at compile time "what type is returned when a value of type `ReturnType` is passed to a callable of type `NextType`." This becomes the return type of the new callback.

The execution flow inside the lambda: first invoke the original callback to get the intermediate result `mid`, then pass `mid` to the subsequent callback.

```cpp
auto mid = std::move(self).run(std::forward<FuncArgs>(args)...);
return std::invoke(std::move(cont), std::move(mid));
```

**Void branch**: The original callback has no return value, and the subsequent callback takes no arguments.

```cpp
using NextRet = std::invoke_result_t<NextType>;
```

`std::invoke_result_t<NextType>` deduces "what type is returned when invoking `NextType` with no arguments."

The execution flow inside the lambda: first execute the original callback (without capturing the return value), then execute the subsequent callback (without passing arguments).

```cpp
std::move(self).run(std::forward<FuncArgs>(args)...);
return std::invoke(std::move(cont));
```

### Lambda Capture: The Core of Ownership

```cpp
[self = std::move(*this), cont = std::forward<Next>(next)]
```

`self = std::move(*this)` is the key to the entire ownership chain — it moves **all contents** of the current `OnceCallback` object (`func_`, `status_`, `token_`) into the lambda's closure object. After the move, the current object enters a "moved-from" state — `func_` and `token_` have already been moved away.

`cont = std::forward<Next>(next)` also moves the subsequent callback into the lambda closure. `std::forward` preserves the value category of `next` — rvalues are moved, lvalues are copied.

This lambda is then passed to a new `OnceCallback<NextRet(FuncArgs...)>` constructor and stored in the new callback's `std::move_only_function`. The type-erasure capability of `move_only_function` ensures that no matter the actual type of the lambda, it can be stored uniformly.

---

## Multi-Level Pipelines

`then()` can be called in a chain to form a multi-level pipeline:

```cpp
using namespace tamcpp::chrome;
auto pipeline = OnceCallback<int(int)>([](int x) {
    return x * 2;
}).then([](int x) {
    return x + 10;
}).then([](int x) {
    return std::to_string(x);
});

std::string result = std::move(pipeline).run(5);
// 5 * 2 = 10, 10 + 10 = 20, to_string(20) = "20"
```

Each `then()` creates a new `OnceCallback`, internally capturing the callback from the previous step in a nested fashion. When the outermost `run()` is invoked, the execution process unfolds recursively: the outermost callback is `run()` → its lambda executes → the lambda internally calls `std::move(self).run()` on the previous level → then calls on the level above that → all the way down to the base level.

Performance-wise, each level of `then()` adds one level of `std::move_only_function` indirection. This is completely acceptable for pipelines of two to three levels. If the pipeline depth exceeds ten levels, we might need to consider a flattened pipeline structure to avoid excessive nesting — but that is beyond the scope of our current discussion.

---

## Common Pitfalls

### `mutable` Cannot Be Omitted

Inside the lambda, we need to call `std::move(self).run()` — this operation modifies the state of `self` (changing status from kValid to kConsumed). If the lambda is const (without `mutable`), `self` becomes a const reference inside, and we cannot call state-modifying operations on a const object, causing a direct compilation failure.

### The State After `self = std::move(*this)`

After the move, both `func_` and `token_` of the current `OnceCallback` object have been moved away — they are in a "moved-from" state. `status_` is not explicitly set to kEmpty, but retains its original value. However, because `func_` has already been moved away, the current object is effectively unusable — any operations on it are undefined behavior (UB). The `&&` qualification on `then()` guarantees that the caller cannot continue using the original object after calling `then()`.

### Why Use `std::invoke` Instead of Direct Invocation

`cont` is a normal callable object (usually a lambda), so direct `cont(mid)` would also work. But `std::invoke` is defensive programming — if someone passes a member function pointer as the subsequent callback, direct call syntax would fail, whereas `std::invoke` would not. Uniformly using `std::invoke` ensures correct behavior regardless of what callable object is passed in.

---

## Summary

In this article, we broke down the complete implementation of `then()`. Its core challenge is ownership management — by using `self = std::move(*this)` to move the entire original callback into the lambda closure, we establish a complete ownership chain. `if constexpr` handles the different semantics of void and non-void return types — void callbacks pass no arguments to the subsequent callback, while non-void callbacks pass the intermediate result. `then()` uses `&&` qualification to express consume semantics (more concise than deducing this in `run()`, since we do not need custom error messages), and the `mutable` keyword cannot be omitted (because the internal logic needs to modify the state of `self`).

The next article is the final one in this series — we will use systematic test cases to verify the entire implementation and compare the performance differences with the original Chromium version.

## References

- [Chromium callback.h source code](https://chromium.googlesource.com/chromium/src/+/HEAD/base/functional/callback.h)
- [cppreference: std::invoke](https://en.cppreference.com/w/cpp/utility/functional/invoke)
- [cppreference: if constexpr](https://en.cppreference.com/w/cpp/language/if)
