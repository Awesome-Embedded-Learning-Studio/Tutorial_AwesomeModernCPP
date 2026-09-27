---
chapter: 7
cpp_standard:
- 17
- 20
description: 'std::span laid bare: a non-owning view of one pointer plus one length,
  the sizeof difference between dynamic and static extents, uniformly accepting
  array/vector/C arrays, zero-copy subspan slicing, byte views via as_bytes, and
  the lifetime traps of dangling views'
difficulty: intermediate
order: 8
platform: host
reading_time_minutes: 7
related:
- 'array: An Aggregate Container with a Compile-Time Fixed Size'
- 'Deep Dive into vector: Three Pointers, Reallocation, and Iterator Invalidation'
tags:
- host
- cpp-modern
- intermediate
- span
- 容器
title: 'span: A Non-owning Contiguous View'
translation:
  source: documents/vol3-standard-library/containers/08-span.md
  source_hash: a47d4d2cce1ffad567eddb40f82d56fb2ee0c7a8fc99c9681b3bf988f7f99a3b
  translated_at: '2026-09-26T02:30:34+00:00'
  engine: anthropic
  token_count: 4000
---
# span: A Non-owning Contiguous View

## What span Is: A Pointer Plus a Length, Nothing More

`std::span` is C++20's standardized view over "a stretch of contiguous data". It does not own that memory; it holds exactly two things: a pointer and a length. It's that simple — you can think of it as "a pointer with boundary information attached", or as the official packaging of the C `(ptr, len)` parameter pair. It never allocates, frees, or copies the underlying data; copying a span copies just those two words (pointer and size), which is extremely cheap.

```cpp
std::vector<int> v = {1, 2, 3, 4};
std::span<int> s(v);       // s points to v's data, but does not own it
s.size();                  // 4
s[0];                      // 1
s.data() == v.data();      // true
```

Its core value is parameter passing: when a function wants to accept "a run of T data", `std::span<const T>` uniformly takes C arrays, `std::array`, `std::vector`, `(pointer, length)` pairs — every contiguous source — without copying the data and without writing the function as a template.

## Why We Need It: The Old Ailments of Pointer-Plus-Length Parameters

In C/C++, the old way to hand "a chunk of memory" to a function is `void f(T* ptr, std::size_t n)`. It runs, but the ailments pile up: whether the length `n` counts elements or bytes, you have to learn from a comment or guess; whether the function modifies the data hinges on `T*` versus `const T*`, which is easy to miss; a caller passing the wrong length gets no compile-time protection whatsoever; and the two parameters have to be passed as a pair and remembered as a pair. span packs the pointer and the length into one object; the type (`span<const T>` vs `span<T>`) states the read-only/read-write intent directly, and the length travels with the object — it cannot get lost.

```cpp
// The old way: the length unit and read-only-ness live in comments
void process_old(const uint8_t* buf, std::size_t n);

// The span way: the type is the semantics
void process(std::span<const uint8_t> buf);   // clear: read-only, length built in
void mutate(std::span<uint8_t> buf);          // clear: will modify, length built in
```

This is also less hassle than writing `template<class C> void process(const C& c)` — no per-container instantiation, so no compile-time bloat.

## Dynamic Extent and Static Extent

span comes in two shapes, differing in "is the length stored at run time or fixed at compile time". `std::span<T>` (fully written `std::span<T, std::dynamic_extent>`) is the **dynamic extent**: the length is stored as a member and can be anything at run time; `std::span<T, N>` is the **static extent**: the length `N` is nailed down at compile time and not stored in the object.

This difference shows up directly in `sizeof` — we'll run and see in a moment. Dynamic extent stores pointer + size (two words); static extent stores only the pointer (the size is known at compile time, so it is dropped). In daily use, dynamic extent is the more common one (data length is usually decided at run time); static extent suits the "I know it's exactly N" situations, saving one word of storage and buying a bit of compile-time checking.

```cpp
int arr[4];
std::span<int, 4> s_fixed(arr);     // can only bind data of length 4
std::span<int>    s_dyn(arr);       // any length, remembers 4 at run time
```

## Accepting Any Contiguous Source: array / vector / C Array / Pointer Plus Length

span's constructors cover essentially every source of contiguous data, which is what lets a `span` parameter rule them all:

```cpp
void print(std::span<const int> s);

int buf[] = {0x10, 0x20, 0x30};
std::array<int, 3> a = {1, 2, 3};
std::vector<int>   v = {4, 5, 6, 7};
int* p = v.data();

print(buf);                 // C array (N deduced automatically)
print(a);                   // std::array
print(v);                   // std::vector
print({p, 2});              // pointer + length
```

The caller copies no data, and inside the function you need neither per-container overloads nor templates. Note that `span<const T>` means a read-only view — if the function is going to modify the data, use `span<T>` (non-const).

## subspan, first, last: Zero-Copy Slicing

span offers the trio `subspan(offset, count)`, `first(n)`, `last(n)`; they return a new span (still a non-owning view) and copy no data at all. This is especially handy in protocol parsing and buffer handling — slice one big buffer into header / payload and pass each down as a span:

```cpp
void recv_packet(std::span<uint8_t> buffer)
{
    if (buffer.size() < 4) {
        return;
    }
    auto header  = buffer.first(4);          // view of the first 4 bytes
    uint16_t len = static_cast<uint16_t>(header[2] | (header[3] << 8));
    if (buffer.size() < 4 + len) {
        return;
    }
    auto payload = buffer.subspan(4, len);   // skip the header, take a payload view
    // payload is still a non-owning view, zero copies
}
```

Not a single byte is copied along the way; the sliced-out header / payload both point inside the original buffer.

## Byte Views: as_bytes / as_writable_bytes

When handling binary data, you often want to look at a `span<T>` as raw bytes. `std::as_bytes(s)` returns `span<const std::byte>`, and `std::as_writable_bytes(s)` returns `span<std::byte>` (available only when T is non-const). This fits CRC, serialization, memory dumps — all those "treat the struct as a byte stream" scenarios:

```cpp
std::span<int> data = /* ... */;
auto bytes = std::as_bytes(data);          // span<const std::byte>, read-only bytes
// crc(bytes.data(), bytes.size());
```

Keep read-only and writable straight: read with `as_bytes`; to modify bytes in place, use `as_writable_bytes` (and the underlying span must be non-const).

## Lifetime: span Does Not Own — Dangling Bites

span's biggest trap, and the inevitable price of its non-owning nature: **it does not manage the underlying memory's lifetime**. The span lives at most as long as the underlying data; once that is gone, the span is a dangling view, and accessing it is undefined behavior. The classic mistake is binding a span to a temporary and then returning it:

```cpp
std::span<int> bad()
{
    std::vector<int> v = {1, 2, 3};
    return v;   // v is destroyed when the function ends; the returned span dangles immediately
}
```

When the caller takes that span and accesses it, that is access to freed memory. Remember the iron rule: **a span's lifetime must not exceed the data it points to**. As long as you don't bind spans to temporaries or store them longer than the underlying data, they are safe.

## Run It: sizeof of Dynamic vs Static Extent

We said dynamic extent stores two words and static extent only the pointer — let's run it and see:

```cpp
#include <span>
#include <iostream>

int main()
{
    int arr[4] = {};
    std::span<int>        dyn;            // dynamic extent: default-constructible (empty span)
    std::span<int, 4>     fixed(arr);     // static extent: must bind to data
    std::cout << "sizeof(span<int>)    = " << sizeof(dyn) << '\n';
    std::cout << "sizeof(span<int,4>)  = " << sizeof(fixed) << '\n';
    std::cout << "sizeof(void*)        = " << sizeof(void*) << '\n';
    return 0;
}
```

```bash
g++ -std=c++20 -O2 -o /tmp/span_sizeof /tmp/span_sizeof.cpp && /tmp/span_sizeof
```

```text
sizeof(span<int>)    = 16
sizeof(span<int,4>)  = 8
sizeof(void*)        = 8
```

(64-bit platform, GCC 16.1.1.) Dynamic extent is 16 bytes (an 8-byte pointer + an 8-byte size); static extent is only 8 bytes (just a pointer — the size is known at compile time, so it is dropped). That is the storage advantage of static extent — in code that passes spans around heavily (buffer views, which embedded code is littered with), saving half the words matters.

## Beyond the Main Line: span in Embedded (DMA / Protocol Parsing)

Because span is lightweight, zero-copy, and uniform across containers, in embedded work it is practically "the modern buffer pointer"; here are a few field uses (beyond the main line, take them as needed). After a DMA callback drops data into a fixed buffer, slice it with spans to parse the header / payload, no copying required; read data from Flash into a buffer, then carve it up with spans; pass small chunks of data on interrupt / real-time paths, where copying a span is cheap (just two words). As long as you hold the line "span does not own, never outlives the underlying data", it is a safe replacement for bare pointers.

## A Few Closing Words: span vs string_view

span and string_view are both "non-owning views"; the dividing line is the element type: `span<T>` works for any element type (writable ones included, `std::byte` included), while `string_view` is dedicated to character sequences (read-only, with string semantics). Use span for binary buffers / arbitrary data, string_view for text. One sentence to remember span by: it is the official packaging of pointer-plus-length — unified parameter passing, zero-copy slicing — but you have to manage the lifetime yourself.

Want to get your hands on it right away? Open the online demo below (it runs, and you can view the assembly too):

<OnlineCompilerDemo
  title="span: A Non-owning Contiguous View"
  source-path="code/examples/vol3/08_span.cpp"
  description="Uniformly accepting C arrays/vector/array, dynamic and static extents, subspan slicing"
  allow-run
/>

## References

- [std::span — cppreference](https://en.cppreference.com/w/cpp/container/span)
- [std::byte — cppreference](https://en.cppreference.com/w/cpp/types/byte)
- [P0122 span proposal — open-std](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p0122r7.pdf)
