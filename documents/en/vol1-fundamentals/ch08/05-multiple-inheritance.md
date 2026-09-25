---
chapter: 8
cpp_standard:
- 11
- 14
- 17
- 20
description: Understand the syntax of multiple inheritance, the diamond inheritance
  problem, and the virtual inheritance solution, and learn to use multiple inheritance
  judiciously.
difficulty: intermediate
order: 5
platform: host
prerequisites:
- Abstract Classes and Interfaces
reading_time_minutes: 9
tags:
- cpp-modern
- host
- intermediate
- 进阶
title: Multiple Inheritance and Virtual Inheritance
translation:
  source: documents/vol1-fundamentals/ch08/05-multiple-inheritance.md
  source_hash: 8965c32d75ed6424b9ab94abfef96c7762f68bcf31fb54c9295609c07deaa851
  translated_at: '2026-09-25T11:26:14+00:00'
  engine: anthropic
  token_count: 5100
---
# Multiple Inheritance and Virtual Inheritance: It Works, but Think It Through First

In the previous chapters, everything we discussed was single inheritance—one class with exactly one direct base class. That covers the vast majority of object-oriented design needs. But C++ also allows a class to inherit from several base classes at once; this is multiple inheritance. Multiple inheritance is powerful yet hugely controversial: used well, it makes a design more flexible; used badly, it makes the whole inheritance hierarchy hard to maintain. (So given the choice, the author trusts composition more.)

In this chapter we will get clear on the syntax of multiple inheritance, the diamond inheritance problem, the virtual inheritance solution, and when to turn around and pick a safer alternative.

## Basic Syntax and Use Cases of Multiple Inheritance

The syntax of multiple inheritance itself is not complicated: a class writes multiple base classes into its base list, separated by commas. The derived class object contains subobjects of all base classes, and it must implement the interfaces of all pure virtual bases.

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

// A config item that is both printable and serializable
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

Once we have created the object, we can manipulate it through any of the base class pointers: `Printable* p = &item; p->print();` or `Serializable* s = &item; s->serialize();`. Construction order: the bases are constructed in the order they are declared in the base list; destruction happens in exactly the reverse order.

When two base classes have members with the same name, the compiler reports an ambiguity error, and we need `obj.BaseA::foo()` to disambiguate explicitly. The safest use of multiple inheritance is **interface inheritance**: all base classes are pure virtual interfaces, with no data members and no concrete implementation. **If you catch yourself trying to reuse code implementations through multiple inheritance instead of expressing the semantics of "has several capabilities", you should most likely be considering composition instead.**

## The Diamond Inheritance Problem

The most classic trap in multiple inheritance is diamond inheritance—a base class is inherited by two intermediate classes, and the final class inherits both intermediate classes at the same time, forming a diamond. Without special handling, the final object contains **two copies** of the common base subobject. Let's look at a concrete example:

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
// ts.id = 1;          // Compile error: ambiguous!
ts.InputDevice::id = 1;
ts.OutputDevice::id = 2;  // Two independent copies of id, they don't affect each other
```

We can see that `Device`'s constructor is called twice, and `id` exists as two independent copies. A touchscreen device should have exactly one ID. Even more serious is the data inconsistency—in a large system, this "two copies of desynchronized state inside one logical object" situation is the source of extremely hard-to-track bugs.

## Virtual Inheritance Solves the Diamond Problem

The solution C++ hands us is **virtual inheritance**: add the `virtual` keyword when the intermediate classes inherit the common base:

```cpp
class InputDevice : virtual public Device {};
class OutputDevice : virtual public Device {};

class TouchScreen : public InputDevice, public OutputDevice
{
public:
    // With virtual inheritance, the most derived class initializes the virtual base
    TouchScreen() : Device(), InputDevice(), OutputDevice() {}
};
```

Now we see `Device` constructed only once, `id` in a single copy, and no more ambiguity. But virtual inheritance never comes for free: the object layout introduces extra virtual base pointers (vbptr), `sizeof(TouchScreen)` grows from 8 bytes to roughly 24 bytes, and accessing virtual base members requires an extra level of indirection.

Construction of the virtual base is the responsibility of the **most derived class**. Initialization-list entries that intermediate constructors write for the virtual base are silently ignored. Without knowing this rule, we might stare at the output scratching our heads for half a day while debugging—"I clearly passed the argument in the intermediate class, why didn't it take effect?"

Virtual inheritance must appear on **all** intermediate classes that directly inherit the common base. If only one of them uses `virtual` and the other doesn't, the diamond problem is not solved—and the compiler won't report an error—but we still end up with two copies of the base subobject.

The object layout under virtual inheritance differs from ordinary inheritance, so using `reinterpret_cast` or C-style casts on virtually inherited objects is extremely dangerous. A `static_cast` crossing a virtual base boundary may require a `this`-pointer offset adjustment. If we ever need to serialize objects into a byte stream, virtual inheritance makes things very thorny.

## Alternatives to Multiple Inheritance

Given the complexity of multiple inheritance—virtual inheritance especially—we have better options in many scenarios.

**Composition over inheritance** is one of the most classic principles in object-oriented design. If our class needs several capabilities at once but is not required to be operated uniformly through base class pointers, directly holding member objects is often clearer than inheriting—hold `Printer` and `JsonSerializer` as member variables instead of inheriting them as base classes. If runtime polymorphism is genuinely required, the **interface delegation pattern** is a more controllable choice than multiple inheritance: define an interface class, and internally delegate to a concrete implementation through a pointer.

In short, as long as the base classes are all pure virtual interfaces (no data members, no implementations), the complexity of multiple inheritance stays within a controllable range. **If data members or concrete method implementations start appearing in your multiple inheritance base classes, stop right there and re-examine your design.**

## Hands-on Verification — multi_inherit.cpp

Here is a complete, compilable example covering multi-interface inheritance and diamond inheritance:

```cpp
// multi_inherit.cpp
// Compile: g++ -Wall -Wextra -std=c++17 -o multi_inherit multi_inherit.cpp

#include <cstdio>
#include <string>

// --- Interface multiple inheritance ---
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

// --- Diamond inheritance (non-virtual) ---
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

// --- Diamond inheritance (virtual inheritance) ---
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
    vw.version = 42;  // OK! Only one copy
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

Let's compare the two groups of output: in non-virtual inheritance, `Component` is constructed twice and the two copies of `version` change independently; in virtual inheritance, `VComponent` is constructed only once and `version` is unified. Also note the difference in `sizeof`—virtual inheritance introduces extra pointer overhead.

## Exercises

### Exercise 1: Multi-Interface Implementation

Design a `LogEntry` class that implements three pure virtual interfaces at once: `IPrintable` (`void print() const`), `ISerializable` (`std::string to_string() const` returning JSON), and `IFilterable` (`bool matches(const std::string& keyword) const`). `LogEntry` contains three fields: `timestamp` (an integer), `level` (e.g. "INFO"), and `message` (a string). Create several log entries and manipulate them through each of the three base class pointers.

### Exercise 2: Fix the Diamond Inheritance

The following code has a diamond inheritance problem. Please fix it with virtual inheritance, making sure `SmartDevice` contains only one `Device` subobject:

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
    int read_status() override { return device_id; }  // Ambiguous!
};
```

Hint: after the change, don't forget to initialize the virtual base `Device` directly in `SmartDevice`'s constructor initialization list.
