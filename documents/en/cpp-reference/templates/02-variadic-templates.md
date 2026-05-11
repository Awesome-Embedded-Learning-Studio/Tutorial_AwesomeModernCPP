<!--
Reference Card Template
Used for feature quick-reference pages under documents/cpp-reference/.
Unlike article-template.md, reference cards use a concise, structured format without a narrative style.

Tag usage rules:
1. Must include exactly 1 platform tag (reference cards uniformly use host)
2. Must include exactly 1 difficulty tag
3. Must include at least 1 topic tag
4. Selected from the VALID_TAGS set in scripts/validate_frontmatter.py
-->

# Variadic Templates (C++11)

## In a Nutshell

Allows templates to accept an arbitrary number of arguments of arbitrary types, providing a type-safe modern alternative to C-style variadic arguments (`va_list`).

## Header

None required (language feature)

## Core API Quick Reference

| Operation | Signature | Description |
|------|------|------|
| Type parameter pack | `typename... Ts` | Accepts zero or more type arguments |
| Non-type parameter pack | `Ts... args` | Accepts zero or more non-type arguments |
| Template template parameter pack | `template<typename> class... Ts` | Accepts zero or more templates |
| Parameter pack expansion | `args...` | Expands a parameter pack into multiple expressions |
| Parameter pack size | `sizeof...(args)` | Returns the number of elements in the parameter pack |
| Fold expression | `(args op ...)` / `(... op args)` | C++17, applies a per-element operation over a parameter pack |

## Minimal Example

```cpp
// Standard: C++11
#include <iostream>

template<typename... Ts>
void print(Ts... args) {
    // 利用初始化列表保证顺序地逐个打印
    int dummy[] = {(std::cout << args << " ", 0)...};
    (void)dummy;
}

int main() {
    print(1, "hello", 3.14);
}
```

## Embedded Applicability: Medium

- Can completely replace unsafe `va_list`, improving type safety and code maintainability
- Template instantiation causes code bloat (increased binary size), so we need to monitor Flash usage
- Suitable for resource-rich scenarios (such as application processors running Linux); requires careful evaluation on bare-metal, low-end MCUs

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| 4.3 | 2.9   | TBD |

## See Also

- [cppreference: Parameter packs](https://en.cppreference.com/w/cpp/language/parameter_pack)

---

*Some content adapted from [cppreference.com](https://en.cppreference.com/) under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) license*
