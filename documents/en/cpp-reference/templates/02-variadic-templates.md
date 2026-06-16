---
chapter: 99
cpp_standard:
- 11
- 14
- 17
- 20
- 23
description: Template mechanism that accepts zero or more template parameters or function
  parameters
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
  translated_at: '2026-06-16T03:29:53.115556+00:00'
  engine: anthropic
  token_count: 407
---
<!--
Reference Card Template
Used for feature cheat sheets under documents/cpp-reference/.
Unlike article-template.md, reference cards use a concise, structured format and do not require a narrative style.

Tag usage rules:
1. Must include 1 platform tag (use 'host' for reference cards)
2. Must include 1 difficulty tag
3. Must include at least 1 topic tag
4. Select from the VALID_TAGS set in scripts/validate_frontmatter.py
-->

# Variadic Templates (C++11)

## One-Liner

Allows templates to accept an arbitrary number of arguments of arbitrary types, serving as a type-safe modern alternative to C-style variadic functions (`...`).

## Header

None required (language feature)

## Core API Cheat Sheet

| Operation | Syntax | Description |
|------|------|------|
| Type parameter pack | `typename... Ts` | Accepts zero or more type arguments |
| Non-type parameter pack | `int... Is` | Accepts zero or more non-type arguments |
| Template template parameter pack | `template<typename> class... Templates` | Accepts zero or more templates |
| Parameter pack expansion | `Ts...` | Expands the parameter pack into multiple expressions |
| Parameter pack size | `sizeof...(Ts)` | Returns the number of elements in the parameter pack |
| Fold expression (unary) | `(expr op ...)` | C++17, performs per-element operation on the pack |
| Fold expression (binary) | `(init op ... op expr)` | C++17, performs per-element operation on the pack |

## Minimal Example

```cpp
// 打印所有参数的函数
// Function to print all arguments
template <typename... Ts>
void print_all(Ts... args) {
    ((std::cout << args << " "), ...); // C++17 折叠表达式 (Fold expression)
}

int main() {
    print_all(1, "Hello", 3.14); // 输出: 1 Hello 3.14
}
```

## Embedded Applicability: Medium

- Can completely replace unsafe `va_list`, improving type safety and code maintainability.
- Template instantiation causes code bloat (increased binary size), so monitor Flash usage.
- Suitable for resource-rich scenarios (e.g., application processors with Linux); careful evaluation is needed on bare-metal low-end MCUs.

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 4.3 | 2.9   | TBD |

## See Also

- [cppreference: Parameter packs](https://en.cppreference.com/w/cpp/language/parameter_pack)

---

*Part of the content references [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
