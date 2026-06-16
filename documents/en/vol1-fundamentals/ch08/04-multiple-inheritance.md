---
chapter: 8
cpp_standard:
- 11
- 14
- 17
- 20
description: Understand the syntax of multiple inheritance, the diamond inheritance
  problem, and the solution using virtual inheritance, and learn to use multiple inheritance
  judiciously.
difficulty: intermediate
order: 4
platform: host
prerequisites:
- 抽象类与接口
reading_time_minutes: 9
tags:
- cpp-modern
- host
- intermediate
- 进阶
title: Multiple Inheritance and Virtual Inheritance
translation:
  source: documents/vol1-fundamentals/ch08/04-multiple-inheritance.md
  source_hash: 2cfb7763eeefbc861e9763e6ea88c3a78887d8e6dc56721c5db746f2ac667386
  translated_at: '2026-06-16T03:45:13.140095+00:00'
  engine: anthropic
  token_count: 2107
---
# Multiple Inheritance and Virtual Inheritance

In previous chapters, we focused on single inheritance—where a class has only one direct base class. This covers the vast majority of object-oriented design needs. However, C++ also allows a class to inherit from multiple base classes simultaneously; this is known as multiple inheritance. Multiple inheritance is powerful but highly controversial—used well, it makes designs more flexible; used poorly, it renders the entire inheritance hierarchy unmaintainable. (Therefore, the author prefers composition.)

In this chapter, we will clarify the syntax of multiple inheritance, the diamond inheritance problem, the solution provided by virtual inheritance, and when to turn to safer alternatives.

> **Learning Objectives**
>
> After completing this chapter, you will be able to:
>
> - [ ] Use multiple inheritance syntax to equip a class with multiple capabilities.
> - [ ] Identify and resolve name ambiguities caused by multiple inheritance.
> - [ ] Understand the cause of the diamond inheritance problem and its impact on object layout.
> - [ ] Use virtual inheritance to solve the diamond inheritance problem and understand its costs.
> - [ ] Make sound engineering judgments between "multiple inheritance" and "composition/interface delegation."

## Environment Setup

All code is compiled and run in the following environment:

- Platform: Linux x86_64 (WSL2 is also acceptable)
- Compiler: GCC 13+ or Clang 17+
- Compiler flags: `-Wall -Wextra -std=c++17`

## Step 1 — Basic Syntax and Use Cases for Multiple Inheritance

The syntax for multiple inheritance is not complex: a class lists multiple base classes in its inheritance list, separated by commas. The derived class object will contain subobjects for all base classes and must implement the interfaces of all pure virtual base classes.

```cpp
class Printable {
public:
    virtual ~Printable() = default;
    virtual void print() const = 0;
};

class Serializable {
public:
    virtual ~Serializable() = default;
    virtual std::string serialize() const = 0;
};

// 同时可打印、可序列化的配置项
class ConfigItem : public Printable, public Serializable {
private:
    std::string key_;
    std::string value_;

public:
    ConfigItem(std::string key, std::string value)
        : key_(std::move(key)), value_(std::move(value))
    {
    }

    void print() const override
    {
        printf("%s = %s\n", key_.c_str(), value_.c_str());
    }

    std::string serialize() const override
    {
        return "\"" + key_ + "\":\"" + value_ + "\"";
    }
};
```

After creating an object, we can manipulate it through any base class pointer: `Printable* p = &item; p->print();` or `Serializable* s = &item; s->serialize();`. The construction order follows the declaration order of the inheritance list, while destruction occurs in the reverse order.

When two base classes have members with the same name, the compiler reports an ambiguity error. We must use `obj.BaseA::foo()` to explicitly resolve the ambiguity. The safest use of multiple inheritance is **interface inheritance**: all base classes are pure virtual interfaces containing no data members or concrete implementations. **If you find yourself trying to reuse code implementation via multiple inheritance rather than expressing the semantics of "having multiple capabilities," you should probably consider composition.**

## Step 2 — The Diamond Inheritance Problem

The classic pitfall in multiple inheritance is diamond inheritance—a base class is inherited by two intermediate classes, and a final class inherits from both intermediate classes, forming a diamond. Without special handling, the final object will contain **two copies** of the common base class subobject. Let's look at a concrete example:

```cpp
class Device {
public:
    int id;
    Device() : id(0) {}
};

class InputDevice : public Device {};
class OutputDevice : public Device {};

class TouchScreen : public InputDevice, public OutputDevice
{
};

TouchScreen ts;
// ts.id = 1;          // 编译错误：歧义！
ts.InputDevice::id = 1;
ts.OutputDevice::id = 2;  // 两份独立的 id，互不影响
```

The constructor for `Device` is called twice, and `id` consists of two independent copies. A touchscreen device should have only one ID. Even worse is data inconsistency—in large systems, this kind of "desynchronized state within a single logical object" is the source of extremely hard-to-track bugs.

## Step 3 — Solving the Diamond Problem with Virtual Inheritance

The solution C++ provides is **virtual inheritance**: we add the `virtual` keyword when the intermediate classes inherit from the common base class:

```cpp
class InputDevice : virtual public Device {};
class OutputDevice : virtual public Device {};

class TouchScreen : public InputDevice, public OutputDevice
{
public:
    // 虚继承下，最底层的派生类负责初始化虚基类
    TouchScreen() : Device(), InputDevice(), OutputDevice() {}
};
```

Now `Device` is constructed only once, `id` exists in a single copy, and ambiguity is resolved. However, virtual inheritance is never a free lunch—the object layout introduces additional virtual base table pointers (vbptr), `sizeof(TouchScreen)` grows from 8 bytes to approximately 24 bytes, and accessing members of the virtual base class requires extra indirect addressing.

> **Pitfall Warning #1**: Construction of the virtual base class is the responsibility of the **most derived class**. Initialization lists for the virtual base class in intermediate class constructors are silently ignored. If you aren't aware of this rule, you might spend hours debugging, wondering, "I passed the parameters in the intermediate class, why didn't it take effect?"
>
> **Pitfall Warning #2**: Virtual inheritance must appear on **all** intermediate classes that directly inherit the common base class. Making only one use `virtual` while the other doesn't will not solve the diamond problem. The compiler won't error, but you will still end up with two copies of the base class subobject.
>
> **Pitfall Warning #3**: The object layout of virtual inheritance differs from normal inheritance. Using `reinterpret_cast` or C-style casts on virtual inheritance objects is extremely dangerous. `static_cast` crossing virtual base class boundaries may require `this` pointer offset adjustments. If you need to serialize objects into byte streams, virtual inheritance makes things very tricky.

## Step 4 — Alternatives to Multiple Inheritance

Given the complexity of multiple inheritance, especially virtual inheritance, we often have better choices in many scenarios.

**Composition over inheritance** is one of the classic principles of object-oriented design. If a class needs multiple capabilities but doesn't require unified manipulation through base class pointers, directly holding member objects is often clearer than inheritance—hold `Printer` and `JsonSerializer` as member variables instead of inheriting from them as bases. If runtime polymorphism is indeed needed, the **delegation-to-interface pattern** is a more controllable choice than multiple inheritance: define an interface class, and internally delegate to a concrete implementation via a pointer.

In summary, as long as the base classes are pure virtual interfaces (no data members, no implementation), the complexity of multiple inheritance can be kept within a manageable range. **If data members or concrete method implementations appear in your multiple inheritance base classes, please stop immediately and re-examine your design.**

## Hands-on Verification — multi_inherit.cpp

Below is a complete, compilable example covering multiple interface inheritance and diamond inheritance:

```cpp
// multi_inherit.cpp
// 编译：g++ -Wall -Wextra -std=c++17 -o multi_inherit multi_inherit.cpp

#include <cstdio>
#include <string>

// --- 接口多继承 ---
class Drawable {
public:
    virtual ~Drawable() = default;
    virtual void draw() const = 0;
};

class Serializable {
public:
    virtual ~Serializable() = default;
    virtual std::string serialize() const = 0;
};

class Shape : public Drawable, public Serializable {
protected:
    std::string name_;
public:
    explicit Shape(std::string name) : name_(std::move(name)) {}
};

class Circle : public Shape {
    double radius_;
public:
    explicit Circle(double r) : Shape("Circle"), radius_(r) {}
    void draw() const override {
        printf("[Draw] %s (r=%.2f)\n", name_.c_str(), radius_);
    }
    std::string serialize() const override {
        return "{\"type\":\"circle\",\"r\":" + std::to_string(radius_) + "}";
    }
};

class Rectangle : public Shape {
    double w_, h_;
public:
    Rectangle(double w, double h) : Shape("Rect"), w_(w), h_(h) {}
    void draw() const override {
        printf("[Draw] %s (%.2fx%.2f)\n", name_.c_str(), w_, h_);
    }
    std::string serialize() const override {
        return "{\"type\":\"rect\",\"w\":" + std::to_string(w_) +
               ",\"h\":" + std::to_string(h_) + "}";
    }
};

// --- 菱形继承（非虚） ---
class Component {
public:
    int version;
    Component() : version(1) { printf("  Component()\n"); }
};

class Renderer : public Component {
public:
    Renderer() { printf("  Renderer()\n"); }
    void render() { printf("  Render (v=%d)\n", version); }
};

class EventHandler : public Component {
public:
    EventHandler() { printf("  EventHandler()\n"); }
    void click() { printf("  Click (v=%d)\n", version); }
};

class Widget : public Renderer, public EventHandler {
public:
    Widget() { printf("  Widget()\n"); }
};

// --- 菱形继承（虚继承） ---
class VComponent {
public:
    int version;
    VComponent() : version(1) { printf("  VComponent()\n"); }
};

class VRenderer : virtual public VComponent {
public:
    VRenderer() { printf("  VRenderer()\n"); }
    void render() { printf("  Render (v=%d)\n", version); }
};

class VEventHandler : virtual public VComponent {
public:
    VEventHandler() { printf("  VEventHandler()\n"); }
    void click() { printf("  Click (v=%d)\n", version); }
};

class VWidget : public VRenderer, public VEventHandler {
public:
    VWidget() : VComponent(), VRenderer(), VEventHandler() {
        printf("  VWidget()\n");
    }
};

int main()
{
    printf("=== Interface Multi-Inheritance ===\n");
    Circle c(5.0);
    Rectangle r(3.0, 4.0);
    Drawable* drawables[] = {&c, &r};
    for (auto* d : drawables) { d->draw(); }

    printf("\n=== Diamond (no virtual) ===\n");
    Widget w;
    w.Renderer::version = 2;
    w.EventHandler::version = 3;
    printf("  sizeof(Widget) = %zu\n", sizeof(Widget));

    printf("\n=== Diamond (virtual) ===\n");
    VWidget vw;
    vw.version = 42;  // OK！只有一份
    printf("  sizeof(VWidget) = %zu\n", sizeof(VWidget));

    return 0;
}
```

Compile and run:

```text
$ g++ -Wall -Wextra -std=c++17 -o multi_inherit multi_inherit.cpp
$ ./multi_inherit
=== Interface Multi-Inheritance ===
[Draw] Circle (r=5.00)
[Draw] Rect (3.00x4.00)

=== Diamond (no virtual) ===
  Component()
  Renderer()
  Component()
  EventHandler()
  Widget()
  sizeof(Widget) = 8

=== Diamond (virtual) ===
  VComponent()
  VRenderer()
  VEventHandler()
  VWidget()
  sizeof(VWidget) = 24
```

Compare the two sets of outputs: in non-virtual inheritance, `Component` is constructed twice, and the two copies of `version` change independently; in virtual inheritance, `VComponent` is constructed only once, and `version` is unified. Also note the difference in `sizeof`—virtual inheritance introduces additional pointer overhead.

## Exercises

### Exercise 1: Multiple Interface Implementation

Design a `LogEntry` class that simultaneously implements three pure virtual interfaces: `IPrintable` (`void print() const`), `ISerializable` (`std::string to_string() const` returns JSON), and `IFilterable` (`bool matches(const std::string& keyword) const`). `LogEntry` contains three fields: `timestamp` (integer), `level` (e.g., "INFO"), and `message` (string). Create several log entries and manipulate them through the three base class pointers respectively.

### Exercise 2: Fix Diamond Inheritance

The following code has a diamond inheritance problem. Please use virtual inheritance to fix it, ensuring that `SmartDevice` contains only one `Device` subobject:

```cpp
class Device {
public:
    int device_id;
    Device(int id) : device_id(id) {}
};

class Networkable : public Device {
public:
    Networkable(int id) : Device(id) {}
    virtual void connect() = 0;
};

class Monitorable : public Device {
public:
    Monitorable(int id) : Device(id) {}
    virtual int read_status() = 0;
};

class SmartDevice : public Networkable, public Monitorable
{
public:
    SmartDevice(int id) : Networkable(id), Monitorable(id) {}
    void connect() override { printf("Connected\n"); }
    int read_status() override { return device_id; }  // 歧义！
};
```

Hint: After modification, don't forget to directly initialize the virtual base class `Device` in the `SmartDevice` constructor's initialization list.

## Summary

Multiple inheritance is a powerful type composition mechanism, but it must be used with caution. In this chapter, we mastered three key judgments: multiple interface inheritance (base classes are all pure virtual functions) is safe and should be the first choice; virtual inheritance can solve data duplication and ambiguity in diamond inheritance, but introduces additional layout complexity; when reusing functional implementations, composition is almost always a better choice than inheritance.

In the next chapter, we will synthesize our knowledge of classes, inheritance, and polymorphism by building a complete mini-project to experience how object-oriented design operates in real-world development.
