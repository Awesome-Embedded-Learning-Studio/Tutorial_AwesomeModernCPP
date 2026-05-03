---
title: "OnceCallback 前置知识（三）：Lambda 高级特性"
description: "深入讲解 mutable lambda、初始化捕获（init capture）、C++20 lambda capture pack expansion 和泛型 lambda——OnceCallback 中 bind_once 与 then() 的核心实现技巧"
chapter: 0
order: 3
tags:
  - host
  - cpp-modern
  - intermediate
  - lambda
  - 函数对象
difficulty: intermediate
platform: host
cpp_standard: [14, 17, 20, 23]
reading_time_minutes: 25
prerequisites:
  - "OnceCallback 前置知识速查：C++11/14/17 核心特性回顾"
related:
  - "OnceCallback 实战（三）：bind_once 实现"
  - "OnceCallback 实战（五）：then 链式组合"
---

# OnceCallback 前置知识（三）：Lambda 高级特性

## 引言

上一篇速查里我们快速过了一遍 lambda 的基础语法。这篇我们要深入到 OnceCallback 实现中真正用到的三个 lambda 高级特性——它们不是什么"锦上添花"的语法糖，而是 `bind_once` 和 `then()` 得以实现的**关键机制**。如果不理解这些特性，后面的实现代码你会看得很痛苦。

具体来说，我们要讲三件事：`mutable` lambda 为什么在 OnceCallback 里不能省、初始化捕获（init capture）怎么让 `then()` 把整个 OnceCallback 对象搬进 lambda 里、以及 C++20 的 lambda capture pack expansion 怎么让 `bind_once` 的代码量缩减到原来的三分之一。

> **学习目标**
>
> - 理解 `mutable` lambda 与 const lambda 的行为差异及其在 OnceCallback 中的必要性
> - 掌握初始化捕获的语法和语义，理解 `self = std::move(*this)` 的所有权转移
> - 学会 C++20 lambda capture pack expansion，理解 `bind_once` 的简洁实现
> - 理解泛型 lambda `(auto&&... args)` 的本质

---

## mutable lambda：为什么在 OnceCallback 里不能省

Lambda 默认生成的 `operator()` 是 `const` 的——这意味着 lambda 内部不能修改值捕获的变量。加 `mutable` 关键字后，`operator()` 变成非 const 的，允许修改。

### 行为对比

```cpp
int x = 10;

// const lambda：不能修改捕获的变量
auto f1 = [x]() {
    // x++;  // 编译错误：operator() 是 const 的
    return x;
};

// mutable lambda：可以修改捕获的变量
auto f2 = [x]() mutable {
    x++;       // OK：operator() 是非 const 的
    return x;
};

f2();  // 返回 11，x 的副本被修改
f2();  // 返回 12，同一个 lambda 对象再次调用，x 继续增加
```

注意第二个例子——`mutable` lambda 的状态在多次调用之间是保持的。这是因为 lambda 的闭包对象持有捕获变量的副本，`mutable` 让 `operator()` 可以修改这些副本。

### 在 OnceCallback 中的角色

`bind_once` 和 `then()` 的 lambda 都必须声明为 `mutable`。原因是这些 lambda 的捕获列表里包含 `OnceCallback` 对象（通过 `self = std::move(*this)` 捕获），而调用 `std::move(self).run()` 会修改 `self` 的内部状态（把 `status_` 从 kValid 改为 kConsumed）。如果 lambda 是 const 的，`self` 在 lambda 内部就是 const 引用，你没法在 const 对象上调用修改状态的操作——编译器会直接报错。

简单说：**一旦 lambda 捕获了需要在调用时被修改的对象（比如 OnceCallback），就必须加 `mutable`**。这不是可选的——不加就编译不过。

```cpp
// then() 内部的 lambda——mutable 不可省略
[self = std::move(*this), cont = std::forward<Next>(next)]
(FuncArgs... args) mutable -> NextRet {
    // self 在这里需要被修改（run() 会消费它）
    auto mid = std::move(self).run(std::forward<FuncArgs>(args)...);
    return std::invoke(std::move(cont), std::move(mid));
}
```

---

## 初始化捕获（Init Capture）：把对象搬进 lambda

C++14 引入了初始化捕获（init capture）语法，允许你在捕获列表中执行表达式并用结果初始化一个捕获变量。语法是 `name = expression`。

### 和简单捕获的区别

简单捕获 `[x]` 只能捕获已经存在的变量，而且是拷贝或引用语义。初始化捕获 `[name = expr]` 允许你做三件简单捕获做不到的事：

```cpp
auto ptr = std::make_unique<int>(42);

// 1. 移动捕获——把 unique_ptr 搬进 lambda
auto f1 = [p = std::move(ptr)]() { return *p; };
// ptr 在外面已经被搬空了

// 2. 存储计算结果
std::string s = "hello";
auto f2 = [len = s.size()]() { return len; };  // len 是 size_t 类型

// 3. 捕获不存在于外部的变量
auto f3 = [counter = 0]() mutable { return ++counter; };  // counter 是 lambda 自己的变量
```

### 在 OnceCallback 中的使用

`then()` 的实现用初始化捕获做了两件关键的事情。

第一件是把整个 OnceCallback 对象搬进 lambda：

```cpp
self = std::move(*this)
```

`*this` 是当前 OnceCallback 对象，`std::move(*this)` 把它转成右值，初始化捕获 `self = std::move(*this)` 触发 OnceCallback 的移动构造，把 `func_`、`status_`、`token_` 全部搬进 lambda 的闭包对象里。移动之后，`*this`（原来的 OnceCallback 对象）进入"被移走"的状态——`func_` 和 `token_` 已经是空的或 null 了。

第二件是把后续回调搬进来：

```cpp
cont = std::forward<Next>(next)
```

`std::forward<Next>(next)` 保持 `next` 的值类别——如果传入的是右值，它就是移动；如果传入的是左值，它就是拷贝。通常 `then()` 接受的都是临时 lambda（右值），所以这里是移动。

### 所有权链

把这两件捕获放在一起看，`then()` 创建的新 lambda 持有了原回调和后续回调的**完整所有权**。这个 lambda 又被存入一个新的 `OnceCallback` 的 `std::move_only_function` 里。整个所有权链条是这样的：

```text
新 OnceCallback -> move_only_function -> lambda 闭包 -> [原 OnceCallback + 后续回调]
```

每一层都通过移动语义传递所有权，没有任何共享或拷贝。这就是 OnceCallback 的 move-only 语义在 `then()` 中的完整体现——所有权从外到内层层传递，没有破绽。

---

## C++20 Lambda Capture Pack Expansion：bind_once 的简洁秘诀

这是这一篇里最重要的特性，也是 `bind_once` 得以用几行代码实现的关键。C++20 之前，可变参数模板的参数包**不能**直接展开到 lambda 的捕获列表里——你得先用 `std::tuple` 把参数打包存起来，然后在 lambda 内部用 `std::apply` 展开调用。

### 旧方案（C++17）：tuple + apply

```cpp
template<typename F, typename... BoundArgs>
auto bind_old(F&& f, BoundArgs&&... args) {
    // 把所有绑定参数打包进 tuple
    return [f = std::forward<F>(f),
            tup = std::make_tuple(std::forward<BoundArgs>(args)...)]
        (auto&&... call_args) mutable -> decltype(auto) {
        // 用 std::apply 展开 tuple 并调用
        return std::apply([&](auto&... bound) -> decltype(auto) {
            return f(bound..., std::forward<decltype(call_args)>(call_args)...);
        }, tup);
    };
}
```

能工作，但代码膨胀了不少——你需要一个中间的 tuple、一个 `std::apply` 调用、以及一个嵌套 lambda 来处理展开。

### 新语法（C++20）：直接在捕获列表里展开包

C++20 允许在 lambda 的初始化捕获中使用包展开。语法是 `...name = expression`，效果是为参数包中的每一个类型生成一个对应的捕获变量。

```cpp
template<typename F, typename... BoundArgs>
auto bind_new(F&& f, BoundArgs&&... args) {
    return [f = std::forward<F>(f),
            ...bound = std::forward<BoundArgs>(args)]  // ← 包展开！
        (auto&&... call_args) mutable -> decltype(auto) {
        return std::invoke(std::move(f),
                          std::move(bound)...,         // ← 展开捕获变量
                          std::forward<decltype(call_args)>(call_args)...);
    };
}
```

### 手动展开一个具体例子

假设我们调用 `bind_new([](int a, std::string b, int c) { ... }, 10, std::string("hello"))`，此时 `BoundArgs = {int, std::string}`。编译器把包展开 `...bound = std::forward<BoundArgs>(args)` 展开成：

```cpp
[f = std::forward<F>(f),
 b1 = std::forward<int>(arg1),              // int 直接转发
 b2 = std::forward<std::string>(arg2)]      // std::string 移动转发
(auto&&... call_args) mutable -> decltype(auto) {
    return std::invoke(std::move(f),
                      std::move(b1), std::move(b2),    // 展开捕获变量
                      std::forward<decltype(call_args)>(call_args)...);
}
```

每个绑定参数变成了 lambda 闭包中的一个独立成员变量，在 lambda 被调用时通过 `std::move(bound)...` 一起展开传给 `std::invoke`。

### 为什么用 std::move 而不是 std::forward

你可能注意到 lambda 内部用的是 `std::move(bound)...` 而不是 `std::forward<BoundArgs>(bound)...`。原因是 lambda 是 `mutable` 的，捕获变量 `bound` 在 lambda 内部是**左值**（具名变量永远是左值）。由于我们希望绑定参数在回调被调用时以右值的方式传出（触发移动语义），所以用 `std::move` 把它们转成右值。如果用 `std::forward`，因为 `bound` 已经是左值了，`std::forward` 只会返回左值引用——移动语义就丢失了。

---

## 泛型 Lambda：auto&& 作为转发引用

`bind_once` 内部的 lambda 用 `(auto&&... call_args)` 来接受运行时传入的参数。这里的 `auto&&` 是转发引用——因为 `auto` 在 lambda 参数中等同于模板参数，所以 `auto&&` 具有和 `T&&`（T 是模板参数时）相同的推导规则。

```cpp
auto f = [](auto&& x) {
    // x 是转发引用
    // 传入左值：auto = int&, x 的类型是 int&（左值引用）
    // 传入右值：auto = int, x 的类型是 int&&（右值引用）
};

int v = 10;
f(v);       // x 绑定到左值
f(10);      // x 绑定到右值
```

`auto&&...` 的组合意味着这个 lambda 可以接受任意数量、任意类型的参数，同时保持每个参数的值类别信息。配合 `std::forward<decltype(call_args)>(call_args)...`，这些参数可以被完美转发到最终的可调用对象。

---

## 小结

这一篇我们掌握了 OnceCallback 实现中最关键的三个 lambda 特性。`mutable` lambda 允许在 lambda 内部修改捕获的对象，OnceCallback 的 `bind_once` 和 `then()` 必须用它才能在 lambda 里调用 `std::move(self).run()` 修改回调状态。初始化捕获 `name = expr` 让 `then()` 能把整个 OnceCallback 对象通过移动语义搬进 lambda 闭包，建立起完整的所有权链。C++20 的 lambda capture pack expansion `...name = expr` 让 `bind_once` 的绑定参数可以直接展开到捕获列表中，替代了 C++17 时代臃肿的 tuple + apply 方案。

下一篇我们去看 Concepts 和 `requires` 约束——它们是保护 OnceCallback 的模板构造函数不被错误匹配的关键防御手段。

## 参考资源

- [cppreference: Lambda 表达式](https://en.cppreference.com/w/cpp/language/lambda)
- [P0780R2 - Pack Expansion in Lambda Init-Capture](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p0780r2.html)
- [cppreference: std::forward](https://en.cppreference.com/w/cpp/utility/forward)
