---
chapter: 0
cpp_standard:
- 11
- 17
- 20
description: "Two memory optimizations behind flat_map: [[no_unique_address]]/EBO lets a stateless comparator cost zero bytes, and the storage is pair<K,V> rather than std::map's pair<const K,V>"
difficulty: intermediate
order: 5
platform: host
prerequisites:
- flat_map prerequisite (III): comparators, strict_weak_order, and transparent lookup
reading_time_minutes: 9
related:
- flat_map in practice (II): the flat_tree core skeleton
- WeakPtr prerequisite (VI): TRIVIAL_ABI and trivially relocatable
tags:
- host
- cpp-modern
- intermediate
- 容器
- map
- 内存管理
- 零开销抽象
title: "flat_map prerequisite (V): NO_UNIQUE_ADDRESS, EBO, and pair storage"
---
# flat_map prerequisite (V): NO_UNIQUE_ADDRESS, EBO, and pair storage

The flat_map class definition has a few small annotations that are easy to skim past on a first read. When we went through the code this time, we stopped to dig into them and found two small memory-level ideas hiding behind them. One lets a stateless comparator (like the default `std::less<>`) cost zero bytes, thanks to `[[no_unique_address]]`. The other swaps the stored element type from `pair<const K,V>` to `pair<K,V>`. That looks like just dropping a `const`, but it actually decides which APIs flat_map can offer and which it can't. We'll take the two apart in this piece.

## EBO: an empty object should be zero bytes

C++ has a piece of historical baggage that made us frown the first time we learned it: an empty object, meaning a class with no data members at all, still has to occupy at least 1 byte. The rule goes like this. Two distinct objects must each occupy their own address, and addresses are counted in bytes, so a 0-byte object can't have an address of its own. That's why `sizeof` of a `struct Empty {}` isn't 0, it's 1.

```cpp
struct Empty {};
sizeof(Empty);   // 1 (not 0)
```

On its own that doesn't hurt. It bites the moment you want to use an empty type as a member. The textbook case is a stateless comparator like `std::less<>`, which holds nothing inside and is purely a call wrapper. If you make it an honest member:

```cpp
struct Holder {
    std::less<> comp;   // empty object, but takes 1 byte (+ alignment padding)
    int data;
};
sizeof(Holder);   // 8 bytes (1 byte comp + 3 bytes padding + 4 bytes data)
```

`comp` is theoretically zero-cost (it has no data at all), yet it eats 1 byte and drags alignment padding along with it. Four bytes of pure bloat.

EBO (Empty Base Optimization) is the escape hatch C++ leaves for the "empty class as a base" case: as long as the empty class sits in the base position, the compiler may let it share an address with the derived class, and its cost drops to zero:

```cpp
struct Empty {};
struct Holder : Empty {   // empty class as a base
    int data;
};
sizeof(Holder);   // 4 bytes (Empty optimized away)
```

Every standard container leans on this trick to keep empty allocators and empty comparators from costing memory. They inherit them as bases instead of stuffing them in as members. But EBO has a boundary: it only kicks in for bases, not for members. Write the empty object as a member and it still pays the byte.

---

## [[no_unique_address]]: extending EBO to members

C++20 loosens that boundary. The new `[[no_unique_address]]` attribute tells the compiler: this member doesn't need its own address, so if it's an empty type, don't allocate space for it. In other words, EBO moves off the base and onto the member:

```cpp
struct Empty {};
struct Holder {
    [[no_unique_address]] Empty comp;   // annotated, empty member can be zero bytes
    int data;
};
sizeof(Holder);   // 4 bytes (comp EBO'd away)
```

Chromium doesn't use the attribute raw. It wraps it in the `NO_UNIQUE_ADDRESS` macro, which expands to `[[no_unique_address]]` when the compiler supports it and to nothing otherwise. That keeps conditional compilation out of every call site.

flat_tree hangs this macro in two spots, both holding a comparator. One is the `key_compare comp_` member of `flat_tree` itself (flat_tree.h:545), where the default `std::less<>` folds to zero. The other sits inside the nested `value_compare` (flat_tree.h:129), same story. So when you write `flat_map<int,int> m;`, the comparator inside costs not a single byte. The container only pays memory for the `vector<pair<K,V>>` that actually holds data. The comparator is free.

---

## GCC vs Clang: equivalent and correct for empty-type EBO

You'll often hear online that Clang handles `[[no_unique_address]]` better than GCC. It's not entirely wrong. In some non-empty-but-overlapping cases (say, several NUA members of the same type in one class) the two really do differ. But on the case flat_map actually cares about, an empty type as a member, we tested both and they behave identically. GCC 16 and Clang 22 both fold it down to 0 bytes.

```cpp
struct Empty {};
struct WithNUA  { [[no_unique_address]] Empty e; int i; };
struct WithoutNUA { Empty e; int i; };
// Measured on GCC 16 / Clang 22:
// sizeof(WithNUA)   = 4 bytes (e optimized away)
// sizeof(WithoutNUA) = 8 bytes (e takes 1 + padding 3 + i takes 4)
```

So flat_map's empty comparator gets EBO on both GCC and Clang. Don't let that popular claim lead you astray.

What the comment at flat_tree.h:542-547 actually points at is a real GCC bug that has nothing to do with semantics ([crbug.com/1156268](https://crbug.com/1156268)): under a particular member declaration order, GCC just emits an ICE (internal compiler error). It's not that the EBO folding differs, it's that the compiler crashed. Chromium's workaround is to shuffle where `comp_` is declared. That's a compiler implementation bug with zero to do with language semantics, so don't conflate it with "NUA semantic differences" when you explain it.

---

## pair<K,V> vs pair<const K,V>: flat_map's storage choice

The second design point hits harder. It reshapes what flat_map's API even looks like. Look at flat_map's template signature (flat_map.h:193):

```cpp
template <class Key, class Mapped, class Compare = std::less<>,
          class Container = std::vector<std::pair<Key, Mapped>>>
class flat_map : ...;
```

`Container` defaults to `std::vector<std::pair<Key, Mapped>>`. Read that `pair` carefully: it's `pair<Key, Mapped>`, **not** `pair<const Key, Mapped>`. That's the exact opposite of `std::map`, whose elements are `pair<const Key, Mapped>`. Once a key lands in a `std::map` node, you can't touch it.

Why does flat_map take the non-const route? The root is that the backing container is a vector. A vector that wants to stay sorted can't avoid the shift inside `insert`/`erase`, where `std::move_backward` relocates a whole range, and that requires the element type to be move-assignable. But the `first` of a `pair<const Key, Mapped>` is const, so it can't be move-assigned:

```cpp
// Measured (C++17):
static_assert(!std::is_move_assignable_v<std::pair<const int, int>>);   // not move-assignable → vector shift is impossible
static_assert( std::is_move_assignable_v<std::pair<int, int>>);        // move-assignable
```

`pair<const K, V>` isn't move-assignable, and that seals off the vector's shift path. If flat_map wants to function at all, it has to pick the non-const `pair<K,V>`.

There's a spot here that's easy to get wrong, and we got dragged astray by it at first. The overwrite in `insert_or_assign` writes `result.first->second = std::forward<M>(obj)` (flat_map.h:339), which **only touches `.second`**. Even if you switched to `pair<const K,V>` it would still compile, because only `.first` is const. So the thing that actually kills the const pair isn't the overwrite. It's the vector shift, which has to move-assign the whole pair, and `pair<const K,V>` can't deliver that. Don't cite the overwrite as the reason. It isn't the root cause.

The cost side needs to be said plainly. Since the storage is `pair<K,V>`, `first` is non-const and the key is in principle mutable through an iterator. If you casually write `it->first = new_key`, you've broken the sorted invariant and the container has no idea. This is entirely on the user's discipline, same flavor as [WeakPtr's sequence contract](../../02_weak_ptr/full/pre-03-weak-ptr-sequence-checker-dcheck-check.md). Release builds don't enforce it. You have to keep your own head straight.

`std::map` doesn't have this worry on its road, because `pair<const K,V>` makes the key unmodifiable by construction. Its luxury comes from the node-based container model: no shift, no assignment, just reconnecting pointers. The two designs each pick one end of the tradeoff. Neither is free.

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
    std::cout << "sizeof(WithNUA)    = " << sizeof(WithNUA)    << " (NUA folds the empty member)\n";
    std::cout << "sizeof(WithoutNUA) = " << sizeof(WithoutNUA) << " (empty member takes 1 + padding)\n";
    std::cout << "pair<const int,int> move-assignable? "
              << std::is_move_assignable_v<std::pair<const int, int>> << " (false = no, which is why flat_map doesn't store it)\n";
    return 0;
}
```

Measured output (GCC 16 / Clang 22): `WithNUA=4`, `WithoutNUA=8`, `pair<const int,int> move-assignable? false`. Two numbers and one false, and the reasoning behind those two flat_map design choices more or less lands on the ground.

That wraps up the prerequisites (pre-00 intro plus pre-01..05, five building blocks). Next stop is the hands-on track, where we put together flat_tree's core skeleton.

## References

- [cppreference: [[no_unique_address]]](https://en.cppreference.com/w/cpp/language/attributes/no_unique_address)
- [cppreference: Empty Base Optimization](https://en.cppreference.com/w/cpp/language/ebo)
- [Chromium `base/containers/flat_tree.h`: NO_UNIQUE_ADDRESS and member declaration comments](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/flat_tree.h)
- [crbug.com/1156268: GCC member declaration order ICE](https://crbug.com/1156268)
