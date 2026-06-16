---
chapter: 3
cpp_standard:
- 14
- 17
- 20
description: Higher-order functions, composition, currying — functional programming
  techniques in C++
difficulty: intermediate
order: 5
platform: host
prerequisites:
- 'Chapter 3: Lambda 基础'
- 'Chapter 3: std::function 与可调用对象'
reading_time_minutes: 15
related:
- 卷四：Ranges 库深入
tags:
- host
- cpp-modern
- intermediate
- lambda
- 函数对象
title: Functional Programming Patterns
translation:
  source: documents/vol2-modern-features/ch03-lambda/05-functional-patterns.md
  source_hash: 5e2df0a7bb75872d206c0956cc8877b06a0233ab93acccb0954b024d95d25f44
  translated_at: '2026-06-16T03:57:27.366787+00:00'
  engine: anthropic
  token_count: 3834
---
# Functional Programming Patterns

## Introduction

When it comes to functional programming, many C++ developers' first reaction might be: "Isn't that stuff for the Haskell crowd? What does it have to do with C++?" In reality, C++ has been absorbing functional programming concepts since C++11—lambdas are anonymous functions that are first-class citizens, `std::function` is a higher-order type, and the `std::ranges` series is essentially a variation of map/filter/reduce. It's just that C++ doesn't wrap these things in a "purely functional" interface.

In this chapter, we will look at practical functional programming patterns in C++—higher-order functions, function composition, partial application, and how to use STL algorithms to write functional-style data processing pipelines. Finally, we will preview C++20's Ranges library, which can be considered the "ultimate form" of functional programming in C++.

> **Learning Objectives**
>
> - Understand the concept of higher-order functions and implement them in C++
> - Master function composition (compose/pipe) techniques
> - Learn to implement map/filter/reduce patterns using STL algorithms
> - Understand the implementation of currying and partial application in C++
> - Establish a basic understanding of C++20 Ranges

---

## Higher-Order Functions—Functions that Accept or Return Functions

Higher-order functions are the cornerstone of functional programming. The definition is simple: either the parameter is a function, or the return value is a function, or both. In C++, higher-order functions are implemented via template parameters or `std::function`.

Let's look at a practical example—a generic retry mechanism. Its parameters include an operation that might fail, a predicate to determine whether a retry is needed, and the maximum number of retries:

```cpp
template <typename Op, typename Pred>
auto retry(Op operation, Pred should_retry, int max_attempts) {
    for (int i = 0; i < max_attempts; ++i) {
        auto result = operation();
        if (!should_retry(result)) {
            return result;
        }
    }
    throw std::runtime_error("Operation failed after max attempts");
}

// Usage:
auto connect = [&]() { return try_connect(); };
auto check = [](auto& status) { return status != success; };
retry(connect, check, 3);
```

You've already used plenty of higher-order functions in the STL—`std::sort` accepts a comparison function, `std::transform` accepts a transformation function, and `std::find_if` accepts a predicate. The common feature of these functions is "extracting strategy from the algorithm and leaving it to the caller." This is the core value of higher-order functions.

### Functions that Return Functions

Higher-order functions don't just "accept functions"; they can also "return functions." This pattern is particularly useful when creating configurable strategy objects. For example, returning a filter with a preset threshold:

```cpp
auto make_threshold_filter(int threshold) {
    return [threshold](int value) { return value > threshold; };
}

auto filter = make_threshold_filter(10);
filter(5);  // false
filter(15); // true
```

However, note that if different branches return different types of lambdas, since each lambda's closure type is unique, returning them directly will cause a type mismatch. For example:

```cpp
auto get_filter(bool use_high) {
    if (use_high) {
        return [](int x) { return x > 10; }; // Type A
    } else {
        return [](int x) { return x > 5; };  // Type B
    }
    // Error: return types differ!
}
```

This situation requires using `std::function` for type erasure to unify the return type:

```cpp
std::function<bool(int)> get_filter(bool use_high) {
    if (use_high) {
        return [](int x) { return x > 10; };
    } else {
        return [](int x) { return x > 5; };
    }
}
```

The cost is that `std::function` introduces a slight runtime overhead (type erasure and possible heap allocation), but in most scenarios, this overhead is negligible.

---

## Function Composition—compose and pipe

Function composition is the process of chaining multiple functions together, where the output of the former becomes the input of the latter. Mathematically, $(f \circ g)(x) = f(g(x))$; in pipeline style, `pipe(f, g)(x)` means applying $g$ first, then $f$.

The cleanest way to implement function composition in C++ is by using generic lambdas and `decltype(auto)` return type deduction:

```cpp
auto compose = [](auto f, auto g) {
    return [f, g](auto... args) {
        return f(g(args...));
    };
};

auto add_one = [](int x) { return x + 1; };
auto times_two = [](int x) { return x * 2; };

auto composed = compose(times_two, add_one);
composed(3); // (3 + 1) * 2 = 8
```

Composing two functions is fairly simple, but when composing multiple functions, nested `compose` calls make the code hard to read. A more elegant approach is to write a variadic version of `pipe`:

```cpp
template <typename... Funcs>
auto pipe(Funcs... funcs) {
    return [funcs...](auto initial_value) {
        // C++17 fold expression: apply functions left-to-right
        return (initial_value | ... | funcs);
    };
}

// Usage:
auto pipeline = pipe(filter_even, times_two, take_first_3);
pipeline(data);
```

C++17's fold expression makes the implementation of variadic templates particularly compact. `pipe` applies functions from left to right—first `filter_even`, then `times_two`, finally `take_first_3`—the direction of data flow matches the order of code writing, making it very natural to read.

---

## Partial Application—Binding Some Arguments

Partial application refers to "presetting some arguments of a function and returning a new function that only needs the remaining arguments." The C++ standard library provides `std::bind`, but in modern C++, lambdas are usually the better choice—the code is clearer, error messages are friendlier, and it avoids the weird edge cases of `std::bind`.

```cpp
// Traditional std::bind approach (not recommended)
auto bound_add = std::bind(add, 10, std::placeholders::_1);

// Modern lambda approach (recommended)
auto partial_add = [](int x) { return add(10, x); };

// Practical example: creating a timer
auto create_timer = [](auto interval, auto callback) {
    return [interval, callback]() {
        start_timer(interval, callback);
    };
};

auto sec_5_timer = create_timer(5s, [] { log("5s passed"); });
```

Partial application is particularly useful in event handling and strategy patterns—you can fix certain parameters during the configuration phase and pass only the remaining parameters at runtime. Compared to writing a full strategy class, a partially applied lambda is much lighter.

### Currying—Understand the Concept Only

Currying and partial application are often confused, but they are different concepts. Currying refers to converting a multi-argument function into a chain of single-argument function calls: `f(a, b, c)` becomes `f(a)(b)(c)`. Partial application fixes some arguments and returns a function with fewer arguments, while currying makes a function accept only one argument at a time and return the next function until all arguments are gathered.

Honestly, the practicality of currying in C++ is not as good as partial application—C++ itself supports multi-argument function calls, so there is no need to split all functions into single-argument chains. Partial application is the more commonly used pattern. The significance of understanding currying is that it reveals a core idea of functional programming: functions themselves are first-class citizens that can be gradually "specialized."

---

## map/filter/reduce—Functional Style with STL Algorithms

map, filter, and reduce are the "three axes" of functional programming data processing. C++ STL algorithms provide corresponding tools: `std::transform` corresponds to map, `std::copy_if` / `std::remove_if` corresponds to filter, and `std::accumulate` corresponds to reduce.

Let's use a complete data processing pipeline to demonstrate these three operations:

```cpp
std::vector<int> input = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

// 1. Map: square each number
std::vector<int> squared;
squared.reserve(input.size());
std::transform(input.begin(), input.end(), std::back_inserter(squared),
               [](int x) { return x * x; });

// 2. Filter: keep only even numbers
std::vector<int> evens;
evens.reserve(squared.size());
std::copy_if(squared.begin(), squared.end(), std::back_inserter(evens),
             [](int x) { return x % 2 == 0; });

// 3. Reduce: calculate sum
int sum = std::accumulate(evens.begin(), evens.end(), 0);

// Result: 4 + 16 + 36 + 64 + 100 = 220
```

### Encapsulating into Reusable Functional Tools

The three-stage writing style above can be encapsulated into generic lambda tools to make the code more functional:

```cpp
auto map = [](auto fn) {
    return [fn](const auto& container) {
        std::vector<std::invoke_result_t<decltype(fn),
            typename decltype(container)::value_type>> result;
        result.reserve(container.size());
        std::transform(container.begin(), container.end(),
                      std::back_inserter(result), fn);
        return result;
    };
};

auto filter = [](auto pred) {
    return [pred](const auto& container) {
        using T = typename decltype(container)::value_type;
        std::vector<T> result;
        std::copy_if(container.begin(), container.end(),
                    std::back_inserter(result), pred);
        return result;
    };
};

auto reduce = [](auto fn, auto init) {
    return [fn, init](const auto& container) {
        return std::accumulate(container.begin(), container.end(), init, fn);
    };
};

// Pipeline usage:
auto result = reduce(std::plus{}, 0)(
                filter([](int x) { return x % 2 == 0; })(
                  map([](int x) { return x * x; })(input)
                )
              );
```

The disadvantage of this approach is that each operation creates a new `std::vector`—multiple filters and maps will produce multiple temporary containers. Performance tests show that a filter+transform pipeline with 1 million elements is about 16 times slower than C++20 Ranges and allocates an additional ~4 MB of memory for intermediate containers. C++20's Ranges library solves this problem through lazy evaluation, which we will mention shortly.

---

## Immutable Data Thinking

A core principle of functional programming is to try not to modify data, but to create new data. This sounds wasteful, but it has several tangible benefits—no data races (the starting point for thread safety), easier to reason about code behavior (deterministic input leads to deterministic output), and easier to implement undo/redo (old data is still there). Adhering strictly to immutable principles in C++ is unrealistic, but we can selectively adopt this mindset on critical paths. For example, writing a "sort without modifying original data" function:

```cpp
auto sorted_copy = [](const auto& container, auto compare) {
    auto result = container; // One copy
    std::sort(result.begin(), result.end(), compare);
    return result; // NRVO/move semantics
};

// Usage:
auto original = std::vector{3, 1, 2};
auto sorted = sorted_copy(original, std::less{});
// original is still {3, 1, 2}
```

In modern C++ (especially at -O2/O3 optimization levels), returning a local `std::vector` is almost always optimized by NRVO or move semantics to eliminate extra copies, so the performance overhead of the immutable style isn't as large as it looks. Performance tests show that for sorting 1 million elements, `sorted_copy` is only about 1.5% slower than directly modifying the original data with `std::sort`—this overhead comes mainly from the initial copy of the input data, not the return value copy. In scenarios where the original data indeed needs to be preserved, this cost is completely acceptable.

---

## Practical Applications

### Data Processing Pipeline

Let's build a log processing pipeline—filter, transform, reduce. This is in line with the Unix pipeline philosophy: each stage does one thing, and data flows from one stage to the next.

```cpp
struct LogEntry { std::string msg; int level; };

// 1. Filter: keep only error logs
auto is_error = [](const LogEntry& e) { return e.level >= 4; };
auto errors = filter(is_error)(raw_logs);

// 2. Transform: extract messages
auto get_msg = [](const LogEntry& e) { return e.msg; };
auto messages = map(get_msg)(errors);

// 3. Reduce: concatenate with newline
auto join = [](std::string acc, const std::string& msg) {
    return acc.empty() ? msg : acc + "\n" + msg;
};
auto report = reduce(join, "")(messages);
```

### Event Filter Chain

A "filter chain" is a series of predicate functions combined together; data must pass all filters to be accepted. This is very useful in scenarios like request validation and data verification. Each filter is an independent pure function that can be tested and combined individually. Need to add a new filtering rule? Just write a lambda and add it to the array; no need to modify any existing code.

```cpp
template <typename T>
class FilterChain {
public:
    void add_filter(std::function<bool(const T&)> filter) {
        filters.push_back(std::move(filter));
    }

    bool validate(const T& data) const {
        return std::all_of(filters.begin(), filters.end(),
                          [&data](auto& f) { return f(data); });
    }

private:
    std::vector<std::function<bool(const T&)>> filters;
};

// Usage:
FilterChain<User> user_validator;
user_validator.add_filter([](const User& u) { return u.age >= 18; });
user_validator.add_filter([](const User& u) { return !u.name.empty(); });

if (user_validator.validate(new_user)) {
    register_user(new_user);
}
```

---

## Ranges Preview—The Ultimate Form of C++20 Functional

Earlier when we used map/filter/reduce to process data, each operation created a new `std::vector` temporary object. If the pipeline has multiple steps, these intermediate containers can cause significant performance overhead. Performance tests show that for pipelines containing filter and transform, traditional methods are about 16 times slower than C++20 Ranges and require allocating multiple temporary containers (for 1 million elements, additional memory is about 4 MB). C++20's Ranges library solves this problem through "lazy evaluation"—views do not calculate results immediately, but calculate on-demand when you iterate.

```cpp
#include <ranges>
#include <algorithm>
#include <vector>

namespace views = std::views;

std::vector<int> nums = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

auto result = nums
    | views::filter([](int x) { return x % 2 == 0; }) // Keep evens
    | views::transform([](int x) { return x * 2; })  // Double them
    | views::take(3);                                 // Take first 3

// result is a view, not a container
// Calculation happens here:
std::vector<int> output(result.begin(), result.end()); // {4, 8, 12}
```

This pipeline expresses: filter even numbers from `nums`, double them, then take the first three. The key is the pipe `|` operator—it chains multiple view operations into a pipeline. The pipeline does nothing when built; it only truly starts calculating when `result` is iterated. No intermediate containers, no redundant data copying.

Ranges' `std::views::filter` and `std::views::transform` correspond to functional programming's filter and map, `std::views::take` and `std::views::drop` correspond to Haskell's `take` and `drop`, and `std::accumulate` corresponds to `foldl`. It can be said that Ranges is C++'s official answer to functional data processing. We will dive deeper into the details of the Ranges library in Volume IV.

---

## Summary

Functional programming isn't about using C++ to write Haskell—it's about borrowing useful ways of thinking and patterns from functional programming to make C++ code clearer, easier to test, and easier to compose. Core takeaways:

- Higher-order functions are functions that accept or return functions; STL algorithms are classic examples.
- Function composition uses `compose`/`pipe` to chain multiple functions into a pipeline; C++17's fold expression makes the variadic version very compact.
- Partial application uses lambdas to fix some arguments, which is clearer and safer than `std::bind`.
- map/filter/reduce are implemented with `std::transform`/`std::copy_if`/`std::accumulate` and are the "three axes" of data processing.
- Immutable data thinking can reduce side effects and improve thread safety, but should be used selectively.
- C++20 Ranges solves the intermediate container problem through lazy evaluation and is the ultimate form of functional data processing.

## Reference Resources

- [STL algorithms - cppreference](https://en.cppreference.com/w/cpp/algorithm)
- [C++20 Ranges - cppreference](https://en.cppreference.com/w/cpp/ranges)
- [Functional programming in C++ - Fluent C++](https://www.fluentcpp.com/2019/01/15/functional-programming-in-cpp/)
