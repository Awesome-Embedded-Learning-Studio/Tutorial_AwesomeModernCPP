---
title: "std::vector Quick Start"
description: "Master vector's CRUD operations and capacity management, and learn to use C++'s most common dynamic container"
chapter: 11
order: 1
difficulty: beginner
reading_time_minutes: 15
platform: host
prerequisites:
  - "Error Handling Approaches Compared"
tags:
  - cpp-modern
  - host
  - beginner
  - 入门
  - 基础
cpp_standard: [11, 14, 17, 20]
---

# std::vector Quick Start

In the previous chapters, we covered the core of the C++ language—type systems, control flow, functions, classes, and inheritance. Now we are entering a whole new territory: the Standard Template Library (STL). The STL provides a large collection of ready-made containers, algorithms, and iterators that save us from reinventing the wheel. Among all containers, `std::vector` is by far the most frequently used—a dynamic array that grows automatically, stores elements contiguously, and supports O(1) random access. Honestly, if you are not sure which container to use, just go with `vector`. Other containers only have an advantage in specific scenarios.

In this chapter, we will start from scratch and walk through `vector`'s construction, insertion, deletion, access, capacity management, and traversal. We will tie everything together with a hands-on task manager program at the end.

> **Learning Objectives**
>
> After completing this chapter, you will be able to:
>
> - [ ] Construct `std::vector` in multiple ways
> - [ ] Master insertion and deletion operations like `push_back`, `emplace_back`, `insert`, and `erase`
> - [ ] Understand the difference between `size` and `capacity`, and use `reserve` to optimize performance
> - [ ] Traverse a vector using range-for, index, and iterator approaches
> - [ ] Apply the remove-erase idiom to delete elements matching a condition

## Starting from Scratch — Constructing a vector

`std::vector` has several construction forms. Let us look at them one by one:

```cpp
#include <vector>
#include <string>

std::vector<int> v1;                    // empty vector
std::vector<int> v2(10);                // 10 elements, each is 0
std::vector<int> v3(10, 42);            // 10 elements, each is 42
std::vector<int> v4 = {1, 2, 3, 4, 5};  // initializer list
std::vector<int> v5(v4);                // copy construction
std::vector<int> v6(std::move(v5));     // move construction, takes over resources
```

One thing worth noting: `v2(10)` creates 10 elements, each initialized to `int()`, which is 0. This is not "reserving 10 slots with no elements"—there are genuinely 10 elements inside. Reserving space and having actual elements are two different concepts. We will discuss this in depth when we cover `reserve`.

> **Pitfall Alert**: `vector<bool>` is a specialization of `vector` that compresses each `bool` into 1 bit to save space. This causes many behaviors of `vector<bool>` to differ from a regular `vector<T>`—for example, `operator[]` returns a proxy object instead of `bool&`. If you need a proper bool array, use `vector<char>` or `deque<bool>` instead.

## Adding Elements

The most common way to add elements to a `vector` is `push_back`, which appends an element to the end. Since C++11, we also have `emplace_back`, which is more efficient than `push_back`—the difference is that `push_back` takes an already-constructed object, while `emplace_back` takes constructor arguments and constructs the object in-place within the vector's memory, saving a move or copy:

```cpp
struct Task {
    std::string name;
    int priority;
    Task(std::string n, int p) : name(std::move(n)), priority(p) {}
};

std::vector<Task> tasks;
tasks.push_back(Task("Write code", 1));   // construct temp, then move
tasks.emplace_back("Test", 2);             // in-place construction, no temp needed
```

For simple types like `int` and `double`, the performance difference is negligible. But for classes containing `std::string` or other members that require dynamic memory allocation, `emplace_back` avoids an unnecessary construction and move. Make it a habit to prefer `emplace_back`.

If you need to insert an element at a specific position in the middle, use `insert`:

```cpp
std::vector<int> v = {10, 20, 30, 40};
v.insert(v.begin() + 1, 15);  // v: {10, 15, 20, 30, 40}
```

Note that `insert` in the middle requires shifting all subsequent elements, giving it O(n) time complexity. If you find yourself frequently inserting at the front or middle of a vector, consider switching to `std::deque` or `std::list`.

> **Pitfall Alert**: Any operation that may cause the vector to reallocate memory (including `push_back`, `emplace_back`, and `insert`) invalidates all previously saved iterators, pointers, and references. Consider this code:

```cpp
std::vector<int> v = {1, 2, 3};
int* p = &v[0];       // points to first element
v.push_back(4);       // may trigger reallocation!
// *p is now undefined behavior—p may point to freed memory
```

If you need to hold a pointer or reference to an element in a vector, either ensure no reallocation-triggering operations will follow, or use indices for indirect access instead.

## Accessing Elements

`vector` provides several ways to access elements. The most commonly used is `operator[]`, which accesses elements by index just like a C array, without bounds checking. If you want bounds checking (throwing `std::out_of_range` on out-of-bounds access), use `at`:

```cpp
std::vector<int> v = {10, 20, 30, 40, 50};
v[0] = 100;          // no bounds check
int y = v.at(10);    // throws std::out_of_range
```

In everyday development, `operator[]` is used more often. However, in scenarios where user input or external data is used as an index, `at` serves as a safety net.

There are also a few convenience access functions: `front()` returns a reference to the first element (equivalent to `v[0]`), `back()` returns a reference to the last element (equivalent to `v[v.size() - 1]`), and `data()` returns a pointer to the underlying array—since vector elements are stored contiguously, `v.data()` can be used directly as a C array, which is particularly handy when interfacing with C-style APIs.

> **Pitfall Alert**: Calling `front()`, `back()`, or `operator[]` on an empty vector is undefined behavior—no exception is thrown; it goes straight into UB territory. `at()` is the only method that performs bounds checking on an empty vector. So before calling `front()` or `back()`, either ensure the vector is not empty or check with `empty()` first.

## Removing Elements

The simplest way is `pop_back`, which removes the last element and returns `void`—it does not return the removed value. If you need that value, call `back()` before `pop_back`.

To remove an element from the middle, use `erase`, which takes an iterator or a range:

```cpp
std::vector<int> v = {10, 20, 30, 40, 50};
v.erase(v.begin() + 2);                // v: {10, 20, 40, 50}
v.erase(v.begin() + 1, v.begin() + 3); // v: {10, 50}
```

To clear all elements at once, use `clear()`—afterwards, `size` becomes 0 but `capacity` remains unchanged, meaning the memory is not freed; only the elements are destroyed. To also free the memory, combine it with `shrink_to_fit`.

### The Remove-Erase Idiom

Now the question arises: how do we remove all elements equal to a particular value from a vector? The answer is the remove-erase idiom, a classic C++ pattern:

```cpp
#include <algorithm>

std::vector<int> v = {1, 2, 3, 2, 4, 2, 5};
v.erase(std::remove(v.begin(), v.end(), 2), v.end());
// v: {1, 3, 4, 5}
```

`std::remove` does not actually delete elements—it moves all elements not equal to 2 to the front, then returns an iterator pointing to the "new logical end." The elements equal to 2 are squeezed to the back. Then `erase` truly removes the elements between the new end and the old end. The reason for this two-step approach is the STL design philosophy: "algorithms should not directly manipulate container interfaces." `std::remove` only knows about iterators; it does not know about vector's `erase` method.

Starting from C++20, we can do it in a single line with `std::erase`: `std::erase(v, 2);`. If you are using a C++20-compatible compiler, this new syntax is highly recommended.

## Understanding size and capacity

`vector` has two easily confused concepts: `size` is the number of elements currently stored, while `capacity` is the number of elements the allocated memory can hold. `capacity` is always greater than or equal to `size`. When continued `push_back` operations cause `size` to approach `capacity`, the vector automatically reallocates—allocating a larger block of memory, moving all elements over, and freeing the old memory. Most standard library implementations double the `capacity` on each growth, so you will see capacity increase in a sequence like: 1, 2, 4, 8, 16, 32 ... Each reallocation involves a full memory allocation and copying/moving of all elements.

If you know roughly how many elements you will need in advance, use `reserve` to pre-allocate enough space and avoid the overhead of multiple reallocations:

```cpp
std::vector<int> v;
v.reserve(1000);  // one-time allocation, capacity becomes 1000, size is still 0

for (int i = 0; i < 1000; ++i) {
    v.push_back(i);  // no reallocation triggered
}
```

`reserve` only affects `capacity`, not `size`. Conversely, if you want to release excess capacity, use `shrink_to_fit`—this is a non-binding request; the standard does not guarantee that memory will be freed, but mainstream implementations do so.

## Traversing a vector

There are three common ways to traverse a vector. The most recommended is the range-for loop (introduced in C++11), which is concise and safe:

```cpp
std::vector<int> v = {10, 20, 30, 40, 50};

// read-only traversal—make it a habit to use const auto& for read-only access
for (const auto& elem : v) {
    std::cout << elem << " ";
}

// when you need to modify elements, drop the const
for (auto& elem : v) {
    elem *= 2;
}
```

Note the use of `const auto&` instead of `auto`. For `int`, the difference is minor, but when traversing a `vector<std::string>`, `auto` triggers a copy while `const auto&` is just a reference. If you need the index, use a traditional loop: `for (std::size_t i = 0; i < v.size(); ++i)`. For finer control or when working with STL algorithms, use iterators: `for (auto it = v.begin(); it != v.end(); ++it)`. In everyday development, range-for covers 90% of traversal needs.

## Hands-On — Building a Task Manager with vector

Let us combine all the knowledge from this chapter into a hands-on program—a task manager that supports adding tasks, marking tasks as complete and removing them, listing all tasks, and viewing capacity information.

```cpp
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

struct Task {
    std::string description;
    bool done;
    Task(std::string desc) : description(std::move(desc)), done(false) {}
};

class TaskManager {
public:
    void add_task(const std::string& desc)
    {
        tasks_.emplace_back(desc);
        std::cout << "  Added: \"" << desc << "\"\n";
    }

    void complete_task(int index)
    {
        if (index < 0 || index >= static_cast<int>(tasks_.size())) {
            std::cout << "  Invalid index: " << index << "\n";
            return;
        }
        tasks_[index].done = true;
        std::cout << "  Completed: \"" << tasks_[index].description << "\"\n";
    }

    void remove_completed()
    {
        // remove-erase idiom: remove all tasks where done == true
        auto it = std::remove_if(tasks_.begin(), tasks_.end(),
            [](const Task& t) { return t.done; });
        int removed = static_cast<int>(tasks_.end() - it);
        tasks_.erase(it, tasks_.end());
        std::cout << "  Removed " << removed << " completed task(s)\n";
    }

    void list_all() const
    {
        if (tasks_.empty()) {
            std::cout << "  (no tasks)\n";
            return;
        }
        for (std::size_t i = 0; i < tasks_.size(); ++i) {
            std::cout << "  [" << i << "] "
                      << (tasks_[i].done ? "[x]" : "[ ]")
                      << " " << tasks_[i].description << "\n";
        }
    }

    void show_status() const
    {
        std::cout << "  size: " << tasks_.size()
                  << ", capacity: " << tasks_.capacity() << "\n";
    }

private:
    std::vector<Task> tasks_;
};

int main()
{
    TaskManager mgr;

    std::cout << "=== Adding tasks ===\n";
    mgr.add_task("Write vector tutorial");
    mgr.add_task("Review pull requests");
    mgr.add_task("Fix build warnings");
    mgr.add_task("Update documentation");

    std::cout << "\n=== All tasks ===\n";
    mgr.list_all();
    mgr.show_status();

    std::cout << "\n=== Completing tasks ===\n";
    mgr.complete_task(0);
    mgr.complete_task(2);

    std::cout << "\n=== Removing completed ===\n";
    mgr.remove_completed();

    std::cout << "\n=== Remaining tasks ===\n";
    mgr.list_all();
    mgr.show_status();

    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17 -Wall -Wextra -o task_manager task_manager.cpp && ./task_manager
```

Expected output:

```text
=== Adding tasks ===
  Added: "Write vector tutorial"
  Added: "Review pull requests"
  Added: "Fix build warnings"
  Added: "Update documentation"

=== All tasks ===
  [0] [ ] Write vector tutorial
  [1] [ ] Review pull requests
  [2] [ ] Fix build warnings
  [3] [ ] Update documentation
  size: 4, capacity: 4

=== Completing tasks ===
  Completed: "Write vector tutorial"
  Completed: "Fix build warnings"

=== Removing completed ===
  Removed 2 completed task(s)

=== Remaining tasks ===
  [0] [ ] Review pull requests
  [1] [ ] Update documentation
  size: 2, capacity: 4
```

Notice that `capacity` is still 4 at the end—`erase` does not release memory. This small detail is often overlooked in real-world development.

> **Pitfall Alert**: Calling `erase` on elements directly within a range-for loop causes undefined behavior because `erase` invalidates iterators. If you need to remove elements during traversal, either use an index loop iterating backwards, or use an iterator loop combined with the return value of `erase`. However, in most cases, marking first and then doing a unified remove-erase is a cleaner approach, as shown in `remove_completed` above.

## Try It Yourself — Exercises

### Exercise 1: Frequency Counter

Given a vector of integers, count how many times each value appears. Hint: you can sort then traverse, or use a nested loop (a simple implementation is fine; no need for `map`).

```cpp
std::vector<int> data = {3, 1, 4, 1, 5, 9, 2, 6, 5, 3, 5};
// Expected output: 1 appears 2 times, 2 appears 1 times, ...
```

### Exercise 2: Deduplication

Write a function that takes a sorted vector and returns a new vector with duplicates removed. Do not use `std::unique`; implement it manually.

```cpp
std::vector<int> deduplicate(const std::vector<int>& sorted);
// deduplicate({1, 1, 2, 3, 3, 3, 4}) -> {1, 2, 3, 4}
```

### Exercise 3: Feel the Power of reserve

Insert 100,000 elements into a vector using two approaches: without calling `reserve` and with `reserve(100000)`. Use `<chrono>` to time both and compare the results. Experience the power of pre-allocating memory.

## Summary

In this chapter, we thoroughly covered the core operations of `std::vector`. Construction methods range from default construction to initializer lists to copy and move semantics. Insertion and deletion operations span from `push_back`/`emplace_back` to `erase` to the classic remove-erase idiom. Access methods include `operator[]`, `at`, and `data()`. Capacity management covers the distinction between `size`/`capacity` and performance optimization with `reserve`. Finally, we tied all the knowledge points together through a hands-on task manager program.

A few key takeaways: prefer `emplace_back` over `push_back`, be mindful of iterator invalidation caused by reallocation, understand the difference between `size` and `capacity` and call `reserve` when appropriate, and use the remove-erase idiom (or C++20's `std::erase`) when deleting elements that match a condition.

In the next chapter, we will look at `std::map` and `std::set`—when you need to look up by key or maintain a sorted collection, they are the go-to choices.

---

> **References**
>
> - [cppreference: std::vector](https://en.cppreference.com/w/cpp/container/vector)
> - [cppreference: std::remove](https://en.cppreference.com/w/cpp/algorithm/remove)
> - [cppreference: std::erase (C++20)](https://en.cppreference.com/w/cpp/container/vector/erase2)
