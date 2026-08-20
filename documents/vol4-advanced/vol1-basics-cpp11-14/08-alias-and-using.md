---
chapter: 12
cpp_standard:
- 11
- 14
- 17
description: C++11 的别名模板(template<typename T> using X = ...)解决了 typedef 不能参数化的老问题,C++14
  起的 _t 别名和 C++17 起的 _v 变量让 type traits 写起来清爽,using 还能在模板继承里引入 dependent base 的名字。这一篇讲清这三种用法
difficulty: intermediate
order: 8
platform: host
prerequisites:
- 名字查找与 ADL:两阶段查找是怎么回事
- 类模板:成员、依赖名与惰性实例化
reading_time_minutes: 7
related:
- 模板友元与 Barton-Nackman:隐藏友元技巧
- CRTP:用奇递归模板做静态多态
tags:
- host
- cpp-modern
- intermediate
- 模板
- 类型别名
- 泛型
title: 别名模板与 using 声明:给类型起个短名字
---
# 别名模板与 using 声明:给类型起个短名字

C++11 给类型起别名这件事做了一次大升级,引入了 `using` 的新用法和**别名模板**(alias template)。老式的 `typedef` 能给一个类型起别名,但它没法参数化,您想给 `std::vector<T>` 起个短名字,`typedef` 做不到,只能借类模板的嵌套类型绕一圈。`using` + 别名模板直接解决了这个问题。这一篇讲三种用法:别名模板本身、C++14 起 type traits 配的 `_t` 别名(C++17 又补了 `_v` 变量)、以及 `using` 在模板继承里引入基类名字的作用(承接第三篇讲过的 `this->`)。

## typedef 的局限:不能参数化

`typedef` 是 C 的老传统,能给一个类型起个新名字。

```cpp
typedef std::vector<int> IntVec;   // IntVec 就是 std::vector<int>
IntVec v;
```

这没问题。但您想给「任意 `T` 的 `vector`」起个通用别名,`typedef` 就抓瞎了,它不能带模板参数。C++11 之前的绕路写法是借助类模板的嵌套 `using`:

```cpp
template <typename T>
struct VecHelper {
    using type = std::vector<T>;   // 嵌套在类模板里,才能参数化
};

VecHelper<int>::type v;   // 又是 ::type,啰嗦
```

这能跑,但每次用都要写 `VecHelper<int>::type`,前面讲依赖名时提过,这里还得加 `typename`,变成 `typename VecHelper<T>::type`,又长又别扭。

## C++11 别名模板:using 的模板化

C++11 的别名模板让这件事干净了。语法是 `template <...> using 名字 = ...`,直接给「带参数的类型」起别名:

```cpp
template <typename T>
using Vec = std::vector<T>;   // 别名模板

Vec<int> v = {1, 2, 3};        // 等价 std::vector<int>
```

跑一下:

```bash
$ g++ -Wall -Wextra -std=c++17 alias.cpp -o alias && ./alias
size = 3
```

`Vec<int>` 就是 `std::vector<int>`,用起来没有任何区别。注意别名模板不是新类型,它纯粹是个「别名」,`Vec<int>` 和 `std::vector<int>` 是同一个类型,赋值、比较、重载都完全互通。这和 `typedef` 的语义一致,只是多了参数化能力。

别名模板的好处不止是短。它还能表达一些 `typedef` 表达不了的复杂类型,比如函数指针类型、带分配器的容器,用 `using` 写出来比 `typedef` 清楚得多:

```cpp
// typedef 写函数指针类型,顺序绕得让人头疼
typedef int (*Callback)(int, int);

// using 写出来,左值右值一致,可读多了
using Callback = int(*)(int, int);
```

现代 C++ 基本都用 `using` 替代 `typedef`,即使不带参数也用 `using`,风格统一。

## C++14 的 `_t` 和 `_v`:type traits 的简写

别名模板最实用的落地,是 C++14 给 `<type_traits>` 配的一套 `_t` 后缀别名。C++11 的 type traits 查询结果是嵌套在类里的 `::type` 或 `::value`,用起来啰嗦:

```cpp
// C++11:取去掉引用后的类型,得写 typename + ::type
typename std::remove_reference<T>::type
```

C++14 给每个返回类型的 trait 加了一个 `_t` 别名模板,一行搞定:

```cpp
// C++14:别名模板,清爽
std::remove_reference_t<T>
```

两者完全等价,`_t` 版本就是个别名模板,定义大致是 `template <typename T> using remove_reference_t = typename remove_reference<T>::type;`。返回布尔值的 trait 也有 `_v` 后缀的简写,比如 `std::is_integral<T>::value` 写成 `std::is_integral_v<T>`。这里要分清三样东西:`_t` 别名模板和 `_v` 变量模板都是**标准库助手**(`_t` 从 C++14 起,`_v` 从 C++17 起);而 `::value` 本身是 `std::integral_constant` 的静态成员常量,从 C++11 就有,跟变量模板是两码事。

实测看两种写法的等价:

```bash
$ g++ -Wall -Wextra -std=c++17 alias.cpp -o alias && ./alias
remove_reference_t<int&> is int?  true
remove_reference<int&>::type is int? true
```

`_t` 让模板元编程代码的可读性提升了一个档次。vol3 讲 concepts 和元编程时您会看到,现代代码里 `::type` 几乎绝迹,全是 `_t`。这也是为什么本卷前面几篇的 type traits 例子我直接用 `_v` 后缀(`is_pointer_v`、`is_same_v`),它们都是变量模板(C++14/17)的简写,和 `_t` 别名模板是同一套思路。

## 别名模板不能特化

别名模板有个绕不过的限制:**它不能特化**,既不能全特化也不能偏特化。您想给某个具体类型提供一个特殊的别名实现,别名模板做不到。

```cpp
template <typename T>
using V = T;

// 试图特化别名模板 —— 编译报错
template <>
using V<int> = long;   // 错:别名模板不能特化
```

```text
alias_bad.cpp:6:1: error: expected unqualified-id before 'using'
```

GCC 的报错措辞有点抽象,但意思就是「别名模板不接受特化」。如果您确实需要「针对不同类型给不同的类型别名」,得用类模板包一层(类模板能特化),把别名藏在嵌套 `using` 里,再写它的特化版本。这是别名模板相比类模板的一个能力短板,设计上就是这么定的,别名模板定位是「纯转发」,不做类型计算的分发。

## using 在模板继承里:引入 dependent base 的名字

第三篇讲 dependent base 时说过,访问基类模板的成员要用 `this->`,因为编译器在第一阶段不查 dependent base。`using` 提供了另一种写法:**用 `using` 声明把基类的名字引入派生类作用域**,之后调用就不用每次写 `this->`。

```cpp
#include <iostream>

template <typename T>
struct Base {
    static T kDefault;
    void greet() { std::cout << "Base::greet\n"; }
};
template <typename T>
T Base<T>::kDefault{42};

template <typename T>
struct Derived : Base<T> {
    // using 把 Base<T>::kDefault 和 Base<T>::greet 引入 Derived 作用域
    using Base<T>::kDefault;
    using Base<T>::greet;

    T fetch() const { return kDefault; }   // 直接用,不用 this->
    void hello() { greet(); }              // 直接用,不用 this->
};
```

跑一下:

```bash
$ g++ -Wall -Wextra -std=c++20 using_base.cpp -o using_base && ./using_base
fetch = 42
Base::greet
```

`using Base<T>::kDefault` 告诉编译器「`kDefault` 这个名字指的是 `Base<T>` 里的那个」,把查找绑定好,后续 `fetch` 里写 `kDefault` 就能直接找到,不用 `this->`。

`using` 引入和 `this->` 是解决同一个问题的两种写法,选哪个看场景。如果只是偶尔访问一两个基类成员,`this->` 现写现用更轻便。如果要频繁访问多个基类成员(比如派生类里到处用基类的类型别名和函数),在类开头集中写一组 `using` 声明,代码更干净。两种都是合法的现代写法,`using` 还有个额外好处:它能把基类的类型别名(`value_type`、`iterator` 这种)「继承」下来,让派生类对外暴露统一的类型接口,STL 容器的适配器和派生类大量这么用。

## 别名模板和模板参数推导

最后提一个 C++14 之后的发展。别名模板能参与模板参数推导,这让代码更灵活。比如您有个函数接受 `std::vector<T>`,传一个 `Vec<int>`(别名)进去,推导照常工作,因为别名就是原类型。C++20 的 CTAD(类模板参数推导)和别名模板也有交互,允许从构造函数推导别名模板的参数,这个比较深,vol3 讲 concepts 和推导时会专门讲。这一篇您只要记住:别名模板是「透明的」,它在推导、重载、类型等价上都和它指向的原类型完全一致。

下一篇是本卷概念部分的重头戏:CRTP,奇递归模板模式。它用「派生类把自己作为模板参数传给基类」的奇巧结构,做出编译期的静态多态,避开虚函数的运行时开销,是 Eigen、表达式模板等高性能库的核心技术。
