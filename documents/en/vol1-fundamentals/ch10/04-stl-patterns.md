---
chapter: 10
cpp_standard:
- 11
- 14
- 17
- 20
description: A container selection guide, common pitfalls, and performance fundamentals
difficulty: beginner
order: 4
platform: host
prerequisites:
- A First Look at the Algorithms Library
reading_time_minutes: 19
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: Common STL Patterns
translation:
  source: documents/vol1-fundamentals/ch10/04-stl-patterns.md
  source_hash: 5b7b2220440d741d7ce82ce7e2abb8c796d1889ddce7fd113c20cc8dcf47871c
  translated_at: '2026-09-27T04:05:04+00:00'
  engine: anthropic
  token_count: 10500
---
# Common STL Patterns: Choosing Containers and Dodging Pitfalls in Real Code

Over the previous three installments we tackled `vector`, the associative containers, and the algorithms library—each chapter digging deep in its own territory. But when we're actually writing code, the question is rarely "how do I use this container" or "how do I call that algorithm". It's "which container should I pick", "why is my program so slow", and "how did I step on the iterator-invalidation trap again". These are cross-container, cross-algorithm problems, and they call for a systematic view.

What this chapter does is string the scattered pieces so far into one thread: first the highest-frequency decision problem—"which container for which scenario"—then a tour of the biggest pitfalls in day-to-day STL use, then some performance fundamentals, and finally one comprehensive program that chains container selection, algorithm pairing, and pitfall defense together. Once you finish this chapter, your understanding of the STL will level up from "knows how to use it" to "knows how to use it right".

## Choose First — A Container Selection Guide

Plenty of folks finish a tour of all the containers and end up more torn than before: which one should I actually use? In the vast majority of scenarios, though, the decision logic is crisp. Let's walk it by core need:

If our data is sequential, its count changes, and we need random access, `std::vector` is almost always the first pick. Its elements sit contiguously in memory so CPU cache prefetching works efficiently, subscript access is O(1), growth and removal at the back are amortized O(1), and its only weak spot is O(n) insertion and erasure in the middle—though most programs don't frequently insert in the middle anyway.

If we need "give a key, fetch a value" and never need to walk the keys in order, `std::unordered_map` is the most efficient choice, with average O(1) lookup. If we also need ordered traversal by key or range queries, switch to `std::map`.

If we need to maintain a collection with "no duplicate elements", use `std::set`. If we merely need to test "is this thing in there" with no ordering required, `std::unordered_set` is faster.

If the element count is fixed at compile time and needs no dynamic growth or shrinkage, use `std::array`—a zero-overhead fixed-size array that skips vector's dynamic-allocation cost and is every bit as efficient as a C array.

Let's tidy all of this into a decision table:

| Core need | First-choice container | Traits |
|-----------|------------------------|--------|
| Sequential storage, random access | `std::vector` | Contiguous memory, cache-friendly |
| Fast lookup by key (no ordering needed) | `std::unordered_map` | Average O(1) lookup |
| Lookup by key with ordered traversal | `std::map` | O(log n), red-black tree |
| Collection of unique elements | `std::set` | Automatic deduplication, ordered |
| Fixed-size array | `std::array` | Zero overhead, stack allocation |

This table covers 90% of everyday decisions. The remaining 10% involves `deque` (a double-ended queue with O(1) insertion and erasure at both ends), `list` (a doubly linked list with O(1) insertion and erasure in the middle but dreadful cache behavior), `multimap` / `multiset` (which allow duplicate keys), and the like—when you run into one of those, go read the documentation then.

One rule of thumb is worth committing to memory: **when in doubt, use `vector`**. Bjarne Stroustrup (the father of C++) and many C++ experts have hammered on this point repeatedly. `vector` performs respectably in most scenarios; even when its theoretical complexity isn't optimal, its cache friendliness often lets it win real benchmarks. Only when you can state precisely "why vector won't do" should you consider another container.

## Where STL Code Most Easily Goes Wrong

After you've used the STL for a while, you'll notice that the real headaches are rarely "how do I call this interface" but the traps where "it compiles, it even runs, and the logic is already wrong". Here we walk through the most common ones, each a pit that the author or C++ developers the author knows have fallen into for real.

### Trap 1: Iterator Invalidation

We raised this issue when we covered `vector`, but it affects more than `vector`, and it strikes more than at growth time. The core rules run like this. For `vector` and `string`, any operation that can reallocate memory (a reallocation triggered by `push_back`, `emplace_back`, `insert`, or `reserve`) invalidates all iterators, pointers, and references. Even without reallocation, `insert` and `erase` invalidate the iterators after the affected position. For `deque`, any insertion invalidates all iterators. For `map`, `set`, `unordered_map`, and `unordered_set`, `erase` invalidates only the iterators pointing at the erased elements; the rest are unaffected—and that is a distinction of real importance.

```cpp
std::vector<int> v = {1, 2, 3, 4, 5};
auto it = v.begin() + 2;  // points to 3
v.push_back(6);           // may trigger a reallocation
// it is now a dangling iterator — dereferencing it is undefined behavior

std::map<int, std::string> m = {{1, "a"}, {2, "b"}, {3, "c"}};
auto mit = m.find(2);
m.erase(1);               // erases the element with key=1
// mit is still valid — map's erase does not affect other iterators
```

The practical upshot of this difference: when we need to erase elements while walking a `map`, we can do it directly through iterators; erasing while walking a `vector` demands real care. Let's look at that more concrete scenario next.

Once we've saved an iterator, treat any operation that can alter the container's structure as "may invalidate my iterator". Don't assume "I only pushed one element back, surely it's fine"—vector's growth strategy is implementation-defined, and we cannot predict which push_back triggers the reallocation. If we genuinely need to keep using a position's information after modifying the container, use an index rather than an iterator, because an index is logically stable.

### Trap 2: Modifying a Container While Iterating

Here is a classic crash site. First, the version that "looks fine at a glance but blows up":

```cpp
std::vector<int> v = {1, 2, 3, 4, 5, 6};
for (auto it = v.begin(); it != v.end(); ++it) {
    if (*it % 2 == 0) {
        v.erase(it);  // undefined behavior! it is already invalidated
    }
}
```

Notice that after `erase` returns, `it` is dead, and doing `++it` on it afterward is undefined behavior. The correct approach uses `erase`'s return value, which is an iterator to the element following the erased one:

```cpp
for (auto it = v.begin(); it != v.end(); /* no ++it here */) {
    if (*it % 2 == 0) {
        it = v.erase(it);  // erase returns an iterator to the next element
    } else {
        ++it;
    }
}
```

This pattern is still error-prone, though—one brief lapse and we forget that the `erase` branch must not do `++it`. The more recommended approach first moves the doomed elements to the end with `std::remove_if`, then erases them in one shot:

```cpp
// Before C++20
auto it = std::remove_if(v.begin(), v.end(), [](int x) { return x % 2 == 0; });
v.erase(it, v.end());

// C++20 — done in one line
std::erase_if(v, [](int x) { return x % 2 == 0; });
```

For `map` and `set`, the safe erase-while-iterating recipe differs slightly. Before C++11, `erase` returned `void`, so the traditional idiom was `m.erase(it++)`—copy the iterator, increment it, then hand the old value to erase. Since C++11, the associative containers' `erase` returns the next iterator as well, so the writing style matches vector's: `it = m.erase(it)`.

Never modify a container's structure (inserting or erasing elements) inside a range-for loop. A range-for runs on iterators underneath, and there is no way to get `erase`'s return value out of one. With sanitizers enabled, this class of bug gets caught easily; without them it may "just happen to run", completely invisible during debugging, only to crash in production under some particular workload—and tracking that down is agony.

### Trap 3: map's operator[] Quietly Inserts Elements

We covered this pit in detail in the associative-containers article, but its cameo rate is simply too high, so let's stress it once more from the "patterns" angle. `map[key]` automatically inserts a default-constructed element when the key is absent. Two consequences follow: on a `const map`, `operator[]` fails to compile at all, because it is a modifying operation; and if we only meant to check whether a key exists but reached for `operator[]`, the map gets quietly modified.

The sneakiest scenario we're likely to hit is accidentally triggering `operator[]` mid-traversal:

```cpp
std::map<std::string, int> word_count = {{"hello", 2}, {"world", 1}};


// "Safely" read the values of all keys — not actually safe!
for (const auto& [word, count] : word_count) {
    // if word_count[some_other_key] is called here, the map gets modified
    // modifying container structure inside a range-for = undefined behavior
}
```

Granted, the example above is a bit extreme, but a subtler variant exists: inside the loop body we call some function, and that function does an `operator[]` access on the map internally. Hence the core principle: **for read-only lookup, always use `find`, `count`, or `contains` (C++20); leave `operator[]` to scenarios that genuinely want "create on access"**.

If the value type has no default constructor (say, a class that only constructs with arguments), `operator[]` won't even compile for a missing key—which is actually a good thing, the compiler walling off this pit for us. The truly dangerous types are the default-constructible ones like `int` and `string`: `operator[]` quietly inserts a 0 or an empty string, the logic is wrong, and the program keeps running as if nothing happened.

## Understanding Performance — Caches, Reserving, and Choices

Pits done, let's talk performance. Plenty of people learn the time complexities of the various containers and conclude that choosing a container means choosing between O(1) and O(log n). In reality, the caching mechanism of modern CPUs often influences performance more than algorithmic complexity does.

### Contiguous Memory and Cache Friendliness

A CPU accesses memory far more slowly than it executes instructions, which is why modern CPUs all carry multiple cache levels (L1, L2, L3). When the CPU reads the data at some address, it loads a whole block of nearby data (usually 64 bytes, one cache line) into the cache alongside it. That means when we are sequentially traversing a data structure laid out in contiguous memory, the first access drags an entire block into the cache, and the following accesses hit the cache directly—at tremendous speed.

The elements of `std::vector` and `std::array` pack tightly together in memory, so traversal hits cache at a very high rate. Each node of `std::list`, by contrast, is independently allocated, the nodes' locations in memory follow no pattern at all, and a walk ends up touching main memory nearly every step—a miserable cache hit rate. Even though `list` inserts and erases in the middle in O(1) while `vector` pays O(n), in actual runs vector is frequently faster, because the power of CPU cache prefetching makes up the theoretical-complexity deficit.

A classic benchmark result: for containers of small elements like `int` or `double`, `vector`'s linear search (O(n)) is often faster than `list`'s node-by-node traversal when n is below roughly 1000. That isn't because O(n) beats O(1)—it's because the cache advantage conferred by contiguous memory is simply that large.

### The Importance of reserve

`vector` growth involves three steps—"allocate new memory -> copy/move every element -> free the old memory"—and they don't come cheap. If we know roughly how many elements we'll store ahead of time, calling `reserve` to allocate the space in one go eliminates the growth overhead entirely:

```cpp
std::vector<int> v;
v.reserve(10000);  // one allocation; the 10000 push_backs afterward never reallocate
for (int i = 0; i < 10000; ++i) {
    v.push_back(i);
}
```

`unordered_map` has an analogous idea: we can call `reserve` to pre-allocate enough buckets and cut down the number of rehashes. When inserting a large batch of elements into an `unordered_map`, a single `reserve` often drops the total time by 30% or even more.

### Small String Optimization in string

A lesser-known but very practical fact: most standard library implementations employ the "Small String Optimization" (SSO). When a `std::string`'s length is below some threshold (usually 15–22 bytes, depending on the implementation), the string data lives directly in a buffer inside the string object, with no heap allocation needed. This makes copying, assigning, and destroying short strings very fast. In real development most strings are short (variable names, configuration keys, log messages, and the like), and SSO quietly saves us a whole lot of memory-allocation overhead.

## Hands-On Time — Putting the STL Patterns Together

Now let's knead everything this chapter has discussed—container selection, pitfall defense, performance awareness—into one comprehensive program. The scenario: we have a batch of sensor readings that need deduplication, outlier filtering, sorting, and statistics, plus a final analysis report printed out.

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

/// An analysis report
struct Report {
    std::string sensor_id;
    double min_val;
    double max_val;
    double avg_val;
    std::size_t count;
};

/// Filter outliers: group by sensor and drop data deviating from that sensor's mean by more than kSigma standard deviations
void filter_outliers(std::vector<Reading>& readings, double k_sigma)
{
    if (readings.empty()) {
        return;
    }

    // Group by sensor and compute the mean and standard deviation for each
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
    groups.reserve(16);  // pre-allocate to reduce rehashes

    for (auto& r : readings) {
        groups[r.sensor_id].push_back(std::move(r));
    }

    std::vector<Report> reports;
    reports.reserve(groups.size());

    for (auto& [id, recs] : groups) {
        if (recs.empty()) {
            continue;
        }

        // sort by timestamp
        std::sort(recs.begin(), recs.end(),
            [](const Reading& a, const Reading& b) {
                return a.timestamp < b.timestamp;
            });

        // compute the statistics with STL algorithms
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

    // sort the output by sensor ID so the result is stable
    std::sort(reports.begin(), reports.end(),
        [](const Report& a, const Report& b) { return a.sensor_id < b.sensor_id; });

    return reports;
}

/// Remove duplicate readings (same sensor plus same timestamp counts as a duplicate)
void deduplicate(std::vector<Reading>& readings)
{
    // Track the (sensor_id, timestamp) combinations already seen in an unordered_set
    struct Key {
        std::string sensor_id;
        uint32_t timestamp;
    };

    // Custom hash and equality — required by unordered_set
    struct KeyHash {
        std::size_t operator()(const Key& k) const
        {
            auto h1 = std::hash<std::string>{}(k.sensor_id);
            auto h2 = std::hash<uint32_t>{}(k.timestamp);
            return h1 ^ (h2 << 1);  // a simple combined hash
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
                return true;  // duplicate; mark for removal
            }
            seen.insert(k);
            return false;
        });
    readings.erase(it, readings.end());
}

int main()
{
    // Simulated sensor data — contains duplicates and outliers
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

The complete code sits right below—hit "Try It Yourself" to run it directly, no terminal needed:

<OnlineCompilerDemo
  title="Hands-On Time: Sensor Data Analysis stl_patterns.cpp"
  source-path="code/examples/vol1/26_stl_patterns.cpp"
  description="Run sensor-data deduplication, filtering, and statistics online. Try changing the multiplier in filter_outliers from 2.0 to 1.0 and see which readings get kicked out as outliers."
  allow-run
/>

Let's unpack this program's design decisions layer by layer. The deduplication step picks `unordered_set` over `set` because we only care about "have we seen it", not ordered traversal—O(1) lookup suits us better than O(log n). Note that `KeyHash` and `KeyEqual` must be hand-written here: `Key` is a custom struct, and the standard library has no default hash for it. If we forget to provide them, the compiler greets us with a wall of template-instantiation errors as its "friendly reminder".

The key design in outlier filtering is **computing the statistics per sensor group**. Different sensors differ wildly in dimension and value range (temperature around 22–23 °C, pressure around 1013 hPa); if we mix every reading together into one mean and standard deviation, no single value will ever register as an outlier. So `filter_outliers` first groups by `sensor_id`, then computes the mean and standard deviation for each group independently—only that way do the 85.0 °C in the temperature sensor and the 12.0 hPa in the pressure sensor get correctly identified as outliers.

The grouping stage picks `unordered_map<string, vector<Reading>>`, again because no ordered traversal by key is needed. `reserve(16)` is a rule-of-thumb pre-allocation—sensor counts are usually small, and one allocation avoids later rehashes. Outlier filtering uses `remove_if` + `erase` rather than erasing mid-traversal, which is both safe and clear. The statistics are all done with STL algorithms: `minmax_element` finds the minimum and maximum in a single pass, `accumulate` sums—no hand-written loops anywhere.

## Time to Practice — Exercises

### Exercise 1: Container Selection in Practice

For each scenario below, pick the most suitable container and explain your reasoning: (a) storing a game character's backpack item list, with frequent additions and removals at the end; (b) maintaining a spell checker's dictionary, with frequent checks for whether a word exists; (c) storing a student-ID-to-name mapping for every student in a class, output in student-ID order; (d) storing the data of a 3x3 matrix.

### Exercise 2: Fixing Buggy Code

The code below hides at least two STL traps—find them and fix them:

```cpp
std::vector<int> data = {1, 2, 3, 4, 5, 6, 7, 8};
for (auto it = data.begin(); it != data.end(); ++it) {
    if (*it % 2 == 0) {
        data.erase(it);
    }
}
```

### Exercise 3: Performance Comparison

Write a benchmark: store 100000 random integers in a `std::vector<int>` and a `std::list<int>` respectively, then time with `<chrono>` and compare (a) the cost of a sequential traversal that sums the values, and (b) the cost of sorting. Let real numbers drive home the effect of cache friendliness.

---

> **References**
>
> - [cppreference: Container library](https://en.cppreference.com/w/cpp/container)
> - [cppreference: std::erase (C++20)](https://en.cppreference.com/w/cpp/container/vector/erase2)
> - [Bjarne Stroustrup: Why you should avoid linked lists](https://www.youtube.com/watch?v=YQs6IC-vgmo)
> - [cppreference: Iterator invalidation](https://en.cppreference.com/w/cpp/container#Iterator_invalidation)
