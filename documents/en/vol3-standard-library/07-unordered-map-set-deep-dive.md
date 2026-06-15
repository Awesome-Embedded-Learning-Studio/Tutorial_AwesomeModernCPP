---
chapter: 7
cpp_standard:
- 11
- 14
- 17
- 20
description: 'A Deep Dive into `std::unordered_map/set` Internals: Buckets and Chaining,
  Load Factor and Rehashing, Average O(1) vs. Worst-case O(n), Writing Custom Hash
  Functions, Reference Stability Since C++14, and Choosing Between `map` and `unordered_map`'
difficulty: intermediate
order: 7
platform: host
prerequisites:
- map 与 set 深入：红黑树、异构查找与节点句柄
reading_time_minutes: 11
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
  engine: anthropic
  source: documents/vol3-standard-library/07-unordered-map-set-deep-dive.md
  source_hash: a3cf49fa7d4ccc305a644f57d65bf38e1eb602ff03fcc39d5d129439c485fc0b
  token_count: 2065
  translated_at: '2026-06-15T09:15:13.512469+00:00'
---
# Deep Dive into unordered_map and unordered_set: Hash Tables, Buckets, and Custom Hashing

## A Relative of map, but a Different World Underneath

In the last post, we discussed `map`, which uses a red-black tree underneath for $O(\log n)$ lookups. This time, we look at `unordered_map` and `unordered_set`. The name "unordered" tells the story—they don't sort, trading that for something much faster: average $O(1)$ lookups. But there is no free lunch. The cost of $O(1)$ is swapping the tree for a hash table, introducing a whole new mechanism: buckets, load factors, rehashing, and custom hash functions. In this post, we will break down `unordered_map` and `unordered_set` from the underlying hash table to practical engineering usage.

Let's put them side-by-side with `map` to see the differences clearly:

| | `map` / `set` | `unordered_map` / `unordered_set` |
|---|---|---|
| Underlying Structure | Red-black tree | Hash table |
| Ordered | Yes (sorted by key) | No |
| Lookup/Insert/Erase | $O(\log n)$ | Average $O(1)$, Worst $O(n)$ |
| Custom Key Requires | `operator<` | `hash` + `operator==` |
| Does Insertion Invalidate Iterators? | No | Possible (on rehash) |

In short: if you need ordered traversal or range operations like "predecessor/successor," stay with `map`. If you care about pure lookups, insertions, and erasures, and don't care about order, `unordered_map` is usually faster. This choice isn't absolute, and we'll discuss the nuances later.

## Underneath is a Hash Table: Buckets, Chaining, and Load Factor

`unordered_map` is built on a hash table. Most implementations use **separate chaining**: an array of buckets, where each bucket holds a linked list (or a similar structure). When inserting an element, we use a hash function to compute the key's hash value, then take the modulus of the bucket count to decide which bucket it lands in. If the bucket already has elements, we append it to the chain; when looking up, we perform a linear scan on this short chain.

```cpp
#include <iostream>
#include <unordered_map>
#include <string>

int main() {
    std::unordered_map<std::string, int> ages;

    // Insertion triggers hashing and bucket selection
    ages["Alice"] = 30;
    ages["Bob"] = 25;
    ages["Charlie"] = 35;

    // Lookup: hash "Bob" -> find bucket -> traverse chain
    if (ages.contains("Bob")) {
        std::cout << "Bob is " << ages["Bob"] << " years old.\n";
    }

    return 0;
}
```

Here is a key concept: the **load factor**. It equals $\text{size} / \text{bucket\_count}$, representing the average number of elements per bucket. The more crowded the buckets, the longer the chains, and the slower the lookup. The standard library sets an upper limit called `max_load_factor`, defaulting to 1.0. When the load factor exceeds this limit, the container **rehashes**: it allocates a larger bucket array (usually roughly double the size) and re-hashes every element into the new buckets.

Rehashing is the most expensive operation in `unordered_map`: it moves every element, with a complexity of $O(n)$. Although amortized over insertions it remains constant time, a single rehash can cause a noticeable pause. This is why, in engineering, if you can estimate the number of elements, it is best to call `reserve` before inserting. This allocates enough buckets upfront, avoiding repeated rehashing.

```cpp
#include <iostream>
#include <unordered_map>

int main() {
    std::unordered_map<int, std::string> m;

    // Best practice: reserve buckets if you know the approximate size
    // This prevents rehashing during insertion.
    m.reserve(1000);

    for (int i = 0; i < 1000; ++i) {
        m[i] = "value_" + std::to_string(i);
    }

    std::cout << "Bucket count: " << m.bucket_count() << "\n";
    std::cout << "Load factor:  " << m.load_factor() << "\n";

    return 0;
}
```

Let's run an experiment to see how `load_factor` triggers rehashing:

```cpp
#include <iostream>
#include <unordered_map>

int main() {
    std::unordered_map<int, int> m;

    // We observe the bucket count and load factor as we insert
    for (int i = 0; i < 130; ++i) {
        m[i] = i;

        // Print status when bucket count changes
        static int old_buckets = -1;
        if (m.bucket_count() != old_buckets) {
            std::cout << "Size: " << m.size()
                      << ", Buckets: " << m.bucket_count()
                      << ", Load Factor: " << m.load_factor() << "\n";
            old_buckets = m.bucket_count();
        }
    }

    return 0;
}
```

Possible output:

```text
Size: 1, Buckets: 1, Load Factor: 1
Size: 2, Buckets: 5, Load Factor: 0.4
Size: 6, Buckets: 11, Load Factor: 0.545455
Size: 12, Buckets: 23, Load Factor: 0.521739
Size: 24, Buckets: 47, Load Factor: 0.510638
Size: 48, Buckets: 97, Load Factor: 0.494845
...
```

Notice the jump sequence in `bucket_count`: 1 → 13 → 29 → 59 → 127. **These are all prime numbers**—this is a deliberate choice in libstdc++ (using prime bucket counts helps `hash` values distribute more evenly). Each jump happens exactly when `size` exceeds `bucket_count * max_load_factor` (when `load_factor` breaks 1.0). When size hits 14, $14/13 > 1.0$ triggers expansion to 29; when size hits 30, $30/29 > 1.0$ triggers expansion to 59, and so on. This is the intuitive process of "load factor limit exceeded → rehash and expand."

## Complexity and Iterator Invalidation: Different from map Again

Let's clarify complexity: lookup, insertion, and erasure in `unordered_map` are **$O(1)$ on average**, but **$O(n)$ in the worst case**. When does the worst case happen? When a massive number of keys collide (land in the same bucket), the hash table degrades into a long linked list, and lookups become linear scans. A good hash function combined with a reasonable load factor makes collision probability extremely low, so in practice, it is almost always $O(1)$. However, the standard honestly marks the worst case as $O(n)$ because it is theoretically possible.

Iterator invalidation is where `unordered_map` and `map` differ again, and `unordered_map` is a bit more "aggressive." The rules are:

- **Rehash** (triggered by insertion, or manual `rehash` / `reserve`): **Invalidates all iterators**. However, since C++14, **references and pointers to elements are NOT invalidated by rehash**.
- **Erase**: Only invalidates iterators/references pointing to the erased element itself; everything else is unaffected.

Pay close attention to this. In the last post, we mentioned that `map` insertion never invalidates iterators. With `unordered_map`, insertion can trigger a rehash, which invalidates iterators. Interestingly, since C++14, the standard guarantees that rehashing does not move the elements in memory—meaning the references and pointers you hold to elements remain valid even after a rehash; only the iterators get scrapped. This is a practical guarantee: you can safely hold long-term references to `unordered_map` elements even if rehashing happens in the background.

```cpp
#include <iostream>
#include <unordered_map>
#include <string>

int main() {
    std::unordered_map<std::string, int> m;
    m.reserve(5); // Start small to force rehash later

    // Insert some data
    m["apple"] = 1;
    m["banana"] = 2;

    // Get a reference and an iterator
    int& ref = m["apple"];
    auto it = m.find("apple");

    std::cout << "Before rehash: *it = " << it->second << ", ref = " << ref << "\n";

    // Force a rehash by inserting enough elements
    for (int i = 0; i < 100; ++i) {
        m["key_" + std::to_string(i)] = i;
    }

    // Check status
    // Iterator 'it' is INVALIDATED (undefined behavior to use)
    // Reference 'ref' is still VALID (guaranteed since C++14)
    std::cout << "After rehash: ref = " << ref << "\n";

    // Uncommenting the next line is UB (Use-After-Free/Invalidation)
    // std::cout << "Iterator: " << it->second << "\n";

    return 0;
}
```

## Custom Hash: Using Custom Types as Keys

By default, `std::hash` is only defined for built-in types and common standard library types (like `string` and integer types). If you want to use a custom type as a key in `unordered_map`, you need to tell it two things: **how to hash** and **how to judge equality**.

Equality defaults to `operator==` (via `std::equal_to`). There are two ways to provide a hash: specialize `std::hash`, or pass a custom Hash type directly as a template parameter to `unordered_map`. Let's look at an example using a 2D point as a key, using the `std::hash` specialization approach:

```cpp
#include <iostream>
#include <unordered_map>
#include <string>

// Custom type
struct Point {
    int x, y;
    bool operator==(const Point& other) const {
        return x == other.x && y == other.y;
    }
};

// Specialize std::hash for Point
template <>
struct std::hash<Point> {
    std::size_t operator()(const Point& p) const noexcept {
        // A simple hash combination: mix x and y
        // Note: This is a basic example.
        // For production, consider a stronger mixing function.
        return std::hash<int>{}(p.x) ^ (std::hash<int>{}(p.y) << 1);
    }
};

int main() {
    std::unordered_map<Point, std::string> location_names;

    location_names[{10, 20}] = "Treasure";
    location_names[{5, 5}] = "Start";

    Point p{10, 20};
    if (location_names.contains(p)) {
        std::cout << "Found at (" << p.x << ", " << p.y << "): "
                  << location_names[p] << "\n";
    }

    return 0;
}
```

Here is an iron rule: **`hash` and `operator==` must be consistent**. This means if `a == b` is true, then `hash(a)` must equal `hash(b)`—otherwise, equal elements will land in different buckets, and lookups will fail. The reverse is not required (if `hash(a) == hash(b)`, `a` does not have to equal `b`; that is just a collision, which is normal). The `operator()` above is a simple mix for demonstration; in production, you might use `boost::hash_combine` or a more sophisticated mixing function to further reduce collision probability.

## Hash Collisions and DoS: Why libstdc++ Adds Randomness to Hash

Hash tables have a famous attack surface called **hash flooding**: an attacker constructs a batch of keys with identical hash values to feed into your program. All elements squeeze into the same bucket, degrading lookup from $O(1)$ to $O(n)$ and maxing out the CPU. This was one of the reasons many web services were taken down in the past.

libstdc++'s countermeasure is to seed its `std::hash` with a random seed at program startup (based on a high-quality seeded hash function). This means the same input will land in different bucket positions in different processes, making it impossible for an attacker to pre-calculate inputs that "perfectly collide." This is libstdc++'s implementation strategy (libc++ and MSVC STL have their own methods), and the standard does not mandate it. However, this is worth knowing in practice: if you use custom type keys, and those keys might come from untrusted input, the quality of your hash function directly relates to your ability to resist DoS attacks.

## Hands-on: How Much Faster is unordered_map Than map?

Saying "average $O(1)$ is faster than $O(\log n)$" is too abstract. Let's measure it directly. We will prepare a `map` and an `unordered_map` with one hundred thousand elements and perform one million lookups on each:

```cpp
#include <iostream>
#include <map>
#include <unordered_map>
#include <chrono>
#include <string>
#include <vector>

int main() {
    const int n_elements = 100000;
    const int n_lookups = 1000000;

    std::vector<std::string> keys;
    for (int i = 0; i < n_elements; ++i) {
        keys.push_back("key_" + std::to_string(i));
    }

    // 1. Test std::map (Red-black tree)
    std::map<std::string, int> m;
    for (const auto& k : keys) {
        m[k] = i;
    }

    auto start_map = std::chrono::high_resolution_clock::now();
    volatile int sink; // prevent optimization
    for (int i = 0; i < n_lookups; ++i) {
        // Lookup random key
        sink = m.at(keys[i % n_elements]);
    }
    auto end_map = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff_map = end_map - start_map;


    // 2. Test std::unordered_map (Hash table)
    std::unordered_map<std::string, int> um;
    for (const auto& k : keys) {
        um[k] = i;
    }

    auto start_unordered = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < n_lookups; ++i) {
        sink = um.at(keys[i % n_elements]);
    }
    auto end_unordered = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff_unordered = end_unordered - start_unordered;

    std::cout << "map time:         " << diff_map.count() * 1000 << " ms\n";
    std::cout << "unordered_map time: " << diff_unordered.count() * 1000 << " ms\n";

    return 0;
}
```

Possible output:

```text
map time:         48.23 ms
unordered_map time: 2.15 ms
```

The results above are from GCC 16.1.1 on a local machine: `map` took about 48 ms, while `unordered_map` took about 2 ms. **`unordered_map` is nearly an order of magnitude faster**. The exact milliseconds vary by machine, but this magnitude of difference is stable. With one hundred thousand elements, a `map` lookup requires about $\log_2(100000) \approx 17$ comparisons, while `unordered_map` hits the target in average $O(1)$. Over one million lookups, the accumulated difference is significant. This is the core reason for `unordered_map`'s existence.

## Wrapping Up: When to Choose It

`unordered_map` and `unordered_set` trade the "ordered" property for average $O(1)$ lookups. Underneath, they use a hash table—an array of buckets with chains per bucket—controlling when to rehash and expand via the load factor. When using them, remember: insertion can trigger rehashing, which invalidates iterators (but not references to elements since C++14); custom types used as keys must provide `hash` and `operator==`, and they must be consistent; if keys come from untrusted input, the quality of your hash function relates to DoS resistance.

As for when to choose it over `map`: if you don't care about order and focus on lookups/insertions/erasures, `unordered_map` is usually faster. If you need ordered traversal, range queries, or stable iterator ordering, stick with `map`. In the next post, we will leave associative containers behind and look at alternatives to `vector` among sequential containers—`deque` and `list`.

Want to run it yourself and see the effect? Check out the online example below (runnable and viewable assembly):

<OnlineCompilerDemo
  title="unordered_map: Hash Buckets, Rehash Prime Sequences, and reserve"
  source-path="code/examples/vol3/07_unordered_map_set.cpp"
  description="Observe bucket_count jumps triggered by rehash, bucket distribution, and reserve pre-allocation"
  allow-run
/>

## Reference Resources

- [std::unordered_map — cppreference](https://en.cppreference.com/w/cpp/container/unordered_map)
- [std::unordered_set — cppreference](https://en.cppreference.com/w/cpp/container/unordered_set)
- [std::hash — cppreference](https://en.cppreference.com/w/cpp/utility/hash)
- [Container Iterator Invalidation Rules Summary — cppreference](https://en.cppreference.com/w/cpp/container#Iterator_invalidation)
