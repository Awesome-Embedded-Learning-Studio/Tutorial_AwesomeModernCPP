---
title: Getting Started with Associative Containers
description: Master the core operations of `std::map`, `std::set`, and `std::unordered_map`,
  and learn how to perform key-based lookups and maintain ordered collections.
chapter: 11
order: 2
difficulty: beginner
reading_time_minutes: 15
platform: host
prerequisites:
- std::vector 快速上手
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
cpp_standard:
- 11
- 14
- 17
- 20
translation:
  source: documents/vol1-fundamentals/ch11/02-map-set.md
  source_hash: 365ba715d7c3abc319104ed0b6fdd7d2464114a8c6dd85968605560e9b1b8897
  translated_at: '2026-05-26T10:59:15.450539+00:00'
  engine: anthropic
  token_count: 2772
---
# Getting Started with Associative Containers

In the previous chapter, we walked through `std::vector` from top to bottom—dynamic arrays, contiguous storage, O(1) random access by index. When dealing with ordered sequences, it is our go-to container. However, in many scenarios, we do not care about "what is the element at index *n*," but rather "what is the value for a given key." For example, counting how many times each word appears in a text, or checking whether a word exists in a spelling dictionary—these "given a key, look up a result" tasks are cumbersome and inefficient with a `vector`, requiring either a sorted binary search or a linear scan. The C++ standard library provides a group of containers specifically designed for such problems, known as **associative containers**.

In this chapter, we will focus on three siblings: `std::map` (ordered key-value pairs), `std::set` (ordered unique element sets), and `std::unordered_map` (hashed key-value pairs). They share a common trait: lookup, insertion, and deletion are all fast, without needing to traverse the entire container. The difference lies in the underlying implementation—`map` and `set` use red-black trees internally, keeping elements sorted at all times with O(log n) complexity for operations; `unordered_map` uses a hash table, offering average O(1) performance but with no ordering guarantees.

> **Learning Objectives**
>
> After completing this chapter, you will be able to:
>
> - [ ] Use `std::map` for insertion, lookup, and deletion operations
> - [ ] Understand the default insertion pitfall of `operator[]` and know when to use `at` or `find`
> - [ ] Use `std::set` to maintain an ordered set of unique elements
> - [ ] Iterate over a map using structured bindings: `for (auto& [k, v] : map)`
> - [ ] Understand the performance differences between `unordered_map` and `map` to make informed choices
> - [ ] Write practical word frequency counting and spell-checking programs using map and set

## Diving In — Basic std::map Operations

`std::map` is an ordered key-value container declared in the `<map>` header. Each element is a `std::pair<const Key, Value>`, where Key is the type of the key and Value is the type of the value. Internally, it uses a red-black tree (a self-balancing binary search tree), so elements are always sorted in ascending order by key, and lookup, insertion, and deletion are all O(log n).

Let's first look at how to add elements:

```cpp
#include <iostream>
#include <map>
#include <string>

int main()
{
    std::map<std::string, int> scores;

    // 方式一：用 operator[] 赋值
    scores["Alice"] = 95;
    scores["Bob"] = 87;

    // 方式二：用 insert 插入 pair
    scores.insert({"Charlie", 72});

    // 方式三：用 emplace 原地构造（推荐）
    scores.emplace("Diana", 91);

    // 方式四：初始化列表
    std::map<std::string, int> ages = {
        {"Alice", 22}, {"Bob", 25}, {"Charlie", 20}
    };

    return 0;
}
```

Each insertion method has its own use cases. `operator[]` is the most intuitive, but it has a very tricky behavior—if the key does not exist, it automatically inserts a value-initialized element (0 for `int`, or the default constructor for class types). This means that `scores["Eve"]` will silently insert a `{"Eve", 0}` into the map even if you only intend to check the value. We will cover this pitfall in detail shortly.

Next is lookup. `find` returns an iterator pointing to the found element, or `end()` if not found. `count` returns the number of matching elements (which is either 0 or 1 for a map). C++20 introduced `contains`, which has more intuitive semantics:

```cpp
// C++11 起所有版本都能用的方式
auto it = scores.find("Alice");
if (it != scores.end()) {
    std::cout << "Alice: " << it->second << "\n";
}

// count 也可以判断存在性
if (scores.count("Bob")) {
    std::cout << "Bob exists\n";
}

// C++20 引入 contains，语义最清晰
if (scores.contains("Diana")) {
    std::cout << "Diana exists\n";
}
```

Deletion uses `erase`, which can remove elements by key or by iterator:

```cpp
scores.erase("Bob");            // 按 key 删除
scores.erase(scores.begin());   // 删除第一个元素（key 最小的）
scores.clear();                 // 清空整个 map
```

> **Pitfall Warning**: `map[key]` **automatically inserts a default value** when the key does not exist. This leads to two consequences: first, if you only want to check whether a key exists, using `operator[]` will silently modify the map, which is a logical bug, and if your value type has no default constructor, it simply will not compile; second, on an `const map`, `operator[]` is completely unavailable because it is a modifying operation. Therefore, for read-only lookups, use `find`, `count`, or `contains`. For bounds-checked access, use `at()`—just like `at` on a vector, it throws a `std::out_of_range` exception if the key is not found.

## A Different Angle — Maintaining Unique Ordered Sets with std::set

Declared in the `<set>` header, `std::set` can be understood as "a map with only keys and no values." All its elements are unique and always sorted. When we need to deduplicate data or determine "whether something belongs to a set," `set` comes into play.

Its basic operations are very similar to those of a map:

```cpp
#include <iostream>
#include <set>

int main()
{
    std::set<int> s = {5, 3, 1, 4, 2, 3, 1};

    // 重复元素被自动忽略，且元素已排序
    // s: {1, 2, 3, 4, 5}

    s.insert(6);        // 插入
    s.emplace(0);       // 原地构造插入
    s.erase(3);         // 按 key 删除

    // 查找
    if (s.contains(4)) {            // C++20
        std::cout << "4 is in the set\n";
    }

    if (s.count(2)) {               // 所有 C++ 版本通用
        std::cout << "2 is in the set\n";
    }

    auto it = s.find(1);
    if (it != s.end()) {
        std::cout << "Found: " << *it << "\n";
    }

    return 0;
}
```

You will notice that set's interface is almost identical to map's, except it lacks `operator[]` and `at`—because set has no "value" to access, and dereferencing an iterator yields the key itself. Another minor difference is that set's `insert` returns a `pair<iterator, bool>`, where `bool` tells you whether the insertion actually took place (it returns `false` if the element already exists).

An easily overlooked feature is that set provides `lower_bound` and `upper_bound`, which can be used for range queries. For example, to find all elements in the set that are greater than or equal to 3 and less than 7:

```cpp
std::set<int> s = {1, 3, 5, 7, 9};
auto lo = s.lower_bound(3);   // 指向 3
auto hi = s.upper_bound(7);   // 指向 9
for (auto it = lo; it != hi; ++it) {
    std::cout << *it << " ";   // 输出: 3 5 7
}
```

## Going Through the Pairs — Iterating Over Associative Containers

Like vector, associative containers support range-for loops. However, the element type of a map is `pair<const Key, Value>`. In C++11, you need to access the key and value through `.first` and `.second`:

```cpp
std::map<std::string, int> scores = {
    {"Alice", 95}, {"Bob", 87}, {"Charlie", 72}
};

// C++11 方式
for (const auto& p : scores) {
    std::cout << p.first << ": " << p.second << "\n";
}
```

C++17 introduced **structured bindings**, allowing us to assign names to the two members of a pair, which greatly improves readability:

```cpp
// C++17 方式——推荐
for (const auto& [name, score] : scores) {
    std::cout << name << ": " << score << "\n";
}
```

`[name, score]` is the structured binding syntax, where `name` binds to `pair.first` and `score` binds to `pair.second`. Note that we use `const auto&` instead of `auto` here, just like when iterating over a vector—to avoid unnecessary copies. If you need to modify the value during iteration (note: the key is `const` and cannot be modified), simply remove `const`:

```cpp
// 给所有人加分
for (auto& [name, score] : scores) {
    score += 5;
    // name += "x";  // 编译错误！key 是 const 的
}
```

Iterating over a set is simpler since it only has a key:

```cpp
std::set<int> s = {5, 3, 1, 4, 2};
for (const auto& elem : s) {
    std::cout << elem << " ";   // 输出: 1 2 3 4 5（有序）
}
```

## A Different Engine — std::unordered_map

Declared in the `<unordered_map>` header, `std::unordered_map` has almost the same functionality as `std::map`—both are key-value containers supporting operations like `insert`, `emplace`, `erase`, `find`, `count`, `contains` (C++20), `operator[]`, and `at`. However, the underlying data structures are completely different: `map` uses a red-black tree, while `unordered_map` uses a hash table.

This difference has several practical implications. In terms of lookup performance, `map` offers stable O(log n) complexity, whereas `unordered_map` averages O(1) but degrades to O(n) in the worst case—when a large number of keys cause hash collisions. Regarding element order, `map` always keeps elements sorted by key, while the order of elements in `unordered_map` is unpredictable and can change with every insertion or deletion. In terms of memory usage, hash tables generally consume more memory than red-black trees.

So, when should we use which? A simple rule of thumb is: if you need to iterate over elements in key order, or if you need range queries like `lower_bound`/`upper_bound`, use `map`; if you only frequently perform "given a key, look up a value" operations and do not care about order, `unordered_map` is faster. In the vast majority of everyday scenarios, `unordered_map` is the more appropriate choice—after all, pure key-based lookups are far more common than ordered traversals.

```cpp
#include <iostream>
#include <string>
#include <unordered_map>

int main()
{
    std::unordered_map<std::string, int> freq;
    freq["hello"] = 3;
    freq["world"] = 5;
    freq.emplace("cpp", 1);

    // 接口和 map 完全一致
    if (auto it = freq.find("hello"); it != freq.end()) {
        std::cout << it->first << ": " << it->second << "\n";
    }

    // 但遍历顺序不保证
    for (const auto& [word, count] : freq) {
        std::cout << word << " -> " << count << "\n";
    }

    return 0;
}
```

> **Pitfall Warning**: `unordered_map` requires the key type to either have a default `std::hash` specialization or for you to manually provide a hash function. The standard library already provides `std::hash` specializations for built-in types (like `int`, `double`, `std::string`, etc.), so these types can be used as keys directly. However, if you want to use a custom struct as a key in `unordered_map`, you need to implement a `std::hash` specialization and `operator==` yourself, otherwise the code will fail to compile. In contrast, `std::map` only requires the key to support `operator<` (or a custom comparator), which is a lower barrier to entry. If you find that your custom type fails to compile as a key, first check whether you used `unordered_map` but forgot to provide a hash function.

## Hands-on Time — Word Frequency Counting and Spell Checking

Now let's combine map and set to write a practical program. The first feature is word frequency counting: read a piece of text and use `std::map` to count the occurrences of each word. The second feature is spell checking: store a dictionary in a `std::set`, and then check whether input words exist in the dictionary.

```cpp
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

/// 将字符串按空格拆分成单词列表
std::vector<std::string> split_words(const std::string& text)
{
    std::vector<std::string> words;
    std::istringstream iss(text);
    std::string word;
    while (iss >> word) {
        words.push_back(word);
    }
    return words;
}

/// 使用 map 统计每个单词的出现频率
void word_frequency_demo()
{
    std::string text = "the cat sat on the mat and the cat slept";
    auto words = split_words(text);

    std::map<std::string, int> freq;
    for (const auto& w : words) {
        // operator[] 在这里正好合适：不存在则插入 0，然后 ++ 自增
        ++freq[w];
    }

    std::cout << "=== Word Frequency ===\n";
    for (const auto& [word, count] : freq) {
        std::cout << "  " << word << ": " << count << "\n";
    }
}

/// 使用 set 做简单的拼写检查
void spell_check_demo()
{
    // 构建一个小词典
    std::set<std::string> dictionary = {
        "the", "cat", "sat", "on", "mat", "and", "slept",
        "dog", "ran", "in", "park", "hello", "world"
    };

    std::string text = "the cat danced on the roof";
    auto words = split_words(text);

    std::cout << "\n=== Spell Check ===\n";
    std::cout << "Input: \"" << text << "\"\n";
    for (const auto& w : words) {
        if (!dictionary.contains(w)) {
            std::cout << "  Unknown word: \"" << w << "\"\n";
        }
    }
}

/// 对比 map 和 unordered_map 的遍历顺序
void map_order_demo()
{
    std::map<std::string, int> ordered = {
        {"delta", 4}, {"alpha", 1}, {"charlie", 3}, {"bravo", 2}
    };

    std::cout << "\n=== std::map (ordered) ===\n";
    for (const auto& [key, val] : ordered) {
        std::cout << "  " << key << ": " << val << "\n";
    }
}

int main()
{
    word_frequency_demo();
    spell_check_demo();
    map_order_demo();
    return 0;
}
```

Compile and run:

```bash
g++ -std=c++20 -Wall -Wextra -o map_demo map_demo.cpp && ./map_demo
```

Expected output:

```text
=== Word Frequency ===
  and: 1
  cat: 2
  mat: 1
  on: 1
  sat: 1
  slept: 1
  the: 3

=== Spell Check ===
Input: "the cat danced on the roof"
  Unknown word: "danced"
  Unknown word: "roof"

=== std::map (ordered) ===
  alpha: 1
  bravo: 2
  charlie: 3
  delta: 4
```

Look at the word frequency output—`map` automatically sorted the results by key in lexicographical order. This is the ordering guaranteed by the red-black tree. In the word frequency counting, we use `++freq[w]` to increment the count. Here, the behavior of `operator[]`—"insert a default value of 0 if it doesn't exist"—is exactly what we want: the first time we encounter a word, it inserts 0 and then increments it to 1; subsequent encounters just continue incrementing. But be careful—this usage only applies when you genuinely want the "create on access" behavior; in read-only lookups, it is a trap.

For the spell-checking part, the `contains` method of `set` (C++20) makes the code very clear—just one line to determine whether a word is in the dictionary. If your compiler does not support C++20, you can use `count` instead: `dictionary.count(w) != 0`.

## Try It Yourself — Exercises

### Exercise 1: Student Grade Management

Use `std::map<std::string, int>` to implement a simple grade management program: support adding students and grades, querying grades by name, deleting students, and listing all students and their grades (sorted by name). Require the use of `find` to check if a student exists, rather than `operator[]`.

```cpp
void add_student(std::map<std::string, int>& db,
                 const std::string& name, int score);
bool get_score(const std::map<std::string, int>& db,
               const std::string& name, int& out_score);
void list_all(const std::map<std::string, int>& db);
```

### Exercise 2: Rewrite Word Frequency Counting with unordered_map

Replace `std::map` in the practical program above with `std::unordered_map`, and observe the change in output order. Then use `<chrono>` to time and compare the performance difference between the two implementations when processing a text containing 100,000 random words. Experience the practical difference between O(1) and O(log n) with large datasets.

### Exercise 3: Set Operations

Use two `std::set<int>` instances to store sets A and B, and manually implement intersection, union, and difference operations. (Hint: iterate over one set, and use `contains` or `find` to look up elements in the other set.)

```cpp
std::set<int> set_union(const std::set<int>& a, const std::set<int>& b);
std::set<int> set_intersection(const std::set<int>& a, const std::set<int>& b);
std::set<int> set_difference(const std::set<int>& a, const std::set<int>& b);
```

## Summary

In this chapter, we covered three core associative containers in C++. `std::map` uses a red-black tree to store ordered key-value pairs, with O(log n) lookup, insertion, and deletion, making it suitable for scenarios requiring ordered traversal by key or range queries. `std::set` is essentially "a map with only keys," used to maintain an ordered set of unique elements, with an interface almost identical to map. `std::unordered_map` is implemented with a hash table, offering average O(1) lookup speed, suitable for pure key-based lookup scenarios, at the cost of no element ordering guarantees and the need to manually provide a hash function for custom key types.

A few key takeaways: when iterating over a map, prefer C++17's structured binding `for (auto& [k, v] : map)` for cleaner code; do not use `operator[]` for read-only lookups—use `find`, `count`, or `contains`; when unsure whether to use map or unordered_map, ask yourself if you need ordered traversal—if not, choose `unordered_map`.

In the next chapter, we will dive into the STL algorithms library—sorting, searching, transforming, and accumulating. The standard library provides a large set of generic algorithms waiting for us to use. You will discover that containers combined with algorithms are where the true power of the STL lies.

---

> **References**
>
> - [cppreference: std::map](https://en.cppreference.com/w/cpp/container/map)
> - [cppreference: std::set](https://en.cppreference.com/w/cpp/container/set)
> - [cppreference: std::unordered_map](https://en.cppreference.com/w/cpp/container/unordered_map)
> - [cppreference: structured binding](https://en.cppreference.com/w/cpp/language/structured_binding)
