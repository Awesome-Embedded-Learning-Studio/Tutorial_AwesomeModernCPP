---
chapter: 99
cpp_standard:
- 17
- 20
- 23
description: Expand and reduce a parameter pack over a binary operator, replacing
  recursive template expansion.
difficulty: intermediate
order: 3
reading_time_minutes: 1
tags:
- host
- cpp-modern
- intermediate
title: Fold Expressions
translation:
  source: documents/cpp-reference/templates/03-fold-expressions.md
  source_hash: bb701c6711523abb1071ff016b6234e625553a35d2ba79ce3c516e527fc49c54
  translated_at: '2026-09-27T02:18:10+00:00'
  engine: anthropic
  token_count: 850
---
# Fold Expressions (C++17)

## In a Nutshell

Fold a variadic template's pack of arguments into a single expression with a given operator, sparing you the trouble of hand-writing recursive termination conditions.

## Header File

No header required (language feature)

## Core API Cheat Sheet

| Operation | Signature | Description |
|------|------|------|
| Unary right fold | `(pack op ...)` | Expands to `E1 op (... op (EN-1 op EN))` |
| Unary left fold | `(... op pack)` | Expands to `(((E1 op E2) op ...) op EN)` |
| Binary right fold | `(pack op ... op init)` | Right fold with an initial value |
| Binary left fold | `(init op ... op pack)` | Left fold with an initial value |
| Empty-pack fold (`&&`) | `(... && args)` | Result is `true` when the pack is empty |
| Empty-pack fold (`\|\|`) | `(... \|\| args)` | Result is `false` when the pack is empty |
| Empty-pack fold (`,`) | `(expr, ...)` | Result is `void()` when the pack is empty |

> `op` can be any of the 32 binary operators: `+ - * / % ^ & | = < > << >> += -= *= /= %= ^= &= |= <<= >>= == != <= >= && || , .* ->*`

## Minimal Example

```cpp
#include <iostream>
// Standard: C++17

template<typename... Args>
void print(Args&&... args) {
    (std::cout << ... << args) << '\n';
}

template<typename... Args>
bool all(Args... args) {
    return (... && args);
}

int main() {
    print(1, " + ", 2, " = ", 3);
    std::cout << all(true, true, false) << '\n';
}
```

## Embedded Applicability: Medium

- Pure compile-time computation (such as condition checks in `static_assert`) has zero runtime overhead, making it highly suitable
- Replaces recursive template instantiation, which can reduce compile-time memory usage and compile time
- Avoid complex fold expressions on frequently called hot paths, to prevent code bloat from increasing Flash usage
- When a comma fold expands multiple statements, confirm that the cost of each statement stays within acceptable bounds

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 6.0 | 3.6 | 19.1 |

## See Also

- [cppreference: Fold expressions](https://en.cppreference.com/w/cpp/language/fold)

---

*Some content is referenced from [cppreference.com](https://en.cppreference.com/), used under the [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) license*
