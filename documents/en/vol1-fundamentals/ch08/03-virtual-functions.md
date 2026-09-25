---
chapter: 8
cpp_standard:
- 11
- 14
- 17
- 20
description: Understand the virtual, override, and vtable mechanisms, and master how runtime polymorphism works and how to use it correctly.
difficulty: intermediate
order: 3
platform: host
prerequisites:
- Single Inheritance
reading_time_minutes: 12
tags:
- cpp-modern
- host
- intermediate
- 进阶
title: Virtual Functions and Polymorphism
translation:
  source: documents/vol1-fundamentals/ch08/03-virtual-functions.md
  source_hash: 50722144a7be915db1ceab543ccfb3c2b6048823c2c1c4763f1d29d3258046c3
  translated_at: '2026-09-25T11:27:39+00:00'
  engine: anthropic
  token_count: 2800
  notes: '原文一处明显笔误按正确拼写译出：GoodBase 示例输出注释中的「~BadBase()」原文疑为「~GoodBase()」（该输出属于 GoodBase/GoodDerived 层次，紧随其后的正文亦作 ~GoodBase()）。'
---
# Virtual Functions and Polymorphism: One Call, Different Implementations

In the previous article we covered single inheritance—a derived class inherits the base class's members and can extend new behavior on top of them. But inheritance by itself only solves half the problem: if we operate on a derived-class object through a base-class pointer and every call lands forever on the base-class version of the function, inheritance loses a big chunk of its expressive power. Virtual functions are the key that completes the other half: they make "calling the derived-class implementation through the base-class interface" possible. That is runtime polymorphism.

Today we sit down and get this thoroughly sorted out: what `virtual` actually does, why `override` should always be written, how the vtable the compiler builds behind the scenes operates, and what kind of disaster a forgotten `virtual` destructor can cause.

## A World Without virtual — The Base-Class Pointer's Nearsightedness

Let's face the problem head-on. Suppose we have a simple shape class hierarchy:

```cpp
#include <cstdio>

class Shape {
public:
    void draw() const { printf("Shape::draw()\n"); }
};

class Circle : public Shape {
public:
    void draw() const { printf("Circle::draw()\n"); }
};

class Rectangle : public Shape {
public:
    void draw() const { printf("Rectangle::draw()\n"); }
};
```

Three classes, and both `Circle` and `Rectangle` define their own `draw()`. Nothing looks wrong—until we make the call through a base-class pointer:

```cpp
int main() {
    Shape* shapes[3];
    shapes[0] = new Shape();
    shapes[1] = new Circle();
    shapes[2] = new Rectangle();

    for (int i = 0; i < 3; ++i) {
        shapes[i]->draw();
    }

    for (int i = 0; i < 3; ++i) {
        delete shapes[i];
    }
    return 0;
}
```

You'd expect three different drawing behaviors, but the actual output is:

```text
Shape::draw()
Shape::draw()
Shape::draw()
```

All three calls print `Shape::draw()`. When compiling `shapes[i]->draw()`, the compiler only sees that the static type of `shapes[i]` is `Shape*`, so it dutifully binds `Shape::draw()`. It neither knows nor cares whether the pointer actually points to a `Circle` or a `Rectangle` at runtime—this is **static binding** (also called early binding). When we need "one unified interface, different behaviors," static binding becomes the obstacle, and `virtual` is exactly the key that breaks through it.

## The virtual Keyword — Deferring the Call Decision to Runtime

Add `virtual` in front of the base class's member function, and everything changes:

```cpp
class Shape {
public:
    virtual void draw() const {   // virtual added
        printf("Shape::draw()\n");
    }
};

class Circle : public Shape {
public:
    void draw() const override {  // implicitly virtual
        printf("Circle::draw()\n");
    }
};

class Rectangle : public Shape {
public:
    void draw() const override {
        printf("Rectangle::draw()\n");
    }
};
```

With just one `virtual` in front of the base-class `draw()`, same-name functions with matching signatures in derived classes automatically become virtual too. Now let's run that same loop again:

The output becomes:

```text
Shape::draw()
Circle::draw()
Rectangle::draw()
```

Each object now calls the version of `draw()` matching its **actual type**—this is **dynamic binding** (also called late binding), i.e. **runtime polymorphism**. The core value of polymorphism: the caller doesn't need to know the object's concrete type, only what the object can do. This ability of "a unified interface with varied behaviors" is the foundation of decoupling in object-oriented design.

## The override Keyword (C++11): The Compiler Checks Signatures for Us

C++11 introduced the `override` keyword. It changes no runtime behavior, yet we **must add it** whenever we write a virtual-function override. The reason is simple: it forces the compiler to check whether we really did correctly override the base class's virtual function.

Let's look at the classic faceplant that happens without `override`:

```cpp
class Shape {
public:
    virtual void draw() const { printf("Shape::draw()\n"); }
};

class Circle : public Shape {
public:
    void draw() {   // forgot the const! Signature mismatch, not an override
        printf("Circle::draw()\n");
    }
};
```

Look closely at the signature of `Circle::draw()`: the `const` is missing. It differs from the base class's `virtual void draw() const`, so the compiler treats it as an ordinary member function newly added by `Circle`, with nothing to do with `Shape::draw()`. Calling `draw()` through a base-class pointer goes through static binding and still invokes `Shape::draw()`. The scariest part: this code **compiles completely clean, without a single warning**. The author's blood pressure has spiked over this more than once.

Once we add `override`, the compiler immediately drags the same problem out into the light:

```cpp
class Circle : public Shape {
public:
    void draw() override {   // Compile error! Signature mismatch
        printf("Circle::draw()\n");
    }
};
```

```text
error: 'void Circle::draw()' marked 'override', but does not override any base class virtual function
```

The compiler tells us plainly: we claimed to be overriding a base-class virtual function, but the signatures don't match. Errors `override` can catch include but are not limited to: the base class has no virtual function with that name at all, the function signature doesn't match (`const`, reference qualifiers, and other differences), and the base-class function isn't `virtual`. So the rule is clear: **whenever you are overriding a virtual function, always write `override`**.

Leaving off `override` won't produce an error, but one wrong signature is a disaster. Make it a habit: add `override` to every virtual-function override, without exception.

## Inside the vtable — The Springboard Behind Polymorphism

Now that we understand what `virtual` does, let's look at what the compiler does behind the scenes. For every class containing virtual functions, the compiler generates a **virtual table** (vtable for short)—essentially an array of function pointers, one entry per virtual function, storing the address of **this class's** actual implementation of that virtual function.

For our shape class hierarchy, the compiler roughly generates three vtables:

![vtable layout of the shape class hierarchy](./03-virtual-functions-vtable.drawio)

And every object of a class with virtual functions carries one extra hidden member in its memory layout—the **vtable pointer** (vptr), which points to the vtable of the class that object belongs to.

When we write `shapes[i]->draw()`, the compiler-generated code roughly performs these steps: find the `vptr` through the object, locate the corresponding vtable, fetch the function-pointer entry for `draw()` from the table, and finally make an indirect call through that pointer:

![How a virtual function call works](./03-virtual-functions-call.drawio)

That is the entire overhead a virtual call adds over an ordinary function call—**one extra indirect jump**. On a PC this cost is nearly negligible. But in resource-tight embedded environments we need to take it seriously: every class with virtual functions carries an extra vtable (consuming Flash), every object carries an extra `vptr` (usually 4 or 8 bytes, consuming RAM), and every virtual call adds an indirect jump (which can affect pipelining and branch prediction). Fortunately, in the vast majority of scenarios this overhead is trivial compared with the architectural payoff that decoupling brings.

These two steps of indirection have been turned into an animation—you can play it, pause it, or step through it with the step button to see clearly which `draw()` each of `shapes[1]` and `shapes[2]` lands on:

<Anim id="vtable-dispatch" />

On an MCU with only a few KB of RAM, one extra `vptr` per object can be fatal. If your system needs to create large numbers of small objects (sensor sample data points, for example), seriously evaluate polymorphism's memory cost.

## Virtual Destructors — The Last Line of Defense for Polymorphism

One detail of polymorphic usage is often overlooked, but overlooking it invites **undefined behavior**: when we intend to `delete` a derived-class object through a base-class pointer, the base class's destructor must be `virtual`.

First, the cautionary tale:

```cpp
class BadBase {
public:
    ~BadBase() { printf("~BadBase()\n"); }   // non-virtual destructor
};

class BadDerived : public BadBase {
    int* data_;
public:
    BadDerived() : data_(new int[100]) {}
    ~BadDerived() { delete[] data_; printf("~BadDerived(): released\n"); }
};

BadBase* p = new BadDerived();
delete p;    // only ~BadBase() is called; ~BadDerived() is skipped!
```

The output shows only `~BadBase()`; `~BadDerived()` is never called at all, and the 400 bytes behind `data_` leak outright. The cause is the same as before: at `delete p`, the compiler sees that `p`'s static type is `BadBase*`, `~BadBase()` is not virtual, so the call statically binds to the base-class destructor, and the derived class's destruction logic is skipped entirely.

The fix is dead simple: mark the base-class destructor `virtual`:

```cpp
class GoodBase {
public:
    virtual ~GoodBase() = default;   // virtual destructor
};
```

Now run the same operation again:

```cpp
GoodBase* p = new GoodDerived();
delete p;
// Output:
// ~GoodDerived(): data_ released
// ~GoodBase()
```

The destruction order is now correct: `~GoodDerived()` first, then `~GoodBase()`, with resources fully released. We used `= default` here because the base-class destructor itself has no special cleanup to do. The key is that `virtual`—it lets the `delete` operation go through dynamic binding too.

So here is a rule that must be followed: **if a class has any virtual function at all, its destructor must be declared `virtual`**. Conversely, if a class has no virtual functions and isn't meant to be inherited from, a non-virtual destructor is perfectly fine. But once you start doing polymorphic design, this is not the place to get sloppy.

Non-virtual destructor + deleting a derived-class object through a base-class pointer = undefined behavior. In embedded systems this usually shows up as "mysterious memory leaks" or "peripherals in a weird state," and it is exceptionally hard to track down. The moment we see virtual functions, immediately check whether the destructor is virtual too.

## Practice: A Polymorphic Shape System

Now let's string the previous pieces together and write a complete polymorphic shape system. This example shows how virtual functions work in real code.

```cpp
#include <cstdio>
#include <vector>

// Abstract base class
class Shape {
public:
    virtual void draw() const = 0;           // pure virtual function
    virtual double area() const = 0;         // pure virtual function
    virtual ~Shape() = default;              // virtual destructor

    const char* name() const { return name_; }

protected:
    const char* name_;   // set by derived classes at construction
};

// Circle
class Circle : public Shape {
private:
    double radius_;

public:
    explicit Circle(double r) : radius_(r) { name_ = "Circle"; }

    void draw() const override {
        printf("  Drawing Circle (r=%.2f)\n", radius_);
    }

    double area() const override {
        return 3.14159265 * radius_ * radius_;
    }
};

// Rectangle
class Rectangle : public Shape {
private:
    double width_;
    double height_;

public:
    Rectangle(double w, double h) : width_(w), height_(h) { name_ = "Rectangle"; }

    void draw() const override {
        printf("  Drawing Rectangle (%.2f x %.2f)\n", width_, height_);
    }

    double area() const override {
        return width_ * height_;
    }
};

// Triangle
class Triangle : public Shape {
private:
    double base_;
    double height_;

public:
    Triangle(double b, double h) : base_(b), height_(h) { name_ = "Triangle"; }

    void draw() const override {
        printf("  Drawing Triangle (base=%.2f, height=%.2f)\n", base_, height_);
    }

    double area() const override {
        return 0.5 * base_ * height_;
    }
};
```

Note the design of `Shape`: `draw()` and `area()` are pure virtual functions (`= 0`), meaning `Shape` itself cannot be instantiated—any class that wants to qualify as a "legitimate shape" must provide its own implementations. The destructor is declared `virtual ... = default`, which guarantees polymorphic safety without hand-written cleanup logic. `name_` sits in the `protected` section so that derived classes can set it in their constructors.

Then in `main()` we create a set of different shapes and operate on them through one unified interface:

```cpp
int main() {
    // Store all shapes in a vector of base-class pointers
    std::vector<Shape*> shapes;
    shapes.push_back(new Circle(3.0));
    shapes.push_back(new Rectangle(4.0, 5.0));
    shapes.push_back(new Triangle(6.0, 2.0));
    shapes.push_back(new Circle(1.5));

    printf("=== Drawing all shapes ===\n");
    for (auto* s : shapes) {
        s->draw();   // Polymorphism: calls draw() on the actual type
    }

    printf("\n=== Areas ===\n");
    double total = 0.0;
    for (auto* s : shapes) {
        double a = s->area();
        printf("  %-12s: %.4f\n", s->name(), a);
        total += a;
    }
    printf("  Total area: %.4f\n", total);

    // Cleanup — the virtual destructor ensures each derived class is released correctly
    for (auto* s : shapes) {
        delete s;
    }
    return 0;
}
```

The output:

```text
=== Drawing all shapes ===
  Drawing Circle (r=3.00)
  Drawing Rectangle (4.00 x 5.00)
  Drawing Triangle (base=6.00, height=2.00)
  Drawing Circle (r=1.50)

=== Areas ===
  Circle       : 28.2743
  Rectangle    : 20.0000
  Triangle     : 6.0000
  Circle       : 7.0686
  Total area: 61.3429
```

The whole loop depends only on the `Shape` interface and has no idea which concrete types the container holds. If we later want to add a `Pentagon` class, we just inherit from `Shape`, implement `draw()` and `area()`, and drop it into the container—**not a single line of the main loop changes**. That is the extensibility polymorphism brings.

## Exercises

### Exercise 1: Polymorphic Document Printing

Design a document class hierarchy: the base class `Document` has a pure virtual function `void print() const` and a virtual destructor; derive `TextDocument` (prints the text content), `ImageDocument` (prints a description of the image), and `PdfDocument` (prints the page count and author). In `main()`, create documents of the different types, store them in a `vector<Document*>`, iterate and call `print()`, and verify that every type outputs its own content.

### Exercise 2: Verifying the Virtual Destructor

Building on Exercise 1, add a `printf` to each derived class's destructor. First clean up normally (`delete` every pointer) and observe the destruction order. Then remove `virtual` from the base-class destructor and run it again to see what changes—you will watch with your own eyes as the derived-class destructors get skipped.
