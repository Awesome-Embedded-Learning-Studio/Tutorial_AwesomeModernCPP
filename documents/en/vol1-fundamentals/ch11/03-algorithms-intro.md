---
chapter: 11
cpp_standard:
- 11
- 14
- 17
- 20
description: Get started with common algorithms in the `<algorithm>` library, and
  implement flexible data processing using lambda expressions.
difficulty: beginner
order: 3
platform: host
prerequisites:
- 关联容器快速上手
reading_time_minutes: 12
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: First Look at the Algorithms Library
translation:
  source: documents/vol1-fundamentals/ch11/03-algorithms-intro.md
  source_hash: 43ec2447bcd2d7fe103638635b62a73e86937d0a724107a604e0cd79bbfe2bc6
  translated_at: '2026-06-16T04:18:56.069452+00:00'
  engine: anthropic
  token_count: 2516
---
# First Look at the Algorithms Library

In the previous two chapters, we covered the basic operations of `std::vector` and associative containers. Now, the question arises—when you need to sort, search, filter, or count a bunch of data, is your first instinct to write a `for` loop?

Honestly, many people's intuition is indeed to write loops by hand. However, the C++ Standard Library's `<algorithm>` header contains hundreds of general-purpose algorithms that have been repeatedly optimized and tested. Replacing hand-written loops with STL algorithms results in shorter code, fewer bugs, clearer intent, and often better performance. (After all, they have stood the test of time.)

In this chapter, starting from practical requirements, we will get hands-on experience with the most commonly used algorithms. We will frequently use lambda expressions—they are the best partners for STL algorithms—so we will spend some time upfront to understand them thoroughly.

> **Learning Objectives**
>
> After completing this chapter, you will be able to:
>
> - [ ] Understand the basic syntax and capture modes of lambda expressions
> - [ ] Use `std::sort`, `std::stable_sort` to sort data
> - [ ] Use `std::find`, `std::find_if`, `std::binary_search`, `std::lower_bound` to find elements
> - [ ] Use `std::copy`, `std::transform`, `std::replace`, `std::remove` to modify data
> - [ ] Use `std::accumulate`, `std::count`, `std::count_if`, `std::minmax_element` to perform statistics

## Meet Our Partner—Lambda Expressions

STL algorithms often require a "predicate" or "operation" as a parameter—such as "what rule to sort by" or "which elements to find." Before C++11, this role was filled by function pointers or function objects (functors), which were verbose and unintuitive. Lambda expressions have completely changed this landscape.

The complete syntax of a lambda is `[capture](parameters) -> return_type { body }`, where the return type can be omitted (the compiler deduces it automatically), so the most common form is `[capture](parameters) { body }`. The `capture` clause in square brackets determines how the lambda accesses external variables, which is the most error-prone part.

`[=]` means capturing all used external variables by value—modifying them inside the lambda does not affect the outside. `[&]` means capturing by reference—you are operating on the external variables themselves. `[a, &b]` is mixed capture—`a` is copied by value, `b` is passed by reference. In actual development, the recommended practice is to explicitly list the variables to be captured, rather than using `[=]` or `[&]` indiscriminately. This makes the code's intent clearer and avoids accidentally modifying external state.

```cpp
// Capture by value: a copy of 'x' is made
int x = 10;
auto foo = [x]() {
    // x++; // Error: cannot modify a copy-by-value variable unless mutable
    return x * 2;
};

// Capture by reference: operates on the external 'y'
int y = 20;
auto bar = [&y]() {
    y++;
};

// Mixed capture: a by value, b by reference
int a = 1, b = 2;
auto baz = [a, &b]() {
    // a = 10; // Error
    b = 20;  // OK
};
```

> **Warning**: When a lambda captures local variables by reference, if the lambda's lifetime exceeds that of the local variable, a dangling reference is created—the referenced memory has been freed. This is particularly common in asynchronous callbacks and scenarios where lambdas are stored. If your lambda needs to be stored or passed to another thread, prioritize capturing by value or explicitly listing variables to capture by value.

## Sorting—`std::sort` and `std::stable_sort`

Sorting is likely the most frequently used operation in the algorithms library. `std::sort` accepts two iterators and sorts in ascending order by default. To pass the whole container directly, use `std::ranges::sort` (C++20): `std::ranges::sort(v)`. Under the hood, it uses Introsort—combining the advantages of quicksort, heapsort, and insertion sort, with an average and worst-case time complexity of O(n log n):

```cpp
std::vector<int> v = {5, 2, 9, 1, 5, 6};

// Default: ascending
std::sort(v.begin(), v.end()); // {1, 2, 5, 5, 6, 9}

// Descending order using a lambda
std::sort(v.begin(), v.end(), [](int a, int b) {
    return a > b; // a comes before b if a is greater
});
```

The third parameter is a lambda—it takes two elements and returns `true` if the first argument should precede the second. This is the standard way to define "custom sorting rules," a pattern you will see repeatedly.

The difference between `std::sort` and `std::stable_sort` lies in "stability"—when two elements compare equally, `std::stable_sort` guarantees they maintain their original relative order. For example, if you first sort by grade, then by class, the second sort will keep students within the same class ordered by grade. `std::stable_sort` comes with slightly higher time and space overhead, but it is indispensable for scenarios requiring stable sorting.

> **Warning**: The comparison function passed to `std::sort` must satisfy "strict weak ordering." Simply put: `comp(a, b)` must return `false` if `comp(b, a)` is `true`, and if `comp(a, b)` is `true` and `comp(b, c)` is `true`, then `comp(a, c)` must be `true` (transitivity). If you write `<=` instead of `<`, it may lead to undefined behavior in some standard library implementations—infinite loops, crashes, or simply incorrect sorting results. Therefore, always use `<` (ascending) or `>` (descending) in comparison functions, never `<=` or `>=`.

## Finding Things—`std::find` Family and Binary Search

### Linear Search

`std::find` performs a linear search within a range for the first element equal to a specific value, returning an iterator to it; if not found, it returns the end iterator. `std::find_if` is similar, but the condition is determined by a lambda:

```cpp
std::vector<int> v = {1, 5, 3, 9, 2};

// Find the first element equal to 5
auto it1 = std::find(v.begin(), v.end(), 5);

// Find the first element greater than 4
auto it2 = std::find_if(v.begin(), v.end(), [](int x) {
    return x > 4;
});
```

Linear search has a time complexity of O(n) and works regardless of whether the data is sorted.

### Binary Search

If your data is already sorted, binary search is much more efficient—O(log n). `std::binary_search` returns a `bool`, telling you if the value exists, but not where it is. If you need the specific location, use `std::lower_bound`, which returns an iterator to the first element that is greater than or equal to the target value:

```cpp
std::vector<int> v = {1, 3, 3, 4, 7};

// Check existence
bool found = std::binary_search(v.begin(), v.end(), 3); // true

// Find position
auto it = std::lower_bound(v.begin(), v.end(), 3);
// it points to the first '3'
```

Calling `std::binary_search` or `std::lower_bound` on unsorted data won't cause a compile error, but the result is undefined—this falls into the category of bugs that "compile fine, don't crash, but give untrustworthy results," which are exceptionally painful to debug.

## Making Changes—Copy, Transform, Replace, Remove

`std::copy` copies elements from a source range to a destination. `std::transform` is more powerful—it applies a transformation function to each element while copying. `std::replace` replaces elements equal to a specific value with another value within a range:

```cpp
std::vector<int> src = {1, 2, 3, 4};
std::vector<int> dst;

// Copy
std::copy(src.begin(), src.end(), std::back_inserter(dst));

// Transform: multiply each element by 2
std::vector<int> transformed;
std::transform(src.begin(), src.end(), std::back_inserter(transformed),
               [](int x) { return x * 2; });

// Replace: replace all 2s with 20
std::replace(src.begin(), src.end(), 2, 20);
```

Here we see a new face: `std::back_inserter`—it is an insert iterator. Assigning to it is equivalent to calling the container's `push_back`. This way, `std::copy` and `std::transform` don't require the destination container to have pre-allocated space.

### Remove-Erase Revisited

In the previous chapter on `std::vector`, we used the remove-erase idiom. Now let's explain the principle more thoroughly. `std::remove` moves all elements *not* equal to the target value to the front and returns an iterator pointing to the "new logical end"—this process does not change the container's size, nor does it call destructors; it purely moves elements within existing memory. Afterward, you use the container's `erase` method to actually delete the elements from the new end to the old end. These two steps complete the operation:

```cpp
std::vector<int> v = {1, 2, 3, 2, 4};

// Step 1: Shift non-2 elements to the front
auto new_end = std::remove(v.begin(), v.end(), 2);
// v is now {1, 3, 4, ?, ?} (logical size 3, physical size 5)

// Step 2: Erase the "garbage" at the tail
v.erase(new_end, v.end());
// v is now {1, 3, 4}
```

`std::remove_if` follows the same pattern, but the condition is determined by a lambda. Starting with C++20, `std::erase` and `std::erase_if` combine these steps into one. If your compiler supports C++20, just use the new syntax.

## Calculating—Accumulate, Count, Extremes

The last set of common algorithms performs "reducing a bunch of data into a single value." `std::accumulate` (requires the `<numeric>` header) accumulates elements in a range sequentially, with an initial value specified by you—it can also accept a custom binary operation to calculate products, concatenate strings, etc. `std::count` / `std::count_if` count the number of elements equal to a value or satisfying a condition. `std::minmax_element` returns a pair of iterators pointing to the minimum and maximum elements:

```cpp
std::vector<int> v = {1, 2, 3, 4, 5};

// Sum: 1 + 2 + ... + 5 = 15
int sum = std::accumulate(v.begin(), v.end(), 0); // Init with 0

// Product: 1 * 2 * ... * 5 = 120
int product = std::accumulate(v.begin(), v.end(), 1, std::multiplies<int>());

// Count evens
int evens = std::count_if(v.begin(), v.end(), [](int x) { return x % 2 == 0; });

// Find min and max
auto [min_it, max_it] = std::minmax_element(v.begin(), v.end());
```

Note that the type of the initial value passed to `std::accumulate` determines the return type of the entire calculation. Passing `0` yields `int`, `0.0` yields `double`, and `0LL` yields `long long`. If your vector stores large integers, passing `0` risks overflow—this is a classic pitfall.

## Game On—Comprehensive Practice: Student Grade Processing

Now let's combine all the algorithms and lambda expressions from this chapter into a practical program. The scenario is simple: process a batch of student grade data to perform sorting, find top students, calculate average scores, and filter out failing grades.

```cpp
#include <algorithm>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

struct Student {
    std::string name;
    int score;
};

int main() {
    std::vector<Student> students = {
        {"Alice", 85}, {"Bob", 58}, {"Charlie", 92}, {"David", 45}, {"Eve", 78}};

    // 1. Sort by score descending
    std::sort(students.begin(), students.end(), [](const Student& a, const Student& b) {
        return a.score > b.score;
    });

    // 2. Find the first student with a score >= 90 (Top student)
    auto top_student = std::find_if(students.begin(), students.end(), [](const Student& s) {
        return s.score >= 90;
    });

    if (top_student != students.end()) {
        std::cout << "Top Student: " << top_student->name << " (" << top_student->score << ")\n";
    }

    // 3. Calculate average score
    int total_score = std::accumulate(students.begin(), students.end(), 0, [](int sum, const Student& s) {
        return sum + s.score;
    });
    double average = static_cast<double>(total_score) / students.size();
    std::cout << "Average Score: " << average << "\n";

    // 4. Remove failing students (score < 60)
    auto new_end = std::remove_if(students.begin(), students.end(), [](const Student& s) {
        return s.score < 60;
    });
    students.erase(new_end, students.end());

    // 5. Print remaining students
    std::cout << "Passing Students:\n";
    std::for_each(students.begin(), students.end(), [](const Student& s) {
        std::cout << s.name << ": " << s.score << "\n";
    });

    return 0;
}
```

Compile and run:

```bash
g++ -std=c++20 student_grades.cpp -o student_grades
./student_grades
```

Expected output:

```text
Top Student: Charlie (92)
Average Score: 71.6
Passing Students:
Charlie: 92
Alice: 85
Eve: 78
```

Throughout the entire program—from sorting to statistics to filtering—there is no hand-written `for` loop for data manipulation. This is the power of STL algorithms. The intent of each operation is clear at a glance: `std::sort` is sorting, `std::max_element` is finding the maximum, `std::count_if` is conditional counting, and `std::remove_if` + `erase` is conditional deletion. Compared to hand-written loops, the intent is expressed much more clearly.

## Your Turn—Exercises

### Exercise 1: Multi-field Sorting

Define a struct `Employee`, containing `name` (`std::string`), `department` (`std::string`), and `salary` (`int`). Create a `vector` containing several employees and implement sorting first by department name in lexicographical order, and within the same department, by salary in descending order. Hint: compare departments first in the lambda, then compare salaries if departments are equal.

```cpp
struct Employee {
    std::string name;
    std::string department;
    int salary;
};

// TODO: Implement sorting
```

### Exercise 2: Text Processing Pipeline

Given a `std::vector<std::string>` representing several lines of text, use STL algorithms to implement a simple text processing pipeline: remove all empty lines (`std::remove_if`), convert every line to lowercase (`std::transform` processing character by character), then sort lexicographically and remove duplicates (`std::sort` + `std::unique`). Complete each step with a separate algorithm call; do not write manual `for` loops.

```cpp
std::vector<std::string> lines = {
    "Hello World", "", "C++ Programming", "HELLO WORLD", "STL Algorithms"
};

// TODO: Implement pipeline
```

## Summary

In this chapter, we went through the most commonly used algorithms in `<algorithm>` and `<numeric>`. Use `std::sort` for sorting, and `std::stable_sort` when stability is required. Finding elements splits into two paths: for unsorted data, use `std::find` / `std::find_if` for linear search; for sorted data, use `std::binary_search` / `std::lower_bound` for binary search. Modifying sequences relies on `std::copy`, `std::transform`, `std::replace`, and deleting elements uses the remove-erase idiom. For statistics and reduction, we have `std::accumulate`, `std::count` / `std::count_if`, and `std::minmax_element`.

Running through all these algorithms is a core concept: don't write loops to express "what to do"; instead, declare intent directly using algorithm names. Combined with lambda expressions, we can flexibly customize comparison rules, filter conditions, and transformation logic while maintaining code readability.

In the next chapter, we will continue to dive deeper into the STL and explore more classic patterns of combining containers with algorithms.

---

> **References**
>
> - [cppreference: \<algorithm\>](https://en.cppreference.com/w/cpp/algorithm)
> - [cppreference: \<numeric\>](https://en.cppreference.com/w/cpp/header/numeric)
> - [cppreference: Lambda expressions](https://en.cppreference.com/w/cpp/language/lambda)
