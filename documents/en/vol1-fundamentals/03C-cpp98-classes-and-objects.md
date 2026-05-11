# C++98 Object-Oriented Programming: A Deep Dive into Classes and Objects

> The complete repository is available at [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP). Feel free to check it out, and if you like it, give it a Star to encourage the author.

Classes and objects are core concepts in C++ object-oriented programming, but in embedded contexts, they are often misunderstood as "heavy," "slow," or "overly fancy." In reality, classes do not equal complexity, and OOP does not mean you must use inheritance and polymorphism. **In resource-constrained embedded systems with clear business logic, the core value of a class comes down to one thing: binding "state" with "the code that operates on that state."**

In other words, the primary value of a class is not abstraction, but **constraint**.

In this chapter, we will start from C structs and gradually transition to C++ classes, breaking down every key concept clearly—including constructors and destructors, member initializer lists, `this` pointers, static members, `const` member functions, friends, and the `explicit` and `mutable` keywords, which are often overlooked but highly useful.

## 1. From struct to class

### 1.1 Limitations of C Structs

In C, we use structs to organize data, and then use standalone functions to operate on that data. For example, LED control code in C style might look like this:

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

This code works, but it has a structural problem: the association between the `led_init`, `led_on`, and `led_off` functions and the `struct LED` struct is **maintained entirely by naming conventions**. There is no syntactic mechanism to prevent you from writing an absurd call like `led_on(&uart_config)`—the compiler will not raise an error, because `led_on` accepts a `struct LED*`, and you might accidentally pass in a pointer to the wrong struct.

### 1.2 The C++ class: Binding Data and Operations Together

The C++ class solves this problem—it bundles data (member variables) and operations (member functions) into a single syntactic unit:

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

Now, when using it, you can only operate on it through the public interface of the `LED` class:

```cpp
LED led(5);    // 构造时指定引脚号
led.on();      // 点亮
led.toggle();  // 切换状态
bool on = led.is_on();  // 查询状态
```

Compared to the C version, the most obvious improvement is that you no longer need to manually pass a struct pointer. The `led.on()` call inherently knows which LED it is operating on—because `on()` is a member function of the `led` object, and the compiler automatically passes the address of `led` as a hidden parameter. Behind the scenes, this is exactly the `this` pointer we will discuss next.

### 1.3 Access Control: public, private, protected

C++ provides three access control keywords to manage the visibility of class members.

`private` members can only be accessed by the class's own member functions. In the `LED` class above, `pin` and `state` are `private`, meaning you cannot directly read or write them from outside the class:

```cpp
LED led(5);
// led.pin = 10;   // 编译错误！pin 是 private 的
// led.state = true; // 编译错误！state 是 private 的
led.on();          // OK，on() 是 public 的
```

`private` is not meant to "prevent hackers," but rather to **syntactically tell users what they shouldn't touch**. You can of course bypass it through various means (such as pointer casting, macros, etc.), but that falls into the realm of undefined behavior (UB). For most engineering code, `private` itself serves as a strong form of self-documentation—it lets anyone reading the code distinguish at a glance between "interface" and "implementation details."

`public` members are visible to all code, forming the class's external interface. `protected` members are visible to the class itself and its derived classes—we will discuss this in detail when we cover inheritance.

Regarding the difference between `class` and `struct`, there is actually only one: the default access level for `class` is `private`, while the default access level for `struct` is `public`. Semantically, `struct` is typically used to express "a collection of data" (C style), while `class` is used to express "an object with behavior." However, the compiler does not force you to follow this convention—you could perfectly well write a `class` with all `public` members, or a `struct` with member functions. Which one you choose is more about conveying your design intent to the reader.

## 2. Constructors and Destructors

### 2.1 Constructors: Bringing Objects into a Valid State

A constructor is a special member function that is automatically called when an object is created, responsible for bringing the object into a **valid, usable state**. The name of a constructor is the same as the class name, it has no return type (not even `void`), it can take parameters, and it supports overloading.

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

When using it, the object is in a usable state as soon as it is created:

```cpp
UARTPort uart(1, 115200);  // 构造时完成全部硬件初始化
uart.send(data, sizeof(data));
// 离开作用域时...
```

The core value of constructors lies in the fact that **they eliminate the possibility of "forgetting to initialize."** In C, you might forget to call `uart_init()`, and then use an uninitialized struct to send data—with disastrous consequences. In C++, object creation and initialization are bound together; it is impossible to have an object that "has been created but not initialized."

### 2.2 Destructors: Cleaning Up at the End of an Object's Lifetime

A destructor is the "partner" of a constructor, and it is automatically called when an object is destroyed. The name of a destructor is `~` followed by the class name, it takes no parameters, and has no return type:

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

In embedded systems, destructors are particularly well-suited for releasing hardware resources: disabling peripherals, releasing DMA channels, restoring pins to their default states, and so on. This pattern of "acquire on construction, release on destruction" has a famous name—**RAII (Resource Acquisition Is Initialization)**. RAII is the core idea of C++ resource management, and we will dive deep into it in a later chapter. For now, just remember one thing: **if you acquire a resource in a constructor, you must release it in the destructor**.

The timing of an object's destruction depends on its storage duration. Local objects are destroyed when they go out of scope, global/static objects are destroyed when the program ends, and objects dynamically allocated via `new` are only destroyed when `delete` is called.

### 2.3 Default Constructors

If you do not define any constructors for a class, the compiler will automatically generate a **default constructor**—a parameterless constructor that does nothing. However, as soon as you define any constructor (even one with parameters), the compiler no longer automatically generates a default constructor.

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

If you need both a parameterized constructor and a parameterless default constructor, you can explicitly define one:

```cpp
class Sensor {
private:
    int pin;

public:
    Sensor() : pin(0) {}       // 默认构造函数
    Sensor(int p) : pin(p) {}  // 带参数的构造函数
};
```

## 3. Member Initializer Lists

### 3.1 Why Use Initializer Lists

In constructors, the member initializer list is the **preferred way to initialize** class members. Many people are accustomed to using assignment statements inside the constructor body to "initialize" member variables, but in C++ semantics, this is not true initialization—it is "default construct first, then assign." For certain types of members, this "construct then assign" approach is not even valid.

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

The core advantage of initializer lists lies in **performance and semantic correctness**. For basic types like `int`, the performance difference between the two approaches is negligible. But for complex class-type members, using an initializer list avoids a default construction followed by an assignment—constructing directly with the target value eliminates the intermediate step.

More importantly, **`const` members and reference members can only be initialized through an initializer list**, because by the time the constructor body executes, they have already been default constructed—and `const` objects cannot be reassigned, nor can references be rebound. So if you have members of these two types, the initializer list is not a "recommendation," but the **only valid choice**.

### 3.2 Embedded Applications of Initializer Lists

In embedded development, initializer lists have another very practical application: configuring hardware parameters directly when an object is constructed.

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

There is one detail to note about initialization order: **the initialization order of member variables depends on their declaration order in the class definition, not the order in which they appear in the initializer list**. If you write `: b(a), a(10)` in the initializer list, the compiler will initialize `a` first (because it is declared first), and then initialize `b`—so `b(a)` will indeed get the correct value of `a`. But if your declaration order has `b` before `a`, then when `b(a)` is initialized, `a` has not been initialized yet, and the value read will be undefined. Most compilers will issue a warning when the initializer list order does not match the declaration order, but it is best to cultivate the habit of keeping them consistent.

## 4. The this Pointer

### 4.1 What is this

Every non-static member function has a hidden parameter at the底层 level—a pointer to the object on which the function is called. This pointer is `this`. In other words, when you write:

```cpp
led.on();
```

The compiler actually translates it into a call similar to this (pseudocode):

```cpp
LED::on(&led);  // 把 led 的地址作为 this 指针传入
```

Inside a member function, `this` points to the current object. You can access member variables and member functions through `this`. In most cases, you do not need to explicitly write out `this`—the compiler will automatically resolve "bare" member names as `this->成员名`. But in certain scenarios, explicitly using `this` is necessary or helpful.

The most common case is when **parameter names clash with member variable names**:

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

Another common application of the `this` pointer is implementing chained calls. The approach is simple: a member function returns a reference to `*this`, allowing the caller to consecutively call multiple methods in a single line of code.

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

This pattern is particularly well-suited for building configuration interfaces or log output in embedded development—each call returns itself, making the code compact to write and fluent to read.

Compared to the C approach, the underlying principle of chained calls is actually the same as "a function returning a struct pointer" in C. The difference is that C++ makes the syntax more natural through `this` and references, eliminating the need to write `->` and the address-of operator everywhere.

## 5. Static Members

### 5.1 Static Member Variables

Static member variables belong to the **class itself**, rather than to any specific object. This means that no matter how many instances of the class you create, there is only one copy of a static member variable in memory.

This is very practical in embedded development. For example, if you want to track how many instances of a peripheral driver are currently in use:

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

Note a detail that is easy to trip over: **static member variables must be defined and initialized outside the class** (C++17 introduced the ability to initialize `inline static` members directly inside the class, but C++98 does not support this). If you only declare `static int active_count;` inside the class but forget to write `int UARTPort::active_count = 0;` in the `.cpp` file, the linker will report an "undefined reference" error, and this error is often hard to track down—because compilation succeeds, and only linking fails.

### 5.2 Static Member Functions

Static member functions also belong to the class itself, rather than to any specific object. Therefore, static member functions **have no `this` pointer**, which also means they cannot access non-static member variables or non-static member functions—because these require `this` to locate a specific object instance.

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

When calling a static member function, we use the `类名::函数名()` approach, without needing to create an object first:

```cpp
UARTPort::init_hal();
if (UARTPort::is_hal_ready()) {
    UARTPort uart(1, 115200);
}
```

This pattern of "check if hardware is ready first, then create an instance" is very common in embedded development, and static member functions happen to provide exactly this calling capability—"related to the class, but not requiring an instance."

## 6. const Member Functions

### 6.1 Semantics of const Member Functions

A `const` member function is a very strong semantic commitment provided by C++: **this function will not modify the state of the object**. It is declared by adding the `const` keyword after the function's parameter list:

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

This is not just for people reading the code; it is even more so for the compiler. The compiler will check at compile time whether a `const` member function contains any operations that modify member variables, and will report an error immediately if it finds any. More importantly, a `const` member function is **the only member function that can be called on a `const` object**:

```cpp
void print_status(const LED& led) {
    led.is_on();   // OK，is_on() 是 const 的
    // led.on();   // 编译错误！on() 不是 const 的，不能通过 const 引用调用
}
```

### 6.2 The Cascading Effect of const Correctness

`const` correctness has a very important characteristic—it is "contagious." If your function declares a `const` reference parameter, then through that reference you can only call `const` member functions. And if those `const` member functions return references to other objects, those references should also be `const`. This cascading effect might seem a bit annoying, but it actually helps you build a very strong "read-only safety net."

Let's look at a practical example in an embedded scenario—a sensor reading class with caching:

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

This example demonstrates a very practical design pattern: providing a non-`const` "force refresh" interface and a `const` "return cached value if available" interface. Callers automatically get different behavioral guarantees depending on whether they hold a `const` reference or a non-`const` reference.

### 6.3 A Practical Rule of Thumb

There is a widely recognized programming guideline in C++: **all member functions that do not modify the object's state should be declared as `const`**. This is not mandatory, but if you don't do it, others using your class will encounter various frustrations like "this is clearly a read operation, so why won't the compiler allow it?"—because someone might hold your object through a `const` reference (such as when passing it as a function parameter), at which point only `const` member functions can be called.

If, when designing a class, a member function "looks like it should just be reading data," but you forget to add `const`, your users will find that they cannot call this "obviously read-only" function when they pass the object to a function accepting a `const` reference. This kind of error is particularly insidious, because the cause is not at the call site, but at the class definition—and the error message is often just "discards qualifiers," which a beginner would have no idea how to interpret.

My recommendation is: **make it a habit—after writing each member function, ask yourself "does this function need to modify the object?" If the answer is no, add `const` immediately.**

## 7. Friends (friend)

### 7.1 What Are Friends

A friend (friend) is a mechanism provided by C++ that allows you to actively **break encapsulation boundaries**—letting an external function or external class access the `private` and `protected` members of the current class.

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

### 7.2 The Danger of Friends

The existence of friends is not inherently evil, but it is almost always a **red flag**. A friend means you are proactively exposing the internal implementation details of your class to external code. From a design perspective, this breaks encapsulation—and encapsulation is one of the core values of a class.

Most scenarios that seem to require friends can actually be avoided through better design. For example, the serialization example above could entirely be implemented by providing a `const` public access interface, without needing to expose the entire internal array:

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

This design is clearly safer—`SensorData` only exposes a read-only pointer and a size, and external code cannot modify the internal data. The friend version, on the other hand, exposes the entire `raw_values` array to the `serialize` function; if `serialize`'s implementation has a bug, it could write out of bounds.

So my recommendation is: **if a class needs a lot of friends to work, it probably shouldn't have been designed as a class in the first place**. Friends should be a last resort, not a常规手段. When your first instinct is "just add a friend," stop and think: is there an alternative that doesn't break encapsulation?

## 8. The explicit Keyword

### 8.1 The Problem with Implicit Conversions

C++ allows constructors to perform implicit type conversions. That is, if you have a constructor that accepts a single parameter, the compiler will automatically call this constructor when needed, quietly converting the parameter type into the class type.

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

This code compiles, but the `set_active(3)` call is semantically ambiguous—you passed in a `int`, but the function expects a `PWMChannel` object. The compiler "helpfully" did the conversion for you, but this kind of "helpfulness" in large projects is often a source of disaster: you might write the wrong parameter type somewhere, and instead of reporting an error, the compiler silently performs a conversion you never expected, and then the program runs in some inexplicable way.

### 8.2 The Role of explicit

The `explicit` keyword is used to prohibit such implicit conversions. Once added, the constructor can only be used in explicit calls:

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

My recommendation is: **all single-parameter constructors should have `explicit`, unless you very explicitly need implicit conversion**. This is a nearly zero-cost defensive measure that can avoid a large number of bugs caused by implicit conversions. Moreover, `explicit` only affects implicit calls to the constructor—explicit calls are completely unaffected, so it does not restrict any functionality you genuinely need.

## 9. The mutable Keyword

### 9.1 The Role of mutable

The `mutable` keyword allows you to modify member variables marked as `mutable` inside a `const` member function. This might sound like it violates the `const` promise, but in reality there are perfectly reasonable use cases.

We already saw a caching example earlier when discussing `const` member functions. Here is a more complete version:

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

In this example, the `read()` function is declared as `const`, because its external promise is "it will not change the sensor's logical state"—from the user's perspective, the sensor has not undergone any change before and after calling `read()`. Internally, however, `read()` does indeed modify the cache and counter—these belong to **implementation details**, not part of the logical state.

### 9.2 When to Use mutable

The scenarios where `mutable` is applicable are very clear: **member variables that belong to implementation details and do not affect the object's logical state**. Typical scenarios include caches, lazy evaluation, debug counters, mutexes, and so on.

But `mutable` can also be easily abused. If you find yourself frequently modifying `mutable` members inside `const` functions, and these modifications affect the object's "observable behavior," there is a high probability that your `const` design is problematic—either the function shouldn't be `const`, or those members shouldn't be `mutable`.

A simple criterion for judgment is: **if you remove the `mutable` marker and the related modification code, is the function's external behavior exactly the same?** If the answer is "yes," then `mutable` is reasonable; if "no," then the design needs to be re-examined.

## Summary

In this chapter, we took a deep dive into the core mechanisms of C++ classes and objects. Starting from C structs, we saw how `class` uses access control to bind data and operations together; constructors and destructors guarantee that objects are "initialized on acquisition" and "cleaned up on departure"; member initializer lists provide a dual guarantee of performance and semantic correctness; the `this` pointer explains why member functions can "know" which object they are operating on; static members provide class-level shared state; `const` member functions establish a strong "read-only" contract; and friends, `explicit`, and `mutable` are three "precision control" tools, each with its own applicable scenarios and boundaries of use.

In the next chapter, we will extend the concept of a single class to type hierarchies—exploring how C++ uses inheritance and polymorphism to organize relationships between multiple classes.
