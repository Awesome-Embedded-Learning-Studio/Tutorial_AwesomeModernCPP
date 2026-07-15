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
title: Function Templates
translation:
  source: documents/vol1-fundamentals/ch09/01-function-templates.md
  source_hash: fb05fc4973a14d3375011b085938d01735fa691d0417c41ac5d4a8fdea28b4fa
  translated_at: '2026-07-15T15:18:15.000000+00:00'
  engine: anthropic
  token_count: 2900
---
# Function Templates

Let's say we want to write a `max` function that takes two values and returns the larger one. The logic is dead simple—two lines and you're done. But if our program needs to compare `int`, `double`, and `std::string` at the same time, we'd need three versions: `max(int, int)`, `max(double, double)`, and `max(std::string, std::string)`. All three are identical logic—`(a > b) ? a : b`—differing only in the parameter type.

This kind of "same logic, different types" repetition is everywhere in real code—sorting, searching, swapping, printing arrays, almost every generic operation runs into it. C++ gives us a way to write the logic once and let the compiler generate the right function version for each type automatically. That's the function template. From this chapter on, we're properly in C++ generic programming territory.

## `template<typename T>`—Where Generics Start

Let's start with the simplest example: a generic `max_value` function. (We don't call it `max` because `std::max` already lives in the standard library; reusing the name can cause conflicts on some compilers—especially on Windows, where `<windows.h>` defines a `max` macro, which is the real blood-pressure moment.)

```cpp
template <typename T>
T max_value(T a, T b)
{
    return (a > b) ? a : b;
}
```

`template <typename T>` tells the compiler: this is a template, `T` is a type parameter. In the function definition that follows, every `T` gets replaced with an actual type when instantiated. When we call `max_value(3, 5)`, the compiler deduces `T = int` and generates an `int max_value(int, int)` version. Calling `max_value(1.0, 2.0)` generates the `double max_value(double, double)` version. The whole process is transparent to the caller.

### What's the difference between `typename` and `class`

In a template parameter list, `typename` and `class` are exactly equivalent—`template <typename T>` and `template <class T>` mean the same thing, with no semantic difference. Early C++ only had `class`; `typename` was added later to kill the misconception that "T must be a class." `T` can be anything—built-in types (`int`, `double`, pointers), custom classes, even function pointers. Modern C++ style leans toward `typename`: more accurate semantics, cleaner to read.

### Multiple Type Parameters

Sometimes one type parameter isn't enough. Say we want a function that converts a value of one type into another:

```cpp
template <typename Dest, typename Source>
Dest cast_to(Source value)
{
    return static_cast<Dest>(value);
}
```

There's no upper limit on the number of template parameters, but in real projects more than two or three is rare—each extra type parameter makes it more likely the caller has to specify it explicitly, and readability drops with it.

## Template Instantiation—The Compiler "Writes Code" For You

A template isn't code itself—it's a "code recipe." Only when you actually call the template function does the compiler "expand" it into a concrete function definition based on the argument types. This is template instantiation. (Feels a bit like a macro, doesn't it? If I remember right, that was actually its original purpose!)

```cpp
int x = max_value(3, 5);       // T = int, generates int max_value(int, int)
double y = max_value(1.0, 2.0); // T = double, generates double max_value(double, double)
```

Those two calls make the compiler generate two fully independent functions. Each exists separately in the compiled binary, just like two handwritten overloads. This is also the core cost of templates—code bloat. If you instantiate the same template with 20 different types, you get 20 copies of the function code. For small functions that's fine, but for large templates (like full specializations of some STL algorithms) the code size can grow noticeably.

### Implicit vs Explicit Instantiation

The "compiler deduces types from call arguments and generates code" approach above is called implicit instantiation, and it's the common case. But sometimes we need to tell the compiler explicitly which type to use—that's explicit instantiation:

```cpp
int result = max_value<double>(3, 5.0);  // explicitly specify T = double
```

Here `3` is `int` and `5.0` is `double`—different types, so the compiler can't deduce `T` as both `int` and `double`. We'll dig into this deduction conflict in the next section. By appending `<double>` to the function name, we pin down `T`; the compiler then implicitly converts `3` to `double` and calls the `max_value<double>` version.

There's also a rarer form—the explicit instantiation definition—which forces the compiler to emit a specific version right here, even if the current translation unit never uses it:

```cpp
template int max_value<int>(int, int);           // explicit instantiation definition
template double max_value(double, double);       // same, omitting the template argument list
```

This shows up occasionally in library development: put the template implementation in a `.cpp` file, then explicitly instantiate the type versions the library needs to export, so user code doesn't have to see the template implementation. In day-to-day work, though, we almost never write explicit instantiation definitions by hand.

## Type Deduction—How the Compiler Guesses `T`

When you call `max_value(3, 5)`, the compiler sees that both `3` and `5` are `int`, so it deduces `T = int`. This is template argument deduction. It happens at compile time and costs nothing at runtime.

The rule is simple to state: every template parameter must be uniquely determined. If the same `T` appears in multiple parameters, then after stripping references and top-level `const`, those parameters' types must match exactly—or deduction fails.

### Typical Deduction Failure

```cpp
auto r = max_value(3, 5.0);  // compile error!
```

This errors out directly. `3` is `int`, so the compiler deduces `T = int`; `5.0` is `double`, so it deduces `T = double`. The same `T` can't equal both `int` and `double` at once—deduction contradicts itself.

> **Pitfall**: Error messages from a failed template deduction are usually long. The compiler lists every overload and template candidate it tried, then tells you "none matched." For a beginner, a dozens-of-lines error like that is pretty demoralizing. The fix is to jump to the last line of the message—that usually names the specific parameter whose type didn't match—then work backward from the call site, checking each argument's type.

There are three ways to resolve a deduction conflict. First, specify the template argument explicitly, like the `max_value<double>(3, 5.0)` we just saw—force `T = double`, and `3` gets implicitly converted. Second, manually convert the argument type: `max_value(static_cast<double>(3), 5.0)`. Third, change the template itself to use two independent type parameters—but be careful with this one; we'll get to it shortly.

### The Two-Type-Parameter Trap

You might think: since `int` and `double` conflict in deduction, just use two type parameters.

```cpp
template <typename T, typename U>
???.??? max_value_two(T a, U b)
{
    return (a > b) ? a : b;
}
```

The problem is the return type—if `T` is `int` and `U` is `double`, is the return `int` or `double`? Using `auto` lets the compiler deduce it. In C++, the ternary operator follows its own type deduction rules: `int` and `double` promote to `double`, so the return is `double`. But that only works for simple cases; in trickier situations you may need `std::common_type_t<T, U>` to get the common type:

```cpp
template <typename T, typename U>
auto max_value_two(T a, U b) -> std::common_type_t<T, U>
{
    return (a > b) ? a : b;
}
```

`std::common_type_t` lives in `<type_traits>` and picks the most appropriate common type based on the two types' implicit conversion rules. Honestly, though, for mixed-type comparisons in everyday code, the simplest thing is still to specify one type explicitly or cast manually—no need to overcomplicate it.

## Template Specialization—When the Generic Version Doesn't Fit

Our `max_value` works fine for most types, but for `const char*` (C-style strings) it compares the two pointer addresses instead of the string contents. That's clearly not what we want.

Template specialization lets us provide a dedicated implementation for a specific type:

```cpp
// generic template
template <typename T>
T max_value(T a, T b)
{
    return (a > b) ? a : b;
}

// specialization for const char*
template <>
const char* max_value<const char*>(const char* a, const char* b)
{
    return (std::strcmp(a, b) > 0) ? a : b;
}
```

`template <>` marks a full specialization—all template parameters are pinned. When you call `max_value("hello", "world")` and the compiler deduces `T = const char*`, it picks the specialization over the generic version.

Specialization is a big topic—partial specialization, SFINAE, `concept` constraints, and more. Here we only need to know it exists and what the basic syntax looks like; the class templates chapter goes deeper.

## Function Overloading vs Templates—When to Use Which

Function overloading and function templates both give you "one name, many types," but the mechanisms are totally different. Overloading means handwriting a version per type and letting the compiler pick the best match by argument type. Templates mean writing one generic "recipe" and letting the compiler generate the version from the call.

The choice is mostly intuitive: if the logic is identical across all types and only the types differ, use a template—one `max_value` template is far cleaner than 20 handwritten overloads. If the logic fundamentally differs between types (say `print(int)` just outputs the number, but `print(std::string)` needs quotes), use overloading, where each version's logic stands on its own.

### Overload Resolution When Mixing Them

Templates and overloads can coexist. The compiler has deterministic overload resolution rules: first it gathers every candidate (plain overloads, plus the specialization the compiler generates once deduction succeeds), then ranks them by how precisely the types match, and picks the best. If several candidates tie, that usually means an ambiguity error—with one important exception: when the tie is between a non-template overload and a template specialization, the non-template overload wins, no ambiguity. The next example shows exactly this rule.

```cpp
template <typename T>
T max_value(T a, T b)
{
    return (a > b) ? a : b;
}

// plain overload: int version
int max_value(int a, int b)
{
    std::cout << "int overload\n";
    return (a > b) ? a : b;
}

int main()
{
    max_value(3, 5);       // calls the plain overload (exact match beats the template)
    max_value(1.0, 2.0);   // calls the template instantiation (no double overload)
    max_value<>(3, 5);     // forces the template, skipping the plain overload
}
```

That example demonstrates the rule directly: `max_value(3, 5)` has two exact-match candidates, and the non-template overload wins; `max_value(1.0, 2.0)` only the template can match, so it takes the template; to force the template, add empty angle brackets `max_value<>(3, 5)`.

> **Pitfall**: The easiest trap when mixing overloads and templates is the "silent failure" of template deduction. Say you write a template `template <typename T> T max_value(T, T)` and an overload `double max_value(double, int)`, then call `max_value(1.0, 2)`. You might expect ambiguity, but run it: it compiles, returns `2`, no ambiguity. Template argument deduction **does not** apply implicit conversions just to unify `T`, so `1.0` deduces `T = double` and `2` deduces `T = int`—a conflict, so deduction fails and the template never becomes a candidate. Only the overload `max_value(double, int)` is left, an exact match, and that's what gets called. The real trap comes later: this line compiles only because the overload catches it. The day you refactor that overload away, the same line flips from "works fine" to "deduction conflict, compile error," with an error message dozens of lines long. So when mixing templates and overloads, keep the interface simple—once you have a template, don't add overloads for the same interface with only subtly different parameter types.
>
> **Pitfall**: Another common one is the interaction between templates and C-style strings. When you call `max_value("hello", "world")`, `T` is deduced as `const char*`. If you haven't written a specialization for `const char*`, it compares pointer addresses instead of string contents—the result depends entirely on where the strings land in memory, likely different every run, and almost certainly not what you wanted.

## Hands-On—func_template.cpp

Now let's pull everything together into a complete example. It has three generic functions—`max_value`, `swap_value`, and `print_array`—instantiated with `int`, `double`, and `std::string`.

```cpp
// func_template.cpp
// compile: g++ -Wall -Wextra -std=c++17 func_template.cpp -o func_template

#include <cstring>
#include <iostream>
#include <string>
// ============================================================
// max_value: return the larger of two values
// ============================================================
template <typename T>
T max_value(T a, T b)
{
    return (a > b) ? a : b;
}

// const char* specialization: compare string contents lexicographically
template <>
const char* max_value<const char*>(const char* a, const char* b)
{
    return (std::strcmp(a, b) > 0) ? a : b;
}
// ============================================================
// swap_value: swap two values
// ============================================================
template <typename T>
void swap_value(T& a, T& b)
{
    T temp = a;
    a = b;
    b = temp;
}
// ============================================================
// print_array: print the contents of an array
// ============================================================
template <typename T, std::size_t kSize>
void print_array(const T (&arr)[kSize])
{
    std::cout << "[";
    for (std::size_t i = 0; i < kSize; ++i) {
        std::cout << arr[i];
        if (i + 1 < kSize) {
            std::cout << ", ";
        }
    }
    std::cout << "]";
}
// ============================================================
// main
// ============================================================
int main()
{
    // --- max_value ---
    std::cout << "=== max_value ===\n";
    std::cout << "max_value(3, 7) = " << max_value(3, 7) << "\n";
    std::cout << "max_value(2.5, 1.3) = " << max_value(2.5, 1.3)
              << "\n";
    std::cout << "max_value(\"banana\", \"apple\") = "
              << max_value("banana", "apple") << "\n";

    // explicit instantiation: mixed types
    std::cout << "max_value<double>(3, 5.7) = "
              << max_value<double>(3, 5.7) << "\n";

    // --- swap_value ---
    std::cout << "\n=== swap_value ===\n";
    int a = 10, b = 20;
    std::cout << "before: a=" << a << ", b=" << b << "\n";
    swap_value(a, b);
    std::cout << "after:  a=" << a << ", b=" << b << "\n";

    double x = 1.5, y = 2.5;
    std::cout << "before: x=" << x << ", y=" << y << "\n";
    swap_value(x, y);
    std::cout << "after:  x=" << x << ", y=" << y << "\n";

    std::string s1 = "hello", s2 = "world";
    std::cout << "before: s1=\"" << s1 << "\", s2=\"" << s2 << "\"\n";
    swap_value(s1, s2);
    std::cout << "after:  s1=\"" << s1 << "\", s2=\"" << s2 << "\"\n";

    // --- print_array ---
    std::cout << "\n=== print_array ===\n";
    int nums[] = {3, 1, 4, 1, 5, 9};
    std::cout << "int[]:    ";
    print_array(nums);
    std::cout << "\n";

    double vals[] = {1.1, 2.2, 3.3};
    std::cout << "double[]: ";
    print_array(vals);
    std::cout << "\n";

    std::string names[] = {"Alice", "Bob", "Charlie"};
    std::cout << "string[]: ";
    print_array(names);
    std::cout << "\n";

    return 0;
}
```

A few key points. `print_array` takes an array-reference parameter `const T (&arr)[kSize]`, which lets the compiler deduce not just the element type `T` but also the array length `kSize`, so there's no need to pass a length separately.

`swap_value` takes its parameters by reference `T&`, which is the only way to actually modify the caller's variables. If the parameters were passed by value as `T a, T b`, you'd only swap copies and the caller wouldn't notice a thing.

### Verify It

```bash
g++ -Wall -Wextra -std=c++17 func_template.cpp -o func_template && ./func_template
```

Expected output:

```text
=== max_value ===
max_value(3, 7) = 7
max_value(2.5, 1.3) = 2.5
max_value("banana", "apple") = banana
max_value<double>(3, 5.7) = 5.7

=== swap_value ===
before: a=10, b=20
after:  a=20, b=10
before: x=1.5, y=2.5
after:  x=2.5, y=1.5
before: s1="hello", s2="world"
after:  s1="world", s2="hello"

=== print_array ===
int[]:    [3, 1, 4, 1, 5, 9]
double[]: [1.1, 2.2, 3.3]
string[]: [Alice, Bob, Charlie]
```

Check a few key results: `max_value(3, 7)` correctly returns `7`; `max_value("banana", "apple")` goes through the `const char*` specialization, compares lexicographically, and since `"banana"` is greater than `"apple"` it returns `"banana"`; `swap_value` swaps the values correctly before and after; `print_array` prints all three arrays of different types correctly, with no trailing comma.

## Exercises

### Exercise 1: Generic Search

Implement a generic function `find_index` that searches an array for a value and returns its index, or `-1` if not found. The signature is roughly:

```cpp
template <typename T, std::size_t kSize>
int find_index(const T (&arr)[kSize], const T& target);
```

Test it with `int`, `double`, and `std::string`. Think: if `T` is a custom class, will this function work? What conditions must the class satisfy?

### Exercise 2: Generic Sort

Implement a simple generic bubble-sort function `bubble_sort` that sorts an array in place. You don't need to write comparison logic yourself—just use `operator>` or `operator<`. It should sort `int`, `double`, and `std::string` arrays and print the results.

### Exercise 3: Generic Accumulator

Implement a generic function `accumulate_all` that sums all elements in an array. Think about the return type: if the elements are `int`, the sum might overflow `int`—how do you handle that? Hint: add a template parameter for the accumulator type.
