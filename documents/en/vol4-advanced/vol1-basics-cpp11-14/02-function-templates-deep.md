---
chapter: 12
cpp_standard:
- 11
- 14
- 17
description: 'A library writer''s second look at function templates: why the inclusion
  model forces templates into headers, how explicit instantiation and extern template
  control code bloat, and the classic trap that function templates cannot be partially
  specialized — overloading is the way around it.'
difficulty: intermediate
order: 2
platform: host
prerequisites:
- Function Templates
- 'Templates, From Scratch: A Code Recipe with Placeholders'
reading_time_minutes: 12
related:
- 'Class Templates: Members, Dependent Names, and Lazy Instantiation'
- 'Template Specialization and Partial Specialization: The Art of Pattern Matching'
tags:
- host
- cpp-modern
- intermediate
- 模板
- 泛型
title: 'Function Templates, In Depth: Compilation Model and the No-Partial-Specialization
  Trap'
translation:
  source: documents/vol4-advanced/vol1-basics-cpp11-14/02-function-templates-deep.md
  source_hash: d855f2daa06c3b04ea0d06416cbaa9f5b7df6c1213301de48f0e9a042188a5fa
  translated_at: '2026-09-26T03:59:26+00:00'
  engine: anthropic
  token_count: 4800
---
# Function Templates, In Depth: Compilation Model and the No-Partial-Specialization Trap

In Volume 1 we already wrote function templates and got the whole routine down: syntax, instantiation, deduction, specialization, overloading. This piece switches perspective. We re-read function templates from the "I want to build a library with templates" angle, and three things become unavoidable: why the compilation model of templates differs from that of ordinary functions, how explicit instantiation and `extern template` help you control code bloat, and a trap that can keep you staring at the screen for half a day — function templates cannot be partially specialized. Once these three are properly covered, when you read the source of the STL or template-heavy libraries like Eigen, you will understand why they organize their code the way they do.

## The Inclusion Model: Why Templates Have to Live in Headers

First, recall how an ordinary function is split across files. The declaration goes into the header `add.h`, the implementation into `add.cpp`, and elsewhere `#include "add.h"` is all it takes to use it. At the call site the compiler sees only the declaration; the linker later binds the call to that implementation in `add.cpp`. This scheme is called **separate compilation**, a long-standing C/C++ tradition.

Templates do not play that game. Templates use the **inclusion model**: the definition of a template must be completely visible at the point where it gets instantiated. You cannot put a function template's declaration in a header, its definition in a `.cpp`, and then use it from another translation unit (TU). The reason: template instantiation happens at compile time — the compiler must substitute the concrete type for `T` at the call site and generate the code on the spot, and that generation process needs to see the complete template definition.

```cpp
// add.h — declaration only; will this work?
template <typename T>
T add(T a, T b);   // declaration only
```

```cpp
// add.cpp — the definition goes here
template <typename T>
T add(T a, T b) { return a + b; }
```

```cpp
// main.cpp
#include "add.h"
int main() {
    return add(1, 2);   // Linker error! undefined reference to `add<int>(int, int)`
}
```

This setup compiles but fails to link: in `main.cpp` the compiler cannot see the definition of `add`, cannot instantiate `add<int>`, and leaves behind nothing but an unresolved symbol reference; meanwhile in `add.cpp`, since nobody actually "uses" `add`, the compiler never instantiates any version at all. The two sides do not match up, and the linker reports undefined reference.

The fix is plain and simple: move the definition back into the header.

```cpp
// add.h — the definition lives in the header too
template <typename T>
T add(T a, T b) { return a + b; }
```

That is why almost every template library is a header-only library; Boost is the classic example. The cost, as we covered in the previous piece: every TU that `#include`s it has to re-parse the template definition all over again, and big projects get slow to compile.

The C++98 committee did try to cure this: `export template` was supposed to give templates their own version of separate compilation. In the end only EDG ever implemented it, the mainstream compilers all stood the committee up, and C++11 simply deleted `export`. So to this day, the inclusion model remains the only realistic option for templates. Explicit instantiation, covered in the next section, is a way to "save a little" within the inclusion model — not true separate compilation.

## Explicit Instantiation: Manually Controlling the Instantiation Point

By default, templates are **implicitly instantiated**: wherever you use `add<int>`, the compiler generates a copy of the `add<int>` code in that translation unit. Ten TUs using `add<int>` means ten generated copies, with the duplicates merged away at link time. Merging is free; generating is not — every TU has to redo the substitution and compilation, and that is the root of the slowness.

**Explicit instantiation** lets you manually designate "generate the code for this particular version here."

An explicit instantiation definition looks like this: it starts with the `template` keyword, followed by a concrete signature without `template<>`:

```cpp
template <typename T>
T add(T a, T b) { return a + b; }

// Explicit instantiation definition: force this TU to generate the add<double> code
template double add<double>(double, double);
```

That line tells the compiler: whether or not this TU actually uses `add<double>`, generate a copy for me anyway. Compile and run, no problem:

```bash
$ g++ -std=c++17 explicit_inst.cpp -o explicit_inst && ./explicit_inst
add(1.0, 2.0) = 3
add(1, 2) = 3
```

Its companion is the `extern template` declaration. It means "this version is already instantiated in some other TU; don't generate it here, just use the copy from elsewhere":

```cpp
// In some .cpp: an explicit instantiation definition, which actually generates the code
//   template int add<int>(int, int);

// In other .cpp / .h files: a declaration — generates nothing, resolved elsewhere at link time
extern template int add<int>(int, int);
```

Let's run a two-TU example and see whether it links properly.

```cpp
// tu_a.cpp — implicitly instantiates doubler<int> here (call_a uses it)
template <typename T>
T doubler(T x) { return x * 2; }
int call_a() { return doubler(21); }
```

```cpp
// tu_b.cpp — declares doubler<int> instantiated elsewhere; this TU no longer generates it
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

Although `tu_b` also calls `doubler(21)`, the `extern template` declaration means it does not generate the code for `doubler<int>`; it waits for link time and uses the copy generated in `tu_a`. The link goes through, and the results are correct.

Where this mechanism really earns its keep is on the library author's side. The standard library uses this trick extensively: for high-frequency combinations like `std::basic_string<char>` and `std::vector<int>`, libstdc++ pre-instantiates them explicitly in its own source files, then declares `extern template` in the public headers for user code. That way the translation units of thousands upon thousands of users never have to redundantly instantiate the dozens of member functions of `std::string`, saving a big chunk of both compile time and binary size. When you write your own template library, doing the same for a few of the most common type combinations pays off immediately.

::: warning extern template is not separate compilation
One easy misunderstanding: `extern template` says "do not generate in this TU." It does free the consuming TU from copying in the complete template definition — a plain declaration plus the `extern template` declaration is enough (writing out the full definition in the earlier `tu_b` example was actually redundant; delete the definition and keep only the declaration, and it still compiles and links). But its role remains "reduce redundant instantiation," not truly separating declaration from definition: an instantiation point for the template must exist (explicitly or implicitly instantiated in some TU), and `extern template` merely lets other TUs reuse it. True separate compilation for templates — declaration in `.h`, implementation in a single `.cpp`, the way ordinary functions do it — still has no clean solution to this day.
:::

## Function Templates Cannot Be Partially Specialized: The Classic Trap

Now for the main event. Class templates can be partially specialized (partial specialization), variable templates (C++14) can too, but **function templates cannot be partially specialized**. This is not some individual compiler's limitation — the standard explicitly says so. cppreference's templates overview page puts it plainly: partial specialization is allowed only for class templates and variable templates.

Most people first hit this wall trying to give a function template "a special version for pointer types." Intuitively, since a class template can be written as `template <typename T> class Foo<T*>`, a function template should be able to follow the same recipe:

```cpp
template <typename T>
T identity(T x) { return x; }

// Attempting to partially specialize for T* — compile error!
template <typename T>
T identity<T*>(T* x) { return *x; }
```

The compiler stops you on the spot:

```text
fn_partial.cpp:6:3: error: non-class, non-variable partial specialization
      'identity<T*>' is not allowed
    6 | T identity<T*>(T* x) { return *x; }
      |   ^~~~~~~~~~~~
```

GCC's wording states the rule plainly: only class and variable partial specializations are allowed; functions do not get them.

Why does the standard rule this way? Because functions have **overloading**, and overloading can do everything partial specialization would want to do, and more flexibly at that. Partial specialization is "provide a specialized version for some pattern of the template parameters"; overloading is "provide an independent function for some argument type." The two goals overlap, so the standard lets functions take the overloading road and denies them an extra partial-specialization syntax on top, to keep the two semantics from fighting each other.

So what do you do when you want a special version for pointer types? Use overloading:

```cpp
template <typename T>
T identity(T x) { return x; }            // general version

// Give pointers a dedicated version via overloading
template <typename T>
T identity(T* x) { return *x; }          // pointer version

int main() {
    int v = 42;
    identity(v);        // calls the T=int version, returns 42
    identity(&v);       // calls the T=int pointer overload, returns 42
}
```

Here the second `identity` is a brand-new function template (its parameter is `T*`), not a partial specialization of the first. When `identity(&v)` is called, the compiler runs overload resolution; the pointer version matches better and wins.

If the branching gets more complicated — "pointers take this path, integers take that one, everything else takes the general one" — overloading can still express it, but it gets wordy. That is where two more modern tools come in, both covered in depth in the second part of this volume: `std::enable_if` combined with SFINAE (C++11), and `if constexpr` for compile-time branching (C++17). The former diverts traffic by "making an overload vanish from the candidate set when its condition is not met"; the latter lets you write a compile-time `if` directly in the function body. Here is a preview of `if constexpr`, so you can feel how concise it gets:

```cpp
template <typename T>
void process(T x) {
    if constexpr (std::is_pointer_v<T>) {
        std::cout << "是指针,解引用:" << *x << "\n";
    } else if constexpr (std::is_integral_v<T>) {
        std::cout << "是整数:" << x << "\n";
    } else {
        std::cout << "其它类型\n";
    }
}
```

`if constexpr` discards the branches that do not hold at compile time, so no "dereferencing a non-pointer" compile error is left behind. Since C++17 it is the first choice for "a function template following different logic for different types," pulling code that used to be written with painful SFINAE contortions back into something a normal human can read.

## Full Specialization Is Legal, but Use It in the Right Place

After all that talk about partial specialization being off the table, what about full specialization? **Full specialization of function templates is legal**: the syntax starts with `template<>`, with every template parameter nailed down:

```cpp
template <typename T>
const char* type_name() { return "unknown"; }

// Full specialization: the int version
template <>
const char* type_name<int>() { return "int"; }

// Full specialization: the double version
template <>
const char* type_name<double>() { return "double"; }
```

One trap with full specialization to know about: it **does not participate in the "template" path of overload resolution**; it enters the candidate set as an ordinary function. This means a full specialization's signature must correspond exactly to some instantiation of the primary template — not one bit off. And once a full specialization is written, it "pins down" that concrete version for good; it will never change along with the primary template.

In real-world engineering, full specialization of function templates is not used much. Most of the time, writing a plain (non-template) overload is less hassle than writing a full specialization, because the overloading rules are more intuitive. Full specialization better fits scenarios where you "want to keep the template identity while customizing the implementation for one particular type" — for instance, specializing a `const char*` version of some function template for string comparison.

::: warning Don't step on the ODR with full specializations
The definition of a full specialization may appear in only one translation unit; otherwise you violate the one definition rule (ODR). If you write a full specialization in a header and multiple `.cpp` files include it, you will get duplicate-definition errors at link time. The fix: either put it in a single `.cpp`, or declare it `inline`. Instantiations of the primary template do not have this problem (the linker merges duplicate instantiations), but a full specialization is an ordinary function and gets no such treatment.
:::

## What a Restrained Little Library Looks Like

Let's string the previous sections together and look at a typical layout for library writing. Suppose we want to provide a `clamp` function template: fast to compile for the common types, without giving up genericity.

The header holds the template definition and declares `extern template` for the most common types:

```cpp
// clamp.h
template <typename T>
const T& clamp(const T& v, const T& lo, const T& hi) {
    if (v < lo) return lo;
    if (hi < v) return hi;
    return v;
}

// Pre-declaration: the int and double versions are instantiated elsewhere; TUs including this header must not regenerate them
extern template const int&    clamp<int>(const int&, const int&, const int&);
extern template const double& clamp<double>(const double&, const double&, const double&);
```

The source file provides explicit instantiation definitions for those types:

```cpp
// clamp.cpp
#include "clamp.h"

// This is where the code is actually generated
template const int&    clamp<int>(const int&, const int&, const int&);
template const double& clamp<double>(const double&, const double&, const double&);
```

With this, every TU that includes `clamp.h` no longer instantiates `clamp<int>` itself when it uses it; it links against the copy in `clamp.cpp`. Other types (say `clamp<long>`) carry no `extern template` declaration, so they keep getting generated per TU through implicit instantiation as usual. The common types save work, and the uncommon types are not blocked off. This is exactly how the standard library does things internally — just at a much larger scale.

Next up, we move on to class templates. Member functions of class templates have a "lazy instantiation" temperament, and inside templates there is the distinction between dependent names and non-dependent names; those two things combined make writing class templates a different experience from function templates.
