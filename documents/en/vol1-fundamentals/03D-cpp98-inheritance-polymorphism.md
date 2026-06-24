---
chapter: 0
cpp_standard:
- 11
- 14
- 17
- 20
description: From a single class to type hierarchies — inheritance expresses "is-a"
  relationships, virtual functions implement runtime polymorphism, abstract classes
  define capability contracts, and virtual destructors ensure safe destruction.
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
  source_hash: b7416da721acd1d2624334582345d2d1aa436298194b06e9139e83950c6c689b
  translated_at: '2026-06-24T00:28:36.335386+00:00'
  engine: anthropic
  token_count: 2898
---
# C++98 OOP: Inheritance and Polymorphism

> The full repository is available at [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP). Feel free to check it out and give it a Star to motivate the author if you like it.

In the previous post, we explored the core mechanisms of classes and objects. Now, we expand our view from "individual classes" to "relationships between classes"—how C++ uses inheritance to express "is-a" semantics, and how it uses polymorphism to achieve "same interface, different behaviors."

Inheritance and polymorphism are the two features in Object-Oriented Programming that are **most easily abused and misunderstood**. Many beginners immediately think of "code reuse" or "writing less code" when they mention inheritance. However, in engineering practice, the real problem inheritance solves is not writing a few lines of code less, but rather **expressing semantic relationships between types**. Polymorphism goes a step further, allowing you to manipulate objects of different types through a unified interface, where specific behaviors are determined at runtime.

## 1. Inheritance

### 1.1 The Essence of Inheritance: Expressing "Is-A" Relationships

The core of inheritance is expressing a very specific relationship: **Derived class is-a Base class**. For example, a temperature sensor "is a" sensor, and a UART "is a" communication interface. Only when this semantic holds true is inheritance natural.

I want to emphasize something—especially in critical design scenarios: **Using correct semantics is always better than cutting corners! Using correct semantics is always better than cutting corners! Using correct semantics is always better than cutting corners!** You don't want to create cleanup work for your future self and your colleagues, do you?

Let's look at a complete sensor hierarchy example:

```cpp
// 基类：所有传感器的共同接口
class SensorBase {
protected:
    int sensor_id;
    bool initialized;

public:
    explicit SensorBase(int id) : sensor_id(id), initialized(false) {}

    virtual ~SensorBase() {}  // 虚析构函数，后面会详细讲

    bool is_initialized() const {
        return initialized;
    }

    int get_id() const {
        return sensor_id;
    }
};

// 派生类：温度传感器
class TemperatureSensor : public SensorBase {
private:
    float offset;  // 温度校准偏移

public:
    TemperatureSensor(int id, float cal_offset = 0.0f)
        : SensorBase(id), offset(cal_offset) {}

    bool init() {
        // 温度传感器特有的初始化
        initialized = true;
        return true;
    }

    float read_celsius() {
        float raw = read_adc();
        return raw + offset;
    }

private:
    float read_adc() {
        // 实际读取 ADC 值
        return 25.0f;
    }
};

// 派生类：压力传感器
class PressureSensor : public SensorBase {
private:
    float altitude_offset;

public:
    PressureSensor(int id, float alt_offset = 0.0f)
        : SensorBase(id), altitude_offset(alt_offset) {}

    bool init() {
        // 压力传感器特有的初始化
        initialized = true;
        return true;
    }

    float read_hpa() {
        float raw = read_adc();
        return raw * 10.0f + altitude_offset;
    }

private:
    float read_adc() {
        // 实际读取 ADC 值
        return 101.325f;
    }
};
```

In this design, `SensorBase` is responsible for defining "capabilities and states common to all sensors"—such as ID and initialization status. Derived classes only need to focus on their specific behaviors. The `protected` members in the base class are intended precisely for this scenario: they remain hidden from the outside world while allowing derived classes to utilize this internal state within a reasonable scope.

### 1.2 Construction and Destruction Order

When we create a derived class object, the order of construction is **from base class to derived class**—the base class subobject is constructed first, followed by the derived class's own members. The order of destruction is exactly the reverse: **from derived class to base class**. This sequence is highly logical: the derived class constructor may rely on base class members already being in a valid state, while during destruction, the derived class must clean up its own resources before the base class can be safely destructed.

```cpp
class Base {
public:
    Base() { printf("Base constructed\n"); }
    ~Base() { printf("Base destroyed\n"); }
};

class Derived : public Base {
public:
    Derived() { printf("Derived constructed\n"); }
    ~Derived() { printf("Derived destroyed\n"); }
};

// 创建和销毁
{
    Derived d;
    // 输出：
    // Base constructed
    // Derived constructed
}
// 离开作用域，输出：
// Derived destroyed
// Base destroyed
```

In a derived class's constructor, we must specify which base class constructor to call via the member initializer list. If we do not specify one, the compiler will call the base class's default constructor. If the base class lacks a default constructor—for example, if it only defines a constructor that takes parameters—then we must explicitly call it in the derived class's initializer list:

```cpp
class TemperatureSensor : public SensorBase {
public:
    TemperatureSensor(int id)
        : SensorBase(id) {  // 必须显式调用基类构造函数
        // ...
    }
};
```

### 1.3 Access Control in Inheritance

The inheritance method itself is subject to access control, a topic that is often confusing. C++ supports three types of inheritance:

- **Public inheritance (`public`)**: `public` members of the base class remain `public` in the derived class, and `protected` members remain `protected`. This is the most commonly used inheritance method, preserving the "is-a" relationship.
- **Protected inheritance (`protected`)**: Both `public` and `protected` members of the base class become `protected` in the derived class.
- **Private inheritance (`private`)**: Both `public` and `protected` members of the base class become `private` in the derived class.

In embedded engineering, we should almost exclusively use **public inheritance**. The reason is simple: only public inheritance maintains the "is-a" relationship and ensures that using derived class objects through a base class interface is safe and intuitive. `protected` and `private` inheritance are primarily language-level tricks with very limited use cases.

### 1.4 Object Slicing

When using inheritance, there is a trap that is very easy to overlook—**object slicing**. When we use a derived class object to initialize or assign to a base class object (not a pointer or reference), the parts specific to the derived class are "sliced off":

```cpp
TemperatureSensor temp(1);
SensorBase base = temp;  // 对象切片！

// base 现在是一个 SensorBase 对象
// TemperatureSensor 特有的成员（offset, read_celsius()）全部丢失
```

The reason for object slicing is simple: `base` is a variable of type `SensorBase`, so its memory space is only large enough to hold members of `SensorBase`. When you assign `temp` to it, the compiler only copies the `SensorBase` portion, and the rest is discarded.

The way to avoid object slicing is also simple: **use references or pointers instead of value types**. Operating on derived class objects via base class references or pointers will not cause slicing:

```cpp
TemperatureSensor temp(1);
SensorBase& ref = temp;   // OK：引用，不会切片
SensorBase* ptr = &temp;  // OK：指针，不会切片
```

### 1.5 Multiple Inheritance and Diamond Inheritance

Multiple inheritance allows a class to inherit from more than one base class simultaneously. In some scenarios, this is quite natural—for example, a device might possess both "readable" and "writable" capabilities:

```cpp
class Readable {
public:
    virtual int read() = 0;
};

class Writable {
public:
    virtual void write(int value) = 0;
};

class SerialPort : public Readable, public Writable {
private:
    int buffer;

public:
    int read() override {
        return buffer;
    }

    void write(int value) override {
        buffer = value;
    }
};
```

This "interface inheritance" style of multiple inheritance is relatively safe. However, the real trouble with multiple inheritance lies in the **diamond problem**—when two base classes inherit from the same common base class:

```cpp
class Base {
public:
    int value;
};

class Derived1 : public Base { };
class Derived2 : public Base { };

class Multiple : public Derived1, public Derived2 {
    void foo() {
        // value 是歧义的：是 Derived1::value 还是 Derived2::value？
    }
};
```

At this point, the `Multiple` object contains **two** `Base` subobjects—one from `Derived1` and one from `Derived2`. When accessing `value`, the compiler cannot determine which one you are referring to and reports an ambiguity error.

C++ provides **virtual inheritance** to solve this problem:

```cpp
class Derived1 : virtual public Base { };
class Derived2 : virtual public Base { };

class Multiple : public Derived1, public Derived2 {
    void foo() {
        value = 10;  // 现在只有一份 Base，不再有歧义
    }
};
```

Virtual inheritance ensures that no matter how many times `Base` is indirectly inherited in the inheritance chain, the final object contains only one `Base` subobject. However, the cost of virtual inheritance is more complex object layout, obscure constructor invocation rules, and potentially an extra level of indirection at runtime. In an embedded environment, this complexity is usually not worth it.

A relatively safe consensus is: **use multiple inheritance only for "interface inheritance" (base classes consist entirely of pure virtual functions), and avoid it for "implementation inheritance"**. If your multiple inheritance base classes contain data members or concrete implementations, you are likely already on a path of unnecessary complexity.

## 2. Polymorphism

### 2.1 What is Polymorphism

If inheritance answers the question "what are you," then polymorphism answers "what do you act like right now." Polymorphism allows you to manipulate a derived class object through a base class pointer or reference, invoking the derived class's implementation at runtime.

The core of this capability lies in **virtual functions**. When a member function is declared as `virtual`, it means: **which specific implementation to invoke is determined at runtime, rather than statically bound at compile time**. This is the fundamental reason why polymorphism works.

Let's start with a basic example:

```cpp
class Animal {
public:
    virtual void speak() {  // 虚函数
        printf("...\n");
    }

    virtual ~Animal() {}  // 虚析构函数
};

class Dog : public Animal {
public:
    void speak() override {
        printf("Woof!\n");
    }
};

class Cat : public Animal {
public:
    void speak() override {
        printf("Meow!\n");
    }
};
```

Now we can call `speak()` through a base class pointer, where the specific behavior depends on the actual object type pointed to:

```cpp
void make_sound(Animal* animal) {
    animal->speak();  // 运行时决定调用哪个版本
}

Dog dog;
Cat cat;
make_sound(&dog);  // 输出 "Woof!"
make_sound(&cat);  // 输出 "Meow!"
```

Although this example is simple, it demonstrates the core value of polymorphism: the `make_sound` function is completely unaware of, and does not need to know, the specific concrete subtype of `Animal`. It only needs to know that "this thing can `speak()`". This ability to **depend only on abstract interfaces rather than concrete types** is the cornerstone of large-scale system architecture.

### 2.2 Underlying Mechanism of Virtual Functions: The vtable

Understanding the underlying mechanism of polymorphism helps us make sound engineering decisions in embedded scenarios. Here is a brief introduction.

When you declare a virtual function in a class (or inherit one), the compiler generates a **virtual table (vtable)** for that class. This table is an array of function pointers, where each entry corresponds to a virtual function and stores the address of the actual implementation for that class.

At the same time, every object containing virtual functions includes a hidden pointer in its memory layout—the **vtable pointer (vptr)**—which points to the vtable of the object's class.

When calling `animal->speak()`, the code generated by the compiler roughly performs the following steps:

1. Locate the start of the object's memory via the `animal` pointer.
2. Retrieve the `vptr` from the object to find the corresponding vtable.
3. Look up the entry for `speak()` in the vtable.
4. Initiate an indirect call via the function pointer.

This explains why virtual function calls involve an extra layer of indirection compared to normal function calls—they require looking up the actual function to call via the vtable at runtime. **This "indirect jump" constitutes the entire runtime cost of polymorphism.**

On a PC, the cost of a single indirect jump is negligible—perhaps just one extra cache access. However, in resource-constrained embedded systems that are sensitive to real-time performance, this overhead must be taken seriously. Specifically:

- **Code Size:** Each class with virtual functions has a vtable, which consumes Flash space.
- **Object Size:** Each object has an extra `vptr` (usually the size of a pointer, 4 or 8 bytes), which can be significant on RAM-constrained MCUs.
- **Call Overhead:** An indirect jump can affect pipelines and branch prediction.

Therefore, a crucial engineering judgment is: **polymorphism should only be used when the "benefits of decoupling" clearly outweigh the "runtime overhead and complexity."**

### 2.3 Pure Virtual Functions and Abstract Classes

A pure virtual function is a special type of virtual function—it has no implementation in the base class and requires all derived classes to provide their own implementation. A class containing at least one pure virtual function is called an **abstract class**, and it cannot be instantiated directly.

```cpp
// 抽象类：通信接口
class CommunicationInterface {
public:
    virtual ~CommunicationInterface() = default;

    virtual bool send(const uint8_t* data, size_t length) = 0;
    virtual size_t receive(uint8_t* buffer, size_t max_length) = 0;
    virtual bool is_connected() const = 0;
};
```

Abstract classes are not meant for creating objects, but rather for **defining a capability contract**. A derived class must fully implement all pure virtual functions to become a "valid concrete type":

```cpp
class UARTDriver : public CommunicationInterface {
private:
    int port;
    int baudrate;

public:
    UARTDriver(int p, int baud) : port(p), baudrate(baud) {}

    bool send(const uint8_t* data, size_t length) override {
        // UART 特定的发送实现
        for (size_t i = 0; i < length; ++i) {
            uart_write_byte(port, data[i]);
        }
        return true;
    }

    size_t receive(uint8_t* buffer, size_t max_length) override {
        // UART 特定的接收实现
        size_t count = 0;
        while (count < max_length && uart_has_data(port)) {
            buffer[count++] = uart_read_byte(port);
        }
        return count;
    }

    bool is_connected() const override {
        return true;  // UART 是有线连接，默认始终连接
    }
};

class SPIDriver : public CommunicationInterface {
private:
    int cs_pin;

public:
    explicit SPIDriver(int cs) : cs_pin(cs) {}

    bool send(const uint8_t* data, size_t length) override {
        gpio_write(cs_pin, LOW);  // 拉低 CS
        spi_transfer(data, length);
        gpio_write(cs_pin, HIGH); // 拉高 CS
        return true;
    }

    size_t receive(uint8_t* buffer, size_t max_length) override {
        gpio_write(cs_pin, LOW);
        size_t count = spi_read(buffer, max_length);
        gpio_write(cs_pin, HIGH);
        return count;
    }

    bool is_connected() const override {
        return gpio_read(cs_pin) == LOW;  // 简单判断
    }
};
```

Now, the upper-layer protocol handling logic can be completely agnostic to whether the underlying layer is UART or SPI:

```cpp
void send_command(CommunicationInterface& comm, const uint8_t* cmd, size_t len) {
    comm.send(cmd, len);
}

// 使用
UARTDriver uart(1, 115200);
SPIDriver spi(5);

send_command(uart, cmd, sizeof(cmd));  // 通过 UART 发送
send_command(spi, cmd, sizeof(cmd));   // 通过 SPI 发送
```

This design pattern is particularly common in driver layers. UART, SPI, and I2C peripherals may appear completely different, but at the level of "sending data" and "receiving data," they can share a common abstract interface. Upper-layer protocol logic depends solely on the interface, not on specific hardware, which significantly improves code portability and testability.

### 2.4 Virtual Destructors

Virtual destructors are a detail in polymorphism that is easily overlooked, yet critically dangerous.

**If you intend to manage the lifetime of derived class objects via base class pointers, the base class destructor must be virtual.** Otherwise, when `delete`ing a base class pointer, only the base class destructor will be called, and resources held by the derived class will never be released.

```cpp
class BadBase {
public:
    ~BadBase() { printf("BadBase destroyed\n"); }  // 非虚析构函数
};

class BadDerived : public BadBase {
private:
    int* data;

public:
    BadDerived() : data(new int[100]) {}
    ~BadDerived() {
        delete[] data;
        printf("BadDerived destroyed\n");
    }
};

// 使用
BadBase* ptr = new BadDerived();
delete ptr;  // 只调用 ~BadBase()，~BadDerived() 被跳过！
// 输出只有 "BadBase destroyed"
// data 对应的 400 字节内存泄漏了！
```

After adding `virtual`:

```cpp
class GoodBase {
public:
    virtual ~GoodBase() { printf("GoodBase destroyed\n"); }
};

class GoodDerived : public GoodBase {
private:
    int* data;

public:
    GoodDerived() : data(new int[100]) {}
    ~GoodDerived() {
        delete[] data;
        printf("GoodDerived destroyed\n");
    }
};

GoodBase* ptr = new GoodDerived();
delete ptr;
// 输出：
// GoodDerived destroyed
// GoodBase destroyed
// 内存正确释放
```

A simple but almost ironclad rule is: **whenever a class contains any virtual functions, you must declare the destructor as virtual as well**. This costs nothing, but it prevents a class of issues that manifest in embedded systems as "inexplicable memory leaks" or "peripheral state anomalies," and are notoriously difficult to track down.

### 2.5 When to Use Polymorphism in Embedded Systems

In real-world embedded engineering, the most valuable use cases for polymorphism often arise in "driver abstraction" and "protocol decoupling." However, polymorphism is not suitable for every scenario.

**Scenarios suitable for polymorphism**: The system needs to support multiple hardware variants (for example, a sensor driver compatible with both UART and SPI communication); or platform-specific code needs to be isolated into concrete implementation classes for portability across different platforms; or you want to extend system behavior by adding new derived classes without modifying existing code.

**Scenarios unsuitable for polymorphism**: The system has only one fixed, unchanging hardware configuration; the number of objects is very large (each object adds a `vptr`, which may be unaffordable on an MCU with only a few KB of RAM); or there are extreme real-time requirements (the indirect jump of a virtual function call incurs overhead, but more critically, indeterminacy—you cannot determine the target address at compile time, which is unacceptable for some hard real-time systems).

My advice is: in embedded development, **start without polymorphism until you clearly feel the need for "a unified interface to operate on different implementations."** Do not introduce polymorphism just to make the "code look more OOP"—this is typical over-engineering.

## Summary

In this chapter, we learned about inheritance and polymorphism—the two core mechanisms of C++ object-oriented programming. Inheritance is used to express "is-a" semantic relationships, with public inheritance being the overwhelming choice. Polymorphism implements runtime behavior dispatch via virtual functions, allowing us to manipulate different derived class objects through a unified base class interface. Virtual destructors are the safety baseline when using polymorphism; forgetting them leads to resource leaks.

Inheritance and polymorphism are powerful tools, but they also introduce more complex object relationships, harder-to-trace call paths, and additional runtime overhead. In embedded development, the criteria for using them are very simple: **does the benefit of decoupling clearly outweigh the introduced complexity and overhead?**

In the next article, we will learn about operator overloading—the ability to participate in expression calculations with user-defined types just like built-in types.
