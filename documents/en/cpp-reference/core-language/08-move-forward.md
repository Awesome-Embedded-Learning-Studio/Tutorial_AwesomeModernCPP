# std::move (C++11)

## In a Nutshell

Casts an lvalue to an rvalue reference, telling the compiler "this object's resources can be stolen," thereby triggering move construction or move assignment to avoid deep copies.

## Header

`#include <utility>`

## Core API Quick Reference

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Move conversion (since C++14) | `template<class T> constexpr std::remove_reference_t<T>&& move(T&& t) noexcept;` | Casts object `t` to an rvalue reference (xvalue) |
| Perfect forwarding | `template<class T> T&& forward(typename std::remove_reference<T>::type& t) noexcept;` | Preserves value categories in forwarding reference scenarios, must be used with `std::move` |
| Conditional move | `template<class T> typename std::conditional<...>::type move_if_noexcept(T& t) noexcept;` | Casts to an rvalue if move construction is non-throwing, otherwise returns an lvalue |

## Minimal Example

```cpp
#include <iostream>
#include <string>
#include <utility>
#include <vector>
// Standard: C++11
int main() {
    std::string str = "Hello";
    std::vector<std::string> v;
    v.push_back(str);              // 拷贝
    v.push_back(std::move(str));   // 移动，str 变为有效但未指定的状态
    std::cout << v[0] << " " << v[1] << "\n";
    std::cout << "str empty: " << str.empty() << "\n";
}
```

## Embedded Applicability: High

- Zero-overhead abstraction: `std::move` is essentially `static_cast`, resolved at compile time with no runtime cost
- Avoids deep copies: Significantly reduces RAM usage and CPU overhead when passing large buffers (such as `std::vector<uint8_t>`, `std::string`)
- Works with custom resource classes: Can transfer raw pointer ownership (requires RAII), replacing manual resource handover
- Note that a moved-from object is in a "valid but unspecified" state; we cannot read its value, but we can assign to it or destroy it

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 4.6 | 3.0   | 19.0 |

## See Also

- [cppreference: std::move](https://en.cppreference.com/w/cpp/utility/move)

---

*Some content referenced from [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
