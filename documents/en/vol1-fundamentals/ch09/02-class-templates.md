---
chapter: 9
cpp_standard:
- 11
- 14
- 17
- 20
description: Master class template definitions, member functions, and template parameters by implementing a generic stack.
difficulty: intermediate
order: 2
platform: host
prerequisites:
- Function Templates
reading_time_minutes: 13
tags:
- cpp-modern
- host
- intermediate
- 进阶
title: Class Templates
translation:
  source: documents/vol1-fundamentals/ch09/02-class-templates.md
  source_hash: 1748d938f855763b659fe4dad8f82f5e9ca072901c26f76893ee525316ac951e
  translated_at: '2026-09-25T11:41:48+00:00'
  engine: anthropic
  token_count: 3200
---
# Class Templates: Passing Types as Parameters

In the previous article we learned how `template <typename T>` makes functions generic—a single `max_value` can handle all sorts of types. But a function template can only generalize "a piece of logic." What if we want a generalized "data structure"? Take a stack: its push, pop, and top operations follow exactly the same logic for every type, yet internally the stack has to store a set of elements of one same type, and that "type" is decided at the moment we write the class. The reason the C++ Standard Library can offer flexible containers like `std::vector<int>` and `std::vector<std::string>` is the class template. It is also the star of this chapter! It lets us parameterize types at the level of the whole class: member variables, member functions, even nested types can all use template parameters. In this article we will sort out the syntax of class templates, how member functions are defined, the kinds of template parameters, and finally build a complete generic stack step by step.

## Basic Class Template Syntax

We define a class template starting with `template <typename T>`, followed immediately by the class definition. At instantiation, every place `T` appears gets replaced with the actual type—member variables, member function parameters, return types, even friend declarations.

```cpp
template <typename T>
class Stack
{
public:
    void push(const T& value);
    void pop();
    T& top();
    const T& top() const;
    bool empty() const;
    std::size_t size() const;

private:
    std::vector<T> data_;
};
```

Notice that `data_` has type `std::vector<T>`—a template nested inside a template, which is extremely common in C++. Upon instantiation, `data_` of `Stack<int>` is a `std::vector<int>`, and `data_` of `Stack<std::string>` is a `std::vector<std::string>`.

When we use a class template, we must provide concrete template arguments (the C++17 CTAD case will be discussed shortly):

```cpp
Stack<int> int_stack;           // T = int
Stack<double> double_stack;     // T = double
Stack<std::string> str_stack;   // T = std::string
```

Tap-tap on the desk, kids. Here is an important difference from function templates: a function template's argument types can usually be deduced from the call arguments, but a class template's cannot—when instantiating an object, the compiler has no way to deduce `T` from the constructor (before C++17), so you must dutifully write out `Stack<int>` yourself.

## Defining Member Functions Inside and Outside the Class

Member functions of a class template can be defined directly inside the class body, or outside of it. In-class definitions work exactly like in an ordinary class—nothing special. Out-of-class definitions, though, call for care: every member function defined outside the class body must carry the complete template header.

Simple member functions can be written directly inside the class body; that is the most common practice:

```cpp
template <typename T>
class Stack
{
public:
    bool empty() const { return data_.empty(); }
    std::size_t size() const { return data_.size(); }

private:
    std::vector<T> data_;
};
```

For an out-of-class definition, we qualify the member function's class with `Stack<T>::`, and the function must be preceded by the template header `template <typename T>`. Every member function defined outside the class needs this treatment—not one can be skipped:

```cpp
template <typename T>
void Stack<T>::push(const T& value)
{
    data_.push_back(value);
}

template <typename T>
void Stack<T>::pop()
{
    if (data_.empty()) {
        throw std::out_of_range("Stack<>::pop(): empty stack");
    }
    data_.pop_back();
}

template <typename T>
T& Stack<T>::top()
{
    if (data_.empty()) {
        throw std::out_of_range("Stack<>::top(): empty stack");
    }
    return data_.back();
}

template <typename T>
const T& Stack<T>::top() const
{
    if (data_.empty()) {
        throw std::out_of_range("Stack<>::top(): empty stack");
    }
    return data_.back();
}
```

The `<T>` in `Stack<T>::` cannot be omitted: `Stack` by itself is a template; only `Stack<T>` is a concrete class. With multiple template parameters—say `template <typename T, typename Alloc>`—the out-of-class definition must read `Stack<T, Alloc>::`, and the template header must be carried in full.

## The Three Forms of Template Parameters

C++'s template system supports three kinds of parameters: type parameters, non-type parameters, and template template parameters. This section covers the first two.

### Type Parameters — the Form You Have Been Using All Along

`typename T` (or `class T`) is a type parameter, and there can be several of them:

```cpp
template <typename Key, typename Value>
class Dictionary
{
    // ...
};
```

`std::map<Key, Value>` is exactly this pattern.

### Non-Type Parameters — Compile-Time Constants

A non-type template parameter is a compile-time constant value, not a type. Its most common use is specifying container capacity:

```cpp
template <typename T, std::size_t kCapacity>
class RingBuffer
{
public:
    void push(const T& value)
    {
        buffer_[write_index_] = value;
        write_index_ = (write_index_ + 1) % kCapacity;
    }

    // ...

private:
    std::array<T, kCapacity> buffer_;
    std::size_t write_index_ = 0;
};
```

`kCapacity` takes part directly in the array declaration, so at instantiation we must supply a value known at compile time:

```cpp
RingBuffer<int, 16> buffer;        // An int ring buffer with capacity 16
RingBuffer<double, 256> big_buf;   // A double ring buffer with capacity 256
```

Non-type parameters can only be integral types, enumerations, pointers, references, or—since C++20—floating-point numbers and class types. In most cases, integral types are all we need.

### Default Template Parameters — Right to Left

Template parameters support default values too, supplied consecutively from right to left:

```cpp
template <typename T, typename Container = std::vector<T>>
class Stack
{
public:
    void push(const T& value) { data_.push_back(value); }
    // ...

private:
    Container data_;
};

Stack<int> s1;                                  // Container defaults to std::vector<int>
Stack<int, std::deque<int>> s2;                 // Container explicitly specified as std::deque<int>
```

The standard library's `std::stack` follows exactly this design: its second parameter defaults to `std::vector<T>` and can be swapped for `std::deque<T>` or `std::list<T>`.

## A Quick Look at CTAD — Letting the Compiler Deduce Template Arguments (C++17)

C++17 introduced CTAD (Class Template Argument Deduction), which lets the compiler deduce template argument types automatically from constructor arguments. The most familiar examples: `std::vector v = {1, 2, 3}` is deduced as `std::vector<int>`, and `std::pair p(1, 2.5)` as `std::pair<int, double>`. For class templates we write ourselves, CTAD works too, as long as the constructor arguments uniquely determine the template argument types. That said, CTAD's deduction rules are fairly intricate, and sometimes the result differs from what you expect. At our current beginner stage, simply knowing this feature exists is enough; when in doubt, write the template arguments out in full.

## Game On — Implementing a Complete Generic Stack

Now let's pull everything together and implement a complete generic stack. Storage is a `std::vector<T>` underneath, and we provide five operations: push, pop, top, empty, and size. All the code goes in one header file—template code must live in headers, and we will explain why shortly.

```cpp
// stack.hpp
// Compile: g++ -Wall -Wextra -std=c++17 stack_demo.cpp -o stack_demo
#pragma once

#include <stdexcept>
#include <vector>

/// @brief A generic stack backed by std::vector
/// @tparam T element type
template <typename T>
class Stack
{
public:
    /// @brief Push an element onto the top of the stack
    void push(const T& value) { data_.push_back(value); }

    /// @brief Pop the top element off the stack
    /// @throws std::out_of_range thrown when the stack is empty
    void pop()
    {
        if (data_.empty()) {
            throw std::out_of_range("Stack::pop(): stack is empty");
        }
        data_.pop_back();
    }

    /// @brief Access the top element (mutable)
    /// @throws std::out_of_range thrown when the stack is empty
    T& top()
    {
        if (data_.empty()) {
            throw std::out_of_range("Stack::top(): stack is empty");
        }
        return data_.back();
    }

    /// @brief Access the top element (read-only)
    /// @throws std::out_of_range thrown when the stack is empty
    const T& top() const
    {
        if (data_.empty()) {
            throw std::out_of_range("Stack::top(): stack is empty");
        }
        return data_.back();
    }

    /// @brief Check whether the stack is empty
    bool empty() const { return data_.empty(); }

    /// @brief Return the number of elements in the stack
    std::size_t size() const { return data_.size(); }

private:
    std::vector<T> data_;
};
```

Every operation is delegated to the internal `std::vector<T>`. `pop` and `top` throw an `std::out_of_range` exception when the stack is empty, which differs from the standard library's `std::stack`—there, operating on an empty stack is undefined behavior (UB). We chose to throw exceptions so the errors are easier to surface.

Next, let's write a test program that instantiates `Stack` with three different types:

```cpp
// stack_demo.cpp
#include <iostream>
#include <string>
#include "stack.hpp"

int main()
{
    // --- Stack<int> ---
    std::cout << "=== Stack<int> ===\n";
    Stack<int> int_stack;
    int_stack.push(10);
    int_stack.push(20);
    int_stack.push(30);
    std::cout << "size: " << int_stack.size() << "\n";
    std::cout << "top:  " << int_stack.top() << "\n";
    int_stack.pop();
    std::cout << "after pop, top: " << int_stack.top() << "\n";
    std::cout << "empty: " << std::boolalpha << int_stack.empty()
              << "\n";

    // --- Stack<double> ---
    std::cout << "\n=== Stack<double> ===\n";
    Stack<double> dbl_stack;
    dbl_stack.push(3.14);
    dbl_stack.push(2.718);
    std::cout << "size: " << dbl_stack.size() << "\n";
    std::cout << "top:  " << dbl_stack.top() << "\n";
    dbl_stack.pop();
    std::cout << "after pop, top: " << dbl_stack.top() << "\n";

    // --- Stack<std::string> ---
    std::cout << "\n=== Stack<std::string> ===\n";
    Stack<std::string> str_stack;
    str_stack.push("hello");
    str_stack.push("world");
    str_stack.push("template");
    std::cout << "size: " << str_stack.size() << "\n";
    std::cout << "top:  " << str_stack.top() << "\n";
    str_stack.pop();
    std::cout << "after pop, top: " << str_stack.top() << "\n";

    // --- Exception test ---
    std::cout << "\n=== Exception test ===\n";
    Stack<int> empty_stack;
    try {
        empty_stack.pop();
    } catch (const std::out_of_range& e) {
        std::cout << "caught: " << e.what() << "\n";
    }

    return 0;
}
```

### Verify the Run

```bash
g++ -Wall -Wextra -std=c++17 stack_demo.cpp -o stack_demo && ./stack_demo
```

Expected output:

```text
=== Stack<int> ===
size: 3
top:  30
after pop, top: 20
empty: false

=== Stack<double> ===
size: 2
top:  2.718
after pop, top: 3.14

=== Stack<std::string> ===
size: 3
top:  template
after pop, top: world

=== Exception test ===
caught: Stack::pop(): stack is empty
```

Let's check the key results: after pushing three elements onto `Stack<int>`, top is `30` (the last one pushed), and after one pop, top becomes `20`—correct. `Stack<double>` and `Stack<std::string>` also behave as LIFO (last in, first out) expects. Calling `pop` on an empty stack correctly throws the `std::out_of_range` exception.

## The Three Hidden Traps of Templates

When writing class templates, there are three traps that almost every C++ programmer has stepped in. Let's take them apart one by one.

**Template declarations and implementations must live in header files.** You may have noticed that we put `Stack`'s declaration and implementation entirely into the `stack.hpp` header instead of splitting them into `.hpp` and `.cpp`. That is not laziness—it is forced by C++'s compilation model. Each `.cpp` file is compiled independently; while processing one translation unit, the compiler only needs to see declarations to compile successfully, and the actual implementations are resolved later at link time. Templates are different—a template is not itself code; it is a "recipe for code." The compiler must see the template's complete definition before it can instantiate concrete code. If you put the declaration in `.h` and the implementation in `.cpp`, other translation units instantiating `Stack<int>` see only the declaration and cannot find the implementation, so linking fails with an `undefined reference` error. The most common practice is to write all the code in the header. If you genuinely want to separate declaration and implementation, you can use explicit instantiation: write `template class Stack<int>;` in a `.cpp` file to force the compiler to generate all of `Stack<int>`'s member functions within that translation unit. But then the template only supports the types we explicitly listed, and the generic flexibility is gone.

**Template error messages are notoriously long and noisy.** Template instantiation happens at compile time, so when there is an error inside the template code, the compiler stuffs the entire context of the expanded template into the error message. A simple type mismatch can produce hundreds of lines of diagnostics. C++20 Concepts improve this situation considerably—they let us attach constraints to template parameters, and the error message then tells us directly "which constraint was not satisfied" instead of "some operator mismatched somewhere along this giant instantiation chain." We will not cover Concepts until later, though; for now, when you hit a template error, read the last line first, locate your own calling code, and trace the types backward.

**Code bloat.** If we instantiate `Stack` with 10 different types, the compiler generates 10 complete copies of the code, each containing the full implementations of `push`, `pop`, `top`, `empty`, and `size`. For a small class template this is usually not a problem, but for large templates or embedded platforms, the growth in code size can be unacceptable. Mitigation strategies include: extracting the parts that do not depend on template parameters into a non-template base class, branching at compile time with `if constexpr` to cut down redundant instantiations, and controlling which versions get compiled through explicit instantiation at the library level.

## Exercises

### Exercise 1: Implement Pair\<T, U\>

Implement a generic `Pair` class template that stores two values of different types. It should provide `first()` and `second()` accessors (both const and non-const versions), plus a `swap(Pair& other)` member function that exchanges the contents of two `Pair` objects. Test it with `Pair<int, std::string>` and `Pair<double, char>`. Hint: a class template can accept multiple type parameters—the syntax is `template <typename T, typename U>`.

### Exercise 2: Implement RingBuffer\<T, N\>

Implement a ring buffer class template whose capacity is specified by the non-type template parameter `std::size_t kCapacity`. It should provide `push(const T&)` to write an element, `pop()` to read and remove the earliest written element, `full() const` and `empty() const` to query the state, and `size() const` to return the current element count. Use `std::array<T, kCapacity>` for the underlying storage, and track positions with two indices (read and write). The core idea of a ring buffer is the modulo operation `% kCapacity`, which makes an index wrap from the end of the array back to the head.
