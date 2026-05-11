# std::make_unique (C++14)

## In a Nutshell

Safely creates `std::unique_ptr`, offering better safety and more concise code than writing `new` directly.

## Header

`#include <memory>`

## Core API Quick Reference

| Operation | Signature | Description |
|-----------|-----------|-------------|
| Construct object | `template<class T, class...Args> unique_ptr<T> make_unique(Args&&... args)` | Creates a unique_ptr for a non-array type (C++14) |
| Construct array | `template<class T> unique_ptr<T> make_unique(std::size_t size)` | Creates an array of unknown bound with value initialization (C++14) |
| Fixed-length array forbidden | `template<class T, class...Args> /* unspecified */ make_unique(Args&&... args) = delete` | Arrays of known bound are explicitly deleted (C++14) |
| Default-initialize object | `template<class T> unique_ptr<T> make_unique_for_overwrite()` | Creates a non-array type with default initialization (C++20) |
| Default-initialize array | `template<class T> unique_ptr<T> make_unique_for_overwrite(std::size_t size)` | Creates an array of unknown bound with default initialization (C++20) |

## Minimal Example

```cpp
#include <memory>
#include <cstdio>
// Standard: C++14
struct Foo {
    Foo(int v) : val(v) { std::printf("Foo(%d)\n", val); }
    ~Foo() { std::printf("~Foo()\n"); }
    int val;
};
int main() {
    auto p1 = std::make_unique<Foo>(42);
    auto p2 = std::make_unique<Foo[]>(3);
}
```

## Embedded Applicability: High

- A zero-overhead abstraction; compiles to code completely equivalent to directly using `new`
- Explicitly expresses exclusive ownership semantics, preventing resource leaks
- Avoids the exception safety hazard caused by separating the `new` expression from the `unique_ptr` constructor
- Available since C++14, supported by all mainstream embedded compilers

## Compiler Support

| GCC | Clang | MSVC |
|-----|-------|------|
| TBA | TBA | TBA |

## See Also

- [cppreference: std::make_unique](https://en.cppreference.com/w/cpp/memory/unique_ptr/make_unique)

---

*Some content referenced from [cppreference.com](https://en.cppreference.com/), licensed under [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)*
