---
chapter: 7
cpp_standard:
- 11
- 14
- 17
- 20
description: Detailed Explanation of std::array Container
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
title: std::array fixed-size container
---
# Modern C++ for Embedded Systems Tutorial — std::array: Compile-Time Fixed-Size Arrays

When writing embedded code, the heap often feels like an unreliable roommate: it can flip the room upside down at any moment. `std::array` is like that steady, quiet friend—its size is determined at compile time, it lives on the stack or in static storage, it has no dynamic allocation, its performance is predictable, and its semantics are clear. A key focus for us is how it compares to traditional C-style arrays.

------

## What is `std::array`

`std::array` is a lightweight class template that wraps a C-style array: the size `N` is fixed at compile time, it provides an STL-style interface (`begin`, `end`, `size`, `data`, iterators, etc.), and it typically incurs little to no runtime overhead compared to a raw array.

------

## Why We Like It in Embedded Systems

- **Zero dynamic allocation**: No `new`/`delete`, making it suitable for heap-less or memory-constrained environments.
- **Predictable memory layout**: Compile-time size and contiguous storage make it easy to use with DMA (Direct Memory Access) and raw pointer interfaces.
- **STL friendly**: Can be passed directly to algorithms (`std::sort`, `std::find`) and container adapters.
- **constexpr support**: Can be used for compile-time lookup tables or constant data.
- **Type safety and self-documenting**: `std::array<T, N>` clearly expresses intent, offering a more modern approach than `T[N]`.

------

## Basic Usage (Code Examples)

```cpp
#include <array>
#include <cstdio>

int main() {
    // 编译期确定大小，栈上分配
    std::array<int, 5> arr = {1, 2, 3, 4, 5};

    // STL 风格访问
    for (auto& elem : arr) {
        printf("%d ", elem);
    }
    printf("\n");

    // 随机访问（无边界检查）
    int val = arr[2]; // 3

    // 带边界检查的访问（可能抛出 std::out_of_range）
    int safe_val = arr.at(4); // 5

    // 获取底层指针（用于 DMA 等）
    int* raw_ptr = arr.data();

    // 填充
    arr.fill(0);

    // 大小
    size_t sz = arr.size(); // 5

    return 0;
}
```

A quick reminder: `at()` is not suitable for bare-metal environments where exceptions are disabled or unavailable; use `operator[]` and ensure your indices are correct.

------

## Comparison with C Arrays and `std::vector`

- **Versus C arrays**: `std::array` is a wrapped class that supports `size()`, iterators, `at()`, structured bindings, and can be copied or assigned as an object. Under the hood, it is still contiguous memory.
- **Versus std::vector**: `std::vector` can be dynamically resized (requiring the heap), whereas `std::array` is heap-free, fixed in size, has lower overhead, and has clearer semantics. Embedded systems generally favor `std::array`.

------

## Common Techniques and Details (From an Embedded Perspective)

### 1. Static Storage or Stack?

- Small arrays (tens or hundreds of bytes) can be placed on the stack. Be mindful of the stack depth limits of tasks/ISRs (interrupt service routines).
- Larger arrays should be made `static`, placed in a specific section, or put in read-only flash (`const` data) to save RAM.

Example:

```cpp
void task_function() {
    // 栈上：小数组，注意栈深度
    std::array<uint8_t, 32> rx_buffer{};

    // 静态区：大数组，不占栈空间
    static std::array<uint8_t, 1024> dma_buffer{};

    // 只读闪存：查表数据
    static const std::array<uint16_t, 256> sine_table = {/* ... */};
}
```

### 2. Using with DMA / Peripherals

Because `std::array` guarantees contiguous memory, you can safely pass `data()` to DMA or the HAL (Hardware Abstraction Layer). However, ensure the element type is **copyable and not a complex type requiring special construction** (typically, use POD or trivial types).

### 3. Compile-Time Tables and `constexpr`

`std::array` can be used for compile-time constant lookup tables (avoiding runtime initialization):

```cpp
constexpr std::array<int, 4> offsets = {10, 20, 30, 40};

constexpr int get_offset(size_t idx) {
    return offsets[idx];
}

// 编译期求值
static_assert(get_offset(2) == 30);
```

If you need to generate a more complex table at compile time, you can combine it with `std::index_sequence` for metaprogramming (we won't dive into complex implementations here, but the idea is to use `std::index_sequence` to expand indices and generate elements within a `constexpr` function).

### 4. Structured Bindings and `std::tuple`

`std::array` supports `std::get` and structured bindings (C++17):

```cpp
std::array<int, 3> rgb = {255, 128, 64};

// 结构化绑定
auto [r, g, b] = rgb;

// std::get
int green = std::get<1>(rgb);
```

### 5. Avoiding the Pointer Decay Pitfall

C-style arrays decay into pointers when passed as arguments, but `std::array` does not. You must explicitly pass it by `std::array<T, N>&`, reference, or `std::span`:

```cpp
// 错误：C 数组退化为指针，丢失大小信息
void process_c(int buf[8]); // 实际上是 int*

// 正确：std::array 保持类型和大小
void process_arr(std::array<int, 8>& buf);

// 更灵活：使用 std::span（C++20）
void process_span(std::span<int> buf);
```

### 6. Compatibility with Bare-Metal Exception Strategies

Some embedded toolchains disable exception support, which affects the use of `at()` (which throws exceptions). In exception-free environments, we recommend using only `operator[]` and employing boundary-checking tools during compilation or development.

------

## Advanced Topic: When Elements Are Not PODs

Elements of `std::array` can be any type `T`. However, common caveats in embedded systems include:

- If `T` has complex constructors/destructors, static initialization behavior (especially zero-initialization) will differ. You must ensure the construction cost is acceptable.
- For buffers that need to be read or written via DMA, `T` should be trivially copyable.

## Give It a Try — Using `std::array` for a Compile-Time CRC Table

```cpp
#include <array>
#include <cstdint>

// 简化的 CRC8 查表生成（constexpr）
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

// 编译期生成完整的 CRC 表
template<size_t N = 256>
constexpr std::array<uint8_t, N> generate_crc8_table() {
    std::array<uint8_t, N> table{};
    for (size_t i = 0; i < N; ++i) {
        table[i] = crc8_table_entry(static_cast<uint8_t>(i));
    }
    return table;
}

// 放入只读闪存
static constexpr auto crc_table = generate_crc8_table();

constexpr uint8_t compute_crc8(std::span<const uint8_t> data) {
    uint8_t crc = 0;
    for (uint8_t byte : data) {
        crc = crc_table[crc ^ byte];
    }
    return crc;
}
```

On supported toolchains, this approach allows us to place lookup table data into flash, saving RAM.
