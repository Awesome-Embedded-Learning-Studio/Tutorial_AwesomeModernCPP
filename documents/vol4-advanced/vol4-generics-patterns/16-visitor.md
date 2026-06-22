---
title: "访问者模式:从两个 if/else 到双分发,再到 variant + visit"
description: "从最直觉的「按 type 串 if/else」起步,一步步逼出经典的双分发 Visitor,看清它为什么对加操作友好、对加类型敌视,最后用 std::variant + std::visit 给出一个编译期类型安全的现代替代"
chapter: 11
order: 16
tags:
  - host
  - cpp-modern
  - intermediate
  - 访问者模式
difficulty: intermediate
platform: host
cpp_standard: [11, 17, 20]
reading_time_minutes: 22
related:
  - "单例模式:从注释约束到 Meyer's Singleton"
  - "策略模式:从一堆 if/else 到编译期可替换的 Policy"
prerequisites:
  - "Chapter 6: 类与对象"
---

# 访问者模式:从两个 if/else 到双分发,再到 variant + visit

## 我们到底在解决什么问题

我们先不急着上类图。想一个特别具体的场景:你手里有一组形状——圆、矩形、三角形——它们各自装着自己的几何数据。现在你想对它们做两件完全不相干的事:一件是**算总面积**,另一件是**把它们画到屏幕上**。

最直觉的做法,是把这两件事都写进每个形状类里:

```cpp
struct Circle {
    double radius;
    double area() const { return std::numbers::pi * radius * radius; }
    void draw() const { /* ... */ }
};
```

刚开始挺干净的。可接下来事情就开始失控了。产品说要把形状导出成 SVG,你给每个类加一个 `to_svg()`。又过了一周要支持 JSON 序列化,又加一个 `to_json()`。再后来要做碰撞检测、要做打印调试、要做面积缓存失效……每加一种「横跨所有形状的操作」,你就得**打开每一个形状类、往里塞一个新成员函数**。形状类本身其实根本不关心 SVG,不关心 JSON,这些操作和「形状是什么」毫无关系,却被物理地焊死在了形状类里。类越来越胖,职责越来越糊,改一个操作要碰一堆文件。

这件事的根子在于:**「形状的数据」和「作用在形状上的操作」被强行塞进了同一个类型**。数据是相对稳定的(圆就那一个 `radius`),操作却会不断膨胀。我们想要的是反过来——**数据类保持精简,只暴露自己的结构;而那一堆不断增长的操作,各自独立成块,加一个新操作时不动任何一个形状类**。

访问者模式要解决的就是这件事。它的核心想法一句话:把作用在一组对象上的操作抽出来,变成独立的「访问者」对象,对象本身只负责「把自己交给访问者」。这样每加一个新操作,就是新增一个访问者类,形状类一行都不用改。

但「把自己交给访问者」这一步,在 C++ 里有个绕不开的技术细节——它依赖一种叫**双分发(double dispatch)**的机制。这件事比单例那种「写个 static 就完事」要绕得多,我们得一步步看清楚它到底在解决一个什么问题,以及为什么经典实现写起来那么啰嗦。然后我们再看,在现代 C++ 里,当你的类型集合是闭合的时候,`std::variant` + `std::visit` 给了一条干净得多、编译期就能检查覆盖的路。

## 第一步:最原始的写法——串 type 判断(反面教材)

我们先看看,如果不懂访问者模式,下意识会怎么写一个「按形状的真实类型分发到不同处理逻辑」的代码。假设我们手里只有一个基类指针 `Shape*`,但下面真正指向的是 `Circle`、`Rectangle` 或 `Triangle` 之一:

```cpp
struct Shape {
    virtual ~Shape() = default;
};

struct Circle : Shape { double radius; };
struct Rectangle : Shape { double width, height; };
struct Triangle : Shape { double base, height; };

double area_of(const Shape* s) {
    if (dynamic_cast<const Circle*>(s)) {
        return std::numbers::pi * std::pow(dynamic_cast<const Circle*>(s)->radius, 2);
    } else if (dynamic_cast<const Rectangle*>(s)) {
        auto* r = dynamic_cast<const Rectangle*>(s);
        return r->width * r->height;
    } else if (dynamic_cast<const Triangle*>(s)) {
        auto* t = dynamic_cast<const Triangle*>(s);
        return 0.5 * t->base * t->height;
    }
    return 0.0;
}
```

能跑,但毛病一大堆。每加一种新形状,你就要在这个 `if/else` 链里再插一个分支,而且**每加一个新操作**(比如再来个 `perimeter_of`),你就得把这条链再抄一遍。更要命的是 `dynamic_cast` 是运行时 RTTI 查询,它要在虚表里翻类型信息,慢且不安全——你写漏一个 `else`、cast 到错误类型,编译器一声不吭全收下。

这条路走不通的根子在于:**你用基类指针拿到的只是一个「被擦除了真实类型的」引用,真实类型信息丢了,只能靠运行时去捞回来**。我们真正想要的是一种机制,能在运行时**根据对象的真实类型,精确地命中一个「针对该类型特化」的函数**,而且这个命中过程不需要你手写 `if/else`,也不需要 RTTI。

这正是双分发要解决的问题。

## 先把术语理清:单分发 vs 双分发

我们停一下,把「分发」这个词说清楚,因为这是理解整个访问者模式的关键。

**单分发(single dispatch)**,就是你天天在用的虚函数。你调 `shape->area()`,运行时根据 `shape` 真正指向的类型,选择对应的 `area()` 实现。**只有一个对象的运行时类型参与决定调用哪个函数**,所以叫单分发。

现在问题来了:假设我有一个「访问者」对象,它针对每种形状写了一个不同的处理函数(`visit(Circle&)`、`visit(Rectangle&)`、`visit(Triangle&)`);我手里有一个形状指针 `Shape* s`,我想让访问者去处理它。我能不能直接写 `visitor.visit(*s)`?

不能。因为 `*s` 的**静态类型是 `Shape&`**,而你的 `visit` 重载里没有 `visit(Shape&)` 这个版本,编译器在编译期就找不到能匹配的重载,直接报错。你拿到的是基类引用,真实类型在运行时才知道,但**普通函数重载是按静态类型在编译期决议的**,运行时的真实类型它根本看不见。

所以我们想要的是一种机制,它要**同时依赖两个对象的运行时类型**——一个是形状的真实类型,一个是访问者的真实类型——来决定到底执行哪段代码。这就是**双分发(double dispatch)**:函数的选择,取决于**两个**对象的运行时类型。

访问者模式的全部精巧之处,就在于它用「两次单分发」拼出了一个双分发。我们来看怎么拼。

## 第二步:经典访问者——两次单分发拼出双分发

我们直接上代码,然后逐行拆它为什么这么写。这是 GoF 经典的侵入式访问者,三个形状 + 一个算面积的访问者:

```cpp
#pragma once
#include <numbers>

struct Circle;
struct Rectangle;
struct Triangle;

// 访问者接口:每新增一个具体形状,这里就要加一个 visit 重载
struct ShapeVisitor {
    virtual ~ShapeVisitor() = default;
    virtual void visit(const Circle& c) = 0;
    virtual void visit(const Rectangle& r) = 0;
    virtual void visit(const Triangle& t) = 0;
};

// 元素接口:accept 把"自己"交给访问者
struct Shape {
    virtual ~Shape() = default;
    virtual void accept(ShapeVisitor& visitor) const = 0;
};

struct Circle : Shape {
    double radius;
    explicit Circle(double r) : radius(r) {}
    void accept(ShapeVisitor& visitor) const override {
        visitor.visit(*this);
    }
};

struct Rectangle : Shape {
    double width, height;
    Rectangle(double w, double h) : width(w), height(h) {}
    void accept(ShapeVisitor& visitor) const override {
        visitor.visit(*this);
    }
};

struct Triangle : Shape {
    double base, height;
    Triangle(double b, double h) : base(b), height(h) {}
    void accept(ShapeVisitor& visitor) const override {
        visitor.visit(*this);
    }
};

// 一个具体的访问者:累计总面积
struct AreaCalculatorVisitor : ShapeVisitor {
    double total_area = 0.0;

    void visit(const Circle& c) override {
        total_area += std::numbers::pi * c.radius * c.radius;
    }
    void visit(const Rectangle& r) override {
        total_area += r.width * r.height;
    }
    void visit(const Triangle& t) override {
        total_area += 0.5 * t.base * t.height;
    }
};
```

用起来是这样的:

```cpp
#include <memory>
#include <print>
#include <vector>

int main() {
    std::vector<std::unique_ptr<Shape>> shapes;
    shapes.emplace_back(std::make_unique<Circle>(3.0));
    shapes.emplace_back(std::make_unique<Rectangle>(4.0, 5.0));
    shapes.emplace_back(std::make_unique<Triangle>(6.0, 2.0));

    AreaCalculatorVisitor calculator;
    for (auto& shape : shapes) {
        shape->accept(calculator);
    }
    std::println("Total area: {}", calculator.total_area);
}
```

跑出来(`{}` 用默认精度打印):

```sh
$ g++ -std=c++23 -O2 -Wall area_cal.cpp -o area_cal
$ ./area_cal
Total area: 54.27433388230814
```

(3²π ≈ 28.27,4×5 = 20,0.5×6×2 = 6,加起来 ≈ 54.27,数对上了。)

::: tip 配套可编译工程
上面这套经典访问者(`Circle`/`Rectangle`/`Triangle` + `AreaCalculatorVisitor`)在本仓库里有完整的 CMake 工程,clone 下来一把就能跑:[visitor](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/volumn_codes/vol4/design-patterns/Visitor)。仓库里的版本用的是非 const 引用的 `visit`,适合你顺手改成 `const` 版练手体会两种写法的差异。
:::

好,代码看着挺长,但真正的核心就一行——每个形状的 `accept` 里那句 `visitor.visit(*this)`。我们现在来死磕这一行,因为整个模式为什么 work,全在这一行里。

### 这一行 `visitor.visit(*this)` 在干什么

假设你拿着一个基类指针 `Shape* s = new Circle{3.0}`,然后调 `s->accept(calculator)`。这里发生了两件事,顺序非常关键。

**第一次分发(单分发)**:这是一次虚函数调用。`s` 的静态类型是 `Shape*`,但运行时它指向的是 `Circle`,所以虚表把这次调用路由到了 `Circle::accept`,而不是 `Shape::accept` 或 `Rectangle::accept`。这一次分发,根据的是**形状的真实类型**。注意,我们现在已经「进入」了 `Circle` 的世界——更妙的是,在 `Circle::accept` 的函数体里,`this` 的**静态类型是 `Circle*`,不是 `Shape*`**(这一点等下要再强调,它是模式成立的地基)。

**第二次分发(重载决议)**:进入 `Circle::accept` 之后,执行 `visitor.visit(*this)`。这里 `*this` 的静态类型是 `const Circle&`,于是编译器在 `ShapeVisitor` 的所有 `visit` 重载里,精确地选中了 `visit(const Circle&)` 这个版本。而这又是一次虚函数调用(因为 `visit` 是 virtual),运行时再根据 `visitor` 的真实类型——这里是 `AreaCalculatorVisitor`——路由到 `AreaCalculatorVisitor::visit(const Circle&)`。这一次分发,根据的是**访问者的真实类型**。

把两次串起来:**第一次分发靠形状的类型选 `accept`,第二次分发靠 `*this` 的静态类型选 `visit` 重载,再靠访问者的类型选 `visit` 的实现**。两个对象的运行时类型同时参与了决定——双分发就这么用「虚函数 + 重载决议 + 虚函数」三段接力拼出来了。

### 为什么 `accept` 必须在每个派生类里各自 override

这里有个特别容易踩的坑,也是理解模式为什么啰嗦的关键。你会想:既然 `accept` 里都是写 `visitor.visit(*this)`,那我把它提到基类 `Shape` 里写一次不就行了,省得每个派生类抄一遍?

不行,而且是一定不行。我们在编译器里验证一下为什么。

```cpp
struct Visitor;

struct Shape {
    virtual ~Shape() = default;
    virtual void accept(Visitor& v) const {
        // 假设我们想在基类里只写一次:
        v.visit(*this);   // 编不过:*this 的静态类型是 const Shape&
    }
};
```

问题就在 `*this` 上。在 `Shape::accept` 里,`this` 的静态类型是 `const Shape*`,于是 `*this` 是 `const Shape&`。而 `Visitor` 里只有 `visit(const Circle&)`、`visit(const Rectangle&)` 这些针对**具体派生类**的重载,根本没有 `visit(const Shape&)`。编译器在编译期做重载决议,找不到能匹配 `visit(const Shape&)` 的候选,**直接报错**。

我们在编译器里把这个机制单独拎出来验证,看清楚 `*this` 的静态类型如何决定第二次分发:

```cpp
#include <iostream>

struct Visitor;

struct Base {
    virtual ~Base() = default;
    virtual void accept(Visitor& v) const = 0;
};

struct DerivedA : Base { void accept(Visitor& v) const override; };
struct DerivedB : Base { void accept(Visitor& v) const override; };

struct Visitor {
    void visit(const DerivedA&) { std::cout << "visit(DerivedA&)\n"; }
    void visit(const DerivedB&) { std::cout << "visit(DerivedB&)\n"; }
    // 故意不提供 visit(const Base&) —— 也没有
};

// 关键:在 DerivedA::accept 里,*this 的静态类型是 DerivedA,
// 于是 v.visit(*this) 精确命中 visit(const DerivedA&)
void DerivedA::accept(Visitor& v) const { v.visit(*this); }
void DerivedB::accept(Visitor& v) const { v.visit(*this); }

int main() {
    Visitor v;
    const Base* a = new DerivedA;
    const Base* b = new DerivedB;
    a->accept(v);   // 第一次分发→DerivedA::accept;第二次分发→visit(const DerivedA&)
    b->accept(v);   // 第一次分发→DerivedB::accept;第二次分发→visit(const DerivedB&)
    delete a;
    delete b;
}
```

编译运行:

```sh
$ g++ -std=c++23 -O2 -Wall double_dispatch.cpp -o double_dispatch
$ ./double_dispatch
visit(DerivedA&)
visit(DerivedB&)
```

输出精准命中。你看,正是因为 `accept` 在 `DerivedA` 里被 override 了,`*this` 才能拿到精确的静态类型 `DerivedA`,第二次分发才分得动。**`accept` 必须在每个具体派生类里各自 override,不是为了多态,而是为了「修正 `*this` 的静态类型」**。这是访问者模式结构上啰嗦的根源——它不是风格选择,是机制硬要求。

### 经典访问者的扩展性账本

把这套机制想明白之后,我们能很清楚地算出它的扩展性账本了。

**加一个新操作**(比如再来个「画到屏幕」的访问者):你只要新增一个 `DrawVisitor`,继承 `ShapeVisitor`,实现三个 `visit` 重载就行。**形状类一行都不用改**。这是访问者模式最大的卖点——它把「操作的扩展」做成了开放式的。

**加一个新形状**(比如再来个 `Hexagon`):你得改 `ShapeVisitor` 接口,加一个 `visit(const Hexagon&)`;然后**每一个已有的访问者类**都要回去补一个 `visit(const Hexagon&)` 的实现,否则因为是纯虚函数、那个访问者就变成抽象类没法实例化。这一改是牵一发动全身的。所以访问者模式对「加类型」是**敌视**的。

这条账本特别重要,它直接决定了你该不该用访问者模式:**如果你的类型集合是稳定的(形状就那么几种),而操作在不断膨胀(今天算面积,明天序列化,后天碰撞检测),访问者是你的朋友;如果你的类型集合本身在不断扩展,经典访问者就是你的噩梦**。

::: warning 别把访问者模式当万能锤
访问者模式有一个硬性的适用前提——**你的元素类型集合要相对稳定**。如果你正在做的系统里,新类型会持续不断地被加进来(插件系统、动态加载的模块、第三方扩展点),经典访问者每一次加类型都要回头改接口和所有访问者,维护成本会爆炸。这种场景你需要的不是经典访问者,而是后面要讲的「非侵入式 + RTTI 分发」或者干脆重新想架构。先把「类型会不会扩展」这个问题问清楚,再决定要不要上访问者,这一步判断错了后面全是坑。
:::

## 第三步:用 `std::variant` + `std::visit` 换一条路

讲到这里你可能会想:经典访问者又是前向声明、又是每个类 override `accept`、又是两次虚函数调用,写得这么累,有没有更省事的办法?

有,而且现代 C++ 给的路子明显更干净。前提是——**你的类型集合是闭合的**,也就是说,所有可能的形状类型,在编译期就能列得全。如果你的场景满足这个前提(大多数「一组形状」「一组 AST 节点」「一组事件」其实都是闭合的),那 `std::variant` + `std::visit` 是一个编译期类型安全、不需要继承体系、不需要侵入元素类的方案。

我们直接看它长什么样:

```cpp
#include <iostream>
#include <numbers>
#include <variant>
#include <vector>

struct Circle { double radius; };
struct Rectangle { double width, height; };
struct Triangle { double base, height; };

// 关键一步:把"一组闭合的类型"打包成一个 variant
using Shape = std::variant<Circle, Rectangle, Triangle>;

// 一个 helper:把多个 lambda 捏成一个重载组(C++17 经典写法)
template <class... Ts> struct Overloaded : Ts... { using Ts::operator()...; };
template <class... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

int main() {
    std::vector<Shape> shapes;
    shapes.emplace_back(Circle{3.0});
    shapes.emplace_back(Rectangle{4.0, 5.0});
    shapes.emplace_back(Triangle{6.0, 2.0});

    double total = 0.0;
    for (auto& s : shapes) {
        total += std::visit(Overloaded{
            [](const Circle& c) -> double {
                return std::numbers::pi * c.radius * c.radius;
            },
            [](const Rectangle& r) -> double { return r.width * r.height; },
            [](const Triangle& t) -> double { return 0.5 * t.base * t.height; }
        }, s);
    }
    std::cout << "Total area: " << total << "\n";
}
```

跑一下:

```sh
$ g++ -std=c++23 -O2 -Wall variant_visit.cpp -o variant_visit
$ ./variant_visit
Total area: 54.2743
```

数跟经典版一模一样。我们来逐条看它好在哪,以及代价是什么。

### 它是怎么分发的

`std::variant` 内部除了存实际数据,还存着一个「判别式(discriminator)」——一个整数,记录当前持有的是第几个类型(`.index()` 能查到)。`std::visit(visitor, variant)` 的职责就是:**读出判别式,然后据此调用 visitor 里匹配当前类型的那一支**。这件事**没有任何虚函数调用**,它是对判别式做一次比较/跳转。

我们光说没用,直接看编译器把 `std::visit` 编译成了什么。下面这个函数,visitor 有针对三个类型的三支(调用外部函数 `g_a/g_b/g_c` 防止被完全内联消失):

```cpp
#include <variant>
struct A { double x; };
struct B { double x, y; };
struct C { double x, y, z; };
using V = std::variant<A, B, C>;
double g_a(double), g_b(double,double), g_c(double,double,double);

double f(const V& v) {
    return std::visit([](auto&& s) -> double {
        if constexpr (std::is_same_v<std::decay_t<decltype(s)>, A>) return g_a(s.x);
        else if constexpr (std::is_same_v<std::decay_t<decltype(s)>, B>) return g_b(s.x, s.y);
        else return g_c(s.x, s.y, s.z);
    }, v);
}
```

用 `g++ -O2 -S` 看 `f` 的汇编,核心就这么几行(GCC 16.1):

```sh
$ g++ -std=c++23 -O2 -S visit_dispatch.cpp -o - | sed -n '/^_Z1fRK/,/ret/p'
_Z1fRKSt7variantIJ1A1B1CEE:
    movzbl  24(%rdi), %eax      # 读出 variant 的判别式(存在 offset 24)
    movsd   (%rdi), %xmm0       # 顺手把数据也读出来
    cmpb    $1, %al
    je      .L2                 # 判别式==1 → 走 B 这支
    cmpb    $2, %al
    jne     .L5                 # 判别式==2 → 走 C 这支
    movsd   16(%rdi), %xmm2
    movsd   8(%rdi), %xmm1
    jmp     _Z3g_cddd@PLT       # → g_c
.L5:
    jmp     _Z3g_ad@PLT         # 否则(判别式==0)→ 走 A 这支 → g_a
.L2:
    movsd   8(%rdi), %xmm1
    jmp     _Z3g_bddd@PLT       # → g_b
    ret
```

你看清楚了——整个分发就是「读一个字节、做两次比较、跳转」,没有任何虚表查找、没有任何间接内存访问。`std::visit` 在编译期就把「判别式 → 该调用哪一支」这个映射固化成了一段比较跳转链(类型多的时候编译器可能用跳转表,但本质都是直接索引分发,不走虚函数)。**这就是它相对于经典访问者的性能优势所在**。

### 它最大的杀手锏:编译期穷举检查

经典访问者有个暗坑:你新加一个形状 `Hexagon`,如果忘了在某个访问者里实现 `visit(const Hexagon&)`,编译其实不会立刻挂——因为 `visit(const Hexagon&)` 是纯虚函数,这个访问者会变成抽象类,报错信息往往是「不能实例化抽象类」,要绕几层才能定位到「哦,是我忘写某个 visit 了」。

`std::visit` 在这件事上严格得多:**它会强制你在编译期覆盖 variant 里的每一个类型,漏一个直接编译失败**。我们故意只写 `A` 和 `B` 两支、漏掉 `C` 试试:

```cpp
#include <variant>
#include <iostream>
struct A {}; struct B {}; struct C {};
using V = std::variant<A, B, C>;
template <class... Ts> struct Overloaded : Ts... { using Ts::operator()...; };
template <class... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

int main() {
    V v = A{};
    std::visit(Overloaded{
        [](const A&) { std::cout << "A\n"; },
        [](const B&) { std::cout << "B\n"; }
        // 故意漏掉 C
    }, v);
}
```

编译,报错(g++ 16.1,节选关键行):

```sh
$ g++ -std=c++23 -O2 visit_missing.cpp -o visit_missing
variant:1145: error: no type named 'type' in
  'struct std::invoke_result<Overloaded<...>, C&>'
```

编译器明确告诉你:对 `C&` 这个类型,你的 visitor 没有能调用的实现。**覆盖检查是编译期完成的,绝不可能漏到运行时**。这是 variant 方案相对经典访问者一个特别实在的安全性提升——加类型时,所有该补的地方编译器一次性给你列全,不靠人脑去对账。

### 想要「默认分支」怎么办:泛型 lambda

有时候你并不想为每一种类型都专门写一支,大多数类型走同一个兜底逻辑就行。`std::visit` 配合一个**泛型 lambda**(`[](const auto&)`)就能做到默认分支,而且依然编译通过:

```cpp
for (auto& s : shapes) {
    std::visit(Overloaded{
        [](const Circle& c) {
            std::cout << "Circle area=" << std::numbers::pi * c.radius * c.radius << "\n";
        },
        [](const auto&) {   // 泛型 lambda:兜底匹配其余所有类型
            std::cout << "(some other shape)\n";
        }
    }, s);
}
```

验证它能编译通过,并且非 `Circle` 的形状都走了兜底分支:

```sh
$ g++ -std=c++23 -O2 -Wall visit_default.cpp -o visit_default && ./visit_default
Circle area=28.2743
(some other shape)
(some other shape)
```

泛型 lambda 的 `operator()` 是个模板,对任何能匹配的类型都能推导,于是它就成了 variant 里的「默认处理」。这样你既能享受编译期穷举检查(只要 variant 里至少有一支能匹配每个类型),又能按需给个别类型开特化分支。

## 这里先验证一下:variant + visit 真的更快吗

口说无凭,我们写个对比:预生成同一批 500 万个形状,分别用经典虚函数访问者和 variant + visit 累加面积,只测分发开销,用 `volatile sink` 防止整段被优化掉。编译器是 GCC 16.1,两种都开 `-O2`:

```cpp
// 经典版:AreaVirt 继承 ShapeVisitor,visit 都是 virtual;
//         主循环 for (auto& s : vs) s->accept(av);
// variant 版:用 Overloaded{} + std::visit,主循环里累加返回值。
// 两个形状集合 (vs / vts) 用同一个 mt19937 种子生成,内容完全对应。
```

我在自己机器上跑(每项 5,000,000 次访问,数字以你机器为准,这里是 GCC 16.1.1 + WSL2 的真实输出):

```sh
$ g++ -std=c++23 -O2 visit_bench.cpp -o visit_bench && ./visit_bench
$ ./visit_bench   # 跑两遍看抖动
virtual:   total=3.26927e+07  33.1 ms
variant:   total=3.26927e+07  22.5 ms
virtual:   total=3.26927e+07  33.3 ms
variant:   total=3.26927e+07  22.6 ms
```

`-O2` 下 variant 版就已经稳定快了约三成(22 ms vs 33 ms)。把优化级别提到 `-O3` 再跑两遍:

```sh
$ g++ -std=c++23 -O3 visit_bench.cpp -o visit_bench_o3 && ./visit_bench_o3
$ ./visit_bench_o3
virtual:   total=3.26927e+07  35.6 ms
variant:   total=3.26927e+07  22.6 ms
virtual:   total=3.26927e+07  34.1 ms
variant:   total=3.26927e+07  21.7 ms
```

`-O3` 下差距没有进一步拉开,variant 版依旧是 22 ms 上下,虚函数版还是 34-35 ms。原因很直接:variant 版的分发(读一个判别式字节、做一两次比较、跳转)从一开始就不依赖虚表,而且 `Overloaded` 里的 lambda 能被整段内联进调用点——计算和分发摊平在一起,没有间接调用挡路;而经典版不管优化级别怎么开,跨异构容器(`vector<unique_ptr<Shape>>`)的那次虚分发挡住了内联,编译器很难把 `accept`+`visit` 整段摊平。

所以「variant 无虚表开销」这句话要精确地理解:**它指的是分发机制本身不依赖虚表(判别式直接比较),而且 visitor 的整段逻辑可被内联**。但请注意,这不是无条件碾压——这个 benchmark 的特点是「访问者逻辑很短、全部能内联」,正好踩在 variant 的甜点区。如果你的 visitor 本身很重、或者形状集合体量大到 `variant` 的判别式跳转链退化(类型特别多时编译器可能改用跳转表,但仍是直接索引分发,不走虚函数),差距会缩小。另外别小看编译器的去虚化(devirtualization):在某些 `final` 类型、或能证明指针指向固定类型的场景下,虚函数版本也能被打平。**别把「variant 一定更快」当银弹,它快的前提是「分发能被内联」,你能不能让编译器吃下这个内联,才是决定性的。**

## variant 方案的代价:类型集合必须闭合

说了这么多 variant 的好,它的代价也得讲清楚——**它的类型集合在编译期就必须完全确定**。`using Shape = std::variant<Circle, Rectangle, Triangle>;` 这一行写下去,`Shape` 能装的就永远只有这三种,你想加个 `Hexagon`,就得改这一行、重新编译,所有用到 `Shape` 的地方都得跟着重新编一遍。它不支持「运行时动态注册一个新类型」。

这意味着:**如果你的系统是插件式的、类型会在运行时被动态加进来**(比如一个支持第三方扩展的 AST、一个脚本引擎的值类型),variant 这条路就走不通,你得回到经典访问者,或者用更重的「非侵入式 + RTTI」方案(`dynamic_cast` 分发、或者 `std::any` + 类型注册表)。

有趣的是,经典访问者其实也并不真正支持「运行时动态加类型」——加类型同样要改接口、重新编译。所以严格说,经典访问者和 variant 在「类型集合必须闭合」这件事上是半斤八两,差别只在于 variant 把闭合性写在了类型系统里(编译期强检查),而经典访问者把闭合性写在了 `Visitor` 接口的方法列表里(同样是编译期强检查,但代码更啰嗦)。真正的「运行时动态类型扩展」,两种访问者都给不了,那是类型擦除(`std::any`、`std::function`)+ 注册表那一套要解决的问题了。

## 选哪个:一张决策表

到这里我们有两个方案,各自的适用面很清楚。我们把它们摆在一起比:

| 维度 | 经典访问者(侵入式双分发) | `std::variant` + `std::visit` |
|---|---|---|
| 类型集合闭合性要求 | 必须闭合(编译期) | 必须闭合(编译期) |
| 加新操作的成本 | **低**:新增一个访问者类即可 | 中:改 visit 调用点(加一支 lambda) |
| 加新类型的成本 | **高**:改接口 + 所有访问者 | 中:改 variant 类型列表 + 编译器强制你补全所有 visit |
| 覆盖检查严格度 | 纯虚函数会报错,但信息绕 | **编译期硬检查**,报错直接 |
| 侵入性 | **强**:元素类必须实现 `accept` | **零**:元素类是普通 struct |
| 分发机制 | 两次虚函数调用 | 判别式比较,可内联 |
| 适合 | 已有继承体系、第三方接口约束、操作远多于类型 | 闭合数据联合、想要值语义、追求零开销 |

怎么选?我把判断逻辑浓缩成几句话。**如果你已经有了一套继承体系,元素类是既定的、无法或不该改动**(比如你在给一个第三方库的类型加操作,或者基类本来就是设计给别人继承的),而且操作的数量会远多于类型的数量,那就用经典访问者——它的侵入性在那个语境下不是缺点,反而是它能把操作挂上去的唯一办法。**如果你是从零开始设计一组闭合的数据类型**(最典型的就是 AST 节点、事件类型、配置项),没有继承包袱,想要值语义、想要编译期穷举检查、想要零虚函数开销,那就果断用 `std::variant` + `std::visit`,在现代 C++ 里这是更轻、更安全的默认选择。

## 踩坑预警:几个经典写法的细节坑

::: warning 经典访问者的 visit 最好走 const&
经典访问者里 `visit` 的参数,如果你写的是 `visit(Circle& c)`(非 const 引用),那意味着这个访问者打算**修改**形状;但像 `AreaCalculatorVisitor` 这种只读访问者,正确写法是 `visit(const Circle& c)`。这不仅是 const 正确性(const-correctness)的洁癖——它直接决定了你的 `Shape::accept` 必须签名匹配:如果 `visit` 收 `const Circle&`,那 `accept` 也得是 `const` 成员函数(`virtual void accept(ShapeVisitor&) const`),否则你从一个 `const Shape&` 上根本调不到 `accept`。配套的 Playground 工程里 `AreaCalculatorVisitor` 用的是非 const 引用,严格说对于「只算面积不改动」的语义,const 版才对。这种 const 链一旦在基类写错,后面所有派生类都得跟着歪,改起来很烦,所以一开始就把「这个访问者是只读还是要改」想清楚。
:::

::: tip 给 variant + visit 配自定义判别式访问
除了 `std::visit`,日常你还会用到 `v.index()`(拿到当前持有类型的下标)、`std::holds_alternative<T>(v)`(问「现在是不是 T」)、`std::get_if<T>(&v)`(安全地拿到指向 T 的指针,不是 T 就返回 nullptr)这几个配套工具。它们都不抛异常,适合在你不方便写 visitor、只想简单判断一下类型时用。但只要你需要对每个类型都做点正经事,优先用 `std::visit`,因为它是编译期强制穷举的,而 `if (holds_alternative<A>) ... else if ...` 又退化成了我们开头那个手写分发的反面教材。
:::

::: warning Overloaded helper 需要 C++17 的推导指引
那个把多个 lambda 捏成重载组的 `Overloaded` 小工具,依赖的是 C++17 的**类模板参数推导指引(CTAD deduction guide)**:`template <class... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;`。没有这一行,你写 `Overloaded{[](A&){}, [](B&){}}` 编译器不知道要推导成 `Overloaded<lambda1, lambda2>`。另外 `using Ts::operator()...;` 这一行用变长 using 声明把每个基类的 `operator()` 都引到派生类里参与重载决议,这也是 C++17 才有的特性。所以这套写法的最低门槛是 **C++17**;到了 C++20 你可以写得更紧凑一点,但核心机制没变。
:::

## 小结

我们把整条演进路径捋一遍:

| 阶段 | 做法 | 为什么还不够 |
|---|---|---|
| `if/else` + `dynamic_cast` | 按 type 串一长串判断 | RTTI 慢、易写漏、加类型/操作都要改这一坨 |
| 经典访问者 | `accept` + `visit`,虚函数 + 重载决议拼双分发 | 加操作友好,但加类型要改接口和所有访问者,且侵入性强 |
| `std::variant` + `std::visit` | 把闭合类型打包成 variant,visit 做编译期分发 | 类型集合必须编译期闭合,不支持运行时扩展 |

记下这几条关键结论:

- 访问者模式解决的是「**操作横跨一组类型、且操作不断增长**」时,避免把操作焊死在数据类里的问题。它的扩展性账本是:**对加操作开放,对加类型封闭**。
- 经典访问者的双分发,是「**虚函数选 accept → `*this` 的静态类型选 visit 重载 → 虚函数选 visit 实现**」三段接力。`accept` 必须在每个派生类里 override,是因为要修正 `*this` 的静态类型,不是风格问题。
- 现代优先考虑 `std::variant` + `std::visit`:它是编译期穷举检查(漏一个类型直接编译失败)、零虚函数开销(分发是判别式比较,可内联)、非侵入(元素类是普通 struct)。代价是类型集合必须在编译期闭合。
- 经典访问者和 variant 在「类型集合必须闭合」上其实是半斤八两;真正的运行时动态类型扩展,两者都给不了,那要靠类型擦除 + 注册表。
- 「variant 一定更快」要精确理解:它的优势在分发机制不依赖虚表、且 visitor 逻辑可被整段内联。实测(`-O2`/`-O3`,500 万次访问)variant 版稳定比虚函数版快约三成;但这是在「访问者很短、能被内联」的甜点区测出来的,visitor 一重或类型一多,差距会缩小。

## 参考资源

- [cppreference:`std::variant`](https://en.cppreference.com/w/cpp/utility/variant)(C++17 起,判别式联合类型)
- [cppreference:`std::visit`](https://en.cppreference.com/w/cpp/utility/variant/visit)(C++17 起,基于判别式的访问者分发)
- [cppreference:`std::numbers::pi`](https://en.cppreference.com/w/cpp/numeric/constants)(C++20 起,替代非标准的 `M_PI`)
- [cppreference:Virtual functions](https://en.cppreference.com/w/cpp/language/virtual)(虚函数与单分发机制)
- GoF,《Design Patterns: Elements of Reusable Object-Oriented Software》—— 访问者模式原始定义
- Andréi Alexandrescu,《Modern C++ Design》第 10 章 —— Acyclic Visitor 等变种的双分发实现
- 配套可编译工程:[visitor](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/volumn_codes/vol4/design-patterns/Visitor)
