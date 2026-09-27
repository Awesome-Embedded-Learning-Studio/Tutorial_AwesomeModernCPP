---
chapter: 6
cpp_standard:
- 11
- 14
- 17
- 20
description: Master the complete use of default constructors, parameterized constructors,
  copy constructors, member initializer lists, and delegating constructors.
difficulty: beginner
order: 2
platform: host
prerequisites:
- Class Definition
reading_time_minutes: 22
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: Constructors
translation:
  source: documents/vol1-fundamentals/ch06/02-constructors.md
  source_hash: aa1b6c49dd092be1bc2eb44bcd24f4cfc9dc1b6fa752942d4a5cd3178a00af51
  translated_at: '2026-09-25T11:02:02+00:00'
  engine: anthropic
  token_count: 9000
---
# Constructors: Don't Let Your Objects Be Born Carrying Garbage Values

In the previous chapter we learned how to define a class—writing member variables, writing member functions, and using `public` and `private` to control access. But there is one question we kept dodging: when an object gets created, what exactly is inside its member variables? The answer: if we do nothing, the member variables of a local object hold **garbage values**—random data left over from whatever last occupied that memory.

Once an object comes into existence, it should be in a **valid, usable, and predictable** state. The constructor is C++'s answer: it runs automatically when the object is created and is responsible for bringing the member variables to their correct initial state. As long as we write the constructor correctly, rookie mistakes like "forgetting to initialize" simply cannot happen.

In this chapter we will take apart every form the constructor takes—default construction, parameterized construction, copy construction, the member initializer list, plus delegating constructors introduced in C++11. Each has its own use cases and hidden pitfalls.

## Default Constructors — Creating Objects Without Arguments

A default constructor takes no arguments at all. When we write `Point p;`, this is the one that gets called.

```cpp
class Point {
private:
    double x_;
    double y_;

public:
    Point() : x_(0.0), y_(0.0) {}
};
```

The `: x_(0.0), y_(0.0)` after `Point()` is a member initializer list—just get familiar with its face for now; we will devote a whole section to it later. The key point is the default constructor's duty: the moment the object exists, it is already a valid origin coordinate.

If we don't write a single constructor, the compiler generates a default constructor for us. But it performs no initialization whatsoever for fundamental types like `int` and `double`—the values remain garbage. So whenever a class has members of fundamental types, we almost always need to write the default constructor ourselves.

The rule for compiler-generated default constructors is just one line long: the moment we hand-write **any** constructor (even one that takes parameters), the compiler stops generating a default constructor. Plenty of folks write a `Point(double x, double y)` and then find that `Point p;` no longer compiles, and they are completely baffled. This is why: we wrote a parameterized constructor, so the compiler concludes "you are managing initialization yourself now, so the default constructor is your job too."

The fix is simple: either add a `Point() : x_(0.0), y_(0.0) {}` ourselves, or use the C++11 `= default` syntax to let the compiler keep generating one for us:

```cpp
class Point {
private:
    double x_;
    double y_;

public:
    Point() = default;                      // Let the compiler generate the default constructor
    Point(double x, double y) : x_(x), y_(y) {}
};
```

Note that a `= default` default constructor still does not zero-initialize fundamental-type members. If we need zero initialization, we still have to write `: x_(0.0), y_(0.0) {}` ourselves or use in-class initializers (covered in the next chapter).

## Parameterized Constructors — Handing Initialization Power to the Caller

Most of the time we want an object to carry real data the moment it is created, rather than some zero-valued default state. A parameterized constructor accepts arguments to initialize the member variables.

```cpp
class Point {
private:
    double x_;
    double y_;

public:
    Point(double x, double y) : x_(x), y_(y) {}
};

Point origin(0.0, 0.0);
Point target(3.5, -2.1);
```

Constructors can be overloaded, so we can offer both a default constructor and a parameterized one and let callers pick as needed. But next we have to talk about a keyword that is easy to overlook: `explicit`. When a constructor accepts exactly one argument (or all its remaining arguments have defaults), it doubles as an implicit type-conversion function. Consider:

```cpp
class PWMChannel {
private:
    int channel_;

public:
    PWMChannel(int ch) : channel_(ch) {}
};

void set_active(PWMChannel ch);

set_active(3);  // Compiles! The int is implicitly converted to PWMChannel(3)
```

In the call `set_active(3)`, the function signature asks for a `PWMChannel`, we pass an `int`, and the compiler helpfully invokes the constructor to perform an implicit conversion. In a tiny example this looks harmless, but in a large project such implicit conversions manufacture hard-to-track-down bugs—we may simply have written the wrong parameter type, and instead of complaining, the compiler "kindly" makes things worse.

The `explicit` keyword exists precisely to forbid that implicit conversion:

```cpp
class PWMChannel {
private:
    int channel_;

public:
    explicit PWMChannel(int ch) : channel_(ch) {}
};

void set_active(PWMChannel ch);

// set_active(3);               // Compile error! No implicit conversion
set_active(PWMChannel(3));      // OK, explicit construction
```

My recommendation: **every single-argument constructor should be marked `explicit`**, unless you have a very clear reason to want the implicit conversion. It is a defensive measure that costs practically nothing.

## The Member Initializer List: The Right Way to Initialize

We have been using the member initializer list all along; now let's take it apart properly.

We write the constructor's initializer list after a colon following the parameter list, entries separated by commas, each member followed by its initial value in parentheses (or braces):

```cpp
class Sensor {
private:
    int pin_;
    double threshold_;

public:
    Sensor(int pin, double threshold)
        : pin_(pin), threshold_(threshold) {}
};
```

You might ask: why not just assign inside the constructor body? Why bother with a dedicated initializer list?

```cpp
// Approach 1: the initializer list (recommended)
Sensor(int pin, double threshold)
    : pin_(pin), threshold_(threshold) {}

// Approach 2: assigning inside the constructor body (compiles, but not recommended)
Sensor(int pin, double threshold) {
    pin_ = pin;           // This is not "initialization"; it is "assignment"
    threshold_ = threshold;
}
```

For fundamental types like `int` and `double`, both approaches produce exactly the same runtime result. The trouble shows up with `const` members and reference members: those two can **only** be initialized, never assigned. By the time the constructor body starts running, every member has already been default-constructed; going back to assign them then is too late for `const` members and references, and the compiler rejects it outright.

```cpp
class Config {
private:
    const int kMaxRetry;
    int& counter_ref;

public:
    // The only legal way: the initializer list
    Config(int max, int& ref)
        : kMaxRetry(max), counter_ref(ref) {}

    // The version below blows up at compile time:
    // Config(int max, int& ref) {
    //     kMaxRetry = max;      // Compile error! const members cannot be assigned
    //     counter_ref = ref;    // Compile error! a reference must be bound at initialization
    // }
};
```

Even without `const` or reference members, the initializer list still wins. For class-type members (like `std::string`), assigning in the body means default-construct first and then overwrite by assignment—two steps; the initializer list constructs directly with the target value—one step, done.

Members are initialized in their **order of declaration** in the class definition, which has **nothing** to do with the order written in the initializer list. This matters a lot: if our initializer list says `: b(a), a(10)` and in the class `a` is declared before `b`, the actual execution initializes `a` to 10 first, then initializes `b` from `a` (by then `a` is already 10)—the result is correct. But flip the declaration order (`b` first, `a` after), and when `b(a)` runs, `a` has not been initialized yet, so `b` reads a garbage value. Most compilers warn when the two orders disagree, but we should still build the habit of keeping the declaration order and the initializer list order identical—don't plant landmines for ourselves.

## Copy Constructors — Creating New Objects from Existing Ones

A copy constructor creates a new object from an existing object of the same type, with a fixed signature `ClassName(const ClassName& other)`:

```cpp
class Point {
private:
    double x_;
    double y_;

public:
    Point(double x, double y) : x_(x), y_(y) {}

    // Copy constructor
    Point(const Point& other) : x_(other.x_), y_(other.y_) {}
};

Point a(1.0, 2.0);
Point b = a;   // Invokes the copy constructor
Point c(a);    // Also invokes the copy constructor
```

We run into three scenarios where the copy constructor gets called: copy initialization (`Point b = a;`), passing arguments by value (the parameter is created via copy construction), and returning by value (the return value is copied via copy construction—though modern compilers usually elide that copy with RVO).

If we don't write a copy constructor ourselves, the compiler generates a default one whose behavior is a **memberwise copy**: each member gets its own copy constructor invoked (for fundamental types, the value is simply copied). For a class like `Point` that holds only fundamental types, the default version is perfectly adequate.

Memberwise copying is catastrophic for classes that contain **raw pointers**. Suppose our class has an `int* data_` pointing to dynamically allocated memory: the default copy constructor copies only the pointer's value (the address), not what it points to. The result is two objects whose `data_` both point at the same block—one object's destructor frees the memory while the other is still using it, leaving a dangling pointer. This is the classic "shallow copy" problem; we will dig into how to solve it later when we cover RAII and smart pointers.

```cpp
class Buffer {
private:
    int* data_;
    std::size_t size_;

public:
    Buffer(std::size_t size) : size_(size), data_(new int[size]()) {}

    // Without a hand-written copy constructor, the default one copies only the pointer's address
    // Both objects then delete the same memory at destruction—boom
};
```

For now we only need to remember one thing: if our class manages a resource (dynamic memory, file handles, network connections, and so on), we must write the copy constructor ourselves (or simply disable it—we will see how later).

## Delegating Constructors — Letting Constructors Help Each Other

C++11 introduced delegating constructors, which let one constructor call **another constructor of the same class** in its initializer list, cutting down duplicated code.

```cpp
class Point {
private:
    double x_;
    double y_;

public:
    // The "primary" constructor: does all the work
    Point(double x, double y) : x_(x), y_(y) {}

    // Default constructor: delegates to the primary constructor above
    Point() : Point(0.0, 0.0) {}
};
```

The initializer list of `Point()` names no member—instead it says `Point(0.0, 0.0)`, calling another constructor. Look at the execution order: first the target constructor's initializer list and body run, then control returns to the delegating constructor's body.

This feature shines when a class has many constructors with overlapping initialization logic: put the core logic in one "primary" constructor and have all the others delegate to it.

One hard rule though: **once the initializer list contains a delegation, it may not initialize any member**. Writing `Point() : Point(0.0, 0.0), x_(0) {}` is illegal—either delegate everything or initialize everything yourself; no mixing.

## Hands-On Practice — constructors.cpp

Let's fold every constructor type from this chapter into one `Student` class and mark each constructor call with output:

```cpp
// constructors.cpp
// Constructor showcase: default, parameterized, copy, and delegating construction

#include <iostream>
#include <string>

class Student {
private:
    std::string name_;
    int age_;
    double score_;

public:
    Student() : name_("Unknown"), age_(0), score_(0.0)
    {
        std::cout << "[默认构造] " << name_ << ", "
                  << age_ << " 岁, " << score_ << " 分" << std::endl;
    }

    Student(const std::string& name, int age, double score)
        : name_(name), age_(age), score_(score)
    {
        std::cout << "[参数化构造] " << name_ << ", "
                  << age_ << " 岁, " << score_ << " 分" << std::endl;
    }

    // Delegating constructor: takes only a name, delegates the rest to the parameterized constructor above
    Student(const std::string& name) : Student(name, 18, 0.0)
    {
        std::cout << "[委托构造] 只指定姓名" << std::endl;
    }

    Student(const Student& other)
        : name_(other.name_), age_(other.age_), score_(other.score_)
    {
        std::cout << "[拷贝构造] 复制: " << name_ << std::endl;
    }

    void print() const
    {
        std::cout << "  " << name_ << ", " << age_
                  << " 岁, " << score_ << " 分" << std::endl;
    }
};

/// @brief Pass by value; triggers the copy constructor
void enroll(Student s)
{
    std::cout << "  注册: ";
    s.print();
}

int main()
{
    std::cout << "=== 默认构造 ===" << std::endl;
    Student s1;
    s1.print();

    std::cout << "\n=== 参数化构造 ===" << std::endl;
    Student s2("Alice", 20, 92.5);
    s2.print();

    std::cout << "\n=== 委托构造 ===" << std::endl;
    Student s3("Bob");
    s3.print();

    std::cout << "\n=== 拷贝构造（拷贝初始化）===" << std::endl;
    Student s4 = s2;
    s4.print();

    std::cout << "\n=== 拷贝构造（按值传参）===" << std::endl;
    enroll(s2);

    return 0;
}
```

Compile and run: `g++ -std=c++17 -Wall -Wextra -o constructors constructors.cpp && ./constructors`

Expected output:

```text
=== 默认构造 ===
[默认构造] Unknown, 0 岁, 0 分
  Unknown, 0 岁, 0 分

=== 参数化构造 ===
[参数化构造] Alice, 20 岁, 92.5 分
  Alice, 20 岁, 92.5 分

=== 委托构造 ===
[参数化构造] Bob, 18 岁, 0 分
[委托构造] 只指定姓名
  Bob, 18 岁, 0 分

=== 拷贝构造（拷贝初始化）===
[拷贝构造] 复制: Alice
  Alice, 20 岁, 92.5 分

=== 拷贝构造（按值传参）===
[拷贝构造] 复制: Alice
  注册:   Alice, 20 岁, 92.5 分
```

Let's verify: the delegating constructor `Student("Bob")` first calls `Student("Bob", 18, 0.0)` (which prints "参数化构造" first), then runs its own body (printing "委托构造"). The copy constructor fires correctly in both scenarios.

## Try It Yourself

### Exercise 1: The Date Class

Write a `Date` class with three members: `year_`, `month_`, and `day_`. It should provide a default constructor (initializing to 2000/1/1), a parameterized constructor (taking year, month, and day, with basic validity checks—month 1-12, day 1-31), and a `print()` method. To verify: construct several date objects, including an invalid one (say, month 13), and watch whether the validation logic kicks in.

::: details Reference Solution

```cpp
#include <iostream>

class Date
{
private:
    int year_;
    int month_;
    int day_;

public:
    // Default constructor
    Date() : year_(2000), month_(1), day_(1)
    {
        std::cout << "[默认构造] "
                  << year_ << "-" << month_ << "-" << day_
                  << std::endl;
    }

    // Parameterized constructor
    Date(int year, int month, int day)
        : year_(year), month_(month), day_(day)
    {
        // Check the month first
        if (month_ < 1 || month_ > 12)
        {
            year_ = 2000;
            month_ = 1;
            day_ = 1;

            std::cout << "错误：月份必须在 1~12 之间\n";
            return;
        }

        // Determine the valid day range based on the month
        int max_day = 0;

        switch (month_)
        {
        // Months with 31 days
        case 1:
        case 3:
        case 5:
        case 7:
        case 8:
        case 10:
        case 12:
            max_day = 31;
            break;

        // Months with 30 days
        case 4:
        case 6:
        case 9:
        case 11:
            max_day = 30;
            break;

        // February
        case 2:
            // Leap year: 29 days
            if ((year_ % 400 == 0) ||
                (year_ % 4 == 0 && year_ % 100 != 0))
            {
                max_day = 29;
            }
            else
            {
                max_day = 28;
            }
            break;
        }

        // Validate the day
        if (day_ < 1 || day_ > max_day)
        {
            year_ = 2000;
            month_ = 1;
            day_ = 1;

            std::cout << "错误："
                      << year << "-" << month << "-" << day
                      << " 不是合法日期\n";

            return;
        }

        std::cout << "[有参构造] "
                  << year_ << "-" << month_ << "-" << day_
                  << std::endl;
    }

    // Print the date
    void print() const
    {
        std::cout << year_ << "-"
                  << month_ << "-"
                  << day_
                  << std::endl;
    }
};

int main()
{
    Date d1;
    d1.print();

    Date d2(2023, 5, 15);
    d2.print();

    Date d3(2023, 13, 15);
    d3.print();

    Date d4(2023, 5, 32);
    d4.print();

    // February has 28 days
    Date d5(2023, 2, 28);
    d5.print();

    // 2023 is not a leap year, so Feb 29 is invalid
    Date d6(2023, 2, 29);
    d6.print();

    // 2024 is a leap year, so Feb 29 is valid
    Date d7(2024, 2, 29);
    d7.print();

    // April has only 30 days
    Date d8(2024, 4, 31);
    d8.print();

    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17 -Wall -Wextra main.cpp -o main && ./main
```

Output:

```text
[默认构造] 2000-1-1
2000-1-1
[有参构造] 2023-5-15
2023-5-15
错误：月份必须在 1~12 之间
2000-1-1
错误：2023-5-32 不是合法日期
2000-1-1
[有参构造] 2023-2-28
2023-2-28
错误：2023-2-29 不是合法日期
2000-1-1
[有参构造] 2024-2-29
2024-2-29
错误：2024-4-31 不是合法日期
2000-1-1
```

> When the input date is invalid, we reset `year_`, `month_`, and `day_` to `2000`, `1`, and `1` respectively, bringing the object back to the same date as the default constructor (January 1, 2000), so we don't keep and print an invalid date that would mislead readers.

:::

### Exercise 2: The Vector3D Class

Write a `Vector3D` class with three `double` members: `x_`, `y_`, and `z_`. Use a delegating constructor so that the default constructor delegates to `Vector3D(0.0, 0.0, 0.0)`, then implement the copy constructor and a `length()` method returning the vector's magnitude. To verify: create a default vector, a custom vector, and a copied vector, then print their values and magnitudes.

::: details Reference Solution

```cpp
#include <cmath>
#include <iostream>

class Vector3D
{
private:
    double x_;
    double y_;
    double z_;

public:
    // Parameterized constructor
    Vector3D(double x, double y, double z)
        : x_(x), y_(y), z_(z)
    {
        std::cout << "[参数化构造] "
                  << "x = " << x_
                  << ", y = " << y_
                  << ", z = " << z_
                  << std::endl;
    }

    // Delegating constructor
    // Delegates to Vector3D(0.0, 0.0, 0.0)
    Vector3D()
        : Vector3D(0.0, 0.0, 0.0)
    {
        std::cout << "[委托构造] 使用默认值 (0.0, 0.0, 0.0)"
                  << std::endl;
    }

    // Copy constructor
    Vector3D(const Vector3D& other)
        : x_(other.x_), y_(other.y_), z_(other.z_)
    {
        std::cout << "[拷贝构造]" << std::endl;
    }

    // Return the vector's magnitude
    double length() const
    {
        return std::sqrt(x_ * x_ +
                         y_ * y_ +
                         z_ * z_);
    }

    // Print the vector
    void print() const
    {
        std::cout << "三维向量 Vector3D("
                  << x_ << ", "
                  << y_ << ", "
                  << z_ << ")"
                  << std::endl;
    }
};

int main()
{
    std::cout << "===== 创建默认三维向量 =====" << std::endl;

    Vector3D v1;
    v1.print();

    std::cout << "模 = " << v1.length() << std::endl;

    std::cout << "\n===== 创建指定三维向量 =====" << std::endl;

    Vector3D v2(1.0, 2.0, 3.0);
    v2.print();

    std::cout << "模 = " << v2.length() << std::endl;

    std::cout << "\n===== 创建拷贝向量 =====" << std::endl;

    Vector3D v3(v2);
    v3.print();

    std::cout << "模 = " << v3.length() << std::endl;

    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17 -Wall -Wextra main.cpp -o main && ./main
```

Output:

```text
===== 创建默认三维向量 =====
[参数化构造] x = 0, y = 0, z = 0
[委托构造] 使用默认值 (0.0, 0.0, 0.0)
三维向量 Vector3D(0, 0, 0)
模 = 0

===== 创建指定三维向量 =====
[参数化构造] x = 1, y = 2, z = 3
三维向量 Vector3D(1, 2, 3)
模 = 3.74166

===== 创建拷贝向量 =====
[拷贝构造]
三维向量 Vector3D(1, 2, 3)
模 = 3.74166
```

:::
