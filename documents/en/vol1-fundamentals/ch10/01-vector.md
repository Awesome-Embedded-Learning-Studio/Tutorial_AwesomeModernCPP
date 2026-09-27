---
chapter: 10
cpp_standard:
- 11
- 14
- 17
- 20
description: Master vector insertion, deletion, updates, lookup, and capacity management, and learn to use C++'s most common dynamic container
difficulty: beginner
order: 1
platform: host
prerequisites:
- 'What Is the STL: Containers, Algorithms, and the Iterators In Between'
reading_time_minutes: 12
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: std::vector Quick Start
---
# std::vector Quick Start

In the previous article, we met the STL's three main players: containers store data, iterators provide the interface, and algorithms do the work. Starting here, we will dig into them one by one. First up is the most frequently used container, `std::vector`: a dynamic array that grows automatically, stores elements contiguously, and offers O(1) random access. If you are not sure which container to use, start with `vector`; the other containers pull ahead only in particular situations.

In this chapter, we will start from scratch and work through constructing a `vector`, adding, removing, updating, and accessing elements, managing capacity, and traversing its contents. We will then tie everything together with a practical task manager program.

## Starting from scratch with vector construction

`std::vector` has several constructors, so let us look at them one at a time:

```cpp
#include <vector>
#include <string>

std::vector<int> v1;                    // Empty vector
std::vector<int> v2(10);                // 10 elements, each initialized to 0
std::vector<int> v3(10, 42);            // 10 elements, each initialized to 42
std::vector<int> v4 = {1, 2, 3, 4, 5};  // Initializer list
std::vector<int> v5(v4);                // Copy construction
std::vector<int> v6(std::move(v5));     // Move construction; takes ownership of the resources
```

There is one point to watch here: `v2(10)` creates 10 elements, each with the value `int()`, which is 0. It does not “reserve 10 slots without creating any elements”; those 10 elements really exist. Reserved storage and actual elements are two different concepts, and we will dig into that distinction when we discuss `reserve`.

`vector<bool>` is a specialized version of `vector` that compresses each `bool` into 1 bit to save space. As a result, much of its behavior differs from an ordinary `vector<T>`—for example, `operator[]` returns a proxy object rather than a `bool&`. If you need a genuine array of Boolean-like values, `vector<char>` or `deque<bool>` is a safer choice.

## Putting things in by adding elements

The most common way to add to a `vector` is `push_back`, which appends an element to the end. Since C++11, we have also had `emplace_back`, which is more efficient than `push_back`: `push_back` accepts an object that has already been constructed, while `emplace_back` accepts constructor arguments and constructs the object directly in the vector's memory, avoiding one move or copy.

```cpp
struct Task {
    std::string name;
    int priority;
    Task(std::string n, int p) : name(std::move(n)), priority(p) {}
};

std::vector<Task> tasks;
tasks.push_back(Task("写代码", 1));   // Construct a temporary object first, then move it
tasks.emplace_back("测试", 2);         // Construct in place, with no temporary object
```

For simple types such as `int` and `double`, the performance difference is negligible. For classes containing `std::string` or other members that require dynamic memory allocation, however, `emplace_back` can avoid an unnecessary construction and move. Let us build the habit of preferring `emplace_back`.

If you need to insert an element at a particular position in the middle, use `insert`:

```cpp
std::vector<int> v = {10, 20, 30, 40};
v.insert(v.begin() + 1, 15);  // v: {10, 15, 20, 30, 40}
```

Keep in mind that inserting in the middle with `insert` requires shifting every later element backward, so its time complexity is O(n). If we find ourselves frequently inserting elements at the front or in the middle of a vector, we may want to consider `std::deque` or `std::list` instead.

Any operation that may cause a vector to reallocate its storage—including `push_back`, `emplace_back`, and `insert`—invalidates all previously saved iterators, pointers, and references if reallocation occurs. Consider this code:

```cpp
std::vector<int> v = {1, 2, 3};
int* p = &v[0];       // Points to the first element
v.push_back(4);       // May trigger a capacity increase!
// Dereferencing p is now undefined behavior—the memory it points to may have been freed
```

If we need to hold a pointer or reference to an element in a vector, we must either ensure that no later operation can trigger reallocation or access the element indirectly through its index instead.

## Taking things out by accessing elements

`vector` provides several ways to access its elements. The one we use most often is `operator[]`, which works like subscripting a C array and performs no bounds checking. If you want bounds checking—and a `std::out_of_range` exception when the index is invalid—use `at`:

```cpp
std::vector<int> v = {10, 20, 30, 40, 50};
v[0] = 100;          // No bounds checking
int y = v.at(10);    // Throws std::out_of_range
```

We use `operator[]` more often in everyday development, but when an index comes from user input or external data, `at` provides a useful safety barrier.

There are also several convenient access functions: `front()` returns a reference to the first element, equivalent to `v[0]`; `back()` returns a reference to the last element, equivalent to `v[v.size() - 1]`; and `data()` returns a pointer to the underlying array. Because a vector's elements are stored contiguously, we can use `v.data()` directly as a C array, which is especially handy when interacting with C-style APIs.

Calling `front()`, `back()`, or `operator[]` on an empty vector is undefined behavior. It does not throw an exception; it drops us straight into the abyss of UB. `at()` is the only one of these access methods that performs bounds checking on an empty vector. Before calling `front()` or `back()`, we should therefore either know that the vector is nonempty or check it with `empty()` first.

## Removing what we do not need

The simplest removal operation is `pop_back`, which removes the last element. It returns `void`, not the removed value. If you need that value, retrieve it with `back()` before calling `pop_back`.

We use `erase` to remove elements from the middle. It accepts either one iterator or a range:

```cpp
std::vector<int> v = {10, 20, 30, 40, 50};
v.erase(v.begin() + 2);                // v: {10, 20, 40, 50}
v.erase(v.begin() + 1, v.begin() + 3); // v: {10, 50}
```

To remove all elements at once, we use `clear()`: afterward, `size` becomes 0, but `capacity` does not change. In other words, the elements are destroyed, but the memory is not released. If we also want to release the memory, we can follow it with `shrink_to_fit`.

### The remove-erase idiom

Now for the question: how do we remove every element equal to a particular value from a vector? The answer is the remove-erase idiom, a classic C++ pattern:

```cpp
#include <algorithm>

std::vector<int> v = {1, 2, 3, 2, 4, 2, 5};
v.erase(std::remove(v.begin(), v.end(), 2), v.end());
// v: {1, 3, 4, 5}
```

`std::remove` does not actually erase elements. It moves every element that is not equal to 2 toward the front and returns an iterator pointing to the “new logical end”; the elements equal to 2 are pushed toward the back. Then `erase` truly removes the elements between the new end and the old end. We need these two steps because of the STL's design philosophy: “algorithms should not operate directly on container interfaces.” `std::remove` knows only about iterators, not the vector's `erase` member function.

Starting in C++20, we can do the whole job in one line with `std::erase`: `std::erase(v, 2);`. If your compiler supports C++20, we strongly recommend this newer form.

## Understanding size and capacity

`vector` has two easily confused concepts: `size` is the number of elements currently stored, while `capacity` is the number of elements that fit in the already allocated storage. `capacity` is always greater than or equal to `size`. When repeated calls to `push_back` are about to make `size` exceed `capacity`, the vector grows automatically: it allocates a larger block of memory, moves every element into it, and then releases the old storage. Most standard library implementations double `capacity`, so we see it grow in a sequence such as 1, 2, 4, 8, 16, 32, and so on. Every growth operation involves a complete allocation and a copy or move of every element.

If we know roughly how many elements we will store, calling `reserve` to allocate enough space up front can avoid the cost of repeated growth:

```cpp
std::vector<int> v;
v.reserve(1000);  // Allocate once; capacity becomes 1000 while size remains 0

for (int i = 0; i < 1000; ++i) {
    v.push_back(i);  // Does not trigger any reallocation
}
```

`reserve` affects only `capacity`, not `size`. Conversely, if we want to release excess capacity, we can use `shrink_to_fit`. This is a non-binding request, so the standard does not guarantee that memory will actually be released, though mainstream implementations do so.

## Walking through every element

There are three common ways to traverse a vector. The one we recommend most is the range-based `for` loop, introduced in C++11, because it is both concise and safe:

```cpp
std::vector<int> v = {10, 20, 30, 40, 50};

// Read-only traversal—make const auto& the default for read-only access
for (const auto& elem : v) {
    std::cout << elem << " ";
}

// Drop const when elements need to be modified
for (auto& elem : v) {
    elem *= 2;
}
```

Notice that we use `const auto&` rather than `auto`. The difference is minor for `int`, but when traversing a `vector<std::string>`, `auto` makes a copy while `const auto&` is only a reference. If you need the index, use a traditional loop: `for (std::size_t i = 0; i < v.size(); ++i)`. When working with STL algorithms or needing finer control, use iterators: `for (auto it = v.begin(); it != v.end(); ++it)`. In day-to-day development, range-based `for` covers 90% of traversal needs.

## Practical time with a vector task manager

All right, let us fold everything we have learned into one practical program. It supports adding tasks, marking tasks complete and removing them, listing every task, and inspecting capacity information.

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
        // The remove-erase idiom: remove every task for which done == true
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

The complete code is available below. Click “Try It Yourself” to run it directly—no terminal required:

<OnlineCompilerDemo
  title="Practical Time: Task Manager task_manager.cpp"
  source-path="code/examples/vol1/23_task_manager.cpp"
  description="Run the task manager online. Notice that capacity is still 4 after removing tasks—erase does not return memory. Add a few more tasks and see when capacity doubles."
  run-options="-O2 -std=c++17"
  allow-run
/>

Notice that the final `capacity` is still 4—`erase` does not release memory. This small detail is often overlooked in real development.

Erasing elements directly inside a range-based `for` loop causes undefined behavior because `erase` invalidates iterators. If we need to remove elements while traversing a vector, we can either iterate backward with indices or use an iterator loop together with the iterator returned by `erase`. In most cases, however, marking elements first and then applying remove-erase in one pass is clearer, just like `remove_completed` in the code above.

## Try it yourself with exercises

### Exercise 1 Frequency counter

Given a vector of integers, count how many times each value occurs. Hint: you can sort the vector and then traverse it, or use nested loops. A simple implementation is enough; there is no need to use a map.

```cpp
std::vector<int> data = {3, 1, 4, 1, 5, 9, 2, 6, 5, 3, 5};
// Expected output: 1 appears 2 times, 2 appears 1 times, ...
```

### Exercise 2 Removing duplicates

Write a function that accepts a sorted vector and returns a new vector with duplicates removed. Do not use `std::unique`; implement the logic yourself.

```cpp
std::vector<int> deduplicate(const std::vector<int>& sorted);
// deduplicate({1, 1, 2, 3, 3, 3, 4}) -> {1, 2, 3, 4}
```

### Exercise 3 Feeling the power of reserve

Insert 100,000 elements into a vector in two ways: once without calling `reserve`, and once after calling `reserve(100000)`. Measure both versions with `<chrono>` and compare their running times to see the impact of allocating storage in advance.

---

> **References**
>
> - [cppreference: std::vector](https://en.cppreference.com/w/cpp/container/vector)
> - [cppreference: std::remove](https://en.cppreference.com/w/cpp/algorithm/remove)
> - [cppreference: std::erase (C++20)](https://en.cppreference.com/w/cpp/container/vector/erase2)

translation:
  source: documents/vol1-fundamentals/ch10/01-vector.md
  source_hash: 05c0397224d621a6bc5fa80a541c0185f2d8062fc97a83b8236c87e61c9023d3
  translated_at: '2026-09-27T06:59:37+00:00'
  engine: anthropic
  token_count: 1648
---
<!-- note: The C++ standard does not prescribe vector's growth factor, and emplace_back is not categorically faster than push_back; the translation preserves the source wording. -->
