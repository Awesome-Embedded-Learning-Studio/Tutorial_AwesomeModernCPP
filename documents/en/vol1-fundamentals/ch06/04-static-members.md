---
chapter: 6
cpp_standard:
- 11
- 14
- 17
- 20
description: Master `static` member variables and functions, and understand class-level
  shared state and the first ideas behind the singleton pattern.
difficulty: beginner
order: 4
platform: host
prerequisites:
- Destructors and Resource Management
reading_time_minutes: 16
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: static Members
translation:
  source: documents/vol1-fundamentals/ch06/04-static-members.md
  source_hash: 6761408c8755fcfdc6b00353b74de2a1055de0ed0dce3b783f28cf248715824e
  translated_at: '2026-09-25T11:01:53+00:00'
  engine: anthropic
  token_count: 4200
---
# static Members: Belonging to the Class, Not to Any Object

Up to this point, every member variable and member function we have encountered has been bound to an "object": each time we create a `Sensor`, we get one more copy of `pin` and one more copy of `cached_value`, all independent of each other. In real-world engineering, however, there is a category of data and operations that naturally does not belong to any specific object—it belongs to the **entire class**. For example: how many `UARTPort` instances have actually been created in the system right now? Has the hardware abstraction layer been initialized yet? What is the default sampling frequency shared by all `Sensor` objects?

Look closely at these requirements and a common trait emerges: the data exists as a single copy shared by all objects; or the function relates only to the class's logic and does not depend on the state of any concrete instance. C++ answers this need with the `static` keyword: put it in front of a member declaration, and that member moves from the "object level" to the "class level".

In this chapter we will take static member variables and static member functions apart and make each of them clear, build an automatic ID allocator along the way, and finally see how `static` feeds into the singleton pattern.

## Static Member Variables—Shared Data That Belongs to the Class

Declaring a static member variable is simple: just add `static` in front of the type:

```cpp
class Employee {
private:
    int id_;
    std::string name_;
    static int next_id_;  // Declaration: a counter shared by all Employee objects
};
```

`next_id_` has exactly one copy in memory. Whether we create a hundred `Employee` objects or zero, `next_id_` exists (strictly speaking, it lives from program start to program end). Each `Employee` object has its own `id_` and `name_`, but the `next_id_` that all objects see is the very same one.

Here we run into a classic pitfall: **static member variables must be defined outside the class**. The `static int next_id_;` inside the class is only a declaration—it tells the compiler "something like this exists" without actually allocating any memory. The real definition has to be written outside the class:

```cpp
// Employee.cpp
int Employee::next_id_ = 1;  // Define and initialize
```

If we only declare it but never define it, compilation still passes, because while processing the class definition the compiler only sees the declaration. But at the linking stage, the linker discovers that no object file contains the actual storage for `Employee::next_id_`, and it throws an `undefined reference` error. This kind of "compiles fine, blows up at link time" problem is a notorious blood-pressure booster, because we have to hunt back and forth across multiple files to find which static member we forgot to define.

Before C++17, non-`const` integral static member variables had to be defined outside the class. If we declare `static int count_;` in a header but forget to write `int MyClass::count_ = 0;` in the matching `.cpp` file, every translation unit that includes that header compiles just fine—then the final link explodes. Worse, the wording of the error message is usually abstract enough that a beginner has no idea what it is talking about.

C++17, however, eased this pain point: `inline static` allows defining a static member directly inside the class:

```cpp
class Employee {
private:
    int id_;
    std::string name_;
    inline static int next_id_ = 1;  // C++17: defined in-class, no out-of-class definition needed
};
```

What `inline` means here is "allowed to be defined in a header without violating the ODR (One Definition Rule)"—the same keyword as the `inline` on inline functions, but with a different meaning. If your project can use C++17, we recommend going straight to `inline static` and saving yourself the chore of maintaining a pile of `Type Class::member = value;` lines in a `.cpp` file.

## Static Member Functions—Class Operations That Need No this

Static member functions, like static member variables, belong to the class itself. Their key characteristic is that they have **no `this` pointer**, because calling one does not require going through any concrete object. Having no `this` means they cannot access any non-static member—after all, the compiler has no way of knowing "which object's members we are operating on".

```cpp
class Employee {
private:
    int id_;
    std::string name_;
    static int next_id_;

public:
    Employee(const std::string& name)
        : id_(next_id_++), name_(name) {}

    /// @brief Get the next ID that will be assigned (static function)
    static int peek_next_id() {
        return next_id_;       // OK: accessing a static member
        // return id_;         // Compile error! A static function has no this, so it cannot access non-static members
    }
};
```

We call a static member function with the `ClassName::function_name()` syntax—no need to create an object first:

```cpp
std::cout << Employee::peek_next_id() << std::endl;  // No Employee instance needed
```

Calling a static function through an object (`emp.peek_next_id()`) is of course syntactically legal too, but that is just syntactic sugar—the compiler still translates it into `Employee::peek_next_id()`, and the object instance plays no part at runtime. Our advice is to prefer the `ClassName::function()` form: the semantics are clearer, and we can tell at a glance that it is a static function.

## In Practice: An Automatic ID Allocator

Let's assemble the pieces from above and write a complete `Employee` class that automatically assigns a unique ID on creation and keeps count of how many employee objects currently exist:

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

// Static member definitions
int Employee::next_id_ = 1;
int Employee::active_count_ = 0;
```

The design idea: `next_id_` is a counter that only ever grows—each construction increments it and takes the current value as that object's ID; `active_count_` goes up by one on construction and down by one on destruction, reflecting the number of currently alive objects in real time.

## Combining `static` and `const`

Things change again when we combine `static` with `const` (or `constexpr`). C++ allows `static constexpr` integral members to be initialized directly in the class, with no out-of-class definition:

```cpp
class Config {
public:
    static constexpr int kMaxRetries = 3;       // OK: a const integral, initialized in-class
    static constexpr double kPi = 3.14159265;   // Since C++11, floating-point types may also be initialized in-class
};
```

This style has been in wide use since C++11. `constexpr` implies `const`, and it requires the value to be determinable at compile time, so the compiler can simply inline the value at each use without allocating actual storage for it—unless we take its address (`&Config::kMaxRetries`), in which case the ODR-use rules require us to provide an out-of-class definition.

There is one easily confused piece of historical baggage here: in the C++03 era, only `static const int` (and other integral types such as `short`, `char`, and `long`) could be initialized in-class. If we wrote `static const double pi = 3.14;`, a C++03 compiler would reject it outright. Once C++11 introduced `constexpr`, this restriction essentially disappeared—the recommendation today is to use `static constexpr` uniformly: the semantics are clearer, and it avoids the pitfalls of the old standards.

If we need a static member whose initial value is only determined at runtime (say, read from a configuration file), then `constexpr` is off the table; the only option is an ordinary `static` member plus an initialization function that assigns the value.

## A First Sketch of the Singleton Pattern

Talking about `static` means talking about its relationship with the singleton pattern. The core requirement of the singleton pattern is: a class has exactly one instance in the entire program, and it provides a global access point. Its implementation cannot do without `static`: a static member function provides the access entry, and a static member variable holds that one and only instance.

We will look at only the most stripped-down sketch—a light touch, without unfolding the full implementation details:

```cpp
class SystemClock {
private:
    SystemClock() = default;  // Constructor is private: prevents external instantiation

    static SystemClock& instance() {
        static SystemClock clock;  // A local static; C++11 guarantees thread-safe initialization
        return clock;
    }

public:
    // Delete copy and assignment to guarantee uniqueness
    SystemClock(const SystemClock&) = delete;
    SystemClock& operator=(const SystemClock&) = delete;

    /// @brief Get the globally unique clock instance
    static SystemClock& get() { return instance(); }

    uint64_t now() const {
        // Return the current timestamp
        return 0;  // Simplified
    }
};

// Usage
uint64_t t = SystemClock::get().now();
```

This pattern is called Meyers' Singleton, and it relies on an important C++11 guarantee: a `static` local variable inside a function is initialized the first time execution reaches its declaration, and that initialization is thread-safe. We will not dive into the pros and cons of singletons here—just remember: `static` members plus a `private` constructor are the foundation of a singleton. We will expand on this properly when we reach design patterns.

## Hands-On Walkthrough—static_demo.cpp

Let's fold this chapter's ideas into one complete program:

```cpp
// static_demo.cpp
// A combined walkthrough of static members: automatic ID assignment, instance counting, static constants

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

/// @brief Create some temporary objects and watch the counters change
void demo_scope()
{
    std::cout << "\n--- Enter demo_scope ---" << std::endl;
    Employee temp1("Zhang San");
    Employee temp2("Li Si");
    std::cout << "Inside scope, active count: "
              << Employee::get_active_count() << std::endl;
    std::cout << "--- Leave demo_scope ---" << std::endl;
    // temp1, temp2 leave the scope and are destroyed
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

Let's verify: IDs start at 1 and increment without repetition; entering `demo_scope` raises `active_count` to 4, and leaving drops it back to 2; `next_id_` only ever grows, so after the scope it is 5 rather than 3—exactly the behavior we wanted.

Be careful when static members are involved in copy or move semantics. The default copy constructor copies member by member, but it does not copy static members—static members do not belong to the object. If the design expects "copying an object to replicate the entire class's state", then something is wrong with that design. The value of a static member is unaffected by the creation, copying, or destruction of any single object (unless we explicitly modify it in a constructor/destructor).

## Try It Yourself

### Exercise 1: Implement an ID Generator

Write a `UniqueIdGenerator` class that stores no object data at all and provides a globally incrementing ID purely through static members. For the interface, follow this sketch: `static int generate()` returns a new unique ID on each call, and `static void reset(int start)` allows resetting the starting value. Once you have written it, test it: call `generate()` three times and confirm it returns 1, 2, 3; then call `reset(100)` and call twice more, confirming it returns 100, 101.

::: details Reference Answer

```cpp
#include <iostream>

class UniqueIdGenerator
{
private:
    inline static int next_id_ = 1;

public:
    UniqueIdGenerator() = delete;

    static int generate()
    {
        return next_id_++;
    }

    static void reset(int start)
    {
        next_id_ = start;
    }
};

int main()
{
    std::cout << UniqueIdGenerator::generate() << '\n';
    std::cout << UniqueIdGenerator::generate() << '\n';
    std::cout << UniqueIdGenerator::generate() << '\n';

    UniqueIdGenerator::reset(100);

    std::cout << UniqueIdGenerator::generate() << '\n';
    std::cout << UniqueIdGenerator::generate() << '\n';

    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17 -Wall -Wextra main.cpp -o main && ./main
```

Output:

```text
1
2
3
100
101
```

:::

### Exercise 2: Instance Tracker

Write a `TrackedObject` class that maintains two counters at once—`active_count` (the number of currently alive objects) and `total_created` (the total number of objects ever created, monotonically increasing). Update both counters in the constructor and destructor, and provide two static functions to query them. To verify: create 5 objects, destroy 3 of them via a brace scope, then print the values of both counters—`active_count` should be 2, and `total_created` should be 5.

::: details Reference Answer

```cpp
#include <iostream>

class TrackedObject
{
private:
    // Number of currently alive objects
    inline static int active_count = 0;
    // Total number of objects ever created
    inline static int total_created = 0;

public:
    TrackedObject()
    {
        ++active_count;
        ++total_created;
    }

    ~TrackedObject()
    {
        --active_count;
    }

    static int get_active_count()
    {
        return active_count;
    }

    static int get_total_created()
    {
        return total_created;
    }
};

int main()
{
    TrackedObject object1;
    {
        TrackedObject object2;
        TrackedObject object3;
        TrackedObject object4;
    }
    TrackedObject object5;

    std::cout << "当前存活对象数: " << TrackedObject::get_active_count() << '\n'
              << "总共创建过的对象数: " << TrackedObject::get_total_created() << '\n';
}
```

Compile and run:

```bash
g++ -std=c++17 -Wall -Wextra main.cpp -o main && ./main
```

Output:

```text
当前存活对象数: 2
总共创建过的对象数: 5
```

:::
