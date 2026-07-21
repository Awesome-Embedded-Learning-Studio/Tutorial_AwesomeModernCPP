---
title: "Template Specialization and Partial Specialization: The Art of Pattern Matching"
description: "Specialization is the most expressive part of templates. Full vs partial specialization, the rule by which the compiler picks the most specialized version, and two classic applications: why std::vector<bool> is a partial specialization, and how the entire type_traits machinery (is_pointer, is_const) is built on partial specialization."
chapter: 12
order: 4
tags:
  - host
  - cpp-modern
  - intermediate
  - 模板
  - 泛型
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17]
reading_time_minutes: 20
prerequisites:
  - "Class Templates: Members, Dependent Names, and Lazy Instantiation"
  - "Function Templates, In Depth: Compilation Model and the No-Partial-Specialization Trap"
related:
  - "Non-Type Template Parameters: From Integers to C++20 Floats and Class Types"
  - "Name Lookup and ADL: How Two-Phase Lookup Works"
---

# Template Specialization and Partial Specialization: The Art of Pattern Matching

The strongest part of templates is not "write one, fit any type." It is **giving some specific type, or some family of types, a separate, different implementation.** This mechanism is called specialization. It comes in two forms: full specialization targets one concrete type, partial specialization targets a family of types matching a pattern. The previous piece noted that function templates cannot be partially specialized; only class templates and variable templates can. This piece takes class-template specialization all the way: full specialization, partial specialization, the priority rule for which version the compiler picks, and two classic cases that push partial specialization to the limit, the standard library's `std::vector<bool>` and the whole of `<type_traits>`.

## Full Specialization: A Separate Copy for One Concrete Type

Full specialization provides a dedicated implementation for one **fully fixed** type. The syntax starts with `template<>`, with every template parameter pinned.

```cpp
// primary template
template <typename T>
struct TypeSize {
    static constexpr std::size_t value = sizeof(T);
};

// full specialization: a dedicated version for int
template <>
struct TypeSize<int> {
    static constexpr std::size_t value = 4;   // assume a 32-bit platform where int is 4 bytes
};
```

With this specialization in place, `TypeSize<int>::value` takes the `4` from the specialization, while `TypeSize<double>::value` goes through the primary template and computes `sizeof(double)`. A full specialization "cuts in line": just as the compiler would instantiate `TypeSize<int>` from the primary template, it spots the existing full specialization and uses that instead, never generating the primary version.

Two things to remember about full specialization. First, it **is no longer a template**; it is a concrete class (or function, or variable) with all template parameters fixed. This affects ODR: the definition of a full specialization may appear in only one translation unit, or it is a duplicate definition. Second, the signature of a full specialization must correspond exactly to some instantiation of the primary template, with no slack.

## Partial Specialization: A Separate Copy for a Family of Types

Partial specialization targets not one concrete type, but **a family of types matching a pattern**. The most common patterns are "pointer types" and "reference types." The syntax starts with `template <typename T>` (note it is still a template, with `T` unbound), and the class name is followed by a patterned form like `<T*>`.

```cpp
// primary template
template <typename T>
struct TypeKind {
    static const char* name() { return "generic (primary)"; }
};

// partial specialization: pointer types
template <typename T>
struct TypeKind<T*> {
    static const char* name() { return "pointer (partial)"; }
};

// partial specialization: lvalue reference types
template <typename T>
struct TypeKind<T&> {
    static const char* name() { return "lvalue reference (partial)"; }
};
```

Note the key difference from full specialization: the partial specialization's `template <typename T>` head still has a `T`, so it remains a template. A full specialization has an empty `template<>` with every parameter pinned. That difference is what lets a partial specialization match "a family of types" while a full specialization matches only "one type."

Run it and see how it picks.

```cpp
int main() {
    std::cout << "double      : " << TypeKind<double>::name() << "\n";
    std::cout << "double*     : " << TypeKind<double*>::name() << "\n";
    std::cout << "double&     : " << TypeKind<double&>::name() << "\n";
    return 0;
}
```

```bash
$ g++ -Wall -Wextra -std=c++20 spec_test.cpp -o spec_test && ./spec_test
double      : generic (primary)
double*     : pointer (partial)
double&     : lvalue reference (partial)
```

(The full specialization for `int*` is not defined yet, so `main` does not touch `int*` here. Once the next section adds the full specialization, we will see how `int*` is picked.)

`double` matches no partial specialization and goes through the primary template. `double*` matches the `T*` partial. `double&` matches the `T&` partial. This is the core power of partial specialization: **dispatch different implementations based on the "shape" of a type**, like a compile-time type switch.

## Priority: Full > Partial > Primary

That `int*` line in the output deserves a closer look. `int*` matches both the `T*` partial (with `T=int`) and the following full specialization.

```cpp
// full specialization: specifically for int*
template <>
struct TypeKind<int*> {
    static const char* name() { return "int* (full, wins over partial)"; }
};
```

Both match. Which does the compiler pick? The rule is **pick the more specialized one**. A full specialization is more specialized than a partial (it has no unbound parameters), and a partial is more specialized than the primary. So the priority is: **full > partial > primary**. Add the full specialization above to the file, then have `main` also print `TypeKind<int*>::name()`, and the output for that line is `int* (full, wins over partial)` — `int*` hits the full specialization rather than the `T*` partial.

When several partial specializations all match, things get subtler, and the compiler compares which one is "more specific." Between `T*` and `T const*` partials, for `const int*`, `T const*` is more specific and wins. The "more specialized" judgment has formal rules; in everyday code, remember "the compiler picks the tightest fit." If there is genuine ambiguity, the compiler reports it and you adjust the patterns.

## What Patterns Partial Specialization Can Match

The expressiveness of partial specialization comes from the rich set of patterns it can match. Common ones include:

- `T*`: pointer types
- `T&` / `T&&`: reference types
- `const T*`, `volatile T`: cv-qualified variants
- `T[N]`: array types
- `Foo<T>`, `Bar<T, U>`: instantiations of a specific template
- even template template parameters can participate in pattern matching

Combined, partial specialization can recognize almost any "type shape." This is what gives `<type_traits>` its many type queries, as we will see next.

## Classic Application One: The std::vector\<bool\> Specialization

The most famous partial specialization in the standard library is `std::vector<bool>`. It is not the ordinary version of `std::vector<T>` instantiated with `T=bool`. It is a specialization the library writes on purpose, so that one `bool` takes one bit instead of one byte, saving memory.

```cpp
// roughly what the standard library does (simplified)
template <typename T, typename Alloc = std::allocator<T>>
class vector { /* ordinary implementation, one T per slot */ };

template <typename Alloc>
class vector<bool, Alloc> { /* specialization: bit-packed, one bool per bit */ };
```

The `vector<bool>` specialization has a visible side effect: its `operator[]` does not return `bool&`. It returns a **proxy object** of type `std::vector<bool>::reference`. The reason is that once bits are packed, you cannot return "a reference to one bit" (the smallest addressable unit of memory is a byte), so it returns a temporary that mimics the behavior of `bool&`. You can see the difference directly.

```cpp
#include <iostream>
#include <type_traits>
#include <vector>

int main() {
    std::cout << std::boolalpha;
    // vector<bool>'s reference is a proxy, not bool&
    std::cout << "vector<bool>::reference is bool&?   "
              << std::is_same_v<std::vector<bool>::reference, bool&> << "\n";
    // an ordinary vector's reference is a real element reference
    std::cout << "vector<char>::reference is char&?  "
              << std::is_same_v<std::vector<char>::reference, char&> << "\n";
}
```

```bash
$ g++ -Wall -Wextra -std=c++20 vecbool.cpp -o vecbool && ./vecbool
vector<bool>::reference is bool&?   false
vector<char>::reference is char&?  true
```

This proxy `reference` has caused `vector<bool>` no end of controversy. It makes `vector<bool>` not fully satisfy the "sequence container" requirements (because `reference` is not a real element reference), and it makes some generic code behave oddly on it. `auto& x = vec[0];` on `vector<bool>` **does not compile at all** — the proxy `reference` is an rvalue temporary, and an rvalue cannot bind to a non-const lvalue reference. Even switching to `auto x = vec[0];` does not give you a `bool`; it gives you the proxy object, and some operations on it behave unexpectedly. The committee has debated whether to pull it out of the standard and replace it with something like `dynamic_bitset`, but since it is used everywhere, the cost of changing it is too high, and it has stayed. It is a cautionary tale about partial specialization: a specialization can completely redefine the implementation, but the cost is that it may no longer satisfy the interface contract the primary template implies, and users trip over it.

## Classic Application Two: The Whole type_traits Pattern

The queries in `<type_traits>` like `std::is_pointer`, `std::is_const`, and `std::is_reference` are all built on the same pattern underneath: "primary template returns false, partial specialization that matches returns true." Hand-write an `is_pointer` and the secret is out.

```cpp
// primary template: default to "not a pointer"
template <typename T>
struct is_pointer {
    static constexpr bool value = false;
};

// partial specialization: only pointer types match
template <typename T>
struct is_pointer<T*> {
    static constexpr bool value = true;
};
```

Run it.

```bash
$ g++ -Wall -Wextra -std=c++20 is_ptr.cpp -o is_ptr && ./is_ptr
is_pointer<int>::value       = false
is_pointer<int*>::value      = true
is_pointer<int**>::value     = true
is_pointer<int&>::value      = false
```

`int` goes through the primary template, `value=false`. `int*` hits the `T*` partial, `value=true`. `int**` also hits it (with `T=int*`). `int&` is a reference, not a pointer, so it goes through the primary template and returns `false`.

That is the entire secret of `std::is_pointer`. The standard library version has a bit more (it also handles pointers to members, cv-qualification, and edge cases), but the core idea is exactly this primary-plus-partial pattern. `is_const` (where `const T*` does not count but `T const` does), `is_reference` (`T&` and `T&&`), `is_array` (`T[N]`) all use the same approach: a primary template with a default value, and a partial specialization that overrides it for the target pattern.

Once this pattern clicks, you can write your own type queries. To check "is it a function pointer," write a primary template that defaults to false and a partial specialization for `R(*)(Args...)` that returns true. The entire `<type_traits>` is a few hundred such partial specializations stacked together. Part two of this volume covers SFINAE, and part three covers concepts, and both build on understanding "compile-time type judgment through partial specialization."

## A Few Limits of Partial Specialization

Finally, nail down the boundaries of partial specialization so you do not trip over them.

**Function templates cannot be partially specialized.** Covered in the previous piece. The standard allows partial specialization only for class templates and variable templates (since C++14). For function dispatch, use overloading, `if constexpr`, or SFINAE.

**Partial specialization usually lives at namespace scope.** The vast majority of partial specializations are written at namespace level. Since C++11 (CWG 727), a partial specialization of a member class template may also appear in the scope of the enclosing class, but the syntax is awkward and rarely used. For peace of mind, just put partial specializations at namespace scope.

**A partial specialization can completely redefine the implementation.** It does not need to have the same members as the primary template; it can look entirely different. But that also means anyone accessing it through the primary template's name must make sure the specialization provides the corresponding members, or the code fails to compile for that type. `vector<bool>` does exactly this, with members that are almost the same as an ordinary `vector` but with subtle semantic differences.

**ODR differs between partial and full specialization.** A partial specialization is still a template, and its instantiations across translation units merge automatically without violating ODR. A full specialization is a concrete entity, so its definition may appear in only one translation unit, or it must be marked `inline`.

Next we move into non-type template parameters. They are an important actor in partial-specialization pattern matching, the `N` in `std::array<T, N>` is one, and C++20 loosened the rules for what a non-type parameter can be, expanding from integers and pointers to floating-point values and class types that meet certain conditions.
