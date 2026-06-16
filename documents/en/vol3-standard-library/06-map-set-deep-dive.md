---
chapter: 7
cpp_standard:
- 11
- 14
- 17
- 20
description: 'Deep dive into `std::map` and `set` via their red-black tree implementation:
  O(log n) complexity and stable iterators, heterogeneous lookup with C++14 transparent
  comparators, and the only correct way to change keys using C++17 node handles (`extract`/`merge`).'
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
  source: documents/vol3-standard-library/06-map-set-deep-dive.md
  source_hash: 2a8c7d7f183542ad3514ba8de981bf4081655fa1bc3db3ce1ae08e4147f09ba4
  translated_at: '2026-06-16T06:11:25.804151+00:00'
  engine: anthropic
  token_count: 2715
---
# Deep Dive into map and set: Red-Black Trees, Heterogeneous Lookup, and Node Handles

## Family Portrait: map, set, and Their Siblings

We use `std::map` and `std::set` countless times, mostly for `insert`, `find`, and iteration, so they might seem unremarkable. But if we peel back a single layer, we find a red-black tree hiding underneath. Interestingly, the Standard never actually mandates a red-black tree—it just happens to be the unanimous choice of the three major standard library implementations. Furthermore, C++14 added heterogeneous lookup, and C++17 introduced node handles, allowing us to move nodes with zero-copy overhead and even modify keys that are supposed to be `const`. In this article, we will thoroughly cover map and set, from their underlying mechanics to modern usage patterns.

First, let's meet the whole family. There are four siblings in the ordered associative container family, all growing from the same red-black tree:

| Container | What it stores | Key Uniqueness |
|------|--------|-----------|
| `map` | key → value pairs | Unique |
| `multimap` | key → value pairs | Duplicates allowed |
| `set` | key only | Unique |
| `multiset` | key only | Duplicates allowed |

The relationship between map and set is actually quite simple: a set is just a map that throws away the value and keeps only the key. The underlying node structure, balancing logic, and iterator rules are identical. Therefore, we will use map as the main thread for this discussion; everything that applies to map applies to set, with the only difference being that "set doesn't store a value."

As for distinguishing them from their neighbors, one sentence suffices: if you need "ordered + logarithmic lookup," use `map`/`set` (red-black tree); if you need "unordered + amortized constant lookup," use `unordered_map`/`unordered_set` (hash table); if you need "ordered + contiguous storage (cache-friendly)," look to C++23's `flat_map`. These three paths cover distinct use cases, and this article focuses exclusively on the red-black tree path.

## Hiding a Red-Black Tree: The Standard Doesn't Specify, But All Three Chose It

The Standard's requirements for map are actually quite restrained: elements must be sorted by key, and lookup, insertion, and deletion must have logarithmic complexity, O(log n). As for what data structure you use to achieve this, the Standard is vague—roughly "balanced binary search tree," without specifying the specific type. The interesting part is this: libstdc++ (GCC), libc++ (Clang), and MSVC STL all ultimately chose the red-black tree.

Why a red-black tree and not the more "strictly balanced" AVL tree? The key is deletion. AVL trees require the height difference between left and right subtrees to be no more than 1. This strict balance means that during deletion, you might have to rotate all the way from the bottom to the top, making the number of rotations hard to control. Red-black trees are looser; they only guarantee that "the longest path is no more than twice the length of the shortest path." In exchange, insertion requires at most 2 rotations and deletion at most 3 rotations—having a clear upper bound on rotations is more cost-effective for maps with frequent modifications.

The rules of a red-black tree are few; let's quickly review them (no need to memorize, just understand how they guarantee O(log n)):

- Every node is either red or black.
- The root node is black.
- Nil leaves (empty sentinels) are black.
- Children of red nodes must be black (no two reds can be adjacent).
- The number of black nodes passed through from any node to all its leaf nodes is the same (this is called "black height").

The combination of the last two rules means you can't have a path that is both long and entirely red, because reds can't be adjacent, and the black height must be consistent. Thus, the longest alternating red-black path is at most twice the length of the shortest all-black path—the tree height is suppressed to O(log n), so lookup is naturally O(log n).

What does a node look like? Compared to a standard binary search tree, it just has one extra color bit and three pointers:

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

That `parent` pointer deserves a closer look. Lookups in a standard binary search tree only go downwards, so they don't need to know about the parent node. However, red-black tree insertions and deletions require bottom-up adjustments to colors and rotations, which means we must be able to backtrack to the parent. This is why every node carries a `parent` pointer. This also explains why red-black tree nodes are "heavier" than standard linked list nodes—they are ternary (three-way). The structure of `set` here is completely isomorphic to `map`; the only difference is whether or not the node payload contains the `Value`. So, for all the mechanisms discussed next regarding `map`, you can simply remove the `Value` to get `set`.

## Complexity and Iterator Invalidation: A Completely Different Set of Rules than `vector`

Let's get the complexity calculations straight first. The height of a red-black tree is $O(\log n)$, so lookups, insertions, and deletions all involve traversing down the tree once, plus potential rotations (which are local $O(1)$ operations). The complexity of common operations is:

| Operation | Complexity |
|-----------|------------|
| `find` / `count` / `contains` / `operator[]` / `at` | $O(\log n)$ |
| `insert` / `emplace` / `erase` | $O(\log n)$ |
| Ordered traversal | $O(n)$ |

What we really need to highlight here isn't the complexity—it's normal for red-black trees to be a bit slower—but rather **iterator invalidation**. The invalidation rules for `map` are completely different from those of `vector`, and this is actually a solid technical reason to choose `map` over `vector` in engineering.

As we discussed in the [article on `vector`](03-vector-deep-dive.md): once a reallocation occurs, all iterators, references, and pointers are invalidated because the underlying memory is contiguous and moved as a whole. `map` is different; its elements are stored in individual tree nodes:

- **Insertion**: Does not invalidate any existing iterators, references, or pointers.
- **Deletion**: Only invalidates the iterator/reference of the deleted element itself; all other elements remain untouched.

What does this imply? It implies that the memory addresses of elements in a `map` are stable. You can pass a pointer or reference to a `map` element around anywhere, and as long as you don't delete that specific element, the pointer remains valid forever. Even if you insert thousands of new elements or delete hundreds of others, that pointer in your hand will still point to the original element.

This property is extremely valuable in real-world engineering. For example, suppose you are writing an event registry. After a callback is registered in the `map`, you might want to hand its pointer to another subsystem for reference or deregistration. If you used a `vector`, a single reallocation would turn all those pointers into dangling pointers (wild pointers). Using `map` keeps things safe and sound.

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

No matter how many elements are inserted or erased in between (as long as element 1 itself isn't deleted), the references and iterators remain valid. This stability stems from the fact that red-black tree nodes are independently allocated on the heap, and it is one of the core engineering values that distinguish `map` from `vector`.

## Heterogeneous Lookup (C++14): Stop Creating Temporary Strings Just to Look Up

The following pitfall is one that most developers who have written maps with string keys have stepped into, perhaps without realizing it. Take a look at this code:

```cpp
std::map<std::string, int> scores;
scores["alice"] = 90;

auto it = scores.find("alice");   // "alice" 是 const char*
```

The signature of `find` is `find(const key_type&)`, where `key_type` is `std::string`. However, we are passing a `const char*`. Consequently, the compiler helpfully constructs a temporary `std::string` from `"alice"` to perform the lookup. One lookup, wasted on a string construction—and if Small String Optimization (SSO) doesn't apply, this temporary string even triggers a heap allocation, only to be destroyed immediately after the search. If we perform such lookups frequently on a hot path, the overhead is entirely spent on manufacturing temporary strings.

C++14 provides the solution: **transparent comparators**.

By default, a map's comparator is `std::less<std::string>`, which only accepts strings. However, the standard library provides a specialization, `std::less<void>` (written as `std::less<>`), which does not bind to a specific type. Instead, it uses `operator<` to compare any two types passed to it—provided they are comparable. As long as we declare the map's comparator as `std::less<>`, it gains heterogeneous lookup capabilities:

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

The mechanism behind this is the nested type `is_transparent`. `std::less<>` internally typedefs `is_transparent`. When the map's lookup overloads detect this marker on the comparator, they enable the heterogeneous version, directly using the native type you provided to compare against the `string` inside the tree. Since `string` can be compared directly with `const char*` and `string_view`, the process proceeds smoothly without constructing a single temporary object.

There are two caveats to keep in mind. First, this requires that your key type and the lookup type are directly comparable—`string` and `const char*` work out of the box, but if you have a custom key type that doesn't implement comparison with `string_view`, you won't benefit from this. Second, heterogeneous lookup primarily applies to search operations like `find`, `count`, and `contains`. While it definitely saves temporary objects, "saving objects means faster" isn't always true—using `const char*` as the lookup type might actually be slower (since it lacks a cached length, forcing repeated `strlen` calls during red-black tree comparisons). Using `string_view` is the real way to gain speed, and we will demonstrate this with a benchmark shortly.

## extract and merge (C++17): Node Handles, Moving House and Changing the Key

C++17 introduced a feature called "node handle" to associative containers. The name sounds abstract, but it actually solves three very practical problems.

First, let's understand what a node handle is. Since C++11, `map` has had a specific rule: the key is `const`. If you obtain an element from a map, you cannot directly modify its key—code like `m.begin()->first = 100` won't even compile (the `first` member, which is the key, is `const`). The reason is straightforward: the map relies on keys for sorting to maintain its red-black tree structure; if you could arbitrarily modify a key, the tree's ordering would be immediately broken.

Node handles bypass this limitation. `extract` allows you to "pluck" a node entirely out of the tree, returning an independent node handle (of type `std::map<K, V>::node_type`). This handle owns the node's resources; it exists outside of any map (removing it doesn't affect other elements), and it doesn't copy the value—it is the original node itself. Once extracted, you can modify its key (because it is now detached from the tree, so changing the key doesn't violate any ordering invariants), and then `insert` it back.

Therefore, since C++17, there is only one legitimate way to "change a map element's key": **extract → modify key → insert**.

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

Notice that `value` is still `"alpha"`—throughout the entire process, `value` was never copied or moved; we simply moved the original node. This is "zero-copy relocation."

The second use case is migrating nodes between containers. If we have two maps and want to move specific nodes from one to the other, we can just use `extract` + `insert`. Again, this does not copy the `value`:

```cpp
std::map<int, std::string> a, b;
a[1] = "x";
a[2] = "y";

// 把 a 里的节点 1 整个搬到 b
auto node = a.extract(1);
b.insert(std::move(node));
```

The third use case is `merge`, which handles everything in one go. `m1.merge(m2)` moves all nodes from `m2` whose keys do not conflict with those in `m1` into `m1` entirely. This is also zero-copy:

```cpp
std::map<int, std::string> m1{{1, "a"}, {2, "b"}};
std::map<int, std::string> m2{{2, "dup"}, {3, "c"}};

m1.merge(m2);
// m1: {1, 2, 3}；m2 里只剩下 key=2 那个（因为 m1 已有 2，冲突没搬走）
```

The complexity of `merge` is O(n·log n) (where n is the number of elements moved), but there is absolutely no copying of `value` elements. This saves significant overhead when migrating large objects (for example, when `value` is a large `vector` or a long string).

## Are Transparent Comparators Actually Faster? Let's Run a Benchmark

First, a quick side note: the underlying `map` implementation in libstdc++, libc++, and MSVC STL is a red-black tree in all three cases. The behavior is identical (as mandated by the standard), though the details of node layout and memory allocation differ. In daily engineering work, we don't need to worry about this; just knowing that "behavior is consistent, implementation varies" is enough.

However, there is a more interesting question worth verifying ourselves: transparent comparators claim to save temporary objects, but are they actually faster? Many people (myself included, before writing this) might assume that "saving construction must be faster." Instead of guessing, let's just run the code and see.

We will prepare a map with a string key, using a long string (44 characters, exceeding the Small String Optimization (SSO) limit, so temporary construction will hit the heap), and then compare three lookup methods: A uses the default comparator with a `const char*` lookup (which constructs a temporary string); B uses a transparent comparator with `const char*`; and C uses a transparent comparator with `string_view`.

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

The results likely contradict your intuition—**B is actually the slowest**, while C is the fastest. Why? The key is that `const char*` does not cache the length. A red-black tree lookup requires `log(n)` comparisons (about 14 here). In B, every comparison involves a raw `const char*` against a `std::string` inside the tree, necessitating a scan to the terminating `'\0'` to calculate the length (`strlen`) each time. With 14 comparisons, that's 14 `strlen` calls. In A, although we pay the cost of constructing a temporary `std::string` (heap allocation) once, the subsequent 14 comparisons are string-to-string, using the cached lengths for `memcmp`, making it faster overall. C uses `string_view`, which calculates and caches the length once upon construction. Subsequent comparisons reuse this length, avoiding repeated `strlen` calls and temporary string construction, making it the fastest.

So, remember this common pitfall: **heterogeneous lookup needs to be paired with `string_view` to actually improve performance; pairing it with `const char*` can actually be slower**. Simply slapping `std::less<>` in there while using the wrong lookup type can cause performance to degrade instead of improve.

## Wrapping Up

The `map` and `set` family of containers may look like simple containers that "sort by key and offer O(log n) lookup," but underneath, they rely on a red-black tree, an implementation chosen by all three major standard libraries. Keep these key properties in mind, and you'll use `map` with confidence: element addresses are stable (insertion does not invalidate iterators, and deletion only invalidates the erased element), making them suitable for registries or observer-like structures that require stable handles. C++14 heterogeneous comparators allow you to avoid creating temporary objects when looking up string keys (but remember to use `string_view` for the lookup type to actually speed things up; using `const char*` can be slower). C++17 node handles provide the only legal way to move keys with zero-copy and to modify keys. As for `set`, it's just the version where the value is omitted, but all the rules remain the same.

In the next article, we will follow this thread to look at map's "unordered sibling," `unordered_map`—swapping the red-black tree's logarithmic lookup for a hash table's amortized constant-time lookup represents a completely different set of trade-offs.

Want to try it out yourself? Check out the online example below (you can run it and view the assembly):

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
- [Container Iterator Invalidation Rules — cppreference](https://en.cppreference.com/w/cpp/container#Iterator_invalidation)
- [N3657: C++14 Heterogeneous Lookup Proposal](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2013/n3657.htm)
