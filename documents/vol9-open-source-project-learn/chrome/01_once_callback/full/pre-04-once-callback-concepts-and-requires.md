---
chapter: 0
cpp_standard:
- 20
description: 从模板构造函数劫持移动构造函数的真实问题出发，理解 Concepts 和 requires 约束如何保护 OnceCallback 的构造函数正确匹配
difficulty: intermediate
order: 4
platform: host
prerequisites:
- OnceCallback 前置知识速查：C++11/14/17 核心特性回顾
- OnceCallback 前置知识（一）：函数类型与模板偏特化
reading_time_minutes: 9
related:
- OnceCallback 实战（二）：核心骨架搭建
- OnceCallback 前置知识（五）：std::move_only_function
tags:
- host
- cpp-modern
- intermediate
- concepts
- 模板
title: OnceCallback 前置知识（四）：Concepts 与 requires 约束
---
# OnceCallback 前置知识（四）：Concepts 与 requires 约束

OnceCallback 的构造函数上有这么一行看起来很多余的约束:

```cpp
template<typename Functor>
    requires not_the_same_t<Functor, OnceCallback>
explicit OnceCallback(Functor&& function);
```

笔者第一次读到这行的时候,脑子里第一反应是:这不是多此一举吗?直接 `template<typename Functor>` 不就完事了,加个 `requires not_the_same_t` 到底在防谁?

后来真踩了一脚,才知道它在防一个 C++ 重载决议里相当阴损的坑——**模板构造函数会劫持移动构造函数**。Concepts 和 `requires` 约束,就是 C++20 给咱们留下的防御武器。这一篇就把这个坑从头刨一遍,顺带把 concepts 这套语法也过一遍。

## 问题:模板构造函数的"越位"

咱们先把这个坑还原一下。

假设咱们写了个简单的包装类,接受任意可调用对象:

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

咱们随手写一行 `Callback cb2 = std::move(cb1);`,意图很直白——走移动构造。但编译器面前其实摆着两条路:一条是隐式生成的移动构造函数 `Callback(Callback&&)`,另一条是模板构造函数实例化出来的 `Callback(Callback&&)`(令 `Functor = Callback`)。

直觉上您肯定觉得移动构造稳赢——毕竟它是"专门为这个类型设计的"。可 C++ 的重载决议不走直觉。模板构造函数那个转发引用 `Functor&&` 太贪婪了,它能完美匹配任何东西,包括 `Callback&&` 本身;而移动构造函数的参数类型是写死的 `Callback&&`。在"匹配谁更精确"这件事上,模板实例化出的版本有时候反而看起来更贴。

好在 C++ 留了条规则兜底:当模板和非模板版本匹配程度相同时,**非模板优先**。所以多数情况下移动构造还是能赢。可这事儿没那么干净——一旦牵扯到转发引用和完美匹配,不同编译器、不同版本的行为就开始飘了;更恶心的是,哪怕移动构造赢了,模板构造函数照样躺在候选列表里,某些 SFINAE 场景会冒出莫名其妙的编译错误。

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

解法就是给模板构造函数套个约束,让它别去碰 `Wrapper` 自身的类型——这就是 `requires` 子句登场的地方。

---

## Concepts 是个什么东西

C++20 引入了 Concepts。官方定义很绕——"一种命名约束的机制"。笔者觉得这么说反而把人绕进去了。concept 这个词字如其名,就是"概念"的意思。

咱们退一步想:在 concepts 出来之前,您要表达“我只接受整数类型”,得用 `enable_if` 那一套——`typename std::enable_if<std::is_integral_v<T>::value, int>::type = 0`,一长串晦涩的玩意儿,读的人得先在脑子里转一圈才能明白您想说什么。而 concept 干的事儿,就是让您**直接说出这是个什么概念**:它叫 `Integral`,它就是"整数"这个概念。就这么简单。`T` 满足 `Integral`,`T` 就是整数;不满足,就别想进来。

声明一个 concept 长这样:

```cpp
template<typename T>
concept Integral = std::is_integral_v<T>;
```

`Integral` 检查 `T` 是不是整数类型,`std::is_integral_v<T>` 是个编译期的布尔常量。咱们想表达的意思就这么点——我就要个整数!有了这个概念,下一步就能把它喂给 `requires` 用。

`requires` 子句往模板声明后面一挂,就给模板参数上了道闸:

```cpp
template<typename T>
    requires Integral<T>
void foo(T x) {
    // 只有 T 是整数类型时，这个函数才会被实例化
}

foo(42);    // OK：int 是整数
foo(3.14);  // 编译错误：double 不满足 Integral
```

`<concepts>` 头文件里还备了一堆标准库现成的 concept,常用的几个长这样:

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

## 把 not_the_same_t 拆开看

现在咱们回头看 OnceCallback 里那个 concept:

```cpp
template<typename F, typename T>
concept not_the_same_t = !std::is_same_v<std::decay_t<F>, T>;
```

一句话总结:`F` 退化(decay)之后,只要不是 `T`,约束就过。里头有三个零件,咱们挨个拆。

先看 `std::decay_t<F>`。它对类型干三件事:去引用(`int&` → `int`)、去顶层 const/volatile(`const int` → `int`)、把数组和函数类型退化掉(`int[5]` → `int*`、`int(int)` → `int(*)(int)`)。在 OnceCallback 这个场景里,最关键的是去引用。咱们写 `OnceCallback cb2 = std::move(cb1)` 的时候,`Functor` 被推导成 `OnceCallback`(不是 `OnceCallback&&`——转发引用的推导规则会把右值推导成非引用);但要是写成 `OnceCallback cb2 = cb1`(拷贝虽被删除,这里只作举例),`Functor` 就会被推导成 `OnceCallback&`。`std::decay_t` 的活儿,就是不管 `Functor` 推导出哪种引用形态,统统退化成裸的 `OnceCallback`,再拿去和 `T = OnceCallback` 比。

再看 `std::is_same_v<A, B>`。它在 `A` 和 `B` 完全相同时返回 `true`。注意"完全相同"这四个字很严——`int` 和 `const int` 不算同,`int&` 和 `int` 也不算同。这就是为什么前面非得先上 `std::decay_t` 把两边的形式统一了,不然一个带引用一个不带,比出来全是噪音。

最后那个取反 `!` 是点睛之笔。整个 concept 的值是 `!std::is_same_v<std::decay_t<F>, T>`——`F` 退化后要是等于 `T`,取反成 `false`,约束失败,模板被踢出候选;不等于 `T`,取反成 `true`,约束通过,模板正常参与重载决议。就这么个逻辑。

把这个约束挂回去看效果:

```cpp
template<typename Functor>
    requires not_the_same_t<Functor, OnceCallback>
explicit OnceCallback(Functor&& f) : status_(Status::kValid), func_(std::move(f)) {}
```

传进来的是 `OnceCallback` 自身(比如移动构造那个场景),`not_the_same_t<OnceCallback, OnceCallback>` 求值 `!true = false`,约束不满足,模板被晾在一边,编译器只能乖乖选移动构造函数。传进来的是 lambda、函数指针这些别的类型,约束满足,模板正常接活儿,被选为构造函数。就是这么干净。

---

## 这不是 OnceCallback 的专利

这玩意儿不是 OnceCallback 独一份的需求。`std::move_only_function` 自己的实现里挂着几乎一模一样的约束,只不过标准库走的是标准 concept `std::constructible_from` 配 `!std::is_same_v` 那一套写法。说穿了,任何 move-only 的类型擦除包装器,都得吃这记防御——只要您的类同时有"接受任意类型的模板构造函数"和"编译器生成的移动构造函数",这两者就一定会掐架,必须拿约束把它们隔开。

```text
模式总结：
模板构造函数 + requires 排除自身类型 = 保护移动语义的正确匹配
```

笔者给您留句话:以后要是自己撸 `unique_function`、`any_invocable` 这类 move-only 包装器,记住这个套路,它是通用的防御手段,省得回头调试半天才发现移动语义被模板截胡了。

---

## 踩坑预警

**坑一:漏掉 `std::decay_t`。** 偷懒只写 `!std::is_same_v<F, T>`,不加 `std::decay_t`,坑就埋下了——`F` 的推导结果可能带引用也可能不带,完全看您怎么调用。看下面这两个场景:

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

场景 B 要是漏了 `decay_t`,`const OnceCallback&` 跟 `OnceCallback` 压根不是同一类型,约束反而通过了,模板构造函数被选中——可语义上咱们要的是编译错误(拷贝已删除),至少也不该是模板构造。补上 `decay_t` 之后,`const OnceCallback&` 退化成 `OnceCallback`,两边对上,约束才正确失败。这个坑笔者踩过,debug 了半天才发现是 `decay_t` 漏了。

**坑二:`static_assert(false)` 在模板里会"误伤"。** C++23 之前,在模板里写 `static_assert(false, "...")`,所有实例化都会触发断言失败——哪怕这个模板从头到尾没人调用过。因为老标准要求 `static_assert(false)` 在模板定义那一刻就立即求值。Chromium 的绕法是 `static_assert(!sizeof(*this), "...")`:`!sizeof` 永远是 `false`,但它依赖 `*this` 的类型,是个依赖型表达式,定义时不会求值,得等实例化才炸。C++23 把这条规则放宽了,但您要是还在 C++20 编译,这事儿照样得留心。

---

下一篇咱们去看 `std::move_only_function`——它是 OnceCallback 的核心存储类型,也是咱们拿标准库设施替换 Chromium 手写 BindState 的关键拼图。

## 参考资源

- [cppreference: Constraints and concepts](https://en.cppreference.com/w/cpp/language/constraints)
- [cppreference: std::decay](https://en.cppreference.com/w/cpp/types/decay)
- [Stack Overflow: Generic constructor template called instead of copy/move constructor](https://stackoverflow.com/questions/70267685/generic-constructor-template-called-instead-of-copy-move-constructor)
