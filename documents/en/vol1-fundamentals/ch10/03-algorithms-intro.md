---
chapter: 10
cpp_standard:
- 11
- 14
- 17
- 20
description: Get started with common algorithms from algorithm and combine them with lambda expressions for flexible data processing
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
title: A First Look at the Algorithms Library
---
# A First Look at the Algorithms Library

In the previous two chapters, we worked through the basic operations of `vector` and the associative containers. Now for the question: when you need to sort, search, filter, or count a collection of data, is your first instinct to write a `for` loop?

For many people, writing a loop by hand is indeed the natural reaction. Yet the C++ standard library's `<algorithm>` header contains more than a hundred general-purpose algorithms that have been optimized and tested again and again. Replacing hand-written loops with STL algorithms makes code shorter, reduces bugs, and communicates intent more clearly; in many cases, it also performs better. They are battle-tested, after all.

In this chapter, we will start from practical needs and get hands-on experience with the most commonly used algorithms. Along the way, we will frequently use lambda expressions—the perfect partner for STL algorithms—so let us first take a little time to understand them.

## Meeting our partner the lambda expression

STL algorithms often need a “condition” or an “operation” as an argument, such as “which ordering rule to use” or “which elements to find.” Before C++11, function pointers or function objects filled that role, but they were verbose and unintuitive to write. Lambda expressions changed the situation completely.

The full lambda syntax is `[capture](parameters) -> return_type { body }`. The return type can be omitted and inferred by the compiler, so the form we see most often is `[capture](params) { body }`. The `capture` inside the square brackets determines how the lambda accesses outside variables, and it is the easiest part to get wrong.

`[=]` captures copies of every used outside variable by value, so modifying those copies does not affect the originals. `[&]` captures by reference, so operations affect the outside variables themselves. `[x, &y]` is a mixed capture: `x` is copied by value, while `y` is passed by reference. In real development, the most strongly recommended approach is to list captured variables explicitly instead of reaching for `[=]` or `[&]` across the board. That makes the code's intent clearer and reduces the chance of modifying outside state by accident.

```cpp
std::vector<int> data = {5, 3, 1, 4, 2};
int threshold = 3;

// Capture threshold by value
auto is_above = [threshold](int x) { return x > threshold; };
int count = std::count_if(data.begin(), data.end(), is_above);
// count == 2

// Capture by reference and accumulate into an outside variable
int sum = 0;
std::for_each(data.begin(), data.end(), [&sum](int x) { sum += x; });
// sum == 15
```

When a lambda captures a local variable by reference and outlives that variable, the result is a dangling reference pointing to memory that has already been released. This is especially common with asynchronous callbacks and stored lambdas. If our lambda needs to be stored or passed to another thread, we should prefer value capture or explicitly list the variables that need to be captured by value.

## Putting things in order with std::sort and std::stable_sort

Sorting is probably the most frequently used operation in the algorithms library. `std::sort` accepts two iterators and sorts in ascending order by default. Want to pass a whole container directly? That is what `std::ranges::sort` is for, and it is available starting in C++20 as `std::ranges::sort(v)`. Under the hood is Introsort, which combines the strengths of quicksort, heapsort, and insertion sort; both its average and worst-case time complexity are O(n log n):

```cpp
std::vector<int> v = {5, 2, 8, 1, 9, 3};

// Ascending order by default
std::sort(v.begin(), v.end());
// v: {1, 2, 3, 5, 8, 9}

// Descending order—pass a comparison lambda as the third argument
std::sort(v.begin(), v.end(), [](int a, int b) { return a > b; });
// v: {9, 8, 5, 3, 2, 1}
```

The third argument is a lambda. It accepts two elements and returns `true` when the first should appear before the second. This is the standard way to write a custom ordering rule, and you will see this pattern repeatedly.

The difference between `std::stable_sort` and `sort` is stability: when two elements compare as equivalent, `stable_sort` guarantees that they retain their original relative order. For example, suppose we sort students by score first and then by class. During the second sort, students in the same class remain ordered from highest to lowest score. `stable_sort` costs a little more in time and space, but it is irreplaceable when stable ordering matters.

The comparison function we pass to `sort` must establish a strict weak ordering. Put simply, `comp(a, a)` must return `false`; if `comp(a, b)` is `true`, then `comp(b, a)` must be `false`; and the relation must also be transitive. If we write `<=` instead of `<`, some standard library implementations may exhibit undefined behavior: the program might enter an infinite loop, crash, or merely produce the wrong order. Always use `<` for ascending order or `>` for descending order, never `<=` or `>=`.

## Finding things with the std::find family and binary search

### Linear search

`std::find` linearly searches a range for the first element equal to a specified value and returns an iterator to it. If no match exists, it returns `end()`. `std::find_if` is similar, but a lambda supplies the condition:

```cpp
std::vector<std::string> names = {"Alice", "Bob", "Charlie", "David"};

// find: locate an element equal to a specified value
auto it1 = std::find(names.begin(), names.end(), "Charlie");
// it1 points to "Charlie"

// find_if: locate the first element satisfying a condition
auto it2 = std::find_if(names.begin(), names.end(),
    [](const std::string& s) { return s.size() > 4; });
// it2 points to "Alice"
```

Linear search has O(n) time complexity and works whether or not the data is sorted.

### Binary search

If your data is already sorted, binary search is far more efficient at O(log n). `std::binary_search` returns a `bool` telling you whether a value exists, but not where it is. If you need the exact position, use `std::lower_bound`, which returns an iterator to the first element greater than or equal to the target value:

```cpp
std::vector<int> v = {1, 3, 5, 7, 9, 11};

bool found = std::binary_search(v.begin(), v.end(), 7);  // true
auto it = std::lower_bound(v.begin(), v.end(), 6);
// *it == 7, the first element >= 6
```

Calling `lower_bound` or `binary_search` on unsorted data produces no error, but the behavior is undefined. It is the sort of bug that compiles, does not crash at runtime, and still gives us a result we cannot trust—especially painful to debug.

## Changing things with copying transforming replacing and removing

`std::copy` copies the elements in a range to a destination. `std::transform` is more powerful: it applies a transformation function to each element while copying. `std::replace` replaces every element in a range equal to one value with another:

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

Here we meet a new face, `std::back_inserter`. It is an insertion iterator, and assigning to it is equivalent to calling the container's `push_back`. That means `copy` and `transform` do not require the destination container to allocate space in advance.

### Revisiting remove-erase

In the previous chapter on `vector`, we used the remove-erase idiom. Now let us explain the mechanism more thoroughly. `std::remove` moves every element not equal to the target value toward the front of the range, then returns an iterator to the “new logical end.” This process does not change the container's size or call any destructors; it only moves elements around within existing storage. We then use the container's `erase` member function to truly remove the elements from the new end to the old end. Only after both steps is the job complete:

```cpp
std::vector<int> v = {1, 2, 3, 2, 4, 2, 5};

auto new_end = std::remove(v.begin(), v.end(), 2);
// v's contents may be: {1, 3, 4, 5, ?, ?, ?}
//                       ^new_end         ^v.end()

v.erase(new_end, v.end());
// v: {1, 3, 4, 5}
```

`std::remove_if` follows the same pattern, except that a lambda supplies the condition. Starting in C++20, `std::erase(v, value)` and `std::erase_if(v, pred)` perform the entire operation in one step. If your compiler supports C++20, go ahead and use the new forms.

## Crunching the numbers with accumulation counting and extrema

The final group of common algorithms reduces a collection of data to a single value. `std::accumulate`, from the `<numeric>` header, accumulates the elements in a range one by one from an initial value that you provide. It can also accept a custom binary operation for jobs such as calculating a product or concatenating strings. `std::count` and `std::count_if` count elements equal to a specified value or satisfying a condition. `std::min_element` and `std::max_element` return iterators to the smallest and largest elements, respectively:

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

Notice that the type of `accumulate`'s initial value determines the return type of the entire calculation. Passing `0` gives us `int`, `0.0` gives us `double`, and `0LL` gives us `long long`. If our vector stores large integers, passing `0` as the initial value risks overflow—a classic trap.

## Putting it all together with student grade processing

Now let us fold all the algorithms and lambda expressions from this chapter into one practical program. The scenario is simple: process a set of student grades by sorting them, finding high-performing students, calculating the average, and filtering out failing grades.

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

    // --- 1. Sort by score from highest to lowest ---
    std::sort(students.begin(), students.end(),
        [](const Student& a, const Student& b) { return a.score > b.score; });

    std::cout << "=== Ranking (high to low) ===\n";
    for (const auto& s : students) { print_student(s); }

    // --- 2. Find the student with the highest score ---
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

The complete code is available below. Click “Try It Yourself” to run it directly—no terminal required:

<OnlineCompilerDemo
  title="Jump In: Student Grade Processing algo_demo.cpp"
  source-path="code/examples/vol1/25_algorithms_demo.cpp"
  description="Run student grade processing online without a single hand-written data-processing loop. Try changing > to < in the sorting comparator and watch the entire ranking reverse."
  run-options="-O2 -std=c++17"
  allow-run
/>

From sorting through counting to filtering, the entire program never uses a hand-written `for` loop for data processing. That is the power of STL algorithms. The intent of every operation is visible at a glance: `sort` sorts, `max_element` finds the maximum, `count_if` counts by condition, and `remove_if` plus `erase` removes by condition. Compared with hand-written loops, the code communicates its intent much more clearly.

## Try it yourself with exercises

### Exercise 1 Sorting by multiple fields

Define an `Employee` structure containing `name` (`std::string`), `department` (`std::string`), and `salary` (`int`). Create a vector containing several employees, then sort it first by department name in lexicographical order and, within the same department, by salary in descending order. Hint: compare departments first in the lambda, then compare salaries when the departments match.

```cpp
struct Employee {
    std::string name;
    std::string department;
    int salary;
};
```

### Exercise 2 Text-processing pipeline

Given a `std::vector<std::string>` representing lines of text, use STL algorithms to build a simple text-processing pipeline: remove every empty line with `remove_if`, convert every line to lowercase by applying `std::transform` character by character, then sort lexicographically and remove duplicates with `std::unique` plus `erase`. Complete each step with a separate algorithm call and do not write a manual `for` loop.

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

translation:
  source: documents/vol1-fundamentals/ch10/03-algorithms-intro.md
  source_hash: ab5313b0f6f5d9fef26d85d88375db6310c9cc969170ce5b9d3c7074ec613b1f
  translated_at: '2026-09-27T06:59:37+00:00'
  engine: anthropic
  token_count: 1709
---
<!-- note: Introsort is a common implementation technique for std::sort, not a strategy mandated by the C++ standard; the translation preserves the source wording. -->
