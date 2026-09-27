---
chapter: 12
cpp_standard:
- 11
- 14
- 17
description: C++11 alias templates (template<typename T> using X = ...) solve the old
  problem that typedef cannot be parameterized, the _t aliases since C++14 and the
  _v variables since C++17 make type traits pleasant to write, and using declarations
  bring dependent-base names into scope in template inheritance. This piece walks through
  all three uses.
difficulty: intermediate
order: 8
platform: host
prerequisites:
- 'Name Lookup and ADL: How Two-Phase Lookup Works'
- 'Class Templates: Members, Dependent Names, and Lazy Instantiation'
reading_time_minutes: 7
related:
- 'Template Friends and Barton-Nackman: The Hidden Friends Trick'
- 'CRTP: Static Polymorphism with the Curiously Recurring Template Pattern'
tags:
- host
- cpp-modern
- intermediate
- 模板
- 类型别名
- 泛型
title: 'Alias Templates and using Declarations: Short Names for Types'
translation:
  source: documents/vol4-advanced/vol1-basics-cpp11-14/08-alias-and-using.md
  source_hash: 2f192dbed15fa7d52b886d87725a22089fc0cf9b1240d26f83d3dff7135e575c
  translated_at: '2026-09-26T04:13:46+00:00'
  engine: anthropic
  token_count: 4500
---
# Alias Templates and using Declarations: Short Names for Types

C++11 gave type aliasing a major upgrade: a new use of `using` and the **alias template**. The old `typedef` can give a type another name, but it cannot be parameterized—you want a short name for `std::vector<T>`, `typedef` cannot do it, and the only workaround is a detour through a nested type of a class template. `using` plus alias templates solve this directly. This piece covers three uses: alias templates themselves, the `_t` aliases that type traits gained starting with C++14 (plus the `_v` variables added in C++17), and the role of `using` declarations in bringing base-class names into scope under template inheritance (picking up the `this->` thread from the third piece).

## The Limits of typedef: It Cannot Be Parameterized

`typedef` is an old C tradition: it gives a type a new name.

```cpp
typedef std::vector<int> IntVec;   // IntVec is std::vector<int>
IntVec v;
```

That works fine. But the moment you want a generic alias for "a `vector` of arbitrary `T`", `typedef` is helpless—it cannot carry template parameters. The pre-C++11 workaround was a nested `using` inside a class template:

```cpp
template <typename T>
struct VecHelper {
    using type = std::vector<T>;   // nested inside a class template, so it can take parameters
};

VecHelper<int>::type v;   // ::type again — wordy
```

It runs, but every use spells out `VecHelper<int>::type`, and—as we said earlier when discussing dependent names—you must also add `typename`, giving `typename VecHelper<T>::type`: long and awkward.

## C++11 Alias Templates: using Becomes a Template

C++11's alias templates clean the whole thing up. The syntax is `template <...> using name = ...`, which directly aliases a "type with parameters":

```cpp
template <typename T>
using Vec = std::vector<T>;   // alias template

Vec<int> v = {1, 2, 3};        // equivalent to std::vector<int>
```

Run it:

```bash
$ g++ -Wall -Wextra -std=c++17 alias.cpp -o alias && ./alias
size = 3
```

`Vec<int>` is `std::vector<int>`; there is no difference in use. Note that an alias template is not a new type—it is purely an "alias": `Vec<int>` and `std::vector<int>` are the same type, fully interchangeable in assignment, comparison, and overloading. The semantics match `typedef`, with parameterization added on top.

The payoff of alias templates goes beyond brevity. They can also express complex types that `typedef` cannot—function pointer types, containers with allocators—and written with `using` they come out far clearer than `typedef`:

```cpp
// writing a function pointer type with typedef: the twisted order hurts your head
typedef int (*Callback)(int, int);

// with using, left and right read consistently — much more readable
using Callback = int(*)(int, int);
```

Modern C++ has largely replaced `typedef` with `using`—`using` even for aliases without parameters, for a consistent style.

## C++14 `_t` and `_v`: Shorthand for Type Traits

The most practical application of alias templates is the set of `_t`-suffixed aliases C++14 added to `<type_traits>`. C++11 type traits report their results as `::type` or `::value` nested inside a class, which is wordy to use:

```cpp
// C++11: to get the reference-stripped type, you must write typename + ::type
typename std::remove_reference<T>::type
```

C++14 gave every type-returning trait a `_t` alias template—one line and done:

```cpp
// C++14: an alias template — clean
std::remove_reference_t<T>
```

The two are exactly equivalent: the `_t` version is just an alias template, defined roughly as `template <typename T> using remove_reference_t = typename remove_reference<T>::type;`. Traits that return a boolean also got a `_v`-suffixed shorthand: `std::is_integral<T>::value` becomes `std::is_integral_v<T>`. Three things need to be kept apart here: the `_t` alias templates and the `_v` variable templates are both **standard library helpers** (`_t` since C++14, `_v` since C++17); `::value` itself, on the other hand, is a static member constant of `std::integral_constant`, there since C++11—a different thing altogether from variable templates.

Let's verify the equivalence of both spellings:

```bash
$ g++ -Wall -Wextra -std=c++17 alias.cpp -o alias && ./alias
remove_reference_t<int&> is int?  true
remove_reference<int&>::type is int? true
```

`_t` lifts the readability of template metaprogramming code by a whole notch. As you will see when vol3 covers concepts and metaprogramming, `::type` has all but vanished from modern code—everything is `_t`. That is also why the type traits examples in this volume's earlier pieces use the `_v` suffix directly (`is_pointer_v`, `is_same_v`): they are shorthands for variable templates (C++14/17), the same idea as the `_t` alias templates.

## Alias Templates Cannot Be Specialized

Alias templates carry one limitation you cannot get around: **they cannot be specialized**—neither fully nor partially. If you want to provide a special alias implementation for one specific type, an alias template cannot do it.

```cpp
template <typename T>
using V = T;

// attempting to specialize an alias template — compile error
template <>
using V<int> = long;   // error: alias templates cannot be specialized
```

```text
alias_bad.cpp:6:1: error: expected unqualified-id before 'using'
```

GCC's wording is a little abstract, but the message is "alias templates do not accept specialization". If you genuinely need "different type aliases for different types", wrap things in a class template (class templates can be specialized), hide the alias in a nested `using`, and then write specializations of that. This is a capability gap of alias templates relative to class templates, and it is deliberate by design: alias templates are positioned as "pure forwarding" and do no dispatching of type computations.

## using in Template Inheritance: Introducing Dependent Base Names

As the third piece said while discussing dependent bases, accessing members of a base template takes `this->`, because the compiler does not look into dependent bases during phase one. `using` offers another way to write it: **a `using` declaration brings the base class's names into the derived class's scope**, after which calls no longer need `this->` every time.

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
    // using brings Base<T>::kDefault and Base<T>::greet into Derived's scope
    using Base<T>::kDefault;
    using Base<T>::greet;

    T fetch() const { return kDefault; }   // use directly, no this->
    void hello() { greet(); }              // use directly, no this->
};
```

Run it:

```bash
$ g++ -Wall -Wextra -std=c++20 using_base.cpp -o using_base && ./using_base
fetch = 42
Base::greet
```

`using Base<T>::kDefault` tells the compiler "the name `kDefault` refers to the one inside `Base<T>`", which binds the lookup, so a bare `kDefault` written in `fetch` afterwards is found directly—no `this->` needed.

`using` declarations and `this->` are two spellings for the same problem; which one to choose depends on the situation. If you only occasionally access one or two base-class members, writing `this->` on the spot is lighter. If you frequently access many base-class members (say the derived class uses the base's type aliases and functions everywhere), concentrating a set of `using` declarations at the top of the class reads cleaner. Both are legitimate modern style, and `using` has one extra benefit: it can "inherit" the base class's type aliases (`value_type`, `iterator`, that sort), letting the derived class expose a unified type interface to the outside—STL container adapters and derived classes lean on this heavily.

## Alias Templates and Template Argument Deduction

Finally, a development from after C++14. Alias templates can participate in template argument deduction, which makes code more flexible. Say you have a function taking `std::vector<T>`: passing in a `Vec<int>` (the alias) deduces just fine, because the alias is the original type. C++20's CTAD (class template argument deduction) also interacts with alias templates, allowing alias template parameters to be deduced from constructors—that territory is deeper, and vol3 treats it in detail when it covers concepts and deduction. The one thing to remember from this piece: alias templates are "transparent"—in deduction, overloading, and type equivalence they behave exactly like the original type they point to.

The next piece is the headline act of this volume's concepts run: CRTP, the Curiously Recurring Template Pattern. With the curious structure of "the derived class passing itself as a template argument to the base class", it achieves compile-time static polymorphism and sidesteps the runtime cost of virtual functions—the core technique behind high-performance libraries such as Eigen and expression templates.
