---
chapter: 3
conference: cppcon
conference_year: 2025
cpp_standard:
- 20
- 23
description: 'CppCon 2025 Talk Notes — Mike Shah: Constrained algorithms, view lazy
  evaluation, pipe operator, and `ranges::to`. Includes eager vs. lazy benchmarking,
  infinite ranges, and a C++20/23/26 views version attribution table.'
difficulty: intermediate
order: 3
platform: host
reading_time_minutes: 19
speaker: Mike Shah
tags:
- cpp-modern
- host
- intermediate
- Ranges
talk_title: 'Back to Basics: C++ Ranges'
title: 'Ranges, Views, and Pipelining: The Power of Lazy Evaluation'
video_youtube: https://www.youtube.com/watch?v=Q434UHWRzI0
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/03-back-to-basics-ranges/03-ranges-views-and-composition.md
  source_hash: 19f27b983bc1f91aeae818f0687f092e3b3d29683f970bafd763ab8696b9cb98
  translated_at: '2026-06-24T01:16:05.550397+00:00'
  engine: anthropic
  token_count: 3931
---
# Ranges, Views, and Pipe Composition: The Power of Lazy Evaluation

:::tip
This is the finale of the CppCon 2025 Mike Shah "Back to Basics: C++ Ranges" series. In the previous two parts, we covered the "Loops → Iterators → Algorithms" progression and dissected several classic iterator pitfalls (invalidation, pairing, and argument order). In this part, we dive into the core of Ranges: constrained algorithms, lazy evaluation of views, pipe composition, and `ranges::to` for materializing results back into containers. This post involves many experiments and spans both C++20 and C++23, so compiler flags will switch between `-std=c++20` and `-std=c++23`—which is actually a plot point in itself. Environment: Arch Linux WSL, GCC 16.1.1.
:::

At the end of the last post, Shah concluded with a hyperbolic slide stating "Iterators Must Go." In this post, we will see how Ranges redesigns a safer, more composable interface layer on top of iterators. Let's start with the most fundamental question: **What exactly did Ranges change?**

## A range is still that pair of iterators, but `end` can be a "sentinel"

The underlying definition hasn't changed—a range is still defined by a beginning and an end. However, C++20 gave it a significant extension: **the end can be something of a different type than the beginning, known as a sentinel**<RefLink :id="1" preview="cppreference, Ranges library — sentinel may differ in type from iterator" />.

Why allow different types? Consider a classic example: iterating over a C-style string terminated by `'\0'`. In the traditional iterator model, you have to call `strlen` to calculate the length before you can determine `end`—but you really just need to "keep going until you hit `'\0'`". A sentinel expresses an endpoint that means "stop until a condition is met"; its type can differ from the iterator, as long as they can be compared (`it == sentinel`). This makes traversing "sequences of unknown length" natural—and this is precisely the foundation for "infinite ranges" later on.

## From range-v3 to Standard Ranges: Concepts are the Key Piece

Ranges didn't appear out of nowhere in C++20. Its prototype was Eric Niebler's **range-v3** library<RefLink :id="2" preview="Eric Niebler, range-v3 — C++14 library, prototype of standard Ranges" />, which has been available since the C++14 era. If your current project is stuck on C++14/17, you can use range-v3 to practice—its API is highly similar to the standard library Ranges, so the future migration cost is very low.

So why did the standard library version wait until C++20? **Because the implementation of Ranges relies heavily on concepts**<RefLink :id="3" preview="cppreference, Concepts library (C++20) — constraints enable Ranges" />. Ranges needs to precisely express constraints like "what counts as a range" or "what iterator counts as random-access". Before concepts, these constraints had to be implemented via SFINAE (Substitution Failure Is Not An Error)—resulting in error messages that spanned dozens of lines of template gibberish if you passed the wrong type. Concepts allow constraints to be named and checked early, which was the final missing piece for Ranges to enter the standard.

## Constrained Algorithms: One Less Argument, One Less Opportunity for Error

The most immediate, tangible improvement in Ranges is **constrained algorithms**—the official name on cppreference. They share names with classic algorithms but reside in the `std::ranges::` namespace. The difference is: **classic algorithms require you to pass a pair of iterators `(first, last)`, while the Ranges version only requires passing a container (or any range)**<RefLink :id="4" preview="cppreference, Constrained algorithms — pass the whole range, not iterator pair" />.

```cpp
#include <algorithm>
#include <ranges>
#include <vector>

std::vector<int> v{3, 1, 4, 1, 5, 9};

std::sort(v.begin(), v.end());   // 经典：传一对迭代器
std::ranges::sort(v);            // ranges：传整个容器
```

`ranges::sort(v)` does exactly the same thing as `sort(v.begin(), v.end())`, but it takes two fewer arguments. The benefit isn't just saving keystrokes—recall Pitfall 2 from the previous article, "Mismatched begin/end". **Classic algorithms allow you to mismatch iterators from two different containers, whereas the ranges version doesn't even give you that chance**, because it accepts only a single object. Eliminating a possibility for error is a tangible improvement in safety.

Constrained algorithms also support `span`, custom containers, and anything that satisfies the `std::ranges::range` concept:

```cpp
int arr[] = {3, 1, 4};
std::ranges::sort(arr);                       // 原生数组也行

std::ranges::find_if(v, [](int i) { return i > 4; });
// ranges::find_if 同样返回迭代器（指向找到的元素），
// 用 ranges::end(v) 判断是否没找到
```

:::tip Iterators are not obsolete
Note that `ranges::find_if` still returns an iterator—**which means everything discussed in the previous article about iterators is still relevant**. Issues like iterator invalidation and pairing still exist in ranges; however, the Ranges interface makes it harder to make these mistakes (it doesn't eliminate them, just makes them less likely). We still need iterators in C++26.
:::

## Views: Lazy Evaluation, The Soul of Ranges

Constrained algorithms are just the appetizer; the real killer feature of Ranges is **views**. A view is a **lazy** way to access a range—it does not copy data or pre-calculate results. Instead, when you iterate over it, it **processes one element at a time**<RefLink :id="5" preview="cppreference, Ranges library — views are lazy" />.

Let's compare the two styles. `std::ranges::sort(v)` is **eager evaluation**—it sorts the entire range immediately and on the spot, returning only when finished. In contrast, `std::views::filter(...)` is **lazy evaluation**—it simply sets up a "filtering pipeline" and performs no computation until you actually iterate over it. It only hands you an element when you encounter one that meets the criteria during iteration.

```cpp
#include <ranges>
#include <vector>
#include <iostream>

std::vector<int> v{1, 2, 3, 4, 5, 6};

// 搭管道：此时 filter 一个元素都没处理
auto gt3 = v | std::views::filter([](int x) { return x > 3; });

// 遍历时才真正执行过滤
for (int x : gt3) {
    std::cout << x << ' ';   // 4 5 6
}
```

That `|` is the **pipe operator**, borrowed from Unix pipes—it feeds the range on the left to the view adaptor (range adaptor) on the right. We can chain multiple views together, composing them like a pipeline:

```cpp
auto result = v
    | std::views::filter([](int x) { return x > 1; })    // 过滤
    | std::views::transform([](int x) { return x * x; }) // 变换
    | std::views::take(3);                                // 只取前 3 个
// 遍历 result 时：3²=9, ... 一路惰性求值
```

## Experiment: Eager vs Lazy, How Big is the Difference?

Simply saying "lazy is more efficient" isn't intuitive enough, so let's run a benchmark. We'll create a `vector` with ten million elements and compare two approaches: **eager**—materializing the filtered result into a temporary `vector` using `ranges::to` first, then iterating to sum; **lazy**—iterating directly over `views::filter` without constructing a temporary container.

```cpp
#include <algorithm>
#include <ranges>
#include <vector>
#include <numeric>
#include <chrono>
#include <iostream>

int main()
{
    constexpr int N = 10'000'000;
    std::vector<int> v(N);
    std::iota(v.begin(), v.end(), 0);
    const auto pred = [](int x) { return x > N / 2; };

    // EAGER：物化过滤结果到一个临时 vector，再求和
    long long se = 0;
    auto t0 = std::chrono::high_resolution_clock::now();
    {
        auto tmp = v | std::views::filter(pred) | std::ranges::to<std::vector<int>>();
        for (int x : tmp) se += x;
    }
    auto t1 = std::chrono::high_resolution_clock::now();

    // LAZY：直接遍历 view，不建临时容器
    long long sl = 0;
    auto t2 = std::chrono::high_resolution_clock::now();
    for (int x : v | std::views::filter(pred)) sl += x;
    auto t3 = std::chrono::high_resolution_clock::now();

    auto ms_e = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    auto ms_l = std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t2).count();
    std::cout << "sum eager=" << se << " lazy=" << sl << "\n";
    std::cout << "eager (ranges::to 临时 + 求和): " << ms_e << " ms\n";
    std::cout << "lazy  (直接遍历 view):       " << ms_l << " ms\n";
}
```

GCC 16.1.1, `-std=c++23 -O2`:

```bash
❯ g++ -std=c++23 -O2 -Wall bench.cpp -o bench && ./bench
sum eager=37499992500000 lazy=37499992500000
eager (ranges::to 临时 + 求和): 23 ms
lazy  (直接遍历 view):       7 ms
```

Both approaches yield the exact same sum (`37499992500000`, verification passed), but **the eager version took 23ms, while the lazy version took only 7ms—over 3x faster**. Furthermore, the lazy version**did not allocate a temporary `vector` with millions of elements**. The eager version is slow for two reasons: first, it has to copy five million matching elements into a temporary vector (involving many `push_back` calls and potential reallocations), and second, it performs an extra complete traversal (materializing first, then summing, effectively traversing twice). The lazy version traverses only once, filtering and summing on the fly. Filtered-out elements are skipped immediately, leaving no trace of any copying overhead.

:::tip How to visually witness "laziness"
To intuitively grasp the concept of "building a pipeline without execution, triggering execution only upon traversal," there is a simple method: add a `std::cout` statement inside the lambdas for both `filter` and `transform`, then **build the pipeline without traversing it**—you will notice that nothing is printed. The moment you write `for (auto x : pipeline)`, each element will **traverse the entire pipeline before the next one is processed**: the first element goes through `filter`, enters `transform` only if it passes, then moves to `take`... This is a single element flowing from start to finish, rather than filtering all elements first and then transforming them. This is the lazy execution model, and it is the reason why "short-circuiting" works later on.
:::

## Infinite Ranges: The Magic Enabled by Laziness

Lazy evaluation unlocks a powerful capability—**infinite ranges**. If evaluation were eager, infinite sequences would be impossible to represent (you cannot pre-calculate an infinite number of elements). But with laziness, as long as you don't actually traverse the "infinity," it can exist.

`std::views::iota(x)` generates an **infinite incrementing** sequence starting from `x`<RefLink :id="6" preview="cppreference, std::views::iota — infinite counting range factory (C++20)" />. When combined with `take` to truncate it, it can be used safely:

```cpp
// 生成 0², 1², 2², ... 的前 5 个
for (int x : std::views::iota(0)
            | std::views::transform([](int n) { return n * n; })
            | std::views::take(5)) {
    std::cout << x << ' ';
}
```

```bash
❯ g++ -std=c++23 -O2 iota.cpp -o iota && ./iota
0 1 4 9 16
```

`iota(0)` generates an infinite sequence (0, 1, 2, 3, ...), but `take(5)` truncates it to just five elements. Lazy evaluation guarantees that the infinite portion beyond `take` **is never evaluated**. This pattern of "defining an infinite source and then using a view to limit how much is used" is extremely handy when dealing with streaming data or generating sequences. `iota` is a range factory introduced in C++20.

## Pipeline Short-Circuiting: Efficiency Brought by Lazy Evaluation

Another direct benefit of laziness is **short-circuiting**. When you chain multiple filters together, if an element is filtered out at one stage, **subsequent stages will not process it at all**—thanks to the execution model where a single element flows through the entire pipeline.

Shah's example involves filtering a collection of strings: first filtering for those "starting with M", then for those "with a length greater than 4". If a string does not start with M, it gets rejected by the first filter, and the predicate of the second filter **is never invoked**. Let's quantify this effect by adding a counter to the filter's predicate to compare the number of predicate invocations between a "full traversal" and a version with `take(5)` for early termination:

```cpp
long long calls_all = 0, calls_take = 0;
auto cp_all  = [&](int) { ++calls_all;  return true; };
auto cp_take = [&](int) { ++calls_take; return true; };

for ([[maybe_unused]] int x : v | std::views::filter(cp_all)) {}
for ([[maybe_unused]] int x : v | std::views::filter(cp_take) | std::views::take(5)) {}

std::cout << "filter 谓词调用次数: 全量=" << calls_all
          << "  加 take(5)=" << calls_take << "\n";
```

On a `v` with ten million elements:

```bash
filter 谓词调用次数: 全量=10000000  加 take(5)=6
```

**Ten million vs six**. After adding `take(5)`, the predicate is invoked only six times (six checks are needed to obtain five elements) before stopping. The remaining ten million evaluations are lazily short-circuited. If you only care about the "first few elements that satisfy the condition," this approach is more than an order of magnitude faster than "filtering a complete list first and then taking the first five"—because the latter (eager evaluation) must traverse all elements through the predicate.

## `ranges::to`: Materializing lazy results back into containers (C++23)

Views are lazy, but often you ultimately want a **concrete container** (for example, for multiple random access or to pass to an interface that only accepts containers). Materializing a view into a container is the job of `std::ranges::to`:

```cpp
auto collected = std::vector{1, 2, 3, 4, 5, 6}
    | std::views::filter([](int x) { return x % 2 == 0; })
    | std::ranges::to<std::vector<int>>();
// collected == {2, 4, 6}
```

```bash
❯ ./ranges_to_demo
ranges::to (evens): 2 4 6
```

:::warning Watch out for a version trap: Shah missed a label
In his talk, Shah says, "we have `ranges::to`," implying it has been available since C++20 alongside the constrained algorithms. **It hasn't.** `std::ranges::to` only entered the standard in **C++23** (proposal P1206R7, feature test macro `__cpp_lib_ranges_to_container=202202L`)<RefLink :id="7" preview="cppreference, std::ranges::to (since C++23) — P1206R7" />, arriving one standard later than the C++20 constrained algorithms.

I compiled the same program under both standards, and the results speak for themselves:

```cpp
auto col = v | std::views::filter(pred) | std::ranges::to<std::vector<int>>();
```

```bash
❯ g++ -std=c++20 probe.cpp
probe.cpp:12:78: error: ‘to’ is not a member of ‘std::ranges’
   12 |     ... | std::ranges::to<std::vector<int>>();
      |                                              ^~

❯ g++ -std=c++23 probe.cpp && echo OK
OK
```

Compiling with `-std=c++20` results in `'to' is not a member of 'std::ranges'`; it only compiles with `-std=c++23`. Therefore, if your project is still on C++20, `ranges::to` is unavailable—you must manually `reserve` and loop with `push_back`, or use `std::copy` with an inserter. The minimum toolchain versions are approximately GCC 14, Clang 18+libc++, or MSVC Visual Studio 2022 17.5.

:::tip Pipe support is also C++23, not a "later addition"
The pipe syntax `r | ranges::to<C>()` comes from proposal P2387R3. It landed in C++23 **simultaneously** with P1206, not as a "patch" added after `ranges::to` was introduced. So, you don't need to worry about "pipe support being an afterthought"—it has been a complete part of C++23 from the beginning.
:::

## Views Cheat Sheet: Which Standard Introduced What

This is another key focus of this adaptation. Views continued to expand after C++20; C++23 added a significant batch, and C++26 is still adding more. Shah broadly referred to `drop_while`, `chunk_by`, `zip`, and `zip_transform` as "new things" in his talk, but **didn't mark the versions**—these actually belong to different standards, and mixing them up will cause compilation errors. I have listed the version attributions verified against cppreference:

| Standard | Views (Representative) |
|------|------|
| **C++20** | `filter`, `transform`, `take`, `drop`, `take_while`, `drop_while`, `reverse`, `join`, `split`, `keys`, `values`, `elements`, `iota` (unbounded), `lazy_split`, `common`, `counted`, `all` |
| **C++23** | `zip`, `zip_transform`, `chunk`, `chunk_by`, `slide`, `join_with`, `stride`, `cartesian_product`, `as_const`, `as_rvalue`, `enumerate`, `adjacent`, `adjacent_transform`, `pairwise`, `pairwise_transform`, `repeat` (factory) |
| **C++26** | `cache_latest` (along with `concat`, `as_input`, `indices`, etc., currently in progress) |

:::warning Versions Easy to Misremember

- **`drop_while` is C++20**, not C++23—don't lump it into C++23 just because it "looks new."
- **`chunk_by`, `zip`, and `zip_transform` are C++23** (`zip`/`zip_transform` from P2210, `chunk_by` from P2442) <RefLink :id="8" preview="cppreference, std::views::zip / chunk_by — C++23, P2210 / P2442" />, requiring `-std=c++23`.
- **`as_rvalue` is C++23**—it is often mistaken for C++26 because it sounds "very new," but it arrived with the `zip` batch.
- **`join` is C++20, but `join_with` is C++23**—don't mistake the `_with` suffixed versions for C++20.
:::

Let's test a few C++23 views to experience their power. `chunk_by` groups elements by continuous equality:

```cpp
std::vector<int> run{1, 1, 2, 3, 3, 3, 4, 5};
for (auto ch : run | std::views::chunk_by([](int a, int b) { return a == b; })) {
    std::cout << '[';
    for (int x : ch) std::cout << x;
    std::cout << ']';
}
```

```bash
❯ g++ -std=c++23 -O2 chunk.cpp -o chunk && ./chunk
[11][2][333][4][5]
```

Consecutive equal elements are grouped together. `zip` traverses multiple ranges in parallel like a zipper, and its length is determined by the shortest range:

```cpp
std::vector<int>  a{1, 2, 3};
std::vector<char> b{'x', 'y', 'z'};
for (auto [x, y] : std::views::zip(a, b)) {
    std::cout << '(' << x << y << ')';
}
```

```bash
❯ ./zip_demo
(1x)(2y)(3z)
```

In the past, traversing two containers in parallel required manually managing two indices and worrying about out-of-bounds errors. `zip` turns this into a one-liner pipeline, and we can even unpack the results directly using structured binding. These new C++23 views significantly expand the boundaries of "expressing data processing pipelines with pipes."

## Custom Iterators: An Iterator is Just a "Pseudo-Pointer with Replaceable Forward Logic"

:::tip This section is advanced and can be skipped
If you want a more solid understanding of "what an iterator actually is," you can write one yourself. Below is a minimal singly-linked list node iterator—it proves that: **the essence of an iterator is just an object that can be `++`'d, `*`'d, and compared; the forward logic is completely replaceable.**
:::

```cpp
struct Node
{
    int data;
    Node* next;
};

struct NodeIterator
{
    Node* current;

    int& operator*() const { return current->data; }
    NodeIterator& operator++() { current = current->next; return *this; }
    bool operator!=(const NodeIterator& other) const { return current != other.current; }
};
```

Once these four operations are in place (dereference, prefix `++`, inequality comparison, and default construction/copying), it can serve as a forward iterator. We can plug it into range-based `for` loops and constrained algorithms. Whether the internal structure is a linked list, a tree, or a graph, externally it can masquerade as "a pseudo-pointer that walks step-by-step." This is the power of iterator abstraction—and it explains why Ranges chose to build upon iterators rather than reinventing the wheel.

## Pitfall Checklist: Watch Out with Ranges

Finally, let's round up the scattered pitfalls from this three-part series to help you review. Ranges make many errors **harder to commit**, but they haven't eliminated them:

1. **`std::advance` performs no bounds checking**—Going out of bounds results in a segmentation fault. In generic code, check with `std::distance` first.
2. **`begin`/`end` must come from the same container**—`process(f().begin(), f().end())` is undefined behavior (UB); store them in named variables.
3. **`list`/`set` iterators do not support `+n`/`-n`**—Use member `sort()` for sorting; don't force `std::sort` onto them.
4. **Views do not own data**—A view is just a window into the underlying range. If the underlying container becomes invalid (reallocation, rehash, destruction), the view dangles. **Never let a view's lifetime exceed the container it observes.**
5. **`ranges::to` without `take` can exhaust memory**—Materializing an infinite `iota` directly via `ranges::to<vector>()` will materialize indefinitely and blow your memory; always constrain it with `take` first.
6. **`reverse` with single-pass iterator views might fail to compile**—Some views require bidirectional iterators. Using `reverse` on a `forward_list` view (single-direction) will result in a compilation error.
7. **Diagnostic messages aren't necessarily shorter**—Ranges use concepts to intercept errors earlier and more accurately, but deeply nested constraint diagnostics can still be lengthy. The real benefit is "making certain bugs unwriteable," not "fewer lines of error output."

## What We've Learned Across Three Articles

From the indexed loops in the first article to the view pipelines in this one, we have traced the evolution of abstraction for "traversing and processing data" in C++. The core of this article boils down to a few points: constrained algorithms mean **passing fewer parameters and mismatching fewer iterator pairs**; lazy evaluation is the soul of Ranges—it **doesn't copy, doesn't pre-calculate, and threads a single element through the entire pipeline during traversal**. Benchmarks show it's over 3x faster than eager materialization (7ms vs 23ms) while saving memory. Laziness enables **infinite ranges** (`iota`) and **short-circuiting** (adding `take(5)` reduces predicate calls from 10 million to six); `ranges::to` materializes lazy results back into containers, but **it is C++23**, so don't be misled by the tone of "now that we have ranges::to"; views are still evolving, with `chunk_by`/`zip`/`zip_transform` arriving in C++23, and `cache_latest` etc. in C++26.

Looking back at Shah's statement that "algorithms are essentially loops"—now we can complete it: the goal of modern C++ is precisely **to free you from writing those loops by hand**. Use constrained algorithms to replace hand-written sorting/searching loops, and use view pipelines to replace multi-pass "filter → transform → collect" loops. This brings code closer to "describing what you want" rather than "describing how to do it." This is the design philosophy of Ranges.

If you want to go deeper, here are a few directions: the concepts article in vol4 will help you understand the constraint system behind ranges; the perfect forwarding and SIMD content in vol6 (Performance) share the same lineage as views' "avoid unnecessary copies"; cppreference's [Ranges library](https://en.cppreference.com/w/cpp/ranges) and [Constrained algorithms](https://en.cppreference.com/w/cpp/algorithm/ranges) are the most authoritative cheat sheets. Ranges isn't perfect—issues like iterator invalidation still exist, it just makes them harder to trigger—but it has indeed made "writing better, safer, higher-performance data processing code" significantly smoother than in the C++11 era.

<ReferenceCard title="References">
  <ReferenceItem
    :id="1"
    author="cppreference.com"
    title="Ranges library (since C++20)"
    :year="2026"
    url="https://en.cppreference.com/w/cpp/ranges"
    chapter="sentinel may differ from iterator type"
  />
  <ReferenceItem
    :id="2"
    author="Eric Niebler"
    title="range-v3 (C++14 library)"
    :year="2014"
    url="https://github.com/ericniebler/range-v3"
    chapter="Prototype for standard Ranges"
  />
  <ReferenceItem
    :id="3"
    author="cppreference.com"
    title="Concepts library (since C++20)"
    :year="2026"
    url="https://en.cppreference.com/w/cpp/concepts"
    chapter="Concepts are the key piece for Ranges"
  />
  <ReferenceItem
    :id="4"
    author="cppreference.com"
    title="Constrained algorithms (since C++20)"
    :year="2026"
    url="https://en.cppreference.com/w/cpp/algorithm/ranges"
    chapter="Pass whole range instead of iterator pairs"
  />
  <ReferenceItem
    :id="5"
    author="cppreference.com"
    title="Ranges library — Views (lazy)"
    :year="2026"
    url="https://en.cppreference.com/w/cpp/ranges"
    chapter="Lazy evaluation of views"
  />
  <ReferenceItem
    :id="6"
    author="cppreference.com"
    title="std::views::iota (since C++20)"
    :year="2026"
    url="https://en.cppreference.com/w/cpp/ranges/iota_view"
    chapter="Infinite counting range factory"
  />
  <ReferenceItem
    :id="7"
    author="cppreference.com"
    title="std::ranges::to (since C++23)"
    :year="2024"
    url="https://en.cppreference.com/w/cpp/ranges/to"
    chapter="P1206R7 / __cpp_lib_ranges_to_container=202202L"
  />
  <ReferenceItem
    :id="8"
    author="cppreference.com"
    title="std::views::zip / zip_transform / chunk_by (C++23)"
    :year="2026"
    url="https://en.cppreference.com/w/cpp/ranges/zip_view"
    chapter="P2210 (zip) / P2442 (chunk_by)"
  />
  <ReferenceItem
    :id="9"
    author="WG21"
    title="P2387R3: Pipe support for user-defined range adaptors"
    :year="2022"
    url="https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2022/p2387r3.html"
    chapter="range_adaptor_closure (landed alongside C++23)"
  />
  <ReferenceItem
    :id="10"
    author="Mike Shah"
    title="Back to Basics: C++ Ranges — CppCon 2025"
    :year="2025"
    url="https://www.youtube.com/watch?v=Q434UHWRzI0"
  />
</ReferenceCard>
