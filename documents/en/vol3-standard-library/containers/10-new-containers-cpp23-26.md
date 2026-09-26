---
chapter: 7
cpp_standard:
- 23
- 26
description: 'A tour through the new members C++23/26 added to the container family:
  flat_map flattens the red-black tree into a sorted vector (ordered and cache-friendly,
  but with O(n) insert/erase), inplace_vector is fixed-capacity and heap-free (C++26),
  mdspan is a multidimensional view (C++23, with submdspan slicing arriving in C++26),
  plus the still-on-the-road hive proposal.'
difficulty: intermediate
order: 10
platform: host
prerequisites:
- 'Deep Dive into map and set: Red-Black Trees, Heterogeneous Lookup, and Node Handles'
- 'Deep Dive into unordered_map and unordered_set: Hash Tables, Buckets, and Custom Hashing'
- 'span: A Non-owning Contiguous View'
- 'array: An Aggregate Container with a Compile-Time Fixed Size'
reading_time_minutes: 10
related:
- 'Container Selection Guide: Picking the Right One by Operations, Memory, and Invalidation Rules'
tags:
- host
- cpp-modern
- intermediate
- 容器
title: 'New Standard Containers: flat_map, inplace_vector, and mdspan'
translation:
  source: documents/vol3-standard-library/containers/10-new-containers-cpp23-26.md
  source_hash: 0fff6dd5a9ce85052d653533e029de14e090d8372a65806ba358355d25641b1a
  translated_at: '2026-09-26T02:31:26+00:00'
  engine: anthropic
  token_count: 5000
---
# New Standard Containers: flat_map, inplace_vector, and mdspan

## What This Article Covers: The Long-Standing Gaps C++23/26 Filled

The standard library's `container` family was settled back in C++98 and then stayed put for over twenty years — `vector`/`map`/`unordered_map` have barely moved since. But a few gaps keep coming up in practice: could the ordered associative containers drop the red-black tree and switch to contiguous storage in exchange for cache friendliness? Between fixed-length `array` and heap-allocating `vector`, could there be a middle ground with "a known capacity ceiling, a runtime-variable length, and a firm promise never to touch the heap"? Could multidimensional data (matrices, images, voxels) get a non-owning multidimensional view like `span`? The C++23 and C++26 waves filled exactly these holes — this article covers the three that are already standardized, `flat_map`/`flat_set`, `inplace_vector`, and `mdspan`, and briefly mentions `hive`, which is still on the way.

A heads-up before we start: these components are all very new. `flat_map` and `mdspan` are C++23 (they need a fairly recent libstdc++/libc++), and `inplace_vector` is C++26 — if your toolchain lags behind, the code won't compile. Understanding their design matters more than being able to use them right away; once you move up to a C++23/26 toolchain, these are ready-made ammunition. Every example in this article was actually run on GCC 16.1.1 (libstdc++, `-std=c++23` / `-std=c++26`): `<flat_map>` and `<mdspan>` are available from GCC 15 onward, while `<inplace_vector>` needs GCC 16.

## flat_map / flat_set: Flattening the Red-Black Tree into a Sorted Vector (C++23)

First up: `std::flat_map` and `std::flat_set` (four in total, counting `flat_multimap`/`flat_multiset`). Their motivation is direct: as [Deep Dive into map and set](06-map-set-deep-dive.md) covered, `map`/`set` sit on a red-black tree underneath — every element is its own heap node, the nodes are strung together by pointers, and both lookup and traversal hop from node to node, so cache hit rates suffer. The complexity is O(log n), but cache unfriendliness eats a big chunk out of the constant factor. `flat_map`'s approach is to **flatten the whole tree into one sorted contiguous container** (by default the underlying container is simply `std::vector`): the key-value pairs sit packed next to each other in memory, lookup is a binary search (O(log n)), but because the memory is contiguous and cache-friendly, the real-world constant comes out noticeably smaller than the red-black tree's.

Interface-wise, `flat_map` is a **near drop-in replacement for `map`** — `insert`/`erase`/`find`/`operator[]`/range iteration are all there, even ordered traversal works, so migration cost is low. But the price is just as clear, and it all traces back to one fact: "the underlying container is contiguous". First, **insertion and erasure are O(n)**: to wedge an element into the middle of a sorted array, everything after it has to shift back; erasing one shifts them forward again. Contrast that with the red-black tree's O(log n) insert/erase, and you see why flat_map fits scenarios where "lookups and traversals vastly outnumber mutations". Second, **iterators and references are unstable**: any insert or erase can trigger shifting or even reallocation, just like `vector`, invalidating every iterator — whereas `map`'s iterators never invalidate. In one sentence: flat_map trades "pricier mutations plus nastier invalidation" for "a faster constant on lookup and traversal". When the data volume is small and reads far outnumber writes, that is a good deal.

```cpp
#include <flat_map>
#include <print>
#include <string>

int main()
{
    std::flat_map<int, std::string> m;
    m.insert({3, "three"});
    m.insert({1, "one"});
    m.insert({2, "two"});          // O(n): keeping the order requires shifting

    auto it = m.find(2);           // O(log n): binary search, contiguous memory is cache-friendly
    std::println("find(2) = {}", it->second);

    m.erase(1);                    // O(n): erasing shifts elements forward
    // it is invalidated here — like vector, don't use it anymore

    for (auto [k, v] : m) {        // ordered traversal: 1 is erased, 2 and 3 remain
        std::println("{}: {}", k, v);
    }
    return 0;
}
```

## inplace_vector: A Fixed-Capacity, Heap-Free Variable-Length Container (C++26)

Second: `std::inplace_vector<T, N>`, standardized in C++26 (proposal P0843). It fills the gap between `array` and `vector`: `array<T, N>` has its size nailed down at compile time and can never change; `vector<T>` can grow but needs the heap (on expansion it news a new block, copies, and frees the old one). A lot of the time what you actually want is "a capacity ceiling known at compile time, a size that varies at runtime, but never a single touch of the heap" — that is exactly `inplace_vector`'s job. Its elements live **directly inside the object itself** (the object occupies that `sizeof(T) * N` block of space, placed on the stack or in static storage), and at runtime it can grow and shrink between 0 and N — no new, no reallocation, no copy-and-move.

Its most endearing property: **when `T` is trivially copyable, `inplace_vector<T, N>` itself is trivially copyable too**. That means it can be `memcpy`-ed as a whole, passed in registers, and safely handed to DMA — things that matter enormously in embedded and systems programming. It collects the same "contiguous memory + trivially copyable" dividend [Deep Dive into array](02-array.md) discussed, while `std::vector` — which holds a heap pointer and is therefore not trivially copyable — cannot. The over-capacity behavior is restrained by design as well: `push_back` past N throws `std::bad_alloc` (degrading to terminate when exceptions are disabled), and if you want to avoid exceptions altogether, C++26's `try_push_back`/`try_emplace_back` don't throw when the limit is exceeded — their return type is `std::optional<T&>`, with an empty value signaling failure — a good fit for `-fno-exceptions` environments.

```cpp
#include <cstdio>
#include <inplace_vector>
#include <type_traits>

int main()
{
    std::inplace_vector<int, 4> v;     // capacity ceiling 4, never heap-allocates
    static_assert(std::is_trivially_copyable_v<decltype(v)::value_type>); // the stored type int is trivially copyable
    static_assert(std::is_trivially_copyable_v<decltype(v)>);             // so the whole inplace_vector is trivially copyable

    v.push_back(1);
    v.push_back(2);
    v.push_back(3);
    v.push_back(4);    // size is now 4, nothing more fits
    std::printf("size = %zu, max_size = %zu, capacity = %zu\n", v.size(), v.max_size(), v.capacity());
    // v.push_back(5) here would exceed capacity and throw bad_alloc
    // to avoid exceptions use try_push_back / try_emplace_back — no throw past the limit, returns std::optional<T&>, empty means failure
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

Keep the boundary between `inplace_vector` and `array` straight: `array<T, N>`'s size is always N — fixed length; `inplace_vector<T, N>`'s capacity ceiling is N, but its size varies at runtime between 0 and N. For fixed length, use array; for "known ceiling + runtime variability + no heap allocation", use inplace_vector.

## mdspan: The Multidimensional Version of span (C++23, Slicing in C++26)

Third: `std::mdspan`, standardized in C++23 (proposal P0009). [Deep Dive into span](08-span.md) showed `span` as a one-dimensional view over contiguous memory, but the real world is full of two- and three-dimensional data — matrices, images, voxel fields, tensors. Historically all you could do was take a one-dimensional pointer and compute subscripts by hand (`data[i * cols + j]`), which is ugly and an easy way to swap rows and columns by accident. `mdspan` wraps "one contiguous block of memory + a multidimensional shape" into a view type, letting you access it directly with multidimensional subscripts like `m[i, j]` — zero-copy, non-owning, and describing nothing more than "how this block of memory is to be interpreted in multiple dimensions".

It has four template parameters: the element type, `Extents` (the shape — how big each dimension is), `LayoutPolicy` (how multidimensional subscripts map to a one-dimensional offset; the default `layout_right` is row-major, C/C++ style), and `Accessor` (how elements are read and written; the default is raw access). The shape is described with `std::extents<IndexType, dims...>`: if a dimension's size is known at compile time, fill in a constant; if it is only known at runtime, fill in `std::dynamic_extent`; and if that is too much fuss, `std::dextents<IndexType, Rank>` directly says "Rank dimensions, all dynamic". Access uses the **multidimensional bracket subscript** `m[i, j]` (riding on the C++23 language feature for multidimensional `operator[]`, P2128), not the old `m[i][j]` — the latter misleads you into thinking a sub-view comes back, when in fact mdspan computes the multidimensional index straight into a one-dimensional offset and returns a reference to the element. Here is an easy trap to fall into: it's brackets `m[i, j]`, not a function call `m(i, j)`. Early mdspan reference implementations (Kokkos) did use `operator()`, but once C++23 standardized it, everything switched to the multidimensional `operator[]` — which is why quite a few older tutorials and blogs still write `m(i, j)`. Copy those verbatim and they won't compile.

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
    // treat the 12 ints as a 3-row, 4-column 2D view, row-major
    std::mdspan<int, std::extents<size_t, 3, 4>> m(raw);

    std::printf("m[1,2] = %d\n", m[1, 2]);   // row 1, column 2 = 7
    std::printf("m[2,3] = %d\n", m[2, 3]);   // row 2, column 3 = 12

    // dimensions known only at runtime: use dextents
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

One pitfall worth calling out: **`submdspan` (slicing) is C++26, not C++23**. When mdspan landed in C++23, the row-slicing, column-slicing, and sub-block-slicing functionality didn't make the cut and was moved to C++26 (P2630). So if you want to grab a row in C++23, you still have to compute the offset yourself; you need to wait for a C++26 toolchain to use zero-copy slices like `std::submdspan(m, std::full_extent, slice)`. mdspan's bigger significance is being the foundation of `std::linalg` (the linear algebra library) — in later standards, the matrix-operation APIs are all built on top of mdspan.

## Still on the Road: hive and Other Proposals

Finally, one that gets mentioned a lot but **has not entered the standard yet**: `std::hive` (from Matt Bentley's `plf::hive`, proposals P0909/P2826). It is a "node container" whose design goals are stable element addresses (insertion and erasure don't disturb the addresses of other elements), fast erasure, and cache-friendly traversal (nodes organized in blocks rather than a pure linked list) — a fit for scenarios where you "hold references pointing at elements long-term while inserting and erasing frequently". As of C++26 it remains a proposal, not adopted — to use it today you can only go through the third-party `plf::hive` library. We mention it here to point at the direction: the standards committee is seriously considering "a node container nicer to use than list", but it is not yet a member of `std::`, so don't write "C++26's hive" in articles or on your resume.

## A Few Parting Words

Each of this wave of new containers fills one hole: `flat_map` serves the "want ordering and cache friendliness" scenario (at the cost of O(n) mutations and vector-style invalidation); `inplace_vector` covers the middle ground of "capacity ceiling known, runtime growth, absolutely no heap allocation" (C++26 — the trivially copyable property is a real treat for embedded work); `mdspan` gives multidimensional data a zero-copy view type (C++23, with submdspan slicing waiting for C++26). All three depend on a fairly recent toolchain — flat_map needs C++23 library support, inplace_vector needs C++26 — so confirm your compiler and standard library versions before putting them into service. That wraps up the container storyline: from `array` all the way to the new standard containers, the tools for storing data have all been covered; next, volume 3 turns to iterators and algorithms — "traversing and manipulating data".

Want to get your hands on it and see the effects directly? Open the online example below (it runs, and it shows the assembly too):

<OnlineCompilerDemo
  title="New Standard Containers: flat_map / inplace_vector / mdspan"
  source-path="code/examples/vol3/10_new_containers.cpp"
  description="flat_map sorted-vector lookup, inplace_vector fixed-capacity with no heap allocation, mdspan multidimensional subscripts m[i,j] (C++26)"
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
- [plf::hive (proposal library reference) — GitHub](https://github.com/mattreecebentley/plf_hive)
