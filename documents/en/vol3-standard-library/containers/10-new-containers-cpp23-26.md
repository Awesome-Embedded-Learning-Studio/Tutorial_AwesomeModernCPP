---
chapter: 7
cpp_standard:
- 23
- 26
description: 'We review the new members added to the container family in C++23/26:
  `flat_map` flattens red-black trees into sorted vectors (ordered and cache-friendly,
  but with O(n) insertion/deletion), `inplace_vector` offers fixed-capacity, heap-free
  allocation (C++26), and `mdspan` provides multi-dimensional views (C++23, with `submdspan`
  slicing in C++26), plus the `hive` proposal still in the pipeline.'
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
  source_hash: c3192046cdc4932a2a256eb7c71bea29e2a7bee2fc04c51c5674b9ba09d6c2ce
  translated_at: '2026-06-16T06:14:10.498720+00:00'
  engine: anthropic
  token_count: 1872
---
# New Standard Containers: flat_map, inplace_vector, and mdspan

## What this article covers: Closing long-standing gaps in C++23/26

Since the standard library's `container` family was established in C++98, it remained stable for over twenty years; the lineup of `vector`/`map`/`unordered_map` hardly changed. However, in practice, there have been several persistent gaps: can ordered associative containers ditch the red-black tree for contiguous storage to become cache-friendly? Can we bridge the gap between the fixed-size `array` and the heap-allocating `vector` with a middle ground that has a "known capacity cap, runtime variable size, and never touches the heap"? Can multidimensional data (matrices, images, voxels) get a non-owning multidimensional view similar to `span`? The C++23 and C++26 waves have filled these gaps exactly—this article covers `flat_map`/`flat_set`, `inplace_vector`, and `mdspan`, which are now standardized, and briefly mentions `hive`, which is still on the way.

A quick heads-up: these components are very new. `flat_map` and `mdspan` are from C++23 (requiring relatively recent libstdc++/libc++), and `inplace_vector` is from C++26. If your toolchain isn't up to date, the code won't compile. Understanding their design philosophy is more important than immediate usability—once you upgrade to a C++23/26 toolchain, these will be ready ammunition. All examples in this article have been tested on GCC 16.1.1 (libstdc++, `-std=c++23` / `-std=c++26`): `<flat_map>` and `<mdspan>` are available starting from GCC 15, while `<inplace_vector>` requires GCC 16.

## flat_map / flat_set: Flattening the red-black tree into a sorted vector (C++23)

Let's look at `std::flat_map` and `std::flat_set` (along with `flat_multimap`/`flat_multiset`, four in total). Their motivation is straightforward: as discussed in [Deep Dive into map and set](06-map-set-deep-dive.md), `map`/`set` are implemented using red-black trees underneath. Every element is a heap node linked by pointers, so lookups and traversals jump between nodes, resulting in poor cache hit rates. Although the complexity is O(log n), the constant factor is significantly degraded by cache unfriendliness. `flat_map` solves this by **flattening the entire tree into a sorted contiguous container** (the default underlying container is `std::vector`). Key-value pairs are arranged contiguously in memory, and lookups use binary search (O(log n)). However, thanks to contiguous memory, it is cache-friendly, and the practical constant factor is significantly lower than that of a red-black tree.

Interface-wise, `flat_map` is a **near drop-in replacement for `map`**—`insert`/`erase`/`find`/`operator[]`/range-based iteration are all present, and even ordered traversal works, making migration costs low. However, the trade-offs are clear, stemming entirely from the fact that "the underlying container is contiguous." First, **insertion and deletion are O(n)**: inserting an element into the middle of a sorted array requires shifting all subsequent elements back; deleting one requires shifting them forward. This contrasts sharply with the red-black tree's O(log n) insertion and deletion, so `flat_map` is best suited for scenarios where "lookups and traversals far outnumber insertions and deletions." Second, **iterators and references are unstable**: any insertion or deletion can trigger moves or even reallocation, just like in `vector`, invalidating all iterators—whereas `map` iterators never invalidate. In short, `flat_map` trades "expensive mutations + aggressive invalidation" for "faster constants on lookups and traversals." For small datasets with many reads and few writes, this is a good deal.

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

Next up is `std::inplace_vector<T, N>`, which is scheduled for C++26 (proposal P0843). It bridges the gap between `array` and `vector`: `array<T, N>` has a size fixed at compile time and cannot change, while `vector<T>` can resize but requires heap allocation (allocating a new block, copying, and freeing the old one during growth). Often, we need a container where the **capacity is known at compile time, the size is variable at runtime, but it never touches the heap**—this is exactly what `inplace_vector` does. Its elements are stored **directly within the object** (the object itself occupies `sizeof(T) * N` space, residing on the stack or in static storage). At runtime, we can add or remove elements between 0 and N without `new`, reallocation, or moving.

One of its most appealing properties is that **when `T` is trivially copyable, `inplace_vector<T, N>` itself is also trivially copyable**. This means we can `memcpy` the entire thing, store it in registers, or safely pass it to DMA—features critical for embedded and systems programming. It enjoys the same benefits of "contiguous memory + trivially copyable" discussed in the [Deep Dive into array](02-array.md), whereas `std::vector` cannot because it holds a heap pointer and is not trivially copyable. The behavior for exceeding capacity is also designed to be restrained: `push_back` throws `std::bad_alloc` if it exceeds `N` (degrading to `terminate` if exceptions are disabled). To avoid exceptions, we can use C++26's `try_push_back` or `try_emplace_back`, which return an error indicator instead of throwing when the limit is exceeded, making them ideal for `-fno-exceptions` environments.

```cpp
#include <cstdio>
#include <inplace_vector>

int main()
{
    std::inplace_vector<int, 4> v;     // 容量上限 4，绝不堆分配
    v.push_back(1);
    v.push_back(2);
    v.push_back(3);                     // size 现在 3，还能再塞一个
    std::printf("size = %zu, capacity = %zu\n", v.size(), v.capacity());
    // 再 push 到满：v.push_back(4) 成功；v.push_back(5) 超容量，抛 bad_alloc
    // 想避免异常用 try_push_back / try_emplace_back——超限不抛，返回失败指示
    return 0;
}
```

```bash
g++ -std=c++26 -O2 -o /tmp/ipv_demo /tmp/ipv_demo.cpp && /tmp/ipv_demo
```

```text
size = 3, capacity = 4
```

We need to clearly distinguish the boundaries between `inplace_vector` and `array`: the size of `array<T, N>` is always equal to N, making it fixed-length; `inplace_vector<T, N>` has a capacity limit of N, but its size is variable at runtime, ranging from zero to N. Use `array` for fixed lengths; use `inplace_vector` when you need a "known upper bound + runtime variability + no heap allocation".

## mdspan: The Multidimensional Version of `span` (C++23, Slicing in C++26)

The third feature is `std::mdspan`, which entered the standard in C++23 (proposal P0009). As discussed in [Deep Dive into span](08-span.md), `span` is a view over one-dimensional contiguous memory. However, real-world data is often two- or three-dimensional—matrices, images, voxel fields, and tensors. In the past, we had to use a raw one-dimensional pointer and manually calculate indices (e.g., `data[i * cols + j]`), which was ugly and prone to swapping rows and columns. `mdspan` wraps the concept of "a block of contiguous memory + a multidimensional shape" into a view type, allowing direct access via multidimensional indices like `m[i, j]`. It involves zero copying, does not own data, and simply describes "how to interpret this memory block as multidimensional".

It has four template parameters: the element type, `Extents` (the shape, i.e., the size of each dimension), `LayoutPolicy` (how to map multidimensional indices to a one-dimensional offset; defaults to `layout_right`, i.e., row-major, C/C++ style), and `Accessor` (how to read/write elements; defaults to raw access). The shape is described using `std::extents<IndexType, dims...>`. If a dimension size is known at compile time, fill in a constant; if it is only known at runtime, use `std::dynamic_extent`. If that feels too verbose, you can use `std::dextents<IndexType, Rank>`, which denotes "Rank dimensions, all dynamic". Access uses `m[i, j]` via **multidimensional bracket subscripting** (relying on the C++23 language feature P2128 for multidimensional `operator[]`), not the old `m[i][j]`—the latter implies returning a sub-view, whereas `mdspan` directly calculates the multidimensional index into a one-dimensional offset and returns a reference to the element. There is a common pitfall here: note that it uses square brackets `m[i, j]`, not function call syntax `m(i, j)`. Early `mdspan` reference implementations (like Kokkos) did use `operator()`, but after standardization in C++23, it was unified to multidimensional `operator[]`. This is why many older tutorials and blogs still write `m(i, j)`—copying that code will result in a compilation error.

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

A caveat worth mentioning: **`submdspan` (slicing) is C++26, not C++23**. When `mdspan` landed in C++23, the functionality for slicing rows, columns, and sub-blocks didn't make the cut and was moved to C++26 (P2630). So, if you want to grab a row in C++23, you still have to calculate the offset yourself. You'll need to wait for a C++26 toolchain to use zero-copy slicing like `std::submdspan(m, std::full_extent, slice)`. The greater significance of `mdspan` lies in it being the foundation for `std::linalg` (Linear Algebra Library)—in future standards, matrix computation APIs will be built on top of `mdspan`.

## Still on the Road: Proposals like hive

Finally, let's discuss something often mentioned but **not yet in the standard**: `std::hive` (from Matt Bentley's `plf::hive`, proposals P0909/P2826). It is a "node container" designed with stable element addresses (insertion/deletion doesn't affect other elements' addresses), fast erasure, and cache-friendly traversal (organizing nodes in blocks rather than a pure linked list). It fits scenarios where you need to "hold references to elements for a long time while frequently adding or removing them." As of C++26, it remains a proposal and has not been adopted—if you want to use it now, you have to resort to the third-party `plf::hive` library. We mention this here to indicate the direction: the standards committee is seriously considering "node containers better than `list`," but it is not yet a member of `std::`. Don't write "C++26's hive" in articles or resumes.

## Wrapping Up

This wave of new containers fills specific gaps: `flat_map` covers scenarios where you want "order and cache-friendliness" (at the cost of O(n) insertion/deletion and invalidation semantics similar to `vector`); `inplace_vector` covers the middle ground of "known capacity cap, runtime variable length, absolutely no heap allocation" (C++26, and its trivially copyable nature is great for embedded systems); `mdspan` provides a zero-copy view type for multi-dimensional data (C++23, but slicing with `submdspan` requires C++26). All three rely on relatively new toolchains—`flat_map` needs C++23 library support, and `inplace_vector` needs C++26—so check your compiler and standard library versions before deploying. The container storyline ends here—from `array` to the new standard containers, we've covered the tools for storing data; next, Volume 3 will shift to iterators and algorithms for "traversing and manipulating data."

Want to try it out right away? Open the online example below (runnable and viewable assembly):

<OnlineCompilerDemo
  title="New Standard Containers: flat_map / inplace_vector / mdspan"
  source-path="code/examples/vol3/10_new_containers.cpp"
  description="flat_map sorted vector lookup, inplace_vector fixed capacity no-heap, mdspan multi-dimensional subscript m[i,j] (C++26)"
  allow-run
  run-compiler="g162"
  run-options="-O2 -std=c++26"
/>

## References

- [std::flat_map — cppreference](https://en.cppreference.com/w/cpp/container/flat_map)
- [std::flat_set — cppreference](https://en.cppreference.com/w/cpp/container/flat_set)
- [std::inplace_vector (C++26) — cppreference](https://en.cppreference.com/w/cpp/container/inplace_vector)
- [std::mdspan — cppreference](https://en.cppreference.com/w/cpp/container/mdspan)
- [std::submdspan (C++26, P2630) — cppreference](https://en.cppreference.com/w/cpp/container/mdspan/submdspan)
- [Details of std::mdspan from C++23 — C++ Stories](https://www.cppstories.com/2025/cpp23_mdspan/)
- [plf::hive (Proposal Library Reference) — GitHub](https://github.com/mattreecebentley/plf_hive)
