---
chapter: 6
cpp_standard:
- 11
- 14
- 17
- 20
description: Understand the essence of the this pointer, and master method chaining and the correct use of const member functions
difficulty: beginner
order: 6
platform: host
prerequisites:
- Friends
reading_time_minutes: 15
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: The this Pointer and Method Chaining
translation:
  source: documents/vol1-fundamentals/ch06/06-this-and-cascading.md
  source_hash: f7379be8d6c45f9d501377c249c4c1f6417a388745e73a4753d339ea0be0f279
  translated_at: '2026-09-25T11:13:37+00:00'
  engine: anthropic
  token_count: 3000
---
# Let's Talk About the this Pointer and Method Chaining

Congratulations! You have made it this far into object-oriented programming! Take a sip of water and celebrate your outstanding perseverance!

All right, back to reality. The classes we write all share an unspoken agreement—member functions "know" which object they are working on. Call `led.on()`, and `on()` works on `led`; call `other_led.on()`, and `on()` works on `other_led`. The same function, called on different objects, behaves differently. No, wait—come back! So how does code living inside a class know which object, stamped out from that very class, it is supposed to access?

The answer is the `this` pointer. Under the hood, every non-static member function has a hidden parameter that points to the object the function was called on. When the compiler compiles your C++ code, it secretly prepends one extra parameter to every non-static member function: the object of that class itself.

Huh? Not following? No rush—let me walk you through it step by step.

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

What the compiler sees is not simply `set_x(42)`. It actually translates the call into something like this (pseudocode, to build intuition):

```cpp
// Pseudocode: the compiler's internal view
Point::set_x(&p, 42);  // p's address is passed in as the first argument
```

Oh-ho! If you come from C, that clicked instantly, didn't it? When we simulated OOP in C, this is exactly **the trick we played—passing the object itself through our own function pointers**. That's right: we were clumsily simulating `this`! Haha!

Inside the body of `set_x`, this hidden parameter is `this`—a pointer to the current object. So `x_ = x` is actually equivalent to `this->x_ = x`; it is just that most of the time the compiler omits the `this->` prefix for us. Once this clicks, a lot of seemingly "magical" behavior starts to make sense. The essential difference between the same `set_x` function being called on `p` versus on `q` is nothing more than which `this` gets passed in: one points to `p`, the other points to `q`.

## The Type of this, and Using It Explicitly

The type of `this` is `ClassName* const`—**a constant pointer to the current object.** The `const` qualifies the pointer itself, not the object it points to, which means we cannot change where `this` points (for example, `this = &other_obj` is illegal), but we can modify the object's members through `this`.

This bit of syntactic sugar is pleasant. Most of the time we don't need to write `this` explicitly—everyone is in perfect tacit understanding, and the compiler automatically resolves **a member name into `this->member_name`**. In two situations, though, using `this` explicitly is necessary or helpful.

Let's look at the first one: **a parameter name collides with a member variable name**. This style is quite common in C++—many engineers like to give constructor parameters the same names as the member variables, and let the initializer list disambiguate by position. But if the assignment happens inside the function body, you must use `this` to resolve the ambiguity:

```cpp
class Point {
    int x_;
    int y_;
public:
    // In the initializer list, x_ outside the parentheses is the member, x_ inside is the parameter
    Point(int x_, int y_) : x_(x_), y_(y_) {}

    void set_x(int x_) {
        this->x_ = x_;  // this->x_ is the member, bare x_ is the parameter; without writing this out explicitly, the compiler throws a warning or even an error, telling you this is self-assignment!
    }
};
```

The other scenario is **returning `*this`**, which is precisely the foundation of method chaining—our main topic next.

## const Member Functions and Their Relationship to this

Before we get to method chaining, we must first straighten out the relationship between `const` member functions and `this`, because this is a pitfall beginners fall into especially easily. When we declare a `const` member function, the compiler internally changes the type of `this` from `Point* const` to `const Point* const`—now not only is the pointer itself unchangeable, so is the object it points to. That is why modifying a member variable inside a `const` member function makes the compiler report an error outright.

This has a very important consequence: **a `const` object can only call `const` member functions**. If we pass an object to a function through a `const` reference, we can only call the methods it has marked `const`:

```cpp
void print_point(const Point& p)
{
    std::cout << p.get_x() << std::endl;  // OK, get_x() is const
    // p.set_x(10);  // Compile error! set_x() is not const
}
```

> Good habit! Remember to add the `const` qualifier to pure getters.
>
> Say we write an `int get_x() { return x_; }`. It "looks like it just reads data", but without the `const` qualifier, the compiler must assume it might modify the object.
>
> The consequence: anyone holding the object through a `const` reference cannot call this getter, and the error message is usually gibberish like "discards qualifiers", which leaves beginners completely lost.
>
> So my advice is: after finishing each member function, ask yourself one question—"does it need to modify the object?" If the answer is no, add `const` immediately.

## Method Chaining: Member Functions Returning *this

This is really just a fun way to write code. But in the projects I have taken part in, quite a lot of people did use it, because it saves us a few keystrokes.

The core idea of this section's topic, **method chaining**, is simple: a member function returns a reference to `*this`, so the caller can invoke several methods in a row within a single statement.

First, let's look at a `Point` class that does not use method chaining, and feel the pain:

```cpp
class Point {
    int x_;
    int y_;
public:
    Point() : x_(0), y_(0) {}

    void set_x(int x) { x_ = x; }
    void set_y(int y) { y_ = y; }
};

// Each setter is its own separate statement
Point p;
p.set_x(3);
p.set_y(4);
```

Four lines of code doing four things—looks acceptable. But once the number of setters grows (**say a `Config` class with a dozen-plus configuration items, where repeatedly writing the object name becomes pure manual labor**), it gets uncomfortable. Converting to method chaining takes a single change: change the return type from `void` to `ClassName&`, and add `return *this;` at the end of the function:

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

// Now it's done in one line
Point p;
p.set_x(3).set_y(4).print();
```

Let's take the mechanism apart: `p.set_x(3)` returns a reference to `p`, so the `.set_y(4)` immediately after it is equivalent to calling `set_y` on `p`; `set_y` in turn returns a reference to `p`, so `.print()` is still called on `p`. The whole chain is strung together, and every step operates on the same object.

In fact, this pattern is used extremely widely in real engineering. `std::cout` from the C++ standard library is the most classic example—`operator<<` returns `std::ostream&`, which is why we can write `std::cout << "a" << "b" << "c";`. In embedded development, hardware configuration interfaces and logging systems also frequently use method chaining to keep code compact.

> In a method chain, if some method returns a value **instead of a reference** (say you accidentally wrote `StringBuilder append(...)` instead of `StringBuilder& append(...)`), the chain still compiles—but every subsequent link in the chain operates on a fresh copy, not the original object. The result is that all the earlier calls go to waste, and only the last method's result is kept. This kind of bug is extremely sneaky, because the code "looks" right and the compiler does not complain either, yet the runtime result is just wrong. Remember this: method chaining must return a **reference**.

## Hands-On: StringBuilder and Config Builder

Now let's combine what we covered earlier and write one complete, compilable file. It contains two classes: a `StringBuilder` that concatenates strings through method chaining, and a `Config` constructed with the Builder pattern.

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

    // const member functions: read-only, no modification
    const char* c_str() const { return buffer_; }
    std::size_t length() const { return length_; }
};
```

Both `append` and `append_char` return `StringBuilder&`, so they can be chained. `c_str()` and `length()`, being read-only operations, carry the `const` qualifier and can be called through a `const` reference too. Next come `Config` and its Builder—the Builder pattern is one of the most classic applications of method chaining; when we need to construct a configuration object with many configuration items, it keeps the code both clear and compact:

```cpp
class Config {
    char name_[64];
    int baudrate_;
    bool use_parity_;
    int timeout_ms_;

    // Private constructor: forces creation through the Builder
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

Note that `Config`'s constructor is `private`—external code cannot create a `Config` object directly; it must be built step by step through `Config::Builder()`. Every setter returns `Builder&`, and the final call to `build()` produces a complete `Config`. Let's run it:

```cpp
int main()
{
    // StringBuilder method chaining
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

    // Config Builder method chaining
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

You can compile and run it yourself to confirm that every link in the chain really does operate on the same object. To verify it further, add a line `std::printf("this = %p\n", (void*)this);` inside each method—you will find that the addresses printed across the whole chain are completely identical: they are all operating on the same object.

## Exercises

### A Rectangle Class with Chainable Setters

Implement a `Rectangle` class with chainable setters: provide two chainable methods, `set_width(int)` and `set_height(int)`, plus an `area() const` that returns the area. Write a piece of test code to verify whether `rect.set_width(3).set_height(4).area()` yields 12.

::: details Reference answer

```cpp
#include <array>
#include <iostream>

class Rectangle {
 private:
  int width_{};
  int height_{};

 public:
  Rectangle() = default;
  Rectangle(int width, int height) : width_(width), height_(height) {}
  Rectangle& set_width(int width) {
    this->width_ = width;
    return *this;
  }
  Rectangle& set_height(int height) {
    this->height_ = height;
    return *this;
  }
  int area() const { return this->width_ * this->height_; }
};
int main()
{
    Rectangle rect{};
    std::cout<<"面积 : "<<rect.set_width(3).set_height(4).area()<<std::endl;
}
```

Compile and run:

```bash
g++ -std=c++17  -Wall -Wextra main.cpp -o main &&./main
```

Output:

```text
面积 : 12
```

:::
