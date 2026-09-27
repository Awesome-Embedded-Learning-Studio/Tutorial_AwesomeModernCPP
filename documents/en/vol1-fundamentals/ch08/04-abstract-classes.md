---
title: "Abstract Classes and Interfaces"
description: "Master the design of pure virtual functions and abstract classes, and learn to organize type hierarchies with the Interface Segregation Principle"
chapter: 8
order: 4
difficulty: intermediate
reading_time_minutes: 12
platform: host
prerequisites:
  - "Virtual Functions and Polymorphism"
tags:
  - cpp-modern
  - host
  - intermediate
  - 进阶
cpp_standard: [11, 14, 17, 20]
translation:
  source: documents/vol1-fundamentals/ch08/04-abstract-classes.md
  source_hash: dfcb0eebe4c7a05db7c6b2153cad09b156e3ba16abd1d948e4793a1daa5f4390
  translated_at: '2026-09-25T11:28:42+00:00'
  engine: anthropic
  token_count: 2450
---

# Abstract Classes and Interfaces: Pin Down the "What", Leave the "How" Open

In the previous chapter we walked through the full mechanism of virtual functions and polymorphism—how, when we call `speak()` through a base class pointer, the compiler looks up the vtable and finds the real function address at runtime. But there is one question we deliberately sidestepped: what if the base class itself should never be instantiated? Say we define a `Shape` class to represent "shapes"—but "shape" is itself an abstract concept. There is no such thing in the world as a Shape object that is "neither a circle, nor a rectangle, nor any specific shape". It is just a common interface; everything truly meaningful lives in its derived classes.

That is exactly the problem abstract classes solve. In this chapter we start from pure virtual functions, get the abstract class mechanism in C++ straight, and then move on to interface design—in particular how to handle the fact that C++ has no `interface` keyword, and how the Interface Segregation Principle plays out in real projects.

If you are coming from Java or C#, you might instinctively assume that C++ abstract classes are fully equivalent to Java's `abstract class`. Most of the time that is indeed the case, but C++ pure virtual functions have a property Java does not offer: a pure virtual function may have a default implementation. We will cover that difference in detail later—no rush.

## The Birth of Pure Virtual Functions and Abstract Classes

To turn a class into an "uninstantiable" abstract class, we only need to declare at least one **pure virtual function** in it. The syntax is simple: append `= 0` to the end of the virtual function declaration:

```cpp
class Shape {
public:
    virtual ~Shape() = default;
    virtual double area() const = 0;       // pure virtual function
    virtual const char* name() const = 0;   // pure virtual function
};
```

The `= 0` notation looks a bit odd, but its semantics are crystal clear: this function has no implementation in the base class, and derived classes **must** provide their own version. A class containing at least one pure virtual function is an **abstract class**, and the compiler stops us from creating its objects directly:

```cpp
Shape s;            // Compile error! Cannot instantiate an abstract class
Shape* p = nullptr; // OK, pointers and references are fine
```

Manipulating derived class objects through pointers or references is perfectly fine—that is precisely the precondition for polymorphism to do its job. For a derived class to become a "concrete class" (one that can be instantiated), it must implement every pure virtual function of the base class, leaving none out:

```cpp
class Circle : public Shape {
    double radius_;
public:
    explicit Circle(double r) : radius_(r) {}
    double area() const override { return 3.14159265 * radius_ * radius_; }
    const char* name() const override { return "Circle"; }
};

class Rectangle : public Shape {
    double width_, height_;
public:
    Rectangle(double w, double h) : width_(w), height_(h) {}
    double area() const override { return width_ * height_; }
    const char* name() const override { return "Rectangle"; }
};

// Operate on everything uniformly through a base class reference
void print_area(const Shape& shape) {
    std::cout << shape.name() << ": " << shape.area() << std::endl;
}

Circle c(2.0);
Rectangle r(3.0, 4.0);
print_area(c);  // Circle: 12.5664
print_area(r);  // Rectangle: 12
```

If a derived class forgets to implement one of the pure virtual functions, the derived class itself becomes abstract as well. When we then try to instantiate it, the compiler reports "cannot instantiate abstract class". Beginners are often left completely baffled by this error, and the cause is usually a single pure virtual function left without an override. The good news is that the compiler's error message usually lists which pure virtual functions are still unimplemented—just fill them in accordingly.

## The Design Philosophy of Abstract Classes

We can boil the design philosophy of abstract classes down to one sentence: **the base class defines "what can be done", and the derived classes decide "how to do it"**.

In our `Shape` example above, `Shape` says "every shape can compute its area and has a name", but how exactly to compute it, and what that name is, is left for `Circle` and `Rectangle` to decide. This division of labor is very clean: the base class is a **contract**, and the derived classes are the **signatories**. Any derived class that wants to become a "usable concrete type" must fulfill every obligation the contract spells out.

Here is an example closer to real engineering. Suppose we are building a logging system that must support multiple output targets—console, file, network. We can define an abstract `ILogger` to unify the interface, then let `ConsoleLogger` write straight to `std::cout` and `FileLogger` append to a specified file. Upper-layer business code depends only on the `ILogger` interface and never needs to know whether the underlying sink is the console or a file:

```cpp
class ILogger {
public:
    virtual ~ILogger() = default;
    virtual void log_info(const std::string& msg) = 0;
    virtual void log_warning(const std::string& msg) = 0;
    virtual void log_error(const std::string& msg) = 0;
};

class ConsoleLogger : public ILogger {
public:
    void log_info(const std::string& msg) override {
        std::cout << "[INFO] " << msg << std::endl;
    }
    void log_warning(const std::string& msg) override {
        std::cout << "[WARN] " << msg << std::endl;
    }
    void log_error(const std::string& msg) override {
        std::cout << "[ERROR] " << msg << std::endl;
    }
};
```

**Decoupling!** Yes folks, this is the decoupling we are always talking about! This is the core value of abstract classes. An abstract class completely separates "interface definition" from "concrete implementation", so we can modify either side independently without affecting the other. When a `NetworkLogger` is added in the future, it just inherits from `ILogger` and implements the three methods—not a single line of upper-layer code changes.

## Interfaces in C++: A World Without the interface Keyword

If you have written Java or C#, you might find it strange: how come C++ does not even have an `interface` keyword? Indeed it does not, but C++'s abstract class mechanism fully covers the semantics of interfaces. The C++ community convention is this: when all of a class's member functions are pure virtual and it has no non-static data members, we call it an **interface class**.

```cpp
// A standard C++ interface class
class ISerializable {
public:
    virtual ~ISerializable() = default;
    virtual std::string serialize() const = 0;
    virtual bool deserialize(const std::string& data) = 0;
};
```

A few details deserve attention. Interface classes are conventionally named with a leading `I` (as in `ISerializable`, `IComparable`, `ILogger`)—a widely used naming convention that lets us recognize "this is a pure interface" at a glance. The virtual destructor is mandatory—as long as our class might be `delete`d through a base class pointer, a virtual destructor is a non-negotiable bottom line. `= default` is the C++11 spelling: more concise than hand-writing an empty destructor body, and it also expresses the intent of "use the compiler-generated default implementation".

## The Interface Segregation Principle—Don't Make Derived Classes Implement Methods They Don't Need

Whenever interface design comes up, we have to mention the **I in SOLID—the Interface Segregation Principle (ISP)**. Its core idea: never force a class to implement methods it will never use.

Let's look at a counterexample. Suppose we define an "all-powerful" device interface that lumps connection management, data reading and writing, and serial-port configuration all together:

```cpp
// Anti-pattern: a bloated "fat interface"
class IDevice {
public:
    virtual ~IDevice() = default;
    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    virtual int read() = 0;
    virtual void write(int value) = 0;
    virtual void flush() = 0;
    virtual void set_baudrate(int rate) = 0;
};
```

If we implement a read-only temperature sensor, it has no use whatsoever for `write()`, `flush()`, or `set_baudrate()`—yet because it inherits from `IDevice`, it still has to implement all of them, even if only as empty stubs or functions that just throw. The more serious problem is that it blurs the semantic boundary between types: a sensor that can only read is forced to claim that it "can write", which easily misleads callers.

The right move is to split the big interface into several **small, focused** interfaces, each describing exactly one capability:

```cpp
class IConnectable {
public:
    virtual ~IConnectable() = default;
    virtual bool connect() = 0;
    virtual void disconnect() = 0;
};

class IReadable {
public:
    virtual ~IReadable() = default;
    virtual int read() = 0;
};

class IWritable {
public:
    virtual ~IWritable() = default;
    virtual void write(int value) = 0;
    virtual void flush() = 0;
};
```

Now each device inherits only the interfaces it genuinely needs. A read-only temperature sensor implements `IConnectable` and `IReadable` and is done—it never touches `IWritable`; meanwhile a full-duplex serial driver can implement all three interfaces. That is precisely the effect the Interface Segregation Principle aims for: **every class exposes exactly the capabilities it truly supports—no more, no less.**

## Default Implementations for Pure Virtual Functions—An Easily Overlooked Advanced Technique

Next let's talk about a feature that is rarely mentioned but very useful in certain scenarios: **a pure virtual function may have a function body**.

That's right, you read that correctly. A pure virtual function declared `= 0` can still be given a default implementation outside the class:

```cpp
class Base {
public:
    virtual ~Base() = default;
    virtual void on_error(const std::string& msg) = 0;  // pure virtual function
};

// Default implementation of the pure virtual function — defined outside the class
void Base::on_error(const std::string& msg) {
    std::cerr << "[ERROR] " << msg << std::endl;
}
```

This looks contradictory—if it is "pure virtual", how can it also have an implementation? The key: `= 0` affects **whether the class is abstract** (whether this implementation exists does not change whether the class is abstract), while the function body provides an **optional default behavior**. Derived classes must still override the function, but we can choose to explicitly call the base class version inside our override to reuse the common logic:

```cpp
class Derived : public Base {
public:
    void on_error(const std::string& msg) override {
        Base::on_error(msg);    // First reuse the base class default behavior
        write_to_log_file(msg); // Then append our own handling
    }
};
```

We often run into this technique in framework design: the base class uses a pure virtual function to force derived classes to "handle this event", while at the same time offering a common default behavior to reuse as needed. This usage is not common in day-to-day business code, though, so we will just touch on it here—knowing that it exists is enough.

## Hands-On Practice—A Serializer Framework

Now let's tie together everything we have learned and build a small but complete framework. The scenario: we need a serialization framework that supports converting data into JSON or XML format. By defining an `ISerializer` interface, the upper-layer code stops caring entirely about which format sits underneath.

```cpp
#include <iostream>
#include <string>

/// @brief Serializer interface — outputs data in different formats
class ISerializer {
public:
    virtual ~ISerializer() = default;
    virtual void begin_object(const std::string& name) = 0;
    virtual void end_object() = 0;
    virtual void write_field(const std::string& key,
                             const std::string& value) = 0;
    virtual std::string result() const = 0;
};
```

Then we implement the two serializers, JSON and XML. Their internal structure is quite different (JSON has to deal with braces and quotes, XML with tag pairs), but the interfaces they expose are completely identical:

```cpp
class JSONSerializer : public ISerializer {
    std::string output_;
    int depth_ = 0;
public:
    void begin_object(const std::string& name) override {
        if (depth_ > 0) output_ += "\"" + name + "\": ";
        output_ += "{\n";
        ++depth_;
    }
    void end_object() override { --depth_; output_ += "}\n"; }
    void write_field(const std::string& key,
                     const std::string& value) override {
        output_ += "  \"" + key + "\": \"" + value + "\",\n";
    }
    std::string result() const override { return output_; }
};

class XMLSerializer : public ISerializer {
    std::string output_;
    std::string current_tag_;
public:
    void begin_object(const std::string& name) override {
        current_tag_ = name;
        output_ += "<" + name + ">\n";
    }
    void end_object() override { output_ += "</" + current_tag_ + ">\n"; }
    void write_field(const std::string& key,
                     const std::string& value) override {
        output_ += "  <" + key + ">" + value + "</" + key + ">\n";
    }
    std::string result() const override { return output_; }
};
```

In the upper-layer function we use the serializer through an interface reference, and it knows nothing about the details of JSON or XML:

```cpp
void serialize_sensor_data(ISerializer& serializer) {
    serializer.begin_object("sensor");
    serializer.write_field("id", "TEMP-001");
    serializer.write_field("value", "23.5");
    serializer.write_field("unit", "celsius");
    serializer.end_object();
}

int main() {
    JSONSerializer json;
    XMLSerializer xml;
    serialize_sensor_data(json);
    serialize_sensor_data(xml);
    std::cout << "=== JSON ===\n" << json.result() << "\n";
    std::cout << "=== XML ===\n" << xml.result() << std::endl;
    return 0;
}
```

Compile and run, and look at the output:

```text
=== JSON ===
{
  "id": "TEMP-001",
  "value": "23.5",
  "unit": "celsius",
}

=== XML ===
<sensor>
  <id>TEMP-001</id>
  <value>23.5</value>
  <unit>celsius</unit>
</sensor>
```

The `serialize_sensor_data` function knows only that "there is a thing called a serializer, and I can write fields into it". When YAML, Protobuf, or any new format needs to be supported in the future, we just add a new class implementing `ISerializer`—not a single line of upper-layer code changes. This is exactly the extensibility that abstract classes and interfaces bring.

## Practice Time

### Exercise 1: Design an IComparable Interface

Define an `IComparable<T>` interface template containing one pure virtual function, `compare_to`. Then implement a `Student` class that sorts by student ID.

```cpp
template <typename T>
class IComparable {
public:
    virtual ~IComparable() = default;
    /// @returns <0 means this < other, 0 means equal, >0 means this > other
    virtual int compare_to(const T& other) const = 0;
};
```

### Exercise 2: A Plugin System Framework

Design a simple plugin framework: define an `IPlugin` interface (containing four pure virtual functions: `name()`, `version()`, `initialize()`, and `shutdown()`), then implement two or three concrete plugin classes. Write a `PluginManager` that manages all plugins with a `std::vector<IPlugin*>` and provides `load_all()` and `unload_all()` methods. This exercise runs abstract classes, interfaces, and runtime polymorphism all together in one pass.
