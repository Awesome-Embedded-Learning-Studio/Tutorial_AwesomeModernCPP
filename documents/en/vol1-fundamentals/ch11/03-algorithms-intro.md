---
chapter: 11
cpp_standard:
- 11
- 14
- 17
- 20
description: Get up to speed quickly with the most-used algorithms in <algorithm>, pairing them with lambda expressions for flexible data processing
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
title: A First Look at the Algorithms Library
translation:
  source: documents/vol1-fundamentals/ch11/03-algorithms-intro.md
  source_hash: b7addbde67a2b7e8e751d5321af93256baeab2323fe4152a8e14d9a77143554b
  translated_at: '2026-09-26T11:57:34+00:00'
  engine: anthropic
  token_count: 3000
---

# A First Look at the Algorithms Library: Before Writing a for Loop, Think of the Standard Library

In the previous two chapters, we walked through the basic operations of `vector` and the associative containers. Now here comes the question: when you need to sort, search, filter, or run statistics over a pile of data, is your first instinct to write a for loop?

For many people, that instinct really is to hand-write the loop. But look at the C++ standard library's `<algorithm>` header: it holds over a hundred general-purpose algorithms that have been optimized and tested over and over. Replacing hand-written loops with STL algorithms gives you shorter code, fewer bugs, and clearer intent—and in many cases better performance too. (They are battle-tested, after all.)

In this chapter we'll start from practical needs and get hands-on with the most frequently used batch of algorithms, all of them. Along the way we'll lean heavily on lambda expressions—they are the best partner for STL algorithms—so we'll first spend a little time getting them straight.

## First, Meet Our Partner — the lambda Expression

As we've seen, STL algorithms often need a "condition to test" or a "way to operate" as a parameter—things like "what rule to sort by" or "which elements matching some condition should be picked out". Before C++11, this role was played by function pointers or function objects, which were verbose to write and not intuitive. lambda expressions changed that situation completely.

The full syntax of a lambda is `[capture](parameters) -> return_type { body }`, where the return type can be omitted (the compiler deduces it automatically), so the most common form is just `[capture](params) { body }`. The `capture` inside the square brackets determines how the lambda accesses outside variables—and this is the easiest place to go wrong.

As we see, `[=]` means capture by value: every outside variable the lambda uses gets copied in, and modifying the copies doesn't affect the outside; `[&]` means capture by reference: what you operate on is the outside variable itself; `[x, &y]` is a mixed capture, where `x` is copied by value and `y` is passed by reference. In real-world development, the most recommended practice is to explicitly list the variables you want to capture, instead of grabbing everything all at once with `[=]` or `[&]`—the code's intent is clearer, and you're less likely to accidentally modify outside state.

```cpp
std::vector<int> data = {5, 3, 1, 4, 2};
int threshold = 3;

// Capture threshold by value
auto is_above = [threshold](int x) { return x > threshold; };
int count = std::count_if(data.begin(), data.end(), is_above);
// count == 2

// Capture by reference, accumulate into an outside variable
int sum = 0;
std::for_each(data.begin(), data.end(), [&sum](int x) { sum += x; });
// sum == 15
```

When a lambda captures a local variable by reference and the lambda's lifetime outlives that local variable, you get a dangling reference: the memory the reference points to has already been freed. This is especially common in asynchronous callbacks and scenarios where lambdas are stored. If our lambda needs to be stored or passed to another thread, prefer capturing by value, or explicitly list the variables to capture by value.

## Let's Sort — std::sort and std::stable_sort

Sorting is probably the most frequently used operation in the algorithms library. `std::sort` takes two iterators and sorts in ascending order by default. Want to just pass the whole container? That's `std::ranges::sort`, available since C++20 (`std::ranges::sort(v)`). Under the hood it's Introsort, which combines the strengths of quicksort, heapsort, and insertion sort, with both average and worst-case time complexity of O(n log n):

```cpp
std::vector<int> v = {5, 2, 8, 1, 9, 3};

// Ascending by default
std::sort(v.begin(), v.end());
// v: {1, 2, 3, 5, 8, 9}

// Descending — pass a third argument, a comparison lambda
std::sort(v.begin(), v.end(), [](int a, int b) { return a > b; });
// v: {9, 8, 5, 3, 2, 1}
```

That third argument is just a lambda: it takes two elements and returns `true` to say the first argument should be placed before the second. This is the standard way to write a "custom sorting rule", and you'll see this pattern again and again later on.

The difference between `std::stable_sort` and `sort` lies in "stability"—when two elements compare equal, `stable_sort` guarantees they keep their original relative order. For example, say we first sort by grade and then by class: after the second sort, the students within the same class still appear in order from highest to lowest grade. The price of `stable_sort` is slightly larger time and space overhead, but for scenarios that need stable sorting it is irreplaceable.

The comparison function we pass to `sort` must satisfy "strict weak ordering". Put simply: `comp(a, a)` must return `false`; if `comp(a, b)` is `true`, then `comp(b, a)` must be `false`; and transitivity must hold as well. If we write `<=` instead of `<`, on some standard library implementations this leads to undefined behavior—it might loop forever, it might crash, or it might just produce wrong sort results. So the comparison function should always use `<` (ascending) or `>` (descending), never `<=` or `>=`.

## Finding Things — The std::find Family and Binary Search

### Linear Search

Let's look at `std::find`: it scans a range linearly for the first element equal to a given value and returns an iterator to it; if no match is found, it returns `end()`. `std::find_if` is similar, except that the condition is decided by a lambda:

```cpp
std::vector<std::string> names = {"Alice", "Bob", "Charlie", "David"};

// find: look for the element equal to the given value
auto it1 = std::find(names.begin(), names.end(), "Charlie");
// it1 points to "Charlie"

// find_if: look for the first element satisfying the condition
auto it2 = std::find_if(names.begin(), names.end(),
    [](const std::string& s) { return s.size() > 4; });
// it2 points to "Alice"
```

Note that linear search runs in O(n) time, and it works whether or not the data is sorted.

### Binary Search

If the data at hand is already sorted, binary search is far more efficient — O(log n). `std::binary_search` returns a `bool` that tells you whether the value exists, but not where it is. If you need to know the exact position, use `std::lower_bound`, which returns an iterator to the first element greater than or equal to the target:

```cpp
std::vector<int> v = {1, 3, 5, 7, 9, 11};

bool found = std::binary_search(v.begin(), v.end(), 7);  // true
auto it = std::lower_bound(v.begin(), v.end(), 6);
// *it == 7, i.e. the first element >= 6
```

If we call `lower_bound` or `binary_search` on unsorted data, nothing complains, but the results are undefined — the kind of bug that "compiles fine, doesn't crash at runtime, but gives untrustworthy results," which makes it especially painful to debug.

## Making Changes — Copy, Transform, Replace, Delete

Let's take a look at `std::copy`, which copies the elements of a range to a destination. `std::transform` is more powerful: while copying, it applies a transformation function to each element. `std::replace` replaces every element in the range that equals a certain value with another value:

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

// replace: replace every 3 with 99
std::vector<int> v = {1, 3, 5, 3, 7};
std::replace(v.begin(), v.end(), 3, 99);
// v: {1, 99, 5, 99, 7}
```

We have a new face here: `std::back_inserter`. It is an insert iterator, and assigning a value
through it calls the container's `push_back`. That means algorithms such as `copy` and
`transform` no longer require us to size the destination container in advance.

### Remove-Erase Revisited

We used the remove-erase idiom in the previous chapter when we covered `vector`. Now let's dig
further into how it works. `std::remove` moves every element in the range that is not equal to
the target value toward the front, then returns an iterator to the "new logical end." It does
not change the container's size or erase any elements; it only rearranges elements within the
existing range. We then call the container's `erase` to truly remove everything between the new
end and the old end. The operation is not complete until we have performed both steps:

```cpp
std::vector<int> v = {1, 2, 3, 2, 4, 2, 5};

auto new_end = std::remove(v.begin(), v.end(), 2);
// v might now contain: {1, 3, 4, 5, ?, ?, ?}
//                       ^new_end         ^v.end()

v.erase(new_end, v.end());
// v: {1, 3, 4, 5}
```

`std::remove_if` follows the same pattern, except that a predicate such as a lambda decides
which elements to remove. Since C++20, `std::erase(v, value)` and `std::erase_if(v, pred)` wrap
the whole operation into a single call. If your compiler supports C++20, go ahead and use these
newer forms.

## Let's Do the Math — Accumulate, Count, Extremes

The last group of common algorithms is all about "boiling a pile of data down to a single value." Let's look at `std::accumulate` (which needs the `<numeric>` header): it sums the elements of a range one by one, with an initial value that you specify, and it can also take a custom binary operation to compute a product, concatenate strings, and so on. `std::count` / `std::count_if` count how many elements equal a given value or satisfy a condition. `std::min_element` / `std::max_element` return iterators pointing to the smallest and the largest element, respectively:

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

Note that the type of `accumulate`'s initial value determines the return type of the entire computation. Pass `0` and you get an `int`; pass `0.0` and you get a `double`; pass `0LL` and you get a `long long`. If our vector stores big integers, passing `0` as the initial value runs the risk of overflow—a classic pitfall.

## Time to Log On — Putting It All Together: Student Score Processing

Now let's knead every algorithm and lambda expression covered in this chapter into one hands-on program. The scenario is simple: process a batch of student score data and carry out a few operations—sorting, finding the top student, computing the average score, and filtering out failing students.

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

    // --- 3. Compute the average score ---
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

Notice how the whole program goes from sorting to counting to filtering without a single hand-written for loop doing the data manipulation—that's the power of STL algorithms. The intent of each operation is visible at a glance: `sort` means sort, `max_element` means find the maximum, `count_if` means count by condition, `remove_if` + `erase` means delete by condition. Compared with hand-written loops, the intent comes across far more clearly.

## Try It Yourself — Exercises

### Exercise 1: Multi-field sorting

Define a struct `Employee` with `name` (`std::string`), `department` (`std::string`), and `salary` (`int`). Create a vector holding several employees, and sort them first by department name in dictionary order, then within the same department by salary in descending order. Hint: in the lambda, compare the departments first, and compare salaries only when the departments are the same.

```cpp
struct Employee {
    std::string name;
    std::string department;
    int salary;
};
```

### Exercise 2: A text-processing pipeline

Given a `std::vector<std::string>` representing several lines of text, use STL algorithms to build a simple text-processing pipeline: drop all the empty lines (`remove_if`), convert every line to all lowercase (`std::transform`, processing character by character), then sort in dictionary order and remove duplicates (`std::unique` + `erase`). Each step should be done with a single, standalone algorithm call—no hand-written for loops.

```cpp
std::vector<std::string> lines = {
    "Hello World", "", "hello world", "Goodbye", "GOODBYE", "", "Alice"
};
```

---

> **References**
>
> - [cppreference: \<algorithm\>](https://en.cppreference.com/w/cpp/algorithm)
> - [cppreference: \<numeric\>](https://en.cppreference.com/w/cpp/header/numeric)
> - [cppreference: Lambda expressions](https://en.cppreference.com/w/cpp/language/lambda)
