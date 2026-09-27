---
title: "Shape baked into the type — why dimensions are template parameters"
description: "Why dimensions belong in the template parameters Tensor<Rows, Cols, StorageType> rather than constructor arguments: compile-time fixed size comes with no heap allocation, the type system doubles as a free shape checker that moves shape errors to compile time, and dimensions stay visible at compile time. The cost: every dimension combination is a distinct type."
chapter: 8
order: 11
platform: host
difficulty: intermediate
cpp_standard: [23]
reading_time_minutes: 6
prerequisites:
  - "Row-major — how a 2D coordinate lands in 1D memory"
related:
  - "The fixed-dimension Tensor — the inference engine's data foundation"
  - "Why not use what's already there — three suspects on trial"
tags:
  - host
  - cpp-modern
  - intermediate
  - 模板
  - 类型安全
translation:
  source: documents/vol8-domains/ai/tiny_ml/stage1/05-shape-in-type.md
  source_hash: 631e5122deb9baaa0b3c44bbf3d0dce229b692b0d240b434f1b0e840057a1d5b
  translated_at: '2026-09-26T03:56:53+00:00'
  engine: anthropic
  token_count: 2500
---

# Shape baked into the type — why dimensions are template parameters

The [previous piece](./04-row-major.md) covered "how the numbers are laid out" (row-major). This one settles the last piece of the puzzle: **why must those two numbers, Rows and Cols, be written into the template parameters `Tensor<Rows, Cols, StorageType>`, rather than passed in at construction time the way an ordinary object does it?**

You might be thinking: dimensions? Just pass rows and cols to the constructor and call it a day — why go out of your way to cram them into template parameters and breed a whole pile of types? This is the step beginners skip most easily in Tensor design, and also the most valuable one — it moves an entire class of "shape mismatch" bugs from runtime to compile time.

## The two approaches, side by side

First, look at how it would be written if the dimensions didn't go into the type. Roughly this beginner-friendly form:

```cpp
class Tensor {
    float* data_;
    int rows_, cols_;
public:
    Tensor(float* data, int rows, int cols)
        : data_(data), rows_(rows), cols_(cols) {}
};
```

The size is a runtime `int` member, passed in at construction. This is how the vast majority of "dynamic array" classes are written — PyTorch's `torch::Tensor` is exactly this: the shape is known only at runtime.

Our version:

```cpp
template <std::size_t Rows, std::size_t Cols, typename StorageType = float>
class Tensor {
    std::array<StorageType, Rows * Cols> internals_{};
    // no rows_/cols_ members — they are the template parameters Rows and Cols themselves
};
```

The size is not a member; it is part of the type. `Tensor<4, 3>` and `Tensor<2, 2>` are two entirely different types, nailed down at compile time and impossible to change at runtime.

## Benefit 1: compile-time fixed size — and no heap allocation along with it

Since Rows and Cols are compile-time constants, `Rows * Cols` is a compile-time constant too, so the size of `std::array<StorageType, Rows*Cols>` is settled at compile time. The compiler knows exactly how big this object is and carves it out directly on the stack or in static storage — no new, no malloc. Two items from the hard constraints, "compile-time fixed size" and "no heap allocation", are satisfied in one stroke.

Flip it around and look at the runtime version's `float* data_`: where does data point? Either at a heap block allocated with new (running straight into "no heap allocation"), or at a buffer handed in from outside (whose lifetime you now have to manage yourself — an easy way to end up with dangling references). Both roads are lined with potholes.

## Benefit 2: the type system catches shape errors for you (the most valuable part)

Here is the real power of putting dimensions in the type. A great many neural network errors are, at bottom, shape errors: the weight matrix's column count doesn't match the input vector's length, two matrices get multiplied with mismatched dimensions, and so on. When shape is a runtime property, these can only be checked at runtime — you find out when the program crashes or hands you an error code. But when shape is part of the type, **the compiler blocks out a whole swath of them for you at compile time**.

A concrete example. A Dense layer demands that the weight matrix's column count equal the input's length: if the weight W is `Tensor<4, 3>`, the input x must be `Tensor<1, 3>` (that 3 has to line up). The day your hand slips and you pass in a `Tensor<1, 5>` input, the compiler rejects it during compilation — the program never even gets the chance to run.

This is a capability dynamically-shaped frameworks cannot offer. With PyTorch's runtime shapes, a dimension mismatch stays hidden until execution reaches that line of code and throws a RuntimeError. Stuffing the dimensions into the type, as we do, is effectively hiring the type system as a free shape checker.

As for how exactly we "stuff" them — when we write the Dense layer in Stage 2, we'll pin the dimension relationships down with `static_assert` and template constraints, and you'll see then how it blocks errors at compile time. From this piece you only need to accept one conclusion for now: **with dimensions in the type, an entire class of shape errors moves from runtime to compile time**.

## Benefit 3: dimensions visible at compile time

There's also a less showy but very real benefit along the way. Since Rows and Cols are compile-time constants, query functions like `row()`, `col()`, and `size()` can be evaluated at compile time — we mark them `constexpr`, which does the job: writing `static_assert(tensor.size() == 12)` simply passes. If you genuinely want to restrict them to "compile-time evaluation only", you'd reach for `consteval` — Stage 1 has no hard need for that.

This also feeds Stage 5: the weights have to be stored as `inline constexpr std::array`, and their shape must be a compile-time-known constant — a perfect match for this "dimensions in the type" design of Tensor. The two sides mesh naturally.

## The cost: dimension combinations are type combinations

Now for an honest account of the cost. Dimensions in the type means **every dimension combination is an independent type**. `Tensor<4, 3>`, `Tensor<3, 4>`, and `Tensor<2, 2>` are three mutually distinct types; when you write a function template, they are separate instantiations.

For our Lab this is not a problem: the MLP's shapes are a fixed, countable handful (input 1×3, weights 4×3 and 3×4, biases 1×4 and 1×3), so type bloat stays under control. But if you're writing a general-purpose framework that accepts arbitrary shapes and supports dynamic reshaping, this "dimensions in the type" scheme stops being enough — you'd have to go back down the old runtime-shape road, which is exactly the choice PyTorch made. We're trading "fixed shapes" for "compile-time shape safety": a bargain for a teaching Lab, not a bargain for a general framework. Horses for courses.

## Five pieces of groundwork, and the set is complete

Look back over the five pieces: [01](./01-what-is-tensor.md) demystified the Tensor into a 2D table of numbers, [02](./02-tensor-in-neural-network.md) watched it hold four kinds of data — inputs/weights/biases/outputs, [03](./03-why-not-built-in.md) voted down three ready-made candidates and squeezed out our own design, [04](./04-row-major.md) settled the row-major layout, and this piece baked the dimensions into the type. What a Tensor is, what it holds, why it's built this way — the five puzzle pieces are now all in place.

The next step is [06-tensor.md](./06-tensor.md): lay out the complete interface, walk through the three remaining small decisions (at returning values instead of references, staying fixed at 2D rather than becoming a variadic template, and how to add the library in CMake), then roll up your sleeves and write it following the interface sketch. The whole point of the groundwork in these five pieces is that when you read the design trade-offs in 06-tensor.md, they won't be left hanging in mid-air.
