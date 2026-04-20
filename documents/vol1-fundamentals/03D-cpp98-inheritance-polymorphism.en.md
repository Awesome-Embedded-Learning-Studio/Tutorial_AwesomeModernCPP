---
chapter: 0
cpp_standard:
- 11
- 14
- 17
- 20
description: From a single class to a type hierarchy — inheritance expresses "is-a"
  relationships, virtual functions implement runtime polymorphism, abstract classes
  define capability contracts, and virtual destructors ensure safe release.
difficulty: beginner
order: 3
platform: host
prerequisites:
- C++98面向对象：类与对象深度剖析
reading_time_minutes: 20
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
  source_hash: e06a45e70a25a16b2267f86191270722d475098ee6fa6d1299c9bf6fd961073a
  translated_at: '2026-04-20T03:05:46.076780+00:00'
  engine: anthropic
  token_count: 2898
---
# C++98 Object-Oriented Programming: Inheritance and Polymorphism

> The complete repository is available at [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP). Feel free to check it out, and if you like it, give it a Star to encourage the author.

In the previous chapter, we explored the core mechanisms of classes and objects. Now, we shift our focus from "a single class" to "relationships between classes"—how C++ uses inheritance to express "is-a" semantics, and how it uses polymorphism to achieve "one interface, multiple behaviors."

Inheritance and polymorphism are **the two most easily abused and misunderstood** features in object-oriented programming. When beginners think of inheritance, "code reuse" and "writing less code" often come to mind. However, in engineering practice, the real problem inheritance solves is not saving a few lines of code, but **expressing semantic relationships between types**. Polymorphism takes this a step further by allowing you to manipulate objects of different types through a unified interface, with the actual behavior determined at runtime.

## 1. Inheritance

### 1.1 The Essence of Inheritance: Expressing "Is-a" Relationships

The core of inheritance is to express a very specific relationship: **a derived class is-a base class**. For example, a temperature sensor "is a sensor," and UART "is a communication interface." Inheritance is only natural when this semantic holds true.

I want to emphasize something: especially in critical design scenarios—**using correct semantics is always better than taking shortcuts! Using correct semantics is always better than taking shortcuts! Using correct semantics is always better than taking shortcuts!** You probably don't want to leave a mess for your future self and your colleagues to clean up.

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

In this design, `SensorBase` is responsible for defining "the capabilities and state that all sensors possess"—ID, initialization status, and so on. Derived classes only need to focus on their own specific behaviors. The `protected` members in the base class are prepared exactly for this scenario: they are not exposed externally, but they allow derived classes to use these internal states within a reasonable scope.

### 1.2 Construction and Destruction Order

When creating a derived class object, the construction order is **from base to derived**—the base class sub-object is constructed first, followed by the derived class's own members. The destruction order is exactly the reverse—**from derived to base**. This order makes perfect sense: the derived class constructor might depend on base class members being in a valid state, and during destruction, the derived class must clean up its own resources before the base class can be safely destructed.

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

In the derived class constructor, you need to specify which base class constructor to call via the initializer list. If you don't specify, the compiler calls the base class's default constructor. If the base class lacks a default constructor—for example, if it only defines a parameterized constructor—you must explicitly call it in the derived class's initializer list:

```cpp
class TemperatureSensor : public SensorBase {
public:
    TemperatureSensor(int id)
        : SensorBase(id) {  // 必须显式调用基类构造函数
        // ...
    }
};
```

### 1.3 Access Control for Inheritance

The inheritance mode itself is subject to access control, but this topic often causes confusion. C++ supports three inheritance modes:

- **Public inheritance (`public`)**: Base class `public` members remain `public` in the derived class, and `protected` members remain `protected`. This is the most commonly used inheritance mode, maintaining the "is-a" semantics.
- **Protected inheritance (`protected`)**: Base class `public` and `protected` members both become `protected` in the derived class.
- **Private inheritance (`private`)**: Base class `public` and `protected` members both become `private` in the derived class.

In embedded engineering, in the vast majority of cases, you should only use **public inheritance**. The reason is simple: only public inheritance maintains the "is-a" semantics and ensures that using derived class objects through a base class interface is safe and intuitive. `protected` inheritance and `private` inheritance are more of a language-level trick with very limited use cases.

### 1.4 Object Slicing

When using inheritance, there is a very easily overlooked pitfall—**object slicing**. When you use a derived class object to initialize or assign to a base class object (not a pointer or reference), the derived class-specific parts get "sliced off":

```cpp
TemperatureSensor temp(1);
SensorBase base = temp;  // 对象切片！

// base 现在是一个 SensorBase 对象
// TemperatureSensor 特有的成员（offset, read_celsius()）全部丢失
```

The reason for object slicing is straightforward: `base` is a variable of type `SensorBase`, and its memory space is only large enough to hold `SensorBase` members. When you assign `temp` to it, the compiler only copies the `SensorBase` part, and the rest is discarded.

The way to avoid object slicing is also simple: **use references or pointers instead of value types directly**. Manipulating derived class objects through base class references or pointers does not cause slicing:

```cpp
TemperatureSensor temp(1);
SensorBase& ref = temp;   // OK：引用，不会切片
SensorBase* ptr = &temp;  // OK：指针，不会切片
```

### 1.5 Multiple Inheritance and Diamond Inheritance

Multiple inheritance allows a class to inherit from multiple base classes simultaneously. In some scenarios, this is natural—for example, a device that has both "readable" and "writable" capabilities:

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

This kind of "interface inheritance" style of multiple inheritance is relatively safe. But the real trouble with multiple inheritance lies in **diamond inheritance**—when two base classes themselves inherit from a common base class:

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

At this point, a `Multiple` object internally contains **two copies** of the `Base` sub-object—one from `Derived1` and one from `Derived2`. When accessing `value`, the compiler doesn't know which copy you want and directly reports an ambiguity error.

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

Virtual inheritance ensures that no matter how many times `Base` is indirectly inherited in the inheritance chain, the final object contains only one copy of the `Base` sub-object. However, the cost of virtual inheritance is: more complex object layout, more obscure constructor calling rules, and potentially an extra level of indirection at runtime. In embedded environments, this complexity is usually not worth it.

A relatively safe consensus is: **use multiple inheritance only for "interface inheritance" (where base classes consist entirely of pure virtual functions), and not for "implementation inheritance."** If your multiple inheritance base classes contain data members or concrete implementations, you are probably already heading down a complex path.

## 2. Polymorphism

### 2.1 What Is Polymorphism

If inheritance answers "what are you," then polymorphism answers "how do you behave right now." Polymorphism allows you to manipulate a derived class object through a base class pointer or reference, and invoke the derived class's implementation at runtime.

The core of this capability lies in **virtual functions**. When a member function is declared as `virtual`, it means: **which specific implementation to call cannot be determined until runtime, rather than being statically bound at compile time**. This is the fundamental reason why polymorphism works.

Let's look at a most basic example first:

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

Now we can call `speak()` through a base class pointer, and the specific behavior depends on the actual type of the object the pointer points to:

```cpp
void make_sound(Animal* animal) {
    animal->speak();  // 运行时决定调用哪个版本
}

Dog dog;
Cat cat;
make_sound(&dog);  // 输出 "Woof!"
make_sound(&cat);  // 输出 "Meow!"
```

Although this example is simple, it already demonstrates the core value of polymorphism: the `make_sound` function completely doesn't know, and doesn't need to know, what the specific subtype of `Animal` is. It only needs to know "this thing can `speak()`." This ability of **the caller not depending on concrete types, but only on abstract interfaces**, is the cornerstone of large-scale system architecture.

### 2.2 The Underlying Mechanism of Virtual Functions: vtable

Understanding the underlying mechanism of polymorphism helps us make sound engineering decisions in embedded scenarios. Here we provide a brief overview.

When you declare a virtual function in a class (or inherit one), the compiler generates a **virtual table (vtable)** for that class. This table is an array of function pointers, where each entry corresponds to a virtual function and stores the address of the actual implementation for that class.

At the same time, every object containing virtual functions has an extra hidden pointer in its memory layout—the **vptr**—which points to the vtable of the object's class.

When calling `animal->speak()`, the code generated by the compiler roughly does the following:

1. Uses the `animal` pointer to find the starting memory address of the object
2. Retrieves the `vptr` from the object to find the corresponding vtable
3. Looks up the entry corresponding to `speak()` in the vtable
4. Initiates an indirect call through the function pointer

This is why a virtual function call has one more level of indirection than a normal function call—it needs to look up the actual function to call via the vtable at runtime. **This "indirect jump" is the entire runtime overhead of polymorphism.**

On a PC, the overhead of an indirect jump is negligible—it might just be one extra cache access. But in resource-constrained, real-time-sensitive embedded systems, this overhead needs to be taken seriously. Specifically:

- **Code size**: Every class with virtual functions has a vtable, which consumes Flash space
- **Object size**: Every object has an extra `vptr` (usually the size of a pointer, four or eight bytes), which can be significant on MCUs with tight RAM
- **Call overhead**: One indirect jump, which may affect the pipeline and branch prediction

Therefore, a very important engineering judgment is: **polymorphism is only worth using when the "benefits of decoupling" clearly outweigh the "runtime overhead and complexity."**

### 2.3 Pure Virtual Functions and Abstract Classes

A pure virtual function is a special kind of virtual function—it has no implementation in the base class and requires all derived classes to provide their own implementation. A class containing at least one pure virtual function is called an **abstract class**, and it cannot be directly instantiated.

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

Abstract classes are not meant for creating objects, but for **defining a capability contract**. A derived class must fully implement all pure virtual functions to become a "valid concrete type":

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

Now, the upper-level protocol processing logic can be completely agnostic to whether the underlying transport is UART or SPI:

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

This design is particularly common in the driver layer. UART, SPI, and I2C look completely different, but at the "send data" and "receive data" level, they can share a common abstract interface. The upper-level protocol processing logic depends only on the interface, not on any specific hardware, which greatly improves code portability and testability.

### 2.4 Virtual Destructors

Virtual destructors are an extremely easily overlooked, yet extremely critical detail in polymorphism.

**As long as you intend to manage the lifetime of a derived class object through a base class pointer, the base class's destructor must be virtual.** Otherwise, when `delete` a base class pointer, only the base class's destructor is called, and the resources held in the derived class will be completely unreleased.

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

A simple but almost ironclad rule of thumb is: **as long as a class has any virtual functions, you must also declare its destructor as virtual**. This costs nothing, but it prevents a class of problems that manifest in embedded systems as "inexplicable memory leaks" or "peripheral state anomalies" and are extremely difficult to track down.

### 2.5 When to Use Polymorphism in Embedded Systems

In actual embedded engineering, the most valuable use cases for polymorphism often appear in "driver abstraction" and "protocol decoupling." However, not all scenarios are suitable for polymorphism.

**Scenarios suitable for polymorphism**: The system needs to support multiple hardware variants (such as a sensor driver compatible with both UART and SPI communication); or platform-specific code needs to be isolated into concrete implementation classes when porting across different platforms; or you want to extend system behavior by adding new derived classes without modifying existing code.

**Scenarios not suitable for polymorphism**: The system has only one fixed, unchanging hardware configuration; the number of objects is very large (every object needs an extra vptr, which may be unaffordable on an MCU with only a few KB of RAM); or there are extreme real-time requirements (although the indirect jump of a virtual function call has overhead, the key issue is non-determinism—you cannot determine the target address at compile time, which is unacceptable for some hard real-time systems).

My advice is: in embedded development, **start without polymorphism, until you clearly feel the need to "use a unified interface to manipulate different implementations."** Don't introduce polymorphism just to make "the code look more OOP"—this is typical over-engineering.

## Summary

In this chapter, we learned about inheritance and polymorphism—the two most core mechanisms in C++'s object-oriented system. Inheritance is used to express "is-a" semantic relationships, with public inheritance being the overwhelmingly preferred choice. Polymorphism achieves runtime behavior dispatch through virtual functions, allowing us to manipulate different derived class objects through a unified base class interface. Virtual destructors are the safety baseline when using polymorphism—forgetting them leads to resource leaks.

Inheritance and polymorphism are powerful tools, but they also introduce more complex object relationships, harder-to-trace call paths, and additional runtime overhead. In embedded development, the criterion for deciding whether to use them is very simple: **do the benefits of decoupling clearly outweigh the introduced complexity and overhead?**

In the next chapter, we will learn about operator overloading—the ability to make custom types participate in expression evaluation just like built-in types.
