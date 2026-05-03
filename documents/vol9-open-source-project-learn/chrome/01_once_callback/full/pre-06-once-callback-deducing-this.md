---
title: "OnceCallback 前置知识（六）：Deducing this (C++23)"
description: "深入理解 C++23 显式对象参数（deducing this）如何让 OnceCallback::run() 在编译期优雅地拦截左值调用，替代 Chromium 的双重重载 hack"
chapter: 0
order: 6
tags:
  - host
  - cpp-modern
  - intermediate
  - 模板
difficulty: intermediate
platform: host
cpp_standard: [23]
reading_time_minutes: 10
prerequisites:
  - "OnceCallback 前置知识速查：C++11/14/17 核心特性回顾"
related:
  - "OnceCallback 实战（二）：核心骨架搭建"
  - "OnceCallback 前置知识（四）：Concepts 与 requires 约束"
---

# OnceCallback 前置知识（六）：Deducing this (C++23)

## 引言

OnceCallback 的 `run()` 方法是整个组件的灵魂，也是 C++23 特性最密集的一个方法。它的声明长这样：

```cpp
template<typename Self>
auto run(this Self&& self, FuncArgs&&... args) -> ReturnType;
```

如果你没见过 `this Self&& self` 这种写法——别慌，这一篇就是专门讲它的。这是 C++23 引入的"显式对象参数"特性，官方名称叫 **deducing this**。它让 OnceCallback 用一个函数模板就实现了"左值调用编译报错、右值调用正常执行"的效果，比 Chromium 的方案干净得多。

> **学习目标**
>
> - 理解 deducing this 的语法和推导规则
> - 掌握 `run()` 如何利用它实现编译期左值/右值拦截
> - 理解惰性实例化（lazy instantiation）在 `static_assert` 中的作用
> - 对比 deducing this 和传统 ref-qualifier 的适用场景

---

## 问题：如何让 cb.run() 编译失败

OnceCallback 的核心语义是"只能调用一次，而且必须通过右值调用"。用代码表达就是：

```cpp
OnceCallback<int(int)> cb([](int x) { return x * 2; });

cb.run(5);                  // 应该编译失败：cb 是左值
std::move(cb).run(5);       // 应该编译通过：std::move(cb) 是右值
```

我们需要一种机制，让 `run()` 能够在编译期区分"通过左值调用"和"通过右值调用"，并且对左值调用给出清晰的错误信息。

### Chromium 的旧方案

Chromium 没有享受 C++23 的福利，它用了一个比较 hack 的方案——两个重载：

```cpp
// 右值版本：真正的执行
R Run() && {
    // 执行回调...
}

// 左值版本：编译报错
R Run() const& {
    static_assert(!sizeof(*this),
        "OnceCallback::Run() may only be invoked on a non-const rvalue, "
        "i.e. std::move(callback).Run().");
}
```

为什么用 `!sizeof(*this)` 而不是直接写 `false`？因为在 C++23 之前，`static_assert(false, "...")` 在模板中会导致所有代码路径都触发断言——即使这个函数从未被调用。C++23 放宽了这个限制。`!sizeof(*this)` 利用了 `sizeof` 必须在完整类型上才能求值的特性——它是一个依赖型表达式，只有在模板实例化时才求值，从而实现了"只在实际调用时才触发"的效果。

能工作，但确实不优雅——需要两个重载函数来处理同一件事，而且 `!sizeof` hack 的可读性不好。

---

## deducing this 的语法与推导规则

C++23 的 deducing this 让我们可以把 `this` 显式地写成成员函数的第一个参数，并用模板参数来推导它的类型和值类别。

### 基本语法

```cpp
struct MyStruct {
    void f(this auto&& self) {
        // self 就是 this——但它的类型是推导出来的
    }
};
```

`this auto&& self` 是显式对象参数的声明。关键字 `this` 出现在类型前面，告诉编译器"这不是一个普通参数，而是显式的对象参数"。`auto&&` 是推导占位符——编译器会根据调用时对象的值类别来推导 `self` 的具体类型。

### 推导规则

`self` 的类型推导规则和转发引用（forwarding reference）完全一样——因为 `self` 的推导上下文等效于模板参数：

- **左值调用** `obj.f()`：`self` 的类型推导为 `MyStruct&`（左值引用）
- **右值调用** `std::move(obj).f()` 或 `MyStruct{}.f()`：`self` 的类型推导为 `MyStruct`（非引用，纯类型）
- **const 左值调用** `std::as_const(obj).f()`：`self` 的类型推导为 `const MyStruct&`

### 验证推导结果

```cpp
#include <iostream>
#include <type_traits>

struct Check {
    void test(this auto&& self) {
        using Self = decltype(self);
        if constexpr (std::is_lvalue_reference_v<Self>) {
            std::cout << "lvalue reference\n";
        } else {
            std::cout << "rvalue (not a reference)\n";
        }
    }
};

int main() {
    Check c;
    c.test();                  // 输出：lvalue reference
    std::move(c).test();       // 输出：rvalue (not a reference)
    std::as_const(c).test();   // 输出：lvalue reference (const)
}
```

---

## 在 OnceCallback::run() 中的应用

现在我们来看 `run()` 的完整实现，理解它是如何利用 deducing this 来拦截左值调用的。

```cpp
template<typename Self>
auto run(this Self&& self, FuncArgs&&... args) -> ReturnType {
    static_assert(!std::is_lvalue_reference_v<Self>,
        "OnceCallback::run() must be called on an rvalue. "
        "Use std::move(cb).run(...) instead.");
    return std::forward<Self>(self).impl_run(std::forward<FuncArgs>(args)...);
}
```

这段代码做了三件事，我们逐一拆解。

### 拦截左值调用

`std::is_lvalue_reference_v<Self>` 检查 `Self` 是否是左值引用类型。当调用方写 `cb.run(args)` 时，`cb` 是左值，`Self` 被推导为 `OnceCallback&`——这是一个左值引用类型，`is_lvalue_reference_v` 返回 `true`，取反后为 `false`，`static_assert` 失败，编译器报出我们写的那句错误信息："OnceCallback::run() must be called on an rvalue. Use std::move(cb).run(...) instead."

当调用方写 `std::move(cb).run(args)` 时，`std::move(cb)` 是右值（严格说是 xvalue），`Self` 被推导为 `OnceCallback`——不是引用类型，`is_lvalue_reference_v` 返回 `false`，取反后为 `true`，`static_assert` 通过，代码继续执行。

### 转发到 impl_run

`std::forward<Self>(self)` 根据 `Self` 的类型决定是返回左值引用还是右值引用。由于 `static_assert` 已经排除了左值的情况，到达这里的 `Self` 一定是非引用类型（右值），所以 `std::forward<Self>(self)` 返回的是右值引用——确保 `impl_run` 在右值上被调用。

### 惰性实例化（Lazy Instantiation）

这里有一个值得玩味的细节——`static_assert` 的条件依赖模板参数 `Self`，所以它只有在模板实例化时才求值。这意味着：

- 如果 `run()` 从未被调用，`static_assert` 不会触发——不管 `OnceCallback` 对象本身是左值还是右值
- 只有在某个具体的调用点上，编译器需要实例化这个模板时，`Self` 的具体类型才会被确定，`static_assert` 才会求值

这叫"惰性实例化"（lazy instantiation），是 C++ 模板的一个基本特性。函数模板只有在使用时才会被实例化——不使用就不实例化，也不做任何检查。这就是为什么 Chromium 不得不用 `!sizeof(*this)` 而不是直接写 `false`——在 C++23 之前，`static_assert(false)` 不依赖模板参数，会在模板定义时就触发，而不是等实例化时才触发。

---

## 与传统 ref-qualifier 的对比

OnceCallback 里有两个方法表达了"只能通过右值调用"的语义——`run()` 用 deducing this，`then()` 用传统的 ref-qualifier `&&`。为什么不统一用一种方式？

### then() 用 ref-qualifier

```cpp
template<typename Next>
auto then(Next&& next) && -> OnceCallback<...>;
```

`then()` 的需求很简单——它只接受右值，不接受左值，不需要区分后给出不同的错误信息。如果调用方写了 `cb.then(next)`（左值调用），编译器直接报"没有匹配的重载函数"，虽然错误信息不如 deducing this 那么有指导意义，但足够用了。ref-qualifier 写起来也更简洁——一个 `&&` 就完事了。

### run() 用 deducing this

`run()` 的需求更精细——它不仅需要拒绝左值调用，还需要给出一个**有指导意义的错误信息**，告诉调用方"你应该用 `std::move(cb).run(...)` 而不是 `cb.run(...)`"。deducing this 让这个需求变得自然——`static_assert` 可以输出我们自定义的错误信息，而不是编译器默认的"no matching function"。

### 选择策略

总结一下：如果你只需要"只接受右值"的约束，用 `&&` 限定更简洁。如果你还需要对左值调用给出自定义的错误信息，用 deducing this 配合 `static_assert` 更合适。

---

## 踩坑预警

### 显式对象参数不能与 cv-qualifier 或 ref-qualifier 共存

有显式对象参数的成员函数不能同时声明为 `const`、`volatile` 或带 ref-qualifier（`&`/`&&`）。这是因为显式对象参数已经接管了对象类型和值类别的推导——`const` 和 `&&` 限定变得多余甚至矛盾。

```cpp
struct Bad {
    void f(this auto&& self) const;   // 编译错误：不能同时有显式对象参数和 const
    void g(this auto&& self) &&;      // 编译错误：不能同时有显式对象参数和 &&
};
```

### 显式对象参数不能是静态函数

显式对象参数函数不是静态函数——它仍然需要一个对象实例来调用。`this` 参数是由编译器从调用表达式推导出来的，不是由调用方手动传入的。

### 编译器支持

Deducing this 是 C++23 特性。GCC 14+、Clang 18+、MSVC 19.34+ 支持此特性。如果你的编译器不支持，只能回退到 Chromium 的双重重载方案。

---

## 小结

这一篇我们搞清楚了 deducing this 的来龙去脉。它让 `run()` 用一个函数模板就实现了编译期的左值/右值拦截——通过 `Self` 的推导类型判断调用方传的是左值还是右值，配合 `static_assert` 给出有指导意义的错误信息。相比 Chromium 的两个重载 + `!sizeof` hack，deducing this 方案更简洁、更符合 C++ 的设计哲学。而 `then()` 不需要自定义错误信息，用传统的 `&&` 限定更简洁。

到这里，所有前置知识都讲完了。下一篇我们正式进入 OnceCallback 的实战环节——从动机分析开始，设计我们的目标 API。

## 参考资源

- [P0847R7 - Deducing this 提案](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2021/p0847r7.html)
- [C++23's Deducing this (Microsoft C++ Blog)](https://devblogs.microsoft.com/cppblog/cpp23-deducing-this/)
- [cppreference: Explicit object parameter](https://en.cppreference.com/w/cpp/language/member_functions#Explicit_object_parameter)
