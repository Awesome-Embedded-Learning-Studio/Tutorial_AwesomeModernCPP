---
title: "Why not use what's already there — three suspects on trial"
description: "Put std::vector, the native 2D array, and nested std::array in the dock and see which v0.1 hard constraint each dies on, forcing out the minimal design: flat array + index mapping + dimensions as template parameters."
chapter: 8
order: 9
platform: host
difficulty: intermediate
cpp_standard: [23]
reading_time_minutes: 7
prerequisites:
  - "What a Tensor holds in a neural network — four kinds of data, one container"
related:
  - "The fixed-dimension Tensor — the inference engine's data foundation"
  - "Row-major — how a 2D coordinate lands in 1D memory"
  - "Shape baked into the type — why dimensions are template parameters"
tags:
  - host
  - cpp-modern
  - intermediate
  - 内存管理
  - 基础
---

# Why not use what's already there — three suspects on trial

The last two pieces kept saying "we're going to build our own Tensor" without ever answering the sharpest question head-on: **C++ already has a pile of number-holding containers — why not use them, why roll our own?**

This piece takes that question apart. We put the three most ready-made candidates in the dock, try them one by one, and see which hard constraint each dies on. Walk through it once and you'll see our Tensor wasn't invented out of thin air — it was forced out by all those "doesn't work" answers.

First recall what it has to satisfy (the v0.1 hard constraints):

- No allocation on the core inference path (no `new` / `malloc`)
- The Tensor doesn't use `std::vector`
- Size fixed at compile time
- Weights must sit as `inline constexpr std::array`
- And the memory layout has to match NumPy's, for the Stage 6 diff

With that checklist in hand, the trial begins.

## Suspect one: `std::vector` (nested included)

I'm sure everyone's first reaction is this one, right — the most direct choice. `std::vector<float>` for the input, `std::vector<std::vector<float>>` for the weight matrix; dynamic size, comfortable to use.

Where it dies: first, `std::vector` is dynamically allocated internally — elements live on the heap, and construction calls `new` (usually via an allocator), which runs straight into the "no allocation on the core path" constraint. On an MCU, every inference allocating and freeing a pile of small blocks means fragmentation and uncontrollable latency — exactly what embedded work abhors. Second, its size is runtime — `v.size()` isn't known at compile time, which fails "size fixed at compile time". Third, if you use nested `vector<vector<float>>` for a matrix, it's worse: every row is an independent vector with its own heap block, memory isn't contiguous, pointers jump all over the place on access, cache hits go to rot, and N rows means N heap allocations.

So the Lab straight-up blacklists `std::vector` in the hard constraints: "the Tensor doesn't use `std::vector`". Not because it's unusable, but because on our track it has no business showing up.

## Suspect two: the native 2D array `float[Rows][Cols]`

```cpp
float weight[4][3];
```

This one doesn't allocate (stack or static), size is fixed at compile time — looks like it satisfies two constraints.

Where it dies: the C-style array is a half-finished product. First, the moment you pass it into a function it decays into a pointer, dimension info gone on the spot — inside `f(float w[4][3])` you can't get at that 4 and 3, just a bare pointer, and every bounds check has to be hand-written. Second, it has no API at all; to get row/column counts you dodge through `sizeof(weight)/sizeof(weight[0])`, annoying every time. Third, the things we want later — bounds checking (`at()` returning `std::expected`), a flat view (`flat()` returning `std::span`), the contiguous-memory guarantee for diffing against NumPy — it carries none of them, all of it has to be wrapped in an outer layer.

Wrapping it is fine, but if you're going to wrap it anyway, what sits underneath is worth rethinking. Which brings up the third candidate.

## Suspect three: nested `std::array<std::array<float, Cols>, Rows>`

```cpp
std::array<std::array<float, 3>, 4> weight;
```

This is the most proper ready-made way to write a "fixed-size 2D array" in C++. `std::array` doesn't allocate, size is fixed at compile time, memory is contiguous (nested array's overall memory is contiguous too, since the inner array is POD). It patches the "dimension decay" hole the second candidate has, because `std::array` is a proper type — pass it by value or by reference, the dimensions travel with it.

Where it dies: it *works* — it's the closest of the three — but it's awkward. First, access is two-level `weight[i][j]`, not the unified `weight(i, j)` interface we want, and it doesn't match NumPy's `W[i, j]` style either. Second, you have to dig the shape out of the type: in `std::array<std::array<float, 3>, 4>` the outer is 4 (rows), the inner is 3 (columns) — read backwards, easy to flip, and a pain to extract with template metaprogramming. Third and most lethal: the flat contiguous view we want later (`std::span<float, 12>`), diffing against NumPy `flatten()`, the `at()` bounds check — nested array gives none of them directly; you have to manually flatten, manually wrap.

Do the math: rather than use nested array and then spend another lap bolting on a flat view, a unified `(i, j)` interface, a row-major guarantee, and error handling — all that wrapper work adds up to about the same as building a Tensor from scratch. And nested array takes a detour on the "flat contiguous" front: the data is inherently one-dimensional, it insists on laying it out as 2D nesting, and then you flatten it back.

## None of the three works — a design gets forced out

Lay the fates of the three candidates side by side and a common thread shows: **what we need is, in essence, a fixed-size contiguous float storage plus a usable 2D access on top.** The three candidates either get the storage wrong (vector on the heap, and dynamically sized) or leave the access layer too rough (native arrays and nested array neither give you `(i, j)`, `flat()`, `at()`).

So go straight for the essence. Storage is `std::array<float, Rows * Cols>` — a fixed-size contiguous memory, compile-time sized, no heap allocation, naturally matching `inline constexpr std::array` weights. Access wraps a layer on top: `operator()(i, j)` turns the 2D coordinate into a 1D index, and for the cases that need checking, a separate `at()` goes through `std::expected`. Dimensions Rows and Cols go straight into the template parameters — baked into the type, visible at compile time, and as a bonus the type system catches shape errors for you.

These three pieces get picked up by the next few pieces: how a 2D coordinate maps to a 1D index (row-major) is [04](./04-row-major.md); what's good about dimensions as template parameters is [05](./05-shape-in-type.md); what the full interface looks like and how to write it is [06-tensor.md](./06-tensor.md).

And there it is — where this Tensor design came from is clear now: not someone's brainstorm, but the minimal shape forced out after stepping on every downside of the ready-made options.
