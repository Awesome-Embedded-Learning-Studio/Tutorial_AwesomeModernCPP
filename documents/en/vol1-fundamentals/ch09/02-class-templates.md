# Class Templates

In the previous chapter, we learned how to use `template <typename T>` to make functions generic — a single `max_value` can handle various types. But function templates only generalize "a piece of logic." What if we want a generic "data structure"? Take a stack, for example — its push, pop, and top operations share the exact same logic across all types, but the stack internally needs to store a set of elements of the same type, and this "type" is determined when we write the class. The reason the C++ standard library can provide flexible containers like `std::vector<int>` and `std::vector<std::string>` comes down to class templates. That is the star of this chapter! Class templates let us parameterize types at the entire class level — member variables, member functions, and even nested types can all use template parameters. In this chapter, we will clarify the syntax of class templates, how to define member functions, the types of template parameters, and finally walk through implementing a complete generic stack step by step.

> **Learning Objectives**
>
> After completing this chapter, you will be able to:
>
> - [ ] Use the `template <typename T>` syntax to define class templates
> - [ ] Define member functions of a class template both inside and outside the class
> - [ ] Distinguish between type parameters and non-type parameters, and master the use of default template arguments
> - [ ] Understand the basic concept of C++17 CTAD (Class Template Argument Deduction)
> - [ ] Implement a complete `Stack<T>` generic stack

## Step One — Understanding the Basic Syntax of Class Templates

A class template definition starts with `template <typename T>`, immediately followed by the class definition. Everywhere `T` appears, it gets replaced with an actual type upon instantiation — including member variables, member function parameters, return types, and even friend declarations.

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

`data_` is a `std::vector<T>` type — a template inside a template, which is very common in C++. Upon instantiation, the `data_` of `Stack<int>` is `std::vector<int>`, and the `data_` of `Stack<std::string>` is `std::vector<std::string>`.

When using a class template, we must provide specific template arguments (we will discuss C++17 CTAD scenarios shortly):

```cpp
Stack<int> int_stack;           // T = int
Stack<double> double_stack;     // T = double
Stack<std::string> str_stack;   // T = std::string
```

Pay attention here, folks. This is an important difference from function templates: a function template's argument types can usually be deduced from the call arguments, but class templates cannot — when instantiating an object, the compiler cannot deduce `T` from the constructor (prior to C++17), so we must explicitly write out `Stack<int>`.

## Step Two — Nailing Down Inside and Outside Class Definitions for Member Functions

Member functions of a class template can be defined directly inside the class body or outside of it. Defining them inside is just like with ordinary classes — nothing special. However, defining them outside requires extra care — every member function defined outside the class body must include the complete template header.

Simple member functions can be written directly inside the class body, which is also the most common approach:

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

When defining outside the class, we need to use `Stack<T>::` to qualify which class the member function belongs to, and the function must be preceded by the template header `template <typename T>`. Every member function defined outside the class must do this — not a single one can be omitted:

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

The `<T>` in `Stack<T>::` cannot be omitted — because `Stack` itself is a template, and only `Stack<T>` is a concrete class. If there are multiple template parameters, such as `template <typename T, typename Alloc>`, the out-of-class definition must be written as `Stack<T, Alloc>::`, and the template header must also be included in full.

## Step Three — Getting to Know the Three Faces of Template Parameters

C++'s template system supports three kinds of parameters: type parameters, non-type parameters, and template template parameters. In this section, we will look at the first two.

### Type Parameters — The Form You Have Been Using All Along

`typename T` (or `class T`) is a type parameter, and there can be multiple ones:

```cpp
template <typename Key, typename Value>
class Dictionary
{
    // ...
};
```

`std::map<Key, Value>` follows this pattern.

### Non-Type Parameters — Compile-Time Constants

A non-type template parameter is a compile-time constant value, rather than a type. The most common use case is specifying container capacity:

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

`kCapacity` directly participates in the array declaration, and a compile-time known value must be provided upon instantiation:

```cpp
RingBuffer<int, 16> buffer;        // 容量为 16 的 int 环形缓冲区
RingBuffer<double, 256> big_buf;   // 容量为 256 的 double 环形缓冲区
```

Non-type parameters can only be integers, enumerations, pointers, references, or — starting in C++20 — floating-point numbers and class types. In most cases, using integers is sufficient.

### Default Template Arguments — Right to Left

Template parameters also support default values, provided continuously from right to left:

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

Stack<int> s1;                                  // Container 默认为 std::vector<int>
Stack<int, std::deque<int>> s2;                 // Container 显式指定为 std::deque<int>
```

The standard library's `std::stack` uses this exact design — the second parameter defaults to `std::vector<T>`, but can be swapped for `std::deque<T>` or `std::list<T>`.

## A Quick Look at CTAD — Letting the Compiler Deduce Template Arguments (C++17)

C++17 introduced CTAD (Class Template Argument Deduction), which lets the compiler automatically deduce template argument types based on constructor arguments. The most common examples: `std::vector v = {1, 2, 3}` is deduced as `std::vector<int>`, and `std::pair p(1, 2.5)` is deduced as `std::pair<int, double>`. For class templates we write ourselves, if the constructor arguments can uniquely determine the template argument types, CTAD works as well. However, CTAD deduction rules are fairly complex, and sometimes the results differ from expectations. At the beginner stage, just be aware of this feature — when in doubt, explicitly write out the template arguments.

## Let's Code — Implementing a Complete Generic Stack

Now let's combine everything we have covered so far and implement a complete generic stack. We will use `std::vector<T>` for underlying storage and provide five operations: push, pop, top, empty, and size. All the code goes in a single header file — template code must be placed in header files, and we will explain why shortly.

```cpp
// stack.hpp
// 编译: g++ -Wall -Wextra -std=c++17 stack_demo.cpp -o stack_demo
#pragma once

#include <stdexcept>
#include <vector>

/// @brief 泛型栈，底层使用 std::vector 存储
/// @tparam T 元素类型
template <typename T>
class Stack
{
public:
    /// @brief 将元素压入栈顶
    void push(const T& value) { data_.push_back(value); }

    /// @brief 弹出栈顶元素
    /// @throws std::out_of_range 栈为空时抛出异常
    void pop()
    {
        if (data_.empty()) {
            throw std::out_of_range("Stack::pop(): stack is empty");
        }
        data_.pop_back();
    }

    /// @brief 访问栈顶元素（可修改）
    /// @throws std::out_of_range 栈为空时抛出异常
    T& top()
    {
        if (data_.empty()) {
            throw std::out_of_range("Stack::top(): stack is empty");
        }
        return data_.back();
    }

    /// @brief 访问栈顶元素（只读）
    /// @throws std::out_of_range 栈为空时抛出异常
    const T& top() const
    {
        if (data_.empty()) {
            throw std::out_of_range("Stack::top(): stack is empty");
        }
        return data_.back();
    }

    /// @brief 判断栈是否为空
    bool empty() const { return data_.empty(); }

    /// @brief 返回栈中元素数量
    std::size_t size() const { return data_.size(); }

private:
    std::vector<T> data_;
};
```

All operations are delegated to the internal `std::vector<T>`. `pop` and `top` throw an `std::out_of_range` exception when the stack is empty, which differs from the standard library's `std::stack` behavior — the standard library exhibits undefined behavior (UB) on an empty stack. We chose to throw exceptions to make errors easier to spot.

Next, we write a test program, instantiating `Stack` with three different types:

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

    // --- 异常测试 ---
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

### Verifying the Output

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

Let's verify the key results: after pushing three elements onto `Stack<int>`, top is `30` (the last one pushed), and after one pop, top becomes `20` — correct. The behavior of `Stack<double>` and `Stack<std::string>` also matches the LIFO (Last-In, First-Out) expectation. Calling `pop` on an empty stack correctly throws an `std::out_of_range` exception.

## Pitfall Warning — Three Hidden Traps of Templates

When writing class templates, there are three traps that almost every C++ programmer has fallen into. Let's break them down one by one.

**Hidden Trap One: Template declarations and implementations must be placed in header files.** You might have noticed that we put the entire declaration and implementation of `Stack` in the `stack.hpp` header file, without splitting it into `.hpp` and `.cpp`. This is not laziness — it is dictated by C++'s compilation model. Each `.cpp` file is compiled independently; when processing a compilation unit, the compiler only needs to see declarations to compile successfully, leaving the actual implementation to be resolved at the linking stage. But templates are different — a template itself is not code; it is a "code recipe." The compiler must see the template's complete definition to instantiate concrete code. If we put the declaration in `.h` and the implementation in `.cpp`, other compilation units instantiating `Stack<int>` would only see the declaration and fail to find the implementation, resulting in `undefined reference` errors at link time. The most common practice is to write all the code in the header file. If we really want to separate declaration and implementation, we can use explicit instantiation — writing `template class Stack<int>;` in a `.cpp` file to force the compiler to generate all member functions of `Stack<int>` within that compilation unit — but this means the template can only support the types we explicitly list, losing its generic flexibility.

**Hidden Trap Two: Template error messages are notoriously long and ugly.** Because template instantiation happens at compile time, if there is an error inside the template code, the compiler will stuff the full context of the expanded template into the error message. A simple type mismatch can produce hundreds of lines of errors. C++20 Concepts largely mitigates this problem — they let us add constraints to template parameters, and the error messages will directly tell us "which constraint was not satisfied" instead of "some operator didn't match in this giant instantiation chain." However, we will cover Concepts later. At this stage, when encountering template errors, look at the last line first, find our own calling code, and then trace the types backward.

**Hidden Trap Three: Code bloat.** If we instantiate `Stack` with 10 different types, the compiler will generate 10 complete copies of the code — each containing the full implementations of `push`, `pop`, `top`, `empty`, and `size`. For small class templates, this is usually not a problem, but for large templates or on embedded platforms, the growth in code size might be unacceptable. Mitigation strategies include: extracting code that does not depend on template parameters into a non-template base class, using `if constexpr` for compile-time branching to reduce redundant instantiations, and controlling which versions get compiled through explicit instantiation at the library level.

## Exercises

### Exercise 1: Implement Pair\<T, U\>

Implement a generic `Pair` class template that stores two values of different types. It must provide `first()` and `second()` accessors (both const and non-const versions), as well as a `swap(Pair& other)` member function to swap the contents of two `Pair` objects. Test it with `Pair<int, std::string>` and `Pair<double, char>` respectively. Hint: a class template can accept multiple type parameters, written as `template <typename T, typename U>`.

### Exercise 2: Implement RingBuffer\<T, N\>

Implement a ring buffer class template using the non-type template parameter `std::size_t kCapacity` to specify capacity. It must provide `push(const T&)` to write an element, `pop()` to read and remove the earliest written element, `full() const` and `empty() const` to check status, and `size() const` to return the current number of elements. Use `std::array<T, kCapacity>` for underlying storage, and track positions with two indices (read and write). The core idea of a ring buffer is to use the modulo operation `% kCapacity` to wrap indices from the end of the array back to the beginning.

## Summary

In this chapter, we extended generic capabilities from functions to classes. The core syntax of class templates is almost identical to function templates — starting with `template <typename T>`, and `T` can appear anywhere a type is needed, such as in member variables, member function parameters, and return types. When defining member functions outside the class, we must include the complete template header and qualify them with `ClassName<T>::` — this is the pitfall beginners fall into most often. Template parameters are divided into type parameters (`typename T`) and non-type parameters (`std::size_t N`), which can be mixed together, with default values provided continuously from right to left. When organizing template code, declarations and implementations must be placed in header files (or use explicit instantiation), and we must also be mindful of code bloat — each instantiated type generates a complete copy of the code.

In the next chapter, we will dive into template specialization — when a generic solution is not good enough for certain specific types, how do we provide specialized implementations for them? We briefly touched on the concept of specialization in the function template chapter, but class template specialization is more flexible and powerful, supporting partial specialization, which is a core tool for building advanced generic components.
