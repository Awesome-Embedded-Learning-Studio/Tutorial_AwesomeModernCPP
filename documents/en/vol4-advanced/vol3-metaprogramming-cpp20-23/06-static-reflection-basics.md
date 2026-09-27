---
chapter: 13
cpp_standard:
- 26
description: 'P2996 static reflection in C++26 turns compile-time introspection into a built-in language capability. Covers the reflection operator, splice recomposition, identifier_of and members_of, template for iteration, and the killer use case of enum to string. Includes real output run on Godbolt clang-p2996 and an honest note that the local compilers do not support it yet.'
difficulty: advanced
order: 6
platform: host
prerequisites:
- 'Compile-Time Strings: NTTP Class Type and fixed_string'
- 'TMP Core Techniques: The World Before Concepts'
reading_time_minutes: 11
related:
- 'Compile-Time Strings: NTTP Class Type and fixed_string'
- 'Template Instantiation Control: extern template and Compile Times'
tags:
- host
- cpp-modern
- advanced
- 编译期计算
- 模板元编程
- 类型安全
title: 'Static Reflection Basics: The Reflection Operator and Splice Recomposition'
translation:
  source: documents/vol4-advanced/vol3-metaprogramming-cpp20-23/06-static-reflection-basics.md
  source_hash: 3cd6650c8dd94ab28efb3fe42bd525793eb2aa22b4e4bcbcb05971bc049a9cc9
  translated_at: '2026-09-26T04:52:48+00:00'
  engine: anthropic
  token_count: 4600
---
# Static Reflection Basics: The Reflection Operator and Splice Recomposition

The last piece, on compile-time strings, dropped a one-line teaser: C++26 reflection is going to turn chores like "enum to string" into built-in language capability. This piece cashes that in. Reflection is a program's ability to **introspect** its own structure at compile time — which members a class has, which values an enum has, what a function signature looks like. C++ waited over twenty years for this capability, until the P2996 proposal was voted into the C++26 working draft at the WG21 Sofia meeting in June 2025. This piece explains the reflection operator, splice, a few core APIs, and two of the most immediate use cases: walking a struct's members, and enum to string.

::: warning This is C++26 — the mainstream compilers on this machine can't run it yet
P2996 made it into the C++26 draft, but the implementation story is very early. As of July 2026, neither GCC nor MSVC ships it; the only implementations that can run it are Bloomberg's open-source clang-p2996 experimental branch and EDG's preview. The GCC 16.1.1 and clang 22.1.8 on this machine don't support it — tested directly, GCC, even with the `-freflection` flag and an empty `<meta>` header in place, still reports `expected primary-expression before '^'` the moment it sees `^^Point`, and clang flat-out doesn't recognize `-freflection-latest` as an option. So every run output for the reflection code in this piece was produced by actually running it on Godbolt's `clang_bb_p2996` (Bloomberg branch trunk-20260701), with the flags `-std=c++2c -freflection-latest`. If you want to verify it yourself, please go to Godbolt and pick that compiler too.
:::

## The Reflection Operator ^^ and the Splice [: :]

The core of P2996 is two pieces of new syntax. The first is the reflection operator `^^`. It acts on a type or a value and produces a compile-time value of a type called `std::meta::info`; holding that, the compiler can go look up the thing's structure.

```cpp
#include <meta>

struct Point { int x; int y; };

constexpr auto refl = ^^Point;   // refl is a std::meta::info, pointing at the type Point
```

`^^Point` evaluates to a compile-time `info` value; you can treat it as "metadata about the type `Point`." Only once you hold this `info` can you go on to ask "what's your name" and "which members do you have."

The second piece of syntax is the splice, written `[: refl :]`, which glues an `info` back into the type or value it came from. `^^` turns a type into an `info`, and `[: :]` turns an `info` back into a type — the two are inverse operations of each other. In the enum example below you'll see `[:enumerator:]` splice an enumerator's `info` back into the enumerator value itself, used for comparison.

## Core API: identifier_of, members_of, access_context

Once you have an `info`, P2996 provides a set of query functions under the `std::meta` namespace. Here are the ones this piece uses:

- `identifier_of(info)` returns the entity's name as a `string_view` (before R13 it was called `name_of`; it was renamed later).
- `nonstatic_data_members_of(type_info, access_context)` returns a `vector<info>` holding all the non-static data members of the type.
- `enumerators_of(enum_info)` returns all the enumerators of an enum.

The `access_context::current()` argument was added after P2996R10. It tells reflection "query members with the current access rights" — after all, private members aren't visible to just anyone. You have to pass it on every call to a member-query function; verbose, but necessary.

There's also one part you can't get around, called `define_static_array`. P2996's query functions return a `constexpr std::vector`, and a `vector`'s memory lives on the heap — a "heap pointer" can't serve as a compile-time constant inside a template. `define_static_array` materializes the vector's contents into a block of static storage and returns a span you can iterate at compile time. It sounds roundabout, but any time you want to walk a query result with `template for`, you have to wrap it in this layer.

## template for: Iterating Reflection Results at Compile Time

The number of things reflection returns is known at compile time (a struct's member count is settled at compile time), but a traditional `for` can't iterate over "types." P1306's expansion statement (`template for`) was built for exactly this: it expands over a set of values known at compile time, one by one, and the variable bound at each iteration is a genuine compile-time constant, usable in a type position.

```cpp
template for (constexpr auto member : members) {
    std::cout << "  " << std::meta::identifier_of(member) << "\n";
}
```

Note that it's `template for`, not `for`, and that the loop variable is declared `constexpr auto`. The loop fully unrolls at compile time, equivalent to handwriting the `cout` statement for every member.

## Use Case 1: Walking a struct's Members

Assemble the parts above and you can write the reflection version of "print every member name of a struct." Full code:

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
    constexpr auto members = define_static_array(nonstatic_data_members_of(refl, ctx));

    std::cout << identifier_of(refl) << "\n";
    template for (constexpr auto member : members) {
        std::cout << "  " << identifier_of(member) << "\n";
    }
}
```

Run it on Godbolt's clang_bb_p2996 (this machine can't run it; see the warning at the top for why):

```text
Point
  x
  y
```

There isn't a single hardcoded string in this code. The name `Point` and the member names `x` and `y` were all looked up by the compiler from the type itself. Add a field to the `struct`, rename one, and the output follows automatically — not a single line of code changes. That is the value of reflection: it erases the seam of manual synchronization between "the type definition" and "the code that processes the type."

## Use Case 2: Enum to String

The use case that put reflection on the map is turning an enumerator into its name. Before reflection, you either hand-wrote a `switch` mapping each enumerator to a string (boilerplate like that is lying all over C++ codebases), or leaned on a third-party library like magic_enum, which sneaks the trick in by parsing `__PRETTY_FUNCTION__`. Reflection turns it into a few lines of straight-up code:

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

This one is worth taking apart. `enumerators_of(^^E)` gets the list of `info` for every enumerator of enum `E`; `template for` looks at them one by one; `[:enumerator:]` splices the current `info` back into the enumerator itself, which is then compared with the `value` passed in; for the one that matches, `identifier_of` fetches the name and returns it. The whole process unrolls at compile time — at runtime only a chain of comparisons is left. No string parsing, no table-lookup overhead.

This template works for any enum. Add a new enumerator and `enum_to_string` supports it automatically — no switch to go patch. Set that against the hoops we jumped through in the last piece to bake a string into a type (`fixed_string`, structural, CTAD): reflection turns "names" from something that takes effort to haul around back into information the compiler already holds, and you just have to ask.

## Still Have to Wait a Bit

P2996 delivers far more than these two. Look up a type by name, annotate types (annotations), auto-generate serialization code, map fields from struct to struct — the jobs that used to need heavyweight code generators or a stack of macros, reflection can kill off inside the language itself. But the precondition is that compilers keep up. The Bloomberg clang-p2996 used in this piece is a "highly experimental" branch; its README says plainly not to use it for anything headed to production. Until GCC and MSVC actually ship, reflection is mostly in a "get a glimpse of the future" state. Worth learning, because the API design has already stabilized (R13 passed the vote), so once compilers arrive you can use it directly; not worth shoving into production code today. In the next piece we return to things you can use today: how to control template instantiation, and how to tame compile times.
