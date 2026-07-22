---
chapter: 12
cpp_standard:
- 11
- 14
- 17
- 20
description: 上一篇讲了 ADL,这一篇讲它最漂亮的搭档:隐藏友元和 Barton-Nackman 技巧。在类模板里把 operator== 定义成友元,每个实例化自动获得专属于该类型的运算符,既不污染全局重载池,又能被
  ADL 精确发现
difficulty: intermediate
order: 7
platform: host
prerequisites:
- 名字查找与 ADL:两阶段查找是怎么回事
- 类模板:成员、依赖名与惰性实例化
reading_time_minutes: 7
related:
- 别名模板与 using 声明:给类型起个短名字
- CRTP:用奇递归模板做静态多态
tags:
- host
- cpp-modern
- intermediate
- 模板
- 泛型
title: 模板友元与 Barton-Nackman:隐藏友元技巧
---
# 模板友元与 Barton-Nackman:隐藏友元技巧

上一篇咱们讲了 ADL,这一篇讲它最漂亮的搭档:**隐藏友元**(hidden friends)和 **Barton-Nackman 技巧**。核心想法一句话:把运算符(比如 `operator==`、`operator<<`)定义成类模板的**友元,并且直接在类内部给出定义**。这样每个实例化会自动生成一个专属于该类型的非模板运算符函数,它既不污染全局重载池,又能被 ADL 在精确类型匹配时发现。这是现代 C++ 给自定义类型配运算符的推荐姿势,标准库自己也大量这么用。

## 友元:快速回顾

友元(friend)是 C++ 给某个外部函数或类「访问自己私有成员」的权限。友元不是类的成员,它是外部实体,只是被特许能碰到私有部分。

```cpp
class Account {
    int balance_;
    friend void audit(const Account&);   // audit 不是成员,但能访问 balance_
public:
    explicit Account(int b) : balance_(b) {}
};

void audit(const Account& a) {
    std::cout << "balance = " << a.balance_ << "\n";   // 能访问私有,因为是友元
}
```

普通友元是「声明在类里,定义在类外」。模板友元在此基础上玩出了新花样,下面一步步看。

## 友元注入:在类模板里定义友元

关键的一步来了。友元不仅可以声明在类里、定义在类外,还可以**直接在类内部给出定义**。当这个类是模板时,类内定义的友元会随着类的实例化,为每个具体类型生成一个独立的函数。这个函数有个特殊性质:**它不在命名空间作用域可见,只能通过 ADL 发现**。

```cpp
template <typename T>
class Box {
    T v_;
public:
    constexpr Box(T v) : v_(v) {}

    // 类内友元定义:不是函数模板,是「随 Box<T> 实例化而生成的非模板函数」
    friend constexpr bool operator==(const Box& a, const Box& b) {
        return a.v_ == b.v_;
    }
};
```

注意这里的 `operator==` 没有独立的 `template <...>` 头,它写在 `Box` 类内部,参数类型用 `const Box&`(在类内部 `Box` 就是 `Box<T>` 的简写)。编译器实例化 `Box<int>` 时,会生成一个 `bool operator==(const Box<int>&, const Box<int>&)` 的具体函数;实例化 `Box<double>` 时,生成另一个 `bool operator==(const Box<double>&, ...)`。这两个函数互不相同,各管各的类型。

这个手法叫 **Barton-Nackman 技巧**,得名于 John Barton 和 Lee Nackman 1994 年的书《Scientific and Engineering C++》,他俩最早系统化地用了这套写法。它解决的核心问题是:给类模板自动配上运算符,又不用为每种类型写一遍特化。

## 完整例子:配齐 == 和 <<

来看一个能跑的完整例子,给 `Box<T>` 配上 `==` 和 `<<`。

```cpp
#include <iostream>

template <typename T>
class Box {
    T v_;
public:
    constexpr Box(T v) : v_(v) {}

    friend constexpr bool operator==(const Box& a, const Box& b) {
        return a.v_ == b.v_;
    }
    friend std::ostream& operator<<(std::ostream& os, const Box& b) {
        return os << "Box{" << b.v_ << "}";
    }
};

int main() {
    Box<int> x{1}, y{1}, z{2};
    Box<double> p{1.5}, q{1.5};
    std::cout << std::boolalpha;
    std::cout << "x == y: " << (x == y) << "\n";   // true
    std::cout << "x == z: " << (x == z) << "\n";   // false
    std::cout << "p == q: " << (p == q) << "\n";   // true(Box<double> 自己的 operator==)
    std::cout << x << "\n";                        // Box{1},ADL 找到 operator<<
    return 0;
}
```

```bash
$ g++ -Wall -Wextra -std=c++20 barton.cpp -o barton && ./barton
x == y: true
x == z: false
p == q: true
Box{1}
```

`Box<int>` 和 `Box<double>` 各自有自己的 `operator==`,比较 `x == y` 走 `Box<int>` 的版本,比较 `p == q` 走 `Box<double>` 的版本。`std::cout << x` 之所以能工作,是因为 `operator<<` 是 `Box<int>` 的隐藏友元,ADL 通过实参 `x` 的类型发现了它。您既没写 `std::operator<<`,也没用 `using namespace`,全靠 ADL。

## 隐藏友元的好处:不污染全局重载池

隐藏友元的真正价值,在和「命名空间作用域的函数模板 operator==」对比时才看得清。老式写法是这样的:

```cpp
// 老式写法:在命名空间作用域定义一个 operator== 模板
// (这里假设 Box 提供公开 value();本篇的 Box 把 v_ 设成私有,只能靠友元访问,
//  所以老式写法要么 Box 开个公开接口,要么把这个 operator== 也声明成友元)
template <typename T>
bool operator==(const Box<T>& a, const Box<T>& b) {
    return a.value() == b.value();
}
```

这个 `operator==` 是一个**函数模板**,它活在命名空间作用域,对所有 `Box<T>` 的比较都参与候选。问题在于,它会参与**所有** `==` 表达式的重载解析(只要实参类型沾边),导致两个麻烦。一是编译慢,每次 `==` 编译器都要考虑这个模板能不能套上。二是二义性风险,如果别的命名空间也有 `operator==` 模板,两个模板可能同时匹配,产生 ambiguity。

隐藏友元把这两个麻烦一起解决了。它不是函数模板,是随实例化生成的具体函数;它不在命名空间作用域可见,只有当 `==` 的实参类型精确匹配 `Box<T>` 时,ADL 才把它拉进候选。换句话说,它只在「该管的时候」出现,别的时候完全透明。这不仅让重载解析更快,也避免了不相关类型之间的意外匹配。

::: warning 隐藏友元的「不跨界」是特性不是 bug

隐藏友元只在实参类型精确匹配时被发现。`Box<int>` 的 `operator==` 只接受 `Box<int>`,不会接受 `Box<double>`。所以下面这行会编译报错:

```cpp
Box<int> x{1};
Box<double> p{1.5};
bool same = (x == p);   // 错:Box<int> 和 Box<double> 没有共同的 operator==
```

```text
barton_bad.cpp:12:20: error: no match for 'operator=='
      (operand types are 'Box<int>' and 'Box<double>')
```

这正是隐藏友元的安全性。如果您确实想让 `Box<int>` 和 `Box<double>` 能比较,您得显式写一个跨类型的运算符,而不是指望它「自动」发生。隐藏友元让类型关系变得明确,这正是它被推崇的原因。

:::

## 为什么这和 ADL 紧密相关

隐藏友元离开 ADL 就没法用。前面说过,类内定义的友元不在命名空间作用域可见,普通查找找不到它。只有 ADL 才能发现它:调用 `x == y` 时,编译器通过实参 `x`(类型 `Box<int>`)的所在作用域,把 `Box<int>` 的隐藏友元 `operator==` 拉进候选集。

所以隐藏友元是 ADL 的最佳搭档。ADL 让「定义在类里的运算符」能被找到,隐藏友元让「不该被找到的调用」找不到。两者配合,运算符的作用域被精确控制在「只对该类型生效」的范围。这也是上一篇花那么大篇幅讲 ADL 的原因,它是理解这一篇的前提。

## 一个实战写法:给类型配齐比较运算符

实际写库时,给一个类型配运算符的推荐模板长这样:

```cpp
class Temperature {
    double kelvin_;
public:
    constexpr explicit Temperature(double k) : kelvin_(k) {}
    constexpr double k() const { return kelvin_; }

    // 隐藏友元:所有运算符都写成类内友元
    friend constexpr bool operator==(Temperature a, Temperature b) {
        return a.kelvin_ == b.kelvin_;
    }
    friend constexpr bool operator!=(Temperature a, Temperature b) {
        return !(a == b);   // 复用 ==
    }
    friend constexpr bool operator<(Temperature a, Temperature b) {
        return a.kelvin_ < b.kelvin_;
    }
    // ... 其它比较运算符
};
```

C++20 之后有个更省事的办法:定义一个 `operator<=>`(三路比较运算符,本卷另一篇专门讲),编译器会自动帮您生成 `==`、`!=`、`<`、`<=`、`>`、`>=`。但如果您不用 spaceship,或者要自定义某些运算符的语义,隐藏友元仍然是首选写法。标准库里的迭代器、`std::chrono` 的时长类型,运算符基本都写成隐藏友元。

下一篇咱们进别名模板和 using 声明。`template <typename T> using vec = std::vector<T>;` 这种写法怎么给类型起短名字、别名模板为什么不能特化、它在模板继承里引入基类名字的作用,都会讲到。
