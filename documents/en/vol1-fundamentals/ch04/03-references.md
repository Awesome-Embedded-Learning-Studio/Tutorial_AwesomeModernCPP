---
chapter: 4
cpp_standard:
- 11
- 14
- 17
- 20
description: 'Deep dive into C++ references: reference syntax, the difference between
  references and pointers, and the vital role of const references in function parameters.'
difficulty: beginner
order: 3
platform: host
prerequisites:
- 指针运算与数组
reading_time_minutes: 15
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: Reference
translation:
  source: documents/vol1-fundamentals/ch04/03-references.md
  source_hash: a94eb73c8884c3ac8abbe5e2e9dcd83c8802dc8728a2f6fa4cb4adee1212d07c
  translated_at: '2026-06-16T03:43:25.233300+00:00'
  engine: anthropic
  token_count: 2311
---
# References

Pointers are powerful, but honestly, they are also prone to causing trouble. In the previous chapter, we spent a lot of time dealing with pointers—dereferencing, taking addresses, null pointer checks, the `*` operator... After writing enough code, you will realize that in many scenarios, we don't need the full capabilities of pointers. We just want to "pass a large object to a function without copying it," or "let a function modify the caller's variable." Pointers can certainly handle these requirements, but the syntax always feels clunky. C++ offers us a safer and more concise alternative: **references**. In this chapter, we will thoroughly understand references from the ground up.

## Step 1 — What exactly is a reference?

The essence of a reference is an **alias**—another name for an existing variable. It's just like a colleague named "Zhang San" whom everyone calls "Lao Zhang"; regardless of which name you call out, it refers to the same person. At the underlying implementation level, references are usually implemented via pointers, but the language layer shields us from those dangerous pointer operations, leaving us with only a clean "another name."

Let's look at the most basic usage:

```cpp
int value = 10;
int& ref = value; // ref is an alias for value
ref = 20;         // value is now 20
```

`int& ref = value;` This line does two things: it declares `ref` as a reference bound to `int`, and immediately binds it to `value`. From this line onward, `ref` and `value` are the same thing—any operation on `ref` is equivalent to an operation on `value`. No extra memory overhead, no syntactic burden of indirection, it's just that simple.

However, references have two very strict constraints. Understanding them is the prerequisite for using references safely. First, **a reference must be initialized when declared**. You cannot write `int& ref;` and then make it point to a variable later—this code won't compile at all. Unlike pointers, which can be set to `nullptr` first and dealt with later, a reference must be bound to a real object from the moment it is born. Second, **a reference cannot be rebound once bound**. This point is particularly easy to trip on, so let's look at it separately:

```cpp
int a = 100;
int b = 200;
int& ref = a; // ref is bound to a
ref = b;      // What happens here?
```

The effect of `ref = b;` here is—it assigns the value of `b` (200) to the object referenced by `ref` (which is `a`). After execution, `a` becomes 200, `ref` is still a reference to `a`, and has nothing to do with `b`. The binding of a reference is **one-time** and **irrevocable**; all subsequent assignment operations only modify the value of the referenced object.

> ⚠️ **Pitfall Warning**
> Many beginners see `ref = b;` and mistakenly think it means "rebinding." In fact, C++ has no syntax for "rebinding a reference"—all assignments to a reference are assignments to the referenced object. If you need "re-pointable" semantics, what you need is not a reference, but a pointer.

## Step 2 — References vs. Pointers, which one to choose?

Since both references and pointers can achieve "indirect object manipulation," what exactly is the difference between them? Let's compare them point by point:

**Must initialize vs. Can be dangling**. A reference must be bound to an object when declared, so a reference is always "valid" (provided you haven't created a dangling reference, which is an advanced bug). A pointer can be declared as `nullptr` first and assigned later, which is flexible but also means you have to consider "could it be null?" every time you use it.

**Non-rebindable vs. Re-pointable**. A reference is bound for life once initialized; a pointer can point to a different object at any time. If you need to traverse memory in an "iterator-like" fashion, or need to express the semantics of "possibly no object," pointers are the only choice.

**No dereference syntax vs. Needs `*` and `&`**. Using a reference is just like using a normal variable; you write the name directly. Pointers require `*` or `->` to access the target, making the code look significantly more verbose.

**No null reference vs. Null pointer**. Strictly speaking, "null references" do not exist in C++—a reference must be bound to a valid object. But pointers can be `nullptr`, which is both the source of its flexibility and the source of many bugs.

Let's use a practical example to feel the difference between the two. Suppose we have a struct that needs to be modified in a function:

```cpp
struct Config {
    int baudrate;
    int timeout;
};

// Using pointers
void update_config(Config* cfg) {
    if (cfg) { // Must check for null
        cfg->baudrate = 115200;
    }
}

// Using references
void update_config(Config& cfg) {
    cfg.baudrate = 115200; // No null check needed
}
```

So when should you use a pointer? My suggestion is—**use references by default, unless you need something references cannot do**. Specifically, use a pointer (or `std::optional`, which we will learn about later) when you need to express the concept of "possibly no object"; use a pointer when you need to change the target at runtime; use a pointer when you need to do pointer arithmetic to traverse memory. For all other scenarios, references are the safer choice.

> ⚠️ **Pitfall Warning**
> Strictly speaking, through certain "unconventional means," you can create a reference bound to a null address, such as `int& ref = *(int*)nullptr;`. This line compiles, but using `ref` is undefined behavior. Never write code like this—if someone says "references can also be null," they are exploiting a loophole in the language rules, and such code should never appear in actual engineering.

## Step 3 — References as function parameters

The most common use of references is as function parameters. Let's first look at a classic example: swapping the values of two variables. In C, we can only pass pointers:

```cpp
void swap(int* a, int* b) {
    int temp = *a;
    *a = *b;
    *b = temp;
}

// Usage
int x = 1, y = 2;
swap(&x, &y);
```

Rewriting with references, the whole world becomes peaceful:

```cpp
void swap(int& a, int& b) {
    int temp = a;
    a = b;
    b = temp;
}

// Usage
int x = 1, y = 2;
swap(x, y);
```

Inside the function, no `*` dereferencing is needed; at the call site, no `&` address-taking is needed—code readability has taken a step up. The standard library's `std::swap` is also implemented using references, with the exact same principle.

But often we pass parameters not to modify them, but to **avoid copy overhead**. A struct containing a large amount of data, a long string—if passed by value, the entire thing must be copied, wasting both stack space and time. This is where `const T&` references come into play:

```cpp
void print_config(const Config& cfg) {
    // Read only, no copy overhead
    std::cout << cfg.baudrate << "\n";
}
```

The `const T&` combination appears extremely frequently in C++; it is basically the standard paradigm for "passing read-only large objects." `const` tells the compiler and the caller two things: first, this function will not modify the passed object; second, the compiler will intercept any modification attempts at compile time. When the caller sees the parameter is `const&`, they can confidently hand over the data without worrying about it being secretly tampered with.

Of course, there is a practical rule of thumb: for basic types (`int`, `double`, pointers, etc.), just pass by value, as the copy overhead is negligible; for anything larger than basic types—`std::string`, structs, containers—pass `const` references.

## Step 4 — References as return values

Functions can also return references, which is a very practical pattern in C++. The most common usage is to return a reference to a class member, allowing external code to directly read and write internal data:

```cpp
class Register {
    int value;
public:
    Register(int v) : value(v) {}

    int& get() { return value; } // Returns a reference
};

// Usage
Register r(0);
r.get() = 42; // Directly modifies internal value
```

Another classic application of returning references is **chaining**—making a function return a reference to `*this`, so the caller can chain multiple operations in one line of code. The standard library's `std::cout` works this way: `std::cout << x << y` can output continuously because each `<<` returns a reference to `std::cout`.

But returning references has a **fatal trap**—absolutely do not return a reference to a local variable. Local variables are stored on the stack, and after the function returns, the stack frame is reclaimed. At that point, the reference points to a block of memory that has been freed:

```cpp
int& dangerous() {
    int temp = 42;
    return temp; // DON'T DO THIS!
}

int& ref = dangerous(); // ref is now a dangling reference
std::cout << ref;      // Undefined behavior
```

The insidious nature of this bug is that the program may occasionally run well, and occasionally crash inexplicably, with the crash location and cause showing no pattern. This is because when that block of stack memory hasn't been overwritten yet, the reference can still read the "correct" value; once it is overwritten by subsequent function calls, what is read is garbage data.

> ⚠️ **Pitfall Warning**
> The rule for judging whether returning a reference is safe is simple—**the lifetime of the referenced object must be longer than the function call itself**. Member variables, global variables, static variables, and objects passed in via parameters are all safe. Local variables within the function body are absolutely unsafe. Compilers usually issue a warning for this, but not all cases can be detected—so this rule must be etched into your brain.

## Step 5 — const references and temporary objects

C++ has a feature that looks strange at first glance: a `const` reference can bind to a temporary object (an rvalue), and will **extend the lifetime of this temporary object** to live and die together with the reference.

```cpp
const int& ref = 42; // Binds to a temporary int
```

What does this line do? The literal `42` is originally an rvalue, and theoretically should disappear after the expression ends. But because `ref` is a `const` reference and is directly bound to this temporary value, C++ rules require the compiler to extend the lifetime of this temporary value to the end of `ref`'s scope. In other words, the compiler quietly creates a temporary `int` behind the scenes, initializes it with 42, and then binds `ref` to this temporary `int`.

This isn't a big deal for `int`, but it is critical for complex types:

```cpp
std::string join(const std::string& a, const std::string& b) {
    return a + b; // Returns a temporary string
}

const std::string& result = join("Hello", " World");
// The temporary string's lifetime is extended here
```

However, there is an important limitation here—**the reference must be directly bound to the temporary object** for lifetime extension to take effect. If there are intermediate steps like function returns, the rule doesn't hold. This topic involves return value optimization and move semantics, which will be discussed in later chapters.

You may have noticed that non-const references cannot bind to temporary objects: `int& r = 42;` won't compile. The reason is also reasonable—if allowing a non-const reference to bind to a temporary value, then modifying through the reference would be modifying an object about to disappear, which is meaningless. `const` references are allowed because they promise read-only access; the compiler knows you won't change that temporary value, so it safely extends its life for you.

## Practical Exercise — references.cpp

Let's integrate the content we learned earlier into a complete program, focusing on comparing the usage differences between references and pointers:

```cpp
#include <iostream>
#include <string>

// 1. Reference parameter: modifies caller's variable
void swap(int& a, int& b) {
    int temp = a;
    a = b;
    b = temp;
}

// 2. const reference: avoids copy, ensures read-only
void print_data(const std::string& data) {
    std::cout << "Data: " << data << "\n";
}

// 3. Return reference: supports chain calls and direct modification
class Counter {
    int count = 0;
public:
    int& get() { return count; }

    Counter& increment() {
        ++count;
        return *this; // Return reference to *this
    }
};

int main() {
    // Test swap
    int x = 10, y = 20;
    std::cout << "Before swap: x=" << x << ", y=" << y << "\n";
    swap(x, y);
    std::cout << "After swap: x=" << x << ", y=" << y << "\n";

    // Test const reference
    std::string large_data = "This is a large string...";
    print_data(large_data); // No copy happened

    // Test return reference
    Counter c;
    c.increment().increment().increment(); // Chaining
    std::cout << "Count: " << c.get() << "\n";

    // Test temporary lifetime extension
    const std::string& temp_ref = std::string("Temporary");
    std::cout << "Extended lifetime: " << temp_ref << "\n";

    return 0;
}
```

Compile and run:

```text
g++ -std=c++17 references.cpp -o references && ./references
```

Result:

```text
Before swap: x=10, y=20
After swap: x=20, y=10
Data: This is a large string...
Count: 3
Extended lifetime: Temporary
```

Let's review what this program did section by section. `swap` uses reference parameters to implement variable swapping; when calling, we pass variable names directly without needing the address-of operator. `print_data` receives parameters using `const&`, which both avoids the struct copy overhead and guarantees at the type system level that the function will not modify the passed data—the caller can rest assured just by looking at the function signature. `Counter::get` returns a reference to a member variable; external code gets the reference and can assign directly, achieving controlled access to internal data. Finally, `temp_ref` demonstrates the ability of a const reference to extend the lifetime of a temporary object—`std::string("Temporary")` was originally a temporary object about to disappear, but because it was bound by a const reference, it lived until the end of the `main` function.

## Try it yourself

### Exercise 1: Refactor a pointer function

The following function implements a simple "double array elements" feature using pointers. Convert it to a reference version:

```cpp
void double_array(int* arr, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        arr[i] *= 2;
    }
}
```

Hint: C-style arrays cannot directly use reference passing to retain length information; consider using `std::array` or `std::vector` instead.

### Exercise 2: Find the bugs

The following code has several issues related to references. Find them all:

```cpp
int& get_ref() {
    int x = 100;
    return x;
}

int main() {
    int& ref = get_ref();
    const int& cref = 42;
    int& bad_ref = cref; // Error?
    int& null_ref = *(int*)nullptr; // Dangerous?
    return 0;
}
```

Analyze line by line: which lines have compilation errors? Which lines are undefined behavior at runtime?

### Exercise 3: Implement a simple chain configurator

Design a class `ServerConfig`, containing `port` and `timeout` two `int` members. Provide `set_port` and `set_timeout` two methods, making them return `ServerConfig&` to support chaining:

```cpp
ServerConfig config;
config.set_port(8080).set_timeout(30);
```

## Summary

In this chapter, starting from the "pain points of pointers," we learned about the core C++ feature: references. A reference is an alias for an existing object; it must be initialized when declared and cannot be changed once bound. Compared to pointers, references have no null value, require no dereferencing syntax, and have an immutable binding relationship—these constraints make them the best choice for "passing objects that definitely exist."

When used as function parameters, references make code cleaner than the pointer version; when modified with `const`, it becomes the standard paradigm for "no copy, no modification" read-only parameter passing. Be extra careful when returning references; you must ensure the referenced object's lifetime is longer than the function call—local variables absolutely cannot have their references returned. Finally, `const` references can bind to temporary objects and extend their lifetime; this feature is common in actual code but is limited to const references.

The next chapter will touch on the basics of C++ dynamic memory management—although it's not yet time to talk about smart pointers, you can have an impression first: modern C++ thoroughly solves the "who is responsible for releasing memory" problem through RAII and smart pointers. Before that, make sure your foundation in references is solid, and the subsequent steps will be much easier.
