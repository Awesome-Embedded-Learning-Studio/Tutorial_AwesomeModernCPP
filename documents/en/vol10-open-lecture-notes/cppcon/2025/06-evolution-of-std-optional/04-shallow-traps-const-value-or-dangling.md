---
title: "Shallow traps of optional references: const, value_or, and dangling"
description: "CppCon 2025 notes — the three positions of shallow const on optional<T&>, conditional explicit, why value_or always returns a value, and why dangling defense uses delete instead of requires"
chapter: 6
order: 4
conference: cppcon
conference_year: 2025
talk_title: 'The Evolution of std::optional: From Boost to C++26'
speaker: Steve Downey
cpp_standard: [17, 23, 26]
difficulty: intermediate
platform: host
reading_time_minutes: 11
tags:
  - cpp-modern
  - host
  - intermediate
  - optional
prerequisites:
  - "What an optional reference is, and why assignment is always a rebind"
related:
  - "What an optional reference is, and why assignment is always a rebind"
  - "The Move-Semantics Traps Hiding Inside Optional References"
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/06-evolution-of-std-optional/04-shallow-traps-const-value-or-dangling.md
  source_hash: 97b67239bc67e0170fbb3a44f2788c20eee0307c0fe3f5acf13168c931d0c4f5
  translated_at: '2026-07-29T00:33:58.679864+00:00'
  engine: anthropic
  token_count: 2726
---

# Shallow traps of optional references: const, value_or, and dangling

[In the previous part](./03-optional-reference-and-assignment.md) we straightened out the core semantics of `optional<T&>`. This part shifts angles and digs into the corners that are easy to step on at the usage level: where const goes, what value_or returns, and what happens when you construct from a temporary. Each trap is small on its own, but together they form a complete map of what to avoid.

## const is shallow

This is the one I personally got most wrong before. I used to think that the reference wrapped inside `const optional<T&>` should come out const when you dereference it — after all, doesn't const "propagate" in C++? Not at all. Run it and you'll be convinced:

```cpp
// shallow_const.cpp
#include <iostream>
#include <optional>

int main() {
    int x = 42;

    // const on the optional
    const std::optional<int&> opt = x;
    *opt = 100;                         // compiles! can modify x
    std::cout << "const optional<int&>: x=" << x << "\n";

    // const on T
    int y = 7;
    std::optional<const int&> opt2 = y;
    // *opt2 = 9;                       // this line would be a compile error
    std::cout << "optional<const int&>: y=" << y << " (cannot modify through *opt2)\n";
}
```

```bash
$ g++ -std=c++26 shallow_const.cpp -o shallow_const && ./shallow_const
const optional<int&>: x=100
optional<const int&>: y=7 (cannot modify through *opt2)
```

The const inside `const optional<int&>` is shallow: it only constrains the optional object itself (you can't reset it, and you can't reassign it to point at something else), and it does not constrain what comes out when you dereference. `*opt = 100` compiles, and x really does get changed to 100.

Looking back, the principle is simple. An `optional<T&>` is just a pointer underneath, and `const optional<T&>` is equivalent to `T* const` — the pointer itself can't change, but modifying what it points at through the pointer is perfectly legal. This matches how C++ itself defines const: const has always been shallow. It only looks "deep" when we write `const int&` because the const directly decorates the int.

### Three positions for const

Lay the three spellings side by side and you'll see they mirror the rules for pointers exactly:

```cpp
int value = 42;

std::optional<const int&> opt1 = value;     // reference points to const, can't modify value through it
const std::optional<int&>   opt2 = value;   // optional itself is const, can't reset/rebind, but can modify value
const std::optional<const int&> opt3 = value; // both locked
```

This corresponds to the distinction in the pointer world between `const int*` (points to const) and `int* const` (the pointer itself is const). `optional<const int&>` and `const optional<int&>` are two different things. I used to think of an optional reference as "a wrapper around a reference," but the right mental model is "a wrapper around a pointer."

:::warning
This trap can bury real bugs in production code. You think that by passing in a `const optional<T&>`, the receiver won't be able to modify your original value — and then they just do `*opt = ...` and change it. Tracking this kind of bug down will make you question reality. Remember: `const optional<T&>` guards against rebinding, not against modifying the referenced object.
:::

## Conditional explicit: how explicit does it have to be

This one leans toward library design, but as a user you still need to know what it implies.

My personal habit is to slap explicit on a constructor whenever I can, to prevent implicit conversions from surprising me. But optional carries too much historical baggage: the implicit construction of `optional<T>` from `T` has existed for a long time, huge amounts of code depend on it, and it can't be changed.

So what about `optional<T&>`? Constructing from `T&`, converting from `optional<U&>` — explicit or implicit? The final design decision is conditional explicit: it follows the explicit-ness of the underlying `T`'s constructor. If `T` can be implicitly constructed from `U`, then `optional<T&>` can be implicitly constructed from `optional<U&>`; if `T`'s construction from `U` is explicit, then optional's side is explicit too.

The strategy sounds reasonable, but the implementation cost is non-trivial. Steve Downey said they paid a steep price in library-design work to make sure the various conversions landed in the right constructor instead of being snatched by other overloads. The most direct impact on us as users is this: some scenarios you thought would implicitly convert may suddenly stop working, and you'll have to write `optional<T&>{...}` explicitly. When you hit a compile error like that, don't freeze up — think about whether the underlying type's explicit-ness is at play.

## value_or always returns a value

`value_or` is one of the most commonly used methods on optional, but its return type is a real headache under `optional<T&>`. Run it:

```cpp
// value_or_type.cpp
#include <iostream>
#include <optional>
#include <type_traits>

int main() {
    int x = 42;
    std::optional<int&> opt = x;
    auto r1 = opt.value_or(0);                  // engaged
    static_assert(std::is_same_v<decltype(r1), int>);

    std::optional<int&> empty;
    auto r2 = empty.value_or(7);                 // empty
    static_assert(std::is_same_v<decltype(r2), int>);

    std::cout << "engaged value_or=" << r1 << " empty value_or=" << r2 << " (both int)\n";
}
```

```bash
$ g++ -std=c++26 value_or_type.cpp -o value_or_type && ./value_or_type
engaged value_or=42 empty value_or=7 (both int)
```

What's stored inside the optional is clearly a reference, so when value_or is engaged why doesn't it hand the reference back to us? Because there's a fundamental contradiction. When the optional is engaged, you want a `T&` back; when it's empty, you want the default value back, and the default value is a temporary — returning a reference to it would be a dangling reference. These two demands can't be unified in a single return type.

The current decision is that value_or always returns a value. It's the safest option. It may not be the most convenient, but at least it can't produce a dangling reference. Steve Downey's stance is clear: when you can't make everyone happy, do the safe thing and come back to it later.

This restriction really is inconvenient in real scenarios. Say you want to choose between an `optional<const Config&>` and a global config and return a reference — value_or is basically unusable, and you have to honestly write the if by hand:

```cpp
const Config& get_config(std::optional<const Config&> override) {
    if (override) return *override;
    return global_config;
}
```

Several proposals (including Steve Downey's own) are trying to generalize value_or so it can return the common reference type of `T` and `U`. This capability has only recently become expressible at the language level, and the library technique is still under construction. For now we live with the "always returns a value" version — safe, but a little clumsy.

## Dangling defense: delete, not requires

This is probably the decision in the whole design that I admire most. Consider this scenario: you construct an `optional<T&>` from a temporary, the temporary dies at the end of the expression, but the optional still holds a reference to it — a classic dangling reference.

```cpp
std::optional<int&> bad() {
    return std::optional<int&>(42);   // construct from a temporary int, 42 dies immediately
}
```

In the past this kind of code might "happen to work," because the compiler doesn't necessarily clean up temporaries right away. Push it to production with optimizations on, the compiler aggressively reclaims temporaries, and you get a bizarre memory problem that's hard even to reproduce.

The design principle is to check for dangling properties. If a conversion would produce a temporary, and that temporary would die at the end of the expression, the overload is deleted outright — not excluded from the overload set with a requires clause.

The difference between the two is critical. With requires, the compiler finds that the overload doesn't satisfy the constraint and keeps looking for other overloads, potentially dropping into some constructor you never expected and throwing a pile of incomprehensible errors. With `= delete`, the compiler tells you directly: this function is deleted. The error comes earlier and more clearly.

```cpp
// Design idea (simplified sketch)
template<typename U>
    requires (std::is_lvalue_reference_v<U>)   // only construct from lvalues
optional(optional<U&>);                         // requires: if unsatisfied, keeps looking for other overloads

// versus
template<typename U>
optional(U&&) = delete;                         // delete: errors directly, doesn't look further
```

What you wrote genuinely doesn't work, rather than being shoved by the compiler into some path that happens to compile. The difference for debugging experience is huge.

Worth a special mention is the range-for fix. Before, you might write a pipeline like this:

```cpp
for (auto& x : some_map | some_transform | another_transform) {
    // ...
}
```

If the middle of the pipeline produced a temporary, and some adapter returned an `optional<T&>` pointing at that temporary, the temporary could die before the first iteration of the for loop even began. C++23 fixed this: temporaries constructed inside a range-for loop now live for the entire duration of the for loop. This fix did rule out a few cases that were previously safe, but it forbade far more dangerous cases — on balance, a net win.

## Up next

Shallow const, conditional explicit, value_or, and dangling defense — these are the traps at the "how do you use it correctly" level of `optional<T&>`. The next part shifts angles again and looks at the territory where it intersects with move semantics. That's where C++ produces its most insidious bugs: one `std::move` in the wrong place and you may have "stolen someone else's cat."
