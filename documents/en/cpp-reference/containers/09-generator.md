---
chapter: 99
cpp_standard:
- 23
description: Coroutine-based synchronous generator that lazily produces a sequence
  of values using `co_yield`.
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
  translated_at: '2026-06-16T03:28:36.604886+00:00'
  engine: anthropic
  token_count: 470
---
<!--
Reference Card Template
For feature cheat sheets under documents/cpp-reference/.
Unlike article-template.md, reference cards follow a refined, structured format and do not require a narrative style.

Tag usage rules:
1. Must include 1 platform tag (use host for reference cards)
2. Must include 1 difficulty tag
3. Must include at least 1 topic tag
4. Select from the VALID_TAGS set in scripts/validate_frontmatter.py
-->

# std::generator (C++23)

## One-Liner

A coroutine generator that lazily produces a sequence of values—replaces hand-written iterators, features zero heap allocation (customizable allocator), and reduces code volume by an order of magnitude.

## Header

`#include <generator>`

## Core API Cheat Sheet

| Operation | Signature | Description |
|------|------|------|
| Generator Type | `template<class T> class generator` | Lazy value sequence, satisfies the `view` concept |
| Yield Value | `co_yield expr;` | Yields a value and suspends |
| Finish Generation | `co_return;` | Ends the generator |
| Iteration | `generator::iterator` | Input iterator, for range-for loops |
| Range Adaptation | Directly usable in `ranges::` pipelines | Generator is a view, composable |
| Reference Type | `generator<const T&>` | Yield by reference (avoid copies) |
| Allocator | `template<class T, class Alloc> class generator` | Customizable coroutine frame allocator |

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

- Lazy evaluation: Computes the next value only when needed, without pre-allocating memory for the entire sequence.
- Coroutine frames can use custom allocators, suitable for static memory pools.
- Replaces hand-written iterators and callback functions, significantly improving code readability.
- C++23 feature; compiler support is still ongoing (GCC 14+, Clang 17+, MSVC 19.34+).
- Generator lifetime management requires attention: accessing yielded values after the generator is destroyed is undefined behavior.

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 14 | 17 | 19.34 |

## See Also

- [cppreference: std::generator](https://en.cppreference.com/w/cpp/coroutine/generator)

---

*部分内容参考自 [cppreference.com](https://en.cppreference.com/)，采用 [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) 许可*
