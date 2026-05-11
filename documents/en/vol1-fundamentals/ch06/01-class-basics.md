---
title: Class definition
description: 'From struct to class: Mastering the basic usage of C++ class definition,
  member variables and functions, and access control'
chapter: 6
order: 1
difficulty: beginner
reading_time_minutes: 15
platform: host
prerequisites:
- std::string
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
---
# Defining Classes

In previous chapters, we used `std::string` to handle text and `std::array` to manage fixed-size collections—these types are convenient to use, but how were they "invented" in the first place? The answer is classes. `std::string` itself is a class, `std::array` is also a class, and almost every tool in the C++ standard library is built using classes. We can confidently say that classes are C++'s core abstraction mechanism: they bundle "data" and "the functions that operate on that data" into a single whole, allowing us to use custom types just like built-in types.

In this chapter, we start from C's `struct`, clarify exactly what C++'s `class` adds, why access control is needed, and how to define and use member functions. Finally, we tie all the knowledge together with a complete `Point` class.

> **Learning Objectives**
>
> After completing this chapter, you will be able to:
>
> - [ ] Understand the motivation for evolving from C `struct` to C++ `class`
> - [ ] Define classes containing member variables and member functions
> - [ ] Use `public`, `private`, and `protected` to control member access
> - [ ] Define member functions outside the class body, understanding the `::` scope resolution operator
> - [ ] Distinguish the semantic differences between `class` and `struct`, and choose appropriately

## Environment Setup

- Platform: Linux x86\_64 (WSL2 is also fine)
- Compiler: GCC 13+ or Clang 17+
- Compiler flags: `-std=c++20 -Wall -Wextra`

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

This approach works, but it has a fundamental problem: the association between functions like `distance` and `print` and the `Point` struct relies entirely on naming conventions. There is no syntactic mechanism to prevent you from writing something as absurd as `distance(p1, p2)` with mismatched arguments—as long as the parameter types happen to match, the compiler will silently let it pass. Even worse, all fields of the struct are public, so anyone can directly write `p1.x = 999999`, turning a point that should represent planar coordinates into a completely meaningless value—and no code will step up and say "wait, this value is unreasonable." That is, until your code crashes the project in some obscure corner written by who knows who.

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

Now `distance_to` and `print` as member functions of `Point` naturally know which point they are operating on—there's no need to pass the struct address back and forth. Meanwhile, `x_` and `y_` are protected by `private`, so external code cannot modify them directly.

## Step 2 — Defining a Class

Let's break down the syntax of class definitions item by item.

### Member Variables and Member Functions

Inside the class body, we can include two kinds of things: member variables (also called data members, describing an object's "state") and member functions (also called methods, describing what an object "can do"). Note that the closing brace of a class definition **must be followed by a semicolon**—forgetting the semicolon is one of the most common mistakes for beginners, and the compiler's error message often points to the next line, which is very misleading.

> ⚠️ **Pitfall Warning**
> The closing brace of a class definition **must be followed by a semicolon**. Forgetting the semicolon is one of the most common mistakes for C++ beginners, and the compiler's error message often points to the next line, which is very misleading. For example, if you write `class Foo {}` and forget the semicolon, then immediately write `int main()`, the compiler might report `expected unqualified-id before 'int'` or the even more bizarre `expected ';' before 'int'`—making you search everywhere for a problem with `int main()`, when the issue is actually on the previous line.

### Access Control: public, private, protected

C++ provides three access control keywords: `public`, `private`, and `protected`. All members following each keyword have the corresponding access level, until the next access control keyword or the end of the class body. These are a major core of class functionality! Very important!

`public` members are visible to all code and form the class's external interface. Anyone can call `public` member functions or read and write `public` member variables. `private` members can only be accessed by the class's own member functions (and friends)—external code cannot touch them at all. `protected` is similar to `private`, but derived classes can also access it—we'll expand on this when we cover inheritance later. For now, just know it exists.

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

In this `BankAccount` class, `balance_` and `owner_` are `private`, so external code cannot directly read or modify the balance. The only way is through the `public` interfaces: `deposit` (deposit money), `withdraw` (withdraw money), and `get_balance` (query balance). The benefit of this is that `deposit` and `withdraw` can include validation logic internally—for example, deposit amounts must be positive, and withdrawals cannot overdraw. If `balance_` were `public`, anyone could write `account.balance_ = -1000`, making these validations completely useless.

This is the core value of encapsulation: it's not about "preventing hackers," but rather telling users at the syntactic level—these internal details are not for you to touch, you should only operate through the interfaces I provide. For the class author, as long as the interface remains unchanged, the internal implementation can be modified in any way without affecting the user's code at all.

> ⚠️ **Pitfall Warning**
> Accessing `private` members from outside the class causes a compilation error, and the error message varies greatly across different compilers. GCC might report `is private within this context`, Clang reports `'balance_' is a private member`, and MSVC reports `cannot access private member`. If you see these kinds of messages, first check whether you're trying to touch members you shouldn't from outside the class.

## Step 3 — Ways to Define Member Functions

There are two ways to define member functions: define them directly inside the class body, or declare them inside the class body and define them outside.

### Defining Inside the Class Body

Writing the function implementation directly inside the class body is the most concise approach, suitable for simple one- or two-line logic:

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

Member functions defined inside the class body are implicitly `inline`—the compiler will try to expand the function body at the call site, eliminating the overhead of a function call. For small functions like `get_balance` that simply return a member variable, `inline` works very well.

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

The `::` in `BankAccount::deposit` is scope resolution—"this `deposit` function is not a global function, it's a member function of the `BankAccount` class." If you forget to write `BankAccount::`, the compiler will think you're defining a regular global function, and then discover it doesn't know what `balance_` and `owner_` are, resulting in an error.

> ⚠️ **Pitfall Warning**
> When defining member functions outside the class body, the `const` qualifier must not be dropped. If you declared `get_balance` as `const` inside the class body, you must also write `const` when defining it outside. If you write `double get_balance()` (dropping `const`), the compiler will consider these to be two different functions—one with `const` that is declared but not defined, and one without `const` that is defined but not declared—and you'll get an "undefined reference" error at link time. This pitfall is very subtle because the compilation phase might not catch it; it only blows up at link time.

## Step 4 — What Exactly Is the Difference Between class and struct

We've talked so much about `class`, but what about `struct`? In C++, `class` and `struct` are almost completely equivalent in functionality—`struct` can also have member functions, constructors, access control keywords, inheritance... The only difference is the **default access level**: members of a `class` default to `private`, while members of a `struct` default to `public`.

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

You can of course change the default behavior by explicitly adding access control keywords—a `struct` with `private:` added and a `class` with `public:` added are completely equivalent semantically, and the compiler generates identical code.

So when should you use `struct` and when should you use `class`? The C++ community has a widely recognized convention: if a type is primarily used to carry data, all members are public, and there are no complex invariants to maintain, use `struct`; if a type has its own invariants (internal constraints) and needs access control to protect data integrity, use `class`. For example, a type representing an RGB color could use `struct` (the `r`, `g`, and `b` components have no constraints), while a `BankAccount` should use `class` (the balance cannot be negative and cannot be modified arbitrarily).

## Step 5 — Hands-On Practice: point.cpp

Now let's combine all the knowledge we've learned so far and write a complete `Point` class, including coordinate access, distance calculation, output printing, and a simple getter/setter pattern.

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

Let's look at a few design decisions in this code. The member variables `x_` and `y_` use a trailing underscore—this is a common naming convention to distinguish member variables from function parameters. `get_x` and `get_y` are typical getter functions, declared as `const` because reading coordinates doesn't modify the object. `distance_to` accepts a `const Point&` parameter—note that although `other` is a different object, a member function of `Point` can access the `private` members of all objects of the same class, so `other.x_` is legal here. The test data uses (3, 4) and (6, 8), which are Pythagorean triples with distances of 5, making it easy to verify the results at a glance.

> ⚠️ **Pitfall Warning**
> `Point p1;` compiles successfully because the compiler automatically generates a default constructor—a no-argument constructor that does nothing. This means the initial values of `p1.x_` and `p1.y_` are undefined. If you call `p1.print()` before calling `p1.set(3, 4)`, it will output garbage values. In the next chapter, we'll cover how to use constructors to ensure objects are in a valid state when created.

## Exercises

These two exercises cover class definition, access control, and member function design. We recommend writing them yourself before checking your approach against the solutions.

### Exercise 1: Rectangle Class

Design a `Rectangle` class with private member variables `width_` and `height_`, and public member functions `set_size` (sets width and height, does not modify if parameters are non-positive), `area` to calculate the area, `perimeter` to calculate the perimeter, and `print` to output rectangle information.

### Exercise 2: Timer Class

Design a `Timer` class to simulate a simple timer. Private member variables include `start_time_` and `running_`, and public member functions include `start`, `stop`, and `elapsed`. Hint: use `std::chrono`'s `std::chrono::steady_clock::now()` to get time points.

## Summary

In this chapter, starting from the limitations of C's `struct`, we understood the motivation for C++ introducing `class`. Key takeaways: classes manage member visibility through `public`, `private`, and `protected`; member functions can be defined inside the class body (implicitly `inline`) or defined outside the class body using `::`; `class` and `struct` are functionally equivalent, differing only in default access level—use `struct` to express "pure data," and use `class` to express "types with behavior and constraints."

However, we deliberately left an important question unanswered: how do we guarantee an object is in a valid state when created? The `Point` class above requires creating an object first and then calling `set`—what if the user forgets? In the next chapter, we'll solve this problem—constructors and destructors, which are the cornerstone of RAII (Resource Acquisition Is Initialization) and the starting point of C++ resource management philosophy.

---

> **Self-Assessment**: If you're still unsure about the access boundaries of `private` and `public`, try deliberately writing a few statements that access private members inside `main` (like `p1.x_`), and see how the compiler reports the error. Understanding what these error messages mean is the first step to mastering C++ classes.
