---
chapter: 0
cpp_standard:
- 11
- 14
- 17
- 20
description: 'From C structs to C++ classes: access control, constructors and destructors,
  initializer lists, the `this` pointer, static members, `const` member functions,
  friends, `explicit`, and `mutable`—we cover every detail.'
difficulty: beginner
order: 3
platform: host
prerequisites:
- C++98入门：命名空间、引用与作用域解析
- C++98函数接口：重载与默认参数
reading_time_minutes: 23
related:
- C++98面向对象：继承与多态
- C++98运算符重载
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: 'C++98 Object-Oriented: In-Depth Analysis of Classes and Objects'
translation:
  source: documents/vol1-fundamentals/03C-cpp98-classes-and-objects.md
  source_hash: 9b6ad50bfe01601ef580fbeb12bd8511ca05959ca25af2c56f9058401d98aca6
  translated_at: '2026-06-16T03:32:02.125831+00:00'
  engine: anthropic
  token_count: 4132
---
# C++98 Object-Oriented: Deep Dive into Classes and Objects

> The complete repository is available at [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP). Feel free to visit, and if you like it, give the project a Star to motivate the author.

Classes and objects are the core concepts of C++ object-oriented programming. However, in embedded contexts, they are often misunderstood as being "heavy," "slow," or "flashy." In reality, classes do not equal complexity, and OOP does not strictly require inheritance or polymorphism. **In resource-constrained embedded systems with clear business logic, the core value of a class is singular: binding "state" with the "code that operates on that state."**

In other words, the primary value of a class is not abstraction, but **constraint**.

In this chapter, starting from C structs, we will gradually transition to C++ classes, dissecting every key concept—including constructors and destructors, member initializer lists, the `this` pointer, static members, `const` member functions, friends, and the `explicit` and `mutable` keywords, which are often overlooked but very useful.

## 1. From struct to class

### 1.1 Limitations of C structs

In C, we use structs to organize data and independent functions to manipulate that data. For example, LED control code in C style looks like this:

```cpp
// C style: Data and logic are separated
typedef struct {
    uint8_t pin;
    bool    state;
} LED_t;

void LED_init(LED_t* led, uint8_t pin) {
    led->pin = pin;
    led->state = false;
    // Configure GPIO as output...
}

void LED_on(LED_t* led) {
    led->state = true;
    // Set GPIO high...
}

void LED_off(LED_t* led) {
    led->state = false;
    // Set GPIO low...
}
```

This code works, but it has a structural problem: the association between `LED_init`, `LED_on`, `LED_off`, and `LED_t` is **maintained entirely by naming conventions**. There is no syntactic mechanism to prevent you from writing something absurd like `LED_on(&some_other_struct)`—the compiler won't complain because `LED_on` accepts a `LED_t*`, and you might happen to pass a pointer to the wrong struct.

### 1.2 C++ class: Binding data and operations together

C++ classes solve this problem by gathering data (member variables) and operations (member functions) into a single syntactic unit:

```cpp
// C++ style: Data and logic are bound together
class LED {
public:
    LED(uint8_t pin) : pin_(pin), state_(false) {
        // Configure GPIO as output...
    }

    void on() {
        state_ = true;
        // Set GPIO high...
    }

    void off() {
        state_ = false;
        // Set GPIO low...
    }

private:
    uint8_t pin_;
    bool    state_;
};
```

Now, when using it, you can only operate on the `LED` through its public interface:

```cpp
LED led(PC13);  // Constructor binds hardware and state
led.on();       // No need to pass a pointer manually
```

Compared to the C version, the most obvious improvement is that you no longer need to manually pass the struct pointer. The `led.on()` call inherently knows which LED it is operating on—because `on()` is a member function of the `LED` object, and the compiler automatically passes the address of `led` as a hidden parameter. Behind the scenes, this is actually the `this` pointer we will discuss next.

### 1.3 Access control: public, private, protected

C++ provides three access control keywords to manage the visibility of class members.

`private` members are accessible only by the class's own member functions. In the `LED` class above, `pin_` and `state_` are `private`, meaning you cannot read or write them directly from outside the class:

```cpp
LED led(PC13);
// led.pin_ = 5;   // Compile error: pin_ is private!
// bool s = led.state_; // Compile error: state_ is private!
```

`private` is not to "defend against hackers," but to **syntactically tell the user: "These are internal details you shouldn't touch."** You can certainly bypass it via pointer casts or macros, but that falls into undefined behavior. For most engineering code, `private` serves as strong self-documentation—it lets readers distinguish between "interface" and "implementation details" at a glance.

`public` members are visible to all code and form the external interface of the class. `protected` members are visible to the class itself and its derived classes—we will discuss this in detail when we cover inheritance.

Regarding the difference between `class` and `struct`, there is actually only one: `class` defaults to `private` access, while `struct` defaults to `public`. Semantically, `struct` is usually used to express "a collection of data" (C-style), while `class` is used to express "objects with behavior." However, the compiler does not enforce this convention—you can write a `class` with all `public` members, or a `struct` with member functions. Choosing one is mostly about conveying your design intent to the reader.

## 2. Constructors and Destructors

### 2.1 Constructors: Bringing objects into a valid state

A constructor is a special member function that is automatically called when an object is created. It is responsible for bringing the object into a **valid, usable state**. The constructor name matches the class name, has no return type (not even `void`), can take parameters, and supports overloading.

Let's look at a more complete example of hardware resource management—a UART port wrapper class:

```cpp
class Uart {
public:
    // Constructor: Initialize hardware and state
    Uart(USART_TypeDef* hw, uint32_t baudrate)
        : hw_(hw), baudrate_(baudrate) {
        // Enable peripheral clock, configure pins, set baudrate...
    }

    void send(const uint8_t* data, size_t len) {
        // Send data via hw_...
    }

private:
    USART_TypeDef* hw_;
    uint32_t       baudrate_;
};
```

When used, the object is in a usable state immediately upon creation:

```cpp
Uart uart1(USART1, 115200); // Object is ready to use immediately
uart1.send("Hello", 5);
```

The core value of a constructor is that **it eliminates the possibility of "forgetting to initialize."** In C, you might forget to call `LED_init`, then use an uninitialized struct to send data—with disastrous consequences. In C++, object creation and initialization are bound together; it is impossible to have an object that is "created but not initialized."

### 2.2 Destructors: Cleaning up when the object lifecycle ends

The destructor is the constructor's "partner," automatically called when an object is destroyed. The destructor name is `~` followed by the class name, has no parameters, and no return type:

```cpp
class Uart {
public:
    Uart(USART_TypeDef* hw, uint32_t baudrate) { /* ... */ }

    // Destructor: Release resources
    ~Uart() {
        // Disable UART, reset pins, maybe free DMA buffer...
    }
};
```

In embedded systems, destructors are particularly useful for releasing hardware resources: disabling peripherals, releasing DMA channels, or resetting pins to default states. This pattern of "acquire in constructor, release in destructor" has a famous name—**RAII (Resource Acquisition Is Initialization)**. RAII is the core idea of C++ resource management, and we will cover it in depth in later chapters. For now, just remember one thing: **if you acquire a resource in the constructor, you must release it in the destructor.**

The timing of destruction depends on the object's storage duration. Local objects are destroyed when leaving scope, global/static objects at program end, and objects allocated via `new` are only destroyed when `delete` is called.

### 2.3 Default constructor

If you do not define any constructor for a class, the compiler automatically generates a **default constructor**—a parameterless constructor that does nothing. However, once you define any constructor (even one with parameters), the compiler stops generating the default constructor.

```cpp
class Point {
public:
    Point(int x, int y) : x_(x), y_(y) {} // User-defined constructor
    // No default constructor generated!
};

// Point p; // Compile error: no matching constructor
```

If you need both a parameterized constructor and a parameterless default constructor, you must explicitly define one:

```cpp
class Point {
public:
    Point() : x_(0), y_(0) {} // Explicit default constructor
    Point(int x, int y) : x_(x), y_(y) {}
};
```

## 3. Member Initializer Lists

### 3.1 Why use initializer lists

In constructors, the member initializer list is the **preferred way to initialize** class members. Many people habitually use assignment statements in the constructor body to "initialize" member variables, but in C++ semantics, this is not true initialization—it is "default construct first, then assign." For certain member types, this "construct-then-assign" approach is even illegal.

Let's look at the difference:

```cpp
class Demo {
public:
    // Method A: Assignment in body (Default construct + Assign)
    Demo(int val) {
        member_ = val;
    }

    // Method B: Initializer list (Direct construct)
    Demo(int val) : member_(val) {}

private:
    SomeType member_;
};
```

The core advantage of initializer lists lies in **performance and semantic correctness**. For basic types like `int`, the performance difference is negligible. But for complex class-type members, using an initializer list avoids a default construction followed by an assignment—constructing directly with the target value saves the intermediate step.

More importantly, **`const` members and reference members can only be initialized via an initializer list**. By the time the constructor body executes, they have already been default constructed—and `const` objects cannot be reassigned, nor can references be rebound. So if you have these types of members, the initializer list is not "recommended," but the **only legal choice**.

### 3.2 Embedded applications of initializer lists

In embedded development, initializer lists have a very practical application: configuring hardware parameters directly at object construction.

```cpp
class PwmTimer {
public:
    PwmTimer(TIM_TypeDef* tim, uint32_t period, uint32_t prescaler)
        : hw_(tim)
        , period_(period)
        , prescaler_(prescaler) // Initialize members in declaration order
    {
        // Apply configuration to hardware registers
        hw_->ARR = period_;
        hw_->PSC = prescaler_;
        // Enable timer...
    }

private:
    TIM_TypeDef* hw_;
    uint32_t     period_;
    uint32_t     prescaler_;
};
```

There is a detail to note about initialization order: **the initialization order of member variables depends on their declaration order in the class definition, not the order in the initializer list**. If you write `PwmTimer(...) : prescaler_(p), period_(per)`, the compiler will initialize `hw_` first (because it is declared first), then `period_`, then `prescaler_`. So `prescaler_` can indeed get the correct value of `period_` if `period_` was declared before it. However, if your declaration order is `prescaler_` first and `period_` last, then `prescaler_` will be initialized before `period_`, reading an undefined value. Most compilers warn when the list order differs from declaration order, but it is best to keep them consistent.

## 4. The this Pointer

### 4.1 What is this

Every non-static member function has a hidden parameter at the low level—a pointer to the object that called the function. This pointer is `this`. In other words, when you write:

```cpp
led.on();
```

The compiler actually translates it into a call similar to this (pseudocode):

```cpp
// LED_on(&led); // Compiler passes 'led' implicitly
```

Inside the member function, `this` points to the current object. You can access member variables and functions through `this->`. In most cases, you do not need to explicitly write `this`—the compiler automatically resolves "bare" member names as `this->member`. However, in certain scenarios, explicit use of `this` is necessary or helpful.

The most common case is **when parameter names shadow member variable names**:

```cpp
class LED {
public:
    void set_pin(uint8_t pin) {
        // 'pin' refers to the parameter
        // 'this->pin' refers to the member
        this->pin = pin;
    }
private:
    uint8_t pin;
};
```

### 4.2 Chaining method calls

Another common application of the `this` pointer is implementing chained calls. The method is simple: the member function returns a reference to `*this`, allowing the caller to call multiple methods in one line.

```cpp
class UartConfig {
public:
    UartConfig& set_baudrate(uint32_t br) {
        baudrate_ = br;
        return *this; // Return reference to self
    }

    UartConfig& set_parity(uint8_t parity) {
        parity_ = parity;
        return *this;
    }

    void apply() { /* ... */ }

private:
    uint32_t baudrate_;
    uint8_t  parity_;
};

// Usage: Chained calls
UartConfig cfg;
cfg.set_baudrate(115200)
   .set_parity(0)
   .apply();
```

This pattern is particularly useful in embedded development for building configuration interfaces or logging outputs—each call returns itself, making the code compact to write and smooth to read.

Compared to C style, the underlying principle of chaining is the same as "functions returning a struct pointer." The difference is that C++ makes the syntax more natural through `this` and references, without needing to write `->` and `&` everywhere.

## 5. Static Members

### 5.1 Static member variables

Static member variables belong to **the class itself**, not a specific object. This means that no matter how many instances of the class you create, there is only one copy of the static member variable in memory.

This is very practical in embedded development. For example, if you want to track how many instances of a peripheral driver are currently active:

```cpp
class SpiDriver {
public:
    SpiDriver() {
        // Increment instance count on construction
        instance_count_++;
    }

    ~SpiDriver() {
        // Decrement instance count on destruction
        instance_count_--;
    }

    // Static function to access static data
    static int get_instance_count() {
        return instance_count_;
    }

private:
    static int instance_count_; // Declaration only
};

// Definition and initialization outside the class (in .cpp file)
int SpiDriver::instance_count_ = 0;
```

Note a tricky detail: **static member variables must be defined and initialized outside the class** (C++17 introduced `inline` members which can be initialized in-class, but C++98 does not support this). If you only declare `instance_count_` inside the class but forget to write `int SpiDriver::instance_count_ = 0;` in a `.cpp` file, the linker will throw an "undefined reference" error. This error is often hard to pinpoint because compilation passes, but linking fails.

### 5.2 Static member functions

Static member functions also belong to the class itself, not a specific object. Therefore, static member functions **have no `this` pointer**, which means they cannot access non-static member variables or non-static member functions—since these require `this` to locate the specific object instance.

```cpp
class SystemClock {
public:
    // Static function: Check hardware state without an instance
    static bool is_hse_ready() {
        return (RCC->CR & RCC_CR_HSERDY) != 0;
    }

    // Non-static function: Configure clock (needs instance state)
    void switch_to_hse() {
        if (is_hse_ready()) { // Call static function
            // Switch logic...
        }
    }
};
```

When calling a static member function, use the `ClassName::function()` syntax; no object needs to be created first:

```cpp
if (SystemClock::is_hse_ready()) {
    SystemClock clk; // Create instance only if needed
    clk.switch_to_hse();
}
```

This pattern of "check hardware readiness first, then create instance" is very common in embedded development, and static member functions provide exactly this capability: "related to the class but not requiring an instance."

## 6. const Member Functions

### 6.1 Semantics of const member functions

A `const` member function is a very strong semantic promise provided by C++: **this function will not modify the object's state**. It is declared by adding the `const` keyword after the function parameter list:

```cpp
class Sensor {
public:
    // Promise not to modify the object
    int read() const {
        return value_;
    }

    void set_value(int v) {
        value_ = v;
    }

private:
    int value_;
};
```

This is not just for the reader of the code, but also for the compiler. The compiler checks at compile time whether any `const` member function modifies member variables; if it finds one, it errors out. More importantly, `const` member functions are **the only member functions that can be called on a `const` object**:

```cpp
void monitor_sensor(const Sensor& s) {
    int val = s.read(); // OK: read() is const
    // s.set_value(10); // Compile error: set_value() is not const
}
```

### 6.2 The cascading effect of const correctness

`const` correctness has a very important characteristic—it is "contagious." If your function declares a `const` reference parameter, you can only call `const` member functions through that reference. And if those `const` member functions return references to other objects, those references should also be `const`. This cascading effect might seem annoying, but it actually helps you build a very strong "read-only safety net."

Let's look at a practical embedded example—a sensor reading class with caching:

```cpp
class TemperatureSensor {
public:
    // Non-const: Forces a hardware read and updates cache
    void update() {
        cached_value_ = read_hardware();
        cache_valid_ = true;
    }

    // Const: Returns cache (or reads if invalid, using mutable)
    int get_celsius() const {
        if (!cache_valid_) {
            // Cannot call non-const update(), but can modify mutable members
            cached_value_ = read_hardware();
            cache_valid_ = true;
        }
        return cached_value_;
    }

private:
    int  read_hardware() const; // Hardware register read

    mutable int  cached_value_; // Logical state is const, but cache can change
    mutable bool cache_valid_;
};
```

This example demonstrates a very practical design pattern: provide a non-`const` "force refresh" interface and a `const` "return cache if available" interface. The caller automatically gets different behavioral guarantees depending on whether they hold a `const` reference or a non-`const` reference.

### 6.3 A practical rule of thumb

In C++, there is a widely recognized programming guideline: **all member functions that do not modify object state should be declared `const`**. This isn't mandatory, but if you don't do it, users of your class will encounter various frustrations where "it looks readable, why won't the compiler let me?"—because someone might hold your object via a `const` reference (e.g., passed as a function parameter), at which point only `const` member functions can be called.

If you are designing a class and a member function "looks like it should just read data," but you forget to add `const`, your users will find they cannot call this "obviously read-only" function when passing the object to a function accepting a `const` reference. This error is particularly insidious because the cause is not at the call site, but in the class definition—and the error message is often just "discards qualifiers," which novices find incomprehensible.

My advice is: **develop a habit—after writing every member function, ask yourself "Does this function need to modify the object?" If the answer is no, add `const` immediately.**

## 7. Friends (friend)

### 7.1 What is a friend

A `friend` is a mechanism in C++ that allows you to actively **break the encapsulation boundary**—granting an external function or external class access to the current class's `private` and `protected` members.

```cpp
class PacketBuffer {
    friend void serialize_buffer(const PacketBuffer& buf); // Friend function

public:
    PacketBuffer(size_t size) : size_(size), data_(new uint8_t[size]) {}
    ~PacketBuffer() { delete[] data_; }

private:
    size_t   size_;
    uint8_t* data_;
};

// External function can access private members
void serialize_buffer(const PacketBuffer& buf) {
    // Direct access to private members
    write_to_network(buf.data_, buf.size_);
}
```

### 7.2 The danger of friends

The existence of friends is not inherently evil, but it is almost always a **smell**. Friends mean you are actively exposing internal implementation details to external code. From a design perspective, this breaks encapsulation—which is a core value of classes.

Most scenarios requiring friends can be avoided through better design. For example, the serialization example above could be implemented by providing a `public` accessor interface, without exposing the entire internal array:

```cpp
class PacketBuffer {
public:
    // Public interface: safe access to internal data
    const uint8_t* data() const { return data_; }
    size_t         size() const { return size_; }
};

// External function uses public interface
void serialize_buffer(const PacketBuffer& buf) {
    write_to_network(buf.data(), buf.size());
}
```

This design is clearly safer—`data()` only exposes a read-only pointer and size, and external code cannot modify the internal data. The friend version exposes the entire `data_` array to the `serialize_buffer` function; if `serialize_buffer` has a bug, it could write out of bounds.

So my advice is: **if a class needs a lot of friends to work, it probably shouldn't have been designed as a class in the first place**. Friends should be a last resort, not a routine tool. When your first reaction is "add a friend," stop and think: is there an alternative that doesn't break encapsulation?

## 8. The explicit Keyword

### 8.1 The problem with implicit conversion

C++ allows constructors to perform implicit type conversion. That is, if you have a constructor that accepts a single parameter, the compiler will automatically call that constructor when needed, "quietly" converting the parameter type to the class type.

```cpp
class UartId {
public:
    UartId(int id) : id_(id) {} // Can be implicitly called
    // ...
};

void configure_uart(UartId uid);

// Usage
configure_uart(1); // Implicitly converts int 1 to UartId
```

This code compiles, but the `configure_uart(1)` call is semantically ambiguous—you passed an `int`, but the function expects a `UartId` object. The compiler is "kind enough" to do the conversion for you, but this "kindness" is often the source of disaster in large projects: you might write the wrong parameter type somewhere, and instead of erroring, the compiler does a conversion you didn't expect, and the program runs in a baffling way.

### 8.2 The role of explicit

The `explicit` keyword prohibits this implicit conversion. Once added, the constructor can only be used in explicit calls:

```cpp
class UartId {
public:
    explicit UartId(int id) : id_(id) {} // Implicit conversion disabled
    // ...
};

// configure_uart(1); // Compile error: no implicit conversion
configure_uart(UartId(1)); // OK: explicit conversion
```

My advice is: **all single-argument constructors should be `explicit`, unless you very clearly need implicit conversion**. This is a near-zero-cost defensive measure that avoids many bugs caused by implicit conversion. Moreover, `explicit` only affects implicit calls—explicit calls are unaffected, so it doesn't restrict any functionality you actually need.

## 9. The mutable Keyword

### 9.1 The role of mutable

The `mutable` keyword allows modifying member variables marked as `mutable` inside a `const` member function. This sounds like violating the `const` promise, but there are perfectly reasonable use cases.

We saw a caching example earlier when discussing `const` member functions. Here is a more complete version:

```cpp
class Sensor {
public:
    int read() const {
        // 'cached_value_' and 'cache_dirty_' are mutable
        if (cache_dirty_) {
            cached_value_ = read_adc(); // Hardware read
            cache_dirty_ = false;
        }
        return cached_value_;
    }

private:
    int read_adc() const;

    mutable int  cached_value_; // Can be modified in const functions
    mutable bool cache_dirty_ = true;
};
```

In this example, `read()` is declared `const` because its external promise is "it does not change the sensor's logical state"—from the user's perspective, the sensor hasn't changed before and after calling `read()`. However, internally, `read()` does modify the cache and the dirty flag—these are **implementation details**, not part of the logical state.

### 9.2 When to use mutable

The scenarios for `mutable` are very clear: **member variables that are implementation details and do not affect the object's logical state**. Typical scenarios include caching, lazy evaluation, debug counters, mutexes, etc.

But `mutable` can also be abused. If you find yourself frequently modifying `mutable` members in `const` functions, and these modifications affect the object's "observable behavior," there is likely a problem with your `const` design—either the function shouldn't be `const`, or those members shouldn't be `mutable`.

A simple criterion is: **if you remove the `mutable` marker and the related modification code, does the function's external behavior remain exactly the same?** If the answer is "yes," then `mutable` is justified; if "no," you need to re-examine the design.

## Run Online

Run the comprehensive class basics example online to observe construction, destruction, the `this` pointer, and static members:

<OnlineCompilerDemo
  title="C++98 Classes & Objects: Construction/Destruction, this, static, mutable"
  source-path="code/examples/vol1/16_cpp98_classes_objects.cpp"
  description="Run online to observe StringBuilder chaining, Sensor lifecycles, and static member counting."
  allow-run
/>

## Summary

In this chapter, we deeply analyzed the core mechanisms of C++ classes and objects. Starting from C structs, we saw how `class` binds data and operations via access control; constructors and destructors ensure "acquire is initialization" and "leave is cleanup"; member initializer lists provide double guarantees for performance and semantic correctness; the `this` pointer explains how member functions "know" which object they are operating on; static members provide class-level shared state; `const` member functions establish a strong "read-only" contract; and `friend`, `explicit`, and `mutable` are three tools for "precise control," each with its own use cases and boundaries.

In the next article, we will extend the concept of a single class to a type hierarchy—seeing how C++ uses inheritance and polymorphism to organize relationships between multiple classes.
