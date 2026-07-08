---
title: "The fixed-dimension Tensor — the inference engine's data foundation"
description: "Build a compile-time fixed-dimension, row-major, std::array-backed Tensor<Rows,Cols,StorageType>, with at going through std::expected returning a value for exception-free error handling. Three design decisions: at returns a value not a reference, fixed 2D, row-major. Companion project at code/volumn_codes/vol8-labs/ai/tiny_ml/stage1/"
chapter: 8
order: 12
platform: host
difficulty: advanced
cpp_standard: [23]
reading_time_minutes: 12
prerequisites:
  - "Project scaffold — pour the toolchain foundation"
  - "Templates and non-type parameters"
tags:
  - host
  - cpp-modern
  - advanced
  - 模板
  - 内存管理
  - 类型安全
---

# The fixed-dimension Tensor — the inference engine's data foundation

Stage 1 builds the entire inference engine's data foundation: a compile-time fixed-dimension, row-major, `std::array`-backed `Tensor<Rows, Cols, StorageType>`, plus exception-free error handling based on `std::expected`. Finish this stage and you can express sensor input as `Tensor<1, 3>` and Dense weights as `Tensor<4, 3>` — though Dense itself waits for Stage 2. Companion project at `code/volumn_codes/vol8-labs/ai/tiny_ml/stage1/`.

This is Stage 1's implementation main doc. If the Tensor concept itself, or why we have to build our own, is still unfamiliar, read the [five intro pieces](./index.md) first and come back; if you already know what a Tensor is and why it's designed this way, just keep reading.

## Why build the Tensor first

Stage 1 bundles the three heaviest of the v0.1 hard constraints together: no heap allocation, no `std::vector`, compile-time fixed size. Once the Tensor is fixed, Stage 2's Dense is just multiply-adding two Tensors, and Stage 5's NumPy export is just filling numbers into an `inline constexpr std::array`. The Tensor's layout is the foundation of the later Python-vs-C++ diff; this line has to be nailed down in Stage 1, or Stage 5's golden test will never line up.

## Three design decisions to make first

### Decision one: at returns `std::expected`'s value, not a reference

The inference hot path uses `operator()` (returns a reference, no check, out-of-bounds is UB — what we want is fast). But once in a while you'll access an element safely somewhere the bounds aren't certain — if it's out of range you need the error to come back, not UB. That "checked version" job goes to `at`.

`at`'s return type is `std::expected<StorageType, Error>` (C++23). Your first instinct might be: shouldn't it return a reference? Doesn't `std::array::at` return a reference? But here we hit a hard C++23 constraint: **`std::expected<T, E>` doesn't accept T being a reference type.** `std::expected<T&, E>` fails a standard `static_assert(!is_reference_v<T>)` and won't compile (locally, g++ 16.1 throws a chain of errors whose root cause is exactly this).

So the "return a reference + go through expected errors" road is blocked at the standard level. Three compromises remain: return a pointer (`expected<T*, Error>`), wrap in a `reference_wrapper`, or just return a value. The first two are either an ugly interface (an extra dereference) or an extra wrapper layer that's hard to explain to beginners. We pick returning a value: `at` gets a copy of the element, and out-of-range goes through expected's error path.

The cost is that `at` can't modify the original element — `t.at(0,0).value() = 5.f` modifies the temporary copy inside the expected, the original stays put. But that's by design: `at`'s role in our Lab is "checked safe read"; to mutate an element, use `operator()`. The inference hot path is all `operator()`; `at` is only for tests and verification, where returning a value is plenty — and this cost isn't worth overturning the design for.

### Decision two: fixed 2D, no variadic template

What's good about putting dimensions in the type is covered in [intro 05](./05-shape-in-type.md); here we only discuss, once they're in the type, whether to do fixed 2D or variadic N-dimensional. The v0.1 MLP only ever has two shapes throughout: a row vector (input, output, bias) and a 2D matrix (weights). A fixed-2D `Tensor<Rows, Cols, StorageType>` covers them all; no need to reach for a variadic `Tensor<StorageType, Dims...>`. That thing is steep to write, and the MLP has no use for 3D+ — paying complexity tax for a requirement that doesn't exist. 1D vectors use the alias `Vector<Cols> = Tensor<1, Cols>` directly; Argmax and Dense inputs/outputs all go through it, semantics unified.

### Decision three: `std::array` storage + row-major

Storage isn't a choice — the hard constraint banned `std::vector`, leaving only `std::array`; the trial of the three candidates is in [intro 03](./03-why-not-built-in.md). The layout is **row-major** `internals_[i*Cols + j]`, matching NumPy's default C order, so that Stage 5's Python weights and the C++ Tensor align digit for digit, and Python's `W[i, j]` and C++'s `W(i, j)` point at the same number. The full derivation of row-major and the memory diagram are in [intro 04](./04-row-major.md); not repeated here.

## Implementation guide

### Interface sketch (aligned with the project code)

The project has one header, `include/tinyml/tensor.hpp`; the signature looks like this (implementation in the project; here we cover the key points):

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
    // error codes hang inside the class: each dimension's Tensor carries its own copy — enough, no fuss
    enum class Error { kShapeMismatch, kOutOfRange };

    // checked safe read: out-of-range goes through expected's error, no exception thrown
    constexpr std::expected<StorageType, Error>
    at(std::size_t i, std::size_t j) noexcept {
        if (i >= Rows || j >= Cols) return std::unexpected{Error::kOutOfRange};
        return internals_[i * Cols + j];
    }
    constexpr std::expected<const StorageType, Error>
    at(std::size_t i, std::size_t j) const noexcept;   // same, const version

    static_assert(Rows > 0 && Cols > 0, "dims must be positive");

    constexpr Tensor() = default;
    constexpr Tensor(std::array<StorageType, Rows * Cols> internals)
        : internals_(std::move(internals)) {}

    constexpr std::size_t row() const noexcept { return Rows; }
    constexpr std::size_t col() const noexcept { return Cols; }
    constexpr std::size_t size() const noexcept { return internals_.size(); }

    // hot-path access: returns a reference, no check, out-of-bounds is UB
    constexpr StorageType&       operator()(std::size_t i, std::size_t j) noexcept;
    constexpr const StorageType& operator()(std::size_t i, std::size_t j) const noexcept;

    // flat span view (Stage 2 Dense reads weights through this, no copy)
    constexpr std::span<const StorageType, Rows * Cols> view() const noexcept;
    constexpr std::span<StorageType,       Rows * Cols> view()       noexcept;

    constexpr std::array<StorageType, Rows * Cols>&       storage()       noexcept;
    constexpr std::array<const StorageType, Rows * Cols>& storage() const noexcept;

  private:
    std::array<StorageType, Rows * Cols> internals_{};   // this {} can't be omitted — see common pitfalls
};

template <std::size_t Cols, typename StorageType = float>
using Vector = Tensor<1, Cols, StorageType>;

} // namespace tamcpp::tinyml
```

A few spots easy to misread.

The template parameter order is `<Rows, Cols, StorageType = float>` — **dimensions first, type defaulted**. So `Tensor<4, 3>` is `Tensor<4, 3, float>` — dimensions are the core of a Tensor's identity, they go first, and with type defaulting to float you save half the writing.

`row()` / `col()` / `size()` with `constexpr` is enough (`static_assert(tensor.size() == 12)` passes); no need for `consteval`. `constexpr` allows runtime calls too — more lenient; if you really want compile-time-only, reach for `consteval` later.

`at` is `noexcept` — it goes through expected's error path, throws no exception, matching the no-exceptions-on-core-path constraint. Note it returns a value, not a reference; see decision one.

**Why the constructors aren't marked noexcept.** Functions like `at` and `operator()` are marked `noexcept`; the default constructor `Tensor() = default` and the constructor taking `std::array` aren't. The difference: access-only functions just do index comparison and element fetch (for float, copying doesn't throw) — they genuinely don't throw, so marking `noexcept` carries no risk; constructors actually have to construct the member `internals_{}`, and whether that throws depends on StorageType's own constructor. So the constructor's `noexcept` is tied to StorageType; we leave it to the compiler — `= default` automatically equips an implicit exception specification based on "whether constructing the member throws", and locally on g++ 16.1 `Tensor<4, 3, float>`'s default constructor is inferred as `noexcept(true)`. Not writing noexcept on the surface doesn't mean it throws; for float it just doesn't throw — the compiler simply owns that fact for you. The source line `// Q: why not noexcept?` is asking exactly that.

### CMake: INTERFACE library + cross-compiler warning wrapper

The top-level `CMakeLists.txt` declares the inference library as an `INTERFACE` (header-only; `Tensor` is entirely inline in the header, no `.cpp` product):

```cmake
add_library(TAMCPP_TinyML INTERFACE include/tinyml/tensor.hpp)
target_include_directories(TAMCPP_TinyML INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
```

Warning flags aren't portable across compilers (MSVC has one set, GCC/Clang another); wrap them in a function to reuse, instead of copying generator-expressions into every target:

```cmake
function(tamcpp_target_warnings target)
    target_compile_options(${target} PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4;/permissive-;/Zi>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall;-Wextra;-Wpedantic;-g>
    )
endfunction()
```

Test targets get the same wrapper sugar; `tests/CMakeLists.txt` registers each test in one line:

```cmake
function(tamcpp_add_test name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE Catch2::Catch2WithMain TAMCPP_TinyML)
    tamcpp_target_warnings(${name})
    catch_discover_tests(${name})
endfunction()

tamcpp_add_test(smoke_catch2 smoke.cpp)
tamcpp_add_test(tensor_api  tensor_api.cpp)   # ← don't forget this one, see pitfall 5
```

One more easily-forgotten line: the top level needs `enable_testing()`, otherwise every `add_test` registered by `catch_discover_tests` hangs in the air, and `ctest` forever reports "No tests found".

## Verification

The cases in `tests/tensor_api.cpp` are the criterion for Stage 1 having "really passed"; the row-major one in particular is a prerequisite for the Stage 5 diff:

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

    // single-dimension out-of-range must be caught too (regression guard against &&: i out of range, j within)
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

All 6 cases green counts as passing. Pay special attention to the row-major one — it sets the alignment relationship with NumPy; if it fails, the Stage 5 diff has no foundation.

## Common pitfalls

1. **`at`'s bounds check uses `||`, not `&&`**: `if (i >= Rows && j >= Cols)` requires i, j to **both** be out of range before erroring; single-dimension overflow (`at(99, 0)`) slips right through and accesses `internals_[198]`, tripping `std::array`'s bounds assertion. Write `||` — i or j out of range returns the error. Verified locally with ASAN.
2. **The `{}` in `internals_{}` can't be omitted**: without `{}` on the member, `Tensor<2,2> t;`'s default construction leaves the `std::array` elements indeterminate, and `t(0, 0)` reads uninitialized garbage (UB). If the test passes, it's because the stack garbage happened to be 0 — pure luck. msan locally reports `use-of-uninitialized-value`. Adding `{}` value-initializes (float becomes 0.0f), and default construction means what it says.
3. **`std::expected` doesn't accept reference types**: `expected<T&, E>` won't compile (standard `static_assert(!is_reference_v<T>)`). So `at` can't "return a reference + go through expected errors"; it has to return a value or a pointer. We pick value; see decision one.
4. **CTAD loses dimensions**: `Tensor t(std::array{...})` — this class template argument deduction drops `Rows` / `Cols` entirely; you must write `Tensor<2, 2>` explicitly. Don't count on CTAD to help.
5. **`tests/CMakeLists.txt` must register the test target**: write `tensor_api.cpp` but forget `tamcpp_add_test(tensor_api tensor_api.cpp)`, and the build won't compile it at all — the `ctest` you run only has smoke. Remember to register a line here when adding a new test file.
