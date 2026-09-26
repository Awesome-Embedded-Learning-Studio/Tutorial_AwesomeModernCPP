---
chapter: 12
cpp_standard:
- 11
- 14
- 17
description: 'Specialization is the most expressive part of templates. This piece
  sorts out the difference between full and partial specialization, the priority
  rule by which the compiler picks the "most specialized" version, and two classic
  applications: why the standard library vector<bool> is a partial specialization,
  and how the whole type_traits machinery of is_pointer/is_const is built on partial
  specialization.'
difficulty: intermediate
order: 4
platform: host
prerequisites:
- 'Class Templates: Members, Dependent Names, and Lazy Instantiation'
- 'Function Templates, In Depth: Compilation Model and the No-Partial-Specialization
  Trap'
reading_time_minutes: 10
related:
- 'Non-Type Template Parameters: From Integers to C++20 Floats and Class Types'
- 'Name Lookup and ADL: How Two-Phase Lookup Works'
tags:
- host
- cpp-modern
- intermediate
- 模板
- 泛型
title: 'Template Specialization and Partial Specialization: The Art of Pattern Matching'
translation:
  source: documents/vol4-advanced/vol1-basics-cpp11-14/04-specialization-partial.md
  source_hash: f17650b53679f4c070eaa8f9c9d52d02e8053af5f351ea28961d98a61ac2aeaa
  translated_at: '2026-09-26T03:58:54+00:00'
  engine: anthropic
  token_count: 2100
---
# Template Specialization and Partial Specialization: The Art of Pattern Matching

The most powerful thing about templates is not "write one recipe that fits every type." It is **writing a separate, different implementation for certain specific types, or for a whole family of types**. The mechanism is called specialization, and it comes in two forms: full specialization targets one concrete type; partial specialization targets a family of types sharing a pattern. In the previous piece we noted that function templates cannot be partially specialized — only class templates and variable templates can. This piece takes class-template specialization all the way to the bottom: full specialization, partial specialization, the priority rule for which version the compiler picks, and two classic cases that push partial specialization to its limit — the standard library's `std::vector<bool>` and the whole of `<type_traits>`.

## Full Specialization: A Separate Implementation for One Concrete Type

Full specialization provides a dedicated implementation for one **fully determined** type. The syntax opens with `template<>`, and every template parameter is pinned down.

```cpp
// primary template
template <typename T>
struct TypeSize {
    static constexpr std::size_t value = sizeof(T);
};

// full specialization: a separate copy just for int
template <>
struct TypeSize<int> {
    static constexpr std::size_t value = 4;   // assume a 32-bit platform where int is 4 bytes
};
```

With this full specialization in place, `TypeSize<int>::value` takes the `4` from the specialization, while `TypeSize<double>::value` goes through the primary template and computes `sizeof(double)`. A full specialization is like cutting in line: right where the compiler would have instantiated `TypeSize<int>` from the primary template, it sees a ready-made full specialization sitting there, uses it directly, and never generates the primary template's version.

Two properties of full specialization are worth committing to memory. First, a full specialization **is no longer a template** — it is a concrete class (or function, or variable) with every template parameter already fixed. This has ODR consequences: the definition of a full specialization may appear in exactly one translation unit, otherwise it is a duplicate definition. Second, a full specialization's signature must correspond precisely to some instantiation of the primary template — not the slightest deviation is allowed.

## Partial Specialization: A Separate Implementation for a Family of Type Patterns

Partial specialization targets not a single concrete type but **a family of type patterns**. The most common patterns are "pointer types" and "reference types." The syntax opens with `template <typename T>` (note that this is still a template — there is still an unbound `T`), and the class name is then followed by a patterned argument such as `<T*>`.

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

Note the key difference between partial and full specialization: the partial specialization's `template <typename T>` header still carries a `T`, so it is still a template; the full specialization's `template<>` is empty, with every parameter pinned. This difference is what allows a partial specialization to match "a family of types" while a full specialization can match only "one type."

Let's run it and watch how the picks happen:

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

(The full specialization for `int*` is not defined yet, so main stays away from `int*` for now; once the next section adds the full specialization, we will see how `int*` gets chosen.)

`double` matches no partial specialization and takes the primary template; `double*` matches the `T*` partial specialization; `double&` matches the `T&` partial specialization. This is the core capability of partial specialization: **routing types into different implementations by their "shape"**, like a compile-time type switch.

## Priority: Full Specialization > Partial Specialization > Primary Template

The `int*` case we parked above deserves its own look. `int*` matches both the `T*` partial specialization (with `T=int`) and the following full specialization:

```cpp
// full specialization: specifically for int*
template <>
struct TypeKind<int*> {
    static const char* name() { return "int* (full, wins over partial)"; }
};
```

Both match — which one does the compiler choose? The rule is **choose the more specialized one**. A full specialization is more specialized than a partial specialization (it has no unbound parameters), and a partial specialization is more specialized than the primary template. So the priority order is: **full specialization > partial specialization > primary template**. Add the full specialization above into the file, have main print one more line `TypeKind<int*>::name()`, and the output becomes `int* (full, wins over partial)` — `int*` hits the full specialization instead of falling through to the `T*` partial specialization.

When several partial specializations all match, the rule gets a little subtler: the compiler compares which of them is "more concrete." For example, between the two partial specializations `T*` and `T const*`, for `const int*` the `T const*` version is more concrete and wins. There is a formal set of rules behind this "more specialized than" judgment; for everyday coding it is enough to remember "the compiler picks the tightest fit." If a genuine ambiguity ever comes up, the compiler reports `ambiguous`, and you adjust the patterns of your partial specializations accordingly.

## Which Patterns a Partial Specialization Can Match

The expressive power of partial specialization lies in how rich the set of "patterns" it can match is. Common ones:

- `T*`: pointer types
- `T&` / `T&&`: reference types
- `const T*`, `volatile T`: with cv-qualifiers
- `T[N]`: array types
- `Foo<T>`, `Bar<T, U>`: instantiations of some specific template
- even template template parameters can join the pattern matching

Combine these, and partial specialization can recognize almost any "type shape." That is exactly the foundation `<type_traits>` stands on to offer so many type queries, as we will see below.

## Classic Case 1: The std::vector\<bool\> Partial Specialization

The most famous partial specialization in the standard library is `std::vector<bool>`. It is not the ordinary version that falls out of instantiating `std::vector<T>` with `T=bool`; it is a partial specialization the library writes on purpose. The goal is to make one `bool` occupy a single bit instead of a full byte, to save memory.

```cpp
// roughly what the standard library does inside (simplified sketch)
template <typename T, typename Alloc = std::allocator<T>>
class vector { /* ordinary implementation: one T per slot of storage */ };

template <typename Alloc>
class vector<bool, Alloc> { /* partial specialization: bit packing, one bool per bit */ };
```

The `vector<bool>` partial specialization has a side effect you can see with the naked eye: its `operator[]` does not return `bool&` but a **proxy object** named `std::vector<bool>::reference`. The reason: after bit packing you cannot return "a reference to one bit" (the smallest addressable unit of memory is a byte), so all it can do is return a temporary object that simulates the behavior of `bool&`. Run it and the difference is plain to see:

```cpp
#include <iostream>
#include <type_traits>
#include <vector>

int main() {
    std::cout << std::boolalpha;
    // vector<bool>'s reference is a proxy class, not bool&
    std::cout << "vector<bool>::reference is bool&?   "
              << std::is_same_v<std::vector<bool>::reference, bool&> << "\n";
    // an ordinary vector's reference is just a reference to the element
    std::cout << "vector<char>::reference is char&?  "
              << std::is_same_v<std::vector<char>::reference, char&> << "\n";
}
```

```bash
$ g++ -Wall -Wextra -std=c++20 vecbool.cpp -o vecbool && ./vecbool
vector<bool>::reference is bool&?   false
vector<char>::reference is char&?  true
```

This proxy `reference` has stirred up no small controversy for `vector<bool>`. It makes `vector<bool>` fall short of the "sequence container" requirements (because `reference` is not a genuine reference to the element), and it makes some generic code misbehave on top of it. For example, `auto& x = vec[0];` **flat-out fails to compile** on `vector<bool>` (the proxy `reference` is an rvalue temporary, which cannot bind to a non-const lvalue reference); and even rewritten as `auto x = vec[0];`, what you get is still the proxy object, not a `bool`, so certain operations behave unexpectedly. The committee has discussed more than once whether to pull it out of the standard and replace it with a standalone facility such as `dynamic_bitset`, but it is already used everywhere and the cost of changing it is too high, so it just stays. It is a cautionary tale about partial specialization: a partial specialization may completely redefine the implementation, but the price is that it may no longer satisfy the interface contract implied by the primary template, and users fall into the pit.

## Classic Case 2: The Entire type_traits Playbook

Under the hood, the queries in `<type_traits>` — `std::is_pointer`, `std::is_const`, `std::is_reference` — all run on the same playbook: "primary template answers false + a partial specialization that hits the pattern answers true." Hand-write an `is_pointer` ourselves and the trick is exposed:

```cpp
// primary template: not a pointer by default
template <typename T>
struct is_pointer {
    static constexpr bool value = false;
};

// partial specialization: only pointer types hit this
template <typename T>
struct is_pointer<T*> {
    static constexpr bool value = true;
};
```

Run it:

```bash
$ g++ -Wall -Wextra -std=c++20 is_ptr.cpp -o is_ptr && ./is_ptr
is_pointer<int>::value       = false
is_pointer<int*>::value      = true
is_pointer<int**>::value     = true
is_pointer<int&>::value      = false
```

`int` takes the primary template, `value=false`; `int*` hits the `T*` partial specialization, `value=true`; `int**` hits it too (with `T=int*`); `int&` is a reference, not a pointer, so it takes the primary template, `false`.

That is the whole secret of `std::is_pointer`. The standard library's `is_pointer` does a little more than this one (it also has to handle edges such as pointers to members and cv-qualification), but the core idea is exactly this primary-template-plus-partial-specialization pattern. `is_const` (`const T*` does not count, `T const` does), `is_reference` (`T&` and `T&&`), `is_array` (`T[N]`) — all of them use the same method: the primary template supplies a default value, and partial specializations override it for the target pattern.

Once this playbook clicks, you can write your own type queries. To ask "is this a function pointer," write the primary template defaulting to false and a partial specialization on `R(*)(Args...)` that answers true. The entire `<type_traits>` is piled up from a few hundred partial specializations like these. Later on, Part 2 covers SFINAE and Part 3 covers concepts — and both are built on this one understanding of yours: "making compile-time type judgments with partial specialization."

## A Few Limits of Partial Specialization

Finally, let's make the boundaries of partial specialization clear, so you don't step into the pits.

**Function templates cannot be partially specialized.** The previous piece covered this one in its own right: the standard permits partial specialization only for class templates and variable templates (since C++14). To route function behavior, use overloading, `if constexpr`, or SFINAE.

**Partial specializations usually live at namespace scope.** The overwhelming majority of partial specializations sit at namespace level. Since C++11 (CWG 727), a partial specialization of a member class template may also appear inside the scope of the enclosing class, but the syntax is roundabout and it is seldom used; in everyday code, just put partial specializations at namespace scope and spare yourself the trouble.

**A partial specialization may completely redefine the implementation.** A partial specialization need not have the same members as the primary template; it can grow an entirely different shape. But that also means anyone accessing it through the primary template's name must be sure the partial specialization provides the corresponding members — otherwise the build breaks as soon as the type changes. `vector<bool>` does exactly this: its members are nearly the same as an ordinary `vector`'s, but with subtly different semantics.

**Partial and full specialization differ in ODR treatment.** A partial specialization is still a template: its instantiations across multiple translation units merge automatically, with no ODR violation. A full specialization is a concrete entity; its definition may live in only one translation unit, or be marked `inline`.

In the next piece we move on to non-type template parameters. Non-type parameters play a major role in the pattern matching of partial specialization — the `N` in `std::array<T, N>` is one — and C++20 greatly widened the types a non-type parameter can accept, from only integers and pointers to floating-point values and even class types that meet the requirements.
