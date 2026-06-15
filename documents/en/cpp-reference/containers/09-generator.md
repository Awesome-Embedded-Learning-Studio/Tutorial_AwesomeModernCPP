---
chapter: 99
cpp_standard:
- 23
description: Coroutine-based synchronous generator that lazily produces a sequence
  of values using `co_yield`
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
  engine: anthropic
  source: documents/cpp-reference/containers/09-generator.md
  source_hash: 6882f358f85f992952fff1c32e9de115e0935af67e80afad88796ecdc681a1c9
  token_count: 467
  translated_at: '2026-05-26T10:15:35.091727+00:00'
---
<!--
Reference Card Template
Used for feature quick-reference pages under documents/cpp-reference/.
Unlike article-template.md, reference cards use a concise, structured format and do not require a narrative style.

Tag usage rules:
1. Must include exactly 1 platform tag (reference cards uniformly use host)
2. Must include exactly 1 difficulty tag
3. Must include at least 1 topic tag
4. Selected from the VALID_TAGS set in scripts/validate_frontmatter.py
-->

# std::generator (C++23)

## One-Liner

A coroutine generator that lazily produces a value sequence using `co_yield` — replaces hand-written iterators with zero heap allocation (customizable allocator), reducing code volume by an order of magnitude.

## Header

`#include <generator>`

## Core API Quick Reference

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Generator type | `template<class T> class generator` | Lazy value sequence, satisfies the `view` concept |
| Yield value | `co_yield expr;` | Yields a value and suspends |
| Complete generation | `co_return;` | Ends the generator |
| Iteration | `generator::iterator` | Input iterator, used for range-for |
| Range adaptation | Directly usable in `ranges::` pipelines | Generator is a view, composable |
| Reference type | `generator<const T&>` | Yields by reference (avoids copies) |
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

## Embedded Applicability: Medium

- Lazy evaluation: computes the next value only when needed, without pre-allocating memory for the entire sequence
- Coroutine frames can use custom allocators, suitable for static memory pools
- Replaces hand-written iterators and callback functions, significantly improving code readability
- C++23 feature; compiler support is still ongoing (GCC 14+, Clang 17+, MSVC 19.34+)
- Generator lifetime management requires attention: accessing a yielded value after the generator is destroyed is undefined behavior (UB)

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 14 | 17 | 19.34 |

## See Also

- [cppreference: std::generator](https://en.cppreference.com/w/cpp/coroutine/generator)

---

*Some content referenced from [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
