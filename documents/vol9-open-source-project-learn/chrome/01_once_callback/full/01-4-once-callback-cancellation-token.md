---
title: "OnceCallback 实战（四）：取消令牌设计"
description: "深入理解 CancelableToken 的设计——用 shared_ptr + atomic<bool> 实现轻量级取消机制，以及它如何集成到 OnceCallback 的执行流程中"
chapter: 1
order: 4
tags:
  - host
  - cpp-modern
  - beginner
  - 回调机制
  - atomic
  - 智能指针
  - 引用计数
difficulty: beginner
platform: host
cpp_standard: [23]
reading_time_minutes: 20
prerequisites:
  - "OnceCallback 实战（二）：核心骨架搭建"
  - "OnceCallback 前置知识速查：C++11/14/17 核心特性回顾"
related:
  - "OnceCallback 实战（五）：then 链式组合"
  - "OnceCallback 实战（六）：测试与性能对比"
---

# OnceCallback 实战（四）：取消令牌设计

## 引言

异步编程里有一个很常见的需求：回调创建之后、执行之前，某个外部条件发生了变化，导致这个回调已经没有意义了——比如回调绑定的对象已经被销毁了，或者任务已经被取消了。这时候我们希望回调在执行前能检查一下"我还该不该执行"，而不是傻乎乎地跑一遍。

这就是取消令牌（cancellation token）的用途。这一篇我们来实现一个简化版的取消令牌，然后看它是怎么集成到 OnceCallback 的执行流程中的。

> **学习目标**
>
> - 理解取消令牌的概念和动机
> - 逐行理解 `CancelableToken` 的实现
> - 理解取消机制在 `impl_run()` 中的集成方式
> - 理解 void 和非 void 回调在取消时的不同行为

---

## 取消令牌的概念

你可以把取消令牌想象成一张"通行证"。创建回调的时候，给回调发一张通行证，通行证上写着"有效"。某个时刻外部条件变化了（比如绑定的对象被销毁），外部代码说"通行证作废了"（调用 `invalidate()`）。之后，所有持有这张通行证的回调在执行前检查时都会发现"通行证已经无效"，跳过执行。

在 Chromium 里，这个"通行证"就是 `WeakPtr` 内部的控制块——`WeakPtr` 指向的对象被销毁后，控制块中的标志位被清除，所有绑定到这个 `WeakPtr` 的回调自动取消。我们的简化版不需要 `WeakPtr` 那么复杂，只需要一个简单的"有效/无效"标志。

### 核心需求

取消令牌需要满足三个条件：多个回调可以共享同一个令牌（一个 `invalidate()` 让所有回调同时失效）、令牌可以被拷贝和移动（方便在 OnceCallback 内部和外部各持有一份）、失效检查是多线程安全的（外部线程可能在一个线程调用 `invalidate()`，回调在另一个线程检查 `is_valid()`）。

---

## CancelableToken 的完整实现

整个取消令牌只有 18 行代码，但每一行都有它的道理。

```cpp
#pragma once
#include <atomic>
#include <memory>

namespace tamcpp::chrome {
class CancelableToken {
    struct Flag {
        std::atomic<bool> valid{true};
    };
    std::shared_ptr<Flag> flag_;

public:
    CancelableToken() : flag_(std::make_shared<Flag>()) {}

    void invalidate() {
        flag_->valid.store(false, std::memory_order_release);
    }

    bool is_valid() const {
        return flag_->valid.load(std::memory_order_acquire);
    }
};
} // namespace tamcpp::chrome
```

### 为什么要用嵌套结构体 Flag

你可能觉得奇怪——为什么不直接在 `CancelableToken` 里放一个 `std::atomic<bool>`？原因是 `shared_ptr` 管理的是一个堆上的对象。如果直接在 `CancelableToken` 里放 `atomic<bool>`，`shared_ptr` 管理的是 `CancelableToken` 本身——但 `CancelableToken` 还有自己的 `flag_` 成员，这就变成了 `shared_ptr<CancelableToken>` 包含 `shared_ptr<Flag>` 的循环。

用嵌套的 `Flag` 结构体把需要共享的状态隔离出来，`shared_ptr` 直接管理 `Flag`，`CancelableToken` 的拷贝和移动都通过 `shared_ptr` 的引用计数自动处理——简洁又正确。另一个好处是 `Flag` 结构体方便后续扩展——如果以后需要加更多原子标志（比如取消原因码），直接往 `Flag` 里加就行。

### shared_ptr 的共享机制

`CancelableToken` 的拷贝构造和拷贝赋值是编译器默认生成的——它做的就是把 `shared_ptr<Flag>` 拷贝一份，引用计数 +1。所有通过拷贝创建的令牌副本共享同一个 `Flag` 对象。当任何一个副本调用 `invalidate()` 时，修改的是同一个 `Flag::valid`，所有副本在下次调用 `is_valid()` 时都会看到 `false`。

```cpp
auto token1 = std::make_shared<CancelableToken>();
auto token2 = token1;  // 共享同一个 Flag

token1->invalidate();
assert(!token2->is_valid());  // token2 也看到了失效
```

### memory_order_acquire/release 配对

`invalidate()` 用 `memory_order_release` 存储 `false`，`is_valid()` 用 `memory_order_acquire` 加载。这是一对配对的内存序。`release` store 保证了在 store 之前的所有写操作（包括调用 `invalidate()` 之前的任何状态修改）对其他线程可见。`acquire` load 保证了在 load 之后的所有读操作能看到 release store 之前的写入。

在我们的场景里，这意味着如果一个线程调用了 `invalidate()`，另一个线程随后调用 `is_valid()` 时一定能看到 `false`——不会有"我刚刚 invalidate 了但 is_valid 还是返回 true"的情况。这是多线程安全的保证。

---

## 集成到 OnceCallback

取消令牌通过 `set_token()` 方法设置到 OnceCallback 中：

```cpp
void set_token(std::shared_ptr<CancelableToken> token) {
    token_ = std::move(token);
}
```

`token_` 是 `shared_ptr<CancelableToken>` 类型，默认是空指针（不启用取消机制）。设置之后，取消令牌的所有权被转移到 OnceCallback 内部。

### is_cancelled() 的完整逻辑

```cpp
[[nodiscard]] bool is_cancelled() const noexcept {
    if (status_ != Status::kValid) return true;
    if (token_ && !token_->is_valid()) return true;
    return false;
}
```

两层检查。第一层：状态不是 kValid 就返回 true——空回调（kEmpty）和已消费回调（kConsumed）都算"已取消"。这很合理——空回调没东西可执行，已消费回调已经执行过了。第二层：如果有取消令牌且令牌失效了，也返回 true。

### impl_run() 中的取消检查

```cpp
ReturnType impl_run(FuncArgs... args) {
    assert(status_ == Status::kValid);

    // 取消检查在执行前
    if (token_ && !token_->is_valid()) {
        status_ = Status::kConsumed;
        func_ = nullptr;
        if constexpr (std::is_void_v<ReturnType>) {
            return;
        } else {
            throw std::bad_function_call{};
        }
    }

    // 正常消费流程...
}
```

取消检查在执行可调用对象**之前**进行。如果已取消，直接消费回调但不执行——`status_` 设为 kConsumed，`func_` 置为 nullptr（析构其内部的可调用对象，释放资源）。

---

## void 与非 void 回调的取消行为差异

这里有一个设计决策值得展开讲——void 回调被取消时直接 return（不执行，也不报错），而非 void 回调被取消时抛出 `std::bad_function_call` 异常。

原因是调用方的期望不同。void 回调的调用方不期望返回值——调用 `std::move(cb).run()` 之后就结束了，不关心回调有没有实际执行。所以被取消的 void 回调直接跳过执行，对调用方是透明的。

非 void 回调的调用方期望拿到返回值——`int result = std::move(cb).run()`。如果回调被取消了，我们没法提供一个有意义的返回值。返回一个默认值（比如 0）可能掩盖错误——调用方以为回调正常执行了，实际上什么都没做。抛异常虽然看起来激进，但它明确告诉调用方"出了问题"，比默默返回错误值更安全。

Chromium 在这里选择直接终止程序（`CHECK` 失败），理由是在 Chrome 的架构中，被取消的回调不应该被调用——调用方应该在调用前检查 `is_cancelled()`。我们选择异常是为了在测试中更容易捕获和验证，而不是直接让程序崩溃。

---

## 使用示例

```cpp
using namespace tamcpp::chrome;

// 创建令牌和回调
auto token = std::make_shared<CancelableToken>();
bool executed = false;

OnceCallback<void()> cb([&executed] { executed = true; });
cb.set_token(token);

// 令牌有效时，正常执行
assert(!cb.is_cancelled());
std::move(cb).run();
assert(executed);  // 回调被执行了

// 创建另一个回调，这次先取消令牌
executed = false;
auto cb2 = OnceCallback<void()>([&executed] { executed = true; });
cb2.set_token(token);
token->invalidate();  // 作废令牌

assert(cb2.is_cancelled());
std::move(cb2).run();  // 取消的 void 回调不执行，不抛异常
assert(!executed);     // 回调没有被执行
```

注意第二个例子中——`cb2.run()` 调用了，但回调内部的 lambda 没有执行。`impl_run()` 在执行前检查到令牌已失效，直接消费回调并 return。

---

## 小结

这一篇我们实现了取消令牌并把它集成到了 OnceCallback 中。`CancelableToken` 用 `shared_ptr` + `atomic<bool>` 实现了轻量级的取消机制——所有令牌副本共享同一个 `Flag` 对象，一个 `invalidate()` 让所有副本同时失效。集成方式是在 `impl_run()` 执行前检查令牌状态——如果已取消，直接消费回调但不执行。void 回调直接 return，非 void 回调抛出 `std::bad_function_call`，这个差异来自调用方对返回值的不同期望。

下一篇我们去看 `then()` 链式组合——OnceCallback 四个功能中所有权设计最精巧的一个。

## 参考资源

- [cppreference: std::shared_ptr](https://en.cppreference.com/w/cpp/memory/shared_ptr)
- [cppreference: std::atomic](https://en.cppreference.com/w/cpp/atomic/atomic)
- [Chromium WeakPtr 文档](https://chromium.googlesource.com/chromium/src/+/main/docs/memory_model/weak_ptr.md)
