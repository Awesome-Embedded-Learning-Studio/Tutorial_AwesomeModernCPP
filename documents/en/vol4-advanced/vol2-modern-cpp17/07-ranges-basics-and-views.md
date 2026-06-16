---
chapter: 9
cpp_standard:
- 11
- 14
- 17
- 20
description: Ranges Library Basics
difficulty: intermediate
order: 7
platform: host
prerequisites:
- 'Chapter 8: 类型安全'
reading_time_minutes: 11
tags:
- cpp-modern
- host
- intermediate
title: C++20 Ranges Library Basics and Views
translation:
  source: documents/vol4-advanced/vol2-modern-cpp17/07-ranges-basics-and-views.md
  source_hash: 89721c9de712eca886a3ab34b440681ebc4107dae1ad5c5d8773b483bc4d232a
  translated_at: '2026-06-16T04:02:14.277090+00:00'
  engine: anthropic
  token_count: 2581
---
# Modern Embedded C++ Tutorial — C++20 Ranges Library Basics and Views

## Introduction

Whenever I handle arrays or container data, I always feel like something is missing. If I use STL algorithms, those `std::vector<int>::iterator`, `std::back_inserter` things are a total pain to write—iterator begin, iterator end, temporary container, and finally paste it back. After this whole routine, the code logic is torn into pieces, and reading it feels like chewing on dry bread.

Then C++20 brought the Ranges library, like installing a "data processing pipeline" into your code. Even more importantly, it introduced the concept of a "View"—**lazy evaluation, zero-overhead copying**—which is simply tailor-made for embedded development.

> TL;DR: **Ranges lets you compose operations like Unix pipes, while Views let you process data without extra copies, making it both elegant and efficient.**

Our current goal is to understand two things: what is a Range, what is a View, and why they are so useful in embedded scenarios.

------

## Starting with the Pain Point: How Annoying Traditional STL Algorithms Are

Let's first look at how we used to process data. Suppose we read a set of data from a sensor, need to filter out anomalies, and then multiply the rest by a coefficient:

```cpp
std::vector<int> raw_data = { /* ... sensor readings ... */ };
std::vector<int> filtered;
std::vector<int> calibrated;

// 1. Filter
for (auto x : raw_data) {
    if (x >= 0 && x <= 1023) {
        filtered.push_back(x);
    }
}

// 2. Calibrate
for (auto x : filtered) {
    calibrated.push_back(x * 2);
}
```

Look at how annoying this code is:

- You have to write the iterator range twice for every operation.
- You need to create temporary containers like `filtered` to store intermediate results.
- The logic is interrupted by intermediate variables; you can't see the "raw data → filter → calibrate" pipeline at a glance.
- Memory is allocated at least twice (`filtered` and `calibrated`).

In embedded scenarios, this kind of temporary memory allocation is particularly a headache—are you sure the heap has enough space? Are you sure it won't fragment? Are you sure real-time performance won't be affected by allocation?

The answers to these questions lie in the Ranges library.

------

## What is a Range: Simply Put, "A Pair of Iterators"

The C++20 standard library's definition of a "Range" is simple: **anything that can provide iterators**.

```cpp
std::vector<int> vec {1, 2, 3};
int arr[10] = {0};
std::list<float> list;
```

These are all Ranges. Previously, we wrote algorithms using `begin()`, `end()`, but now we can directly throw the entire container into the algorithm:

```cpp
std::ranges::sort(vec); // No more vec.begin(), vec.end()
```

But this is just syntactic sugar on the surface. The real power lies in a whole new set of tools in the `<ranges>` header file.

First, we need to distinguish two concepts: **Range** and **View**.

- **Range**: Anything iterable, including `std::vector`, `std::list`, native arrays.
- **View**: A special kind of Range that does not own data, it is just a "specific angle of observation" on existing data, and it performs **lazy evaluation**.

The concept of a View is so important that we will dedicate a whole section to it.

------

## Views: Zero-Overhead Data Lenses

The essence of a View can be summarized in four words: **Lazy, Non-owning, Composable, O(1) copy**.

### Lazy Views

Views are "lazy"—when you define them, nothing is calculated. Calculation only happens when you actually iterate over them:

```cpp
auto even = [](int i) { return i % 2 == 0; };
auto evens_view = std::views::filter(data, even); // No calculation happens here yet

// Calculation happens only during iteration
for (int val : evens_view) {
    // ...
}
```

### Non-owning Data

Views just "look at" the underlying data, they don't own it:

```cpp
std::vector<int> get_data() {
    return {1, 2, 3, 4, 5};
}

auto view = std::views::all(get_data()); // Dangerous! get_data() returns a temporary vector
// view is now dangling because the temporary vector is destroyed
```

### O(1) Copy

The copy cost of a View is constant level—it only stores a few pointers/iterators and does not copy the underlying data:

```cpp
auto view1 = std::views::filter(data, pred);
auto view2 = view1; // O(1), just copies pointers, no data copying
```

This is crucial for embedded systems—you can pass Views around everywhere without worrying about the overhead of data copying.

------

## Common View Factory Functions

The `<ranges>` header provides a series of "view factory" functions to create various Views. Let's pick the most useful ones for embedded development.

### filter: Filter Data

`std::views::filter` creates a View containing only elements that meet the condition:

```cpp
std::vector<int> data = {10, 25, 3, 8, 30};
auto valid = std::views::filter(data, [](int x) { return x > 5; });
// valid is now a view of {10, 25, 8, 30}
```

### transform: Convert Each Element

`std::views::transform` applies a function to each element:

```cpp
auto volts = std::views::transform(readings, [](int adc) { return adc * 3.3 / 4096; });
```

### take and drop: Take First N or Skip First N

```cpp
auto first_5 = std::views::take(data, 5);   // Take first 5
auto rest = std::views::drop(data, 2);      // Skip first 2
```

In embedded scenarios, this is particularly useful when dealing with protocol headers:

```cpp
auto payload = std::views::drop(packet, 4); // Skip 4-byte header
```

### split: Split by Delimiter

`std::views::split` splits a Range into sub-Ranges based on a delimiter:

```cpp
std::string msg = "ID:123;TEMP:25.5";
auto parts = std::views::split(msg, ';'); // Now ["ID:123", "TEMP:25.5"]
```

It's especially useful for parsing NMEA sentences (GPS data format):

```cpp
std::string nmea = "$GPGGA,123519,4807.036,N,01131.000,E*1A";
auto fields = std::views::split(nmea, ',');
```

### iota: Generate Sequence

`std::views::iota` generates an incrementing sequence:

```cpp
auto indices = std::views::iota(0, 10); // 0, 1, 2, ..., 9
```

------

## Composing Views: Start Building Pipelines

A single View has limited power, but combined they are powerful. We can use the pipe operator `|` to chain Views together (we will detail this in the next chapter, but let's warm up here):

```cpp
auto processed = readings
    | std::views::filter([](int x) { return x > 0; })
    | std::views::transform([](int x) { return x * 3.3 / 4096; })
    | std::views::take(5);
```

This code reads like a sentence: "From readings, filter valid values, convert to voltage, take the first 5." No temporary variables, no intermediate containers, the logic is touchingly clear.

------

## Embedded Practice: Sensor Data Processing Pipeline

Let's use a real embedded scenario to demonstrate the power of Views. Suppose we are building a temperature monitoring system with a set of temperature sensors, and we need to:

1. Filter out invalid readings (< -50 or > 150)
2. Convert Celsius to Fahrenheit
3. Calculate moving average
4. Output result

```cpp
std::vector<double> temps = { /* sensor readings */ };

auto pipeline = temps
    | std::views::filter([](double t) { return t >= -50.0 && t <= 150.0; })
    | std::views::transform([](double t) { return t * 9.0 / 5.0 + 32.0; })
    | std::views::transform([](double t) { /* moving average logic */ return t; });

for (auto val : pipeline) {
    printf("Temp: %.2f F\n", val);
}
```

Notice the beauty of this code:

- No temporary containers like `filtered_temps`, `calibrated_temps`.
- The whole process traverses the data only once.
- Memory overhead is O(1)—Views only store a few pointers.

------

## View vs Container: When to Use What

Views are powerful, but not a panacea. Here is a simple decision tree:

**Use View when:**

- Read-only data, no modification needed.
- Need to compose multiple operations.
- Want zero-overhead copying.
- Data source lifetime is long enough.
- One-time traversal.

**Use Container when:**

- Need to modify data.
- Need to traverse the same result multiple times.
- Data source might be destroyed.
- Need to own the data.

```cpp
// Good: One-time processing
for (auto x : data | std::views::filter(pred)) { /* ... */ }

// Good: Need to reuse results
auto result = std::vector<int>(data | std::views::filter(pred));
```

------

## Pitfall Guide

### Pitfall 1: View Lifetime

Views don't own data, so if the underlying data is destroyed, the View becomes dangling:

```cpp
auto get_view() {
    std::vector<int> local = {1, 2, 3};
    return std::views::all(local); // BUG: local is destroyed after return
}
```

### Pitfall 2: Invalid After Iteration

Some Views can only be iterated once, or their state changes after iteration:

```cpp
auto r = std::views::single(42);
auto it = r.begin();
*it; // OK
++it; // UB
```

If you need to iterate multiple times, consider converting to a container:

```cpp
auto vec = std::vector<int>(view); // Materialize the view
```

### Pitfall 3: View Types

The type of a View is a complex template instantiation product. Don't try to write it manually, use `auto`:

```cpp
// Bad
std::ranges::filter_view<std::ranges::ref_view<std::vector<int>>, Lambda> view = ...;

// Good
auto view = std::views::filter(data, pred);
```

### Bad News: Not All Compilers Fully Support It

C++20 Ranges are new, and some older compilers might have incomplete support. GCC 11+, Clang 13+, MSVC 2019+ are generally fine. If your compiler spits out a pile of template errors, check the version first.

------

## Summary

Views are the core concept of the C++20 Ranges library:

- **Lazy Evaluation**: No calculation on definition, calculation on iteration.
- **Non-owning Data**: Just a "lens" on underlying data.
- **O(1) Copy**: Passing Views around has zero overhead.
- **Composable**: Chain multiple operations with the pipe operator.

For embedded developers, Views allow us to write elegant data processing code while maintaining zero-overhead runtime performance. No need to choose between "elegant code" and "efficient code"—we want both.

In the next chapter, we will dive into the usage of the pipe operator `|` and more practical Ranges techniques. Then you will see how the philosophy of Unix pipes is perfectly implemented in C++.
