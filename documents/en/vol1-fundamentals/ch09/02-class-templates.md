---
chapter: 9
cpp_standard:
- 11
- 14
- 17
- 20
description: Master class template definitions, member functions, and template parameters
  by implementing a generic stack.
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
  source_hash: a56347edd2ddb3e60b438a6ab19b885bcd3e477bc8adc32ce72f94c8089950b9
  translated_at: '2026-09-27T03:51:34+00:00'
  engine: anthropic
  token_count: 6500
---
# Fooled You—There's One More Level: Class Templates

In the last article, we finally, after much grunting and straining, learned function templates—though put that way it sounds rather unglamorous. Let me use it as a quick review: we successfully marched our functions into the era of generic programming, and we can now write functions that accept parameters of arbitrary types, and even return arbitrary types.

The problem is, programming isn't only about algorithms—there are data structures too! What if we want a generalized "data structure"? Take a stack: its push, pop, and top operations follow exactly the same logic for every type, yet internally the stack has to store a group of elements of one same type, and that "type" is decided the moment we write the class. What lets the C++ Standard Library offer flexible containers like `std::vector<int>` and `std::vector<std::string>` is the class template. And that is the star of this article! It lets us parameterize types at the level of an entire class: member variables, member functions, even nested types can all use template parameters.

## Basic Class Template Syntax

We define a class template starting with `template <typename T>`, immediately followed by the class definition. Everywhere `T` appears gets replaced by the actual type at instantiation, including member variables, member function parameters, return types, even friend declarations.

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

Notice that `data_` has type `std::vector<T>`—a template nested inside a template, which is extremely common in C++. At instantiation, `data_` of `Stack<int>` is `std::vector<int>`, and `data_` of `Stack<std::string>` is `std::vector<std::string>`.

When we use a class template, we must supply concrete template arguments (the C++17 CTAD case comes later):

```cpp
Stack<int> int_stack;           // T = int
Stack<double> double_stack;     // T = double
Stack<std::string> str_stack;   // T = std::string
```

Tap the desk, kids—here is an important difference from function templates: a function template's argument types can usually be deduced from the call arguments, but a class template's cannot. When instantiating an object, the compiler has no way to deduce `T` from the constructor (before C++17), so you must dutifully write out `Stack<int>` yourself.

## Defining Member Functions Inside and Outside the Class

Member functions of a class template can be defined directly inside the class body, or outside of it. Defining them inside the class body works just like in an ordinary class—nothing special. But defining them outside the class demands care: every member function defined outside the class body must carry the complete template header.

We can simply write the simple member functions directly inside the class body; that is also the most common practice:

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

When defining outside the class, we need `Stack<T>::` to qualify which class the member function belongs to, and the function must be preceded by the template header `template <typename T>`. Every member function defined outside the class needs this treatment—not a single one can be skipped:

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

The `<T>` in `Stack<T>::` cannot be omitted: `Stack` by itself is a template; only `Stack<T>` is a concrete class. With multiple template parameters—say `template <typename T, typename Alloc>`—an out-of-class definition must be written as `Stack<T, Alloc>::`, and the template header must be complete as well.

## The Three Kinds of Template Parameters

C++'s template system supports three kinds of parameters: type parameters, non-type parameters, and template template parameters. This section covers the first two.

### Type Parameters—The Form You've Been Using All Along

`typename T` (or `class T`) is a type parameter, and there can be several of them:

```cpp
template <typename Key, typename Value>
class Dictionary
{
    // ...
};
```

`std::map<Key, Value>` follows exactly this pattern.

### Non-Type Parameters—Compile-Time Constants

A non-type template parameter is a compile-time constant value, not a type. The most common use is specifying a container's capacity:

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

`kCapacity` participates directly in the array declaration, so at instantiation we must supply a value that is known at compile time:

```cpp
RingBuffer<int, 16> buffer;        // an int ring buffer with capacity 16
RingBuffer<double, 256> big_buf;   // a double ring buffer with capacity 256
```

Non-type parameters can only be integral types, enumerations, pointers, references, or—since C++20—floating-point and class types. In most cases, integral types are all we need.

### Default Template Parameters—From Right to Left

Template parameters also support default values, supplied consecutively from right to left:

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

Note that the Standard Library's `std::stack` follows exactly this design: its second parameter defaults to `std::deque<T>` and can be swapped for `std::vector<T>` or `std::list<T>`.

## A Quick Look at CTAD—Letting the Compiler Deduce Template Parameters (C++17)

C++17 introduced CTAD (Class Template Argument Deduction), which lets the compiler automatically deduce the template argument types from the constructor arguments. The most common examples: `std::vector v = {1, 2, 3}` is deduced as `std::vector<int>`, and `std::pair p(1, 2.5)` is deduced as `std::pair<int, double>`. For class templates we write ourselves, CTAD also works as long as the constructor arguments uniquely determine the template argument types. That said, CTAD's deduction rules are fairly intricate, and the results sometimes differ from what you would expect. At our beginner stage, just knowing this feature exists is enough; when in doubt, play it safe and write the template arguments out explicitly.

## Game Time—Implementing a Complete Generic Stack

Now let's pull everything together and implement a complete generic stack. Storage is a `std::vector<T>` underneath, and we provide five operations: push, pop, top, empty, and size. All the code goes into one header file—template code must live in headers, and we'll explain why shortly.

```cpp
// stack.hpp
// Compile: g++ -Wall -Wextra -std=c++17 stack_demo.cpp -o stack_demo
#pragma once

#include <stdexcept>
#include <vector>

/// @brief A generic stack stored on top of std::vector
/// @tparam T Element type
template <typename T>
class Stack
{
public:
    /// @brief Push an element onto the top of the stack
    void push(const T& value) { data_.push_back(value); }

    /// @brief Pop the top element off the stack
    /// @throws std::out_of_range Thrown when the stack is empty
    void pop()
    {
        if (data_.empty()) {
            throw std::out_of_range("Stack::pop(): stack is empty");
        }
        data_.pop_back();
    }

    /// @brief Access the top element (mutable)
    /// @throws std::out_of_range Thrown when the stack is empty
    T& top()
    {
        if (data_.empty()) {
            throw std::out_of_range("Stack::top(): stack is empty");
        }
        return data_.back();
    }

    /// @brief Access the top element (read-only)
    /// @throws std::out_of_range Thrown when the stack is empty
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

All the operations delegate to the internal `std::vector<T>`. `pop` and `top` throw `std::out_of_range` when the stack is empty, which differs from the Standard Library's `std::stack`—there, operating on an empty stack is undefined behavior (UB). We chose to throw exceptions so that errors are easier to catch.

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

### Verifying the Run

The complete code is right below—hit "Try It Yourself" to run it directly, no terminal needed (the online version merges `stack.hpp` and `stack_demo.cpp` into a single file):

<OnlineCompilerDemo
  title="Hands-On Practice: stack_demo.cpp"
  source-path="code/examples/vol1/18_class_templates.cpp"
  description="Run the generic Stack online and check it against the checkpoints below. Try pushing a few more elements, or deliberately call top on an empty stack to see what the exception looks like."
  allow-run
/>

Let's verify the key results: after pushing three elements onto `Stack<int>`, top is `30` (the last one pushed), and after one pop, top becomes `20`—correct. `Stack<double>` and `Stack<std::string>` also behave the way LIFO (last in, first out) expects. Calling `pop` on an empty stack correctly throws `std::out_of_range`.

## Three Hidden Pitfalls of Templates

When writing class templates, there are three pitfalls that almost every C++ programmer has stepped in. Let's take them apart one by one.

**Template declarations and implementations must live in header files.** You may have noticed that we put all of `Stack`'s declaration and implementation into the `stack.hpp` header instead of splitting it into `.hpp` and `.cpp`. That is not laziness—it is dictated by C++'s compilation model. Each `.cpp` file is compiled independently; when the compiler works on one translation unit, it only needs to see declarations to get through compilation, and the concrete implementations are left to be resolved at link time. Templates are different—a template is not code itself; it is a "recipe for code." The compiler must see the template's complete definition before it can instantiate concrete code from it. If you put the declaration in a `.h` and the implementation in a `.cpp`, other translation units instantiating `Stack<int>` see only the declaration and cannot find the implementation, so linking fails with an `undefined reference` error. The most common practice is simply to put all the code in the header. If you genuinely want to separate declaration from implementation, you can use explicit instantiation: write `template class Stack<int>;` in the `.cpp` file to force the compiler to generate all of `Stack<int>`'s member functions within that translation unit. But then the template only supports the types we explicitly listed, and the generic flexibility is gone.

**Template error messages are long and foul.** Because template instantiation happens at compile time, an error inside template code makes the compiler stuff the entire expanded context into the error message. A simple type mismatch can produce hundreds of lines of diagnostics. C++20 Concepts improve this situation considerably—they let us attach constraints to template parameters, and the error message then tells us directly "which constraint was not satisfied" instead of "somewhere in this giant instantiation chain, an operator did not match." We won't cover Concepts until later, though; for now, when you hit a template error, read the last line first, find our own calling code, and work the types out backwards.

**Code bloat.** If we instantiate `Stack` with 10 different types, the compiler generates 10 complete copies of the code, each containing the full implementations of `push`, `pop`, `top`, `empty`, and `size`. For a small class template this is usually not a problem, but for large templates or embedded platforms, the growth in code size can be unacceptable. Mitigation strategies include extracting the code that does not depend on the template parameter into a non-template base class, using `if constexpr` to branch at compile time and cut down redundant instantiations, and controlling which versions get compiled through explicit instantiation at the library level.

## Exercises

### Exercise 1: Implement Pair\<T, U\>

Implement a generic `Pair` class template that stores two values of different types. It should provide `first()` and `second()` accessors (both const and non-const versions), plus a `swap(Pair& other)` member function that exchanges the contents of two `Pair` objects. Test it with `Pair<int, std::string>` and `Pair<double, char>`. Hint: a class template can take multiple type parameters, written as `template <typename T, typename U>`.

### Exercise 2: Implement RingBuffer\<T, N\>

Implement a ring buffer class template that uses the non-type template parameter `std::size_t kCapacity` to specify the capacity. It should provide `push(const T&)` to write an element, `pop()` to read and remove the oldest written element, `full() const` and `empty() const` to query the state, and `size() const` to return the current element count. Store the data in an `std::array<T, kCapacity>` underneath and track positions with two indices (read and write). The core idea of a ring buffer is to use the modulo operation `% kCapacity` so that indices wrap from the end of the array back around to the front.
