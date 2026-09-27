---
chapter: 0
cpp_standard:
- 14
- 17
- 20
description: "Breaking down flat_map's comparator: the strict weak order requirements, std::less vs the transparent std::less<>, and how a transparent comparator uses ConditionalT compile-time dispatch to enable heterogeneous lookup"
difficulty: intermediate
order: 3
platform: host
prerequisites:
- 'flat_map prerequisite (II): complexity and amortized analysis'
reading_time_minutes: 10
related:
- 'flat_map prerequisite (V): NO_UNIQUE_ADDRESS, EBO, and pair storage'
- 'OnceCallback prerequisite (IV): Concepts and requires constraints'
tags:
- host
- cpp-modern
- intermediate
- 容器
- map
- 类型安全
title: "flat_map prerequisite (III): comparators, strict_weak_order, and transparent lookup"
translation:
  source: documents/vol9-open-source-project-learn/chrome/03_flat_map/full/pre-03-flat-map-comparator-and-transparent.md
  source_hash: 66a6e95c93b538f162ec1c2311fd47c4f6fdf27ea4594e1fbfde59a3f470a04f
  translated_at: '2026-09-26T02:48:32+00:00'
  engine: anthropic
  token_count: 4600
---
# flat_map prerequisite (III): comparators, strict_weak_order, and transparent lookup

flat_map is an ordered container. But "ordered" begs one follow-up — ordered by what? This article takes two things apart, and both come down to the comparator. First, a comparator is not just any `<` you scribble down; it has to satisfy a mathematical contract called strict weak order, or the container's sorting and lookup can both go wrong. Second, flat_map's default comparator is `std::less<>` (transparent), not `std::less<Key>` (opaque). That gap looks tiny, but on a hot lookup path it can cost a whole malloc/free pair — and the reason modern C++ favors `std::less<>` is rooted right here.

## The comparator: a function object that decides order

First, look at flat_map's template signature, `flat_map<Key, Mapped, Compare = std::less<>, Container = ...>` (flat_map.h:190-193). The third template parameter, `Compare`, is the comparator — in essence a function object that takes two arguments and returns whether the first should be ordered before the second.

The default is `std::less<>`, which means comparing with `<`; a `flat_map<int, std::string>` orders by int ascending out of the box. You can pass your own instead — sorting by string length, say:

```cpp
struct ByLength {
    bool operator()(const std::string& a, const std::string& b) const {
        return a.size() < b.size();
    }
};
flat_map<std::string, Config, ByLength> m;   // ordered by string length
```

But a comparator is not something you get to write however you like — a mathematical contract governs it.

## strict weak order: the comparator's mathematical contract

For the container to sort correctly and find correctly, the comparator you hand it must satisfy strict weak order. Four properties; let's go through them one at a time.

Irreflexive: `comp(a, a)` must be false — a cannot be less than itself. Antisymmetric: when `comp(a, b)` is true, `comp(b, a)` must be false. Transitive: when `comp(a, b)` and `comp(b, c)` hold, `comp(a, c)` must hold too. The first three are just the properties of `<`, intuitive enough to accept.

The one that actually gets missed is the fourth: transitivity of incomparability. What does "incomparable" mean? It's `!comp(a,b) && !comp(b,a)` — a is not less than b, b is not less than a, so the two count as "equivalent". The fourth rule demands: if a is equivalent to b, and b is equivalent to c, then a must be equivalent to c as well. This is what guarantees "equivalent" is a genuine equivalence relation, closed under transitivity, so the container can partition elements into equivalence classes and order them class by class.

We stress the fourth because it is the easiest one to violate without noticing. Take NaN in floating-point comparisons: `NaN < x` and `x < NaN` are both false, so by the rule they are "equivalent" — yet two NaNs don't compare against each other either, and transitivity of incomparability breaks right there. Or say you write a comparator with a tolerance, where `abs(a-b) < eps` counts as equal: pick eps badly, or compare in an unstable order, and the "equal" relation stops being transitive, and the sort results turn to mush. Elements can "vanish" during a find, or show up duplicated — bugs like that are no fun to chase. C++20 codified this contract into the `std::strict_weak_order` concept, so you can constrain your comparator with it and get violations caught at compile time.

One line to take away: comparing with `<` needs no worrying; write your own comparator — especially a multi-field or tolerance-based one — and those four rules had better be in your head.

## std::less vs std::less<>: opaque vs transparent

Here we step into an important modern C++ distinction. `std::less` has two faces. One is `std::less<Key>`, around since C++98: opaque, accepting only the single type `Key` — the `operator()` of `std::less<std::string>` has the signature `bool operator()(const std::string&, const std::string&)`, and anything else you pass it doesn't count. The other is `std::less<>`, added in C++14: transparent, with a templated `operator()` that accepts any type and routes through `<` internally — which is why it's also called a transparent comparator.

One detail you may not have noticed: `std::map` defaults to `std::less<Key>`, opaque, while flat_map defaults to `std::less<>`, transparent (flat_map.h:192). Same standard-library family, one side conservative and the other aggressive — and this is where it starts. The difference looks inconsequential at first glance, yet its impact on lookup performance is very real.

## Transparent lookup: skip building the temporary

Suppose you have a `flat_map<std::string, Config>` in hand and want to look up the key `"timeout"`:

```cpp
flat_map<std::string, Config> m;
auto it = m.find("timeout");   // "timeout" is a const char[8]
```

That `"timeout"` is a `const char[8]`, not a `std::string`. If the comparator is `std::less<std::string>` (opaque), then `find`'s argument must be a `std::string`, and the container has no way out: it takes your `const char[8]`, constructs a temporary `std::string` — allocating heap memory, copying characters — runs the binary search against that temporary, and then destructs it. One lookup, a malloc/free pair paid for nothing.

If the comparator is `std::less<>` (transparent), the picture changes. `find` compares with the `const char*` directly, because both `std::string` and `const char*` work with `<` (or the generic path through `std::less<void>::operator()`), so no temporary `std::string` gets built at all. That's the value of transparent lookup — one search, one temporary construction saved.

For a light key like int, it doesn't matter. For a heavy key like `std::string` or a custom type, with find called repeatedly on a hot path, the saved temporary constructions accumulate into a sizable number. The first time this really hit us was profiling a hot configuration path: the malloc list was a whole wall of `std::string` temporaries, every one of them from map.find. Switch to a transparent comparator, and that wall simply vanishes. It left a deep impression.

## How flat_map implements transparency: compile-time dispatch via KeyT

So how does flat_map know whether the comparator is transparent? Through a compile-time type trait named `is_transparent`. A transparent comparator (like `std::less<>`) carries a nested type `is_transparent` — just an empty struct serving as a marker; an opaque one (like `std::less<int>`) has no such type. flat_tree uses the `KeyT<K>` alias to dispatch at compile time (flat_tree.h:109-111):

```cpp
template <typename K>
using KeyT = ConditionalT<
    requires { typename KeyCompare::is_transparent; },   // is the comparator transparent?
    K,                                                     // transparent: keep the K the caller passed in
    Key>;                                                  // opaque: force fallback to Key
```

`ConditionalT` looks like `std::conditional_t`, but its arguments don't depend on each other, so deduction works normally. The logic in one sentence: with a transparent comparator, `KeyT<K>` is exactly the `K` the caller passed in, `const char*` say; with an opaque comparator, `KeyT<K>` gets bent back to `Key` by force, `std::string` say.

So `find`'s signature follows the comparator:

```cpp
// transparent comparator (std::less<>): heterogeneous keys accepted
template <class K = Key>
auto find(const KeyT<K>& key);   // KeyT<K> = K (transparent)

// opaque comparator (std::less<string>): Key only
template <class K = Key>
auto find(const KeyT<K>& key);   // KeyT<K> = Key = string
```

The caller passes a `const char*`: the transparent version takes it in as-is; the opaque version has to implicitly convert the `const char*` into a `std::string` (constructing a temporary) before the call matches. All of this is settled at compile time, with zero runtime cost. To us this is the most beautiful part of the design — one `find` signature, its behavior switched entirely at compile time by a single nested type on the comparator, and the user's code doesn't change a line.

## KeyValueCompare: the implementation details of heterogeneous comparison

One layer deeper. How does the lower level take a heterogeneous key and compare it against an element that stores a `pair<K,V>`? flat_tree's `KeyValueCompare` (flat_tree.h:439-462) handles this with two `extract_if_value_type` overloads standing guard. If one side of the comparison is a `value_type` (that is, `pair<K,V>`), it first goes through `GetKeyFromValue` to dig the key out, and compares that; if one side is a bare `K` (a heterogeneous key, `const char*` for example), it passes through untouched and gets compared directly.

With that in place, `lower_bound(data, "timeout", comp)` can take a `const char*` straight to an array storing `pair<std::string, Config>` — no wrapping `"timeout"` into a `pair`, and no cracking the whole value out of each element. This is the implementation detail that lets heterogeneous lookup land inside the binary-search loop. It looks unremarkable, but with one layer of overloads it smooths over the two things that seem not to line up: "a heterogeneous key" and "an array storing value_type".

## A minimal reproduction

Let's roll our own minimal version of transparent comparison and get a feel for the compile-time dispatch:

```cpp
// Platform: host | C++ Standard: C++20
#include <compare>
#include <concepts>
#include <iostream>
#include <string>

// transparent comparator (carries the is_transparent marker)
struct TransparentLess {
    using is_transparent = void;   // the key: marks transparency
    template <typename A, typename B>
    bool operator()(A&& a, B&& b) const { return std::forward<A>(a) < std::forward<B>(b); }
};

// opaque comparator (no is_transparent)
struct OpaqueLess {
    bool operator()(const std::string& a, const std::string& b) const { return a < b; }
};

template <typename Comp, typename K>
constexpr bool is_transparent_v = requires { typename Comp::is_transparent; };

int main() {
    std::cout << std::boolalpha;
    std::cout << "TransparentLess 透明? " << is_transparent_v<TransparentLess, int> << "\n";  // true
    std::cout << "OpaqueLess     透明? " << is_transparent_v<OpaqueLess, int> << "\n";        // false
    return 0;
}
```

Drop this `is_transparent_v` into the `ConditionalT` line from earlier, and you have flat_tree's `KeyT` dispatch. The real code is at flat_tree.h:109-111, character for character.

With that, the comparator side of flat_map is fully taken apart. strict weak order is the mathematical foundation of correct sorting; `std::less<>` beats `std::less<Key>` by looking things up with heterogeneous keys and constructing no temporary objects; flat_map defaults to the transparent route, finishing the dispatch at compile time with the `is_transparent` marker plus `ConditionalT`, and charging nothing at runtime.

flat_map hides another rather clever zero-cost construction — the `sorted_unique_t` tag, which skips sorting via tag dispatch. We'll take that apart in the next piece.

## References

- [cppreference: std::less (including the transparent form)](https://en.cppreference.com/w/cpp/utility/functional/less)
- [cppreference: strict_weak_order (C++20 concept)](https://en.cppreference.com/w/cpp/concepts/strict_weak_order)
- [cppreference: is_transparent and heterogeneous lookup](https://en.cppreference.com/w/cpp/utility/functional/less_void)
- [Chromium `base/containers/flat_tree.h` — KeyT/KeyValueCompare](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/flat_tree.h)
