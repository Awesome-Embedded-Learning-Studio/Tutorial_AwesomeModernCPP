---
chapter: 99
cpp_standard:
- 11
- 14
- 17
- 20
- 23
description: A template mechanism that accepts zero or more template parameters or
  function arguments
difficulty: intermediate
order: 2
reading_time_minutes: 2
tags:
- host
- cpp-modern
- intermediate
title: Variadic Templates
translation:
  source: documents/cpp-reference/templates/02-variadic-templates.md
  source_hash: 026a457e68bebaedb8178ad0b62daab959454588c00b2e29168773526d5b942f
  translated_at: '2026-09-27T02:18:03+00:00'
  engine: anthropic
  token_count: 600
---
<!--
Reference Card Template
Used for feature cheat sheets under documents/cpp-reference/.
Unlike article-template.md, reference cards use a concise, structured format and do not require a narrative style.

Tag usage rules:
1. Must include 1 platform tag (reference cards uniformly use host)
2. Must include 1 difficulty tag
3. Must include at least 1 topic tag
4. Select from the VALID_TAGS set in scripts/validate_frontmatter.py
-->

# Variadic Templates (C++11)

## One-Liner

Lets templates accept any number of arguments of any type — the modern, type-safe replacement for C-style variadics (`va_list`).

## Header

No header required (language feature)

## Core API Cheat Sheet

| Operation | Signature | Description |
|------|------|------|
| Type parameter pack | `typename... Ts` | Accepts zero or more type parameters |
| Non-type parameter pack | `Ts... args` | Accepts zero or more non-type parameters |
| Template template parameter pack | `template<typename> class... Ts` | Accepts zero or more templates |
| Parameter pack expansion | `args...` | Expands the parameter pack into multiple expressions |
| Parameter pack size | `sizeof...(args)` | Returns the number of elements in the parameter pack |
| Fold expression | `(args op ...)` / `(... op args)` | C++17, applies an element-wise operation to the pack |

## Minimal Example

```cpp
// Standard: C++11
#include <iostream>

template<typename... Ts>
void print(Ts... args) {
    // Use an initializer list to print the arguments one by one, in order
    int dummy[] = {(std::cout << args << " ", 0)...};
    (void)dummy;
}

int main() {
    print(1, "hello", 3.14);
}
```

## Embedded Applicability: Medium

- Can fully replace the unsafe `va_list`, improving type safety and code maintainability
- Template instantiation causes code bloat (larger binary size), so keep an eye on Flash usage
- A good fit for resource-richer settings (e.g., application processors running Linux); needs careful evaluation on low-end bare-metal MCUs

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 4.3 | 2.9   | TBD |

## See Also

- [cppreference: Parameter packs](https://en.cppreference.com/w/cpp/language/parameter_pack)

---

*Some content is adapted from [cppreference.com](https://en.cppreference.com/), used under the [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) license*
