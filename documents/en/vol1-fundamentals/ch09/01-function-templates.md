---
chapter: 9
cpp_standard:
- 11
- 14
- 17
- 20
description: Master the syntax of `template<typename T>`, instantiation mechanisms,
  and type deduction, and learn how to write generic functions.
difficulty: intermediate
order: 1
platform: host
prerequisites:
- OOP 实战
reading_time_minutes: 16
tags:
- cpp-modern
- host
- intermediate
- 进阶
title: Function Template
translation:
  source: documents/vol1-fundamentals/ch09/01-function-templates.md
  source_hash: fe91740ae144c93cd244068e786b5e136c862f0a4d83f8442533a27c3bc1a72e
  translated_at: '2026-06-16T03:47:17.921514+00:00'
  engine: anthropic
  token_count: 2875
---
# Function Templates

Let's say we want to write a `max` function that accepts two values and returns the larger one. The logic is straightforward—just two lines of code. But if our program needs to compare `int`, `double`, and `float` simultaneously, we would need to write three versions: one `max_int`, one `max_double`, and one `max_float`. The logic of all three versions is identical—`return a > b ? a : b`—and the only difference is the parameter type.

This kind of repetitive code—"same logic, different types"—is everywhere in real-world projects. Sorting, searching, swapping, printing arrays—almost every generic operation encounters this. C++ provides a mechanism that allows us to write the logic only once, and then the compiler automatically generates the corresponding function versions for different types. This is the function template. Starting from this chapter, we officially enter the world of C++ generic programming.

> **Learning Objectives**
>
> After completing this chapter, you will be able to:
>
> - [ ] Write generic functions using `template <typename T>` syntax
> - [ ] Understand the template instantiation mechanism—the difference between implicit and explicit instantiation
> - [ ] Master type deduction rules, knowing when deduction fails and how to resolve it
> - [ ] Understand the basic concept of template specialization
> - [ ] Make reasonable choices between function overloading and templates

## `template<typename T>`—The Start of Generic Programming

Let's start with the simplest example and write a generic `my_max` function (we don't call it `max` because `max` already exists in the standard library; using the same name can easily cause conflicts on some compilers—especially on Windows where `windows.h` defines a `max` macro, which is truly blood-pressure-raising).

```cpp
#include <iostream>

// Define a simple function template
template <typename T>
T my_max(T a, T b) {
    return (a > b) ? a : b;
}

int main() {
    int a = 10, b = 20;
    std::cout << my_max(a, b) << std::endl; // Output: 20

    double x = 3.14, y = 2.71;
    std::cout << my_max(x, y) << std::endl; // Output: 3.14
    return 0;
}
```

`template <typename T>` tells the compiler: this is a template, and `T` is a type parameter. In the function definition that follows, every occurrence of `T` will be replaced by an actual type upon instantiation. When we call `my_max(int, int)`, the compiler deduces `T` as `int` and generates an `int` version of the function. Calling `my_max(double, double)` generates a `double` version. The entire process is transparent to the caller.

### What is the difference between `typename` and `class`

In a template parameter list, `typename` and `class` are completely equivalent—`template <typename T>` and `template <class T>` express the same meaning with no semantic difference. Early C++ only supported the `class` keyword; later, `typename` was introduced to eliminate the misconception that "T must be a class." `T` can be any type—built-in types (`int`, `double`, pointers), custom classes, or even function pointers. Modern C++ style prefers `typename` as it is semantically more accurate and clearer to read.

### Multiple Type Parameters

In some scenarios, one type parameter isn't enough. For example, if we want to write a function that converts a value of one type to another:

```cpp
template <typename To, typename From>
To cast_to(From f) {
    return static_cast<To>(f);
}

int main() {
    double d = 3.14;
    int i = cast_to<int>(d); // Explicitly specify To as int, From is deduced as double
    std::cout << i << std::endl;
}
```

There is no upper limit on the number of template parameters, but in real projects, having more than two or three is rare. The more type parameters you add, the more likely the caller will need to specify them explicitly, and code readability decreases.

## Template Instantiation—The Compiler "Writes Code" For You

A template itself is not code—it is a "code recipe." Only when you actually call the template function does the compiler "expand" the template into a specific function definition based on the types of the arguments passed. This process is called template instantiation. (It feels a bit like a macro, doesn't it? If I recall correctly, its original purpose was exactly that!)

```cpp
my_max<int>(10, 20);   // Generates void my_max(int, int)
my_max<double>(1.5, 2.5); // Generates void my_max(double, double)
```

With the two calls above, the compiler generates two completely independent functions. They exist separately in the compiled binary file, just like hand-writing two overloaded functions. This is also the core cost of templates—code bloat. If you instantiate the same template with 20 different types, the compiler will generate 20 copies of the function code. For small functions, this isn't an issue, but for large templates (like full specializations of certain STL algorithms), the code size can increase significantly.

### Implicit Instantiation vs Explicit Instantiation

The method described above, where "the compiler automatically deduces types based on call arguments and generates code," is called implicit instantiation, and it is the most common way. However, sometimes we need to explicitly tell the compiler which type to use; this is explicit instantiation:

```cpp
int main() {
    int a = 10;
    double b = 3.14;

    // Error! Deduction conflict: T cannot be both int and double
    // std::cout << my_max(a, b) << std::endl;

    // OK: Explicitly specify T as double, 'a' is converted to double
    std::cout << my_max<double>(a, b) << std::endl;
}
```

Here `a` is `int`, and `b` is `double`. The types differ, so the compiler cannot deduce `T` as both `int` and `double` simultaneously—we will discuss this deduction conflict in detail in the next section. By adding `<double>` after the function name, we explicitly specify the type of `T`. The compiler will implicitly convert `a` to `double` and then call the `double` version.

There is also a rarer syntax—explicit instantiation definition—which forces the compiler to generate code for a specific version here, even if the current compilation unit doesn't use it:

```cpp
template int my_max(int, int); // Explicitly instantiate the int version
```

This syntax is occasionally used in library development: putting the template implementation in a `.cpp` file and then explicitly instantiating the type versions the library needs to export. This way, user code doesn't need to see the template implementation. However, in daily application development, we almost never need to write explicit instantiation definitions manually.

## Type Deduction—How the Compiler Guesses `T`

When calling `my_max(a, b)`, the compiler sees that the arguments `a` and `b` are both `int`, so it deduces `T` as `int`. This process is called template argument deduction. Deduction happens at compile time and incurs no runtime overhead.

The rules for deduction are simple to state: every template parameter must be uniquely determined. If the same `T` appears in multiple parameters, the types of these parameters—after removing references and top-level `const`—must be exactly the same, otherwise deduction fails.

### Typical Scenarios for Deduction Failure

```cpp
template <typename T>
T my_max(T a, T b) {
    return (a > b) ? a : b;
}

int main() {
    int a = 10;
    double b = 3.14;

    // Error: Deduction failed
    // T deduced as int from 'a', but deduced as double from 'b'
    auto val = my_max(a, b);
}
```

This code will error directly. The reason is that `a`'s type is `int`, so the compiler deduces `T` as `int`. `b`'s type is `double`, so the compiler deduces `T` as `double`. The same `T` cannot be equal to both `int` and `double` simultaneously; the deduction contradicts itself.

> **Pitfall Warning**: Error messages for template deduction failures are usually very long. The compiler will list all overloads and template candidates it tried, then tell you "none matched." For beginners, this dozens-of-lines error message is quite discouraging. The solution is to locate the last line of the error message—it usually points out exactly which parameter's type doesn't match. Then trace back from the call site and check if the types of each argument are consistent.

There are three ways to resolve deduction conflicts. The first is to explicitly specify the template argument, like `my_max<double>(a, b)` we just saw, forcing `T` to be `double`, and `a` will be implicitly converted. The second is to manually convert the argument type: `my_max(a, static_cast<int>(b))`. The third is to modify the template itself to use two independent type parameters—though this approach requires care, which we will discuss shortly.

### The Trap of Two Type Parameters

One might think: since `my_max(a, b)` with `int` and `double` causes a deduction conflict, let's just use two type parameters.

```cpp
template <typename T, typename U>
// auto my_max(T a, U b) { // Problem: What is the return type?
//     return (a > b) ? a : b;
// }

// Better approach: use 'auto' for return type deduction
auto my_max(T a, U b) {
    return (a > b) ? a : b;
}
```

The problem lies with the return type—if `T` is `int` and `U` is `double`, is the return value `int` or `double`? Using `auto` lets the compiler deduce it itself. In C++, the ternary operator follows specific type deduction rules: `a` and `b` will be promoted to a common type, so the return value is `double`. However, this only works for simple cases. In more complex scenarios, you might need `std::common_type` to get the common type of two types:

```cpp
#include <type_traits>

template <typename T, typename U>
typename std::common_type<T, U>::type my_max(T a, U b) {
    return (a > b) ? a : b;
}
```

`std::common_type` is defined in `<type_traits>` and selects the most appropriate common type based on the implicit conversion rules of the two types. However, honestly, in daily use, when encountering mixed-type comparisons, the simplest way is still to explicitly specify one type or manually cast; no need to make it so complex.

## Template Specialization—When the Generic Solution Doesn't Fit

The `my_max` we wrote works fine for most types, but for `const char*` (C-style strings), it compares the addresses of the two pointers, not the content of the strings. This behavior is obviously not what we want.

Template specialization allows us to provide a specific implementation for a particular type:

```cpp
// Generic version
template <typename T>
T my_max(T a, T b) {
    return (a > b) ? a : b;
}

// Full specialization for const char*
template <>
const char* my_max<const char*>(const char* a, const char* b) {
    return (strcmp(a, b) > 0) ? a : b;
}
```

`template <>` indicates this is a full specialization—all template parameters are determined. When calling `my_max`, if the compiler deduces `T` as `const char*`, it will prioritize using the specialized version over the generic version.

Specialization is a large topic involving partial specialization, SFINAE, C++20 `concepts`, and more. Here we only need to know of its existence and basic syntax—we will discuss it in depth in the class templates chapter.

## Function Overloading vs Templates—When to Use Which

Function overloading and function templates can both achieve "same function name handling different types," but their mechanisms are completely different. Function overloading involves manually writing a version for each type, and the compiler selects the best match based on argument types. Function templates involve writing a generic "recipe," and the compiler automatically generates the corresponding version based on the call.

The principle of choice is actually quite intuitive: if the processing logic for all types is exactly the same, and only the types differ, use a template—one `my_max` template is much cleaner than 20 manually written overloaded functions. If the processing logic for different types differs fundamentally—for example, printing an `int` outputs the number directly, while printing a `char*` requires quotes—then use overloading, where each version's logic is independent and clear.

### Overload Resolution When Mixing Them

Templates and overloading can coexist. The compiler has a set of deterministic overload resolution rules: first, it collects all candidate functions (including normal overloads and template-instantiated versions), then sorts them based on the precision of type matching, and selects the best match. If multiple candidates have the same match score, an ambiguity error occurs.

```cpp
void print(int i) {
    std::cout << "Int: " << i << std::endl;
}

template <typename T>
void print(T t) {
    std::cout << "Generic: " << t << std::endl;
}

int main() {
    print(100);      // Calls void print(int) - non-template is preferred
    print<>(100);    // Calls template version - empty <> forces template usage
    print(3.14);     // Calls template version (no int overload exists)
}
```

When both a normal overload and a template instantiation exist, if the match degree is the same, the non-template function takes precedence over the template instantiation version. If you want to force the use of the template, you can use empty angle brackets `<>`.

> **Pitfall Warning**: When mixing overloads and templates, the easiest pitfall is ambiguity. Suppose you write a template `template <typename T> void foo(T, int)` and an overload `void foo(int, int)`, then call `foo(10, 10)`. The compiler will find that the template can be deduced as `foo<int, int>` (the second parameter `int` matches exactly), while the overload version is also an exact match (`int` and `int`). The match degrees are similar, so it reports an ambiguity error. The solution is to keep interfaces simple—if you use a template, don't add overloads for the same interface with subtly different parameter types.
>
> **Pitfall Warning**: Another common pitfall is the interaction between templates and C-style strings. When calling `my_max("hello", "world")`, `T` is deduced as `const char*`. If you haven't written a specialized version for `const char*`, it compares pointer addresses rather than string content. The result depends entirely on where the strings are located in memory—it might differ every run, and it's almost certainly not the result you expect.

## Practical Exercise—func_template.cpp

Now let's synthesize all the knowledge we learned and write a complete example program. It includes generic `my_max`, `swap`, and `print_array` functions, instantiated with `int`, `double`, and `const char*`.

```cpp
#include <iostream>
#include <cstring> // For strcmp
#include <algorithm> // For std::swap (though we will write our own)

// Generic max function
template <typename T>
T my_max(T a, T b) {
    return (a > b) ? a : b;
}

// Specialization for C-style strings
template <>
const char* my_max<const char*>(const char* a, const char* b) {
    return (strcmp(a, b) > 0) ? a : b;
}

// Generic swap function
template <typename T>
void my_swap(T& a, T& b) {
    T temp = a;
    a = b;
    b = temp;
}

// Generic print array function
// Uses array reference to deduce size automatically
template <typename T, std::size_t N>
void print_array(const T (&arr)[N]) {
    for (std::size_t i = 0; i < N; ++i) {
        std::cout << arr[i];
        if (i < N - 1) {
            std::cout << ", ";
        }
    }
    std::cout << std::endl;
}

int main() {
    // 1. Test my_max
    int i1 = 10, i2 = 20;
    std::cout << "Max int: " << my_max(i1, i2) << std::endl;

    double d1 = 1.1, d2 = 2.2;
    std::cout << "Max double: " << my_max(d1, d2) << std::endl;

    const char* s1 = "Apple";
    const char* s2 = "Banana";
    std::cout << "Max string: " << my_max(s1, s2) << std::endl;

    // 2. Test my_swap
    std::cout << "Before swap: " << i1 << ", " << i2 << std::endl;
    my_swap(i1, i2);
    std::cout << "After swap: " << i1 << ", " << i2 << std::endl;

    // 3. Test print_array
    int ints[] = {1, 2, 3};
    double doubles[] = {1.1, 2.2, 3.3};
    const char* strings[] = {"A", "B", "C"};

    std::cout << "Int array: ";
    print_array(ints);

    std::cout << "Double array: ";
    print_array(doubles);

    std::cout << "String array: ";
    print_array(strings);

    return 0;
}
```

Let's break down a few key points. `print_array` uses an array reference parameter `const T (&arr)[N]`. This not only allows the compiler to deduce the array element type `T` but also deduces the array length `N`, so there's no need to pass an extra length argument.

`my_swap`'s parameters are references `T&`, which is necessary to modify the caller's variables. If the parameters were passed by value as `T`, only copies would be swapped, leaving the caller completely unaffected.

### Verify Execution

Compile and run the program:

```bash
g++ -std=c++20 func_template.cpp -o func_template
./func_template
```

Expected output:

```text
Max int: 20
Max double: 2.2
Max string: Banana
Before swap: 10, 20
After swap: 20, 10
Int array: 1, 2, 3
Double array: 1.1, 2.2, 3.3
String array: A, B, C
```

Check a few key results: `my_max` correctly returns `20`; `my_max("Apple", "Banana")` takes the `const char*` specialization path, compares lexicographically, "Banana" is greater than "Apple" so it returns "Banana"; `my_swap` correctly swaps the values before and after; `print_array` correctly prints the contents of three different type arrays without extra trailing commas.

## Exercises

### Exercise 1: Generic Search

Implement a generic function `find_index` that searches for a value in an array and returns its index; if not found, return `-1`. The function signature is roughly:

```cpp
template <typename T, std::size_t N>
int find_index(const T (&arr)[N], T value) {
    // Your code here
}
```

Test with `int`, `double`, and `const char*` types. Think about it: if `T` is a custom class, can this function work normally? What conditions must the custom class satisfy?

### Exercise 2: Generic Sort

Implement a simple generic bubble sort function `bubble_sort` to sort an array in place. You don't need to implement comparison logic yourself—directly use `my_max` or the `>` operator. It should be able to sort `int`, `double`, and `const char*` arrays and print the results.

### Exercise 3: Generic Accumulator

Implement a generic function `accumulate` that calculates the sum of all elements in an array. Think about the return type issue: if the array elements are `int`, the sum might exceed the `int` range, how should this be handled? Hint: You can add a template parameter as the accumulator type.

## Summary

In this chapter, we learned the core mechanism of C++ function templates. `template <typename T>` allows us to write logic only once, and the compiler automatically generates corresponding function versions for different types based on calls. Template instantiation happens at compile time with no runtime overhead, but it produces code bloat. Type deduction requires that the same template parameter be deduced as the same type in all positions where it appears; otherwise, deduction fails—at this point, you can use explicit template arguments, type conversion, or multiple type parameters to resolve it. Template specialization allows us to provide specialized implementations for specific types, making up for the shortcomings of the generic solution.

A few key takeaways: `typename` and `class` are equivalent in template parameter lists, but `typename` is semantically clearer; pay attention to ambiguity when mixing overloads and templates; `my_max` compares pointer addresses rather than string content for C-style strings, so either write a specialization or use `std::string`.

In the next chapter, we enter class templates—extending generic capabilities from functions to entire classes. Function templates let us write "type-independent functions," while class templates let us write "type-independent classes." Containers (`std::vector`, `std::list`), smart pointers (`std::unique_ptr`, `std::shared_ptr`), and even `std::array` are essentially class templates. Understanding function templates makes learning class templates much smoother—the core idea is the same, just the scope expands from functions to classes.
