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
  source_hash: 75e180340b51863e6836426da286ee3c8758c395826196a7192685bd377b64eb
  translated_at: '2026-05-26T10:21:55.262119+00:00'
  engine: anthropic
  token_count: 2555
---
# Getting Started with C++98: Namespaces, References, and Scope Resolution

> The full repository is available at [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP). Feel free to check it out, and if you like it, give it a Star to encourage the author.

In the previous chapter, we systematically reviewed the core syntax of C. Starting from this chapter, we officially step into the world of C++. But before diving into object-oriented programming, let's look at the immediate improvements C++ offers on the "non-object-oriented" level—namespaces solve naming conflicts in large projects, references free function parameter passing from the clumsiness of pointer syntax, and the scope resolution operator lets us precisely tell the compiler "this is the name I want."

None of these three features involve classes, nor do they require any object-oriented background knowledge. They belong to the things you can use immediately after migrating from C to C++. We put them first because they are simple enough, practical enough, and—most importantly—they do not interfere with performance at any level.

## 1. Namespaces

### 1.1 Why We Need Namespaces

In C projects, naming conflicts are an old headache. If your project uses three third-party libraries, and each library has a function called `init()`, congratulations—you'll get a bunch of "multiple definition" errors at the linking stage. The C convention is to prefix all names: `sensor_init()`, `uart_init()`, `display_init()`... It sounds workable, but it's tedious to write, and it doesn't completely avoid conflicts (what if two libraries both use `network_buffer_create()`?).

C++ namespaces solve this problem at the language level. Essentially, they automatically add a "last name" to every name during compilation, but this "last name" is a structured, nestable prefix system provided by namespaces. Because this substitution happens at compile time, namespaces incur zero runtime overhead—the final compiled symbol names are exactly the same as if you had handwritten the prefixes, but you don't have to type out those long, ugly fully qualified names yourself.

### 1.2 Defining and Using Namespaces

Let's look directly at a piece of embedded-style code. Suppose we are developing a sensor module:

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

The definitions can be spread across multiple files—meaning you can first declare `sensor::init()` in a header file, and then wrap the implementation with the same `namespace sensor { ... }` in the corresponding `.cpp` file. The compiler automatically "merges" all declarations within the same namespace.

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

There are three ways to use them, from the most explicit to the most permissive:

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

Each of these three approaches has its own suitable scenarios. Method one is best suited for function bodies in `.cpp` files; although it requires more typing, it absolutely won't cause problems. Method two is suitable when you frequently use only a few names from a particular namespace. As for method three... honestly, if you use `using namespace std` inside a function body in a `.cpp` file, most people won't say anything; but if you put it in a header file at global scope—that's basically planting a landmine in your codebase that will eventually go off.

Regarding the dangers of `using namespace` in header files, we won't launch into a lengthy discussion here. Just remember one ironclad rule: **never write `using namespace` in a header file**. The reason is simple—`using namespace` is irreversible. Once a header file globally introduces a namespace, all code that `#include`s that header is forced to accept all symbols from that namespace, and they might not even know it. When two different libraries define symbols with the same name in their respective namespaces, and your header file `using`s both namespaces—congratulations, ambiguity errors will pop up in the most unexpected places.

### 1.3 Nested Namespaces

Namespaces can be nested. This feature is very practical when organizing complex codebases because we can use namespace hierarchies to reflect module hierarchies. For example, a hardware abstraction layer:

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

If you find `hardware::gpio::` too long, you can use a namespace alias to simplify it:

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

This syntax is just syntactic sugar; it is functionally equivalent to manual nesting, but it does make the code much cleaner. If your project is still using C++11/14, just honestly write them out layer by layer.

### 1.4 Anonymous Namespaces

Anonymous namespaces are an easily overlooked but highly practical feature in C++. Their purpose is to provide **file-level scope**—anything defined inside an anonymous namespace is visible only to the current translation unit (i.e., the current `.cpp` file) and is completely invisible to the outside.

In C, we use the `static` keyword to achieve a similar effect:

```c
// C 风格：限制在当前文件可见
static int buffer_size = 256;
static void internal_helper() { /* ... */ }
```

In C++, using an anonymous namespace is recommended to replace `static`:

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

Why does C++ recommend anonymous namespaces over `static`? There are two key reasons. First, `static` only applies to functions, variables, and anonymous unions, but **not** to type definitions—you cannot write `static class Foo { ... };`. Anonymous namespaces, on the other hand, can wrap anything: classes, structs, enums, templates—nothing is off-limits. Second, starting from C++11, entities in anonymous namespaces are explicitly given internal linkage, making them semantically equivalent to `static` but with a broader scope of application. Both the C++ Core Guidelines and clang-tidy recommend preferring anonymous namespaces.

Of course, `static` hasn't been deprecated—it's retained for C compatibility. In real projects, mixing the two won't cause issues, but maintaining consistency is a good habit. Our advice is: **use anonymous namespaces for all new code, and don't rush to change it when you see it in old code**, unless you are actively refactoring that particular section.

## 2. References

### 2.1 What Is a Reference

A reference is a core concept introduced in C++—it provides an **alias** for a variable. Calling it an "alias" might be a bit abstract, so we can understand it this way: a reference is like giving a person a nickname; whether you call them by their real name or their nickname, you're referring to the same person. At the bottom level, references are usually implemented through pointers, but at the syntax level, references are much safer and more concise than pointers.

The most basic usage:

```cpp
int value = 42;
int& ref = value;  // ref 是 value 的引用（别名）

ref = 100;         // 修改 ref 就是修改 value
// 此时 value 也变成了 100
```

References have two very important constraints, and understanding them is a prerequisite for avoiding pitfalls. First, **a reference must be initialized at declaration**—you cannot declare a reference first and then make it point to a variable later. This is different from pointers: a pointer can first be declared as `nullptr` and assigned later, but a reference cannot. Second, **once a reference is bound, it cannot be rebound to another variable**. Look at this easily confusing example:

```cpp
int other = 200;
ref = other;  // 这不是重新绑定！
```

This line of code does not make `ref` point to `other`; rather, it assigns the value of `other` (200) to the object referenced by `ref` (which is `value`). After execution, `value` becomes 200, and `ref` is still a reference to `value`. This distinction is very important—the binding of a reference is **one-time**, and subsequent assignment operations only modify the value of the referenced object.

### 2.2 References as Function Parameters

The most common use of references is as function parameters. In C, if a function needs to modify the caller's variable or avoid the copy overhead of a large object, we pass a pointer. But pointer syntax is clumsy—there are `*`s and `->`s everywhere, and you have to check for null pointers every time before using them. References perfectly solve both problems.

Let's use an embedded scenario as an example to compare three parameter-passing methods:

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

Passing by reference is the cleanest approach—no `*`, no `->`, and no null pointer checks needed. In most cases, if you want to "let a function modify the caller's variable" in C++, a reference should be your first choice.

But the story doesn't end here. Often, we pass parameters not to modify them, but to avoid copy overhead—such as a struct containing a large amount of data, or a string. In these cases, using a `const` reference is the best choice:

```cpp
// const 引用：既高效又防止修改
void read_only_access(const SensorData& data) {
    float temp = data.temperature;  // 可以读取
    // data.temperature = 0;  // 错误！编译器会阻止你修改 const 引用
}
```

The elegance of a `const` reference lies in that it simultaneously achieves two goals: "no copy" and "no modification." The caller sees `const SensorData&` and knows this function won't modify their data; the compiler sees `const` and will intercept any modification attempts at compile time. This pattern was already very common in C++98 and is basically the standard paradigm for "passing read-only large objects."

### 2.3 const References and the Lifetime of Temporary Objects

Here is a very important detail, and also a place where many C++ learners easily stumble. When we bind a temporary object (an rvalue) with a `const` reference, C++ **extends the lifetime of this temporary object**, making it live as long as the reference:

```cpp
const int& ref = 42;  // OK！42 本来是个临时值，但 const 引用延长了它的寿命
// ref 在整个作用域内都有效
```

This might not seem like a big deal—after all, how big is a `int`? But when the temporary object is a complex type, this rule becomes crucial:

```cpp
std::string get_name();

const std::string& name = get_name();
// get_name() 返回的临时 string 本来在完整表达式结束后就该销毁
// 但因为绑定了 const 引用，它的生命被延长到了 name 的整个生命周期
// 所以 name 在整个作用域内都是安全的
```

However, this lifetime extension has a **key prerequisite**: the reference must **directly bind** to the temporary object. If the reference is indirectly bound through an intermediate value returned by a function, the lifetime extension will not take effect. This is a relatively advanced topic; for now, just remember the rule that "direct binding is required for it to work." Later, when we discuss return value optimization and move semantics, we will come back and explore this in detail.

### 2.4 References as Return Values

Functions can return references, which provides us with two very practical programming patterns: chained calls and subscript access.

The core idea of chained calls is to have a function return a reference to `*this`, so the caller can chain multiple operations together in a single line of code:

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

Subscript access, by returning a reference to an internal element, allows the caller to directly read from and write to data inside a container via `[]`:

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

But returning a reference has a **fatal pitfall**: never return a reference to a local variable. Local variables are stored on the stack, and once the function returns, the stack frame is reclaimed. At that point, the reference points to a piece of memory that has already been freed—this is typical undefined behavior. The program might occasionally run fine, occasionally crash, and the crash location and reason will be completely unpredictable.

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

The principle for determining whether returning a reference is safe is simple: **the lifetime of the referenced object must be longer than the function call itself**. Member variables, global variables, static variables, and objects passed in via parameters—these are all safe. Local variables inside a function body—are not safe.

### 2.5 References vs. Pointers: When to Use Which

Since references are so great, are pointers useless now? Of course not. References and pointers each have their own use cases; the key is understanding their differences.

The advantage of references lies in safety and conciseness: they must be initialized, cannot be null, cannot be rebound, and don't require a dereference operator when used. These characteristics make references a better choice than pointers in the scenario of "passing an object that definitely exists."

But there are many things references cannot do: you cannot make a reference "point to null" to express the concept of "no object"; you cannot make a reference "repoint" to another object; you cannot make a reference point to an element array (there is no concept of an "array of references," although you can create an array of references); and you cannot perform arithmetic operations on references to traverse memory. In these scenarios, pointers remain irreplaceable.

Our advice is: **default to references, unless you need something references can't do**. Specifically, prefer references for function parameter passing (especially `const` references); use pointers (or C++17's `std::optional`) when you need to express "there might be no object"; and use pointers when you need to manually manage memory, traverse arrays, or implement data structures.

## 3. The Scope Resolution Operator `::`

### 3.1 Accessing Global Scope

The scope resolution operator `::` is a very basic but easily overlooked tool in C++. Its simplest use case is: when a local variable shadows a global variable, use `::` to tell the compiler "I want the global one":

```cpp
int value = 100;  // 全局变量

void function() {
    int value = 50;  // 局部变量，遮蔽了全局的 value

    printf("Local: %d\n", value);      // 50
    printf("Global: %d\n", ::value);   // 100
}
```

In C, once a local variable shadows a global variable, there is no way to access the global version inside the function—unless you change the name. C++'s `::` solves this problem. That being said, **the best practice is still to avoid same-name shadowing**, because variables with the same name easily lead to confusion when reading code. While `::` can solve the syntax-level problem, it doesn't solve the readability problem.

### 3.2 Accessing Namespace Members

Another core use of `::` is to access members within a namespace. We already used this operator extensively when discussing namespaces earlier:

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

The semantics of `::` here are very clear: from the "scope" on the left, retrieve the "name" on the right. The left side can be a namespace, a class, a struct—or even empty (representing global scope).

### 3.3 Accessing Static Members of a Class

`::` can also be used to access static members and nested types of a class. Although we haven't formally covered classes in this chapter yet, this usage is very similar to namespaces, so let's get familiar with it in advance:

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

## Run Online

Run a comprehensive example of namespaces, references, and scope resolution online:

<OnlineCompilerDemo
  title="Namespaces, References, and Scope Resolution"
  source-path="code/examples/vol1/14_namespace_reference.cpp"
  description="Run online and observe the actual behavior of namespace nesting, pass-by-reference, and :: scope resolution."
  allow-run
/>

## Summary

In this chapter, we learned three fundamental features of C++. Namespaces solve the naming conflict problem at the language level without incurring any runtime overhead—they are purely a compile-time "automatic prefix" mechanism. References provide aliases for variables, making function parameter passing safer and more concise than pointers, and `const` references can even bind to temporary objects and extend their lifetime. The scope resolution operator `::` allows us to precisely specify "which name from which scope we want."

None of these three features involve object-oriented programming; you can use them immediately when writing any C++ code—even the simplest "better C" style code. In the next article, we will look at two important improvements C++ made to function interface design: function overloading and default arguments.
