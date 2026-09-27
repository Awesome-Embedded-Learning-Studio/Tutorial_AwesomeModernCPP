---
title: "The fixed-dimension Tensor — the inference engine's data foundation"
description: "Build a compile-time fixed-dimension, row-major, std::array-backed Tensor<Rows,Cols,StorageType>, with at returning a std::expected value for exception-free error handling. Three design decisions: at returns a value instead of a reference, fixed 2D, row-major. Companion project at code/volumn_codes/vol8-labs/ai/tiny_ml/stage1/"
chapter: 8
order: 12
platform: host
difficulty: advanced
cpp_standard: [23]
reading_time_minutes: 12
prerequisites:
  - "Project scaffold — pour the toolchain foundation"
  - "Class Templates"
tags:
  - host
  - cpp-modern
  - advanced
  - 模板
  - 内存管理
  - 类型安全
translation:
  source: documents/vol8-domains/ai/tiny_ml/stage1/06-tensor.md
  source_hash: 39e8cde596c9a57858b55e0ca1e11900ee70166724d1c249c0288f00accb9044
  translated_at: '2026-09-26T04:10:30+00:00'
  engine: anthropic
  token_count: 2100
---

# The fixed-dimension Tensor — the inference engine's data foundation

What Stage 1 builds is the data foundation for the entire inference engine: a `Tensor<Rows, Cols, StorageType>` with compile-time fixed dimensions, row-major layout, and `std::array` storage, plus a `std::expected`-based error-handling layer that never throws. Once this stage is done, you can express sensor inputs as `Tensor<1, 3>` and Dense weights as `Tensor<4, 3>` — Dense itself waits until Stage 2. The companion project lives at `code/volumn_codes/vol8-labs/ai/tiny_ml/stage1/`.

This piece is Stage 1's main implementation document. If the Tensor concept itself — or why we insist on building our own — is still new to you, read the [five intro articles](./index.md) first and come back; if you are already clear on what a Tensor is and why it is designed this way, just read on.

## Why build the Tensor first

Stage 1 pulls the three most load-bearing of the v0.1 hard constraints into one place: no heap allocation, no `std::vector`, compile-time fixed size. Once the Tensor is settled, Stage 2's Dense is nothing more than multiply-accumulating two Tensors, and Stage 5's NumPy export is nothing more than filling numbers into an `inline constexpr std::array`. The Tensor's layout is the foundation of the later Python-vs-C++ diff, so this line has to be pinned down in Stage 1 — otherwise Stage 5's golden test will never line up.

## Three design decisions to settle first

### Decision one: at returns a `std::expected` value, not a reference

The inference hot path uses `operator()` (returns a reference, no check, out-of-bounds is UB — the point is speed). But once in a while you need to access one element safely in a spot where the bounds are not certain — if it is out of range, the error has to come back carrying information, not turn into UB. That "checked version" job goes to `at`.

`at`'s return type is `std::expected<StorageType, Error>` (C++23). Your first reaction might be: shouldn't it return a reference? Doesn't `std::array::at` return one? But here we run into a hard C++23 constraint: **`std::expected<T, E>` does not accept T being a reference type.** `std::expected<T&, E>` walks straight into the standard's `static_assert(!is_reference_v<T>)` and won't compile (tested locally on g++ 16.1: it reports a whole chain of errors whose root cause is exactly this line).

So the "return a reference + route errors through expected" road is blocked at the standard level. Three compromises remain: return a pointer (`expected<T*, Error>`), wrap in a `reference_wrapper`, or simply return a value. The first two either give you an ugly interface (one extra dereference) or add a wrapper layer that is hard to explain to beginners. We settled on returning a value: what `at` hands you is a copy of the element, and out-of-range goes down expected's error path.

The cost is that `at` cannot modify the original element — `t.at(0,0).value() = 5.f` modifies the temporary copy inside the expected; the original element stays put. But that is the design intent: in our Lab, `at`'s role is "the checked, safe read"; to mutate an element, go through `operator()`. The inference hot path is all `operator()`; `at` shows up only in tests and verification, where returning a value is plenty — and this small cost buys nothing that would justify overturning the design.

### Decision two: fixed 2D, no variadic template

What dimensions-in-the-type buys you is covered in [intro article 05](./05-shape-in-type.md); here we only discuss what to do once they are in the type — fixed 2D, or variadic N-dimensional. The v0.1 MLP only ever takes two shapes end to end: row vectors (inputs, outputs, biases) and 2D matrices (weights). A fixed-2D `Tensor<Rows, Cols, StorageType>` covers both; there is no need to reach for a variadic `Tensor<StorageType, Dims...>`. That thing is steep to write, and the MLP has no use for 3D+ at all — complexity tax paid for a requirement that does not exist. 1D vectors simply use the alias `Vector<Cols> = Tensor<1, Cols>`; Argmax and Dense inputs and outputs all go through it, keeping the semantics unified.

### Decision three: `std::array` storage + row-major

Storage is not a choice — the hard constraint banned `std::vector`, leaving only `std::array`; the trial the three candidates went through is in [intro article 03](./03-why-not-built-in.md). The layout is **row-major**, `internals_[i*Cols + j]`, matching NumPy's default C order, so that Stage 5's Python weights and the C++ Tensor can be diffed digit by digit — Python's `W[i, j]` and C++'s `W(i, j)` point at the same number. The full derivation of row-major and the memory diagram are in [intro article 04](./04-row-major.md); we won't repeat them here.

## Implementation guide

### Interface sketch (aligned with the project code)

The project has exactly one header, `include/tinyml/tensor.hpp`; the signature looks like this (the implementation is in the project — here we cover the key points):

```cpp
#pragma once
#include <array>
#include <cstddef>
#include <expected>
#include <span>

namespace tamcpp::tinyml {

template <std::size_t Rows, std::size_t Cols, typename StorageType = float>
class Tensor {
  public:
    // Error codes live inside the class: every Tensor dimension carries its own copy — enough, no fuss
    enum class Error { kShapeMismatch, kOutOfRange };

    // Checked safe read: out-of-range goes through expected's error, no exception thrown
    constexpr std::expected<StorageType, Error>
    at(std::size_t i, std::size_t j) noexcept {
        if (i >= Rows || j >= Cols) return std::unexpected{Error::kOutOfRange};
        return internals_[i * Cols + j];
    }
    constexpr std::expected<const StorageType, Error>
    at(std::size_t i, std::size_t j) const noexcept;   // same as above, const version

    static_assert(Rows > 0 && Cols > 0, "dims must be positive");

    constexpr Tensor() = default;
    constexpr Tensor(std::array<StorageType, Rows * Cols> internals)
        : internals_(std::move(internals)) {}

    constexpr std::size_t row() const noexcept { return Rows; }
    constexpr std::size_t col() const noexcept { return Cols; }
    constexpr std::size_t size() const noexcept { return internals_.size(); }

    // Hot-path access: returns a reference, no check, out-of-bounds is UB
    constexpr StorageType&       operator()(std::size_t i, std::size_t j) noexcept;
    constexpr const StorageType& operator()(std::size_t i, std::size_t j) const noexcept;

    // Flat span view (Stage 2 Dense reads weights through it, no copy)
    constexpr std::span<const StorageType, Rows * Cols> view() const noexcept;
    constexpr std::span<StorageType,       Rows * Cols> view()       noexcept;

    constexpr std::array<StorageType, Rows * Cols>&       storage()       noexcept;
    constexpr std::array<const StorageType, Rows * Cols>& storage() const noexcept;

  private:
    std::array<StorageType, Rows * Cols> internals_{};   // this {} must not be dropped — see common pitfalls
};

template <std::size_t Cols, typename StorageType = float>
using Vector = Tensor<1, Cols, StorageType>;

} // namespace tamcpp::tinyml
```

A few easy-to-misread spots are worth calling out.

The template parameter order is `<Rows, Cols, StorageType = float>` — **dimensions first, type defaulted**. So `Tensor<4, 3>` means `Tensor<4, 3, float>` — the dimensions are the core of a Tensor's identity, so they go first, and with the type defaulting to float you save half the typing.

`constexpr` is enough for `row()` / `col()` / `size()` (`static_assert(tensor.size() == 12)` passes); there is no need to insist on `consteval`. `constexpr` also allows runtime calls — a bit more lenient — and if you later decide to lock them to compile time, switching to `consteval` can wait until then.

`at` is `noexcept` — it routes errors through expected instead of throwing, which satisfies the exception-free-core-path constraint. Note that it returns a value, not a reference; the reasoning is in decision one.

**Why the constructors are not marked noexcept.** Functions like `at` and `operator()` are marked `noexcept`, yet the default constructor `Tensor() = default` and the constructor taking a `std::array` are not. The difference: the access-style functions only do an index comparison and an element fetch (for float, copying does not throw), so they genuinely do not throw and marking them `noexcept` carries no risk; the constructors actually have to construct the member `internals_{}`, and whether that step throws depends on StorageType's own constructor. So a constructor's `noexcept` is tied to StorageType — here we simply leave it to the compiler: `= default` automatically gets an implicit exception specification based on "does constructing the member throw", and tested locally on g++ 16.1, `Tensor<4, 3, float>`'s default constructor is deduced as `noexcept(true)`. Not writing noexcept on its face does not mean it throws; for float it simply does not — you are just letting the compiler own that fact for you. The source line `// Q: why not noexcept?` is asking about exactly this.

### CMake: an INTERFACE library + a cross-compiler warning wrapper

The top-level `CMakeLists.txt` declares the inference library as `INTERFACE` (a header-only library — `Tensor` is entirely inline in the header, no `.cpp` artifact):

```cmake
add_library(TAMCPP_TinyML INTERFACE include/tinyml/tensor.hpp)
target_include_directories(TAMCPP_TinyML INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
```

Warning flags are not portable across compilers (MSVC has one set, GCC/Clang another), so wrap them in one reusable function instead of copying generator-expressions into every target:

```cmake
function(tamcpp_target_warnings target)
    target_compile_options(${target} PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4;/permissive-;/Zi>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall;-Wextra;-Wpedantic;-g>
    )
endfunction()
```

Test targets get the same layer of sugar; in `tests/CMakeLists.txt` each test is registered in one line:

```cmake
function(tamcpp_add_test name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE Catch2::Catch2WithMain TAMCPP_TinyML)
    tamcpp_target_warnings(${name})
    catch_discover_tests(${name})
endfunction()

tamcpp_add_test(smoke_catch2 smoke.cpp)
tamcpp_add_test(tensor_api  tensor_api.cpp)   # ← don't miss this one, see pitfall 5
```

One more easily-forgotten line: the top level needs `enable_testing()`, otherwise every `add_test` registered by `catch_discover_tests` dangles in the air, and `ctest` forever reports "No tests found".

## Verification

The cases in `tests/tensor_api.cpp` are the criteria for Stage 1 having "really passed" — especially the row-major one, a prerequisite for the Stage 5 diff:

```cpp
TEST_CASE("dims are compile-time visible", "[tensor]") {
    Tensor<4, 3> tensor;
    static_assert(tensor.size() == 12);
    static_assert(tensor.row() == 4);
    static_assert(tensor.col() == 3);
}

TEST_CASE("construct and access", "[tensor]") {
    Tensor<2, 2> t(std::array{1.f, 2.f, 3.f, 4.f});
    REQUIRE(t(1, 0) == 3.f);
}

TEST_CASE("row-major layout matches flat storage", "[tensor]") {
    Tensor<2, 2> t(std::array{1.f, 2.f, 3.f, 4.f});
    for (std::size_t i = 0; i < 2; ++i)
        for (std::size_t j = 0; j < 2; ++j)
            REQUIRE(t(i, j) == t.storage()[i * 2 + j]);
    REQUIRE(t.view().front() == t.storage()[0]);
}

TEST_CASE("vector is a row tensor", "[tensor]") {
    Vector<3> v;
    static_assert(v.row() == 1 && v.col() == 3);
}

TEST_CASE("out-of-range goes through expected", "[tensor]") {
    Tensor<2, 2> t;
    auto r = t.at(99, 99);
    REQUIRE_FALSE(r);
    REQUIRE(r.error() == Tensor<2, 2>::Error::kOutOfRange);

    // A single out-of-range dimension must be caught too (regression guard against &&: i out of range while j is within)
    auto r_single = t.at(99, 0);
    REQUIRE_FALSE(r_single);
    REQUIRE(r_single.error() == Tensor<2, 2>::Error::kOutOfRange);
}

TEST_CASE("default construction is zero-initialized", "[tensor]") {
    Tensor<2, 2> t;
    REQUIRE(t(0, 0) == 0.f);
}
```

```bash
cmake -S . -B build && cmake --build build -j
ctest --test-dir build
```

All 6 cases green counts as passing. Watch the row-major case especially — it fixes the alignment relationship with NumPy, and if it does not pass, the Stage 5 diff is left without a foundation.

## Common pitfalls

1. **at's bounds check uses `||`, not `&&`**: `if (i >= Rows && j >= Cols)` demands that i and j are **both** out of range before erroring, so a single-dimension overflow (`at(99, 0)`) slips straight through and reads `internals_[198]`, tripping `std::array`'s out-of-bounds assertion. Write `||`, and i or j being out of range — either one — returns the error. Verified locally with ASAN.
2. **The `{}` in `internals_{}` must not be dropped**: without the `{}` on the member, the default construction of `Tensor<2,2> t;` leaves the `std::array` elements indeterminate, and `t(0, 0)` reads uninitialized garbage (UB). If the test passes, it is because the stack garbage happened to be 0 — pure luck. msan locally confirmed `use-of-uninitialized-value`. With the `{}`, the member is value-initialized (float becomes 0.0f), and default construction lives up to its name.
3. **`std::expected` does not accept reference types**: `expected<T&, E>` won't compile (the standard's `static_assert(!is_reference_v<T>)`). So `at` cannot do "return a reference + route errors through expected"; it has to return a value or a pointer. We picked the value; see decision one.
4. **CTAD drops the dimensions**: with `Tensor t(std::array{...})`, class template argument deduction loses both `Rows` and `Cols` — you must write `Tensor<2, 2>` explicitly. Don't count on CTAD to do you any favors here.
5. **`tests/CMakeLists.txt` must register the test target**: write `tensor_api.cpp` but forget `tamcpp_add_test(tensor_api tensor_api.cpp)`, and the build won't compile it at all — the `ctest` you run has only the smoke test. When you add a new test file, remember to register a line for it here.
