---
chapter: 10
cpp_standard:
- 11
- 14
- 17
- 20
description: Master the core operations of std::map, std::set, and std::unordered_map, and learn key-based lookup and maintaining sorted collections
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
  source: documents/vol1-fundamentals/ch10/02-map-set.md
  source_hash: 7abdb1a6d1c5ac7233661a138f4f33194abdf6790cbecd2823f87ad03f90840c
  translated_at: '2026-09-27T03:59:27+00:00'
  engine: anthropic
  token_count: 4200
---
# Associative Containers: Give a Key, Get a Result

In the previous article we walked `std::vector` from head to tail—dynamic array, contiguous storage, O(1) random access by subscript; it is the workhorse whenever we process a sequence in order. But in many scenarios what we care about is not "which element sits at position N" but "what is the value for this key". Counting how many times each word appears in a text, or checking whether a word is in a spelling dictionary—needs of the "give a key, get a result" kind. Handle them with a vector and you end up either sorting followed by binary search, or a linear scan: painful to write and poor in performance. The C++ standard library has ready for us a group of containers dedicated to exactly this class of problems, called **associative containers**.

What this article sorts out is a trio of siblings: `std::map` (sorted key-value pairs), `std::set` (a sorted collection of unique elements), and `std::unordered_map` (hashed key-value pairs). Their shared trait: lookup, insertion, and deletion are all fast, and none of them require us to walk the whole container. The difference: `map` and `set` are implemented internally with a red-black tree, so elements stay sorted at all times and operations cost O(log n); `unordered_map` uses a hash table—O(1) on average, but with no ordering guarantee.

## Logging In — Basic std::map Operations

Let's look at `std::map`: a sorted key-value container declared in the `<map>` header. Each of its elements is a `std::pair<const Key, Value>`, where Key is the key's type and Value is the value's type. It stores elements in a red-black tree (a self-balancing binary search tree), so they always sit in ascending key order, and lookup, insertion, and deletion are all O(log n).

First, let's see how to stuff things into it:

```cpp
#include <iostream>
#include <map>
#include <string>

int main()
{
    std::map<std::string, int> scores;

    // Method 1: assign through operator[]
    scores["Alice"] = 95;
    scores["Bob"] = 87;

    // Method 2: insert a pair with insert
    scores.insert({"Charlie", 72});

    // Method 3: construct in place with emplace (recommended)
    scores.emplace("Diana", 91);

    // Method 4: initializer list
    std::map<std::string, int> ages = {
        {"Alice", 22}, {"Bob", 25}, {"Charlie", 20}
    };

    return 0;
}
```

Each insertion method has its place. `operator[]` is the most intuitive, but it hides a genuinely insidious behavior: when the key does not exist, it automatically inserts a value-initialized element (0 for `int`; for class types it calls the default constructor). In other words, even when all we want is to peek at the value, `scores["Eve"]` stuffs a `{"Eve", 0}` into the map. More on this later.

Next, lookup. `find` returns an iterator pointing to the element found, or `end()` if there is none. `count` returns the number of matching elements (for a map, either 0 or 1). C++20 added `contains`, whose semantics are the most straightforward:

```cpp
// Works in every standard from C++11 on
auto it = scores.find("Alice");
if (it != scores.end()) {
    std::cout << "Alice: " << it->second << "\n";
}

// count also tests for existence
if (scores.count("Bob")) {
    std::cout << "Bob exists\n";
}

// C++20 introduced contains: the clearest semantics
if (scores.contains("Diana")) {
    std::cout << "Diana exists\n";
}
```

For deletion we use `erase`, either by key or by iterator:

```cpp
scores.erase("Bob");            // erase by key
scores.erase(scores.begin());   // erase the first element (the smallest key)
scores.clear();                 // empty the whole map
```

When the key is absent, `map[key]` **automatically inserts a default value**. Two consequences follow. If all we want is to check whether a key exists, going through `operator[]` silently modifies the map—a logic bug—and if our value type has no default constructor, it fails to compile outright. Moreover, on a `const map`, `operator[]` is not available at all, because it is a modifying operation. So for read-only lookup use `find`, `count`, or `contains`; for bounds-checked access use `at()`—just like vector's `at`, it throws a `std::out_of_range` exception when the key does not exist.

## Switching Gears — std::set Maintains a Sorted, Unique Collection

`std::set` is declared in the `<set>` header and can be understood as "a map with keys but no values". All of its elements are unique and always sorted. Whenever we need to deduplicate, or to answer "does this thing belong to the set", `set` earns its keep.

Notice how closely its basic operations mirror map's:

```cpp
#include <iostream>
#include <set>

int main()
{
    std::set<int> s = {5, 3, 1, 4, 2, 3, 1};

    // Duplicate elements are silently dropped, and the elements are sorted
    // s: {1, 2, 3, 4, 5}

    s.insert(6);        // insert
    s.emplace(0);       // construct in place and insert
    s.erase(3);         // erase by key

    // Lookup
    if (s.contains(4)) {            // C++20
        std::cout << "4 is in the set\n";
    }

    if (s.count(2)) {               // works in every C++ version
        std::cout << "2 is in the set\n";
    }

    auto it = s.find(1);
    if (it != s.end()) {
        std::cout << "Found: " << *it << "\n";
    }

    return 0;
}
```

You will find that set's interface is nearly identical to map's, except there is no `operator[]` and no `at`—set has no "value" to access, and dereferencing the iterator hands you the key itself. Another small difference: set's `insert` returns a `pair<iterator, bool>`, where the `bool` tells you whether an insertion actually happened (`false` if the element already existed).

One easily overlooked feature is that set provides `lower_bound` and `upper_bound`, which come in handy for range queries. For example, finding every element in the set that is greater than or equal to 3 and less than 7:

```cpp
std::set<int> s = {1, 3, 5, 7, 9};
auto lo = s.lower_bound(3);   // points at 3
auto hi = s.upper_bound(7);   // points at 9
for (auto it = lo; it != hi; ++it) {
    std::cout << *it << " ";   // Output: 3 5 7
}
```

## Walking the Pairs — Iterating Associative Containers

Associative containers support range-for loops just like vector. But map's element type is `pair<const Key, Value>`, so in C++11 we reach the key and the value through `.first` and `.second`:

```cpp
std::map<std::string, int> scores = {
    {"Alice", 95}, {"Bob", 87}, {"Charlie", 72}
};

// The C++11 way
for (const auto& p : scores) {
    std::cout << p.first << ": " << p.second << "\n";
}
```

C++17 introduced **structured bindings**, which let us give each of the pair's two members a name of its own—a big jump in readability:

```cpp
// The C++17 way—recommended
for (const auto& [name, score] : scores) {
    std::cout << name << ": " << score << "\n";
}
```

`[name, score]` is the structured-binding syntax: `name` binds to `pair.first`, `score` binds to `pair.second`. Note that we use `const auto&` rather than `auto`—same as when iterating a vector—to avoid needless copies. If we need to modify values during iteration (careful: the key is `const` and cannot be modified), just drop the `const`:

```cpp
// Add points for everyone
for (auto& [name, score] : scores) {
    score += 5;
    // name += "x";  // Compile error! The key is const
}
```

Set iteration is even simpler, since it carries only a key:

```cpp
std::set<int> s = {5, 3, 1, 4, 2};
for (const auto& elem : s) {
    std::cout << elem << " ";   // Output: 1 2 3 4 5 (sorted)
}
```

## Swapping the Engine — std::unordered_map

`std::unordered_map` is declared in the `<unordered_map>` header and does almost exactly what `std::map` does: both are key-value containers, and both support `insert`, `emplace`, `erase`, `find`, `count`, `contains` (C++20), `operator[]`, and `at`. But the underlying data structures are completely different: `map` uses a red-black tree, `unordered_map` a hash table.

Let's look at the practical consequences of that difference. Lookup performance: `map` is a steady O(log n), while `unordered_map` is O(1) on average but O(n) in the worst case—it degrades when large numbers of keys hash to colliding slots. Element order: `map` is always sorted by key, while `unordered_map`'s order is unpredictable, and every insertion or deletion may reshuffle it. Memory footprint: a hash table usually takes more memory than a red-black tree.

So when should you use which? A simple selection rule: if we need to iterate elements in key order, or need range queries like `lower_bound`/`upper_bound`, use `map`; if we just do frequent "give a key, fetch a value" operations and don't care about order, `unordered_map` is faster. In the vast majority of everyday scenarios `unordered_map` is the better fit—after all, pure key lookup is far more common than ordered traversal.

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

    // The interface is identical to map's
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

`unordered_map` demands that the key type either has a default `std::hash` specialization or gets a hash function from us by hand. The standard library already provides `std::hash` specializations for built-in types (`int`, `double`, `std::string`, and friends), so those work as keys directly. But if we want to use a custom struct as an `unordered_map` key, we have to implement the `std::hash` specialization and `operator==` ourselves, or compilation fails on the spot. By comparison, `std::map` only asks the key to support `operator<` (or a custom comparator)—a lower bar. When a custom-key type refuses to compile, first check whether we used `unordered_map` and forgot to supply a hash function.

## Hands-On Time — Word Frequency Counting and Spell Checking

Now let's knead map and set together into one practical program. Feature one is word frequency counting: read in a piece of text and count each word's occurrences with a `std::map`. Feature two is spell checking: store a dictionary in a `std::set`, then check whether each input word is in it.

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

/// Count how often each word appears, using a map
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

/// Compare iteration order of map and unordered_map
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

The complete code is right below—hit "Try It" to run it directly, no terminal needed:

<OnlineCompilerDemo
  title="Hands-On Time: Word Frequency and Spell Checking map_demo.cpp"
  source-path="code/examples/vol1/24_map_set_demo.cpp"
  description="Run the word frequency counter and spell checker online; map's output is automatically sorted lexicographically. Try swapping the map for an unordered_map and watch how the output order changes."
  allow-run
/>

Look at the word-frequency output: `map` sorted the results by key in lexicographic order automatically—that is the ordering the red-black tree buys us. We count with `++freq[w]`, and here `operator[]`'s "insert the default value 0 if absent" behavior is exactly what we want: the first time a word appears it is inserted as 0 and incremented to 1, and later encounters keep incrementing. But make no mistake—this pattern only fits when you genuinely want "create on access"; in a read-only lookup it is a trap.

In the spell-check part, set's `contains` method (C++20) keeps the code crystal clear—one line decides whether a word is in the dictionary. If your compiler does not support C++20, substitute `count`: `dictionary.count(w) != 0`.

## Time to Practice — Exercises

### Exercise 1: Student Score Management

Use a `std::map<std::string, int>` to build a simple score-management program: support adding students with scores, querying a score by name, removing a student, and listing all students with their scores (sorted by name). The requirement: use `find` to test whether a student exists, not `operator[]`.

```cpp
void add_student(std::map<std::string, int>& db,
                 const std::string& name, int score);
bool get_score(const std::map<std::string, int>& db,
               const std::string& name, int& out_score);
void list_all(const std::map<std::string, int>& db);
```

### Exercise 2: Rewriting Word Frequency Counting with unordered_map

Replace the `std::map` in the practical program above with `std::unordered_map` and observe how the output order changes. Then time both versions with `<chrono>` and compare their performance on a text of 100000 random words. Get a feel for how wide the practical gap between O(1) and O(log n) grows once the data volume is large.

### Exercise 3: Set Operations

Use two `std::set<int>` to store sets A and B respectively, and implement intersection, union, and difference by hand. (Hint: iterate one of the sets and probe the other with `contains` or `find`.)

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
