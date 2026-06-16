---
chapter: 0
cpp_standard:
- 11
- 14
- 17
- 20
description: The first step from C to C++ — namespaces for resolving name conflicts,
  references for passing arguments instead of pointers, and scope resolution for accessing
  global and namespace members. We thoroughly explain these three fundamental features.
difficulty: beginner
order: 3
platform: host
prerequisites:
- C语言速通复习
reading_time_minutes: 16
related:
- C++98函数接口：重载与默认参数
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: 'C++98 Primer: Namespaces, References, and Scope Resolution'
translation:
  source: documents/vol1-fundamentals/03A-cpp98-namespace-reference.md
  source_hash: c9a02a466063a6e1b10e6e834d7a9db8da7f07c18de16e8554247fad2acad855
  translated_at: '2026-06-16T03:31:35.037852+00:00'
  engine: anthropic
  token_count: 2550
---
# C++98 Primer: Namespaces, References, and Scope Resolution

> The full repository is available at [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP). Feel free to visit and give it a Star to motivate the author if you like it.

In the previous chapter, we systematically reviewed the core syntax of the C language. Starting from this chapter, we officially step into the world of C++. However, before diving into object-oriented programming, let's look at the immediate improvements C++ offers on a "non-object-oriented" level—namespaces solve naming conflicts in large projects, references allow us to say goodbye to the clumsy pointer syntax for function arguments, and the scope resolution operator allows us to tell the compiler precisely "which name I want."

None of these three features involve classes, nor do they require any object-oriented knowledge. They belong to the set of tools you can use immediately after migrating from C to C++. We put them first because they are simple enough, practical enough, and—crucially—will not interfere with performance at any level.

## 1. Namespaces

### 1.1 Why We Need Namespaces

In C projects, naming conflicts are a chronic headache. If your project uses three third-party libraries, and each has a function called `init`, congratulations—you'll get a pile of "multiple definition" errors at link time. The C convention is to prefix all names: `timer_init`, `uart_init`, `spi_init`... It sounds workable, but it's verbose to write, and it doesn't completely avoid conflicts (what if two libraries both use `hal_init`?).

C++ namespaces solve this problem at the language level. Essentially, they automatically add a "surname" to every name during compilation, but this "surname" is a structured, nestable prefix system provided by the namespace. Because this substitution happens at compile time, namespaces incur no runtime overhead—the final symbol names in the compiled output are exactly the same as if you had handwritten the prefixes, but you don't have to type out that long, ugly fully qualified name yourself.

### 1.2 Defining and Using Namespaces

Let's look directly at a piece of embedded-style code. Suppose we are developing a sensor module:

```cpp
// sensor.h
namespace Driver {
    constexpr int MAX_SENSORS = 10;

    struct Config {
        int sample_rate;
        bool enabled;
    };

    void init(const Config& cfg);
    void read(int id);
}
```

Definitions can be scattered across multiple files—meaning you can declare `Driver::init` in a header file, and then wrap the implementation in the same `namespace Driver` in the corresponding `.cpp` file. The compiler automatically merges all declarations belonging to the same namespace.

When implementing, we write it like this:

```cpp
// sensor.cpp
#include "sensor.h"

namespace Driver {
    // Constants and structures from the header are implicitly visible here
    void init(const Config& cfg) {
        // Implementation details
    }

    void read(int id) {
        // Implementation details
    }
}
```

There are three ways to use them, from most explicit to most permissive:

```cpp
// Method 1: Fully qualified name
Driver::init(cfg);

// Method 2: Using declaration
using Driver::init;
init(cfg); // OK
read(0);   // Error: read not declared in this scope

// Method 3: Using directive
using namespace Driver;
init(cfg); // OK
read(0);   // OK
```

Each method has its use cases. Method 1 is best suited for function bodies in `.cpp` files; while it requires more typing, it is foolproof. Method 2 is suitable when you frequently use only a few specific names from a namespace. Method 3... honestly, if you use `using namespace` inside a function body in a `.cpp` file, most people won't mind; but if you put it in a header file, especially at the global scope level—that's basically burying a landmine in the codebase that will eventually explode.

Regarding the dangers of `using namespace` in headers, I won't write a long essay here. Just remember one iron rule: **Never write `using namespace` in a header file**. The reason is simple—`using namespace` is irreversible. Once a header globally introduces a namespace, all code that `#include`s that header is forced to accept all symbols from that namespace, often without even knowing it. When two different libraries define identically named symbols in their respective namespaces, and your header `using namespace`s both—congratulations, ambiguity errors will pop up in the most unexpected places.

### 1.3 Nested Namespaces

Namespaces can be nested. This feature is very practical when organizing complex codebases because we can use the namespace hierarchy to reflect the module hierarchy. For example, a hardware abstraction layer:

```cpp
namespace HAL {
    namespace GPIO {
        void init();
        void set(int pin);
    }

    namespace UART {
        void init();
        void send(char data);
    }
}
```

When using them:

```cpp
HAL::GPIO::init();
HAL::UART::send('A');
```

If `HAL::GPIO` feels too long, you can use a namespace alias to simplify it:

```cpp
namespace Gpio = HAL::GPIO;
Gpio::init();
```

Aliases are only valid in the current scope, so you can safely give the same namespace different short names in different functions without polluting the global scope.

It is worth mentioning that C++17 introduced a more concise nested syntax:

```cpp
namespace HAL::GPIO {
    void init();
    void set(int pin);
}
```

This syntax is just syntactic sugar; it is functionally equivalent to manual nesting, but it does make the code much cleaner. If your project is still on C++11/14, just stick to writing them out layer by layer.

### 1.4 Anonymous Namespaces

Anonymous namespaces are an easily overlooked but very practical feature in C++. Their purpose is to provide **file-level scope**—anything defined inside an anonymous namespace is visible only to the current translation unit (i.e., the current `.cpp` file) and is completely invisible to the outside.

In C, we use the `static` keyword to achieve a similar effect:

```cpp
// utils.c
static void internal_helper() {
    // Only visible in this file
}
```

In C++, it is recommended to use anonymous namespaces instead of `static`:

```cpp
// utils.cpp
namespace {
    void internal_helper() {
        // Only visible in this file
    }

    // Can also contain types!
    struct LocalConfig {
        int value;
    };
}
```

Why does C++ recommend anonymous namespaces over `static`? There are two key reasons. First, `static` only applies to functions, variables, and anonymous unions; it **cannot** apply to type definitions—you cannot write `static struct MyStruct`. Anonymous namespaces, however, can wrap anything: classes, structs, enums, templates—you name it. Second, starting from C++11, entities in anonymous namespaces are explicitly given internal linkage, which is semantically equivalent to `static` but with a broader scope. The C++ Core Guidelines and clang-tidy both suggest prioritizing anonymous namespaces.

Of course, `static` hasn't been deprecated—it's retained for C compatibility. In actual projects, mixing both won't cause issues, but consistency is a good habit. My advice is: **Use anonymous namespaces for all new code; don't rush to change old code**, unless you are refactoring that specific area.

## 2. References

### 2.1 What Is a Reference

A reference is a core concept introduced in C++—it provides an **alias** for a variable. "Alias" might sound abstract, so think of it this way: a reference is like a nickname for a person; whether you call them by their real name or nickname, you're referring to the same person. Under the hood, references are usually implemented via pointers, but syntactically, they are much safer and more concise than pointers.

The most basic usage:

```cpp
int value = 10;
int& ref = value; // ref is a reference to value

ref = 20;         // value becomes 20
```

References have two very important constraints, and understanding them is the prerequisite for avoiding pitfalls. First, **a reference must be initialized when declared**—you cannot declare a reference first and bind it to a variable later. This is different from pointers: a pointer can be declared as `nullptr` first and assigned later, but a reference cannot. Second, **once a reference is bound, it cannot be rebound to another variable**. Look at this confusing example:

```cpp
int a = 100;
int b = 200;
int& ref = a;
ref = b; // What happens here?
```

This line does not make `ref` point to `b`. Instead, it assigns the value of `b` (200) to the object referenced by `ref` (which is `a`). After execution, `a` becomes 200, and `ref` is still a reference to `a`. This distinction is critical—the binding of a reference is **one-time**; subsequent assignment operations merely modify the value of the referenced object.

### 2.2 References as Function Parameters

The most common use for references is as function parameters. In C, if a function needs to modify a caller's variable or avoid the overhead of copying large objects, we pass pointers. But pointer syntax is clumsy—there are `&`s and `*`s everywhere, and you have to check for null pointers before every use. References solve both problems perfectly.

Let's use an embedded scenario to compare three parameter passing methods:

```cpp
struct SensorData {
    int id;
    float values[128];
};

// Method 1: Pass by value (inefficient copy)
void process_by_value(SensorData data) {
    data.id = 0; // Local modification only
}

// Method 2: Pass by pointer (C style)
void process_by_ptr(SensorData* data) {
    if (data) { // Must check for null
        data->id = 0;
    }
}

// Method 3: Pass by reference (C++ style)
void process_by_ref(SensorData& data) {
    data.id = 0; // Safe, concise, no null check needed
}
```

Passing by reference is the cleanest—no `*`, no `->`, no null check needed. In most cases, if you want to "let a function modify a caller's variable" in C++, references should be your first choice.

But that's not the end of the story. Often, we pass parameters not to modify them, but to avoid copy overhead—such as a struct containing a large amount of data or a string. In these cases, using a `const` reference is the best choice:

```cpp
// Read-only access, no copy overhead
void print_data(const SensorData& data) {
    // data.id = 0; // Compile error! Cannot modify const reference
    printf("Sensor ID: %d\n", data.id);
}
```

The beauty of `const` references is that it achieves both "no copy" and "no modification" goals simultaneously. The caller sees `const` and knows the function won't touch their data; the compiler sees `const` and intercepts any modification attempts at compile time. This pattern was very common in C++98 and is basically the standard paradigm for "passing read-only large objects."

### 2.3 Const References and Temporary Object Lifetimes

Here is a very important detail, and a place where many C++ learners stumble. When we bind a `const` reference to a temporary object (an rvalue), C++ **extends the lifetime of this temporary object** to match the lifetime of the reference:

```cpp
const int& ref = 100 + 200; // The temporary result of 100+200 lives as long as ref
printf("%d", ref);          // Safe
```

This might not look like a big deal—after all, how big is an `int`? But when the temporary object is a complex type, this rule becomes crucial:

```cpp
std::string get_name(); // Returns a temporary string

void process() {
    const std::string& name = get_name();
    // The temporary string returned by get_name() is destroyed
    // only when 'name' goes out of scope, not immediately.
    use(name); // Safe
}
```

However, this lifetime extension has a **key precondition**: the reference must be **directly bound** to the temporary object. If the reference is indirectly bound through a function return intermediate value, lifetime extension will not apply. This is a more advanced topic; for now, let's just remember the rule "direct binding is required," and we will discuss this further when covering return value optimization and move semantics.

### 2.4 References as Return Values

Functions can return references, which provides us with two very practical programming patterns: chaining calls and subscript access.

The core idea of chaining is to let the function return a reference to `*this`, so the caller can chain multiple operations in one line of code:

```cpp
struct Counter {
    int value;
    Counter& increment() {
        value++;
        return *this;
    }
};

Counter c;
c.increment().increment().increment(); // value becomes 3
```

Subscript access returns a reference to an internal element, allowing the caller to read and write data in the container directly via `[]`:

```cpp
class Array {
    int data[10];
public:
    int& operator[](int index) {
        return data[index];
    }
};

Array arr;
arr[5] = 100; // Writes via the returned reference
```

But returning references has a **fatal trap**: absolutely never return a reference to a local variable. Local variables are stored on the stack; when the function returns, the stack frame is reclaimed. At that point, the reference points to freed memory—this is typical undefined behavior; the program might run sometimes, crash other times, and the crash location and reason will be unpredictable.

```cpp
int& bad_function() {
    int temp = 42;
    return temp; // ERROR: Returning reference to local variable!
}
```

The principle for judging whether returning a reference is safe is simple: **the lifetime of the referenced object must be longer than the function call itself**. Member variables, global variables, static variables, and objects passed in as parameters—these are safe. Local variables within the function body—unsafe.

### 2.5 References vs. Pointers: When to Use Which

Since references are so good, are pointers useless? Of course not. References and pointers each have their uses; the key is understanding their differences.

The advantage of references lies in safety and conciseness: they must be initialized, cannot be null, cannot be rebound, and don't need a dereference operator when used. These features make references a better choice than pointers in scenarios where you are "passing a definitely existing object."

But there are many things references cannot do: you cannot make a reference "point to null" to express the concept of "no object"; you cannot make a reference "repoint" to another object; you cannot make a reference point to an array of elements (there is no concept of "array of references," though you can have an array of references); you cannot perform arithmetic on references to traverse memory. In these scenarios, pointers remain indispensable.

My advice is: **use references by default, unless you need something references can't do**. Specifically, prefer references for function parameter passing (especially `const` references); use pointers when you need to express "possibly no object" (or C++17's `std::optional`); use pointers when you need to manually manage memory, traverse arrays, or implement data structures.

## 3. Scope Resolution Operator `::`

### 3.1 Accessing Global Scope

The scope resolution operator `::` is a very basic but easily overlooked tool in C++. Its simplest use is: when a local variable shadows (hides) a global variable, use `::` to tell the compiler "I want the global one":

```cpp
int count = 0; // Global

void func() {
    int count = 5; // Local
    printf("%d", count);  // Prints 5 (local)
    printf("%d", ::count); // Prints 0 (global)
}
```

In C, once a local variable shadows a global variable, there is no way to access the global version inside the function—unless you change the name. C++'s `::` solves this problem. That said, **the best practice is still to avoid name shadowing**, because variables with the same name can lead to confusion when reading code. While `::` solves the syntactic problem, it doesn't solve the readability problem.

### 3.2 Accessing Namespace Members

Another core use of `::` is to access members of a namespace. We have already used this operator extensively when discussing namespaces:

```cpp
Driver::init(cfg);
HAL::GPIO::set(5);
```

The semantics of `::` here are very clear: from the "scope" on the left, take out the "name" on the right. The left side can be a namespace, a class, a struct—or even empty (indicating global scope).

### 3.3 Accessing Class Static Members

`::` can also be used to access static members and nested types of a class. Although we haven't formally covered classes yet, this usage is very similar to namespaces, so let's get familiar with it now:

```cpp
class Hardware {
public:
    static constexpr int VERSION = 1;

    struct Pins {
        int tx;
        int rx;
    };
};

int main() {
    int v = Hardware::VERSION;
    Hardware:: Pins p = { 1, 2 };
}
```

As you can see, the semantics of `::` are always unified—"take a name from a certain scope." Whether that scope is global, a namespace, or a class, `::` is the same operator doing the same thing.

## Run Online

Run a comprehensive example of namespaces, references, and scope resolution online:

<OnlineCompilerDemo
  title="Namespaces, References, and Scope Resolution"
  source-path="code/examples/vol1/14_namespace_reference.cpp"
  description="Run online and observe the actual behavior of namespace nesting, reference passing, and :: scope resolution."
  allow-run
/>

## Summary

In this chapter, we learned three basic features of C++. Namespaces solve naming conflicts at the language level without any runtime overhead—they are purely a compile-time "automatic prefix" mechanism. References provide aliases for variables; when passing function arguments, they are safer and more concise than pointers, and `const` references can even bind to temporary objects and extend their lifetime. The scope resolution operator `::` allows us to specify exactly "which name in which scope" we want.

None of these three features involve object-oriented programming; you can use them immediately when writing any C++ code—even the simplest "better C" style code. In the next article, we will look at two important improvements C++ makes to function interface design: function overloading and default parameters.
