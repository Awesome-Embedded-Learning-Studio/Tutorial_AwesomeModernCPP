---
chapter: 6
cpp_standard:
- 17
- 20
description: The CTAD mechanism in C++17 and custom deduction guides
difficulty: intermediate
order: 3
platform: host
prerequisites:
- 'Chapter 6: Deep Dive into auto Deduction: More Than Just Laziness'
reading_time_minutes: 13
related:
- decltype and Return Type Deduction
tags:
- host
- cpp-modern
- intermediate
- 泛型
title: Class Template Argument Deduction (CTAD)
translation:
  source: documents/vol2-modern-features/ch06-auto-decltype/03-ctad.md
  source_hash: c5bf18a9bc6f1ca378a56e3dc05c3c212ee6802660f6638394a7f68dfad91cb1
  translated_at: '2026-09-25T15:48:42+00:00'
  engine: anthropic
  token_count: 3200
---
# Class Template Argument Deduction (CTAD): If It Can Be Deduced, Don't Write It by Hand

Before C++17, every time we instantiated a class template we had to spell out the full template argument list. Even when the compiler could perfectly well deduce the template parameters from the constructor's arguments, we still had to dutifully write them all out:

```cpp
std::pair<int, double> p(1, 2.0);           // this could obviously be deduced
std::tuple<int, float, std::string> t(42, 3.14f, "hi");
std::vector<int> v = {1, 2, 3};              // this one doesn't need much typing
std::lock_guard<std::mutex> lock(mtx);       // the mutex type, written yet again
```

C++17 finally lets us drop these redundant template arguments. The feature is called CTAD (Class Template Argument Deduction). It makes class templates feel more like ordinary classes—the compiler deduces the template parameters automatically from the constructor's arguments, so we no longer specify them by hand.

> In one sentence: **CTAD spares you the trouble of writing class template arguments by hand—the compiler deduces them from the constructor's arguments. And when the defaults aren't what you want, you can write custom deduction guides to override the behavior.**

------

## The Motivation for CTAD

### How Annoying It Used to Be

Let's look at a few situations where, before C++17, we had to write out the full template argument list:

```cpp
// pair's types could be fully deduced from the arguments, but had to be written by hand
auto p = std::pair<int, double>(1, 2.0);

// make_pair solves the problem for pair, but it isn't general
auto p2 = std::make_pair(1, 2.0);

// tuple also requires writing out every type
auto t = std::tuple<int, float, std::string>(42, 3.14f, "hi");

// lock_guard's mutex type has to be written too
std::lock_guard<std::mutex> lock(mtx);
```

`std::make_pair`, `std::make_tuple`, and friends—these "factory functions" exist essentially to work around the fact that class templates could not deduce their own arguments. But they are a special-case workaround, and not every class template comes with a matching make function.

### After CTAD

```cpp
std::pair p(1, 2.0);            // deduced as std::pair<int, double>
std::tuple t(42, 3.14f, "hi");  // deduced as std::tuple<int, float, const char*>
std::lock_guard lock(mtx);      // deduced as std::lock_guard<std::mutex>
```

The code is cleaner, and the pile of make_xxx factory functions is no longer needed. In fact, after C++17 the only remaining use for many make functions is to cover the corner cases of CTAD—most of the time, the bare class name is enough.

------

## CTAD in the Standard Library

C++17 added deduction guides to many of the standard library's class templates. Here are the most commonly used ones:

### pair and tuple

This is the most intuitive use case for CTAD. Each element's type is deduced from the constructor's arguments:

```cpp
std::pair p(1, 2.0);               // std::pair<int, double>
std::pair p2 = {1, 2.0};           // same as above
std::tuple t(1, 2.0, "three");     // std::tuple<int, double, const char*>
```

### vector and Other Containers

`std::vector` has a special deduction guide: the element type is deduced from a pair of iterators:

```cpp
std::vector v1 = {1, 2, 3};                    // std::vector<int>
std::vector v2(v1.begin(), v1.begin() + 2);    // std::vector<int>

// iterating over another container
std::set<int> s = {1, 2, 3};
std::vector v3(s.begin(), s.end());             // std::vector<int>
```

Note: `std::vector v = {1, 2, 3}` deduces fine because the standard library provides a deduction guide for `std::vector` that accepts a `std::initializer_list<T>`. But not every container has such a guide—for example, braced-initialization deduction for `std::map` is not solid in C++17; formal pair-like deduction support only arrives in C++26.

### smart pointers

**Note**: `std::unique_ptr` and `std::shared_ptr` do **not** support CTAD from a raw pointer. The following code fails to compile:

```cpp
// Compile error! Smart pointers don't support CTAD from a new expression
// std::unique_ptr up(new int(42));
// std::shared_ptr sp(new int(42));
```

The reason is that the constructor template argument deduction rules for smart pointers differ from those for ordinary class templates—their constructors accept pointer types, but there is no way to deduce the template arguments from a raw pointer.

**The right approach** is to use `make_unique` and `make_shared` (recommended), or to specify the template arguments explicitly:

```cpp
// Recommended: use the make functions (exception-safe)
auto up1 = std::make_unique<int>(42);
auto sp1 = std::make_shared<int>(42);

// Or specify the template arguments explicitly
std::unique_ptr<int> up2(new int(42));
std::shared_ptr<int> sp2(new int(42));
```

CTAD with smart pointers mostly shows up in scenarios with custom deleters, but even then you still have to spell out the deleter type explicitly:

```cpp
std::unique_ptr<FILE, decltype(&std::fclose)> fp(std::fopen("file.txt", "r"), &std::fclose);
// Template arguments must be written explicitly; no CTAD here
```

### optional and variant

```cpp
std::optional o = 42;          // std::optional<int>
std::optional o2 = 3.14;       // std::optional<double>

// variant's CTAD is special—deduction goes through assignment
std::variant<int, double> v = 42;  // template arguments still written by hand
```

### array

```cpp
std::array a = {1, 2, 3, 4, 5};  // std::array<int, 5>
// The second template argument (the size) is deduced from the length of the braced list
```

This one already works in C++17, and it is particularly handy—no more counting elements by hand.

### Summary: Standard Library CTAD at a Glance

| Class template | CTAD usage | Deduced result | Notes |
|--------|----------|---------|------|
| `std::pair` | `std::pair p(1, 2.0)` | `pair<int, double>` | ✓ Supported |
| `std::tuple` | `std::tuple t(1, 2.0, "hi")` | `tuple<int, double, const char*>` | ✓ Supported |
| `std::vector` | `std::vector v = {1,2,3}` | `vector<int>` | ✓ Supported |
| `std::array` | `std::array a = {1,2,3}` | `array<int, 3>` | ✓ Supported (deduction guide) |
| `std::optional` | `std::optional o = 42` | `optional<int>` | ✓ Supported |
| `std::unique_ptr` | `std::unique_ptr up(new T)` | — | ✗ **Not supported** |
| `std::shared_ptr` | `std::shared_ptr sp(new T)` | — | ✗ **Not supported** |
| `std::lock_guard` | `std::lock_guard lock(mtx)` | `lock_guard<mutex>` | ✓ Supported |

------

## Implicit Deduction Guides

CTAD isn't magic—the compiler knows how to deduce template arguments through deduction guides. If a class template's constructors use all of its template parameters, the compiler automatically generates an implicit deduction guide.

### Deducing from Constructors

```cpp
template<typename T, typename U>
struct MyPair {
    T first;
    U second;
    MyPair(T f, U s) : first(f), second(s) {}
};

MyPair p(1, 2.0);  // implicitly deduced as MyPair<int, double>
```

Seeing the constructor `MyPair(T f, U s)`, the compiler automatically generates an equivalent deduction guide: whenever `int` and `double` arguments are passed, `T` is deduced as `int` and `U` as `double`.

### When There Are Multiple Constructors

If a class template has multiple constructors, the compiler generates an implicit deduction guide for each of them. When we create an object, the compiler tries all the deduction guides and picks the best match:

```cpp
template<typename T>
class Wrapper {
public:
    Wrapper(T val) : value_(val) {}
    Wrapper(const T* ptr) : value_(*ptr) {}
private:
    T value_;
};

Wrapper w1(42);        // uses the first constructor; deduced as Wrapper<int>
int x = 10;
Wrapper w2(&x);        // uses the second constructor; deduced as Wrapper<int>
```

### The Limits of Implicit Deduction

Implicit deduction guides cannot deduce nested template parameters. For example, given a `Container<std::vector<T>>`, implicit deduction cannot work backwards from `std::vector<int>` to `T = int`. That takes a custom deduction guide.

On top of that, when a constructor has default arguments, the implicit deduction guide only considers the parameters without defaults. Template parameters with default values won't be deduced automatically—unless you write a custom deduction guide.

------

## Custom Deduction Guides

When the implicit deduction guides aren't enough, you can write deduction guides by hand. The syntax looks a bit like a function signature:

```cpp
template<typename ...>
ClassName(params) -> ClassName<deduced types>;
```

### A Basic Example

Suppose we have a strong-typedef wrapper for distinguishing values with different units:

```cpp
template<typename T, typename Tag>
class StrongType {
public:
    explicit StrongType(T value) : value_(value) {}
    T get() const { return value_; }
private:
    T value_;
};

struct MeterTag {};
struct SecondTag {};

using Meter  = StrongType<double, MeterTag>;
using Second = StrongType<double, SecondTag>;
```

In this class, only the template parameter `T` appears in the constructor; `Tag` doesn't show up there at all. Implicit deduction can deduce `T`, but not `Tag`. In this situation CTAD isn't a great fit—just use a `using` alias instead.

But let's change the design so that Tag can take part in deduction too:

```cpp
template<typename T, typename Tag>
class StrongType {
public:
    explicit StrongType(T value) : value_(value) {}
    T get() const { return value_; }
private:
    T value_;
};

// Custom deduction guide: deduce from the value type
template<typename T>
StrongType(T) -> StrongType<T, struct DefaultTag>;

StrongType s(42);  // StrongType<int, DefaultTag>
```

### A More Practical Deduction Guide

A more practical scenario is a custom container. Suppose we have a simple fixed-size buffer:

```cpp
template<typename T, std::size_t N>
class FixedBuffer {
public:
    FixedBuffer(std::initializer_list<T> init) {
        std::copy(init.begin(), init.begin() + N, data_.begin());
    }

    // ... other members

private:
    std::array<T, N> data_;
};

// Custom deduction guide: deduce T and N from the braced list
template<typename T, typename... Args>
FixedBuffer(T, Args...) -> FixedBuffer<T, 1 + sizeof...(Args)>;
```

With this deduction guide, we can create a buffer like this:

```cpp
FixedBuffer buf = {1, 2, 3, 4, 5};  // FixedBuffer<int, 5>
```

Deduction guides work much like overload resolution for function templates. The compiler considers all deduction guides—both implicitly generated ones and user-written ones—and picks the best match. If a custom deduction guide fits better than the implicit ones, the compiler chooses the custom one.

How constructor arguments determine template arguments, and how a guide teaches the compiler to deduce when plain deduction comes up short, has been turned into an animation—you can play it, pause it, or step through it one keypress at a time to see every step of the deduction clearly:

<Anim id="ctad-deduction" />

### Custom Deduction Guides in the Standard Library

The standard library itself makes heavy use of custom deduction guides. For example, the guide that lets `std::vector` deduce from a pair of iterators:

```cpp
// Roughly equivalent to the deduction guide in the standard library
template<typename InputIt>
vector(InputIt, InputIt) -> vector<typename iterator_traits<InputIt>::value_type>;
```

This guide is what lets `std::vector v(it1, it2)` deduce the element type correctly, instead of trying to treat the iterator type as the element type.

------

## CTAD's Limits and Pitfalls

### Aggregates Don't Support CTAD in C++17

C++17's CTAD does not support aggregate types. An aggregate is a class with no user-declared constructors, no private or protected members, and no base classes. `std::array` is an aggregate underneath; the reason it supports CTAD is that the standard library wrote a dedicated deduction guide for it.

```cpp
template<typename T, std::size_t N>
struct MyArray {
    T data[N];
    // No constructor—an aggregate
};

MyArray a = {1, 2, 3};  // C++17: compile error! Aggregates don't support CTAD
```

### C++20: The Limits of Aggregate CTAD

**Important clarification**: C++20 did **not** add general CTAD support for all aggregate types. The following code **still fails to compile** in C++20:

```cpp
template<typename T, std::size_t N>
struct MyArray {
    T data[N];  // No constructor, an aggregate
};

MyArray a = {1, 2, 3};  // C++20: still a compile error!
```

C++20's support for aggregate CTAD is very limited—the main improvement allows deduction in certain specific scenarios, but there is no general aggregate CTAD. To make the code above work, we still need to write a deduction guide by hand or add a constructor.

**Why does `std::array` work with CTAD?**

The reason `std::array a = {1, 2, 3}` works is that the standard library provides a dedicated deduction guide for it—not because of C++20's aggregate CTAD:

```cpp
// The standard library's deduction guide (simplified)
template<typename T, typename... Args>
array(T, Args...) -> array<T, 1 + sizeof...(Args)>;
```

If you need your own aggregate type to support CTAD, the most reliable route is to add a deduction guide or provide a constructor.

### Alias Templates Don't Support CTAD

We can't deduce arguments through an alias template directly—an alias template is not a class template, and CTAD applies only to class templates:

```cpp
template<typename T>
using MyVec = std::vector<T, MyAllocator<T>>;

MyVec v = {1, 2, 3};  // Compile error: alias templates don't support CTAD
```

C++20 introduced support for deduction guides on alias templates, but the rules are fairly intricate, and many compilers' support is incomplete.

### Forwarding References and CTAD

When a constructor takes a forwarding reference, CTAD may deduce unexpected types, because a forwarding reference can match any type—including reference types:

```cpp
template<typename T>
struct Wrapper {
    Wrapper(T&& val) : value_(std::forward<T>(val)) {}
    T value_;
};

int x = 42;
Wrapper w(x);  // T deduced as int& (not int!)
```

Here, under the forwarding-reference rules, when the lvalue `x` is passed, `T` is deduced as `int&`. So `Wrapper w(x)` has type `Wrapper<int&>`, and its member `value_` has type `int&`. That is probably not the behavior we want. The fix is to use `std::remove_reference_t` or a custom deduction guide to constrain the deduction result.

### Copy Initialization vs Direct Initialization

CTAD can behave differently in copy initialization (`=`) versus direct initialization (`()`):

```cpp
std::vector v1{1, 2, 3};        // Direct initialization; CTAD works
std::vector v2 = {1, 2, 3};     // Copy initialization; CTAD works (there's a dedicated deduction guide)

// Some custom types may work in only one of the two forms
```

A word of advice: if CTAD doesn't work under one initialization form, try the other. Or check whether your deduction guides actually cover that form of initialization.

------

## In Practice: Deduction Guides for a Strong-Typedef Wrapper

Let's write a complete example showing how CTAD makes a strong-typedef wrapper feel more natural to use.

```cpp
#include <cstdint>
#include <utility>

/// @brief A strong-typedef wrapper that prevents mixing types with different semantics
template<typename T, typename Tag>
class StrongTypedef {
public:
    explicit constexpr StrongTypedef(T value) : value_(value) {}
    constexpr T& get() { return value_; }
    constexpr const T& get() const { return value_; }
private:
    T value_;
};

// Tag types (empty classes, take up no space)
struct MeterTag {};
struct KilometerTag {};
struct CelsiusTag {};

// Aliases
using Meter     = StrongTypedef<double, MeterTag>;
using Kilometer = StrongTypedef<double, KilometerTag>;
using Celsius   = StrongTypedef<double, CelsiusTag>;

// Custom deduction guide: a literal automatically deduces to the corresponding type
// (Not really needed in this example, since we already have using aliases)
// but it demonstrates the syntax
template<typename T>
StrongTypedef(T) -> StrongTypedef<T, struct GenericTag>;

// Usage
int main() {
    Meter distance(100.0);
    Celsius temp(23.5);

    // distance + temp is a compile error—different Tags can't be mixed
    // This is the core value of strong types
}
```

This example showcases CTAD's design philosophy: for types that already have a `using` alias (like `Meter`), just construct through the alias—no CTAD needed. CTAD shines more in scenarios where the template arguments follow naturally from the constructor's arguments.

------

## References

- [cppreference: Class template argument deduction](https://en.cppreference.com/w/cpp/language/class_template_argument_deduction)
- [CTAD in C++17 - Simon Toth](https://medium.com/@simontoth/daily-bit-e-of-c-class-template-argument-deduction-ctad-f0886131c129)
- [C++17's CTAD - Andreas Fertig](https://andreasfertig.com/blog/2022/11/cpp17s-ctad-a-sometimes-underrated-feature/)
