---
chapter: 7
cpp_standard:
- 11
- 14
- 17
- 20
description: 'Deep dive into the underlying implementation of Red-Black Trees: `std::map`
  and `set` with O(log n) complexity and stable iterators, heterogeneous lookup with
  C++14 transparent comparators, and the only correct way to modify keys using C++17
  node handles (`extract`/`merge`).'
difficulty: intermediate
order: 6
platform: host
prerequisites:
- vector 深入：三指针、扩容与迭代器失效
reading_time_minutes: 15
related:
- 容器选择指南
tags:
- host
- cpp-modern
- intermediate
- map
- 容器
title: 'Deep Dive into map and set: Red-Black Trees, Heterogeneous Lookup, and Node
  Handles'
translation:
  source: documents/vol3-standard-library/containers/06-map-set-deep-dive.md
  source_hash: 2a8c7d7f183542ad3514ba8de981bf4081655fa1bc3db3ce1ae08e4147f09ba4
  translated_at: '2026-06-24T00:36:01.891773+00:00'
  engine: anthropic
  token_count: 2715
---
# Deep Dive into map and set: Red-Black Trees, Heterogeneous Lookup, and Node Handles

## Family Portrait: map, set, and Their Siblings

We use `std::map` and `std::set` countless times. Usually, we just `insert`, `find`, and iterate, so they might seem unremarkable. But if you peel back a layer, you'll find a red-black tree hiding underneath. What's more, the Standard never actually mandates a red-black tree—it's just that the three major standard library implementations all converged on it. Not to mention, C++14 added heterogeneous lookup, and C++17 stuffed in node handles, allowing zero-copy moves and even letting you modify a key that is supposed to be const. In this article, we will clarify map and set from the bottom up to modern usage.

First, let's recognize the whole family. There are four siblings in the ordered associative container family, all growing on the same red-black tree:

| Container | What it stores | Key Uniqueness |
|------|--------|-----------|
| `map` | key → value pairs | Unique |
| `multimap` | key → value pairs | Duplicates allowed |
| `set` | key only | Unique |
| `multiset` | key only | Duplicates allowed |

The relationship between map and set is actually quite simple: a set is just a map that threw away the value and kept only the key. The underlying node structure, balancing logic, and iterator rules are all identical. So, in this article, we will focus on map as the main thread; set has everything map has, with the only difference being "set doesn't store a value."

As for boundaries with neighbors, one sentence is enough: if you want "ordered + logarithmic lookup," use `map`/`set` (red-black tree); if you want "unordered + amortized constant lookup," use `unordered_map`/`unordered_set` (hash table); if you want "ordered + contiguous storage (cache-friendly)," go for C++23's `flat_map`. These three routes cover their respective domains; this article only covers the red-black tree path.

## Hiding a Red-Black Tree: The Standard Doesn't Mandate It, But the Big Three Chose It

The Standard's requirements for map are actually quite restrained: elements are sorted by key, and lookup, insertion, and deletion all have logarithmic complexity O(log n). As for what data structure you use to achieve this, the Standard is vague—roughly "balanced binary search tree," but not specifying which kind. The interesting part is here: libstdc++ (GCC), libc++ (Clang), and MSVC STL all ultimately chose the red-black tree.

Why a red-black tree and not the more "strictly balanced" AVL tree? The key is deletion. AVL trees require the height difference between left and right subtrees to be no more than one. The balance is tight, but the cost is that deletion might require rotations all the way from the bottom to the top, with an uncontrollable number of rotations. Red-black trees are looser; they only guarantee "the longest path is no more than twice the shortest path." In exchange, insertion requires at most two rotations, and deletion at most three—there is a clear upper bound on rotation counts, which is a better deal for maps with frequent additions and deletions.

The rules of red-black trees are few; let's quickly run through them (no need to memorize, just understand how they guarantee O(log n)):

- Every node is either red or black
- The root is black
- Nil leaves (empty sentinels) are black
- Children of a red node must be black (no two reds can be adjacent)
- The number of black nodes passed from any node to all its leaf nodes is the same (this is called "black height")

The last two rules combined result in this: you can't have a path that is both long and entirely red, because reds can't be adjacent, and the black height must be consistent. Thus, the longest alternating red-black path is at most twice the shortest all-black path—the tree height is suppressed to O(log n), so lookup is naturally O(log n).

What does a node look like? Compared to a normal binary search tree, it just has one extra color bit and three pointers:

```cpp
// 红黑树节点的简化骨架（标准库内部实现，各厂细节不同，这里只看结构）
struct TreeNode {
    bool      is_red;    // 颜色位
    TreeNode* parent;    // 父节点指针（自底向上调整时要用）
    TreeNode* left;
    TreeNode* right;
    // map 节点这里存 pair<const Key, Value>；set 节点只存 Key
};
```

That `parent` pointer deserves a closer look. In a standard binary search tree, lookups only go down, so we don't need to know the parent. However, red-black trees require bottom-up adjustments during insertion and deletion—recoloring and rotating—so we must be able to backtrack to the parent. This is why every node carries a `parent` pointer. This also explains why red-black tree nodes are "heavier" than standard linked list nodes—they are ternary (three-way). `set` is isomorphic to `map` here; the only difference is whether the node payload contains that `Value`. So, for every mechanism we discuss about `map` next, just erase the `Value` and you have `set`.

## Complexity and Iterator Invalidation: A Completely Different Rulebook than `vector`

Let's get the complexity calculations straight first. A red-black tree has a height of $O(\log n)$, so lookup, insertion, and deletion all traverse down the tree once, plus potential rotations (which are local $O(1)$ operations). Here is the complexity for common operations:

| Operation | Complexity |
|-----------|------------|
| `find` / `count` / `contains` / `operator[]` / `at` | $O(\log n)$ |
| `insert` / `emplace` / `erase` | $O(\log n)$ |
| Ordered traversal | $O(n)$ |

What specifically needs to be highlighted here isn't the complexity—it's normal for red-black trees to be a bit slower—but **iterator invalidation**. The invalidation rules for `map` are completely different from `vector`, and this is actually a solid technical reason to choose `map` over `vector` in engineering.

As we discussed in the [article on `vector`](03-vector-deep-dive.md), once a `vector` reallocates, all iterators, references, and pointers are invalidated because the underlying memory is contiguous and moves as a whole. `map` is different; its elements are stored on individual tree nodes:

- **Insertion**: Does not invalidate any existing iterators, references, or pointers.
- **Deletion**: Only invalidates the iterator/reference pointing to the deleted element itself; all other elements remain untouched.

What does this imply? It implies that the memory addresses of elements in a `map` are stable. You can pass a pointer or reference to a `map` element around to other subsystems, and as long as you don't delete that specific element, that pointer remains valid forever. Even if you insert thousands of new elements or delete hundreds of others, that pointer in your hand still points to the original element.

This property is incredibly valuable in engineering. For example, if you write an event registry where each callback is registered into a `map`, and you want to hand out its pointer to other subsystems for reference or unregistration—using a `vector` risks turning all those pointers into dangling pointers during a reallocation; using a `map` keeps things safe and sound.

Let's run a small example to see this stability in action:

```cpp
#include <iostream>
#include <map>
#include <string>

int main()
{
    std::map<int, std::string> registry;
    registry[1] = "alpha";
    registry[2] = "beta";

    // 拿一个指向元素 1 的引用和迭代器
    std::string& ref = registry.at(1);
    auto it = registry.find(1);

    // 狂插一堆新元素，触发多次红黑树重平衡
    for (int i = 100; i < 200; ++i) {
        registry[i] = "x";
    }

    // 再删掉一些无关元素
    registry.erase(150);
    registry.erase(160);

    // 原来的引用和迭代器还有效吗？
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

No matter how many elements are inserted or erased in between (as long as element 1 itself isn't deleted), the references and iterators remain valid. This stability stems from the fact that red-black tree nodes are independently allocated on the heap, and it represents one of the core engineering values that distinguish `map` from `vector`.

## Heterogeneous Lookup (C++14): Stop Creating Temporary Strings Just to Look Things Up

The pitfall below is one that most developers who have written maps with string keys have stumbled into, even if they didn't realize it at the time. Take a look at this code:

```cpp
std::map<std::string, int> scores;
scores["alice"] = 90;

auto it = scores.find("alice");   // "alice" 是 const char*
```

The signature of `find` is `find(const key_type&)`, where `key_type` is `std::string`. However, you are passing a `const char*`. Consequently, the compiler helpfully constructs a temporary `std::string` from `"alice"` to perform the lookup. One lookup results in a wasted string construction. Furthermore, if SSO (Small String Optimization) fails, this temporary string triggers a heap allocation, only to be destroyed immediately after the lookup. If you perform such lookups frequently on a hot path, the overhead is entirely spent on creating temporary strings.

C++14 provides the solution: **transparent comparators**.

By default, a map's comparator is `std::less<std::string>`, which only accepts strings. However, the standard library provides a specialization, `std::less<void>` (written as `std::less<>`), which does not bind to a specific type. Instead, it uses `operator<` to compare any two types passed to it—provided they are comparable. By declaring the map's comparator as `std::less<>`, we enable heterogeneous lookup:

```cpp
#include <map>
#include <string>
#include <string_view>

// 关键：比较器用 std::less<>（透明），而不是默认的 std::less<std::string>
std::map<std::string, int, std::less<>> scores;
scores["alice"] = 90;

// 现在这两种查法都不构造临时 string
scores.find("alice");                    // const char* 直接比
scores.find(std::string_view("alice"));  // string_view 直接比
```

The mechanism behind this is the nested type `is_transparent`. `std::less<>` internally typedefs `is_transparent`. When the map's lookup overloads detect this marker on the comparator, they enable the heterogeneous versions, taking the native type you provided and comparing it directly against the `string` inside the tree. Since `string` supports comparison with `const char*` and `string_view`, the process goes smoothly without constructing a single temporary object.

There are two caveats to note. First, this requires that your key type and the lookup type are directly comparable—`string` and `const char*` work, but if your custom key type doesn't provide a comparison operator with `string_view`, you can't benefit from this. Second, heterogeneous lookup primarily takes effect in lookup operations like `find`, `count`, and `contains`. While it's true that temporaries are saved, "saving temporaries" doesn't automatically mean "faster"—using `const char*` as the lookup type might actually be slower (since it lacks a cached length, requiring repeated `strlen` calls during red-black tree comparisons). You need to use `string_view` to get a real speed boost, and we will demonstrate this for you shortly.

## `extract` and `merge` (C++17): Node Handles, Moving House and Changing the Key

C++17 introduced something called "node handles" to associative containers. The name sounds mysterious, but it actually solves three very practical problems.

First, let's look at what a node handle is. Since C++11, `map` has had a rule: the key is `const`. Once you have a map element, you cannot directly modify its key—code like `m.begin()->first = 100` won't even compile (the `first` field, which is the key, is `const`). The reason is understandable: the map relies on keys for sorting to maintain the red-black tree structure. If you could arbitrarily change keys, the tree's ordering would immediately break.

Node handles bypass this limitation. `extract` can "pluck" a node entirely out of the tree, returning a standalone node handle (of type `std::map<K, V>::node_type`). This handle owns the node; it exists outside of any map (removing it doesn't affect other elements), and it doesn't copy the value—it is the original node itself. Once extracted, you can modify its key (because it is now detached from the tree, so changing the key won't break any ordering), and then `insert` it back.

Therefore, since C++17, there is only one legitimate way to "change a map element's key": **extract → change key → insert**.

```cpp
#include <iostream>
#include <map>
#include <string>

int main()
{
    std::map<int, std::string> m;
    m[1] = "alpha";

    // 直接改 key 编译不过（map 的 key 是 const）
    // m.begin()->first = 100;

    // 正确做法：extract 摘节点，改 key，再 insert
    auto node = m.extract(1);      // 摘下 key=1 的节点
    node.key() = 100;              // 现在能改 key 了（节点已脱离树）
    m.insert(std::move(node));     // 插回去，新 key=100

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

Notice that `value` is still `"alpha"`—throughout this process, `value` was never copied or moved; we simply moved the original node itself. This is "zero-copy relocation."

The second use case is migrating nodes between containers. If we have two maps and want to move specific nodes from one to the other, we can just use `extract` + `insert`. Again, this does not copy the `value`:

```cpp
std::map<int, std::string> a, b;
a[1] = "x";
a[2] = "y";

// 把 a 里的节点 1 整个搬到 b
auto node = a.extract(1);
b.insert(std::move(node));
```

The third use case is `merge`, which handles everything in one go. `m1.merge(m2)` moves all nodes from `m2` whose keys do not conflict with those in `m1` into `m1`, again with zero copying:

```cpp
std::map<int, std::string> m1{{1, "a"}, {2, "b"}};
std::map<int, std::string> m2{{2, "dup"}, {3, "c"}};

m1.merge(m2);
// m1: {1, 2, 3}；m2 里只剩下 key=2 那个（因为 m1 已有 2，冲突没搬走）
```

The complexity of `merge` is O(n·log n) (where n is the number of elements moved), but there are no copies of `value` throughout the process. When migrating large objects (for example, if `value` is a large vector or a long string), the overhead saved is substantial.

## Are Transparent Comparators Actually Faster? Let's Run a Benchmark

First, a quick aside: the underlying `map` implementation in libstdc++, libc++, and the MSVC STL is a red-black tree in all three cases. The behavior is identical (as mandated by the standard), but the details of node layout and memory allocation differ. In daily engineering work, we don't need to stress over this; knowing that "behavior is consistent, implementations vary" is enough.

However, there is a more important question worth verifying ourselves: transparent comparators claim to save temporary objects, but are they actually faster? Many people (myself included before writing this) might assume that "saving construction must be faster." Instead of guessing, let's just run it and see.

We prepare a map with string keys, using long strings (44 characters, exceeding the Small String Optimization (SSO) limit, so temporary construction hits the heap), and compare three lookup methods: A uses the default comparator with `const char*` (constructs a temporary string); B uses a transparent comparator with `const char*`; and C uses a transparent comparator with `string_view`.

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

(GCC 16.1.1, native; the exact milliseconds will vary by machine, but the relative ranking remains consistent.)

The result likely contradicts your intuition—**B is actually the slowest**, while C is the fastest. Why? The key is that `const char*` does not cache the length. A red-black tree lookup requires `log(n)` comparisons (about 14 here). In B, every time the raw `const char*` is compared against a `string` in the tree, it must scan to `'\0'` to calculate the length (`strlen`), so 14 comparisons mean 14 `strlen` calls. In A, although we pay the cost of constructing a temporary `string` once (which involves the heap), the subsequent 14 comparisons are string-to-string, using the cached lengths for `memcmp`, which is faster. C uses `string_view`, which calculates and caches the length once upon construction, and reuses it for subsequent comparisons. It avoids both repeated `strlen` calls and temporary string construction, making it the fastest.

So, remember this common pitfall: **heterogeneous comparators should be paired with `string_view` for real speed gains; using `const char*` can actually be slower**. Simply slapping `std::less<>` in there while using the wrong lookup type can degrade performance instead of improving it.

## Wrapping Up

The `map` and `set` family appears to be just containers that "sort by key and support O(log n) lookup," but underneath, they all rely on a red-black tree. Keep these key properties in mind, and you'll be confident when using maps: element addresses are stable (insertion doesn't invalidate iterators, and deletion only invalidates the erased element), making them suitable for registries and observer-like structures that require stable handles. C++14 heterogeneous comparators let you look up string-keyed maps without creating temporary objects (but remember, use `string_view` for the lookup type to actually speed it up; `const char*` can be slower). C++17 node handles provide the only legal way to move keys with zero-copy and modify keys. As for `set`, it's just the version where the value is removed from the mechanism, and all the rules apply.

In the next article, we will follow this thread to look at map's "unordered sibling," `unordered_map`—swapping the red-black tree's logarithmic search for a hash table's amortized constant-time search represents a completely different trade-off.

Want to run it and see the effect immediately? Open the online example below (runnable and viewable assembly):

<OnlineCompilerDemo
  title="map / set: Red-black Tree Ordering, Heterogeneous Lookup, extract"
  source-path="code/examples/vol3/06_map_set.cpp"
  description="Automatic ordering by key, std::less<> transparent comparator with string_view heterogeneous lookup, extract node zero-copy transfer"
  allow-run
/>

## References

- [std::map — cppreference](https://en.cppreference.com/w/cpp/container/map)
- [std::set — cppreference](https://en.cppreference.com/w/cpp/container/set)
- [std::less\<void\> transparent comparator — cppreference](https://en.cppreference.com/w/cpp/utility/functional/less_void)
- [map::extract / merge node handle — cppreference](https://en.cppreference.com/w/cpp/container/map/extract)
- [Container iterator invalidation rules summary — cppreference](https://en.cppreference.com/w/cpp/container#Iterator_invalidation)
- [N3657: C++14 Heterogeneous Lookup Proposal](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2013/n3657.htm)
