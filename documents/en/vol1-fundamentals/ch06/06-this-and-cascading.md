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
title: this Pointer and Chaining
translation:
  source: documents/vol1-fundamentals/ch06/06-this-and-cascading.md
  source_hash: 488556a2a6418b386f097378b368cbffa1269d0bf37d0bf3e1d142abab8e6e71
  translated_at: '2026-06-16T04:38:48.451432+00:00'
  engine: anthropic
  token_count: 2230
---
# The `this` Pointer and Method Chaining

Until now, the classes we have written have shared an implicit understanding: member functions "know" which object they are operating on. Calling `uart.init()` operates on `uart`; calling `spi.send()` operates on `spi`. The same function behaves differently depending on the object calling it. This might seem obvious, but the underlying mechanism is worth exploring: how exactly does the compiler ensure a function "knows" who called it?

The answer is the `this` pointer. Every non-static member function has a hidden parameter at the low level, pointing to the object that invoked the function. In this chapter, we will thoroughly clarify what `this` is, how it works, and how to leverage it to write elegant method chaining code.

## Every Member Function Has a Hidden Parameter

When we write code like this:

```cpp
uart.init(115200);
```

The compiler sees more than just a simple `init` call. It actually translates this invocation into a form similar to this (pseudo-code for understanding):

```cpp
// What the compiler actually does:
// uart.init(&uart, 115200);
void init(Uart* this, int baudrate) {
    this->configure(baudrate);
}
```

Inside the function body of `init`, this hidden parameter is `this`—a pointer to the current object. Therefore, `configure(baudrate)` is actually equivalent to `this->configure(baudrate)`, though most of the time the compiler omits the `this->` prefix for us. Once we understand this, many seemingly "magical" behaviors become reasonable. The same `init` function, when called by `uart1` versus `uart2`, essentially differs only in the passed `this` pointer—one pointing to `uart1`, the other pointing to `uart2`.

## The Type of `this` and Explicit Usage

The type of `this` is `ClassName* const`—a constant pointer to the current object. The `const` qualifier applies to the pointer itself, not the object it points to. This means you cannot change where `this` points (e.g., `this = &other;` is illegal), but you can modify the object's members through `this`.

In most cases, we do not need to explicitly write `this`, because the compiler automatically resolves member names as `this->member`. However, in two scenarios, explicitly using `this` is either necessary or helpful.

The first scenario is when **parameter names conflict with member variable names**. Honestly, this style is quite common in C++—many engineers like to give constructor parameters the same names as member variables, relying on position in the initialization list to distinguish them. However, if assigning inside the function body, we must use `this` to disambiguate:

```cpp
class Uart {
    int baudrate;
public:
    // 'baudrate' is the parameter, 'this->baudrate' is the member
    void setBaudrate(int baudrate) {
        this->baudrate = baudrate; // Explicit 'this' required here
    }
};
```

> **Warning**: If you write `baudrate = baudrate;` in a member function without adding `this->`, some compilers might not issue a warning—it will assume both `baudrate` references refer to the parameter itself, making the assignment a "self-assignment." A safer approach is to add a uniform suffix or prefix to member variables (like `_baudrate` or `m_baudrate`) to fundamentally avoid naming conflicts.

The second scenario is **returning `*this`**—this is precisely the foundation of method chaining, which we will focus on next.

## The Relationship Between `const` Member Functions and `this`

Before discussing method chaining, we must clarify the relationship between `const` member functions and `this`, as this is a pitfall where beginners often stumble. When we declare a `const` member function, the compiler internally changes the type of `this` from `ClassName* const` to `const ClassName* const`—not only is the pointer itself immutable, but the object it points to is also immutable. Therefore, if you try to modify a member variable inside a `const` member function, the compiler will directly report an error.

This leads to a very important consequence: **`const` objects can only call `const` member functions**. If you pass an object to a function via a `const` reference, you can only call methods marked with `const` on it:

```cpp
void printStatus(const Uart& uart) {
    uart.send("Status"); // Error! 'send' is not const
    uart.getBaudrate();  // OK, 'getBaudrate' is const
}
```

> **Warning**: Forgetting to add `const` to getters is one of the most frequent mistakes for C++ newcomers. You write a `getBaudrate()` that "looks like it just reads data," but without the `const` modifier, the compiler assumes it might modify the object. The result is that anyone holding the object via a `const` reference cannot call this getter. The error message usually involves nonsense like "discards qualifiers," which leaves beginners completely puzzled. The author's advice is: after writing every member function, ask yourself, "Does it need to modify the object?" If the answer is no, add `const` immediately.

## Method Chaining—Making Interfaces Flow

The core idea of method chaining is simple: member functions return a reference to `this`, allowing the caller to continuously call multiple methods in a single statement.

Let's first look at a `Config` class that does not use method chaining to feel the pain:

```cpp
Config cfg;
cfg.setBaudrate(115200);
cfg.setTimeout(100);
cfg.setParity('N');
cfg.apply();
```

Four lines of code do four things, which looks okay. But if the number of setters increases—for example, a `SystemConfig` class has over a dozen configuration items—repeating the object name becomes pure manual labor. Changing to method chaining requires only one modification: change the return type from `void` to `Config&`, and return `*this` at the end of the function:

```cpp
class Config {
public:
    Config& setBaudrate(int rate) {
        baudrate = rate;
        return *this; // Return reference to current object
    }
    Config& setTimeout(int ms) {
        timeout = ms;
        return *this;
    }
    // ... other setters
};

// Usage:
cfg.setBaudrate(115200).setTimeout(100).setParity('N').apply();
```

Let's break down the principle: `setBaudrate` returns a reference to `cfg`, so the subsequent `setTimeout` is equivalent to calling `setTimeout` on `cfg`; `setTimeout` again returns a reference to `cfg`, so `setParity` is still called on `cfg`. The whole chain is strung together, with every step operating on the same object.

In fact, this pattern is used very widely in actual engineering. `std::cout` in the C++ standard library is the most classic example—`operator<<` returns `std::ostream&`, so we can write `std::cout << a << b << c`. Hardware configuration interfaces in embedded development and logging systems also frequently use method chaining to make code more compact.

> **Warning**: In method chaining, if a method returns a value instead of a reference (e.g., accidentally writing `return *this` where the return type is `Config` instead of `Config&`), the method chaining will still compile—but every step in the chain will operate on a new copy, not the original object. The result is that all previous calls are wasted, and only the result of the last method is preserved. This bug is very subtle because the code "looks" right, the compiler doesn't complain, but the runtime result is just wrong. Remember: method chaining must return a **reference**.

## Hands-on: StringBuilder and Config Builder

Now let's synthesize what we discussed and write a complete, compilable file. It contains two classes—a `StringBuilder` that concatenates strings via method chaining, and a `ConfigBuilder` that constructs configurations using the Builder pattern.

```cpp
#include <iostream>
#include <string>
#include <sstream>
#include <memory>

class StringBuilder {
public:
    StringBuilder& append(const std::string& str) {
        buffer << str;
        return *this;
    }

    StringBuilder& appendLine(const std::string& str) {
        buffer << str << "\n";
        return *this;
    }

    // Read-only operation, marked const
    std::string toString() const {
        return buffer.str();
    }

    // Read-only operation, marked const
    size_t length() const {
        return buffer.str().length();
    }

private:
    std::ostringstream buffer;
};
```

`append` and `appendLine` both return `StringBuilder&`, so they can be chained. `toString` and `length` are read-only operations, so they are marked `const` and can be called via a `const` reference. Next is `SystemConfig` and its Builder—the Builder pattern is one of the classic applications of method chaining. When we need to construct a configuration object with many items, it makes the code both clear and compact:

```cpp
class SystemConfig {
    int baudrate = 9600;
    int timeout = 1000;
    bool parity = false;

    // Constructor is private, external code cannot create directly
    SystemConfig() = default;

public:
    // Static factory method
    static SystemConfig create() { return SystemConfig(); }

    // Getters
    int getBaudrate() const { return baudrate; }
    int getTimeout() const { return timeout; }
    bool hasParity() const { return parity; }

    // Builder class
    class Builder {
        SystemConfig config;
    public:
        Builder& setBaudrate(int rate) {
            config.baudrate = rate;
            return *this;
        }
        Builder& setTimeout(int ms) {
            config.timeout = ms;
            return *this;
        }
        Builder& enableParity(bool enable = true) {
            config.parity = enable;
            return *this;
        }
        SystemConfig build() {
            return config;
        }
    };
};
```

Note that `SystemConfig`'s constructor is `private`—external code cannot directly create `SystemConfig` objects; they must be built step-by-step via `Builder`. Each setter returns `Builder&`, and finally calling `build()` produces a complete `SystemConfig`. Let's run it:

```cpp
int main() {
    // 1. StringBuilder example
    StringBuilder sb;
    std::string result = sb.append("Hello ")
                          .append("World ")
                          .appendLine("from C++")
                          .append("Method Chaining!")
                          .toString();
    std::cout << result << std::endl;
    std::cout << "Length: " << sb.length() << std::endl;

    // 2. Builder pattern example
    SystemConfig cfg = SystemConfig::Builder()
                           .setBaudrate(115200)
                           .setTimeout(500)
                           .enableParity(true)
                           .build();

    std::cout << "Baudrate: " << cfg.getBaudrate() << "\n";
    std::cout << "Timeout: " << cfg.getTimeout() << "\n";
    std::cout << "Parity: " << (cfg.hasParity() ? "ON" : "OFF") << "\n";

    return 0;
}
```

Compile and run:

```bash
g++ -std=c++11 -o main main.cpp && ./main
```

Expected output:

```text
Hello World from C++
Method Chaining!
Length: 35
Baudrate: 115200
Timeout: 500
Parity: ON
```

You can compile and run this yourself to confirm that every link in the chain is indeed operating on the same object. If you want to verify further, you can add a line like `std::cout << "this: " << this << std::endl;` in each method. You will find that the addresses printed throughout the chain are completely consistent—they are operating on the exact same object.

## The Difference Between `*this` and `this`

Finally, let's clarify a question beginners often confuse. `this` is a pointer, while `*this` is a reference to the current object. If you want a function to return the current object itself, the syntax is:

```cpp
ClassName& func() {
    return *this; // Returns a reference
}
```

If you write `return this;`, the return type must be `ClassName*`—the caller gets a pointer, and subsequent calls must use `->` instead of `.`, destroying the fluidity of method chaining. Although returning a pointer can work, the style is inconsistent and does not align with standard library conventions (e.g., `std::cout` uses `&` not `*`). Therefore, the standard method chaining pattern is always `*this` paired with the return type `ClassName&`.

## Exercises

1. **Implement a `Rectangle` class with chained setters**. Requirements: provide `setWidth` and `setHeight` chainable methods, and a `getArea` method that returns the area. Write a test snippet to verify that a `3x4` `Rectangle` yields an area of 12.

2. **Implement a simple `SqlBuilder`**. Requirements: build a SQL query string via method chaining—`select`, `where`, `orderBy` should return `SqlBuilder&`. Hint: maintain a character buffer internally using the `StringBuilder` approach, where each chainable method appends the corresponding SQL fragment.

## Summary

In this chapter, we dissected the underlying mechanism of the `this` pointer—every non-static member function has a hidden `this` parameter pointing to the object invoking the function. `const` member functions turn `this` into a pointer to a constant, thereby prohibiting object modification at compile time. The method chaining pattern links multiple method calls together by returning a reference to `*this`. This pattern is heavily used in the Builder pattern and operator overloading. At this point, we have covered all the basics of OOP. In the next chapter, we will enter operator overloading—let's see how to make custom types support operators like `+`, `-`, `[]`, just like built-in types.
