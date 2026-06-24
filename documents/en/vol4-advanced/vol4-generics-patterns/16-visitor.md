---
title: 'Visitor Pattern: From Two if/else to Double Dispatch, Then to variant + visit'
description: Starting from the most intuitive "chaining `if/else` by type", we progressively
  derive the classic double-dispatch Visitor pattern. We examine why it is friendly
  to adding operations but hostile to adding types, and finally present a modern,
  compile-time type-safe alternative using `std::variant` + `std::visit`.
chapter: 11
order: 16
tags:
- host
- cpp-modern
- intermediate
- 访问者模式
difficulty: intermediate
platform: host
cpp_standard:
- 11
- 17
- 20
reading_time_minutes: 22
related:
- 单例模式:从注释约束到 Meyer's Singleton
- 策略模式:从一堆 if/else 到编译期可替换的 Policy
prerequisites:
- 'Chapter 6: 类与对象'
translation:
  source: documents/vol4-advanced/vol4-generics-patterns/16-visitor.md
  source_hash: 1dcdac089677d984726b4a1912841f73ca8c5d3dbda6965e6a820939c6484ce5
  translated_at: '2026-06-24T01:02:13.707357+00:00'
  engine: anthropic
  token_count: 5164
---
# Visitor Pattern: From Two `if/else` to Double Dispatch, and Then to `variant` + `visit`

## What Problem Are We Actually Solving?

Let's not rush to the class diagram just yet. Consider a very specific scenario: you have a set of shapes—circles, rectangles, and triangles—each holding its own geometric data. Now, you want to perform two completely unrelated operations on them: one is to **calculate the total area**, and the other is to **draw them on the screen**.

The most intuitive approach is to implement both operations directly within each shape class:

```cpp
struct Circle {
    double radius;
    double area() const { return std::numbers::pi * radius * radius; }
    void draw() const { /* ... */ }
};
```

It starts out clean enough. But then things spiral out of control. Product wants to export shapes to SVG, so you add a `to_svg()` to every class. A week later, they want JSON serialization, so you add `to_json()`. Later comes collision detection, debug printing, area cache invalidation... Every time you add a "cross-cutting operation," you have to **open every shape class and stuff a new member function into it**. The shape classes themselves don't care about SVG or JSON; these operations have nothing to do with what a shape *is*, yet they are physically welded inside the shape classes. The classes get bloated, responsibilities get blurred, and changing one operation requires touching a pile of files.

The root of the problem is this: **"Shape data" and "Operations on shapes" are forced into the same type**. The data is relatively stable (a Circle is just a `radius`), but operations keep expanding. What we want is the reverse—**data classes stay lean, only exposing their structure; while that pile of growing operations are separated into independent blocks, so adding a new operation doesn't require touching a single shape class**.

The Visitor pattern solves exactly this. Its core idea is simple: extract operations on a group of objects into independent "visitor" objects, while the objects themselves only handle "handing themselves over to the visitor." This way, adding a new operation just means adding a new visitor class; the shape classes don't need a single line of changes.

However, that "handing themselves over" step relies on a technical detail in C++ that is impossible to avoid—**double dispatch**. This is more convoluted than something like Singleton where you "just write a static variable and you're done." We need to walk through exactly what problem it solves and why the classic implementation is so verbose. Then, we will look at how in modern C++, when your set of types is closed, `std::variant` + `std::visit` offers a much cleaner path that checks coverage at compile time.

## Step 1: The Primitive Approach — Switching on Type (The Anti-Pattern)

Let's look at how we might subconsciously write code that "dispatches to different logic based on the shape's real type" if we aren't familiar with the Visitor pattern. Assume we only have a base class pointer `Shape*`, but it actually points to one of `Circle`, `Rectangle`, or `Triangle`:

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

It runs, but it's riddled with issues. Every time you add a new shape, you have to insert another branch into this `if/else` chain. Worse, **every time you add a new operation** (like a `perimeter_of`), you have to copy this entire chain all over again. The fatal flaw is that `dynamic_cast` is a runtime RTTI query. It has to dig through the virtual table for type information, which is slow and unsafe. If you miss an `else` or cast to the wrong type, the compiler silently accepts it.

The root cause of this dead end is: **The reference you get via a base class pointer has its "real type erased." The real type information is lost, so you can only retrieve it at runtime.** What we really want is a mechanism that, at runtime, **precisely hits a "function specialized for that type" based on the object's real type**, and this process doesn't require hand-written `if/else` chains or RTTI.

This is exactly the problem that **double dispatch** solves.

## Let's clarify the terminology: Single Dispatch vs. Double Dispatch

Let's pause and clarify the term "dispatch," as it is key to understanding the Visitor pattern.

**Single dispatch** is what you use every day with virtual functions. When you call `shape->area()`, the runtime selects the corresponding `area()` implementation based on the actual type `shape` points to. **Only one object's runtime type participates in deciding which function to call**, hence the name single dispatch.

Now, the problem arises: Suppose we have a "Visitor" object that has a different handler function for each shape (`visit(Circle&)`, `visit(Rectangle&)`, `visit(Triangle&)`). We hold a shape pointer `Shape* s` and want the visitor to process it. Can we just write `visitor.visit(*s)`?

No. Because the **static type of `*s` is `Shape&`**, and your `visit` overload set doesn't contain a `visit(Shape&)` version. The compiler fails to find a matching overload at compile time and throws an error. You hold a base class reference, but the real type is only known at runtime. However, **normal function overloading is resolved at compile time based on static types**, so it cannot see the runtime type.

Therefore, what we want is a mechanism that relies on the runtime types of **two objects** simultaneously—the shape's real type and the visitor's real type—to decide which code block to execute. This is **double dispatch**: the function selection depends on the runtime types of **two** objects.

The entire ingenuity of the Visitor pattern lies in using "two single dispatches" to compose a double dispatch. Let's see how it's done.

## Step 2: The Classic Visitor—Composing Double Dispatch with Two Single Dispatches

Let's jump straight to the code and then break it down line by line to see why it's written this way. This is the classic GoF intrusive visitor, with three shapes and one area-calculating visitor:

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

Here is how we use it:

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

Run the output (using default precision for `{}`):

```sh
$ g++ -std=c++23 -O2 -Wall area_cal.cpp -o area_cal
$ ./area_cal
Total area: 54.27433388230814
```

(3²π ≈ 28.27, 4×5 = 20, 0.5×6×2 = 6, sum ≈ 54.27, the numbers match.)

::: tip Companion Buildable Project
The classic Visitor example above (`Circle`/`Rectangle`/`Triangle` + `AreaCalculatorVisitor`) is available as a complete CMake project in this repository. Just clone it and run: [visitor](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/volumn_codes/vol4/design-patterns/Visitor). The version in the repository uses a non-const reference for `visit`, which is perfect for you to modify to a `const` version as an exercise to understand the difference between the two styles.
:::

Okay, the code looks long, but the real core is just one line—the `visitor.visit(*this)` inside each shape's `accept`. We are going to scrutinize this line, because the entire reason the pattern works lies right here.

### What does this line `visitor.visit(*this)` actually do?

Assume we hold a base class pointer `Shape* s = new Circle{3.0}`, and we call `s->accept(calculator)`. Two things happen here, and the order is critical.

**First Dispatch (Single Dispatch)**: This is a virtual function call. The static type of `s` is `Shape*`, but at runtime it points to a `Circle`, so the virtual table routes this call to `Circle::accept`, not `Shape::accept` or `Rectangle::accept`. This dispatch is based on the **actual type of the shape**. Note that we have now "entered" the world of `Circle`—even better, inside the function body of `Circle::accept`, the **static type of `this` is `Circle*`, not `Shape*`** (we will emphasize this again later, as it is the foundation of the pattern).

**Second Dispatch (Overload Resolution)**: After entering `Circle::accept`, we execute `visitor.visit(*this)`. Here, the static type of `*this` is `const Circle&`, so the compiler precisely selects the `visit(const Circle&)` version among all `visit` overloads in `ShapeVisitor`. This is another virtual function call (because `visit` is virtual), so at runtime, it routes to `AreaCalculatorVisitor::visit(const Circle&)` based on the actual type of `visitor`—which is `AreaCalculatorVisitor` here. This dispatch is based on the **actual type of the visitor**.

Linking the two together: **The first dispatch uses the shape's type to select `accept`, the second dispatch uses the static type of `*this` to select the `visit` overload, and then uses the visitor's type to select the `visit` implementation**. The runtime types of two objects participate in the decision simultaneously—this is how double dispatch is assembled using the three-stage relay of "virtual function + overload resolution + virtual function".

### Why `accept` must be overridden in every derived class

There is a pitfall here that is easy to fall into, and it is key to understanding why the pattern is verbose. You might think: Since `accept` just contains `visitor.visit(*this)`, can't I just lift it to the base class `Shape` and write it once, saving the trouble of copying it into every derived class?

No, and it absolutely won't work. Let's verify in the compiler why.

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

The problem lies with `*this`. Inside `Shape::accept`, the static type of `this` is `const Shape*`, so `*this` is a `const Shape&`. However, the `Visitor` only contains overloads like `visit(const Circle&)` and `visit(const Rectangle&)` for **specific derived classes**; there is no `visit(const Shape&)`. When the compiler performs overload resolution at compile time, it cannot find a candidate matching `visit(const Shape&)` and **reports an error directly**.

Let's extract this mechanism and verify it in the compiler to see how the static type of `*this` determines the second dispatch:

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

Build and Run:

```sh
$ g++ -std=c++23 -O2 -Wall double_dispatch.cpp -o double_dispatch
$ ./double_dispatch
visit(DerivedA&)
visit(DerivedB&)
```

The output hits the mark precisely. You see, precisely because `accept` is overridden in `DerivedA`, `*this` captures the exact static type `DerivedA`, enabling the second dispatch to work. **`accept` must be overridden individually in every concrete derived class, not for polymorphism, but to "correct the static type of `*this`."** This is the root cause of the Visitor pattern's verbosity—it is not a stylistic choice, but a hard requirement of the mechanism.

### The Extensibility Ledger of the Classic Visitor

Once we understand this mechanism, we can clearly calculate its extensibility trade-offs.

**Adding a new operation** (e.g., adding a "Draw to Screen" visitor): You simply add a new `DrawVisitor`, inherit from `ShapeVisitor`, and implement three `visit` overloads. **You don't need to modify a single line of the shape classes.** This is the Visitor pattern's biggest selling point—it makes "extending operations" open.

**Adding a new shape** (e.g., adding a `Hexagon`): You must modify the `ShapeVisitor` interface to add a `visit(const Hexagon&)`. Then, **every existing visitor class** must go back and implement a `visit(const Hexagon&)`, otherwise, because it is a pure virtual function, that visitor becomes an abstract class and cannot be instantiated. This change triggers a ripple effect across the entire codebase. Therefore, the classic Visitor pattern is **hostile** towards "adding types."

This trade-off is particularly important because it directly determines whether you should use the Visitor pattern: **If your set of types is stable (there are only so many shapes), but operations are constantly expanding (calculate area today, serialize tomorrow, collision detection the day after), the Visitor pattern is your friend; if the set of types itself is constantly expanding, the classic Visitor is your nightmare.**

::: warning Don't treat the Visitor Pattern as a Universal Hammer
The Visitor pattern has a hard prerequisite—**your set of element types must be relatively stable**. If you are working on a system where new types are continuously added (plugin systems, dynamically loaded modules, third-party extension points), the classic Visitor requires modifying the interface and all visitors every time a type is added, causing maintenance costs to explode. In such scenarios, you don't need the classic Visitor, but rather the "Non-intrusive + RTTI Dispatch" discussed later, or simply a rethinking of the architecture. Ask the question "Will the types expand?" clearly before deciding to use the Visitor; if you get this step wrong, everything that follows is a pitfall.
:::

## Step 3: Using `std::variant` + `std::visit` to Change Course

At this point, you might be thinking: The classic Visitor involves forward declarations, overriding `accept` in every class, and two virtual function calls. It's so tedious to write—isn't there an easier way?

There is, and the path provided by modern C++ is significantly cleaner. The prerequisite is—**your set of types is closed**, meaning all possible shape types can be fully listed at compile time. If your scenario meets this prerequisite (most "sets of shapes," "sets of AST nodes," or "sets of events" are indeed closed), then `std::variant` + `std::visit` is a solution that offers compile-time type safety, requires no inheritance hierarchy, and is non-intrusive to the element classes.

Let's look directly at what it looks like:

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

Run it:

```sh
$ g++ -std=c++23 -O2 -Wall variant_visit.cpp -o variant_visit
$ ./variant_visit
Total area: 54.2743
```

The numbers are exactly the same as the classic version. Let's go through them one by one to see why they are good and what the trade-offs are.

### How it dispatches

Internally, besides storing the actual data, `std::variant` also stores a **discriminator** — an integer that records which type it currently holds (accessible via `.index()`). The job of `std::visit(visitor, variant)` is to: **read the discriminator and then invoke the branch in the visitor that matches the current type**. This process involves **absolutely no virtual function calls**; it performs a comparison and jump based on the discriminator.

Just talking about it isn't enough; let's look directly at what the compiler compiles `std::visit` into. In the function below, the visitor has three branches for three types (calling external functions `g_a`, `g_b`, and `g_c` to prevent them from being completely inlined away):

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

Let's look at the assembly for `f` using `g++ -O2 -S`. The core logic boils down to just these few lines (GCC 16.1):

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

Look closely—the entire dispatch is just "read one byte, do two comparisons, and jump." There is no virtual table lookup and no indirect memory access. `std::visit` hard-codes the "discriminator → which branch to call" mapping into a chain of comparison-and-jump instructions at compile time (if there are many types, the compiler might use a jump table, but the essence is direct indexed dispatch, bypassing virtual functions). **This is the source of its performance advantage over the classic visitor.**

### Its Ace in the Hole: Compile-Time Exhaustiveness Check

The classic visitor has a hidden pitfall: if you add a new shape, `Hexagon`, and forget to implement `visit(const Hexagon&)` in a specific visitor, the compilation won't necessarily fail immediately. Since `visit(const Hexagon&)` is a pure virtual function, the visitor simply becomes an abstract class. The error message often reads "cannot instantiate abstract class," requiring you to dig through several layers to realize, "Oh, I forgot to write a visit method."

`std::visit` is much stricter in this regard: **it forces you to cover every type in the variant at compile time; miss one, and compilation fails immediately.** Let's try writing only branches for `A` and `B`, intentionally omitting `C`:

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

```text
Compilation, error (g++ 16.1, excerpt of key lines):
```

```sh
$ g++ -std=c++23 -O2 visit_missing.cpp -o visit_missing
variant:1145: error: no type named 'type' in
  'struct std::invoke_result<Overloaded<...>, C&>'
```

The compiler explicitly tells you: for the type `C&`, your visitor has no callable implementation. **The coverage check is performed at compile time, so it is absolutely impossible to slip through to runtime**. This is a substantial safety improvement of the `variant` approach over the classic visitor pattern—when adding a type, the compiler lists every location that needs updating in one go, eliminating the need for manual cross-referencing.

### How to implement a "default branch": generic lambda

Sometimes you don't want to write a specific overload for every single type; most types can simply share the same fallback logic. `std::visit`, combined with a **generic lambda** (`[](const auto&)`), allows you to implement a default branch while still ensuring the code compiles:

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

Verify that it compiles successfully, and that all shapes other than `Circle` fall back to the default branch:

```sh
$ g++ -std=c++23 -O2 -Wall visit_default.cpp -o visit_default && ./visit_default
Circle area=28.2743
(some other shape)
(some other shape)
```

Since the `operator()` of a generic lambda is a template, it deduces arguments for any matching type. This effectively makes it the "default handler" within the `variant`. This approach allows us to enjoy compile-time exhaustive checks (ensuring at least one branch in the variant matches each type), while also allowing us to add specialized branches for specific types as needed.

## Let's Verify First: Is `variant` + `visit` Really Faster?

Talk is cheap. Let's write a comparison: we pre-generate the same batch of five million shapes and sum their areas using the classic virtual function visitor pattern versus `variant` + `visit`. We measure only the dispatch overhead and use a `volatile sink` to prevent the compiler from optimizing the entire block away. The compiler is GCC 16.1, and both tests use `-O2`:

```cpp
// 经典版:AreaVirt 继承 ShapeVisitor,visit 都是 virtual;
//         主循环 for (auto& s : vs) s->accept(av);
// variant 版:用 Overloaded{} + std::visit,主循环里累加返回值。
// 两个形状集合 (vs / vts) 用同一个 mt19937 种子生成,内容完全对应。
```

I ran this on my machine (5,000,000 accesses per item; numbers will vary on your machine; this is actual output from GCC 16.1.1 on WSL2):

```sh
$ g++ -std=c++23 -O2 visit_bench.cpp -o visit_bench && ./visit_bench
$ ./visit_bench   # 跑两遍看抖动
virtual:   total=3.26927e+07  33.1 ms
variant:   total=3.26927e+07  22.5 ms
virtual:   total=3.26927e+07  33.3 ms
variant:   total=3.26927e+07  22.6 ms
```

Under `-O2`, the `variant` version is already consistently about 30% faster (22 ms vs 33 ms). Let's bump the optimization level up to `-O3` and run it twice:

```sh
$ g++ -std=c++23 -O3 visit_bench.cpp -o visit_bench_o3 && ./visit_bench_o3
$ ./visit_bench_o3
virtual:   total=3.26927e+07  35.6 ms
variant:   total=3.26927e+07  22.6 ms
virtual:   total=3.26927e+07  34.1 ms
variant:   total=3.26927e+07  21.7 ms
```

Under `-O3`, the performance gap does not widen further; the `variant` version remains around 22 ms, while the virtual function version stays at 34–35 ms. The reason is straightforward: the `variant` version's dispatch (reading a discriminator byte, performing one or two comparisons, and jumping) never relied on a vtable. Furthermore, the lambdas inside `Overloaded` can be fully inlined into the call site—computation and dispatch are flattened together, with no indirect calls blocking the way. In contrast, regardless of the optimization level, the virtual dispatch across the heterogeneous container (`vector<unique_ptr<Shape>>`) in the classic version blocks inlining. The compiler struggles to flatten the combined `accept` + `visit` logic.

Therefore, the statement "variant has no vtable overhead" must be understood precisely: **it means the dispatch mechanism itself does not rely on a vtable (direct discriminator comparison), and the visitor's entire logic can be inlined.** However, note that this is not an unconditional victory. This benchmark is characterized by "very short visitor logic that can be fully inlined," which hits the `variant` sweet spot. If your visitor is heavy, or if the set of shapes is so large that the `variant`'s discriminator jump chain degrades (the compiler might switch to a jump table for many types, but it remains direct indexed dispatch, not virtual functions), the gap will narrow. Also, don't underestimate compiler devirtualization: in scenarios involving `final` types or where the compiler can prove a pointer points to a fixed type, the virtual function version can also be flattened. **Don't treat "variant is always faster" as a silver bullet; its speed premise is "dispatch can be inlined." Whether you can get the compiler to swallow that inlining is the decisive factor.**

## The Cost of the `variant` Approach: Closed Type Set

Having discussed the benefits of `variant`, we must clearly state its cost—**its set of types must be fully determined at compile time**. Once you write `using Shape = std::variant<Circle, Rectangle, Triangle>;`, `Shape` can only ever hold these three types. If you want to add a `Hexagon`, you must modify this line and recompile; every place that uses `Shape` must be recompiled as well. It does not support "registering a new type dynamically at runtime."

This means: **if your system is plugin-based and types can be added dynamically at runtime** (for example, an AST supporting third-party extensions, or a script engine's value types), the `variant` path won't work. You must revert to the classic visitor, or use a heavier "non-intrusive + RTTI" solution (`dynamic_cast` dispatch, or `std::any` + a type registry).

Interestingly, the classic visitor doesn't truly support "adding types at runtime" either—adding a type requires modifying interfaces and recompiling. Strictly speaking, the classic visitor and `variant` are "six of one, half a dozen of the other" regarding the "closed type set" requirement. The difference is that `variant` encloses this closure in the type system (strong compile-time checks), while the classic visitor encloses it in the `Visitor` interface's method list (also strong compile-time checks, but more verbose code). True "runtime dynamic type extension" is impossible with either visitor pattern; that is the domain of type erasure (`std::any`, `std::function`) + registries.

## Which One to Choose: A Decision Table

We now have two approaches with clear scopes. Let's compare them side by side:

| Dimension | Classic Visitor (Intrusive Double Dispatch) | `std::variant` + `std::visit` |
|---|---|---|
| Type Set Closure Requirement | Must be closed (compile time) | Must be closed (compile time) |
| Cost of Adding New Operations | **Low**: Add a new visitor class | Medium: Modify visit call sites (add a lambda branch) |
| Cost of Adding New Types | **High**: Modify interface + all visitors | Medium: Modify variant type list + compiler forces you to complete all visits |
| Exhaustiveness Check Strictness | Pure virtual functions error, but info is indirect | **Hard compile-time check**, direct errors |
| Intrusiveness | **High**: Element classes must implement `accept` | **Zero**: Element classes are plain structs |
| Dispatch Mechanism | Two virtual function calls | Discriminator comparison, inlineable |
| Best For | Existing inheritance hierarchies, third-party interface constraints, operations >> types | Closed data unions, value semantics desired, zero overhead desired |

How to choose? I'll boil the decision logic down to a few sentences. **If you already have an inheritance hierarchy where element classes are established and cannot or should not be modified** (e.g., adding operations to types in a third-party library, or the base class is designed for external inheritance), and the number of operations will far exceed the number of types, use the classic visitor. In that context, its intrusiveness isn't a drawback; it's the only way to attach operations. **If you are designing a closed set of data types from scratch** (most typically AST nodes, event types, or configuration items), without inheritance baggage, wanting value semantics, compile-time exhaustiveness checks, and zero virtual function overhead, then decisively use `std::variant` + `std::visit`. In modern C++, this is the lighter, safer default.

## Pitfall Warning: Details of Classic Patterns

::: warning Classic Visitor `visit` Should Prefer `const&`
In the classic visitor, if you write the `visit` parameter as `visit(Circle& c)` (non-const reference), it implies the visitor intends to **modify** the shape. However, for a read-only visitor like `AreaCalculatorVisitor`, the correct form is `visit(const Circle& c)`. This isn't just about const-correctness pedantry—it directly dictates the signature of your `Shape::accept`. If `visit` takes `const Circle&`, then `accept` must also be a `const` member function (`virtual void accept(ShapeVisitor&) const`). Otherwise, you cannot call `accept` from a `const Shape&`. In the companion Playground project, `AreaCalculatorVisitor` uses a non-const reference; strictly speaking, for the semantic of "calculating area without modification," the `const` version is correct. If this `const` chain is wrong in the base class, every derived class follows suit, making fixes annoying. So, decide upfront: "is this visitor read-only or modifying?"
:::

::: tip Custom Discriminator Access for `variant` + `visit`
Besides `std::visit`, you will often use `v.index()` (get the index of the currently held type), `std::holds_alternative<T>(v)` (ask "is it currently T?"), and `std::get_if<T>(&v)` (safely get a pointer to T, returning nullptr if it's not T). These tools don't throw exceptions and are suitable when you don't want to write a full visitor and just need a simple type check. However, as long as you need to do something substantive for each type, prioritize `std::visit`. It enforces compile-time exhaustiveness, whereas `if (holds_alternative<A>) ... else if ...` regresses into the anti-pattern we started with.
:::

::: warning The `Overloaded` Helper Requires C++17 Deduction Guides
The `Overloaded` utility that combines multiple lambdas into an overload set relies on C++17's **Class Template Argument Deduction (CTAD) guide**: `template <class... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;`. Without this line, the compiler doesn't know how to deduce `Overloaded{[](A&){}, [](B&){}}` into `Overloaded<lambda1, lambda2>`. Additionally, `using Ts::operator()...;` uses a variadic `using` declaration to pull each base class's `operator()` into the derived class for overload resolution, which is also a C++17 feature. So, the minimum requirement for this pattern is **C++17**. In C++20, you can write it more compactly, but the core mechanism remains unchanged.
:::

## Summary

Let's review the entire evolution path:

| Stage | Approach | Why It Falls Short |
|---|---|---|
| `if/else` + `dynamic_cast` | Chain of type checks | RTTI is slow, prone to missing cases, adding types/ops requires modifying this blob |
| Classic Visitor | `accept` + `visit`, virtual functions + overload resolution for double dispatch | Friendly for adding operations, but adding types requires changing interfaces and all visitors; highly intrusive |
| `std::variant` + `std::visit` | Pack closed types into a variant, visit performs compile-time dispatch | Type set must be closed at compile time, no runtime extension |

Keep these key conclusions in mind:

- The Visitor pattern solves the problem of **avoiding welding operations into data classes when operations span a set of types and constantly grow**. Its extensibility ledger is: **open for adding operations, closed for adding types**.
- The classic visitor's double dispatch is a three-stage relay: **virtual function selects `accept` → static type of `*this` selects `visit` overload → virtual function selects `visit` implementation**. `accept` must be overridden in every derived class to correct the static type of `*this`; this is a technical necessity, not a stylistic choice.
- Prioritize `std::variant` + `std::visit` in modern C++: it offers compile-time exhaustiveness checks (missing a type causes a compilation failure), zero virtual function overhead (dispatch is discriminator comparison and inlineable), and is non-intrusive (element classes are plain structs). The cost is that the type set must be closed at compile time.
- The classic visitor and `variant` are actually "six of one, half a dozen of the other" regarding the "closed type set"; true runtime dynamic type extension is impossible with both, requiring type erasure + registries.
- "Variant is always faster" must be understood precisely: its advantage lies in a dispatch mechanism independent of vtables and the ability to inline the visitor logic. Benchmarks (`-O2`/`-O3`, 5 million visits) show the `variant` version is stably about 30% faster than the virtual function version; however, this is measured in the "short visitor, inlineable" sweet spot. If the visitor is heavy or types are numerous, the gap will narrow.

## References

- [cppreference: `std::variant`](https://en.cppreference.com/w/cpp/utility/variant) (Since C++17, discriminated union type)
- [cppreference: `std::visit`](https://en.cppreference.com/w/cpp/utility/variant/visit) (Since C++17, discriminator-based visitor dispatch)
- [cppreference: `std::numbers::pi`](https://en.cppreference.com/w/cpp/numeric/constants) (Since C++20, replaces non-standard `M_PI`)
- [cppreference: Virtual functions](https://en.cppreference.com/w/cpp/language/virtual) (Virtual functions and single dispatch mechanism)
- GoF, *Design Patterns: Elements of Reusable Object-Oriented Software* — Original definition of the Visitor pattern
- Andrei Alexandrescu, *Modern C++ Design* Chapter 10 — Double dispatch implementations for variants like Acyclic Visitor
- Companion compilable project: [visitor](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/volumn_codes/vol4/design-patterns/Visitor)
