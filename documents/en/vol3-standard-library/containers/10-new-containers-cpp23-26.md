---
chapter: 7
cpp_standard:
- 23
- 26
description: 'Here is the translation of the description:


  We review the new members added to the container family in C++23/26: `flat_map`
  flattens the red-black tree into a sorted vector (ordered and cache-friendly, but
  with O(n) insertion and deletion), `inplace_vector` offers fixed-capacity storage
  without heap allocation (C++26), and `mdspan` provides a multidimensional view (C++23,
  with `submdspan` slicing in C++26), plus the `hive` proposal still in the pipeline.'
difficulty: intermediate
order: 10
platform: host
prerequisites:
- map 与 set 深入
- unordered_map 与 set 深入
- span：非拥有的连续视图
- array：编译期固定大小的聚合容器
reading_time_minutes: 10
related:
- 容器选择指南：按操作、内存与失效规则挑对容器
tags:
- host
- cpp-modern
- intermediate
- 容器
title: 'New Standard Containers: flat_map, inplace_vector, and mdspan'
translation:
  source: documents/vol3-standard-library/containers/10-new-containers-cpp23-26.md
  source_hash: 0fff6dd5a9ce85052d653533e029de14e090d8372a65806ba358355d25641b1a
  translated_at: '2026-06-24T00:36:38.363295+00:00'
  engine: anthropic
  token_count: 2031
---
# New Standard Containers: flat_map, inplace_vector, and mdspan

## What this article covers: Long-standing gaps filled by C++23/26

Since the standard library's `container` family was established in C++98, it remained stable for over twenty years; the lineup of `vector`, `map`, and `unordered_map` has barely changed. However, in practice, a few gaps have been constantly discussed: Can ordered associative containers ditch the red-black tree for contiguous storage to be more cache-friendly? Between fixed-size `array` and heap-allocating `vector`, can we have an intermediate state where "capacity is known, length is runtime-adjustable, but the heap is never touched"? For multidimensional data (matrices, images, voxels), can we have a non-owning multidimensional view like `span`? C++23 and C++26 happen to fill these gaps—this article covers `flat_map`/`flat_set`, `inplace_vector`, and `mdspan`, which have already been standardized, and briefly mentions `hive`, which is still on the way.

A quick heads-up: these components are very new. `flat_map` and `mdspan` are from C++23 (requiring relatively recent libstdc++/libc++), and `inplace_vector` is from C++26. If your toolchain isn't up to date, the code won't compile. Understanding their design philosophy is more important than immediate usability—once you upgrade to a C++23/26 toolchain, these will be ready ammunition. All examples in this article have been tested on GCC 16.1.1 (libstdc++, `-std=c++23` / `-std=c++26`): `<flat_map>` and `<mdspan>` are available starting from GCC 15, while `<inplace_vector>` requires GCC 16.

## flat_map / flat_set: Flattening the red-black tree into a sorted vector (C++23)

Let's look at `std::flat_map` and `std::flat_set` (along with `flat_multimap`/`flat_multiset`, four in total). Their motivation is straightforward: as discussed in [Deep Dive into map and set](06-map-set-deep-dive.md), `map`/`set` are implemented using red-black trees underneath. Every element is a heap node linked by pointers, so lookups and traversals jump between nodes, resulting in poor cache hit rates. Although the complexity is O(log n), the constant factor is heavily penalized by cache unfriendliness. `flat_map` solves this by **flattening the entire tree into a sorted, contiguous container** (defaulting to `std::vector`). Key-value pairs are arranged contiguously in memory, and lookups use binary search (O(log n)). However, thanks to contiguous memory, it is cache-friendly, resulting in a smaller constant factor than red-black trees.

In terms of interface, `flat_map` is a **near drop-in replacement for `map`**—`insert`, `erase`, `find`, `operator[]`, and range-based iteration are all present, and even ordered traversal works, making migration costs low. However, the trade-offs are clear, stemming entirely from the fact that "the underlying container is contiguous." First, **insertion and deletion are O(n)**: inserting an element into the middle of a sorted array requires shifting all subsequent elements back; deleting one requires shifting them forward. This stands in stark contrast to the O(log n) insertion/deletion of red-black trees, so `flat_map` is suitable for scenarios where "lookups and traversals far outnumber insertions and deletions." Second, **iterators and references are unstable**: any insertion or deletion can trigger moving or even reallocation, just like in `vector`, invalidating all iterators—whereas `map` iterators never invalidate. In short, `flat_map` trades "expensive mutations and aggressive invalidation" for "faster constant factors in lookup and traversal." When data volume is small and reads are much more frequent than writes, this is a good deal.

```cpp
#include <flat_map>
#include <print>
#include <string>

int main()
{
    std::flat_map<int, std::string> m;
    m.insert({3, "three"});
    m.insert({1, "one"});
    m.insert({2, "two"});          // O(n)：维护有序要搬移

    auto it = m.find(2);           // O(log n)：二分，连续内存 cache 友好
    std::println("find(2) = {}", it->second);

    m.erase(1);                    // O(n)：删了要往前挪
    // it 在这里已失效——和 vector 一样，别再用

    for (auto [k, v] : m) {        // 有序遍历：1 已删，剩下 2, 3
        std::println("{}: {}", k, v);
    }
    return 0;
}
```

## inplace_vector: Fixed-Capacity, Heapless Variable-Length Container (C++26)

Next is `std::inplace_vector<T, N>`, a feature standardized in C++26 (proposal P0843). It fills the gap between `array` and `vector`: `array<T, N>` has a size fixed at compile time and cannot change, while `vector<T>` can resize but requires heap allocation (allocating a new block, copying, and freeing the old block during expansion). Often, we need a container where the **capacity is known at compile time, the size is variable at runtime, but we never touch the heap**—this is exactly what `inplace_vector` does. Its elements are stored **directly within the object** (the object itself occupies `sizeof(T) * N` space, residing on the stack or in static storage). At runtime, we can add or remove elements between 0 and N without `new`, reallocation, or moving elements.

One of its most attractive properties is: **when `T` is trivially copyable, `inplace_vector<T, N>` itself is also trivially copyable**. This means it can be `memcpy`-ed as a whole, passed in registers, or safely handed off to DMA—features critical for embedded and systems programming. It enjoys the same benefits of "contiguous memory + trivially copyable" discussed in the [Deep Dive into array](02-array.md), whereas `std::vector` cannot because it holds a heap pointer and is not trivially copyable. The behavior when exceeding capacity is also designed to be restrained: `push_back` beyond `N` throws `std::bad_alloc` (degrading to terminate when exceptions are disabled). To avoid exceptions, we can use C++26's `try_push_back` or `try_emplace_back`. These do not throw when the limit is exceeded; instead, they return `std::optional<T&>`, where an empty value represents failure, making them ideal for `-fno-exceptions` environments.

```cpp
#include <cstdio>
#include <inplace_vector>
#include <type_traits>

int main()
{
    std::inplace_vector<int, 4> v;     // 容量上限 4，绝不堆分配
    static_assert(std::is_trivially_copyable_v<decltype(v)::value_type>); // 由于存储类型int是trivially copyable
    static_assert(std::is_trivially_copyable_v<decltype(v)>);             // 整个inplace_vector都会是trivially copyable

    v.push_back(1);
    v.push_back(2);
    v.push_back(3);
    v.push_back(4);    // size 现在 4，无法再塞了
    std::printf("size = %zu, max_size = %zu, capacity = %zu\n", v.size(), v.max_size(), v.capacity());
    // 此时如果 v.push_back(5) 超容量，抛 bad_alloc
    // 想避免异常用 try_push_back / try_emplace_back——超限不抛，返回std::optional<T&>, 空值来代表失败
    std::optional<int&> res = v.try_push_back(5);
    if (res.has_value()) {
        std::printf("successfully pushed the fifth element %d", res.value());
    } else {
        std::printf("failed to push the fifth element due to out of fixed capacity");
    }
    return 0;
}
```

```bash
g++ -std=c++26 -O2 -o /tmp/ipv_demo /tmp/ipv_demo.cpp && /tmp/ipv_demo
```

```text
size = 4, max_size = 4, capacity = 4
failed to push the fifth element due to out of fixed capacity
```

We must clearly distinguish the boundaries between `inplace_vector` and `array`: the size of `array<T, N>` is always N, making it fixed-length; `inplace_vector<T, N>` has a capacity limit of N, but its size is variable at runtime, ranging from zero to N. Use `array` for fixed lengths; use `inplace_vector` when you need a "known upper bound + runtime variability + no heap allocation".

## `mdspan`: The Multidimensional Version of `span` (C++23, Slicing in C++26)

The third feature is `std::mdspan`, which was standardized in C++23 (proposal P0009). We previously discussed in [Deep Dive into span](08-span.md) that `span` is a view over contiguous one-dimensional memory. However, real-world data is often multi-dimensional—matrices, images, voxel fields, and tensors. In the past, we had to use a raw one-dimensional pointer and manually calculate indices (e.g., `data[i * cols + j]`), which was ugly and prone to mixing up rows and columns. `mdspan` wraps "a contiguous block of memory + a multidimensional shape" into a view type, allowing direct access via multidimensional indices like `m[i, j]`. It involves zero copy, does not own data, and simply describes "how to interpret this memory as multidimensional".

It has four template parameters: the element type, `Extents` (the shape, i.e., the size of each dimension), `LayoutPolicy` (how to map multidimensional indices to a one-dimensional offset; defaults to `layout_right`, which is row-major/C/C++ style), and `Accessor` (how to read/write elements; defaults to raw access). The shape is described using `std::extents<IndexType, dims...>`. If a dimension size is known at compile time, fill in a constant; if it is only known at runtime, use `std::dynamic_extent`. For convenience, you can use `std::dextents<IndexType, Rank>`, which indicates "Rank dimensions, all dynamic". Access is performed using `m[i, j]` syntax, utilizing the **multidimensional subscript operator** (enabled by the C++23 language feature P2128), rather than the legacy `m[i][j]`. The latter suggests a return of a sub-view, but `mdspan` actually calculates the multidimensional index into a one-dimensional offset and returns a reference to the element. There is a common pitfall here: note that it uses square brackets `m[i, j]`, not function calls `m(i, j)`. Early `mdspan` reference implementations (like Kokkos) did use `operator()`, but after standardization in C++23, it was unified to the multidimensional `operator[]`. This is why many older tutorials and blogs still write `m(i, j)`—copying that code will result in a compilation error.

```cpp
#include <mdspan>
#include <cstdio>

int main()
{
    int raw[12] = {
        1, 2, 3, 4,
        5, 6, 7, 8,
        9, 10, 11, 12,
    };
    // 把 12 个 int 当成 3 行 4 列的二维视图，行优先
    std::mdspan<int, std::extents<size_t, 3, 4>> m(raw);

    std::printf("m[1,2] = %d\n", m[1, 2]);   // 第 1 行第 2 列 = 7
    std::printf("m[2,3] = %d\n", m[2, 3]);   // 第 2 行第 3 列 = 12

    // 维度运行期才知道：用 dextents
    std::mdspan<int, std::dextents<size_t, 2>> d(raw, 3, 4);
    std::printf("d[0,0] = %d, rank = %zu\n", d[0, 0], d.rank());
    return 0;
}
```

```bash
g++ -std=c++23 -O2 -o /tmp/mdspan_demo /tmp/mdspan_demo.cpp && /tmp/mdspan_demo
```

```text
m[1,2] = 7
m[2,3] = 12
d[0,0] = 1, rank = 2
```

Here is the translation of the provided Markdown content.

One notable caveat: **`submdspan` (slicing) is C++26, not C++23**. When `mdspan` landed in C++23, the functionality for slicing rows, columns, and sub-blocks didn't make the cut and was moved to C++26 (P2630). Therefore, if we want to extract a row in C++23, we still have to calculate the offset manually; we will need to wait for a C++26 toolchain to use zero-copy slicing like `std::submdspan(m, std::full_extent, slice)`. The greater significance of `mdspan` lies in its role as the foundation for `std::linalg` (Linear Algebra Library)—in future standards, matrix computation APIs will be built on top of `mdspan`.

## Still in the Pipeline: Proposals like hive

Finally, let's discuss `std::hive` (from Matt Bentley's `plf::hive`, proposals P0909/P2826), which is frequently mentioned but **has not yet entered the standard**. It is a "node-based container" designed with the goals of stable element addresses (insertion and deletion do not affect the addresses of other elements), fast erasure, and cache-friendly traversal (it organizes nodes in blocks rather than a pure linked list). It is suitable for scenarios where we need to "hold references to elements for a long time while frequently adding and removing them." As of C++26, it remains a proposal and has not been accepted—if we want to use it now, we must rely on the third-party `plf::hive` library. We mention this here to indicate the direction: the standards committee is seriously considering "node-based containers that are better than `list`," but it is not yet a member of `std::`, so avoid writing "C++26's hive" in articles or resumes.

## Wrapping Up

This wave of new containers fills specific niches: `flat_map` covers scenarios where we "want order and cache-friendliness" (at the cost of O(n) insertion/deletion and invalidation semantics similar to `vector`); `inplace_vector` covers the middle ground where "capacity is known at compile time, length varies at runtime, and heap allocation is strictly forbidden" (C++26, and its trivially copyable nature is very attractive for embedded systems); `mdspan` provides a zero-copy view type for multi-dimensional data (C++23, though slicing via `submdspan` requires C++26). All three rely on relatively new toolchains: `flat_map` requires C++23 library support, and `inplace_vector` requires C++26, so verify your compiler and standard library versions before deploying them. This concludes our coverage of the container main thread—from `array` to the new standard containers, we have fully covered the tools for storing data; in Vol. 3, we will shift focus to iterators and algorithms for "traversing and manipulating data."

Want to try it out right away? Open the online example below (you can run it and inspect the assembly):

<OnlineCompilerDemo
  title="New Standard Containers: flat_map / inplace_vector / mdspan"
  source-path="code/examples/vol3/10_new_containers.cpp"
  description="flat_map sorted vector lookup, inplace_vector fixed-capacity no-heap allocation, mdspan multi-dimensional subscript m[i,j] (C++26)"
  allow-run
  run-compiler="g162"
  run-options="-O2 -std=c++26"
/>

## Reference Resources

- [std::flat_map — cppreference](https://en.cppreference.com/w/cpp/container/flat_map)
- [std::flat_set — cppreference](https://en.cppreference.com/w/cpp/container/flat_set)
- [std::inplace_vector（C++26）— cppreference](https://en.cppreference.com/w/cpp/container/inplace_vector)
- [std::mdspan — cppreference](https://en.cppreference.com/w/cpp/container/mdspan)
- [std::submdspan（C++26，P2630）— cppreference](https://en.cppreference.com/w/cpp/container/mdspan/submdspan)
- [Details of std::mdspan from C++23 — C++ Stories](https://www.cppstories.com/2025/cpp23_mdspan/)
- [plf::hive（提案库参考）— GitHub](https://github.com/mattreecebentley/plf_hive)
