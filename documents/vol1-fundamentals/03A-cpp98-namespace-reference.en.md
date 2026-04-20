---
title: 'C++98 Basics: Namespaces, References, and Scope Resolution'
description: 'The first step from C to C++ — a thorough explanation of three fundamental
  features: namespaces for resolving name conflicts, references replacing pointers
  for passing arguments, and scope resolution for accessing global and namespace members.'
chapter: 0
order: 3
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
difficulty: beginner
reading_time_minutes: 20
prerequisites:
- C语言速通复习
related:
- C++98函数接口：重载与默认参数
cpp_standard:
- 11
- 14
- 17
- 20
platform: host
translation:
  source: documents/vol1-fundamentals/03A-cpp98-namespace-reference.md
  source_hash: 4f81ce08cfa313804f0c53b34fe3077e5cebd280457fa5a7ee351963b957c1e0
  translated_at: '2026-04-20T03:02:22.938128+00:00'
  engine: anthropic
  token_count: 2504
---
# Getting Started with C++98: Namespaces, References, and Scope Resolution

> The full repository is available at [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP). Feel free to check it out, and if you like it, give it a Star to encourage the author.

In the previous chapter, we systematically reviewed the core syntax of C. Starting from this chapter, we officially step into the world of C++. But before diving into object-oriented programming, let's look at the immediate, non-object-oriented improvements C++ offers—namespaces solve naming conflicts in large projects, references free function parameters from clunky pointer syntax, and the scope resolution operator lets us precisely tell the compiler "this is the name I mean."

None of these three features involve classes, nor do they require any object-oriented background. They belong to the set of improvements you can leverage the moment you migrate from C to C++. We place them first because they are simple enough, practical enough, and—most importantly—they do not interfere with performance at any level.

## 1. Namespaces

### 1.1 Why We Need Namespaces

In C projects, naming conflicts are a chronic headache. If your project uses three third-party libraries, and each has a function called `init()`, congratulations—you'll get a bunch of "multiple definition" errors at link time. The C convention is to prefix every name: `sensor_init()`, `uart_init()`, `display_init()`... It sounds workable, but it's verbose to write and doesn't completely prevent conflicts (what if two libraries both use `network_buffer_create()`?).

C++ namespaces solve this problem at the language level. Essentially, they automatically append a "last name" to every name during compilation, but this "last name" is a structured, nestable prefix system provided by the namespace. Because this substitution happens at compile time, namespaces incur zero runtime overhead—the final compiled symbol names are exactly the same as if you had handwritten the prefixes, but you don't have to type out those long, ugly fully qualified names yourself.

### 1.2 Defining and Using Namespaces

Let's look at some embedded-style code directly. Suppose we are developing a sensor module:

```cpp
namespace sensor {
    const int MAX_READINGS = 100;

    struct Reading {
        float temperature;
        float humidity;
    };

    void init();
    Reading get_reading();
}
```

Definitions can be spread across multiple files—that is, you can declare `sensor::init()` in a header file first, and then wrap the implementation in the same `namespace sensor { ... }` in the corresponding `.cpp` file. The compiler automatically "merges" all declarations within the same namespace.

When implementing, we write it like this:

```cpp
// sensor.cpp
namespace sensor {
    void init() {
        // 初始化传感器硬件
    }

    Reading get_reading() {
        Reading r;
        // 读取传感器数据
        return r;
    }
}
```

There are three ways to use them, from most explicit to most permissive:

```cpp
int main() {
    // 方式一：完全限定名——最明确，永远不会产生歧义
    sensor::init();
    sensor::Reading data = sensor::get_reading();

    // 方式二：using 声明——引入特定名称
    using sensor::Reading;
    Reading data2 = sensor::get_reading();

    // 方式三：using 指令——引入整个命名空间
    using namespace sensor;
    init();
    Reading data3 = get_reading();

    return 0;
}
```

Each approach has its use cases. Approach one is best suited for function bodies in `.cpp` files; while it requires more typing, it is completely bulletproof. Approach two is ideal when you frequently use only a few names from a particular namespace. As for approach three... honestly, if you use `using namespace std` inside a function body in a `.cpp` file, most people won't say anything. But if you put it in a header file at global scope—that's basically planting a landmine in your codebase that will inevitably go off.

Regarding the dangers of `using namespace` in header files, we won't go into a lengthy rant here. Just remember one ironclad rule: **never write `using namespace` in a header file**. The reason is simple—`using namespace` is irreversible. Once a header file globally introduces a namespace, all code that `#include` that header is forced to accept all symbols from that namespace, possibly without even knowing it. When two different libraries define symbols with the same name in their respective namespaces, and your header `using` both—congratulations, ambiguity errors will pop up in the most unexpected places.

### 1.3 Nested Namespaces

Namespaces can be nested. This feature is extremely practical when organizing complex codebases because we can use namespace hierarchy to reflect module hierarchy. For example, a hardware abstraction layer:

```cpp
namespace hardware {
    namespace gpio {
        enum PinMode {
            INPUT,
            OUTPUT,
            ALTERNATE
        };

        void set_mode(int pin, PinMode mode);
    }

    namespace uart {
        void init(int baudrate);
        void send(const char* data);
    }
}
```

When using it:

```cpp
hardware::gpio::set_mode(5, hardware::gpio::OUTPUT);
hardware::uart::init(115200);
```

If `hardware::gpio::` feels too long, we can use a namespace alias to simplify it:

```cpp
namespace hw = hardware;
hw::gpio::set_mode(5, hw::gpio::OUTPUT);
```

The alias is only valid in the current scope, so you can safely give the same namespace different short names in different functions without polluting the global scope.

It's worth mentioning that C++17 introduced a more concise nested syntax:

```cpp
// C++17 起，等价于上面的嵌套定义
namespace hardware::gpio {
    void set_mode(int pin, PinMode mode);
}
```

This syntax is just syntactic sugar, functionally equivalent to manual nesting, but it does make the code much cleaner. If your project is still on C++11/14, just write them out layer by layer the old-fashioned way.

### 1.4 Anonymous Namespaces

Anonymous namespaces are an easily overlooked but highly practical feature in C++. Their purpose is to provide **file-level scope**—anything defined inside an anonymous namespace is visible only to the current translation unit (i.e., the current `.cpp` file) and completely invisible to the outside world.

In C, we use the `static` keyword to achieve a similar effect:

```c
// C 风格：限制在当前文件可见
static int buffer_size = 256;
static void internal_helper() { /* ... */ }
```

In C++, we recommend using anonymous namespaces to replace `static`:

```cpp
// C++ 风格：推荐
namespace {
    const int BUFFER_SIZE = 256;

    void internal_helper() {
        // 内部辅助函数
    }
}

void public_function() {
    internal_helper();  // 可以直接调用
}
```

Why does C++ recommend anonymous namespaces over `static`? There are two key reasons. First, `static` only applies to functions, variables, and anonymous unions, but **not** to type definitions—you can't write `static class Foo { ... };`. Anonymous namespaces, on the other hand, can wrap anything: classes, structs, enums, templates—nothing is off-limits. Second, starting from C++11, entities in anonymous namespaces are explicitly given internal linkage, making them semantically equivalent to `static` but with a broader scope of application. Both the C++ Core Guidelines and clang-tidy recommend favoring anonymous namespaces.

Of course, `static` hasn't been deprecated—it's retained for C compatibility. In practice, mixing the two won't cause issues, but consistency is a good habit. Our advice: **use anonymous namespaces for all new code, and don't rush to change old code when you see it**, unless you're actively refactoring that section.

## 2. References

### 2.1 What Is a Reference

A reference is a core concept introduced in C++—it provides an **alias** for a variable. Calling it an "alias" might sound a bit abstract, but we can think of it this way: a reference is like a nickname for a person; whether you call them by their real name or their nickname, you're referring to the same person. Under the hood, references are typically implemented via pointers, but syntactically, references are much safer and cleaner than pointers.

The most basic usage:

```cpp
int value = 42;
int& ref = value;  // ref 是 value 的引用（别名）

ref = 100;         // 修改 ref 就是修改 value
// 此时 value 也变成了 100
```

References have two very important constraints, and understanding them is essential to avoiding pitfalls. First, **a reference must be initialized at declaration**—you cannot declare a reference first and bind it to a variable later. This differs from pointers: a pointer can be declared as `nullptr` first and assigned later, but a reference cannot. Second, **once a reference is bound, it cannot be rebound to another variable**. Look at this easily confusing example:

```cpp
int other = 200;
ref = other;  // 这不是重新绑定！
```

This line does not make `ref` point to `other`; rather, it assigns the value of `other` (200) to the object referenced by `ref` (which is `value`). After execution, `value` becomes 200, and `ref` is still a reference to `value`. This distinction is crucial—the binding of a reference is **one-time**, and subsequent assignment operations merely modify the value of the referenced object.

### 2.2 References as Function Parameters

The most common use of references is as function parameters. In C, if a function needs to modify the caller's variable or avoid the copy overhead of a large object, we pass a pointer. But pointer syntax is clunky—`*` and `->` are everywhere, and you have to check for null pointers every time before using them. References solve both problems perfectly.

Let's use an embedded scenario to compare three parameter-passing approaches:

```cpp
struct SensorData {
    float temperature;
    float humidity;
    float pressure;
    char sensor_id[32];
};

// 方式一：传值——拷贝整个结构体（低效）
void process_by_value(SensorData data) {
    // data 是副本，修改它不会影响原始数据
    data.temperature += 10;  // 只修改了副本
}

// 方式二：传指针——需要检查空指针，语法稍显笨拙
void process_by_pointer(SensorData* data) {
    if (data != nullptr) {
        data->temperature += 10;  // 需要使用 -> 而不是 .
    }
}

// 方式三：传引用——高效且语法简洁
void process_by_reference(SensorData& data) {
    data.temperature += 10;  // 直接使用 . 操作符
    // 不需要空指针检查，引用总是有效的
}
```

Passing by reference is the cleanest approach—no `*`, no `->`, no null pointer checks. In most cases, if you want a function to "modify the caller's variable" in C++, references should be your first choice.

But the story doesn't end there. Often, we pass parameters not to modify them, but to avoid copy overhead—such as with a struct containing lots of data, or a string. In these cases, a `const` reference is the best choice:

```cpp
// const 引用：既高效又防止修改
void read_only_access(const SensorData& data) {
    float temp = data.temperature;  // 可以读取
    // data.temperature = 0;  // 错误！编译器会阻止你修改 const 引用
}
```

The elegance of a `const` reference lies in how it achieves both "no copy" and "no modification" simultaneously. The caller sees `const SensorData&` and knows the function won't touch their data; the compiler sees `const` and intercepts any modification attempts at compile time. This pattern was already very common in C++98 and is essentially the standard paradigm for "passing read-only large objects."

### 2.3 const References and Temporary Object Lifetimes

Here is a very important detail, and a common pitfall for C++ learners. When we bind a `const` reference to a temporary object (an rvalue), C++ **extends the lifetime of that temporary object**, making it live as long as the reference:

```cpp
const int& ref = 42;  // OK！42 本来是个临时值，但 const 引用延长了它的寿命
// ref 在整个作用域内都有效
```

This might not seem like a big deal—after all, how large is an `int`? But when the temporary object is a complex type, this rule becomes crucial:

```cpp
std::string get_name();

const std::string& name = get_name();
// get_name() 返回的临时 string 本来在完整表达式结束后就该销毁
// 但因为绑定了 const 引用，它的生命被延长到了 name 的整个生命周期
// 所以 name 在整个作用域内都是安全的
```

However, this lifetime extension has a **key prerequisite**: the reference must **directly bind** to the temporary object. If the reference is indirectly bound through an intermediate value returned by a function, the lifetime extension does not take effect. This is a somewhat advanced topic; for now, just remember the rule that "direct binding is required for it to work." We'll revisit this in detail later when we discuss return value optimization and move semantics.

### 2.4 References as Return Values

Functions can return references, which gives us two very practical programming patterns: chained calls and subscript access.

The core idea of chained calls is to have a function return a reference to `*this`, allowing the caller to chain multiple operations in a single line of code:

```cpp
class Buffer {
private:
    uint8_t data[256];
    size_t size;

public:
    Buffer() : size(0) {}

    Buffer& append(uint8_t byte) {
        if (size < 256) {
            data[size++] = byte;
        }
        return *this;  // 返回当前对象的引用
    }
};

// 链式调用
Buffer buf;
buf.append(0x01).append(0x02).append(0x03);
```

Subscript access, by returning a reference to an internal element, allows the caller to directly read from or write to data inside a container via `[]`:

```cpp
class ByteBuffer {
private:
    uint8_t data[256];
    size_t size;

public:
    ByteBuffer() : size(0) {}

    uint8_t& operator[](size_t index) {
        return data[index];
    }

    const uint8_t& operator[](size_t index) const {
        return data[index];
    }
};

ByteBuffer buf;
buf[0] = 0xFF;  // 通过引用直接修改内部数据
```

But returning a reference has a **fatal trap**: never return a reference to a local variable. Local variables are stored on the stack, and once the function returns, the stack frame is reclaimed. At that point, the reference points to freed memory—this is typical undefined behavior (UB). The program might seem to run fine sometimes and crash other times, with the crash location and cause following no discernible pattern.

```cpp
// 危险！绝对不要这样做！
int& dangerous_function() {
    int local = 42;
    return local;  // 返回局部变量的引用
    // 函数返回后 local 已经销毁，引用变成了悬空引用
}

// 安全的做法
int& safe_function(int& input) {
    return input;  // 返回参数的引用是安全的
}
```

The principle for determining whether returning a reference is safe is simple: **the lifetime of the referenced object must outlive the function call itself**. Member variables, global variables, static variables, and objects passed in via parameters—all safe. Local variables inside a function body—not safe.

### 2.5 References vs. Pointers: When to Use Which

Since references are so great, are pointers useless? Of course not. References and pointers each have their own use cases; the key is understanding their differences.

The advantage of references lies in safety and conciseness: they must be initialized, cannot be null, cannot be rebound, and don't require a dereference operator when used. These characteristics make references a better choice than pointers in scenarios where you need to "pass an object that definitely exists."

But there are many things references cannot do: you can't make a reference "point to null" to express the concept of "no object"; you can't make a reference "repoint" to another object; you can't make a reference point to an element array (there is no concept of an "array of references," although you can create an array of references); and you can't perform arithmetic on references to traverse memory. In these scenarios, pointers remain irreplaceable.

Our advice: **default to references, unless you need something references can't do**. Specifically, prefer references for function parameter passing (especially `const` references); use pointers (or C++17's `std::optional`) when you need to express "there might be no object"; and use pointers when you need to manually manage memory, traverse arrays, or implement data structures.

## 3. The Scope Resolution Operator `::`

### 3.1 Accessing Global Scope

The scope resolution operator `::` is a very fundamental but easily overlooked tool in C++. Its simplest use case is: when a local variable shadows a global variable, use `::` to tell the compiler "I mean the global one":

```cpp
int value = 100;  // 全局变量

void function() {
    int value = 50;  // 局部变量，遮蔽了全局的 value

    printf("Local: %d\n", value);      // 50
    printf("Global: %d\n", ::value);   // 100
}
```

In C, once a local variable shadows a global variable, there is no way to access the global version inside the function—unless you change the name. C++'s `::` solves this problem. That being said, **the best practice is still to avoid same-name shadowing**, because variables with the same name easily lead to confusion when reading code. While `::` can solve the syntactic problem, it doesn't solve the readability problem.

### 3.2 Accessing Namespace Members

Another core use of `::` is accessing members within a namespace. We already used this operator extensively when discussing namespaces earlier:

```cpp
namespace math {
    const double PI = 3.14159;

    double circumference(double radius) {
        return 2.0 * PI * radius;
    }
}

double c = math::circumference(5.0);
double pi = math::PI;
```

The semantics of `::` here are very clear: "retrieve the name on the right from the scope on the left." The left side can be a namespace, a class, a struct—or even empty (representing global scope).

### 3.3 Accessing Static Class Members

`::` can also be used to access static class members and nested types. Although we haven't formally covered classes in this chapter, this usage is very similar to namespaces, so let's get a quick glimpse:

```cpp
class UARTConfig {
public:
    static const int DEFAULT_BAUDRATE = 115200;
    enum Parity { NONE, EVEN, ODD };
};

int baud = UARTConfig::DEFAULT_BAUDRATE;
UARTConfig::Parity p = UARTConfig::NONE;
```

As we can see, the semantics of `::` are always consistent—"retrieve a certain name from a certain scope." Whether that scope is global, a namespace, or a class, `::` is the same operator doing the same thing.

## Summary

In this chapter, we learned three fundamental C++ features. Namespaces solve naming conflicts at the language level without introducing any runtime overhead—they are purely a compile-time "automatic prefix" mechanism. References provide aliases for variables, making function parameter passing safer and more concise than pointers, and `const` references can even bind to temporary objects and extend their lifetimes. The scope resolution operator `::` lets us precisely specify "which scope's name we want."

None of these three features involve object-oriented programming. You can use them immediately when writing any C++ code—even the simplest "better C" style code. In the next chapter, we'll look at two important improvements C++ made to function interface design: function overloading and default arguments.
