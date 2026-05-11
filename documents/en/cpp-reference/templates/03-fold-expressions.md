# Fold Expressions (C++17)

## One-Liner

Folds a parameter pack from a variadic template into a single expression using a specified operator, eliminating the need to manually write recursive base cases.

## Header

None required (language feature)

## Core API Quick Reference

| Operation | Signature | Description |
|------|------|------|
| Unary right fold | `(pack op ...)` | Expands to `E1 op (... op (EN-1 op EN))` |
| Unary left fold | `(... op pack)` | Expands to `(((E1 op E2) op ...) op EN)` |
| Binary right fold | `(pack op ... op init)` | Right fold with an initial value |
| Binary left fold | `(init op ... op pack)` | Left fold with an initial value |
| Empty pack fold (`&&`) | `(... && args)` | Result is `true` when the pack is empty |
| Empty pack fold (`\|\|`) | `(... \|\| args)` | Result is `false` when the pack is empty |
| Empty pack fold (`,`) | `(expr, ...)` | Result is `void()` when the pack is empty |

> `op` supports 32 binary operators: `+ - * / % ^ & | = < > << >> += -= *= /= %= ^= &= |= <<= >>= == != <= >= && || , .* ->*`

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

- Compile-time pure computations (such as condition checks in `static_assert`) have zero runtime overhead, making them highly suitable
- Replacing recursive template instantiation can reduce compile-time memory usage and compilation time
- Avoid using complex fold expressions in frequently called hot paths to prevent code bloat from increasing Flash usage
- When using comma folds to expand multiple statements, we must confirm that the overhead of each statement is within an acceptable range

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 6.0 | 3.6 | 19.1 |

## See Also

- [cppreference: Fold expressions](https://en.cppreference.com/w/cpp/language/fold)

---

*Some content referenced from [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
