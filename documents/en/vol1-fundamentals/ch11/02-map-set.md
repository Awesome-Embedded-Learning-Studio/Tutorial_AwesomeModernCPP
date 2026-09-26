---
chapter: 11
cpp_standard:
- 11
- 14
- 17
- 20
description: Master the core operations of `std::map`, `std::set`, and `std::unordered_map`, and learn key-based lookup and maintaining sorted collections.
difficulty: beginner
order: 2
platform: host
prerequisites:
- std::vector Quick Start
reading_time_minutes: 13
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: Associative Containers Quick Start
translation:
  source: documents/vol1-fundamentals/ch11/02-map-set.md
  source_hash: 4a8480038d214e0f7a6833c9b4c89b0b6912577fa315d34a5e5266fd86fdcd30
  translated_at: '2026-09-25T11:58:47+00:00'
  engine: anthropic
  token_count: 5500
---
# Associative Containers Quick Start: Give a Key, Get a Result

In the previous article we walked `std::vector` from head to tail—a dynamic array with contiguous storage and O(1) subscript access, the go-to tool whenever you process data whose element order matters. But in many scenarios what we care about is not "which element sits at position N" but "what value does this key map to". Counting how often each word appears in a text, or checking whether a word is in a spelling dictionary—these "give a key, get a result" demands are awkward with a vector: you either sort and binary-search, or scan linearly, both of which are painful to write and perform poorly. The C++ standard library ships a group of containers built precisely for this class of problems, called **associative containers**.

This chapter introduces three siblings: `std::map` (sorted key-value pairs), `std::set` (a sorted collection of unique elements), and `std::unordered_map` (hashed key-value pairs). They share one trait: lookup, insertion, and deletion are all fast, and none of them requires traversing the whole container. The difference lies underneath: `map` and `set` are implemented with red-black trees, so elements stay ordered and every operation costs O(log n); `unordered_map` uses a hash table—O(1) on average, but with no ordering guarantee.

## Logging In — Basic std::map Operations

`std::map` is a sorted key-value container, declared in the `<map>` header. Each of its elements is a `std::pair<const Key, Value>`, where Key is the key type and Value is the value type. Internally it stores elements in a red-black tree (a self-balancing binary search tree), so elements are always arranged in ascending key order, and lookup, insertion, and deletion are all O(log n).

First, let's see how to put things into it:

```cpp
#include <iostream>
#include <map>
#include <string>

int main()
{
    std::map<std::string, int> scores;

    // Way 1: assign through operator[]
    scores["Alice"] = 95;
    scores["Bob"] = 87;

    // Way 2: insert a pair with insert
    scores.insert({"Charlie", 72});

    // Way 3: construct in place with emplace (recommended)
    scores.emplace("Diana", 91);

    // Way 4: initializer list
    std::map<std::string, int> ages = {
        {"Alice", 22}, {"Bob", 25}, {"Charlie", 20}
    };

    return 0;
}
```

Each of these insertion styles has its place. `operator[]` is the most intuitive, but it hides a genuinely sneaky behavior: when the key does not exist, it automatically inserts a value-initialized element (0 for `int`; for class types it calls the default constructor). In other words, even if we only wanted to peek at the value, `scores["Eve"]` stuffs a `{"Eve", 0}` into the map. More on that later.

Next, lookup. `find` returns an iterator pointing to the element found, or `end()` if there is none. `count` returns the number of matching elements (for a map, either 0 or 1). C++20 added `contains`, whose meaning is the most direct:

```cpp
// Works in every version since C++11
auto it = scores.find("Alice");
if (it != scores.end()) {
    std::cout << "Alice: " << it->second << "\n";
}

// count also tests for existence
if (scores.count("Bob")) {
    std::cout << "Bob exists\n";
}

// contains, introduced in C++20, has the clearest semantics
if (scores.contains("Diana")) {
    std::cout << "Diana exists\n";
}
```

For deletion we use `erase`, either by key or by iterator:

```cpp
scores.erase("Bob");            // erase by key
scores.erase(scores.begin());   // erase the first element (the smallest key)
scores.clear();                 // clear the whole map
```

When the key is absent, `map[key]` **automatically inserts a default value**. Two consequences follow. If we only meant to check whether a key exists, using `operator[]` silently modifies the map—which is a logic bug—and if our value type has no default constructor, it will not even compile. On top of that, `operator[]` is flat-out unusable on a `const map`, because it is a mutating operation. So: for read-only lookups, use `find`, `count`, or `contains`; for bounds-checked access, use `at()`—like vector's `at`, it throws `std::out_of_range` when the key does not exist.

## Switching Gears — Maintaining Unique, Sorted Collections with std::set

`std::set` is declared in the `<set>` header and can be understood as "a map with keys but no values". All of its elements are unique and always sorted. Whenever we need to deduplicate, or to answer "does this thing belong to the set", `set` earns its keep.

Its basic operations look nearly identical to map's:

```cpp
#include <iostream>
#include <set>

int main()
{
    std::set<int> s = {5, 3, 1, 4, 2, 3, 1};

    // Duplicate elements are dropped automatically, and elements end up sorted
    // s: {1, 2, 3, 4, 5}

    s.insert(6);        // insert
    s.emplace(0);       // construct in place and insert
    s.erase(3);         // erase by key

    // Lookup
    if (s.contains(4)) {            // C++20
        std::cout << "4 is in the set\n";
    }

    if (s.count(2)) {               // works in all C++ versions
        std::cout << "2 is in the set\n";
    }

    auto it = s.find(1);
    if (it != s.end()) {
        std::cout << "Found: " << *it << "\n";
    }

    return 0;
}
```

You will find that set's interface mirrors map's almost exactly, minus `operator[]` and `at`—there is no "value" to access, and dereferencing the iterator hands you the key itself. One other small difference: set's `insert` returns a `pair<iterator, bool>`, where the `bool` tells you whether the insertion actually happened (`false` means the element was already there).

One easily overlooked feature is that set provides `lower_bound` and `upper_bound`, which we can use for range queries. For example, to find all elements in the set that are greater than or equal to 3 and less than 7:

```cpp
std::set<int> s = {1, 3, 5, 7, 9};
auto lo = s.lower_bound(3);   // points at 3
auto hi = s.upper_bound(7);   // points at 9
for (auto it = lo; it != hi; ++it) {
    std::cout << *it << " ";   // Output: 3 5 7
}
```

## Walking the Key-Value Pairs — Traversing Associative Containers

Like vector, associative containers support range-for traversal. But a map's element type is `pair<const Key, Value>`, so in C++11 we access the key and the value through `.first` and `.second`:

```cpp
std::map<std::string, int> scores = {
    {"Alice", 95}, {"Bob", 87}, {"Charlie", 72}
};

// C++11 style
for (const auto& p : scores) {
    std::cout << p.first << ": " << p.second << "\n";
}
```

C++17 introduced **structured bindings**, which let us give the two members of the pair a name each—a big readability win:

```cpp
// C++17 style — recommended
for (const auto& [name, score] : scores) {
    std::cout << name << ": " << score << "\n";
}
```

`[name, score]` is the structured-binding syntax: `name` binds to `pair.first`, `score` binds to `pair.second`. Note that we use `const auto&` rather than `auto`—same as when traversing a vector—to avoid unnecessary copies. If we need to modify values during traversal (note: the key is `const` and cannot be modified), just drop the `const`:

```cpp
// Give everyone extra points
for (auto& [name, score] : scores) {
    score += 5;
    // name += "x";  // compile error! the key is const
}
```

Traversing a set is even simpler, since it has only a key:

```cpp
std::set<int> s = {5, 3, 1, 4, 2};
for (const auto& elem : s) {
    std::cout << elem << " ";   // Output: 1 2 3 4 5 (sorted)
}
```

## Swapping the Engine — std::unordered_map

`std::unordered_map` is declared in the `<unordered_map>` header and does almost exactly what `std::map` does: both are key-value containers, and both support `insert`, `emplace`, `erase`, `find`, `count`, `contains` (C++20), `operator[]`, and `at`. But the underlying data structure is completely different: `map` uses a red-black tree, `unordered_map` uses a hash table.

That difference has several practical consequences. Lookup performance: `map` is a steady O(log n), while `unordered_map` averages O(1) but degrades to O(n) in the worst case—when a large number of keys collide in the hash table. Element order: `map` is always sorted by key, whereas `unordered_map`'s element order is unpredictable, and every insertion or deletion may reshuffle it. Memory usage: a hash table usually consumes more memory than a red-black tree.

So when should you use which? A simple rule of thumb: if you need to traverse elements in key order, or need range queries like `lower_bound`/`upper_bound`, use `map`; if you are just doing frequent "give a key, fetch a value" operations and don't care about order, `unordered_map` is faster. For the vast majority of everyday scenarios, `unordered_map` is the more fitting choice—after all, pure key-lookup use cases far outnumber those that genuinely need ordered traversal.

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

    // The interface is exactly the same as map's
    if (auto it = freq.find("hello"); it != freq.end()) {
        std::cout << it->first << ": " << it->second << "\n";
    }

    // But iteration order is not guaranteed
    for (const auto& [word, count] : freq) {
        std::cout << word << " -> " << count << "\n";
    }

    return 0;
}
```

`unordered_map` requires the key type to either have a default `std::hash` specialization, or to come with a hash function we provide ourselves. The standard library already provides `std::hash` specializations for the built-in types (`int`, `double`, `std::string`, and so on), so those types work as keys directly. But if we want to use a custom struct as an `unordered_map` key, we have to implement a `std::hash` specialization and `operator==` ourselves, otherwise it simply fails to compile. By comparison, `std::map` only asks that the key support `operator<` (or a custom comparator)—a much lower bar. When a custom type used as a key refuses to compile, first check whether you used `unordered_map` and forgot to supply a hash function.

## Hands-On Time — Word Frequency Counting and Spell Checking

Now let's knead map and set together into one hands-on program. The first feature is word frequency counting: read in a piece of text and count how often each word appears, using a `std::map`. The second feature is spell checking: store a dictionary in a `std::set`, then check whether the input words are in it.

```cpp
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

/// Split a string into a list of words by whitespace
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

/// Count how often each word occurs, using a map
void word_frequency_demo()
{
    std::string text = "the cat sat on the mat and the cat slept";
    auto words = split_words(text);

    std::map<std::string, int> freq;
    for (const auto& w : words) {
        // operator[] is exactly right here: inserts 0 if absent, then ++ increments
        ++freq[w];
    }

    std::cout << "=== Word Frequency ===\n";
    for (const auto& [word, count] : freq) {
        std::cout << "  " << word << ": " << count << "\n";
    }
}

/// Simple spell checking with a set
void spell_check_demo()
{
    // Build a small dictionary
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

/// Compare the iteration order of map and unordered_map
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

Look at the word frequency output: `map` automatically arranged the results in lexicographic key order—that is the ordering the red-black tree buys us. We counted with `++freq[w]`, and here `operator[]`'s "insert the default value 0 if absent" behavior is exactly what we want: the first time a word appears, it is inserted as 0 and incremented to 1; every later occurrence just keeps incrementing. But be very careful—this pattern is only appropriate when you truly want "create on access"; in a read-only lookup it is a trap.

In the spell-check part, set's `contains` method (C++20) keeps the code crystal clear: a single line decides whether a word is in the dictionary. If your compiler does not support C++20, substitute `count`: `dictionary.count(w) != 0`.

## Your Turn — Exercises

### Exercise 1: Student Grade Management

Use `std::map<std::string, int>` to implement a simple grade-management program: it should support adding students with grades, querying a grade by name, removing a student, and listing all students with their grades (sorted by name). You are required to use `find`—not `operator[]`—to check whether a student exists.

```cpp
void add_student(std::map<std::string, int>& db,
                 const std::string& name, int score);
bool get_score(const std::map<std::string, int>& db,
               const std::string& name, int& out_score);
void list_all(const std::map<std::string, int>& db);
```

### Exercise 2: Rewriting Word Frequency Counting with unordered_map

Replace the `std::map` in the hands-on program above with `std::unordered_map`, and observe how the output order changes. Then time both versions with `<chrono>` and compare their performance on a text containing 100,000 random words. Get a feel for how much O(1) and O(log n) actually differ once the data grows.

### Exercise 3: Set Operations

Use two `std::set<int>` objects to store sets A and B respectively, and implement intersection, union, and difference by hand. (Hint: iterate one of the sets and probe the other with `contains` or `find`.)

```cpp
std::set<int> set_union(const std::set<int>& a, const std::set<int>& b);
std::set<int> set_intersection(const std::set<int>& a, const std::set<int>& b);
std::set<int> set_difference(const std::set<int>& a, const std::set<int>& b);
```

---

> **References**
>
> - [cppreference: std::map](https://en.cppreference.com/w/cpp/container/map)
> - [cppreference: std::set](https://en.cppreference.com/w/cpp/container/set)
> - [cppreference: std::unordered_map](https://en.cppreference.com/w/cpp/container/unordered_map)
> - [cppreference: structured binding](https://en.cppreference.com/w/cpp/language/structured_binding)
