---
title: 'Deep Dive into map and set: Red-Black Trees, Heterogeneous Lookup, and Node
  Handles'
description: 'Deep dive into the underlying Red-Black Tree implementation of `std::map`
  and `set`: O(log n) complexity and stable iterators, heterogeneous lookup with C++14
  transparent comparators, and the only correct way to modify keys using C++17 node
  handles (`extract`/`merge`).'
chapter: 7
order: 6
tags:
- host
- cpp-modern
- intermediate
- map
- 容器
difficulty: intermediate
platform: host
cpp_standard:
- 11
- 14
- 17
- 20
reading_time_minutes: 25
prerequisites:
- vector 深入：三指针、扩容与迭代器失效
related:
- 容器选择指南
translation:
  source: documents/vol3-standard-library/06-map-set-deep-dive.md
  source_hash: 77321460fc6211a6e3fcec9b1c10ff5f68cd10c7c94768e2dadaa01998741357
  translated_at: '2026-06-15T09:14:20.578956+00:00'
  engine: anthropic
  token_count: 2719
---
# Deep Dive into map and set: Red-Black Trees, Heterogeneous Lookup, and Node Handles

## Family Portrait: map, set, and Their Siblings

We have used `std::map` and `std::set` countless times. Daily usage usually boils down to `[]`, `find()`, or iteration, so they might seem unremarkable. But once you peel back a layer, you will find a red-black tree hiding underneath. Interestingly, the Standard never explicitly mandates a red-black tree—yet the three major standard library implementations all converged on this choice. Not to mention, C++14 added heterogeneous lookup, and C++17 stuffed in node handles, allowing you to move elements with zero-copy and even modify a key that was supposed to be `const`. In this post, we will thoroughly clarify `map` and `set`, from the underlying implementation to modern usage patterns.

First, let's recognize the whole family. There are four siblings in the ordered associative container family, all built on the same red-black tree:

| Container | What it stores | Key uniqueness |
|------|--------|-----------|
| `std::map` | key → value pairs | Unique |
| `std::multimap` | key → value pairs | Duplicates allowed |
| `std::set` | Stores only keys | Unique |
| `std::multiset` | Stores only keys | Duplicates allowed |

The relationship between `map` and `set` is actually quite simple: `set` is just a `map` that threw away the `value` and kept only the `key`. The underlying node structure, balancing logic, and iterator rules are identical. Therefore, this post will focus on `map`. `set` has everything `map` has; the only difference is that "set does not store a value."

As for the boundaries with their neighbors, one sentence is enough: if you want "ordered + logarithmic lookup," use `map`/`set` (red-black tree); if you want "unordered + amortized constant lookup," use `unordered_map`/`unordered_set` (hash table); if you want "ordered + contiguous storage (cache-friendly)," use C++23's `std::flat_map`. These three paths cover different needs; this post focuses only on the red-black tree path.

## Hiding Underneath is a Red-Black Tree: The Standard Doesn't Specify, But All Three Chose It

The Standard's requirements for `map` are actually quite restrained: elements are sorted by key, and lookup, insertion, and deletion must have logarithmic complexity O(log n). As for what data structure you use to achieve this, the Standard is vague—roughly "balanced binary search tree," without specifying the specific type. The interesting part is this: libstdc++ (GCC), libc++ (Clang), and MSVC STL all ended up choosing red-black trees.

Why a red-black tree and not the more "strictly balanced" AVL tree? The key is deletion. AVL trees require the height difference between left and right subtrees to be no more than 1. This tight balance means that during deletion, you might have to rotate from the bottom all the way to the top, with an uncontrollable number of rotations. Red-black trees are looser; they only guarantee that "the longest path is no more than twice the shortest path." In exchange, insertion requires at most 2 rotations and deletion at most 3—since the number of rotations has a clear upper bound, it is more cost-effective for maps with frequent additions and deletions.

There are only a few rules for red-black trees. Let's quickly go through them (no need to memorize, just understand how they guarantee O(log n)):

- Every node is either red or black.
- The root node is black.
- Nil leaves (empty sentinels) are black.
- The children of a red node must be black (no two reds can be adjacent).
- The number of black nodes passed from any node to all its leaf nodes is the same (this is called "black height").

The last two rules combined result in this: you can't make a path long and entirely red, because reds can't be adjacent, and the black height must be consistent. Thus, the longest red-black alternating path is at most twice the shortest all-black path—the tree height is suppressed to O(log n), so lookup is naturally O(log n).

What does a node look like? Compared to a normal binary search tree, it just adds a color bit and three pointers:

```cpp
// Simplified red-black tree node structure
struct Node {
    Node* parent;   // Parent pointer
    Node* left;     // Left child
    Node* right;    // Right child
    Color color;    // Red or Black
    Key key;
    Value value;    // Only map has this; set doesn't
};
```

That `parent` pointer is worth mentioning. Normal binary search tree lookups only go down and don't need to know the parent. However, red-black tree insertion and deletion require bottom-up color adjustments and rotations, so the ability to find the parent is necessary. This also explains why red-black tree nodes are "heavier" than normal linked list nodes—they are tri-directional. `set` is completely isomorphic to `map` here; the only difference is whether the node payload contains that `Value`. So, for all the mechanisms of `map` discussed next, if you erase the `Value`, you get `set`.

## Complexity and Iterator Invalidation: A Completely Different Set of Rules than vector

Let's calculate the complexity clearly first. Red-black tree height is O(log n), so lookup, insertion, and deletion are all a single trip down the tree, plus possible rotations (rotation itself is a local O(1) operation). Complexity of common operations:

| Operation | Complexity |
|------|--------|
| `find` / `insert` / `erase` / `lower_bound` / `upper_bound` | O(log n) |
| `[]` / `at` / `count` | O(log n) |
| Ordered traversal | O(n) |

What needs to be singled out here is not the complexity—it's normal for red-black trees to be a bit slower—but **iterator invalidation**. The invalidation rules for `map` are completely different from `vector`, and this is precisely a hard reason why you might choose `map` over `vector` in engineering.

We covered `vector` in [that post](03-vector-deep-dive): once reallocation happens, all iterators, references, and pointers are invalidated because the underlying memory is contiguous and moves as a whole. `map` is different; its elements hang on independent tree nodes:

- **Insertion**: Does not invalidate any existing iterators, references, or pointers.
- **Deletion**: Only invalidates the iterator/reference of the deleted element itself; all other elements remain untouched.

What does this mean? It means the addresses of elements in a `map` are stable. You can pass a pointer or reference to a `map` element around to other subsystems; as long as you don't delete that element, the pointer remains valid forever. Even if you insert thousands of new elements or delete hundreds of other elements in the `map`, that pointer in your hand still points to the original element.

This property is very valuable in engineering. For example, if you write an event registry where every callback is registered into a `map`, and you want to hand its pointer to another subsystem for reference or deregistration—if you use `vector`, one reallocation turns all those pointers into dangling pointers. With `map`, it's completely stable.

Let's run a small example to see this stability:

```cpp
#include <iostream>
#include <map>

int main() {
    std::map<int, std::string> m = {{1, "alpha"}, {2, "beta"}};

    // Get reference and iterator to element 1
    std::string& ref = m[1];
    auto it = m.find(1);

    std::cout << "Before operations: " << ref << std::endl;

    // Perform massive insertions and deletions
    for (int i = 10; i < 100; ++i) {
        m[i] = "data";
    }
    m.erase(2);
    m.erase(10);

    // Reference and iterator are still valid!
    std::cout << "After operations: " << ref << std::endl;
    std::cout << "Via iterator: " << it->second << std::endl;

    return 0;
}
```

```text
Before operations: alpha
After operations: alpha
Via iterator: alpha
```

No matter how many insertions or deletions happened in between (as long as element 1 itself wasn't deleted), that reference and iterator remain valid. This is the stability brought by the red-black tree's "nodes independently hanging on the heap," and it is one of the core engineering values that distinguish `map` from `vector`.

## Heterogeneous Lookup (C++14): Stop Creating Temporary Strings for Lookups

The following pitfall is one most people who have written string-key maps have stepped on, perhaps without realizing it. Look at this:

```cpp
std::map<std::string, int> m = {{"hello", 1}, {"world", 2}};
// Pitfall: constructing a temporary std::string just for lookup
if (m.contains("hello")) { ... }
```

`contains`'s signature is `bool contains(const Key& key)`, where `key_type` is `std::string`. But you passed in a `const char*`. So the compiler kindly helps you construct a temporary `std::string` using `std::string(const char*)`, then uses that temporary object for the lookup. One lookup, wasting one `string` construction—and if SSO doesn't hold, this temporary string also allocates memory on the heap, only to be destroyed immediately after the lookup. If you do this frequently in a hot path, the overhead is entirely spent on creating temporary strings.

C++14 provided the correct solution: **transparent comparator**.

By default, `map`'s comparator is `std::less<std::string>`, which only recognizes `string`. However, the standard library also provides a specialized version `std::less<void>` (written as `std::less<>`), which doesn't bind to a specific type but uses `operator<` to directly compare any two types passed in—provided those two types are comparable. As long as you declare the map's comparator as `std::less<>`, it gains heterogeneous lookup capability:

```cpp
#include <map>
#include <string_view>

int main() {
    // Use std::less<> to enable heterogeneous lookup
    std::map<std::string, int, std::less<>> m = {{"hello", 1}, {"world", 2}};

    // No temporary std::string is constructed here
    // Directly compares const char* with std::string
    if (m.contains("hello")) {
        // ...
    }
}
```

The mechanism behind this is the nested type `is_transparent`. `std::less<void>` internally typedefs an `is_transparent`. When the map's lookup overloads see this marker on the comparator, they enable the heterogeneous version, directly taking the native type you gave and comparing it with the `string` in the tree. `string` and `const char*`, `std::string_view` already support comparison, so it's smooth sailing without constructing a single temporary object.

Note two boundaries. First, this requires that your key type and lookup type can be directly compared—`string` and `const char*` can compare, but if your custom key type doesn't provide comparison with `const char*`, you can't enjoy this. Second, heterogeneous lookup mainly takes effect on lookup operations like `find`, `count`, `contains`. It really does save temporary objects, but "saving them makes it faster" is not necessarily true—using lookup type `const char*` might actually be slower (it has no cached length, and red-black tree multiple comparisons require repeated `strlen`); you must use `std::string_view` to truly speed it up. We'll show you this in a run later.

## extract and merge (C++17): Node Handles, Moving House and Changing the Key

C++17 stuffed a thing called "node handle" into associative containers. The name sounds mysterious, but it actually solves three very practical problems.

First, what is a node handle? Since C++11, `map` has a rule: the key is `const`. Once you get a map element, you can't directly modify its key—writing `it->first = new_key` won't even compile (that `first` is `const Key`). The reason is understandable: `map` relies on key sorting to maintain the red-black tree structure. If you could arbitrarily change the key, the tree's order would collapse immediately.

Node handles bypass this limitation. `extract` can "pick" a node entirely out of the tree and return an independent node handle (type `typename std::map<...>::node_type`). This handle owns the node's ownership; it is in no map (picking it out doesn't affect other elements), nor does it copy the value—it is the original node itself. After picking it out, you can modify its key (because at this point it has detached from the tree, changing the key doesn't break any ordering), and then `insert` it back.

So, "changing a map element's key" has had the only legitimate way since C++17: **extract → change key → insert**.

```cpp
#include <iostream>
#include <map>
#include <string>

int main() {
    std::map<int, std::string> m = {{1, "alpha"}, {3, "gamma"}};

    // 1. Extract the node with key 1
    auto node = m.extract(1);

    // 2. Modify the key (node.key() is non-const)
    node.key() = 2; // Change key from 1 to 2

    // 3. Insert back (value remains "alpha", zero copy)
    m.insert(std::move(node));

    // Result: { {2, "alpha"}, {3, "gamma"} }
    for (const auto& [k, v] : m) {
        std::cout << k << ": " << v << std::endl;
    }

    return 0;
}
```

```text
2: alpha
3: gamma
```

Notice the value is still "alpha"—throughout the entire process, the value was never copied or moved; we just moved the original node. This is "zero-copy moving."

The second use case is migrating nodes between containers. For two maps, if you want to move certain nodes from one to the other, `extract` + `insert` works, again without copying the value:

```cpp
std::map<int, std::string> src = {{1, "one"}, {2, "two"}};
std::map<int, std::string> dst;

// Move node 1 from src to dst
auto node = src.extract(1);
dst.insert(std::move(node));
```

The third use case is `merge`, a one-shot deal. `merge` moves all nodes from `m2` that don't conflict with keys in `m1` into `m1`, again zero-copy:

```cpp
std::map<int, std::string> m1 = {{10, "ten"}};
std::map<int, std::string> m2 = {{1, "one"}, {2, "two"}, {10, "conflict"}};

// Merge m2 into m1. Node 10 in m2 is ignored because m1 already has key 10.
// Nodes 1 and 2 are moved to m1 without copying the string content.
m1.merge(m2);
```

`merge`'s complexity is O(n·log n) (where n is the number moved), but there is zero copying of values throughout—when migrating large objects (e.g., value is a large `vector` or long string), the saved overhead is very real.

## Are Transparent Comparators Actually Faster? Let's Run It

First, a side fact: libstdc++, libc++, and MSVC STL all use red-black trees for `map` underneath. Their behavior is completely identical (mandated by the Standard), only the node layout and memory allocation details differ. Daily engineering doesn't need to worry about it; knowing "behavior is identical, implementations vary" is enough.

But there is a question more worth verifying personally: transparent comparators claim to save temporary objects, but are they actually faster? Many people (including me before writing this) would assume "saving construction must be faster." Let's not guess; let's run it directly.

Prepare a `map` with string keys, use long strings for keys (44 characters, exceeding SSO, so temporary construction hits the heap), then compare three lookup methods: A is default comparator using `const char*` lookup (constructs temporary `string`); B is transparent comparator using `const char*` lookup; C is transparent comparator using `std::string_view` lookup.

```cpp
#include <map>
#include <string>
#include <string_view>
#include <chrono>

// Long string key, exceeds SSO, forces heap allocation
using LongString = std::string;

// A: Default comparator, lookup with const char* (constructs temp string)
std::map<LongString, int> map_a;

// B: Transparent comparator, lookup with const char*
std::map<LongString, int, std::less<>> map_b;

// C: Transparent comparator, lookup with std::string_view
std::map<LongString, int, std::less<>> map_c;

void benchmark() {
    // Prepare data
    for (int i = 0; i < 10000; ++i) {
        map_a.emplace("key_" + std::to_string(i), i);
        map_b.emplace("key_" + std::to_string(i), i);
        map_c.emplace("key_" + std::to_string(i), i);
    }

    const char* target = "key_5000"; // Lookup target

    // Measure A
    auto start = std::chrono::high_resolution_clock::now();
    for (volatile int i = 0; i < 100000; ++i) {
        map_a.find(target);
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto time_a = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    // Measure B
    start = std::chrono::high_resolution_clock::now();
    for (volatile int i = 0; i < 100000; ++i) {
        map_b.find(target);
    }
    end = std::chrono::high_resolution_clock::now();
    auto time_b = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    // Measure C
    std::string_view target_sv = target;
    start = std::chrono::high_resolution_clock::now();
    for (volatile int i = 0; i < 100000; ++i) {
        map_c.find(target_sv);
    }
    end = std::chrono::high_resolution_clock::now();
    auto time_c = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    std::cout << "A (default, const char*): " << time_a.count() << " ms\n";
    std::cout << "B (transparent, const char*): " << time_b.count() << " ms\n";
    std::cout << "C (transparent, string_view): " << time_c.count() << " ms\n";
}
```

```text
A (default, const char*): 18 ms
B (transparent, const char*): 32 ms
C (transparent, string_view): 12 ms
```

(GCC 16.1.1, local machine; specific milliseconds vary with your machine, but the relative size relationship is stable.)

The result is likely contrary to your intuition—**B is actually the slowest**, C is the fastest. Why? The key is `const char*` has no cached length. One red-black tree lookup requires comparing log(n) times (about 14 times here). B compares the raw `const char*` with the `string` in the tree every time, and must scan from the start to `\0` to calculate the length (`strlen`) each time. 14 comparisons mean 14 `strlen`s. Although A spends one construction of a temporary `string` (hitting the heap) first, the subsequent 14 comparisons are string-to-string, directly using their respective cached lengths for `operator<`, which is faster. C uses `std::string_view`, which calculates and caches the length once upon construction, and subsequent comparisons reuse this length. It avoids repeated `strlen` and doesn't construct a temporary `string`, so it is the fastest.

So remember this easy-to-fall-into pit: **transparent comparators need to be paired with `std::string_view` to truly speed up; pairing with `const char*` might actually be slower**. Just putting `std::less<>` there but using the wrong lookup type results in performance degradation, not improvement.

## Wrapping Up

The `map` and `set` family looks like containers that "can sort by key and look up in O(log n)" on the surface, but underneath they are red-black trees that all three major implementations converged on. Keep a few key properties in mind, and you'll be confident using `map` in the future: element addresses are stable (insertion doesn't invalidate, deletion only invalidates the deleted one), making them suitable for registries and observer-like structures that need stable handles; C++14's transparent comparator saves you from creating temporary objects when looking up string-key maps (but remember to pair with `std::string_view` lookup to truly speed up, using `const char*` is slower); C++17's node handles give you the only legal channel for zero-copy moving and changing keys. As for `set`, it's just the version with the `value` erased from the same mechanism; all rules apply.

In the next post, following this thread, we will look at map's "unordered sibling" `std::unordered_map`—swapping the red-black tree's logarithmic lookup for a hash table's amortized constant lookup is a completely different trade-off.

Want to run it yourself and see the effect? Open the online example below (runnable, and viewable assembly):

<OnlineCompilerDemo
  title="map / set: Red-Black Tree Order, Heterogeneous Lookup, extract"
  source-path="code/examples/vol3/06_map_set.cpp"
  description="Automatically ordered by key, std::less<> transparent comparator uses string_view for heterogeneous lookup, extract nodes for zero-copy transfer"
  allow-run
/>

## Reference Resources

- [std::map — cppreference](https://en.cppreference.com/w/cpp/container/map)
- [std::set — cppreference](https://en.cppreference.com/w/cpp/container/set)
- [std::less\<void\> transparent comparator — cppreference](https://en.cppreference.com/w/cpp/utility/functional/less_void)
- [map::extract / merge node handles — cppreference](https://en.cppreference.com/w/cpp/container/map/extract)
- [Container Iterator Invalidation Rules Summary Table — cppreference](https://en.cppreference.com/w/cpp/container#Iterator_invalidation)
- [N3657: C++14 Heterogeneous Lookup Proposal](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2013/n3657.htm)
