---
chapter: 6
cpp_standard:
- 11
- 14
- 17
- 20
description: Master `static` variables and functions in classes, and understand class-level
  shared state and the foundational concepts of the singleton pattern.
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
title: Static Members
translation:
  engine: anthropic
  source: documents/vol1-fundamentals/ch06/04-static-members.md
  source_hash: 8013ec2441fed211c39e9c07f16c8edaa5612cd665e7a3706e5f533761378002
  token_count: 2309
  translated_at: '2026-05-26T10:51:23.323464+00:00'
---
# static Members

So far, every member variable and member function we have encountered is bound to an "object" — each time we create a `Sensor`, we get another `pin` and another `cached_value`, each independent and isolated. In real-world engineering, however, there is a category of data and operations that inherently do not belong to any specific object, but rather to the **entire class**. For example: how many `UARTPort` instances have been created in the current system? Has the hardware abstraction layer been initialized? What is the default sampling frequency shared by all `Sensor` objects?

If we look closely at these requirements, their common trait is clear: the data exists in only one copy, shared by all objects; or the function relates only to class logic and does not depend on the state of any specific instance. C++ uses the `static` keyword to address such needs — by placing it before a member declaration, that member is elevated from the "object level" to the "class level."

In this chapter, we will break down static member variables and static member functions separately, implement an automatic ID allocator along the way, and take a quick look at how `static` paves the way for the Singleton pattern.

## Static Member Variables — Shared Data Belonging to the Class

Declaring a static member variable is simple: just add `static` before the type:

```cpp
class Employee {
private:
    int id_;
    std::string name_;
    static int next_id_;  // 声明：所有 Employee 共享的计数器
};
```

`next_id_` has only one copy in memory. Whether you create a hundred `Employee` objects or zero, `next_id_` exists (strictly speaking, it exists from program startup until termination). Each `Employee` object has its own `id_` and `name_`, but all objects see the same `next_id_`.

Here is a classic pitfall: **a static member variable must be defined outside the class**. The `static int next_id_;` inside the class is merely a declaration, telling the compiler "this thing exists," but it does not actually allocate memory. The real definition must be written outside the class:

```cpp
// Employee.cpp
int Employee::next_id_ = 1;  // 定义并初始化
```

If you only declare but do not define it, compilation will succeed — because the compiler only sees the declaration when processing the class definition. But at the linking stage, the linker will find no actual storage location for `Employee::next_id_` in any object file, and it will throw an `undefined reference` error. This "compiles fine, linker errors out" problem is notoriously frustrating, because you have to hunt across multiple files to figure out which static member you forgot to define.

> **Pitfall Warning**: Before C++17, non-`const` integral static member variables had to be defined outside the class. If you declared `static int count_;` in a header but forgot to write `int MyClass::count_ = 0;` in the corresponding `.cpp` file, every translation unit including that header would compile fine, but linking would blow up. Worse, the error messages are often so abstract that newcomers have no idea what they mean.

In C++17, however, this pain point was alleviated — `inline static` allows static members to be defined directly inside the class:

```cpp
class Employee {
private:
    int id_;
    std::string name_;
    inline static int next_id_ = 1;  // C++17：类内定义，不需要类外定义
};
```

The semantics of `inline` here are "allowed to be defined in a header without violating the one definition rule (ODR)." It is the same keyword as the `inline` used for inline functions, but with a different meaning. If your project can use C++17, we recommend using `inline static` directly, saving you the hassle of maintaining a bunch of `Type Class::member = value;` lines in `.cpp` files.

## Static Member Functions — Class Operations Without this

Like static member variables, static member functions belong to the class itself. Their key characteristic is that **there is no `this` pointer** — because calling them does not require a specific object. No `this` means they cannot access any non-static members, since the compiler has no idea "which object's members you want to operate on."

```cpp
class Employee {
private:
    int id_;
    std::string name_;
    static int next_id_;

public:
    Employee(const std::string& name)
        : id_(next_id_++), name_(name) {}

    /// @brief 获取下一个将被分配的 ID（静态函数）
    static int peek_next_id() {
        return next_id_;       // OK：访问静态成员
        // return id_;         // 编译错误！静态函数没有 this，无法访问非静态成员
    }
};
```

We call a static member function using the `类名::函数名()` syntax, without needing to create an object first:

```cpp
std::cout << Employee::peek_next_id() << std::endl;  // 不需要任何 Employee 实例
```

Of course, calling a static function through an object is also syntactically valid (`emp.peek_next_id()`), but this is just syntactic sugar — the compiler still translates it into `Employee::peek_next_id()`, and the object instance does not participate at runtime. Our recommendation is to always use the `ClassName::function()` style for calls, as the semantics are clearer and readers can tell at a glance that this is a static function.

## Hands-on: Automatic ID Allocator

Putting the pieces together, we write a complete version of the `Employee` class, which automatically allocates a unique ID upon creation and keeps track of how many employee objects currently exist:

```cpp
class Employee {
private:
    int id_;
    std::string name_;
    static int next_id_;
    static int active_count_;

public:
    explicit Employee(const std::string& name)
        : id_(next_id_++), name_(name)
    {
        ++active_count_;
    }

    ~Employee() { --active_count_; }

    int id() const { return id_; }
    const std::string& name() const { return name_; }

    static int get_active_count() { return active_count_; }
    static int peek_next_id() { return next_id_; }
};

// 静态成员定义
int Employee::next_id_ = 1;
int Employee::active_count_ = 0;
```

The design idea here is: `next_id_` is a monotonically increasing counter; each time an object is constructed, it increments and takes the current value as that object's ID. `active_count_` increments on construction and decrements on destruction, reflecting the number of currently alive objects in real time.

## Combining static with const

When `static` and `const` (or `constexpr`) are combined, the situation is a bit different. C++ allows `static constexpr` integral members to be initialized directly inside the class, without an out-of-class definition:

```cpp
class Config {
public:
    static constexpr int kMaxRetries = 3;       // OK：const 整型，类内初始化
    static constexpr double kPi = 3.14159265;   // C++11 起也允许浮点类型类内初始化
};
```

This syntax has been widely used since C++11. `constexpr` implicitly implies `const`, and it requires the value to be determinable at compile time, so the compiler can inline the value directly at the point of use without allocating actual storage for it — unless you take its address (`&Config::kMaxRetries`), in which case ODR-use rules require you to provide an out-of-class definition.

There is, however, a historically confusing legacy issue: in the C++03 era, only `static const int` (and other integral types like `short`, `char`, and `long`) could be initialized inside the class. If you wrote `static const double pi = 3.14;`, a C++03 compiler would error out directly. After C++11 introduced `constexpr`, this restriction essentially disappeared — we now recommend uniformly using `static constexpr` for clearer semantics and to avoid pitfalls from older standards.

If you need a static member whose initial value cannot be determined until runtime (for example, reading from a configuration file), you cannot use `constexpr`. You must use a regular `static` member plus an initialization function to assign the value.

## The Prototype of the Singleton Pattern

When mentioning `static`, we cannot avoid discussing its relationship with the Singleton Pattern. The core requirement of the Singleton pattern is: a class has only one instance throughout the entire program, and it provides a global access point. Its implementation relies on `static` — using a static member function to provide the access entry point, and a static member variable to hold that single instance.

We will only look at a minimal prototype here, just to illustrate the idea without diving into full implementation details:

```cpp
class SystemClock {
private:
    SystemClock() = default;  // 构造函数 private：阻止外部创建实例

    static SystemClock& instance() {
        static SystemClock clock;  // C++11 保证线程安全的局部静态变量
        return clock;
    }

public:
    // 删除拷贝和赋值，确保唯一性
    SystemClock(const SystemClock&) = delete;
    SystemClock& operator=(const SystemClock&) = delete;

    /// @brief 获取全局唯一的时钟实例
    static SystemClock& get() { return instance(); }

    uint64_t now() const {
        // 返回当前时间戳
        return 0;  // 简化
    }
};

// 使用
uint64_t t = SystemClock::get().now();
```

This pattern is called Meyers' Singleton, leveraging an important C++11 guarantee: a `static` local variable inside a function is initialized the first time execution reaches its declaration, and this initialization is thread-safe. We will not delve into the pros and cons of singletons here — just remember that `static` members + a `private` constructor are the cornerstones of the Singleton. We will formally expand on this when we cover design patterns later.

## Hands-on Practice — static_demo.cpp

Let us integrate the concepts from this chapter into a complete program:

```cpp
// static_demo.cpp
// static 成员综合演练：自动 ID 分配、实例计数、静态常量

#include <iostream>
#include <string>

class Employee {
private:
    int id_;
    std::string name_;
    static int next_id_;
    static int active_count_;

public:
    static constexpr int kMaxNameLength = 50;

    explicit Employee(const std::string& name)
        : id_(next_id_++), name_(name)
    {
        ++active_count_;
        std::cout << "[construct] Employee #" << id_
                  << " \"" << name_ << "\" created. "
                  << "Active: " << active_count_ << std::endl;
    }

    ~Employee()
    {
        --active_count_;
        std::cout << "[destruct]  Employee #" << id_
                  << " \"" << name_ << "\" destroyed. "
                  << "Active: " << active_count_ << std::endl;
    }

    int id() const { return id_; }
    const std::string& name() const { return name_; }

    static int get_active_count() { return active_count_; }
    static int peek_next_id() { return next_id_; }
};

int Employee::next_id_ = 1;
int Employee::active_count_ = 0;

/// @brief 创建一些临时对象，观察计数变化
void demo_scope()
{
    std::cout << "\n--- Enter demo_scope ---" << std::endl;
    Employee temp1("Zhang San");
    Employee temp2("Li Si");
    std::cout << "Inside scope, active count: "
              << Employee::get_active_count() << std::endl;
    std::cout << "--- Leave demo_scope ---" << std::endl;
    // temp1, temp2 离开作用域，析构
}

int main()
{
    std::cout << "=== Static Member Demo ===" << std::endl;
    std::cout << "Max name length: " << Employee::kMaxNameLength << std::endl;
    std::cout << "Next ID before any creation: "
              << Employee::peek_next_id() << std::endl;

    Employee emp1("Wang Wu");
    Employee emp2("Zhao Liu");

    std::cout << "\nCurrent active count: "
              << Employee::get_active_count() << std::endl;
    std::cout << "Next ID to be assigned: "
              << Employee::peek_next_id() << std::endl;

    demo_scope();

    std::cout << "\nAfter demo_scope, active count: "
              << Employee::get_active_count() << std::endl;
    std::cout << "Next ID to be assigned: "
              << Employee::peek_next_id() << std::endl;

    return 0;
}
```

Compile and run: `g++ -std=c++17 -Wall -Wextra -o static_demo static_demo.cpp && ./static_demo`

Expected output:

```text
=== Static Member Demo ===
Max name length: 50
Next ID before any creation: 1
[construct] Employee #1 "Wang Wu" created. Active: 1
[construct] Employee #2 "Zhao Liu" created. Active: 2

Current active count: 2
Next ID to be assigned: 3

--- Enter demo_scope ---
[construct] Employee #3 "Zhang San" created. Active: 3
[construct] Employee #4 "Li Si" created. Active: 4
Inside scope, active count: 4
--- Leave demo_scope ---
[destruct]  Employee #4 "Li Si" destroyed. Active: 3
[destruct]  Employee #3 "Zhang San" destroyed. Active: 2

After demo_scope, active count: 2
Next ID to be assigned: 5
[destruct]  Employee #2 "Zhao Liu" destroyed. Active: 1
[destruct]  Employee #1 "Wang Wu" destroyed. Active: 0
```

Let us verify: IDs increment starting from one without duplicates; when entering `demo_scope`, `active_count` increases to four, and after exiting it drops to two; `next_id_` only increases and never decreases, so after exiting it is five instead of three — exactly the behavior we want.

> **Pitfall Warning**: If your static members involve copy or move semantics, be careful. The default copy constructor copies members one by one, but it does not copy static members — because static members do not belong to the object. If you expect to "copy the entire class's state by copying an object," then there is a design flaw. The value of a static member is not affected by the creation, copying, or destruction of any single object (unless you explicitly modify it in the constructor or destructor).

## Try It Yourself

### Exercise 1: Implement an ID Generator

Write a `UniqueIdGenerator` class that stores no object data, but provides a globally incrementing ID through static members. Reference interface design: `static int generate()` returns a new unique ID each time it is called, and `static void reset(int start)` allows resetting the starting value. After writing it, test it: call `generate()` three times and confirm it returns one, two, three; then call `reset(100)`, call it twice more, and confirm it returns 100, 101.

### Exercise 2: Instance Tracker

Write a `TrackedObject` class that maintains two counters simultaneously — `active_count` (the number of currently alive objects) and `total_created` (the total number of objects ever created, monotonically increasing). Update both counters in the constructor and destructor, and provide two static functions to query them. Verification method: create five objects, destroy three of them using a brace-delimited scope, and print the values of both counters — `active_count` should be two, and `total_created` should be five.

## Summary

`static` members elevate data and functions from the object level to the class level. Static member variables have only one copy in memory, shared by all objects, and must be defined outside the class (except with C++17's `inline static`); static member functions have no `this` pointer, can only access static members, and are called using the `ClassName::function()` syntax. `static constexpr` provides an elegant way to write compile-time constants, and `static` + a `private` constructor are the cornerstones of the Singleton pattern.

In the next chapter, we will look at `friend` — C++'s mechanism for "selectively breaking encapsulation."
