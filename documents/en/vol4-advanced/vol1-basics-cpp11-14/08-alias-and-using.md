---
title: "Alias Templates and using Declarations: Short Names for Types"
description: "C++11 alias templates (template<typename T> using X = ...) fix the old problem that typedef cannot be parameterized. The _t alias (from C++14) and the _v variable (from C++17) make type_traits clean to write, and using introduces dependent-base names in template inheritance. This piece covers all three uses."
chapter: 12
order: 8
tags:
  - host
  - cpp-modern
  - intermediate
  - 模板
  - 类型别名
  - 泛型
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17]
reading_time_minutes: 16
prerequisites:
  - "Name Lookup and ADL: How Two-Phase Lookup Works"
  - "Class Templates: Members, Dependent Names, and Lazy Instantiation"
related:
  - "Template Friends and Barton-Nackman: The Hidden Friends Trick"
  - "CRTP: Static Polymorphism with the Curiously Recurring Template Pattern"
---

# Alias Templates and using Declarations: Short Names for Types

C++11 upgraded type aliasing with a new use of `using` and the **alias template**. The old `typedef` can give one type a new name, but it cannot be parameterized. You want a short name for `std::vector<T>`, and `typedef` cannot do it; you have to go through a nested type inside a class template. `using` plus alias templates fix this directly. This piece covers three uses: the alias template itself, the `_t` alias (from C++14) and the `_v` variable (from C++17) used to type_traits, and the role of `using` in introducing base-class names in template inheritance (a follow-up to the `this->` discussion in piece three).

## The Limit of typedef: No Parameterization

`typedef` is an old C tradition, giving a type a new name.

```cpp
typedef std::vector<int> IntVec;   // IntVec is std::vector<int>
IntVec v;
```

That works. But if you want a general alias for "a `vector` of any `T`," `typedef` is stuck. It cannot take template parameters. The pre-C++11 workaround borrows a nested `using` inside a class template.

```cpp
template <typename T>
struct VecHelper {
    using type = std::vector<T>;   // nested inside a class template, so it can be parameterized
};

VecHelper<int>::type v;   // again with ::type, noisy
```

This runs, but every use spells `VecHelper<int>::type`, and as noted in the piece on dependent names, you also need `typename`, turning it into `typename VecHelper<T>::type`. Long and awkward.

## C++11 Alias Templates: Parameterizing using

The C++11 alias template cleans this up. The syntax is `template <...> using name = ...`, giving a parameterized type a direct alias.

```cpp
template <typename T>
using Vec = std::vector<T>;   // alias template

Vec<int> v = {1, 2, 3};        // equivalent to std::vector<int>
```

Run it.

```bash
$ g++ -Wall -Wextra -std=c++17 alias.cpp -o alias && ./alias
size = 3
```

`Vec<int>` is `std::vector<int>`, with no difference in use. Note that an alias template is not a new type. It is purely an "alias." `Vec<int>` and `std::vector<int>` are the same type, fully interchangeable for assignment, comparison, and overloading. This matches `typedef` semantics, with parameterization added.

The payoff of alias templates is more than brevity. They can also express some complex types that `typedef` struggles with, like function pointer types or containers with allocators, where `using` reads far more clearly than `typedef`.

```cpp
// typedef for function pointer types: the ordering makes your head hurt
typedef int (*Callback)(int, int);

// using: left-to-right, consistent, far more readable
using Callback = int(*)(int, int);
```

Modern C++ basically replaces `typedef` with `using` everywhere, even without parameters, for a consistent style.

## C++14 `_t` and C++17 `_v`: The type_traits Shortcut

The most practical application of alias templates is the `_t` suffix that C++14 added to `<type_traits>`. C++11 type_traits results are nested `::type` or `::value` inside a class, noisy to use.

```cpp
// C++11: to get the type with reference removed, you need typename + ::type
typename std::remove_reference<T>::type
```

C++14 added a `_t` alias template for every type-returning trait, one line.

```cpp
// C++14: alias template, clean
std::remove_reference_t<T>
```

The two are fully equivalent. The `_t` version is just an alias template, defined roughly as `template <typename T> using remove_reference_t = typename remove_reference<T>::type;`. Traits that return a boolean get a `_v` suffix, so `std::is_integral<T>::value` shortens to `std::is_integral_v<T>`. Three things need separating here: the `_t` alias template and the `_v` variable template are both **standard-library helpers** (`_t` from C++14 onward, `_v` from C++17 onward); `::value` itself is a static member constant of `std::integral_constant`, present since C++11, and is a separate thing from variable templates.

A quick check shows the two spellings are equivalent.

```bash
$ g++ -Wall -Wextra -std=c++17 alias.cpp -o alias && ./alias
remove_reference_t<int&> is int?  true
remove_reference<int&>::type is int? true
```

`_t` lifts the readability of template metaprogramming by a notch. In part three, when we cover concepts and metaprogramming, you will see that `::type` has basically vanished from modern code, replaced entirely by `_t`. This is also why the type_traits examples in earlier pieces of this volume use `_v` suffixes directly (`is_pointer_v`, `is_same_v`). They are the variable-template shortcuts (C++17 onward), the same idea as the `_t` alias templates.

## Alias Templates Cannot Be Specialized

There is a limit alias templates cannot get past: **they cannot be specialized**, neither fully nor partially. If you want a special alias implementation for one concrete type, an alias template cannot do it.

```cpp
template <typename T>
using V = T;

// trying to specialize an alias template -- compile error
template <>
using V<int> = long;   // error: alias templates cannot be specialized
```

```text
alias_bad.cpp:6:1: error: expected unqualified-id before 'using'
```

GCC's wording is a little abstract, but the meaning is "alias templates do not accept specialization." If you genuinely need "different type aliases for different types," you wrap it in a class template (class templates can be specialized), hiding the alias in a nested `using` and writing specializations of the wrapper. This is a capability gap of alias templates compared with class templates. By design, alias templates are positioned as "pure forwarding," not for type-computation dispatch.

## using in Template Inheritance: Introducing dependent-base Names

Piece three, on dependent bases, said that accessing members of a base class template requires `this->`, because the compiler does not look inside a dependent base at phase one. `using` offers another spelling: **use a `using` declaration to bring the base-class name into the derived-class scope**, and then calls no longer need `this->` each time.

```cpp
#include <iostream>

template <typename T>
struct Base {
    static T kDefault;
    void greet() { std::cout << "Base::greet\n"; }
};
template <typename T>
T Base<T>::kDefault{42};

template <typename T>
struct Derived : Base<T> {
    // using brings Base<T>::kDefault and Base<T>::greet into the Derived scope
    using Base<T>::kDefault;
    using Base<T>::greet;

    T fetch() const { return kDefault; }   // direct use, no this->
    void hello() { greet(); }              // direct use, no this->
};
```

Run it.

```bash
$ g++ -Wall -Wextra -std=c++20 using_base.cpp -o using_base && ./using_base
fetch = 42
Base::greet
```

`using Base<T>::kDefault` tells the compiler "the name `kDefault` refers to the one in `Base<T>`," binding the lookup so that a later bare `kDefault` in `fetch` finds it without `this->`.

`using` injection and `this->` are two spellings for the same problem, and the choice depends on the situation. If you only occasionally touch one or two base-class members, `this->` written on the spot is lighter. If you frequently touch many base-class members (say, the derived class uses the base's type aliases and functions everywhere), a batch of `using` declarations at the top of the class makes the code cleaner. Both are legitimate modern spellings. `using` has a bonus: it can "inherit" base-class type aliases (`value_type`, `iterator`) so the derived class exposes a unified type interface, something STL container adaptors and derived classes do heavily.

## Alias Templates and Template Argument Deduction

A final note on a post-C++14 development. Alias templates can participate in template argument deduction, which makes code more flexible. For example, if a function takes `std::vector<T>` and you pass a `Vec<int>` (an alias), deduction still works, because the alias is the original type. C++20 CTAD (class template argument deduction) also interacts with alias templates, allowing deduction of alias-template parameters from constructors. That is deeper territory, covered in part three when we discuss concepts and deduction. For this piece, just remember: an alias template is "transparent," fully identical to the original type it points to for deduction, overloading, and type equivalence.

Next is the centerpiece of the concept portion of this volume: CRTP, the curiously recurring template pattern. With the curious structure of "a derived class passes itself as a template argument to its base," it achieves compile-time static polymorphism and avoids the runtime cost of virtual functions. It is a core technique in high-performance libraries like Eigen and expression templates.
