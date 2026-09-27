---
title: 'Visitor Pattern: From Two if/else to Double Dispatch, and Then to `variant` + `visit`'
description: 'Starting from the most intuitive "chain if/else on type" approach, we squeeze out the classic double-dispatch Visitor step by step, see clearly why it is friendly to adding operations yet hostile to adding types, and finally arrive at a compile-time type-safe modern alternative with `std::variant` + `std::visit`'
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
  - 'Singleton Pattern: From Comment-Only Constraints to Meyer''s Singleton'
  - 'Strategy Pattern: From a Heap of if/else to Compile-Time Swappable Policies'
prerequisites:
  - Classes and Object-Oriented Programming
translation:
  source: documents/vol4-advanced/vol4-generics-patterns/16-visitor.md
  source_hash: 1dcdac089677d984726b4a1912841f73ca8c5d3dbda6965e6a820939c6484ce5
  translated_at: '2026-09-26T05:43:09+00:00'
  engine: anthropic
  token_count: 7700
---

# Visitor Pattern: From Two if/else to Double Dispatch, and Then to `variant` + `visit`

## What problem are we actually solving

Let's hold off on the class diagram for a moment. Picture a very concrete scenario: you have a set of shapes — circle, rectangle, triangle — each carrying its own geometric data. Now you want to do two completely unrelated things to them: one is **compute the total area**, the other is **draw them to the screen**.

The most intuitive approach is to write both tasks into every shape class:

```cpp
struct Circle {
    double radius;
    double area() const { return std::numbers::pi * radius * radius; }
    void draw() const { /* ... */ }
};
```

At first it's quite clean. Then things start to spiral. Product says shapes need to be exported as SVG, so you add a `to_svg()` to every class. A week later JSON serialization support is required, so in goes a `to_json()`. Later still: collision detection, debug printing, area-cache invalidation... every time you add an "operation that cuts across all shapes", you have to **open up every single shape class and stuff a new member function into it**. The shape classes themselves don't care about SVG at all, don't care about JSON — these operations have nothing to do with "what a shape is", yet they are physically welded into the shape classes. The classes grow fatter, the responsibilities grow blurrier, and changing one operation means touching a pile of files.

The root of the problem: **"the shape's data" and "the operations acting on the shape" are forcibly crammed into the same type**. The data is relatively stable (a circle is just that one `radius`), while the operations keep ballooning. What we want is the reverse — **the data classes stay lean and only expose their structure, while that ever-growing pile of operations each becomes an independent block, so that adding a new operation touches not a single shape class**.

That is exactly the problem the Visitor pattern solves. Its core idea in one sentence: pull the operations acting on a group of objects out into independent "visitor" objects, and let the objects themselves be responsible only for "handing themselves to the visitor". Then every new operation is just a new visitor class, and not one line of any shape class changes.

But that step — "handing yourself to the visitor" — has a technical wrinkle in C++ that cannot be dodged: it relies on a mechanism called **double dispatch**. This is far more roundabout than the Singleton's "write a static and call it a day"; we have to take it apart step by step to see clearly what problem it actually solves and why the classic implementation is so verbose to write. Then we'll look at how, in modern C++, when your set of types is closed, `std::variant` + `std::visit` offers a much cleaner path whose coverage is checked at compile time.

## Step 1: The most primitive approach — a chain of type tests (an anti-example)

Let's first see what you would write by reflex, not knowing the Visitor pattern, for "dispatch to different handling logic based on the shape's actual type". Suppose all we have is a base-class pointer `Shape*`, but what it actually points to below is one of `Circle`, `Rectangle`, or `Triangle`:

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

It runs, but it's riddled with problems. Every new shape means inserting another branch into that `if/else` chain — and **every new operation** (say, a `perimeter_of` next) means copying the whole chain over again. Worse, `dynamic_cast` is a runtime RTTI query that digs through the vtable for type information — slow and unsafe: miss an `else`, cast to the wrong type, and the compiler silently swallows it all.

The root reason this road is a dead end: **all a base-class pointer gets you is a reference whose actual type has been erased — the real type information is lost, and you can only fish it back at runtime**. What we actually want is a mechanism that, at runtime, **lands precisely on a "function specialized for that type" based on the object's actual type**, where the landing process requires no hand-written `if/else` from you and no RTTI.

That is exactly the problem double dispatch solves.

## First, let's get the terms straight: single dispatch vs double dispatch

Let's pause for a moment and pin down the word "dispatch", because it is the key to understanding the entire Visitor pattern.

**Single dispatch** is what you already use every day: virtual functions. You call `shape->area()`, and at runtime, based on the type `shape` actually points to, the corresponding `area()` implementation is chosen. **Only one object's runtime type participates in deciding which function gets called** — hence the name single dispatch.

Now the question arises: suppose I have a "visitor" object with a different handling function written for each shape (`visit(Circle&)`, `visit(Rectangle&)`, `visit(Triangle&)`); I'm holding a shape pointer `Shape* s`, and I want the visitor to handle it. Can I just write `visitor.visit(*s)`?

No. Because the **static type of `*s` is `Shape&`**, and your `visit` overloads contain no `visit(Shape&)` version — at compile time the compiler finds no matching overload and errors out on the spot. What you hold is a base-class reference; the actual type is only known at runtime, but **ordinary function overloading is resolved at compile time on the static type** — the runtime actual type is invisible to it.

So what we want is a mechanism that depends on **the runtime types of two objects at once** — one being the shape's actual type, the other the visitor's actual type — to decide which piece of code executes. That is **double dispatch**: the choice of function depends on the runtime types of **two** objects.

The entire ingenuity of the Visitor pattern lies in assembling a double dispatch out of two single dispatches. Let's see how the pieces fit.

## Step 2: The classic Visitor — assembling double dispatch out of two single dispatches

Let's jump straight to the code, then take it apart line by line to see why it's written this way. This is the classic GoF intrusive Visitor: three shapes plus one area-computing visitor:

```cpp
#pragma once
#include <numbers>

struct Circle;
struct Rectangle;
struct Triangle;

// Visitor interface: every new concrete shape means adding a visit overload here
struct ShapeVisitor {
    virtual ~ShapeVisitor() = default;
    virtual void visit(const Circle& c) = 0;
    virtual void visit(const Rectangle& r) = 0;
    virtual void visit(const Triangle& t) = 0;
};

// Element interface: accept hands "yourself" over to the visitor
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

// A concrete visitor: accumulates the total area
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

Using it looks like this:

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

Run it (`{}` prints at default precision):

```sh
$ g++ -std=c++23 -O2 -Wall area_cal.cpp -o area_cal
$ ./area_cal
Total area: 54.27433388230814
```

(3²π ≈ 28.27, 4×5 = 20, 0.5×6×2 = 6, which adds up to ≈ 54.27 — the numbers check out.)

::: tip The companion compilable project
This classic Visitor setup (`Circle`/`Rectangle`/`Triangle` + `AreaCalculatorVisitor`) has a complete CMake project in this repository — clone it and it runs in one go: [visitor](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/volumn_codes/vol4/design-patterns/Visitor). The version in the repo uses non-const-reference `visit`s, perfect for you to convert to the `const` version yourself as an exercise in feeling out the difference between the two spellings.
:::

All right, the code looks long, but the real core is a single line — the `visitor.visit(*this)` inside each shape's `accept`. We're going to chew on that one line until it gives, because why the whole pattern works is contained entirely in it.

### What that one line, `visitor.visit(*this)`, is doing

Suppose you're holding a base-class pointer `Shape* s = new Circle{3.0}`, and you call `s->accept(calculator)`. Two things happen here, and the order is critical.

**First dispatch (single dispatch)**: this is a virtual function call. The static type of `s` is `Shape*`, but at runtime it points to a `Circle`, so the vtable routes this call to `Circle::accept`, not `Shape::accept` or `Rectangle::accept`. This dispatch is decided by **the shape's actual type**. Note that we have now "entered" the world of `Circle` — and the delightful part is that inside `Circle::accept`'s function body, the **static type of `this` is `Circle*`, not `Shape*`** (we'll stress this again in a moment; it is the bedrock the whole pattern stands on).

**Second dispatch (overload resolution)**: once inside `Circle::accept`, `visitor.visit(*this)` executes. The static type of `*this` here is `const Circle&`, so among all of `ShapeVisitor`'s `visit` overloads, the compiler selects precisely the `visit(const Circle&)` version. And this in turn is a virtual function call (because `visit` is virtual), so at runtime it routes — by the visitor's actual type, here `AreaCalculatorVisitor` — to `AreaCalculatorVisitor::visit(const Circle&)`. This dispatch is decided by **the visitor's actual type**.

String the two together: **the first dispatch uses the shape's type to pick `accept`; the second dispatch uses the static type of `*this` to pick the `visit` overload, and then the visitor's type to pick the `visit` implementation**. The runtime types of two objects participate in the decision simultaneously — and that is how double dispatch gets assembled from a "virtual function + overload resolution + virtual function" three-leg relay.

### Why `accept` must be overridden separately in each derived class

There's a trap here that is remarkably easy to step on, and it's also the key to understanding why the pattern is verbose. You think: since every `accept` body just says `visitor.visit(*this)`, why not hoist it into the base class `Shape` and write it once, instead of copying it into every derived class?

No — and absolutely not. Let's verify in the compiler why.

```cpp
struct Visitor;

struct Shape {
    virtual ~Shape() = default;
    virtual void accept(Visitor& v) const {
        // Suppose we want to write it just once in the base class:
        v.visit(*this);   // Won't compile: *this's static type is const Shape&
    }
};
```

The problem is `*this` itself. Inside `Shape::accept`, the static type of `this` is `const Shape*`, so `*this` is a `const Shape&`. But `Visitor` only has overloads targeting **the concrete derived classes** — `visit(const Circle&)`, `visit(const Rectangle&)` and so on — there is no `visit(const Shape&)` at all. The compiler does overload resolution at compile time, finds no candidate that can match `visit(const Shape&)`, and **errors out directly**.

Let's pull this mechanism out on its own and verify it in the compiler, to see clearly how the static type of `*this` decides the second dispatch:

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
    // Deliberately not providing visit(const Base&) — and there isn't one
};

// Key point: inside DerivedA::accept, *this's static type is DerivedA,
// so v.visit(*this) precisely hits visit(const DerivedA&)
void DerivedA::accept(Visitor& v) const { v.visit(*this); }
void DerivedB::accept(Visitor& v) const { v.visit(*this); }

int main() {
    Visitor v;
    const Base* a = new DerivedA;
    const Base* b = new DerivedB;
    a->accept(v);   // first dispatch → DerivedA::accept; second dispatch → visit(const DerivedA&)
    b->accept(v);   // first dispatch → DerivedB::accept; second dispatch → visit(const DerivedB&)
    delete a;
    delete b;
}
```

Compile and run:

```sh
$ g++ -std=c++23 -O2 -Wall double_dispatch.cpp -o double_dispatch
$ ./double_dispatch
visit(DerivedA&)
visit(DerivedB&)
```

The output hits precisely. You see, it is precisely because `accept` is overridden inside `DerivedA` that `*this` can carry the exact static type `DerivedA`, and only then can the second dispatch dispatch at all. **`accept` must be overridden separately in every concrete derived class — not for polymorphism's sake, but to "correct the static type of `*this`"**. This is the structural root of the classic Visitor's verbosity — it is not a style choice, it is a hard requirement of the mechanism.

### The classic Visitor's extensibility ledger

With this mechanism thought through, we can tally its extensibility ledger very clearly.

**Adding a new operation** (say, another visitor that "draws to the screen"): you just add a new `DrawVisitor`, inherit from `ShapeVisitor`, and implement the three `visit` overloads. **Not one line of any shape class changes**. That is the Visitor pattern's biggest selling point — it makes "extending operations" open-ended.

**Adding a new shape** (say, a `Hexagon`): you must modify the `ShapeVisitor` interface, adding a `visit(const Hexagon&)`; then **every existing visitor class** has to go back and supply its `visit(const Hexagon&)` implementation — otherwise, since those are pure virtual functions, that visitor becomes an abstract class that can't be instantiated. One change drags the whole body along with it. So the Visitor pattern is **hostile** to "adding types".

This ledger matters enormously, because it directly decides whether you should use the Visitor pattern at all: **if your set of types is stable (the shapes are just those few) while the operations keep ballooning (area today, serialization tomorrow, collision detection the day after), the Visitor is your friend; if your set of types itself keeps extending, the classic Visitor is your nightmare**.

::: warning Don't treat the Visitor pattern as a universal hammer
The Visitor pattern has one hard precondition for applicability — **your set of element types must be relatively stable**. If the system you're building keeps taking in new types (plugin systems, dynamically loaded modules, third-party extension points), the classic Visitor forces you to go back and modify the interface and every visitor on each added type, and the maintenance cost explodes. What that scenario needs is not the classic Visitor, but either the "non-intrusive + RTTI dispatch" approach discussed later, or simply rethinking the architecture. Get the question "will the type set extend" answered clearly first, then decide whether to bring in a Visitor; get this one judgment wrong and every step after it is a pit.
:::

## Step 3: Taking a different road with `std::variant` + `std::visit`

By this point you may be thinking: the classic Visitor needs forward declarations, an `accept` override in every class, two virtual function calls — so tiresome to write. Isn't there an easier way?

There is, and modern C++'s road is noticeably cleaner. The precondition — **your set of types is closed**, meaning all possible shape types can be enumerated in full at compile time. If your scenario meets this precondition (most "a set of shapes", "a set of AST nodes", "a set of events" are in fact closed), then `std::variant` + `std::visit` is a solution that is compile-time type-safe, needs no inheritance hierarchy, and intrudes on no element class.

Let's just look at what it looks like:

```cpp
#include <iostream>
#include <numbers>
#include <variant>
#include <vector>

struct Circle { double radius; };
struct Rectangle { double width, height; };
struct Triangle { double base, height; };

// The key step: pack "a closed set of types" into a variant
using Shape = std::variant<Circle, Rectangle, Triangle>;

// A helper: kneads several lambdas into one overload set (the classic C++17 spelling)
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

The numbers are identical to the classic version. Let's go through its advantages point by point — and its price.

### How it dispatches

Besides storing the actual data, a `std::variant` internally stores a "discriminator" — an integer recording which of the types is currently held (`.index()` reads it out). `std::visit(visitor, variant)`'s one job is: **read the discriminator, and based on it invoke the branch of the visitor that matches the current type**. This involves **no virtual function call whatsoever** — it is one comparison/jump on the discriminator.

Talk alone is useless; let's look at what the compiler turns `std::visit` into. In the function below, the visitor has one branch for each of the three types (it calls the external functions `g_a/g_b/g_c` to keep it from being entirely inlined away):

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

Viewing `f`'s assembly with `g++ -O2 -S`, the core is just these few lines (GCC 16.1):

```sh
$ g++ -std=c++23 -O2 -S visit_dispatch.cpp -o - | sed -n '/^_Z1fRK/,/ret/p'
_Z1fRKSt7variantIJ1A1B1CEE:
    movzbl  24(%rdi), %eax      # read the variant's discriminator (stored at offset 24)
    movsd   (%rdi), %xmm0       # pick up the data while we're at it
    cmpb    $1, %al
    je      .L2                 # discriminator==1 → take the B branch
    cmpb    $2, %al
    jne     .L5                 # discriminator==2 → take the C branch
    movsd   16(%rdi), %xmm2
    movsd   8(%rdi), %xmm1
    jmp     _Z3g_cddd@PLT       # → g_c
.L5:
    jmp     _Z3g_ad@PLT         # otherwise (discriminator==0) → the A branch → g_a
.L2:
    movsd   8(%rdi), %xmm1
    jmp     _Z3g_bddd@PLT       # → g_b
    ret
```

See it clearly now — the entire dispatch is "read one byte, do two comparisons, jump": no vtable lookup, no indirect memory access. At compile time, `std::visit` has already frozen the "discriminator → which branch to call" mapping into a chain of compares and jumps (with many types the compiler may use a jump table, but either way it is direct indexed dispatch, no virtual functions). **That is where its performance advantage over the classic Visitor lies**.

### Its biggest killer feature: compile-time exhaustiveness checking

The classic Visitor has a hidden pit: you add a new shape `Hexagon`, forget to implement `visit(const Hexagon&)` in one of the visitors, and the build doesn't fail immediately in a helpful way — because `visit(const Hexagon&)` is a pure virtual function, that visitor becomes an abstract class, and the error message is usually "cannot instantiate abstract class"; it takes several hops to track back to "ah, I forgot to write one of the visits".

`std::visit` is far stricter on this point: **it forces you to cover every type in the variant at compile time; miss one and the build fails outright**. Let's deliberately write only the `A` and `B` branches and leave out `C`:

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
        // Deliberately leaving out C
    }, v);
}
```

Compile — error (g++ 16.1, key lines excerpted):

```sh
$ g++ -std=c++23 -O2 visit_missing.cpp -o visit_missing
variant:1145: error: no type named 'type' in
  'struct std::invoke_result<Overloaded<...>, C&>'
```

The compiler tells you outright: for the type `C&`, your visitor has no callable implementation. **The coverage check is done at compile time and can never leak into runtime**. This is a very tangible safety upgrade of the variant approach over the classic Visitor — when you add a type, the compiler lists every place you need to fill in, all at once, instead of relying on human brains to reconcile the books.

### What if you want a "default branch": generic lambdas

Sometimes you don't want to write a dedicated branch for every type — most types can just go through the same fallback logic. `std::visit` combined with a **generic lambda** (`[](const auto&)`) can give you a default branch, and it still compiles:

```cpp
for (auto& s : shapes) {
    std::visit(Overloaded{
        [](const Circle& c) {
            std::cout << "Circle area=" << std::numbers::pi * c.radius * c.radius << "\n";
        },
        [](const auto&) {   // generic lambda: the fallback matching all remaining types
            std::cout << "(some other shape)\n";
        }
    }, s);
}
```

Verify that it compiles, and that every non-`Circle` shape took the fallback branch:

```sh
$ g++ -std=c++23 -O2 -Wall visit_default.cpp -o visit_default && ./visit_default
Circle area=28.2743
(some other shape)
(some other shape)
```

A generic lambda's `operator()` is a template that can deduce any type able to match, so it becomes the "default handler" inside the variant. This way you keep the compile-time exhaustiveness check (as long as at least one branch in the variant can match every type), while still opening specialized branches for individual types as needed.

## Let's verify first: is variant + visit really faster

Talk is cheap, so let's write a comparison: pre-generate the same batch of 5 million shapes, accumulate their areas once with the classic virtual-function visitor and once with variant + visit, measure only the dispatch overhead, and use a `volatile sink` so the whole stretch can't be optimized away. The compiler is GCC 16.1, both at `-O2`:

```cpp
// Classic version: AreaVirt inherits ShapeVisitor, all visits are virtual;
//                  main loop: for (auto& s : vs) s->accept(av);
// variant version: uses Overloaded{} + std::visit, accumulating return values in the main loop.
// The two shape collections (vs / vts) are generated with the same mt19937 seed; their contents correspond exactly.
```

I ran it on my own machine (5,000,000 visits per entry; your numbers will vary — this is real output from GCC 16.1.1 + WSL2):

```sh
$ g++ -std=c++23 -O2 visit_bench.cpp -o visit_bench && ./visit_bench
$ ./visit_bench   # run twice to see the jitter
virtual:   total=3.26927e+07  33.1 ms
variant:   total=3.26927e+07  22.5 ms
virtual:   total=3.26927e+07  33.3 ms
variant:   total=3.26927e+07  22.6 ms
```

At `-O2` the variant version is already consistently about 30% faster (22 ms vs 33 ms). Bump the optimization level to `-O3` and run twice more:

```sh
$ g++ -std=c++23 -O3 visit_bench.cpp -o visit_bench_o3 && ./visit_bench_o3
$ ./visit_bench_o3
virtual:   total=3.26927e+07  35.6 ms
variant:   total=3.26927e+07  22.6 ms
virtual:   total=3.26927e+07  34.1 ms
variant:   total=3.26927e+07  21.7 ms
```

At `-O3` the gap doesn't widen further: the variant version still sits around 22 ms, the virtual-function version still at 34-35 ms. The reason is very direct: the variant version's dispatch (read one discriminator byte, do one or two comparisons, jump) never depended on a vtable to begin with, and the lambdas inside `Overloaded` can be inlined wholesale into the call site — computation and dispatch flatten into one, with no indirect call blocking the way; in the classic version, no matter how the optimization level is set, that one virtual dispatch across a heterogeneous container (`vector<unique_ptr<Shape>>`) blocks inlining, and the compiler can hardly flatten the whole `accept`+`visit` stretch.

So read "variant has no vtable overhead" precisely: **it means the dispatch mechanism itself does not depend on a vtable (direct comparison of the discriminator), and that the visitor's whole logic can be inlined**. But note — this is not an unconditional crushing victory. This benchmark's character is "visitor logic is short and fully inlinable", which lands squarely in variant's sweet spot. If your visitor is itself heavy, or the shape set is large enough that the variant's discriminator jump chain degenerates (with very many types the compiler may switch to a jump table — still direct indexed dispatch, still no virtual functions), the gap narrows. Also, don't underestimate the compiler's devirtualization: with certain `final` types, or in contexts where it can prove the pointer targets a fixed type, the virtual-function version can be flattened too. **Don't take "variant is always faster" as a silver bullet — the precondition for its speed is "the dispatch can be inlined", and whether you can get the compiler to swallow that inlining is what's decisive.**

## The variant approach's price: the type set must be closed

After all this praise for variant, its price must be made clear too — **its type set must be fully determined at compile time**. Once you write `using Shape = std::variant<Circle, Rectangle, Triangle>;`, what `Shape` can hold is forever only those three; if you want to add a `Hexagon`, you must edit that line and recompile, and every place that uses `Shape` must be rebuilt along with it. It does not support "dynamically registering a new type at runtime".

This means: **if your system is plugin-based, with types added dynamically at runtime** (say, an AST supporting third-party extensions, or a scripting engine's value types), the variant road is closed to you — you have to go back to the classic Visitor, or use the heavier "non-intrusive + RTTI" schemes (`dynamic_cast` dispatch, or `std::any` + a type registry).

The amusing part is that the classic Visitor doesn't truly support "adding types dynamically at runtime" either — adding a type likewise means modifying the interface and recompiling. So strictly speaking, the classic Visitor and variant are six of one and half a dozen of the other on "the type set must be closed"; the only difference is that variant writes closedness into the type system (compile-time hard check), while the classic Visitor writes closedness into the `Visitor` interface's method list (also a compile-time hard check, just with more verbose code). True "runtime dynamic type extension" is beyond both visitors — that is the problem type erasure (`std::any`, `std::function`) + a registry exists to solve.

## Which one to pick: a decision table

At this point we have two approaches, each with a clear domain of applicability. Let's put them side by side:

| Dimension | Classic Visitor (intrusive double dispatch) | `std::variant` + `std::visit` |
|---|---|---|
| Closed type-set requirement | Must be closed (compile time) | Must be closed (compile time) |
| Cost of adding a new operation | **Low**: just add one new visitor class | Medium: modify the visit call site (add a lambda branch) |
| Cost of adding a new type | **High**: modify the interface + all visitors | Medium: modify the variant's type list + the compiler forces you to complete all visits |
| Strictness of coverage checking | Pure virtual functions do error, but the message is roundabout | **Compile-time hard check**, errors are direct |
| Intrusiveness | **Strong**: element classes must implement `accept` | **Zero**: element classes are plain structs |
| Dispatch mechanism | Two virtual function calls | Discriminator comparison, inlinable |
| Fits | An existing inheritance hierarchy, third-party interface constraints, operations far outnumbering types | Closed data unions, wanting value semantics, chasing zero overhead |

How to choose? Let me distill the decision logic into a few sentences. **If you already have an inheritance hierarchy in place, and the element classes are a given that cannot or should not change** (say you're adding operations to a third-party library's types, or the base class was designed for others to inherit from in the first place), and the number of operations will far outnumber the number of types — use the classic Visitor. In that context its intrusiveness is not a flaw; it is precisely the only way to hang the operations on at all. **If you are designing a closed set of data types from scratch** (most typically AST nodes, event types, config entries), with no inheritance baggage, wanting value semantics, compile-time exhaustiveness checking, and zero virtual-function overhead — then go with `std::variant` + `std::visit` without hesitation; in modern C++ it is the lighter, safer default choice.

## Pitfall warnings: detail traps in a few classic idioms

::: warning The classic Visitor's visit should take const&
In the classic Visitor, if you write the `visit` parameter as `visit(Circle& c)` (a non-const reference), that says this visitor intends to **modify** the shape; for a read-only visitor like `AreaCalculatorVisitor`, the correct spelling is `visit(const Circle& c)`. This is not just const-correctness fussiness — it directly determines that your `Shape::accept` must match signatures: if `visit` takes `const Circle&`, then `accept` must also be a `const` member function (`virtual void accept(ShapeVisitor&) const`), otherwise you simply cannot call `accept` on a `const Shape&` at all. The companion Playground project uses non-const references for `AreaCalculatorVisitor`; strictly speaking, for the "compute the area without changing anything" semantics, the const version is the correct one. And once this const chain is written wrong in the base class, every derived class after it bends along with it, which is a pain to fix — so settle from the start whether each visitor is read-only or mutating.
:::

::: tip Pairing variant + visit with direct discriminator access
Besides `std::visit`, day to day you'll also use a few companion tools: `v.index()` (get the index of the currently held type), `std::holds_alternative<T>(v)` (ask "is it a T right now"), and `std::get_if<T>(&v)` (safely get a pointer to the T, or nullptr if it isn't one). None of them throws, which suits the moments when writing a full visitor is inconvenient and you just want a quick type check. But the moment every type needs something real done to it, prefer `std::visit`, because it is compile-time exhaustively checked — whereas `if (holds_alternative<A>) ... else if ...` degenerates right back into the hand-written dispatch anti-example we opened this article with.
:::

::: warning The Overloaded helper needs C++17 deduction guides
That little `Overloaded` utility — the one kneading several lambdas into an overload set — relies on C++17's **class template argument deduction guide (CTAD deduction guide)**: `template <class... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;`. Without this line, when you write `Overloaded{[](A&){}, [](B&){}}`, the compiler has no idea it should deduce `Overloaded<lambda1, lambda2>`. Also, the line `using Ts::operator()...;` uses a variadic using-declaration to pull every base class's `operator()` into the derived class to participate in overload resolution — likewise a feature that only exists from C++17. So the minimum bar for this spelling is **C++17**; in C++20 you can write it a bit more compactly, but the core mechanism is unchanged.
:::

## Summary

Let's trace the whole evolutionary path once through:

| Stage | Approach | Why it's still not enough |
|---|---|---|
| `if/else` + `dynamic_cast` | A long chain of type tests | RTTI is slow, easy to miss a case, and adding types/operations means editing this whole blob |
| Classic Visitor | `accept` + `visit`, virtual functions + overload resolution assembling double dispatch | Friendly to adding operations, but adding a type means modifying the interface and all visitors, and it's highly intrusive |
| `std::variant` + `std::visit` | Pack the closed types into a variant, visit does compile-time dispatch | The type set must be closed at compile time; no runtime extension |

Note down these key takeaways:

- The Visitor pattern solves the problem of, when **operations cut across a set of types and keep growing**, avoiding welding those operations into the data classes. Its extensibility ledger: **open for adding operations, closed for adding types**.
- The classic Visitor's double dispatch is a "**virtual function picks `accept` → `*this`'s static type picks the `visit` overload → virtual function picks the `visit` implementation**" three-leg relay. `accept` must be overridden in each derived class because the static type of `*this` needs correcting — it is not a matter of style.
- In modern code, reach first for `std::variant` + `std::visit`: compile-time exhaustiveness checking (miss one type and the build fails outright), zero virtual-function overhead (dispatch is a discriminator comparison, inlinable), non-intrusive (element classes are plain structs). The price is that the type set must be closed at compile time.
- The classic Visitor and variant are actually six of one and half a dozen of the other on "the type set must be closed"; genuine runtime dynamic type extension is beyond both — that takes type erasure + a registry.
- Read "variant is always faster" precisely: its advantage is that the dispatch mechanism doesn't depend on a vtable, and that the visitor logic can be inlined wholesale. In actual measurements (`-O2`/`-O3`, 5 million visits) the variant version was consistently about 30% faster than the virtual-function version; but that was measured in the "visitor is short and inlinable" sweet spot — with a heavy visitor or many types, the gap narrows.

## References

- [cppreference: `std::variant`](https://en.cppreference.com/w/cpp/utility/variant) (since C++17, discriminated union)
- [cppreference: `std::visit`](https://en.cppreference.com/w/cpp/utility/variant/visit) (since C++17, discriminator-based visitor dispatch)
- [cppreference: `std::numbers::pi`](https://en.cppreference.com/w/cpp/numeric/constants) (since C++20, replacement for the non-standard `M_PI`)
- [cppreference: Virtual functions](https://en.cppreference.com/w/cpp/language/virtual) (virtual functions and the single-dispatch mechanism)
- GoF, *Design Patterns: Elements of Reusable Object-Oriented Software* — the original definition of the Visitor pattern
- Andrei Alexandrescu, *Modern C++ Design* Chapter 10 — double-dispatch implementations of variants such as the Acyclic Visitor
- The companion compilable project: [visitor](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/volumn_codes/vol4/design-patterns/Visitor)
