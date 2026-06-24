---
chapter: 0
cpp_standard:
- 11
- 14
- 17
- 20
description: The leap from C structs to C++ classes—access control, constructors and
  destructors, initializer lists, the `this` pointer, static members, `const` member
  functions, friends, `explicit`, and `mutable`, covering every detail.
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
title: 'C++98 Object-Oriented: Deep Dive into Classes and Objects'
translation:
  source: documents/vol1-fundamentals/03C-cpp98-classes-and-objects.md
  source_hash: 80a5f011f0fc9b591fd75685818e09a67a641a3810fa9f8b6a3bc627cb93e9f7
  translated_at: '2026-06-24T00:27:59.019206+00:00'
  engine: anthropic
  token_count: 4132
---
# C++98 OOP: Deep Dive into Classes and Objects

> The full repository is available at [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP). Feel free to visit, and if you like it, give the project a Star to motivate the author.

Classes and objects are the core concepts of C++ Object-Oriented Programming (OOP). However, in the embedded context, they are often misunderstood as being "heavy," "slow," or "flashy." In reality, classes do not equal complexity, and OOP does not strictly require inheritance or polymorphism. **In resource-constrained embedded systems with clear business logic, the core value of a class is singular: binding "state" with the "code that operates on that state."**

In other words, the primary value of a class is not abstraction, but **constraint**.

In this chapter, we will start from C structs and gradually transition to C++ classes, dissecting every key concept—including constructors and destructors, member initializer lists, the `this` pointer, static members, `const` member functions, friends, and the `explicit` and `mutable` keywords, which are often overlooked but extremely useful.

## 1. From struct to class

### 1.1 Limitations of C structs

In C, we use structs to organize data, and then use separate functions to operate on that data. For example, LED control code in C style might look like this:

```c
// C 风格：数据和操作分离
struct LED {
    int pin;
    bool state;
};

void led_init(struct LED* led, int pin) {
    led->pin = pin;
    led->state = false;
    gpio_init(pin, OUTPUT);
}

void led_on(struct LED* led) {
    led->state = true;
    gpio_write(led->pin, HIGH);
}

void led_off(struct LED* led) {
    led->state = false;
    gpio_write(led->pin, LOW);
}
```

This code works, but it has a structural problem: the association between the functions `led_init`, `led_on`, `led_off`, and the `struct LED` relies **entirely on naming conventions**. There is no syntactic mechanism to prevent you from writing an absurd call like `led_on(&uart_config)`. The compiler won't complain because `led_on` accepts a `struct LED*`, and you might just happen to pass in a pointer to the wrong structure.

### 1.2 C++ Classes: Binding Data and Operations Together

C++ classes solve this problem by grouping data (member variables) and operations (member functions) into a single syntactic unit:

```cpp
class LED {
private:
    int pin;
    bool state;

public:
    LED(int pin_number) : pin(pin_number), state(false) {
        gpio_init(pin, OUTPUT);
    }

    void on() {
        state = true;
        gpio_write(pin, HIGH);
    }

    void off() {
        state = false;
        gpio_write(pin, LOW);
    }

    void toggle() {
        state = !state;
        gpio_write(pin, state ? HIGH : LOW);
    }

    bool is_on() const {
        return state;
    }
};
```

When using it now, we can only operate on the `LED` class through its public interface:

```cpp
LED led(5);    // 构造时指定引脚号
led.on();      // 点亮
led.toggle();  // 切换状态
bool on = led.is_on();  // 查询状态
```

Compared to the C version, the most obvious improvement is that you no longer need to manually pass a struct pointer. The `led.on()` call inherently knows which LED it is operating on—because `on()` is a member function of the `led` object, and the compiler automatically passes the address of `led` as a hidden parameter. Behind the scenes, this is actually the `this` pointer we will discuss next.

### 1.3 Access Control: public, private, protected

C++ provides three access control keywords to manage the visibility of class members.

`private` members are accessible only by the class's own member functions. In the `LED` class above, `pin` and `state` are `private`, which means you cannot read or write them directly from outside the class:

```cpp
LED led(5);
// led.pin = 10;   // 编译错误！pin 是 private 的
// led.state = true; // 编译错误！state 是 private 的
led.on();          // OK，on() 是 public 的
```

`private` is not meant to "stop hackers"; rather, it serves to **tell users at the syntactic level: what you shouldn't touch**. You can certainly bypass it through various means (such as force-casting pointers or macro definitions), but that falls into the realm of undefined behavior (UB). For most engineering code, `private` acts as a strong form of self-documentation—it allows readers to instantly distinguish between the "interface" and "implementation details."

`public` members are visible to all code and constitute the external interface of the class. `protected` members are visible to the class itself and its derived classes—we will discuss this in detail when we cover inheritance.

Regarding the difference between `class` and `struct`, there is actually only one: the default access specifier for a `class` is `private`, while for a `struct` it is `public`. Semantically, `struct` is typically used to express "a collection of data" (C-style), whereas `class` is used to express "objects with behavior." However, the compiler does not enforce this convention—you could perfectly well write a `class` with all `public` members, or a `struct` with member functions. The choice is more about conveying your design intent to the reader.

## 2. Constructors and Destructors

### 2.1 Constructors: Bringing Objects into a Valid State

A constructor is a special member function that is automatically called when an object is created. It is responsible for bringing the object into a **valid, usable state**. The constructor has the same name as the class, has no return type (not even `void`), can accept parameters, and supports overloading.

Let's look at a more complete example of hardware resource management—a UART port wrapper class:

```cpp
class UARTPort {
private:
    int port_number;
    int baudrate;
    bool initialized;

public:
    // 构造函数：初始化 UART 硬件
    UARTPort(int port, int baud) : port_number(port), baudrate(baud), initialized(false) {
        // 配置硬件引脚复用
        configure_pins(port_number);
        // 设置波特率
        set_baudrate(baudrate);
        // 启用 UART 外设时钟
        enable_clock(port_number);

        initialized = true;
    }

    void send(const uint8_t* data, size_t length) {
        if (!initialized) return;
        // 发送数据
    }

    bool is_initialized() const {
        return initialized;
    }
};
```

Once created, the object is immediately ready for use:

```cpp
UARTPort uart(1, 115200);  // 构造时完成全部硬件初始化
uart.send(data, sizeof(data));
// 离开作用域时...
```

The core value of constructors lies in the fact that **they eliminate the possibility of "forgetting to initialize."** In C, you might forget to call `uart_init()` and then use an uninitialized structure to send data—with disastrous consequences. In C++, however, object creation and initialization are bound together; it is impossible to have an object that "exists but is uninitialized."

### 2.2 Destructors: Performing Cleanup at the End of an Object's Lifecycle

The destructor is the constructor's "partner," automatically invoked when the object is destroyed. A destructor is named with a `~` followed by the class name, takes no parameters, and has no return type:

```cpp
class UARTPort {
private:
    int port_number;
    // ... 其他成员

public:
    UARTPort(int port, int baud) {
        // 初始化硬件
    }

    ~UARTPort() {
        // 关闭 UART
        disable_uart(port_number);
    }
};
```

In embedded systems, destructors are particularly well-suited for releasing hardware resources: turning off peripherals, releasing DMA channels, or resetting pins to their default states. This pattern of "acquire at construction, release at destruction" has a famous name—**RAII (Resource Acquisition Is Initialization)**. RAII is the core concept of resource management in C++, and we will cover it in depth in later chapters. For now, just remember one thing: **if you acquire a resource in a constructor, you must release it in the destructor**.

The timing of an object's destruction depends on its storage duration. Local objects are destroyed when they go out of scope, global or static objects are destroyed when the program ends, and objects dynamically allocated via `new` are only destroyed when `delete` is called.

### 2.3 Default Constructor

If you do not define any constructors for a class, the compiler automatically generates a **default constructor**—a parameterless constructor that does nothing. However, as soon as you define any constructor (even one with parameters), the compiler stops generating the default constructor automatically.

```cpp
class Sensor {
private:
    int pin;

public:
    Sensor(int p) : pin(p) {}  // 定义了一个有参构造函数
    // 此时编译器不再生成默认构造函数
};

Sensor s1(5);   // OK
Sensor s2;      // 编译错误！没有默认构造函数可用
```

If we need both a parameterized constructor and a parameterless default constructor, we can explicitly define one:

```cpp
class Sensor {
private:
    int pin;

public:
    Sensor() : pin(0) {}       // 默认构造函数
    Sensor(int p) : pin(p) {}  // 带参数的构造函数
};
```

## 3. Member Initializer List

### 3.1 Why use an initializer list

In constructors, the member initializer list is the preferred way to **initialize** class members. Many developers are accustomed to using assignment statements inside the constructor body to "initialize" member variables. However, under C++ semantics, this is not true initialization—it is "default construct, then assign." For certain member types, this "construct then assign" approach is not even valid.

Let's look at the difference between the two:

```cpp
class Example {
private:
    int x;
    int y;
    const int max_value;  // const 成员
    int& ref;             // 引用成员

public:
    // 方式一：初始化列表（推荐）
    Example(int a, int b, int max, int& r)
        : x(a), y(b), max_value(max), ref(r) {
        // 构造函数体可以为空
    }

    // 方式二：构造函数体内赋值（不推荐，而且对 const/引用成员根本不可行）
    // Example(int a, int b, int max, int& r) {
    //     x = a;
    //     y = b;
    //     max_value = max;  // 编译错误！const 成员不能赋值
    //     ref = r;          // 编译错误！引用必须在初始化时绑定
    // }
};
```

The core advantages of initializer lists lie in **performance and semantic correctness**. For fundamental types like `int`, the performance difference between the two approaches is negligible. However, for complex class type members, using an initializer list avoids a default construction followed by an assignment—instead, it constructs directly with the target value, eliminating the intermediate steps.

More importantly, **`const` members and reference members can only be initialized via an initializer list**. This is because, by the time the constructor body executes, they have already been default constructed—and `const` objects cannot be reassigned, nor can references be rebound. Therefore, if you have members of these two types, using an initializer list is not just "recommended," but the **only valid choice**.

### 3.2 Embedded Applications of Initializer Lists

In embedded development, initializer lists have a very practical application: configuring hardware parameters directly during object construction.

```cpp
class PWMChannel {
private:
    int channel;
    int frequency;

public:
    PWMChannel(int ch, int freq)
        : channel(ch), frequency(freq) {
        // 配置硬件定时器
        configure_timer(channel, frequency);
    }
};
```

One detail regarding initialization order requires attention: **the initialization order of member variables depends on their declaration order in the class definition, not the order in the initializer list**. If you write `: b(a), a(10)` in the initializer list, the compiler will initialize `a` first (because it was declared first), and then initialize `b`—so `b(a)` correctly receives the initialized value of `a`. However, if your declaration order lists `b` before `a`, then `a` remains uninitialized when `b(a)` runs, resulting in undefined behavior. Most compilers will issue a warning if the initializer list order differs from the declaration order, but it is best to maintain the habit of keeping them consistent.

## 4. The `this` Pointer

### 4.1 What is `this`

Every non-static member function has a hidden parameter at the low level—a pointer to the object that called the function. This pointer is `this`. In other words, when you write:

```cpp
led.on();
```

The compiler effectively translates this into a call similar to the following (pseudocode):

```cpp
LED::on(&led);  // 把 led 的地址作为 this 指针传入
```

Inside a member function, `this` points to the current object. We can access member variables and member functions through `this`. In most cases, we do not need to write `this` explicitly—the compiler automatically resolves "bare" member names to `this->member`. However, in certain scenarios, explicit use of `this` is either required or helpful.

The most common case is when **parameter names conflict with member variable names**:

```cpp
class Sensor {
private:
    int pin;

public:
    Sensor(int pin) : pin(pin) {}  // 初始化列表中，前面的 pin 是成员，后面的 pin 是参数

    void set_pin(int pin) {
        this->pin = pin;  // this->pin 是成员变量，pin 是参数
    }
};
```

### 4.2 Chained Method Calls

Another common application of the `this` pointer is implementing chained calls. The approach is straightforward: a member function returns a reference to `*this`, allowing the caller to invoke multiple methods consecutively in a single line of code.

```cpp
class StringBuilder {
private:
    char buffer[256];
    size_t length;

public:
    StringBuilder() : length(0) {
        buffer[0] = '\0';
    }

    StringBuilder& append(const char* str) {
        while (*str && length < 255) {
            buffer[length++] = *str++;
        }
        buffer[length] = '\0';
        return *this;  // 返回自身的引用
    }

    StringBuilder& append_char(char c) {
        if (length < 255) {
            buffer[length++] = c;
            buffer[length] = '\0';
        }
        return *this;
    }

    const char* c_str() const {
        return buffer;
    }
};

// 链式调用
StringBuilder sb;
sb.append("Hello").append(", ").append("World!").append_char('\n');
printf("%s", sb.c_str());
```

This pattern is particularly useful in embedded development for building configuration interfaces or logging systems—each call returns the object itself, making the code compact to write and fluent to read.

Compared to the approach in C, the underlying principle of method chaining is actually the same as "returning a struct pointer" in C. The difference is that C++ makes the syntax more natural through `this` and references, eliminating the need to constantly use `->` and the address-of operator.

## 5. Static Members

### 5.1 Static Member Variables

Static member variables belong to the **class itself**, rather than to a specific object. This means that no matter how many instances of the class we create, there is only one copy of the static member variable in memory.

This is very practical in embedded development. For example, if we want to track how many instances of a peripheral driver are currently in use:

```cpp
class UARTPort {
private:
    int port_number;
    static int active_count;  // 声明静态成员

public:
    UARTPort(int port) : port_number(port) {
        active_count++;
    }

    ~UARTPort() {
        active_count--;
    }

    static int get_active_count() {
        return active_count;
    }
};

// 静态成员必须在类外定义（C++17 前的规则）
int UARTPort::active_count = 0;
```

Watch out for a common pitfall: **static member variables must be defined and initialized outside the class** (C++17 introduced `inline static` members, which allow initialization inside the class, but C++98 does not support this). If you only declare `static int active_count;` inside the class but forget to write `int UARTPort::active_count = 0;` in the `.cpp` file, the linker will report an "undefined reference" error. This error is often tricky to locate because the compilation succeeds, and only the linking step fails.

### 5.2 Static Member Functions

Static member functions also belong to the class itself, rather than to a specific object. Therefore, static member functions **do not have a `this` pointer**. This means they cannot access non-static member variables or non-static member functions, as these require `this` to locate the specific object instance.

```cpp
class UARTPort {
private:
    int port_number;
    static bool hal_initialized;

public:
    static void init_hal() {
        // 初始化硬件抽象层
        hal_initialized = true;
        // port_number = 1;  // 编译错误！静态函数不能访问非静态成员
    }

    static bool is_hal_ready() {
        return hal_initialized;
    }
};
```

When calling a static member function, we use the syntax `ClassName::functionName()`, which does not require creating an object first:

```cpp
UARTPort::init_hal();
if (UARTPort::is_hal_ready()) {
    UARTPort uart(1, 115200);
}
```

This pattern of "checking if the hardware is ready before creating an instance" is very common in embedded development. Static member functions provide exactly this capability: "associated with the class, but not requiring an instance."

## 6. const Member Functions

### 6.1 Semantics of const Member Functions

A `const` member function is a strong semantic promise provided by C++: **this function will not modify the state of the object**. We declare it by adding the `const` keyword after the function parameter list:

```cpp
class LED {
private:
    int pin;
    bool state;

public:
    bool is_on() const {  // const 成员函数
        return state;      // 可以读取成员变量
        // state = true;   // 编译错误！不能修改成员变量
    }
};
```

This is not just for human readers; the compiler sees it too. The compiler checks at compile time whether any operations within a `const` member function modify member variables, and will issue an error if it finds any. More importantly, `const` member functions are the **only member functions that can be called on `const` objects**:

```cpp
void print_status(const LED& led) {
    led.is_on();   // OK，is_on() 是 const 的
    // led.on();   // 编译错误！on() 不是 const 的，不能通过 const 引用调用
}
```

### 6.2 The Cascading Effect of `const` Correctness

`const` correctness has a very important characteristic—it is "contagious." If your function declares a `const` reference parameter, we can only call `const` member functions through that reference. Furthermore, if those `const` member functions return references to other objects, those references should also be `const`. While this cascading effect might seem annoying, it actually helps us build a very strong "read-only safety net."

Let's look at a practical example in an embedded context—a sensor reading class with a cache:

```cpp
class TemperatureSensor {
private:
    int pin;
    mutable float cached_value;    // mutable 允许在 const 函数中修改
    mutable bool cache_valid;

public:
    TemperatureSensor(int p) : pin(p), cached_value(0), cache_valid(false) {}

    // 非 const：强制重新从硬件读取
    float read() {
        cached_value = read_from_hardware();
        cache_valid = true;
        return cached_value;
    }

    // const：优先返回缓存值
    float read_cached() const {
        if (!cache_valid) {
            // cache_valid = true;  // 如果没有 mutable，这里会编译错误
            cached_value = read_from_hardware();
            cache_valid = true;
        }
        return cached_value;
    }

    float get_cached() const {
        return cached_value;
    }

private:
    float read_from_hardware() const {
        // 实际读取 ADC
        return 25.0f;
    }
};

// 使用
void report_temperature(const TemperatureSensor& sensor) {
    // sensor.read();          // 编译错误！read() 不是 const 的
    float temp = sensor.read_cached();  // OK
    printf("Temperature: %.1f C\n", temp);
}
```

This example demonstrates a very practical design pattern: providing a non-`const` "force refresh" interface and a `const` "return cached value if available" interface. Callers automatically obtain different behavioral guarantees depending on whether they hold a `const` reference or a non-`const` reference.

### 7.3 A Practical Rule of Thumb

There is a widely recognized programming guideline in C++: **all member functions that do not modify the object's state should be declared as `const`**. This is not mandatory, but if you fail to do so, your class will encounter various frustrations for users—such as "why does the compiler prevent me from reading this?"—because others may hold your object via a `const` reference (for example, when passing it as a function argument), at which point they can only call `const` member functions.

If you are designing a class and a member function "seems like it should just read data," but you forget to add `const`, your users will find that they cannot call this "read-only" function when passing the object to a function accepting a `const` reference. This error is particularly insidious because the cause lies not at the call site, but in the class definition—and the error message is often just "discards qualifiers," which leaves beginners completely bewildered.

My advice is: **make it a habit—after writing each member function, ask yourself, "Does this function need to modify the object?" If the answer is no, add `const` immediately.**

## 8. Friends (friend)

### 8.1 What is a Friend?

A `friend` is a mechanism provided by C++ that allows you to actively **break encapsulation boundaries**—granting an external function or external class access to the current class's `private` and `protected` members.

```cpp
class SensorData {
private:
    float raw_values[100];
    int count;

public:
    SensorData() : count(0) {}

    // 声明 serialize 为友元函数
    friend void serialize(const SensorData& data, uint8_t* buffer);
};

// 友元函数可以直接访问 private 成员
void serialize(const SensorData& data, uint8_t* buffer) {
    memcpy(buffer, data.raw_values, data.count * sizeof(float));
    // 这里直接访问了 raw_values 和 count，它们是 private 的
    // 但因为 serialize 被声明为友元，所以编译器允许
}
```

### 7.2 The Dangers of Friends

The existence of `friend` is not inherently evil, but it is almost always a **red flag**. Friendship implies that you are actively exposing a class's internal implementation details to external code. From a design perspective, this breaks encapsulation—which is one of the core values of a class.

In most scenarios where `friend` seems necessary, we can actually avoid it through better design. For instance, in the serialization example above, we could simply provide a `const` public access interface instead of exposing the entire internal array:

```cpp
class SensorData {
private:
    float raw_values[100];
    int count;

public:
    // 提供只读访问接口，不需要友元
    const float* data() const { return raw_values; }
    int size() const { return count; }
};

void serialize(const SensorData& data, uint8_t* buffer) {
    memcpy(buffer, data.data(), data.size() * sizeof(float));
}
```

This design is clearly safer—`SensorData` only exposes a read-only pointer and its size, so external code cannot modify the internal data. The friend version, however, exposes the entire `raw_values` array to the `serialize` function. If the implementation of `serialize` contains a bug, it could perform an out-of-bounds write.

Therefore, my suggestion is this: **if a class requires a large number of friends to function, it probably shouldn't have been designed as a class in the first place**. Friendship should be a last resort, not a routine practice. When your first instinct is to "just make it a friend," pause and consider: is there an alternative that doesn't break encapsulation?

## 8. The `explicit` keyword

### 8.1 The problem with implicit conversion

C++ allows constructors to perform implicit type conversion. This means that if you have a constructor accepting a single argument, the compiler will automatically invoke this constructor when necessary, silently converting the argument type to the class type.

```cpp
class PWMChannel {
private:
    int channel;

public:
    // 没有 explicit：允许隐式转换
    PWMChannel(int ch) : channel(ch) {}
};

void set_active(PWMChannel ch) {
    // 设置某个通道为活跃
}

set_active(3);  // OK：3 被隐式转换为 PWMChannel(3)
```

This code compiles, but the `set_active(3)` call is semantically ambiguous—you pass an `int`, but the function expects a `PWMChannel` object. The compiler "helpfully" performs the conversion for you, but this "kindness" is often the source of disasters in large projects: you might mistype a parameter type somewhere, and instead of reporting an error, the compiler silently performs a conversion you never anticipated, causing the program to behave in inexplicable ways.

### 8.2 The Role of `explicit`

The `explicit` keyword is used to prohibit such implicit conversions. Once added, the constructor can only be used when explicitly invoked:

```cpp
class SafePWMChannel {
private:
    int channel;

public:
    explicit SafePWMChannel(int ch) : channel(ch) {}
};

void set_active(SafePWMChannel ch);

// set_active(3);                      // 编译错误！不能隐式转换
set_active(SafePWMChannel(3));         // OK：显式构造
set_active((SafePWMChannel)3);         // OK：显式转换（C 风格，不推荐）
```

My recommendation is: **all single-argument constructors should be marked `explicit`, unless you explicitly need implicit conversion**. This is a near-zero-cost defensive measure that prevents numerous bugs caused by implicit conversions. Furthermore, `explicit` only affects implicit constructor calls—explicit calls are completely unaffected, so it does not restrict any functionality you actually need.

## 9. The `mutable` Keyword

### 9.1 The Role of `mutable`

The `mutable` keyword allows us to modify member variables marked as `mutable` inside `const` member functions. While this might sound like a violation of the `const` contract, there are actually perfectly valid use cases for it.

We previously looked at a caching example when discussing `const` member functions. Let's look at a more complete version here:

```cpp
class Sensor {
private:
    int pin;
    mutable float cached_value;   // mutable：允许 const 函数修改
    mutable bool cache_valid;
    mutable int read_count;       // 统计读取次数

public:
    explicit Sensor(int p)
        : pin(p), cached_value(0), cache_valid(false), read_count(0) {}

    float read() const {
        read_count++;              // OK：read_count 是 mutable 的
        if (!cache_valid) {
            cached_value = read_from_hardware();
            cache_valid = true;
        }
        return cached_value;
    }

    int get_read_count() const {
        return read_count;
    }

private:
    float read_from_hardware() const {
        // 实际读取硬件
        return 25.0f;
    }
};
```

In this example, the `read()` function is declared `const` because it makes a promise to the outside world: "it will not change the logical state of the sensor"—from the user's perspective, the sensor remains unchanged before and after calling `read()`. Internally, however, `read()` does modify the cache and the counter—these are **implementation details**, not part of the logical state.

### 9.2 When to Use `mutable`

The scenarios where `mutable` applies are very clear: **member variables that belong to implementation details and do not affect the logical state of the object**. Typical scenarios include caching, lazy calculation, debug counters, and mutexes.

However, `mutable` can also be easily abused. If you find yourself frequently modifying `mutable` members in `const` functions, and these modifications affect the "observable behavior" of the object, there is likely a flaw in your `const` design—either the function should not be `const`, or those members should not be `mutable`.

A simple criterion is: **If you remove the `mutable` qualifier and the related modification code, does the function behave exactly the same externally?** If the answer is "yes," then `mutable` is appropriate; if "no," you need to re-examine the design.

## Run Online

Run the comprehensive class basics example online to observe construction, destruction, the `this` pointer, and static members:

<OnlineCompilerDemo
  title="C++98 Classes and Objects: Construction, Destruction, this, static, mutable"
  source-path="code/examples/vol1/16_cpp98_classes_objects.cpp"
  description="Run online to observe StringBuilder chaining, Sensor lifecycle, and static member counting."
  allow-run
/>

## Summary

In this chapter, we analyzed the core mechanisms of C++ classes and objects in depth. Starting from C structs, we saw how `class` binds data and operations together through access control; constructors and destructors guarantee "acquisition is initialization" and "cleanup on exit"; member initializer lists provide a dual guarantee of performance and semantic correctness; the `this` pointer explains how member functions "know" which object they are operating on; static members provide class-level shared state; `const` member functions establish a strong "read-only" contract; and `friend`, `explicit`, and `mutable` serve as three tools for "precise control," each with its own use cases and boundaries.

In the next article, we will extend the concept of individual classes to type hierarchies—seeing how C++ uses inheritance and polymorphism to organize relationships between multiple classes.
