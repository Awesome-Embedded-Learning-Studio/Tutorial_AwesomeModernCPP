---
chapter: 13
cpp_standard:
- 20
description: 'Welds everything this part covered — concepts, requires, TMP, exception safety — into one thing: a mini-STL algorithm library constrained by C++20 concepts. Implements transform, accumulate, and find_if with proper constraints, and shows what concepts bring to a real generic library: signatures you can read and error messages that behave.'
difficulty: intermediate
order: 9
platform: host
prerequisites:
- 'Concepts: Putting Constraints in the Signature'
- 'Constraining Templates with Concepts: Subsumption and Overloading'
- 'Requires Expressions, In Depth: The Four Kinds of Requirements'
- 'TMP Core Techniques: The World Before Concepts'
reading_time_minutes: 11
related:
- 'Concepts: Putting Constraints in the Signature'
- 'Templates and Exception Safety: move_if_noexcept and Reallocation'
tags:
- host
- cpp-modern
- intermediate
- 模板
- 泛型
- concepts
- 编译期计算
title: 'Capstone Project: A mini-STL Algorithm Library Constrained by Concepts'
translation:
  source: documents/vol4-advanced/vol3-metaprogramming-cpp20-23/09-mini-stl-with-concepts.md
  source_hash: d2c6896b34cb3aa36490d3a71ccb6e11d6b881323cf6aeb3fdba719a0110c8d9
  translated_at: '2026-09-26T05:02:20+00:00'
  engine: anthropic
  token_count: 2900
---
# Capstone Project: A mini-STL Algorithm Library Constrained by Concepts

By this point in the part we have made a full loop of the conceptual material: how to write concepts, the four kinds of requirements in a requires expression, the old TMP tricks, compile-time strings, C++26 reflection, instantiation control, exception safety. This piece welds all of that into something real — a mini-STL algorithm library constrained by concepts. We will implement three classic algorithms, `transform`, `accumulate`, and `find_if`, give them proper constraints, and see what concepts actually buy you in a real generic library. No suspense — just two things: the signature becomes readable, and the error messages start talking like a human.

## The Goal: Three Algorithms, Signatures as Documentation

First, pin down what we are building. `transform` runs a function over the elements of a range and writes the results to an output; `accumulate` adds the elements of a range together; `find_if` finds the first element that satisfies a predicate. The standard library already has all three (`std::ranges::transform` and friends), and rewriting them is not about replacing those — it is about pooling what this part taught into one runnable example.

One design principle up front: **every algorithm's template parameters are constrained by a concept, so the signature alone tells you what it needs**. Let's walk through them one by one.

## Two Custom Concepts: Naming the Needs the Standard Library Doesn't Cover

The standard library's `<concepts>` and `<iterator>` hand you a batch of ready-made concepts (`input_iterator`, `predicate`, `invocable`, `convertible_to`, ...), and most needs are covered directly. But two of our needs have no off-the-shelf concept, so we name them ourselves:

```cpp
template <typename T, typename U = T>
concept Addable = requires(T a, U b) {
    { a + b } -> std::convertible_to<U>;
};

template <typename T>
concept Ordered = requires(T a, T b) {
    { a < b } -> std::convertible_to<bool>;
};
```

`Addable<T, U>` asks: "can `T` and `U` be added, with a result convertible to `U`?" The default `U = T` means `Addable<int>` reads as "int can add to itself." `Ordered` asks "can it be compared with `<`, with a result convertible to bool?" Once these two names stand, the algorithm signatures below have something to be written with. This step maps to the compound requirement from piece 03: `{ a + b } -> std::convertible_to<U>` both checks that the expression is valid and checks that the return type satisfies the constraint.

## transform: Using the Ready-Made Range and Iterator Concepts

```cpp
template <std::ranges::input_range R, typename Out, typename F>
    requires std::output_iterator<Out, std::ranges::range_value_t<R>>
          && std::invocable<F&, std::ranges::range_reference_t<R>>
Out transform(R&& r, Out out, F f) {
    for (auto&& x : r) {
        *out++ = std::invoke(f, x);
    }
    return out;
}
```

This signature reads as its own documentation: `R` is an input range, `Out` is an output iterator (one that can write `R`'s element type), and `F` is a callable (one that accepts references to `R`'s elements). All three requirements sit right in the template parameter list and the requires clause — not a single `enable_if` nesting doll in sight.

One detail from piece 03 is worth rereading here: `std::invocable<F&, std::ranges::range_reference_t<R>>` uses `range_reference_t<R>`, not `range_value_t<R>`. Iterating a range hands you **references** to the elements, and the function has to accept the reference type to be conforming. This "get the type right" kind of work used to require a detour through `decltype` and `std::declval`; now the standard library's aliases (`range_value_t`, `range_reference_t`) hand it to you directly.

## accumulate: The Custom Addable Takes the Stage

```cpp
template <std::ranges::input_range R, typename T>
    requires Addable<T, std::ranges::range_value_t<R>>
T accumulate(R&& r, T init) {
    for (auto&& x : r) {
        init = init + x;
    }
    return init;
}
```

The constraint is `Addable<T, range_value_t<R>>`: the accumulator type `T` must be addable with the range's element type. That is already more direct than the standard `std::accumulate`'s "unconstrained template" — pass the standard one a wrong type and the error burrows all the way into a substitution failure inside `operator+`, while ours simply says "`Addable` was not satisfied." Another virtue of `Addable` is that it **is not limited to numbers**: as long as a type has an `operator+` whose result converts back, `std::string` accumulates just as well (we will see it live in the run below).

## find_if: Using std::predicate

```cpp
template <std::ranges::input_range R, typename Pred>
    requires std::predicate<Pred&, std::ranges::range_reference_t<R>>
std::ranges::borrowed_iterator_t<R> find_if(R&& r, Pred pred) {
    for (auto it = std::ranges::begin(r); it != std::ranges::end(r); ++it) {
        if (std::invoke(pred, *it)) return it;
    }
    return std::ranges::end(r);
}
```

`std::predicate<Pred&, T>` is stricter than `std::invocable`: not only must it be callable, the return type must also be convertible to `bool`. A `find_if` predicate had better return bool, so `predicate` fits better than `invocable`. The return type `borrowed_iterator_t<R>` is a ranges detail that handles "will the iterator dangle if a temporary range is passed in" — we won't unpack it here.

## Running It

Run all three algorithms together, with the tests covering numbers, strings, and a predicate:

<OnlineCompilerDemo allow-run
  title="A concepts-constrained mini-STL: transform / accumulate / find_if"
  source-path="code/examples/vol4/vol3-metaprogramming-cpp20-23/mini_stl.cpp"
  description="transform squares, accumulate sums and concatenates, find_if finds the first even number — the same accumulate adds integers and concatenates strings."
/>

The output:

```text
transform 平方: 1 4 9 16
accumulate 求和:  10
accumulate 拼接:  start:abc
find_if 第一个偶数: 2
```

All four lines match expectations. `transform` squares `{1,2,3,4}` into `{1,4,9,16}`; `accumulate` sums to 10; the third line is the most interesting — `accumulate` concatenates the strings into `start:abc`. One and the same algorithm adds integers and concatenates strings, because `Addable` only demands `operator+` and never presumes numbers. That is the value of generics, and concepts make this generic code both flexible (any Addable type) and safe (non-Addable types don't get in the door).

## Passing the Wrong Type: An Error in Plain Language

The most convincing part is watching how it rejects a wrong type. Define a `NoPlus` with no `operator+` and call `accumulate` on it:

```cpp
struct NoPlus {};
std::vector<NoPlus> v(3);
my::accumulate(v, NoPlus{});   // Addable constraint not satisfied
```

```text
constraints not satisfied
required for the satisfaction of 'Addable<T, std::ranges::range_value_t<_Range>>'
    [with T = NoPlus; R = std::vector<NoPlus, ...>&]
```

Set that next to the `enable_if` hieroglyphics from piece 04. There it was `no type named 'type' in 'std::enable_if<false, void>'`, a message entirely about `enable_if`'s internal machinery; here it says outright that `Addable<NoPlus, ...>` was not satisfied, naming both the constraint and the concrete types. A reader who knows nothing about SFINAE sees at a glance: "ah, `NoPlus` cannot be added." The "half your hair saved" promise from piece 01's discussion of concept error messages — this is exactly how it cashes out in a real generic library.

## What Concepts Bring to a Generic Library

Put these three snippets next to an equivalent C++17 `enable_if` version, and the difference is clear. Concepts bring a generic library three concrete things.

First, **the signature is the documentation**. Written as `requires input_range<R> && output_iterator<Out, ...>`, the signature alone tells you what the algorithm needs; you don't have to dig into the implementation to learn what it secretly assumes about its arguments. Second, **errors name the constraint**. Pass a wrong type, and the compiler says "which concept was not satisfied" instead of "enable_if substitution failed." Third, **constraints are reusable and composable**. `Addable` is defined once, and any algorithm that needs "can be added" can use it; multiple concepts combine with `&&`, and they can even take part in overload dispatch through the subsumption rules from piece 02. Add the three together, and you get the leap from "generic code you can write" to "generic code that is comfortable to write, clear to read, and forgiving when it fails."

## Wrapping Up This Part

This part opened with concepts — how to write constraints, how they take part in overloading, the four kinds of requirements in a requires expression — then stepped back to look at TMP's old craft: the specialization internals of `type_traits`, template recursion, SFINAE, `void_t`, fold expressions, and how to migrate that machinery onto concepts. Next came compile-time strings and C++20's NTTP class type; then a glance toward C++26 static reflection; and finally back down to engineering ground — how to control template instantiation, how templates and exceptions entangle, and this mini-STL algorithm library to weld the whole thread together. Concepts are the spine of this part, but they are not all of it — TMP's recursion and specialization, fold expressions, conditional `noexcept` are still everyday tools for writing generic code after concepts arrived. Finish this part, and you should be able to write constraints the new C++20 way, read the SFINAE and `void_t` idioms in older libraries, and know when to reach for which.
