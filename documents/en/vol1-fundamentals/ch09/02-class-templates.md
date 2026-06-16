---
chapter: 9
cpp_standard:
- 11
- 14
- 17
- 20
description: Master class template definitions, member functions, and template parameters
  to implement a generic stack.
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 函数模板
reading_time_minutes: 13
tags:
- cpp-modern
- host
- intermediate
- 进阶
title: Class Template
translation:
  source: documents/vol1-fundamentals/ch09/02-class-templates.md
  source_hash: 9f35e699566499c3b2edaf6115e9b2f24bc89e9aaebf29bdb65d86f2e9e60c1f
  translated_at: '2026-06-16T03:46:10.921082+00:00'
  engine: anthropic
  token_count: 2558
---
# Class Templates

In the previous chapter, we learned how to use `template <typename T>` to make functions generic—one function body handles various types. However, function templates can only generalize "a piece of logic." What if we want a generic "data structure"? For example, a stack—the logic for `push`, `pop`, and `top` is exactly the same for all types, but the stack internally needs to store a set of elements of the same type. This "type" is determined when we write the class. The reason the C++ Standard Library can provide flexible containers like `std::vector` and `std::map` is due to class templates. This is the protagonist of our chapter! It allows us to parameterize types at the class level—member variables, member functions, and even nested types can all use template parameters. In this chapter, we will clarify the syntax of class templates, how to define member functions, the types of template parameters, and finally, implement a complete generic stack by hand.

> **Learning Objectives**
>
> After completing this chapter, you will be able to:
>
> - [ ] Use `template <typename T>` syntax to define class templates
> - [ ] Define member functions of template classes both inside and outside the class
> - [ ] Distinguish between type parameters and non-type parameters, and master the use of default template arguments
> - [ ] Understand the basic concepts of C++17's CTAD (Class Template Argument Deduction)
> - [ ] Implement a complete `Stack` generic stack

## Step 1 — Understanding Basic Class Template Syntax

The definition of a class template begins with `template <typename T>`, followed immediately by the class definition. Everywhere `T` appears will be replaced by the actual type upon instantiation—including member variables, member function parameters, return types, and even friend declarations.

```cpp
template <typename T>
class Stack {
private:
    T data[100]; // T is the element type
    int top_index;
public:
    void push(const T& value);
    T pop();
    // ...
};
```

`std::vector` is a `template <typename T>` type—a template within a template, which is very common in C++. Upon instantiation, the `T` of `std::vector<int>` is `int`, and the `T` of `std::vector<std::string>` is `std::string`.

When using a class template, you must provide specific template arguments (CTAD scenarios in C++17 will be discussed shortly):

```cpp
Stack<int>         int_stack;       // A stack for integers
Stack<std::string> string_stack;    // A stack for strings
```

Knock, knock, kids. Here is an important difference from function templates: a function template's argument types can usually be deduced from the call arguments, but class templates cannot—when instantiating an object, the compiler cannot deduce `T` from the constructor (prior to C++17), so you must explicitly write `Stack<int>`.

## Step 2 — Handling Inside and Outside Member Function Definitions

Member functions of a class template can be defined directly inside the class body or outside the class body. Defining inside is just like a normal class, nothing special. But defining outside requires attention—every member function defined outside the class must carry the complete template header.

Simple member functions can be written directly inside the class; this is the most common approach:

```cpp
template <typename T>
class Stack {
    // ...
    void push(const T& value) {
        data[++top_index] = value;
    }
};
```

When defining outside, you must use `Stack::` to qualify the class to which the member function belongs, and the function must be preceded by the template header `template <typename T>`. Every member function defined outside needs to do this, without exception:

```cpp
template <typename T>
T Stack::pop() {
    if (top_index < 0) {
        throw std::out_of_range("Stack is empty");
    }
    return data[top_index--];
}
```

The `` in `Stack::` cannot be omitted—because `Stack` itself is a template, only `Stack` is a specific class. If there are multiple template parameters, like `template <typename K, typename V>`, the outside definition must write `HashMap::`, and the template header must be included completely.

## Step 3 — Getting to Know the Three Faces of Template Parameters

C++'s template system supports three kinds of parameters: type parameters, non-type parameters, and template template parameters. In this section, we look at the first two.

### Type Parameters — The Form You've Been Using

`typename T` (or `class T`) is a type parameter, and there can be multiple:

```cpp
template <typename Key, typename Value>
class Map {
    // Key is the key type, Value is the value type
};
```

`std::map` is exactly this pattern.

### Non-Type Parameters — Compile-Time Constants

A non-type template parameter is a compile-time constant value, not a type. The most common use is specifying container capacity:

```cpp
template <typename T, std::size_t N>
class Array {
    T data[N]; // N directly participates in the array declaration
    // ...
};
```

When instantiating, a value known at compile-time must be provided:

```cpp
Array<int, 10>   a1; // Array of 10 integers
Array<double, 5> a2; // Array of 5 doubles
```

Non-type parameters can only be integers, enumerations, pointers, references, or (since C++20) floating-point numbers and class types. In most cases, integers are sufficient.

### Default Template Parameters — Right to Left

Template parameters also support default values, provided continuously from right to left:

```cpp
template <typename T, typename Allocator = std::allocator>
class List {
    // ...
};
```

The standard library's `std::vector` follows this design—the second parameter defaults to `std::allocator`, but can be swapped for a custom allocator or a pool allocator.

## Quick Look at CTAD — Letting the Compiler Deduce Template Arguments (C++17)

C++17 introduced CTAD (Class Template Argument Deduction), allowing the compiler to automatically deduce template argument types based on constructor arguments. The most common examples: `std::vector v{1, 2, 3}` is deduced as `std::vector`, and `std::pair p(42, "hello")` is deduced as `std::pair<int, const char*>`. For class templates we write ourselves, if the constructor arguments can uniquely determine the template argument types, CTAD also works. However, CTAD deduction rules are complex, and sometimes the result differs from expectations. At the beginner stage, just knowing about this feature is enough; when in doubt, explicitly write out the template arguments.

## Game On — Implementing a Complete Generic Stack

Now let's synthesize the previous content to implement a complete generic stack. We use `std::vector` internally for storage and provide five operations: `push`, `pop`, `top`, `empty`, and `size`. All code is written in a single header file—template code must be placed in header files, we will explain why shortly.

```cpp
// stack.hpp
#ifndef STACK_HPP
#define STACK_HPP

#include <vector>
#include <stdexcept>
#include <string>

template <typename T>
class Stack {
private:
    std::vector data;

public:
    // Push an element onto the stack
    void push(const T& value) {
        data.push_back(value);
    }

    // Remove the top element and return it
    T pop() {
        if (empty()) {
            throw std::runtime_error("Stack<>::pop(): empty stack");
        }
        T value = data.back();
        data.pop_back();
        return value;
    }

    // Get the top element
    const T& top() const {
        if (empty()) {
            throw std::runtime_error("Stack<>::top(): empty stack");
        }
        return data.back();
    }

    // Check if the stack is empty
    bool empty() const {
        return data.empty();
    }

    // Get the number of elements
    std::size_t size() const {
        return data.size();
    }
};

#endif // STACK_HPP
```

All operations are delegated to the internal `std::vector`. `pop` and `top` throw a `std::runtime_error` exception when the stack is empty, which differs from the standard library `std::stack` behavior—the standard library defines this as undefined behavior (UB) on an empty stack. We chose to throw exceptions to make errors easier to detect.

Next, we write a test program, instantiating `Stack` with three different types:

```cpp
// main.cpp
#include "stack.hpp"
#include <iostream>
#include <string>

int main() {
    // Test 1: Integer stack
    Stack<int> int_stack;
    int_stack.push(10);
    int_stack.push(20);
    int_stack.push(30);

    std::cout << "int_stack top: " << int_stack.top() << std::endl; // 30
    int_stack.pop();
    std::cout << "after pop, top: " << int_stack.top() << std::endl; // 20
    std::cout << "int_stack size: " << int_stack.size() << std::endl; // 2

    // Test 2: String stack
    Stack<std::string> string_stack;
    string_stack.push("Hello");
    string_stack.push("World");

    std::cout << "string_stack top: " << string_stack.top() << std::endl; // World
    string_stack.pop();
    std::cout << "after pop, top: " << string_stack.top() << std::endl; // Hello

    // Test 3: Double stack
    Stack<double> double_stack;
    double_stack.push(3.14);
    double_stack.push(2.71);

    while (!double_stack.empty()) {
        std::cout << "double_stack pop: " << double_stack.pop() << std::endl;
    }

    // Test 4: Exception handling
    try {
        Stack<int> empty_stack;
        empty_stack.pop(); // Should throw
    } catch (const std::runtime_error& e) {
        std::cout << "Caught exception: " << e.what() << std::endl;
    }

    return 0;
}
```

### Verify Execution

Compile and run:

```text
g++ -std=c++20 -Wall -Wextra -pedantic main.cpp -o stack_test
./stack_test
```

Expected output:

```text
int_stack top: 30
after pop, top: 20
int_stack size: 2
string_stack top: World
after pop, top: Hello
double_stack pop: 2.71
double_stack pop: 3.14
Caught exception: Stack<>::pop(): empty stack
```

Check key results: after pushing three elements onto `int_stack`, `top` is `30` (the last one pushed), after one `pop`, `top` becomes `20`, correct. `string_stack` and `double_stack` behavior also matches LIFO (Last In, First Out) expectations. Calling `pop` on an empty stack correctly throws a `std::runtime_error` exception.

## Pitfall Warning — Three Hidden Traps of Templates

When writing class templates, there are three traps that almost every C++ programmer has fallen into. Let's break them down one by one.

**Hidden Trap 1: Template declarations and definitions must be in the header file.** You may have noticed that we put both the declaration and implementation of `Stack` entirely in the `stack.hpp` header file, without splitting them into `.h` and `.cpp`. This isn't laziness—it's dictated by C++'s compilation model. Each `.cpp` file is compiled independently; when processing a compilation unit, the compiler only needs to see the declaration to compile, leaving the specific implementation to be resolved at the linking stage. But templates are different—a template itself is not code, it is a "recipe for code." The compiler must see the template's complete definition to instantiate the specific code. If you put the declaration in `.h` and the implementation in `.cpp`, other compilation units can only see the declaration when instantiating `Stack`, and cannot find the implementation, resulting in an `undefined reference` error at link time. The most common practice is to write all code in the header file. If you really want to separate declaration and implementation, you can use explicit instantiation—write `template class Stack<int>;` in a `.cpp` file to force the compiler to generate all member functions of `Stack<int>` within this compilation unit—but this way, the template can only support the types you explicitly listed, losing generic flexibility.

**Hidden Trap 2: Template error messages are notoriously long.** Because template instantiation happens at compile time, if there is an error inside the template code, the compiler will stuff the complete context after template expansion into the error message. A simple type mismatch can generate hundreds of lines of errors. C++20 Concepts largely improve this problem—they allow you to add constraints on template parameters, and the error message will directly tell you "which constraint was not satisfied" rather than "some operator didn't match in this huge instantiation chain." However, we will cover Concepts later; at this stage, if you encounter template errors, look at the last line first, find your own calling code, and then trace back the types.

**Hidden Trap 3: Code bloat.** If you instantiate `Stack` with 10 different types, the compiler will generate 10 complete copies of the code—each containing the full implementation of `push`, `pop`, `top`, `empty`, and `size`. For small class templates this is usually not a problem, but for large templates or embedded platforms, the growth in code size might be unacceptable. Mitigation strategies include: extracting code that does not depend on template parameters into a non-template base class, using `if constexpr` to branch at compile time to reduce redundant instantiation, and controlling which versions are compiled through explicit instantiation at the library level.

## Exercises

### Exercise 1: Implement `Pair<T, U>`

Implement a generic `Pair` class template that stores two values of different types. Requirements: provide `first()` and `second()` accessors (const and non-const versions), and a `swap()` member function to swap the contents of two `Pair` objects. Test with `Pair<int, double>` and `Pair<std::string, int>`. Hint: class templates can accept multiple type parameters, the syntax is `template <typename T, typename U>`.

### Exercise 2: Implement `RingBuffer`

Implement a ring buffer class template, using the non-type template parameter `N` to specify capacity. Requirements: provide `put` to write an element, `get` to read and remove the earliest written element, `empty` and `full` to check status, and `size` to return the current element count. Use `T data[N]` for underlying storage, and use two indices (read and write) to track positions. The core idea of a ring buffer is to use the modulo operator `% N` to make the index wrap from the end of the array back to the head.

## Summary

In this chapter, we extended generic capabilities from functions to classes. The core syntax of class templates is almost identical to function templates—starting with `template <typename T>`, `T` can appear anywhere a type is needed: member variables, member function parameters, return types, etc. When defining member functions outside the class, you must include the complete template header and qualify with `ClassName`, a pitfall newcomers often trip over. Template parameters are divided into type parameters (`typename T`) and non-type parameters (`std::size_t N`), both can be mixed, and defaults are provided continuously from right to left. When organizing template code, declarations and implementations must be in the header file (or use explicit instantiation), and you must be mindful of code bloat—each instantiation type generates a complete copy of the code.

In the next chapter, we enter template specialization—when a generic solution isn't good enough for certain specific types, how to provide specialized implementations for them. We briefly touched on the concept of specialization in the function template chapter, but class template specialization is more flexible and powerful, supporting partial specialization, which is a core tool for building advanced generic components.
