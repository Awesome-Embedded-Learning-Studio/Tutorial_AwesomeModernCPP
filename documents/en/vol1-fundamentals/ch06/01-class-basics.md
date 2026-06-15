---
chapter: 6
cpp_standard:
- 11
- 14
- 17
- 20
description: 'From `struct` to `class`: Master the basics of defining C++ classes,
  member variables and functions, and access control'
difficulty: beginner
order: 1
platform: host
prerequisites:
- std::string
reading_time_minutes: 14
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: Class Definition
translation:
  engine: anthropic
  source: documents/vol1-fundamentals/ch06/01-class-basics.md
  source_hash: fef76e4b2368c43d8b0910024fd797430c627288688bbe071f40366219d885f3
  token_count: 2522
  translated_at: '2026-05-26T10:50:41.032281+00:00'
---
# Defining Classes

In previous chapters, we used `std::string` to handle text and `std::array` to manage fixed-size collections. These types are convenient to use, but how exactly were they "invented"? The answer is classes. `std::string` itself is a class, `std::array` is also a class, and almost all tools in the C++ standard library are built using classes. We can certainly say that classes are C++'s core abstraction mechanism: they bundle "data" and "the functions that operate on that data" into a single whole, allowing us to use custom types just like built-in types.

In this chapter, we start from the C language's `struct`, figure out exactly what C++'s `class` adds, why access control is needed, and how to define and use member functions. Finally, we tie all this knowledge together with a complete `Point` class.

> **Learning Objectives**
>
> After completing this chapter, you will be able to:
>
> - [ ] Understand the motivation for evolving from a C struct to a C++ class
> - [ ] Define classes containing member variables and member functions
> - [ ] Use `public`, `private`, and `protected` to control member access
> - [ ] Define member functions outside the class body and understand the `::` scope resolution operator
> - [ ] Distinguish the semantic differences between `class` and `struct`, and choose appropriately

## Environment Setup

- Platform: Linux x86\_64 (WSL2 is also fine)
- Compiler: GCC 13+ or Clang 17+
- Compiler flags: `-Wall -Wextra -std=c++17`

## Step 1 — From struct to class

In C, we use `struct` to group related data fields together. For example, a point on a 2D plane:

```c
// C 风格：只有数据，没有行为
struct Point {
    double x;
    double y;
};
```

Then we use standalone functions to operate on this struct:

```c
double point_distance(struct Point a, struct Point b)
{
    double dx = a.x - b.x;
    double dy = a.y - b.y;
    return sqrt(dx * dx + dy * dy);
}

void point_print(struct Point p)
{
    printf("(%g, %g)", p.x, p.y);
}
```

This approach works, but it has a fundamental problem: the association between functions like `point_distance` and `point_print` and the `struct Point` type relies entirely on naming conventions. There is no syntactic mechanism to prevent you from writing absurd calls like `point_distance(some_circle, some_triangle)`—as long as the parameter types happen to match, the compiler will silently let it pass. Even worse, all fields of the struct are public, so anyone can directly write `p.x = -999999;`, turning a point that should represent planar coordinates into a completely meaningless value—and no code will step up and say, "Wait, this value is invalid." That is, until your code suddenly crashes the project, thanks to some mysterious piece of code written by who knows who.

C++ classes solve both problems simultaneously. They bundle data and the functions that operate on that data into a single syntactic unit, and allow you to control which members are visible externally and which are internal implementation details. In C++, `struct` can actually contain member functions too—`struct` and `class` are almost completely equivalent syntactically, with the only difference being the default access level. Let's look at the most basic form first:

```cpp
// C++ 风格：数据 + 行为绑定在一起
class Point {
private:
    double x;
    double y;

public:
    void set(double new_x, double new_y)
    {
        x = new_x;
        y = new_y;
    }

    double distance_to(const Point& other) const
    {
        double dx = x - other.x;
        double dy = y - other.y;
        return std::sqrt(dx * dx + dy * dy);
    }

    void print() const
    {
        std::cout << "(" << x << ", " << y << ")";
    }
};
```

Now `distance_to` and `print`, as member functions of `Point`, inherently know which point they are operating on—there is no need to pass the struct's address back and forth. Meanwhile, `x` and `y` are protected by `private`, preventing external code from modifying them directly.

## Step 2 — Defining a Class

Let's break down the syntax of class definitions item by item.

### Member Variables and Member Functions

Inside the class body, we can include two kinds of things: member variables (also called data members, describing an object's "state") and member functions (also called methods, describing what an object "can do"). Note that the closing brace of a class definition **must be followed by a semicolon**—forgetting this semicolon is one of the most common mistakes for newcomers, and the compiler's error message often points to the next line, which can be highly misleading.

> ⚠️ **Pitfall Warning**
> The closing brace of a class definition **must be followed by a semicolon**. Forgetting this semicolon is one of the most common mistakes for C++ newcomers, and the compiler's error message often points to the next line, which can be highly misleading. For example, if you write `class Foo { ... }` and forget the semicolon, then immediately write `int main() { ... }`, the compiler might report `error: expected ';' after class definition` or the even more absurd `error: 'main' does not name a type`—making you search everywhere for a problem with `main`, when the issue actually lies on the previous line.

### Access Control: public, private, protected

C++ provides three access control keywords: `public`, `private`, and `protected`. All members following them have the corresponding access level, until the next access control keyword or the end of the class body. These are a major core feature of classes! Very important!

`public` members are visible to all code and form the class's external interface. Anyone can call `public` member functions, or read and write `public` member variables. `private` members can only be accessed by the class's own member functions (and friends); external code cannot touch them at all. `protected` is similar to `private`, but derived classes can also access it—we will expand on this when we cover inheritance later. For now, you just need to know it exists.

```cpp
class BankAccount {
private:
    std::string owner;
    double balance;

public:
    void deposit(double amount)
    {
        if (amount > 0) {
            balance += amount;
        }
    }

    bool withdraw(double amount)
    {
        if (amount > 0 && amount <= balance) {
            balance -= amount;
            return true;
        }
        return false;
    }

    double get_balance() const
    {
        return balance;
    }

    const std::string& get_owner() const
    {
        return owner;
    }
};
```

In this `BankAccount` class, `owner` and `balance` are `private`, so external code cannot directly read or modify the balance. The only way to interact with them is through the `public` interfaces: `deposit` (deposit), `withdraw` (withdraw), and `get_balance` (query balance). The benefit of this is that `deposit` and `withdraw` can internally include validation logic—for example, deposit amounts must be positive, and withdrawals cannot overdraw. If `balance` were `public`, anyone could write `account.balance = -999999;`, rendering these validations completely useless.

This is the core value of encapsulation: it is not about "preventing hackers," but rather telling users at the syntactic level—"these internal details are not for you to touch; you should only operate through the interfaces I provide." For the class author, as long as the interface remains unchanged, the internal implementation can be modified in any way without affecting the user's code at all.

> ⚠️ **Pitfall Warning**
> Accessing `private` members from outside the class causes a compilation error, and the error message varies greatly across different compilers. GCC might report `error: 'double BankAccount::balance' is private within this context`, Clang reports `error: 'balance' is a private member of 'BankAccount'`, and MSVC reports `error C2248: 'BankAccount::balance': cannot access private member declared in class 'BankAccount'`. If you see these kinds of messages, first check whether you are trying to touch members you shouldn't from outside the class.

## Step 3 — Ways to Define Member Functions

There are two ways to define member functions: directly inside the class body, or declared inside the class body and defined outside of it.

### Defining Inside the Class Body

Writing the function's implementation directly inside the class body is the most concise approach, suitable for simple one- or two-line logic:

```cpp
class Point {
private:
    double x;
    double y;

public:
    double get_x() const { return x; }
    double get_y() const { return y; }
};
```

Member functions defined inside the class body are implicitly `inline`—the compiler will attempt to expand the function body directly at the call site, eliminating the overhead of a function call. For small functions like `get_x` that simply return a member variable, `inline` works very well.

### Defining Outside the Class Body — The Scope Resolution Operator

For functions with longer logic, we typically write only the declaration inside the class body and move the definition outside. In this case, we must use the scope resolution operator `::` to tell the compiler "which class does this function belong to":

```cpp
// point.hpp
class Point {
private:
    double x;
    double y;

public:
    void set(double new_x, double new_y);
    double distance_to(const Point& other) const;
    void print() const;
};
```

```cpp
// point.cpp
#include <cmath>
#include <iostream>

#include "point.hpp"

void Point::set(double new_x, double new_y)
{
    x = new_x;
    y = new_y;
}

double Point::distance_to(const Point& other) const
{
    double dx = x - other.x;
    double dy = y - other.y;
    return std::sqrt(dx * dx + dy * dy);
}

void Point::print() const
{
    std::cout << "(" << x << ", " << y << ")";
}
```

The `Point::` in `Point::set` is the scope resolution—"this `set` function is not a global function; it is a member function of the `Point` class." If you forget to write `Point::`, the compiler will assume you are defining a regular global function, and then fail because it does not know what `x` and `y` are.

> ⚠️ **Pitfall Warning**
> When defining member functions outside the class body, the `const` qualifier must not be dropped. If you declared `void print() const;` inside the class body, you must also write `void Point::print() const { ... }` when defining it outside. If you write `void Point::print() { ... }` (missing `const`), the compiler will treat them as two different functions—one with `const` that is declared but not defined, and one without `const` that is defined but not declared—and you will get an "undefined reference" error at link time. This pitfall is very subtle because the compilation phase might not catch it; it only blows up during linking.

## Step 4 — What Exactly Is the Difference Between class and struct

We have talked so much about `class`, but what about `struct`? In C++, `struct` and `class` are almost completely equivalent in functionality—`struct` can also have member functions, constructors, access control keywords, inheritance, and so on. The only difference is the **default access level**: members of `class` are `private` by default, while members of `struct` are `public` by default.

```cpp
class ClassStyle {
    int x;      // 默认 private
    void foo(); // 默认 private
};

struct StructStyle {
    int x;      // 默认 public
    void foo(); // 默认 public
};
```

You can of course change the default behavior by explicitly adding access control keywords—a `struct` with `private:` and a `class` with `public:` are completely equivalent semantically, and the compiler generates identical code.

So when should we use `class`, and when should we use `struct`? The C++ community has a widely recognized convention: if a type is primarily used to carry data, all members are public, and there are no complex invariants to maintain, use `struct`; if a type has its own invariants (internal constraints) and needs access control to protect data integrity, use `class`. For example, a type representing an RGB color could use `struct` (the `r`, `g`, and `b` components have no constraints), while a `BankAccount` should use `class` (the balance cannot be negative and should not be modified arbitrarily).

## Step 5 — Hands-on Practice: point.cpp

Now let's combine all the knowledge we have learned so far and write a complete `Point` class, including coordinate access, distance calculation, output printing, and a simple getter/setter pattern.

```cpp
// point.cpp
#include <cmath>
#include <iostream>
#include <string>

/// @brief 二维平面上的点，演示类的基本定义与封装
class Point {
private:
    double x_;
    double y_;

public:
    /// @brief 设置坐标
    /// @param new_x 新的 x 坐标
    /// @param new_y 新的 y 坐标
    void set(double new_x, double new_y)
    {
        x_ = new_x;
        y_ = new_y;
    }

    /// @brief 获取 x 坐标
    /// @return x 坐标的值
    double get_x() const { return x_; }

    /// @brief 获取 y 坐标
    /// @return y 坐标的值
    double get_y() const { return y_; }

    /// @brief 计算到另一个点的欧几里得距离
    /// @param other 目标点
    /// @return 两点之间的距离
    double distance_to(const Point& other) const
    {
        double dx = x_ - other.x_;
        double dy = y_ - other.y_;
        return std::sqrt(dx * dx + dy * dy);
    }

    /// @brief 计算到原点的距离
    /// @return 到原点 (0, 0) 的距离
    double distance_to_origin() const
    {
        return std::sqrt(x_ * x_ + y_ * y_);
    }

    /// @brief 打印坐标到标准输出
    void print() const
    {
        std::cout << "Point(" << x_ << ", " << y_ << ")";
    }
};

int main()
{
    Point p1;
    p1.set(3.0, 4.0);

    Point p2;
    p2.set(6.0, 8.0);

    // 打印两个点
    std::cout << "p1 = ";
    p1.print();
    std::cout << "\n";

    std::cout << "p2 = ";
    p2.print();
    std::cout << "\n";

    // 计算距离
    std::cout << "distance(p1, p2) = " << p1.distance_to(p2) << "\n";
    std::cout << "distance(p1, origin) = " << p1.distance_to_origin() << "\n";

    // 尝试访问 private 成员——取消下面的注释会编译报错
    // p1.x_ = 100.0;  // error: 'double Point::x_' is private

    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17 -Wall -Wextra -o point point.cpp
./point
```

Output:

```text
p1 = Point(3, 4)
p2 = Point(6, 8)
distance(p1, p2) = 5
distance(p1, origin) = 5
```

Let's look at a few design decisions in this code. The member variables `x_` and `y_` use a trailing underscore—this is a common naming convention to distinguish member variables from function parameters. `get_x` and `get_y` are typical getter functions, declared as `const` because reading coordinates does not modify the object. `distance_to` accepts a `const Point&` parameter—note that although `other` is a different object, a member function of `Point` can access the `private` members of all objects of the same class, so `other.x_` is perfectly legal here. The test data uses (3, 4) and (6, 8), which are Pythagorean triples with distances of 5, making it easy to verify the results at a glance.

> ⚠️ **Pitfall Warning**
> `Point p1;` compiles successfully because the compiler automatically generates a default constructor—a parameterless constructor that does nothing. This means the initial values of `x_` and `y_` are undefined. If you call `print` before calling `set`, it will output garbage values. In the next chapter, we will cover how to use constructors to ensure objects are in a valid state upon creation.

## Run Online

Run the Point class example online to observe class encapsulation and member function calls:

<OnlineCompilerDemo
  title="类的定义与封装：Point 二维点类"
  source-path="code/examples/vol1/12_class_point.cpp"
  description="在线运行并观察类的成员函数、const 方法和对象间交互。"
  allow-run
/>

## Exercises

These two exercises cover class definition, access control, and member function design. We recommend writing the code yourself before checking against the suggested approach.

### Exercise 1: Rectangle Class

Design a `Rectangle` class with private member variables `width_` and `height_`, and public member functions `set_size(double w, double h)` (sets width and height; does not modify them if parameters are non-positive), `area()` to calculate the area, `perimeter()` to calculate the perimeter, and `print()` to print the rectangle's information.

### Exercise 2: Timer Class

Design a `Timer` class to simulate a simple timer. Private member variables include `start_time_` and `running_`. Public member functions include `start()`, `stop()`, and `elapsed_seconds()`. Hint: use `std::chrono::steady_clock` from `<chrono>` to get time points.

## Summary

In this chapter, starting from the limitations of the C language's `struct`, we understood the motivation for introducing `class` in C++. Key takeaways: classes manage member visibility through `public`, `private`, and `protected`; member functions can be defined inside the class body (implicitly `inline`) or outside the class body using `::`; `class` and `struct` are functionally equivalent, with the only difference being the default access level—use `struct` to express "pure data," and use `class` to express "a type with behavior and constraints."

However, we intentionally left an important question unanswered: how do we guarantee that an object is in a valid state when it is created? The `Point` class above requires creating an object first and then calling `set`—what if the user forgets? In the next chapter, we will solve this problem—constructors and destructors, which are the cornerstone of RAII (Resource Acquisition Is Initialization) and the starting point of C++'s resource management philosophy.

---

> **Self-Assessment**: If you are still unsure about the access boundaries of `private` and `public`, try intentionally writing a few statements that access private members inside `main` in `point.cpp` (such as `p1.x_ = 100;`), and see how the compiler reports the error. Understanding the meaning of these error messages is the first step to mastering C++ classes.
