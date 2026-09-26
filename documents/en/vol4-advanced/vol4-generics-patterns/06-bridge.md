---
title: "Bridge Pattern: Decoupling Abstraction from Implementation and Introducing pImpl Along the Way"
description: "Starting from the most naive 'one class does everything' version, we derive the Bridge pattern step by step, then use it to explain exactly why pImpl keeps header edits from cascading into project-wide recompiles"
chapter: 11
order: 6
tags:
  - host
  - cpp-modern
  - intermediate
  - 桥接模式
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17, 20]
reading_time_minutes: 22
related:
  - "Singleton Pattern: From Comment-Only Constraints to Meyer's Singleton"
prerequisites:
  - Classes and Object-Oriented Programming
translation:
  source: documents/vol4-advanced/vol4-generics-patterns/06-bridge.md
  source_hash: 9e5a5119662fddcb5a6df642968e8463ed36e5addc43602b3188f01d6b07e55e
  translated_at: '2026-09-26T05:14:45+00:00'
  engine: anthropic
  token_count: 4100
---

# Bridge Pattern: Decoupling Abstraction from Implementation and Introducing pImpl Along the Way

## What Problem Are We Actually Solving

Once again, let's not rush to a definition. Picture the most classic scenario: you're building a cross-platform graphics library that draws all kinds of shapes — circles, squares, triangles — and when each shape actually hits the screen, what's underneath may be OpenGL or DirectX. Written the most straightforward way, you'll naturally produce an inheritance tree: `OpenGLCircle`, `DirectXCircle`, `OpenGLSquare`, `DirectXSquare`... Combine the two dimensions (shape × backend) and the number of classes grows **multiplicatively**. Add one shape, and you must re-implement every backend for it; add one backend, and you must re-implement every shape for it. This is what the textbooks call **class explosion**.

The Bridge pattern exists for exactly this "two dimensions expanding at the same time" predicament, and its core fits in one sentence: **split the Abstraction (the high-level logic) and the Implementor (the low-level details) into two independent inheritance chains, have the abstraction hold a reference to an implementation interface, and use that reference to "bridge" the two chains together**. The shape chain cares only about "who am I and how do I compute my geometry"; the backend chain cares only about "how do I actually get pixels onto the screen". The two dimensions extend independently of each other and combine freely at run time (`Circle + OpenGL` or `Circle + DirectX`), and the class count drops from multiplication back to addition.

Once you've had a taste of this "separate the abstraction from the implementation" idea, you'll want to use it everywhere. Next we'll watch step by step how it travels from a graphics-library design all the way to pImpl, the idiom every C++ engineer knows — the latter is essentially the Bridge pattern specialized to the pair of dimensions "interface class vs implementation class".

## Step One: The Most Straightforward Approach — One Class Does Everything

First let's see what things look like "without the bridge". Suppose there is only one dimension: drawing a circle, with OpenGL underneath. The most intuitive way to write it is to stuff the geometry parameters and the drawing code into one class:

```cpp
class OpenGLCircle {
public:
    OpenGLCircle(double x, double y, double r) : x_(x), y_(y), r_(r) {}
    void draw() {
        // Geometry + OpenGL calls + rendering details, all in one place
        std::cout << "[OpenGL] draw circle at (" << x_ << "," << y_
                  << ") r=" << r_ << "\n";
    }
private:
    double x_, y_, r_;
};
```

As long as the requirement is "this one case and nothing else", this style is perfectly fine — you could even call it the clearest possible version. But the story clearly doesn't end here: the product folks come over and say we need to support DirectX too. Your knee-jerk reaction might be to write another `DirectXCircle`:

```cpp
class DirectXCircle {
public:
    DirectXCircle(double x, double y, double r) : x_(x), y_(y), r_(r) {}
    void draw() {
        std::cout << "[DirectX] draw circle at (" << x_ << "," << y_
                  << ") r=" << r_ << "\n";
    }
private:
    double x_, y_, r_;
};
```

Notice anything? Apart from that one `cout` line inside `draw()`, the two classes are **identical** — same members, same constructors, same geometry logic. Now you're asked to add a `Square`, and you're writing `OpenGLSquare` and `DirectXSquare` all over again, computing the geometry twice. Once both dimensions start growing on their own, duplicate code piles up at multiplicative speed. That's the price of not separating abstraction from implementation: every "combination path" is its own separate branch.

## Step Two: Extract an Implementation Interface — Walking on Two Legs

Let's pull apart "the part that repeats" from "the part that varies". The varying part is "which backend does the drawing", so we pull that out into an interface; the invariant part is "the shape's geometry logic", which stays in the abstraction layer. The abstraction doesn't draw by itself — it **holds a pointer to a backend interface** and delegates the drawing to the backend:

```cpp
// Implementor (implementation interface): describes only the "ability to draw", agnostic to shapes
struct DrawingAPI {
    virtual ~DrawingAPI() = default;
    virtual void draw_circle(double x, double y, double r) = 0;
};

// ConcreteImplementor: the actual backend implementations
struct OpenGLApi : DrawingAPI {
    void draw_circle(double x, double y, double r) override {
        std::cout << "[OpenGL]  绘制圆 中心(" << x << "," << y
                  << ") 半径 " << r << "\n";
    }
};

struct DirectXApi : DrawingAPI {
    void draw_circle(double x, double y, double r) override {
        std::cout << "[DirectX] 绘制圆 中心(" << x << "," << y
                  << ") 半径 " << r << "\n";
    }
};
```

This `DrawingAPI` inheritance chain is the "implementation" dimension, and it takes care of exactly one thing: hand me coordinates and a radius, and I'll draw. Next comes the "abstraction" dimension:

```cpp
// Abstraction: holds the implementation interface, doesn't draw itself, delegates to the backend
class Shape {
public:
    explicit Shape(std::unique_ptr<DrawingAPI> api)
        : api_(std::move(api)) {}
    virtual ~Shape() = default;
    virtual void draw() = 0;
protected:
    std::unique_ptr<DrawingAPI> api_;
};

// RefinedAbstraction: a concrete shape, responsible only for its own geometry
class Circle : public Shape {
public:
    Circle(double x, double y, double r, std::unique_ptr<DrawingAPI> api)
        : Shape(std::move(api)), x_(x), y_(y), r_(r) {}
    void draw() override { api_->draw_circle(x_, y_, r_); }
private:
    double x_, y_, r_;
};
```

See, `Circle` now has no idea which backend is drawing it — all it knows is "I hold something that can draw a circle", and which one that is gets injected from outside at run time. The two dimensions are fully decoupled: the shape chain can grow `Square`, `Triangle`; the backend chain can grow `VulkanApi`, `MetalApi`; each side evolves without bothering the other. The class count is back to addition — what you need is `number of shapes + number of backends`, not `number of shapes × number of backends`.

Let's run it and verify that "the same circle, injected with different backends, really does behave differently":

```sh
$ g++ -std=c++23 -O2 bridge_shape.cpp -o bridge_shape
$ ./bridge_shape
a 用 OpenGL 后端:
[OpenGL]  绘制圆 中心(1,2) 半径 3
b 用 DirectX 后端:
[DirectX] 绘制圆 中心(4,5) 半径 6
```

Same `Circle`, pass in different `DrawingAPI`s, and out comes different backends' output. That is the Bridge: the `api_` pointer is the "bridge" connecting the two inheritance chains.

::: tip When to reach for the Bridge
The criterion is simple: **when you find yourself circling inside a two-dimensional (or even higher-dimensional) expansion space — "shape × backend", "message type × transport protocol", "window widget × theme skin" — and every added dimension means copying all the existing code over again, it's time to pull one of the dimensions out into an implementation interface and let the other dimension hold it**. Bridge is "consciously separating two dimensions from the very start of the design", and on that point it's completely different from the Adapter we'll meet later — an adapter is a shell slapped over an existing class after the fact. We'll compare them properly at the end of this article.
:::

## The Story Doesn't End Here: The Same Idea, Swapped Dimensions, Is pImpl

The graphics-library example shows you the two-dimensional "abstraction × implementation" separation, but it's still one step away from the C++ we write every day. Now let's swap the dimensions for a more common pair: **"the interface exposed to the outside" × "the implementation details hidden inside"**.

Back to a pain you've surely stepped in on a real project: header bloat. You wrote a `Widget` and dutifully declared its private members in the header:

```cpp
// widget.h
#include <string>
#include <vector>

class Widget {
public:
    void do_work();
private:
    std::vector<int> data_;
    std::string name_;
};
```

Nothing looks wrong at all — until the day you add a `std::unordered_map<std::string, Config> cache_;` next to `data_`, or replace `std::string` with `std::filesystem::path`. To your irritation you discover: **the moment the header changes by even a hair, every translation unit that does `#include "widget.h"` has to be recompiled in full**. If this is a foundational class referenced by hundreds of files across the project, one small edit of yours triggers a rebuild of hundreds of files, and CI time doubles on the spot.

The root of the problem: the types of private members like `data_` and `name_` are "private" at the interface level, but at the **header level they are public**. Any translation unit that includes this header has to chew through the complete definitions of those types to compute `sizeof(Widget)` and to lay out the stack. So heavyweight headers like `std::vector`, `std::string`, and even `<unordered_map>` get transmitted to the whole project through `widget.h`. You clearly wrote `private:` in your definition, yet the compiler is telling you: **you are only private logically; physically, those details are still exposed in the header**.

The Bridge pattern gives us the way out: move the "implementation details" wholesale into a separate `Impl` class, and the public-facing interface class holds only a pointer to `Impl`. The interface class is the "abstraction" dimension, `Impl` is the "implementation" dimension, and the pointer between them is the bridge. This technique has a proper name — **pImpl (Pointer to IMPLementation)** — and it is the Bridge pattern's most celebrated representative in C++ engineering.

## pImpl Step One: A Raw Pointer and a Hand-Written Destructor

We start from the most primitive pImpl: forward-declare `Impl`, and leave nothing but a raw pointer in the header:

```cpp
// widget.h — nothing left but the interface
class Widget {
public:
    Widget();
    ~Widget();
    void do_work();
private:
    struct Impl;       // forward declaration; Impl's definition moves into the cpp
    Impl* pImpl;       // raw pointer
};
```

```cpp
// widget.cpp — all the implementation details hide in here
#include "widget.h"
#include <iostream>
#include <string>
#include <vector>

struct Widget::Impl {
    std::vector<int> data;
    std::string name;
    void do_work_impl() {
        std::cout << "doing heavy work, data.size=" << data.size() << "\n";
    }
};

Widget::Widget() : pImpl(new Impl{}) {}
Widget::~Widget() { delete pImpl; }   // hand-written destructor
void Widget::do_work() { pImpl->do_work_impl(); }
```

Look at that: `widget.h` now contains no `<vector>`, no `<string>`, no heavyweight header at all. However `Impl`'s members change, only `widget.cpp` gets touched — the outside world knows nothing. For the tiny price of one pointer dereference, we bought complete isolation of implementation details. That's the whole magic of pImpl — **it moves the compile-time dependency from the "header layer" into a "single translation unit"**.

But the raw-pointer road has obvious shortcomings. You have to hand-write `~Widget() { delete pImpl; }` — forget that once and it's a memory leak. You want copy support? Now you're hand-writing the copy constructor and copy assignment to `new` up a fresh `Impl`. And if an exception fires somewhere along the way, resource management is back to that white-knuckled C++98 state. We write modern C++ here; this shall not stand.

## pImpl Step Two: Let `std::unique_ptr` Take Over the Lifetime

Let RAII manage the pointer for us. Replace `Impl*` with `std::unique_ptr<Impl>`, and hand destruction and move semantics entirely to the smart pointer:

```cpp
// widget.h
#include <memory>

class Widget {
public:
    Widget();
    ~Widget();
    void do_work();
private:
    struct Impl;
    std::unique_ptr<Impl> pImpl;
};
```

```cpp
// widget.cpp
#include "widget.h"
#include <iostream>
#include <string>
#include <vector>

struct Widget::Impl {
    std::vector<int> data;
    std::string name;
    void do_work_impl() { /* ... */ }
};

Widget::Widget() : pImpl(std::make_unique<Impl>()) {}
Widget::~Widget() = default;   // key point: only = default works here, see below
void Widget::do_work() { pImpl->do_work_impl(); }
```

It looks so good it's practically free — and the real trap hides exactly in this step. You might ask: since `unique_ptr` destroys things automatically, can't I just write `~Widget() = default;` directly in the header? It's only a defaulted destructor, after all. **No — get this wrong and it will not compile**, guaranteed. Let's verify.

## Let's Verify First: Why `~Widget()` Must Move Into the cpp

Talk is cheap, so let's put `~Widget() = default;` back into the header and deliberately let the compiler see it at a point where `Impl` is incomplete:

```cpp
// widget.h — a counter-example
#pragma once
#include <memory>

class Widget {
public:
    Widget() = default;
    ~Widget() = default;   // ← deliberately written in the header, where Impl is incomplete
    void do_work();
private:
    struct Impl;
    std::unique_ptr<Impl> pImpl;
};
```

Compile it once, and gcc's error is remarkably blunt:

```sh
$ g++ -std=c++23 -O2 widget.cpp main.cpp -o wtest
In file included from /usr/include/c++/16.1.1/memory:80,
                 from widget.h:2,
                 from main.cpp:1:
/usr/include/c++/16.1.1/bits/unique_ptr.h: In instantiation of
'constexpr void std::default_delete<_Tp>::operator()(_Tp*) const
[with _Tp = Widget::Impl]':
widget.h:6:5: required from here
unique_ptr.h:90:23: error: invalid application of 'sizeof' to
incomplete type 'Widget::Impl'
   90 |         static_assert(sizeof(_Tp)>0,
      |                       ^~~~~~~~~~~
```

Here's what happened. `std::unique_ptr<Impl>`'s destructor has to call `delete` to destroy the `Impl` object, and `delete` internally performs a `static_assert(sizeof(Impl) > 0)` — it must confirm that `Impl` is a complete type with a well-defined `sizeof`; otherwise the compiler cannot generate the correct destruction call. But once you write `~Widget() = default;` in the header, the compiler must instantiate `Widget`'s destructor (and thereby `unique_ptr<Impl>`'s destructor) **at the very instant the header is included** — and at that instant `Impl` is still just a forward declaration, `sizeof(Impl)` cannot be computed at all, and everything blows up.

There is exactly one fix: **move the definition of `~Widget()` into `widget.cpp`**. In the cpp, `Impl` has already been fully defined, so writing `Widget::~Widget() = default;` there generates the destructor code just fine. This pit looks trivial, yet it is the first — and the most likely to make people give up — hurdle in the pImpl pattern: you copy a version from a blog, and then comes a string of incomprehensible `incomplete type` errors, and your blood pressure shoots right up.

::: warning Burn this pit into memory
Whenever your class contains a `std::unique_ptr<IncompleteType>`, **the class's destructor — along with any special member function whose definition would trigger the `unique_ptr`'s destruction (move ctor/assign if they are `= default`, for instance) — must have its definition moved into the translation unit where the incomplete type has already been fully defined**. For pImpl specifically, that means moving everything into `widget.cpp`. And it isn't just the destructor; the same rule holds for the move constructor we'll get to below.
:::

## pImpl Step Three: Restoring Copy Semantics (clone + copy-and-swap)

At this point we have a pImpl class that destructs correctly and moves — but it still **cannot be copied**. The reason is direct: `std::unique_ptr<Impl>` is move-only, so the copy constructor and copy assignment the compiler would otherwise generate for `Widget` get deleted. Write `Widget b = a;` anyway, and you'll get an error about using a deleted function.

Yet in real engineering, a "pImpl class" is often exactly the thing that needs to copy — it's a value-semantic, outward-facing type, and both putting it in containers and passing it by value require it to be copyable. How do we add that back? The most robust approach is to sink the copy logic down into `Impl`, and let `Widget` forward through `clone()`:

```cpp
// in widget.cpp, Impl gains a clone
struct Widget::Impl {
    std::vector<int> data;
    std::string name;
    void do_work_impl() { /* ... */ }
    std::unique_ptr<Impl> clone() const {
        return std::make_unique<Impl>(*this);   // deep copy
    }
};
```

Then write out `Widget`'s copy constructor and copy assignment by hand, delegating to `clone()`:

```cpp
// widget.cpp
Widget::Widget(const Widget& other)
    : pImpl(other.pImpl ? other.pImpl->clone() : nullptr) {}

Widget& Widget::operator=(const Widget& other) {
    Widget tmp(other);          // copy into a temporary first
    swap(*this, tmp);           // then swap it with *this
    return *this;               // tmp's destruction frees the old Impl
}
```

This uses the classic **copy-and-swap** idiom: first use the copy constructor to build a temporary `tmp`, then swap it with `*this`; when the function returns, `tmp` leaves scope and the old `Impl` is destroyed automatically. The benefit is the **strong exception guarantee** — if `clone()` throws, `*this` hasn't been touched at all and its state is completely unchanged; if `clone()` succeeds, the swap is merely a non-throwing pointer shuffle, and releasing the old resource afterwards can't go wrong either.

Note that `swap`: it's a `friend` function that simply exchanges the two `unique_ptr`s, and it's `noexcept` too:

```cpp
// in widget.h, as a friend of Widget
friend void swap(Widget& a, Widget& b) noexcept {
    using std::swap;
    swap(a.pImpl, b.pImpl);
}
```

::: warning Don't put swap's inline in the wrong place
Some older notes write the friend `swap` as `friend void inline swap(...)`. That spelling compiles, but `inline` after the return type is a very old, discouraged style, and it's easy to confuse with the whole `using std::swap;` idiom. The proper spelling is `friend void swap(...) noexcept` — a `friend` function defined inside the class body is already `inline`, so gilding the lily is unnecessary.
:::

Let's put the whole package (deep copy + move) together and verify it — is the copied object really independent, and is the moved-from source really hollowed out:

```sh
$ g++ -std=c++23 -O2 -pthread bridge_verify.cpp -o bridge_verify
$ ./bridge_verify
a.size after move = 0 (expect 0,被 move 走了)
b.size = 100, b[0] = 42 (expect 100, 42,深拷贝独立)
c.size = 100 (expect 100,move 接管)
```

After `a` is `std::move`d into `c`, `a`'s own `size` is 0 — the source object was hollowed out, which is precisely move semantics. Meanwhile `b` was copied out of `a`, and it has its own independent 100 copies of `42`: modifying `b` won't affect `c`, nor the other way around — a deep copy is genuinely independent. pImpl, together with `clone()` and copy-and-swap, rounds out every behavior value semantics is supposed to have.

## pImpl Step Four: Mark the Moves `noexcept` So vector Reallocates by Moving

You now have a pImpl class that copies and moves. But one last step separates it from "drop it straight into a `std::vector` without falling apart", and it's a step many people miss — **the move constructor and move assignment must be marked `noexcept`**.

Why is this one keyword so decisive? The reason: when `std::vector` reallocates (say a `push_back` triggers reallocation), it must carry the elements from the old memory into the new. At that point it has a choice: move where it can, copy where it can't. But vector's strong exception-safety promise requires "if anything throws during the move, the original vector must remain unchanged" — and once a move throws, the source object has already been hollowed out, so exception safety is broken. Hence vector's policy: **only when the element's move constructor is `noexcept` does it dare to move; otherwise it would rather copy** (if a copy throws, the source object is still there and things can roll back).

Your pImpl class's move really only shuffles one `unique_ptr` and can never throw — but **if you don't write `noexcept`, vector doesn't know that, and it will dutifully fall back to copying** — `clone()` one after another, and every one of those is a heap allocation. Let's verify directly; the gap is an order of magnitude:

```sh
$ ./bridge_verify
[noexcept move]    copy=0 move=1020 (扩容应纯走 move,copy=0)
[non-noexcept move] copy=1020 move=0 (无 noexcept,扩容走 copy,move=0)
```

Same "stuff 1000 elements into a `vector`", same multiple triggered reallocations: the group with `noexcept` marked **moves exclusively, zero copies**; the group without it is the exact opposite — vector doesn't trust your move, every reallocation goes through copy, 1020 deep copies. For a pImpl class, that means 1020 extra heap allocations. One keyword, and performance differs by an order of magnitude.

So a pImpl class's special member functions should look like this:

```cpp
class Widget {
public:
    Widget();
    ~Widget();                          // defined out-of-line (Impl complete)
    Widget(const Widget&);              // clone deep copy
    Widget& operator=(const Widget&);   // copy-and-swap
    Widget(Widget&&) noexcept;          // move must also be out-of-line + noexcept
    Widget& operator=(Widget&&) noexcept;
    // ...
};
```

```cpp
// widget.cpp
Widget::Widget(Widget&&) noexcept = default;
Widget& Widget::operator=(Widget&&) noexcept = default;
```

The move's `= default` likewise has to be written in the cpp, for exactly the same reason as the destructor — internally it has to move a `unique_ptr<Impl>`, which also needs `Impl` to be complete.

## Wrapping Up: What pImpl Actually Buys You

Stitch the steps above together and we get a production-ready pImpl recipe: the header keeps only a forward declaration and one `unique_ptr`; the destructor and moves all move into the cpp; copying is implemented via `clone()` + copy-and-swap; moves are uniformly `noexcept`. It buys three very tangible benefits.

The first is **a steep collapse in compile-time dependencies**. The header no longer contains heavyweight headers like `<vector>` and `<string>`; any change to `Impl`'s members is locked inside the single translation unit `widget.cpp`, and the outside world doesn't need to recompile at all. In a large project, this one benefit alone is enough to make you fall in love with pImpl.

The second is **a stable ABI**. Let's verify something very concrete — after pImpl, how big is the object, really:

```sh
$ ./bridge_verify2
sizeof(Widget)           = 8
sizeof(NaiveWidget)      = 56
sizeof(void*)            = 8
说明:Widget 压成一个指针大小,Impl 再怎么长,Widget 的 ABI 不变
```

Under the naive style, `Widget` is 56 bytes (in pointer terms: three for the `vector`, plus a `string` that occupies 32 bytes under libstdc++ — 56 in total); after pImpl, `Widget` compresses to 8 bytes — exactly one pointer. This means that as long as your **public interface doesn't change** (add members inside `Impl` or swap their types to your heart's content), the binaries your users compiled keep linking as usual, no rebuild needed — that is ABI stability. For teams shipping dynamic libraries (.so / .dll) it's a hard requirement: you can't ask users to relink every time you touch an internal member.

The third is **genuine encapsulation**. `private:` in a header blocks "access" but not "visibility" — the types of your private members are public knowledge to everyone; whether you used `std::vector` or a homegrown container is on full display. pImpl physically moves those details into the cpp, blocking even "visibility" — that's encapsulation taken all the way home.

The costs deserve equal candor: one extra pointer dereference per member access (pImpl's bridge); one mandatory heap allocation for `Impl`; the header-side inlining optimization no longer applies to the implementation functions (they live in a cpp; cross-translation-unit inlining needs LTO); and copying goes through `clone()`, a deep copy with allocation overhead. Measured against what you gain on the compile-and-release side, those costs are worth paying the vast majority of the time.

::: tip When to reach for pImpl
Not every class deserves pImpl. A simple litmus test: **use pImpl when the class is an "interface/facade" that the whole project includes heavily, when you want its binary ABI to stay stable across versions, or when you want to completely hide a set of heavyweight third-party dependencies (`<unordered_map>`, `<boost/...>`)**. Conversely, for an implementation class used only inside a single cpp, or a small, extremely performance-sensitive type whose members are inlined everywhere, pImpl's indirection overhead doesn't pay off. Measure first, then decide.
:::

## Bridge vs Adapter: Similar Looks, Completely Different Intent

With the Bridge covered, one comparison is unavoidable: it looks far too much like the Adapter pattern — both have a "delegation pointer", and both wrap one object and forward to another. But the two patterns' **intent and design timing** are completely different, and that is the only reliable standard for telling them apart.

The Bridge **consciously splits the dimensions from the very start of the design**: you foresee that "shape" and "backend" will each evolve on their own, so up front you split them into two inheritance chains and let the abstraction hold the implementation. Its keywords are "**separate in advance, both dimensions extend independently**". The Adapter, on the other hand, is a **remedy after the fact**: you already have an off-the-shelf class (say, an old networking library) whose interface doesn't match what the new system expects, so you have no choice but to wrap it in a shell that "translates" the old interface into the new one. Its keywords are "**glue afterwards, one-way conversion**".

A simple rule of thumb: **if you are designing so that "two dimensions can each extend on their own", use Bridge; if you are patching so that "an existing class gets plugged into a new interface", use Adapter**. The two look alike on the outside (both have a delegation pointer), but their design motivations are worlds apart — don't lump them together.

## Summary

Let's walk the evolution path once more:

| Stage | Approach | Why it still falls short |
|---|---|---|
| One class does everything | `OpenGLCircle` hard-codes geometry and backend together | Every new backend means duplicating the whole class — class explosion |
| Extract the implementation interface | `Shape` holds a `DrawingAPI`; the two chains extend independently | **The Bridge pattern is in place** — each dimension evolves on its own |
| Raw-pointer pImpl | `Impl*` + a hand-written `delete` | Tedious memory management; copying/exceptions are error-prone |
| `unique_ptr<Impl>` | RAII takes over; destructor and moves defined out-of-line | Not copyable — value semantics missing |
| clone + copy-and-swap | The deep copy sinks into `Impl`; strong exception safety | The moves aren't `noexcept`, so vector reallocation degrades to copying |
| Mark the moves noexcept | vector dares to move during reallocation | **Production-ready** — compile dependencies and ABI both stable |

Note down these key takeaways:

- **The essence of the Bridge is "split two independently-extending dimensions into two inheritance chains, then bridge them with one pointer"**; the class count drops from multiplication to addition, and pImpl is its special case for the "interface × implementation" pair of dimensions.
- **A pImpl header keeps only a forward declaration and a `unique_ptr<Impl>`**; all implementation details (heavyweight headers included) move into the cpp, buying a steep drop in compile-time dependencies and a stable ABI.
- **The destructor, move constructor, and move assignment definitions must move into the cpp** — because `unique_ptr<IncompleteType>`'s destructor needs a complete type, writing them in the header is a guaranteed compile failure.
- **Copying relies on `Impl::clone()` + copy-and-swap, and the moves must be marked `noexcept`** — otherwise `vector` won't dare to move during reallocation, things degrade into deep copies, and performance differs by an order of magnitude.
- **The difference between Bridge and Adapter lies in intent**: the former separates two dimensions in advance; the latter adapts an interface one-way after the fact.

::: tip Accompanying compilable project
This section's examples have a complete compilable project in the repository under `code/volumn_codes/vol4/design-patterns/Bridge/` (`.h` + main + `CMakeLists.txt`); `cmake -S . -B build && cmake --build build` reproduces all the outputs above.
:::

## References

- [cppreference: `std::unique_ptr`](https://en.cppreference.com/w/cpp/memory/unique_ptr) (since C++11; the incomplete-type and destruction requirements are under *Notes*)
- [cppreference: `std::make_unique`](https://en.cppreference.com/w/cpp/memory/unique_ptr/make_unique) (since C++14)
- [cppreference: `std::move` and `noexcept` move semantics](https://en.cppreference.com/w/cpp/utility/move) (the `move_if_noexcept` mechanism behind `vector` reallocation is covered in the notes on [vector](https://en.cppreference.com/w/cpp/container/vector))
- Herb Sutter, GotW #28/100: "The Fast PImpl Idiom" and "Compilation Firewalls" (the compilation-firewall motivation behind pImpl and its best practices)
- ISO C++ Core Guidelines: [C.133–C.139 / R.20–R.23](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines) (class layout and smart-pointer ownership)
