---
chapter: 8
cpp_standard:
- 11
- 14
- 17
- 20
description: Understand `virtual`, `override`, and the vtable mechanism, and master
  the implementation principles and correct usage of runtime polymorphism.
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 单继承
reading_time_minutes: 12
tags:
- cpp-modern
- host
- intermediate
- 进阶
title: Virtual Functions and Polymorphism
translation:
  engine: anthropic
  source: documents/vol1-fundamentals/ch08/02-virtual-functions.md
  source_hash: 256898e67ae8b8da6b8ed28822fcc1a071f732d37e8bdc64f974442388343556
  token_count: 2293
  translated_at: '2026-05-26T10:54:21.144336+00:00'
---
# Virtual Functions and Polymorphism

In the previous chapter, we covered single inheritance—a derived class inherits members from a base class and can extend them with new behaviors. But inheritance alone only solves half the problem: if we use a base class pointer to manipulate a derived class object, we always end up calling the base class version of the function, which severely limits the expressiveness of inheritance. Virtual functions are the key to completing the other half—they make it possible to "call a derived class implementation through a base class interface," and this is known as runtime polymorphism.

Today, we are going to sit down and thoroughly understand this: what exactly `virtual` does, why you should always write `override`, how the vtable the compiler sets up behind the scenes actually works, and what kind of disaster strikes when you forget to write a `virtual` destructor.

## A World Without virtual — The "Nearsightedness" of Base Class Pointers

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

Three classes, and both `Circle` and `Rectangle` define their own `draw()`. This looks fine—but when we call through a base class pointer, things go wrong:

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

You expect to see three different drawing behaviors, but the actual output is:

```text
Shape::draw()
Shape::draw()
Shape::draw()
```

All three outputs are `Shape::draw()`. When compiling `shapes[i]->draw()`, the compiler only sees that the static type of `shapes[i]` is `Shape*`, so it dutifully binds to `Shape::draw()`. It has no idea, nor does it care, whether this pointer actually points to a `Circle` or a `Rectangle` at runtime—this is **static binding** (also known as early binding). When we need "a unified interface with different behaviors," static binding becomes a stumbling block, and `virtual` is exactly the key to breaking through it.

## The virtual Keyword — Making Function Calls "Wait Until Runtime"

Adding `virtual` in front of a base class member function changes everything:

```cpp
class Shape {
public:
    virtual void draw() const {   // 加上 virtual
        printf("Shape::draw()\n");
    }
};

class Circle : public Shape {
public:
    void draw() const override {  // 隐式虚函数
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

You only need to add `virtual` before `draw()` in the base class, and any matching function with the same signature in the derived class automatically becomes a virtual function, too. Now let's run that loop again:

The output becomes:

```text
Shape::draw()
Circle::draw()
Rectangle::draw()
```

Each object calls the corresponding version of `draw()` based on its **actual type**—this is **dynamic binding** (also known as late binding), which is **runtime polymorphism**. The core value of polymorphism lies in this: the caller doesn't need to know the concrete type of the object, it only needs to know "what this object can do." This ability to have "a unified interface with diverse behaviors" is the cornerstone of decoupling in object-oriented design.

## The override Keyword (C++11) — The "Seatbelt" the Compiler Watches For You

C++11 introduced the `override` keyword. It doesn't change any runtime behavior, but it is something you **must add** when overriding virtual functions. The reason is simple: it forces the compiler to check whether you have actually and correctly overridden a base class virtual function.

Let's look at a classic pitfall scenario without `override`:

```cpp
class Shape {
public:
    virtual void draw() const { printf("Shape::draw()\n"); }
};

class Circle : public Shape {
public:
    void draw() {   // 忘了 const！签名不匹配，不是重写
        printf("Circle::draw()\n");
    }
};
```

Notice the signature of `Circle::draw()`—it's missing `const`. This differs from the signature of the base class's `virtual void draw() const`, so the compiler considers this to be a brand-new ordinary member function belonging to `Circle` itself, completely unrelated to `Shape::draw()`. When calling `draw()` through a base class pointer, it follows static binding and still calls `Shape::draw()`. The most terrifying part is: this code **compiles perfectly with no warnings whatsoever**. I've had my blood pressure spike over this more than once.

With `override` added, the exact same problem is immediately caught by the compiler:

```cpp
class Circle : public Shape {
public:
    void draw() override {   // 编译错误！签名不匹配
        printf("Circle::draw()\n");
    }
};
```

```text
error: 'void Circle::draw()' marked 'override', but does not override any base class virtual function
```

The compiler explicitly tells you: you claim to be overriding a base class virtual function, but the signatures don't match. Errors that `override` can catch include but are not limited to: the virtual function doesn't even exist in the base class, function signature mismatches (differences in `const`, reference qualifiers, etc.), and the base class function not being `virtual`. So the iron rule is—**whenever you override a virtual function, always write `override`**.

> **Pitfall Warning**: Not adding `override` won't cause an error, but a wrong signature is a disaster. Make it a habit: add `override` to every virtual function override, treating it as a mandatory action just like buckling a seatbelt.

## Demystifying the vtable — The Springboard Behind Polymorphism

After understanding the effect of `virtual`, let's look at what the compiler does behind the scenes. For every class that contains virtual functions, the compiler generates a **virtual table** (vtable)—essentially an array of function pointers where each entry corresponds to a virtual function and stores the address of **this class's** actual implementation of that virtual function.

Taking our shape class hierarchy as an example, the compiler roughly generates three vtables:

![vtable layout of the shape class hierarchy](./02-virtual-functions-vtable.drawio)

And every object that contains virtual functions has an extra hidden member in its memory layout—the **virtual table pointer** (vptr)—which points to the vtable of the object's class.

When you write `shapes[i]->draw()`, the code generated by the compiler roughly does the following: first, it finds the `vptr` through the object, locates the corresponding vtable, then retrieves the function pointer for `draw()` from the table, and finally makes an indirect call through this pointer:

![Diagram of the virtual function call process](./02-virtual-functions-call.drawio)

This is the entire overhead that a virtual function call adds compared to a normal function call—**one extra indirect jump**. On a PC, this overhead is almost negligible. But in resource-constrained embedded environments, it needs to be taken seriously: every class with virtual functions adds an extra vtable (consuming Flash), every object adds an extra `vptr` (usually 4 or 8 bytes, consuming RAM), and every virtual function call adds an extra indirect jump (potentially affecting the pipeline and branch prediction). Fortunately, in the vast majority of scenarios, these costs are trivial compared to the "architectural benefits gained from decoupling."

> **Pitfall Warning**: On an MCU with only a few KB of RAM, adding an extra `vptr` to every object can be fatal. If your system needs to create a large number of small objects (such as sensor sampling data points), carefully evaluate the memory overhead of polymorphism.

## Virtual Destructors — The Last Line of Defense for Polymorphism

There is a detail in the use of polymorphism that is often overlooked, but ignoring it leads to **undefined behavior**: when you intend to `delete` a derived class object through a base class pointer, the base class's destructor must be `virtual`.

Let's look at a counterexample first:

```cpp
class BadBase {
public:
    ~BadBase() { printf("~BadBase()\n"); }   // 非虚析构
};

class BadDerived : public BadBase {
    int* data_;
public:
    BadDerived() : data_(new int[100]) {}
    ~BadDerived() { delete[] data_; printf("~BadDerived(): released\n"); }
};

BadBase* p = new BadDerived();
delete p;    // 只调用了 ~BadBase()，~BadDerived() 被跳过！
```

The output is only `~BadBase()`; `~BadDerived()` is never called at all, and the 400 bytes of memory corresponding to `data_` leak directly. The reason is the same as before: when `delete p`, the compiler sees that the static type of `p` is `BadBase*`, and since `~BadBase()` is not a virtual function, it statically binds to the base class's destructor. The derived class's destruction logic is completely skipped.

The solution is very simple—add `virtual` to the base class destructor:

```cpp
class GoodBase {
public:
    virtual ~GoodBase() = default;   // 虚析构函数
};
```

Now execute the same operation again:

```cpp
GoodBase* p = new GoodDerived();
delete p;
// 输出：
// ~GoodDerived(): data_ released
// ~GoodBase()
```

The destruction order is correct: first `~GoodDerived()`, then `~GoodBase()`, and resources are fully released. Here we use `= default` because the base class destructor itself doesn't have any special cleanup work to do. The key is that `virtual`—it allows the `delete` operation to use dynamic binding, too.

So there is an iron rule: **as long as a class has any virtual functions, its destructor must be declared as `virtual`**. Conversely, if a class has no virtual functions and is not intended to be inherited from—then a non-virtual destructor is perfectly fine. But once you start designing for polymorphism, there is no room for ambiguity on this.

> **Pitfall Warning**: Non-virtual destructor + deleting a derived class object through a base class pointer = undefined behavior. In embedded systems, this usually manifests as "inexplicable memory leaks" or "abnormal peripheral states," and it is extremely difficult to track down. When you see virtual functions, immediately check whether the destructor is also virtual.

## Practical Exercise — A Polymorphic Shape System

Now let's tie together what we've learned and write a complete polymorphic shape system. This example demonstrates how virtual functions work in real code.

```cpp
#include <cstdio>
#include <vector>

// 抽象基类
class Shape {
public:
    virtual void draw() const = 0;           // 纯虚函数
    virtual double area() const = 0;         // 纯虚函数
    virtual ~Shape() = default;              // 虚析构函数

    const char* name() const { return name_; }

protected:
    const char* name_;   // 派生类在构造时设置
};

// 圆形
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

// 矩形
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

// 三角形
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

Notice the design of `Shape`: `draw()` and `area()` are pure virtual functions (`= 0`), meaning `Shape` itself cannot be instantiated, and any class that wants to be a "valid shape" must provide its own implementations. The destructor is declared as `virtual ... = default`, ensuring polymorphic safety without needing to manually write cleanup logic. `name_` is placed in the `protected` section so that derived classes can set it in their constructors.

Then, in `main()`, we create a group of different shapes and manipulate them through a unified interface:

```cpp
int main() {
    // 用基类指针的 vector 存储所有图形
    std::vector<Shape*> shapes;
    shapes.push_back(new Circle(3.0));
    shapes.push_back(new Rectangle(4.0, 5.0));
    shapes.push_back(new Triangle(6.0, 2.0));
    shapes.push_back(new Circle(1.5));

    printf("=== Drawing all shapes ===\n");
    for (auto* s : shapes) {
        s->draw();   // 多态：调用实际类型的 draw()
    }

    printf("\n=== Areas ===\n");
    double total = 0.0;
    for (auto* s : shapes) {
        double a = s->area();
        printf("  %-12s: %.4f\n", s->name(), a);
        total += a;
    }
    printf("  Total area: %.4f\n", total);

    // 清理——虚析构函数确保每个派生类正确释放
    for (auto* s : shapes) {
        delete s;
    }
    return 0;
}
```

Runtime result:

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

The entire loop relies only on the `Shape` interface, with no knowledge of what concrete types are inside the container. In the future, if we want to add a `Pentagon` class, we just need to inherit from `Shape`, implement `draw()` and `area()`, and toss it into the container—**we don't need to change a single line of code in the main loop**. This is the extensibility that polymorphism brings.

## Exercises

1. **Polymorphic Document Printing**: Design a document class hierarchy. The base class `Document` has a pure virtual function `void print() const` and a virtual destructor. Derive `TextDocument` (prints text content), `ImageDocument` (prints image description information), and `PdfDocument` (prints page count and author). In `main()`, create different types of documents, store them in a `vector<Document*>`, iterate and call `print()`, and verify that each type outputs its own content.

2. **Verify Virtual Destructors**: Building on Exercise 1, add a `printf` output to each derived class's destructor. First, clean up normally (`delete` each pointer) and observe the destruction order. Then, remove the `virtual` from the base class destructor and run it again to see what changes—you will witness firsthand the process of derived class destructors being skipped.

## Summary

In this chapter, we thoroughly broke down runtime polymorphism around virtual functions. Without `virtual`, a base class pointer can only statically bind to the base class's function implementation—this is the root cause for many beginners who write inheritance but find that "polymorphism doesn't work." The `virtual` keyword turns function calls into dynamic binding, deciding which version to call based on the actual type of the object. `override` is the seatbelt C++11 gave us—always add it after every virtual function override, and let the compiler check whether the signature truly matches. The virtual destructor is the safety baseline for using polymorphism; forgetting it means that when deleting a derived class object through a base class pointer `delete`, the derived class's destruction logic is skipped, resulting in resource leaks or undefined behavior.

At the underlying mechanism level, the compiler achieves all of this through the vtable and vptr: each class has a vtable storing function pointers, each object has a vptr pointing to its class's vtable, and a virtual function call is completed through this indirect springboard. The overhead is small, but in extremely resource-constrained embedded scenarios, we need to keep it in mind.

In the next chapter, we will dive into abstract classes and pure virtual functions—pushing polymorphism toward a more rigorous design level, using "capability contracts" to constrain what behaviors a derived class must provide.
