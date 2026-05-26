---
title: Constraints and Concepts
description: Apply semantic constraints to template parameters at compile time, providing
  clear error messages.
chapter: 99
order: 1
tags:
- host
- cpp-modern
- 模板
difficulty: intermediate
cpp_standard:
- 20
- 23
translation:
  source: documents/cpp-reference/templates/01-concepts.md
  source_hash: 863a07dfc69e39d779e7f20666cb954b8f24f117a900fc82c971e8464ee496e6
  translated_at: '2026-05-26T10:18:09.108361+00:00'
  engine: anthropic
  token_count: 423
---
# Constraints and Concepts (C++20)

## In a Nutshell

A mechanism for specifying semantic requirements on template parameters (such as "hashable" or "iterator"), which intercepts incorrect types at compile time and produces readable error messages.

## Header

`#include <concepts>`

## Core API Quick Reference

| Operation | Signature | Description |
|------|------|------|
| Concept definition | `template<...> concept Name = constraint-expression;` | Defines a named set of constraints |
| requires expression | `requires { /* 表达式 */ }` | Checks if an expression is valid |
| Nested requirement | `{ expr } -> std::convertible_to<T>;` | Requires an expression to be valid and its result convertible to T |
| Abbreviated function template | `void f(Concept auto param)` | Uses concept constraints directly in the parameter list |
| requires clause | `template<typename T> requires Concept<T> void f(T);` | Appends constraints after a template declaration |
| Trailing requires | `template<typename T> void f(T) requires Concept<T>;` | Appends constraints after a function parameter list |
| Logical AND | `Concept1 && Concept2` | Combines multiple constraints (conjunction) |
| Logical OR | `Concept1 \|\| Concept2` | Combines multiple constraints (disjunction) |

## Minimal Example

```cpp
#include <concepts>
#include <iostream>

template<typename T>
concept Addable = requires(T a, T b) { a + b; };

template<Addable T>
T add(T a, T b) { return a + b; }

int main() {
    std::cout << add(1, 2) << '\n';     // OK: int 满足 Addable
    // add("a", "b");                   // Error: const char* 不满足 Addable
}
```

## Embedded Applicability: High

- A pure compile-time feature with zero runtime overhead, making it ideal for resource-constrained environments
- Constraint-driven design intercepts type errors at compile time, preventing undefined behavior (UB) from triggering on the target board
- Standard library concepts (such as `std::integral`, `std::same_as`) can be used directly to constrain the interfaces of hardware register wrapper types
- Error messages are significantly shortened, greatly accelerating the development and debugging cycle of low-level template libraries

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 10.0 | 10.0 | 19.28 |

## See Also

- [cppreference: Constraints and concepts](https://en.cppreference.com/w/cpp/language/constraints)

---

*Some content referenced from [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
