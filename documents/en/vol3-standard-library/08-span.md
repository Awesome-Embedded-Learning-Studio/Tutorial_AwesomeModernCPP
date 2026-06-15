---
title: 'span: Non-owning Contiguous View'
description: 'Mastering `std::span`: a non-owning view of pointer plus length, memory
  differences between dynamic and static extent, unified acceptance of `array`/`vector`/C
  arrays, zero-copy slicing with `subspan`, byte views via `as_bytes`, and lifetime
  pitfalls of dangling views.'
chapter: 7
order: 8
tags:
- host
- cpp-modern
- intermediate
- span
- 容器
difficulty: intermediate
platform: host
cpp_standard:
- 17
- 20
reading_time_minutes: 14
related:
- array：编译期固定大小的聚合容器
- vector 深入：三指针、扩容与迭代器失效
translation:
  source: documents/vol3-standard-library/08-span.md
  source_hash: aa13cd106e6e9e1905111e31764926c9549f43cc5deca56a6f2e91837bb6a009
  translated_at: '2026-06-15T09:16:15.140682+00:00'
  engine: anthropic
  token_count: 1441
---
# span: A Non-owning Contiguous View

## What is span: A pointer plus a size, that's it

`std::span` is the standardized view introduced in C++20 for "a contiguous sequence of data." It does not own the memory; it only holds two things: a pointer and a size. It's just that simple—you can think of it as a "pointer with boundary information," or a formal wrapper for the C-style `pointer, length` parameter pair. It doesn't allocate, deallocate, or copy the underlying data. Copying a span just copies those two words (pointer and size), which is extremely cheap.

```cpp
// A span is just a pointer and a size
std::span<int> s1;     // Dynamic extent: size is stored at runtime
std::span<int, 5> s2;  // Static extent: size is fixed at compile time
```

Its core value lies in "passing arguments": when a function wants to accept "a sequence of T," using `std::span<T>` allows it to uniformly receive C arrays, `std::vector`, `std::array`, and `std::string` (via `data()`) from all contiguous sources. It avoids copying data and eliminates the need to turn the function into a template.

## Why we need it: The old headaches of pointer+length parameters

In C/C++, the old way to pass "a chunk of memory" to a function is `pointer, length`. This works, but it has many flaws: the unit of the `length` parameter (elements vs. bytes) relies on comments or guessing; whether the function modifies data depends on spotting `const` vs. non-`const`, which is easy to miss; passing the wrong length offers no compile-time protection; and these two parameters must be passed and remembered as a pair. `span` bundles the pointer and length into a single object. The type (`span<const T>` vs. `span<T>`) directly expresses read-only vs. read-write intent, and the length travels with the object, so it can't get lost.

```cpp
// Old way: error-prone and verbose
void process_data_old(int* ptr, size_t len); // Is len bytes or elements?

// Modern way: clear and type-safe
void process_data_modern(std::span<int> buffer); // Intent is explicit
```

This is also more convenient than writing templates—you don't need to instantiate a function for every container type, avoiding code bloat.

## Dynamic extent vs. static extent

`span` has two forms, differing in whether the "length is stored at runtime or fixed at compile time." `std::span<T>` (fully written `std::span<T, std::dynamic_extent>`) is a **dynamic extent**: the length is stored as a member and is determined at runtime. `std::span<T, N>` is a **static extent**: the length `N` is fixed at compile time and is not stored in the object.

This distinction is directly reflected in `sizeof`—we'll test this in a bit. Dynamic extent stores a pointer + size (two words), while static extent stores only the pointer (size is known at compile time, saving space). In daily use, dynamic extent is more common (since data length is often only known at runtime). Static extent is suitable for situations where "I know it's exactly N items," saving a word of storage and gaining some compile-time checks.

```cpp
void process_fixed(std::span<int, 4> buf); // Must be exactly 4 elements
void process_dynamic(std::span<int> buf); // Can be any size
```

## Accepting any contiguous source: array / vector / C array / pointer+length

`span`'s constructors cover almost all contiguous data sources, allowing function parameters using `std::span<T>` to unify everything:

```cpp
#include <span>
#include <array>
#include <vector>

void read_sensor_data(std::span<const uint8_t> data);

void demo() {
    // C array
    uint8_t c_arr[10] = {0};
    read_sensor_data(c_arr);

    // std::array
    std::array<uint8_t, 10> arr = {0};
    read_sensor_data(arr);

    // std::vector
    std::vector<uint8_t> vec(10);
    read_sensor_data(vec);

    // Pointer + length
    read_sensor_data({c_arr, 5});
}
```

The caller doesn't need to copy data, and the function doesn't need to write overloads or templates for every container type. Note that `span<const T>` represents a read-only view—if the function needs to modify data, use `std::span<T>` (non-const).

## subspan, first, last: Zero-copy slicing

`span` provides the `subspan`, `first`, and `last` toolkit. They return new `span` objects (still non-owning views) without copying any data. This is particularly handy for protocol parsing and buffer handling—splitting a large buffer into header/payload and passing them down as spans:

```cpp
void parse_packet(std::span<const uint8_t> buffer) {
    // Assume header is first 4 bytes
    auto header = buffer.first(4);
    // Payload is the rest
    auto payload = buffer.subspan(4);

    // Pass views down, no copies
    process_header(header);
    process_payload(payload);
}
```

Throughout this process, no bytes are copied; the sliced header and payload point to the interior of the original buffer.

## Byte views: as_bytes / as_writable_bytes

When handling binary data, we often need to treat a `span<T>` as raw bytes. `as_bytes` returns `span<const std::byte>`, and `as_writable_bytes` returns `span<std::byte>` (only available if T is non-const). This fits scenarios like CRC, serialization, and memory dumps where "treating a structure as a byte stream" is required:

```cpp
struct Header {
    uint16_t id;
    uint16_t len;
};

void serialize_header(std::span<const Header> h) {
    // View the struct as raw bytes for transmission
    auto byte_view = std::as_bytes(h);
    send_data(byte_view.data(), byte_view.size_bytes());
}
```

Distinguish between read-only and writable: use `as_bytes` for reading, and `as_writable_bytes` for modifying bytes in-place (and the underlying span must be non-const).

## Lifetime: span is non-owning, dangling references bite

The biggest pitfall of `span`, and the inevitable price of its "non-owning" nature, is that **it does not manage the lifetime of the underlying memory**. The span lives only as long as the underlying data; if the underlying data dies, the span becomes a dangling view, and accessing it is undefined behavior. The classic mistake is binding a span to a temporary object and returning it:

```cpp
// WRONG: Returning a span to a local temporary
std::span<int> get_bad_span() {
    std::vector<int> local = {1, 2, 3};
    return local; // local dies here, returned span is dangling
}
```

When the caller accesses this span, they are accessing freed memory. Remember this iron rule: **the lifetime of a span must not exceed the data it points to**. As long as you don't bind a span to a temporary or store it longer than the underlying data, it is safe.

## Let's run it: sizeof dynamic vs. static extent

Earlier we mentioned that dynamic extent stores two words and static extent stores only a pointer. Let's verify this:

```cpp
// code/examples/vol3/08_span_extent.cpp
#include <span>
#include <iostream>

int main() {
    std::cout << "sizeof(span<int>):         "
              << sizeof(std::span<int>) << '\n';
    std::cout << "sizeof(span<int, 5>):      "
              << sizeof(std::span<int, 5>) << '\n';
    return 0;
}
```

```text
sizeof(span<int>):         16
sizeof(span<int, 5>):      8
```

(On a 64-bit platform, GCC 16.1.1.) Dynamic extent is 16 bytes (one 8-byte pointer + one 8-byte size), while static extent is only 8 bytes (just a pointer; size is known at compile time, so it's omitted). This is the storage advantage of static extent—in scenarios where spans are passed frequently (like buffer views everywhere in embedded systems), saving half the space is meaningful.

## Extension: span in embedded systems (DMA / protocol parsing)

Because `span` is lightweight, zero-copy, and unified across containers, it is essentially the "modern buffer pointer" in embedded systems. Here are a few practical uses (side notes, use as needed). After a DMA callback places data into a fixed buffer, use `span` slicing to parse the header/payload without copying; read data from Flash into a buffer and use `span` to chunk it; pass small segments of data in interrupt/real-time paths, where copying a span is cheap (just two words). As long as you stick to the rule "span doesn't own, don't outlive the underlying data," it is a safe replacement for raw pointers.

## In closing: How to distinguish between span and string_view

Both `span` and `string_view` are "non-owning views." The distinction lies in the element type: `span` is generic for any element type (including writable, including `std::byte`), while `string_view` is specifically for character sequences (read-only, with string semantics). Use `span` for binary buffers/arbitrary data, and `string_view` for text. To remember `span` in one sentence: it's the formal wrapper for pointer plus length, unifying parameters and zero-copy slicing, but you must manage the lifetime yourself.

Want to try it out right now? Check out the online example below (runnable, with assembly view available):

<OnlineCompilerDemo
  title="span: A Non-owning Contiguous View"
  source-path="code/examples/vol3/08_span.cpp"
  description="Unified reception of C arrays/vectors/arrays, dynamic vs. static extent, subspan slicing"
  allow-run
/>

## Reference Resources

- [std::span — cppreference](https://en.cppreference.com/w/cpp/container/span)
- [std::byte — cppreference](https://en.cppreference.com/w/cpp/types/byte)
- [P0122 span proposal — open-std](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p0122r7.pdf)
