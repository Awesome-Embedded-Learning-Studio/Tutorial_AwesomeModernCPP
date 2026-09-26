---
chapter: 6
cpp_standard:
- 11
- 14
- 17
- 20
description: From struct to class - master the basics of C++ class definitions,
  member variables and functions, and access control
difficulty: beginner
order: 1
platform: host
prerequisites:
- std::string
reading_time_minutes: 19
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: Class Definition
translation:
  source: documents/vol1-fundamentals/ch06/01-class-basics.md
  source_hash: 674acce5246522d639b196d94e0fbe71cc0f72002191922296f6349f98dfd0b3
  translated_at: '2026-09-25T11:02:15+00:00'
  engine: anthropic
  token_count: 6900
---
# Class Definition: Packaging Data and Operations into Your Own Type

In earlier chapters we processed text with `std::string` and managed fixed-size collections with `std::array`. These types are convenient to use—but how exactly were they "invented"? The answer: classes. `std::string` itself is a class, `std::array` is a class too, and nearly every tool in the C++ standard library is built out of classes. You could say the class is C++'s most central abstraction mechanism: it bundles "data" and "the functions that operate on that data" into a single whole, letting us use custom types just as we use built-in types.

In this chapter we start from C's `struct`, pin down exactly what C++'s `class` adds, why access control is needed, and how to define and use member functions, and finally tie everything together with a complete `Point` class.

## From struct to class

In C, we use a `struct` to gather related data fields together—for example, a point on a 2D plane:

```c
// C style: data only, no behavior
struct Point {
    double x;
    double y;
};
```

We then manipulate the struct with standalone functions:

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

This style works, but it has a fundamental problem: the association between functions like `point_distance` and `point_print` and `struct Point` is held together purely by naming convention. There is no syntax-level mechanism to stop us from writing an absurd call like `point_distance(some_circle, some_triangle)`—as long as the parameter types happen to match, the compiler waves it through without a sound. Worse, every field of the struct is public: anyone can write `p.x = -999999;`, turning a point that is supposed to hold plane coordinates into a completely meaningless value, and no check can stop it—until one day the project gets wrecked by some mystery contributor's code, and only then do we discover the value has been rotten all along.

The C++ class solves both problems at once. It pulls the data and the functions that operate on it into the same syntactic unit, and lets us control which members are visible to the outside and which are internal implementation details. In C++, a `struct` can in fact contain member functions too: `struct` and `class` are almost fully equivalent in syntax, and the only difference is the default access level. Let's look at the most basic form first:

```cpp
// C++ style: data + behavior bound together
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

Now, as member functions of `Point`, `distance_to` and `print` inherently know which point they are operating on—we no longer need to pass a struct address around. And with `x` and `y` guarded by `private`, external code cannot modify them directly.

## Defining a Class

Let's take the class-definition syntax apart piece by piece.

### Member Variables and Member Functions

Inside a class body we can put two kinds of things: member variables (also called data members, describing an object's "state") and member functions (also called methods, describing what an object "can do").

The closing brace of a class definition **must be followed by a semicolon**. Forgetting that semicolon is one of the most common mistakes C++ beginners make, and the compiler's error message usually points at the next line, which is deeply misleading. Say we write `class Foo { ... }`, forget the semicolon, and immediately follow it with `int main() { ... }`: the compiler may report `error: expected ';' after class definition`, or something even more bizarre like `error: 'main' does not name a type`, sending us on a wild goose chase for what's wrong with `main` when the real problem is on the line above.

### Access Control: public, private, protected

C++ gives us three access-control keywords: `public`, `private`, and `protected`. Every member following such a keyword carries the corresponding access level, until the next access-control keyword or the end of the class body. This is a huge part of what classes do—really important!

`public` members are visible to all code and form the class's external interface: anyone may call a `public` member function or read and write a `public` member variable. `private` members are accessible only to the class's own member functions (and friends)—outside code cannot touch them at all. `protected` is like `private`, except derived classes can access those members as well; we'll expand on that later when we cover inheritance, so for now just knowing it exists is enough.

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

Look at this `BankAccount` class: `owner` and `balance` are `private`, so outside code cannot read or modify the balance directly. The only way in is through the `public` interfaces `deposit`, `withdraw`, and `get_balance`. The benefit is that `deposit` and `withdraw` can embed validation logic—deposits must be positive, withdrawals must not overdraw. If `balance` were `public`, anyone could write `account.balance = -999999;` and all that validation would be pure decoration.

This is the core value of encapsulation: at the syntactic level it tells users "these internal details are off-limits; interact only through the interface the class provides." It is not meant to defend against hackers—it defends against everyday slips and misuse. For the class author, as long as the interface stays unchanged, the internals can be reworked however they like, and we users are completely unaffected.

Accessing a `private` member from outside the class is a compile error, and the message varies a lot across compilers. GCC may report `error: 'double BankAccount::balance' is private within this context`, Clang says `error: 'balance' is a private member of 'BankAccount'`, and MSVC produces `error C2248: 'BankAccount::balance': cannot access private member declared in class 'BankAccount'`. Whenever we see messages like these, the first thing to check is whether we tried to touch a member we shouldn't have from outside the class.

## Ways to Define Member Functions

There are two ways to define member functions: define them directly inside the class body, or declare them inside the body and define them outside.

### Defining Inside the Class Body

We write the function implementation directly inside the class body. This is the most concise style, well suited to simple one- or two-line functions:

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

Member functions defined inside the class body are implicitly `inline`: the compiler will try to expand the function body right at the call site, saving the overhead of a function call. For tiny functions like `get_x`, which just return a member variable, `inline` works beautifully.

### Defining Outside the Class Body - the Scope Resolution Operator

For functions with longer logic, we usually write only the declaration inside the class body and move the definition outside. At that point we must use the scope resolution operator `::` to tell the compiler which class the function belongs to:

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

The `Point::` in `Point::set` is exactly the scope resolution—"this `set` is not a global function; it is a member function of the `Point` class." If we forget to write `Point::`, the compiler thinks we are defining an ordinary global function, then discovers it has no idea what `x` and `y` are, and errors out.

When defining a member function outside the class body, the `const` qualifier must not be dropped. If we declared `void print() const;` inside the class, the out-of-body definition must also be written `void Point::print() const { ... }`. Writing `void Point::print() { ... }` (missing the `const`) makes the compiler treat them as two different functions: a `const` declaration with no definition, and a non-`const` definition with no declaration. At link time we then get an "undefined reference" error. This trap is notoriously sneaky because the compile stage may not catch it—it only blows up at link time.

## What Exactly Is the Difference Between class and struct

We've said so much about `class`—so what about `struct`? In C++, `struct` and `class` are almost fully equivalent in capability: a `struct` can also have member functions, constructors, access-control keywords, inheritance… The only difference is the **default access level**: members of a `class` are `private` by default, while members of a `struct` are `public` by default.

```cpp
class ClassStyle {
    int x;      // private by default
    void foo(); // private by default
};

struct StructStyle {
    int x;      // public by default
    void foo(); // public by default
};
```

We can of course override the defaults by adding access-control keywords explicitly: a `struct` with `private:` and a `class` with `public:` are semantically identical, and the compiler generates exactly the same code for both.

So when do we use `class`, and when `struct`? The C++ community has a widely accepted convention: if a type mainly carries data, all of its members are public, and there is no complicated invariant to maintain, use `struct`; if a type has its own invariants (internal constraints) and needs access control to protect data integrity, use `class`. For example, a type representing an RGB color can be a `struct` (the `r`, `g`, `b` components carry no constraints), whereas a `BankAccount` should be a `class` (the balance must never go negative or be modified arbitrarily).

## Hands-On Practice: point.cpp

Let's combine everything we've learned and write a complete `Point` class, including coordinate access, distance computation, printing, and a simple getter/setter pattern.

```cpp
// point.cpp
#include <cmath>
#include <iostream>
#include <string>

/// @brief A point on the 2D plane, demonstrating basic class definition and encapsulation
class Point {
private:
    double x_;
    double y_;

public:
    /// @brief Set the coordinates
    /// @param new_x The new x coordinate
    /// @param new_y The new y coordinate
    void set(double new_x, double new_y)
    {
        x_ = new_x;
        y_ = new_y;
    }

    /// @brief Get the x coordinate
    /// @return The value of the x coordinate
    double get_x() const { return x_; }

    /// @brief Get the y coordinate
    /// @return The value of the y coordinate
    double get_y() const { return y_; }

    /// @brief Compute the Euclidean distance to another point
    /// @param other The target point
    /// @return The distance between the two points
    double distance_to(const Point& other) const
    {
        double dx = x_ - other.x_;
        double dy = y_ - other.y_;
        return std::sqrt(dx * dx + dy * dy);
    }

    /// @brief Compute the distance to the origin
    /// @return The distance to the origin (0, 0)
    double distance_to_origin() const
    {
        return std::sqrt(x_ * x_ + y_ * y_);
    }

    /// @brief Print the coordinates to standard output
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

    // Print the two points
    std::cout << "p1 = ";
    p1.print();
    std::cout << "\n";

    std::cout << "p2 = ";
    p2.print();
    std::cout << "\n";

    // Compute the distances
    std::cout << "distance(p1, p2) = " << p1.distance_to(p2) << "\n";
    std::cout << "distance(p1, origin) = " << p1.distance_to_origin() << "\n";

    // Try accessing a private member - uncommenting the line below fails to compile
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

Let's walk through a few design decisions in this code. The member variables `x_` and `y_` carry an underscore suffix—a common naming style for distinguishing member variables from function parameters. `get_x` and `get_y` are classic getters, declared `const` because reading a coordinate never needs to modify the object. `distance_to` takes a `const Point&` parameter; note that although `other` is a different object, a member function of `Point` may access the `private` members of every object of the same class, so `other.x_` is perfectly legal here. The test data deliberately uses the Pythagorean pairs (3, 4) and (6, 8), both at distance 5, so we can verify the results at a glance.

`Point p1;` compiles because the compiler automatically generates a default constructor—a parameterless constructor that does nothing. That means the initial values of `x_` and `y_` are indeterminate; if we call `print` before calling `set`, we get garbage output. In the next chapter we'll cover how constructors make sure an object is already in a valid state the moment it is created.

## Run It Online

You can also run the `Point` example online and observe class encapsulation and member function calls:

<OnlineCompilerDemo
  title="Class Definition and Encapsulation: Point 2D Class"
  source-path="code/examples/vol1/12_class_point.cpp"
  description="Run online and observe class member functions, const methods, and interaction between objects."
  allow-run
/>

## Exercises

These two exercises cover class definition, access control, and member function design. Try writing your own solution first, then compare it against the reference.

### Exercise 1: The Rectangle Class

Design a `Rectangle` class with private member variables `width_` and `height_`, plus public member functions `set_size(double w, double h)` (set the width and height; leave them unchanged if either argument is non-positive), `area()` to compute the area, `perimeter()` to compute the perimeter, and `print()` to print the rectangle's information.

::: details Reference Solution

```cpp
#include <iostream>

class Rectangle {
private:
    double width_;
    double height_;

public:
    void set_size(double w, double h)
    {
        // Leave unchanged when an argument is non-positive
        if (w > 0 && h > 0) {
            width_ = w;
            height_ = h;
        }
    }

    double area() const
    {
        return width_ * height_;
    }

    double perimeter() const
    {
        return 2 * (width_ + height_);
    }

    void print() const
    {
        std::cout << "Rectangle(width=" << width_
                  << ", height=" << height_ << ")" << std::endl;
    }
};

int main()
{
    Rectangle rect;
    rect.set_size(5.0, 3.0);
    rect.print();
    std::cout << "面积: " << rect.area() << std::endl;
    std::cout << "周长: " << rect.perimeter() << std::endl;

    rect.set_size(10.0, 4.0);
    rect.print();
    std::cout << "面积: " << rect.area() << std::endl;
    std::cout << "周长: " << rect.perimeter() << std::endl;

    // Test the non-positive-argument case (should not modify)
    rect.set_size(-5.0, 3.0);
    rect.print();  // Should still be 10.0, 4.0

    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17 -Wall -Wextra main.cpp -o main && ./main
```

Result:

```text
Rectangle(width=5, height=3)
面积: 15
周长: 16
Rectangle(width=10, height=4)
面积: 40
周长: 28
Rectangle(width=10, height=4)
```

:::

### Exercise 2: The Timer Class

Design a `Timer` class that simulates a simple stopwatch. Its private member variables include `start_time_` and `running_`, and its public member functions include `start()`, `stop()`, and `elapsed_seconds()`. Hint: use `std::chrono::steady_clock` from `<chrono>` to obtain time points. `elapsed_seconds()` should return the time elapsed so far when `running_` is `true`, and the duration recorded at the last stop when it is `false`.

::: details Reference Solution

```cpp
#include <iostream>
#include <chrono>
#include <thread>

class Timer
{
private:
    std::chrono::time_point<std::chrono::steady_clock> start_time;
    std::chrono::time_point<std::chrono::steady_clock> end_time;
    bool running = false;

public:
    void start()
    {
        start_time = std::chrono::steady_clock::now();
        running = true;
    }
    void stop()
    {
        end_time = std::chrono::steady_clock::now();
        running = false;
    }
    int64_t elapsed() const
    {
        if (running)
        {
            std::chrono::time_point<std::chrono::steady_clock> current_time = std::chrono::steady_clock::now();
            return std::chrono::duration_cast<std::chrono::microseconds>(current_time - start_time).count();
        }
        return std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();
    }
};
int main()
{
    Timer timer;
    timer.start();
    std::this_thread::sleep_for(std::chrono::microseconds(200));

    timer.stop();
    std::cout << "运行时间: " << timer.elapsed() << " 微秒" << std::endl;
    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17 -Wall -Wextra main.cpp -o main && ./main
```

Result:

```text
运行时间: 285 微秒
```

:::
