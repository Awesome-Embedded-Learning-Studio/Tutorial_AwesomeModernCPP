---
chapter: 13
cpp_standard:
- 20
description: 'A concept is a named compile-time predicate. It lifts the requirement on a template parameter out of the enable_if dark arts and puts it in the signature. The four syntax forms, the error message contrast with enable_if, and the standard library concepts you reach for most.'
difficulty: intermediate
order: 1
platform: host
prerequisites:
- 'Class templates: members, dependent names, lazy instantiation'
- 'Alias templates and the using declaration'
reading_time_minutes: 13
related:
- 'Constraining templates with concepts: subsumption and overloading'
- 'Requires expressions, in depth: the four kinds'
tags:
- host
- cpp-modern
- intermediate
- 模板
- 泛型
- concepts
- 类型安全
title: 'Concepts: Putting Constraints in the Signature'
---
# Concepts: Putting Constraints in the Signature

Back in Volume 1 we wrote function templates. We know that `template <typename T> T add(T a, T b)` works for `int`, for `double`, and so on. But the moment you want to say "`add` only takes numeric types, strings stay out," the pre-C++20 answer was `std::enable_if`. You shoved the constraint into an extra default template parameter and let SFINAE kick this overload out when substitution failed. It works. But it buries a simple requirement inside layers of template-parameter nesting, and the error messages are worse. When something goes wrong, the compiler dumps a wall of `enable_if<false, void>` internal expansion, and you have to already understand SFINAE to guess which condition wasn't met.

C++20 concepts fix exactly this. They let you **name the constraint and put it in the signature**, writing requirements the way you write types, and the compiler can finally report "constraint not satisfied" in words a person can read. This piece covers what a concept actually is, the forms it comes in, why its error messages save you half your hair, and the ready-made concepts in `<concepts>` you can use today.

## What a concept is: a named compile-time predicate

A concept is at bottom a **predicate that evaluates to bool at compile time**, except you gave it a name. The name is the point. With a name, it can show up in a signature, get named in an error, and be reused across templates.

```cpp
#include <concepts>

// Define a concept: T is "numeric" if it's an integer or floating-point type
template <typename T>
concept Numeric = std::integral<T> || std::floating_point<T>;
```

`Numeric<T>` evaluates to `true` or `false` at compile time. It produces no code on its own. It is just a named judgment. Once you have the name, you can use it directly as a constraint in the template parameter list:

```cpp
template <Numeric T>
T add(T a, T b) {
    return a + b;
}
```

Read `template <Numeric T>` as "`T` is a `Numeric` type." The constraint went from "dark magic hidden in a default template parameter" to "a plain requirement written where the type goes." That is the core value of a concept: it gives the constraint a name, making the requirement readable, reusable, and referenceable by the compiler.

## Four syntax forms

Once a concept exists, there are four places it can show up in a template. Let's write the same `add` four ways. All of them compile and run.

```cpp
#include <concepts>
#include <iostream>
#include <string>

template <typename T>
concept Numeric = std::integral<T> || std::floating_point<T>;

// Form 1: the concept directly as a constraint in the template parameter list
template <Numeric T>
T form1(T a, T b) { return a + b; }

// Form 2: a requires clause (trailing requires-clause), after the parameter list
template <typename T>
    requires Numeric<T>
T form2(T a, T b) { return a + b; }

// Form 3: abbreviated template syntax (constrained auto), a constraint before auto
auto form3(Numeric auto a, Numeric auto b) { return a + b; }

// Form 4: a requires after the parameter list, with a requires expression inside
template <typename T>
    requires requires(T x) { x + x; }
T form4(T a, T b) { return a + b; }
```

Form 1 is the most direct. It fits the case where the constraint is already a concept you have. Form 2, the requires clause, is more flexible, and we'll see it combine multiple conditions later. Form 3 is C++20 shorthand that reads like an ordinary function. `Numeric auto` is equivalent to a constrained template parameter. Form 4 has two `requires` in a row. The outer one is a clause, the inner one is an expression (we take that apart in the next piece). This form doesn't need a concept defined up front; you describe the requirement on the spot.

Run it, calling each form once:

<OnlineCompilerDemo allow-run
  title="The four syntax forms of a concept"
  source-path="code/examples/vol4/vol3-metaprogramming-cpp20-23/concepts_four_forms.cpp"
  description="Constrain the same add four ways: in the parameter list, as a requires clause, as constrained auto, and as an inline requires expression."
/>

Run it:

```text
form1: 8
form2: 5
form3: 30
form4: ab
```

Four writes, four calls, all returning what you'd expect. Form 4 takes `std::string` fine too, because `string` has `operator+`, and the inner `requires(T x) { x + x; }` holds for it.

## The error message contrast: why concepts save your hair

Saying "the errors are better" is empty. Let's run it. Same `add`, constrained to "numeric only" once with `enable_if` and once with a concept. Then we deliberately call it with `std::string` and see what each version prints.

First, the `enable_if` version (excerpt):

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

The heart of the error is that last line, `no type named 'type' in 'struct std::enable_if<false, void>'`. You have to know that `enable_if<false>` has no `type` member, and know this is SFINAE kicking the overload out on substitution failure, to back out "oh, it's because `string` isn't numeric." The whole message is about `enable_if`'s internals. It says nothing about the thing you actually care about: what did `string` fail to satisfy.

Now the concept version (excerpt):

```text
add_concept.cpp:16:8: error: no matching function for call to 'add(std::string&, std::string&)'
  • candidate 1: 'template<class T>  requires  Numeric<T> T add(T, T)'
      • template argument deduction/substitution failed:
        • constraints not satisfied
          • required for the satisfaction of 'Numeric<T>'
              [with T = std::__cxx11::basic_string<char>]
            concept Numeric = std::integral<T> || std::floating_point<T>;
```

The key difference is `constraints not satisfied` and `required for the satisfaction of 'Numeric<T>' [with T = ... basic_string<char>]`. The compiler tells you straight: `string` did not satisfy `Numeric`. The constraint's name is called out, and the failing type is substituted in. You don't need to understand SFINAE, you don't read any `enable_if` expansion. You see at a glance which rule didn't pass.

::: warning Don't judge by line count alone
On a newer GCC (we're using 16.1.1 here), the two errors are actually close in length. The compiler has learned to structure the `enable_if` diagnostics too. So the win from concepts is not "the error is dozens of lines shorter," that's old talk. The win is that **the information points straight at the constraint**. What you want is "string is not Numeric," not "enable_if<false> has no type." On an older compiler (GCC 9 or 10, say), the length gap is brutal, and that was one of the main forces behind concepts back then.
:::

Readability of errors is the most direct payoff of concepts. When you write a library and someone passes the wrong type, they no longer see hieroglyphics. They see "the Numeric constraint wasn't satisfied."

## The ready-made concepts in `<concepts>`

You don't have to write every concept yourself. The standard library `<concepts>` ships a batch of common ones, covering type relations, constructibility, convertibility, and other high-frequency cases. Let's pick a few you bump into most and actually run them:

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
  title="Standard library concepts, run live"
  source-path="code/examples/vol4/vol3-metaprogramming-cpp20-23/stdconcepts_demo.cpp"
  description="Real judgments from same_as, convertible_to, derived_from, common_with, integral, and floating_point."
/>

Run it:

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

There's a trap here that's easy to step on. `same_as<int, const int>` comes out **false**, even though your gut says "they're both int." The reason is that the `const` qualifier makes them two distinct types. `std::is_same_v<int, const int>` is false to begin with, and `same_as` is built on top of it, so it's false too. If you want "after stripping cv-qualifiers and references, is it the same type," you have to erase the qualifiers with `std::remove_cvref_t` first, then compare.

`derived_from` also checks accessibility of the inheritance (only `public` inheritance counts). A privately inherited base will return false here. `convertible_to` allows implicit narrowing, so `int` to `double` (promotion) and `double` to `int` (narrowing) are both true. The standard library has no ready-made "no narrowing" concept. When you need strict numeric checking, you either accept this loose behavior or pair `std::is_convertible_v` with a no-narrowing trait of your own to block it. `integral<bool>` is true because `bool` belongs to the integer type family in the standard. When you write numeric algorithms and want to decide whether `bool` counts, that's on you to filter with `std::integral && !std::same_as<T, bool>`.

These off-the-shelf concepts cover about nine tenths of everyday type judgments. The cases where you actually need to write your own concept are usually "this algorithm requires the type to provide certain operations." Things like "has a `size()` method," "can be compared with `<`," "has a `value_type` nested type." We'll come back to those repeatedly when we talk about constraining templates and requires expressions.

## A concept is a bool, you can use it directly to judge

A concept is, in the end, a compile-time bool. So it doesn't only live in signatures. It can be used directly anywhere you need a compile-time judgment, like `static_assert` or `if constexpr`:

```cpp
template <typename T>
concept Numeric = std::integral<T> || std::floating_point<T>;

static_assert(Numeric<int>);          // compile-time assertion: int is numeric
static_assert(!Numeric<std::string>); // string isn't, assert that it "isn't"

template <typename T>
void describe(T x) {
    if constexpr (Numeric<T>) {
        // compile-time branch: this block only compiles when T is numeric
    }
}
```

This becomes the main character in the next piece, how to use a concept to constrain templates and do overload dispatch. With a constraint that doubles as a bool, overload resolution based on constraints finally has something to stand on.

In the next piece we go a level deeper. A concept isn't just "nice to look at in a signature." It actually participates in **overload resolution**. When several overloads with different constraints sit together, the compiler picks the best fit using a rule called subsumption (constraint entailment). That is the part of concepts that actually changes how you write generic code.
