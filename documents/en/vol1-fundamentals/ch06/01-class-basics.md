---
chapter: 6
cpp_standard:
- 11
- 14
- 17
- 20
description: 'From struct to class: Mastering C++ class definitions, member variables
  and functions, and basic access control'
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
  source: documents/vol1-fundamentals/ch06/01-class-basics.md
  source_hash: 2c69eece288b7cb0e3fcae25cc3d810b593bb405cdb796d582de65c629df7a27
  translated_at: '2026-06-16T03:44:09.767972+00:00'
  engine: anthropic
  token_count: 2518
---
# Class Definitions

In previous chapters, we used `std::string` to handle text and `std::array` to manage fixed-size collections. These types are convenient to use, but how exactly are they "invented"? The answer is the **class**. `std::string` itself is a class, `std::array` is a class, and almost every tool in the C++ standard library is built using classes. We can confidently say that the class is C++'s core abstraction mechanism: it bundles "data" and "functions that operate on that data" into a single unit, allowing us to use custom types just like built-in types.

In this chapter, starting from the limitations of C's `struct`, we will clarify exactly what C++ `class` adds, why access control is needed, how to define and use member functions, and finally tie it all together with a complete `Point` class.

> **Learning Objectives**
>
> After completing this chapter, you will be able to:
>
> - [ ] Understand the motivation for evolving from C `struct` to C++ `class`.
> - [ ] Define classes containing member variables and member functions.
> - [ ] Use `public`, `private`, and `protected` to control member access permissions.
> - [ ] Define member functions outside the class body and understand the scope resolution operator `::`.
> - [ ] Distinguish between the semantic differences of `class` and `struct` and choose the appropriate one.

## Environment Setup

- **Platform**: Linux x86_64 (WSL2 is also acceptable)
- **Compiler**: GCC 13+ or Clang 17+
- **Compiler flags**: `-Wall -Wextra -std=c++17`

## Step 1 — From struct to class

In C, we use `struct` to group related data fields together. For example, a point on a 2D plane:

```cpp
struct Point {
    double x;
    double y;
};
```

We then use standalone functions to manipulate this structure:

```cpp
double distance(struct Point* p1, struct Point* p2) {
    double dx = p1->x - p2->x;
    double dy = p1->y - p2->y;
    return sqrt(dx * dx + dy * dy);
}

void print_point(struct Point* p) {
    printf("Point(%.2f, %.2f)\n", p->x, p->y);
}

int main() {
    struct Point p1 = {3.0, 4.0};
    struct Point p2 = {6.0, 8.0};
    printf("Distance: %.2f\n", distance(&p1, &p2));
    return 0;
}
```

This approach works, but it has a fundamental problem: the association between functions like `distance` and `print_point` and the `Point` struct is maintained purely by naming conventions. There is no syntactic mechanism to prevent you from writing something absurd like `distance(&p1, &p1)`—as long as the argument types happen to match, the compiler will silently let it pass. Even worse, all fields of the struct are public; anyone can directly write `p.x = 99999`, turning a point that should represent planar coordinates into a completely meaningless value—and no code can step up to say "wait, this value is unreasonable." Until your code suddenly crashes the project thanks to some "benefactor's" code.

C++ classes solve both problems simultaneously. They bundle data and the functions that operate on that data into the same syntactic unit, and allow you to control which members are visible externally and which are internal implementation details. In C++, `struct` can actually contain member functions too—`struct` and `class` are syntactically almost equivalent; the only difference is the default access permission. Let's look at the most basic form:

```cpp
class Point {
public:
    void set(double x_, double y_) {
        x = x_;
        y = y_;
    }

    double distance_to(const Point& other) const {
        double dx = x - other.x;
        double dy = y - other.y;
        return sqrt(dx * dx + dy * dy);
    }

private:
    double x;
    double y;
};
```

Now `set` and `distance_to` are member functions of `Point`, and they inherently know which point they are operating on—no need to pass struct addresses back and forth. Meanwhile, `x` and `y` are protected by `private`, so external code cannot modify them directly.

## Step 2 — Defining a Class

Let's break down the syntax of class definitions item by item.

### Member Variables and Member Functions

Inside the class body, we can include two types of things: member variables (also called data members, describing the object's "state") and member functions (also called methods, describing what the object can "do"). Note that the closing brace at the end of a class definition **must be followed by a semicolon**—forgetting the semicolon is one of the most common mistakes for newcomers, and the compiler's error message often points to the next line, which is very misleading.

> ⚠️ **Pitfall Warning**
> The closing brace at the end of a class definition **must be followed by a semicolon**. Forgetting the semicolon is one of the easiest mistakes for C++ beginners to make, and the error message often points to the next line, which is very confusing. For example, if you write `class Point {}` and forget the semicolon, immediately followed by `int main()`, the compiler might report `expected ';' before 'int'` or the even more bizarre `main' does not name a type`—causing you to search everywhere for a problem with `main`, when the issue is actually on the previous line.

### Access Control: public, private, protected

C++ provides three access control keywords: `public`, `private`, and `protected`. All members following them have the corresponding access permission until the next access control keyword or the end of the class body is encountered. These are a major core feature of classes! Very important!

`public` members are visible to all code and form the external interface of the class. Anyone can call `public` member functions or read/write `public` member variables. `private` members can only be accessed by the class's own member functions (and friends); external code can't touch them at all. `protected` is similar to `private`, but derived classes can also access it—we'll expand on this when we cover inheritance later, for now just know it exists.

```cpp
class BankAccount {
public:
    void deposit(double amount) {
        if (amount > 0) {
            balance += amount;
        }
    }

    bool withdraw(double amount) {
        if (amount > 0 && balance >= amount) {
            balance -= amount;
            return true;
        }
        return false;
    }

    double get_balance() const {
        return balance;
    }

private:
    double balance;
};
```

In this `BankAccount` class, `balance` is `private`. External code cannot directly read or modify the balance. The only way is through the `public` interfaces: `deposit`, `withdraw`, and `get_balance`. The benefit of this is that validation logic can be added inside `deposit` and `withdraw`—for example, deposit amounts must be positive, and withdrawals cannot overdraw. If `balance` were `public`, anyone could write `account.balance = 999999;`, rendering these validations useless.

This is the core value of encapsulation: it's not about "preventing hackers," but about telling users syntactically—"these are internal details you shouldn't touch; you should only operate through the interfaces I provide." For the class author, as long as the interface remains unchanged, the internal implementation can be modified however you like without affecting the user's code.

> ⚠️ **Pitfall Warning**
> Accessing `private` members from outside the class will cause a compilation error, and the error message varies significantly between compilers. GCC might report `is private within this context`, Clang might report `is a private member of`, and MSVC might report `cannot access private member`. If you see such messages, first check if you are trying to touch members you shouldn't from outside the class.

## Step 3 — Defining Member Functions

Member functions can be defined in two ways: directly inside the class body, or declared inside the class body and defined outside.

### Definition Inside the Class

Writing the function implementation directly inside the class body is the most concise method, suitable for simple one or two-line functions:

```cpp
class Point {
public:
    double get_x() const {
        return x;
    }

    double get_y() const {
        return y;
    }
    // ...
};
```

Member functions defined inside the class body are implicitly `inline`—the compiler will try to expand the function body at the call site, saving the overhead of a function call. For small functions like `get_x` that just return a member variable, `inline` works very well.

### Definition Outside the Class — Scope Resolution Operator

For longer logic, we usually write only the declaration inside the class body and move the definition outside. In this case, we must use the scope resolution operator `::` to tell the compiler "which class this function belongs to":

```cpp
class Point {
public:
    double distance_to(const Point& other) const;
    // ...
};

double Point::distance_to(const Point& other) const {
    double dx = x - other.x;
    double dy = y - other.y;
    return sqrt(dx * dx + dy * dy);
}
```

```cpp
// ❌ Wrong: Missing scope resolution
double distance_to(const Point& other) const {
    // ...
}
```

`Point::` in `Point::distance_to` is the scope resolution—"this `distance_to` function is not a global function, it is a member function of the `Point` class." If you forget to write `Point::`, the compiler will think you are defining a normal global function, then realize it doesn't know what `x` and `y` are, and report an error directly.

> ⚠️ **Pitfall Warning**
> When defining a member function outside the class, the `const` qualifier must not be dropped. If you declared `distance_to` as `const` inside the class, you must also write `const` in the definition outside. If you write `double Point::distance_to(const Point& other)` (omitting `const`), the compiler will treat these as two different functions—one with a `const` declaration has no definition, and one without `const` has a definition but no declaration—and will report an "undefined reference" error during linking. This pitfall is very hidden because it might not be caught during compilation; it only explodes during linking.

## Step 4 — What is the difference between class and struct

We've talked so much about `class`, but what about `struct`? In C++, `struct` and `class` are functionally almost completely equivalent—`struct` can also have member functions, constructors, access control keywords, inheritance... The only difference is the **default access permission**: members of a `class` are `private` by default, while members of a `struct` are `public` by default.

```cpp
class DefaultPrivate {
    int x;  // 默认 private
public:
    int y;  // 显式 public
};

struct DefaultPublic {
    int x;  // 默认 public
private:
    int y;  // 显式 private
};
```

You can of course change the default behavior by explicitly adding access control keywords—a `struct` with `private` and a `class` with `public` are semantically completely equivalent, and the compiler generates identical code.

So when do we use `struct` and when do we use `class`? The C++ community has a widely accepted convention: if a type is primarily used to carry data, all members are public, and there are no complex invariants to maintain, use `struct`; if a type has its own invariants (internal constraints) and needs access control to protect data integrity, use `class`. For example, a type representing RGB color can use `struct` (the `R`, `G`, `B` components have no constraints), while a `BankAccount` should use `class` (balance cannot be negative and cannot be modified arbitrarily).

## Step 5 — Practice: point.cpp

Now let's synthesize all the knowledge we've learned so far to write a complete `Point` class, including coordinate access, distance calculation, output printing, and a simple getter/setter pattern.

```cpp
#include <iostream>
#include <cmath>

class Point {
public:
    // Setter
    void set(double x_, double y_) {
        x = x_;
        y = y_;
    }

    // Getters
    double get_x() const { return x; }
    double get_y() const { return y; }

    // 计算到另一个点的距离
    double distance_to(const Point& other) const {
        double dx = x - other.x;
        double dy = y - other.y;
        return std::sqrt(dx * dx + dy * dy);
    }

    // 打印点信息
    void print() const {
        std::cout << "Point(" << x << ", " << y << ")" << std::endl;
    }

private:
    double x_;
    double y_;
};

int main() {
    Point p1;
    p1.set(3.0, 4.0);

    Point p2;
    p2.set(6.0, 8.0);

    p1.print();
    p2.print();

    std::cout << "Distance: " << p1.distance_to(p2) << std::endl;

    return 0;
}
```

Compile and run:

```bash
g++ -Wall -Wextra -std=c++17 point.cpp -o point
./point
```

Output:

```text
Point(3, 4)
Point(6, 8)
Distance: 5
```

Let's look at a few design decisions in this code. The member variables `x_` and `y_` use an underscore suffix—this is a common naming convention to distinguish member variables from function parameters. `get_x` and `get_y` are typical getter functions, declared as `const` because reading coordinates does not require modifying the object. `distance_to` accepts a `const Point&` parameter—note that even though `other` is a separate object, `distance_to` (a member function of `Point`) can access the `private` members of any object of the same class, so `other.x_` and `other.y_` are legal here. The test data chose (3, 4) and (6, 8), two Pythagorean triples where the distance is 5, making it easy to verify the result at a glance.

> ⚠️ **Pitfall Warning**
> `Point p1;` compiles successfully because the compiler automatically generated a default constructor—a parameterless constructor that does nothing. This means the initial values of `p1.x_` and `p1.y_` are undefined. If you call `p1.print()` before calling `p1.set()`, it will output garbage values. In the next chapter, we will cover how to use constructors to ensure objects are created in a valid state.

## Run Online

Run the Point class example online to observe class encapsulation and member function calls:

<OnlineCompilerDemo
  title="Class Definition and Encapsulation: Point 2D Class"
  source-path="code/examples/vol1/12_class_point.cpp"
  description="Run online and observe class member functions, const methods, and interaction between objects."
  allow-run
/>

## Exercises

These two exercises cover class definition, access control, and member function design. It is recommended to write them yourself before checking the logic.

### Exercise 1: Rectangle Class

Design a `Rectangle` class containing private member variables `width` and `height`, and public member functions `set_size(double w, double h)` (sets width and height, does not modify if parameters are non-positive), `area()` to calculate area, `perimeter()` to calculate perimeter, and `print()` to output rectangle information.

### Exercise 2: Timer Class

Design a `Timer` class to simulate a simple timer. Private member variables include `start_time` and `end_time`, and public member functions include `start()`, `stop()`, and `elapsed_seconds()`. Hint: use `std::chrono`'s `std::chrono::steady_clock::now()` to get time points.

## Summary

In this chapter, starting from the limitations of C's `struct`, we understood the motivation for C++ introducing `class`. Key takeaways: classes manage member visibility through `public`, `private`, and `protected`; member functions can be defined inside the class body (implicitly `inline`) or outside the class body using `::`; `class` and `struct` are functionally equivalent, differing only in default access permissions—use `struct` to express "plain data" and `class` to express "types with behavior and constraints."

However, we intentionally left an important question: how do we ensure an object is in a valid state when created? The `Point` class above required creating the object first and then calling `set`. What if the user forgets? In the next chapter, we will solve this problem—constructors and destructors. They are the cornerstone of RAII and the starting point of C++ resource management philosophy.

---

> **Self-Assessment**: If you are still unsure about the access boundaries of `public` and `private`, try intentionally writing a few statements accessing private members in `main` (like `p1.x_ = 0;`), and see how the compiler reports errors. Understanding the meaning of these error messages is the first step to mastering C++ classes.
