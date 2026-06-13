---
title: 'Ranges, Views, and Pipeline Composition: The Power of Lazy Evaluation'
description: 'CppCon 2025 Talk Notes — Mike Shah: Constrained algorithms, view lazy
  evaluation, pipe operator, ranges::to, plus eager vs. lazy benchmark comparisons,
  infinite ranges, and a views version attribution table (C++20/23/26)'
conference: cppcon
conference_year: 2025
talk_title: 'Back to Basics: C++ Ranges'
speaker: Mike Shah
video_youtube: https://www.youtube.com/watch?v=Q434UHWRzI0
tags:
- cpp-modern
- host
- intermediate
- Ranges
difficulty: intermediate
platform: host
cpp_standard:
- 20
- 23
chapter: 3
order: 3
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/03-back-to-basics-ranges/03-ranges-views-and-composition.md
  source_hash: 1a4d9d53040a5421050b3d5066124dd1f0ad9a30db9364f11086d6e1d865fb90
  translated_at: '2026-06-13T02:14:47.820594+00:00'
  engine: anthropic
  token_count: 3928
---
# Ranges, Views, and Pipeline Composition: The Power of Lazy Evaluation

:::tip
This is the finale of Mike Shah's "Back to Basics: C++ Ranges" series at CppCon 2025. In the first two parts, we traced the path from "loops → iterators → algorithms" and dissected the classic iterator pitfalls (invalidation, mismatching pairs, and argument order). In this part, we dive into the core of Ranges: constrained algorithms, lazy evaluation of views, pipeline composition, and materializing results back into containers with `ranges::to`. This part is experiment-heavy and spans both C++20 and C++23, so the compiler flags will switch between `-std=c++20` and `-std=c++23` — a detail that is itself a foreshadowing of this article's themes. Environment: Arch Linux WSL, GCC 16.1.1.
:::

At the end of the previous part, Shah closed with an exaggerated slide declaring "iterators must go." In this part, we'll see how Ranges redesigns a safer, more composable interface layer on top of iterators. Let's start with the most fundamental question: **what exactly did Ranges change?**

## A range is still that pair of iterators, but the end can be a "sentinel"

The underlying definition hasn't changed — a range is still bounded by a beginning and an end. But C++20 gave it an important extension: **the end can be a different type from the beginning, called a sentinel**<RefLink :id="1" preview="cppreference, Ranges library — sentinel may differ in type from iterator" />.

Why allow different types? Consider a classic example: iterating over a C-style string terminated by `'\0'`. In the traditional iterator model, you have to `strlen` calculate the length first before you can determine `end` — but you really just need to "keep going until you hit `'\0'`." A sentinel expresses an end condition of "walk until some condition is met." Its type can differ from the iterator, as long as they are comparable (`it == sentinel`). This makes iterating over "sequences of unknown length" natural — and this is precisely the foundation that makes "infinite ranges" possible later on.

## From range-v3 to Standard Ranges: concepts are the missing piece

Ranges didn't just appear out of nowhere in C++20. Its prototype was Eric Niebler's **range-v3** library<RefLink :id="2" preview="Eric Niebler, range-v3 — C++14 library, prototype of standard Ranges" />, which was available as early as the C++14 era. If your project is still stuck on C++14/17, you can use range-v3 to practice — its API is highly similar to the Standard Library Ranges, making future migration costs very low.

So why did the standard library version wait until C++20? **Because Ranges relies heavily on concepts for its implementation**<RefLink :id="3" preview="cppreference, Concepts library (C++20) — constraints enable Ranges" />. Ranges needs to precisely express constraints like "what counts as a range" or "what qualifies as a random-access iterator." Before concepts, these constraints could only be implemented via SFINAE (Substitution Failure Is Not An Error) — resulting in error messages that routinely spanned dozens of lines of template gibberish, making them completely unreadable. Concepts allow constraints to be named and evaluated early, and that was the final missing piece that allowed Ranges to enter the standard.

## Constrained algorithms: one fewer parameter, one fewer chance for error

The most immediately noticeable improvement in Ranges is **constrained algorithms** — the official name on cppreference. They share the same names as classic algorithms, but reside in the `std::ranges::` namespace. The difference is: **classic algorithms require you to pass an iterator pair `(first, last)`, while the ranges version only requires you to pass a container (or any range)**<RefLink :id="4" preview="cppreference, Constrained algorithms — pass the whole range, not iterator pair" />.

```cpp
#include <algorithm>
#include <ranges>
#include <vector>

std::vector<int> v{3, 1, 4, 1, 5, 9};

std::sort(v.begin(), v.end());   // 经典：传一对迭代器
std::ranges::sort(v);            // ranges：传整个容器
```

`ranges::sort(v)` does exactly the same thing as `sort(v.begin(), v.end())`, but it takes two fewer parameters. The benefit isn't just less typing — returning to pitfall #2 from the previous part, "mismatching begin/end," **classic algorithms allow you to accidentally pair iterators from two different containers, while the ranges version doesn't even give you that opportunity**, because it only accepts a single object. Eliminating one possible error is a tangible safety improvement.

Constrained algorithms also support span, custom containers, or anything that satisfies the `std::ranges::range` concept:

```cpp
int arr[] = {3, 1, 4};
std::ranges::sort(arr);                       // 原生数组也行

std::ranges::find_if(v, [](int i) { return i > 4; });
// ranges::find_if 同样返回迭代器（指向找到的元素），
// 用 ranges::end(v) 判断是否没找到
```

:::tip Iterator knowledge is not obsolete
Note that `ranges::find_if` still returns an iterator — **which means all the iterator knowledge from the previous part is still useful**. Iterator invalidation and pairing issues still exist in ranges; Ranges just makes them harder to trigger (not eliminated, just harder). We will still need iterators in C++26.
:::

## Views: lazy evaluation, the soul of Ranges

Constrained algorithms are just the appetizer. The real killer feature of Ranges is **views**. A view is a **lazy** way to access a range — it doesn't copy data or precompute results. Instead, as you iterate over it, it **processes one element at a time**<RefLink :id="5" preview="cppreference, Ranges library — views are lazy" />.

Let's compare the two styles. `std::ranges::sort(v)` is **eager evaluation** — it immediately sorts the entire range in place and only returns after finishing. In contrast, `std::views::filter(...)` is **lazy evaluation** — it simply sets up a "filtering pipeline" without doing any computation, and only yields each element to you as you actually iterate over it, but only if it meets the condition.

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

That `|` is the **pipe operator**, borrowed from Unix pipes — it feeds the range on the left into the view adaptor (range adaptor) on the right. You can chain multiple views together, composing them like a pipeline:

```cpp
auto result = v
    | std::views::filter([](int x) { return x > 1; })    // 过滤
    | std::views::transform([](int x) { return x * x; }) // 变换
    | std::views::take(3);                                // 只取前 3 个
// 遍历 result 时：3²=9, ... 一路惰性求值
```

## Experiment: eager vs lazy, what's the actual difference?

Simply saying "lazy is more efficient" isn't intuitive enough, so let's run a benchmark. We'll create a `vector` with ten million elements and compare two approaches: **eager** — first use `ranges::to` to materialize the filtered results into a temporary `vector`, then iterate to sum them up; **lazy** — directly iterate over `views::filter` without building a temporary container.

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

Both approaches compute the exact same sum (`37499992500000`, verification passed), but **eager took 23ms while lazy only took 7ms — over 3 times faster**, and the lazy version **didn't allocate that temporary `vector` with millions of elements**. The eager approach is slower for two reasons: first, it has to copy five million matching elements into a temporary vector (a bunch of `push_back` plus potential reallocations), and second, it requires an extra complete traversal (materialize first, then sum, effectively traversing twice). The lazy approach traverses only once, filtering and summing simultaneously — filtered-out elements are simply skipped, with no copying whatsoever.

:::tip How to see "laziness" with your own eyes
To intuitively feel that "the pipeline is set up but not executed, and execution only happens during iteration," there's a simple trick: add a `std::cout` inside the lambdas for both filter and transform, then **just set up the pipeline without iterating** — you'll find that nothing gets printed. Once you write `for (auto x : pipeline)`, each element will **traverse the entire pipeline before the next one is processed**: the first element goes through filter, and only if it passes does it enter transform, then take... It's one element going all the way through, not filtering all elements first and then transforming them. This is the lazy execution model, and it's also the reason why "short-circuiting" works later.
:::

## Infinite ranges: magic enabled by laziness

Lazy evaluation unlocks a very cool capability — **infinite ranges**. If evaluation were eager, infinite sequences would be impossible to express (you can't precompute an infinite number of elements). But with laziness, as long as you don't actually try to iterate over "infinity," it can exist.

`std::views::iota(x)` starting from `x` generates an **infinitely incrementing** sequence<RefLink :id="6" preview="cppreference, std::views::iota — infinite counting range factory (C++20)" />. Paired with `take` to truncate it, it can be used safely:

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

`iota(0)` by itself is infinite (0, 1, 2, 3, ...), but `take(5)` truncates it to five elements. Lazy evaluation guarantees that the infinite portion beyond `take` **will never be evaluated**. This pattern of "defining an infinite source, then using a view to limit how much is used" is very handy when dealing with streaming data or generating sequences. `iota` is a range factory available since C++20.

## Pipeline short-circuiting: efficiency brought by lazy evaluation

Another direct benefit of laziness is **short-circuiting**. When you chain multiple filters together, as long as an element is filtered out at one stage, **the subsequent stages will not process it at all** — because the execution model is "one element goes all the way through."

The example Shah gave was filtering a collection of strings: first filter for "starts with M," then filter for "length greater than 4." If a string doesn't start with M, it gets blocked at the first filter, and the predicate for the second filter **is never even called**. Let's quantify this effect — we'll add a counter to the filter's predicate and compare the number of predicate calls between a "full traversal" and "early termination with `take(5)`":

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

**Ten million times vs six times**. After adding `take(5)`, the predicate was only called six times (it takes six checks to retrieve five elements) before stopping, and the remaining ten million evaluations were all short-circuited away by laziness. If you only care about "the first few elements that meet the condition," this approach is more than an order of magnitude faster than "filtering into a complete list first and then taking the first five" — because the latter (eager) must run every element through the predicate.

## ranges::to: materializing lazy results back into containers (C++23)

Views are lazy, but often you ultimately want a **concrete container** (for example, when you need random access multiple times, or when passing to an interface that only accepts containers). Materializing a view into a container is the job of `std::ranges::to`:

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

:::warning There's a version trap here that Shah failed to flag
In his talk, Shah says "we have `ranges::to`" in a tone that implies it's been available alongside constrained algorithms since C++20. **It's not.** `std::ranges::to` only entered the standard in **C++23** (proposal P1206R7, feature test macro `__cpp_lib_ranges_to_container=202202L`)<RefLink :id="7" preview="cppreference, std::ranges::to (since C++23) — P1206R7" />, a full version later than the C++20 constrained algorithms.

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

`-std=c++20` directly throws a `'to' is not a member of 'std::ranges'`; only `-std=c++23` compiles successfully. So if your project is still on C++20, `ranges::to` won't work — you'll have to manually `reserve` plus loop `push_back`, or use `std::copy` with an inserter. The minimum toolchain versions are roughly GCC 14 / Clang 18+libc++ / MSVC VS2022 17.5.

:::tip Pipe support is also C++23, not a "later addition"
The pipe syntax like `r | ranges::to<C>()` comes from proposal P2387R3. It landed in C++23 **alongside** P1206, not as "first there was `ranges::to`, and pipe support was patched in later." So you don't need to worry about "the pipe version being a patch" — it was a complete part of C++23 from the start.
:::
:::

## Views cheat sheet: which standard introduced which

This is another key addition in this article. Views have continued to expand since C++20, with C++23 adding a large batch and C++26 still adding more. In his talk, Shah broadly labels `drop_while`, `chunk_by`, `zip`, and `zip_transform` as "new things," but **doesn't flag the versions** — these actually belong to different standards, and mixing them up will cause compilation failures. I've listed the version attributions verified against cppreference:

| Standard | Views (representative) |
|------|------|
| **C++20** | `filter`, `transform`, `take`, `drop`, `take_while`, `drop_while`, `reverse`, `join`, `split`, `keys`, `values`, `elements`, `iota` (infinite), `lazy_split`, `common`, `counted`, `all` |
| **C++23** | `zip`, `zip_transform`, `chunk`, `chunk_by`, `slide`, `join_with`, `stride`, `cartesian_product`, `as_const`, `as_rvalue`, `enumerate`, `adjacent`, `adjacent_transform`, `pairwise`, `pairwise_transform`, `repeat` (factory) |
| **C++26** | `cache_latest` (along with `concat`, `as_input`, `indices` etc. in progress) |

:::warning A few versions that are easy to misremember

- **`drop_while` is C++20**, not C++23 — don't relegate it to '23 just because it "looks new."
- **`chunk_by`, `zip`, and `zip_transform` are C++23** (`zip`/`zip_transform` come from P2210, `chunk_by` from P2442)<RefLink :id="8" preview="cppreference, std::views::zip / chunk_by — C++23, P2210 / P2442" />, requiring `-std=c++23`.
- **`as_rvalue` is C++23**, very easily misremembered as C++26 — because it sounds "very new," but it actually came in alongside the zip batch.
- **`join` is C++20, but `join_with` is C++23** — don't assume the version with `_with` is C++20.
:::

Let's test-drive a few C++23 views to get a feel for their power. `chunk_by` groups consecutive equal elements:

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

Consecutive equal elements are each grouped together. `zip` "zips" multiple ranges for parallel traversal, taking the length of the shortest one:

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

Previously, to traverse two containers in parallel, you had to manually write two indices and worry about out-of-bounds access; `zip` turns this into a one-liner pipeline, and you can even directly use structured bindings to unpack the results. These new C++23 views significantly broaden the boundaries of what "expressing data processing pipelines with pipes" can do.

## Custom iterators: an iterator is just a "pseudo-pointer with replaceable forward logic"

:::tip This section is advanced and can be skipped
If you want a more solid understanding of "what an iterator really is," you can write one yourself. Below is a minimal singly-linked-list node iterator — it proves that: **the essence of an iterator is simply an object that "can `++`, can `*`, and can be compared," and the forward logic is completely replaceable.**
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

As long as these four operations are present (dereference, prefix `++`, inequality comparison, and default-constructible/copyable), it can serve as a forward iterator, plugging into range-based for loops and constrained algorithms. Whether the container internally uses a linked list, a tree, or a graph, it can masquerade as "a pseudo-pointer that can step forward one at a time" on the outside. This is the power of the iterator abstraction — and it's why Ranges chose to build on top of iterators rather than starting from scratch.

## Pitfall checklist: things to watch out for even with Ranges

Finally, let's consolidate the pitfalls scattered across the three parts of this series for your review. Ranges make many errors **harder to commit**, but they don't eliminate them:

1. **`std::advance` does not perform bounds checking** — out-of-bounds access means a segfault; in generic code, check with `std::distance` first.
2. **`begin`/`end` must come from the same container** — `process(f().begin(), f().end())` is UB; store them in named variables.
3. **`list`/`set` iterators do not support `+n`/`-n`** — use the member `sort()` for sorting; don't force `std::sort`.
4. **Views do not own data** — they are merely a view of the underlying range. Once the underlying container is invalidated (due to reallocation, rehashing, or destruction), the view dangles. **Don't let a view's lifetime exceed the container it observes.**
5. **`ranges::to` without a `take` safety net will exhaust memory** — directly `ranges::to<vector>()`-ing an infinite `iota` will materialize infinitely and blow up memory; always `take` to limit it first.
6. **`reverse` combined with views over single-pass iterators may fail to compile** — some views require bidirectional iterators; using `reverse` on a single-pass `forward_list` view will cause a compilation failure.
7. **Algorithm error messages aren't necessarily shorter** — ranges use concepts to intercept errors earlier and more accurately, but deeply nested constraint errors can still be quite long; the real benefit is "you can't write certain bugs," not "fewer lines of error output."

## What we've figured out across these three parts

From index-based loops in the first part to view pipeline composition in this one, we've walked through the evolution of C++'s abstractions for "iterating and processing data." The core of this part can be distilled into a few points: constrained algorithms let you **pass fewer parameters and avoid mismatching iterator pairs**; the lazy evaluation of views is the soul of Ranges — it **doesn't copy, doesn't precompute, and processes one element through the entire pipeline during iteration**, benchmarking over 3 times faster than eager materialization (7ms vs 23ms) while saving memory; laziness enables **infinite ranges** (`iota`) and **short-circuiting** (adding `take(5)` reduced predicate calls from ten million down to six); `ranges::to` materializes lazy results back into containers, but **it's C++23** — don't be misled by the tone of "we have ranges::to"; views are still evolving, with `chunk_by`/`zip`/`zip_transform` being C++23, and `cache_latest` being C++26.

Looking back at Shah's statement that "algorithms are essentially loops" — we can now complete the thought: the goal of modern C++ is precisely **to spare you from writing those loops by hand**. Use constrained algorithms to replace hand-written sorting/searching loops, and use view pipelines to replace multi-pass "filter → transform → collect" loops, making your code closer to "describing what you want" rather than "describing how to do it." This is the design philosophy of Ranges.

If you want to dive deeper, there are a few directions: the concepts article in vol4 can help you understand the constraint system behind ranges; the perfect forwarding and SIMD content in the vol6 performance issue share the same lineage as views' "avoiding unnecessary copies"; and cppreference's [Ranges library](https://en.cppreference.com/w/cpp/ranges) and [Constrained algorithms](https://en.cppreference.com/w/cpp/algorithm/ranges) are the most authoritative cheat sheets. Ranges aren't perfect — issues like iterator invalidation are just harder to trigger, not eliminated — but they genuinely make "writing better, safer, higher-performance data processing code" a lot smoother than in the C++11 era.

<ReferenceCard title="References">
  <ReferenceItem
    :id="1"
    author="cppreference.com"
    title="Ranges library (since C++20)"
    :year="2026"
    url="https://en.cppreference.com/w/cpp/ranges"
    chapter="sentinel may differ in type from iterator"
  />
  <ReferenceItem
    :id="2"
    author="Eric Niebler"
    title="range-v3 (C++14 library)"
    :year="2014"
    url="https://github.com/ericniebler/range-v3"
    chapter="Prototype of standard Ranges"
  />
  <ReferenceItem
    :id="3"
    author="cppreference.com"
    title="Concepts library (since C++20)"
    :year="2026"
    url="https://en.cppreference.com/w/cpp/concepts"
    chapter="Concepts are the missing piece for Ranges"
  />
  <ReferenceItem
    :id="4"
    author="cppreference.com"
    title="Constrained algorithms (since C++20)"
    :year="2026"
    url="https://en.cppreference.com/w/cpp/algorithm/ranges"
    chapter="Pass the whole range instead of an iterator pair"
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
    chapter="range_adaptor_closure (landed in C++23 alongside other features)"
  />
  <ReferenceItem
    :id="10"
    author="Mike Shah"
    title="Back to Basics: C++ Ranges — CppCon 2025"
    :year="2025"
    url="https://www.youtube.com/watch?v=Q434UHWRzI0"
  />
</ReferenceCard>
