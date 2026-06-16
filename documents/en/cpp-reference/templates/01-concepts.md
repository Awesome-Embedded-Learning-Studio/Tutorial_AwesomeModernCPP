---
chapter: 99
cpp_standard:
- 20
- 23
description: Apply semantic constraints to template parameters at compile time, providing
  clear error messages.
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
  translated_at: '2026-06-16T03:29:53.028445+00:00'
  engine: anthropic
  token_count: 427
---
# Constraints and Concepts (C++20)

## In a Nutshell

A mechanism for specifying semantic requirements for template parameters (such as "hashable" or "iterator"), which intercepts incorrect types at compile time and produces readable error messages.

## Header File

```cpp
<concepts>
```

## Core API Cheat Sheet

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Concept definition | `template <...> concept Name = ...;` | Defines a named set of constraints |
| requires expression | `requires { expression; }` | Checks if an expression is valid |
| Nested requirement | `requires expression;` | Requires expression validity and result convertible to T |
| Abbreviated function template | `void func(C auto& x)` | Uses concept constraints directly in parameter list |
| requires clause | `template<...> requires ...` | Appends constraints after template declaration |
| Trailing requires | `void func(...) requires ...` | Appends constraints after function parameter list |
| Logical AND | `C1 && C2` | Combines multiple constraints (conjunction) |
| Logical OR | `C1 \|\| C2` | Combines multiple constraints (disjunction) |

## Minimal Example

```cpp
#include <concepts>
#include <vector>
#include <print>

// Define a concept: 'T' must be an integral type
template<typename T>
concept Integral = std::is_integral_v<T>;

// Use concept to constrain function template
// Only accepts types satisfying the Integral concept
auto add(Integral auto a, Integral auto b) {
    return a + b;
}

int main() {
    // OK: int satisfies Integral
    std::println("{}", add(1, 2));

    // Compile Error: double does not satisfy Integral
    // std::println("{}", add(1.0, 2.0));

    return 0;
}
```

## Embedded Applicability: High

- Pure compile-time feature with zero runtime overhead, suitable for resource-constrained environments.
- Constraint-driven design intercepts type errors at compile time, avoiding undefined behavior on the target board.
- Standard library concepts (such as `std::integral`, `std::floating_point`) can directly constrain interfaces of hardware register wrapper types.
- Significantly shortens error messages, accelerating the development and debugging cycle of low-level template libraries.

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 10.0 | 10.0 | 19.28 |

## See Also

- [cppreference: Constraints and concepts](https://en.cppreference.com/w/cpp/language/constraints)

---

*Part of the content references [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
