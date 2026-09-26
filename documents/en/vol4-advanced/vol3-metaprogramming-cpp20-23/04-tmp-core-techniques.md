---
chapter: 13
cpp_standard:
- 17
- 20
description: 'For the decade-plus before concepts, generic libraries leaned on TMP to answer type questions. Covers the specialization internals of type_traits, template recursion, SFINAE and enable_if, the void_t detection idiom, C++17 fold expressions, and how to migrate constraint-style SFINAE onto concepts.'
difficulty: intermediate
order: 4
platform: host
prerequisites:
- 'Concepts: Putting Constraints in the Signature'
- 'Constraining Templates with Concepts: Subsumption and Overloading'
- 'Requires Expressions, In Depth: The Four Kinds of Requirements'
reading_time_minutes: 17
related:
- 'Requires Expressions, In Depth: The Four Kinds of Requirements'
- 'Compile-Time Strings: NTTP Class Type and fixed_string'
tags:
- host
- cpp-modern
- intermediate
- 模板元编程
- 编译期计算
- 泛型
title: 'TMP Core Techniques: The World Before Concepts'
translation:
  source: documents/vol4-advanced/vol3-metaprogramming-cpp20-23/04-tmp-core-techniques.md
  source_hash: 7a0c4ed1b3d6cc4804e22fac8b87bc341ce0b5097d6c2cb71dc7cd56e3bdffb0
  translated_at: '2026-09-26T04:31:29+00:00'
  engine: anthropic
  token_count: 3100
---
# TMP Core Techniques: The World Before Concepts

For three pieces now we've been looking at "constraints" under the light of C++20 concepts. But concepts only entered the standard in 2020. For more than a decade before that, the author of a generic library answered "is this type qualified" with an entirely different mechanism: template metaprogramming (TMP). This piece steps back to look at a few of TMP's signature moves: using specialization to ask questions at compile time, template recursion to loop at compile time, SFINAE to let an ill-fitting overload bow out gracefully, `void_t` to detect whether a member exists, and C++17 fold expressions to replace a good chunk of template recursion.

This stuff looks verbose today, but concepts still haven't fully replaced it. Open the standard library source, open Boost, open Chromium's base: SFINAE and `void_t` are everywhere. What concepts took over is the "is this type qualified" family of constraints; the "compute a value on the type" work still falls to TMP. So this piece is groundwork you can't dodge when reading or writing real generic code.

## Inside type_traits: specialization is compile-time if-else

`std::is_pointer_v<int*>` works out to `true`, and `std::is_pointer_v<int>` works out to `false`. How is that compile-time judgment implemented? The answer is disarmingly plain: template partial specialization. Hand-write an `is_pointer` ourselves and the structure looks about like the standard library's:

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

The primary template is the catch-all, handing every type a `false`; the partial specialization exists to match `T*` — "pointer to some type" — and hands it a `true`. When the compiler instantiates `is_pointer_impl<int*>`, it finds the partial specialization fits better than the primary and picks it. When it instantiates `is_pointer_impl<int>`, no more-specialized version matches, so it falls back to the primary. This is TMP's most basic paradigm: dispatch at compile time with specialization, equivalent to an if-else unrolled over types.

Run it and check against the standard library:

<OnlineCompilerDemo allow-run
  title="Hand-written is_pointer: specialization as compile-time dispatch"
  source-path="code/examples/vol4/vol3-metaprogramming-cpp20-23/traits_from_scratch.cpp"
  description="The primary template defaults to false, the partial specialization matches T* and gives true, and the results agree with std::is_pointer."
/>

Output:

```text
is_pointer_v<int>:    false
is_pointer_v<int*>:   true
is_pointer_v<int**>:  true
is_pointer_v<double*>:true
与 std::is_pointer 结果一致
```

The one-to-two hundred traits in the standard library's `<type_traits>` (`is_integral`, `is_class`, `remove_const`, `decay`, ...) are almost all this same machinery underneath: a primary template plus a set of partial specializations covering the various cases. `remove_const_t<const int>` turning into `int` is nothing more than a partial specialization for `const T` with `using type = T` inside. Once this "specialization is dispatch" model clicks for you, reading the `<type_traits>` source stops being intimidating.

## Template recursion: instantiation as a loop

Type queries alone aren't enough; TMP can also do arithmetic at compile time. The trick is to unroll the loop into a chain of template instantiations, with specialization providing the termination condition. The classic example is factorial:

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

Evaluating `Factorial<5>::value` is the compiler instantiating its way down at compile time: `Factorial<5>` refers to `Factorial<4>::value`, `Factorial<4>` refers to `Factorial<3>`, until it hits the full specialization `Factorial<0>`, whose `value` is `1` — only then does the recursion wind back up and multiply out each level. This unrolling happens entirely at compile time; by run time `Factorial<5>::value` is just the constant `120`, with no function-call overhead at all.

<OnlineCompilerDemo allow-run
  title="Template recursion computing factorial, with specialization as the termination condition"
  source-path="code/examples/vol4/vol3-metaprogramming-cpp20-23/tmp_factorial.cpp"
  description="Factorial<N> recursively refers to Factorial<N-1> until it hits the full specialization Factorial<0>; the value is settled at compile time."
/>

Output:

```text
Factorial<5>::value  = 120
Factorial<10>::value = 3628800
编译期断言全部通过
```

`static_assert` can assert at compile time that `Factorial<10>::value == 3628800`, which shows the value is already settled during compilation. This "template recursion + specialization as termination" is TMP's classic loop pattern, once used for heavy jobs like compile-time sorting, compile-time string processing, and typelist manipulation. Its costs are just as real: deep instantiation chains, slow compiles, and error messages that are notoriously hard to read. That is the direct motivation behind fold expressions.

## SFINAE: letting the wrong overload bow out gracefully

Traits answer "what is this type," but a generic library has another problem to solve: I have two overloads, one for integers and one for floating point — how do I make the compiler, when handed the wrong type, **not raise an error but quietly skip the overload that doesn't fit**? The answer is a set of rules called SFINAE, short for "Substitution Failure Is Not An Error."

The meaning: while substituting template parameters into the signature, if some step of the substitution goes wrong (say it produces a nonexistent type like `int::value_type`), the compiler does not error out immediately. Instead it marks that overload as a "substitution failure," silently kicks it out of the candidate set, and moves on to try the others. `std::enable_if` is the veteran tool built on this rule; it hides the constraint inside a default template parameter:

```cpp
template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
T add_old(T a, T b) {
    return a + b;
}
```

`std::enable_if_t<cond>` has a nested `type` only when the condition is `true`; when the condition is `false` it is an empty shell, and substituting it in makes the default parameter's deduction fail, so SFINAE kicks the overload out. The upshot: passing `int` matches, while with a `std::string` this overload is not in the candidate set in the first place.

SFINAE works, but its error messages are famously torturous. Let's deliberately call that `add_old` above with a `std::string` and see what GCC 16.1.1 spits out (excerpt):

```text
error: no matching function for call to 'add_old(std::string, std::string)'
  candidate: 'template<class T, class> T add_old(T, T)'
    template argument deduction/substitution failed:
    error: no type named 'type' in 'struct std::enable_if<false, void>'
```

The heart of the error is `no type named 'type' in 'std::enable_if<false, void>'`. It describes `enable_if`'s internal machinery — the condition is false, therefore there is no member `type` — and pointedly says nothing about the thing you actually care about. You have to understand SFINAE first, then reason backward to "ah, it's because string isn't an integer." This is exactly the old road the previous three pieces kept contrasting with concepts; here we look at it from the mechanism side, to see clearly why it reads so badly.

## void_t and the detection idiom: C++17's moment of glory

SFINAE's most elegant use is a pattern proposed around 2014 by Walter Brown called the detection idiom. At its core sits a tool that looks like it does nothing at all: `std::void_t`.

```cpp
template <typename...>
using void_t = void;
```

`void_t` maps any type to `void`. By itself it has zero logic, but paired with partial specialization it can do the single hardest thing of the SFINAE era: **elegantly detect whether a type has a given member**. Let's detect whether `T` has a nested type named `value_type`:

```cpp
template <typename T, typename = void>
struct has_value_type : std::false_type {};

template <typename T>
struct has_value_type<T, std::void_t<typename T::value_type>> : std::true_type {};
```

The primary template inherits `false_type` by default; the partial specialization's second template parameter is `std::void_t<typename T::value_type>`. The key is how the compiler picks between the two versions. When instantiating `has_value_type<std::vector<int>>`, the compiler tries the more specialized partial specialization first, substituting `std::vector<int>` for `T` and then attempting to evaluate `std::void_t<std::vector<int>::value_type>` — `vector<int>` really does have `value_type`, the substitution succeeds, `void_t` turns it into `void`, the partial specialization's second parameter settles as `void`, which lines up with the primary template's default `void`, the partial specialization is chosen, and the result is `true_type`. The other way around, instantiating `has_value_type<int>`: `int::value_type` does not exist, the substitution fails — and by the SFINAE rules a failure is not an error, the partial specialization is quietly kicked out, the compiler falls back to the primary template, and the result is `false_type`.

Run it:

<OnlineCompilerDemo allow-run
  title="The void_t detection idiom: detecting whether a nested type exists"
  source-path="code/examples/vol4/vol3-metaprogramming-cpp20-23/void_t_detection.cpp"
  description="The primary template inherits false_type; the partial specialization gets selected whenever the void_t substitution succeeds, so the answer comes back true or false gracefully."
/>

Output:

```text
has_value_type_v<std::vector<int>>: true
has_value_type_v<std::string>:      true
has_value_type_v<int>:              false
```

`void_t` condenses the old, dirty, screen-wide SFINAE detection code down to two lines of partial specialization, and that is why it was formally adopted into the standard in C++17. But one fact needs saying plainly.

::: warning Only void_t made it into the standard, not the whole detection idiom
Walter Brown wrote three proposals around the detection idiom (N3911 → N4436 → N4502); the last one, N4502, proposed adopting a ready-made detection toolkit — `std::is_detected`, `std::detected_t`, and friends — into the standard library. But that library toolkit **was ultimately never voted in**; the only atomic part that entered C++17 is `void_t` itself. That is why you won't find `std::is_detected` in the standard library: to use the detection idiom you either hand-write it following the two-line partial specialization above, or lean on a third-party library such as Boost. After concepts arrived, the need to "detect whether some operation exists" can mostly be written more directly as `requires(T t){ t.foo(); }`, so the detection idiom is fading from new code — but when you read older libraries, it is still everywhere.
:::

## Fold expressions: killing the recursion boilerplate

Template recursion can compute factorials, but for a variadic job like "sum any number of arguments," the recursive spelling needs a primary template, a recursive branch, plus a terminating specialization — heavy boilerplate. C++17 fold expressions do away with that entirely. Compare the two snippets:

```cpp
// The old way: recursion + termination
template <typename T>
constexpr T sum_rec(T first) { return first; }

template <typename T, typename... Rest>
constexpr T sum_rec(T first, Rest... rest) {
    return first + sum_rec(rest...);
}

// C++17: one fold expression collects it all
template <typename... Ts>
constexpr auto sum_fold(Ts... ts) {
    return (ts + ...);  // unary right fold
}
```

`(ts + ...)` folds everything in the parameter pack together with `+`. Run it: both spellings give the same result.

<OnlineCompilerDemo allow-run
  title="Variadic recursion vs a C++17 fold expression"
  source-path="code/examples/vol4/vol3-metaprogramming-cpp20-23/fold_vs_recursion.cpp"
  description="The old way needs three pieces — primary template, recursive branch, terminating specialization — while C++17 collapses it all into one (ts + ...) fold."
/>

Output:

```text
sum_rec(1,2,3,4):  10
sum_fold(1,2,3,4): 10
逗号折叠展开: 1 2.5 hi
```

That last line, the comma-fold expansion, is another common use of folds: bundle a pack of operations with the comma operator, and `(printer(1), printer(2.5), printer("hi"))` expands any number of calls in a single line. In variadic settings this has nearly replaced the old recursive expansion.

Folds come in exactly four forms; one table is all you need to remember:

| Form | Syntax | Meaning (pack is a, b, c; init value e) |
|---|---|---|
| Unary right fold | `(pack op ...)` | `(a op (b op c))` |
| Unary left fold | `(... op pack)` | `((a op b) op c)` |
| Binary right fold | `(pack op ... op e)` | `(a op (b op (c op e)))` |
| Binary left fold | `(e op ... op pack)` | `(((e op a) op b) op c)` |

The binary forms carry an initial value `e`, mainly to solve the empty-pack problem. A unary fold over an empty parameter pack is ill-formed: `(ts + ...)` with no arguments fails to compile, because there is no way to `+` "nothing." But unary folds over `&&`, `||`, and the comma operator have prescribed defaults for an empty pack (`&&` gives `true`, `||` gives `false`, comma gives `void()`), so `(... && bs)` compiles even when `bs` is empty. This is especially useful when writing "every one of a pile of constraints holds," and it will come up again in the capstone project later in this volume.

## Migrating SFINAE to concepts: old and new spellings of the same need

At this point we can weld this piece together with the previous three. Take the same requirement — an `add` that accepts only integers — and put the old SFINAE way next to the new concept way:

```cpp
// SFINAE (since C++11): the constraint hides in a default template parameter
template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
T add_old(T a, T b) { return a + b; }

// concept (C++20): the constraint lives in the signature
template <typename T>
    requires std::integral<T>
T add_new(T a, T b) { return a + b; }
```

Call the concept version with a `std::string` and the error names the constraint outright:

```text
error: no matching function for call to 'add_new(std::string, std::string)'
  constraints not satisfied
  required for the satisfaction of 'integral<T>' [with T = std::__cxx11::basic_string<char>]
```

Compare that with the SFINAE version's `no type named 'type' in 'std::enable_if<false, void>'` from earlier and the difference jumps out: the concept version speaks human, telling you directly that `integral<T>` was not satisfied, and even substituting in the concrete type. The `enable_if` version talks about its own internal plumbing, and you have to translate it back yourself into "which requirement exactly failed."

So should all SFINAE migrate to concepts? Broadly, yes: any SFINAE whose job is "constrain whether a template parameter is qualified" should prefer a concept today — the readability and the error-message quality are a step change. Needs of the detection-idiom kind, "does this member or operation exist," also write quite cleanly with a requires expression (`requires(T t){ t.foo(); }`), and migrating them in new code is likewise recommended. What genuinely does not need to migrate is SFINAE used as a type-computation part — picking one of two types based on some condition, for instance, was always `std::conditional_t`'s job and has nothing to do with constraints.

Concepts took over the least readable part of SFINAE, but not all of TMP: specialization for type queries and fold expressions for compile-time folding remain everyday tools for writing generic code. In the next piece we push TMP in one concrete direction — compile-time string handling — and see how C++20's NTTP class types turned "a string as a template parameter," once an extremely painful business, into something that feels natural.
