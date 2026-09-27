---
chapter: 8
cpp_standard:
- 11
- 14
- 17
- 20
description: Understand the syntax of multiple inheritance, the diamond inheritance problem, and the virtual inheritance solution, and learn to use multiple inheritance with care
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
  source_hash: d5e2c2c014cd1b6e447948f4ac6c2ae447c1cebad05965d448999bcd3144eb12
  translated_at: '2026-09-27T03:51:38+00:00'
  engine: anthropic
  token_count: 3300
---
# Multiple Inheritance and Virtual Inheritance: It Works, but Think It Through Before Using It

In the preceding chapters, everything we discussed was single inheritance—one class with exactly one direct base class. That covers the vast majority of object-oriented design needs. But C++ also allows a class to inherit from several base classes at once, and that is multiple inheritance. It is also a feature surrounded by huge controversy: used well, it makes a design more flexible; used badly, it turns the whole inheritance hierarchy into something hard to maintain. (So given the comparison, we put more trust in composition.) We are also very strongly against casually messing with multiple inheritance. **The project we work on once kept us up all night hunting down a crash caused by multiple inheritance! Unless you genuinely know why you need multiple inheritance, reach for composition—every single time!**

In this chapter we will get all of this straight: the syntax of multiple inheritance, the diamond inheritance problem, the virtual inheritance solution, and when to turn around and choose a safer alternative.

## Basic Syntax and Use Cases of Multiple Inheritance

As we can see, the syntax of multiple inheritance itself is not complicated: a class writes multiple base classes into its base list, separated by commas. The derived-class object then contains subobjects of all the base classes, and it must implement the interfaces of all the pure virtual base classes.

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

Once we have created an object, we can operate on it through any of its base class pointers: `Printable* p = &item; p->print();` or `Serializable* s = &item; s->serialize();`. The base classes are constructed in the order they are declared in the base list, and destroyed in exactly the reverse order.

When two base classes have members with the same name, the compiler reports an ambiguity error, and we need `obj.BaseA::foo()` to disambiguate explicitly. The safest way to use multiple inheritance is **interface inheritance**: every base class is a pure virtual interface, with no data members and no concrete implementation. **If you find yourself trying to reuse code implementations through multiple inheritance rather than expressing the semantics of "having several capabilities", chances are you should be considering composition.**

## The Diamond Inheritance Problem

The most classic trap in multiple inheritance is diamond inheritance—a shared base class is inherited by two intermediate classes, and the final class inherits both of them, forming a diamond. Without special handling, the final object contains **two** copies of the shared base class subobject. Let's look at a concrete example:

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
ts.OutputDevice::id = 2;  // Two independent ids that do not affect each other
```

We can see that `Device`'s constructor is called twice and `id` exists as two independent copies. A touchscreen device should have exactly one ID. Even worse is the data inconsistency—in a large system, this kind of "two out-of-sync pieces of state inside one logical object" is a source of bugs that is extremely hard to track down.

## Virtual Inheritance Solves the Diamond Problem

The solution C++ provides us is **virtual inheritance**: add the `virtual` keyword when the intermediate classes inherit the shared base class:

```cpp
class InputDevice : virtual public Device {};
class OutputDevice : virtual public Device {};

class TouchScreen : public InputDevice, public OutputDevice
{
public:
    // With virtual inheritance, the most-derived class is responsible for initializing the virtual base
    TouchScreen() : Device(), InputDevice(), OutputDevice() {}
};
```

We now see `Device` constructed only once, `id` existing as a single copy, and no more ambiguity. But virtual inheritance never comes without a cost: the object layout gains extra virtual base pointers (vbptrs), `sizeof(TouchScreen)` grows from 8 bytes to roughly 24, and accessing virtual base members requires an extra level of indirection.

Construction of the virtual base is the responsibility of the **most-derived class**. The virtual-base initializers written in the intermediate classes' constructor initializer lists are silently ignored. Without knowing this rule, we can stare at the output and scratch our heads for half a day while debugging—"I clearly passed the argument in the intermediate class, why didn't it take effect?"

Virtual inheritance must appear in **all** of the intermediate classes that directly inherit the shared base class. Making only one of them `virtual` and leaving the other one alone does not solve the diamond problem; the compiler will not report an error, but we still end up with two base class subobjects.

The object layout under virtual inheritance differs from that of ordinary inheritance, so `reinterpret_cast` or C-style casts on virtually inherited objects are extremely dangerous. A `static_cast` that crosses a virtual base boundary may require a this-pointer adjustment. If we ever need to serialize objects into a byte stream, virtual inheritance makes things very tricky.

## Alternatives to Multiple Inheritance

Given the complexity of multiple inheritance—virtual inheritance in particular—in many scenarios we have better options.

**Composition over inheritance** is one of the most classic principles in object-oriented design. If our class needs to have several capabilities at once but does not need to be operated on uniformly through base class pointers, holding member objects directly is often clearer than inheriting—hold a `Printer` and a `JsonSerializer` as member variables rather than inheriting them as base classes. If runtime polymorphism is genuinely required, the **interface delegation pattern** is a more controllable choice than multiple inheritance: define an interface class that delegates internally, through a pointer, to a concrete implementation.

In short, as long as every base class is a pure virtual interface (no data members, no implementations), the complexity of multiple inheritance can be kept within controllable bounds. **The moment a data member or a concrete method implementation appears in one of your multiple inheritance base classes, stop right there and re-examine your design.**

## Hands-On Verification: multi_inherit.cpp

Let's look at a complete, compilable example that covers both multi-interface inheritance and diamond inheritance:

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

Let's compare the two groups of output: in the non-virtual version, `Component` is constructed twice and the two `version` copies change independently; in the virtual version, `VComponent` is constructed once and `version` is unified. Also note the `sizeof` difference—virtual inheritance introduces extra pointer overhead.

## Exercises

### Exercise 1: Implementing Multiple Interfaces

Design a `LogEntry` class that implements three pure virtual interfaces at once: `IPrintable` (`void print() const`), `ISerializable` (`std::string to_string() const`, returning JSON), and `IFilterable` (`bool matches(const std::string& keyword) const`). `LogEntry` contains three fields: `timestamp` (an integer), `level` (such as "INFO"), and `message` (a string). Create several log entries and operate on them through each of the three base class pointers.

### Exercise 2: Fixing Diamond Inheritance

The code below has a diamond inheritance problem. Fix it with virtual inheritance and make sure `SmartDevice` contains only one `Device` subobject:

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

Hint: after the modification, don't forget to initialize the virtual base `Device` directly in `SmartDevice`'s constructor initializer list.
