---
chapter: 11
cpp_standard:
- 11
- 14
- 17
- 20
description: Master the core operations of `std::map`, `std::set`, and `std::unordered_map`,
  and learn how to perform key-based lookups and maintain ordered collections.
difficulty: beginner
order: 2
platform: host
prerequisites:
- std::vector 快速上手
reading_time_minutes: 13
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: Quick Start with Associative Containers
translation:
  source: documents/vol1-fundamentals/ch11/02-map-set.md
  source_hash: 00dfbb2064cc83d1d706821fd59f93f6fa2a60b3c3141b0a245196c460d0819e
  translated_at: '2026-06-16T03:47:31.886997+00:00'
  engine: anthropic
  token_count: 2768
---
# Quick Start with Associative Containers

In the previous chapter, we went through `std::vector` from beginning to end—dynamic arrays, contiguous storage, O(1) random access via index. It's the go-to choice for handling ordered sequences. However, in many scenarios, we don't care about "what is the element at position X," but rather "what is the value corresponding to a specific key." For example, counting word occurrences in a text, or checking if a word exists in a spelling dictionary. For these "lookup by key" requirements, using a `vector` requires either sorting followed by binary search or linear scanning, which is tedious to write and performs poorly. The C++ Standard Library provides a group of containers specifically designed for such problems, known as **associative containers**.

In this chapter, we will focus on the trio: `std::map` (ordered key-value pairs), `std::set` (ordered unique element sets), and `std::unordered_map` (hashed key-value pairs). Their shared characteristic is that lookup, insertion, and deletion operations are fast without traversing the entire container. The difference lies in the implementation: `std::map` and `std::set` use red-black trees internally, keeping elements ordered with O(log n) complexity; while `std::unordered_map` uses hash tables, offering average O(1) performance without guaranteed order.

> **Learning Objectives**
>
> After completing this chapter, you will be able to:
>
> - [ ] Use `std::map` for insertion, lookup, and deletion operations
> - [ ] Understand the default insertion trap of `operator[]` and know when to use `insert()` or `try_emplace()`
> - [ ] Use `std::set` to maintain ordered unique element sets
> - [ ] Iterate over maps using structured binding: `for (auto &[key, value] : map)`
> - [ ] Understand the performance differences between `std::map` and `std::unordered_map` and make informed choices
> - [ ] Write practical programs for word frequency statistics and spell checking using maps and sets

## Getting Started — Basic Operations with std::map

`std::map` is an ordered key-value container declared in the `<map>` header file. Each element is a `std::pair<Key, Value>`, where `Key` is the key type and `Value` is the value type. It uses a red-black tree (a self-balancing binary search tree) internally, so elements are always sorted in ascending order by key. Lookup, insertion, and deletion are all O(log n).

Let's first look at how to insert data:

```cpp
#include <iostream>
#include <string>
#include <map>

int main() {
    // 1. Constructor initialization
    std::map<std::string, int> scores = {
        {"Alice", 90},
        {"Bob", 85}
    };

    // 2. insert() member function
    scores.insert({"Charlie", 88}); // Insert pair directly
    scores.insert(std::make_pair("David", 92)); // Insert using make_pair

    // 3. operator[] (CAUTION: modifies map if key doesn't exist)
    scores["Eve"] = 95;

    // 4. try_emplace() (C++17, recommended for avoiding temporary objects)
    scores.try_emplace("Frank", 89);

    for (const auto& [name, score] : scores) {
        std::cout << name << ": " << score << std::endl;
    }

    return 0;
}
```

Each insertion method has its use cases. `operator[]` is the most intuitive, but it has a very insidious behavior—if the key doesn't exist, it automatically inserts a value-initialized element (0 for `int`, or the default constructor called for class types). This means `scores["Unknown"]` will insert a `{"Unknown", 0}` entry even if you just wanted to check the value. We will detail this pitfall later.

Next is lookup. `find()` returns an iterator pointing to the found element, or `end()` if not found. `count()` returns the number of matching elements (0 or 1 for a map). C++20 introduced `contains()`, which has more intuitive semantics:

```cpp
#include <map>

int main() {
    std::map<std::string, int> scores = { {"Alice", 90} };

    // 1. find() - returns iterator
    auto it = scores.find("Alice");
    if (it != scores.end()) {
        std::cout << "Found: " << it->second << std::endl;
    }

    // 2. count() - returns 0 or 1
    if (scores.count("Bob")) {
        std::cout << "Bob exists" << std::endl;
    }

    // 3. contains() - C++20, returns bool
    if (scores.contains("Alice")) {
        std::cout << "Alice is here" << std::endl;
    }

    return 0;
}
```

Deletion uses `erase()`, which can remove by key or by iterator:

```cpp
#include <map>

int main() {
    std::map<std::string, int> scores = { {"Alice", 90}, {"Bob", 85}, {"Charlie", 88} };

    // 1. Erase by key
    scores.erase("Bob");

    // 2. Erase by iterator
    auto it = scores.find("Charlie");
    if (it != scores.end()) {
        scores.erase(it);
    }

    return 0;
}
```

> **Pitfall Warning**: `operator[]` **automatically inserts a default value** when the key doesn't exist. This has two consequences: First, if you just want to check if a key exists, using `operator[]` silently modifies the map, which is a logical bug. Furthermore, if your value type doesn't have a default constructor, the code won't compile. Second, on `const map`, `operator[]` is simply not available because it is a modifying operation. Therefore, for read-only lookup, please use `find()`, `count()`, or `contains()`. If you need bounds-checked access, use `at()`—it throws an `std::out_of_range` exception, just like `vector::at()`.

## Different Style — Maintaining Unique Ordered Sets with std::set

`std::set` is declared in the `<set>` header file and can be understood as a "map with only keys and no values." All its elements are unique and always sorted. When we need deduplication or to determine "if something belongs to a set," `std::set` comes into play.

Basic operations are very similar to `map`:

```cpp
#include <iostream>
#include <set>

int main() {
    std::set<int> numbers;

    // Insert
    numbers.insert(5);
    numbers.insert(3);
    numbers.insert(5); // Duplicate, will be ignored
    numbers.insert(1);

    // Lookup
    if (numbers.contains(3)) { // C++20
        std::cout << "3 is in the set" << std::endl;
    }

    // Deletion
    numbers.erase(5);

    // Iteration
    for (int n : numbers) {
        std::cout << n << " "; // Output: 1 3
    }
    std::cout << std::endl;

    return 0;
}
```

You will notice that `set`'s interface is almost identical to `map`, except it lacks `operator[]` and `at()`—because `set` has no "value" to access; dereferencing an iterator yields the key itself. Another minor difference is that `set::insert()` returns a `pair<iterator, bool>`, where the `bool` tells you whether the insertion actually happened (returns `false` if the element already exists).

An easily overlooked feature is that `set` provides `lower_bound()` and `upper_bound()`, which are useful for range queries. For example, finding all elements in the set greater than or equal to 3 and less than 7:

```cpp
#include <iostream>
#include <set>

int main() {
    std::set<int> numbers = {1, 3, 5, 7, 9, 11};

    // Find first element >= 3
    auto start = numbers.lower_bound(3);
    // Find first element > 7
    auto end = numbers.upper_bound(7);

    for (auto it = start; it != end; ++it) {
        std::cout << *it << " "; // Output: 3 5 7
    }
    std::cout << std::endl;

    return 0;
}
```

## Iterating Key-Value Pairs — Traversing Associative Containers

Like `vector`, associative containers support range-for loops. However, `map`'s element type is `std::pair<const Key, Value>`. In C++11, you needed to access keys and values via `first` and `second`:

```cpp
for (const auto& pair : scores) {
    std::cout << pair.first << ": " << pair.second << std::endl;
}
```

C++17 introduced **structured binding**, allowing us to name the two members of the pair individually, significantly improving readability:

```cpp
for (const auto& [key, value] : scores) {
    std::cout << key << ": " << value << std::endl;
}
```

`auto [key, value]` is the syntax for structured binding. `key` binds to `pair.first`, and `value` binds to `pair.second`. Note the use of `const auto&` instead of `auto`—just like when iterating vectors, this avoids unnecessary copies. If you need to modify the value during iteration (note: the `key` is `const` and cannot be modified), simply remove the `const`:

```cpp
for (auto& [key, value] : scores) {
    value += 10; // OK
    // key = "new"; // ERROR: key is const
}
```

Iterating `set` is simpler since it only has a key:

```cpp
for (int n : numbers) {
    std::cout << n << " ";
}
```

## Changing the Engine — std::unordered_map

`std::unordered_map` is declared in the `<unordered_map>` header. Its functionality is nearly identical to `std::map`—both are key-value containers supporting `insert()`, `erase()`, `find()`, `count()`, `contains()` (C++20), `operator[]`, and `at()`. However, the underlying data structure is completely different: `std::map` uses a red-black tree, while `std::unordered_map` uses a hash table.

This difference brings several practical implications. Regarding lookup performance, `std::map` is stable at O(log n), while `std::unordered_map` is average O(1) but worst-case O(n)—degrading when many keys hash to the same bucket. Regarding element order, `std::map` is always sorted by key, whereas `std::unordered_map`'s order is unpredictable; insertion or deletion can change the order. Regarding memory usage, hash tables generally consume more memory than red-black trees.

So, when should you use which? A simple selection criterion: if you need to iterate elements in key order or need range queries like `lower_bound()`/`upper_bound()`, use `std::map`. If you just frequently do "give a key, get a value" and don't care about order, `std::unordered_map` is faster. In most daily scenarios, `std::unordered_map` is the better choice—pure key-based lookup is far more common than ordered traversal.

```cpp
#include <iostream>
#include <string>
#include <unordered_map>

int main() {
    std::unordered_map<std::string, int> ages;

    ages["Alice"] = 30;
    ages["Bob"] = 25;

    // Fast lookup
    if (ages.contains("Alice")) {
        std::cout << "Alice is " << ages["Alice"] << " years old." << std::endl;
    }

    // Order is not guaranteed
    for (const auto& [name, age] : ages) {
        std::cout << name << ": " << age << std::endl;
    }

    return 0;
}
```

> **Pitfall Warning**: `std::unordered_map` requires the key type to either have a default `std::hash` specialization or for you to manually provide a hash function. The standard library provides `std::hash` specializations for built-in types (`int`, `double`, `std::string`, etc.), so these can be used as keys directly. However, if you want to use a custom struct as a key for `std::unordered_map`, you must implement the `std::hash` specialization and `operator==`, otherwise the code won't compile. In contrast, `std::map` only requires the key to support `operator<` (or a custom comparator), which is a lower barrier to entry. If you find that compilation fails with a custom key type, check if you are using `std::unordered_map` and forgot to provide a hash function.

## Practice Time — Word Frequency and Spell Checking

Now let's combine `map` and `set` to write a practical program. The first feature is word frequency statistics: read a text and use `std::map` to count occurrences of each word. The second feature is spell checking: use a `std::set` to store a dictionary and check if input words exist in it.

```cpp
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <map>
#include <set>
#include <algorithm> // For std::transform

// Helper function to convert string to lowercase
std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    return s;
}

int main() {
    // 1. Word Frequency Statistics
    std::string text = "Hello world Hello C++ World Map Set";
    std::stringstream ss(text);
    std::string word;

    std::map<std::string, int> word_counts;

    while (ss >> word) {
        word = to_lower(word);
        word_counts[word]++;
    }

    std::cout << "--- Word Frequencies ---" << std::endl;
    for (const auto& [word, count] : word_counts) {
        std::cout << word << ": " << count << std::endl;
    }

    // 2. Spell Checking
    std::set<std::string> dictionary = {
        "hello", "world", "cpp", "map", "set", "test"
    };

    std::cout << "\n--- Spell Check ---" << std::endl;
    std::vector<std::string> words_to_check = {"hello", "java", "map", "rust"};

    for (const auto& w : words_to_check) {
        if (dictionary.contains(to_lower(w))) { // C++20
            std::cout << w << ": OK" << std::endl;
        } else {
            std::cout << w << ": MISSPELLED" << std::endl;
        }
    }

    return 0;
}
```

Compile and run:

```bash
g++ -std=c++20 main.cpp -o main
./main
```

Expected output:

```text
--- Word Frequencies ---
c++: 1
hello: 2
map: 1
set: 1
world: 2

--- Spell Check ---
hello: OK
java: MISSPELLED
map: OK
rust: MISSPELLED
```

Looking at the word frequency output—`std::map` automatically sorted the results by key in lexicographical order. This is the ordering provided by the red-black tree. In the frequency statistics, we used `operator[]` for counting. Here, the behavior of `operator[]` ("insert default value 0 if missing") is exactly what we want—on the first encounter of a word, it inserts 0 and then increments to 1; subsequent encounters just increment. However, be careful: this usage only applies when you truly want "create on access." In read-only lookups, it is a trap.

For the spell checking part, the `contains()` method (C++20) of `std::set` makes the code very clear—just one line to check if a word is in the dictionary. If your compiler doesn't support C++20, you can use `count()` instead: `if (dictionary.count(word) > 0)`.

## Your Turn — Exercises

### Exercise 1: Student Grade Management

Use `std::map` to implement a simple grade management program: support adding students and grades, querying grades by name, deleting students, and listing all students and their grades (sorted by name). Requirement: use `find()` to check if a student exists, not `operator[]`.

```cpp
#include <iostream>
#include <string>
#include <map>

int main() {
    std::map<std::string, int> grades;
    std::string command, name;
    int score;

    while (std::cin >> command) {
        if (command == "add") {
            std::cin >> name >> score;
            grades[name] = score;
        } else if (command == "query") {
            std::cin >> name;
            auto it = grades.find(name);
            if (it != grades.end()) {
                std::cout << name << "'s score: " << it->second << std::endl;
            } else {
                std::cout << "Student " << name << " not found." << std::endl;
            }
        } else if (command == "delete") {
            std::cin >> name;
            if (grades.erase(name)) {
                std::cout << "Deleted " << name << std::endl;
            } else {
                std::cout << "Student " << name << " not found." << std::endl;
            }
        } else if (command == "list") {
            for (const auto& [n, s] : grades) {
                std::cout << n << ": " << s << std::endl;
            }
        } else if (command == "exit") {
            break;
        }
    }
    return 0;
}
```

### Exercise 2: Rewrite Word Frequency with unordered_map

Replace `std::map` with `std::unordered_map` in the practical program above and observe the change in output order. Then use `std::chrono` to time the execution and compare the performance difference between the two implementations when processing a text containing 100,000 random words. Experience the actual difference between O(1) and O(log n) with large datasets.

### Exercise 3: Set Operations

Use two `std::set`s to store sets A and B, and manually implement intersection, union, and difference operations. (Hint: Iterate through one set and use `find()` or `contains()` to check in the other set.)

```cpp
#include <iostream>
#include <set>
#include <vector>

int main() {
    std::set<int> A = {1, 2, 3, 4, 5};
    std::set<int> B = {4, 5, 6, 7, 8};
    std::set<int> intersection, difference;

    // Intersection
    for (int x : A) {
        if (B.contains(x)) { // C++20, or use B.count(x)
            intersection.insert(x);
        }
    }

    // Difference (A - B)
    for (int x : A) {
        if (!B.contains(x)) {
            difference.insert(x);
        }
    }

    std::cout << "Intersection: ";
    for (int x : intersection) std::cout << x << " ";
    std::cout << std::endl;

    std::cout << "Difference (A-B): ";
    for (int x : difference) std::cout << x << " ";
    std::cout << std::endl;

    return 0;
}
```

## Summary

In this chapter, we covered three core associative containers in C++. `std::map` uses a red-black tree to store ordered key-value pairs with O(log n) lookup, insertion, and deletion, suitable for scenarios requiring ordered traversal or range queries by key. `std::set` is essentially a "map with only keys," used to maintain ordered unique element sets with an interface almost identical to `map`. `std::unordered_map` uses a hash table for average O(1) lookup speed, suitable for pure key-based lookup scenarios, at the cost of guaranteed element order and requiring manual hash functions for custom key types.

Key takeaways: When iterating maps, prioritize C++17's structured binding `for (auto &[key, value] : map)` for clarity. For read-only lookup, avoid `operator[]`; use `find()`, `contains()`, or `count()`. When unsure whether to use `map` or `unordered_map`, ask yourself if you need ordered traversal—if not, choose `unordered_map`.

In the next chapter, we will dive into the STL algorithm library—sorting, searching, transforming, and statistics. The standard library provides a plethora of generic algorithms ready for use. You will discover that containers combined with algorithms represent the true power of the STL.

---

> **References**
>
> - [cppreference: std::map](https://en.cppreference.com/w/cpp/container/map)
> - [cppreference: std::set](https://en.cppreference.com/w/cpp/container/set)
> - [cppreference: std::unordered_map](https://en.cppreference.com/w/cpp/container/unordered_map)
> - [cppreference: structured binding](https://en.cppreference.com/w/cpp/language/structured_binding)
