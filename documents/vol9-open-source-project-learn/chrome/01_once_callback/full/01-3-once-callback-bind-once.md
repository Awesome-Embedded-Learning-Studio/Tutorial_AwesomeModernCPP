---
title: "OnceCallback 实战（三）：bind_once 实现"
description: "逐行拆解 bind_once 的参数绑定实现——从动机到 lambda 捕获包展开，再到手动展开一个完整的模板实例化例子"
chapter: 1
order: 3
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
reading_time_minutes: 9
prerequisites:
  - "OnceCallback 实战（二）：核心骨架搭建"
  - "OnceCallback 前置知识（二）：std::invoke 与统一调用协议"
  - "OnceCallback 前置知识（三）：Lambda 高级特性"
related:
  - "OnceCallback 实战（四）：取消令牌设计"
---

# OnceCallback 实战（三）：bind_once 实现

## 引言

核心骨架搭好了，`run()` 能消费回调了。但每次构造 OnceCallback 都得传一个签名叫 `R(Args...)` 的可调用对象，所有参数都得在调用时才传入。现实中经常遇到的情况是：某些参数在创建回调时就已经知道了，只有一部分参数要留到调用时才传入。`bind_once` 就是用来解决这个问题的——它把"已知参数"提前塞进回调里，让调用方只需关心"未知参数"。

这一篇我们逐行拆解 `bind_once` 的实现，手动展开一个完整的模板实例化例子，让你看清编译器在背后做了什么。

> **学习目标**
>
> - 理解参数绑定解决了什么问题
> - 逐行理解 `bind_once` 的完整实现
> - 手动展开一个具体的模板实例化，看清编译器做了什么
> - 理解为什么 `Signature` 必须显式指定

---

## 参数绑定解决了什么问题

先看一个没有 `bind_once` 时的场景。假设你有一个三参数函数，但前两个参数在绑定时就能确定：

```cpp
int compute(int x, int y, int z) {
    return x + y + z;
}

// 没有 bind_once：每次调用都得传三个参数
auto cb = OnceCallback<int(int, int, int)>(compute);
int r = std::move(cb).run(10, 20, 30);  // r == 60
```

如果 `x = 10` 和 `y = 20` 在绑定时就确定了，只有 `z` 要留到调用时传入，我们希望得到一个只需传一个参数的 `OnceCallback<int(int)>`。

不用 `bind_once`，你只能手写一个 lambda 包一层：

```cpp
auto wrapped = OnceCallback<int(int)>(
    [](int z) { return compute(10, 20, z); }
);
int r = std::move(wrapped).run(30);  // r == 60
```

能用，但如果参数多了、类型复杂了（比如绑定的是 move-only 的 `unique_ptr`），手写 lambda 就会变得很繁琐。`bind_once` 就是把这个"手写 lambda 包一层"的过程自动化了。

```cpp
auto bound = bind_once<int(int)>(compute, 10, 20);
int r = std::move(bound).run(30);  // r == 60
```

---

## bind_once 的完整实现逐行拆解

对照源码，我们逐行理解 `bind_once` 做了什么。

```cpp
template<typename Signature, typename F, typename... BoundArgs>
auto bind_once(F&& funtor, BoundArgs&&... args) {
    return OnceCallback<Signature>(
        [f = std::forward<F>(funtor),
         ...bound = std::forward<BoundArgs>(args)]
        (auto&&... call_args) mutable -> decltype(auto) {
            return std::invoke(
                std::move(f),
                std::move(bound)...,
                std::forward<decltype(call_args)>(call_args)...
            );
        }
    );
}
```

### 模板参数

`bind_once` 有三个模板参数。`Signature` 是目标回调的函数签名（比如 `int(int)`），必须由调用方显式指定。`F` 是可调用对象的类型（lambda 的闭包类型、函数指针类型等），由编译器从第一个函数参数推导。`BoundArgs...` 是绑定参数的类型包，也是编译器推导的。

### lambda 捕获列表

捕获列表是整个实现中最精巧的部分。`f = std::forward<F>(funtor)` 用初始化捕获（init capture）把可调用对象完美转发到 lambda 闭包里——如果传入的是右值，它被移动进来；如果传入的是左值，它被拷贝进来。

`...bound = std::forward<BoundArgs>(args)` 是 C++20 引入的 lambda init capture pack expansion。它为 `BoundArgs...` 中的每一个类型生成一个对应的捕获变量，每个变量用 `std::forward` 完美转发初始化。假设 `BoundArgs = {int, std::string}`，展开后等价于：

```cpp
[f = std::forward<F>(funtor),
 b1 = std::forward<int>(arg1),
 b2 = std::forward<std::string>(arg2)]
```

### lambda 参数与 mutable

`(auto&&... call_args)` 是泛型 lambda 的转发引用参数——运行时传入的参数通过它接收。`auto&&` 在这里等效于模板参数的 `T&&`，是转发引用。

`mutable` 关键字不可省略——lambda 内部需要调用 `std::move(f)` 和 `std::move(bound)...`，这些操作会修改捕获变量。如果 lambda 是 const 的，捕获变量在内部就是 const 的，没法从 const 对象上 move。

### lambda 体

```cpp
return std::invoke(
    std::move(f),
    std::move(bound)...,
    std::forward<decltype(call_args)>(call_args)...
);
```

`std::invoke` 统一处理所有类型的可调用对象——前置知识（二）里已经讲过了。`std::move(f)` 把可调用对象以右值方式传出，`std::move(bound)...` 把所有绑定参数以右值方式传出（因为 `mutable` lambda 内部的捕获变量是左值，需要用 `std::move` 转成右值），`std::forward<decltype(call_args)>(call_args)...` 把运行时参数完美转发。

绑定参数在前（`std::move(bound)...`），运行时参数在后（`call_args...`），这个顺序很重要——它决定了哪些参数被"预绑定"、哪些参数在调用时才传入。

---

## 手动展开一个具体例子

让我们用一个具体的调用例子，手动展开模板实例化后的完整代码。假设：

```cpp
struct Calc {
    int multiply(int a, int b) { return a * b; }
};

Calc calc;
auto bound = bind_once<int(int)>(&Calc::multiply, &calc, 5);
int r = std::move(bound).run(8);  // r == 40
```

### 模板参数推导

`Signature = int(int)`（显式指定），`F = int (Calc::*)(int, int)`（成员函数指针类型），`BoundArgs = {Calc*, int}`（对象指针 + 第一个参数）。

### lambda 捕获展开

```cpp
[f = std::forward<int (Calc::*)(int, int)>(&Calc::multiply),
 b1 = std::forward<Calc*>(&calc),
 b2 = std::forward<int>(5)]
```

`f` 捕获了成员函数指针，`b1` 捕获了对象指针，`b2` 捕获了绑定的整数 5。

### lambda 体内的 std::invoke 展开

当 `bound.run(8)` 被调用时，`call_args = {8}`。`std::invoke` 收到的是：

```cpp
std::invoke(std::move(f), std::move(b1), std::move(b2), 8)
```

也就是：

```cpp
std::invoke(&Calc::multiply, &calc, 5, 8)
```

`std::invoke` 检测到第一个参数是成员函数指针，第二个参数是指向对象的指针，于是展开为：

```cpp
((*(&calc)).*(&Calc::multiply))(5, 8)
```

等价于 `calc.multiply(5, 8)`，结果为 `40`。

### 生命周期陷阱

注意 `b1 = std::forward<Calc*>(&calc)` 捕获的是一个裸指针 `&calc`。`bind_once` 不会管理 `calc` 的生命周期。如果 `calc` 在回调被调用之前被销毁了，lambda 内部持有的就是一个悬空指针，`std::invoke` 通过悬空指针访问已释放的内存——未定义行为。

Chromium 用 `base::Unretained` 显式标记裸指针的安全性，用 `base::Owned` 接管所有权，用 `base::WeakPtr` 在对象析构时自动取消回调。我们的简化版暂时把安全责任交给调用方。

---

## 为什么签名必须显式指定

你可能注意到 `bind_once<int(int)>(...)` 的 `int(int)` 必须手动写。理想情况下，编译器应该能从可调用对象的签名和绑定参数的数量自动推导出剩余签名。但这件事在 C++ 里比想象中困难。

对于函数指针 `R(*)(Args...)`，可以通过模板偏特化提取参数列表，然后用编译期的"类型列表切片"去掉前 N 个类型。对于有确定签名的仿函数，可以通过 `decltype(&T::operator())` 提取签名。但对于**泛型 lambda**（`[](auto x) { ... }`），它的 `operator()` 本身是模板，不存在唯一确定的签名——编译器无法在类型层面获取"这个 lambda 接受什么参数"的信息。

Chromium 为此写了几百行模板元编程代码来处理各种边界情况。对教学目的来说，让调用方多写一个模板参数 `int(int)` 是更务实的选择。

---

## 小结

这一篇我们逐行拆解了 `bind_once` 的实现。它通过 C++20 的 lambda capture pack expansion 把绑定参数展开到 lambda 的捕获列表中，通过 `std::invoke` 统一处理各种可调用对象（特别是成员函数指针），通过 `mutable` 关键字允许 lambda 内部修改捕获变量。我们手动展开了一个成员函数绑定的完整模板实例化过程，看清了 `std::invoke` 是如何把成员函数指针 + 对象指针展开成普通的成员函数调用的。最后讨论了为什么 `Signature` 必须显式指定——泛型 lambda 的存在让自动推导变得极其复杂。

下一篇我们去看取消令牌的设计——一个用 `shared_ptr` 和 `atomic<bool>` 实现的轻量级取消机制。

## 参考资源

- [Chromium bind_internal.h 源码](https://chromium.googlesource.com/chromium/src/+/HEAD/base/functional/bind_internal.h)
- [cppreference: std::invoke](https://en.cppreference.com/w/cpp/utility/functional/invoke)
- [P0780R2 - Pack Expansion in Lambda Capture](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p0780r2.html)
