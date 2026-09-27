---
chapter: 99
cpp_standard:
- 23
description: Coroutine-based synchronous generator that lazily produces value sequences
  via `co_yield`
difficulty: intermediate
order: 9
reading_time_minutes: 2
tags:
- host
- cpp-modern
- intermediate
- coroutine
title: std::generator
translation:
  source: documents/cpp-reference/containers/09-generator.md
  source_hash: ced911b94dac4527906956a92c81fd6df3caeafa376264b2ba426abe03e25fba
  translated_at: '2026-09-27T02:03:26+00:00'
  engine: anthropic
  token_count: 450
---
<!--
Reference Card Template
For feature cheat sheets under documents/cpp-reference/.
Unlike article-template.md, reference cards follow a refined, structured format and do not require a narrative style.

Tag usage rules:
1. Must include 1 platform tag (reference cards uniformly use host)
2. Must include 1 difficulty tag
3. Must include at least 1 topic tag
4. Select from the VALID_TAGS set in scripts/validate_frontmatter.py
-->

# std::generator (C++23)

## One-Liner

A coroutine generator that lazily produces a sequence of values with `co_yield` — it replaces hand-written iterators, allocates nothing on the heap (the allocator is customizable), and cuts the amount of code by an order of magnitude.

## Header

`#include <generator>`

## Core API Cheat Sheet

| Operation | Signature | Description |
|------|------|------|
| Generator type | `template<class T> class generator` | Lazy sequence of values; satisfies the `view` concept |
| Yield a value | `co_yield expr;` | Produces a value and suspends |
| Finish generating | `co_return;` | Ends the generator |
| Iteration | `generator::iterator` | Input iterator, for range-for loops |
| Range adaptation | Usable directly in `ranges::` pipelines | A generator is a view, so it composes |
| Reference type | `generator<const T&>` | Yields by reference (avoids copies) |
| Allocator | `template<class T, class Alloc> class generator` | Customizable allocator for the coroutine frame |

## Minimal Example

```cpp
// Standard: C++23
#include <generator>
#include <iostream>

std::generator<int> fibonacci() {
    int a = 0, b = 1;
    while (true) {
        co_yield a;
        auto tmp = a;
        a = b;
        b = tmp + b;
    }
}

int main() {
    for (int v : fibonacci() | std::views::take(8)) {
        std::cout << v << " "; // 0 1 1 2 3 5 8 13
    }
}
```

## Embedded Applicability: Moderate

- Lazy evaluation: the next value is computed only when needed, with no memory pre-allocated for the whole sequence
- The coroutine frame can use a custom allocator, which suits static memory pools
- Replaces hand-written iterators and callbacks, a big readability win
- C++23 feature; compiler support is still rolling out (GCC 14+, Clang 17+, MSVC 19.34+)
- Watch the generator's lifetime: accessing a yielded value after the generator is destroyed is undefined behavior

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 14 | 17 | 19.34 |

## See Also

- [cppreference: std::generator](https://en.cppreference.com/w/cpp/coroutine/generator)

---

*Some content is referenced from [cppreference.com](https://en.cppreference.com/) and used under the [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) license*
