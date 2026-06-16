---
chapter: 7
cpp_standard:
- 17
- 20
description: 'Mastering `std::span`: a non-owning view of pointer plus length, memory
  differences between dynamic and static extent, unified acceptance of `array`/`vector`/C
  arrays, zero-copy slicing with `subspan`, byte views via `as_bytes`, and the lifetime
  pitfalls of dangling views.'
difficulty: intermediate
order: 8
platform: host
reading_time_minutes: 7
related:
- array：编译期固定大小的聚合容器
- vector 深入：三指针、扩容与迭代器失效
tags:
- host
- cpp-modern
- intermediate
- span
- 容器
title: 'span: Non-owning Contiguous View'
translation:
  source: documents/vol3-standard-library/08-span.md
  source_hash: a47d4d2cce1ffad567eddb40f82d56fb2ee0c7a8fc99c9681b3bf988f7f99a3b
  translated_at: '2026-06-16T06:13:04.857832+00:00'
  engine: anthropic
  token_count: 1435
---
# span: A Non-owning Contiguous View

## What is span: A Pointer Plus a Size, That's It

`std::span` is the standard view for "a contiguous sequence of data" introduced in C++20. It does not own the underlying memory; it only holds two things: a pointer and a size. It's just that simple—you can think of it as a "pointer with boundary information," or a formal wrapper for the C-style `(ptr, len)` parameter pair. It allocates nothing, frees nothing, and does not copy the underlying data. Copying a span just copies those two words (the pointer and the size), which is extremely cheap.

```cpp
std::vector<int> v = {1, 2, 3, 4};
std::span<int> s(v);       // s 指向 v 的数据，但不拥有
s.size();                  // 4
s[0];                      // 1
s.data() == v.data();      // true
```

Its core value lies in **parameter passing**: when a function needs to accept "a range of `T` data," using `std::span<const T>` allows it to uniformly accept C arrays, `std::array`, `std::vector`, and `(pointer, length)` pairs from any contiguous source. This approach avoids copying data and eliminates the need to turn the function into a template.

## Why we need it: The drawbacks of pointer-plus-length parameters

In C/C++, the traditional way to pass "a chunk of memory" to a function is `void f(T* ptr, std::size_t n)`. While this works, it has several drawbacks: whether the length `n` refers to elements or bytes relies on comments or guesswork; whether the function modifies data depends on spotting `T*` versus `const T*`, which is easy to miss; there is no compile-time protection if the caller passes the wrong length; and these two parameters must be passed and remembered together. `span` bundles the pointer and length into a single object. The type (`span<const T>` vs `span<T>`) directly expresses read-only or write-only intent, and the length stays with the object, so it cannot be lost.

```cpp
// 老办法：长度单位、只读与否全靠注释
void process_old(const uint8_t* buf, std::size_t n);

// span 办法：类型即语义
void process(std::span<const uint8_t> buf);   // 明确：只读，长度内建
void mutate(std::span<uint8_t> buf);          // 明确：会改，长度内建
```

This is also less hassle than writing `template<class C> void process(const C& c)`—we don't instantiate a version for every container, which avoids code bloat.

## Dynamic extent vs. static extent

`span` has two forms, distinguished by whether the length is stored at runtime or fixed at compile time. `std::span<T>` (fully written as `std::span<T, std::dynamic_extent>`) is a **dynamic extent**: the length is stored as a member and is determined at runtime. `std::span<T, N>` is a **static extent**: the length `N` is fixed at compile time and is not stored in the object.

This difference is directly reflected in `sizeof`—we'll test this in a moment. Dynamic extent stores a pointer plus a size (two words), while static extent only stores a pointer (the size is known at compile time, so it's omitted). In practice, dynamic extent is more common (since data length is often only known at runtime), while static extent is suitable for situations where "we know it's exactly N items," saving one word of storage and gaining some compile-time checks.

```cpp
int arr[4];
std::span<int, 4> s_fixed(arr);     // 只能绑长度 4 的数据
std::span<int>    s_dyn(arr);       // 任意长度，运行时记 4
```

## Accepting any contiguous source: array / vector / C array / pointer + length

`span` constructors cover almost all contiguous data sources, allowing us to unify function parameters using `span`:

```cpp
void print(std::span<const int> s);

int buf[] = {0x10, 0x20, 0x30};
std::array<int, 3> a = {1, 2, 3};
std::vector<int>   v = {4, 5, 6, 7};
int* p = v.data();

print(buf);                 // C 数组（自动推 N）
print(a);                   // std::array
print(v);                   // std::vector
print({p, 2});              // 指针 + 长度
```

The caller does not need to copy data, and the function does not need to write overloads or templates for every container type. Note that `span<const T>` represents a read-only view—if the function needs to modify data, use `span<T>` (non-const).

## subspan, first, last: Zero-Copy Slicing

`span` provides a trio of utilities: `subspan(offset, count)`, `first(n)`, and `last(n)`. These return a new `span` (still a non-owning view) without copying any data. This is particularly handy for protocol parsing and buffer handling—splitting a large buffer into header and payload, and passing them along as `span`s:

```cpp
void recv_packet(std::span<uint8_t> buffer)
{
    if (buffer.size() < 4) {
        return;
    }
    auto header  = buffer.first(4);          // 前 4 字节视图
    uint16_t len = static_cast<uint16_t>(header[2] | (header[3] << 8));
    if (buffer.size() < 4 + len) {
        return;
    }
    auto payload = buffer.subspan(4, len);   // 跳过 header 取 payload 视图
    // payload 仍是非拥有视图，零拷贝
}
```

Throughout this process, no bytes are copied; the sliced header and payload point directly into the original buffer.

## Byte View: as_bytes / as_writable_bytes

When handling binary data, we often need to treat a `span<T>` as raw bytes. `std::as_bytes(s)` returns a `span<const std::byte>`, while `std::as_writable_bytes(s)` returns a `span<std::byte>` (available only when `T` is non-const). This is ideal for scenarios like CRC calculation, serialization, and memory dumps, where we need to "treat a structure as a byte stream":

```cpp
std::span<int> data = /* ... */;
auto bytes = std::as_bytes(data);          // span<const std::byte>，只读字节
// crc(bytes.data(), bytes.size());
```

Be careful to distinguish between read-only and writable data: use `as_bytes` for reading, and use `as_writable_bytes` for modifying bytes in place (and the underlying span must be non-const).

## Lifetime: span is non-owning, so dangling references will bite

The biggest pitfall of `span`, and the inevitable cost of its "non-owning" nature, is that **it does not manage the lifetime of the underlying memory**. The `span` can only live as long as the underlying data; once the underlying data is gone, the `span` becomes a dangling view, and accessing it results in undefined behavior. The classic mistake is binding a `span` to a temporary object and then returning it:

```cpp
std::span<int> bad()
{
    std::vector<int> v = {1, 2, 3};
    return v;   // v 在函数结束时销毁，返回的 span 立刻悬垂
}
```

If the caller accesses this span, they are accessing freed memory. Remember this golden rule: **the lifetime of a span must not exceed the data it points to**. As long as we don't bind a span to a temporary or store it longer than the underlying data, it is safe.

## Let's Run It: sizeof for Dynamic vs. Static Extents

We mentioned earlier that a dynamic extent stores two words, while a static extent stores only a pointer. Let's verify this:

```cpp
#include <span>
#include <iostream>

int main()
{
    int arr[4] = {};
    std::span<int>        dyn;            // 动态 extent：可默认构造（空 span）
    std::span<int, 4>     fixed(arr);     // 静态 extent：必须绑定数据
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

(On 64-bit platforms with GCC 16.1.1.) A dynamic extent is 16 bytes (one 8-byte pointer plus one 8-byte size), while a static extent is only 8 bytes (just a pointer, as the size is known at compile time and omitted). This represents the storage advantage of static extent—in scenarios where we pass spans extensively (such as buffer views common in embedded systems), saving half the bytes is significant.

## Extension: span in Embedded Systems (DMA / Protocol Parsing)

Because `span` is lightweight, zero-copy, and consistent across containers, it is essentially the "modern buffer pointer" in embedded development. Here are a few practical usage patterns (supplementary to the main thread, use as needed). After a DMA callback places data into a fixed buffer, we can use `span` slicing to parse headers and payloads without copying; when reading data from Flash into a buffer, we can use `span` to chunk the processing; in interrupt or real-time paths passing small data segments, copying a `span` is cheap (just two words). As long as we adhere to the rule that "span does not own the data and must not outlive the underlying lifetime," it serves as a safe alternative to raw pointers.

## Wrapping Up: Differentiating span and string_view

Both `span` and `string_view` are "non-owning views," and the distinction depends on the element type: `span<T>` is generic for any element type (including writable ones and `std::byte`), while `string_view` is specialized for character sequences (read-only, with string semantics). We use `span` for binary buffers or arbitrary data, and `string_view` for text. To remember `span` in a nutshell: it is the formal encapsulation of a pointer plus a length, offering unified parameter passing and zero-copy slicing, but we must manage the object lifetimes ourselves.

Want to try it out immediately and see the results? Open the online example below (you can run it and view the assembly):

<OnlineCompilerDemo
  title="span: Non-owning Contiguous View"
  source-path="code/examples/vol3/08_span.cpp"
  description="Unified acceptance of C arrays/vectors/arrays, dynamic and static extent, subspan slicing"
  allow-run
/>

## Reference Resources

- [std::span — cppreference](https://en.cppreference.com/w/cpp/container/span)
- [std::byte — cppreference](https://en.cppreference.com/w/cpp/types/byte)
- [P0122 span Proposal — open-std](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p0122r7.pdf)
