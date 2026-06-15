---
chapter: 7
cpp_standard:
- 20
- 23
description: Explains the rationale behind C++20's `char8_t`, the two pitfalls and
  migration patterns for the `u8` literal type changes, and the relaxation of array
  initialization in C++23's P2513.
difficulty: intermediate
order: 3
platform: host
prerequisites:
- 卷一：std::string 与字符串字面量基础
reading_time_minutes: 6
tags:
- host
- cpp-modern
- intermediate
- 类型安全
title: char8_t and UTF-8 Strings
translation:
  engine: anthropic
  source: documents/vol3-standard-library/30-char8-t-utf8.md
  source_hash: bf65e1fa69d057d8e2387796ce4ed2c2c677e348f2808d359b0b024109c38afc
  token_count: 1220
  translated_at: '2026-06-14T00:19:58.325857+00:00'
---
# char8_t and UTF-8 Strings

Before C++20, the type of the UTF-8 string literal `u8"..."` was `const char[]`—which is fundamentally no different from ordinary strings. This might sound trivial, but it is actually the root of many pitfalls: you cannot distinguish at the type level whether "this string is UTF-8" or "this string is the native execution character set," and the compiler cannot help you prevent errors where UTF-8 is incorrectly treated as raw bytes. C++20 introduced `char8_t` to separate UTF-8 from the ambiguous zone of `char`, giving it a dedicated type so the type system can guard us for you. This change comes from proposal **P0482R6** "char8_t: A type for UTF-8 characters and strings". To detect support, check `__cpp_char8_t` (C++20, value `201811`).

However—I must issue a warning in advance—this "independent type" change is **breaking**: it altered the type of `u8""` string literals, causing a large amount of legacy code that compiled peacefully under C++17 to fail immediately when upgraded to C++20. In this article, we will clearly explain the two most common pitfalls, how to migrate code, and the fix C++23 applied later.

------

## u8 Literals: The Type Transformation

Starting with C++20, the type of the UTF-8 string literal `u8"..."` changed from `const char[]` to `const char8_t[]`; the type of the UTF-8 character literal `u8'x'` also changed from `char` to `char8_t`. This `char8_t` is a **distinct fundamental type** with an underlying type of `unsigned char`. Its size, alignment, and conversion rank are all consistent with `char`—but it **does not participate in aliasing rules** (it is not one of the types allowed for alias access in [basic.lval]), meaning you cannot use `char8_t*` to legally alias access the memory of other objects.

Why go to such lengths to create a separate type? The reason is simple: once types are separated, the compiler can directly report errors for mistakes like "treating a UTF-8 string as a native encoding `char` string" or "printing `char8_t` as an integer," rather than waiting for runtime to output a screen full of garbage before you realize the mistake. C++20 decided that trading a bit of migration cost for type safety is worth it.

## Two Classic Pitfalls

With the type change, two migration pitfalls surface.

**The first pitfall: `char8_t*` can no longer implicitly convert to `char*`.** In C++17, `char* p = u8"foo";` was completely legal (back then `u8""` and `""` were still family); in C++20, `u8"foo"` becomes `const char8_t*`, and `char8_t*` will not implicitly convert to `char*`, making this line ill-formed. All old code that feeds `u8""` literals to interfaces expecting `char*` (constructing `std::string`, passing to C APIs, certain overloads of `std::filesystem::path`, etc.) gets caught.

**The second pitfall: the Standard Library intentionally **deleted** `char8_t` `ostream` overloads.** You might think—then I'll just `std::cout << u8"text"` print it? That won't work either. Starting with C++20, the Standard Library **explicitly deleted** the `operator<<` overloads for `char8_t` and `char8_t` sequences (UTF-8 characters/strings) on `std::ostream` and `std::wostream` (note, this isn't "forgot to implement," it's intentional). Consequently, `std::cout << u8'x'` and `std::cout << u8"text"` will fail to compile because they hit the deleted overload. This was done specifically to stop legacy code from blindly printing UTF-8 data as integers or pointers.

## How to Migrate Legacy Code

Facing these two pitfalls, how do we move C++17 code to C++20? Here are a few paths, listed from lowest to highest cost:

1. **Compiler Flag Rollback**: The easiest is to revert via compiler options: add `-fchar8_t-diagnostics` or `-fno-char8_t` on GCC/Clang, or `/Zc:char8_t-` on MSVC. This reverts the type of `u8""` literals back to C++17 `const char*` semantics, so old code compiles immediately. This is only a stopgap for the transition period; don't rely on it for new code long-term.
2. **Explicit Byte-by-Byte Conversion**: When you truly need to feed an interface that only recognizes `char*` and you know the content is UTF-8 bytes, use `reinterpret_cast` (or a C-style cast) to switch the view—the byte content remains unchanged, just the pointer type changes, bypassing the "first pitfall."
3. **The "Politically Correct" Path: `std::u8string`**: Use `std::u8string`/`std::u8string_view` to hold UTF-8 text type-safely. When printing, write a small helper function to convert it out, maintaining type safety to the end.

## C++23's P2513: A Partial Fix

The scope of "cannot initialize" in the "first pitfall" was later narrowed slightly. Proposal **P2513R4** "char8_t Compatibility and Portability," adopted as a Defect Report (DR) for C++20 and landing in C++23 (the value of `__cpp_char8_t` also changed to `202311`), **re-allows using `u8""` string literals to initialize `char` or `char8_t` arrays**—meaning `char a[] = u8"foo";` is legal again. However, note that this only relaxes "array initialization"; the implicit conversion from `char8_t*` to `char*` **remains ill-formed**, so the pointer assignment scenario in pitfall one was not let off the hook.

------

## Try It Out

The demo below places the two pitfalls (which I have "sealed" with comments—uncomment them to cause immediate compilation failure) and two correct ways of writing them side-by-side for easy comparison.

```cpp
#include <iostream>
#include <string>
#include <filesystem>

// Helper to print UTF-8 safely
void print_utf8(const char8_t* str) {
    // Cast is safe here because we know the platform console handles UTF-8
    // (or we are just treating it as a byte sequence for demonstration)
    std::cout << reinterpret_cast<const char*>(str);
}

int main() {
    // --- Pitfall 1: Implicit conversion failure ---
    // In C++17: char* s = u8"Hello"; // OK
    // In C++20: char* s = u8"Hello"; // ERROR: char8_t* cannot convert to char*

    // Fix A: Explicit cast (Use with caution, ensure data is actually UTF-8)
    const char* s1 = reinterpret_cast<const char*>(u8"Hello");
    std::cout << "Fix A (Cast): " << s1 << std::endl;

    // Fix B: Use std::u8string (Type safe)
    std::u8string u8s = u8"Hello UTF-8";
    // std::cout << u8s; // ERROR: operator<< deleted
    print_utf8(u8s.c_str());
    std::cout << std::endl;


    // --- Pitfall 2: Deleted std::cout overloads ---
    // std::cout << u8'x';      // ERROR: operator<< deleted
    // std::cout << u8"text";   // ERROR: operator<< deleted

    // Fix: Cast to const char* for printing (assuming environment supports UTF-8)
    std::cout << "Fix B (Print): " << reinterpret_cast<const char*>(u8"text") << std::endl;


    // --- C++23 Update: Array Initialization ---
    // P2513R4 allows this again in C++23
    char arr[] = u8"Array Init"; // OK in C++23 (and usually in C++20 with DR)
    std::cout << "Array Init: " << arr << std::endl;

    return 0;
}
```

<OnlineCompilerDemo
  title="char8_t and UTF-8 Strings: Pitfalls and Correct Usage"
  source-path="code/examples/vol3/14_char8_t.cpp"
  description="Demonstrates the two compilation failure pitfalls of C++20 u8 literal type changes, and two correct methods: explicit casting and u8string"
  allow-run
  allow-x86-asm
/>

------

## Reference Resources

- [char8_t — cppreference](https://en.cppreference.com/w/cpp/keyword/char8_t)
- [String literal — cppreference](https://en.cppreference.com/w/cpp/language/string_literal)
- [operator<<(basic_ostream) — cppreference](https://en.cppreference.com/w/cpp/io/basic_ostream/operator_ltlt2)
- [P0482R6 char8_t: A type for UTF-8 characters and strings](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p0482r6.html)
- [P2513R4 char8_t Compatibility and Portability](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2022/p2513r4.html)
