# Range-based for Loop (C++11)

## One-Liner

Syntactic sugar for traversing all elements of a container or array without manually writing iterators, making loop code more concise and less error-prone.

## Header

None (language feature)

## Core API Quick Reference

| Operation | Signature | Description |
|------|------|------|
| Read-only traversal | `for (auto item : range)` | Copies each element to `item` |
| Reference traversal | `for (auto& item : range)` | Accesses elements via lvalue reference (modifiable) |
| Const reference traversal | `for (const auto& item : range)` | Avoids copies and prevents modification |
| Init statement | `for (init; auto& item : range)` | Executes initialization before the loop (since C++20) |
| Array traversal | `for (auto item : arr)` | Supports native arrays of known size |

## Minimal Example

```cpp
#include <vector>
#include <iostream>
// Standard: C++11
int main() {
    std::vector<int> v = {1, 2, 3};
    for (const auto& x : v) {
        std::cout << x << ' ';
    }
    return 0;
}
```

## Embedded Applicability: High

- Zero-overhead abstraction: compiles down to code completely equivalent to hand-written iterator or index loops, with no extra runtime cost
- Concise syntax reduces errors caused by out-of-bounds indexing or iterator invalidation
- Combined with `constexpr` arrays, compile-time traversal is also very practical
- Note: be cautious of lifetime issues when traversing member functions that return temporary objects (this is UB prior to C++23)

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 4.6 | 3.0   | 2010 |

## See Also

- [cppreference: Range-based for loop](https://en.cppreference.com/w/cpp/language/range-for)

---

*Some content referenced from [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
