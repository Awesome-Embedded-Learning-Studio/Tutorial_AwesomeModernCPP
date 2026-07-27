---
chapter: 13
cpp_standard:
- 26
description: 'P2996 static reflection in C++26 makes compile-time introspection a built-in language capability. The reflection operator, splice, identifier_of and members_of, template for iteration, and enum-to-string as the killer use case. Includes real output run on Godbolt clang-p2996 and an honest note that mainstream compilers do not support it yet.'
difficulty: advanced
order: 6
platform: host
prerequisites:
- 'Compile-Time Strings: NTTP Class Type and fixed_string'
- 'TMP Core Techniques: The World Before Concepts'
reading_time_minutes: 11
related:
- 'Compile-Time Strings: NTTP Class Type and fixed_string'
- 'Template Instantiation Control: extern template and Compile Time'
tags:
- host
- cpp-modern
- advanced
- 编译期计算
- 模板元编程
- 类型安全
title: 'Static Reflection Basics: The Reflection Operator and Splice'
---
# Static Reflection Basics: The Reflection Operator and Splice

The last piece ended on a teaser: C++26 reflection is about to turn "enum to string" boilerplate into a built-in language capability. This piece cashes that in. Reflection is the ability of a program to **introspect** its own structure at compile time: which members a class has, which values an enum has, what a function signature looks like. C++ waited over twenty years for this capability, until the P2996 proposal was voted into the C++26 working draft at the WG21 Sofia meeting in June 2025. This piece explains the reflection operator, splice, a few core APIs, and two of the clearest use cases: walking a struct's members, and enum to string.

::: warning This is C++26. Mainstream compilers can't run it yet
P2996 made it into the C++26 draft, but the implementation story is early. As of July 2026, neither GCC nor MSVC ships it. The only things that can run it are Bloomberg's open-source clang-p2996 experimental fork and EDG's preview. The local GCC 16.1.1 and clang 22.1.8 on this machine both fail. Tested directly, GCC with the `-freflection` flag and an empty `<meta>` header still reports `expected primary-expression before '^'` on `^^Point`. Clang doesn't recognize `-freflection-latest` at all. So every run output in this piece was produced on Godbolt's `clang_bb_p2996` (Bloomberg branch trunk-20260701), compiled with `-std=c++2c -freflection-latest`. To verify by hand, go to Godbolt and pick that compiler.
:::

## The reflection operator ^^ and the splice [: :]

The core of P2996 is two new syntax forms. The first is the reflection operator `^^`. It acts on a type or a value and produces a compile-time value of type `std::meta::info`. The compiler can use it to look up that thing's structure.

```cpp
#include <meta>

struct Point { int x; int y; };

constexpr auto refl = ^^Point;   // refl is a std::meta::info, pointing at the type Point
```

`^^Point` evaluates to a compile-time `info` value. You can read it as "metadata about the type `Point`." Once you hold this `info`, you can ask "what's your name" and "what members do you have."

The second form is the splice, written `[: refl :]`. It glues an `info` back into the original type or value. `^^` turns a type into an `info`, and `[: :]` turns an `info` back into a type. The two are inverse operations. In the enum example below you'll see `[:enumerator:]` splice an enumerator's `info` back into the enumerator value itself, used for comparison.

## Core APIs: identifier_of, members_of, access_context

Once you have an `info`, P2996 provides a set of query functions under the `std::meta` namespace. Here are the ones this piece uses:

- `identifier_of(info)` returns the entity's name as a `string_view` (before R13 it was called `name_of`, later renamed).
- `nonstatic_data_members_of(type_info, access_context)` returns a `vector<info>` holding all the non-static data members of the type.
- `enumerators_of(enum_info)` returns all enumerators of an enum.

The `access_context::current()` argument was added in P2996R10. It tells reflection "look up members with the current access rights." After all, private members aren't visible to just anyone. You have to pass it in every time you call a member-query function. Verbose, but necessary.

There's also a part you can't skip, called `define_static_array`. The P2996 query functions return a `constexpr std::vector`, and a `vector`'s memory is on the heap. A "heap pointer" can't serve as a compile-time constant inside a template. `define_static_array` materializes the vector's contents into static storage and returns a span that can be iterated at compile time. It sounds convoluted, but any time you want to walk a query result with `template for`, you have to wrap it in this.

## template for: walk reflection results at compile time

The number of things reflection returns is known at compile time (a struct's member count is fixed at compile time), but a traditional `for` can't iterate over "types." P1306's expansion statement (`template for`) was built for this. It expands over a compile-time-known set of values, and each iteration's variable is a real compile-time constant, usable in a type position.

```cpp
template for (constexpr auto member : members) {
    std::cout << "  " << std::meta::identifier_of(member) << "\n";
}
```

Note it's `template for`, not `for`, and the loop variable is declared `constexpr auto`. The loop fully unrolls at compile time, equivalent to writing each member's `cout` statement by hand.

## Use case one: walk a struct's members

Stitch the parts together and you get a reflection version of "print every member name of a struct." Full code:

```cpp
#include <meta>
#include <iostream>

struct Point {
    int x;
    int y;
};

int main() {
    using namespace std::meta;
    constexpr auto refl = ^^Point;
    constexpr auto ctx = access_context::current();
    // define_static_array materializes the vector to static storage, so template for compiles
    constexpr auto members = define_static_array(nonstatic_data_members_of(refl, ctx));

    std::cout << identifier_of(refl) << "\n";
    template for (constexpr auto member : members) {
        std::cout << "  " << identifier_of(member) << "\n";
    }
}
```

Run it on Godbolt's clang_bb_p2996 (local compilers can't, see the warning at the top):

```text
Point
  x
  y
```

No hardcoded strings anywhere in this code. The name `Point` and the member names `x` and `y` were all queried by the compiler from the type itself. Add a field to the struct, rename one, and the output follows automatically without changing a line of code. That's the value of reflection. It erases the seam between "the type definition" and "the code that processes the type."

## Use case two: enum to string

The standout use case for reflection is turning an enumerator into its name. Before reflection, you either hand-wrote a `switch` mapping each enumerator to a string (this boilerplate is everywhere in C++ codebases) or leaned on a third-party library like magic_enum, which sneaks it in by parsing `__PRETTY_FUNCTION__`. Reflection turns it into a few direct lines:

```cpp
#include <meta>
#include <iostream>
#include <string_view>

enum class Color { Red, Green, Blue };

template <typename E>
constexpr std::string_view enum_to_string(E value) {
    using namespace std::meta;
    constexpr auto enumerators = define_static_array(enumerators_of(^^E));
    template for (constexpr auto enumerator : enumerators) {
        if (value == [:enumerator:]) {
            return identifier_of(enumerator);
        }
    }
    return "<unknown>";
}

int main() {
    std::cout << enum_to_string(Color::Red) << "\n";
    std::cout << enum_to_string(Color::Green) << "\n";
    std::cout << enum_to_string(Color::Blue) << "\n";
}
```

```text
Red
Green
Blue
```

Worth breaking down. `enumerators_of(^^E)` gets the `info` list of all of enum `E`'s enumerators. `template for` looks at each one. `[:enumerator:]` splices the current `info` back into the enumerator value itself, then compares it with the `value` passed in. On a match, `identifier_of` returns the name. The whole thing unrolls at compile time. At runtime there's only a chain of comparisons. No string parsing, no table lookup overhead.

This template works for any enum. Add a new enumerator and `enum_to_string` supports it automatically, no switch to maintain. Compare that with the lengths we went to in the last piece to bake a string into a type (`fixed_string`, structural, CTAD). Reflection turns "names" from something you have to haul around back into information the compiler already holds. You just ask for it.

## Still worth the wait

P2996 delivers far more than these two. Look up a type by name, annotate types (annotations), auto-generate serialization code, map fields from struct to struct. The work that used to need heavy code generators or stacks of macros, reflection can do inside the language. But only once compilers catch up. The Bloomberg clang-p2996 used in this piece is a "highly experimental" fork, and its README says plainly: don't use it for anything headed to production. Until GCC and MSVC actually ship, reflection is mostly a "see the future" state. Worth learning, because the API design has stabilized (R13 was voted in), and once compilers arrive you'll use it directly. Not worth pushing into production code today. In the next piece we go back to something you can use right now: how to control template instantiation, and how to treat build time.
