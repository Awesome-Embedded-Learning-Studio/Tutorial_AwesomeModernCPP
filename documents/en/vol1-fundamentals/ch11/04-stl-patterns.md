# Common STL Patterns

In the previous three chapters, we covered `vector`, associative containers, and the algorithm library, diving deep into each domain. But in real-world code, the questions are rarely "how do I use this container" or "how do I call this algorithm." Instead, they are "which container should I choose," "why is my program so slow," and "did I just hit iterator invalidation again." These are cross-cutting concerns that require a systematic perspective.

In this chapter, we connect the dots from the previous chapters. We start with the most frequent decision: which container to use for a given scenario. Next, we walk through the most common STL pitfalls. Then, we cover essential performance fundamentals. Finally, we tie container selection, algorithm pairing, and pitfall avoidance together in a comprehensive practical example. After this chapter, your understanding of the STL will level up from "knowing how to use it" to "knowing how to use it right."

> **Learning Objectives**
>
> After completing this chapter, you will be able to:
>
> - [ ] Quickly select the appropriate STL container based on actual requirements
> - [ ] Identify and avoid common pitfalls like iterator invalidation and modifying containers during iteration
> - [ ] Understand the impact of cache friendliness on container performance
> - [ ] Proficiently use the erase-remove idiom and C++20's `std::erase`
> - [ ] Apply the "algorithms over hand-written loops" principle to write clearer code

## Making the Choice — Container Selection Guide

Many developers feel more conflicted after learning about all the containers: which one should I actually use? In reality, the decision logic is very clear for the vast majority of scenarios. Let's walk through it based on your core needs:

If your data is sequential, its size will change, and you need random access, `std::vector` is almost always the first choice. Its elements are stored contiguously in memory, allowing CPU cache prefetching to work efficiently. Subscript access is O(1), and amortized O(1) for push/pop at the back. Its only weakness is O(n) insertion and deletion in the middle—but honestly, most programs don't need frequent middle insertions.

If you need to "look up a value by key" and don't need to iterate in key order, `std::unordered_map` is the most efficient choice, offering average O(1) lookup speed. If you also need ordered iteration by key or range queries, switch to `std::map`.

If you need to maintain a "set of unique elements," use `std::set`. If you only need to check "whether something is present" and don't need ordering, `std::unordered_set` is faster.

If the number of elements is known at compile time and doesn't need dynamic resizing, use `std::array`—it is a zero-overhead fixed-size array that avoids the dynamic allocation overhead of a vector and is just as efficient as a C array.

Let's organize this into a decision table:

| Core Need | First Choice | Characteristics |
|----------|----------|------|
| Sequential storage, random access | `std::vector` | Contiguous memory, cache friendly |
| Fast key lookup (no ordering needed) | `std::unordered_map` | Average O(1) lookup |
| Key lookup with ordered iteration | `std::map` | O(log n), red-black tree |
| Unique element set | `std::set` | Automatic deduplication, ordered |
| Fixed-size array | `std::array` | Zero overhead, stack allocated |

This table covers 90% of daily decisions. The remaining 10% involves `deque` (double-ended queue, O(1) insertion/deletion at both ends), `list` (doubly linked list, O(1) middle insertion/deletion but terrible cache performance), `multimap` / `multiset` (allowing duplicate keys), and so on. You can look up the documentation when you encounter them.

There is a practical rule of thumb worth remembering: **if you're not sure what to use, just use `vector`**. Bjarne Stroustrup (the creator of C++) and many C++ experts have repeatedly emphasized this point. `vector` performs decently in most scenarios; even when its theoretical complexity isn't optimal, its cache friendliness often makes it win in real-world benchmarks. Only consider other containers when you can clearly articulate "why vector won't work."

## Pitfall Warnings — Where the STL Most Often Goes Wrong

After using the STL for a while, you'll find that what really gives you a headache isn't "how to call a certain interface," but those traps where "it compiles, even runs fine, but the logic is already wrong." Here we go through the most common pitfalls one by one, each of which I or C++ developers I know have stepped into for real.

### Pitfall 1: Iterator Invalidation

We mentioned this issue when discussing `vector`, but it doesn't just affect `vector`, and it doesn't only happen during reallocation. The core rule is this: for `vector` and `string`, any operation that might trigger reallocation (`push_back`, `emplace_back`, `insert`, or reallocation caused by `reserve`) invalidates all iterators, pointers, and references. Even without reallocation, `insert` and `erase` invalidate iterators at and after the affected position. For `deque`, any insertion operation invalidates all iterators. For `map`, `set`, `unordered_map`, and `unordered_set`, `erase` only invalidates the iterator pointing to the erased element—other iterators remain unaffected. This is a very important distinction.

```cpp
std::vector<int> v = {1, 2, 3, 4, 5};
auto it = v.begin() + 2;  // 指向 3
v.push_back(6);           // 可能触发扩容
// it 现在是悬垂迭代器——解引用是未定义行为

std::map<int, std::string> m = {{1, "a"}, {2, "b"}, {3, "c"}};
auto mit = m.find(2);
m.erase(1);               // 删除 key=1 的元素
// mit 仍然有效——map 的 erase 不影响其他迭代器
```

The practical significance of this distinction is that if you need to delete elements while iterating over a `map`, you can do so directly with an iterator. However, deleting elements while iterating over a `vector` requires extra care. Let's look at this more specific scenario next.

> **Pitfall Warning**: After saving an iterator, treat any operation that might modify the container's structure as "potentially invalidating the iterator." Don't assume "I just push_backed one element, it should be fine"—vector's reallocation strategy is implementation-dependent, and you can't predict which push_back will trigger reallocation. If you truly need to continue using information about a certain position after modifying the container, use an index instead of an iterator, because indexes are logically stable.

### Pitfall 2: Modifying a Container During Iteration

This is a very classic failure scenario. First, let's look at an example that "looks fine at first glance but will blow up":

```cpp
std::vector<int> v = {1, 2, 3, 4, 5, 6};
for (auto it = v.begin(); it != v.end(); ++it) {
    if (*it % 2 == 0) {
        v.erase(it);  // 未定义行为！it 已失效
    }
}
```

After calling `erase`, `it` is invalidated, and doing `++it` on it is undefined behavior. The correct approach is to use the return value of `erase`—it returns an iterator pointing to the element following the deleted one:

```cpp
for (auto it = v.begin(); it != v.end(); /* 不在这里 ++it */) {
    if (*it % 2 == 0) {
        it = v.erase(it);  // erase 返回下一个元素的迭代器
    } else {
        ++it;
    }
}
```

But honestly, this approach is error-prone—a moment of carelessness and you'll forget not to do `++it` in the `erase` branch. A more recommended approach is to first use `std::remove_if` to move the elements to be deleted to the end, then `erase` them all at once:

```cpp
// C++20 之前
auto it = std::remove_if(v.begin(), v.end(), [](int x) { return x % 2 == 0; });
v.erase(it, v.end());

// C++20——一行搞定
std::erase_if(v, [](int x) { return x % 2 == 0; });
```

For `map` and `set`, the safe way to delete during iteration is slightly different. Because prior to C++11, `erase` returned `void`, the traditional approach was `m.erase(it++)`—copy the iterator, increment it, then pass the copy to erase. Starting from C++11, the `erase` of associative containers also returns the next iterator, so the approach is the same as for vector: `it = m.erase(it)`.

> **Pitfall Warning**: You must absolutely never modify a container's structure (inserting or deleting elements) inside a range-for loop. Range-for uses iterators under the hood, and you cannot capture the return value of `erase` inside a range-for. If the compiler has sanitizers enabled, these bugs are easy to catch; but if not, they might "happen to run"—completely invisible during the debug phase, only to crash under a specific load in production, making debugging extremely painful.

### Pitfall 3: map's operator[] Silently Inserting Elements

We covered this pitfall in detail when discussing associative containers, but it appears so frequently that we need to emphasize it again from a "pattern" perspective. `map[key]` automatically inserts a default-constructed element when the key doesn't exist. This means two consequences: first, using `operator[]` on a `const map` simply won't compile, because it is a modifying operation; second, if you just want to check whether a key exists and use `operator[]`, the map gets silently modified.

The most insidious scenario is accidentally triggering `operator[]` during iteration:

```cpp
std::map<std::string, int> word_count = {{"hello", 2}, {"world", 1}};

// "安全地"读取所有 key 的值——其实不是！
for (const auto& [word, count] : word_count) {
    // 如果在这里调用 word_count[some_other_key]，map 会被修改
    // 在 range-for 中修改容器结构 = 未定义行为
}
```

Of course, the example above is a bit extreme, but a more hidden variant is: you call a function inside the loop body, and that function internally does a `operator[]` access on the map. So the core principle is: **for read-only lookups, always use `find`, `count`, or `contains` (C++20); leave `operator[]` for scenarios where you genuinely need "create on access."**

> **Pitfall Warning**: If your value type doesn't have a default constructor (for example, a class that only accepts arguments for construction), then `operator[]` won't even compile when the key is missing—which is actually a good thing, because the compiler blocks the pitfall for you. What's truly dangerous are types like `int` and `string` that can be default-constructed; `operator[]` silently inserts a 0 or an empty string, the logic is wrong, but the program keeps running without a hitch.

## Understanding Performance — Cache, Reservation, and Selection

Now that we've covered the pitfalls, let's talk about performance. Many developers, after learning the time complexities of various containers, think choosing a container is simply choosing between O(1) and O(log n). In reality, modern CPU cache mechanisms often have a greater impact on performance than algorithmic complexity.

### Contiguous Memory and Cache Friendliness

CPUs access memory much slower than they execute instructions, so modern CPUs have multi-level caches (L1, L2, L3). When a CPU reads data from a certain address, it loads an entire block of nearby data (usually 64 bytes, i.e., one cache line) into the cache at once. This means that if you are sequentially traversing a contiguous memory data structure, the first access brings a whole block of data into the cache, and subsequent accesses hit the cache directly, making them extremely fast.

The elements of `std::vector` and `std::array` are tightly packed in memory, resulting in very high cache hit rates during traversal. In contrast, each node of a `std::list` is independently allocated, and the positions of nodes in memory have no pattern whatsoever, meaning almost every access during traversal hits main memory, resulting in extremely low cache hit rates. Even though `list` has O(1) middle insertion and deletion while `vector` is O(n), vector is often faster in actual execution—because the power of CPU cache prefetching compensates for the disadvantage in theoretical complexity.

A classic benchmarking conclusion is that for containers storing small elements like `int` or `double`, a linear search on a `vector` (O(n)) is often faster than node-by-node traversal on a `list` when n is around 1000 or less. This isn't because O(n) is better than O(1), but because the cache advantage of contiguous memory is simply too large.

### The Importance of reserve

`vector` reallocation involves three steps—"allocate new memory -> copy/move all elements -> free old memory"—and the cost is not trivial. If you know roughly how many elements you'll store in advance, calling `reserve` to allocate the space all at once completely eliminates reallocation overhead:

```cpp
std::vector<int> v;
v.reserve(10000);  // 一次分配，之后 10000 次 push_back 零扩容
for (int i = 0; i < 10000; ++i) {
    v.push_back(i);
}
```

`unordered_map` has a similar concept—you can use `reserve` to pre-allocate enough buckets, reducing the number of rehashes. When inserting a large number of elements into an `unordered_map`, a single `reserve` can often reduce the overall time by 30% or more.

### String's Small String Optimization

A lesser-known but very practical fact is that most standard library implementations use "Small String Optimization" (SSO). When a `std::string`'s length is below a certain threshold (usually 15–22 bytes, depending on the implementation), the string data is stored directly in an internal buffer within the string object, requiring no heap allocation. This means copying, assigning, and destroying short strings are very fast. In real-world development, most strings are short (variable names, configuration items, log messages, etc.), and SSO quietly saves you a massive amount of memory allocation overhead.

## Practical Exercise — Comprehensively Applying STL Patterns

Now let's combine all the knowledge points discussed in this chapter—container selection, pitfall avoidance, and performance awareness—into a comprehensive practical program. The scenario is this: we have a batch of sensor readings, and we need to deduplicate them, filter out anomalous values, sort them, compute statistics, and output a final analysis report.

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

/// 单条传感器读数
struct Reading {
    std::string sensor_id;
    double value;
    uint32_t timestamp;
};

/// 分析报告
struct Report {
    std::string sensor_id;
    double min_val;
    double max_val;
    double avg_val;
    std::size_t count;
};

/// 过滤异常值：按传感器分组，去掉偏离该传感器均值超过 kSigma 个标准差的数据
void filter_outliers(std::vector<Reading>& readings, double k_sigma)
{
    if (readings.empty()) {
        return;
    }

    // 按传感器分组，分别计算均值和标准差
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

    // remove-erase 删除异常值
    auto it = std::remove_if(readings.begin(), readings.end(),
        [&](const Reading& r) {
            const auto& [mean, stddev] = stats[r.sensor_id];
            return std::abs(r.value - mean) > k_sigma * stddev;
        });
    readings.erase(it, readings.end());
}

/// 为每个传感器生成分析报告
std::vector<Report> generate_reports(std::vector<Reading>& readings)
{
    // 用 unordered_map 按传感器分组（不需要有序遍历，O(1) 查找）
    std::unordered_map<std::string, std::vector<Reading>> groups;
    groups.reserve(16);  // 预分配，减少 rehash

    for (auto& r : readings) {
        groups[r.sensor_id].push_back(std::move(r));
    }

    std::vector<Report> reports;
    reports.reserve(groups.size());

    for (auto& [id, recs] : groups) {
        if (recs.empty()) {
            continue;
        }

        // 按时间戳排序
        std::sort(recs.begin(), recs.end(),
            [](const Reading& a, const Reading& b) {
                return a.timestamp < b.timestamp;
            });

        // 用 STL 算法计算统计量
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

    // 按传感器 ID 排序输出，保证结果稳定
    std::sort(reports.begin(), reports.end(),
        [](const Report& a, const Report& b) { return a.sensor_id < b.sensor_id; });

    return reports;
}

/// 去除重复读数（同一传感器、同一时间戳视为重复）
void deduplicate(std::vector<Reading>& readings)
{
    // 用 unordered_set 记录已见过的 (sensor_id, timestamp) 组合
    struct Key {
        std::string sensor_id;
        uint32_t timestamp;
    };

    // 自定义哈希和相等比较——unordered_set 必需
    struct KeyHash {
        std::size_t operator()(const Key& k) const
        {
            auto h1 = std::hash<std::string>{}(k.sensor_id);
            auto h2 = std::hash<uint32_t>{}(k.timestamp);
            return h1 ^ (h2 << 1);  // 简单组合哈希
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
                return true;  // 重复，标记删除
            }
            seen.insert(k);
            return false;
        });
    readings.erase(it, readings.end());
}

int main()
{
    // 模拟传感器数据——包含重复和异常值
    std::vector<Reading> readings = {
        {"temp-01", 22.5, 1001},
        {"temp-01", 22.7, 1002},
        {"temp-01", 22.5, 1001},  // 重复
        {"temp-01", 85.0, 1003},  // 异常值
        {"temp-01", 22.9, 1004},
        {"temp-01", 22.6, 1005},
        {"temp-01", 23.0, 1006},
        {"press-01", 1013.2, 1001},
        {"press-01", 1013.5, 1002},
        {"press-01", 1013.2, 1001},  // 重复
        {"press-01", 12.0, 1003},    // 异常值
        {"press-01", 1013.8, 1004},
        {"press-01", 1013.0, 1005},
        {"press-01", 1013.6, 1006},
    };

    std::cout << "=== Raw readings: " << readings.size() << " ===\n";

    // 第一步：去重
    deduplicate(readings);
    std::cout << "After dedup: " << readings.size() << "\n";

    // 第二步：过滤异常值（2 倍标准差）
    filter_outliers(readings, 2.0);
    std::cout << "After outlier filter: " << readings.size() << "\n";

    // 第三步：生成分析报告
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

Let's break down the design decisions in this program layer by layer. For deduplication, we chose `unordered_set` instead of `set` because we only care about "have we seen this before" and don't need ordered traversal, making O(1) lookup more appropriate than O(log n). Note that we must custom-define `KeyHash` and `KeyEqual` here—because `Key` is a custom struct, and the standard library doesn't have a default hash function for it. If you forget to provide them, the compiler will "kindly remind" you with a barrage of template instantiation errors.

The key design in anomalous value filtering is **calculating statistics grouped by sensor**. Different sensors have vastly different units and value ranges (temperature is around 22–23°C, air pressure is around 1013 hPa). If you mix all readings together to calculate the mean and standard deviation, no single value would be considered anomalous. Therefore, `filter_outliers` first groups by `sensor_id`, then independently calculates the mean and standard deviation for each group. This way, 85.0°C in the temperature sensor and 12.0 hPa in the air pressure sensor can be correctly identified as anomalous values.

For grouping, we chose `unordered_map<string, vector<Reading>>`, again because we don't need ordered iteration by key. `reserve(16)` is an empirical pre-allocation—the number of sensors is usually small, and a single allocation avoids subsequent rehashes. For filtering anomalous values, we used `remove_if` + `erase` instead of deleting directly during iteration—this is both safe and clear. The statistics section is done entirely with STL algorithms—`minmax_element` finds the max and min values in a single pass, `accumulate` computes the sum, with no hand-written loops.

## Try It Yourself — Exercises

### Exercise 1: Container Selection in Practice

Choose the most appropriate container for the following scenarios and explain your reasoning: (a) storing a game character's inventory item list, with frequent additions and deletions at the end; (b) maintaining a spell checker's dictionary, requiring frequent checks of whether a word exists; (c) storing a student ID-to-name mapping for an entire class, outputting in student ID order; (d) storing data for a 3x3 matrix.

### Exercise 2: Fixing Buggy Code

The following code has at least two STL pitfalls. Find and fix them:

```cpp
std::vector<int> data = {1, 2, 3, 4, 5, 6, 7, 8};
for (auto it = data.begin(); it != data.end(); ++it) {
    if (*it % 2 == 0) {
        data.erase(it);
    }
}
```

### Exercise 3: Performance Comparison

Write a benchmark: store 100,000 random integers in both a `std::vector<int>` and a `std::list<int>`, and use `<chrono>` to time and compare their (a) sequential traversal summation time, and (b) sorting time. Use real data to experience the impact of cache friendliness.

## Summary

In this chapter, we reorganized the knowledge from the previous three chapters from the perspective of "how to use the STL correctly." Regarding container selection, the core idea is to decide based on requirements: choose `vector` for sequential storage, `unordered_map` for fast lookup, `map` for ordered key-value pairs, `set` for deduplication, and `array` for fixed sizes. If you're unsure, just use `vector`; it's almost always a safe choice.

Regarding pitfall avoidance, the three traps requiring the most vigilance are iterator invalidation (especially after vector reallocation and erase), modifying containers during iteration (use remove-erase instead of hand-written deletion loops), and map's `operator[]` silently inserting elements (use `find` or `contains` for read-only lookups).

Regarding performance, the cache friendliness of contiguous memory often makes `vector` run faster in real-world scenarios than `list`, which has a better theoretical complexity. `reserve` is a powerful tool for eliminating reallocation overhead, and it works for both vector and unordered_map.

With this, Chapter 11 is fully complete. We started with `vector`, learned about associative containers and the algorithm library, and finally integrated this knowledge into systematic STL usage patterns. In the next chapter, we dive into the C++ memory model—from memory layout to stack and heap allocation, from `new`/`delete` to memory alignment. These are the low-level foundations for writing high-performance C++ code.

---

> **References**
>
> - [cppreference: Container library](https://en.cppreference.com/w/cpp/container)
> - [cppreference: std::erase (C++20)](https://en.cppreference.com/w/cpp/container/vector/erase2)
> - [Bjarne Stroustrup: Why you should avoid linked lists](https://www.youtube.com/watch?v=YQs6IC-vgmo)
> - [cppreference: Iterator invalidation](https://en.cppreference.com/w/cpp/container#Iterator_invalidation)
