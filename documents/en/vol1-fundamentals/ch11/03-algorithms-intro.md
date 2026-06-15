---
chapter: 11
cpp_standard:
- 11
- 14
- 17
- 20
description: Get started with commonly used algorithms from <algorithm>, combined
  with lambda expressions for flexible data processing
difficulty: beginner
order: 3
platform: host
prerequisites:
- Associative Containers Quick Start
reading_time_minutes: 12
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: Introduction to the Algorithm Library
---
# Introduction to the Algorithm Library

In the previous two chapters, we covered the basic operations of `vector` and associative containers. Now the question is—when you need to sort, search, filter, or aggregate a collection of data, is your first instinct to write a for loop?

Honestly, many people's intuition is indeed to hand-write loops. But the C++ standard library's `<algorithm>` header contains over a hundred thoroughly optimized and tested generic algorithms. Replacing hand-written loops with STL algorithms leads to shorter code, fewer bugs, clearer intent, and often better performance. (These algorithms are battle-tested, after all.)

In this chapter, we will take a practical approach and walk through the most commonly used algorithms hands-on. Along the way, we will frequently use lambda expressions—they are the best partner for STL algorithms, so we will spend a little time understanding them first.

> **Learning Objectives**
>
> After completing this chapter, you will be able to:
>
> - [ ] Understand the basic syntax and capture modes of lambda expressions
> - [ ] Use `std::sort` and `std::stable_sort` to sort data
> - [ ] Use `std::find`, `std::find_if`, `std::binary_search`, and `std::lower_bound` to search for elements
> - [ ] Use `std::copy`, `std::transform`, `std::replace`, and `std::remove` to modify data
> - [ ] Use `std::accumulate`, `std::count`, `std::min_element`, and `std::max_element` for aggregation

## Meet Our Partner — Lambda Expressions

STL algorithms often need a "predicate" or "operation" as a parameter—for example, "what rule to sort by" or "which elements to find." Before C++11, this role was filled by function pointers or function objects, which were verbose and unintuitive. Lambda expressions changed this completely.

The full syntax of a lambda is `[capture](parameters) -> return_type { body }`, where the return type can be omitted (the compiler deduces it automatically), so the most common form is `[capture](params) { body }`. The `capture` in square brackets determines how the lambda accesses outer variables, and this is the part most prone to mistakes.

`[=]` means capture all used outer variables by value—making copies that don't affect the originals. `[&]` means capture all by reference—operations directly affect the outer variables. `[x, &y]` is a mixed capture—`x` by value, `y` by reference. In practice, the recommended approach is to explicitly list the variables you want to capture rather than using `[=]` or `[&]` as a blanket catch-all. This makes the intent clearer and reduces the risk of accidentally modifying external state.

```cpp
std::vector<int> data = {5, 3, 1, 4, 2};
int threshold = 3;

// Capture threshold by value
auto is_above = [threshold](int x) { return x > threshold; };
int count = std::count_if(data.begin(), data.end(), is_above);
// count == 2

// Capture by reference, accumulate into outer variable
int sum = 0;
std::for_each(data.begin(), data.end(), [&sum](int x) { sum += x; });
// sum == 15
```

> **Pitfall Warning**: When a lambda captures a local variable by reference and the lambda's lifetime exceeds that of the local variable, you get a dangling reference—the referenced memory has already been freed. This situation is especially common with async callbacks and stored lambdas. If your lambda needs to be stored or passed to another thread, prefer value capture or explicitly list the variables to capture by value.

## Sort It Out — std::sort and std::stable_sort

Sorting is probably the most frequently used operation in the algorithm library. `std::sort` takes two iterators (or directly a container starting from C++20) and sorts in ascending order by default. Under the hood it uses Introsort—a hybrid of quicksort, heapsort, and insertion sort, with both average and worst-case time complexity of O(n log n):

```cpp
std::vector<int> v = {5, 2, 8, 1, 9, 3};

// Default ascending order
std::sort(v.begin(), v.end());
// v: {1, 2, 3, 5, 8, 9}

// Descending — pass a third parameter, a comparison lambda
std::sort(v.begin(), v.end(), [](int a, int b) { return a > b; });
// v: {9, 8, 5, 3, 2, 1}
```

The third parameter is a lambda—it receives two elements and returns `true` when the first should come before the second. This is the standard pattern for "custom sort rules," and you will see it repeatedly.

The difference between `std::stable_sort` and `sort` is "stability"—when two elements compare equal, `stable_sort` guarantees they maintain their original relative order. For example, if you sort by grade first and then by class, the second sort preserves the grade ordering within each class. The trade-off is slightly higher time and space overhead, but for scenarios that require sort stability, it is indispensable.

> **Pitfall Warning**: The comparison function passed to `sort` must satisfy "strict weak ordering." Simply put: `comp(a, a)` must return `false`, if `comp(a, b)` is `true` then `comp(b, a)` must be `false`, and transitivity must hold. If you write `<=` instead of `<`, some standard library implementations will cause undefined behavior—possibly an infinite loop, a crash, or just incorrect sort results. So always use `<` (ascending) or `>` (descending) in your comparison functions, never `<=` or `>=`.

## Find Things — The std::find Family and Binary Search

### Linear Search

`std::find` linearly searches a range for the first element equal to a specified value, returning an iterator to it; if not found, it returns `end()`. `std::find_if` is similar, but the match condition is determined by a lambda:

```cpp
std::vector<std::string> names = {"Alice", "Bob", "Charlie", "David"};

// find: search for an element equal to the specified value
auto it1 = std::find(names.begin(), names.end(), "Charlie");
// it1 points to "Charlie"

// find_if: find the first element satisfying a condition
auto it2 = std::find_if(names.begin(), names.end(),
    [](const std::string& s) { return s.size() > 4; });
// it2 points to "Alice"
```

Linear search has O(n) time complexity and works regardless of whether the data is sorted.

### Binary Search

If your data is already sorted, binary search is much more efficient—O(log n). `std::binary_search` returns a `bool` telling you whether the value exists, but not where it is. If you need to know the exact position, use `std::lower_bound`, which returns an iterator to the first element greater than or equal to the target value:

```cpp
std::vector<int> v = {1, 3, 5, 7, 9, 11};

bool found = std::binary_search(v.begin(), v.end(), 7);  // true
auto it = std::lower_bound(v.begin(), v.end(), 6);
// *it == 7, i.e., the first element >= 6
```

Calling `lower_bound` or `binary_search` on unsorted data won't produce an error, but the result is undefined—the kind of bug where "it compiles, it runs, it doesn't crash, but the results are untrustworthy." These are especially painful to debug.

## Make Some Changes — Copy, Transform, Replace, Remove

`std::copy` copies elements from one range to a destination. `std::transform` is more powerful—it applies a transformation function to each element while copying. `std::replace` replaces all elements equal to a certain value with another value:

```cpp
std::vector<int> src = {1, 2, 3, 4, 5};

// copy
std::vector<int> dst;
std::copy(src.begin(), src.end(), std::back_inserter(dst));
// dst: {1, 2, 3, 4, 5}

// transform: multiply each element by 10
std::vector<int> multiplied;
std::transform(src.begin(), src.end(), std::back_inserter(multiplied),
    [](int x) { return x * 10; });
// multiplied: {10, 20, 30, 40, 50}

// replace: replace all 3s with 99
std::vector<int> v = {1, 3, 5, 3, 7};
std::replace(v.begin(), v.end(), 3, 99);
// v: {1, 99, 5, 99, 7}
```

Here we see a new face: `std::back_inserter`—it is an insert iterator where assigning to it is equivalent to calling the container's `push_back`. This way, `copy` and `transform` don't need the destination container to be pre-allocated.

### Revisiting Remove-Erase

In the previous chapter on `vector`, we used the remove-erase idiom. Now let's understand the mechanics more deeply. `std::remove` moves all elements not equal to the target value to the front, then returns an iterator pointing to the "new logical end"—this process does not change the container's size or call destructors; it purely moves elements around in known memory. After that, you use the container's `erase` to actually delete everything from the new end to the old end. It takes two steps to complete the job:

```cpp
std::vector<int> v = {1, 2, 3, 2, 4, 2, 5};

auto new_end = std::remove(v.begin(), v.end(), 2);
// v's contents might be: {1, 3, 4, 5, ?, ?, ?}
//                         ^new_end         ^v.end()

v.erase(new_end, v.end());
// v: {1, 3, 4, 5}
```

`std::remove_if` follows the same pattern, but the condition is determined by a lambda. Starting from C++20, `std::erase(v, value)` and `std::erase_if(v, pred)` do it in one step. If your compiler supports C++20, just use the new syntax.

## Crunch the Numbers — Accumulate, Count, Min/Max

The last group of commonly used algorithms is about "reducing a collection of data to a single value." `std::accumulate` (requires the `<numeric>` header) accumulates elements in a range one by one, starting from an initial value you specify—it can also accept a custom binary operation to compute products, concatenate strings, and so on. `std::count` / `std::count_if` count elements equal to a value or satisfying a condition. `std::min_element` / `std::max_element` return iterators to the smallest and largest elements, respectively:

```cpp
std::vector<int> v = {3, 1, 4, 1, 5, 9, 2, 6};

int sum = std::accumulate(v.begin(), v.end(), 0);          // 31
int product = std::accumulate(v.begin(), v.end(), 1,       // 6480
    std::multiplies<int>());
int ones = std::count(v.begin(), v.end(), 1);               // 2
int above_4 = std::count_if(v.begin(), v.end(),             // 3
    [](int x) { return x > 4; });

auto min_it = std::min_element(v.begin(), v.end());  // *min_it == 1
auto max_it = std::max_element(v.begin(), v.end());  // *max_it == 9
```

Note that the type of `accumulate`'s initial value determines the return type of the entire computation. Passing `0` gives `int`, `0.0` gives `double`, and `0LL` gives `long long`. If your vector holds large integers and you pass `0` as the initial value, there is an overflow risk—this is a classic pitfall.

## Let's Go — Hands-On: Student Grade Processing

Now let's combine all the algorithms and lambda expressions from this chapter into a practical program. The scenario is straightforward: process a batch of student grade data, performing sorting, finding top students, calculating averages, and filtering failing grades.

```cpp
#include <algorithm>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

struct Student {
    std::string name;
    double score;
};

void print_student(const Student& s)
{
    std::cout << "  " << s.name << ": " << s.score << "\n";
}

int main()
{
    std::vector<Student> students = {
        {"Alice",   92.5},
        {"Bob",     58.0},
        {"Charlie", 76.0},
        {"Diana",   88.5},
        {"Eve",     45.0},
        {"Frank",   95.0},
        {"Grace",   71.5},
    };

    // --- 1. Sort by score, high to low ---
    std::sort(students.begin(), students.end(),
        [](const Student& a, const Student& b) { return a.score > b.score; });

    std::cout << "=== Ranking (high to low) ===\n";
    for (const auto& s : students) { print_student(s); }

    // --- 2. Find the top student ---
    auto top = std::max_element(students.begin(), students.end(),
        [](const Student& a, const Student& b) { return a.score < b.score; });
    std::cout << "\nTop student: " << top->name
              << " (" << top->score << ")\n";

    // --- 3. Calculate the average score ---
    double sum = std::accumulate(students.begin(), students.end(), 0.0,
        [](double acc, const Student& s) { return acc + s.score; });
    std::cout << "Average score: "
              << sum / static_cast<double>(students.size()) << "\n";

    // --- 4. Count passing and failing students ---
    int passing = std::count_if(students.begin(), students.end(),
        [](const Student& s) { return s.score >= 60.0; });
    std::cout << "Passing: " << passing
              << ", Failing: " << static_cast<int>(students.size()) - passing
              << "\n";

    // --- 5. Filter out failing students (remove-erase) ---
    std::vector<Student> filtered = students;
    auto it = std::remove_if(filtered.begin(), filtered.end(),
        [](const Student& s) { return s.score < 60.0; });
    filtered.erase(it, filtered.end());

    std::cout << "\n=== Passing students ===\n";
    for (const auto& s : filtered) { print_student(s); }

    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17 -Wall -Wextra -o algo_demo algo_demo.cpp && ./algo_demo
```

Expected output:

```text
=== Ranking (high to low) ===
  Frank: 95
  Alice: 92.5
  Diana: 88.5
  Charlie: 76
  Grace: 71.5
  Bob: 58
  Eve: 45

Top student: Frank (95)
Average score: 75.2143
Passing: 5, Failing: 2

=== Passing students ===
  Frank: 95
  Alice: 92.5
  Diana: 88.5
  Charlie: 76
  Grace: 71.5
```

The entire program—from sorting to aggregation to filtering—uses no hand-written for loops for data manipulation. That is the power of STL algorithms. The intent of each operation is immediately clear: `sort` means sorting, `max_element` means finding the maximum, `count_if` means conditional counting, and `remove_if` + `erase` means conditional deletion. Compared to hand-written loops, the intent is expressed far more clearly.

## Try It Yourself — Exercises

### Exercise 1: Multi-Field Sorting

Define a struct `Employee` with `name` (`std::string`), `department` (`std::string`), and `salary` (`int`). Create a vector of employees and sort them first by department name in lexicographic order, then by salary in descending order within each department. Hint: in the lambda, compare departments first, then compare salaries when departments are equal.

```cpp
struct Employee {
    std::string name;
    std::string department;
    int salary;
};
```

### Exercise 2: Text Processing Pipeline

Given a `std::vector<std::string>` representing lines of text, use STL algorithms to implement a simple text processing pipeline: remove all empty lines (`remove_if`), convert each line to lowercase (`std::transform` processing character by character), then sort lexicographically and deduplicate (`std::unique` + `erase`). Each step should be a single algorithm call—no manual for loops.

```cpp
std::vector<std::string> lines = {
    "Hello World", "", "hello world", "Goodbye", "GOODBYE", "", "Alice"
};
```

## Summary

In this chapter, we walked through the most commonly used algorithms from `<algorithm>` and `<numeric>`. For sorting, use `std::sort`, or `std::stable_sort` when stability is needed. Searching follows two paths: linear search with `std::find` / `std::find_if` for unsorted data, and binary search with `std::binary_search` / `std::lower_bound` for sorted data. For modifying sequences, rely on `std::copy`, `std::transform`, and `std::replace`. For deleting elements, use the remove-erase idiom. For aggregation, there are `std::accumulate`, `std::count` / `std::count_if`, and `std::min_element` / `std::max_element`.

The core philosophy running through all these algorithms is: don't write loops to express "what to do"—instead, use the algorithm's name to declare your intent directly. Combined with lambda expressions, we can flexibly customize comparison rules, filter conditions, and transformation logic while keeping code readable.

In the next chapter, we will continue our deep dive into the STL, looking at classic patterns for combining containers and algorithms.

---

> **References**
>
> - [cppreference: \<algorithm\>](https://en.cppreference.com/w/cpp/algorithm)
> - [cppreference: \<numeric\>](https://en.cppreference.com/w/cpp/header/numeric)
> - [cppreference: Lambda expressions](https://en.cppreference.com/w/cpp/language/lambda)
