---
title: "std::generator"
description: "Coroutine-based synchronous generator that lazily produces value sequences with co_yield"
chapter: 99
order: 9
tags:
  - host
  - cpp-modern
  - intermediate
  - coroutine
difficulty: intermediate
cpp_standard: [23]
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

## In a Nutshell

A coroutine generator that lazily produces value sequences via `co_yield` -- replaces hand-written iterators, zero heap allocation (allocator-customizable), and reduces code by an order of magnitude.

## Header

`#include <generator>`

## Core API Quick Reference

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Generator type | `template<class T> class generator` | Lazy value sequence, satisfies the `view` concept |
| Yield value | `co_yield expr;` | Produces a value and suspends |
| Finish generation | `co_return;` | Ends the generator |
| Iteration | `generator::iterator` | Input iterator, for use with range-for |
| Range adaptation | Usable directly in `ranges::` pipelines | Generator is a view, composable |
| Reference type | `generator<const T&>` | Yield by reference (avoids copies) |
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

- Lazy evaluation: computes the next value only when needed, no pre-allocation of the entire sequence's memory
- Coroutine frames can use custom allocators, suitable for static memory pools
- Replaces hand-written iterators and callback functions, dramatically improving code readability
- C++23 feature, compiler support is still advancing (GCC 14+, Clang 17+, MSVC 19.34+)
- Generator lifetime management requires care: accessing yielded values after the generator is destroyed is undefined behavior

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 14 | 17 | 19.34 |

## See Also

- [cppreference: std::generator](https://en.cppreference.com/w/cpp/coroutine/generator)

---

*Some content adapted from [cppreference.com](https://en.cppreference.com/) under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) license*
