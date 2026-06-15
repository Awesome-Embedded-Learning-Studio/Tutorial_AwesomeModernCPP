---
chapter: 6
cpp_standard:
- 11
- 14
- 17
- 20
description: Understand the essence of the `this` pointer, master the chaining pattern,
  and learn the correct usage of `const` member functions.
difficulty: beginner
order: 6
platform: host
prerequisites:
- 友元
reading_time_minutes: 11
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: '`this` Pointer and Method Chaining'
translation:
  engine: anthropic
  source: documents/vol1-fundamentals/ch06/06-this-and-cascading.md
  source_hash: a8aa1a6a4a9b014bfc0998aaf6c183d3228f8811cb4f3772d2cd2868c4f4cee1
  token_count: 2234
  translated_at: '2026-05-26T10:51:40.177155+00:00'
---
# The `this` Pointer and Method Chaining

So far, the classes we have written share an unspoken understanding—member functions "know" which object they are operating on. Calling `led.on()` operates on `led`; calling `other_led.on()` operates on `other_led`. The same function behaves differently depending on the object calling it. This might seem obvious, but the underlying mechanism is worth exploring: how exactly does the compiler let a function "know" who the caller is?

The answer is the `this` pointer. Every non-static member function has a hidden parameter at the lower level, pointing to the object that called the function. In this chapter, we will thoroughly understand what `this` is, how it works, and how to use it to write elegant method chaining code.

## Every Member Function Has a Hidden Parameter

When we write code like this:

```cpp
class Point {
    int x_;
    int y_;
public:
    void set_x(int x) { x_ = x; }
};

Point p;
p.set_x(42);
```

The compiler doesn't just see `set_x(42)`. It actually translates this call into something like the following form (pseudocode for understanding):

```cpp
// 伪代码：编译器的内部视角
Point::set_x(&p, 42);  // 把 p 的地址作为第一个参数传入
```

Inside the body of `set_x`, this hidden parameter is `this`—a pointer to the current object. Therefore, `x_ = x` is essentially equivalent to `this->x_ = x`, except that the compiler usually omits the `this->` prefix for us. Once you understand this, many seemingly "magical" behaviors make perfect sense. When the same `set_x` function is called by `p` versus `q`, the fundamental difference is the `this` passed in—one points to `p`, and the other points to `q`.

## The Type of `this` and Explicit Usage

The type of `this` is `ClassName* const`—a constant pointer to the current object. The `const` qualifier modifies the pointer itself, not the object it points to, meaning you cannot change where `this` points (for example, `this = &other_obj` is illegal), but you can modify the object's members through `this`.

In most cases, we do not need to explicitly write `this` because the compiler automatically resolves member names to `this->成员名`. However, in two scenarios, explicitly using `this` is necessary or helpful.

The first scenario is when **parameter names conflict with member variable names**. Frankly, this practice is quite common in C++—many engineers prefer to give constructor parameters the same names as member variables, relying on position to distinguish them in the initializer list. But if you are assigning values inside the function body, you must use `this` to resolve the ambiguity:

```cpp
class Point {
    int x_;
    int y_;
public:
    // 初始化列表中，括号外的 x_ 是成员，括号内的 x_ 是参数
    Point(int x_, int y_) : x_(x_), y_(y_) {}

    void set_x(int x_) {
        this->x_ = x_;  // this->x_ 是成员，裸 x_ 是参数
    }
};
```

> **Pitfall Warning**: If you write `x_ = x_` in a member function without adding `this->`, some compilers might not issue a warning—it will assume both `x_` refer to the parameter itself, turning the assignment into "assigning a value to itself." A safer approach is to add a consistent suffix or prefix to member variables (such as `x_` or `m_x`) to avoid naming conflicts from the root.

The second scenario is **returning `*this`**—which is exactly the foundation of method chaining, and we will focus on this next.

## The Relationship Between `const` Member Functions and `this`

Before diving into method chaining, we must clarify the relationship between `const` member functions and `this`, because this is a pitfall where beginners easily trip up. When we declare a `const` member function, the compiler internally changes the type of `this` from `Point* const` to `const Point* const`—not only is the pointer itself immutable, but the object it points to is also immutable. So if you try to modify a member variable inside a `const` member function, the compiler will directly throw an error.

This leads to a very important consequence: **a `const` object can only call `const` member functions**. If you pass an object to a function via a `const` reference, you can only call its methods marked with `const`:

```cpp
void print_point(const Point& p)
{
    std::cout << p.get_x() << std::endl;  // OK，get_x() 是 const 的
    // p.set_x(10);  // 编译错误！set_x() 不是 const 的
}
```

> **Pitfall Warning**: Forgetting to add `const` to a getter is one of the most frequent mistakes made by C++ newcomers. You write a `int get_x() { return x_; }` that "looks like it just reads data," but without the `const` qualifier, the compiler assumes it might modify the object. The result is that anyone holding the object through a `const` reference cannot call this getter, and the error message is usually nonsense like "discards qualifiers," leaving beginners completely baffled. My advice is: after writing each member function, ask yourself, "Does it need to modify the object?" If the answer is no, add `const` immediately.

## Method Chaining—Making Interfaces Flow

The core idea of method chaining is simple: a member function returns a reference to `*this`, allowing the caller to consecutively call multiple methods in a single statement.

Let's first look at a `Point` class without method chaining to feel the pain point:

```cpp
class Point {
    int x_;
    int y_;
public:
    Point() : x_(0), y_(0) {}

    void set_x(int x) { x_ = x; }
    void set_y(int y) { y_ = y; }
};

// 每个 setter 都是独立的语句
Point p;
p.set_x(3);
p.set_y(4);
```

Four lines of code do four things, which looks okay. But if the number of setters increases—for example, a `Config` class has over a dozen configuration options—repeatedly writing the object name becomes pure manual labor. Switching to method chaining requires only one change: change the return type from `void` to `ClassName&`, and `return *this;` at the end of the function:

```cpp
class Point {
    int x_;
    int y_;
public:
    Point() : x_(0), y_(0) {}

    Point& set_x(int x)
    {
        x_ = x;
        return *this;
    }

    Point& set_y(int y)
    {
        y_ = y;
        return *this;
    }

    Point& print()
    {
        std::cout << "(" << x_ << ", " << y_ << ")" << std::endl;
        return *this;
    }
};

// 现在一行搞定
Point p;
p.set_x(3).set_y(4).print();
```

Let's break down the principle: `p.set_x(3)` returns a reference to `p`, so the immediately following `.set_y(4)` is equivalent to calling `set_y` on `p`; `set_y` also returns a reference to `p`, so `.print()` is still called on `p`. The entire chain is strung together, with each step operating on the same object.

In practice, this pattern is used extensively in real-world engineering. The `std::cout` in the C++ standard library is the most classic example—`operator<<` returns `std::ostream&`, so we can write `std::cout << "a" << "b" << "c";`. Hardware configuration interfaces and logging systems in embedded development also frequently use method chaining to make code more compact.

> **Pitfall Warning**: In method chaining, if a method returns a value instead of a reference (for example, accidentally writing `StringBuilder append(...)` instead of `StringBuilder& append(...)`), the chain will still compile—but each step in the chain will operate on a new copy, not the original object. The result is that all preceding calls are completely wasted, and only the result of the last method is preserved. This bug is very subtle because the code "looks" correct, the compiler doesn't complain, but the runtime results are just wrong. Remember: method chaining must return a **reference**.

## Hands-on Practice: StringBuilder and Config Builder

Now let's combine everything we discussed above and write a complete, compilable file. It contains two classes—a `StringBuilder` that concatenates strings through method chaining, and a `Config` that constructs configurations using the Builder pattern.

```cpp
#include <cstdio>
#include <cstring>

class StringBuilder {
    char buffer_[256];
    std::size_t length_;

public:
    StringBuilder() : length_(0) { buffer_[0] = '\0'; }

    StringBuilder& append(const char* str)
    {
        while (*str && length_ < 255) {
            buffer_[length_++] = *str++;
        }
        buffer_[length_] = '\0';
        return *this;
    }

    StringBuilder& append_char(char c)
    {
        if (length_ < 255) {
            buffer_[length_++] = c;
            buffer_[length_] = '\0';
        }
        return *this;
    }

    // const 成员函数：只读取，不修改
    const char* c_str() const { return buffer_; }
    std::size_t length() const { return length_; }
};
```

Both `append` and `append_char` return `StringBuilder&`, so they can be chained. Meanwhile, `c_str()` and `length()` are read-only operations, marked with `const`, so they can also be called through a `const` reference. Next is the `Config` and its Builder—the Builder pattern is one of the most classic applications of method chaining. When we need to construct a configuration object with many options, it keeps the code both clear and compact:

```cpp
class Config {
    char name_[64];
    int baudrate_;
    bool use_parity_;
    int timeout_ms_;

    // 私有构造，强制通过 Builder 创建
    Config(const char* name, int baud, bool parity, int timeout)
        : baudrate_(baud), use_parity_(parity), timeout_ms_(timeout)
    {
        std::strncpy(name_, name, 63);
        name_[63] = '\0';
    }

public:
    class Builder {
        char name_[64];
        int baudrate_;
        bool use_parity_;
        int timeout_ms_;

    public:
        Builder() : baudrate_(9600), use_parity_(false), timeout_ms_(1000)
        {
            name_[0] = '\0';
        }

        Builder& set_name(const char* name)
        {
            std::strncpy(name_, name, 63);
            name_[63] = '\0';
            return *this;
        }

        Builder& set_baudrate(int baud)
        {
            baudrate_ = baud;
            return *this;
        }

        Builder& set_parity(bool parity)
        {
            use_parity_ = parity;
            return *this;
        }

        Builder& set_timeout(int ms)
        {
            timeout_ms_ = ms;
            return *this;
        }

        Config build() const
        {
            return Config(name_, baudrate_, use_parity_, timeout_ms_);
        }
    };

    void print() const
    {
        std::printf("Config: name=%s, baud=%d, parity=%s, timeout=%dms\n",
                    name_, baudrate_,
                    use_parity_ ? "yes" : "no",
                    timeout_ms_);
    }
};
```

Note that the `Config` constructor is `private`—external code cannot directly create a `Config` object; it must be built step by step through a `Config::Builder()`. Each setter returns `Builder&`, and finally calling `build()` produces a complete `Config`. Let's run it:

```cpp
int main()
{
    // StringBuilder 链式调用
    StringBuilder sb;
    sb.append("Hello")
          .append(", ")
          .append("this ")
          .append("is ")
          .append("a ")
          .append("chain!")
          .append_char('\n');

    std::printf("--- StringBuilder ---\n");
    std::printf("%s", sb.c_str());
    std::printf("Total length: %zu\n\n", sb.length());

    // Config Builder 链式调用
    Config cfg = Config::Builder()
                     .set_name("UART1")
                     .set_baudrate(115200)
                     .set_parity(false)
                     .set_timeout(500)
                     .build();

    std::printf("--- Config Builder ---\n");
    cfg.print();

    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17 -Wall -Wextra -o this_demo this_demo.cpp && ./this_demo
```

Expected output:

```text
--- StringBuilder ---
Hello, this is a chain!
Total length: 24

--- Config Builder ---
Config: name=UART1, baud=115200, parity=no, timeout=500ms
```

You can compile and run it yourself to confirm that every link in the chain is indeed operating on the same object. If you want to verify further, you can add a line of `std::printf("this = %p\n", (void*)this);` in each method, and you will find that the addresses printed throughout the chain are completely identical—they are operating on the exact same object.

## The Difference Between `*this` and `this`

Finally, let's clarify a question that beginners often confuse. `this` is a pointer, while `*this` is a reference to the current object. If you want a function to return the current object itself, the way to write it is:

```cpp
// 返回对当前对象的引用
Point& set_x(int x)
{
    x_ = x;
    return *this;  // 解引用 this 指针，得到对象的引用
}
```

If you write `return this;`, the return type must be `Point*`—the caller receives a pointer, and subsequent calls must use `->` instead of `.`, completely destroying the fluid feel of method chaining. Although `p.set_x(3)->set_y(4)->print()` can also work, the style is inconsistent and clashes with standard library conventions (e.g., `std::cout` uses `.`, not `->`). Therefore, the standard method chaining pattern is always `return *this;` paired with the return type `ClassName&`.

## Exercises

1. **Implement a `Rectangle` class with chained setters**. Requirements: provide two chained methods, `set_width(int)` and `set_height(int)`, and a `area() const` that returns the area. Write a test snippet to verify that the result of `rect.set_width(3).set_height(4).area()` is 12.

2. **Implement a simple `QueryBuilder`**. Requirements: build a SQL query string through method chaining—`select("id, name").from("users").where("age > 18").build()` should return `"SELECT id, name FROM users WHERE age > 18"`. Hint: use the `StringBuilder` approach internally to maintain a character buffer, and have each chained method append the corresponding SQL fragment to it.

## Summary

In this chapter, we broke down the underlying mechanism of the `this` pointer—every non-static member function has a hidden `this` parameter pointing to the object that called the function. A `const` member function turns `this` into a pointer to a constant, thereby prohibiting object modification at compile time. The method chaining pattern links multiple method calls together by returning a reference to `*this`, and this pattern is heavily used in the Builder pattern and operator overloading. At this point, we have covered all the basics of OOP. In the next chapter, we will dive into operator overloading—seeing how to make custom types support operators like `+`, `==`, and `<<` just like built-in types.
