---
chapter: 3
cpp_standard:
- 14
- 17
- 20
description: Higher-order functions, composition, and currying — functional programming
  techniques in C++
difficulty: intermediate
order: 5
platform: host
prerequisites:
- 'Chapter 3: Lambda Basics: The Elegant Expression of Anonymous Functions'
- 'Chapter 3: std::function, std::invoke, and Callable Objects'
reading_time_minutes: 15
related:
- 'Volume 4: Deep Dive into the Ranges Library'
tags:
- host
- cpp-modern
- intermediate
- lambda
- 函数对象
title: Functional Programming Patterns
translation:
  source: documents/vol2-modern-features/ch03-lambda/05-functional-patterns.md
  source_hash: 793fd51f45233e82bd480696950b326d17263f4f76a8575df03cf1c46d0d31f5
  translated_at: '2026-09-25T15:20:40+00:00'
  engine: anthropic
  token_count: 3900
---
# Functional Programming Patterns: Passing Functions Around as Values

When functional programming comes up, many C++ developers' first reaction is probably: "Isn't that the Haskell crowd's thing? What does it have to do with C++?" In fact, C++ has been absorbing functional programming ideas ever since C++11—lambdas are anonymous functions that behave as first-class citizens, `std::function` is a higher-order type, and the `std::algorithm` family is essentially a set of map/filter/reduce variants. It's just that C++ doesn't wrap these things in a "purely functional" interface.

In this chapter we'll look at the functional programming patterns that actually pay off in C++—higher-order functions, function composition, and partial application—and how to write functional-style data processing pipelines with STL algorithms. At the end we'll preview the C++20 Ranges library, which you could fairly call the "ultimate form" of functional programming in C++.

---

## Higher-Order Functions—Functions That Take or Return Functions

The higher-order function is the cornerstone of functional programming. The definition is simple: either a parameter is a function, or the return value is a function, or both. In C++, higher-order functions are implemented through template parameters or `std::function`.

Let's look at a real example—a generic retry mechanism. Its parameters are an operation that might fail, a predicate that decides whether a retry is needed, and a maximum number of attempts:

```cpp
#include <iostream>
#include <functional>
#include <random>

// A higher-order function: takes an "operation" and a "decision function" as arguments
template<typename Operation, typename ShouldRetry>
auto with_retry(Operation&& op, ShouldRetry&& should_retry, int max_attempts)
    -> std::invoke_result_t<Operation>
{
    for (int attempt = 1; attempt <= max_attempts; ++attempt) {
        try {
            auto result = op();
            return result;
        } catch (const std::exception& e) {
            if (attempt == max_attempts || !should_retry(attempt, e)) {
                throw;
            }
            std::cout << "Attempt " << attempt << " failed: " << e.what()
                      << ", retrying...\n";
        }
    }
    throw std::runtime_error("unreachable");
}

// Usage example
void demo_higher_order() {
    int call_count = 0;

    auto result = with_retry(
        [&call_count]() -> int {
            call_count++;
            if (call_count < 3) {
                throw std::runtime_error("connection timeout");
            }
            return 42;
        },
        [](int attempt, const std::exception& e) {
            return attempt < 5;   // retry at most 5 times
        },
        5
    );

    std::cout << "Result: " << result << "\n";   // Result: 42
}
```

You have already used plenty of higher-order functions from the STL—`std::sort` takes a comparison function, `std::transform` takes a transformation function, `std::find_if` takes a predicate. What these functions have in common is that they "pull the strategy out of the algorithm and let the caller decide". That is the core value of higher-order functions.

### Functions That Return Functions

Higher-order functions don't just take functions—they can also return them. This pattern is especially useful for creating configurable strategy objects. For example, returning a filter with a preset threshold:

```cpp
auto make_threshold_filter(int threshold) {
    return [threshold](const std::vector<int>& data) {
        std::vector<int> result;
        std::copy_if(data.begin(), data.end(), std::back_inserter(result),
                    [threshold](int x) { return x > threshold; });
        return result;
    };
}

auto filter_above_50 = make_threshold_filter(50);
auto filter_above_80 = make_threshold_filter(80);
```

One caveat, though: if different branches return lambdas of different types, returning them directly leads to a type mismatch—every lambda's closure type is unique. Take this example:

```cpp
// ❌ Compile error: the lambdas in the two branches have different types
auto make_counter(bool start_high) {
    if (start_high) {
        return []() { return 100; };  // closure type A
    } else {
        return []() { return 0; };    // closure type B
    }
}
```

In this situation you need `std::function` to erase the types and unify the return type:

```cpp
// ✅ Correct: std::function unifies the type
std::function<int()> make_counter(bool start_high) {
    if (start_high) {
        return []() { return 100; };
    } else {
        return []() { return 0; };
    }
}
```

The cost is that `std::function` introduces a little runtime overhead (type erasure and possible heap allocation), but in most scenarios that overhead is negligible.

---

## Function Composition—compose and pipe

Function composition chains multiple functions together, with one function's output feeding the next function's input. Mathematically, `compose(f, g)(x) = f(g(x))`; in pipeline style, `pipe(g, f)(x) = f(g(x))`—apply g first, then f.

The cleanest way to implement function composition in C++ is generic lambdas plus `auto` return type deduction:

```cpp
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>

// compose: f(g(x))
auto compose = [](auto f, auto g) {
    return [f = std::move(f), g = std::move(g)](auto&&... args) {
        return f(g(std::forward<decltype(args)>(args)...));
    };
};

// pipe: g first, then f (more intuitive semantics)
auto pipe = [](auto g, auto f) {
    return [g = std::move(g), f = std::move(f)](auto&&... args) {
        return f(g(std::forward<decltype(args)>(args)...));
    };
};

void demo_composition() {
    auto double_it = [](int x) { return x * 2; };
    auto add_one = [](int x) { return x + 1; };
    auto to_string = [](int x) { return std::to_string(x); };

    // compose(add_one, double_it)(5) = add_one(double_it(5)) = add_one(10) = 11
    auto composed = compose(add_one, double_it);
    std::cout << composed(5) << "\n";    // 11

    // multi-level composition
    auto pipeline = compose(to_string, compose(add_one, double_it));
    std::cout << pipeline(5) << "\n";    // "11"
}
```

Composing two functions is easy enough, but once you compose several, nested `compose` calls make the code hard to read. A more elegant approach is a variadic version, `compose_all`:

```cpp
// Compose multiple functions: apply right to left
template<typename F>
auto compose_all(F f) {
    return f;
}

template<typename F, typename... Fs>
auto compose_all(F f, Fs... rest) {
    return [f = std::move(f), ...rest = std::move(rest)](auto&&... args) {
        return f(compose_all(rest...)(std::forward<decltype(args)>(args)...));
    };
}

// pipe_all: apply left to right (more intuitive)
template<typename F>
auto pipe_all(F f) {
    return f;
}

template<typename F, typename... Fs>
auto pipe_all(F f, Fs... rest) {
    return [f = std::move(f), ...rest = std::move(rest)](auto&&... args) {
        return pipe_all(rest...)(f(std::forward<decltype(args)>(args)...));
    };
}

void demo_multi_compose() {
    auto double_it = [](int x) { return x * 2; };
    auto add_one = [](int x) { return x + 1; };
    auto negate_it = [](int x) { return -x; };

    // pipe: 5 -> add_one -> double_it -> negate_it
    // 5 -> 6 -> 12 -> -12
    auto pipeline = pipe_all(add_one, double_it, negate_it);
    std::cout << pipeline(5) << "\n";   // -12
}
```

C++17 fold expressions make variadic template implementations remarkably compact. `pipe_all` applies its functions from left to right—first `add_one`, then `double_it`, finally `negate_it`—so the data flows in the same direction as the code is written, which reads very naturally.

---

## Partial Application—Binding Some Arguments

Partial application means "presetting some of a function's arguments and returning a new function that only needs the remaining ones". The C++ standard library provides `std::bind`, but in modern C++ a lambda is usually the better choice—the code is clearer, the error messages friendlier, and there are none of `std::bind`'s weird corner cases.

```cpp
#include <iostream>
#include <functional>

// Partial application with a lambda
auto make_adder(int base) {
    return [base](int x) { return base + x; };
}

// A more general partial application: fix the first N arguments
auto partial = [](auto f, auto... fixed_args) {
    return [f = std::move(f), ...fixed_args = std::move(fixed_args)](auto&&... rest_args) {
        return f(fixed_args..., std::forward<decltype(rest_args)>(rest_args)...);
    };
};

void demo_partial_application() {
    auto add = [](int a, int b, int c) { return a + b + c; };

    // Fix the first argument at 1
    auto add1 = partial(add, 1);
    std::cout << add1(2, 3) << "\n";   // 6

    // Fix the first two arguments
    auto add1_2 = partial(add, 1, 2);
    std::cout << add1_2(3) << "\n";    // 6

    // A more practical example: create a filter with a preset threshold
    auto make_threshold_filter = [](int threshold) {
        return [threshold](const std::vector<int>& data) {
            std::vector<int> result;
            std::copy_if(data.begin(), data.end(),
                        std::back_inserter(result),
                        [threshold](int x) { return x > threshold; });
            return result;
        };
    };

    auto filter_above_50 = make_threshold_filter(50);
    auto filter_above_80 = make_threshold_filter(80);

    std::vector<int> data = {12, 45, 67, 89, 23, 90};
    auto r1 = filter_above_50(data);   // {67, 89, 90}
    auto r2 = filter_above_80(data);   // {89, 90}
}
```

Partial application is especially handy in event handling and the strategy pattern—you can pin down certain arguments during the configuration phase and pass only the remaining ones at runtime. Compared with writing a full strategy class, a partially applied lambda is far lighter.

### Currying—Just Know the Concept

Currying and partial application often get confused with each other, but they are different concepts. Currying converts a multi-argument function into a chain of single-argument calls: `f(a, b, c)` becomes `f(a)(b)(c)`. Partial application fixes some arguments and returns a function that takes fewer of them, while currying makes a function accept exactly one argument at a time and return the next function, until all the arguments are in.

Honestly, currying is less practical in C++ than partial application—C++ natively supports multi-argument function calls, so there is no reason to break every function into single-argument chains. Partial application is the pattern you will actually use. The value of understanding currying is the idea it reveals, one of the core insights of functional programming: functions themselves are first-class citizens that can be progressively "specialized".

---

## map/filter/reduce—Functional Style with STL Algorithms

map (mapping), filter (filtering), and reduce (reducing) are the three workhorses of functional data processing. C++'s STL algorithms provide the matching tools: `std::transform` plays map, `std::copy_if` / `std::remove_if` play filter, and `std::accumulate` plays reduce.

Strung together, the three stages move the data along like stations on a line:

![The filter, map, reduce data processing pipeline](./05-functional-patterns-pipeline.drawio)

Let's demonstrate these three operations with a complete data processing pipeline:

```cpp
#include <algorithm>
#include <numeric>
#include <vector>
#include <iostream>
#include <string>

struct SensorReading {
    std::string sensor_id;
    double value;
    uint32_t timestamp;
};

void demo_map_filter_reduce() {
    std::vector<SensorReading> readings = {
        {"temp_01", 23.5, 1000},
        {"temp_01", 24.1, 2000},
        {"temp_02", 45.0, 1000},
        {"temp_01", 22.8, 3000},
        {"temp_02", 47.3, 2000},
        {"temp_01", 25.0, 4000},
        {"temp_02", 44.5, 3000},
        {"temp_03", 18.2, 1000},
    };

    // === Filter: keep only the temp_01 readings ===
    std::vector<SensorReading> filtered;
    std::copy_if(readings.begin(), readings.end(),
                std::back_inserter(filtered),
                [](const SensorReading& r) { return r.sensor_id == "temp_01"; });

    // === Map: extract the temperature values ===
    std::vector<double> values(filtered.size());
    std::transform(filtered.begin(), filtered.end(),
                  values.begin(),
                  [](const SensorReading& r) { return r.value; });

    // === Reduce: compute the average ===
    double sum = std::accumulate(values.begin(), values.end(), 0.0);
    double avg = sum / static_cast<double>(values.size());

    std::cout << "temp_01 readings: ";
    for (double v : values) std::cout << v << " ";
    std::cout << "\n";
    std::cout << "Average: " << avg << "\n";
    // temp_01 readings: 23.5 24.1 22.8 25
    // Average: 23.85
}
```

### Wrapping Them into Reusable Functional Tools

That three-stage pattern can be wrapped into generic lambda utilities, pushing the code one notch further toward the functional style:

```cpp
auto functional_map = [](const auto& container, auto func) {
    using Value = std::decay_t<decltype(func(*container.begin()))>;
    std::vector<Value> result;
    result.reserve(container.size());
    std::transform(container.begin(), container.end(),
                  std::back_inserter(result), func);
    return result;
};

auto functional_filter = [](const auto& container, auto pred) {
    using Value = std::decay_t<typename std::decay_t<decltype(container)>::value_type>;
    std::vector<Value> result;
    std::copy_if(container.begin(), container.end(),
                std::back_inserter(result), pred);
    return result;
};

// Chaining example: filter the evens -> double them
std::vector<int> data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
auto evens = functional_filter(data, [](int x) { return x % 2 == 0; });
auto doubled = functional_map(evens, [](int x) { return x * 2; });
```

The drawback of this style is that every operation creates a new `std::vector`—multiple filters and maps mean multiple temporary containers. Benchmarks show that for a filter+transform pipeline over 1 million elements, this approach is roughly 16x slower than C++20 Ranges, and it allocates an extra ~4 MB of memory for the intermediate containers. The C++20 Ranges library solves this problem with lazy evaluation, which we will come to shortly.

---

## Thinking in Immutable Data

Functional programming has a core principle: avoid modifying data—create new data instead. It sounds wasteful, but the benefits are tangible—no data races (the starting point of thread safety), easier reasoning about code behavior (deterministic input gives deterministic output), and easier undo/redo (the old data is still around). Obeying immutability fully in C++ is unrealistic, but we can adopt this mindset selectively on critical paths. For example, a "sort without modifying the original data" function:

```cpp
#include <vector>
#include <algorithm>

// Immutable style: return a new container, leave the original data untouched
std::vector<int> sorted_copy(const std::vector<int>& input) {
    std::vector<int> result = input;        // copy
    std::sort(result.begin(), result.end()); // sort the copy
    return result;                           // NRVO elides the copy of the return value
}
```

In modern C++ (especially at -O2/-O3 optimization levels), returning a `std::vector` almost always has its extra copies optimized away by NRVO or move semantics, so the performance cost of the immutable style is smaller than it looks. Benchmarks show that for sorting 1 million elements, `sorted_copy` is only about 1.5% slower than `std::sort` mutating the data in place—and that cost comes mainly from the initial copy of the input, not from copying the return value. In scenarios where you genuinely need to keep the original data, that price is perfectly acceptable.

---

## Practical Applications

### Data Processing Pipeline

Let's build a log-processing pipeline—the filter, transform, reduce trio. It follows the same idea as Unix pipes: each stage does one thing, and data flows from one stage into the next.

```cpp
struct LogEntry {
    std::string level;
    std::string message;
    int timestamp;
};

void demo_pipeline() {
    std::vector<LogEntry> logs = {
        {"ERROR", "Disk full", 100}, {"INFO", "User login", 150},
        {"ERROR", "Network timeout", 250}, {"ERROR", "Database error", 350},
    };

    // Filter: keep only ERROR
    std::vector<LogEntry> errors;
    std::copy_if(logs.begin(), logs.end(), std::back_inserter(errors),
                [](const LogEntry& e) { return e.level == "ERROR"; });

    // Map: extract the messages
    std::vector<std::string> messages(errors.size());
    std::transform(errors.begin(), errors.end(), messages.begin(),
                  [](const LogEntry& e) { return e.message; });

    // Reduce: concatenate
    std::string report = std::accumulate(
        messages.begin(), messages.end(), std::string{"Errors:\n"},
        [](const std::string& acc, const std::string& msg) {
            return acc + "  - " + msg + "\n";
        });
    std::cout << report;
}
```

### Event Filter Chain

A "filter chain" is a series of predicate functions combined so that data must pass every filter to be accepted. This is extremely practical in scenarios like request validation and data checking. Each filter is an independent pure function that can be tested and combined on its own. Need to add a new filtering rule? Write a lambda and drop it into the array—no existing code has to change.

```cpp
struct Request {
    std::string source;
    int priority;
    std::string payload;
};

void demo_filter_chain() {
    using Filter = std::function<bool(const Request&)>;
    auto combine = [](std::vector<Filter> filters) -> Filter {
        return [filters = std::move(filters)](const Request& r) {
            return std::all_of(filters.begin(), filters.end(),
                              [&r](const Filter& f) { return f(r); });
        };
    };

    auto combined = combine({
        [](const Request& r) { return r.priority >= 0 && r.priority <= 10; },
        [](const Request& r) { return r.source == "trusted"; },
        [](const Request& r) { return r.payload.size() <= 1024; },
    });

    std::cout << std::boolalpha;
    std::cout << combined({"trusted", 5, "hello"}) << "\n";    // true
    std::cout << combined({"unknown", 5, "hello"}) << "\n";    // false
}
```

---

## A Ranges Preview—The Ultimate Form of Functional Programming in C++20

Earlier, when we processed data with map/filter/reduce, every operation created a new temporary `std::vector`. If the pipeline has several steps, these intermediate containers take a noticeable toll on performance. Benchmarks show that for a pipeline containing filter and transform, the traditional approach is roughly 16x slower than C++20 Ranges and needs to allocate multiple temporary containers (for 1 million elements, roughly 4 MB of extra memory). The C++20 Ranges library solves this problem with lazy evaluation—a view does not compute its result up front; it computes on demand, as you iterate.

```cpp
#include <ranges>
#include <vector>
#include <iostream>
#include <algorithm>

void demo_ranges_preview() {
    std::vector<int> data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

    // Ranges: a lazy pipeline, no intermediate containers
    auto result = data
        | std::views::filter([](int x) { return x % 2 == 0; })   // evens
        | std::views::transform([](int x) { return x * 2; })      // double them
        | std::views::take(3);                                     // take the first 3

    std::cout << "Ranges result: ";
    for (int x : result) {
        std::cout << x << " ";   // 4 8 12
    }
    std::cout << "\n";
}
```

This pipeline says: from `data`, filter out the even numbers, double them, then take the first three. The key is the `|` operator—it chains multiple view operations into one pipeline. The whole pipeline does nothing when it is built; the computation only really starts when `for (int x : result)` iterates. No intermediate containers, no redundant data copies.

Ranges' `views::filter` and `views::transform` correspond to functional programming's filter and map, `views::take` and `views::drop` to Haskell's `take` and `drop`, and `views::join` to `concat`. You could say Ranges is C++'s official answer to functional data processing. We will unpack the details of the Ranges library in Volume 4.

---

## References

- [STL algorithms - cppreference](https://en.cppreference.com/w/cpp/algorithm)
- [C++20 Ranges - cppreference](https://en.cppreference.com/w/cpp/ranges)
- [Functional programming in C++ - Fluent C++](https://www.fluentcpp.com/2019/01/15/functional-programming-in-cpp/)
