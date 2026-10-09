---
chapter: 3
cpp_standard:
- 14
- 17
- 20
description: Higher-order functions, composition, and currying — functional programming techniques in C++
difficulty: intermediate
order: 5
platform: host
prerequisites:
- 'Chapter 3: Lambda Basics: Elegant Anonymous Functions'
- 'Chapter 3: Deep Dive into Lambda Capture'
- 'Chapter 3: Generic Lambdas and Template Lambdas'
- 'Chapter 3: std::function, std::invoke, and Callable Objects'
reading_time_minutes: 15
related:
- 'C++20 Ranges: Ranges and Views'
tags:
- host
- cpp-modern
- intermediate
- lambda
- 函数对象
title: Functional Programming Patterns
translation:
  source: documents/vol2-modern-features/ch03-lambda/05-functional-patterns.md
  source_hash: d94231283a183686f9b47999ea5091b40e9b34cf43c1ccadd7262ae2e95694f3
  translated_at: '2026-09-27T10:30:33+00:00'
  engine: anthropic
  token_count: 4500
---
# Functional Programming Patterns

Whenever functional programming comes up, plenty of people in the C++ world file it under the Haskell camp's name and figure it has little to do with C++. I used to draw that same line myself, until I actually went back and counted—and found that C++ has been absorbing this set of ideas ever since C++11: lambdas made anonymous functions first-class citizens, `std::function` packs callables of every stripe into a single type, and the `std::algorithm` family is, at heart, a set of map, filter, and reduce variants. C++ just never wrapped these things up in an interface that bills itself as "purely functional."

This is the last article of the chapter, and all the parts are already in hand: lambda captures, `auto` parameters for generic lambdas, and `std::function`'s type erasure—we took them apart one by one over the first four articles. This time we put them to work together: a function can serve as an argument, as a return value, and as something you keep in a container. Let's start with higher-order functions.

---

## Higher-Order Functions—Functions That Take or Return Functions

A higher-order function, as we mean it, is a function that takes or returns functions. Having a function among the parameters counts; returning a function as the result counts; covering both ends counts just the same. In C++, this is built on template parameters or `std::function`, and article 04 just had us take a good look under their hoods. Let's go straight to a practical example: a generic retry mechanism. What we pass in is an operation that may fail, a predicate that decides whether to retry, plus a maximum retry count:

```cpp
#include <iostream>
#include <functional>
#include <random>

// Higher-order function: takes an "operation" and a "decision function" as arguments
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

You have been using the STL's higher-order functions for a long time already: `std::sort` takes a comparison function, `std::transform` takes a transformation function, and `std::find_if` takes a predicate. These algorithms all do the same job—they pull the policy out of the algorithm and hand the decision to the caller. The skeleton of the sort never moves; what counts as bigger or smaller, who goes in front and who goes behind, is decided entirely by which function you pass in.

### Functions That Return Functions

The other half of a higher-order function's repertoire is returning functions. It is a particularly smooth way to build configurable strategy objects—here is a function that returns a filter with a preset threshold, so you can feel it for yourself:

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

One spot to keep an eye on, though: if different branches return lambdas of different types—and every lambda's closure type is one of a kind—the types being returned directly no longer line up. Here is an example that fails to compile:

```cpp
// ❌ Compile error: the lambdas in the different branches have different types
auto make_counter(bool start_high) {
    if (start_high) {
        return []() { return 100; };  // closure type A
    } else {
        return []() { return 0; };    // closure type B
    }
}
```

The error the compiler spits out can scroll a whole screen, and the first time you meet it you will probably freeze for a second. To unify the return type, we bring in the type erasure from article 04, and the tool for that is `std::function`:

```cpp
// ✅ Correct: use std::function to unify the type
std::function<int()> make_counter(bool start_high) {
    if (start_high) {
        return []() { return 100; };
    } else {
        return []() { return 0; };
    }
}
```

The cost is the small bit of runtime overhead that `std::function` brings along—type erasure and possible heap allocation all included—but in most settings we can write that overhead off as negligible. If it really does land on a hot path: the numbers I measured in article 04 put it at 7 to 9 times slower than a direct call.

---

## Function Composition—compose and pipe

Function composition, as we usually mean it, is the style of chaining several functions together, with one function's output feeding exactly into the next function's input. In mathematical notation we write `compose(f, g)(x) = f(g(x))`; the pipeline-style notation is `pipe(g, f)(x) = f(g(x))`—`g` finishes, then `f` takes its turn, following the direction the data flows. The cleanest implementation in C++ is generic lambdas plus `auto` return type deduction, and the skills we banked in article 03 come into play here:

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

// pipe: g first, then f (reads more intuitively)
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

Composing two functions still reads fine; once there are more of them, the nested `compose` calls stop being readable. So let's upgrade to a variadic `compose_all` and tidy up multi-level composition in one shot:

```cpp
// Compose multiple functions: applied right to left
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

// pipe_all: applied left to right (more intuitive)
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

By the way, the `compose_all` and `pipe_all` above take the recursive-expansion route; the C++17 fold expression hasn't been brought in yet. That line `((current = transforms(current)), ...)` in `make_pipeline` from article 03—that's exactly one. `pipe_all` applies functions left to right: the order you read the code in is the order the data flows through, which is why it reads so naturally.

---

## Partial Application—Binding Part of the Parameters

Partial application is the style of fixing part of a function's arguments ahead of time and getting back a new function that waits only for the remaining ones. The standard library stocks `std::bind` for this job, but in modern C++ a lambda is usually the better choice: the code comes out clearer, the error messages come out friendlier, and none of `std::bind`'s odd corner cases tag along. Let's write it with a lambda directly:

```cpp
#include <iostream>
#include <functional>

// Partial application via a lambda
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

    // Fix the first argument to 1
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

Partial application is especially handy in event handling and strategy-pattern settings. You pin certain arguments down at configuration time, and at runtime you only pass in what's left. Compared to solemnly writing out a full-blown strategy class, a partially applied lambda weighs far less.

### Currying—Knowing the Concept Is Enough

Currying and partial application get lumped together all the time, but the two are not the same thing. What currying does is convert a multi-argument function into a chain of single-argument function calls—that is, `f(a, b, c)` gets taken apart into the call chain `f(a)(b)(c)`. Partial application fixes part of the arguments and returns a function with fewer of them. Currying instead has the function take in exactly one argument at a time, hand back the next function once it has it, and count as done only when every argument has been gathered. The three-argument `add` from above makes the contrast easiest to see: partial application pins down the first argument, and the returned function waits for the other two. On the currying route, the function returned by `add(a)` waits for exactly one `b`, and only after `b` is in does `c` get its turn.

Currying is honestly less practical in C++ than partial application. C++ already supports multi-argument function calls, so there is no need to take every function apart into a chain of single-argument ones; what gets used day to day is partial application. So why do we still cover currying? Because it points at one thing: a function can be "specialized" step by step—every argument we supply leaves us holding a newer, more specific function.

---

## map/filter/reduce—The Functional Face of STL Algorithms

Map, filter, and reduce are the three basic operations functional programming uses to process data, and the STL algorithms stock a tool for each: `std::transform` does map's job, `std::copy_if` / `std::remove_if` do filter's job, and `std::accumulate` does reduce's job. You already met map and filter in the earlier code. Reduce counts as a new acquaintance for us, though you have handwritten its job in article 02: that accumulating lambda that used reference capture to pile numbers into `sum` was doing reduction. Chain the three steps together—data flows out of one step's output and into the next step's input—and you have a data processing pipeline, drawn below:

![filter, map, reduce data processing pipeline](./05-functional-patterns-pipeline.drawio)

Let's take all three steps through a complete data processing pipeline for real:

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

    // === Filter: keep only temp_01 readings ===
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

### Wrapping Them into Reusable Functional Utilities

That three-stage style can go one step further: wrap the map and filter logic inside generic lambdas, and a call takes a single line. Let's wrap two of them ourselves:

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

// Chaining example: filter even numbers -> double
std::vector<int> data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
auto evens = functional_filter(data, [](int x) { return x % 2 == 0; });
auto doubled = functional_map(evens, [](int x) { return x * 2; });
```

The drawback of writing it this way sits right out in the open too: every operation creates a new `std::vector`, and once the filter and map stages multiply, temporary containers pop up one after another. How much overhead that adds up to—we'll let the numbers speak at the end of this article. The C++20 Ranges library solves this problem through lazy evaluation: its views are in no hurry to compute results; they produce the data on demand, only when you actually iterate over them.

---

## Thinking in Immutable Data

Functional programming holds one more basic position: try not to modify data—if you want a new result, create new data. The first time you hear that it sounds rather wasteful, but think the position through once and the benefits are all in front of you. If we don't modify data, data races have nowhere to come from—that is where thread safety starts. Fixed input means fixed output, so when you read the code, the behavior is something you can reason about. The old data is always still there; when you want undo and redo, the old versions are still in our hands. Holding fully to immutability in C++ isn't realistic, but we can pick the critical paths where this mindset earns its keep—for instance, writing a "sort without touching the original data" function:

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

In modern C++ (at -O2/O3 optimization levels), the extra copies that come with returning a `std::vector` are almost entirely optimized away by NRVO (named return value optimization) or move semantics, so the cost of the immutable style is smaller than it looks. For a sort of one million elements, I measured `sorted_copy` against a `std::sort` that modifies the data in place: only about 1.5% slower—and that 1.5% goes mostly into the initial copy of the input data, not into copying the return value. In settings where the original data genuinely has to be kept, that is a price we can accept without complaint.

> **Performance data source**: my benchmark in `code/volumn_codes/vol2/ch03-lambda/test_immutability_nrvo.cpp`, run on GCC 15.2.1 with `-O2`.

---

## Practical Applications

### A Data Processing Pipeline

Let's build a log-processing pipeline, still following the filter, transform, and reduce three-stage layout. It carries over the Unix pipeline idea: each stage does only its own job, and the data flows out of one stage and into the next.

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

### An Event Filter Chain

A "filter chain" is the pattern of combining a set of predicate functions: the data has to pass every one of the filters to be accepted. You will find it very smooth to use in request validation and data checking. Each filter is an independent pure function—same input only ever yields same output, and it doesn't touch external state either. We can test any one of them in isolation, or swap any one of them out. Want to add a new filtering rule? Write a lambda and drop it into the array; not a single line of the existing code has to change.

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

## A Ranges Preview—C++20's Lazy Views

Earlier, while we were processing data with map/filter/reduce, every operation created a new temporary `std::vector`; once a pipeline has many steps, those intermediate containers pile up into real overhead. I measured this one too: for a pipeline over one million elements with a filter and a transform, the old style ran about 16 times slower than C++20 Ranges, and it also had to allocate several extra temporary containers for the intermediate results, roughly 4 MB of additional memory. A 16x gap is not small, and the code squares with it: temporary containers get copied layer upon layer, while Ranges views waive every one of those intermediate layers—and lazy evaluation, mentioned just above, is what they lean on.

> **Performance data source**: again my benchmark in `code/volumn_codes/vol2/ch03-lambda/test_ranges_performance.cpp`, run on GCC 15.2.1 with `-O2`.

```cpp
#include <ranges>
#include <vector>
#include <iostream>
#include <algorithm>

void demo_ranges_preview() {
    std::vector<int> data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

    // Ranges: lazy pipeline, no intermediate containers
    auto result = data
        | std::views::filter([](int x) { return x % 2 == 0; })   // even numbers
        | std::views::transform([](int x) { return x * 2; })      // double
        | std::views::take(3);                                     // take the first 3

    std::cout << "Ranges result: ";
    for (int x : result) {
        std::cout << x << " ";   // 4 8 12
    }
    std::cout << "\n";
}
```

This pipeline says three steps: keep the even numbers, double them, take the first three. The part that deserves a second look is the `|` operator, which strings several view operations into one pipeline. The whole pipeline does nothing while it is being built; the computation truly begins only once our `for` loop starts iterating. The intermediate containers are gone, and so are the surplus data copies.

We turned the side-by-side comparison of the two styles into an animation you can step through frame by frame with the step controls: the old style materializes intermediate containers layer by layer, while the Ranges pipeline computes one step only when it reaches it.

<Anim id="ranges-lazy-pipeline" />

Ranges' `views::filter` and `views::transform` correspond to functional programming's filter and map, `views::take` and `views::drop` correspond to Haskell's `take` and `drop`, and `views::join` corresponds to `concat`. Set the mappings side by side and you can more or less see it: Ranges is the functional data-processing scheme C++ has taken into its standard library. Volume 4 will dig into its details for us.

---

## References

- [STL algorithms - cppreference](https://en.cppreference.com/w/cpp/algorithm)
- [C++20 Ranges - cppreference](https://en.cppreference.com/w/cpp/ranges)
