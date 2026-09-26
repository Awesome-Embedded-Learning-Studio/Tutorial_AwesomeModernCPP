---
chapter: 99
cpp_standard:
- 23
description: Non-owning multidimensional view over contiguous memory, the multidimensional
  generalization of span; zero-copy indexing of two-dimensional and higher contiguous
  data
difficulty: intermediate
order: 11
reading_time_minutes: 3
tags:
- host
- cpp-modern
- intermediate
- span
- 容器
title: std::mdspan
translation:
  source: documents/cpp-reference/containers/11-mdspan.md
  source_hash: b90102866092a90cd0a641085f138de422a669d5f28190b4259a607483e373e1
  translated_at: '2026-09-25T09:21:59+00:00'
  engine: anthropic
  token_count: 520
---
<!--
Reference card: std::mdspan (C++23). The multidimensional generalization of span; verified locally with GCC 16.1.1 -std=c++23.
Indexing pitfall: GCC 16's mdspan uses the C++23 multidimensional operator[] (m[i,j]); the old operator() syntax no longer works.
-->

# std::mdspan (C++23)

## In a nutshell

The multidimensional generalization of `std::span`: a multidimensional view over one contiguous block of memory. It does not own the data, its dimensions can be fixed at compile time or given at runtime, and indexing `m[i, j]` is zero-overhead.

## Header file

`#include <mdspan>`

## Core API Cheat Sheet

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Fixed extents | `mdspan<T, extents<size_t, R, C>>` | Rows and columns known at compile time |
| Dynamic extents | `mdspan<T, dextents<size_t, Rank>>` | Dimensions given at runtime |
| Indexing | `m[i, j]` | Multidimensional subscript (C++23 P2128) — note it is `[]`, not `()` |
| Row count | `m.extent(0)` | Size of dimension 0 |
| Column count | `m.extent(1)` | Size of dimension 1 |
| Total elements | `m.size()` | Product of all dimensions |
| Rank | `m.rank()` | How many dimensions |
| Underlying pointer | `m.data_handle()` | Get the raw pointer |
| Layout | `layout_right` (default) / `layout_left` / `layout_stride` | Row-major / column-major / custom strides |

## Minimal Example

```cpp
// Standard: C++23
#include <mdspan>
#include <cstdio>

int main() {
    int data[2 * 3] = {1, 2, 3, 4, 5, 6};

    // Fixed extents: rows and columns known at compile time
    std::mdspan<int, std::extents<std::size_t, 2, 3>> m(data);
    std::printf("rows=%zu cols=%zu  m[0,0]=%d m[1,2]=%d\n",
                m.extent(0), m.extent(1), m[0, 0], m[1, 2]);

    // Dynamic extents: rows and columns given at runtime
    std::mdspan<int, std::dextents<std::size_t, 2>> dm(data, 2, 3);
    std::printf("dm[1,1]=%d size=%zu\n", dm[1, 1], dm.size());
}
```

Actual output (GCC 16.1.1, `-std=c++23`):

```text
rows=2 cols=3  m[0,0]=1 m[1,2]=6
dm[1,1]=5 size=6
```

## Embedded Suitability: Medium

- A zero-ownership view with no allocation: its size is just a pointer plus the extents (a few dozen bytes), which suits memory-constrained settings
- Multidimensional sensor data (images, matrices, ADC samples) can be passed around zero-copy, replacing hand-computed `row*cols+col` indexing
- The compile-time fixed-extent (`extents`) version can be optimized so thoroughly that everything lives in registers, with no runtime overhead
- Note that the multi-argument `operator[]` only exists as of C++23; the old `operator()` spelling found in older references no longer compiles on GCC 16

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 14 | 17 | 19.36 |

## See Also

- [cppreference: std::mdspan](https://en.cppreference.com/w/cpp/header/mdspan)
- [std::span — the one-dimensional view](01-span.md)

---

*Part of the content is referenced from [cppreference.com](https://en.cppreference.com/) and is licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
