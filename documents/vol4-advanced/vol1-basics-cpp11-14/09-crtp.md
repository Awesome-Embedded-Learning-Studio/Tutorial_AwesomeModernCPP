---
title: "CRTP:用奇递归模板做静态多态"
description: "CRTP 让派生类把自己作为模板参数传给基类,做出编译期的静态多态,避开虚函数的 vtable 和运行时分派。这一篇讲清它的结构、零开销的汇编证据、mixin 等典型用途,以及它的坑和 C++23 deducing this 这个现代替代"
chapter: 12
order: 9
tags:
  - host
  - cpp-modern
  - intermediate
  - 模板
  - CRTP
  - 泛型
  - 零开销抽象
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17, 20, 23]
reading_time_minutes: 22
prerequisites:
  - "类模板:成员、依赖名与惰性实例化"
  - "名字查找与 ADL:两阶段查找是怎么回事"
  - "别名模板与 using 声明:给类型起个短名字"
related:
  - "综合项目:fixed_vector<T, N>"
  - "模板友元与 Barton-Nackman:隐藏友元技巧"
---

# CRTP:用奇递归模板做静态多态

CRTP,全称 Curiously Recurring Template Pattern,奇递归模板模式。它的结构乍一看很怪:派生类在继承基类时,把自己作为模板参数传给基类。`struct Derived : Base<Derived>`。这种「自己引用自己」的奇巧写法,能做出编译期的**静态多态**,在不需要运行时确定具体类型时,完全避开虚函数的 vtable 和分派开销。Eigen 的表达式模板、很多高性能库的 mixin 机制,都建立在 CRTP 上。这一篇讲清它的结构、零开销的汇编证据、几个典型用途,以及它绕不开的坑。

## CRTP 长什么样:派生类把自己传给基类

先看最小例子。基类 `Shape` 是个模板,模板参数是「派生类自己」。

```cpp
#include <iostream>

template <typename Derived>
struct Shape {
    const char* name() {
        return static_cast<Derived*>(this)->name_impl();   // 把 this 转成派生类指针
    }
    double area() {
        return static_cast<Derived*>(this)->area_impl();
    }
};

struct Circle : Shape<Circle> {        // Circle 把自己 Circle 传给 Shape
    double r;
    explicit Circle(double r_) : r(r_) {}
    const char* name_impl() { return "Circle"; }
    double area_impl() { return 3.14159 * r * r; }
};

struct Square : Shape<Square> {
    double side;
    explicit Square(double s) : side(s) {}
    const char* name_impl() { return "Square"; }
    double area_impl() { return side * side; }
};
```

关键在 `struct Circle : Shape<Circle>` 这一行。`Circle` 继承 `Shape`,但传给 `Shape` 的模板参数是 `Circle` 自己。于是基类 `Shape<Derived>` 里的 `Derived` 就是 `Circle`,基类的 `area()` 通过 `static_cast<Derived*>(this)->area_impl()` 调用,实际调的是 `Circle::area_impl`。

跑一下:

```bash
$ g++ -Wall -Wextra -std=c++20 crtp_basic.cpp -o crtp_basic && ./crtp_basic
Circle area = 12.5664
Square area = 9
```

`Circle` 走 `Circle::area_impl`,`Square` 走 `Square::area_impl`,各算各的。看起来和虚函数多态效果一样,但机制完全不同。

## 静态多态:编译期就知道具体类型

虚函数的多态是**运行时**的。`base->area()` 时,`base` 指向的具体类型要到运行时才知道,编译器只能生成「查 vtable、间接调用」的代码。

CRTP 的多态是**编译期**的。`Shape<Circle>::area()` 在实例化时,`Derived` 已经钉死是 `Circle`,`static_cast<Derived*>(this)->area_impl()` 就是 `static_cast<Circle*>(this)->area_impl()`,编译器清楚地知道调的是 `Circle::area_impl`,可以直接调用,甚至内联。没有 vtable,没有运行时分派。

这一点是 CRTP 的核心价值:**在编译期能确定具体类型时,用静态多态替代虚函数,换来零开销**。

## 零开销的汇编证据

口说无凭,看汇编。写一个 CRTP 版和一个虚函数版,功能一样(返回 42),用 `-O2` 编译,对比 `use` 函数的反汇编。

```cpp
// crtp_asm.cpp —— CRTP 版
template <typename D>
struct Base {
    int compute() { return static_cast<D*>(this)->compute_impl(); }
};
struct Concrete : Base<Concrete> {
    int compute_impl() { return 42; }
};
int use_crtp() { Concrete c; return c.compute(); }
```

CRTP 版 `use_crtp` 的完整反汇编只有两条指令:

```text
0000000000000000 <_Z8use_crtpv>:
   0:   b8 2a 00 00 00   mov    $0x2a,%eax    ; 0x2a = 42,直接把结果放进返回值
   5:   c3               ret
```

`compute()` 和 `compute_impl()` 全被内联了,编译器直接算出结果是 42,连函数调用都省了。`use_crtp` 就是「把 42 放进 eax,返回」。

再看虚函数版(通过引用调用,强制虚分派):

```cpp
// vtable_asm.cpp —— 虚函数版
struct Base {
    virtual int compute() = 0;
    virtual ~Base() = default;
};
struct Concrete : Base {
    int compute() override { return 42; }
};
int use_virtual(Base& b) { return b.compute(); }   // 通过引用,编译器无法去虚化
```

`use_virtual` 的反汇编要长得多:

```text
0000000000000000 <_Z11use_virtualR4Base>:
   0:   48 8b 07         mov    (%rdi),%rax        ; 从对象取 vtable 指针
   3:   48 8d 15 ...     lea    ...(%rip),%rdx     ; Concrete 的 vtable 地址
   a:   48 8b 00         mov    (%rax),%rax        ; 从 vtable 取 compute 的函数指针
   d:   48 39 d0         cmp    %rdx,%rax          ; 推测去虚化:是不是 Concrete?
  10:   75 0e            jne    20                 ; 不是才走间接 call
  12:   b8 2a 00 00 00   mov    $0x2a,%eax         ; 是 Concrete,直接返回 42
  17:   c3               ret
```

虚函数版有 vtable 指针的两次解引用(`mov (%rdi)` 取 vtable,`mov (%rax)` 取函数指针),还有一次比较和条件跳转(GCC 的「推测去虚化」优化:先猜 `b` 是不是 `Concrete`,猜中就直接返回,猜不中才走真正的间接调用)。即便开了 `-O2`,这套 vtable 访问和去虚化检查的开销仍在。

对比一目了然:CRTP 版两条指令、零内存访问;虚函数版七条指令、两次内存访问(vtable)。这就是「静态多态零开销」最直接的证据。在 Eigen 这种数值计算库里,表达式求值被 CRTP 完全摊平到编译期,运行时就是一串直接的算术指令,没有任何多态分派,这是它能和手写循环一样快的根本原因。

## CRTP 的典型用途

CRTP 不只用来做静态多态,它还有几个高频用法。

**静态多态**(本篇重点)。基类定义接口骨架,派生类提供具体实现,编译期绑定,无虚函数开销。适合「接口固定、实现多样、性能敏感」的场景,比如数值库的运算、容器的迭代器。

**Mixin(混入)**。基类给派生类「注入」一段通用功能,派生类只需提供基础数据。比如给所有支持 `<` 的类型自动生成全套比较运算符:

```cpp
template <typename Derived>
struct Comparable {
    friend bool operator>(const Derived& a, const Derived& b)  { return b < a; }
    friend bool operator<=(const Derived& a, const Derived& b) { return !(b < a); }
    friend bool operator>=(const Derived& a, const Derived& b) { return !(a < b); }
    friend bool operator!=(const Derived& a, const Derived& b) { return !(a == b); }
};

struct Point : Comparable<Point> {
    int x, y;
    Point(int x_, int y_) : x(x_), y(y_) {}
    friend bool operator<(const Point& a, const Point& b) { return a.x < b.x; }
    friend bool operator==(const Point& a, const Point& b) { return a.x == b.x; }
};
// Point 现在自动有了 > <= >= !=,只需实现 < 和 ==
```

跑一下,确认 mixin 注入的运算符确实能用:

```bash
$ g++ -Wall -Wextra -std=c++20 comparable.cpp -o comparable && ./comparable
p1 < p2:  true
p1 > p2:  false
p1 <= p2: true
p1 >= p2: false
p1 != p2: true
```

`Point` 只写了 `<` 和 `==`,其余四个比较运算符全由 `Comparable<Point>` 自动补齐。

只要 `Point` 实现了 `<` 和 `==`,混入 `Comparable<Point>` 就把其余比较运算符全自动补齐了。这和第三部分要讲的 Barton-Nackman 是一对搭档,CRTP 提供结构,友元注入提供运算符。

**编译期接口注入 / 策略注入**。基类可以要求派生类提供某些 typedef 或常量,在编译期检查,实现「静态接口」。比如基类里写 `using value_type = typename Derived::value_type;`,派生类没暴露 `value_type` 就编不过。这种「concept-like」的编译期约束,在 concepts 出现之前常用 CRTP 模拟。

**表达式模板**(expression templates)。Eigen 的 `a + b * c` 不产生临时矩阵,而是生成一个「表达式类型」记录运算,最后赋值时一次性求值,避免中间临时对象。这套机制完全建立在 CRTP 上,是 CRTP 最惊艳的应用,vol3 元编程会专门拆。

## CRTP 的坑

CRTP 强大,但有几个坑要避。

**基类构造时派生类未完成**。基类的构造函数执行时,派生类部分还没构造,这时如果在基类构造函数里调 `static_cast<Derived*>(this)` 去访问派生类成员,是未定义行为(对象还没成形)。同理基类析构函数里也别调派生类方法。CRTP 的跨层调用只在那次调用时对象已经完整的情况下安全,构造/析构期间不行。

**`static_cast` 的安全假设**。`static_cast<Derived*>(this)` 假设 `this` 确实指向一个 `Derived` 对象,这个假设由您(写 `struct Derived : Base<Derived>` 时)保证。好消息是类型系统其实帮您拦了一部分:如果您写歪了,比如 `struct Wrong : Base<Other>`(`Wrong` 和 `Other` 毫无关系),基类里的 `static_cast<Other*>(this)` 通常会编译失败,因为 `Wrong*` 和 `Other*` 是无关类型,`static_cast` 拒绝转换。所以 CRTP 的类型安全比看上去强,前提是您老老实实把派生类自己传给基类。

**虚函数和 CRTP 不互通**。CRTP 的 `area()` 不是虚函数,您没法把 `Circle*` 和 `Square*` 塞进同一个 `Shape*` 数组里统一调用 `area()`。需要运行时异构集合的场景,还得用虚函数。CRTP 适合「编译期就知道具体类型」的场景,这是它的边界。

## C++23 deducing this:CRTP 的现代替代

CRTP 写起来有点绕(`static_cast<Derived*>(this)`),C++23 给了一个更直观的替代,叫 **deducing this**(显式对象参数)。它让成员函数显式接收一个 `this` 参数,编译器根据调用对象的类型推导:

```cpp
// C++23 deducing this 写法:Shape 不再是模板
struct Shape {
    // self 的类型由调用对象推导,不再是硬编码的 Derived
    double area(this auto const& self) { return self.area_impl(); }
};

struct Circle : Shape {   // Circle 仍继承 Shape,但不用再把 Shape<Circle> 这样传模板参数
    double r;
    double area_impl() const { return 3.14159 * r * r; }
};
// Circle c; c.area() 里 self 推导为 Circle const&,调 Circle::area_impl
```

deducing this 让静态多态写得更像普通函数,不用 `static_cast`、不用把派生类传给自己。它是 C++23 的重要特性,本卷第三部分(元编程)和 vol2 讲现代特性时会详讲。在那之前,CRTP 仍是 C++17 及更早项目里做静态多态的标准手段,您读 Eigen、读 Boost 的代码,到处都是它。

下一篇是本卷概念部分的收尾:一个 `fixed_vector<T, N>` 综合项目。它用编译期固定大小的连续存储,把前面学的模板、非类型参数、迭代器、CRTP(可选)全串起来,实现一个零动态分配的定长容器,并和 C++23/26 的 `std::inplace_vector` 对比。
