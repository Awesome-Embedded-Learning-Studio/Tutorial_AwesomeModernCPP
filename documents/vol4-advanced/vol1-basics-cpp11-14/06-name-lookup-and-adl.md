---
title: "名字查找与 ADL:两阶段查找是怎么回事"
description: "模板里的名字查找跟普通代码完全不同,它分两个阶段。讲清两阶段查找、依赖名与非依赖名、ADL(实参依赖查找),以及为什么前面几篇的 typename、this->、隐藏友元那些看似奇怪的规则必须存在"
chapter: 12
order: 6
tags:
  - host
  - cpp-modern
  - intermediate
  - 模板
  - 泛型
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17, 20]
reading_time_minutes: 20
prerequisites:
  - "类模板:成员、依赖名与惰性实例化"
  - "非类型模板参数:从整数到 C++20 的浮点与类类型"
related:
  - "模板友元与 Barton-Nackman:隐藏友元技巧"
  - "模板特化与偏特化:模式匹配的艺术"
---

# 名字查找与 ADL:两阶段查找是怎么回事

写普通代码时,您用一个名字,编译器在当前位置查一查,找到就用。模板里这一套不灵。模板的名字查找分两个阶段进行,一个在模板定义的时候,一个在模板实例化的时候。这套机制叫两阶段查找(two-phase lookup),它直接解释了前面几篇里那些看起来很奇怪的规则:`typename` 为什么不能省、`this->` 为什么必须有、隐藏友元为什么有用。这一篇把两阶段查找、依赖名和非依赖名、ADL 一次讲透,讲完之后,模板里那些「莫名其妙」的报错,您都能找到根源。

## 普通代码的名字查找:一次查完

先看普通函数。在普通函数里用到一个名字,编译器在函数定义点做一次普通查找(ordinary lookup),从内向外一层层命名空间找,找到候选集就停下来。

```cpp
void helper(int) {}

void caller() {
    helper(42);   // 编译器在 caller 定义点查 helper,找到 ::helper(int)
}
```

这套很直觉。普通查找只看「定义点之前」可见的名字,定义点之后才声明的同名函数,它不看。

## 模板不一样:分两阶段

模板把这件事拆成了两个阶段。

**第一阶段**(模板定义阶段):编译器解析模板定义时,对所有**非依赖名**做查找和绑定。这时候 `T` 还不知道是什么,所以依赖 `T` 的名字没法查,先放一边。

**第二阶段**(模板实例化阶段):模板被某个具体类型实例化时,`T` 已经确定了,编译器再对**依赖名**做查找,这一阶段的主要工具是 ADL(实参依赖查找)。

为什么要分两阶段?因为模板定义的时候,编译器根本不知道 `T` 会是什么,自然不知道 `T::value_type` 是个类型还是个变量,也不知道 `foo(t)` 里的 `foo` 该去哪个命名空间找。所以它把能查的(非依赖名)先查了,不能查的(依赖名)留到实例化时再说。

来看一个能直接看出两阶段效果的经典例子。

```cpp
#include <iostream>

// 模板定义之前就存在的 helper
void helper(int) { std::cout << "::helper(int), defined before template\n"; }

template <typename T>
void call_it(T x) {
    helper(x);   // helper 是非依赖名,第一阶段(定义点)查找并绑定
}

// 模板定义之后才加的 helper
void helper(double) { std::cout << "::helper(double), defined after template\n"; }

int main() {
    call_it(3.14);   // T=double。直觉上会不会期望走 helper(double)?
    return 0;
}
```

跑一下:

```bash
$ g++ -Wall -Wextra -std=c++20 twophase.cpp -o twophase && ./twophase
::helper(int), defined before template
```

`call_it(3.14)` 实际走的是 `helper(int)`,不是定义在后面的 `helper(double)`。原因就是两阶段:`helper` 这个名字是非依赖名(不带 `T`),在第一阶段(模板定义点)就被查找并绑定到当时唯一可见的 `helper(int)`。定义在模板后面的 `helper(double)`,第一阶段看不到;第二阶段只对依赖名做 ADL,`double` 是内置类型没有关联命名空间,ADL 引入不了新候选。于是 `helper(x)` 永远绑定到 `helper(int)`,`3.14` 被截断成 `3` 传进去。

注意这里和普通函数其实没有差异:非依赖名的查找都在「定义点」完成,不管 `call_it` 是模板还是普通函数,只要它定义在 `helper(double)` 之前,就只能看到 `helper(int)`。两阶段查找真正和普通函数不一样的地方,在依赖名的第二阶段——下一节讲 ADL 时会看到,模板能通过实参类型在实例化点找到定义点根本看不到的函数,这才是两阶段查找的独门本事。

## 依赖名 vs 非依赖名

这条线决定一个名字在哪个阶段查,值得单独强调。

非依赖名(non-dependent name),不带任何模板参数的名字。比如 `helper`、`std::cout`、`int`。它们在第一阶段查。

依赖名(dependent name),直接或间接依赖某个模板参数的名字。比如 `T::value_type`、`x.foo()`(当 `x` 的类型是 `T`)、`foo(t)`(当 `t` 的类型依赖 `T`)。它们在第二阶段查,主要靠 ADL。

一个调用 `foo(t)` 是不是依赖,看参数 `t` 的类型是否依赖 `T`。如果 `t` 是 `T` 类型,那 `foo(t)` 是依赖调用,第二阶段会通过 ADL 在 `T` 所在的命名空间里找 `foo`。这是 ADL 发挥作用的入口。

## typename 和 this->:两阶段的直接后果

回头看前面几篇的两条规则,它们都是两阶段查找的直接后果。

`typename` 消歧义。`T::value_type` 这种依赖名,编译器在第一阶段(定义点)不知道 `T` 是什么,自然不敢假定 `value_type` 是类型。它默认把依赖的限定名当成变量或函数,除非您用 `typename` 显式声明「这是个类型」。这是两阶段在定义点「查不动」的必然结果。

`this->` 访问 dependent base 成员。基类是 `Base<T>` 时,它的成员在第一阶段也看不到(因为 `Base<T>` 的具体长相要等 `T` 确定)。编译器在第一阶段不查 dependent base 里的非依赖名,所以直接写 `helper()` 找不到基类的 `helper`,要用 `this->helper()` 把查找推迟到第二阶段(实例化时,`this` 的类型已知,基类成员可见)。

理解了这两条规则都源于两阶段查找,您就不会再觉得它们是「多余的语法」,它们是让模板行为可预测的必要机制。没有两阶段,模板的查找结果会随着实例化点的上下文剧烈变化,谁也写不出可靠的模板库。

## ADL:实参依赖查找

ADL(argument-dependent lookup)是两阶段第二阶段查依赖名的核心工具。规则说起来朴素:**调用一个函数时,除了普通查找,编译器还会在「实参类型所在的命名空间」里找候选函数**。

```cpp
#include <iostream>

namespace geo {
    struct Point { int x; int y; };
    void draw(const Point&) { std::cout << "geo::draw(Point)\n"; }
}

int main() {
    geo::Point p{1, 2};
    draw(p);   // 既没写 geo:: 限定,也没 using namespace geo
    return 0;
}
```

跑一下:

```bash
$ g++ -Wall -Wextra -std=c++20 adl.cpp -o adl && ./adl
geo::draw(Point)
```

`main` 里调用 `draw(p)`,既没有 `geo::` 限定,也没有 `using namespace geo`。普通查找在全局命名空间找不到 `draw`,本该报错。但 ADL 介入了:实参 `p` 的类型是 `geo::Point`,它在 `geo` 命名空间里,于是编译器去 `geo` 里找 `draw`,找到了 `geo::draw`。调用成功。

ADL 的别名 Koenig lookup,得名于 Andrew Koenig,他最早提出了这套规则。它的设计动机是:操作某个类型的函数,通常就定义在那个类型所在的命名空间里。`std::cout << x` 能工作,靠的就是 ADL 在 `x` 的类型所在命名空间里找 `operator<<`。如果没有 ADL,您每次都得写完整的限定,泛型代码根本没法写。

## ADL 的工程意义:运算符和泛型算法

ADL 不是花架子,它是现代 C++ 几个关键惯用法的支柱。

**运算符查找**。`std::cout << myObj` 之所以能找到 `myObj` 类型对应的 `operator<<`,全靠 ADL。`myObj` 的类型在哪个命名空间,ADL 就去那个命名空间找 `operator<<`。这是运算符重载能跨命名空间工作的根本。

**swap 惯用法**。泛型代码里交换两个值,标准写法是:

```cpp
using std::swap;
swap(a, b);   // 不写 std::swap(a,b),而是先 using std::swap 再裸调
```

`using std::swap` 把 `std::swap` 引入候选,然后裸调 `swap(a, b)`。这样如果 `a` 的类型在某个命名空间里提供了更高效的 `swap`(比如 `ns::swap`),ADL 会找到它并优先用;如果没有,退化到 `std::swap`。这就是为什么标准库到处用 `using std::swap; swap(a,b);` 而不是直接 `std::swap(a,b)`,给自定义类型留优化空间。`std::begin`、`std::end`、`std::size`、`std::data` 这一组也是同一思路。

**「可被发现」的设计**。您想让某个函数被泛型算法找到,把它放进实参类型所在的命名空间就行,ADL 会自动发现。这是 C++ 命名空间设计里很重要的一环:函数跟着它操作的类型走,而不是全局散落。

## 两阶段查找的坑和编译器差异

两阶段查找有几个坑,加上历史包袱,值得说一下。

**「定义点之后加的重载模板看不到」**。前面 `helper` 的例子就是这个坑:在模板定义之后才声明的同名重载,模板内部的非依赖名调用看不到它们。解决办法要么把重载声明挪到模板前面,要么让调用变成依赖的(让 ADL 能介入)。

**MSVC 的历史包袱**。很长一段时间,MSVC 的编译器**不严格实现两阶段查找**,它把所有查找都推迟到实例化阶段一次性做。这导致一些在 GCC、Clang 上会报错的代码(比如违反两阶段规则的),在 MSVC 上能编译通过;反之也有。这种「MSVC 能过、别处炸」或「别处能过、MSVC 炸」的差异,是跨平台模板库开发的经典痛点。MSVC 后来加了 `/permissive-` 开关来严格遵循两阶段,现代项目基本都会开,但老代码的坑还在。

**ADL 的意外命中**。ADL 有时会找到您没想到的函数。比如某个类型恰好在某个命名空间里有个同名函数,ADL 可能把它卷进来,导致二义性或调到错误实现。隐藏友元(hidden friends,下一篇讲)就是治这个病的:把运算符定义成类的隐藏友元,让它只对实参类型精确匹配时才被 ADL 发现,避免污染全局重载池。

下一篇咱们进模板友元和 Barton-Nackman 技巧。隐藏友元、奇递归模板里的友元注入,都是建立在您理解了 ADL 之后才能讲清的东西。
