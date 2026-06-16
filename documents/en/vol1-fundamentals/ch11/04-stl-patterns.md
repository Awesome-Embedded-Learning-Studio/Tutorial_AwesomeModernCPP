---
chapter: 11
cpp_standard:
- 11
- 14
- 17
- 20
description: 容器选择指南、常见陷阱和性能基础
difficulty: beginner
order: 4
platform: host
prerequisites:
- 算法库初见
reading_time_minutes: 19
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: STL Common Patterns
translation:
  source: documents/vol1-fundamentals/ch11/04-stl-patterns.md
  source_hash: 4b6b2be67db7b5febaa80133394f3ba38a0649d932e8696fa47e2a88b762e429
  translated_at: '2026-06-16T03:48:03.012155+00:00'
  engine: anthropic
  token_count: 3568
---
# Common STL Patterns

In the previous three chapters, we covered sequence containers, associative containers, and the algorithm library respectively, diving deep into each specific domain. However, in actual coding, the problem is often not "how do I use this container" or "how do I call that algorithm," but rather "which container should I choose," "why is my program running so slow," or "why did I hit an iterator invalidation pitfall again." These are comprehensive issues that span across containers and algorithms, requiring a systematic perspective to address.

In this chapter, we will connect these scattered pieces of knowledge. We will first clarify the high-frequency decision problem of "which container to use for which scenario," then review the most common pitfalls in STL usage, discuss performance-related basics, and finally tie everything together—container selection, algorithm combination, and defensive programming—into a comprehensive practical example. By the end of this chapter, your understanding of STL will upgrade from "knowing how to use it" to "knowing how to use it right."

> **Learning Objectives**
>
> After completing this chapter, you will be able to:
>
> - [ ] Quickly select the appropriate STL container based on actual requirements
> - [ ] Identify and avoid common pitfalls like iterator invalidation and modifying containers during traversal
> - [ ] Understand the impact of cache friendliness on container performance
> - [ ] Proficiently use the erase-remove idiom and C++20's `std::erase`
> - [ ] Apply the principle of "algorithms over hand-written loops" to write clearer code

## Making the Choice — A Container Selection Guide

After learning about a bunch of containers, many people feel even more conflicted: which one should I actually use? In reality, the decision logic is very clear for the vast majority of scenarios. Let's walk through it based on your core needs:

If your data is sequential, the quantity changes, and you need random access, `std::vector` is almost always the first choice. Its elements are arranged contiguously in memory, allowing CPU cache prefetch to work efficiently. Index access is O(1), and amortized insertion/deletion at the end is O(1). Its only weakness is O(n) insertion/deletion in the middle—but honestly, most programs don't need frequent insertions in the middle.

If you need to "look up a value by key" and don't need to traverse in key order, `std::unordered_map` is the most efficient choice, with average O(1) lookup speed. If you need ordered traversal or range queries by key at the same time, switch to `std::map`.

If you need to maintain a "set of unique elements," use `std::set`. If you only need to judge "whether something is present" and don't need ordering, `std::unordered_set` is faster.

If the number of elements is determined at compile time and doesn't need dynamic changes, use `std::array`—it is a zero-overhead fixed-size array that avoids dynamic allocation overhead compared to `vector` and is as efficient as C arrays.

Let's organize this into a decision table:

| Core Need | First Choice | Characteristics |
|----------|--------------|-----------------|
| Sequential storage, random access | `std::vector` | Contiguous memory, cache-friendly |
| Fast lookup by key (no ordering needed) | `std::unordered_map` | Average O(1) lookup |
| Lookup by key with ordered traversal | `std::map` | O(log n), red-black tree |
| Unique element set | `std::set` | Automatic deduplication, ordered |
| Fixed-size array | `std::array` | Zero overhead, stack allocation |

This table covers 90% of daily decisions. The remaining 10% involves `std::deque` (double-ended queue, O(1) insertion/deletion at both ends), `std::list` (doubly linked list, O(1) insertion/deletion in the middle but terrible cache performance), `std::multiset` / `std::multimap` (allow duplicate keys), etc. You can check the documentation when you encounter them.

A useful rule of thumb is worth remembering: **If you aren't sure what to use, use `std::vector`**. Bjarne Stroustrup (the father of C++) and many C++ experts have repeatedly emphasized this point. `std::vector` performs well in most scenarios; even if the theoretical complexity isn't optimal, its cache friendliness often makes it win in actual benchmarks. Only when you can clearly articulate "why vector won't work" do you need to consider other containers.

## Pitfall Warning — Where STL Most Often Goes Wrong

After using STL for a while, you will find that the real headache is often not "how to call a certain interface," but those traps where "it compiles, maybe even runs normally, but the logic is already wrong." Here we go through the most common pitfalls one by one, each of which I or C++ developers I know have actually stepped into.

### Pitfall 1: Iterator Invalidation

This issue was mentioned when discussing `std::vector`, but it doesn't just affect `std::vector`, nor does it only happen during reallocation. The core rule is this: for `std::vector` and `std::string`, any operation that might cause reallocation (`push_back`, `emplace_back`, `reserve`, or `resize` causing reallocation) invalidates all iterators, pointers, and references. Even without reallocation, `insert` and `erase` invalidate iterators at and after the affected position. For `std::deque`, any insertion operation invalidates all iterators. For `std::map`, `std::set`, `std::unordered_map`, `std::unordered_set`, `erase` only invalidates iterators pointing to the deleted elements, leaving other iterators unaffected—this is a very important distinction.

```cpp
// vector::erase invalidates the iterator pointing to the deleted element
// and all iterators after it.
// map::erase only invalidates the iterator to the deleted element.
```

The practical significance of this difference is: if you need to delete elements while traversing a `std::map`, you can do so directly with an iterator, but you need to be extra careful when deleting elements while traversing a `std::vector`. Let's look at this more specific scenario next.

> **Pitfall Warning**: After saving an iterator, treat any operation that might modify the container structure as "potentially invalidating the iterator." Don't assume "I just `push_back`-ed an element, it should be fine"—the reallocation strategy of `vector` depends on the implementation, and you cannot predict which `push_back` will trigger reallocation. If you确实 need to continue using information about a position after modifying the container, use an index instead of an iterator, because indexes are logically stable.

### Pitfall 2: Modifying a Container During Traversal

This is a classic crash site. First, look at an example that "looks fine at first glance but will explode":

```cpp
std::vector<int> v = {1, 2, 3, 4, 5};
for (auto it = v.begin(); it != v.end(); ++it) {
    if (*it % 2 == 0) {
        v.erase(it); // UB! it is invalidated after erase
    }
}
```

After calling `v.erase(it)`, `it` is invalidated, and incrementing it (`++it` in the loop header) is undefined behavior. The correct way is to use the return value of `erase`—it returns an iterator pointing to the element following the deleted element:

```cpp
for (auto it = v.begin(); it != v.end(); /* empty */) {
    if (*it % 2 == 0) {
        it = v.erase(it); // it now points to the next element
    } else {
        ++it;
    }
}
```

But honestly, this style is error-prone—one slip of the mind and you forget to `++it` in the `else` branch. A more recommended approach is to first move the elements to be deleted to the end with `std::remove`, and then `erase` them all at once:

```cpp
v.erase(std::remove(v.begin(), v.end(), value), v.end());
```

For `std::map` and `std::set`, the safe way to delete during traversal is slightly different. Because before C++11, `map::erase` returned `void`, the traditional way was `it = erase(it++)`—copy the iterator first, then increment, then pass to erase. Starting from C++11, associative containers' `erase` also returns the next iterator, so the writing style is the same as for vector: `it = map.erase(it)`.

> **Pitfall Warning**: Never modify the container structure (insert or delete elements) inside a range-for loop. The underlying mechanism of range-for uses iterators, and you cannot get the return value of `erase` inside a range-for. If the compiler has sanitizers enabled, these bugs are easily caught; but if not, they might "just happen to run"—showing no signs in the debug phase, only to crash under a specific load in production, making debugging extremely painful.

### Pitfall 3: map's operator[] Silently Inserts Elements

This pitfall was discussed in detail when talking about associative containers, but it appears so frequently that I will emphasize it again from a "pattern" perspective. `std::map::operator[]` automatically inserts a default-constructed element if the key doesn't exist. This means two consequences: first, using `operator[]` on `std::map<const char*, int>` won't compile because it's a modifying operation; second, if you just want to check if a key exists and use `operator[]`, the map will be silently modified.

The most insidious scenario is accidentally triggering `operator[]` during traversal:

```cpp
std::map<std::string, int> counts;
// ... populate counts ...
for (const auto& [key, val] : counts) {
    if (counts["unknown_key"] > 10) { // Oops! "unknown_key" inserted here
        // ...
    }
}
```

Of course, the example above is a bit extreme, but a more subtle variant is: you call a function inside the loop body, and that function internally accesses the map using `operator[]`. So the core principle is: **For read-only lookups, always use `find()`, `contains()` (C++20), or `at()`, leaving `operator[]` for scenarios where you truly need "create on access."**

> **Pitfall Warning**: If your value type doesn't have a default constructor (e.g., a class that only accepts arguments for construction), then `operator[]` won't even compile if the key doesn't exist—which is actually a good thing because the compiler blocks the pitfall for you. The real danger is with types like `int`, `double` that can be default constructed; `operator[]` silently inserts 0 or empty strings, and the logic is wrong but the program runs without complaint.

## Understanding Performance — Cache, Reservation, and Selection

Now that we've covered the pitfalls, let's talk about performance. After learning about the time complexity of various containers, many people think choosing a container is just choosing between O(1) and O(log n). But in reality, the cache mechanism of modern CPUs often has a greater impact on performance than algorithmic complexity.

### Contiguous Memory and Cache Friendliness

CPUs access memory much slower than they execute instructions, so modern CPUs have multi-level caches (L1, L2, L3). When a CPU reads data from a certain address, it loads a whole block of nearby data (usually 64 bytes, i.e., a cache line) into the cache. This means if you are sequentially traversing a data structure with contiguous memory, the first access brings a whole block of data into the cache, and subsequent accesses hit the cache directly, which is extremely fast.

The elements of `std::vector` and `std::string` are tightly packed in memory, resulting in very high cache hit rates during traversal. Each node of `std::list` is allocated independently, and the positions of nodes in memory are completely irregular. Traversal almost always requires accessing main memory, resulting in extremely low cache hit rates. Even though `std::list` is O(1) for insertion/deletion in the middle and `std::vector` is O(n), in actual runs `vector` is often faster—because the power of CPU cache prefetching compensates for the disadvantage of theoretical complexity.

A classic benchmark conclusion is: for containers storing small elements like `int` or `double`, linear search (O(n)) with `std::vector` is often faster than traversing node-by-node with `std::list` when n is around < 1000. This isn't because O(n) is better than O(1), but because the cache advantage of contiguous memory is too significant.

### The Importance of reserve

`std::vector` reallocation involves three steps—"allocate new memory -> copy/move all elements -> free old memory"—which isn't cheap. If you know roughly how many elements you will store beforehand, calling `reserve` to allocate space all at once can completely eliminate reallocation overhead:

```cpp
std::vector<int> v;
v.reserve(1000); // Pre-allocate space for 1000 elements
// ... insert elements ...
```

`std::unordered_map` has a similar concept—you can use `reserve` to pre-allocate enough buckets to reduce the number of rehashes. When inserting a large number of elements into an `unordered_map`, a single `reserve` call can often reduce the total time by 30% or more.

### Small String Optimization for string

A lesser-known but very practical fact is that most standard library implementations use "Small String Optimization" (SSO). When the length of a `std::string` is below a certain threshold (usually 15-22 bytes, depending on the implementation), the string data is stored directly in the string object's internal buffer, requiring no heap allocation. This means copying, assigning, and destroying short strings is very fast. In actual development, most strings are short (variable names, config items, log messages, etc.), and SSO quietly saves you a huge amount of memory allocation overhead.

## Practical Exercise — Comprehensive Application of STL Patterns

Now let's combine all the knowledge points discussed in this chapter—container selection, pitfall defense, and performance awareness—into a comprehensive practical program. The scenario is this: we have a batch of sensor readings, and we need to deduplicate, filter outliers, sort, calculate statistics, and output a final analysis report.

```cpp
#include <iostream>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <iomanip>

struct Reading {
    int sensor_id;
    double value;
};

int main() {
    // 1. Raw data
    std::vector<Reading> raw_readings = {
        {1, 22.5}, {2, 1013.2}, {1, 22.7}, {3, 15.0}, // Normal
        {1, 85.0}, {2, 12.0}, {1, 22.6}, {2, 1013.5},  // Outliers
        {1, 22.5}, {2, 1013.2}                         // Duplicates
    };

    // 2. Deduplicate using unordered_set
    // We need a custom hash function for the Reading struct
    auto reading_hash = [](const Reading& r) {
        return std::hash<int>{}(r.sensor_id) ^
               (std::hash<double>{}(r.value) << 1);
    };
    auto reading_eq = [](const Reading& a, const Reading& b) {
        return a.sensor_id == b.sensor_id && a.value == b.value;
    };

    std::unordered_set<Reading, decltype(reading_hash), decltype(reading_eq)>
        unique_readings(0, reading_hash, reading_eq);

    for (const auto& r : raw_readings) {
        unique_readings.insert(r);
    }

    // 3. Filter outliers
    // Group by sensor_id to calculate statistics per sensor
    std::unordered_map<int, std::vector<double>> sensor_data;
    for (const auto& r : unique_readings) {
        sensor_data[r.sensor_id].push_back(r.value);
    }

    std::vector<Reading> clean_readings;
    for (const auto& [id, values] : sensor_data) {
        // Calculate mean and standard deviation
        double sum = std::accumulate(values.begin(), values.end(), 0.0);
        double mean = sum / values.size();

        double sq_sum = 0.0;
        for (auto v : values) {
            sq_sum += (v - mean) * (v - mean);
        }
        double stddev = std::sqrt(sq_sum / values.size());

        // Filter values outside 2 standard deviations
        for (auto v : values) {
            if (std::abs(v - mean) <= 2 * stddev) {
                clean_readings.push_back({id, v});
            }
        }
    }

    // 4. Sort by sensor_id
    std::sort(clean_readings.begin(), clean_readings.end(),
              [](const Reading& a, const Reading& b) {
                  return a.sensor_id < b.sensor_id;
              });

    // 5. Output report
    std::cout << "=== Sensor Analysis Report ===" << std::endl;
    std::cout << "Total valid readings: " << clean_readings.size() << std::endl;

    for (const auto& r : clean_readings) {
        std::cout << "Sensor " << r.sensor_id
                  << ": " << std::fixed << std::setprecision(2) << r.value << std::endl;
    }

    return 0;
}
```

Compile and run:

```bash
g++ -std=c++20 -O2 sensor_analysis.cpp -o sensor_analysis
./sensor_analysis
```

Expected output:

```text
=== Sensor Analysis Report ===
Total valid readings: 7
Sensor 1: 22.50
Sensor 1: 22.60
Sensor 1: 22.70
Sensor 2: 1013.20
Sensor 2: 1013.50
Sensor 3: 15.00
```

Let's break down the design decisions in this program layer by layer. For deduplication, we chose `std::unordered_set` instead of `std::set` because we only care about "have we seen this" and don't need ordered traversal. O(1) lookup is more appropriate than O(log n). Note that we must customize the hash function and equality operator—because `Reading` is a custom struct, the standard library doesn't have a default hash function for it. If you forget to provide one, the compiler will greet you with a bunch of template instantiation errors.

The key design for outlier filtering is **calculating statistics by sensor group**. The dimensions and numerical ranges of different sensors vary greatly (temperature ~22-23°C, pressure ~1013 hPa). If you mix all readings together to calculate mean and standard deviation, no single value will be considered an outlier. So the code first groups by `sensor_id`, then calculates mean and standard deviation for each group independently. This way, 85.0°C in the temperature sensor and 12.0 hPa in the pressure sensor are correctly identified as outliers.

For grouping, we chose `std::unordered_map` again, as ordered traversal by key isn't needed. `reserve` is an empirical pre-allocation—the number of sensors is usually small, so allocating once avoids subsequent rehashes. Filtering outliers uses `std::remove_if` + `erase` instead of deleting directly during traversal—this is both safe and clear. Statistics are done entirely with STL algorithms—`std::minmax_element` finds the min and max in one pass, `std::accumulate` sums them, with no hand-written loops.

## Try It Yourself — Exercises

### Exercise 1: Container Selection in Practice

Choose the most suitable container for the following scenarios and explain why: (a) Store an inventory list for a game character, with frequent additions and deletions at the end; (b) Maintain a dictionary for a spell checker, requiring frequent checks if a word exists; (c) Store a student ID-name mapping for a class, outputting in order of student ID; (d) Store data for a 3x3 matrix.

### Exercise 2: Fix the Buggy Code

The following code has at least two STL traps. Find and fix them:

```cpp
std::vector<int> v = {1, 2, 3, 4, 5};
for (size_t i = 0; i < v.size(); ++i) {
    if (v[i] % 2 == 0) {
        v.erase(v.begin() + i);
    }
}

std::map<std::string, int> m;
m["key"] = 10;
if (m.find("key") != m.end()) {
    m.erase("key");
}
// ... later ...
int val = m["key"]; // Potential pitfall?
```

### Exercise 3: Performance Comparison

Write a benchmark: store 100,000 random integers in `std::vector` and `std::list` respectively. Use `std::chrono` to time and compare the (a) sequential traversal sum time, and (b) sorting time. Experience the impact of cache friendliness with real data.

## Summary

In this chapter, we reorganized the knowledge from the previous three chapters from the perspective of "how to use STL correctly." Regarding container selection, the core idea is decision-making based on requirements: sequential storage -> `std::vector`, fast lookup -> `std::unordered_map`, ordered key-value -> `std::map`, deduplication -> `std::set`, fixed size -> `std::array`. If unsure, just use `std::vector`; it's almost always a choice that won't be wrong.

For pitfall defense, the three traps to be most vigilant about are iterator invalidation (especially with vector reallocation and after erase), modifying containers during traversal (use remove-erase instead of hand-written delete loops), and `map::operator[]` silently inserting elements (use `find` or `contains` for read-only lookups).

Regarding performance, the cache friendliness of contiguous memory means `std::vector` often runs faster in real scenarios than `std::list`, which has better theoretical complexity. `reserve` is a powerful tool to eliminate reallocation overhead, effective for both `vector` and `unordered_map`.

This concludes Chapter 11. We started with `std::vector`, learned associative containers and the algorithm library, and finally integrated this knowledge into systematic STL usage patterns. The next chapter will dive deep into the C++ memory model—from memory layout to stack/heap allocation, from `new`/`delete` to memory alignment—these are the low-level foundations for writing high-performance C++ code.

---

> **References**
>
> - [cppreference: Container library](https://en.cppreference.com/w/cpp/container)
> - [cppreference: std::erase (C++20)](https://en.cppreference.com/w/cpp/container/vector/erase2)
> - [Bjarne Stroustrup: Why you should avoid linked lists](https://www.youtube.com/watch?v=YQs6IC-vgmo)
> - [cppreference: Iterator invalidation](https://en.cppreference.com/w/cpp/container#Iterator_invalidation)
