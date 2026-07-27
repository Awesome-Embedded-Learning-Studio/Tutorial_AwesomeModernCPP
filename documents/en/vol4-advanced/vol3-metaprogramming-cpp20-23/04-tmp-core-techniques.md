---
chapter: 13
cpp_standard:
- 17
- 20
description: 'For more than a decade before concepts, generic library authors answered type questions with TMP. The internals of type_traits specialization, template recursion, SFINAE and enable_if, the void_t detection idiom, C++17 fold expressions, and how to migrate constraint-style SFINAE onto concepts.'
difficulty: intermediate
order: 4
platform: host
prerequisites:
- 'Concepts: Putting Constraints in the Signature'
- 'Constraining Templates with Concepts: Subsumption and Overloading'
- 'Requires Expressions, In Depth: The Four Kinds'
reading_time_minutes: 17
related:
- 'Requires Expressions, In Depth: The Four Kinds'
- 'Compile-Time Strings: NTTP Class Type and fixed_string'
tags:
- host
- cpp-modern
- intermediate
- 模板元编程
- 编译期计算
- 泛型
title: 'TMP Core Techniques: The World Before Concepts'
---
# TMP Core Techniques: The World Before Concepts

For three pieces we've been looking at "constraints" under the light of C++20 concepts. But concepts are new, they only entered the standard in 2020. For more than a decade before that, if a generic library author wanted to answer "is this type qualified," they used a completely different mechanism: template metaprogramming (TMP). This piece steps back to look at TMP's core techniques: using specialization for compile-time queries, template recursion for compile-time loops, SFINAE to make the wrong overload quietly back off, `void_t` to detect whether a member exists, and C++17 fold expressions to replace a good chunk of template recursion.

This stuff looks verbose today, but it has not been fully replaced by concepts. Open the standard library source, open Boost, open Chromium's base, and SFINAE and `void_t` are everywhere. Concepts take over "is the type qualified" style constraints, but "compute a value on the type" still needs TMP. So this piece isn't a museum exhibit. It's groundwork you can't skip when reading or writing real generic code.

## The internals of type_traits: specialization is a compile-time if-else

`std::is_pointer_v<int*>` evaluates to `true`, `std::is_pointer_v<int>` to `false`. How does that compile-time judgment work? The answer is almost disappointingly plain: template partial specialization. Let's hand-write an `is_pointer`. The structure is close to what the standard library does:

```cpp
template <typename T>
struct is_pointer_impl {
    static constexpr bool value = false;
};

template <typename T>
struct is_pointer_impl<T*> {
    static constexpr bool value = true;
};

template <typename T>
constexpr bool is_pointer_v = is_pointer_impl<T>::value;
```

The primary template is the fallback, giving every type `false`. The partial specialization matches `T*`, "pointer to some type," and gives `true`. When the compiler instantiates `is_pointer_impl<int*>`, it finds the partial specialization fits better than the primary and picks it. When it instantiates `is_pointer_impl<int>`, no more-specialized version matches, so it falls back to the primary. That's the most basic TMP idiom: **use specialization for compile-time dispatch**, equivalent to an if-else over types.

Run it and compare against the standard library:

<OnlineCompilerDemo allow-run
  title="Hand-written is_pointer, specialization as compile-time dispatch"
  source-path="code/examples/vol4/vol3-metaprogramming-cpp20-23/traits_from_scratch.cpp"
  description="Primary template defaults to false, partial specialization matches T* and gives true. Result matches std::is_pointer."
/>

Run it:

```text
is_pointer_v<int>:    false
is_pointer_v<int*>:   true
is_pointer_v<int**>:  true
is_pointer_v<double*>:true
与 std::is_pointer 结果一致
```

The one or two hundred traits in the standard `<type_traits>` (`is_integral`, `is_class`, `remove_const`, `decay`, and so on) are all built on the same pattern at bottom: a primary template plus a set of partial specializations covering various cases. `remove_const_t<const int>` becoming `int` is nothing more than a partial specialization for `const T` with `using type = T` inside. Once you internalize the "specialization is dispatch" model, the `<type_traits>` source stops being intimidating.

## Template recursion: use instantiation as a loop

Type queries aren't enough. TMP can also do arithmetic at compile time. The trick is to unfold a loop into a chain of template instantiations, with a specialization as the base case. The classic example is factorial:

```cpp
template <unsigned N>
struct Factorial {
    static constexpr unsigned value = N * Factorial<N - 1>::value;
};

template <>
struct Factorial<0> {
    static constexpr unsigned value = 1;
};
```

The evaluation of `Factorial<5>::value` is the compiler instantiating down a chain at compile time: `Factorial<5>` references `Factorial<4>::value`, `Factorial<4>` references `Factorial<3>`, until it hits the full specialization `Factorial<0>` where `value` is `1`, and the recursion unwinds, multiplying each layer out. This all happens at compile time. At runtime, `Factorial<5>::value` is just the constant `120`, with no function call overhead.

<OnlineCompilerDemo allow-run
  title="Template recursion computes factorial, specialization is the base case"
  source-path="code/examples/vol4/vol3-metaprogramming-cpp20-23/tmp_factorial.cpp"
  description="Factorial<N> references Factorial<N-1>, until it hits the Factorial<0> full specialization. The value is fixed at compile time."
/>

Run it:

```text
Factorial<5>::value  = 120
Factorial<10>::value = 3628800
编译期断言全部通过
```

`static_assert` can assert `Factorial<10>::value == 3628800` at compile time, which means the value was already computed before the program ran. This "template recursion plus a base-case specialization" is the classic TMP loop pattern. It used to do heavy lifting like compile-time sorting, compile-time string handling, and typelist operations. The cost is real: deep instantiation layers, slow builds, and errors that are hard to read. That's exactly why fold expressions were invented.

## SFINAE: let the wrong overload back off gracefully

Traits answer "what is this type." But generic libraries have another problem. I have two overloads, one for integers and one for floating-point. How do I make the compiler **not error, but quietly skip the wrong one** when the argument doesn't fit? The answer is a rule called SFINAE, short for "Substitution Failure Is Not An Error."

The meaning is: when the compiler substitutes template parameters into a signature, if some step of substitution goes wrong (say it produces a nonexistent type like `int::value_type`), it doesn't error immediately. It marks that overload as "substitution failure," silently drops it from the candidate set, and keeps trying others. `std::enable_if` is the workhorse that uses this rule. It hides the constraint inside an extra default template parameter:

```cpp
template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
T add_old(T a, T b) {
    return a + b;
}
```

`std::enable_if_t<condition>` only has a nested `type` when the condition is `true`. When the condition is `false`, it's an empty shell. Substituting it in makes the default parameter's type deduction fail, and SFINAE drops the overload. The result: passing `int` matches, passing `std::string` means this overload isn't even a candidate.

SFINAE works, but its error messages are famously torturous. Let's deliberately call that `add_old` with `std::string` and see what GCC 16.1.1 prints (excerpt):

```text
error: no matching function for call to 'add_old(std::string, std::string)'
  candidate: 'template<class T, class> T add_old(T, T)'
    template argument deduction/substitution failed:
    error: no type named 'type' in 'struct std::enable_if<false, void>'
```

The heart of the error is `no type named 'type' in 'std::enable_if<false, void>'`. It's about `enable_if`'s internals, the condition was false so there's no `type` member, and it never mentions the thing you actually care about. You have to already understand SFINAE to back out "oh, it's because string isn't an integer." This is exactly the old path concepts kept contrasting with in the first three pieces. Here we see from the mechanism side why it's so hard to read.

## void_t and the detection idiom: the highlight of C++17

The most elegant use of SFINAE is a pattern called the detection idiom, proposed around 2014 by Walter Brown. Its heart is a tool that looks like it does nothing: `std::void_t`.

```cpp
template <typename...>
using void_t = void;
```

`void_t` maps any types to `void`. It has no logic of its own, but paired with partial specialization it does the thing SFINAE-era code found hardest: **elegantly detect whether a type has a given member**. Let's detect "does T have a nested `value_type`":

```cpp
template <typename T, typename = void>
struct has_value_type : std::false_type {};

template <typename T>
struct has_value_type<T, std::void_t<typename T::value_type>> : std::true_type {};
```

The primary template defaults to inheriting `false_type`. The partial specialization's second template parameter is `std::void_t<typename T::value_type>`. The key is how the compiler picks between the two. When instantiating `has_value_type<std::vector<int>>`, the compiler tries the more-specialized partial first, substituting `T` with `std::vector<int>`, then tries to evaluate `std::void_t<std::vector<int>::value_type>`. `vector<int>` does have `value_type`, substitution succeeds, `void_t` maps it to `void`, the partial's second parameter settles on `void`, which matches the primary's default `void`, the partial wins, and the result is `true_type`. The other way, instantiating `has_value_type<int>`: `int::value_type` doesn't exist, substitution fails, and by the SFINAE rule failure is not an error. The partial is silently dropped, the compiler falls back to the primary, and the result is `false_type`.

Run it:

<OnlineCompilerDemo allow-run
  title="The void_t detection idiom, detecting a nested type"
  source-path="code/examples/vol4/vol3-metaprogramming-cpp20-23/void_t_detection.cpp"
  description="The primary template inherits false_type. The partial specialization wins via void_t only when substitution succeeds, returning true or false gracefully."
/>

Run it:

```text
has_value_type_v<std::vector<int>>: true
has_value_type_v<std::string>:      true
has_value_type_v<int>:              false
```

`void_t` folds what used to be long, ugly SFINAE detection code into two lines of partial specialization. That's why it was brought into the standard in C++17. But one fact has to be said plainly.

::: warning Only void_t made it into the standard, not the whole detection idiom
Walter Brown wrote three proposals around the detection idiom (N3911, then N4436, then N4502). The last one, N4502, proposed bringing a ready-made toolkit (`std::is_detected`, `std::detected_t`, and friends) into the standard library. That toolkit **was never voted in**. Only `void_t` itself, the atomic building block, made it into C++17. So you won't find `std::is_detected` in the standard library. To use the detection idiom you have to hand-write the two-line partial specialization shown above, or lean on a third-party library like Boost. After concepts arrived, "does some operation exist" can mostly be written more directly as `requires(T t){ t.foo(); }`, so the detection idiom shows up less in new code. But when you read older libraries, it's still everywhere.
:::

## Fold expressions: kill the recursion boilerplate

Template recursion can compute factorials, but for variadic work like "sum any number of arguments," the recursive style needs a primary template, a recursive case, and a termination specialization. The boilerplate is heavy. C++17 fold expressions cut it out. Compare these two:

```cpp
// Old way: recursion plus termination
template <typename T>
constexpr T sum_rec(T first) { return first; }

template <typename T, typename... Rest>
constexpr T sum_rec(T first, Rest... rest) {
    return first + sum_rec(rest...);
}

// C++17: one fold expression does it
template <typename... Ts>
constexpr auto sum_fold(Ts... ts) {
    return (ts + ...);  // unary right fold
}
```

`(ts + ...)` folds the whole parameter pack with `+`. Run it, both writes give the same result:

<OnlineCompilerDemo allow-run
  title="Variadic recursion vs a C++17 fold expression"
  source-path="code/examples/vol4/vol3-metaprogramming-cpp20-23/fold_vs_recursion.cpp"
  description="The old way needs a primary template, a recursive case, and a termination specialization. A C++17 (ts + ...) fold replaces all of it."
/>

Run it:

```text
sum_rec(1,2,3,4):  10
sum_fold(1,2,3,4): 10
逗号折叠展开: 1 2.5 hi
```

That last line, "comma fold expansion," is another common use of folds. With the comma operator, you merge a pack of operations. `(printer(1), printer(2.5), printer("hi"))` expands any number of calls in one line. In variadic contexts it has almost completely replaced the old recursive expansion.

There are four forms of fold. One table is enough:

| Form | Syntax | Meaning (pack a, b, c; initial value e) |
|---|---|---|
| Unary right fold | `(pack op ...)` | `(a op (b op c))` |
| Unary left fold | `(... op pack)` | `((a op b) op c)` |
| Binary right fold | `(pack op ... op e)` | `(a op (b op (c op e)))` |
| Binary left fold | `(e op ... op pack)` | `(((e op a) op b) op c)` |

The binary form takes an initial value `e`, mostly to handle the empty-pack case. A unary fold is ill-formed on an empty parameter pack. `(ts + ...)` fails to compile with no arguments, because "nothing" can't be `+`-ed. But three operators have a defined default for empty packs in unary folds: `&&` is `true`, `||` is `false`, and comma is `void()`. So `(... && bs)` compiles even when `bs` is empty. This comes in handy when writing "all of these constraints must hold," and we'll use it again in the comprehensive project.

## Migrating SFINAE onto concepts: old and new for the same need

With all of that in hand, let's weld this piece together with the first three. Same need: "`add` only takes integers." The SFINAE version and the concept version side by side:

```cpp
// SFINAE (since C++11): the constraint hides in a default template parameter
template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
T add_old(T a, T b) { return a + b; }

// concept (C++20): the constraint sits in the signature
template <typename T>
    requires std::integral<T>
T add_new(T a, T b) { return a + b; }
```

Call both with `std::string`. The concept version points straight at the constraint:

```text
error: no matching function for call to 'add_new(std::string, std::string)'
  constraints not satisfied
  required for the satisfaction of 'integral<T>' [with T = std::__cxx11::basic_string<char>]
```

Compared with the SFINAE version's `no type named 'type' in 'std::enable_if<false, void>'`, the difference is plain. The concept version speaks like a person, telling you `integral<T>` wasn't satisfied, and substitutes in the concrete type. The `enable_if` version talks about its own internals, and you have to translate it back to "which requirement failed."

So should all SFINAE migrate to concepts? In general, any SFINAE used to "constrain whether a template parameter is qualified" should move to a concept today. The readability and error quality are a step change. For "does some member or operation exist" needs, requires expressions write just as cleanly (`requires(T t){ t.foo(); }`), so new code should migrate too. The case that doesn't need to migrate is SFINAE used as a "type computation building block," like picking one of two types based on a condition. That should use `std::conditional_t`, which has nothing to do with constraints.

Concepts took over the hardest-to-read part of SFINAE, but not all of TMP: specialization for type queries and fold expressions for compile-time folding are still everyday tools. In the next piece we push TMP toward a concrete direction, compile-time string handling, and see how C++20's NTTP class type makes "a string as a template parameter," something that used to be painful, feel natural.
