---
title: "Introduction to the Algorithm Library"
description: "Get started with commonly used algorithms from <algorithm>, combined with lambda expressions for flexible data processing"
chapter: 11
order: 3
difficulty: beginner
reading_time_minutes: 15
platform: host
prerequisites:
  - "Associative Containers Quick Start"
tags:
  - cpp-modern
  - host
  - beginner
  - getting-started
  - fundamentals
cpp_standard: [11, 14, 17, 20]
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

STL algorithms often need a "condition" or "operation" as a parameter—for example, "what rule to sort by" or "which elements to find." Before C++11, this role was filled by function pointers or function objects, which were verbose and unintuitive. Lambda expressions changed the game entirely.

The full syntax of a lambda is `[capture](parameters) -> return_type { body }`, where the return type can be omitted (the compiler deduces it automatically), so the most common form is `[capture](params) { body }`. The `capture` in the square brackets determines how the lambda accesses external variables—this is the most error-prone part.

`[=]` captures all used external variables by value—modifying them does not affect the outside. `[&]` captures all used external variables by reference—operations directly affect the external variables themselves. `[x, &y]` is a mixed capture—`x` is captured by value, `y` by reference. In practice, the recommended approach is to explicitly list the variables you want to capture rather than using `[=]` or `[&]` indiscriminately. This makes the code's intent clearer and reduces the risk of accidentally modifying external state.

```cpp
std::vector<int> data = {5, 3, 1, 4, 2};
int threshold = 3;

// capture threshold by value
auto is_above = [threshold](int x) { return x > threshold; };
int count = std::count_if(data.begin(), data.end(), is_above);
// count == 2

// capture by reference, accumulate into external variable
int sum = 0;
std::for_each(data.begin(), data.end(), [&sum](int x) { sum += x; });
// sum == 15
```

> **Pitfall Alert**: When a lambda captures a local variable by reference, and the lambda's lifetime exceeds that of the local variable, a dangling reference occurs—the reference points to memory that has already been freed. This situation is especially common in asynchronous callbacks and scenarios where lambdas are stored. If your lambda needs to be stored or passed to another thread, prefer capturing by value or explicitly listing variables to capture by value.

## Sorting — std::sort and std::stable_sort

Sorting is probably the most frequently used operation in the algorithm library. `std::sort` takes two iterators (or a container directly since C++20) and sorts in ascending order by default. Under the hood, it uses Introsort—a hybrid of quicksort, heapsort, and insertion sort—with both average and worst-case time complexity of O(n log n):

```cpp
std::vector<int> v = {5, 2, 8, 1, 9, 3};

// default ascending order
std::sort(v.begin(), v.end());
// v: {1, 2, 3, 5, 8, 9}

// descending order—pass a third parameter, a comparison lambda
std::sort(v.begin(), v.end(), [](int a, int b) { return a > b; });
// v: {9, 8, 5, 3, 2, 1}
```

The third parameter is a lambda—it takes two elements and returns `true` if the first should come before the second. This is the standard pattern for "custom sort rules," and you will see it repeatedly.

`std::stable_sort` differs from `sort` in "stability"—when two elements compare equal, `stable_sort` guarantees they maintain their original relative order. For example, if you first sort by score and then by class, the second sort preserves the score ordering within each class. The tradeoff is slightly higher time and space overhead, but for scenarios that require sort stability, it is irreplaceable.

> **Pitfall Alert**: The comparison function passed to `sort` must satisfy "strict weak ordering." Simply put: `comp(a, a)` must return `false`, if `comp(a, b)` is `true` then `comp(b, a)` must be `false`, and transitivity must hold. If you write `<=` instead of `<`, some standard library implementations will cause undefined behavior—possibly an infinite loop, a crash, or just incorrect sort results. So always use `<` (ascending) or `>` (descending) in comparison functions, never `<=` or `>=`.

## Searching — The std::find Family and Binary Search

### Linear Search

`std::find` performs a linear search for the first element equal to a specified value within a range, returning an iterator to it; if not found, it returns `end()`. `std::find_if` is similar, but the condition is determined by a lambda:

```cpp
std::vector<std::string> names = {"Alice", "Bob", "Charlie", "David"};

// find: search for element equal to specified value
auto it1 = std::find(names.begin(), names.end(), "Charlie");
// it1 points to "Charlie"

// find_if: find first element satisfying a condition
auto it2 = std::find_if(names.begin(), names.end(),
    [](const std::string& s) { return s.size() > 4; });
// it2 points to "Alice"
```

Linear search has O(n) time complexity and works regardless of whether the data is sorted.

### Binary Search

If your data is already sorted, binary search is far more efficient—O(log n). `std::binary_search` returns a `bool` telling you whether a value exists, but not where it is. If you need to know the exact position, use `std::lower_bound`, which returns an iterator to the first element that is greater than or equal to the target value:

```cpp
std::vector<int> v = {1, 3, 5, 7, 9, 11};

bool found = std::binary_search(v.begin(), v.end(), 7);  // true
auto it = std::lower_bound(v.begin(), v.end(), 6);
// *it == 7, i.e., the first element >= 6
```

Calling `lower_bound` or `binary_search` on unsorted data will not produce an error, but the result is undefined—it is one of those "compiles fine, does not crash, but results are unreliable" bugs that are particularly painful to debug.

## Modifying — Copy, Transform, Replace, Remove

`std::copy` copies elements from one range to a destination. `std::transform` is more powerful—it applies a transformation function to each element while copying. `std::replace` replaces all elements equal to a given value with another value within a range:

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

Here we see a new face: `std::back_inserter`—it is an insert iterator where assigning to it is equivalent to calling the container's `push_back`. This way, `copy` and `transform` do not require the destination container to be pre-allocated.

### Revisiting Remove-Erase

In the previous chapter on `vector`, we used the remove-erase idiom. Now let us understand the principle in more depth. `std::remove` moves all elements not equal to the target value to the front, then returns an iterator pointing to the "new logical end"—this process does not change the container's size or call any destructors; it purely moves elements within known memory. Then you use the container's `erase` to truly remove the elements between the new end and the old end. Only after these two steps is the job complete:

```cpp
std::vector<int> v = {1, 2, 3, 2, 4, 2, 5};

auto new_end = std::remove(v.begin(), v.end(), 2);
// v's contents might be: {1, 3, 4, 5, ?, ?, ?}
//                          ^new_end         ^v.end()

v.erase(new_end, v.end());
// v: {1, 3, 4, 5}
```

`std::remove_if` follows the same pattern, but the condition is determined by a lambda. Starting from C++20, `std::erase(v, value)` and `std::erase_if(v, pred)` do it in one step. If your compiler supports C++20, just use the new syntax.

## Aggregation — Accumulate, Count, Min/Max

The last group of commonly used algorithms "reduces a collection of data to a single value." `std::accumulate` (requires the `<numeric>` header) sequentially accumulates elements in a range, with an initial value you specify—it can also accept a custom binary operation to compute products, concatenate strings, and so on. `std::count` / `std::count_if` count elements equal to a specified value or satisfying a condition. `std::min_element` / `std::max_element` return iterators to the minimum and maximum elements, respectively:

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

Note that the type of `accumulate`'s initial value determines the return type of the entire computation. Passing `0` gives `int`, `0.0` gives `double`, and `0 LL` gives `long long`. If your vector contains large integers and you pass `0` as the initial value, there is an overflow risk—this is a classic pitfall.

## Hands-On — Student Grade Processing

Now let us combine all the algorithms and lambda expressions from this chapter into a hands-on program. The scenario is straightforward: process a batch of student grade data with sorting, finding top students, calculating averages, and filtering failing grades.

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

    // --- 2. Find the top-scoring student ---
    auto top = std::max_element(students.begin(), students.end(),
        [](const Student& a, const Student& b) { return a.score < b.score; });
    std::cout << "\nTop student: " << top->name
              << " (" << top->score << ")\n";

    // --- 3. Calculate average score ---
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

The entire program—from sorting to aggregation to filtering—does not contain a single hand-written for loop for data operations. That is the power of STL algorithms. The intent of each operation is immediately clear: `sort` for sorting, `max_element` for finding the maximum, `count_if` for conditional counting, `remove_if` + `erase` for conditional removal. Compared to hand-written loops, the intent is expressed far more clearly.

## Try It Yourself — Exercises

### Exercise 1: Multi-Field Sorting

Define a struct `Employee` with `name` (`std::string`), `department` (`std::string`), and `salary` (`int`). Create a vector of employees and sort them first by department name in lexicographic order, then within the same department by salary in descending order. Hint: in the lambda, compare departments first; if they are equal, compare salaries.

```cpp
struct Employee {
    std::string name;
    std::string department;
    int salary;
};
```

### Exercise 2: Text Processing Pipeline

Given a `std::vector<std::string>` representing several lines of text, use STL algorithms to implement a simple text processing pipeline: remove all empty lines (`remove_if`), convert each line to lowercase (`std::transform` on each character), then sort lexicographically and deduplicate (`std::unique` + `erase`). Complete each step with a separate algorithm call—do not write manual for loops.

```cpp
std::vector<std::string> lines = {
    "Hello World", "", "hello world", "Goodbye", "GOODBYE", "", "Alice"
};
```

## Summary

In this chapter, we covered the most commonly used algorithms from `<algorithm>` and `<numeric>`. For sorting, use `std::sort`, or `std::stable_sort` when stability is needed. Searching has two approaches: for unsorted data, use `std::find` / `std::find_if` for linear search; for sorted data, use `std::binary_search` / `std::lower_bound` for binary search. Modifying sequences relies on `std::copy`, `std::transform`, and `std::replace`, while removing elements uses the remove-erase idiom. For aggregation, we have `std::accumulate`, `std::count` / `std::count_if`, and `std::min_element` / `std::max_element`.

The core principle running through all these algorithms is: instead of hand-writing loops to express "what to do," use the algorithm's name to directly declare the intent. Combined with lambda expressions, we can flexibly customize comparison rules, filter conditions, and transformation logic while keeping the code readable.

In the next chapter, we will continue exploring the STL and look at classic patterns for combining containers with algorithms.

---

> **References**
>
> - [cppreference: \<algorithm\>](https://en.cppreference.com/w/cpp/algorithm)
> - [cppreference: \<numeric\>](https://en.cppreference.com/w/cpp/header/numeric)
> - [cppreference: Lambda expressions](https://en.cppreference.com/w/cpp/language/lambda)
