---
chapter: 13
cpp_standard:
- 20
description: 'A concept is a named compile-time predicate that lifts "what this template
  parameter must look like" out of enable_if black magic and into the signature. Covers
  its four syntax forms, the error-message contrast against enable_if, and commonly
  used concepts from the standard library.'
difficulty: intermediate
order: 1
platform: host
prerequisites:
- 'Class Templates: Members, Dependent Names, and Lazy Instantiation'
- 'Alias Templates and using Declarations: Short Names for Types'
reading_time_minutes: 13
related:
- 'Constraining Templates with Concepts: Subsumption and Overloading'
- 'Requires Expressions, In Depth: The Four Kinds of Requirements'
tags:
- host
- cpp-modern
- intermediate
- 模板
- 泛型
- concepts
- 类型安全
title: 'Concepts: Putting Constraints in the Signature'
translation:
  source: documents/vol4-advanced/vol3-metaprogramming-cpp20-23/01-concepts.md
  source_hash: b02535ac079fff96773d76288c4ee79814cbd3cc62ca6a9f2b32d085ad15d9ba
  translated_at: '2026-09-26T04:29:07+00:00'
  engine: anthropic
  token_count: 3000
---
# Concepts: Putting Constraints in the Signature

Back in Volume 1 we wrote function templates and saw that `template <typename T> T add(T a, T b)` works for both `int` and `double`. But the moment you want to rule that "`add` takes numeric types only—strings need not apply," in the days of C++17 and earlier the standard answer was `std::enable_if`: stuff the constraint into an extra default template parameter and let SFINAE kick this overload out when substitution fails. The mechanism runs, but it buries one simple requirement inside layers of nested template parameters—and the error messages are the real killer. When something goes wrong, the compiler spits out a pile of `enable_if<false, void>` internal expansion, and you have to understand SFINAE first before you can guess which condition was not met.

C++20 concepts solve exactly this problem. They let you **name the constraint and write it into the signature**, state requirements the way you write types, and the compiler can finally report errors in words a human actually speaks: "constraint not satisfied." This piece covers what a concept really is, the several ways to write one, why its error messages save you half your hair, and the ready-to-use concepts in the standard library's `<concepts>`.

## What a concept is: a named compile-time predicate

A concept is at heart a **predicate that evaluates to bool at compile time**—you have simply given it a name. The name is the key: once named, it can appear in signatures, be called out by name in error messages, and be reused across templates.

```cpp
#include <concepts>

// Define a concept: T counts as "numeric" if it is an integer or floating-point type
template <typename T>
concept Numeric = std::integral<T> || std::floating_point<T>;
```

`Numeric<T>` works out to either `true` or `false` at compile time. It produces no code of its own; it is nothing more than a named judgment. Once the name exists, you can use it directly as a constraint in the template parameter list:

```cpp
template <Numeric T>
T add(T a, T b) {
    return a + b;
}
```

The line `template <Numeric T>` reads as "`T` is a `Numeric` type." The constraint goes from "black magic hidden in a default template parameter" to "a plain-language requirement written in the type position." That is the core value of concepts: they give a constraint a name, making the requirement readable, reusable, and something the compiler can cite.

## Four syntax forms

Once a concept is written, there are four places in a template where it can be put to work. Let us write the same `add` function four different ways—each one compiles and runs.

```cpp
#include <concepts>
#include <iostream>
#include <string>

template <typename T>
concept Numeric = std::integral<T> || std::floating_point<T>;

// Form 1: the concept itself written as a constraint in the template parameter list
template <Numeric T>
T form1(T a, T b) { return a + b; }

// Form 2: a requires clause (trailing requires-clause), written after the parameter list
template <typename T>
    requires Numeric<T>
T form2(T a, T b) { return a + b; }

// Form 3: abbreviated template syntax (constrained auto), a constraint in front of auto
auto form3(Numeric auto a, Numeric auto b) { return a + b; }

// Form 4: requires after the template parameter list, with a requires expression inside
template <typename T>
    requires requires(T x) { x + x; }
T form4(T a, T b) { return a + b; }
```

Form 1 is the most intuitive—right for the case where the constraint is a ready-made concept. Form 2, the `requires` clause, is more flexible; we will see later that it can combine several conditions. Form 3 is C++20's abbreviated syntax: it reads like an ordinary function, and `Numeric auto` is equivalent to a constrained template parameter. Form 4 has two `requires` in a row—the outer one is a clause, the inner one an expression (picked apart in detail in the next piece)—and this form needs no concept defined in advance; you describe the requirement on the spot.

Let us run it, calling each of the four forms once:

<OnlineCompilerDemo allow-run
  title="The four syntax forms of a concept"
  source-path="code/examples/vol4/vol3-metaprogramming-cpp20-23/concepts_four_forms.cpp"
  description="Constrain the same add four ways: in the template parameter list, as a requires clause, as abbreviated auto, and as an inline requires expression."
/>

Output:

```text
form1: 8
form2: 5
form3: 30
form4: ab
```

Four spellings, four calls, everything returns as expected. Form 4 takes `std::string` in stride as well, because `string` has `operator+`, so the inner `requires(T x) { x + x; }` holds for it.

## The error message contrast: why concepts save your hair

Just saying "the errors are better" proves nothing—let us run it and see. The same `add`, constrained to "numeric types only" once with `enable_if` and once with a concept, then deliberately called with `std::string`, so we can see what the compiler spits out in each case.

First, the `enable_if` version's error (excerpt):

```text
add_enable_if.cpp:13:8: error: no matching function for call to 'add(std::string&, std::string&)'
   13 |     add(s1, s2);
      |     ~~~^~~~~~~~
  • candidate 1: 'template<class T, class> T add(T, T)'
      • template argument deduction/substitution failed:
        • /usr/include/c++/16/type_traits: In substitution of
          'template<bool _Cond, class _Tp> using std::enable_if_t = ... [with bool _Cond = false; _Tp = void]':
        • error: no type named 'type' in 'struct std::enable_if<false, void>'
```

The heart of the error is the last line: `no type named 'type' in 'struct std::enable_if<false, void>'`. You have to know that `enable_if<false>` contains no member type named `type`, and that this is what SFINAE kicking the overload out on substitution failure looks like, before you can work backward to "oh, it is because `string` is not a numeric type." The message talks about `enable_if`'s internal machinery from start to finish, and never mentions the one thing you actually care about: what exactly did `string` fail to satisfy.

Now the concept version's error (excerpt):

```text
add_concept.cpp:16:8: error: no matching function for call to 'add(std::string&, std::string&)'
  • candidate 1: 'template<class T>  requires  Numeric<T> T add(T, T)'
      • template argument deduction/substitution failed:
        • constraints not satisfied
          • required for the satisfaction of 'Numeric<T>'
              [with T = std::__cxx11::basic_string<char>]
            concept Numeric = std::integral<T> || std::floating_point<T>;
```

The key difference lies in the two lines `constraints not satisfied` and `required for the satisfaction of 'Numeric<T>' [with T = ... basic_string<char>]`. The compiler tells you outright: `string` failed to satisfy the `Numeric` constraint. The constraint's name is called out, and the concrete failing type is substituted in. No SFINAE knowledge needed, no wading through the `enable_if` expansion—one glance tells you which rule did not pass.

::: warning Don't take line count as the only yardstick
On a newer GCC (we used 16.1.1 here), the two errors are actually about the same length—the compiler has learned to structure its `enable_if` diagnostics too. So the advantage of concepts is not the outdated story of "the error being dozens of lines shorter"; it is that **the information points straight at the constraint itself**. What you want is "string is not Numeric", not "enable_if<false> has no type". Switch to an older compiler (GCC 9 or 10, say) and the gap in line count becomes enormous—that was one of the main driving forces behind pushing concepts back in the day.
:::

Readable errors are the most direct payoff of concepts. When you write a library for other people, a caller who passes the wrong type no longer sees hieroglyphics, but a plain sentence: "the Numeric constraint was not satisfied."

## Ready-made concepts from the standard library's `<concepts>`

You do not have to build a concept from scratch every time. The standard library's `<concepts>` provides a batch of commonly used concepts, covering the high-frequency scenarios: type relationships, constructibility, convertibility. Let us pick a few you run into most often and actually run them to see their verdicts:

```cpp
#include <concepts>
#include <iostream>
#include <vector>

struct Base {};
struct Derived : Base {};
struct Unrelated {};

int main() {
    std::cout << std::boolalpha;
    std::cout << "same_as<int,int>:             " << std::same_as<int, int> << "\n";
    std::cout << "same_as<int, const int>:      " << std::same_as<int, const int> << "\n";
    std::cout << "convertible_to<int,double>:   " << std::convertible_to<int, double> << "\n";
    std::cout << "convertible_to<double,int>:   " << std::convertible_to<double, int> << "\n";
    std::cout << "derived_from<Derived,Base>:   " << std::derived_from<Derived, Base> << "\n";
    std::cout << "derived_from<Unrelated,Base>: " << std::derived_from<Unrelated, Base> << "\n";
    std::cout << "common_with<int,double>:      " << std::common_with<int, double> << "\n";
    std::cout << "default_initializable<int>:   " << std::default_initializable<int> << "\n";
    std::cout << "integral<int>:                " << std::integral<int> << "\n";
    std::cout << "integral<bool>:               " << std::integral<bool> << "\n";
    std::cout << "floating_point<float>:        " << std::floating_point<float> << "\n";
}
```

<OnlineCompilerDemo allow-run
  title="Common standard library concepts, tested live"
  source-path="code/examples/vol4/vol3-metaprogramming-cpp20-23/stdconcepts_demo.cpp"
  description="What same_as / convertible_to / derived_from / common_with / integral / floating_point actually decide."
/>

Output:

```text
same_as<int,int>:             true
same_as<int, const int>:      false
convertible_to<int,double>:   true
convertible_to<double,int>:   true
derived_from<Derived,Base>:   true
derived_from<Unrelated,Base>: false
common_with<int,double>:      true
default_initializable<int>:   true
integral<int>:                true
integral<bool>:               true
floating_point<float>:        true
```

There is a spot here that is easy to trip on. `same_as<int, const int>` comes out **false**, though your intuition may say "aren't they both int?" The reason is that the `const` qualifier makes them two different types: `std::is_same_v<int, const int>` was already false, and `same_as` is built on top of it, so it is naturally false too. If you want to test "same type once cv-qualifiers and references are stripped off," you must first erase the qualifiers with `std::remove_cvref_t` and then compare.

`derived_from` also checks the **accessibility** of the inheritance (only public inheritance counts); a base inherited privately returns false here. `convertible_to` permits implicit narrowing, so both `int` to `double` (promotion) and `double` to `int` (narrowing) come out true. The standard library has no ready-made "narrowing forbidden" concept; when you need strict numeric-type checking, either accept this lenient behavior or intercept it yourself with `std::is_convertible_v` paired with a no-narrowing trait. `integral<bool>` is true because `bool` belongs to the integer type family in the standard; whether to count `bool` in when you write numeric algorithms is yours to gate with `std::integral && !std::same_as<T, bool>`.

These ready-made concepts cover roughly ninety percent of everyday type-judgment needs. The scenarios where you genuinely need to write your own concept are usually "my algorithm requires the type to provide certain operations"—"has a `size()` method", "can be compared with `<`", "has a nested `value_type`"—and those will come up again and again when the next piece covers constraints and the one after covers requires expressions.

## A concept is a bool: test with it directly

A concept is, in the final analysis, a compile-time bool. So it does not only live in signatures—it can also be used directly in the places that need a compile-time judgment, like `static_assert` and `if constexpr`:

```cpp
template <typename T>
concept Numeric = std::integral<T> || std::floating_point<T>;

static_assert(Numeric<int>);        // compile-time assertion: int is numeric
static_assert(!Numeric<std::string>); // string is not; assert that it "is not"

template <typename T>
void describe(T x) {
    if constexpr (Numeric<T>) {
        // compile-time branch: this block only compiles when T is a numeric type
    }
}
```

This point takes center stage in the next piece, on "how to constrain templates with concepts and dispatch overloads"—once a constraint can double as a bool, constraint-based function overloading finally has a foundation.

In the next piece we go one level deeper: a concept is not just "pretty to look at in a signature"—it genuinely participates in **overload resolution**. When several overloads with different constraints sit side by side, the compiler picks the most fitting one by a rule called subsumption (constraint entailment), and that is a key link in how concepts change the way generic code is written.
