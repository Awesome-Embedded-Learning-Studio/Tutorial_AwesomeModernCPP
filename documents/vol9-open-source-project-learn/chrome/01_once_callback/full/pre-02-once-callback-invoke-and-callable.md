---
title: "OnceCallback 前置知识（二）：std::invoke 与统一调用协议"
description: "深入理解 std::invoke 如何统一函数指针、成员函数指针、lambda、仿函数的调用方式，以及 std::invoke_result_t 在 OnceCallback 中的类型推导作用"
chapter: 0
order: 2
tags:
  - host
  - cpp-modern
  - intermediate
  - 函数对象
  - std_invoke
difficulty: intermediate
platform: host
cpp_standard: [17]
reading_time_minutes: 20
prerequisites:
  - "OnceCallback 前置知识速查：C++11/14/17 核心特性回顾"
  - "OnceCallback 前置知识（一）：函数类型与模板偏特化"
related:
  - "OnceCallback 实战（三）：bind_once 实现"
  - "OnceCallback 实战（五）：then 链式组合"
---

# OnceCallback 前置知识（二）：std::invoke 与统一调用协议

## 引言

假设你在写一个回调系统——就像我们正在做的 OnceCallback。你的系统需要接受各种各样的"可调用对象"：普通函数指针、lambda、仿函数（重载了 `operator()` 的类对象），甚至成员函数指针。问题来了，这些可调用对象的调用语法各不相同。普通函数直接 `f(args...)`，成员函数指针必须写成 `(obj.*pmf)(args...)`。如果你的代码里有十种不同的可调用对象，是不是要写十个 `if-else` 分支来分别处理？

`std::invoke`（C++17）就是为了消灭这种分裂而生的。它提供了一种统一的调用语法，让所有可调用对象都能用同一种方式调用。OnceCallback 的 `bind_once` 和 `then()` 内部全部依赖它来实现"不管传进来什么可调用对象，都能正确调用"这个需求。

> **学习目标**
>
> - 理解为什么需要统一调用协议——各种可调用对象的调用语法差异
> - 掌握 `std::invoke` 的完整分派规则
> - 学会使用 `std::invoke_result_t` 在编译期推导调用结果的类型

---

## 问题：可调用对象的调用语法分裂

C++ 中至少有四种常见的可调用对象，它们的调用语法各不相同。我们逐一看看。

### 普通函数指针

```cpp
int add(int a, int b) { return a + b; }
int (*fp)(int, int) = &add;

int result = fp(3, 4);       // 直接调用
int result2 = (*fp)(3, 4);   // 解引用后调用（等价）
```

### Lambda / 仿函数

```cpp
auto lam = [](int a, int b) { return a + b; };
int result = lam(3, 4);  // 通过 operator() 调用

struct Adder {
    int operator()(int a, int b) { return a + b; }
};
Adder fn;
int result2 = fn(3, 4);  // 同样通过 operator() 调用
```

### 成员函数指针

这里语法开始变得古怪了。成员函数指针不能像普通函数那样直接调用——你必须有一个对象实例，然后用 `.*` 或 `->*` 运算符来调用。

```cpp
struct Calculator {
    int multiply(int a, int b) { return a * b; }
};

Calculator calc;
int (Calculator::*pmf)(int, int) = &Calculator::multiply;

// 必须用 .* 运算符
int result = (calc.*pmf)(3, 4);  // result == 12
```

### 指向数据成员的指针

是的，C++ 允许你获取数据成员的"指针"——它其实是一个偏移量。访问方式也是通过 `.*` 运算符。

```cpp
struct Point {
    double x, y;
};

Point p{1.0, 2.0};
double Point::*pmx = &Point::x;

double val = p.*pmx;  // val == 1.0
```

问题很清楚了：如果你在写一个模板函数，需要调用一个"不知道具体是什么类型的可调用对象"，你没法写出一个统一的调用语法——因为你不知道它是普通函数还是成员函数指针。`std::invoke` 就是来解决这个问题的。

---

## std::invoke 的分派规则

`std::invoke(f, args...)` 的工作是：根据 `f` 和 `args` 的具体类型，选择正确的调用语法。标准规定了以下几种情况（C++ 标准术语叫 INVOKE 表达式）：

### 情况一：成员函数指针 + 对象

当 `f` 是指向成员函数的指针，`args` 的第一个元素是对象（或对象的引用、或指向对象的指针）时，`std::invoke` 展开为通过对象调用成员函数。

```cpp
struct Calculator {
    int multiply(int a, int b) { return a * b; }
};

Calculator calc;

// 通过引用
std::invoke(&Calculator::multiply, calc, 3, 4);        // (calc.*multiply)(3, 4)
// 通过指针
std::invoke(&Calculator::multiply, &calc, 3, 4);       // ((*ptr).*multiply)(3, 4)
```

注意第二种情况——当第一个参数是指针（`&calc`）时，`std::invoke` 会自动解引用指针。这个行为在 `bind_once` 绑定成员函数时非常重要。

### 情况二：指向数据成员的指针 + 对象

当 `f` 是指向数据成员的指针时，`std::invoke` 展开为通过对象访问数据成员。

```cpp
struct Point { double x, y; };
Point p{1.0, 2.0};

double val = std::invoke(&Point::x, p);    // p.*&Point::x == p.x
```

### 情况三：其他可调用对象

当 `f` 是函数指针、lambda、仿函数等"可以直接调用的东西"时，`std::invoke` 就是简单的 `f(args...)`。

```cpp
std::invoke([](int a, int b) { return a + b; }, 3, 4);  // lambda(3, 4)
```

### 统一接口

关键在于，不管 `f` 是上面哪种情况，调用语法都是 `std::invoke(f, args...)`。在你的模板代码里，你不需要知道 `f` 的具体类型——`std::invoke` 在内部帮你分派到正确的调用语法。

---

## std::invoke_result_t：编译期推导返回类型

光有统一调用还不够——有时候你还需要在编译期知道 `std::invoke(f, args...)` 的返回类型是什么。比如在 `then()` 的实现中，我们需要推导"把前一个回调的返回值传给下一个回调，返回什么类型"。

`std::invoke_result_t<F, Args...>` 就是干这个的。给定可调用对象类型 `F` 和参数类型 `Args...`，它在编译期计算出 `std::invoke(f, args...)` 的返回类型。

```cpp
#include <type_traits>
#include <functional>

auto add(int a, int b) -> int { return a + b; }

// 编译期推导 add(1, 2) 的返回类型
using R = std::invoke_result_t<decltype(add), int, int>;
static_assert(std::is_same_v<R, int>);

// 对 lambda 也能推导
auto lam = [](double x) { return std::to_string(x); };
using R2 = std::invoke_result_t<decltype(lam), double>;
static_assert(std::is_same_v<R2, std::string>);
```

### 在 OnceCallback 中的使用

`then()` 的实现用 `std::invoke_result_t` 来推导链式调用中新回调的返回类型。具体来说，当 `then()` 接受一个后续回调 `next` 时，它需要知道 `next(上一个回调的返回值)` 会返回什么类型：

```cpp
// 在 then() 的非 void 分支中
using NextRet = std::invoke_result_t<NextType, ReturnType>;
// NextRet 就是"把 ReturnType 类型的值传给 next，返回什么类型"
```

void 分支中，后续回调不接受参数：

```cpp
// 在 then() 的 void 分支中
using NextRet = std::invoke_result_t<NextType>;
// next 不接受参数，直接调用
```

---

## 在 OnceCallback 源码中的具体使用

让我们对照实际源码，看看 `std::invoke` 在 OnceCallback 中的两个使用场景。

### bind_once 中的 std::invoke

```cpp
// bind_once 的 lambda 内部
return std::invoke(
    std::move(f),
    std::move(bound)...,
    std::forward<decltype(call_args)>(call_args)...
);
```

这里 `f` 可能是任何可调用对象——普通 lambda、成员函数指针，甚至指向数据成员的指针。如果不用 `std::invoke` 而是直接写 `f(bound..., call_args...)`，当 `f` 是成员函数指针时就会编译失败——因为成员函数指针不能直接用 `()` 调用。

### then() 中的 std::invoke

```cpp
// then() 的非 void 分支
auto mid = std::move(self).run(std::forward<FuncArgs>(args)...);
return std::invoke(std::move(cont), std::move(mid));
```

`cont`（后续回调）在 `then()` 的设计里是一个普通的可调用对象（通常是 lambda），不是 `OnceCallback`。所以理论上直接 `cont(mid)` 也能工作——大部分情况下确实如此。但使用 `std::invoke` 是一种防御性编程：如果有人传进来一个成员函数指针作为后续回调，直接调用语法会失败，`std::invoke` 不会。统一使用 `std::invoke` 保证了无论传什么可调用对象都能正确工作，不需要额外的代码来处理特殊类型。

---

## 踩坑预警：成员函数绑定的生命周期陷阱

`std::invoke` 能统一处理成员函数指针，但它不会帮你管理对象的生命周期。当你在 `bind_once` 中绑定一个成员函数时：

```cpp
struct Calculator {
    int multiply(int a, int b) { return a * b; }
};

Calculator calc;
auto bound = bind_once<int(int)>(&Calculator::multiply, &calc, 5);
```

`&calc` 是一个裸指针，`bind_once` 会把它存到 lambda 的捕获列表里。如果 `calc` 在回调被调用之前就被销毁了，lambda 内部持有的就是一个悬空指针，`std::invoke` 通过悬空指针访问已释放的内存——未定义行为，大概率段错误。

Chromium 用 `base::Unretained` 显式标记"我知道这个裸指针的生命周期是安全的"，用 `base::Owned` 接管对象的所有权，用 `base::WeakPtr` 在对象析构时自动取消回调。我们的简化版暂时不提供这些保护机制——安全责任在调用方手上。这是一个重要的设计取舍，我们在实战篇里会再提到。

---

## 小结

这一篇我们弄清楚了 `std::invoke` 的来龙去脉。核心动机是各种可调用对象的调用语法各不相同——普通函数直接 `f(args...)`，成员函数指针要 `(obj.*pmf)(args...)`，数据成员指针要 `obj.*pmd`。`std::invoke` 把这些全部统一成 `std::invoke(f, args...)` 一种语法，配合 `std::invoke_result_t` 可以在编译期推导调用的返回类型。在 OnceCallback 中，`bind_once` 和 `then()` 都依赖它来实现"不关心可调用对象的具体类型，只要能调用就行"的泛型设计。

下一篇我们去看 Lambda 的高级特性——特别是 C++20 引入的 lambda init capture 包展开，它是 `bind_once` 得以简洁实现的关键。

## 参考资源

- [cppreference: std::invoke](https://en.cppreference.com/w/cpp/utility/functional/invoke)
- [cppreference: std::invoke_result](https://en.cppreference.com/w/cpp/types/result_of)
- [cppreference: Callable](https://en.cppreference.com/w/cpp/named_req/Callable)
