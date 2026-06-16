---
chapter: 6
cpp_standard:
- 11
- 14
- 17
- 20
description: Master `static` member variables and functions, and understand class-level
  shared state and the initial concepts of the singleton pattern.
difficulty: beginner
order: 4
platform: host
prerequisites:
- 析构函数与资源管理
reading_time_minutes: 11
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: Static Member
translation:
  source: documents/vol1-fundamentals/ch06/04-static-members.md
  source_hash: 943fd1161a33105016393858eac03d822f992858cc641403bf4a073c0e4a7217
  translated_at: '2026-06-16T03:44:23.696093+00:00'
  engine: anthropic
  token_count: 2305
---
# Static Members

Until now, every member variable and member function we have encountered has been bound to an "object"—every time we create an object, we get another copy of the member variables, independent of each other. However, in real-world engineering, there is a class of data and operations that naturally do not belong to a specific object, but rather to the **entire class**. For example: How many instances of a specific class currently exist in the system? Has the Hardware Abstraction Layer (HAL) been initialized? What is the default sampling frequency shared by all peripherals?

If we look closely at these requirements, their common characteristic is: the data exists in only one copy, shared by all objects; or the function is related only to the class logic and does not depend on the state of any specific instance. C++ uses the `static` keyword to satisfy these needs—by adding it to a member declaration, that member moves from the "object level" to the "class level."

In this chapter, we will clarify static member variables and static member functions separately, implement an automatic ID allocator along the way, and finally take a quick look at how `static` paves the way for the Singleton pattern.

## Static Member Variables—Shared Data Belonging to the Class

Declaring a static member variable is simple; just add `static` before the type:

```cpp
class MyClass {
public:
    static int s_count; // Declaration
};
```

`s_count` has only one copy in memory. Whether you create one hundred or zero `MyClass` objects, `s_count` exists (strictly speaking, it exists from program start to finish). Each `MyClass` object has its own non-static members, but all objects see the same `s_count`.

Here is a classic pitfall: **static member variables must be defined outside the class**. The `s_count` inside the class is just a declaration, telling the compiler "this thing exists," but it does not actually allocate memory. The real definition must be written outside the class:

```cpp
// Definition (usually in the .cpp file)
int MyClass::s_count = 0;
```

If you only declare but do not define, the compilation will pass—because the compiler only sees the declaration when processing the class definition. But when it gets to the linking stage, the linker finds that no object file contains the actual storage location for `s_count` and will throw a linker error. This "compiles OK, link fails" problem often drives people crazy, because you have to search across multiple files to figure out which static member you forgot to define.

> **Pitfall Warning**: Before C++17, non-`const` integral static member variables had to be defined outside the class. If you declared it in a header file but forgot to write the definition in the corresponding `.cpp` file, every translation unit including that header would compile, but the final link would crash. Furthermore, the error messages are often abstract, and beginners have no idea what they are talking about.

However, in C++17, this pain point was alleviated—`inline` allows static members to be defined directly inside the class:

```cpp
class MyClass {
public:
    static inline int s_count = 0; // C++17 inline variable
};
```

`inline` here means "allowed to be defined in a header file without violating the ODR (One Definition Rule)," and it is the same keyword as for inline functions, but with a different meaning. If your project can use C++17, it is recommended to use `inline` directly, saving the trouble of maintaining a pile of definitions in `.cpp` files.

## Static Member Functions—Class Operations Without `this`

Static member functions, like static member variables, belong to the class itself. Their key characteristic is **no `this` pointer**—because calling them does not require a specific object. No `this` means they cannot access any non-static members, as the compiler doesn't know "which object's members you are operating on."

```cpp
class MyClass {
public:
    static void func() {
        // No 'this' pointer here
        s_count = 0; // OK: accessing static member
        // x = 0;     // Error: 'x' is non-static
    }
private:
    static int s_count;
    int x; // Non-static member
};
```

Call a static member function using the `ClassName::functionName` syntax, no need to create an object first:

```cpp
MyClass::func();
```

Of course, calling a static function through an object (`obj.func()`) is also syntactically legal, but this is just syntactic sugar—the compiler will still translate it to `MyClass::func()`, and the object instance does not participate at runtime. The author suggests trying to use the `ClassName::` method for calling, as the semantics are clearer, and readers can see at a glance that this is a static function.

## In Practice: Automatic ID Allocator

Putting the pieces together, let's write a complete `Employee` class that automatically assigns a unique ID upon creation and counts how many employee objects currently exist:

```cpp
class Employee {
public:
    Employee() : m_id(next_id++) { ++active_count; }
    ~Employee() { --active_count; }

    int get_id() const { return m_id; }
    static int get_active_count() { return active_count; }

private:
    int m_id;
    static int next_id;      // Monotonically increasing ID generator
    static int active_count; // Current number of surviving objects
};

// Definition of static members
int Employee::next_id = 1;
int Employee::active_count = 0;
```

The design idea here is: `next_id` is a monotonically increasing counter; every time an object is constructed, it increments and takes the current value as that object's ID; `active_count` increments on construction and decrements on destruction, reflecting in real-time the number of currently surviving objects.

## Combination of `static` and `const`

When `static` and `const` (or `constexpr`) are combined, the situation is different. C++ allows `const` integral members to be initialized directly inside the class without an out-of-class definition:

```cpp
class Config {
public:
    static const int MAX_ITEMS = 100;
};
```

This usage has been widespread since C++11. `const` implicitly implies `inline` for this purpose, and requires the value to be determinable at compile time, so the compiler can inline the value directly where it is used, without needing to allocate actual storage space for it—unless you take its address (`&Config::MAX_ITEMS`), in which case ODR-use rules require you to provide an out-of-class definition.

However, there is a confusing legacy issue here: in the C++03 era, only `const` integers (and `bool`, `char`, etc.) could be initialized in-class. If you wrote `static const double`, a C++03 compiler would error directly. After C++11 introduced `constexpr`, this restriction basically disappeared—now it is recommended to uniformly use `constexpr`, as the semantics are clearer and you won't hit the pitfalls of old standards.

If you need a static member whose initial value is determined at runtime (e.g., read from a configuration file), you cannot use `constexpr`; you must use a normal `static` member plus an initialization function to assign the value.

## Prototype of the Singleton Pattern

Mentioning `static`, we cannot avoid its relationship with the Singleton Pattern. The core requirement of the Singleton pattern is: a class has only one instance in the entire program and provides a global access point. Its implementation cannot be separated from `static`—using a static member function to provide the access entry, and a static member variable to hold that unique instance.

Let's just look at a simplified prototype, touching on it without expanding into full implementation details:

```cpp
class Singleton {
public:
    static Singleton& getInstance() {
        static Singleton instance; // Initialized on first call
        return instance;
    }
    // Delete copy and move operations
    Singleton(const Singleton&) = delete;
    Singleton& operator=(const Singleton&) = delete;

private:
    Singleton() = default;  // Private constructor
    ~Singleton() = default;
};
```

This pattern is called Meyers' Singleton. It utilizes an important guarantee of C++11: `static` local variables inside a function are initialized when the declaration is first executed, and the initialization is thread-safe. We won't discuss the pros and cons of Singletons deeply here—just remember: `static` member + private constructor is the cornerstone of the Singleton. We will expand on this formally when we cover design patterns.

## Live Combat—static_demo.cpp

Let's integrate the knowledge points of this chapter into a complete program:

```cpp
#include <iostream>

class Employee {
public:
    Employee() : m_id(next_id++) {
        ++active_count;
        std::cout << "Employee " << m_id << " created. Active: " << active_count << "\n";
    }

    ~Employee() {
        --active_count;
        std::cout << "Employee " << m_id << " destroyed. Active: " << active_count << "\n";
    }

    int get_id() const { return m_id; }
    static int get_active_count() { return active_count; }
    static int get_next_id() { return next_id; }

private:
    int m_id;
    static int next_id;
    static int active_count;
};

// Definitions
int Employee::next_id = 1;
int Employee::active_count = 0;

int main() {
    std::cout << "Initial: next_id=" << Employee::get_next_id()
              << ", active=" << Employee::get_active_count() << "\n";

    Employee e1, e2;
    {
        Employee e3, e4;
        std::cout << "Inside scope: active=" << Employee::get_active_count() << "\n";
    } // e3, e4 destroyed here

    std::cout << "Outside scope: active=" << Employee::get_active_count() << "\n";
    std::cout << "Final next_id: " << Employee::get_next_id() << "\n";

    return 0;
}
```

Compile and run: `g++ -std=c++17 static_demo.cpp -o static_demo && ./static_demo`

Expected output:

```text
Initial: next_id=1, active=0
Employee 1 created. Active: 1
Employee 2 created. Active: 2
Employee 3 created. Active: 3
Employee 4 created. Active: 4
Inside scope: active=4
Employee 4 destroyed. Active: 3
Employee 3 destroyed. Active: 2
Outside scope: active=2
Final next_id: 5
```

Verify: IDs start at 1 and increment without repetition; entering the scope `active_count` rises to 4, drops to 2 after exiting; `next_id` only increases, ending at 5 instead of 3—this is exactly the behavior we wanted.

> **Pitfall Warning**: If your static members involve copy or move semantics, be very careful. The default copy constructor copies member-by-member, but it will not copy static members—because static members do not belong to the object. If you expect to "copy the entire class's state by copying an object," the design is flawed. The value of a static member is unaffected by the creation, copying, or destruction of any single object (unless you explicitly modify it in the constructor/destructor).

## Try It Yourself

### Exercise 1: Implement an ID Generator

Write an `IdGenerator` class that stores no object data, only provides a globally incrementing ID through static members. Interface design reference: `next()` returns a new unique ID each time it is called, `reset(val)` allows resetting the starting value. After writing, test: call `next()` three times, confirm it returns 1, 2, 3; then `reset(100)`, call twice more, confirm it returns 100, 101.

### Exercise 2: Instance Tracker

Write an `InstanceTracker` class that maintains two counters—`active` (current number of surviving objects) and `total` (total number of objects created, monotonically increasing). Update these two counters in the constructor and destructor, and provide two static functions to query them. Verification method: create 5 objects, destroy 3 of them using a brace scope, print the values of the two counters—`active` should be 2, `total` should be 5.

## Summary

`static` members elevate data and functions from the object level to the class level. Static member variables have only one copy in memory, shared by all objects, and must be defined outside the class (except for C++17's `inline`); static member functions have no `this` pointer, can only access static members, and are called using `ClassName::` syntax. `static constexpr` provides an elegant way to write compile-time constants, and `static` member + private constructor is the cornerstone of the Singleton pattern.

In the next chapter, we will look at `friend`—the mechanism provided by C++ to "selectively break encapsulation."
