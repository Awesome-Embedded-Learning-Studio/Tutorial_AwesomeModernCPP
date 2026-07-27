---
chapter: 13
cpp_standard:
- 20
description: 'Weld the volume together. A mini-STL algorithm library constrained by C++20 concepts. Implementing transform, accumulate, and find_if with proper constraints, and seeing what concepts buy you in a real generic library: readable signatures and error messages that name the constraint.'
difficulty: intermediate
order: 9
platform: host
prerequisites:
- 'Concepts: Putting Constraints in the Signature'
- 'Constraining Templates with Concepts: Subsumption and Overloading'
- 'Requires Expressions, In Depth: The Four Kinds'
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
title: 'Comprehensive Project: A mini-STL Algorithm Library with Concepts'
---
# Comprehensive Project: A mini-STL Algorithm Library with Concepts

We've covered a loop of conceptual material in this volume: how to write concepts, the four kinds of requires expressions, the old TMP techniques, compile-time strings, C++26 reflection, instantiation control, exception safety. This piece welds them into something concrete: a mini-STL algorithm library constrained by concepts. We'll implement three classic algorithms, `transform`, `accumulate`, `find_if`, give them proper constraints, and see what concepts actually buy in a real generic library. No suspense: two things, signatures you can read, and error messages that talk like a person.

## The goal: three algorithms, signatures as documentation

First, what we're building. `transform` runs a function over a range and writes the results out. `accumulate` folds the elements of a range together. `find_if` finds the first element that satisfies a predicate. The standard library has all three (`std::ranges::transform` and friends). We're not replacing them. We're putting the volume's tools into one runnable example.

One design principle: **every template parameter is constrained by a concept, and the signature alone tells you what it needs**. Let's walk through each.

## Two custom concepts: name the needs the standard library doesn't cover

The standard `<concepts>` and `<iterator>` provide a batch of ready-made concepts (`input_iterator`, `predicate`, `invocable`, `convertible_to`, and more), and most needs are covered. But two needs have no off-the-shelf concept, so we name them ourselves:

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

`Addable<T, U>` asks "can `T` and `U` be added, with a result convertible to `U`." The default `U = T` means `Addable<int>` is "int can add with itself." `Ordered` asks "can it be compared with `<`, result convertible to bool." Once these two names exist, the algorithm signatures have something to write. This step maps to the compound requirement from piece 03: `{ a + b } -> std::convertible_to<U>` both checks that the expression is valid and that the return type satisfies the constraint.

## transform: use ready-made range and iterator concepts

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

This signature reads as its own documentation: `R` is an input range, `Out` is an output iterator (writable with `R`'s element type), `F` is callable (accepting a reference to `R`'s element). Three requirements, all in the template parameter list and the requires clause. No `enable_if` nesting anywhere.

One detail from piece 03 is worth rereading here. `std::invocable<F&, std::ranges::range_reference_t<R>>` uses `range_reference_t<R>`, not `range_value_t<R>`. Iterating a range yields **references** to elements, and the function has to accept the reference type to be correct. Getting this type right, before concepts, meant a detour through `decltype` and `std::declval`. Now the standard aliases (`range_value_t`, `range_reference_t`) hand it to you.

## accumulate: the custom Addable takes the stage

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

The constraint is `Addable<T, range_value_t<R>>`: the accumulator type `T` must be addable with the range's element type. That's more direct than the standard `std::accumulate`, which is an unconstrained template. The standard one, given a wrong type, sends the error deep into a substitution failure inside `operator+`. Ours says "`Addable` wasn't satisfied." `Addable` also isn't limited to numbers. As long as a type has `operator+` whose result converts back, `std::string` accumulates just fine (we'll see it in the run below).

## find_if: use std::predicate

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

`std::predicate<Pred&, T>` is stricter than `std::invocable`: not only must it be callable, the return must be convertible to bool. A `find_if` predicate obviously returns bool, so `predicate` fits better than `invocable`. The return type `borrowed_iterator_t<R>` is a ranges detail that handles "does the iterator dangle if a temporary range is passed in." We won't unpack it here.

## Run it

Run all three together, covering numbers, strings, and a predicate:

<OnlineCompilerDemo allow-run
  title="A concepts-constrained mini-STL: transform / accumulate / find_if"
  source-path="code/examples/vol4/vol3-metaprogramming-cpp20-23/mini_stl.cpp"
  description="transform squares, accumulate sums and concatenates, find_if finds the first even. The same accumulate adds integers and concatenates strings."
/>

Run it:

```text
transform squared: 1 4 9 16
accumulate sum:  10
accumulate concat:  start:abc
find_if first even: 2
```

All four lines match what you'd expect. `transform` squares `{1,2,3,4}` into `{1,4,9,16}`. `accumulate` sums to 10. The third line is the interesting one: `accumulate` concatenates strings into `start:abc`. The same algorithm adds integers and concatenates strings, because `Addable` only asks for `operator+`, not for the type to be numeric. That's the value of generics, and concepts make it both flexible (any Addable type) and safe (non-Addable types don't get in).

## Wrong type, error message that talks

The most convincing thing is how it rejects the wrong type. Define a `NoPlus` with no `operator+`, and call `accumulate` on it:

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

Compare that to the `enable_if` hieroglyphics from piece 04. There it was `no type named 'type' in 'std::enable_if<false, void>'`, all about `enable_if`'s internals. Here it says straight that `Addable<NoPlus, ...>` wasn't satisfied, calling out the constraint name and substituting in the concrete type. The reader doesn't need to understand SFINAE. They see "`NoPlus` can't be added." The "save half your hair" promise from piece 01 is delivered, in a real generic library.

## What concepts buy a generic library

Put these three snippets next to an equivalent C++17 `enable_if` version, and the difference is clear. Concepts bring three concrete things.

First, **signatures as documentation**. `requires input_range<R> && output_iterator<Out, ...>` reads like a spec. You don't have to dig into the implementation to see what it assumes about the parameters. Second, **errors name the constraint**. Pass the wrong type and the compiler says "which concept wasn't satisfied," not "enable_if substitution failed." Third, **constraints are reusable and composable**. `Addable` is defined once, and any algorithm that needs "can be added" uses it. Multiple concepts combine with `&&`, and they can participate in overload dispatch through the subsumption rules from piece 02. Together that's the jump from "you can write generic code" to "you can write it comfortably, read it clearly, and survive errors."

## Volume closing

This volume started with concepts, explaining how to write constraints, how they participate in overloading, and the four kinds of requires expressions. Then it stepped back to TMP's old toolkit: the internals of `type_traits`, template recursion, SFINAE, `void_t`, fold expressions, and how to migrate them onto concepts. Compile-time strings and C++20 NTTP class type came next. A look at C++26 static reflection followed. Then back to engineering: template instantiation control, how templates and exceptions tangle, and a mini-STL algorithm library to weld the main line together. Concepts are the spine of this volume, but they aren't all of it. TMP recursion and specialization, fold expressions, conditional `noexcept` are still everyday tools after concepts arrived. Read through this volume and you should be able to write constraints the C++20 way, read the SFINAE and `void_t` in older libraries, and know when to reach for which.
