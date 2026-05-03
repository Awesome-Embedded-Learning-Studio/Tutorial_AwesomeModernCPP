---
title: "OnceCallback 前置知识（四）：Concepts 与 requires 约束"
description: "从模板构造函数劫持移动构造函数的真实问题出发，理解 Concepts 和 requires 约束如何保护 OnceCallback 的构造函数正确匹配"
chapter: 0
order: 4
tags:
  - host
  - cpp-modern
  - intermediate
  - concepts
  - 模板
difficulty: intermediate
platform: host
cpp_standard: [20]
reading_time_minutes: 11
prerequisites:
  - "OnceCallback 前置知识速查：C++11/14/17 核心特性回顾"
  - "OnceCallback 前置知识（一）：函数类型与模板偏特化"
related:
  - "OnceCallback 实战（二）：核心骨架搭建"
  - "OnceCallback 前置知识（五）：std::move_only_function"
---

# OnceCallback 前置知识（四）：Concepts 与 requires 约束

## 引言

OnceCallback 的构造函数上有这么一行看起来很多余的约束：

```cpp
template<typename Functor>
    requires not_the_same_t<Functor, OnceCallback>
explicit OnceCallback(Functor&& function);
```

你可能会问——为什么不直接写 `template<typename Functor>` 就完事了？多加一个 `requires not_the_same_t` 是在防什么？

这一篇我们就来回答这个问题。答案涉及 C++ 重载决议中一个不太为人知的陷阱：**模板构造函数可能在某些情况下劫持移动构造函数的调用**。Concepts 和 `requires` 约束是 C++20 给我们的防御武器。

> **学习目标**
>
> - 理解模板构造函数与移动构造函数之间的重载竞争问题
> - 掌握 concept 的基本语法和 `requires` 子句的用法
> - 能够解读 `not_the_same_t` 的设计意图和每一行代码的含义

---

## 问题引入：模板构造函数的"越位"

### 场景还原

假设我们有一个简单的包装类，接受任意可调用对象：

```cpp
template<typename FuncSignature>
class Callback;

template<typename R, typename... Args>
class Callback<R(Args...)> {
public:
    // 模板构造函数：接受任意可调用对象
    template<typename Functor>
    explicit Callback(Functor&& f) {
        // 用 f 初始化内部存储...
    }

    // 编译器隐式生成的移动构造函数
    // Callback(Callback&& other) noexcept;
};
```

现在我们写 `Callback cb2 = std::move(cb1);`——意图很明显，我们想调用移动构造函数。编译器面前有两条路：

1. 隐式生成的移动构造函数 `Callback(Callback&&)`
2. 模板构造函数实例化 `Callback(Callback&&)`（令 `Functor = Callback`）

直觉上我们会觉得移动构造函数应该优先——毕竟它是"专门为这种类型设计的"。但 C++ 的重载决议规则不是这么简单。在某些情况下，模板实例化出来的函数签名比隐式声明的特殊成员函数是"更精确"的匹配——因为模板参数 `Functor` 可以完美匹配传入参数的类型（包括 `Callback&&`），而移动构造函数的参数类型是固定的 `Callback&&`。

当两个重载的匹配程度相同时，C++ 规则规定**非模板函数优先于模板函数**。所以大多数情况下移动构造函数确实会赢。但边缘情况比较微妙——特别是当涉及到转发引用和完美匹配时，有些编译器版本可能会有不同行为。更关键的是，即使移动构造函数赢了，如果模板构造函数也在候选列表中，某些 SFINAE 场景可能导致意外的编译错误。

### 最小复现

```cpp
struct Wrapper {
    // 模板构造函数：接受任何类型
    template<typename T>
    Wrapper(T&& x) {
        std::cout << "template constructor\n";
    }

    // 移动构造函数（编译器隐式生成或显式声明）
    Wrapper(Wrapper&& other) noexcept {
        std::cout << "move constructor\n";
    }
};

Wrapper a;
Wrapper b = std::move(a);  // 你期望输出 "move constructor"
                            // 在某些情况下可能输出 "template constructor"
```

解决方案就是给模板构造函数加约束——让它**不要**匹配 `Wrapper` 自身的类型。

---

## Concept 基础语法

C++20 引入了 Concepts——一种命名约束的机制。你可以把 concept 想象成"带名字的编译期布尔条件"。这样说如果你感觉不好懂了——笔者认为，concept这个东西字如其名：就是概念的意思，相比之前我们要用enable_if来晦涩的表达是什么，我们可以更加容易的说出他是什么了——他是XXX，XXX就是一个concept。就这么简单。

### 声明 concept

```cpp
template<typename T>
concept Integral = std::is_integral_v<T>;
```

`Integral` 是一个 concept，它检查 `T` 是否是整数类型。`std::is_integral_v<T>` 是一个编译期布尔常量。我们这里表达的意思很简单——我们就只要一个整形！拿着这个概念，就能下一步的被requires使用了。

### 使用 requires 子句

`requires` 子句可以加在模板声明后面，用来约束模板参数必须满足某个条件：

```cpp
template<typename T>
    requires Integral<T>
void foo(T x) {
    // 只有 T 是整数类型时，这个函数才会被实例化
}

foo(42);    // OK：int 是整数
foo(3.14);  // 编译错误：double 不满足 Integral
```

### 标准库常用 concept

C++20 在 `<concepts>` 头文件中提供了一批预定义的 concept：

```cpp
#include <concepts>

// std::invocable<F, Args...>：F 是否可以用 Args... 调用
static_assert(std::invocable<int(*)(int), int>);

// std::same_as<A, B>：A 和 B 是否是同一类型
static_assert(std::same_as<int, int>);

// std::convertible_to<From, To>：From 是否能隐式转换到 To
static_assert(std::convertible_to<int, double>);
```

---

## not_the_same_t：逐行拆解

现在我们来看 OnceCallback 中的这个 concept：

```cpp
template<typename F, typename T>
concept not_the_same_t = !std::is_same_v<std::decay_t<F>, T>;
```

它做的事情用一句话说就是：**F 退化后的类型不是 T**。我们逐个拆解里面的三个关键组件。

### std::decay_t<F>：退化掉引用和 cv 限定符

`std::decay_t` 对类型做三件事：去掉引用（`int&` → `int`）、去掉顶层 const/volatile（`const int` → `int`）、数组和函数类型退化（`int[5]` → `int*`，`int(int)` → `int(*)(int)`）。

在 OnceCallback 的场景里，最关键的是去掉引用。当我们写 `OnceCallback cb2 = std::move(cb1)` 时，`Functor` 被推导为 `OnceCallback`（不是 `OnceCallback&&`，因为转发引用的推导规则会把右值推导为非引用类型）。但如果是 `OnceCallback cb2 = cb1;`（虽然拷贝被删除了，这里只是举例），`Functor` 就会被推导为 `OnceCallback&`。`std::decay_t` 保证了无论 `Functor` 推导出什么引用形式，退化后都是 `OnceCallback`，和 `T = OnceCallback` 做比较。

### std::is_same_v<...>：比较两个类型

`std::is_same_v<A, B>` 在 `A` 和 `B` 完全相同时返回 `true`。注意"完全相同"是很严格的——`int` 和 `const int` 不同，`int&` 和 `int` 也不同。这就是为什么我们需要 `std::decay_t` 先统一形式。

### 取反 `!`：F 不是 T 时约束通过

整个 concept 的值是 `!std::is_same_v<std::decay_t<F>, T>`——取反意味着当 `F` 退化后和 `T` 相同时约束失败（模板被排除），不同时约束通过（模板参与重载决议）。

### 加上约束后的效果

```cpp
template<typename Functor>
    requires not_the_same_t<Functor, OnceCallback>
explicit OnceCallback(Functor&& f) : status_(Status::kValid), func_(std::move(f)) {}
```

当传入的是 `OnceCallback` 本身时（比如移动构造的场景），`not_the_same_t<OnceCallback, OnceCallback>` 求值为 `!true = false`，约束不满足，模板被排除出候选列表，编译器只能选择移动构造函数。当传入的是 lambda、函数指针等其他类型时，约束满足，模板正常参与重载决议，被选为构造函数。

---

## 这个模式在标准库中的应用

这不仅仅是 OnceCallback 的特殊需求。`std::move_only_function` 自己的实现里也有几乎一样的约束——只不过标准库用的是标准 concept `std::constructible_from` 配合 `!std::is_same_v` 的形式。任何 move-only 的类型擦除包装器都需要这个防御——只要你的类同时有"接受任意类型的模板构造函数"和"编译器生成的移动构造函数"，就必须加约束来防止两者竞争。

```text
模式总结：
模板构造函数 + requires 排除自身类型 = 保护移动语义的正确匹配
```

如果你以后写类似的组件——比如自己的 `unique_function`、`any_invocable` 之类的 move-only 包装器——记住这个模式，它是一个通用的防御手段。

---

## 踩坑预警

### 如果忘记 std::decay_t

如果只写 `!std::is_same_v<F, T>` 而不加 `std::decay_t`，问题出在 `F` 的推导结果可能带引用也可能不带引用，取决于调用上下文。考虑以下场景：

```cpp
OnceCallback cb1([](int x) { return x; });

// 场景 A：std::move(cb1) 是右值
// Functor 推导为 OnceCallback（不带引用）
// is_same_v<OnceCallback, OnceCallback> == true → 约束失败 ✓ 正确

// 场景 B：const OnceCallback& ref = cb1;
// 如果有人写了 OnceCallback cb2(ref);
// Functor 推导为 const OnceCallback&
// is_same_v<const OnceCallback&, OnceCallback> == false → 约束通过 ✗ 错误！
```

场景 B 中，不加 `decay_t` 的话，`const OnceCallback&` 和 `OnceCallback` 不相同，约束通过，模板构造函数被选中——但语义上我们期望的是编译错误（拷贝已删除）或至少不是模板构造函数。加了 `decay_t` 后，`const OnceCallback&` 退化为 `OnceCallback`，和 `OnceCallback` 相同，约束正确失败。

### static_assert(false) 的陷阱

在 C++23 之前，`static_assert(false, "...")` 在模板中会导致所有实例化都触发断言失败——即使这个模板从未被调用。这是因为 C++ 标准在 C++23 之前要求 `static_assert(false)` 在模板定义时就立即求值。Chromium 用 `static_assert(!sizeof(*this), "...")` 来绕过这个限制（`!sizeof` 总是 `false`，但依赖 `*this` 的类型所以是依赖型表达式，不会在定义时求值）。C++23 放宽了这个规则，但如果你用 C++20 编译，仍然需要注意这个问题。

---

## 小结

这一篇我们搞清楚了 OnceCallback 构造函数上那个看似多余的 `requires not_the_same_t` 约束。它的存在是为了防止模板构造函数在 `OnceCallback cb2 = std::move(cb1)` 这种场景下劫持移动构造函数的调用。`not_the_same_t` 通过 `std::decay_t` 去掉 `F` 上的引用和 const 修饰后与 `T` 比较，取反后确保传入自身类型时模板被排除。这个模式在所有 move-only 的类型擦除包装器中都会用到——`std::move_only_function` 也有类似的约束。

下一篇我们去看 `std::move_only_function`——它是 OnceCallback 的核心存储类型，也是我们用标准库设施替代 Chromium 手写 BindState 的关键。

## 参考资源

- [cppreference: Constraints and concepts](https://en.cppreference.com/w/cpp/language/constraints)
- [cppreference: std::decay](https://en.cppreference.com/w/cpp/types/decay)
- [Stack Overflow: Generic constructor template called instead of copy/move constructor](https://stackoverflow.com/questions/70267685/generic-constructor-template-called-instead-of-copy-move-constructor)
