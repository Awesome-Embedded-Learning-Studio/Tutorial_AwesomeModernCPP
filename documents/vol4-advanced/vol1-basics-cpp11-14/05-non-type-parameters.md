---
title: "非类型模板参数:从整数到 C++20 的浮点与类类型"
description: "非类型模板参数让一个「值」也能被参数化,array<T,N> 里的 N 就是它。讲清它能用什么类型、C++17 的 auto 推导、C++20 把它放宽到浮点和满足 structural 条件的类类型,以及哪些实参算「同一个」实例化"
chapter: 12
order: 5
tags:
  - host
  - cpp-modern
  - intermediate
  - 模板
  - 泛型
  - 编译期计算
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17, 20]
reading_time_minutes: 18
prerequisites:
  - "模板特化与偏特化:模式匹配的艺术"
  - "类模板:成员、依赖名与惰性实例化"
related:
  - "名字查找与 ADL:两阶段查找是怎么回事"
  - "模板友元与 Barton-Nackman:隐藏友元技巧"
---

# 非类型模板参数:从整数到 C++20 的浮点与类类型

前面几篇咱们参数化的都是「类型」,`typename T` 占位一个类型。模板还能参数化另一种东西:**一个编译期已知的值**。`std::array<T, N>` 里的 `N`、`std::bitset<N>` 里的 `N`,都是这种参数,叫非类型模板参数(non-type template parameter)。这一篇讲它能接受什么类型的值,C++17 的 `auto` 怎么让它更灵活,C++20 又怎么大刀阔斧地把它从「只能用整数和指针」放宽到「浮点甚至类类型」,以及哪些实参算「同一个」实例化。C++20 这次放宽是非类型参数诞生以来最大的一次升级,直接催生了 fixed_string、编译期常量对象这些新玩法。

## 非类型参数是什么:参数化一个值

类型参数 `typename T` 占位的是一个类型,非类型参数占位的是一个**具体的值**。最经典的形式是整数:

```cpp
template <typename T, std::size_t N>
struct array {
    T data[N];   // N 是编译期就知道的大小
};
```

这里 `N` 是一个非类型参数,它的「类型」是 `std::size_t`,实例化时您要给它一个具体的值,比如 `array<int, 8>`,`N` 就是 8。`N` 必须在编译期就能确定,因为编译器要用它生成 `int data[8]` 这个数组类型,这是类型的一部分,运行时变不了。

非类型参数能用哪些类型?在 C++17 之前,规则挺窄:整数类型(各种 `int`、`char`、`bool`)、枚举、指针、引用,还有指向成员的指针。浮点数不行,普通的类对象也不行。这个限制催生了一堆绕路写法,C++20 才把它们扶正。

## 整数和指针:最传统的用法

整数非类型参数最常见,固定大小的容器、位集、编译期常量都靠它。

```cpp
template <int Lower, int Upper>
struct Range {
    static constexpr int lo = Lower;
    static constexpr int hi = Upper;
};

// 实参必须是编译期常量
constexpr int kMin = 10;
Range<kMin, kMin + 100> r;   // OK:实参都是常量表达式
// Range<some_runtime_value, 100> r2;   // 编译报错:实参不是常量
```

指针和引用也能做非类型参数,但实参必须是「编译期能确定地址」的对象,比如静态变量的地址、函数地址:

```cpp
template <int* P>
struct PtrHolder {
    static int* get() { return P; }
};

int global_var = 42;
PtrHolder<&global_var> ph;   // OK:全局变量地址编译期已知
```

这条「实参必须是常量表达式」的要求,是非类型参数和普通函数参数最根本的区别。普通函数参数运行时传值,非类型参数在编译期就得是确定值。

## C++17 的 auto:让类型自动推导

C++17 给非类型参数加了一个很实用的东西:`auto` 占位符。以前您得写明非类型参数的类型,`template <int N>`;现在可以写 `template <auto N>`,让编译器根据实参推导。

```cpp
template <auto N>
struct Constant {
    static constexpr auto value = N;
};
```

跑一下看它的灵活:

```bash
$ g++ -Wall -Wextra -std=c++17 ntp_auto.cpp -o ntp_auto && ./ntp_auto
Constant<42>::value = 42
Constant<true>::value = 1
Constant<'a'>::value = a
```

`Constant<42>` 推出 `N` 是 `int`,`Constant<true>` 推出 `bool`,`Constant<'a'>` 推出 `char`。一个模板参数,装下了不同类型的值。这在写通用元函数时省去了一堆 `template <typename T, T N>` 的样板,以前要两个参数(类型 + 值),现在一个 `auto` 搞定。

## C++20 的两大放宽:浮点和类类型

C++20 对非类型参数做了一次大手术,放开了两块以前不让碰的禁区。

**浮点数**。C++20 之前,浮点数不能做非类型参数,原因是浮点的等价性判定有精度坑(两个编译期浮点值是不是「相等」很难定义干净)。C++20 把规则定好后,放开了:

```cpp
template <double Pi>
struct CircleArea {
    static constexpr double compute(double r) { return Pi * r * r; }
};
```

```bash
$ g++ -Wall -Wextra -std=c++20 ntp_float.cpp -o ntp_float && ./ntp_float
area(2.0) = 12.5664
```

`CircleArea<3.14159265>` 把圆周率当编译期常量烤进类型里。以前这种事只能靠 `constexpr double pi = ...` 的普通变量,现在能直接进模板参数,意味着不同的 `Pi` 值会生成不同的类型,您可以让类型本身就携带精度信息。

**类类型(structural class)**。这是 C++20 放宽里更有想象力的一块。以前类对象不能做非类型参数,因为对象有构造、有地址、有生命周期,没法简单比较等价。C++20 引入了 **structural type** 的概念:满足几个条件的类类型,可以当非类型参数用。条件是:它得是个 **literal class type**(字面类,只要有 constexpr 构造函数就行,**不要求是聚合 aggregate**)、所有基类和非静态数据成员都得是 `public` 且非 `mutable`、且基类和成员本身也是 structural。简单说就是一个「全公开、成员不可变的值类型」,编译器能用它的成员逐一比较等价。

这里有个容易混的点:literal class 和 aggregate 不是一回事。aggregate 禁止用户声明构造函数,literal class 只要求有 constexpr 构造函数(可以有用户构造)。structural 要的是 literal class,所以**带 constexpr 构造函数的类(哪怕不是 aggregate)也能做非类型参数**——下一节要讲的 fixed_string 正是靠这个:它有 constexpr 构造拷贝 `char` 数组,不是 aggregate,但符合 structural。

```cpp
struct Point {      // structural:public、非 mutable、成员都是 structural(int);也是 literal class
    int x;
    int y;
};

template <Point P>
struct Pixel {
    static constexpr Point pos = P;
};
```

```bash
$ g++ -Wall -Wextra -std=c++20 ntp_struct.cpp -o ntp_struct && ./ntp_struct
origin: (0,0)
corner: (3,4)
```

`Pixel<Point{3, 4}>` 把一个二维坐标烤进了类型。这个能力的招牌应用是 **fixed_string**:用一个 structural 的字符串类(带 constexpr 构造、内部是 `char` 数组),把字符串包进类型里,实现「类型里带字符串」。日志库给每个日志级别一个 fixed_string 标签、网络库把 URL 路径当类型,都建立在这上面。注意字符串字面量**从来不能**直接做非类型参数(C++20 也一样),它的类型是 `const char[N]`,会 decay 成指针,不同字面量地址不同,等价性没法处理;C++20 的突破是允许用 structural 的 fixed_string 把字符串**包起来**当 NTTP,而不是字面量直接上。

## 等价性:哪些实参算「同一个」

非类型参数有个绕不开的问题:两个实参什么时候算「同一个实例化」?规则叫 template-argument-equivalent。对整数,看值相等:`1 + 1` 和 `2` 是等价的,`Tag<1 + 1>` 和 `Tag<2>` 是同一个类型。

```cpp
#include <iostream>
#include <type_traits>

template <int N>
struct Tag {};

int main() {
    std::cout << std::boolalpha;
    std::cout << "Tag<1+1> is Tag<2>? " << std::is_same_v<Tag<1 + 1>, Tag<2>> << "\n";
    std::cout << "Tag<2*3> is Tag<6>? " << std::is_same_v<Tag<2 * 3>, Tag<6>> << "\n";
}
```

```bash
$ g++ -Wall -Wextra -std=c++20 ntp_equiv.cpp -o ntp_equiv && ./ntp_equiv
Tag<1+1> is Tag<2>? true
Tag<2*3> is Tag<6>? true
```

程序输出 `true`,就证明 `Tag<1+1>` 和 `Tag<2>` 是同一个类型。这条规则的实际意义是:不管写 `1+1` 还是 `2`,编译器不会重复实例化两份 `Tag<2>`,它认得这是同一个东西。

对指针和引用,等价性看「指向同一个对象或函数」。对浮点(C++20)和 structural 类类型(C++20),等价性看值的位级或成员级比较。浮点这里有个要注意的点:两个浮点实参只有「位级相同」才算等价,不是「数值相等」。最典型的反例是 `Circle<0.0>` 和 `Circle<-0.0>`:`0.0 == -0.0` 数值上相等,但位表示不同(符号位不同),所以是**两个不同的类型**。这避免了精度模糊带来的歧义,也意味着传浮点模板参数时,字面量要写精确。

## 非类型参数的典型用途

把非类型参数的常见用法归个类,您碰到时就知道往哪想。

固定大小的容器。`std::array<T, N>`、`std::bitset<N>`、本卷最后的 `fixed_vector<T, N>` 综合项目,都用整数非类型参数把大小烤进类型。好处是存储在栈上、无动态分配、大小类型安全(不同 `N` 的 `array` 是不同类型,编译器帮您防误用)。

编译期常量。把物理常量、配置值、版本号当非类型参数烤进类型,本篇的 `CircleArea<3.14>` 就是这类。类型携带了值,可以实现编译期的多态分发。

C++20 之后的字符串与对象。fixed_string、编译期坐标、编译期配置对象,都靠 structural class non-type parameter。这是 C++20 给模板元编程开的新窗口,vol3 元编程部分会专门展开。

## 几个坑

实参必须是常量表达式。这是铁律,运行时的值传不进去,会编不过。

字符串字面量从来不能直接做非类型参数(C++20 也一样)。`Foo<"hello">` 不行,因为字符串字面量 decay 成指针后等价性没法处理。C++20 的解法是用 structural 的 fixed_string 类把字符串包一层,vol3 会讲。

浮点实参的等价性是位级比较。`Circle<0.0>` 和 `Circle<-0.0>` 数值相等但位级不同,是两个类型;反过来 `Circle<3.14>` 和 `Circle<3.14000>` 因为词法折叠后位级完全相同,反而是同一个类型。判据是位级,不是数值,也不是写法。

下一篇咱们进两阶段名字查找和 ADL。模板里的名字查找跟普通代码完全不是一个套路,它分两个阶段进行,这个机制直接解释了前面几篇里 `typename`、`this->`、隐藏友元这些看似奇怪的规则为什么必须存在。
