---
title: "Shape baked into the type — why dimensions are template parameters"
description: "Why dimensions are template parameters Tensor<Rows,Cols,StorageType> rather than constructor arguments: compile-time fixed size comes with no heap allocation, the type system acts as a free shape-checker moving shape errors to compile time, and dimensions are visible at compile time. The cost: every dimension combination is a distinct type."
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
---

# Shape baked into the type — why dimensions are template parameters

[The previous piece](./04-row-major.md) covered "how the numbers are laid out" (row-major). This piece covers the last piece: **why do those two numbers, Rows and Cols, have to be written into the template parameters `Tensor<Rows, Cols, StorageType>`, instead of being passed in at construction like a normal object?**

You might think, dimensions, just pass rows and cols at construction and be done with it — why bake them into the template parameters and sprout a pile of types. This step is the one most easily skipped by beginners, and yet the most valuable — it moves a whole category of "shape wrong" bugs from runtime to compile time.

## The two ways, side by side

First look at how it'd be written if dimensions didn't go into the type. Roughly this beginner-friendly form:

```cpp
class Tensor {
    float* data_;
    int rows_, cols_;
public:
    Tensor(float* data, int rows, int cols)
        : data_(data), rows_(rows), cols_(cols) {}
};
```

Size is a runtime `int` member, passed in at construction. This is how the vast majority of "dynamic array" classes are written; PyTorch's `torch::Tensor` is like this — shape known only at runtime.

Our way is:

```cpp
template <std::size_t Rows, std::size_t Cols, typename StorageType = float>
class Tensor {
    std::array<StorageType, Rows * Cols> internals_{};
    // rows_/cols_ don't exist; they ARE the template parameters Rows, Cols
};
```

Size isn't a member — it's part of the type. `Tensor<4, 3>` and `Tensor<2, 2>` are two completely different types, fixed at compile time, unchangeable at runtime.

## Benefit one: compile-time fixed size, and no heap allocation along with it

Since Rows and Cols are compile-time constants, `Rows * Cols` is a compile-time constant too, and the size of `std::array<StorageType, Rows*Cols>` is fixed at compile time. The compiler knows how big this object is and lays it out directly on the stack or in static storage — no `new`, no `malloc`. The "compile-time fixed size" and "no heap allocation" constraints are both satisfied in one step.

Conversely, that `float* data_` in the runtime version — where does data point? Either at a heap block from `new` (violates "no heap allocation"), or at an externally-passed buffer (lifetime is on you, dangling references waiting to happen). Both roads have pits.

## Benefit two: the type system catches shape errors for you (the most valuable part)

This is where dimensions-in-the-type truly pays off. A huge class of errors in neural networks is, at root, shape errors: the weight matrix's column count doesn't match the input vector's length, two matrices have mismatched dimensions for multiplication, and so on. If shape is runtime, these can only be checked at runtime — you find out when it crashes or returns an error code. But if shape is part of the type, **the compiler can hold off a whole swath of them at compile time.**

Example. A Dense layer requires the weight matrix's column count to equal the input's length: weights W are `Tensor<4, 3>`, input x must be `Tensor<1, 3>` (that 3 has to match). If one day you slip and pass in a `Tensor<1, 5>` input, the compiler rejects it at compile time — the program never even gets to run.

This is something dynamic-shape frameworks can't give you. PyTorch's runtime shape means a dimension mismatch has to wait until that line of code runs and throws a RuntimeError. Baking the dimensions into the type turns the type system into a free shape-checker.

The specific "how to bake it" — Stage 2 will use `static_assert` and template constraints to pin down dimension relationships when writing Dense, and you'll see it block errors at compile time. For this piece you only need to accept the conclusion: **dimensions in the type, and a whole category of shape errors moves from runtime to compile time.**

## Benefit three: dimensions visible at compile time

A less flashy but quite real side benefit. Since Rows and Cols are compile-time constants, the query functions `row()`, `col()`, `size()` can evaluate at compile time — we mark them `constexpr`, which is enough; writing `static_assert(tensor.size() == 12)` just works. If you really want to force "compile-time only", reach for `consteval` — Stage 1 has no such need.

This also pays off in Stage 5: weights live as `inline constexpr std::array`, and their shape has to be a compile-time-known constant — which lines up exactly with the Tensor's "dimensions in the type" design. The two sides mesh naturally.

## The cost: dimension combinations are type combinations

Honest about the cost. Dimensions in the type means **every dimension combination is a distinct type.** `Tensor<4, 3>`, `Tensor<3, 4>`, `Tensor<2, 2>` are three mutually distinct types; when writing function templates they're different instantiations.

For our Lab this isn't a problem: the MLP's shapes are a fixed few (input 1×3, weights 4×3 and 3×4, biases 1×4 and 1×3), countable, type bloat contained. But if you were writing a general framework that accepts arbitrary shapes and even reshapes dynamically, this "dimensions in the type" wouldn't be enough — you'd have to go back to runtime shape, which is what PyTorch chose. We trade "fixed shape" for "compile-time shape safety"; the deal is worth it for a teaching Lab, not worth it for a general framework. Horses for courses.

## The five-piece intro, complete

Looking back at these five pieces: [01](./01-what-is-tensor.md) demystifies a Tensor into a 2D table, [02](./02-tensor-in-neural-network.md) sees it hold input/weights/bias/output, [03](./03-why-not-built-in.md) rejects three ready-made suspects and forces out a design, [04](./04-row-major.md) nails down the row-major layout, and this piece bakes the dimensions into the type. What a Tensor is, what it holds, why it's built this way — the five pieces of the puzzle are complete.

Next is [06-tensor.md](./06-tensor.md): lay out the full interface, cover the three remaining small decisions (at returns a value not a reference, fixed 2D without variadic templates, how to add the library in CMake), then write it following the interface sketch. All the groundwork from these five pieces is so that the design trade-offs in 06-tensor.md don't read like they're hanging in mid-air.
