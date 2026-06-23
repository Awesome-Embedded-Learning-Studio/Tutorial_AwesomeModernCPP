---
chapter: 7
cpp_standard:
- 11
- 14
- 17
- 20
description: 'Deep dive into the underlying mechanics of `std::unordered_map/set`:
  buckets and chaining, load factor and rehash, average O(1) vs. worst-case O(n),
  writing custom hash functions, non-invalidating references on rehash since C++14,
  and decision-making between `map` and `unordered_map`.'
difficulty: intermediate
order: 7
platform: host
prerequisites:
- map 与 set 深入：红黑树、异构查找与节点句柄
reading_time_minutes: 10
related:
- 容器选择指南
tags:
- host
- cpp-modern
- intermediate
- unordered_map
- 容器
title: 'Deep Dive into unordered_map and unordered_set: Hash Tables, Buckets, and
  Custom Hash'
translation:
  source: documents/vol3-standard-library/containers/07-unordered-map-set-deep-dive.md
  source_hash: 99e24b989cc0f15825bfe5b0f3939f6caad91139a9602606db0a3454dff0789d
  translated_at: '2026-06-16T06:11:33.805063+00:00'
  engine: anthropic
  token_count: 2060
---
# Deep Dive into unordered_map and unordered_set: Hash Tables, Buckets, and Custom Hashing

## It's related to map, but the underlying implementation is a whole new world

In the previous post, we discussed `map`, which is backed by a red-black tree and offers logarithmic $O(\log n)$ lookup. In this post, we look at `unordered_map`. As the name implies, it is "unordered"—it sacrifices sorting for something more aggressive: average $O(1)$ lookup. But there is no such thing as a free lunch. The cost of $O(1)$ is swapping the underlying tree for a hash table, introducing a whole new set of mechanisms: buckets, load factor, rehashing, and custom hashing. In this post, we will cover `unordered_map` and `unordered_set` from the low-level hash table implementation to practical engineering usage.

Let's compare it with `map` side-by-side to see the differences clearly:

| | `map` / `set` | `unordered_map` / `unordered_set` |
|---|---|---|
| Underlying Structure | Red-black tree | Hash table |
| Ordered | Yes (sorted by key) | No |
| Lookup/Insert/Delete | $O(\log n)$ | Average $O(1)$, Worst case $O(n)$ |
| Custom Key Requirement | `operator<` | hash + `operator==` |
| Insertion Invalidates Iterators? | No | Possible (when rehash triggers) |

In a nutshell: if you need ordered traversal or range operations like "predecessor/successor," stick with `map`. If you purely need lookup, insertion, or deletion and don't care about order, `unordered` is usually faster. This choice isn't absolute, and we will discuss the nuances later.

## The Underlying Hash Table: Buckets, Linked Lists, and Load Factor

Under the hood, `unordered_map` is a hash table. Most implementations use **separate chaining**: an array of buckets, where each bucket holds a linked list (or a similar structure). When inserting an element, we use a hash function to calculate the key's hash value, then take the modulus of the bucket count to determine which bucket it falls into. If the bucket already contains elements, the new element is appended to the list; during lookup, we perform a linear scan on this short list.

```cpp
// 链地址法哈希表的简化骨架（标准库内部，各厂细节不同）
struct HashTable {
    std::vector<Bucket> buckets;   // bucket 数组，每个桶内部是同 hash 元素的链表
};
// 插入/查找定位：bucket_index = hash(key) % buckets.size();
```

Here is a key concept: the **load factor**. It equals `size() / bucket_count()`, representing the average number of elements in each bucket. The more crowded the buckets are, the longer the linked lists become, and the slower lookups become. The standard library sets a limit via `max_load_factor()`, which defaults to 1.0. When the load factor exceeds this limit, the container will **rehash**: it allocates a larger bucket array (usually expanding to about twice the size), and re-hashes and redistributes all elements into the new buckets.

Rehashing is the most expensive operation for `unordered_map`: it moves every single element, resulting in O(n) complexity. Although the cost is amortized to a constant time per insertion, a single rehash event causes a noticeable pause. This is why, in production code, if you can estimate the number of elements, it is best to call `reserve(n)` before inserting. This allocates sufficient buckets upfront, avoiding repeated rehashing later.

```cpp
std::unordered_map<int, std::string> m;
m.reserve(10000);   // 提前开好桶，避免逐个插入时的多次 rehash
```

Let's run this to see how `load_factor` triggers a rehash:

```cpp
#include <iostream>
#include <unordered_map>

int main()
{
    std::unordered_map<int, int> m;
    std::size_t prev = m.bucket_count();
    std::cout << "初始 bucket_count = " << prev << "\n";
    for (int i = 0; i < 100; ++i) {
        m[i] = i;
        if (m.bucket_count() != prev) {
            std::cout << "size=" << m.size()
                      << " rehash: " << prev << " -> " << m.bucket_count()
                      << " (load_factor=" << m.load_factor() << ")\n";
            prev = m.bucket_count();
        }
    }
    return 0;
}
```

```bash
g++ -std=c++20 -O2 -o /tmp/lf_rehash /tmp/lf_rehash.cpp && /tmp/lf_rehash
```

```text
初始 bucket_count = 1
size=1 rehash: 1 -> 13 (load_factor=0.0769231)
size=14 rehash: 13 -> 29 (load_factor=0.482759)
size=30 rehash: 29 -> 59 (load_factor=0.508475)
size=60 rehash: 59 -> 127 (load_factor=0.472441)
```

Pay close attention to the jump sequence of `bucket_count`: 1 → 13 → 29 → 59 → 127. **These are all prime numbers**—this is the specific choice made by libstdc++ (using a prime number of buckets ensures a more uniform distribution for `hash % bucket_count`). Each jump occurs the moment `size` exceeds `bucket_count` (meaning the load factor breaks 1.0): when `size` reaches 14, 14/13 > 1.0 triggers an expansion to 29; when `size` reaches 30, 30/29 > 1.0 triggers an expansion to 59, and so on. This visually demonstrates the process of "load factor limit exceeded → rehash and expand buckets."

## Complexity and Iterator Invalidation: Different from `map` Again

Let's clarify complexity first: lookup, insertion, and deletion in `unordered_map` are **O(1) on average**, but **O(n) in the worst case**. When does the worst case happen? When a large number of keys hash collide (landing in the same bucket), the hash table degenerates into a long linked list, and lookups become linear scans. A good hash function combined with a reasonable load factor makes the probability of collision extremely low, so in practice it is almost always O(1); however, the standard honestly specifies the worst case as O(n) because it is theoretically possible.

Regarding iterator invalidation, `unordered_map` differs from `map` again, and it is a bit more "aggressive." The rules are:

- **rehash** (triggered by insertion, or manual `reserve` / `rehash`): **invalidates all iterators**; however, since C++14, **references and pointers to elements are not invalidated by rehash**.
- **erase**: only invalidates the iterator/reference of the erased element itself; others are unaffected.

Pay special attention to this rule. In the previous article, we mentioned that `map` insertion never invalidates iterators; however, `unordered_map` iterators can be invalidated because insertion might trigger a rehash. Interestingly, since C++14, the standard provides an extra guarantee that rehashing does not invalidate references and pointers to elements. This means that the `value_type&` and element pointers you hold remain valid even after a rehash, while only the iterators are废弃. This is a practical guarantee: you can safely hold references to `unordered_map` elements for a long time, even if rehashing occurs in the meantime.

```cpp
#include <iostream>
#include <unordered_map>
#include <string>

int main()
{
    std::unordered_map<int, std::string> m;
    m[1] = "alpha";
    std::string& ref = m.at(1);   // 持有元素引用

    m.reserve(1000);              // 触发 rehash，迭代器全失效
    for (int i = 100; i < 200; ++i) {
        m[i] = "x";               // 大量插入可能再次 rehash
    }

    std::cout << ref << '\n';     // C++14 起，引用仍然有效
    return 0;
}
```

```bash
g++ -std=c++20 -O2 -o /tmp/umap_ref /tmp/umap_ref.cpp && /tmp/umap_ref
```

```text
alpha
```

## Custom Hash: Using Custom Types as Keys

By default, `std::hash<T>` is only defined for built-in types and common standard library types (like `string` or integer types). If we want to use a custom type as a key in `unordered_map`, we need to specify two things: **how to calculate the hash** and **how to determine equality**.

Equality checking defaults to `operator==` (via `std::equal_to`). There are two ways to provide the hash logic: specialize `std::hash`, or pass a custom Hash type directly as a template parameter to `unordered_map`. Let's look at an example using a 2D point as a key, using the `std::hash` specialization approach:

```cpp
#include <iostream>
#include <unordered_map>

struct Point {
    int x, y;
    bool operator==(Point const& o) const { return x == o.x && y == o.y; }
};

// 特化 std::hash<Point>
namespace std {
template <>
struct hash<Point> {
    std::size_t operator()(Point const& p) const noexcept
    {
        // 把两个 int 组合成一个 size_t；这是简化版，生产里用更好的混合
        return static_cast<std::size_t>(p.x) * 31 + static_cast<std::size_t>(p.y);
    }
};
}  // namespace std

int main()
{
    std::unordered_map<Point, std::string> grid;
    grid[{1, 2}] = "A";
    grid[{3, 4}] = "B";

    auto it = grid.find({1, 2});
    std::cout << (it != grid.end() ? it->second : "not found") << '\n';
    return 0;
}
```

```bash
g++ -std=c++20 -O2 -o /tmp/custom_hash /tmp/custom_hash.cpp && /tmp/custom_hash
```

```text
A
```

Here is an ironclad rule: **hash and `==` must be consistent**. This means that if `a == b` is true, then `hash(a)` must equal `hash(b)`—otherwise, equal elements would land in different buckets, and lookups would fail. The converse is not required (when `hash(a) == hash(b)`, `a` does not necessarily have to equal `b`; this is just a collision, which is a normal phenomenon). The `x*31 + y` above is a simple mix for demonstration purposes; in production, we can use `boost::hash_combine` or more sophisticated mixing functions to further reduce the probability of collisions.

## Hash Collisions and DoS: Why libstdc++ Adds Randomness to Your Hash

Hash tables have a well-known attack surface called **hash flooding**: an attacker carefully constructs a massive number of keys with identical hash values and feeds them to your program. All elements cram into a single bucket, degrading lookups from O(1) to O(n) and maxing out the CPU—this was one of the reasons many web services were taken down in the early days.

libstdc++'s countermeasure is that its `std::hash<std::string>` uses a random seed for hashing every time the program starts (based on a high-quality seeded hash function). This way, the same input lands in different buckets across different processes, making it impossible for an attacker to pre-calculate inputs that "just happen to collide completely." This is libstdc++'s implementation strategy (libc++ and MSVC STL have their own approaches), and the standard does not mandate it—but in practice, this is worth knowing: if you use a custom type as a key, and that key might come from untrusted input, the quality of your hash function directly impacts your resistance to DoS attacks.

## Hands-on: How Much Faster Is unordered_map Than map

Simply saying "average O(1) is faster than O(log n)" is too abstract, so let's measure it directly. We'll prepare a `map` and an `unordered_map` with one hundred thousand elements and perform one million lookups on each:

```cpp
#include <iostream>
#include <map>
#include <unordered_map>
#include <chrono>

int main()
{
    std::map<int, int> om;
    std::unordered_map<int, int> um;
    for (int i = 0; i < 100000; ++i) {
        om[i] = i;
        um[i] = i;
    }
    volatile int sink = 0;

    auto bench = [&](auto& m) {
        auto t0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < 1000000; ++i) {
            sink += m.find(i % 100000)->second;
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double, std::milli>(t1 - t0).count();
    };

    std::cout << "map:           " << bench(om) << " ms\n";
    std::cout << "unordered_map: " << bench(um) << " ms\n";
    return 0;
}
```

```bash
g++ -std=c++20 -O2 -o /tmp/uvm /tmp/uvm.cpp && /tmp/uvm
```

```text
map:           48.4 ms
unordered_map: 2.2 ms
```

The results above are from GCC 16.1.1 running locally: `map` takes about 48 ms, while `unordered_map` takes about 2 ms—**making the unordered version nearly an order of magnitude faster**. The exact milliseconds will vary depending on your machine, but this order-of-magnitude difference is stable. With one hundred thousand elements, a single lookup in `map` requires log₂(100000) ≈ 17 comparisons, whereas `unordered_map` averages O(1) direct hits. Over a million lookups, this accumulated difference becomes stark. This is the fundamental reason for the existence of `unordered_map`.

## Wrapping Up: When to Choose It

`unordered_map` and `unordered_set` discard the "ordering" property in exchange for average O(1) lookups. Under the hood, they use hash tables—a bucket array where each bucket holds a linked list, relying on the load factor to control when to rehash and expand. Here are a few things to remember when using them: insertion can trigger rehashing, which invalidates iterators (though since C++14, references to elements remain valid); custom types used as keys must provide both a hash function and `==` operator, and the two must be consistent; if keys come from untrusted input, the quality of the hash function is critical for mitigating hash collision DoS attacks.

As for when to choose it over `map`: if you don't care about order and your workload is primarily lookup/insertion/deletion, `unordered` is usually faster. If you need ordered traversal, range queries, or stable iterator ordering, stick with `map`. In the next article, we will move away from associative containers and explore alternatives to `vector` among sequential containers—`deque` and `list`.

Want to jump in and see the results in action? Check out the online example below (runnable and viewable assembly):

<OnlineCompilerDemo
  title="unordered_map: Hash Buckets, Rehash Prime Sequence, Reserve"
  source-path="code/examples/vol3/07_unordered_map_set.cpp"
  description="Observe bucket count jumps triggered by rehash, bucket distribution, and bucket pre-allocation via reserve"
  allow-run
/>

## Reference Resources

- [std::unordered_map — cppreference](https://en.cppreference.com/w/cpp/container/unordered_map)
- [std::unordered_set — cppreference](https://en.cppreference.com/w/cpp/container/unordered_set)
- [std::hash — cppreference](https://en.cppreference.com/w/cpp/utility/hash)
- [Container Iterator Invalidation Rules Summary — cppreference](https://en.cppreference.com/w/cpp/container#Iterator_invalidation)
