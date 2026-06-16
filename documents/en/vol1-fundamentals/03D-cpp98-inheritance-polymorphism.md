---
chapter: 0
cpp_standard:
- 11
- 14
- 17
- 20
description: From a single class to type hierarchy—inheritance expresses "is-a" relationships,
  virtual functions implement runtime polymorphism, abstract classes define capability
  contracts, and virtual destructors ensure safe deallocation.
difficulty: beginner
order: 3
platform: host
prerequisites:
- C++98面向对象：类与对象深度剖析
reading_time_minutes: 16
related:
- C++98运算符重载
- 何时用C++、用哪些C++特性
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: 'C++98 Object-Oriented Programming: Inheritance and Polymorphism'
translation:
  source: documents/vol1-fundamentals/03D-cpp98-inheritance-polymorphism.md
  source_hash: 110199e9245d4e2ec543f39c1922c1c0e017400ce2aca90a1bb7814f06c2e7c6
  translated_at: '2026-06-16T03:31:45.048762+00:00'
  engine: anthropic
  token_count: 2898
---
# C++98 Object-Oriented: Inheritance and Polymorphism

> The complete repository is available at [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP). Feel free to visit, and if you like it, give the project a Star to motivate the author.

In the previous post, we dove deep into the core mechanisms of classes and objects. Now, we expand our view from "individual classes" to "relationships between classes"—how C++ uses inheritance to express "is-a" semantics, and how it uses polymorphism to achieve "same interface, different behavior."

Inheritance and polymorphism are the two features in Object-Oriented Programming that are **most easily abused and most easily misunderstood**. When beginners mention inheritance, they often immediately think of "code reuse" or "writing less code," but in engineering practice, the real problem inheritance solves isn't writing fewer lines of code, but **expressing semantic relationships between types**. Polymorphism goes a step further, allowing you to manipulate objects of different types through a unified interface, with specific behaviors determined at runtime.

## 1. Inheritance

### 1.1 The Essence of Inheritance: Expressing "Is-A" Relationships

The core of inheritance is to express a very specific relationship: **a derived class is-a base class**. For example, a temperature sensor "is a sensor," and a UART "is a communication interface." Only when this semantic holds true is inheritance natural.

I must emphasize something: especially in critical design scenarios—**using the correct semantics is always better than cutting corners! Using the correct semantics is always better than cutting corners! Using the correct semantics is always better than cutting corners!** You don't want to be working overtime cleaning up the mess for your future self and your colleagues, do you?

Let's look at a complete sensor hierarchy example:

```cpp
class Sensor {
public:
    Sensor(int id) : id_(id), initialized_(false) {}

    virtual ~Sensor() {} // We'll discuss why this is virtual later

    virtual void init() = 0; // Pure virtual, must be implemented by derived classes
    virtual void read() = 0;

    int getId() const { return id_; }
    bool isInitialized() const { return initialized_; }

protected:
    void setInitialized(bool status) { initialized_ = status; }

    int id_;
    bool initialized_;
};

class TemperatureSensor : public Sensor {
public:
    TemperatureSensor(int id) : Sensor(id) {}

    void init() override {
        // Hardware initialization logic here
        setInitialized(true);
    }

    void read() override {
        // Read temperature data
    }
};

class HumiditySensor : public Sensor {
public:
    HumiditySensor(int id) : Sensor(id) {}

    void init() override {
        // Hardware initialization logic here
        setInitialized(true);
    }

    void read() override {
        // Read humidity data
    }
};
```

In this design, `Sensor` is responsible for defining "capabilities and states common to all sensors"—ID, initialization status, etc. Derived classes only need to care about their specific behaviors. The `protected` members in the base class are prepared exactly for this scenario: they are not exposed externally, but they allow derived classes to use these internal states within a reasonable scope.

### 1.2 Construction and Destruction Order

When creating a derived class object, the order of construction is **from base class to derived class**—first the base class subobject is constructed, then the derived class's own members. The order of destruction is exactly the reverse—**from derived class to base class**. This order is very logical: the derived class constructor might depend on base class members already being in a valid state, and during destruction, the derived class must clean up its own resources before the base class can be safely destructed.

```cpp
class Base {
public:
    Base() { std::cout << "Base constructed\n"; }
    ~Base() { std::cout << "Base destructed\n"; }
};

class Derived : public Base {
public:
    Derived() { std::cout << "Derived constructed\n"; }
    ~Derived() { std::cout << "Derived destructed\n"; }
};

int main() {
    Derived d;
    // Output:
    // Base constructed
    // Derived constructed
    // Derived destructed
    // Base destructed
}
```

In a derived class constructor, you need to specify which base class constructor to call via the initialization list. If you don't specify, the compiler calls the base class's default constructor. If the base class lacks a default constructor—for instance, if the base class only defines a constructor that takes arguments—you must explicitly call it in the derived class's initialization list:

```cpp
class Base {
public:
    Base(int value) : value_(value) {}
private:
    int value_;
};

class Derived : public Base {
public:
    // Error: Base has no default constructor
    // Derived() {}

    // Correct: Explicitly call Base(int)
    Derived(int x, int y) : Base(x), derivedValue_(y) {}
private:
    int derivedValue_;
};
```

### 1.3 Access Control in Inheritance

The inheritance method itself also has access control distinctions, but this topic often causes confusion. C++ supports three inheritance modes:

- **Public inheritance (`public`)**: The `public` members of the base class remain `public` in the derived class, and `protected` members remain `protected`. This is the most commonly used inheritance mode, maintaining the "is-a" semantic.
- **Protected inheritance (`protected`)**: The `public` and `protected` members of the base class both become `protected` in the derived class.
- **Private inheritance (`private`)**: The `public` and `protected` members of the base class both become `private` in the derived class.

In embedded engineering, in the vast majority of cases, you should only use **public inheritance**. The reason is simple: only public inheritance maintains the "is-a" semantic and ensures that using derived class objects through the base class interface is safe and intuitive. `protected` inheritance and `private` inheritance are more of language-level tricks with very limited applicable scenarios.

### 1.4 Object Slicing

When using inheritance, there is a very easily overlooked trap—**Object Slicing**. When you use a derived class object to initialize or assign to a base class object (not a pointer or reference), the parts specific to the derived class get "sliced off":

```cpp
class Base {
public:
    int x;
};

class Derived : public Base {
public:
    int y;
};

int main() {
    Derived d;
    d.x = 10;
    d.y = 20;

    Base b = d; // Object slicing occurs here!
    // b only contains x (value 10), y is lost
}
```

The reason for object slicing is simple: `b` is a variable of type `Base`, and its memory space is only large enough to hold members of `Base`. When you assign `d` to it, the compiler only copies the `Base` part, and the rest is discarded.

The way to avoid object slicing is also simple: **use references or pointers, not value types directly**. Manipulating derived class objects through base class references or pointers does not cause slicing:

```cpp
void process(Base& b) {
    // b refers to a Derived object, no slicing
}

int main() {
    Derived d;
    process(d); // Safe, no slicing
}
```

### 1.5 Multiple Inheritance and Diamond Inheritance

Multiple inheritance allows a class to inherit from multiple base classes simultaneously. In some scenarios, this is natural—for example, a device has both "readable" and "writable" capabilities:

```cpp
class Readable {
public:
    virtual int read() = 0;
    virtual ~Readable() {}
};

class Writable {
public:
    virtual void write(int data) = 0;
    virtual ~Writable() {}
};

class UART : public Readable, public Writable {
public:
    int read() override { /* ... */ }
    void write(int data) override { /* ... */ }
};
```

This kind of "interface inheritance" style multiple inheritance is relatively safe. But the real trouble with multiple inheritance lies in **Diamond Inheritance**—when two base classes inherit from the same common base class:

```cpp
class A {
public:
    int value;
};

class B : public A {};
class C : public A {};

class D : public B, public C {
    // D contains two copies of A::value!
};
```

At this point, a `D` object internally contains **two copies** of the `A` subobject—one from `B` and one from `C`. Accessing `value` causes a compilation error because the compiler doesn't know which copy you want.

C++ provides **Virtual Inheritance** to solve this problem:

```cpp
class B : virtual public A {};
class C : virtual public A {};

class D : public B, public C {
    // D now contains only one copy of A
};
```

Virtual inheritance ensures that no matter how many times `A` is indirectly inherited in the inheritance chain, the final object contains only one `A` subobject. But the cost of virtual inheritance is: object layout is more complex, constructor calling rules are more obscure, and there may be an extra level of indirection at runtime. In an embedded environment, this complexity is usually not worth it.

A relatively safe consensus is: **use multiple inheritance only for "interface inheritance" (base classes are all pure virtual functions), not for "implementation inheritance"**. If your multiple inheritance base classes contain data members or concrete implementations, you are probably already on a complex path.

## 2. Polymorphism

### 2.1 What is Polymorphism

If inheritance answers "what are you," then polymorphism answers "what are you acting like right now." Polymorphism allows you to manipulate a derived class object through a base class pointer or reference, and call the derived class's implementation at runtime.

The core of this capability lies in the **virtual function**. When a member function is declared as `virtual`, it means: **which implementation is actually called won't be determined until runtime, rather than being statically bound at compile time**. This is the fundamental reason why polymorphism works.

Let's look at a most basic example first:

```cpp
class Animal {
public:
    virtual void makeSound() {
        std::cout << "Some generic animal sound\n";
    }
    virtual ~Animal() {}
};

class Dog : public Animal {
public:
    void makeSound() override {
        std::cout << "Woof!\n";
    }
};

class Cat : public Animal {
public:
    void makeSound() override {
        std::cout << "Meow!\n";
    }
};
```

Now we can call `makeSound` through a base class pointer, and the specific behavior depends on the actual object type the pointer points to:

```cpp
void playWithAnimal(Animal* animal) {
    animal->makeSound(); // Dynamic dispatch
}

int main() {
    Dog dog;
    Cat cat;

    playWithAnimal(&dog); // Outputs: Woof!
    playWithAnimal(&cat); // Outputs: Meow!
}
```

Although this example is simple, it demonstrates the core value of polymorphism: the `playWithAnimal` function doesn't know and doesn't need to know what specific subtype of `Animal` it is. It only needs to know "this thing can `makeSound`". This ability of **the caller depending only on the abstract interface, not the concrete type**, is the cornerstone of large-scale system architecture.

### 2.2 Underlying Mechanism of Virtual Functions: The vtable

Understanding the underlying mechanism of polymorphism helps us make correct engineering judgments in embedded scenarios. Here is a brief introduction.

When you declare a virtual function (or inherit one) in a class, the compiler generates a **virtual function table (vtable)** for that class. This table is an array of function pointers, where each entry corresponds to a virtual function and stores the address of the actual implementation of that virtual function for that class.

At the same time, every object containing virtual functions has an additional hidden pointer in its memory layout—the **vtable pointer (vptr)**—which points to the vtable of the class to which the object belongs.

When calling `animal->makeSound()`, the code generated by the compiler roughly does these things:

1. Find the object's memory starting location through the `animal` pointer
2. Extract the `vptr` from the object to find the corresponding vtable
3. Look up the entry corresponding to `makeSound` in the vtable
4. Initiate an indirect call through the function pointer

This is why a virtual function call has one more level of indirection than a normal function call—it needs to look up the function to be actually called via the vtable at runtime. **This "indirect jump" is the entire runtime cost of polymorphism.**

On a PC, the cost of an indirect jump is negligible—maybe just one extra cache access. But in resource-constrained, real-time sensitive embedded systems, this cost needs to be taken seriously. Specifically:

- **Code size**: Each class with virtual functions has a vtable, which occupies Flash space.
- **Object size**: Each object has an extra `vptr` (usually the size of a pointer, 4 or 8 bytes), which can be significant on RAM-constrained MCUs.
- **Call overhead**: One indirect jump, which may affect the pipeline and branch prediction.

Therefore, a very important engineering judgment is: **polymorphism is worth using only when the "benefit of decoupling" clearly outweighs the "runtime overhead and complexity."**

### 2.3 Pure Virtual Functions and Abstract Classes

A pure virtual function is a special kind of virtual function—it has no implementation in the base class and requires all derived classes to provide their own implementation. A class containing at least one pure virtual function is called an **abstract class**, and it cannot be instantiated directly.

```cpp
class CommunicationInterface {
public:
    virtual void send(const uint8_t* data, size_t len) = 0;
    virtual size_t receive(uint8_t* buffer, size_t len) = 0;
    virtual ~CommunicationInterface() = default;
};
```

Abstract classes are not meant to create objects, but to **define a capability contract**. Derived classes must implement all pure virtual functions completely to become "legitimate concrete types":

```cpp
class UART_Driver : public CommunicationInterface {
public:
    void send(const uint8_t* data, size_t len) override {
        // UART specific sending logic
    }
    size_t receive(uint8_t* buffer, size_t len) override {
        // UART specific receiving logic
    }
};

class SPI_Driver : public CommunicationInterface {
public:
    void send(const uint8_t* data, size_t len) override {
        // SPI specific sending logic
    }
    size_t receive(uint8_t* buffer, size_t len) override {
        // SPI specific receiving logic
    }
};
```

Now, the upper-layer protocol processing logic can be completely indifferent to whether the underlying hardware is UART or SPI:

```cpp
void processPacket(CommunicationInterface& comm) {
    uint8_t header[2];
    comm.receive(header, 2); // Polymorphic call
    // ... process logic ...
    comm.send(response, len); // Polymorphic call
}
```

This design is particularly common in the driver layer. UART, SPI, and I2C look completely different, but at the level of "send data" and "receive data," they can share a set of abstract interfaces. Upper-layer protocol processing logic depends only on the interface, not on any specific hardware, which greatly improves code portability and testability.

### 2.4 Virtual Destructors

Virtual destructors are an extremely easily overlooked yet fatal detail in polymorphism.

**As long as you intend to manage the lifecycle of a derived class object through a base class pointer, the base class's destructor must be virtual.** Otherwise, when `delete`ing the base class pointer, only the base class's destructor will be called, and the resources held by the derived class will be completely un-released.

```cpp
class Base {
public:
    ~Base() { std::cout << "Base cleanup\n"; }
};

class Derived : public Base {
public:
    ~Derived() { std::cout << "Derived cleanup\n"; }
};

int main() {
    Base* b = new Derived();
    delete b; // Only Base's destructor is called! Derived's resources leak!
    // Output: Base cleanup
}
```

After adding `virtual`:

```cpp
class Base {
public:
    virtual ~Base() { std::cout << "Base cleanup\n"; }
};

// Derived remains the same

int main() {
    Base* b = new Derived();
    delete b;
    // Output:
    // Derived cleanup
    // Base cleanup
}
```

A simple but almost iron-clad rule of thumb is: **as long as a class has any virtual functions, you must declare the destructor as virtual as well**. This costs nothing, but it avoids a class of problems that manifest in embedded systems as "inexplicable memory leaks" or "peripheral state anomalies" and are extremely difficult to track down.

### 2.5 When to Use Polymorphism in Embedded Systems

In actual embedded engineering, the most valuable application scenarios for polymorphism often appear in "driver abstraction" and "protocol decoupling." However, not all scenarios are suitable for using polymorphism.

**Scenarios suitable for polymorphism**: The system needs to support multiple hardware variants (e.g., a sensor driver compatible with both UART and SPI communication); or when porting between different platforms, isolating platform-specific code into specific implementation classes; or if you want to extend system behavior by adding new derived classes without modifying existing code.

**Scenarios not suitable for polymorphism**: The system has only one deterministic, unchanging hardware configuration; the number of objects is very large (every object has an extra vptr, which might be unbearable on an MCU with only a few KB of RAM); or there are extreme real-time requirements (the indirect jump of a virtual function call has overhead, but more critically, uncertainty—you cannot determine the target address at compile time, which is unacceptable for some hard real-time systems).

The author's suggestion is: in embedded development, **start with no polymorphism, until you clearly feel the need for "a unified interface to operate different implementations"**. Don't introduce polymorphism just to make "code look more OOP"—this is typical over-engineering.

## Summary

In this chapter, we learned about inheritance and polymorphism—the two core mechanisms of C++'s object-oriented system. Inheritance is used to express "is-a" semantic relationships, with public inheritance being the overwhelming choice. Polymorphism implements runtime behavior dispatch through virtual functions, allowing us to manipulate different derived class objects through a unified base class interface. Virtual destructors are the safety baseline when using polymorphism; forgetting them results in resource leaks.

Inheritance and polymorphism are powerful tools, but they also introduce more complex object relationships, harder-to-trace call paths, and additional runtime overhead. In embedded development, the criterion for whether to use them is very simple: **does the benefit of decoupling clearly outweigh the introduced complexity and overhead?**

In the next post, we will learn about operator overloading—the ability to make custom types participate in expression calculations just like built-in types.
