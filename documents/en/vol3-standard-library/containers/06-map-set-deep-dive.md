---
chapter: 7
cpp_standard:
- 11
- 14
- 17
- 20
description: 'Explains std::map and set all the way down to their red-black tree implementation:
  O(log n) complexity with stable iterators, heterogeneous lookup through the C++14
  transparent comparator, and C++17 node handles (extract/merge) — the only legitimate
  way to change a key.'
difficulty: intermediate
order: 6
platform: host
prerequisites:
- 'Deep Dive into vector: Three Pointers, Reallocation, and Iterator Invalidation'
reading_time_minutes: 15
related:
- 'Container Selection Guide: Picking the Right One by Operations, Memory, and Invalidation Rules'
tags:
- host
- cpp-modern
- intermediate
- map
- 容器
title: 'Deep Dive into map and set: Red-Black Trees, Heterogeneous Lookup, and Node Handles'
translation:
  source: documents/vol3-standard-library/containers/06-map-set-deep-dive.md
  source_hash: 2a8c7d7f183542ad3514ba8de981bf4081655fa1bc3db3ce1ae08e4147f09ba4
  translated_at: '2026-09-26T02:12:25+00:00'
  engine: anthropic
  token_count: 8300
---
# Deep Dive into map and set: Red-Black Trees, Heterogeneous Lookup, and Node Handles

## Family Portrait: map, set, and Their Siblings

We have used `std::map` and `std::set` countless times — day to day it is `insert`, `find`, iterate, nothing mysterious. But peel back just one layer and you will find a red-black tree hiding under both of them — and here is the twist: the Standard never actually named the red-black tree as the requirement. The three major standard library implementations all converged on it independently. On top of that, C++14 fitted them with heterogeneous lookup, and C++17 squeezed in a node handle that lets you relocate elements with zero copies — and, as a bonus, change that key that was supposed to be const. In this article we will comb through map and set in one pass, from the underlying machinery all the way up to modern usage.

First, meet the whole family. The ordered associative containers are four blood brothers, all grown on the same red-black tree:

| Container | What it stores | Key uniqueness |
|------|--------|-----------|
| `map` | key → value pairs | unique |
| `multimap` | key → value pairs | duplicates allowed |
| `set` | keys only | unique |
| `multiset` | keys only | duplicates allowed |

The relationship between map and set is really that simple: set is just a map that threw away the value and kept only the key — the underlying node structure, the balancing logic, and the iterator rules are all identical. So this article follows map as its main thread: everything map has, set has too, and the entire difference boils down to one sentence — "set does not store a value".

As for the border with the neighbors next door, one sentence is enough: if you want "ordered + logarithmic lookup", use `map`/`set` (red-black tree); if you want "unordered + amortized constant lookup", use `unordered_map`/`unordered_set` (hash table); if you want "ordered + contiguous storage (cache-friendly)", step up to C++23's `flat_map`. Three routes, each minding its own lane — and this article only minds the red-black tree lane.

## A Red-Black Tree Hides Underneath: The Standard Never Named It, but All Three Implementations Chose It

The Standard's demands on map are actually quite restrained: elements sorted by key, and lookup, insertion, and erasure all at logarithmic complexity O(log n). As for which data structure you use to deliver that, the Standard stays vague — roughly "a balanced binary search tree", with no specific one mandated. And that is exactly where it gets interesting: libstdc++ (GCC), libc++ (Clang), and MSVC STL all ended up choosing the red-black tree.

Why a red-black tree rather than the more "strictly balanced" AVL tree? The crux is erasure. An AVL tree requires the heights of the left and right subtrees to differ by no more than 1 — tight balancing, at the price that an erasure may have to rotate all the way from the bottom to the top, with a count that is hard to pin down. The red-black tree loosens the leash: it only promises "the longest path is at most twice the shortest", and in exchange insertion rotates at most 2 times and erasure at most 3 — the rotation count has an explicit ceiling, which is the better deal for a map that keeps inserting and erasing.

The rules of a red-black tree are just a handful — let us breeze through them (no need to memorize anything; just grasp why they buy you O(log n)):

- Every node is either red or black
- The root is black
- nil leaves (the empty sentinels) are black
- A red node's children must be black (no two reds glued together)
- From any node down to each of its leaves, every path passes through the same number of black nodes (this is called the "black height")

The last two rules together have this effect: you cannot make a path both long and all-red, because reds cannot sit in a row and the black height has to agree. So the longest red-black-alternating path is at most twice the shortest all-black path — the tree height is pressed down to O(log n), and lookup is O(log n) along with it.

What does a node look like? Compared to an ordinary binary search tree, it just gains one color bit and three pointers:

```cpp
// Simplified skeleton of a red-black tree node (standard library internals; details vary by vendor — structure only)
struct TreeNode {
    bool      is_red;    // the color bit
    TreeNode* parent;    // parent pointer (needed for bottom-up fixups)
    TreeNode* left;
    TreeNode* right;
    // a map node stores pair<const Key, Value> here; a set node stores only Key
};
```

That parent pointer deserves one more word. Lookup in an ordinary binary search tree only walks downward and never needs to know the father; but when a red-black tree inserts or erases, it has to adjust colors and perform rotations bottom-up, so it must be able to turn back and find the parent — hence every node carries a parent pointer. This also explains why a red-black tree node is "heavier" than an ordinary list node — it forks three ways. set is completely isomorphic to map here; the only difference is whether that Value sits in the node payload. So for every map mechanism we cover from here on, erase the Value in your head and you have set.

## Complexity and Iterator Invalidation: A Completely Different Rulebook from vector

First let us settle the complexity bill. The red-black tree is O(log n) tall, so lookup, insertion, and erasure each make one walk down the tree, plus possible rotations (a rotation itself is an O(1) local operation). The complexity of the common operations:

| Operation | Complexity |
|------|--------|
| `find` / `count` / `contains` / `operator[]` / `at` | O(log n) |
| `insert` / `emplace` / `erase` | O(log n) |
| Ordered traversal | O(n) |

What deserves to be singled out here is not the complexity — if the red-black tree is a touch slower, so be it, that is normal — but **iterator invalidation**. map's invalidation rules and vector's are two entirely different rulebooks, and that happens to be one hard engineering reason to pick map over vector.

We covered vector in [that article](03-vector-deep-dive.md): once it reallocates, every iterator, reference, and pointer goes stale, because the storage underneath is contiguous and the whole thing moves house. map is different — its elements hang off independent tree nodes:

- **Insertion**: invalidates no existing iterator, reference, or pointer
- **Erasure**: invalidates only the iterator/reference of the erased element itself; every other element stays exactly where it was

What does that mean? It means the address of an element in a map is stable. You can pass a pointer or reference to a map element around wherever you like, and as long as you never erase that element, the pointer stays valid forever. Even if you insert a few thousand more elements into the map, or erase a few hundred others, the pointer in your hand still points at the very same element.

This property is worth serious money in engineering. Suppose you write an event registry: after each callback is checked into the map, you want to hand its pointer to other subsystems to hold on to and to deregister with — with a vector, one reallocation smashes every one of those pointers into dangling ones; with a map, everything sits tight.

Let us run a small example and see this stability with our own eyes:

```cpp
#include <iostream>
#include <map>
#include <string>

int main()
{
    std::map<int, std::string> registry;
    registry[1] = "alpha";
    registry[2] = "beta";

    // Grab a reference and an iterator to element 1
    std::string& ref = registry.at(1);
    auto it = registry.find(1);

    // Frantically insert a pile of new elements, triggering repeated red-black tree rebalancing
    for (int i = 100; i < 200; ++i) {
        registry[i] = "x";
    }

    // Then erase some unrelated elements
    registry.erase(150);
    registry.erase(160);

    // Are the original reference and iterator still valid?
    std::cout << "ref = " << ref << '\n';
    std::cout << "it = " << it->second << '\n';

    return 0;
}
```

```bash
g++ -std=c++20 -O2 -o /tmp/map_stable /tmp/map_stable.cpp && /tmp/map_stable
```

```text
ref = alpha
it = alpha
```

No matter how much was inserted or erased in between (as long as element 1 itself was not erased), that reference and that iterator stayed valid the whole time. This is the stability that comes from "each node hanging independently on the heap", and it is one of map's core engineering values over vector.

## Heterogeneous Lookup (C++14): Stop Building a Temporary string Just to Search

The pit below is one most people who have written a string-keyed map have stepped in — usually without noticing. Take a look:

```cpp
std::map<std::string, int> scores;
scores["alice"] = 90;

auto it = scores.find("alice");   // "alice" is a const char*
```

The signature of `find` is `find(const key_type&)`, and key_type is `std::string`. What you passed in, though, is a `const char*`. So the compiler, ever so thoughtful, constructs a temporary `std::string` from `"alice"` for you and does the lookup with that temporary. One lookup, one string construction thrown away — and if SSO cannot hold it, the temporary string also has to allocate on the heap, then gets destroyed and freed the instant the lookup finishes. Do this at high frequency on a hot path and the entire cost goes into manufacturing temporary strings.

C++14 shipped the proper fix: **transparent comparators**.

By default the map's comparator is `std::less<std::string>`, which only recognizes string. But the standard library also provides a specialization, `std::less<void>` (spelled `std::less<>`), which binds to no specific type — it compares whatever two types you hand it directly with `operator<`, provided those two types are comparable. Declare the map's comparator as `std::less<>` and it gains heterogeneous lookup:

```cpp
#include <map>
#include <string>
#include <string_view>

// The key point: the comparator is std::less<> (transparent), not the default std::less<std::string>
std::map<std::string, int, std::less<>> scores;
scores["alice"] = 90;

// Now neither of these two lookups constructs a temporary string
scores.find("alice");                    // const char* compared directly
scores.find(std::string_view("alice"));  // string_view compared directly
```

The mechanism behind the curtain is the nested type `is_transparent`. `std::less<>` typedefs an `is_transparent` on the inside; when the map's lookup overloads spot this marker on the comparator, they switch on the heterogeneous versions and compare the raw type you provided directly against the strings in the tree. string is already comparable with `const char*` and `string_view`, so everything sails through — not a single temporary object constructed.

Mind two boundary conditions. First, this requires your key type and the lookup type to be directly comparable — string and `const char*` compare fine, but if your custom key type provides no comparison with `string_view`, you get none of the benefit. Second, heterogeneous lookup mainly takes effect on lookup-flavored operations such as `find`, `count`, and `contains`. The saved temporaries are real, but "saved, therefore faster" does not follow — a lookup type of `const char*` can actually be slower (it caches no length, so the red-black tree's repeated comparisons call strlen over and over); you need `string_view` for a genuine speedup. We will run that for you in a moment.

## extract and merge (C++17): Node Handles — Move House and Change a Key While You Are at It

C++17 slipped into the associative containers a thing called a "node handle". The name sounds mystical; what it actually solves are three very down-to-earth problems.

First, what a node handle is. map has had a rule since C++11: the key is const. Once you hold a map element, there is no way to modify its key directly — writing `m.begin()->first = 100` will not even compile (the `first` holding the key is `const`). The reason is easy to sympathize with: map keeps the red-black tree structure sorted by key, and if keys could be changed at will, the tree's ordering would collapse on the spot.

Node handles route around that restriction. `extract` can pluck a node clean out of the tree and hand you an independent node handle (of type `std::map<K, V>::node_type`). The handle owns the node: it is no longer inside any map (plucking it disturbs no other element), and it copies no value — it is the original node itself, body and soul. Once it is plucked, you can change its key (it has left the tree, so changing the key breaks no ordering whatsoever), and then `insert` it back.

So since C++17, "change a map element's key" has exactly one legitimate path: **extract → change the key → insert**.

```cpp
#include <iostream>
#include <map>
#include <string>

int main()
{
    std::map<int, std::string> m;
    m[1] = "alpha";

    // Changing the key directly won't compile (a map's key is const)
    // m.begin()->first = 100;

    // The correct approach: extract the node, change the key, insert it back
    auto node = m.extract(1);      // pluck out the node with key=1
    node.key() = 100;              // now the key can be changed (the node is off the tree)
    m.insert(std::move(node));     // insert it back, new key=100

    std::cout << "count(1)   = " << m.count(1) << '\n';
    std::cout << "count(100) = " << m.count(100) << '\n';
    std::cout << "value      = " << m.at(100) << '\n';

    return 0;
}
```

```bash
g++ -std=c++17 -O2 -o /tmp/map_extract /tmp/map_extract.cpp && /tmp/map_extract
```

```text
count(1)   = 0
count(100) = 1
value      = alpha
```

Notice the value is still "alpha" — across the entire process the value was never copied or moved once; what moved is the original node itself. This is what "zero-copy relocation" means.

The second use is migrating nodes across containers. Two maps, and you want to carry certain nodes from one into the other: `extract` + `insert` does it, again without copying the value:

```cpp
std::map<int, std::string> a, b;
a[1] = "x";
a[2] = "y";

// Move node 1 of a wholesale into b
auto node = a.extract(1);
b.insert(std::move(node));
```

The third use is `merge` — the one-stroke version. `m1.merge(m2)` carries every node of m2 whose key does not clash with m1 wholesale into m1, again zero-copy:

```cpp
std::map<int, std::string> m1{{1, "a"}, {2, "b"}};
std::map<int, std::string> m2{{2, "dup"}, {3, "c"}};

m1.merge(m2);
// m1: {1, 2, 3}; only the key=2 one is left in m2 (m1 already had 2, so the conflict wasn't moved)
```

`merge` costs O(n·log n) (n being the number of nodes moved), but at no point is any value copied — when you are migrating large objects (say the value is a big vector or a long string), the savings are extremely tangible.

## Is the Transparent Comparator Actually Faster? Let Us Run It

First, a side note of fact: under the hood, the map in libstdc++, libc++, and MSVC STL is a red-black tree in every case, and the behavior is identical (the Standard forces that); only the node layout and memory-allocation details differ from vendor to vendor. Day-to-day engineering has no need to agonize over this — knowing "identical behavior, differing implementations" is enough.

But there is a question more worth verifying with your own hands: the transparent comparator claims to save temporary objects — is it actually faster? Plenty of people (me included, before writing this) take it for granted that "fewer constructions must mean faster". Let us not guess. Let us just run it.

Prepare a string-keyed map whose keys are long strings (44 characters — past SSO, so the temporary construction must hit the heap), then compare three lookup styles: A is the default comparator searched with `const char*` (constructs a temporary string); B is the transparent comparator searched with `const char*`; C is the transparent comparator searched with `string_view`.

```cpp
#include <iostream>
#include <map>
#include <string>
#include <string_view>
#include <chrono>

int main()
{
    std::map<std::string, int> classic;
    std::map<std::string, int, std::less<>> transparent;
    for (int i = 0; i < 10000; ++i) {
        std::string k(40, 'a');
        k += std::to_string(i);
        classic[k] = i;
        transparent[k] = i;
    }
    std::string needle_str(40, 'a');
    needle_str += "9999";
    const char* needle = needle_str.c_str();
    std::string_view needle_sv(needle);
    volatile int sink = 0;

    auto bench = [&](auto fn) {
        auto t0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < 100000; ++i) {
            sink += fn()->second;
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double, std::milli>(t1 - t0).count();
    };

    std::cout << "A classic find(const char*):     "
              << bench([&] { return classic.find(needle); }) << " ms\n";
    std::cout << "B transparent find(const char*): "
              << bench([&] { return transparent.find(needle); }) << " ms\n";
    std::cout << "C transparent find(string_view): "
              << bench([&] { return transparent.find(needle_sv); }) << " ms\n";
    return 0;
}
```

```bash
g++ -std=c++20 -O2 -o /tmp/map_bench3 /tmp/map_bench3.cpp && /tmp/map_bench3
```

```text
A classic find(const char*):     10.5 ms
B transparent find(const char*): 15.5 ms
C transparent find(string_view): 8.7 ms
```

(GCC 16.1.1, my machine; the exact milliseconds will vary with your machine, but the ordering of the three is stable.)

The result most likely runs against your intuition — **B is actually the slowest**, and C the fastest. Why? The key is that `const char*` caches no length. One red-black tree lookup makes log(n) comparisons (about 14 here); in B, every comparison of the bare `const char*` against a string in the tree has to scan from the head to `'\0'` to get the length (`strlen`) — 14 comparisons means 14 strlens. A, on the other hand, spends one temporary string construction up front (on the heap), but its 14 comparisons afterwards are all string versus string, going straight to `memcmp` with each side's cached length — which ends up faster. C uses `string_view`: the length is computed once at construction and cached, and every comparison reuses it — no per-comparison strlen, no temporary string either — so it comes out fastest.

So etch this easy-to-step-in pit into memory: **a transparent comparator only truly speeds things up when paired with `string_view`; paired with `const char*` it can actually be slower**. Merely plopping `std::less<>` into place while using the wrong lookup type makes performance go down instead of up.

## A Few Parting Words

The map and set family looks on the surface like "containers that sort by key and search in O(log n)", but underneath sits the red-black tree that all three major implementations independently settled on. Lock in a few of its key properties and you will use map with confidence from here on: element addresses are stable (insertion invalidates nothing; erasure invalidates only the erased element), which makes it a natural fit for registries, observers, and other structures that need stable handles; C++14's transparent comparator spares you the pointless temporary objects when searching a string-keyed map (but remember: only `string_view` lookups are genuinely faster — `const char*` is actually slower); C++17's node handles hand you the one legitimate channel for zero-copy moves and key changes. And set? It is the version of the same machinery with the value erased — every rule carries over unchanged.

In the next article we follow this thread to map's "unordered sibling" `unordered_map` — swapping the red-black tree's logarithmic lookup for a hash table's amortized constant lookup is an entirely different trade-off.

Want to roll up your sleeves and see the effects directly? Open the online example below (it runs, and it shows the assembly too):

<OnlineCompilerDemo
  title="map / set: red-black tree ordering, heterogeneous lookup, extract"
  source-path="code/examples/vol3/06_map_set.cpp"
  description="Automatic ordering by key, heterogeneous lookup with the std::less<> transparent comparator via string_view, zero-copy node transfer with extract"
  allow-run
/>

## References

- [std::map — cppreference](https://en.cppreference.com/w/cpp/container/map)
- [std::set — cppreference](https://en.cppreference.com/w/cpp/container/set)
- [std::less\<void\> transparent comparator — cppreference](https://en.cppreference.com/w/cpp/utility/functional/less_void)
- [map::extract / merge node handles — cppreference](https://en.cppreference.com/w/cpp/container/map/extract)
- [Master table of container iterator invalidation rules — cppreference](https://en.cppreference.com/w/cpp/container#Iterator_invalidation)
- [N3657: the C++14 heterogeneous lookup proposal](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2013/n3657.htm)
