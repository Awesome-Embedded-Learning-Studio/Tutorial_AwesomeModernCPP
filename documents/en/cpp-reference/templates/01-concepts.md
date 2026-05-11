# Constraints and Concepts (C++20)

## One-Liner

A mechanism for specifying semantic requirements on template parameters (such as "hashable" or "iterator"), catching incorrect types early at compile time and producing readable error messages.

## Header

`#include <concepts>`

## Core API Cheat Sheet

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Concept definition | `template<...> concept Name = constraint-expression;` | Defines a named set of constraints |
| requires expression | `requires { /* 表达式 */ }` | Checks whether an expression is valid |
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

- A purely compile-time feature with zero runtime overhead, suitable for resource-constrained environments
- Constraint-driven design catches type errors at compile time, preventing undefined behavior (UB) on the target board
- Standard library concepts (such as `std::integral`, `std::same_as`) can directly constrain the interfaces of hardware register wrapper types
- Error messages are significantly shortened, greatly accelerating the development and debugging cycle of low-level template libraries

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 10.0 | 10.0 | 19.28 |

## See Also

- [cppreference: Constraints and concepts](https://en.cppreference.com/w/cpp/language/constraints)

---

*Some content referenced from [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
