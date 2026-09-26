---
chapter: 11
cpp_standard:
- 11
- 14
- 17
- 20
description: Master vector's insert, delete, update and query operations along with capacity management, and learn to use the most frequently used C++ dynamic container
difficulty: beginner
order: 1
platform: host
prerequisites:
- 错误处理方式对比
reading_time_minutes: 12
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: std::vector Quick Start
translation:
  source: documents/vol1-fundamentals/ch11/01-vector.md
  source_hash: 1f980193fd6ad5c39c76746205c36cf85c67992571911a892ffd623071e21648
  translated_at: '2026-09-26T11:57:26+00:00'
  engine: anthropic
  token_count: 3000
---

# std::vector: Not Sure Which Container to Use? Just Use It

In the previous few chapters we walked through the core of the C++ language (the type system, control flow, functions, classes and inheritance). From here on, we're stepping into a brand-new territory: the Standard Template Library (STL). The STL ships a whole pile of ready-made containers, algorithms, and iterators, saving us from reinventing a lot of wheels. And among all the containers, `std::vector` is definitely the one that shows up most often: a dynamic array that grows automatically, stores its elements contiguously, and gives O(1) random access. If you're not sure which container to use, `vector` is the right call — the other containers only win in specific scenarios.

In this chapter we start from zero and walk through `vector`'s construction, insertion/deletion/update/query, capacity management, and ways to iterate over it, and finally tie everything together with a hands-on task-manager program.

## Starting from Zero — Constructing a vector

`std::vector` has quite a few ways to be constructed; let's go through them one by one:

```cpp

#include <vector>

#include <string>

std::vector<int> v1;                    // empty vector
std::vector<int> v2(10);                // 10 elements, each is 0
std::vector<int> v3(10, 42);            // 10 elements, each is 42
std::vector<int> v4 = {1, 2, 3, 4, 5};  // initializer list
std::vector<int> v5(v4);                // copy construction
std::vector<int> v6(std::move(v5));     // move construction, takes over the resources
```

One thing we should note here: `v2(10)` creates 10 elements, each with the value `int()`, that is, 0. This is not "reserving 10 slots with no elements in them" — there really are 10 elements inside. Reserved space and actual elements are two different concepts, and we'll dig into that when we cover `reserve` later.

`vector<bool>` is a specialized version of `vector` that compresses each `bool` down to 1 bit to save space. This makes `vector<bool>` behave differently from an ordinary `vector<T>` in many ways — for example, `operator[]` doesn't return a `bool&` but a proxy object. If you need a real array of bools, `vector<char>` or `deque<bool>` is the safer choice.

## Stuffing Things In — Adding Elements

The most commonly used add operation on a `vector` is `push_back`, which appends an element at the end. Since C++11 we also have `emplace_back`, which is more efficient than `push_back`: `push_back` takes an already-constructed object, while `emplace_back` takes constructor arguments and constructs the object in place directly in the vector's memory, saving one move or copy.

```cpp
struct Task {
    std::string name;
    int priority;
    Task(std::string n, int p) : name(std::move(n)), priority(p) {}
};

std::vector<Task> tasks;
tasks.push_back(Task("写代码", 1));   // constructs a temporary first, then moves it
tasks.emplace_back("测试", 2);         // constructs in place, no temporary needed
```

For simple types like `int` and `double`, there's practically no performance difference between the two. But for classes containing `std::string` or other members that need dynamic memory allocation, `emplace_back` can save one unnecessary construction and move. Let's make it a habit: prefer `emplace_back`.

If you need to insert an element at some position in the middle, use `insert`:

```cpp
std::vector<int> v = {10, 20, 30, 40};
v.insert(v.begin() + 1, 15);  // v: {10, 15, 20, 30, 40}
```

But note that inserting in the middle requires shifting all the elements after it backward, so the time complexity is O(n). If we find ourselves frequently inserting elements at the head or in the middle of a vector, maybe we should consider switching to `std::deque` or `std::list`.

Any operation that may cause the vector to reallocate its memory (including `push_back`, `emplace_back`, and `insert`) invalidates all previously saved iterators, pointers, and references. Let's look at the following piece of code:

```cpp
std::vector<int> v = {1, 2, 3};
int* p = &v[0];       // points to the first element
v.push_back(4);       // may trigger a reallocation!
// *p is now undefined behavior — the memory p points to may already have been freed
```

If we need to hold a pointer or reference to an element of a vector, we should either make sure we don't do anything afterward that could trigger a reallocation, or switch to accessing the element indirectly through an index.

## Taking Things Out — Accessing Elements

`vector` provides several ways to access elements. The one we use most is `operator[]`, which accesses by subscript just like a C array and does no bounds checking. If you want bounds checking (throwing a `std::out_of_range` exception when the index is out of range), use `at`:

```cpp
std::vector<int> v = {10, 20, 30, 40, 50};
v[0] = 100;          // no bounds checking
int y = v.at(10);    // throws std::out_of_range
```

In our day-to-day development we use `operator[]` more often, but in scenarios where the index comes from user input or external data, `at` is a safety net.

There are also a few convenient accessors: `front()` returns a reference to the first element (equivalent to `v[0]`), `back()` returns a reference to the last element (equivalent to `v[v.size() - 1]`), and `data()` returns a pointer to the underlying array. Because a vector's elements are stored contiguously, we can use `v.data()` directly as a C array, which is especially handy when talking to C-style APIs.

Calling `front()`, `back()`, or `operator[]` on an empty vector is undefined behavior: it won't throw an exception — instead we fall straight into undefined behavior. `at()` is the only one of these that bounds-checks an empty vector. So before we call `front()` or `back()`, we should either confirm the vector isn't empty or check with `empty()` first.

## Dropping What We Don't Need — Erasing Elements

The simplest is `pop_back`, which removes the element at the end. It returns `void` and does not return the removed value. If you need that value, grab it with `back()` before calling `pop_back`.

To remove elements from the middle we use `erase`, which accepts an iterator or a range:


```cpp
std::vector<int> v = {10, 20, 30, 40, 50};
v.erase(v.begin() + 2);                // v: {10, 20, 40, 50}
v.erase(v.begin() + 1, v.begin() + 3); // v: {10, 50}
```

To remove every element at once, we use `clear()`. Afterward, `size` becomes 0 while
`capacity` stays unchanged: the elements have been destroyed, but the vector retains its
allocated storage. If we also want to ask the vector to release that unused storage, we can
follow it with `shrink_to_fit`.

### The Remove-Erase Idiom

Now for the next question: what if we want to remove every element in a vector that equals a
particular value? The answer is the remove-erase idiom, a classic C++ pattern:

```cpp

#include <algorithm>

std::vector<int> v = {1, 2, 3, 2, 4, 2, 5};
v.erase(std::remove(v.begin(), v.end(), 2), v.end());
// v: {1, 3, 4, 5}
```

Notice that `std::remove` does not actually erase anything. It moves every element that is not
equal to 2 toward the front, then returns an iterator to the "new logical end." The elements
beyond that iterator are left in a valid but unspecified state. `erase` then truly removes the
elements between the new end and the old end. We need these two steps because of the STL's
design philosophy: algorithms should not operate directly on a container's interface.
`std::remove` knows only about iterators; it knows nothing about `vector::erase`.

Since C++20, we can do the same job in one line: `std::erase(v, 2);`. If your compiler supports
C++20, we strongly recommend this newer spelling.

## Understanding size and capacity

`vector` involves two easily confused concepts: `size` is the number of elements actually stored right now, while `capacity` is the number of elements the allocated memory can hold, and `capacity` is always greater than or equal to `size`. When we keep `push_back`-ing until `size` is about to exceed `capacity`, the `vector` grows automatically: it allocates a bigger block of memory, moves all the elements over, and then frees the old memory. Most standard library implementations double the `capacity` when growing, so we'll see capacity increase in a sequence like this: 1, 2, 4, 8, 16, 32 ... Every growth step involves a full memory allocation plus a copy/move of all the elements.

If we know roughly how many elements we'll need to store ahead of time, using `reserve` to allocate enough space up front saves us the overhead of repeated reallocations:

```cpp
std::vector<int> v;
v.reserve(1000);  // One-shot allocation: capacity becomes 1000, size stays 0

for (int i = 0; i < 1000; ++i) {
    v.push_back(i);  // Triggers no reallocation at all
}
```

`reserve` only affects `capacity`, not `size`. The other way around, if we want to release the spare capacity, we use `shrink_to_fit` — though this is a non-binding request: the standard doesn't guarantee the memory actually gets freed, but mainstream implementations do it anyway.

## Going Over the Elements — Iterating a vector

We have three common ways to iterate a vector. The most recommended one is the range-for loop (introduced in C++11), which is concise and safe:

```cpp
std::vector<int> v = {10, 20, 30, 40, 50};

// Read-only traversal — make it a habit: always use const auto& for read-only access
for (const auto& elem : v) {
    std::cout << elem << " ";
}

// Drop the const when you need to modify the elements
for (auto& elem : v) {
    elem *= 2;
}
```

Notice that we use `const auto&` here instead of `auto`. For `int` the difference is minor, but when iterating a `vector<std::string>`, `auto` triggers a copy, whereas `const auto&` is just a reference. If you need the index, use a traditional loop: `for (std::size_t i = 0; i < v.size(); ++i)`. When you need to pair up with STL algorithms or want finer control, use iterators: `for (auto it = v.begin(); it != v.end(); ++it)`. In day-to-day development, range-for covers 90% of iteration needs.

## Hands-on Time — Writing a Task Manager with vector

Alright folks, let's knead everything we've learned so far into one practical program: a task manager that supports adding tasks, marking tasks done and removing them, listing all tasks, and showing capacity info.

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
        // remove-erase idiom: remove every task with done == true
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

Note that `capacity` is still 4 at the end — `erase` doesn't release memory. This little detail gets overlooked all the time in real-world development.

Calling `erase` on elements directly inside a range-for loop leads to undefined behavior, because `erase` invalidates iterators. If we need to remove elements while iterating, we either use an index-based loop walking backwards, or an iterator loop paired with `erase`'s return value. In most cases, though, marking first and then doing one unified remove-erase pass is the cleaner approach — just like `remove_completed` does in the code above.

## Try It Yourself — Exercises

### Exercise 1: Frequency counter

Given a vector of integers, count how many times each value occurs. Hint: you can sort it and then walk through it, or use a double loop (a simple implementation is fine — no need for a map).

```cpp
std::vector<int> data = {3, 1, 4, 1, 5, 9, 2, 6, 5, 3, 5};
// Expected output: 1 appears 2 times, 2 appears 1 times, ...
```

### Exercise 2: Deduplication

Write a function that takes a sorted vector and returns a new vector with duplicates removed. You're not allowed to use `std::unique` — implement it by hand.

```cpp
std::vector<int> deduplicate(const std::vector<int>& sorted);
// deduplicate({1, 1, 2, 3, 3, 3, 4}) -> {1, 2, 3, 4}
```

### Exercise 3: Feel the power of reserve

Insert 100000 elements into a vector twice — once without calling reserve, once with reserve(100000) — and time both versions with `<chrono>` to compare. Get a feel for the power of allocating memory up front.

---

> **References**
>
> - [cppreference: std::vector](https://en.cppreference.com/w/cpp/container/vector)
> - [cppreference: std::remove](https://en.cppreference.com/w/cpp/algorithm/remove)
> - [cppreference: std::erase (C++20)](https://en.cppreference.com/w/cpp/container/vector/erase2)
