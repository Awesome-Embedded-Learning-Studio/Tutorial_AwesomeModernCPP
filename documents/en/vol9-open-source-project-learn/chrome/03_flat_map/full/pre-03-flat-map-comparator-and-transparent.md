---
chapter: 0
cpp_standard:
- 14
- 17
- 20
description: "Breaking down flat_map's comparator: the strict weak order contract, std::less vs the transparent std::less<>, and how transparent comparators use ConditionalT to dispatch heterogeneous lookups at compile time"
difficulty: intermediate
order: 3
platform: host
prerequisites:
- flat_map prerequisite (II): complexity and amortized analysis
reading_time_minutes: 10
related:
- flat_map prerequisite (V): NO_UNIQUE_ADDRESS + EBO + the pair type
- OnceCallback prerequisite (IV): concepts and requires constraints
tags:
- host
- cpp-modern
- intermediate
- 容器
- map
- 类型安全
title: "flat_map prerequisite (III): comparators, strict_weak_order, and transparent lookup"
---
# flat_map prerequisite (III): comparators, strict_weak_order, and transparent lookup

flat_map is an ordered container. "Ordered" deserves a follow-up, though: ordered by what? This piece breaks two things apart, and both come down to the comparator. First, a comparator isn't just any old `<` you scribble down; it has to satisfy a mathematical contract called strict weak order, or the container's sorting and lookup go wrong. Second, flat_map's default comparator is `std::less<>` (transparent), not `std::less<Key>` (opaque). That gap looks tiny, but on a hot lookup path it costs a whole malloc/free pair. Why modern C++ prefers `std::less<>` has its roots right here.

## The comparator: a function object that decides order

Look at flat_map's template signature, `flat_map<Key, Mapped, Compare = std::less<>, Container = ...>` (flat_map.h:190-193). The third template parameter, `Compare`, is the comparator: a function object that takes two arguments and returns whether the first should sort before the second.

The default is `std::less<>`, meaning "compare with `<`," so `flat_map<int, std::string>` ends up ordered by int ascending. You can pass your own, sorting by string length for instance:

```cpp
struct ByLength {
    bool operator()(const std::string& a, const std::string& b) const {
        return a.size() < b.size();
    }
};
flat_map<std::string, Config, ByLength> m;   // ordered by string length
```

But a comparator isn't a free-for-all. Behind it sits a mathematical contract.

## strict weak order: the comparator's mathematical contract

For a container to sort and find correctly, the comparator you hand it must satisfy strict weak order. Four properties; we'll go one at a time.

Irreflexive: `comp(a, a)` must be false, a cannot be less than itself. Antisymmetric: if `comp(a, b)` is true then `comp(b, a)` must be false. Transitive: if `comp(a, b)` and `comp(b, c)` hold, then `comp(a, c)` must hold. The first three are basically the properties of `<`, intuitive enough.

The one people actually miss is the fourth, intransitivity of incomparability. What's "incomparable"? It's `!comp(a,b) && !comp(b,a)`: a is not less than b, b is not less than a, so the two are "equivalent." The fourth rule says: if a and b are equivalent, and b and c are equivalent, then a and c must be equivalent too. This guarantees "equivalent" is a genuine equivalence relation, transitively closed, so the container can partition elements into equivalence classes and order them class by class.

We harp on the fourth because it's the one that breaks quietly. Take NaN in floating point: `NaN < x` and `x < NaN` are both false, so by the rule they're "equivalent," but two NaNs don't compare to each other either, intransitivity of incomparability blown right there. Or say you write a comparator with a tolerance, `abs(a-b) < eps` counts as equal: pick eps wrong or compare in an unstable order and "equal" stops being transitive, and the sort results get muddy. Elements can "disappear" during a find, or show up duplicated, and bugs like that are not fun to chase. C++20 codifies this contract as the `std::strict_weak_order` concept, so you can constrain your comparator with it and catch violations at compile time.

The one-liner: with `<` you don't worry; with a hand-rolled comparator, especially multi-field or tolerance-based, keep those four rules in your head.

## std::less vs std::less<>: opaque vs transparent

Here we hit a distinction that matters in modern C++. `std::less` has two faces. One is `std::less<Key>`, around since C++98, opaque, accepting only `Key`. The `operator()` on `std::less<std::string>` has signature `bool operator()(const std::string&, const std::string&)`, and it won't take anything else. The other is `std::less<>`, added in C++14, transparent, with a templated `operator()` that accepts any type and routes through `<` internally, which is why it's also called a transparent comparator.

One detail you might have glossed over: `std::map` defaults to `std::less<Key>` (opaque), while flat_map defaults to `std::less<>` (transparent) (flat_map.h:192). Same standard-library family, one conservative and one aggressive, and this right here is the root of it. The difference looks academic until you see what it does to lookup performance.

## Transparent lookup: skip the temporary

Say you have a `flat_map<std::string, Config>` and you want to look up the key `"timeout"`:

```cpp
flat_map<std::string, Config> m;
auto it = m.find("timeout");   // "timeout" is const char[8]
```

`"timeout"` is `const char[8]`, not `std::string`. If the comparator is `std::less<std::string>` (opaque), then `find`'s argument must be a `std::string`, and the container has no choice: it takes your `const char[8]`, constructs a temporary `std::string` (heap allocation, character copy), uses that temporary for the binary search, then destructs it. One lookup, a malloc/free pair for free.

If the comparator is `std::less<>` (transparent), the picture changes. `find` compares directly with `const char*`, because both `std::string` and `const char*` work with `<` (or the generic path through `std::less<void>::operator()`), so no temporary `std::string` gets built at all. That's the payoff of transparent lookup: one search, one temporary construction skipped.

For a light key like int, who cares. For a heavy key like `std::string` or a custom type, finding repeatedly on a hot path, those skipped temporaries add up. The first time this clicked for us was profiling a hot config path: the malloc top was a wall of `std::string` temporaries, all coming from map.find. Swap in a transparent comparator and that wall just vanishes. Stuck with me.

## How flat_map pulls off transparency: compile-time dispatch via KeyT

How does flat_map know whether the comparator is transparent? Through a compile-time type trait called `is_transparent`. A transparent comparator (like `std::less<>`) carries a nested `is_transparent` type, just an empty struct acting as a tag; an opaque one (like `std::less<int>`) has no such type. flat_tree uses a `KeyT<K>` alias to dispatch at compile time (flat_tree.h:109-111):

```cpp
template <typename K>
using KeyT = ConditionalT<
    requires { typename KeyCompare::is_transparent; },   // is the comparator transparent?
    K,                                                     // yes: keep K as the caller passed it
    Key>;                                                  // no: force fallback to Key
```

`ConditionalT` looks like `std::conditional_t`, but its arguments don't depend on each other, so it can be deduced normally. The logic in one line: if the comparator is transparent, `KeyT<K>` equals the K the caller passed in (say `const char*`); if it's opaque, `KeyT<K>` gets forced back to `Key` (say `std::string`).

So `find`'s signature tracks the comparator:

```cpp
// transparent comparator (std::less<>): accepts heterogeneous keys
template <class K = Key>
auto find(const KeyT<K>& key);   // KeyT<K> = K (transparent)

// opaque comparator (std::less<string>): accepts only Key
template <class K = Key>
auto find(const KeyT<K>& key);   // KeyT<K> = Key = string
```

The caller passes `const char*`: the transparent version eats it raw; the opaque version has to implicitly convert `const char*` into `std::string` (constructing a temporary) to match. All of this happens at compile time, with zero runtime cost. The part we find most elegant is exactly this: one `find` signature, behavior switched entirely by a nested type on the comparator at compile time, and the user's code doesn't change a line.

## KeyValueCompare: the heterogeneous-comparison internals

One layer deeper. How does the lower level take a heterogeneous key and compare it against an element storing a `pair<K,V>`? flat_tree's `KeyValueCompare` (flat_tree.h:439-462) handles this with two `extract_if_value_type` overloads as guards. If one side of the comparison is a `value_type` (i.e., `pair<K,V>`), it first runs `GetKeyFromValue` to pull out the key and compares that; if one side is a bare `K` (a heterogeneous key like `const char*`), it passes through as-is and compares directly.

With that, `lower_bound(data, "timeout", comp)` can compare a `const char*` directly against an array of `pair<std::string, Config>`, with no need to wrap `"timeout"` into a `pair` and no need to crack the whole value out of each element. This is the implementation detail that lets heterogeneous lookup land inside the binary-search loop. It looks unremarkable, but it's the overloads that iron out the mismatch between "a heterogeneous key" and "an array storing value_type."

## A minimal reproduction

Let's roll our own minimal version of a transparent comparator, to feel out the compile-time dispatch:

```cpp
// Platform: host | C++ Standard: C++20
#include <compare>
#include <concepts>
#include <iostream>
#include <string>

// transparent comparator (carries the is_transparent tag)
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
    std::cout << "TransparentLess transparent? " << is_transparent_v<TransparentLess, int> << "\n";  // true
    std::cout << "OpaqueLess     transparent? " << is_transparent_v<OpaqueLess, int> << "\n";        // false
    return 0;
}
```

Drop this `is_transparent_v` into that `ConditionalT` line above, and you have flat_tree's `KeyT` dispatch. The real code is at flat_tree.h:109-111, verbatim.

That wraps up flat_map's comparator. strict weak order is the mathematical foundation for correct sorting; `std::less<>` beats `std::less<Key>` by enabling heterogeneous-key lookup without constructing temporaries; flat_map defaults to the transparent route, using the `is_transparent` tag plus `ConditionalT` to do the dispatch at compile time and charge nothing at runtime.

flat_map hides another neat zero-cost trick: the `sorted_unique_t` tag, which skips sorting via tag dispatch. We pull that apart next.

## References

- [cppreference: std::less (including the transparent form)](https://en.cppreference.com/w/cpp/utility/functional/less)
- [cppreference: strict_weak_order (C++20 concept)](https://en.cppreference.com/w/cpp/concepts/strict_weak_order)
- [cppreference: is_transparent and heterogeneous lookup](https://en.cppreference.com/w/cpp/utility/functional/less_void)
- [Chromium `base/containers/flat_tree.h`: KeyT / KeyValueCompare](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/flat_tree.h)
