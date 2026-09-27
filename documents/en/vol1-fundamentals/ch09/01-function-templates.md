---
chapter: 9
cpp_standard:
- 11
- 14
- 17
- 20
description: Master the syntax of `template<typename T>`, the instantiation mechanism,
  and type deduction, and learn how to write generic functions.
difficulty: intermediate
order: 1
platform: host
prerequisites:
- Why We Need Templates
reading_time_minutes: 16
tags:
- cpp-modern
- host
- intermediate
- 进阶
title: Function Templates
translation:
  source: documents/vol1-fundamentals/ch09/01-function-templates.md
  source_hash: 8312bf76500b5793bf72c2a796dea891cf1d6b3e128ee92c110c300982e56e6a
  translated_at: '2026-09-27T03:54:54+00:00'
  engine: anthropic
  token_count: 7500
---
# Function Templates: One Copy of the Logic, Not Three

At last, we can formally get started on function templates. A quick recap: **when it is the types themselves that vary, inheritance can't help—just hand the type to the compiler as a parameter**.

Easy to say, but it hasn't quite landed in practice yet, right? Fine then. Let's start with the simplest kind of template usage—**the illustrious function template. Here we go!**

No hypotheticals: you are doing this right now! Start by writing a `max_value` function. The author's requirement: it takes two values and returns the larger one.

> Let's insert a pause here. Stop for a second and think this through: is solving the problem with a template really the most reasonable choice?
>
> A second pause: the reason the function isn't named `max` is that `std::max` already lives in the standard library, and reusing the name invites conflicts on some compilers—especially on Windows, where `<windows.h>` defines a `max` macro. That is a genuine blood-pressure-maxing moment. I have been burned by it before, and it is part of why the author really doesn't like Windows. That kind of overstepping happens far too often!

The idea is dead simple—two lines of code and we are done. But if our program needs to compare `int`, `double`, and `std::string` at the same time, we have to write three versions: one `max_value(int, int)`, one `max_value(double, double)`, and one `max_value(std::string, std::string)`. The logic of all three is exactly the same—`(a > b) ? a : b`—and the only difference is the parameter type. At this point, a saner approach is no longer optional. Let's get moving!

## `template<typename T>`—Where Generics Begin

It really isn't hard:

```cpp
template <typename T>
T max_value(T a, T b)
{
    return (a > b) ? a : b;
}
```

`template <typename T>` tells the compiler: this is a template, and `T` is a type parameter. In the function definition that immediately follows, every place where `T` appears gets replaced with the actual type at instantiation.

Finding it a bit tricky? One example settles it. When we call `max_value(3, 5)`, the compiler deduces that `T` is `int` and generates an `int max_value(int, int)` version of the function. Calling `max_value(1.0, 2.0)` generates the `double max_value(double, double)` version. The whole process is transparent to the caller.

### The Difference Between typename and class

Hey! Some of the older textbooks write `template<class T>`. Let me put it in the most direct, least roundabout, straight-to-the-point way I can: inside a template parameter list, `typename` and `class` are completely equivalent!

`template <typename T>` and `template <class T>` **mean exactly the same thing, with no semantic difference whatsoever**. Early C++ only supported the `class` keyword; `typename` was introduced later precisely to clear up the misconception that "T must be a class" (and afterwards it even took on a second job: emphasizing that some name we write later on is a type, so the compiler shouldn't butt in and assume it is a variable!).

`T` can be any type: built-in types (`int`, `double`, pointers), custom classes, even function pointers. Modern C++ style leans toward `typename`—it reads more precisely to us, and cleaner too.

### Multiple Type Parameters

In some situations a single type parameter isn't enough. Say we want a function that converts a value of one type into another:

```cpp
template <typename Dest, typename Source>
Dest cast_to(Source value)
{
    return static_cast<Dest>(value);
}
```

There is no upper limit on the number of template parameters, but in real projects going past two or three is uncommon: every extra type parameter makes it more likely that callers have to specify types explicitly, and readability drops right along with it.

## Template Instantiation—The Compiler Writes the Code for You

**A template by itself is not code—it is a "code recipe".** You tell it how to do the job, and it cooks up something big for you!

Only when you actually call the template function does the compiler take the types of the call arguments and "expand" the template into a concrete function definition. This process is called template instantiation. (Feels a bit like a macro, doesn't it? If the author's memory serves, that was truly its original role at the very, very beginning!)

```cpp
int x = max_value(3, 5);       // T = int, generates int max_value(int, int)
double y = max_value(1.0, 2.0); // T = double, generates double max_value(double, double)
```

The two calls above make the compiler generate two completely independent functions. Each exists on its own in the compiled binary, exactly as if we had hand-written two overloaded functions. **This is also the core cost of templates: code bloat—and friends from the embedded world need to pay special attention to it, because inappropriate or accidental instantiation can easily leave your code unable to fit into Flash.**

### Implicit Instantiation vs Explicit Instantiation

The style above—"the compiler deduces the types from the call arguments and generates code automatically"—is called implicit instantiation, and it is the most common form. But sometimes we need to tell the compiler explicitly which type to use; that is explicit instantiation:

```cpp
int result = max_value<double>(3, 5.0);  // Explicitly specify T = double
```

Here `3` is an `int` and `5.0` is a `double`; the two types differ, and the compiler cannot deduce `T` as both `int` and `double` at once—we will talk through that deduction conflict in detail in the next section. By appending `<double>` after the function name, we explicitly pin down `T`; the compiler then implicitly converts `3` to `double` and calls the `max_value<double>` version.

There is also a rarer form, the explicit instantiation definition, which forces the compiler to generate the code for a particular version right here, even if the current translation unit never uses it at all:

```cpp
template int max_value<int>(int, int);           // Explicit instantiation definition
template double max_value(double, double);       // Same as above, with the template argument list omitted
```

This form shows up occasionally in library development: put the template's implementation in a `.cpp` file, then explicitly instantiate the type versions the library needs to export, so that user code never has to see the template implementation. In day-to-day work, though, we almost never need to hand-write explicit instantiation definitions.

## Type Deduction—How the Compiler Guesses T

When we call `max_value(3, 5)`, the compiler sees that both arguments, `3` and `5`, are `int`, so it deduces `T = int`. This process is called template argument deduction. Deduction happens at compile time and costs nothing at runtime.

The deduction rules are simple to state—one rule is all we need to remember: every template parameter must be uniquely determined. If the same `T` appears in multiple parameters, those parameters' types must match exactly after references and top-level `const` are stripped away; otherwise deduction fails.

### Typical Deduction Failure Scenarios

```cpp
auto r = max_value(3, 5.0);  // Compile error!
```

This code fails outright. The reason: `3` has type `int`, so the compiler deduces `T = int`; `5.0` has type `double`, so the compiler deduces `T = double`. One `T` cannot equal both `int` and `double` at the same time—the deduction contradicts itself.

Error messages from a failed template deduction are usually very long. The compiler lists every overload and template candidate it tried, then tells you "none of them match". For newcomers, dozens of lines of that is thoroughly discouraging. The way out is to locate the last line of the error message—it usually points at exactly which parameter's type mismatches—then work backward from the call site, checking that each argument's type agrees.

> Clang, if I remember right, handles these errors quite elegantly; whether GCC has improved, I am not sure—in the past, one error and the rest of your code became unreadable. Of course, the arrival of concepts makes the errors a lot more comfortable: the compiler tells you directly that some concept wasn't satisfied, upgrading readability from compiler-speak to syntax-level indication. I think that is a good thing, and it is also an important thread in the more aggressive modern C++ we will cover later on.

There are three ways to resolve a deduction conflict.

The first is to specify the template argument explicitly, like the `max_value<double>(3, 5.0)` we just saw: force `T = double`, and `3` gets implicitly converted.

The second is to convert the argument type by hand: `max_value(static_cast<double>(3), 5.0)`.

The third is to change the template itself to take two independent type parameters—though that route needs care; we will discuss it shortly.

### The Two-Type-Parameter Trap

We might think: since `int` and `double` conflict in deduction, just use two type parameters.

```cpp
template <typename T, typename U>
???.??? max_value_two(T a, U b)
{
    return (a > b) ? a : b;
}
```

The problem is the return type: if `T` is `int` and `U` is `double`, is the return value `int` or `double`? With `auto`, the compiler deduces it itself: `(a > b) ? a : b` follows the conditional operator's type deduction rules in C++, where `int` and `double` promote to `double`, so the return value is `double`. But that only works for simple cases; in more complex situations you may need `std::common_type_t<T, U>` to obtain the common type of the two:

```cpp
template <typename T, typename U>
auto max_value_two(T a, U b) -> std::common_type_t<T, U> // Yes, "common" as in shared — a type both can be handled as uniformly
{
    return (a > b) ? a : b;
}
```

`std::common_type_t` is defined in `<type_traits>`; it picks the most suitable common type according to the implicit conversion rules between the two types. Still, when we do hit a mixed-type comparison in daily work, the simplest approach remains specifying one type explicitly or casting by hand—no need for anything this elaborate.

## Template Specialization—When the Generic Version Doesn't Fit

The `max_value` we wrote works fine for most types, but for `const char*` (C-style strings) it compares the addresses of the two pointers rather than the string contents. **I am fairly sure nobody has the slightest interest in comparing the addresses where strings happen to live—that is definitely not what we want!**

So this is where we bring out the concept of **template specialization**—why? Because it lets us provide a dedicated implementation for one specific type.

```cpp
// Generic template
template <typename T>
T max_value(T a, T b)
{
    return (a > b) ? a : b;
}

// Specialized version for const char*
template <>
const char* max_value<const char*>(const char* a, const char* b)
{
    return (std::strcmp(a, b) > 0) ? a : b; // When the compiler notices you passed a raw const char*, it goes here
}
```

`template <>` marks a full specialization: all the template parameters are pinned down. When we call `max_value("hello", "world")` and the compiler deduces `T = const char*`, it prefers the specialized version over the generic one.

Specialization is a fairly big topic, involving partial specialization, SFINAE, `concept` constraints, and more. For now, knowing that it exists and what its basic syntax looks like is enough; we will go deeper in the class-templates chapter later.

## Function Overloading vs Templates—When to Use Which

Function overloading and function templates both deliver "same-name function handling different types", but the machinery is completely different. With overloading, we hand-write one version per type, and the compiler picks the best match by argument type. With templates, we write one generic "recipe", and the compiler generates the matching version from each call.

The principle for choosing is really intuitive: if the processing logic is identical for every type and only the types differ, use a template—one `max_value` template is far cleaner than 20 hand-written overloads. If the logic genuinely differs per type (say, `print(int)` just outputs the number, while `print(std::string)` needs quotes around it), use overloading: each version's logic stays independent and clear to write.

### Overload Resolution When Mixing Them

Templates and overloads can coexist, and the compiler has a deterministic set of overload resolution rules: first gather all the candidate functions (the plain overloads, plus the specializations generated after successful template deduction), then rank them by how exactly the types match, and pick the best match. If several candidates tie, the usual outcome is an ambiguity error—with one important exception: when the tie is between a non-template overload and a template specialization, the non-template overload wins, and there is no ambiguity. The example below shows this rule in action.

```cpp
template <typename T>
T max_value(T a, T b)
{
    return (a > b) ? a : b;
}

// Plain overload: the int version
int max_value(int a, int b)
{
    std::cout << "int overload\n";
    return (a > b) ? a : b;
}

int main()
{
    max_value(3, 5);       // Calls the plain overload (exact match preferred over the template)
    max_value(1.0, 2.0);   // Calls the template instantiation (no overload version for double)
    max_value<>(3, 5);     // Forces the template, skipping the plain overload
}
```

The example above demonstrates exactly that rule: for `max_value(3, 5)`, both candidates match exactly and the non-template overload wins; for `max_value(1.0, 2.0)`, only the template can match, so the template it is; to force the template, add empty angle brackets—`max_value<>(3, 5)`.

The easiest trap to fall into when mixing overloads and templates is the template's "silent" deduction failure. Suppose you write a template `template <typename T> T max_value(T, T)` plus an overload `double max_value(double, int)`, then call `max_value(1.0, 2)`. Intuitively you might worry about ambiguity, but actually run it: it compiles, returns `2` normally, and there is no ambiguity at all. The reason is that template argument deduction does **not** apply implicit conversions to arguments just to make `T` agree: `1.0` deduces `T = double`, `2` deduces `T = int`, the two conflict, deduction simply fails, and the template never even makes it into the candidate set; in the end only the overload `max_value(double, int)` matches exactly, so it gets called. The real trap comes later: this line compiles only because the overload is there as a safety net—the day you refactor that overload away, the same line goes from "works fine" to "deduction conflict, compile error", with error messages easily dozens of lines long. So when mixing templates and overloads, keep the interface as simple as possible: if you have a template, don't also add overloads for the same interface whose parameter types differ from it only in subtle ways.

Another common pitfall is templates interacting with C-style strings. When we call `max_value("hello", "world")`, `T` is deduced as `const char*`. If you haven't written a specialized version for `const char*`, what gets compared is pointer addresses rather than string contents, and the result depends entirely on where the strings sit in memory—likely different from run to run, and almost certainly not what you expected.

## Hands-On Practice—func_template.cpp

Now let's pull together everything we've learned and write a complete example program. It contains three generic functions—`max_value`, `swap_value`, and `print_array`—instantiated with `int`, `double`, and `std::string` respectively.

```cpp
// func_template.cpp
// Compile: g++ -Wall -Wextra -std=c++17 func_template.cpp -o func_template

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

    // Explicit instantiation: mixed types
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

Let's unpack a few key points. `print_array` takes an array-by-reference parameter, `const T (&arr)[kSize]`: it lets the compiler deduce both the element type `T` of the array and the array length `kSize`, so there is no need to pass a separate length argument.

`swap_value`'s parameters are references, `T&`—that is what allows it to modify the caller's variables. If we wrote the parameters as pass-by-value `T a, T b`, we would only be swapping copies, and the caller would never notice a thing.

### Verifying the Run

The complete code is right below—hit "Try It" to run it directly, no terminal needed:

<OnlineCompilerDemo
  title="Hands-On Practice: func_template.cpp"
  source-path="code/examples/vol1/17_function_templates.cpp"
  description="Run func_template.cpp online and check it against the checkpoints below. Try feeding print_array an array of some other type, or delete the const char* specialization and see what happens."
  allow-run
/>

Let's verify a few key results: `max_value(3, 7)` correctly returns `7`; `max_value("banana", "apple")` goes through the `const char*` specialized version and compares lexicographically, so `"banana"` being greater than `"apple"` means `"banana"` is returned; `swap_value` swaps the values correctly before and after; and `print_array` prints the contents of three arrays of different types correctly, with no stray trailing comma.

## Exercises

### Exercise 1: Generic Find

Implement a generic function `find_index` that searches an array for a value and returns its index, or `-1` if the value isn't found. The signature is roughly:

```cpp
template <typename T, std::size_t kSize>
int find_index(const T (&arr)[kSize], const T& target);
```

Test it separately with `int`, `double`, and `std::string`. Also think it through: if `T` is a custom class, will this function still work correctly? What conditions must the custom class satisfy?

### Exercise 2: Generic Sorting

Implement a simple generic bubble-sort function `bubble_sort` that sorts an array in place. You don't need to write comparison logic yourself—just use `operator>` or `operator<` directly. It must be able to sort and print the results for `int`, `double`, and `std::string` arrays respectively.

### Exercise 3: A Generic Accumulator

Implement a generic function `accumulate_all` that computes the sum of all elements in an array. Think about the return-type question: if the array elements are `int`, the sum may overflow the range of `int`—how should you handle that? Hint: add a template parameter to serve as the accumulator's type.
