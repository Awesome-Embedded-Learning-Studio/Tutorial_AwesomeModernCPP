---
title: "OnceCallback 实战（二）：核心骨架搭建"
description: "从零开始五步搭建 OnceCallback 的类骨架——模板偏特化、数据成员、构造函数约束、run() 消费语义、查询接口"
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
cpp_standard: [23]
reading_time_minutes: 30
prerequisites:
  - "OnceCallback 实战（一）：动机与接口设计"
  - "OnceCallback 前置知识（一）：函数类型与模板偏特化"
  - "OnceCallback 前置知识（四）：Concepts 与 requires 约束"
  - "OnceCallback 前置知识（五）：std::move_only_function"
  - "OnceCallback 前置知识（六）：Deducing this"
related:
  - "OnceCallback 实战（三）：bind_once 实现"
  - "OnceCallback 实战（四）：取消令牌设计"
---

# OnceCallback 实战（二）：核心骨架搭建

## 引言

上一篇我们搞清楚了"为什么需要 OnceCallback"和"目标 API 长什么样"。现在我们正式上手写代码。这一篇的任务是把 OnceCallback 的类骨架从零搭建起来——不是一口气写完所有功能，而是分五步，每一步在前一步的基础上加一层。搭完骨架之后，后续的 `bind_once`、取消令牌、`then()` 都是往这个骨架上加组件。

所有前置知识我们在前面七篇文章里都已经讲透了。这一篇是纯实战——我们直接对照实际源码，把每一个设计决策落实到代码上。

> **学习目标**
>
> - 从零搭建 `OnceCallback<R(Args...)>` 的完整类骨架
> - 理解每个数据成员和方法的职责
> - 掌握 `run()` 的 deducing this 实现和 `impl_run()` 的消费逻辑

---

## 第一步：主模板与偏特化

前置知识（一）里我们已经讲过"函数类型 + 模板偏特化"这个模式。现在把它直接应用到 OnceCallback 上。

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

当你写 `OnceCallback<int(int, int)>` 时，编译器把 `int(int, int)` 匹配到主模板的 `FuncSignature`，然后发现偏特化能把它拆成 `ReturnType = int`、`FuncArgs = {int, int}`，于是选择偏特化版本。`FuncSig` 是一个类型别名，保存了完整的函数签名——后面声明 `std::move_only_function<FuncSig>` 时会用到。

---

## 第二步：数据成员——三个核心存储

现在往偏特化类里添加数据成员。OnceCallback 需要三个东西来管理自己的状态。

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

`func_` 是类型擦除的核心——它把各种不同形态的可调用对象（lambda、函数指针、仿函数）统一包装成 `FuncSig` 签名的调用接口。不管你传入什么，`func_` 都能用同一个 `operator()` 调用它。

`status_` 是一个三态枚举，区分"从未赋值"、"随时可调用"和"已经调用过了"。为什么不能只靠 `func_` 的判空？因为 `std::move_only_function` 的 `operator bool()` 只能区分"空"和"非空"两种状态，而且移动后的状态未指定——前置知识（五）里已经详细讲过了。

`token_` 是一个可选的取消令牌，用于在回调执行前检查是否应该取消执行。默认是空指针（不启用取消机制），通过 `set_token()` 方法设置。这个我们后面有专门一篇讲。

---

## 第三步：构造函数与 requires 约束

接下来添加构造函数。这里的关键点是模板构造函数必须用 `requires` 约束来防止它劫持移动构造函数——前置知识（四）里已经讲过这个问题了。

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

让我们逐个理解这些构造函数。

**模板构造函数**是最常用的——当你写 `OnceCallback<int(int)>([](int x) { return x; })` 时调用的就是这个。`Functor` 被推导为 lambda 的闭包类型，`requires not_the_same_t` 确保当传入的是 `OnceCallback` 本身时模板被排除（让移动构造函数来处理）。`std::move(function)` 把传入的可调用对象移入 `func_`，`status_` 设为 `kValid`。

**默认构造函数**创建一个空的 OnceCallback——`status_` 是 `kEmpty`（由成员初始化器的默认值决定），`func_` 和 `token_` 都是空的。

**移动构造函数**从另一个 OnceCallback 那里偷走所有内容——`func_` 和 `token_` 通过 `std::move` 转移，`status_` 也一起复制过来。关键点是移动后源对象被设为 `kEmpty`——这是我们主动做的，不是依赖 `std::move_only_function` 的移动后状态。

---

## 第四步：run() 的 deducing this 实现

这一步是整个骨架的灵魂。`run()` 利用 deducing this 在编译期拦截左值调用，通过右值调用时转发到内部的 `impl_run()`。

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

当调用方写 `cb.run(args)` 时，`Self` 被推导为 `OnceCallback&`（左值引用），`static_assert` 触发，报错信息直接告诉调用方该怎么做。当写 `std::move(cb).run(args)` 时，`Self` 被推导为 `OnceCallback`（非引用），编译通过，转发到 `impl_run`。

`impl_run` 是真正执行回调的地方：

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

有几个关键细节值得注意。

先看消费顺序——`impl_run` 先把 `func_` move 出来作为局部变量 `functor`，然后把 `func_` 置空、`status_` 设为 kConsumed，最后执行 `functor`。这个顺序很重要：先把可调用对象拿出去、状态标记好，再执行。即使可调用对象内部抛出异常，`status_` 也已经是 `kConsumed` 了，回调不会处于不一致的状态。

再看 `if constexpr`——void 返回类型不能用常规方式赋值和返回。`if constexpr (std::is_void_v<ReturnType>)` 在编译期选择分支，void 的情况走"调用但不赋值"的路径，非 void 的情况走"调用并赋值给 return"的路径。这是我们速查篇里讲过的标准模式。

最后看取消检查——在执行前检查取消令牌。如果已取消，直接消费回调但不执行。void 返回直接 `return`，非 void 返回抛出 `std::bad_function_call`。非 void 的抛异常行为可能看起来激进，但理由很充分：调用方期望得到一个返回值，但我们无法提供一个有意义的值，所以抛异常比返回未定义值更安全。

---

## 第五步：查询接口

最后加上一组查询方法，让调用方可以在执行前检查回调的状态。

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

`is_cancelled()` 的逻辑是：状态不是 kValid 就返回 true（空回调和已消费回调都算"已取消"），如果有令牌且令牌失效也返回 true。`maybe_valid()` 暂时就是 `!is_cancelled()`。`is_null()` 只检查是否从未被赋值。`operator bool()` 综合了空和取消两个条件。

所有查询方法都标注了 `[[nodiscard]]`——调用这些方法就是为了拿返回值做判断，忽略返回值的调用大概率是手滑写错了。`explicit` 关键字防止隐式转换到 `bool`。

---

## 验证核心骨架

骨架搭完了，我们来快速验证几个基本场景：

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

如果这四个场景都通过——构造回调能拿到正确的返回值、void 回调能正常执行、捕获 `unique_ptr` 的回调用完之后资源被释放、移动后源对象变空目标对象有效——骨架就没有问题。

---

## 小结

这一篇我们分五步搭建了 OnceCallback 的核心骨架。模板偏特化 `OnceCallback<R(Args...)>` 通过模式匹配拆解函数类型。三个数据成员各司其职——`func_` 负责类型擦除、`status_` 负责三态管理、`token_` 负责取消机制。构造函数用 `requires not_the_same_t` 保护移动构造函数不被劫持。`run()` 用 deducing this 在编译期拦截左值调用，`impl_run()` 通过"先 move 出 func_ 再执行"的顺序保证消费语义的异常安全。

下一篇我们往骨架上加第一个组件——`bind_once()`，实现参数绑定。

## 参考资源

- [Chromium callback.h 源码](https://chromium.googlesource.com/chromium/src/+/HEAD/base/functional/callback.h)
- [cppreference: std::move_only_function](https://en.cppreference.com/w/cpp/utility/functional/move_only_function)
- [P0847R7 - Deducing this 提案](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2021/p0847r7.html)
