---
chapter: 11
cpp_standard:
- 11
- 14
- 17
- 20
description: A container selection guide, common STL pitfalls, and performance fundamentals
difficulty: beginner
order: 4
platform: host
prerequisites:
- First Look at the Algorithms Library
reading_time_minutes: 19
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: Common STL Patterns
translation:
  source: documents/vol1-fundamentals/ch11/04-stl-patterns.md
  source_hash: eb76b672d8d0eae7c5214e6c0f2ae2ac8ae5c263f8b68ec7aecb43c91b47bfc8
  translated_at: '2026-09-25T11:58:47+00:00'
  engine: anthropic
  token_count: 4800
---
# Common STL Patterns: Container Choice and Pitfall Avoidance in Real Code

Over the previous three chapters we tackled `vector`, the associative containers, and the algorithms library—each chapter digging deep into its own territory. But when we actually sit down to write code, the question is rarely "how do I use this container" or "how do I call that algorithm"; it's "which container should I pick", "why is my program so slow", or "how did I step on the iterator-invalidation rake again". These are cross-container, cross-algorithm problems, and they call for a systematic perspective.

This chapter's job is to string those scattered pieces of knowledge into one thread: first pin down the highest-frequency decision problem—"which container for which scenario"—then walk through the biggest pitfalls in day-to-day STL use, then cover some performance fundamentals, and finally tie container selection, algorithm pairing, and pitfall defense all together in one comprehensive hands-on program. By the end of this chapter, your understanding of the STL will level up from "knowing how to use it" to "knowing how to use it right".

## Make the Choice First — A Container Selection Guide

Plenty of people finish a tour of the containers feeling more torn than before: which one should I actually use? In the vast majority of scenarios, though, the decision logic is crystal clear. Let's walk through it by core need:

If our data is sequential, its count changes at runtime, and we need random access, `std::vector` is almost always the first pick. Its elements sit contiguously in memory, so CPU cache prefetching works efficiently; subscript access is O(1), and insertion and removal at the end are amortized O(1). Its only weak spot is O(n) insertion and deletion in the middle—but most programs don't need frequent insertions in the middle anyway.

If we need "give me the value for this key" and don't need to traverse in key order, `std::unordered_map` is the most efficient choice, with average O(1) lookup. If we also need ordered traversal by key or range queries, switch to `std::map`.

If we need to maintain a "no duplicate elements" collection, use `std::set`. If we only need to answer "is this thing in there" and don't need ordering, `std::unordered_set` is faster.

If the number of elements is fixed at compile time and needs no dynamic growth or shrinkage, use `std::array`—a zero-overhead fixed-size array that skips vector's dynamic-allocation cost and is every bit as efficient as a C array.

Let's organize this into a decision table:

| Core need | First choice | Characteristics |
|----------|----------|------|
| Sequential storage, random access | `std::vector` | Contiguous memory, cache-friendly |
| Fast lookup by key (no ordering needed) | `std::unordered_map` | Average O(1) lookup |
| Lookup by key with ordered traversal | `std::map` | O(log n), red-black tree |
| A set of unique elements | `std::set` | Automatic deduplication, ordered |
| Fixed-size array | `std::array` | Zero overhead, stack allocation |

This table covers 90% of everyday decisions. The remaining 10% involves `deque` (a double-ended queue with O(1) insertion and deletion at both ends), `list` (a doubly linked list with O(1) insertion and deletion in the middle but dismal cache behavior), `multimap` / `multiset` (duplicate keys allowed), and so on—look those up in the documentation when you meet them.

One practical rule of thumb is worth committing to memory: **when in doubt, use `vector`**. Bjarne Stroustrup (the father of C++) and many C++ experts have hammered on this point repeatedly. `vector` performs respectably in most scenarios; even when its theoretical complexity isn't optimal, its cache friendliness often lets it win real benchmarks. Only when you can articulate exactly "why vector won't work here" is it time to consider another container.

## Where the STL Most Easily Goes Wrong

After using the STL for a while, you'll notice that the real headache usually isn't "how do I call this interface"—it's the traps where "it compiles, maybe even runs fine, but the logic is already wrong". Here we walk through the most common pitfalls one by one; every one of them has been stepped on for real, by the author or by C++ developers the author knows.

### Pitfall 1: Iterator Invalidation

We touched on this issue back in the `vector` chapter, but it affects more than `vector`, and it strikes at more than reallocation time. The core rule goes like this: for `vector` and `string`, any operation that can trigger memory reallocation (`push_back`, `emplace_back`, `insert`, or a `reserve` that forces reallocation) invalidates all iterators, pointers, and references. Even without reallocation, `insert` and `erase` invalidate iterators after the affected position. For `deque`, any insertion invalidates all iterators. For `map`, `set`, `unordered_map`, and `unordered_set`, `erase` only invalidates the iterators pointing at the erased element—every other iterator is unaffected. That last distinction matters enormously.

```cpp
std::vector<int> v = {1, 2, 3, 4, 5};
auto it = v.begin() + 2;  // points to 3
v.push_back(6);           // may trigger reallocation
// it is now a dangling iterator—dereferencing it is undefined behavior

std::map<int, std::string> m = {{1, "a"}, {2, "b"}, {3, "c"}};
auto mit = m.find(2);
m.erase(1);               // erases the element with key=1
// mit is still valid—map's erase doesn't affect other iterators
```

The practical upshot of this distinction: when we need to erase elements while traversing a `map`, we can do it directly through the iterator; erasing while traversing a `vector` demands extra care. Let's look at that more concrete scenario next.

Once we've saved an iterator, any operation that can modify the container's structure must be treated as "may invalidate the iterator". Don't take it on faith that "I only push_back-ed one element, it should be fine"—vector's growth policy is implementation-defined, and we cannot predict which push_back triggers the reallocation. If we genuinely need to keep using information about a position after modifying the container, use an index rather than an iterator, because an index is logically stable.

### Pitfall 2: Modifying a Container While Traversing It

Here's a genuinely classic crash site. First, an example that "looks fine at first glance but will blow up":

```cpp
std::vector<int> v = {1, 2, 3, 4, 5, 6};
for (auto it = v.begin(); it != v.end(); ++it) {
    if (*it % 2 == 0) {
        v.erase(it);  // Undefined behavior! it is already invalidated
    }
}
```

Watch what happens: after `erase` is called, `it` is invalidated, and doing `++it` on it afterwards is undefined behavior. The correct approach uses `erase`'s return value, which is an iterator to the element after the erased one:

```cpp
for (auto it = v.begin(); it != v.end(); /* no ++it here */) {
    if (*it % 2 == 0) {
        it = v.erase(it);  // erase returns an iterator to the next element
    } else {
        ++it;
    }
}
```

This style is easy to get wrong, though—one moment of carelessness and we forget that the `erase` branch must not do `++it`. The more recommended approach first moves the elements to be deleted to the end with `std::remove_if`, then erases them in one shot:

```cpp
// Before C++20
auto it = std::remove_if(v.begin(), v.end(), [](int x) { return x % 2 == 0; });
v.erase(it, v.end());

// C++20—one line and done
std::erase_if(v, [](int x) { return x % 2 == 0; });
```

For `map` and `set`, the safe way to erase during traversal looks slightly different. Because before C++11 `erase` returned `void`, the traditional idiom was `m.erase(it++)`—copy the iterator, increment it, then pass it to erase. Since C++11, the associative containers' `erase` also returns the next iterator, so the pattern matches vector's: `it = m.erase(it)`.

Never modify a container's structure (insert or erase elements) inside a range-for loop. A range-for runs on iterators underneath, and we can't get `erase`'s return value out of a range-for. With sanitizers enabled, this class of bug gets caught easily; without them, it may "just happen to run", showing no symptoms at all during debugging, and then falling over under some particular production workload—agonizing to debug.

### Pitfall 3: map's operator[] Silently Inserts Elements

We covered this pitfall in detail in the associative-containers chapter, but it makes appearances far too often, so let's stress it once more from a "pattern" angle. If the key doesn't exist, `map[key]` automatically inserts a default-constructed element. Two consequences follow: on a `const map`, `operator[]` flat-out fails to compile, because it is a modifying operation; and if we only wanted to check whether a key exists but reached for `operator[]`, the map gets silently modified.

The sneakiest scenario we're most likely to hit is accidentally triggering `operator[]` in the middle of a traversal:

```cpp
std::map<std::string, int> word_count = {{"hello", 2}, {"world", 1}};


// "Safely" reading the values of all keys—not actually safe!
for (const auto& [word, count] : word_count) {
    // if word_count[some_other_key] is called here, the map gets modified
    // modifying the container structure inside a range-for = undefined behavior
}
```

Granted, the example above is a bit extreme; the subtler variant is calling some function inside the loop body, and that function internally does an `operator[]` access on the map. Hence the core principle: **for read-only lookups, always use `find`, `count`, or `contains` (C++20); reserve `operator[]` for cases that genuinely want "create on access"**.

If our value type has no default constructor (say, a class that only accepts construction with arguments), then `operator[]` won't even compile when the key is missing—which is actually a good thing: the compiler blocks the pitfall for us. The truly dangerous types are the default-constructible ones like `int` and `string`, where `operator[]` quietly inserts a 0 or an empty string, the logic is wrong, and the program keeps running without complaint.

## Understanding Performance — Cache, Reservation, and Choice

Pitfalls covered—now let's talk performance. After learning the time complexities of the various containers, many people assume that picking a container means picking between O(1) and O(log n). In practice, though, the caching machinery of a modern CPU often has a bigger impact on performance than algorithmic complexity.

### Contiguous Memory and Cache Friendliness

A CPU accesses memory far more slowly than it executes instructions, which is why modern CPUs carry multiple cache levels (L1, L2, L3). When the CPU reads data at some address, it loads an entire block of nearby data (usually 64 bytes, i.e., one cache line) into the cache. This means that when we are sequentially traversing a contiguous-memory data structure, the first access brings a whole block of data into the cache, and subsequent accesses hit the cache directly—at terrific speed.

The elements of `std::vector` and `std::array` are packed tightly in memory, so traversal achieves a very high cache hit rate. Each node of `std::list`, by contrast, is allocated independently, with no rhyme or reason to where the nodes land in memory; traversal hits main memory almost every time, and the cache hit rate is dismal. Even though `list` does middle insertion and deletion in O(1) while `vector` pays O(n), vector is often faster in actual runs, because the power of CPU cache prefetching makes up for the theoretical-complexity disadvantage.

One classic benchmark conclusion: for containers storing small elements like `int` or `double`, `vector`'s linear search (O(n)) is often faster than `list`'s node-by-node traversal when n is below roughly 1000. That isn't because O(n) beats O(1)—it's because the cache advantage brought by contiguous memory is simply that large.

### The Importance of reserve

A `vector` growth spurt involves three steps—"allocate new memory -> copy/move all elements -> free the old memory"—and it isn't cheap. If we know roughly how many elements we will store beforehand, one `reserve` call allocates the space up front and eliminates the reallocation overhead entirely:

```cpp
std::vector<int> v;
v.reserve(10000);  // one allocation; the 10000 push_backs after it trigger zero reallocations
for (int i = 0; i < 10000; ++i) {
    v.push_back(i);
}
```

`unordered_map` has an analogous concept: we can use `reserve` to pre-allocate enough buckets and cut down the number of rehashes. When inserting a large number of elements into an `unordered_map`, a single `reserve` often drops the total time by 30% or more.

### Small String Optimization for string

One lesser-known but very practical fact: most standard library implementations use "Small String Optimization" (SSO). When the length of a `std::string` is below a certain threshold (usually 15-22 bytes, depending on the implementation), the string data is stored directly in a buffer inside the string object, with no heap allocation needed. This means copying, assigning, and destroying short strings are all very fast. In real-world development most strings are short (variable names, config keys, log messages, and the like), so SSO quietly saves us a large amount of memory-allocation overhead.

## Hands-On Practice — Putting the STL Patterns to Work

Now let's knead everything this chapter has discussed—container selection, pitfall defense, performance awareness—into one comprehensive program. The scenario is this: we have a batch of sensor readings and need to deduplicate, filter out outliers, sort, compute statistics, and print the final analysis report.

```cpp
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

/// A single sensor reading
struct Reading {
    std::string sensor_id;
    double value;
    uint32_t timestamp;
};

/// Analysis report
struct Report {
    std::string sensor_id;
    double min_val;
    double max_val;
    double avg_val;
    std::size_t count;
};

/// Filter outliers: group by sensor and drop readings more than kSigma standard deviations from that sensor's mean
void filter_outliers(std::vector<Reading>& readings, double k_sigma)
{
    if (readings.empty()) {
        return;
    }

    // Group by sensor and compute the mean and standard deviation of each group
    std::unordered_map<std::string, std::vector<double>> groups;
    for (const auto& r : readings) {
        groups[r.sensor_id].push_back(r.value);
    }

    std::unordered_map<std::string, std::pair<double, double>> stats;
    for (const auto& [id, values] : groups) {
        double sum = std::accumulate(values.begin(), values.end(), 0.0);
        double mean = sum / static_cast<double>(values.size());

        double sq_sum = std::accumulate(values.begin(), values.end(), 0.0,
            [mean](double acc, double v) { return acc + (v - mean) * (v - mean); });
        double stddev = std::sqrt(sq_sum / static_cast<double>(values.size()));

        stats[id] = {mean, stddev};
    }

    // remove-erase to drop the outliers
    auto it = std::remove_if(readings.begin(), readings.end(),
        [&](const Reading& r) {
            const auto& [mean, stddev] = stats[r.sensor_id];
            return std::abs(r.value - mean) > k_sigma * stddev;
        });
    readings.erase(it, readings.end());
}

/// Generate an analysis report for each sensor
std::vector<Report> generate_reports(std::vector<Reading>& readings)
{
    // Group by sensor with an unordered_map (no ordered traversal needed, O(1) lookup)
    std::unordered_map<std::string, std::vector<Reading>> groups;
    groups.reserve(16);  // pre-allocate to reduce rehashing

    for (auto& r : readings) {
        groups[r.sensor_id].push_back(std::move(r));
    }

    std::vector<Report> reports;
    reports.reserve(groups.size());

    for (auto& [id, recs] : groups) {
        if (recs.empty()) {
            continue;
        }

        // Sort by timestamp
        std::sort(recs.begin(), recs.end(),
            [](const Reading& a, const Reading& b) {
                return a.timestamp < b.timestamp;
            });

        // Compute statistics with STL algorithms
        auto [min_it, max_it] = std::minmax_element(recs.begin(), recs.end(),
            [](const Reading& a, const Reading& b) {
                return a.value < b.value;
            });

        double sum = std::accumulate(recs.begin(), recs.end(), 0.0,
            [](double acc, const Reading& r) { return acc + r.value; });

        reports.push_back({
            id,
            min_it->value,
            max_it->value,
            sum / static_cast<double>(recs.size()),
            recs.size()
        });
    }

    // Sort by sensor ID for output, keeping the results stable
    std::sort(reports.begin(), reports.end(),
        [](const Report& a, const Report& b) { return a.sensor_id < b.sensor_id; });

    return reports;
}

/// Remove duplicate readings (same sensor + same timestamp counts as a duplicate)
void deduplicate(std::vector<Reading>& readings)
{
    // Track the (sensor_id, timestamp) pairs already seen in an unordered_set
    struct Key {
        std::string sensor_id;
        uint32_t timestamp;
    };

    // Custom hash and equality—required by unordered_set
    struct KeyHash {
        std::size_t operator()(const Key& k) const
        {
            auto h1 = std::hash<std::string>{}(k.sensor_id);
            auto h2 = std::hash<uint32_t>{}(k.timestamp);
            return h1 ^ (h2 << 1);  // a simple hash combination
        }
    };

    struct KeyEqual {
        bool operator()(const Key& a, const Key& b) const
        {
            return a.sensor_id == b.sensor_id && a.timestamp == b.timestamp;
        }
    };

    std::unordered_set<Key, KeyHash, KeyEqual> seen;
    seen.reserve(readings.size());

    auto it = std::remove_if(readings.begin(), readings.end(),
        [&seen](const Reading& r) {
            Key k{r.sensor_id, r.timestamp};
            if (seen.count(k)) {
                return true;  // duplicate: mark for removal
            }
            seen.insert(k);
            return false;
        });
    readings.erase(it, readings.end());
}

int main()
{
    // Simulated sensor data—duplicates and outliers included
    std::vector<Reading> readings = {
        {"temp-01", 22.5, 1001},
        {"temp-01", 22.7, 1002},
        {"temp-01", 22.5, 1001},  // duplicate
        {"temp-01", 85.0, 1003},  // outlier
        {"temp-01", 22.9, 1004},
        {"temp-01", 22.6, 1005},
        {"temp-01", 23.0, 1006},
        {"press-01", 1013.2, 1001},
        {"press-01", 1013.5, 1002},
        {"press-01", 1013.2, 1001},  // duplicate
        {"press-01", 12.0, 1003},    // outlier
        {"press-01", 1013.8, 1004},
        {"press-01", 1013.0, 1005},
        {"press-01", 1013.6, 1006},
    };

    std::cout << "=== Raw readings: " << readings.size() << " ===\n";

    // Step 1: deduplicate
    deduplicate(readings);
    std::cout << "After dedup: " << readings.size() << "\n";

    // Step 2: filter outliers (2 standard deviations)
    filter_outliers(readings, 2.0);
    std::cout << "After outlier filter: " << readings.size() << "\n";

    // Step 3: generate the analysis reports
    auto reports = generate_reports(readings);

    std::cout << "\n=== Analysis Reports ===\n";
    for (const auto& r : reports) {
        std::cout << "  [" << r.sensor_id << "] "
                  << "min=" << r.min_val << ", max=" << r.max_val
                  << ", avg=" << r.avg_val
                  << ", n=" << r.count << "\n";
    }

    return 0;
}
```

Compile and run:

```bash
g++ -std=c++20 -Wall -Wextra -o stl_patterns stl_patterns.cpp && ./stl_patterns
```

Expected output:

```text
=== Raw readings: 14 ===
After dedup: 12
After outlier filter: 10

=== Analysis Reports ===
  [press-01] min=1013, max=1013.8, avg=1013.42, n=5
  [temp-01] min=22.5, max=23, avg=22.74, n=5
```

Let's unpack the design decisions in this program layer by layer. Deduplication picks `unordered_set` over `set` because we only care whether we've seen an entry before, not about ordered traversal, so O(1) lookup suits us better than O(log n). Note that the custom `KeyHash` and `KeyEqual` are mandatory here—`Key` is a custom struct, and the standard library provides no default hash for it. If we forget to provide them, the compiler delivers its "friendly reminder" as a wall of template instantiation errors.

The key design in outlier filtering is **computing statistics per sensor group**. Different sensors differ wildly in units and value ranges (temperature around 22-23°C, pressure around 1013 hPa); if we mixed all readings together to compute the mean and standard deviation, no individual value would ever be flagged as anomalous. So `filter_outliers` groups by `sensor_id` first, then computes the mean and standard deviation independently for each group—only then do 85.0°C from the temperature sensor and 12.0 hPa from the pressure sensor get correctly identified as outliers.

Grouping uses `unordered_map<string, vector<Reading>>`, again because no ordered traversal by key is needed. `reserve(16)` is an experience-based pre-allocation—the number of sensors is usually small, so one allocation avoids later rehashes. Outlier filtering uses `remove_if` + `erase` rather than erasing directly mid-traversal, which is both safe and clear. And the statistics are done entirely with STL algorithms: `minmax_element` finds the min and max in one pass, `accumulate` sums them up—no hand-written loops anywhere.

## Try It Yourself — Exercises

### Exercise 1: Container Selection in Practice

Pick the most suitable container for each scenario below and explain your reasoning: (a) storing a game character's backpack item list, with frequent additions and removals at the end; (b) maintaining a spell checker's dictionary, with frequent checks for whether a word exists; (c) storing a student-ID-to-name mapping for all students in a class, printed in student-ID order; (d) storing the data of a 3x3 matrix.

### Exercise 2: Fix the Buggy Code

The following code hides at least two STL traps. Find and fix them:

```cpp
std::vector<int> data = {1, 2, 3, 4, 5, 6, 7, 8};
for (auto it = data.begin(); it != data.end(); ++it) {
    if (*it % 2 == 0) {
        data.erase(it);
    }
}
```

### Exercise 3: Performance Comparison

Write a benchmark: store 100000 random integers in a `std::vector<int>` and a `std::list<int>` respectively, then use `<chrono>` to time and compare the two on (a) sequential traversal and summation, and (b) sorting. Feel the impact of cache friendliness with real data.

---

> **References**
>
> - [cppreference: Container library](https://en.cppreference.com/w/cpp/container)
> - [cppreference: std::erase (C++20)](https://en.cppreference.com/w/cpp/container/vector/erase2)
> - [Bjarne Stroustrup: Why you should avoid linked lists](https://www.youtube.com/watch?v=YQs6IC-vgmo)
> - [cppreference: Iterator invalidation](https://en.cppreference.com/w/cpp/container#Iterator_invalidation)
