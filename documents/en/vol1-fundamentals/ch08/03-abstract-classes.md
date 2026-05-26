---
title: Abstract Classes and Interfaces
description: Master the design of pure virtual functions and abstract classes, and
  learn to organize type hierarchies using the interface segregation principle.
chapter: 8
order: 3
difficulty: intermediate
reading_time_minutes: 12
platform: host
prerequisites:
- 虚函数与多态
tags:
- cpp-modern
- host
- intermediate
- 进阶
cpp_standard:
- 11
- 14
- 17
- 20
translation:
  source: documents/vol1-fundamentals/ch08/03-abstract-classes.md
  source_hash: 84024ca39ba3d7140e2edd9a9e59c171b4845b54a36bd079cfd3bb322066b64e
  translated_at: '2026-05-26T10:54:11.683879+00:00'
  engine: anthropic
  token_count: 2388
---
# Abstract Classes and Interfaces

In the previous chapter, we thoroughly broke down the mechanics of virtual functions and polymorphism. We saw how the compiler looks up the vtable and finds the real function address at runtime when we call a virtual function through a base class pointer. But we deliberately sidestepped one question—what if the base class itself shouldn't be instantiated? For example, if we define a `Shape` class to represent "shapes," "shape" itself is an abstract concept. No `Shape` object exists in the real world that "isn't a circle, a rectangle, or any specific shape." It's simply a common interface; the truly meaningful entities are its derived classes.

This is the problem that abstract classes solve. In this chapter, we start with pure virtual functions, clarify the abstract class mechanism in C++, and then discuss interface design—specifically, how to handle the fact that C++ lacks an `interface` keyword, and how to put the Interface Segregation Principle into practice in real-world engineering.

> **Pitfall Warning**: If you're coming from Java or C#, you might instinctively assume that C++ abstract classes are completely equivalent to Java's `abstract class`. That's mostly true, but C++ pure virtual functions have one feature that Java lacks—a pure virtual function can have a default implementation. We'll cover this difference in detail later, so don't rush ahead.

## Step One — Pure Virtual Functions and the Birth of Abstract Classes

To make a class an "uninstantiable" abstract class, we simply declare at least one **pure virtual function** in it. The syntax is straightforward: append `= 0` to the end of the virtual function declaration:

```cpp
class Shape {
public:
    virtual ~Shape() = default;
    virtual double area() const = 0;       // 纯虚函数
    virtual const char* name() const = 0;   // 纯虚函数
};
```

`= 0` This syntax might look a bit odd, but its semantics are clear: this function has no implementation in the base class, and derived classes **must** provide their own versions. A class containing at least one pure virtual function is an **abstract class**, and the compiler will prevent you from directly creating objects of that type:

```cpp
Shape s;            // 编译错误！不能实例化抽象类
Shape* p = nullptr; // OK，指针和引用是可以的
```

Operating on derived class objects through pointers or references works perfectly fine—this is the very prerequisite for polymorphism to work. For a derived class to become a "concrete class" (one that can be instantiated), it must implement every single pure virtual function from the base class, without exception:

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

// 通过基类引用统一操作
void print_area(const Shape& shape) {
    std::cout << shape.name() << ": " << shape.area() << std::endl;
}

Circle c(2.0);
Rectangle r(3.0, 4.0);
print_area(c);  // Circle: 12.5664
print_area(r);  // Rectangle: 12
```

> **Pitfall Warning**: If a derived class forgets to implement a pure virtual function, the derived class itself becomes abstract. If you then try to instantiate it, the compiler will error with "cannot instantiate abstract class." Beginners are often baffled by this error, and the root cause is usually a missed `override` on a pure virtual function. The good news is that the compiler's error message typically lists which pure virtual functions are still unimplemented—just fill them in accordingly.

## Design Philosophy of Abstract Classes

The design philosophy of abstract classes can be summed up in one sentence: **the base class defines "what to do," and the derived class decides "how to do it."**

In our `Shape` example above, `Shape` says "every shape can calculate its area and has a name," but exactly how to calculate it and what the name is are left for `Circle` and `Rectangle` to decide. This division of labor is very clear—the base class is a **contract**, and the derived class is the **signatory**. Any derived class that wants to be a "usable concrete type" must fulfill all obligations stipulated in the contract.

Let's look at a more practical engineering example. Suppose we're developing a logging system that needs to support multiple output targets—console, file, and network. We can define an abstract `ILogger` to unify the interface, then have `ConsoleLogger` output directly to `std::cout`, and `FileLogger` append to a specified file. Upper-layer business code only depends on the `ILogger` interface and doesn't need to know at all whether the underlying output goes to the console or a file:

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

**Decoupling!** This is the decoupling we always talk about, folks! This is the core value of abstract classes. Abstract classes completely separate "interface definition" from "concrete implementation," allowing us to modify one end independently without affecting the other. In the future, adding a `NetworkLogger` only requires inheriting from `ILogger` and implementing three methods—the upper-layer code doesn't need to change a single line.

## C++ Interfaces — A World Without the `interface` Keyword

If you've written Java or C#, you might find it strange: how does C++ not even have an `interface` keyword? It truly doesn't, but C++'s abstract class mechanism fully covers the semantics of interfaces. The C++ community convention is: when all of a class's member functions are pure virtual and it has no non-static data members, we call it an **interface class**.

```cpp
// 标准的 C++ 接口类
class ISerializable {
public:
    virtual ~ISerializable() = default;
    virtual std::string serialize() const = 0;
    virtual bool deserialize(const std::string& data) = 0;
};
```

Notice a few details. Interface class names conventionally start with a capital `I` (such as `ISerializable`, `IComparable`, `ILogger`), which is a widely used naming convention that lets readers recognize at a glance that "this is a pure interface." A virtual destructor is mandatory—as long as your class might be `delete`d through a base class pointer, a virtual destructor is a non-negotiable baseline. `= default` is a C++11 feature that's more concise than hand-writing an empty destructor body, and it expresses the semantic intent of "use the compiler-generated default implementation."

## Interface Segregation Principle — Don't Force Derived Classes to Implement Methods They Don't Need

When discussing interface design, we have to mention **I — the Interface Segregation Principle (ISP)** from the SOLID principles. Its core idea is: don't force a class to implement methods it doesn't use.

Let's look at a counterexample. Suppose we define an "all-powerful" device interface that mixes connection management, data read/write, and serial port configuration all together:

```cpp
// 反面教材：臃肿的"胖接口"
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

If we want to implement a read-only temperature sensor, it doesn't need `write()`, `setBaudRate()`, or `setDataBits()` at all. But because it inherits from `IDevice`, it still has to implement all of them—even if it's just writing an empty function or throwing an exception directly. A more serious problem is that it blurs the semantic boundaries between types: a sensor that can only read is forced to declare that it "can write," which can easily mislead callers.

The correct approach is to split the large interface into several **small, focused** interfaces, each describing a single capability:

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

Now each device only needs to inherit the interfaces it truly requires. A read-only temperature sensor only needs to implement `IReadable` and `IConnectable`, and doesn't need to touch `IWritable` at all. A full-duplex serial driver, on the other hand, can implement all three interfaces. This is exactly the effect the Interface Segregation Principle aims to achieve: **each class exposes only the capabilities it truly supports, no more and no less.**

## Default Implementations for Pure Virtual Functions — An Easily Overlooked Advanced Technique

Next, let's discuss a feature that's rarely mentioned but very useful in certain scenarios: **pure virtual functions can have function bodies**.

That's right, you read that correctly. A function declared as a pure virtual function with `= 0` can still provide a default implementation outside the class:

```cpp
class Base {
public:
    virtual ~Base() = default;
    virtual void on_error(const std::string& msg) = 0;  // 纯虚函数
};

// 纯虚函数的默认实现——类外部定义
void Base::on_error(const std::string& msg) {
    std::cerr << "[ERROR] " << msg << std::endl;
}
```

This seems contradictory—if it's "pure virtual," how can it have an implementation? The key is: `= 0` affects the **abstract nature of the class** (whether the function has a body or not doesn't change whether the class is abstract), while the function body provides an **optional default behavior**. Derived classes still must override this function, but they can choose to explicitly call the base class version within their override to reuse common logic:

```cpp
class Derived : public Base {
public:
    void on_error(const std::string& msg) override {
        Base::on_error(msg);    // 先复用基类的默认行为
        write_to_log_file(msg); // 再追加自己的处理
    }
};
```

This technique is commonly used in framework design—the base class uses a pure virtual function to force derived classes to "handle this event," while simultaneously providing a common default behavior for on-demand reuse. To be honest, though, this usage isn't common in day-to-day business code. We'll just touch on it here—know that it exists and leave it at that.

## Hands-On Practice — A Serializer Framework

Now let's tie together what we've learned and build a complete mini-framework. The scenario is this: we need a serialization framework that supports converting data into JSON or XML format. By defining an `ISerializer` interface, the upper-layer code doesn't care at all about the underlying format.

```cpp
#include <iostream>
#include <string>

/// @brief 序列化器接口——支持不同格式的数据输出
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

Then we implement JSON and XML serializers. Their internal structures are quite different—JSON needs to handle curly braces and quotes, while XML needs to handle tag pairs—but the interfaces they expose are completely identical:

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

Upper-layer functions use the serializer through an interface reference—they know nothing about JSON or XML details:

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

Compile and run, and check the output:

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

The `serializePerson` function only knows that "there's a serializer thing, and I can write fields to it." In the future, if we need to support YAML, Protobuf, or any new format, we just need to add a new implementation class that inherits from `ISerializer`, and the upper-layer code doesn't need to change a single line. This is the extensibility that abstract classes and interfaces bring.

## Practice Time

### Exercise 1: Design an `IComparable` Interface

Define an `IComparable<T>` interface template containing a pure virtual function `compareTo`. Then implement a `Student` class that sorts by student ID.

```cpp
template <typename T>
class IComparable {
public:
    virtual ~IComparable() = default;
    /// @returns <0 表示 this < other, 0 表示相等, >0 表示 this > other
    virtual int compare_to(const T& other) const = 0;
};
```

### Exercise 2: Plugin System Framework

Design a simple plugin framework: define an `IPlugin` interface (containing four pure virtual functions: `getName`, `initialize`, `execute`, and `shutdown`), and then implement two or three concrete plugin classes. Write a `PluginManager` that uses a `std::vector` to manage all plugins, and provides `loadAll` and `executeAll` methods. This exercise will help you combine abstract classes, interfaces, and runtime polymorphism in one go.

## Summary

In this chapter, centered around the requirement that "base classes shouldn't be instantiated," we learned about pure virtual functions and abstract classes. The key takeaways are: appending `= 0` after a virtual function declaration makes it a pure virtual function, which in turn makes the containing class an abstract class; derived classes must implement all pure virtual functions to become concrete classes. C++ doesn't have a dedicated `interface` keyword, but a class with "all pure virtual functions + no data members + virtual destructor" is a de facto interface. The Interface Segregation Principle tells us that rather than designing an all-encompassing fat interface, we should split it into several small, focused interfaces, letting each class bear only the responsibilities it truly needs.

In the next chapter, we'll discuss multiple inheritance and virtual inheritance—when inheritance relationships become complex, what mechanisms does C++ provide to handle ambiguity and redundancy.
