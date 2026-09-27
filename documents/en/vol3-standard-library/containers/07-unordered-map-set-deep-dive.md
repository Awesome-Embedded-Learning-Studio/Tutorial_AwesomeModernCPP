---
chapter: 7
cpp_standard:
- 11
- 14
- 17
- 20
description: 'Explains std::unordered_map/set all the way down to the hash table underneath:
  buckets and separate chaining, load factor and rehash, average O(1) vs worst-case
  O(n), how to write a custom hash, why rehash no longer invalidates references
  since C++14, and how to decide between it and map.'
difficulty: intermediate
order: 7
platform: host
prerequisites:
- 'Deep Dive into map and set: Red-Black Trees, Heterogeneous Lookup, and Node Handles'
reading_time_minutes: 10
related:
- 'Container Selection Guide: Picking the Right One by Operations, Memory, and Invalidation Rules'
tags:
- host
- cpp-modern
- intermediate
- unordered_map
- 容器
title: 'Deep Dive into unordered_map and unordered_set: Hash Tables, Buckets, and Custom Hashing'
translation:
  source: documents/vol3-standard-library/containers/07-unordered-map-set-deep-dive.md
  source_hash: 99e24b989cc0f15825bfe5b0f3939f6caad91139a9602606db0a3454dff0789d
  translated_at: '2026-09-26T02:30:22+00:00'
  engine: anthropic
  token_count: 5100
---
# Deep Dive into unordered_map and unordered_set: Hash Tables, Buckets, and Custom Hashing

## Related to map, but a Whole Different World Underneath

In the previous article we covered map: underneath it sits a red-black tree, and lookup is logarithmic O(log n). This article's `unordered_map` has "unordered" right in the name — it doesn't sort, and what that sacrifice buys is something fiercer: average O(1) lookup. But there is no free lunch. The price of O(1) is that the tree underneath gets swapped for a hash table, which drags in a whole new set of machinery: buckets, load factor, rehash, custom hash. In this article we will take `unordered_map` and `unordered_set` apart, from the hash table underneath all the way up to real-world usage.

First, put it side by side with map and the differences jump out at a glance:

| | `map` / `set` | `unordered_map` / `unordered_set` |
|---|---|---|
| Underlying structure | Red-black tree | Hash table |
| Ordered | Yes (sorted by key) | No |
| Find/insert/erase | O(log n) | Average O(1), worst case O(n) |
| Custom keys need | `operator<` | A hash + `operator==` |
| Does insert invalidate iterators | No | Possibly (when a rehash triggers) |

In one sentence: if you need ordered traversal, or range operations like "predecessor/successor", stay with map; if all you do is lookup, insert, and erase and you don't care about order, `unordered` is usually faster. That choice is not absolute — we will come back to it later.

## Under the Hood: A Hash Table with Buckets, Chains, and a Load Factor

Underneath, `unordered_map` is a hash table, and the vast majority of implementations use **separate chaining**: an array of buckets, with each bucket holding a linked list (or something similar). When you insert an element, the hash function first computes the key's hash value, then takes it modulo the bucket count to decide which bucket it lands in; if that bucket already has elements, the new one is appended to the chain, and a lookup linearly scans that short chain.

```cpp
// Simplified skeleton of a separate-chaining hash table (standard library internals; details vary by vendor)
struct HashTable {
    std::vector<Bucket> buckets;   // bucket array; inside each bucket is a chain of elements with the same hash
};
// Insert/lookup positioning: bucket_index = hash(key) % buckets.size();
```

Here is a key concept: the **load factor**. It equals `size() / bucket_count()` — on average, how many elements hang off each bucket. The more crowded the buckets, the longer the chains, the slower the lookup. The standard library sets an upper bound `max_load_factor()`, 1.0 by default — once the load factor exceeds that bound, the container **rehashes**: it allocates a larger bucket array (usually growing to roughly twice the size) and re-hashes every element into the new buckets.

A rehash is the most expensive operation on an `unordered_map`: it relocates every element, at O(n) complexity. Amortized over each insertion it is still constant, but the single moment a rehash happens brings a noticeable pause. That is why, in real projects, if you can estimate the number of elements up front, it is best to call `reserve(n)` before inserting — it opens enough buckets in one shot and spares you the repeated rehashing afterwards.

```cpp
std::unordered_map<int, std::string> m;
m.reserve(10000);   // pre-size the buckets, avoiding repeated rehashes as elements are inserted one by one
```

Let's run it and watch how load_factor triggers a rehash:

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

Watch the bucket_count jump sequence: 1 → 13 → 29 → 59 → 127 — **all primes**. That is exactly libstdc++'s choice (prime bucket counts spread `hash % bucket_count` more evenly). And every jump happens at the moment `size` has just exceeded `bucket_count` (that is, load_factor has crossed 1.0): at size 14, 14/13 > 1.0 triggers growth to 29; at size 30, 30/29 > 1.0 triggers growth to 59; and so on. That is the intuitive picture of "load factor over the limit → rehash into more buckets".

## Complexity and Iterator Invalidation: Different from map Again

Complexity first, stated plainly: `unordered_map` find, insert, and erase are O(1) **on average** and O(n) **in the worst case**. When does the worst case happen? When a large number of keys hash to the same value (all landing in the same bucket), the hash table degenerates into one long chain and lookup becomes a linear scan. A good hash function plus a reasonable load factor keeps the collision probability tiny, so in practice it is almost always O(1); but the Standard honestly documents the worst-case O(n), because in theory it really can happen.

On iterator invalidation, `unordered_map` differs from map again — and is a bit "fiercer" than map. The rules:

- **rehash** (triggered by an insertion, or by a manual `reserve` / `rehash`): **invalidates all iterators**; but since C++14, **references and pointers to elements are not invalidated by a rehash**
- **erase**: invalidates only the erased element's own iterator/reference; everything else is unaffected

This one deserves special attention. In the previous article we said inserting into a map never invalidates iterators; with `unordered_map`, because an insertion may rehash, iterators do get invalidated. But the interesting part is that since C++14 the Standard additionally guarantees that a rehash does not disturb references and pointers to elements — in other words, the `value_type&` you hold and your pointers to elements remain valid after a rehash; only iterators go bad. That is a practical guarantee: you can safely hold a long-lived reference to an `unordered_map` element, even if a rehash happens along the way.

```cpp
#include <iostream>
#include <unordered_map>
#include <string>

int main()
{
    std::unordered_map<int, std::string> m;
    m[1] = "alpha";
    std::string& ref = m.at(1);   // hold a reference to the element

    m.reserve(1000);              // triggers a rehash; all iterators invalidated
    for (int i = 100; i < 200; ++i) {
        m[i] = "x";               // bulk insertion may rehash again
    }

    std::cout << ref << '\n';     // since C++14, the reference is still valid
    return 0;
}
```

```bash
g++ -std=c++20 -O2 -o /tmp/umap_ref /tmp/umap_ref.cpp && /tmp/umap_ref
```

```text
alpha
```

## Custom Hashes: Making Your Own Types Work as Keys

By default, `std::hash<T>` is defined only for built-in types and common standard library types (string, integer types, and so on). To use your own type as an `unordered_map` key, you have to tell it two things: **how to compute the hash** and **how to compare for equality**.

Equality defaults to `operator==` (via `std::equal_to`). There are two ways to supply the hash: specialize `std::hash`, or pass a custom Hash type directly as a template parameter to `unordered_map`. Let's look at an example that uses a 2D point as a key, written the specialize-`std::hash` way:

```cpp
#include <iostream>
#include <unordered_map>

struct Point {
    int x, y;
    bool operator==(Point const& o) const { return x == o.x && y == o.y; }
};

// Specialize std::hash<Point>
namespace std {
template <>
struct hash<Point> {
    std::size_t operator()(Point const& p) const noexcept
    {
        // Combine the two ints into one size_t; simplified — use a better mix in production
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

There is an iron rule here: **the hash and `==` must agree**. That is, if `a == b` is true, then `hash(a)` must equal `hash(b)` — otherwise equal elements land in different buckets and lookups can't find them. The reverse is not required (when `hash(a) == hash(b)`, `a` need not equal `b`; that is just a collision, a perfectly normal phenomenon). The `x*31 + y` above is a toy blend for demonstration; in production you can use `boost::hash_combine` or a more deliberate mixing function to push the collision probability down further.

## Hash Collisions and DoS: Why libstdc++'s hash Comes with a Bit of Randomness

Hash tables have a notorious attack surface called **hash flooding**: the attacker carefully crafts a large batch of keys with identical hash values and feeds them to your program; all the elements crowd into the same bucket, lookup degrades from O(1) to O(n), and the CPU gets saturated — one of the reasons many early web services were dragged down.

libstdc++'s answer: its `std::hash<std::string>` hashes with a random seed generated at each program start (built on a seeded, high-quality hash function). That way, the same input lands in different buckets in different processes, and the attacker cannot pre-craft an input that "just happens" to collide across the board. This is libstdc++'s implementation strategy (libc++ and MSVC STL each have their own approach), and the Standard does not mandate it — but it is worth knowing in practice: if you use a custom key type and the keys may come from untrusted input, the quality of the hash function you write directly determines your resistance to DoS.

## Hands On: How Much Faster Is unordered_map than map

Just saying "average O(1) beats O(log n)" is too abstract — let's measure it directly. Set up a map and an unordered_map with 100,000 elements each, and run one million lookups on each:

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

Above are the results from GCC 16.1.1 on my machine: map at about 48 ms, unordered_map at about 2 ms — **unordered is nearly an order of magnitude faster**. The exact milliseconds will vary with your machine, but the gap at this scale is stable: at 100,000 elements, one map lookup takes log₂(100000) ≈ 17 comparisons, while unordered_map hits directly in average O(1), and across a million lookups the accumulated difference is exactly this dramatic. That is the core reason `unordered_map` exists.

## A Few Closing Words: When to Choose It

`unordered_map` and `unordered_set` throw away the "ordered" property and get average O(1) lookup in exchange. Underneath they are hash tables — a bucket array with one chain per bucket, with the load factor controlling when to rehash and grow. A few things to remember when using them: insertion may trigger a rehash, which invalidates iterators but, since C++14, not references to elements; a custom key type must supply both a hash and `==`, and the two must agree; and if keys come from untrusted input, the quality of the hash function is what stands between you and collision-based DoS.

As for when to pick it over map: if you don't care about order and your workload is dominated by lookup/insert/erase, `unordered` is usually faster; if you need ordered traversal, range queries, or a stable iteration order, go back to map. In the next article we leave associative containers behind and look at the sequential-container alternatives to vector — deque and list.

Want to run it yourself and see? Open the online example below (it runs, and it shows the assembly too):

<OnlineCompilerDemo
  title="unordered_map: hash buckets, the rehash prime sequence, reserve"
  source-path="code/examples/vol3/07_unordered_map_set.cpp"
  description="Watch the bucket_count jumps as rehash triggers, the bucket distribution, and reserve pre-sizing the buckets"
  allow-run
/>

## References

- [std::unordered_map — cppreference](https://en.cppreference.com/w/cpp/container/unordered_map)
- [std::unordered_set — cppreference](https://en.cppreference.com/w/cpp/container/unordered_set)
- [std::hash — cppreference](https://en.cppreference.com/w/cpp/utility/hash)
- [Master table of container iterator invalidation rules — cppreference](https://en.cppreference.com/w/cpp/container#Iterator_invalidation)
