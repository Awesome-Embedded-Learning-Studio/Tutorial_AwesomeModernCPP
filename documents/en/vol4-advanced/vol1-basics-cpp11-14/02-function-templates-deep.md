---
chapter: 12
cpp_standard:
- 11
- 14
- 17
description: 'Re-reading function templates from a library writer''s angle: why the
  inclusion model forces templates into headers, how explicit instantiation and extern
  template control code bloat, and the classic trap that function templates cannot
  be partially specialized.'
difficulty: intermediate
order: 2
platform: host
prerequisites:
- Volume 1 · Function Templates
- 'Templates, From Scratch: A Code Recipe with Placeholders'
reading_time_minutes: 11
related:
- 'Class Templates: Members, Dependent Names, Lazy Instantiation'
- 'Template Specialization and Partial Specialization: The Art of Pattern Matching'
tags:
- host
- cpp-modern
- intermediate
- 模板
- 泛型
title: 'Function Templates, In Depth: Compilation Model and the No-Partial-Specialization
  Trap'
---
# Function Templates, In Depth: Compilation Model and the No-Partial-Specialization Trap

Volume 1 already covered function templates: the syntax, instantiation, deduction, specialization, and overloading. This piece shifts perspective to "I want to build a library with templates," where three things are unavoidable. Why the compilation model for templates differs from ordinary functions. How explicit instantiation and `extern template` help you control code bloat. And a trap that can leave you staring at the screen for half an hour: function templates cannot be partially specialized. Get these three straight, and reading the source of STL or template-heavy libraries like Eigen starts to make sense, why they organize the code the way they do.

## The Inclusion Model: Why Templates Live in Headers

First, recall how an ordinary function is split across files. The declaration goes in a header `add.h`, the implementation in `add.cpp`, and anyone else `#include "add.h"` to use it. The compiler only sees the declaration at the call site, and the linker later binds the call to the implementation in `add.cpp`. This is **separate compilation**, an old C/C++ tradition.

Templates do not work that way. Templates use the **inclusion model**: the definition of a template must be fully visible at the point where it is instantiated. You cannot put a function template declaration in a header, the definition in a `.cpp`, and then use it from another translation unit (TU). Instantiation happens at compile time, and the compiler must substitute `T` with the concrete type and generate code on the spot at the call site. That generation needs the full template definition.

```cpp
// add.h -- only a declaration, will this work?
template <typename T>
T add(T a, T b);   // declaration only
```

```cpp
// add.cpp -- the definition
template <typename T>
T add(T a, T b) { return a + b; }
```

```cpp
// main.cpp
#include "add.h"
int main() {
    return add(1, 2);   // link error! undefined reference to `add<int>(int, int)`
}
```

This compiles, but the link fails. In `main.cpp` the compiler cannot see the definition of `add`, so it cannot instantiate `add<int>`, and what it leaves behind is an unresolved symbol reference. In `add.cpp`, because nobody "uses" `add`, the compiler never instantiates any version at all. The two sides never line up, and the linker reports undefined reference.

The fix is plain: move the definition back into the header.

```cpp
// add.h -- definition lives in the header
template <typename T>
T add(T a, T b) { return a + b; }
```

This is why nearly every template library is a header-only library. boost is the canonical example. The cost was mentioned in the previous piece: every TU that `#include`s it re-parses the template definition, which is why large projects compile slowly.

The C++98 committee did try to fix this with `export template`, attempting to give templates their own separate compilation. Only EDG ever implemented it, the major compilers all bailed, and C++11 removed `export` entirely. So today the inclusion model is the only realistic choice for templates. Explicit instantiation, covered next, is a way to "save a little" within the inclusion model. It is not real separate compilation.

## Explicit Instantiation: Manually Placing Instantiation Points

By default, templates use **implicit instantiation**: wherever you use `add<int>`, the compiler generates a copy of `add<int>` in that translation unit. Ten TUs all using `add<int>` means ten copies generated, with duplicates merged at link time. Merging is free; generating is not. Every TU has to do the substitution and compilation all over again, which is the root of the slowness.

**Explicit instantiation** lets you manually say "generate the code for this version, here."

An explicit instantiation definition starts with the `template` keyword, followed by a concrete signature with no `template<>`:

```cpp
template <typename T>
T add(T a, T b) { return a + b; }

// explicit instantiation definition: force the compiler to generate add<double> in this TU
template double add<double>(double, double);
```

This line tells the compiler: whether or not this TU actually uses `add<double>`, generate it anyway. It compiles and runs fine.

```bash
$ g++ -std=c++17 explicit_inst.cpp -o explicit_inst && ./explicit_inst
add(1.0, 2.0) = 3
add(1, 2) = 3
```

Paired with it is the `extern template` declaration. It says "this version has already been instantiated in another TU, so do not generate it here, use the one from there."

```cpp
// In some .cpp: explicit instantiation definition, actually emits the code
//   template int add<int>(int, int);

// In other .cpp / .h: a declaration, generates nothing, links to the other one
extern template int add<int>(int, int);
```

Let us run a two-TU example and see it link.

```cpp
// tu_a.cpp -- implicit instantiation of doubler<int> happens here (call_a uses it)
template <typename T>
T doubler(T x) { return x * 2; }
int call_a() { return doubler(21); }
```

```cpp
// tu_b.cpp -- declare that doubler<int> is instantiated elsewhere; do not generate here
template <typename T>
T doubler(T x) { return x * 2; }
extern template int doubler<int>(int);
int call_b() { return doubler(21); }
```

```cpp
// main_ab.cpp
#include <iostream>
int call_a();
int call_b();
int main() {
    std::cout << "call_a=" << call_a() << " call_b=" << call_b() << "\n";
}
```

```bash
$ g++ -std=c++17 tu_a.cpp tu_b.cpp main_ab.cpp -o main_ab && ./main_ab
call_a=42 call_b=42
```

`tu_b` calls `doubler(21)` too, but because of the `extern template` declaration it does not emit `doubler<int>`. It waits for the linker to bind to the copy `tu_a` produced. The link succeeds and the result is correct.

The real home for this mechanism is library authoring. The standard library uses this pattern heavily. High-frequency combinations like `std::basic_string<char>` and `std::vector<int>` are pre-instantiated inside libstdc++ source files, and the public headers declare `extern template` for user code. That way tens of thousands of user TUs no longer each reinstantiate the dozens of member functions of `std::string`. Both compile time and binary size drop noticeably. When you write your own template library, doing the same for a few most-common type combinations pays off immediately.

::: warning extern template is not separate compilation
One easy misconception: `extern template` lets you "not generate in this TU." It really does free the consuming TU from copying the full definition in — a declaration plus the `extern template` declaration is enough (the `tu_b` example above spells out the full definition, but that is redundant; drop the definition, keep only the declaration, and it still compiles and links). Its job, though, is still to cut redundant instantiation, not to truly separate declaration from definition. The instantiation point must still exist somewhere (explicitly or implicitly instantiated in some TU); `extern template` just lets other TUs reuse it. Clean separate compilation for templates, the way ordinary functions put declarations in `.h` and implementations in one `.cpp`, still has no clean answer today.
:::

## Function Templates Cannot Be Partially Specialized: The Classic Trap

Here is the main event. Class templates can be partially specialized. Variable templates (since C++14) can be partially specialized. **Function templates cannot be.** This is not a quirk of some compiler; the standard says so explicitly. cppreference puts it plainly on the templates overview: partial specialization is only allowed for class templates and variable templates.

Most people first hit this wall trying to write "a special version of a function template for pointer types." Intuitively, if a class template can do `template <typename T> class Foo<T*>`, a function template should be able to copy the pattern.

```cpp
template <typename T>
T identity(T x) { return x; }

// trying to partially specialize for T* -- compile error!
template <typename T>
T identity<T*>(T* x) { return *x; }
```

The compiler stops it on the spot.

```text
fn_partial.cpp:6:3: error: non-class, non-variable partial specialization
      'identity<T*>' is not allowed
    6 | T identity<T*>(T* x) { return *x; }
      |   ^~~~~~~~~~~~
```

GCC's wording states the rule clearly: only partial specializations of classes and variables are allowed, not functions.

Why does the standard say so? Because functions have **overloading**, and overloading already does everything partial specialization would want to do, more flexibly. Partial specialization is "provide a specialized version for some pattern of the template parameters." Overloading is "provide a separate function for some argument types." The two overlap, so the standard lets functions take the overloading path and skips bolting on a partial-specialization syntax that would fight it semantically.

So how do you write a special version for pointers? Use an overload.

```cpp
template <typename T>
T identity(T x) { return x; }            // general version

// an overload that gives pointers a dedicated version
template <typename T>
T identity(T* x) { return *x; }          // pointer version

int main() {
    int v = 42;
    identity(v);        // calls the T=int version, returns 42
    identity(&v);       // calls the pointer overload, returns 42
}
```

The second `identity` here is a new function template (parameter `T*`), not a partial specialization of the first. When `identity(&v)` is called, the compiler does overload resolution, the pointer version matches better, and it wins.

When the branching gets more complex, say "pointers go this way, integers go that way, everything else is general," overloading works but gets verbose. There are two more modern tools for this, both covered in part two of this volume: `std::enable_if` with SFINAE (C++11), and `if constexpr` for compile-time branching (C++17). The former dispatches by "making a particular overload vanish from the candidate set when a condition fails," the latter just writes a compile-time `if` directly in the body. Here is a preview of `if constexpr`, to show how clean it can get.

```cpp
template <typename T>
void process(T x) {
    if constexpr (std::is_pointer_v<T>) {
        std::cout << "pointer, dereferenced: " << *x << "\n";
    } else if constexpr (std::is_integral_v<T>) {
        std::cout << "integer: " << x << "\n";
    } else {
        std::cout << "other type\n";
    }
}
```

`if constexpr` discards the failing branches at compile time, so it never leaves behind a "dereference a non-pointer" compile error. This is the preferred way to handle "a function template walks different logic for different types" after C++17, dragging code that used to require convoluted SFINAE back into something a normal human can read.

## Full Specialization Is Legal, but Use It in the Right Place

We spent a while on no partial specialization. What about full specialization? **Full specialization of a function template is legal.** The syntax begins with `template<>`, with every template parameter pinned.

```cpp
template <typename T>
const char* type_name() { return "unknown"; }

// full specialization: int version
template <>
const char* type_name<int>() { return "int"; }

// full specialization: double version
template <>
const char* type_name<double>() { return "double"; }
```

There is a trap to know about full specialization: it does **not** participate in the "template" path of overload resolution. It inserts itself into the candidate set as an ordinary function. That means the signature of a full specialization must exactly correspond to some instantiation of the primary template, with no slack. And once a full specialization exists, it pins down that specific version and no longer tracks the primary template.

In practice, full specialization of function templates is not common. Most of the time, writing an ordinary overload (a non-template function) is less hassle than a full specialization, because overload rules are more intuitive. Full specialization fits better when you want to "preserve template identity, but customize the implementation for one type," for example specializing a function template for `const char*` to do string comparison.

::: warning Do not trip ODR on full specialization
The definition of a full specialization may appear in only one translation unit, otherwise it violates the one-definition rule (ODR). If you put a full specialization in a header that several `.cpp` files include, the linker reports a duplicate definition. The fix is either to put it in a single `.cpp`, or to mark it `inline`. Instantiations of the primary template do not have this problem (the linker merges duplicate instantiations), but a full specialization is an ordinary function and does not get that treatment.
:::

## What a Restrained Little Library Looks Like

String the previous sections together and look at a typical layout when writing a library. Suppose we offer a `clamp` function template, and we want the common types to compile fast without giving up genericity.

The header holds the template definition and declares `extern template` for the most common types.

```cpp
// clamp.h
template <typename T>
const T& clamp(const T& v, const T& lo, const T& hi) {
    if (v < lo) return lo;
    if (hi < v) return hi;
    return v;
}

// pre-declare: int and double versions are instantiated elsewhere; TUs including this header should not generate them
extern template const int&    clamp<int>(const int&, const int&, const int&);
extern template const double& clamp<double>(const double&, const double&, const double&);
```

The source file provides explicit instantiation definitions for those types.

```cpp
// clamp.cpp
#include "clamp.h"

// actually emit the code here
template const int&    clamp<int>(const int&, const int&, const int&);
template const double& clamp<double>(const double&, const double&, const double&);
```

Now every TU that includes `clamp.h` and uses `clamp<int>` skips instantiating it and links to the copy in `clamp.cpp`. Other types (like `clamp<long>`) still instantiate implicitly per TU, because there is no `extern template` for them. Common types are saved; uncommon types are not blocked. This is the standard library's internal pattern, just at much larger scale.

Next we move into class templates. Member functions of class templates have a "lazy instantiation" temper, and inside templates there is a split between dependent names and non-dependent names. Together, these two things make writing class templates a different experience from function templates.
