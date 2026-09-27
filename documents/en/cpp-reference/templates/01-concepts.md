---
chapter: 99
cpp_standard:
- 20
- 23
description: Impose semantic constraints on template parameters at compile time and
  get clear, readable error messages.
difficulty: intermediate
order: 1
reading_time_minutes: 2
tags:
- host
- cpp-modern
- 模板
title: Constraints and Concepts
translation:
  source: documents/cpp-reference/templates/01-concepts.md
  source_hash: 4aa373e06d3ae09a4d5618df99378f708fc92fdf655983bfaae3c71839ba4ab3
  translated_at: '2026-09-27T02:17:18+00:00'
  engine: anthropic
  token_count: 1100
---
# Constraints and Concepts (C++20)

## In a Nutshell

A mechanism for specifying semantic requirements on template parameters (such as "hashable" or "iterator"), which catches wrong types as early as possible at compile time and produces readable error messages.

## Header File

`#include <concepts>`

## Core API Cheat Sheet

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Concept definition | `template<...> concept Name = constraint-expression;` | Defines a named set of constraints |
| requires expression | `requires { /* expression */ }` | Checks whether the expressions are valid |
| Nested requirement | `{ expr } -> std::convertible_to<T>;` | Requires the expression to be valid and its result convertible to T |
| Abbreviated template parameter | `void f(Concept auto param)` | Uses a concept constraint directly in the parameter list |
| requires clause | `template<typename T> requires Concept<T> void f(T);` | Appends a constraint after the template declaration |
| Trailing requires | `template<typename T> void f(T) requires Concept<T>;` | Appends a constraint after the function parameter list |
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
    std::cout << add(1, 2) << '\n';     // OK: int satisfies Addable
    // add("a", "b");                   // Error: const char* does not satisfy Addable
}
```

## Embedded Applicability: High

- A pure compile-time feature with zero runtime overhead, well suited to resource-constrained environments
- Constraint-driven design intercepts type errors at compile time, avoiding undefined behavior triggered on the target board
- Standard library concepts (such as `std::integral`, `std::same_as`) can be used directly to constrain the interfaces of hardware register wrapper types
- Error messages shrink dramatically, noticeably speeding up the develop-and-debug cycle of low-level template libraries

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 10.0 | 10.0 | 19.28 |

## See Also

- [cppreference: Constraints and concepts](https://en.cppreference.com/w/cpp/language/constraints)

---

*Some content is referenced from [cppreference.com](https://en.cppreference.com/), used under the [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) license*
