---
chapter: 7
cpp_standard:
- 11
- 14
- 17
- 20
description: C++20 array view
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 'Chapter 6: RAII与智能指针'
reading_time_minutes: 8
tags:
- cpp-modern
- host
- intermediate
title: std::span array view
---
# Embedded C++ Tutorial: std::span—A Lightweight, Non-owning Array View

Think of `std::span` as a "transparent conveyor belt" in C++: it doesn't own the cargo on top (memory), but calmly and efficiently tells you "how many elements are here and where they start." In embedded development, we often need to pass a chunk of memory to a function—without copying it, and without losing type or boundary information. `std::span` was born for exactly this scenario.

Or rather, it wasn't until C++20 that a standard view container finally appeared.

- `std::span<T>` is a **non-owning** view: it is not responsible for memory deallocation.
- It is typically a pointer plus a length (very lightweight, cheap to copy).
- Using `std::span<const T>` as a function parameter elegantly accepts multiple sources like `T[]`, `std::array`, `std::vector`, and raw pointers with a length.
- **Key warning**: Do not let a `span` outlive the underlying data — a dangling pointer will still bite you.

------

## Motivation: Why not just use a pointer or vector?

In embedded code, we often see function signatures like this:

```cpp
void process_buffer(uint8_t* buf, size_t n);

```

This approach is indeed flexible, but it has a downside: the reader has to remember the type of `buf`, whether the length unit is "number of elements" or "number of bytes", and whether the function modifies the data... There are simply too many places for things to go wrong. `std::span` makes these semantics explicit: the type and value (length) are in the same object, improving both readability and safety.

------

## Basic Usage

```cpp
#include <span>
#include <vector>
#include <array>
#include <iostream>

void print_bytes(std::span<const uint8_t> s) {
    for (auto b : s) std::cout << std::hex << int(b) << ' ';
    std::cout << std::dec << '\n';
}

int main() {
    uint8_t buffer[] = {0x10, 0x20, 0x30};
    std::vector<uint8_t> v = {1,2,3,4};
    std::array<uint8_t, 3> a = {9,8,7};

    print_bytes(buffer);             // 从内置数组构造
    print_bytes(v);                  // 从 vector 构造
    print_bytes(a);                  // 从 std::array 构造
    print_bytes({v.data(), 2});      // 从 pointer + size 构造
}

```

`print_bytes` uses `std::span<const uint8_t>` to receive input: it states that the content won't be modified, accepts multiple container sources, and requires the caller to copy no data.

------

## Dynamic vs. Static Extent

`std::span` has two forms:

- `std::span<T>` (or `std::span<T, std::dynamic_extent>`): runtime size;
- `std::span<T, N>`: compile-time fixed element count `N` (known as a static extent).

Example:

```cpp
int arr[4];
std::span<int, 4> s_fixed(arr);      // 只有长度为 4 的数组能绑定
std::span<int> s_dyn(arr, 4);        // 任意长度，运行时记录

```

A static `Extent` can enable extra compile-time checks or optimizations in certain scenarios, but in embedded development, a dynamic extent is more common (since buffer lengths are often determined at runtime).

------

## Useful Member Functions

```cpp
s.size();          // 元素个数
s.size_bytes();    // 字节数（注意！元素个数 * sizeof(T)）
s.data();          // 指向首元素的指针（可能为 nullptr 当 size()==0）
s.empty();
s.front(), s.back();
s[i];              // 下标，不做运行时检查（与 operator[] 语义一致）
s.subspan(offset, count);   // 切片，返回新的 span（仍为 non-owning）
s.first(n), s.last(n);     // 前 n 个或后 n 个元素视图
std::as_bytes(s);          // 将 span<T> 视为 span<const std::byte>
std::as_writable_bytes(s); // 视为 span<std::byte>（当 T 可写时）

```

Note: `operator[]` does not perform bounds checking; if you need boundary checks, use a `at`-like wrapper or add assertions during debugging.

------

## Advanced Example: Subspan and Byte Operations

```cpp
#include <span>
#include <cstddef> // for std::byte

void recv_packet(std::span<uint8_t> buffer) {
    if (buffer.size() < 4) return;
    auto header = buffer.first(4);
    uint16_t len = header[2] | (header[3] << 8);

    if (buffer.size() < 4 + len) return;
    auto payload = buffer.subspan(4, len);

    // 把 payload 当作字节流传给 CRC 函数
    auto bytes = std::as_bytes(payload);
    // crc_check(bytes.data(), bytes.size()); // 示例：调用检验函数
}

```

This pattern of slicing an overall buffer into header/payload is especially well-suited for embedded protocol parsing—it is concise and safe (as long as you ensure the incoming `buffer` is valid).

------

## Best Practices for Function Parameters

Designing an API to accept `std::span` has several benefits:

- The caller can pass an array, `std::array`, `std::vector`, or a raw pointer with a length;
- The function signature clearly expresses "this is a view (possibly read-only)";
- The function doesn't need template generics to support various containers.

Example:

```cpp
void process(std::span<const int> data); // 明确：不修改数据
void mutate(std::span<int> data);         // 明确：会修改数据

```

This is more intuitive than writing `template<class Container> void process(const Container& c)`, and it avoids unnecessary compile-time bloat.

------

## Common Pitfalls

1. **Dangling views**: The most common mistake. Do not bind a `std::span` to the `data()` of a local `std::vector` and return it to the caller:

   ```cpp
   std::span<int> bad() {
       std::vector<int> v = {1,2,3};
       return v; // ❌ v 被销毁，返回的 span 悬垂
   }

   ```

1. **Assuming ownership**: A span does not hold memory, and it will not destruct or release it. If you need ownership, use `std::vector`, `unique_ptr`, etc.

1. **Improper byte views**: `std::as_bytes` returns a `span<const std::byte>` for read-only byte access; use `as_writable_bytes` only when the underlying data is writable.

1. **Out-of-bounds access**: `operator[]` does not check boundaries. Perform explicit checks when necessary, or use debug assertions.

1. **Not a null-terminated string**: A `std::span<char>` is not a `C` string and does not guarantee termination with `'\0'`. For string handling, use `std::string_view` or process it with an explicit length.

------

## Comparison with `std::string_view`

- `std::string_view` is designed specifically for character sequences (read-only view) and carries string semantics (commonly used for text).
- `std::span<char>`/`std::span<std::byte>` are generic for any element type, including writable scenarios.
  When dealing with binary protocols/buffers, `std::span` is more appropriate; when handling immutable text, `string_view` is more semantic.

------

## Quick Embedded Scenario Examples

- A DMA callback places data into a fixed buffer, and the callback passes a `std::span` to the processing function without copying.
- Data is read from Flash into a buffer, and then `std::span` is used to slice and parse the header and blocks.
- When passing small chunks of data in an interrupt or real-time path, the copy overhead of `span` is extremely low.

------

## Code Tips

1. Write function parameters as `std::span<const T>` to express read-only intent.
2. If you want to accept a buffer of size N without changing the logic, you can accept a `std::span<T, N>` (static extent).
3. Use `subspan`, `first`, and `last` to construct subviews, rather than manually calculating pointer offsets.
4. Explicitly state in your public API documentation: **a span does not manage lifetimes**.

------

## Quick API Reference

`s` for `std::span<T>`:

- `s.size()`, `s.size_bytes()`, `s.data()`, `s.empty()`
- `s[i]` (no bounds checking), `s.front()`, `s.back()`
- `s.begin()`, `s.end()` (supports range-for)
- `s.subspan(offset, count)`, `s.first(n)`, `s.last(n)`
- `std::as_bytes(s)`, `std::as_writable_bytes(s)`
