---
chapter: 0
cpp_standard:
- 11
- 14
- 17
- 20
description: 深入理解函数类型 int(int,int) 是什么，以及 OnceCallback<R(Args...)> 背后的模板偏特化技巧——编译器如何通过模式匹配拆解函数签名
difficulty: intermediate
order: 1
platform: host
prerequisites:
- OnceCallback 前置知识速查：C++11/14/17 核心特性回顾
reading_time_minutes: 8
related:
- OnceCallback 前置知识（五）：std::move_only_function
- OnceCallback 实战（二）：核心骨架搭建
tags:
- host
- cpp-modern
- intermediate
- 模板
- 泛型
title: OnceCallback 前置知识（一）：函数类型与模板偏特化
---
# OnceCallback 前置知识（一）：函数类型与模板偏特化

笔者第一次在 Chromium 源码里撞见 `OnceCallback<int(int, int)>` 这个写法，盯着看了半天。`int(int, int)` 看起来就像函数声明残骸，可它偏偏待在模板参数的位置上。这玩意儿到底是啥？编译器又怎么从 `int(int, int)` 里读出"返回 int、收两个 int"这些信息的？

笔者当时没想通,后来才知道这套写法是 `std::function`、`std::move_only_function` 乃至咱们整个 `OnceCallback` 的共同底子。这一篇笔者就把这块拆透——先把"函数类型"这个被忽略的概念摆正,再看主模板加偏特化那套模式匹配是怎么把签名拆开的。咱们会顺手撸一个最小的 `FuncTraits` 把它跑通,最后聊聊为什么标准库集体选了签名式而没用更直白的写法。

## 函数类型：C++ 里一个容易被错过的类型

咱们从一个最朴素的问题起手：`int(int, int)` 在 C++ 里算一种类型吗？

算。它有个名字叫函数类型（function type），说的是"收两个 int、返回 int 的函数"。这里笔者要特别点一下,函数类型比函数指针更底层,它跟 `int(*)(int, int)` 这种指针、`int(&)(int, int)` 这种引用都不是一回事。后面咱们会看到,正是这个"底层"让它能被偏特化逮住。

`static_assert` 一验便知:

```cpp
#include <type_traits>

static_assert(std::is_function_v<int(int, int)>);           // 通过：是函数类型
static_assert(!std::is_pointer_v<int(int, int)>);           // 通过：不是指针
static_assert(std::is_pointer_v<int(*)(int, int)>);         // 通过：这是函数指针
```

函数类型在实际代码里露脸的次数比咱们以为的多。随手写一个函数声明：

```cpp
int add(int a, int b);
```

`add` 的类型就是 `int(int, int)`。您可以把它当成一种签名,它完整说清了这个函数收什么、吐什么,但不说函数本身存在哪。

函数类型跟函数指针之间还有个隐式转换：函数名在多数表达式里会自动退化成指向自己的指针。这点跟数组名退化成指针是一路的。`int arr[5]` 里的 `arr` 在多数上下文中变成 `int*`,`int add(int, int)` 里的 `add` 也会变成 `int(*)(int, int)`。

可一旦它作为模板参数传进去,函数类型就不退化了,编译器原模原样收下这个类型。这正是后面能用偏特化拆它的前提。

## 主模板加偏特化：拆解函数类型的套路

接下来咱们看 `OnceCallback` 的模板声明是怎么写的。它走的是两步:先甩出一个只收一个类型参数的主模板,再为"这个类型参数恰好是函数类型"这种情形单开一个偏特化版本。

### 第一步：主模板声明

```cpp
template<typename FuncSignature>
class OnceCallback;  // 主模板：只有声明，没有定义
```

主模板故意不给实现。这不是笔者忘写,是刻意的。要是谁手滑写出 `OnceCallback<int>` 这种——传了个普通 int 进来而不是函数签名——实例化的时候直接报错找不到定义。算一道编译期安全网。

### 第二步：偏特化版本

```cpp
template<typename ReturnType, typename... FuncArgs>
class OnceCallback<ReturnType(FuncArgs...)> {
    // 所有真正的代码都在这里
};
```

这版的模板参数列表是 `<typename ReturnType, typename... FuncArgs>`,而类名后头跟的 `OnceCallback<ReturnType(FuncArgs...)>` 才是关键,它是偏特化的模式匹配条件,意思就一句:当 `FuncSignature` 能凑成 `ReturnType(FuncArgs...)` 这个样子,就用这版。

### 编译器是怎么配对的

写 `OnceCallback<int(int, int)>` 的时候,编译器干了几件事。

它先看到您要实例化 `OnceCallback`,模板参数是 `int(int, int)`。然后去对主模板,把 `FuncSignature` 绑成 `int(int, int)` 这个整体。接着回头查有没有偏特化能用。偏特化要求 `FuncSignature` 匹配 `ReturnType(FuncArgs...)` 的模式,`int(int, int)` 恰好拆得开,`ReturnType = int`、`FuncArgs = {int, int}`,配上了,偏特化就被选中。

整个过程您可以当成类型层面的模式匹配。打个比方,正则 `(\w+)\((\w+(?:,\s*\w+)*)\)` 能从字符串 `int(int, int)` 里抠出返回值和参数列表,模板偏特化干的是同样的事,只不过它操作的是类型,不是字符。

### 跟 std::function 用的是一模一样的招

去翻 `std::function` 的标准库实现,您会发现它用的是同一套：

```cpp
// std::function 的简化实现
template<typename> class function; // 主模板

template<typename R, typename... Args>
class function<R(Args...)> {        // 偏特化
    // ...
};
```

`std::move_only_function`(C++23)也一样。主模板加函数类型偏特化这个组合,在标准库里至少露了三次脸,是个被反复验证过的设计。咱们自己写 `OnceCallback` 的时候,没理由另起炉灶。

## 动手实践：撸一个 FuncTraits

光看不练忘得快。咱们自己动手写一个最小的函数签名拆解工具,把刚才那套理解夯结实。目标是这样:丢给它一个函数类型 `R(Args...)`,它能把返回类型 `R` 和参数包 `Args...` 都吐出来。

```cpp
#include <type_traits>

// 主模板：对非函数类型不提供定义
template<typename T>
struct FuncTraits;

// 偏特化：拆解函数类型 R(Args...)
template<typename R, typename... Args>
struct FuncTraits<R(Args...)> {
    using ReturnType = R;
    using ArgsTuple = std::tuple<Args...>;

    static constexpr std::size_t kArity = sizeof...(Args);
};

// 验证
static_assert(std::is_same_v<FuncTraits<int(double, char)>::ReturnType, int>);
static_assert(std::is_same_v<FuncTraits<void()>::ReturnType, void>);
static_assert(FuncTraits<int(int, int, int)>::kArity == 3);
```

`FuncTraits` 跟 `OnceCallback` 走的是同一个偏特化套路。区别只有一处:`FuncTraits` 把拆出来的类型存成 `using` 别名和 `static constexpr` 常量留着外面用,`OnceCallback` 则直接在偏特化类内部拿这些类型去定义数据成员和方法。

咱们编译跑一下这个示例。`static_assert` 全过(没编译错误)就说明偏特化把函数类型拆对了。您也可以再丢几个更复杂的类型进去试:

```cpp
// 更复杂的验证
static_assert(std::is_same_v<
    FuncTraits<std::string(const std::string&, int)>::ReturnType,
    std::string>);
static_assert(std::is_same_v<
    FuncTraits<void(int&&)>::ArgsTuple,
    std::tuple<int&&>>);
```

---

## 为什么不写成 OnceCallback<R, Args...>？

您可能会琢磨,既然要的就是返回类型加参数列表,干嘛不直接写成 `OnceCallback<R, Args...>` 这种更直白的样式?像这样:

```cpp
template<typename R, typename... Args>
class OnceCallback {
    // ...
};

// 使用：OnceCallback<int, int, int> cb([](int a, int b) { return a + b; });
```

这种写法技术上完全能跑。但用户体验差一截。咱们对比一下两种调用:

```cpp
// 签名式：一个模板参数，看起来像函数签名
OnceCallback<int(int, int)> cb1([](int a, int b) { return a + b; });

// 参数罗列式：返回类型和参数分开写
OnceCallback<int, int, int> cb2([](int a, int b) { return a + b; });
```

第一种读起来自然。`int(int, int)` 就是一个完整函数签名,一眼到底。第二种得在脑子里分一下:头一个 `int` 是返回类型,后头俩 `int, int` 才是参数,凭空加了认知负担。标准库也选了签名式——`std::function<int(int, int)>`,不是 `std::function<int, int, int>`。

签名式还有个微妙好处,它跟 C++ 类型系统更对得上号。`int(int, int)` 是个真实存在的类型;而"一个返回类型加一坨参数类型"不算一个类型,只是几个类型摆一块儿。拿函数类型当模板参数,操作的是类型系统本身,不是在语法糖上打补丁。

不过签名式也有个让人不痛快的角落:编译器没法从可调用对象自己推出完整签名。这就是 `bind_once` 第一个模板参数 `Signature` 非得手写的原因。这个取舍笔者留到 `bind_once` 实现篇再展开聊。

## 参考资源

- [cppreference: 函数类型](https://en.cppreference.com/w/cpp/language/function)
- [cppreference: 模板偏特化](https://en.cppreference.com/w/cpp/language/template_specialization)
- [cppreference: std::is_function](https://en.cppreference.com/w/cpp/types/is_function)
