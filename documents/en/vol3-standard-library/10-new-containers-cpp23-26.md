---
chapter: 7
cpp_standard:
- 23
- 26
description: 'A review of the new members added to the containers family in C++23/26:
  `flat_map` flattens the red-black tree into a sorted `vector` (ordered and cache-friendly,
  but O(n) insertion/deletion), `inplace_vector` is a fixed-capacity container without
  heap allocation (C++26), `mdspan` provides a multidimensional view (C++23, with
  `submdspan` slicing in C++26), and the `hive` proposal is still in progress.'
difficulty: intermediate
order: 10
platform: host
prerequisites:
- map 与 set 深入
- unordered_map 与 set 深入
- span：非拥有的连续视图
- array：编译期固定大小的聚合容器
reading_time_minutes: 9
related:
- 容器选择指南：按操作、内存与失效规则挑对容器
tags:
- host
- cpp-modern
- intermediate
- 容器
title: 'New Standard Containers: flat_map, inplace_vector, and mdspan'
translation:
  engine: anthropic
  source: documents/vol3-standard-library/10-new-containers-cpp23-26.md
  source_hash: 4523da607c36be4c2dea1098f2d4dfdc971c898009bca41835d083bfb92bd015
  token_count: 1880
  translated_at: '2026-06-15T09:18:18.938018+00:00'
---
# New Standard Containers: flat_map, inplace_vector, and mdspan

## What this article covers: Long-standing gaps filled by C++23/26

The standard library's `std::vector` family has remained stable for over twenty years since C++98, and the suite of `std::map`/`std::set`/`std::unordered_map` has barely changed. However, practical development has several long-standing gaps: Can ordered associative containers ditch the red-black tree for contiguous storage to be cache-friendly? Between fixed-size `std::array` and heap-allocating `std::vector`, can we have a middle ground where the capacity is known at compile time, the length is variable at runtime, and it never touches the heap? For multidimensional data (matrices, images, voxels), can we get a non-owning multidimensional view like `std::span`? C++23 and C++26 have filled these gaps—this article covers `std::flat_map`/`std::flat_set`, `std::inplace_vector`, and `std::mdspan`, which have already been standardized, with a brief mention of `std::hive`, which is still on the way.

A quick heads-up: these components are very new. `std::flat_map` and `std::mdspan` are from C++23 (requiring relatively recent libstdc++/libc++), and `std::inplace_vector` is from C++26. If your toolchain isn't up to date, they won't compile. Understanding their design philosophy is more important than immediate usability—once you upgrade to a C++23/26 toolchain, these will be ready-to-use ammunition. All examples in this article have been tested on GCC 16.1.1 (libstdc++, C++23 / C++26): `flat_map` and `mdspan` have been available since GCC 15, while `inplace_vector` requires GCC 16.

## flat_map / flat_set: Flattening the red-black tree into a sorted vector (C++23)

First, let's look at `std::flat_map` and `std::flat_set` (along with `std::flat_multimap`/`std::flat_multiset`, totaling four). Their motivation is straightforward: as discussed in [Deep Dive into map and set](06-map-set-deep-dive.md), `std::map`/`std::set` are implemented as red-black trees underneath. Every element is a heap node, linked by pointers. Lookups and traversals jump between nodes, resulting in poor cache hit rates. Although the complexity is O(log n), the constant factor is heavily impacted by cache unfriendliness. `std::flat_map` solves this by **flattening the entire tree into a sorted contiguous container** (defaulting to `std::vector`), where key-value pairs are arranged adjacently in memory. Lookups use binary search (O(log n)), but thanks to contiguous memory, it is cache-friendly, resulting in a smaller constant factor than red-black trees.

Interface-wise, `std::flat_map` is a **near drop-in replacement for `std::map`**—`operator[]`, `at`, `count`, `find`, and range iteration are all present. Even ordered traversal works, making migration costs low. However, the trade-offs are clear, stemming entirely from the fact that "the underlying container is contiguous." First, **insertion and deletion are O(n)**: inserting an element into the middle of a sorted array requires shifting all subsequent elements; deleting one requires shifting them forward. This contrasts sharply with the O(log n) insertion/deletion of red-black trees, so `flat_map` is suitable for scenarios where "lookups and traversals far outnumber insertions and deletions." Second, **iterators and references are unstable**: any insertion or deletion might trigger moving or even reallocation, just like `std::vector`, invalidating all iterators—whereas `std::map`'s iterators never invalidate. In short, `flat_map` trades "expensive mutations + aggressive invalidation" for "faster constant factors in lookup and traversal." When data volume is small and reads outnumber writes, this trade-off is worth it.

```text
[Diagram: flat_map vs map structure comparison]
```

## inplace_vector: Fixed-capacity, heap-avoiding variable-length container (C++26)

The second is `std::inplace_vector`, which entered the standard in C++26 (proposal P0843). It fills the gap between `std::array` and `std::vector`: `std::array` has a size fixed at compile time and cannot change; `std::vector` can change size but requires heap allocation (allocating a new block, copying, and freeing the old one during expansion). Often, what you need is "capacity known at compile time, variable size at runtime, but absolutely no heap touching"—`std::inplace_vector` does exactly this. Its elements are stored **directly inside the object** (the object itself occupies the space of `N` elements, placed on the stack or in static storage). At runtime, you can add or remove elements between 0 and N, without `new`, without reallocation, and without copying or moving.

Its most appealing property is: **when `T` is trivially copyable, `std::inplace_vector` itself is also trivially copyable**. This means it can be `memcpy`'d as a whole, stored in registers, or safely handed to DMA—features critical for embedded and systems programming. As discussed in [Deep Dive into array](02-array.md), `inplace_vector` enjoys the same benefits of "contiguous memory + trivially copyable," whereas `std::vector` cannot because it holds a heap pointer and is not trivially copyable. Behavior when exceeding capacity is also designed to be restrained: `push_back` exceeding N throws `std::bad_alloc` (or degrades to `terminate` if exceptions are disabled). To avoid exceptions, you can use C++26's `try_push_back`/`try_emplace_back`, which return an error indicator instead of throwing, making them suitable for freestanding environments.

```text
[Diagram: inplace_vector memory layout]
```

```text
[Diagram: inplace_vector vs array vs vector comparison]
```

```text
[Diagram: inplace_vector trivially copyable property]
```

The boundary between `std::array` and `std::inplace_vector` needs to be clear: `std::array`'s size is always N (fixed length); `std::inplace_vector`'s capacity is capped at N, but its size is variable at runtime between 0 and N. Use `array` for fixed length; use `inplace_vector` for "known upper bound + runtime variable + no heap allocation."

## mdspan: The multidimensional version of span (C++23, slicing in C++26)

The third is `std::mdspan`, standardized in C++23 (proposal P0009). As discussed in [Deep Dive into span](08-span.md), `std::span` is a one-dimensional contiguous memory view, but reality is full of 2D and 3D data—matrices, images, voxel fields, tensors. In the past, we had to use a raw 1D pointer and manually calculate subscripts (`data[y * width + x]`), which was ugly and prone to mixing up rows and columns. `std::mdspan` wraps "a contiguous block of memory + a multidimensional shape" into a view type, allowing direct access using multidimensional subscripts `m[i, j]`. It involves zero copying, holds no data, and only describes "how to interpret this memory as multidimensional."

It has four template parameters: element type, `Extents` (shape, the size of each dimension), `LayoutPolicy` (how to map multidimensional subscripts to a 1D offset, default `layout_right` i.e., row-major, C/C++ style), and `AccessorPolicy` (how to read/write elements, default direct access). Shape is described by `std::extents`, where compile-time known dimension sizes are filled with constants, and runtime-known ones use `std::dextent`; if that's too much hassle, you can use `std::dextents`, meaning "all Rank dimensions are dynamic." Access uses the **multidimensional bracket subscript** `m[i, j]` (relying on the C++23 multidimensional `operator[]` language feature P2128), not the old `operator()`—the latter might imply returning a sub-view, whereas `mdspan` directly calculates the multidimensional index into a 1D offset and returns a reference to the element. A common pitfall: note that it uses square brackets `[]`, not function calls `()`; early `mdspan` reference implementations (like Kokkos) did use `()`, but the C++23 standard unified it to multidimensional `[]`. This is why many older tutorials and blogs still write `()`—copying them will result in compilation errors.

```text
[Diagram: mdspan concept and usage]
```

```text
[Diagram: mdspan Extents and Layout]
```

```text
[Diagram: mdspan multidimensional subscript access]
```

A pitfall worth mentioning: **`submdspan` (slicing) is C++26, not C++23**. When `mdspan` landed in C++23, the functionality to slice rows, columns, or sub-blocks didn't make it and was moved to C++26 (P2630). So, if you want to extract a row in C++23, you still have to calculate the offset manually; you'll need to wait for a C++26 toolchain to use zero-copy slicing like `submdspan`. The greater significance of `mdspan` lies in it being the foundation for `std::linalg` (linear algebra library)—in future standards, matrix operation APIs will be built on top of `mdspan`.

## Still on the way: hive and other proposals

Finally, a mention of something often discussed but **not yet in the standard**: `std::hive` (from Matt Bentley's `plf::hive`, proposals P0909/P2826). It is a "node container" designed for stable element addresses (insertions/deletions don't affect other elements' addresses), fast erasure, and cache-friendly traversal (organizing nodes in blocks rather than pure linked lists). It fits scenarios where "you need to hold references to elements for a long time and also frequently insert/delete." As of C++26, it is still a proposal and has not been adopted—if you want to use it now, you must resort to the third-party `plf::hive` library. I mention this here to indicate the direction: the standards committee is seriously considering "node containers better than list," but it is not yet a member of the `std::` family, so don't write "C++26's hive" in articles or resumes.

## Wrapping up

This wave of new containers fills specific gaps: `std::flat_map` is for scenarios wanting "ordered + cache-friendly" (cost is O(n) mutations and vector-like invalidation); `std::inplace_vector` fills the middle ground of "known capacity cap + runtime variable length + absolutely no heap allocation" (C++26, trivially copyable properties are sweet for embedded); `std::mdspan` provides a zero-copy view type for multidimensional data (C++23, slicing `submdspan` waits for C++26). All three rely on relatively new toolchains; `flat_map` needs C++23 library support, and `inplace_vector` needs C++26, so verify your compiler and standard library versions before deploying. The container thread ends here—from `std::vector` to new standard containers, we've covered the tools for storing data; next, Vol. 3 will turn to iterators and algorithms for "traversing and manipulating data."

Want to try running these examples directly? Click the online demo below (you can run them and view the assembly):

<OnlineCompilerDemo
  title="New Standard Containers: flat_map / inplace_vector / mdspan"
  source-path="code/examples/vol3/10_new_containers.cpp"
  description="flat_map sorted vector lookup, inplace_vector fixed-capacity no-heap, mdspan multidimensional subscript m[i,j] (C++26)"
  allow-run
  run-compiler="g162"
  run-options="-O2 -std=c++26"
/>

## Reference Resources

- [std::flat_map — cppreference](https://en.cppreference.com/w/cpp/container/flat_map)
- [std::flat_set — cppreference](https://en.cppreference.com/w/cpp/container/flat_set)
- [std::inplace_vector (C++26) — cppreference](https://en.cppreference.com/w/cpp/container/inplace_vector)
- [std::mdspan — cppreference](https://en.cppreference.com/w/cpp/container/mdspan)
- [std::submdspan (C++26, P2630) — cppreference](https://en.cppreference.com/w/cpp/container/mdspan/submdspan)
- [Details of std::mdspan from C++23 — C++ Stories](https://www.cppstories.com/2025/cpp23_mdspan/)
- [plf::hive (Proposal library reference) — GitHub](https://github.com/mattreecebentley/plf_hive)
