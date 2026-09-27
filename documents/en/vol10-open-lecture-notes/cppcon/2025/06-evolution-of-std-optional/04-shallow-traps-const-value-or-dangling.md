---
title: "Shallow Traps of Optional References: const, value_or, and Dangling"
description: 'CppCon 2025 notes — the three positions of shallow const on optional<T&>, conditional explicit, why value_or always returns a value, and why dangling defense uses delete instead of requires'
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
  - "What an Optional Reference Is, and Why Assignment Is Always a Rebind"
related:
  - "What an Optional Reference Is, and Why Assignment Is Always a Rebind"
  - "The Move-Semantics Traps Hiding Inside Optional References"
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/06-evolution-of-std-optional/04-shallow-traps-const-value-or-dangling.md
  source_hash: 97b67239bc67e0170fbb3a44f2788c20eee0307c0fe3f5acf13168c931d0c4f5
  translated_at: '2026-09-26T16:22:36+00:00'
  engine: anthropic
  token_count: 2800
---
# Shallow Traps of Optional References: const, value_or, and Dangling

[The previous part](./03-optional-reference-and-assignment.md) straightened out the core semantics of `optional<T&>`. This part changes perspective and digs into the corners where it is easy to go wrong at the usage level: where const goes, what value_or returns, and what happens when you construct from a temporary. Each trap looks small on its own; put together, they form a complete map of what to avoid.

## const Is Shallow

This is the one I personally had the most backwards. I used to assume that the reference wrapped inside `const optional<T&>` would come out const when you dereference it — after all, doesn't const "propagate" in C++? Not even close. Run it and you will be convinced:

```cpp
// shallow_const.cpp
#include <iostream>
#include <optional>

int main() {
    int x = 42;

    // const on the optional
    const std::optional<int&> opt = x;
    *opt = 100;                         // compiles! and it really does modify x
    std::cout << "const optional<int&>: x=" << x << "\n";

    // const on T
    int y = 7;
    std::optional<const int&> opt2 = y;
    // *opt2 = 9;                       // this line would be a compile error
    std::cout << "optional<const int&>: y=" << y << "（不能通过 *opt2 改）\n";
}
```

```bash
$ g++ -std=c++26 shallow_const.cpp -o shallow_const && ./shallow_const
const optional<int&>: x=100
optional<const int&>: y=7（不能通过 *opt2 改）
```

The const in `const optional<T&>` is shallow: it constrains only the optional object itself (you cannot reset it, and you cannot reassign it to point at something else) and does not constrain what dereferencing hands back at all. `*opt = 100` compiles, and x really does get changed to 100.

In hindsight, the principle is simple. An `optional<T&>` is nothing but a pointer underneath, so `const optional<T&>` is equivalent to `T* const`: the pointer itself cannot change, but modifying the thing it points at through the pointer is perfectly legitimate. This is consistent with how the C++ language itself defines const — const has always been shallow. It only looks "deep" when we write `const int&`, because there the const decorates the int directly.

### The Three Positions of const

Lay the three spellings side by side and you can see they mirror the pointer rules exactly:

```cpp
int value = 42;

std::optional<const int&> opt1 = value;     // the reference points to const; cannot modify value through it
const std::optional<int&>   opt2 = value;   // the optional itself is const: no reset/rebind, but value stays modifiable
const std::optional<const int&> opt3 = value; // both locked
```

This corresponds to the pointer-world distinction between `const int*` (points to const) and `int* const` (the pointer itself is const). `optional<const int&>` and `const optional<int&>` are two different things. I used to model an optional reference as "a wrapper around a reference"; the correct model is "a wrapper around a pointer."

:::warning
This trap can bury real bugs in production code. You think that by passing in a `const optional<T&>` the receiver cannot modify your original value — and then they just go ahead and change it with `*opt = ...`. Tracking this kind of bug down will genuinely make you question your life choices. Remember: `const optional<T&>` guards against rebinding, not against modification of the referenced object.
:::

## Conditional explicit: How Explicit Is Explicit Enough

This one leans toward library design, but as a user you still need to know what it means for you.

My personal habit is to mark a constructor explicit whenever I can, to keep implicit conversions from springing surprises on me. But optional carries too much historical baggage: implicit construction of `optional<T>` from `T` has existed for a long time, enormous amounts of code depend on it, and it cannot be changed.

So what about `optional<T&>`? Constructing from `T&`, converting from `optional<U&>` — explicit or implicit? The final design decision is conditional explicit: it follows the explicit-ness of the underlying `T`'s construction. If `T` can be implicitly constructed from `U`, then `optional<T&>` can be implicitly constructed from `optional<U&>`; if `T`'s construction from `U` is explicit, then optional's side is explicit too.

The strategy sounds reasonable, but implementing it was anything but cheap. Steve Downey said they paid a heavy price in library-design work making sure the various conversions landed in the right constructor instead of being intercepted by other overloads. The most direct impact on us as users is this: some scenarios you assumed would convert implicitly may suddenly stop working, and you will have to spell out `optional<T&>{...}` explicitly. When you hit that kind of compile error, don't freeze up — think about whether the underlying type's explicit-ness is at play.

## value_or Always Returns a Value

`value_or` is one of the most commonly used methods on optional, but its return type is a genuine headache under `optional<T&>`. Run it:

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

    std::cout << "engaged value_or=" << r1 << " empty value_or=" << r2 << "（都是 int）\n";
}
```

```bash
$ g++ -std=c++26 value_or_type.cpp -o value_or_type && ./value_or_type
engaged value_or=42 empty value_or=7（都是 int）
```

What is stored inside the optional is plainly a reference, so when value_or is engaged, why doesn't it hand the reference back to us? Because there is a fundamental contradiction here. When the optional is engaged, you want a `T&` back; when it is empty, you want the default value back — and the default value is a temporary, so returning a reference to it would be a dangling reference. These two demands cannot be reconciled in a single return type.

The decision as it stands: value_or always returns a value. It is the safest choice — maybe not the most convenient, but at least it can never produce a dangling reference. Steve Downey's stance is clear: when you cannot make everyone happy, do the safest thing and come back to it later.

In real scenarios this restriction is genuinely inconvenient. Say you want to choose between an `optional<const Config&>` and a global config and return a reference — value_or is basically unusable there, and you just have to honestly write the if by hand:

```cpp
const Config& get_config(std::optional<const Config&> override) {
    if (override) return *override;
    return global_config;
}
```

Several proposals (including Steve Downey's own) are trying to generalize value_or so it can return the common reference type of `T` and `U`. This capability has only recently become expressible at the language level, and the library technique is still under construction. For now we live with the "always returns a value" version — safe, but a bit clumsy.

## Dangling Defense: delete Instead of requires

This is probably the decision in the whole design that I admire most. Picture this scenario: you construct an `optional<T&>` from a temporary, the temporary dies at the end of the expression, and the optional still holds a reference to it — a classic dangling reference.

```cpp
std::optional<int&> bad() {
    return std::optional<int&>(42);   // constructed from a temporary int; 42 dies immediately
}
```

In the past, code like this might "happen to work," because the compiler does not necessarily clean temporaries up right away. Push it to production with optimizations on, the compiler reclaims temporaries aggressively, and you get a bizarre memory problem that is hard even to reproduce.

The design principle is to check for dangling-ness. If a conversion would produce a temporary, and that temporary would die at the end of the expression, the overload is deleted outright rather than excluded from the overload set with a requires clause.

The difference between the two is critical. With requires, the compiler finds that the overload does not satisfy the constraint and keeps looking for other overloads — potentially dropping into a constructor you never expected and dumping a pile of incomprehensible errors. With `= delete`, the compiler tells you directly: this function is deleted. The error comes earlier, and it is clearer.

```cpp
// Design idea (simplified sketch)
template<typename U>
    requires (std::is_lvalue_reference_v<U>)   // only construct from lvalues
optional(optional<U&>);                         // requires: if unsatisfied, the compiler goes looking for other overloads

// versus
template<typename U>
optional(U&&) = delete;                         // delete: errors outright, no further lookup
```

What you wrote genuinely does not work, rather than being shoved by the compiler into some path that happens to compile. For debugging experience, that difference is enormous.

Worthy of special mention is the range-for fix. You used to write pipelines like this:

```cpp
for (auto& x : some_map | some_transform | another_transform) {
    // ...
}
```

If the middle of the pipeline produced a temporary, and some adapter returned an `optional<T&>` pointing at that temporary, the temporary could be dead before the for loop's very first iteration. C++23 fixed this: temporaries constructed in a range-for loop now live for the entire duration of the for loop. Admittedly this fix rules out a few cases that were safe before, but it forbids far more dangerous ones — on balance, a clear win.

## What Comes Next

Shallow const, conditional explicit, value_or, dangling defense — these are the traps at the "how do I use it correctly" level of `optional<T&>`. The next part changes angles again and looks at the territory where it crosses paths with move semantics. That is where C++ hides its most insidious bugs: one `std::move` in the wrong place, and you may well have "stolen someone else's cat."
