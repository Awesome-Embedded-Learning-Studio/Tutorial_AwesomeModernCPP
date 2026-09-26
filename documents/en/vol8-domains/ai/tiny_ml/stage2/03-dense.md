---
title: "The Dense layer — span views and weight layout"
description: "Build the inference engine's first layer that actually computes: Dense<In,Out> points at external weight storage through std::span views instead of owning value members (an instance shrinks from 392KB to 16 bytes), forward does the matvec inline inside the class, and shapes locked in at compile time mean no std::expected is needed. The span means zero interface changes when Stage 5 swaps in inline constexpr weights. Companion project at code/volumn_codes/vol8-labs/ai/tiny_ml/stage2/"
chapter: 8
order: 15
platform: host
difficulty: advanced
cpp_standard: [23]
reading_time_minutes: 14
prerequisites:
  - "The fixed-dimension Tensor — the inference engine's data foundation"
  - "Why weights are [Out, In] — the cache ledger under row-major"
related:
  - "What Dense computes — one multiply-add, broken down per output"
  - "Why weights are [Out, In] — the cache ledger under row-major"
tags:
  - host
  - cpp-modern
  - advanced
  - 模板
  - 内存管理
  - 类型安全
translation:
  source: documents/vol8-domains/ai/tiny_ml/stage2/03-dense.md
  source_hash: e219ba57faa3275e765252b3ec46bb62a3e71d466567bf15b0c35fe0350775e7
  translated_at: '2026-09-25T08:48:25+00:00'
  engine: anthropic
  token_count: 8800
---

# The Dense layer — span views and weight layout

What Stage 2 builds is the whole inference engine's first layer that genuinely "computes something": a `Dense<In, Out>` that takes an input vector of length In, produces an output vector of length Out, and calculates the `y = W·x + b` we already broke down in [piece 01](./01-what-is-dense.md). Activation waits for Stage 3; here we only do the affine part. Companion project at `code/volumn_codes/vol8-labs/ai/tiny_ml/stage2/`.

This is Stage 2's implementation main doc. If what Dense computes, or why the weights are `[Out, In]`, is still unfamiliar to you, read the [two intro pieces](./index.md) first and come back; if you're already clear on those, just keep reading.

## Why build Dense first

With Stage 1's data foundation Tensor built solid, what the inference engine still lacks is the part that "makes Tensors compute against each other". Dense is the only arithmetic layer in our MLP (the whole network is just two Dense layers sandwiching one ReLU); once it stands up, Stage 3's ReLU/Argmax are merely element-wise or reduction operations on a single Tensor, and Stage 4's chaining is merely wiring a few layers into a pipeline. So Dense is the arithmetic foundation of every later stage — like the Tensor, if it collapses, everything downstream has to be rewritten with it.

## Three design decisions to make first

### Decision one: weights carry no storage of their own, only a `std::span` view

This is Stage 2's most load-bearing decision, and the one that diverges furthest from the "intuitive way of writing it".

The intuitive way is to make the weights and bias value members of `Dense`, copied in at construction:

```cpp
template <std::size_t In, std::size_t Out>
struct Dense {
    Tensor<Out, In> weight_;   // value member, owns its storage
    Vector<Out>     bias_;
};
```

Written this way, a `Dense` instance carries storage for `Out*In + Out` floats. Sounds harmless, until you compute `sizeof`. Take the MNIST first-layer `Dense<784, 128>` magnitude (measured locally on g++ 16.1):

```text
sizeof(Dense<784,128>) [value-member version] = 401920 bytes (392.5 KB)
```

One Dense instance carries nearly 400KB of its own. That means wherever the instance goes, the 400KB follows: as a global variable it sits permanently in one copy in `.bss`; as a local variable it gets copied onto the stack (on an MCU with a few KB of stack, that's an instant blow-up); `new`-ed onto the heap, it violates v0.1's heap ban. All three roads are blocked.

You might think: at construction I'll `std::move` the weights in — surely that doesn't count as copying. Measurement says otherwise — `std::array<float, N>`'s move constructor moves element by element, and fundamental types like `float` have no move semantics of their own (a float's move constructor is its copy constructor). So moving a large float weight really does copy `Out*In` times; move saves you nothing here. This point was verified with a move-counter type: moving an `array<MoveTrk, 1000>` triggers exactly 1000 element moves — clear-cut corroboration.

So the right direction is: **`Dense` carries no weight storage of its own, only a view pointing at external storage**. The view is a `std::span` (fixed extent, size known at compile time), and the weight data is maintained outside:

```cpp
template <std::size_t In, std::size_t Out>
struct Dense {
    std::span<const float, Out * In> weight_;   // view, points at external storage
    std::span<const float, Out>     bias_;
};
```

The same `Dense<784, 128>` drops to:

```text
sizeof(Dense<784,128>) [span version] = 16 bytes
```

16 bytes — two fixed-extent spans (8 bytes each, storing only a pointer; the extent is a template parameter, known at compile time, so no length needs storing). Copy the instance freely, stack it freely — zero pressure.

Where this decision really pays off is Stage 5. Weights are in essence "trained constants", and their final form is an `inline constexpr std::array` (baked into `.rodata`, which on an MCU can sit in Flash without occupying RAM). The span approach lets the `Dense` interface accept local test Tensors now, and when Stage 5 swaps the external storage for a global `inline constexpr std::array`, not a single line of `Dense`'s interface or `forward` changes — it only ever looks at a span and doesn't care whether the external storage is a local or a global constant. The other way around, if Stage 2 used value members, Stage 5 would have to overturn the member type from `Tensor` to span and rework the interface once. So span isn't "doing Stage 5's work early" — it's "letting Stage 2's interface evolve into Stage 5's at zero cost". This interface contract gets nailed down right here:

| | External storage | Dense instance | constexpr |
|---|---|---|---|
| **Stage 2 (testing)** | local `Tensor` | holds its `view()` | verified by sealing it inside a constexpr function |
| **Stage 5 (deployment)** | global `inline constexpr std::array` | holds its `view()` | `constexpr Dense layer(gw,gb)` is legal, the whole chain works |

The cost must be spelled out, too. A span is a non-owning view, and `Dense` no longer manages the weights' lifetime itself — the external storage must outlive the `Dense`. In tests, that means the Tensor used to construct the `Dense` can't be a temporary (`Dense(Tensor{...} temporary, ...)`: once it is destroyed the span dangles); it has to be promoted to a named variable. The target scenario (weights as global constexpr constants) satisfies this naturally, so the constraint only costs two extra lines in tests. Incidentally, Dense no longer has a default constructor — a span has no meaningful default state, and a default-constructed Dense would have no weights bound and couldn't forward; such an "empty Dense" has no business meaning either.

### Decision two: weight shape `[Out, In]`

The full argument is in [piece 02](./02-weight-shape.md); here we only restate the conclusion: computing the o-th output reads the o-th row of the weights, and under row-major a row is contiguous and cache-cooperative (measured on 1024×1024: row order is 7× faster than column order); at the same time it aligns with PyTorch `nn.Linear`'s `(out, in)` convention, so Stage 5's diff runs with zero friction. Inside `Dense`, the weight span's extent is written as `Out * In`, and the subscript expands as `o * In + i`.

### Decision three: `forward` is inline inside the class, and doesn't return `std::expected`

Two sub-points.

First, `forward` must be written inside the class (inline); it can't be declared in the header and defined outside the class. That's a dual requirement of template method + `constexpr`: a template method is header-only to begin with, and `constexpr` further demands that the definition be visible at the point of evaluation. Separating declaration and definition makes constexpr evaluation report `used before its definition`; putting it in a `.cpp` reports `used but never defined`. We hit both of these in local testing; writing the body straight into the class just works.

Second, `forward` doesn't go through `std::expected`. Stage 1's `Tensor::at` uses expected because out-of-range is something only known at runtime, and the error information has to come back. But `Dense`'s shape is locked in the template parameters — the input dimension that `forward(const Vector<In>&)` takes is guaranteed by the type, so at runtime there simply is no "wrong shape" error to report. This is the payoff of [Stage 1's piece 05](../stage1/05-shape-in-type.md) stuffing "shape into the type": a whole class of shape errors moves to compile time, and `forward` just computes — no checking to fret over.

## Implementation guide

### Interface sketch (aligned with the project code)

The project has just one header, `include/tinyml/dense.hpp`; the signature looks like this:

```cpp
#pragma once

#include <cstddef>
#include <span>

#include "tinyml/tensor.hpp"

namespace tamcpp::tinyml {

template <std::size_t In, std::size_t Out, typename StorageType = float>
struct Dense {
    static constexpr std::size_t kIn = In;
    static constexpr std::size_t kOut = Out;

    static_assert(In > 0 && Out > 0, "We haven't seen a tensor with 0 size");

    using Bias_t      = Vector<Out, StorageType>;
    using BiasView_t  = std::span<const StorageType, Out>;
    using Weight_t    = Tensor<Out, In, StorageType>;
    using WeightView_t = std::span<const StorageType, Out * In>;

    // Takes a Tensor, stores only its view internally — zero copy.
    // weight / bias must outlive the Dense (a span is a non-owning view).
    constexpr Dense(const Weight_t& weight, const Bias_t& bias) noexcept
        : weight_(weight.view()), bias_(bias.view()) {}

    // y = W·x + b. Shape locked at compile time — no std::expected needed.
    constexpr Vector<Out, StorageType>
    forward(const Vector<In, StorageType>& vec_in) const noexcept {
        Vector<Out, StorageType> result{};
        for (std::size_t vec_out_index = 0; vec_out_index < Out; ++vec_out_index) {
            StorageType& acc = result(0, vec_out_index); // take a reference, saving the subscript recompute on each +=
            acc = bias_[vec_out_index];
            for (std::size_t vec_in_index = 0; vec_in_index < In; ++vec_in_index) {
                acc += vec_in(0, vec_in_index) * weight_[vec_out_index * In + vec_in_index];
            }
        }
        return result;
    }

    constexpr std::span<const StorageType, Out * In> weight() const noexcept { return weight_; }
    constexpr std::span<const StorageType, Out>      bias()    const noexcept { return bias_; }

  private:
    WeightView_t weight_;
    BiasView_t bias_;
};

} // namespace tamcpp::tinyml
```

A few spots easy to misread.

The template parameter order is `<In, Out, StorageType = float>` — **input dimension first**. That's a different semantics from `Tensor<Rows, Cols>`'s "rows first": Tensor's Rows/Cols is the storage shape, while Dense's In/Out is the layer's dataflow direction (how much comes in, how much goes out). The `static constexpr` `kIn`/`kOut` are mirrors for dot-access like `layer.kIn`; their values are simply the template parameters themselves.

`forward`'s double-loop structure — the outer loop over outputs `vec_out_index`, the inner loop over inputs `vec_in_index` — is [piece 02](./02-weight-shape.md)'s cache dividend made concrete: the inner loop reads along one contiguous row of `weight_`. The accumulator `acc` starts from `bias_[o]` and then `+=`s, saving a separate zeroing pass; binding `StorageType& acc` to an element of `result` saves re-running the `operator()` subscript computation on every `+=`. On the `x` side we write `vec_in(0, vec_in_index)` because `Vector<In>` is exactly `Tensor<1, In>` with the row subscript fixed at 0 ([piece 01](./01-what-is-dense.md) mentioned this: a non-zero row subscript runs past the `std::array` bounds — UB at runtime, and it also fails constexpr evaluation at compile time).

`weight()` / `bias()` return a copy of the span (a span is only two or three words, copying is cheap), for tests and for shape-checking when Stage 4 chains layers together.

### CMake: add `dense.hpp` to the INTERFACE source list

The top-level `CMakeLists.txt`'s INTERFACE library needs the new header added (Stage 1 only hung `tensor.hpp` there):

```cmake
add_library(TAMCPP_TinyML INTERFACE
    include/tinyml/tensor.hpp
    include/tinyml/dense.hpp)
```

The header is findable through `target_include_directories`, but if the INTERFACE's `SOURCES` doesn't list it, CMake warns and the IDE doesn't recognize it either. Test targets register with Stage 1's sugar; add one line to `tests/CMakeLists.txt`:

```cmake
tamcpp_add_test(dense_api dense_api.cpp)
```

## Verification

The cases in `tests/dense_api.cpp` are the criterion for Stage 2 having "really passed" — the row-major one in particular is a prerequisite for the Stage 5 diff:

```cpp
TEST_CASE("dense dims are compile-time visible", "[dense]") {
    Tensor<2, 3> w(std::array{0.f, 0.f, 0.f, 0.f, 0.f, 0.f}); // [Out=2, In=3]
    Vector<2> b(std::array{0.f, 0.f});
    Dense<3, 2> layer(w, b);                                   // In=3 -> Out=2
    static_assert(layer.kIn == 3);
    static_assert(layer.kOut == 2);
}

TEST_CASE("dense forward matches hand-computed matvec", "[dense]") {
    // W[Out=2, In=2] = [[1,2],[3,4]], x=[5,6], b=[10,20]
    // y[0] = 1*5 + 2*6 + 10 = 27
    // y[1] = 3*5 + 4*6 + 20 = 59
    Tensor<2, 2> w(std::array{1.f, 2.f, 3.f, 4.f});
    Vector<2> b(std::array{10.f, 20.f});
    Dense<2, 2> layer(w, b);
    Vector<2> x(std::array{5.f, 6.f});
    auto y = layer.forward(x);
    REQUIRE(y(0, 0) == 27.f);
    REQUIRE(y(0, 1) == 59.f);
}

TEST_CASE("dense weight storage is row-major [Out, In]", "[dense]") {
    Tensor<2, 3> w(std::array{1.f, 2.f, 3.f,   // row o=0
                              4.f, 5.f, 6.f}); // row o=1
    Vector<2> b(std::array{0.f, 0.f});
    Dense<3, 2> layer(w, b);                   // In=3, Out=2
    REQUIRE(layer.weight()[1 * 3 + 0] == 4.f); // weight 0 of output 1
}

TEST_CASE("dense zero-weight layer forward is zero", "[dense]") {
    Tensor<2, 2> w(std::array{0.f, 0.f, 0.f, 0.f}); // explicit all zeros (no default constructor)
    Vector<2> b(std::array{0.f, 0.f});
    Dense<2, 2> layer(w, b);
    Vector<2> x(std::array{1.f, 2.f});
    auto y = layer.forward(x);
    REQUIRE(y(0, 0) == 0.f);
    REQUIRE(y(0, 1) == 0.f);
}

// The span holds a pointer; persisting it into a static constexpr variable makes
// g++ report "incompletely initialized variable". Seal it inside a constexpr
// function so the pointer doesn't escape the frame. Only after Stage 5 swaps in
// a global inline constexpr array can you write constexpr Dense layer(global_w, ...)
// directly. See common pitfall 2.
constexpr bool dense_forward_is_constexpr() {
    Tensor<2, 2> w(std::array{1.f, 2.f, 3.f, 4.f});
    Vector<2> b(std::array{0.f, 0.f});
    Dense<2, 2> layer(w, b);
    Vector<2> x(std::array{1.f, 1.f});
    auto y = layer.forward(x);
    return y(0, 0) == 3.f && y(0, 1) == 7.f; // 1*1+2*1=3, 3*1+4*1=7
}

TEST_CASE("dense forward is constexpr-evaluable", "[dense]") {
    static_assert(dense_forward_is_constexpr());
}
```

```bash
cmake -S . -B build && cmake --build build -j
ctest --test-dir build
```

Measured locally: all 12 cases green (6 tensor/smoke cases carried over from Stage 1 + Stage 2's 5 dense cases + 1 smoke):

```text
100% tests passed out of 12
Total Test time (real) = 0.05 sec
```

All 5 dense cases green counts as passing. The hand-computed matvec one (27, 59) verifies that `forward` computes correctly; the row-major one verifies the weight layout matches what [piece 02](./02-weight-shape.md) nailed down; the constexpr one verifies `forward` really is compile-time evaluable.

## Common pitfalls

1. **`forward` must be inline inside the class** (verified locally): the dual requirement of template method + `constexpr`. Declaring in the header and defining outside the class makes constexpr evaluation report `used before its definition`; putting it in a `.cpp` reports `used but never defined`. Write the body straight into the class — don't separate them. This is where the span-version forward differs from Stage 1's Tensor: Tensor's method bodies are all short and naturally fit in-class, while Dense's forward has loop bodies that tempt you to pull it outside the class. Don't.
2. **constexpr can't persist a local span** (verified locally): `constexpr Dense layer(local w, local b)` makes g++ report `not a constant expression / incompletely initialized variable`. Root cause: a span is a pointer inside, and the address of a function-local constexpr object being **persisted into a static constexpr variable** isn't legal. Workaround: seal it inside a constexpr function (the pointer doesn't escape the frame) and verify with `static_assert(func())`. Once Stage 5 swaps the weights for a global `inline constexpr std::array` (static storage duration + compile-time addressable), this restriction lifts on its own — which is exactly where span + inline constexpr fit together.
3. **Loop variables are `std::size_t`, not `int`**: `for (int i = 0; i < Out; ...)` compared against `std::size_t` draws a `-Wsign-compare` warning (measured: two of them under `-Wall -Wextra`). The tests still pass because all the values are small positives that happen to be right after promotion to unsigned; but once `Out > INT_MAX`, `int` overflows (typewise, a signed index should never meet an unsigned dimension). Same as [Stage 1's Tensor](../stage1/06-tensor.md) — loop variables should honestly be `std::size_t`.
4. **The weight subscript is `o*In + i`, not `i*Out + o`**: the span is one-dimensional, and its row-major expansion is `o*In + i`. Write it backwards as `i*Out + o`, and when `Out == In` (the 2×2 test) the shape happens not to error — but what you're computing is Wᵀ·x. That's why the tests deliberately include a `2×3` non-square case (`Dense<3, 2>`) that a square matrix can't mask.
5. **bias forgotten / not used as the starting value**: accumulating only `W·x` and forgetting the bias leaves the hand-computed case off by exactly one bias (27 becomes 17). The convenient way is starting from `acc = bias_[o]` and then accumulating (that's how this project writes it), saving one separate zeroing pass.
6. **Header hygiene**: `dense.hpp` must go into the INTERFACE's `SOURCES` (same kind of pitfall as 1.4); don't forget `#pragma once` on new headers; explicitly include whatever you use, like `<cstdio>` (don't lean on transitive includes — switch compiler or standard-library version and you're on thin ice); don't include the same header twice in a test file (`#pragma once` blocks the redefinition so it didn't blow up, but lint will flag it). The Stage 2 project should also carry a `.gitignore` ignoring `build*/` (Stage 0 has one; a directory copied over from Stage 1 easily misses it), otherwise the whole `build/` gets into git.

## And with that, Stage 2 is complete

Looking back at Stage 2's three pieces: [01](./01-what-is-dense.md) broke the Dense formula into Out weighted sums, [02](./02-weight-shape.md) nailed down the `[Out, In]` weight layout's cache ledger and its alignment with PyTorch, and this piece built `Dense` on span views. `forward` computes the affine part; activation waits for Stage 3.

Next up, Stage 3 builds ReLU (zeroing the negatives) and Argmax (picking the position of the maximum). With those two, Stage 4 can wire two Dense layers sandwiching one ReLU into the complete MLP, with Argmax spitting out the classification result at the end. The span-view storage strategy keeps earning its keep then — when Stage 5 bakes the trained weights into an `inline constexpr std::array`, `Dense` connects without a single interface line changing.
