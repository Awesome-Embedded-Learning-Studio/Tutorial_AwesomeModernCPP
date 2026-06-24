---
title: 'Bridge Pattern: Decoupling Abstraction from Implementation, and Introducing
  pImpl'
description: Starting from the most straightforward approach where "one class does
  it all," we progressively derive the Bridge pattern, and then use it to thoroughly
  explain why pImpl prevents header files from suffering from cascading recompilations.
chapter: 11
order: 6
tags:
- host
- cpp-modern
- intermediate
- 桥接模式
difficulty: intermediate
platform: host
cpp_standard:
- 11
- 14
- 17
- 20
reading_time_minutes: 22
related:
- 单例模式:从注释约束到 Meyer's Singleton
prerequisites:
- 'Chapter 6: 类与对象'
translation:
  source: documents/vol4-advanced/vol4-generics-patterns/06-bridge.md
  source_hash: 9e5a5119662fddcb5a6df642968e8463ed36e5addc43602b3188f01d6b07e55e
  translated_at: '2026-06-24T00:55:27.622650+00:00'
  engine: anthropic
  token_count: 3902
---
# Bridge Pattern: Decoupling Abstraction from Implementation, and a Look at pImpl

## What problem are we actually solving?

Let's hold off on the formal definition for a moment. Consider a classic scenario: you are building a cross-platform graphics library that needs to render various shapes—circles, squares, triangles. When it comes time to actually draw these shapes to the screen, the underlying technology might be OpenGL or DirectX. If you follow the most straightforward approach, you will naturally end up with an inheritance tree like `OpenGLCircle`, `DirectXCircle`, `OpenGLSquare`, `DirectXSquare`... With two dimensions (shapes × backends), the number of classes grows via **multiplication**. Every time you add a shape, you have to re-implement it for every backend; every time you add a backend, you have to re-implement all shapes for it. This is the **class explosion** often mentioned in textbooks.

The Bridge pattern aims to solve this dilemma of "simultaneous expansion in two dimensions." Its core concept can be summarized in one sentence: **Decouple the "Abstraction" (upper-level logic) and the "Implementor" (low-level details) into two separate inheritance chains. Have the abstraction hold a reference to the implementation interface, effectively "bridging" the two chains together.** The shape chain only cares about "who I am and how to calculate geometry," while the backend chain only cares about "how to actually push pixels to the screen." Both dimensions can expand independently, and they can be freely combined at runtime (`Circle + OpenGL` or `Circle + DirectX`). The number of classes changes from multiplication to addition.

Once you experience the benefits of this "separation of abstraction and implementation," you will want to use it everywhere. Next, we will see step-by-step how this design evolves from a graphics library scenario into the well-known pImpl idiom in C++ engineering—which is essentially a specific application of the Bridge pattern applied to the dimensions of "interface class vs. implementation class."

## Step one: The most straightforward approach—one class does it all

First, let's look at what things look like "without a bridge." Suppose we have only one dimension: drawing circles using OpenGL. The most intuitive approach is to stuff geometric parameters and drawing code into a single class:

```cpp
class OpenGLCircle {
public:
    OpenGLCircle(double x, double y, double r) : x_(x), y_(y), r_(r) {}
    void draw() {
        // 几何信息 + OpenGL 调用 + 渲染细节,全在一起
        std::cout << "[OpenGL] draw circle at (" << x_ << "," << y_
                  << ") r=" << r_ << "\n";
    }
private:
    double x_, y_, r_;
};
```

This approach works perfectly fine when "this is the only requirement," and is arguably the clearest. But the story clearly doesn't end there—the product manager comes along and says: we need to support DirectX. Your immediate reaction might be to write another `DirectXCircle`:

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

Haven't you noticed? Apart from that one `cout` line in `draw()`, these two classes are **identical** in terms of members, constructors, and geometric logic. Now, if we ask you to add a `Square`, you have to write `OpenGLSquare` and `DirectXSquare`, calculating the geometry twice. Once both dimensions start expanding, duplicate code begins to pile up multiplicatively. This is the cost of failing to separate abstraction from implementation: every "combination path" becomes a separate branch.

## Step 2: Extract the Implementation Interface — Walking on Two Legs

We split the "repeating part" from the "varying part." The varying part is "which backend to use for drawing," so we extract it into an interface. The invariant part is the "geometric logic of the shape," which remains in the abstraction layer. The abstraction layer doesn't draw by itself; instead, it **holds a pointer to the backend interface** and delegates the drawing operation to the backend:

```cpp
// Implementor(实现接口):只描述「画的能力」,不管形状
struct DrawingAPI {
    virtual ~DrawingAPI() = default;
    virtual void draw_circle(double x, double y, double r) = 0;
};

// ConcreteImplementor:真正的后端实现
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

This `DrawingAPI` inheritance chain represents the "implementation" dimension. It handles only one thing: give me coordinates and a radius, and I will draw it. Next is the "abstraction" dimension:

```cpp
// Abstraction:持有实现接口,自己不画,委托给后端
class Shape {
public:
    explicit Shape(std::unique_ptr<DrawingAPI> api)
        : api_(std::move(api)) {}
    virtual ~Shape() = default;
    virtual void draw() = 0;
protected:
    std::unique_ptr<DrawingAPI> api_;
};

// RefinedAbstraction:具体形状,只负责自己的几何
class Circle : public Shape {
public:
    Circle(double x, double y, double r, std::unique_ptr<DrawingAPI> api)
        : Shape(std::move(api)), x_(x), y_(y), r_(r) {}
    void draw() override { api_->draw_circle(x_, y_, r_); }
private:
    double x_, y_, r_;
};
```

You see, `Circle` is now completely unaware of which backend draws it — it only knows that "I have something capable of drawing a circle," and the runtime implementation is determined by external injection. The two dimensions are thoroughly decoupled: we can stack `Square` and `Triangle` on the shape side, and `VulkanApi` and `MetalApi` on the backend side. Both sides evolve independently without interfering with each other. The number of classes has also returned to addition — you only need `Number of Shapes + Number of Backends`, rather than `Number of Shapes × Number of Backends`.

Let's run it to verify that "the same circle, with different backends injected, indeed behaves differently":

```sh
$ g++ -std=c++23 -O2 bridge_shape.cpp -o bridge_shape
$ ./bridge_shape
a 用 OpenGL 后端:
[OpenGL]  绘制圆 中心(1,2) 半径 3
b 用 DirectX 后端:
[DirectX] 绘制圆 中心(4,5) 半径 6
```

With the same `Circle`, passing in a different `DrawingAPI` yields output for a different backend. This is the Bridge pattern: that `api_` pointer is the "bridge" connecting the two inheritance chains.

::: tip When to use the Bridge pattern
The criterion is simple: **when you find yourself going in circles within a "two-dimensional or even multi-dimensional" extension space—such as "Shape × Backend", "Message Type × Transport Protocol", or "Widget × Theme"—and every time you add a dimension you have to duplicate all existing code, it is time to extract one dimension into an implementation interface and let the other dimension hold it.** The Bridge pattern is about "consciously separating two dimensions from the very beginning of the design," which is completely different from the Adapter pattern we will discuss later. The Adapter pattern is about applying a wrapper to an existing class after the fact; we will specifically compare the two at the end of this article.
:::

## The story doesn't end here: the same idea, applied to a different dimension, is pImpl

The graphics library example helps you understand the separation of the "Abstraction × Implementation" dimensions, but it is still one step away from our daily C++ routine. Now, let's switch dimensions to a more common pair: **"Public Interface × Internal Implementation Details"**.

Let's revisit a pain point you have definitely encountered in real projects: header file bloat. You wrote a `Widget` and dutifully declared private members in the header file:

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

This looks perfectly fine until the day you add a `std::unordered_map<std::string, Config> cache_;` next to `data_`, or swap `std::string` for `std::filesystem::path`. You will be annoyed to discover that: **as soon as the header changes slightly, every compilation unit that includes `#include "widget.h"` must be completely recompiled.** If this is a base class referenced by hundreds of files across the project, a small change on your part triggers a recompilation of hundreds of files, and your CI time doubles immediately.

The root of the problem is this: although the types of private members like `data_` and `name_` are "private" in terms of the interface, they are **exposed at the header file level**. Any compilation unit that includes this header must see the full definitions of these types to calculate `sizeof(Widget)` and arrange the stack layout. Consequently, heavy headers like `std::vector`, `std::string`, and even `<unordered_map>` are propagated throughout the entire project via `widget.h`. You wrote `private:` in your definition, but the compiler is telling you: **you are logically private, but physically these details are still exposed in the header.**

The Bridge Pattern offers a way out: move the "implementation details" entirely into a separate `Impl` class, while the public interface class holds only a pointer to `Impl`. The interface class represents the "abstraction" dimension, `Impl` represents the "implementation" dimension, and the pointer connecting them is the bridge. This technique has a specific name—**pImpl (Pointer to IMPLementation)**—and it is the most prominent representative of the Bridge Pattern in C++ engineering.

## The First Step of pImpl: Raw Pointer + Hand-written Destructor

Let's start with the most primitive pImpl approach. We forward declare `Impl` and keep only a raw pointer in the header:

```cpp
// widget.h —— 干净得只剩下接口
class Widget {
public:
    Widget();
    ~Widget();
    void do_work();
private:
    struct Impl;       // 前向声明,Impl 的定义挪到 cpp
    Impl* pImpl;       // 裸指针
};
```

```cpp
// widget.cpp —— 实现细节全藏在这里
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
Widget::~Widget() { delete pImpl; }   // 手写析构
void Widget::do_work() { pImpl->do_work_impl(); }
```

Look, `widget.h` no longer includes `<vector>`, `<string>`, or any other heavy headers. No matter how the members of `Impl` change, we only modify `widget.cpp`, and the outside world remains unaware. We pay a tiny price—a single pointer dereference—in exchange for complete isolation of implementation details. This is the entire magic of pImpl—**it shifts compilation dependencies from the "header file layer" to a "single compilation unit."**

However, using raw pointers has obvious downsides. You have to manually write `~Widget() { delete pImpl; }`; forget this step, and you have a memory leak. If you want to support copying, you must manually write the copy constructor and copy assignment to `new` a fresh `Impl`. If an exception is thrown in the middle, resource management reverts to the nerve-wracking state of C++98. We are writing modern C++, and this is unacceptable.

## Step 2 of pImpl: Managing Lifetime with `std::unique_ptr`

We let RAII manage the pointer for us. By replacing `Impl*` with `std::unique_ptr<Impl>`, we delegate destruction and move semantics entirely to the smart pointer:

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
Widget::~Widget() = default;   // 关键:这里只能写 = default,见下文
void Widget::do_work() { pImpl->do_work_impl(); }
```

It looks almost too good to be true, but the real pitfall is hidden right here. You might ask: since `unique_ptr` automatically destroys the object, can I just write `~Widget() = default;` directly in the header file? After all, it's just a default destructor. **No, this will definitely fail to compile**, so let's verify this first.

## Verification: Why we must move `~Widget()` to the cpp file

Talk is cheap. Let's put `~Widget() = default;` back in the header file, intentionally forcing the compiler to see it while `Impl` is still an incomplete type:

```cpp
// widget.h —— 反面教材
#pragma once
#include <memory>

class Widget {
public:
    Widget() = default;
    ~Widget() = default;   // ← 故意写在头里,此时 Impl 不完整
    void do_work();
private:
    struct Impl;
    std::unique_ptr<Impl> pImpl;
};
```

Let's compile it. The error message from GCC is quite straightforward:

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

Here is the backstory. The destructor of `std::unique_ptr<Impl>` needs to call `delete` to destroy the `Impl` object. Internally, `delete` performs a `static_assert(sizeof(Impl) > 0)`—it must verify that `Impl` is a complete type with a known `sizeof`; otherwise, the compiler cannot generate the correct destructor call. However, once you write `~Widget() = default;` in the header file, the compiler must instantiate `Widget`'s destructor (and consequently `unique_ptr<Impl>`'s destructor) **the moment the header is included**. At that point, `Impl` is still just a forward declaration, so `sizeof(Impl)` cannot be calculated, and everything blows up.

The solution is simple: **move the definition of `~Widget()` to `widget.cpp`**. In the `.cpp` file, `Impl` has been fully defined, so writing `Widget::~Widget() = default;` there allows the destructor code to be generated correctly. This pitfall seems simple, but it is the first and most discouraging hurdle in the pImpl pattern—you copy a version from a blog, get a string of unintelligible `incomplete type` errors, and your blood pressure spikes.

::: warning Remember this pitfall
Whenever your class contains a `std::unique_ptr<IncompleteType>`, **the definitions of this class's destructor, and any special member functions that trigger the `unique_ptr` destructor (such as move constructor/assignment if defaulted), must be moved to the compilation unit where the incomplete type is fully defined**. Specifically for pImpl, move them all to `widget.cpp`. This applies not only to destructors but also to move constructors, as discussed below.
:::

## Step 3 of pImpl: Implementing Copy Semantics (clone + copy-and-swap)

At this point, we have a pImpl class that can be correctly destroyed and moved, but it **cannot be copied**. The reason is straightforward: `std::unique_ptr<Impl>` is move-only, so the copy constructor and copy assignment that the compiler automatically generates for `Widget` are deleted. If you try to write `Widget b = a;`, you will get an error about the function being deleted.

However, "pImpl classes" often need to be copied in real-world projects—they are value-semantic types intended to be stored in containers and passed by value. How do we fix this? The most robust approach is to push the copy logic down to `Impl`, and then have `Widget` forward the call via `clone()`:

```cpp
// widget.cpp 里 Impl 增加一个 clone
struct Widget::Impl {
    std::vector<int> data;
    std::string name;
    void do_work_impl() { /* ... */ }
    std::unique_ptr<Impl> clone() const {
        return std::make_unique<Impl>(*this);   // 深拷贝
    }
};
```

Then we implement the copy constructor and copy assignment for `Widget` manually, delegating to `clone()`:

```cpp
// widget.cpp
Widget::Widget(const Widget& other)
    : pImpl(other.pImpl ? other.pImpl->clone() : nullptr) {}

Widget& Widget::operator=(const Widget& other) {
    Widget tmp(other);          // 先拷一份临时对象
    swap(*this, tmp);           // 再和*this交换
    return *this;               // tmp 析构时自动释放旧 Impl
}
```

Here we use the classic **copy-and-swap** idiom: we first create a temporary object `tmp` using the copy constructor, then swap it with `*this`. When the function returns, `tmp` goes out of scope, and the old `Impl` is automatically destroyed. The benefit of this approach is **strong exception safety**—if `clone()` throws an exception, `*this` remains untouched and its state is unchanged; if `clone()` succeeds, the swap is simply a noexcept pointer swap, and the subsequent release of the old resource will not cause issues.

Note that `swap` is a `friend` function that simply swaps the `unique_ptr` on both sides, and it is also `noexcept`:

```cpp
// widget.h 里,作为 Widget 的友元
friend void swap(Widget& a, Widget& b) noexcept {
    using std::swap;
    swap(a.pImpl, b.pImpl);
}
```

::: warning Don't put the `inline` specifier for `swap` in the wrong place
Some older notes might write the friend `swap` as `friend void inline swap(...)`. While this compiles, placing `inline` after the return type is an archaic and discouraged style, and it can easily be confused with the `using std::swap;` idiom. The standard way is `friend void swap(...) noexcept`. Since a `friend` function defined inside the class body is implicitly `inline`, adding it is redundant.
:::

Let's verify this whole package (deep copy + move) together to see if the copied object is truly independent and if the source object is actually hollowed out after a move:

```sh
$ g++ -std=c++23 -O2 -pthread bridge_verify.cpp -o bridge_verify
$ ./bridge_verify
a.size after move = 0 (expect 0,被 move 走了)
b.size = 100, b[0] = 42 (expect 100, 42,深拷贝独立)
c.size = 100 (expect 100,move 接管)
```

After `a` is moved to `c` via `std::move`, `a`'s own `size` is 0—the source object has been hollowed out, which is exactly the semantics of move. Meanwhile, `b` is copied from `a` and has its own independent 100 `42`s; modifying `b` does not affect `c`, and vice versa—deep copy ensures true independence. Combined with `clone()` and copy-and-swap, pImpl fills in all the behaviors expected of value semantics.

## Step 4 of pImpl: Mark move as `noexcept` to make vector reallocation use moves

You now have a pImpl class that can copy and move. However, there is one final step before it is "ready to be dropped into `std::vector` without issues"—a step many people miss: **move constructors and move assignment operators must be marked `noexcept`**.

Why is this single keyword so critical? The reason is that when `std::vector` reallocates (for example, when `push_back` triggers a reallocation), it needs to move elements from the old memory to the new memory. At this point, it has a choice: move if possible, otherwise copy. However, vector's strong exception safety guarantee requires that "if an exception is thrown during the move, the original vector must remain unchanged"—but if a move throws an exception, the source object has already been hollowed out, breaking exception safety. Therefore, vector's strategy is: **it only dares to use move for relocation if the element's move constructor is `noexcept`; otherwise, it would rather copy** (if a copy throws, the source object still exists and can be rolled back).

Your pImpl class's move operation essentially just transfers a `unique_ptr` and will absolutely never throw an exception. However, **if you do not write `noexcept`, the vector is unaware of this and will dutifully perform copies**—calling `clone()` one by one, each time involving a heap allocation. Let's verify this directly; the difference is orders of magnitude:

```sh
$ ./bridge_verify
[noexcept move]    copy=0 move=1020 (扩容应纯走 move,copy=0)
[non-noexcept move] copy=1020 move=0 (无 noexcept,扩容走 copy,move=0)
```

Similarly, when inserting 1,000 elements into a `vector` and triggering multiple reallocations, the group marked with `noexcept` relies **purely on move operations, with zero copies**. The group without it does the opposite—the vector does not trust your move constructor, so every reallocation falls back to copying, resulting in 1,020 deep copies. For a pImpl class, this means 1,020 additional heap allocations. One keyword can cause a performance difference of an order of magnitude.

Therefore, the special member functions of a pImpl class should look like this:

```cpp
class Widget {
public:
    Widget();
    ~Widget();                          // 类外定义(Impl 完整)
    Widget(const Widget&);              // clone 深拷贝
    Widget& operator=(const Widget&);   // copy-and-swap
    Widget(Widget&&) noexcept;          // move 也必须类外定义 + noexcept
    Widget& operator=(Widget&&) noexcept;
    // ...
};
```

```cpp
// widget.cpp
Widget::Widget(Widget&&) noexcept = default;
Widget& Widget::operator=(Widget&&) noexcept = default;
```

The move constructor's `= default` must also be defined in the `.cpp` file. The rationale is identical to that of the destructor: it needs to move the `unique_ptr<Impl>`, which requires `Impl` to be a complete type.

## Wrapping Up: What Do We Actually Get from pImpl?

Let's assemble the previous steps to arrive at a production-ready pImpl idiom: the header contains only the forward declaration and a `unique_ptr`, while the destructor and move operations are moved to the `.cpp` file. Copy operations are implemented via `clone()` combined with copy-and-swap, and move operations are uniformly `noexcept`. This approach yields three tangible benefits.

The first is a **drastic reduction in compilation dependencies**. The header no longer includes heavy headers like `<vector>` or `<string>`. Any changes to the members of `Impl` are confined to the `widget.cpp` compilation unit, so the outside world never needs to recompile. In large projects, this benefit alone is enough to make you fall in love with pImpl.

The second is **ABI stability**. Let's verify a very intuitive fact—after applying pImpl, what is the actual size of the object?

```sh
$ ./bridge_verify2
sizeof(Widget)           = 8
sizeof(NaiveWidget)      = 56
sizeof(void*)            = 8
说明:Widget 压成一个指针大小,Impl 再怎么长,Widget 的 ABI 不变
```

In the naive implementation, `Widget` is 56 bytes (three pointers for `vector` plus one for `string`, totaling 32 bytes in libstdc++, summing to 56). After pImpl, `Widget` is compressed to 8 bytes—just a single pointer. This means that as long as your **public interface remains unchanged** (you can add or swap member types inside `Impl` at will), the binaries compiled by the consumer can link normally without recompilation—this is ABI stability. This is a critical requirement for teams shipping dynamic libraries (.so / .dll): you cannot require users to relink every time you modify an internal member.

The third benefit is **true encapsulation**. `private:` in a header file only blocks "access," not "visibility"—the types of private members are public to everyone. Whether you use `std::vector` or a custom container is fully visible. pImpl physically moves these details into the cpp file, blocking even "visibility," which is what we call thorough encapsulation.

We must also be clear about the costs: every member access incurs an extra pointer dereference (the bridge to pImpl); `Impl` must be allocated on the heap once; inline optimizations in the header for implementation functions are invalid (implementation is in cpp, cross-TU inlining requires LTO); and copying via `clone()` is a deep copy with allocation overhead. In the vast majority of cases, these costs are worth the benefits gained on the "compilation/release" side.

::: tip When to use pImpl
Not every class is worth pImpl. Here is a simple criterion: **Use pImpl when this class is an "interface/facade" included heavily across the project, when you want its binary ABI to remain stable across versions, or when you want to completely hide a set of heavy third-party dependencies (`<unordered_map>`, `<boost/...>`).** Conversely, for an implementation class used only within a single cpp, or a small type where performance is critical and members are frequently accessed via inlining, the indirection overhead of pImpl is not worth it. Measure first, then decide.
:::

## Bridge vs Adapter: Similar Looks, Completely Different Intentions

Having covered the Bridge, we have an unavoidable comparison: it looks incredibly similar to the Adapter pattern—both have a "delegate pointer" and wrap one object to forward calls to another. However, their **intent and design timing** are completely different, which is the only reliable standard for distinguishing them.

The Bridge is a **conscious separation of dimensions from the very start of the design**: You foresee that "Shape" and "Backend" will both evolve, so you split them into two separate inheritance chains from the beginning, letting the abstraction hold the implementation. Its keyword is "**preemptive separation, bi-directional independent extension**". The Adapter, on the other hand, is a **retroactive fix**: You already have an existing class (e.g., an old network library), and its interface doesn't match what the new system expects, so you wrap it in a shell to "translate" the old interface into the new one. Its keyword is "**post-hoc gluing, single-direction conversion**".

A simple criterion: **If you are designing to "allow two dimensions to extend independently," use Bridge; if you are patching to "connect an existing class to a new interface," use Adapter.** They look similar (both have a delegate pointer), but their design motivations are poles apart—don't confuse them.

## Summary

Let's review this evolutionary path:

| Stage | Approach | Why it falls short |
|---|---|---|
| Single Class Monolith | `OpenGLCircle` writes geometry and backend together | Adding a backend requires duplicating the entire class, class explosion |
| Abstract Implementation Interface | `Shape` holds `DrawingAPI`, two chains extend independently | **Bridge Pattern established**, two dimensions evolve independently |
| Raw Pointer pImpl | `Impl*` + manual `delete` | Memory management is tedious, copy/exception prone |
| `unique_ptr<Impl>` | RAII takes over, destructor/move defined out-of-class | Not copyable, lacks value semantics |
| clone + copy-and-swap | Deep copy pushed to `Impl`, strong exception safety | `move` isn't `noexcept`, vector resizing degrades to copy |
| Mark move as noexcept | Lets vector use move for relocation | **Production ready**, compilation dependency and ABI are stable |

Keep these key conclusions in mind:

- **The essence of Bridge is "splitting two independently evolving dimensions into two inheritance chains, bridged by a pointer"**; the number of classes drops from multiplication to addition, and pImpl is a special case of this for the "Interface × Implementation" dimension.
- **The pImpl header only keeps forward declarations and `unique_ptr<Impl>`**; all implementation details (including heavy headers) are moved into the cpp, trading for drastically reduced compilation dependencies and ABI stability.
- **Definitions of destructor, move constructor, and move assignment must be moved to the cpp**—because `unique_ptr`'s destructor requires a complete type, defining them in the header guarantees a compilation failure.
- **Copying relies on `Impl::clone()` + copy-and-swap; moving must be marked `noexcept`**, otherwise `vector` resizing won't dare to use move and will degrade to deep copy, resulting in an order-of-magnitude performance difference.
- **The difference between Bridge and Adapter lies in intent**: the former preemptively separates two dimensions, the latter retroactively adapts an interface in a single direction.

::: tip Companion Compilable Project
The examples for this section are in the repository `code/volumn_codes/vol4/design-patterns/Bridge/` as a complete compilable project (`.h` + main + `CMakeLists.txt`). Run `cmake -S . -B build && cmake --build build` to see the output described above.
:::

## Reference Resources

- [cppreference: `std::unique_ptr`](https://en.cppreference.com/w/cpp/memory/unique_ptr) (Since C++11, see *Notes* for incomplete type and destructor requirements)
- [cppreference: `std::make_unique`](https://en.cppreference.com/w/cpp/memory/unique_ptr/make_unique) (Since C++14)
- [cppreference: `std::move` and `noexcept` move semantics](https://en.cppreference.com/w/cpp/utility/move) (See [vector](https://en.cppreference.com/w/cpp/container/vector) notes for the `move_if_noexcept` mechanism during vector resizing)
- Herb Sutter, GotW #28/100: "The Fast PImpl Idiom" and "Compilation Firewalls" (Motivation and best practices for the pImpl compilation firewall)
- ISO C++ Core Guidelines: [C.133–C.139 / R.20–R.23](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines) (Class layout and smart pointer ownership)
