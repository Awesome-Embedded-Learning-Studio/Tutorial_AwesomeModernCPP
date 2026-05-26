---
chapter: 7
cpp_standard:
- 11
- 14
- 17
- 20
description: A Detailed Look at the std::array Container
difficulty: intermediate
order: 1
platform: host
prerequisites:
- 'Chapter 6: RAII与智能指针'
reading_time_minutes: 6
tags:
- cpp-modern
- host
- intermediate
title: std::array Fixed-Size Container
translation:
  source: documents/vol3-standard-library/01-array.md
  source_hash: 88bd85974c6f55b74530a26bf6cc96111f8a17e3ddaccef992e13ab5f1ed4b6b
  translated_at: '2026-05-26T11:37:08.725307+00:00'
  engine: anthropic
  token_count: 937
---
# Modern C++ for Embedded Systems Tutorial — std::array: Compile-Time Fixed-Size Arrays

When writing embedded code, the heap often feels like an unreliable roommate: it can turn your world upside down at any moment. `std::array` is like that steady, quiet friend—its size is determined at compile time, it lives on the stack or in static storage, it involves no dynamic allocation, its performance is predictable, and its semantics are clear. A key focus here is how it compares to traditional C-style arrays.

------

## What is `std::array`

`std::array` is a lightweight class template that wraps a C-style array: the size `N` is fixed at compile time, it provides an STL-style interface (`at()`, `operator[]`, `front()`, `back()`, iterators, etc.), and it typically incurs little to no runtime overhead compared to a raw array.

------

## Why We Like It in Embedded

- **Zero dynamic allocation**: No `new`/`delete`, making it suitable for heap-less or memory-constrained environments.
- **Predictable memory layout**: Compile-time size and contiguous storage make it easy to use with DMA (Direct Memory Access) and raw pointer interfaces.
- **STL friendly**: Can be passed directly to algorithms (`std::sort`, `std::copy`) and container adapters.
- **constexpr support**: Can be used for compile-time lookup tables or constant data.
- **Type safety and self-documenting**: `std::array<T, N>` clearly expresses intent, offering a more modern approach than `T[N]`.

------

## Basic Usage (Code Examples)

```cpp
#include <array>
#include <cstdio>

int main() {
    // 编译期确定大小，无动态分配
    std::array<int, 5> arr = {1, 2, 3, 4, 5};

    // 范围 for 循环
    for (const auto& val : arr) {
        printf("%d ", val);
    }
    printf("\n");

    // STL 算法
    std::sort(arr.begin(), arr.end());

    // at() 带边界检查（可能抛出 std::out_of_range）
    int x = arr.at(2);

    // size() 是常量表达式
    static_assert(arr.size() == 5);

    return 0;
}
```

A quick reminder: `at()` is not suitable for bare-metal environments where exceptions are disabled or unavailable; use `operator[]` and ensure your indices are correct.

------

## Comparison with C Arrays and `std::vector`

- **Versus C arrays**: `std::array` is a wrapped class that supports `size()`, iterators, `begin()`/`end()`, and structured bindings, and it can be copied or assigned as an object. Under the hood, it is still contiguous memory.
- **Versus std::vector**: `std::vector` can be dynamically resized (requiring the heap), whereas `std::array` is heap-free, fixed in size, has lower overhead, and carries clearer semantics. In embedded development, we generally prefer `std::array`.

------

## Common Techniques and Details (From an Embedded Perspective)

### 1. Static Storage or Stack?

- Small arrays (tens or hundreds of bytes) can live on the stack. Be mindful of the stack depth limits of your tasks/ISRs (interrupt service routines).
- Larger arrays should be declared `static` or placed in a dedicated section, or put in read-only flash (`constexpr` data) to save RAM.

Example:

```cpp
// 栈上：小数组，函数作用域
void foo() {
    std::array<uint8_t, 16> buffer{};
    // ...
}

// 静态区：大数组，生命周期贯穿程序运行
static std::array<uint32_t, 1024> adc_buffer{};

// Flash（只读）：编译期常量表
constexpr std::array<uint8_t, 256> sine_table = []() constexpr {
    std::array<uint8_t, 256> t{};
    for (int i = 0; i < 256; ++i) {
        t[i] = static_cast<uint8_t>(128 + 127 * sin(i * 2 * 3.14159265 / 256));
    }
    return t;
}();
```

### 2. Using with DMA / Peripherals

Because `std::array` guarantees contiguous memory, you can safely pass `data()` to DMA or the HAL (Hardware Abstraction Layer). However, ensure the element type is **trivially copyable and does not have complex construction requirements** (typically, use POD or trivial types).

### 3. Compile-Time Tables and `constexpr`

`std::array` can be used for compile-time constant lookup tables (avoiding runtime initialization):

```cpp
constexpr std::array<int, 4> multipliers = {2, 4, 8, 16};

// 编译期求值
static_assert(multipliers[2] == 8);

constexpr int get_multiplier(int idx) {
    return multipliers[idx]; // 编译期查表
}
```

If you need to generate a more complex table at compile time, you can combine it with `std::index_sequence` for metaprogramming (we won't dive into complex implementations here, but the idea is to use `std::index_sequence` to expand indices and generate elements within a `constexpr` function).

### 4. Structured Bindings and `std::tuple`

`std::array` supports `std::get` and structured bindings (C++17):

```cpp
std::array<int, 3> rgb = {255, 128, 0};

// 结构化绑定
auto [r, g, b] = rgb;

// std::get
int red = std::get<0>(rgb);
```

### 5. Avoiding the Pointer Decay Pitfall

C-style arrays decay into pointers when passed as arguments, but `std::array` does not. You must explicitly pass it by reference (`&`) or by value:

```cpp
// C 数组：退化为指针，丢失大小信息
void c_func(int arr[4]) { // arr 实际是 int*
    // sizeof(arr) != sizeof(int)*4
}

// std::array：不退化，保留大小信息
void cpp_func(const std::array<int, 4>& arr) {
    static_assert(arr.size() == 4);
    int x = arr[0];
}
```

### 6. Compatibility with Bare-Metal Exception Strategies

Some embedded toolchains disable exception support, which affects the use of `at()` (which throws exceptions). In exception-free environments, we recommend using only `operator[]` and employing boundary-checking tools during compilation or development.

------

## Advanced Topic: When Elements Are Not POD

Elements of `std::array` can be of any type `T`. However, there are common caveats in embedded development:

- If `T` has complex constructors/destructors, static initialization behavior (especially zero-initialization) will differ, and you must ensure the construction cost is acceptable.
- For buffers that need to be read or written via DMA, `T` should be trivially copyable.

## Run Online

Experience the basic usage of `std::array`, `constexpr` lookup tables, and structured bindings online:

<OnlineCompilerDemo
  title="std::array Fixed-Size Container"
  source-path="code/examples/vol34567/01_array.cpp"
  description="Experience basic std::array operations, constexpr CRC lookup tables, and structured bindings"
  allow-run
  allow-x86-asm
/>

## Try It Out — Using `std::array` as a Compile-Time CRC Table

```cpp
#include <array>
#include <cstdint>

// 简化的 CRC8 查表生成（编译期）
constexpr uint8_t crc8_table_entry(uint8_t idx) {
    uint8_t crc = idx;
    for (int i = 0; i < 8; ++i) {
        if (crc & 0x80)
            crc = (crc << 1) ^ 0x07;
        else
            crc <<= 1;
    }
    return crc;
}

constexpr std::array<uint8_t, 256> generate_crc_table() {
    std::array<uint8_t, 256> table{};
    for (int i = 0; i < 256; ++i) {
        table[i] = crc8_table_entry(static_cast<uint8_t>(i));
    }
    return table;
}

// 编译期生成，存放在 Flash 中
constexpr auto crc_table = generate_crc_table();

constexpr uint8_t compute_crc8(const uint8_t* data, size_t len) {
    uint8_t crc = 0;
    for (size_t i = 0; i < len; ++i) {
        crc = crc_table[crc ^ data[i]];
    }
    return crc;
}

int main() {
    static_assert(compute_crc8((const uint8_t*)"123456789", 9) == 0xF4);
    return 0;
}
```

On supported toolchains, this approach allows you to place lookup table data in flash, saving RAM.
