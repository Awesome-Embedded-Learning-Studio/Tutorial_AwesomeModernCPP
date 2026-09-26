---
chapter: 12
cpp_standard:
- 11
- 14
- 17
- 20
description: 'Strip templates back to what they really are: a code recipe with placeholders.
  Sorts out how they differ from macros and from virtual-function polymorphism, and
  what each of the four kinds of template entities in C++—function, class, variable,
  alias—is for.'
difficulty: intermediate
order: 1
platform: host
prerequisites:
- Function Templates
reading_time_minutes: 11
related:
- 'Function Templates, In Depth: Compilation Model and the No-Partial-Specialization Trap'
- 'Class Templates: Members, Dependent Names, and Lazy Instantiation'
tags:
- host
- cpp-modern
- intermediate
- 模板
- 泛型
title: 'Templates, From Scratch: A Code Recipe with Placeholders'
translation:
  source: documents/vol4-advanced/vol1-basics-cpp11-14/01-templates-introduction.md
  source_hash: 9490d4a03a987e2b3c3a7ffa6e76091cfaef5bdfdcf4948a001276b4cb26d0c8
  translated_at: '2026-09-26T03:58:20+00:00'
  engine: anthropic
  token_count: 6500
---
# Templates, From Scratch: A Code Recipe with Placeholders

We already wrote function templates back in Volume 1: once you write `template <typename T> T max_value(T a, T b)`, the compiler can generate one copy of the code for `int`, another for `double`, and another for `std::string`. This volume no longer lingers on "how to use them"—we want to work out a few other things: what a template actually is, how it pulls off "write once, fit many types", and what this mechanism costs. With those low-level details properly digested, you won't feel shaky later, whether you are reading the STL sources, reading template-heavy industrial code like Chromium, or writing a library of your own.

## What a Template Actually Is: A Code Recipe with Placeholders

We like to think of a template as a **code recipe**. The template itself is not code; it is a description of "how the code should be written". `T` is a placeholder in the recipe, meaning "leave this spot blank for now; fill it in when the recipe is actually used".

```cpp
template <typename T>
T max_value(T a, T b) {
    return (a > b) ? a : b;
}
```

For these few lines the compiler generates no machine code. It simply writes the recipe down. What actually brings machine code into existence is **instantiation**. When we write `max_value(3, 5)`, the compiler sees that both `3` and `5` are `int`, copies the recipe out with every `T` replaced by `int`, and ends up with a real `int max_value(int, int)` function—only that copy gets compiled into machine code.

```cpp
int x = max_value(3, 5);        // T=int: the compiler copies out an int version
double y = max_value(1.0, 2.0); // T=double: another copy, the double version
```

From these two calls the compiler produces two completely independent functions, each compiled separately. The effect is indistinguishable from handwriting two overloads ourselves.

One counterintuitive point deserves a pause. Many people assume a template "picks a version at runtime based on the type", when in fact it "generates one copy per type used, at compile time". That is why calling a template has **no runtime overhead**: you are calling an ordinary function, with none of that virtual-table dispatch machinery. As for the price—we'll get to it shortly.

::: warning Placeholders are not macro substitution
Some people understand templates as "a slightly fancier macro substitution". That is only half right. A macro (`#define`) is pure text replacement at the preprocessing stage: it recognizes neither types nor scopes, and one careless step wrecks everything. A template's substitution happens at the compilation stage, where the compiler knows `T` is a type and performs type checking, overload resolution, and name lookup. When we get to two-phase lookup later, we will see that this machinery is far more refined than a macro. Keeping "a macro with type checking" as a first impression is fine—just don't stop there.
:::

## Why Not Macros, and Why Not Virtual Functions

The "same logic, different types" need can be met down several roads in C++. Let's line up the three that get compared most often.

The macro route: `#define max(a, b) ((a) > (b) ? (a) : (b))` does work, but it is pure text replacement during preprocessing. Arguments get evaluated twice, types are ignored, there is no scope, and the debugger never sees it. As Volume 1 mentioned when introducing function templates, the `max` macro from `<windows.h>` on Windows can send your blood pressure through the roof. Nearly everything a macro can do, a template does better.

The virtual-function-polymorphism route: write `max` as a virtual function, and dispatch to the matching implementation at runtime based on the object's actual type. It requires every participating type to live in one inheritance hierarchy, and every call pays a virtual-table lookup—runtime overhead. Worse still, built-in types like `int` and `double` can never enter your inheritance hierarchy.

The template route: generate one copy of the code per type used at compile time, so every call lands on an ordinary, inlineable function. No inheritance-hierarchy constraint—built-in types and user-defined types are treated exactly alike—and no runtime dispatch overhead.

None of these three routes replaces another. Virtual functions suit "same interface, concrete implementation known only at runtime"—plugin systems, GUI event dispatch; templates suit "same logic, types known at compile time"—containers and algorithms. As for macros, the rule is basically: if you can avoid them, do.

## C++ Has Four Kinds of Template Entities

Many people assume templates are just function templates and class templates, but in the C++ standard a template can define four kinds of entities. cppreference gives the definition: a template is a C++ entity that defines a family of entities.

A function template defines a family of functions—`std::max` and `std::sort` are both function templates. A class template defines a family of classes—`std::vector` and `std::map` are both class templates. These two have existed since C++98 and are the ones we know best. On top of them come two newer kinds: variable templates (since C++14), which define a family of variables or static members, and alias templates (since C++11), which define a family of type aliases.

Variable templates answer a very plain need: give "each type" its own constant. Before C++14, people emulated that with a class template's static member:

```cpp
// The old pre-C++14 way: emulate a "parameterized constant" with a class template's static member
template <typename T>
struct pi_trait {
    static constexpr T value = T(3.1415926535897932385L);
};
double r = pi_trait<double>::value * 2.0;
```

C++14 brought variable templates: write it directly as one parameterized variable, much cleaner:

```cpp
template <typename T>
constexpr T pi = T(3.1415926535897932385L);  // variable template

double r = pi<double> * 2.0;  // reads like an ordinary constant, just with a <T>
```

Part of the reason `std::numeric_limits<T>::max()` in the standard library is a function rather than a variable is that variable templates did not exist when it was born. `std::tuple_size`, `std::extent`, and their kin later all gained matching `_v` variable-template versions (`std::extent_v<T>`, since C++17), precisely so that everyone writes one less `::value`. On cppreference, the feature-test macro for variable templates is `__cpp_variable_templates = 201304L`, corresponding to C++14.

Alias templates answer "give a family of types one short name":

```cpp
// Before alias templates, naming vector<T> meant going through a class template's nested using
template <typename T>
struct vec_alias { using type = std::vector<T>; };
typename vec_alias<int>::type v;  // typename and ::type all over again—verbose

// C++11 alias template: one line and done
template <typename T>
using vec = std::vector<T>;
vec<int> v;  // clean
```

A later part of this volume is dedicated to alias templates; for now, just keep the impression: **in C++, it is not only functions and classes that can be parameterized**.

## Three Kinds of Template Parameters

A template's "placeholders" come in three kinds, one per kind of parameter.

The type parameter, written `typename T` or `class T`, stands for a type—the most common kind. The non-type parameter, written in forms like `template <int N>`, stands for a **value** already known at compile time: an integer, a pointer, a reference, plus—since C++20—floating-point numbers and class types satisfying certain conditions. The template template parameter stands for a template itself.

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

`std::array<T, N>` is the classic example of the first two cooperating: `T` is the element type, `N` is the array size, and `N` must be known at compile time. Template template parameters rarely show up in daily work, but you will run into them when writing higher-order generic libraries (say, a strategy to "swap in a different underlying container"). All three kinds get their own chapter later in this volume, and the chapter on non-type parameters covers in detail the great loosening C++20 gave them.

## Templates Are Compile-Time Turing-Complete

This deserves a section of its own, because it decides how far templates can go. **C++'s template mechanism is Turing-complete**—that is, if we are willing, we can carry out arbitrarily complex computation with templates at compile time. Branches, loops (simulated with recursion), any logic at all: everything completes during compilation, and the runtime receives results that are already computed.

A minimal example—computing factorials at compile time:

```cpp
template <unsigned N>
struct Factorial {
    static constexpr unsigned value = N * Factorial<N - 1>::value;
};

template <>
struct Factorial<0> {              // recursion terminates: 0! = 1
    static constexpr unsigned value = 1;
};

template <unsigned N>
constexpr unsigned factorial_v = Factorial<N>::value;  // C++14 variable template, put to use while we're here
```

Run it, and every multiplication happens at compile time:

```bash
$ g++ -std=c++14 factorial.cpp -o factorial && ./factorial
5! = 120
10! = 3628800
```

```cpp
static_assert(Factorial<5>::value == 120, "");      // already verified at compile time
static_assert(factorial_v<10> == 3628800, "");
```

`Factorial<5>::value` is already `120` before the program ever runs. This is the root of template metaprogramming (TMP): stuff the computation into compile time. It enables remarkable things—expression templates, compile-time strings, and `constexpr` compile-time parsing are all built on it—at the cost of slow compiles, hard-to-read error messages, and poor code readability. Part 3 of this volume (C++20/23 metaprogramming) covers how C++ uses `concepts`, `consteval`, and `if constexpr` to pull TMP back from "black magic" to "code a human can write". For this chapter, just remember one thing: **a template does not only generate code; it can itself compute things at compile time**.

## The Cost: Instantiation, Code Bloat, and Header-Only

Templates are not free; there are three bills to pay.

The first is **lazy instantiation**. Of a class template's member functions, only the ones actually used get instantiated. Writing a `std::vector<Heavy>` does not instantiate every member function of `vector`—only the ones you actually call. This is also why the STL dares to pack that much functionality into one template: what you don't use, you don't pay for.

```cpp
template <typename T>
struct Demo {
    void used_function() { T t{}; /* ... */ }
    void unused_function() {
        // Even code using members that don't exist on T at all wouldn't error here
        // This function is never called, so it is never instantiated
    }
};

int main() {
    Demo<int> d;
    d.used_function();   // only this one gets instantiated
    // unused_function() is never called, so it is never instantiated and the compiler never sees the garbage inside
}
```

This cuts both ways. The upside: faster compiles and a smaller binary. The downside: some errors stay well hidden and surface only once you actually use them.

The second is **code bloat**. Every type used gets its own copy of the code. Using `max_value` once each for `int`, `double`, and `std::string` means three functions. For small functions, no big deal; for large templates (full specializations of certain STL algorithms, say), the accumulation visibly fattens the binary. The function-templates-in-depth chapter later in this volume covers `extern template`, precisely the tool for keeping this bloat in check.

The third is **header-only**. A template's definition must be visible at the point where it is instantiated, so the overwhelming majority of template libraries are pure header-only libraries—boost is the classic example. You cannot do what you do with ordinary functions: declaration in the header, implementation in a `.cpp`. This feeds directly into C++'s old "slow compiles" problem: every translation unit re-parses the template definitions. C++20 modules exist to cure exactly this disease, but that is another big topic.

## export template: An Abandoned Attempt

Speaking of "the definition must be visible": the committee actually did offer a way out in the C++98 standard, called `export template`. The idea was lovely: a template marked `export` could be instantiated from its declaration alone, without including the definition—separate compilation for templates.

Reality was less kind. In the entire C++98 standard, only the Edison Design Group (EDG) front end and the Comeau compiler built on it ever truly implemented `export template`; GCC, Clang, and MSVC never did. The mechanism was extraordinarily complex to implement, while the benefit was hard to see. Come C++11, the committee simply removed it from the standard; cppreference marks it `(until C++11)`.

The lesson this history leaves us: **separate compilation of templates remains an unsolved problem to this day**. `extern template` cuts down redundant instantiations but cannot truly separate declaration from definition; C++20 modules are a fresh, completely different answer, and their ecosystem is still being built. So for now, template code basically still has to live in headers—that is a reality C++ programmers accept.

## The Road This Volume Takes

In this chapter we stripped templates back to what they really are: a code recipe with placeholders; at compile time, real code is generated from the recipe, with zero runtime overhead, at the cost of code bloat and mandatory headers. From here on, we push from "can use" toward "can write libraries". The next chapter takes function templates apart: the compilation model, `extern template`, and the classic trap that function templates cannot be partially specialized. After that come class templates, specialization and partial specialization, non-type parameters, two-phase name lookup, friend injection, and alias templates; CRTP then closes out static polymorphism, and finally a `fixed_vector` capstone project strings everything you learned together.

Reading to here, you should feel confident about what a template *is*. In the pages ahead, we work through the *why*, one piece at a time.
