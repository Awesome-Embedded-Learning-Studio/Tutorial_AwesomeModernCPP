---
chapter: 0
cpp_standard:
- 11
- 17
- 20
description: "Breaking down flat_map's two memory optimizations — [[no_unique_address]]/EBO giving the empty comparator zero space overhead, and why flat_map stores pair<K,V> instead of std::map's pair<const K,V>"
difficulty: intermediate
order: 5
platform: host
prerequisites:
- 'flat_map prerequisite (III): comparators, strict_weak_order, and transparent lookup'
reading_time_minutes: 9
related:
- 'flat_map hands-on (II): the flat_tree core skeleton'
- 'WeakPtr prerequisite (VI): TRIVIAL_ABI and trivial relocatability'
tags:
- host
- cpp-modern
- intermediate
- 容器
- map
- 内存管理
- 零开销抽象
title: "flat_map prerequisite (V): NO_UNIQUE_ADDRESS, EBO, and pair storage"
translation:
  source: documents/vol9-open-source-project-learn/chrome/03_flat_map/full/pre-05-flat-map-enua-ebo-and-pair-storage.md
  source_hash: 5225cd0457fdc2b30283c0d6d57eecd4f53312400c417e1ee752c0eefcd6c756
  translated_at: '2026-09-26T02:55:34+00:00'
  engine: anthropic
  token_count: 4200
---
# flat_map prerequisite (V): NO_UNIQUE_ADDRESS, EBO, and pair storage

flat_map's class definition carries a few small annotations that are easy to skim right past on a first read. When we went through the code this time, we deliberately stopped to dig into them, and it turns out two small memory-level ideas are hiding behind them. One makes a stateless comparator (the default `std::less<>`, for instance) take not a single byte — courtesy of `[[no_unique_address]]`. The other changes the stored element from `pair<const K,V>` to `pair<K,V>`; a change that looks like nothing more than a dropped const actually decides which APIs flat_map can offer and which it cannot. This piece takes both apart.

## EBO: an empty object ought to cost zero bytes

C++ carries a piece of historical baggage that made us frown the first time we learned of it: an empty object — a class with no data members at all — still has to occupy at least 1 byte. The rule goes like this: two distinct objects must each occupy a distinct address, and addresses are counted in bytes, so a 0-byte object simply cannot have its own address. Hence the `sizeof` of a `struct Empty {}` is not 0 — it is 1.

```cpp
struct Empty {};
sizeof(Empty);   // 1 (not 0)
```

On its own, this doesn't hurt. It starts to hurt the moment you want to use an empty type as a member. The canonical case is a stateless comparator such as `std::less<>` — there is nothing inside it, it is purely a call wrapper. If you declare it as a member, by the book:

```cpp
struct Holder {
    std::less<> comp;   // empty object, yet takes 1 byte (+ alignment padding)
    int data;
};
sizeof(Holder);   // 8 bytes (1 byte comp + 3 bytes padding + 4 bytes data)
```

In theory `comp` is zero-overhead (it holds no data at all), yet in practice it takes 1 byte and drags alignment padding along with it — 4 bytes of pointless bloat.

EBO (Empty Base Optimization) is the escape hatch reserved for the "empty class as base class" scenario: as long as the empty class sits in the base-class position, the compiler is allowed to let it share an address with the derived class, and its footprint drops to zero:

```cpp
struct Empty {};
struct Holder : Empty {   // empty class as base
    int data;
};
sizeof(Holder);   // 4 bytes (Empty optimized away)
```

The standard library's containers all rely on this trick to keep empty allocators and empty comparators from costing memory — they inherit from them as base classes instead of tucking them in as members. But EBO has a boundary: it only works for base classes, not for members. Write the empty object as a member and it costs what it costs.

---

## [[no_unique_address]]: extending EBO to members

C++20 loosened that boundary. The new `[[no_unique_address]]` attribute tells the compiler: this member does not need a unique address of its own, so if its type is empty, don't allocate any space for it. In other words, what EBO did for base classes now extends to members:

```cpp
struct Empty {};
struct Holder {
    [[no_unique_address]] Empty comp;   // annotated, the empty member can be zero bytes
    int data;
};
sizeof(Holder);   // 4 bytes (comp EBO'd away)
```

Chromium doesn't use the attribute raw; it wraps it in a macro, `NO_UNIQUE_ADDRESS` — on compilers that support the attribute it expands to `[[no_unique_address]]`, otherwise to nothing. That way the code doesn't need conditional compilation everywhere.

flat_tree hangs this macro in two places, both holding comparators. One is flat_tree's own member `key_compare comp_` (flat_tree.h:545), where the default `std::less<>` collapses to zero; the other is inside the nested `value_compare` (flat_tree.h:129), same story. So when you write `flat_map<int,int> m;`, the comparator inside takes not one byte — the container pays memory only for the `vector<pair<K,V>>` that actually stores data, and the comparator rides for free.

---

## GCC vs Clang: equally correct EBO for empty types

A claim you often hear online is that `[[no_unique_address]]` works better on Clang than GCC. That is not entirely wrong — in certain non-empty but overlap-capable scenarios (several NUA members of the same class, say) the two do differ. But in the scenario flat_map actually cares about — an empty type as a member — our measurements show the two behave identically: GCC 16 and Clang 22 both dutifully fold it to 0 bytes.

```cpp
struct Empty {};
struct WithNUA  { [[no_unique_address]] Empty e; int i; };
struct WithoutNUA { Empty e; int i; };
// Measured on GCC 16 / Clang 22:
// sizeof(WithNUA)   = 4 bytes (e optimized away)
// sizeof(WithoutNUA) = 8 bytes (e takes 1 + padding 3 + i takes 4)
```

So flat_map's empty comparator gets EBO on both GCC and Clang; don't let the popular claim lead you astray.

What the comment block at flat_tree.h:542-547 points at, though, is a real GCC pitfall that has nothing to do with semantics ([crbug.com/1156268](https://crbug.com/1156268)): under particular member declaration orderings, GCC outright emits an ICE (an internal compiler error) — not a difference in EBO folding behavior, the compiler itself crashes. Chromium's workaround is to shift where `comp_` is declared. That is a compiler-implementation bug with zero relation to the language's semantics; when teaching, don't mix it up with "NUA semantic differences."

---

## pair<K,V> vs pair<const K,V>: flat_map's storage choice

The second design point cuts deeper — it reshapes what flat_map's API even looks like. Take flat_map's template signature (flat_map.h:193):

```cpp
template <class Key, class Mapped, class Compare = std::less<>,
          class Container = std::vector<std::pair<Key, Mapped>>>
class flat_map : ...;
```

`Container` defaults to `std::vector<std::pair<Key, Mapped>>` — look closely at that `pair`: it is `pair<Key, Mapped>`, **not** `pair<const Key, Mapped>`. This is the exact opposite of `std::map`, whose elements are `pair<const Key, Mapped>` — once a key enters the node, it can never be modified again.

Why does flat_map take the non-const road? The root is that the underlying container is a vector. To keep a vector ordered, there is no getting around the shifts in `insert`/`erase` — `std::move_backward` hauling elements in bulk — and that requires the element type to be move-assignable. But in a `pair<const Key, Mapped>`, `first` is const and cannot be move-assigned:

```cpp
// Measured (C++17):
static_assert(!std::is_move_assignable_v<std::pair<const int, int>>);   // not move-assignable → vector shift impossible
static_assert( std::is_move_assignable_v<std::pair<int, int>>);        // move-assignable
```

`pair<const K, V>` is not move-assignable, and that seals off the vector's shift path. For flat_map to survive, it has no choice but the non-const `pair<K,V>`.

There is a spot where it is easy to get confused — it misled us at first, too. The overwrite in `insert_or_assign` writes `result.first->second = std::forward<M>(obj)` (flat_map.h:339), touching **only `.second`**, so even a `pair<const K,V>` would compile there — only `.first` is const. What actually kills the const pair is not the overwrite but the vector's shift: it must move-assign the whole pair, and `pair<const K,V>` cannot deliver that. Don't cite the overwrite as the reason; it is not the root of the disease.

The cost side deserves equal clarity. Since what is stored is `pair<K,V>`, `first` is non-const, so the key can in theory be modified through an iterator. Casually write `it->first = new_key` and you have broken the sorted invariant, and the container has no idea. This is purely on user discipline — the same nature as [WeakPtr's sequence contract](../../02_weak_ptr/full/pre-03-weak-ptr-sequence-checker-dcheck-check.md): not enforced in release builds, so you had better know what you are doing.

`std::map` has no such worry on its road: `pair<const K,V>` makes the key immutable by construction. Its capital comes from the node-based container model — no shifts, no assignments, just pointer rewiring. Each road buys one end of the trade; neither eats for free.

---

## A minimal reproduction: verifying EBO and the pair type

```cpp
// Platform: host | C++ Standard: C++20
#include <iostream>
#include <type_traits>
#include <utility>

struct EmptyLess {
    using is_transparent = void;
    template <typename A, typename B>
    bool operator()(A&& a, B&& b) const { return a < b; }
};

struct WithNUA    { [[no_unique_address]] EmptyLess c; int i; };
struct WithoutNUA { EmptyLess c; int i; };

int main() {
    std::cout << "sizeof(WithNUA)    = " << sizeof(WithNUA)    << " (NUA 折叠空成员)\n";
    std::cout << "sizeof(WithoutNUA) = " << sizeof(WithoutNUA) << " (空成员占 1 + 填充)\n";
    std::cout << "pair<const int,int> 可 move-assign? "
              << std::is_move_assignable_v<std::pair<const int, int>> << " (false = 不可,所以 flat_map 不存它)\n";
    return 0;
}
```

Measured output (GCC 16 / Clang 22): `WithNUA=4`, `WithoutNUA=8`, and the `pair<const int,int>` move-assignable check prints false. Two numbers plus one false, and the rationale behind those two flat_map design choices falls neatly into place.

That completes the prerequisites (the pre-00 intro plus the five parts pre-01..05). Next stop is the hands-on work: putting together the core skeleton of flat_tree.

## References

- [cppreference: [[no_unique_address]]](https://en.cppreference.com/w/cpp/language/attributes/no_unique_address)
- [cppreference: Empty Base Optimization](https://en.cppreference.com/w/cpp/language/ebo)
- [Chromium `base/containers/flat_tree.h` — NO_UNIQUE_ADDRESS and the member declaration comments](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/flat_tree.h)
- [crbug.com/1156268 — GCC member declaration order ICE](https://crbug.com/1156268)
