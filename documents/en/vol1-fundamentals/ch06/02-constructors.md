---
title: Constructor
description: Master the complete usage of default constructors, parameterized constructors,
  copy constructors, initializer lists, and delegating constructors.
chapter: 6
order: 2
difficulty: beginner
reading_time_minutes: 15
platform: host
prerequisites:
- 类的定义
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
cpp_standard:
- 11
- 14
- 17
- 20
translation:
  source: documents/vol1-fundamentals/ch06/02-constructors.md
  source_hash: 1ad8bd59a228c1844fabd052c252a1acf11f2050fd269dc703b6090beea3994e
  translated_at: '2026-05-26T10:50:53.314174+00:00'
  engine: anthropic
  token_count: 2206
---
# Constructors

In the previous chapter, we learned how to define a class—writing member variables, writing member functions, and using `public` and `private` to control access. But we've been sidestepping one question: when an object is created, what is inside its member variables? The answer is—if you do nothing, the member variables of a local object hold **garbage values!** They are random leftover data from the previous use of that memory.

Once an object is created, it should be in a **valid, usable, and predictable** state. The constructor is C++'s solution: it executes automatically when the object is created, bringing member variables to the correct initial state. As long as the constructor is written correctly, the rookie mistake of "forgetting to initialize" simply cannot happen.

In this chapter, we will break down every form of the constructor—default constructors, parameterized constructors, copy constructors, member initializer lists, and the delegating constructors introduced in C++11. Each one has its own use cases and hidden pitfalls.

## Default Construction — Creating Objects Without Arguments

A default constructor takes no arguments. When you write `Point p;`, this is what gets called.

```cpp
class Point {
public:
    Point() : x(0), y(0) {}  // 默认构造函数
private:
    int x, y;
};
```

The `: x(0), y(0)` after the parameter list is the member initializer list. Let's just get familiar with its face for now; we'll cover it in detail later. The key takeaway here is the responsibility of the default constructor: the moment the object comes into existence, it is already a valid origin coordinate.

If you don't write any constructors at all, the compiler will generate a default constructor for you. However, it does not initialize fundamental types like `int` or `double` at all—their values remain garbage. Therefore, when a class contains fundamental type members, you almost always need to write your own default constructor.

> **Pitfall Warning**: There is only one rule for compiler-generated default constructors—as soon as you write **any** constructor (even one with parameters), the compiler stops generating a default constructor for you. Many people write a `Point(int x, int y)` and then find that `Point p;` fails to compile, leaving them completely baffled. The reason is right here: you wrote a parameterized constructor, so the compiler assumes, "Since you're managing initialization yourself, you need to write the default constructor yourself, too."

The fix is simple—either write a `Point()` yourself, or use C++11's `= default` syntax to tell the compiler to keep generating it for you:

```cpp
class Point {
public:
    Point() = default;              // 显式要求编译器生成默认构造函数
    Point(int x, int y) : x(x), y(y) {}
private:
    int x, y;
};
```

Note that a default constructor generated with `= default` still will not zero-initialize fundamental types. If you need zero-initialization, you still have to write `Point() : x(0), y(0) {}` yourself, or use in-class initializers (which we'll cover in the next chapter).

## Parameterized Construction — Giving the Caller Control Over Initialization

Often, we want an object to come into existence with specific data, rather than a "zero-value" default state. A parameterized constructor accepts arguments to initialize member variables.

```cpp
class Point {
public:
    Point(int x, int y) : x(x), y(y) {}
    // ...
};
```

Constructors support overloading, so you can provide both a default constructor and a parameterized constructor, letting the caller choose as needed. However, we now need to talk about an easily overlooked keyword—`explicit`. When a constructor takes only one argument (or when all remaining arguments have default values), it acts as an implicit type conversion function. Look at this code:

```cpp
class Point {
public:
    Point(int x) : x(x), y(0) {}  // 没有写 explicit！
    int x, y;
};

void printPoint(Point p) { /* ... */ }

printPoint(42);  // 编译通过！42 隐式转换为 Point(42)
```

For the `printPoint(42)` call, the function signature expects a `Point`, but you passed an `int`. The compiler helpfully called the constructor to perform an implicit conversion. In a short example, this looks harmless, but in a large project, such implicit conversions create hard-to-track bugs—you might have simply written the wrong parameter type, and instead of reporting an error, the compiler "tries to help" and ends up doing more harm than good.

The `explicit` keyword exists to prohibit this kind of implicit conversion:

```cpp
class Point {
public:
    explicit Point(int x) : x(x), y(0) {}  // 禁止隐式转换
    int x, y;
};

printPoint(42);       // 编译错误！必须写 printPoint(Point(42))
printPoint(Point(42)); // OK
```

My recommendation is: **all single-argument constructors should have `explicit`**, unless you have a very clear reason to need implicit conversion. It is a nearly zero-cost defensive measure.

## Member Initializer Lists — The Proper Battleground for Initialization

We've been using member initializer lists all along; now let's formally break them down.

A constructor's initializer list is placed after the colon following the parameter list, separated by commas, with each member followed by its initial value in parentheses (or curly braces):

```cpp
class Sensor {
public:
    Sensor(int id, const char* name, bool active)
        : id(id), name(name), active(active) {}
private:
    int id;
    const char* name;
    bool active;
};
```

You might be wondering: can't I just assign values inside the constructor body? Why bother with a dedicated initializer list?

```cpp
// 不推荐：在函数体内赋值
Sensor(int id, const char* name, bool active) {
    this->id = id;
    this->name = name;
    this->active = active;
}
```

For fundamental types like `int` and `bool`, both approaches produce the exact same result. But the problem arises with `const` members and reference members—these two things **can only** be initialized, not assigned. By the time the constructor body begins executing, all members have already been default-constructed. Trying to assign values then is too late for `const` members and references—the compiler will throw an error directly.

```cpp
class Bad {
public:
    Bad(int val) {
        c = val;      // 错误！const 成员不能赋值
        r = val;      // 错误！引用不能重新绑定
    }
private:
    const int c;
    int& r;
};
```

Even without `const` and reference members, the initializer list is still superior. For class-type members (like `std::string`), assigning inside the function body means default-constructing first and then assigning over it—a two-step operation. The initializer list constructs directly with the target value, getting it right in one step.

> **Pitfall Warning**: The initialization order of members is determined by their **declaration order** in the class definition, and has **nothing to do with** the order they are written in the initializer list. This is extremely important—if your initializer list says `y(x), x(10)`, but `x` is declared first and `y` second in the class, the actual execution order is to first initialize `x` to 10, then initialize `y` to `x` (at which point `x` is already 10), yielding the correct result. But if the declaration order is reversed—with `y` before `x`—then when `y(x)` executes, `x` hasn't been initialized yet, so you read a garbage value. Most compilers will issue a warning when the two orders are inconsistent, but it's best to develop the habit of keeping the declaration order and the initializer list order consistent, so you don't plant landmines for yourself.

## Copy Construction — Creating New Objects From Existing Ones

A copy constructor creates a new object from an existing object of the same type, with a fixed signature of `T(const T& other)`:

```cpp
class Point {
public:
    Point(const Point& other) : x(other.x), y(other.y) {}
    // ...
private:
    int x, y;
};
```

The copy constructor is invoked in three scenarios: copy initialization (`Point p2 = p1;`), passing arguments by value (the formal parameter is created via copy construction), and returning by value (the return value is copied via copy construction, though modern compilers usually use RVO to eliminate this copy).

If you don't write a copy constructor yourself, the compiler generates a default version—whose behavior is **memberwise copy**, meaning it calls the copy constructor for each member individually (for fundamental types, it just copies the value directly). For a class like `Point` that only contains fundamental types, the default version is perfectly adequate.

> **Pitfall Warning**: Memberwise copy is disastrous for classes that contain **raw pointers**. Suppose your class has an `int* data` pointing to dynamically allocated memory. The default copy constructor will only copy the pointer's value (the address), not the content the pointer points to. The result is that two objects' `data` pointers point to the same block of memory—when one is destroyed and frees the memory, the other is still using it, becoming a dangling pointer. This is the classic "shallow copy" problem. We'll dive into how to solve it when we cover RAII and smart pointers later.

```cpp
class Buffer {
public:
    Buffer(size_t size) : size(size), data(new int[size]) {}
    // 危险：默认拷贝构造函数会导致浅拷贝！
    // 两个 Buffer 的 data 指针会指向同一块内存
    ~Buffer() { delete[] data; }
private:
    size_t size;
    int* data;
};
```

For now, just remember one thing: if your class manages a resource (dynamic memory, file handles, network connections, etc.), you must write your own copy constructor (or simply disable it—we'll cover how to do that later).

## Delegating Constructors — Letting Constructors Help Each Other

C++11 introduced delegating constructors, which allow one constructor to call **another constructor of the same class** in its initializer list, reducing code duplication.

```cpp
class Point {
public:
    Point() : Point(0, 0) {              // 委托给参数化构造函数
        std::cout << "委托构造\n";
    }
    Point(int x, int y) : x(x), y(y) {   // "主"构造函数
        std::cout << "参数化构造\n";
    }
private:
    int x, y;
};
```

In the initializer list of `Point()`, we don't write a member name, but rather `Point(0, 0)`—calling another constructor. The execution order is: first, the target constructor's initializer list and body execute, then control returns to the delegating constructor's body.

This feature is especially useful when a class has many constructors with overlapping initialization logic—put the core logic in one "primary" constructor, and have the others delegate to it.

However, delegating constructors have one hard rule: **once a delegation appears in the initializer list, you cannot initialize any members**. Writing something like `Point() : Point(0, 0), x(42) {}` is illegal—you must either delegate entirely or initialize entirely yourself; you cannot mix the two.

## Hands-On Practice — constructors.cpp

Let's integrate all the constructor types covered in this chapter into a single `Point` class, and mark every constructor call with an output statement:

```cpp
#include <iostream>

class Point {
public:
    // 默认构造函数
    Point() : Point(0, 0) {
        std::cout << "委托构造\n";
    }

    // 参数化构造函数
    Point(int x, int y) : x(x), y(y) {
        std::cout << "参数化构造\n";
    }

    // 拷贝构造函数
    Point(const Point& other) : x(other.x), y(other.y) {
        std::cout << "拷贝构造\n";
    }

    void print() const {
        std::cout << "(" << x << ", " << y << ")\n";
    }

private:
    int x, y;
};

void byValue(Point p) {
    p.print();
}

Point byReturnValue() {
    Point p(3, 4);
    return p;
}

int main() {
    std::cout << "--- 默认构造 ---\n";
    Point p1;
    p1.print();

    std::cout << "\n--- 参数化构造 ---\n";
    Point p2(1, 2);
    p2.print();

    std::cout << "\n--- 拷贝构造 ---\n";
    Point p3 = p2;       // 拷贝初始化
    p3.print();

    std::cout << "\n--- 按值传参 ---\n";
    byValue(p2);         // 实参到形参的拷贝

    std::cout << "\n--- 按值返回 ---\n";
    Point p4 = byReturnValue();  // 可能被 RVO 优化掉
    p4.print();

    return 0;
}
```

Compile and run: `g++ -std=c++17 -o constructors constructors.cpp && ./constructors`

Expected output:

```text
--- 默认构造 ---
参数化构造
委托构造
(0, 0)

--- 参数化构造 ---
参数化构造
(1, 2)

--- 拷贝构造 ---
拷贝构造
(1, 2)

--- 按值传参 ---
拷贝构造
(1, 2)

--- 按值返回 ---
参数化构造
(3, 4)
```

Let's verify: the delegating constructor `Point()` first calls `Point(0, 0)` (outputting "参数化构造" first), then executes its own body (outputting "委托构造"). The copy constructor is correctly triggered in both scenarios.

## Try It Yourself

### Exercise 1: Implement a Date Class

Write a `Date` class containing three members: `year`, `month`, and `day`. Provide a default constructor (initializing to 2000/1/1), a parameterized constructor (accepting year, month, and day, with basic validity checks—month 1-12, day 1-31), and a `print()` method. Verification: construct several date objects, including one with an invalid date (such as month 13), and observe whether the validation logic takes effect.

### Exercise 2: Implement a Vector3D Class

Write a `Vector3D` class containing three `double` members: `x`, `y`, and `z`. Use a delegating constructor so that the default constructor delegates to the parameterized constructor, then implement a copy constructor and a `magnitude()` method that returns the vector's magnitude. Verification: create a default vector, a custom vector, and a copied vector, and print their values and magnitudes.

## Summary

Constructors are the starting point of an object's lifecycle, ensuring that an object is in a valid state the moment it is born. Default constructors are used for creating objects without arguments, but note—once you write any constructor, a default constructor is no longer automatically generated. Parameterized constructors initialize objects with specific data, and `explicit` prevents implicit conversions by single-argument constructors. The member initializer list is the proper way to initialize; it is the only option for `const` and reference members, and the initialization order follows the declaration order, not the written order. Copy constructors create new objects from existing ones, performing memberwise copy by default—which is a hidden bomb for classes containing pointers. C++11's delegating constructors allow constructors to reuse each other, reducing code duplication.

In the next chapter, we will cover destructors—constructors bring objects into the world, and destructors are responsible for safely sending them off. Together, the two form the core philosophy of C++ resource management: RAII.
