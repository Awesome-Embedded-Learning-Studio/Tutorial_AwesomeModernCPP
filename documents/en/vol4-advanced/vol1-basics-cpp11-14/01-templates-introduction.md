---
title: "Templates, From Scratch: A Code Recipe with Placeholders"
description: "Strip templates back to what they really are: a code recipe with placeholders. How they differ from macros and virtual dispatch, and the four kinds of template entities in C++ (functions, classes, variables, aliases)."
chapter: 12
order: 1
tags:
  - host
  - cpp-modern
  - intermediate
  - 模板
  - 泛型
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17, 20]
reading_time_minutes: 18
prerequisites:
  - "Volume 1 · Function Templates"
related:
  - "Function Templates, In Depth: Compilation Model and extern template"
  - "Class Templates: Members, Dependent Names, Lazy Instantiation"
---

# Templates, From Scratch: A Code Recipe with Placeholders

We already wrote function templates back in Volume 1. We know that `template <typename T> T max_value(T a, T b)` lets the compiler stamp out a version for `int`, for `double`, for `std::string`. This volume stops at "how to use them" and asks different questions. What is a template, really? How does it pull off "write once, fit any type"? And what does that mechanism cost? Get the under-the-hood details straight, and reading the STL source, reading template-heavy industrial code like Chromium, or writing a library of your own stops feeling intimidating.

## What a Template Actually Is: A Code Recipe with Placeholders

I like to think of a template as a **code recipe**. The template itself is not code; it is a description of how the code should be written. `T` is a placeholder in the recipe. It says "leave this blank for now, fill it in when the recipe is actually used."

```cpp
template <typename T>
T max_value(T a, T b) {
    return (a > b) ? a : b;
}
```

These lines produce no machine code on their own. The compiler just records the recipe. What actually produces machine code is **instantiation**. When we write `max_value(3, 5)`, the compiler sees that `3` and `5` are both `int`, copies the recipe with `T` replaced by `int`, and gets a real `int max_value(int, int)` function. That copy is what gets compiled.

```cpp
int x = max_value(3, 5);        // T=int, compiler stamps out an int version
double y = max_value(1.0, 2.0); // T=double, stamps out a double version
```

These two calls produce two completely independent functions, each compiled separately. The effect is the same as if you had written two overloads by hand.

Here is a counterintuitive point worth pausing on. Many people assume a template picks a version at runtime based on type. It does not. It generates a separate version at compile time for every type you use. So a template call has **no runtime overhead**: you are calling an ordinary function, with no vtable dispatch. The cost comes later, and we will get to it.

::: warning A placeholder is not a macro substitution
Some people read templates as "a fancier macro substitution," and that is half right. A macro (`#define`) is pure text replacement at the preprocessing stage. It ignores types, ignores scope, and trips you up the moment you stop paying attention. Template substitution happens during compilation. The compiler knows `T` is a type, and it runs type checking, overload resolution, and name lookup on it. When we get to two-phase lookup later, you will see that this mechanism is far more precise than a macro. Treating templates as "a macro with type checking" is fine as a first impression, but do not stop there.
:::

## Why Not a Macro, and Why Not a Virtual Function

"Same logic, different types" is a need C++ answers in several ways. Let us line up the three most often compared.

Macros. `#define max(a, b) ((a) > (b) ? (a) : (b))` works, but it is pure text replacement at preprocessing. Arguments get evaluated twice, types are ignored, there is no scope, and the debugger never sees it. Back in Volume 1 we mentioned the `max` macro from `<windows.h>` on Windows, the one that sends your blood pressure through the roof. Anything a macro can do, a template does better.

Virtual dispatch. Write `max` as a virtual function, and the right implementation is picked at runtime based on the actual type of the object. This forces every participating type into one inheritance hierarchy, and every call goes through a vtable lookup, which costs runtime. Worse, builtin types like `int` and `double` cannot enter your hierarchy at all.

Templates. The compiler generates a separate copy for every type used, and the call goes to an ordinary, inlineable function. No inheritance hierarchy required. Builtin types and user-defined types are treated the same. No runtime dispatch.

None of these replaces the others. Virtual dispatch fits "one interface, implementation decided at runtime" cases like plugin systems or GUI event dispatch. Templates fit "one piece of logic, type known at compile time" cases like containers and algorithms. As for macros, mostly avoid them when you can.

## C++ Has Four Kinds of Template Entities

A lot of people think templates come in two flavors, function templates and class templates. The C++ standard actually defines four kinds of entities a template can produce. cppreference puts it plainly: a template is a C++ entity that defines a family of entities.

A function template defines a family of functions; `std::max` and `std::sort` are examples. A class template defines a family of classes; `std::vector` and `std::map` are examples. Both have been around since C++98, and these are the two you know best. There are two newer ones. A variable template (since C++14) defines a family of variables or static members. An alias template (since C++11) defines a family of type aliases.

Variable templates answer a plain need: attach a constant to "each type." Before C++14, people simulated this with a static member of a class template.

```cpp
// The old way, before C++14: simulate "a parameterized constant" with a class template static member
template <typename T>
struct pi_trait {
    static constexpr T value = T(3.1415926535897932385L);
};
double r = pi_trait<double>::value * 2.0;
```

C++14 gave us variable templates, and you can write it as a single parameterized variable, much cleaner.

```cpp
template <typename T>
constexpr T pi = T(3.1415926535897932385L);  // variable template

double r = pi<double> * 2.0;  // reads like an ordinary constant, just with a <T>
```

Part of the reason `std::numeric_limits<T>::max()` is a function rather than a variable is that variable templates did not exist when it was born. `std::tuple_size` and `std::extent` later grew matching `_v` variable-template versions (things like `std::extent_v<T>`, introduced in C++17), precisely so you could stop writing `::value`. The feature-test macro on cppreference is `__cpp_variable_templates = 201304L`, tied to C++14.

Alias templates answer the need to give a family of types a short name.

```cpp
// Before alias templates, naming vector<T> meant borrowing a nested using inside a class template
template <typename T>
struct vec_alias { using type = std::vector<T>; };
typename vec_alias<int>::type v;  // typename, then ::type, noisy

// C++11 alias template, one line
template <typename T>
using vec = std::vector<T>;
vec<int> v;  // clean
```

This volume has a whole piece on alias templates later. For now, just hold this impression: **in C++, it is not only functions and classes that can be parameterized**.

## Three Kinds of Template Parameters

The "placeholder" in a template comes in three forms.

A type parameter, written `typename T` or `class T`, stands in for a type. This is the most common. A non-type parameter, written like `template <int N>`, stands in for a compile-time **value**: an integer, a pointer, a reference, and since C++20 a floating-point value or a class type that meets certain conditions. A template template parameter stands in for a template itself.

```cpp
template <typename T, std::size_t N>   // T is a type parameter, N is a non-type parameter
struct array {
    T data[N];
};

template <template <typename> class Container>  // Container is a template template parameter
struct wrapper {
    Container<int> c;
};
```

`std::array<T, N>` is the classic pairing of the first two: `T` is the element type, `N` is the size, and `N` must be known at compile time. Template template parameters are rare in day-to-day code, but they show up when you write higher-order generic libraries (say, a policy that swaps out the underlying container). All three get their own treatment later in this volume, and the non-type parameter piece spends time on how much C++20 loosened the rules.

## Templates Are Compile-Time Turing Complete

This deserves a section of its own, because it determines how far templates can go. **The C++ template mechanism is Turing complete.** Given the patience, you can do arbitrary computation at compile time with templates: branching, looping (faked with recursion), any logic, all finished before the program runs. What you get at runtime is a precomputed result.

A minimal example, computing factorials at compile time.

```cpp
template <unsigned N>
struct Factorial {
    static constexpr unsigned value = N * Factorial<N - 1>::value;
};

template <>
struct Factorial<0> {              // recursion base: 0! = 1
    static constexpr unsigned value = 1;
};

template <unsigned N>
constexpr unsigned factorial_v = Factorial<N>::value;  // C++14 variable template, convenient
```

Run it, and all the multiplication happened at compile time.

```bash
$ g++ -std=c++14 factorial.cpp -o factorial && ./factorial
5! = 120
10! = 3628800
```

```cpp
static_assert(Factorial<5>::value == 120, "");      // checked at compile time
static_assert(factorial_v<10> == 3628800, "");
```

`Factorial<5>::value` is already `120` before the program starts. This is the root of template metaprogramming (TMP): push computation into compile time. It powers impressive things, expression templates, compile-time strings, `constexpr` parsing all build on it, at the cost of slow builds, unreadable errors, and code that is hard to follow. Part three of this volume (C++20/23 metaprogramming) is about how C++ uses `concepts`, `consteval`, and `if constexpr` to drag TMP out of "dark arts" and into "code a person can write." For this piece, remember one thing: **a template does not only generate code, it can compute at compile time**.

## The Cost: Instantiation, Code Bloat, Header-Only

Templates are not free. There are three costs.

The first is **lazy instantiation**. Only the member functions of a class template that are actually used get instantiated. Writing `std::vector<Heavy>` does not instantiate every member function of `vector`, only the ones you call. This is why the STL dares to pack so much into one template. You do not pay for what you do not use.

```cpp
template <typename T>
struct Demo {
    void used_function() { T t{}; /* ... */ }
    void unused_function() {
        // Even if this body refers to a member that does not exist on T,
        // it will not error. This function is never called, so it is never instantiated.
    }
};

int main() {
    Demo<int> d;
    d.used_function();   // only this one gets instantiated
    // unused_function() is never called, so it is never instantiated,
    // and the compiler never sees the nonsense inside it
}
```

This cuts both ways. The upside is faster builds and smaller binaries. The downside is that some errors stay hidden until you actually use the thing.

The second is **code bloat**. Every type you use gets its own copy. `max_value` used with `int`, `double`, and `std::string` is three functions. For a small function this is nothing. For large templates (say, the full specialization of some STL algorithm), it piles up and the binary grows noticeably. The function-template-in-depth piece in this volume covers `extern template`, the tool for keeping this bloat under control.

The third is **header-only**. A template definition has to be visible at the point where it is instantiated, so almost every template library is a header-only library. boost is the canonical example. You cannot do what ordinary functions do, put the declaration in a header and the implementation in a `.cpp`. This is a direct cause of the old "C++ builds slowly" complaint: every translation unit re-parses the template definitions. C++20 modules exist to treat this disease, but that is a separate, large topic.

## export template: An Abandoned Attempt

On the subject of "the definition must be visible," the C++98 committee did offer a way out, called `export template`. The idea was beautiful: a template marked `export` could be instantiated by users who only saw the declaration, no need to include the definition, and you would get separate compilation for templates.

Reality was unkind. Across the entire C++98 standard, only the Edison Design Group (EDG) front end and the Comeau compiler built on it ever implemented `export template`. GCC, Clang, and MSVC never did. The mechanism was enormously complex to implement, and the payoff was unclear. C++11 removed it from the standard entirely. cppreference marks it `(until C++11)`.

The lesson here is that **separate compilation for templates is still an unsolved problem**. `extern template` cuts down redundant instantiation, but it does not truly separate declaration from definition. C++20 modules are a different, new answer, and their ecosystem is still being built. For now, template code still basically has to live in headers. That is the reality C++ programmers work in.

## The Road This Volume Takes

This piece stripped templates back to what they are: a code recipe with placeholders, stamped into real code at compile time, with no runtime overhead, paid for in code bloat and the need to live in headers. From here we move from "can use" to "can write libraries with." The next piece breaks open the compilation model of function templates, `extern template`, and the classic trap that function templates cannot be partially specialized. After that comes class templates, specialization and partial specialization, non-type parameters, two-phase name lookup, friend injection, and alias templates, closing with CRTP for static polymorphism and a `fixed_vector` project that strings everything together.

Read this far, and you should feel grounded on "what a template is." The rest of the volume takes the "why" apart, piece by piece.
