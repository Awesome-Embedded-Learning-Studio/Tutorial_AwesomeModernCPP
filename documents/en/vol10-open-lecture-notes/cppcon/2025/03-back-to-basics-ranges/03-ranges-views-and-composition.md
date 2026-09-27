---
chapter: 3
conference: cppcon
conference_year: 2025
cpp_standard:
- 20
- 23
description: 'CppCon 2025 talk notes — Mike Shah: constrained algorithms, lazy evaluation of views, the pipe operator, and ranges::to, plus a measured eager-vs-lazy benchmark, infinite ranges, and a views version attribution table (C++20/23/26)'
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
  translated_at: '2026-09-26T16:00:47+00:00'
  engine: anthropic
  token_count: 10500
---
# Ranges, Views, and Pipelining: The Power of Lazy Evaluation

:::tip
This is the finale of the CppCon 2025 Mike Shah "Back to Basics: C++ Ranges" series. In the previous two parts we walked the whole "loops → iterators → algorithms" line and dissected the classic iterator pitfalls (invalidation, pairing, argument order). This part finally steps into the core of Ranges: constrained algorithms, lazy evaluation of views, pipeline composition, and `ranges::to` for materializing results back into a container. There are quite a few experiments in this one, and they straddle C++20 and C++23, so the compiler flag keeps switching between `-std=c++20` and `-std=c++23` — and that switching is itself a piece of foreshadowing for this article. Environment: Arch Linux WSL, GCC 16.1.1.
:::

At the end of the previous article, Shah closed with a hyperbolic "iterators must go" slide. In this one, we get to see how Ranges builds a safer, more composable interface layer on top of iterators. Let's start from the most basic question: **what exactly did Ranges change?**

## A range Is Still That Pair of Iterators, but `end` Can Be a "Sentinel"

The underlying definition has not changed — a range is still delimited by a begin and an end. What C++20 adds is an important extension: **the end is allowed to be something whose type differs from the begin's, called a sentinel**<RefLink :id="1" preview="cppreference, Ranges library — sentinel may differ in type from iterator" />.

Why allow a different type? Take the classic example: traversing a C string terminated by `'\0'`. In the traditional iterator model, you first have to run `strlen` to compute the length before you can pin down `end` — when all you actually need is "keep going until you hit `'\0'`". A sentinel is exactly this kind of end point, one that expresses "walk until some condition holds"; its type may differ from the iterator's, as long as the two can be compared (`it == sentinel`). That makes traversing "a sequence of unknown length" natural — and it is precisely the foundation that lets "infinite ranges" exist later on.

## From range-v3 to Standard Ranges: concepts Are the Key Puzzle Piece

Ranges did not spring out of nowhere in C++20. The prototype is Eric Niebler's **range-v3** library<RefLink :id="2" preview="Eric Niebler, range-v3 — C++14 library, prototype of standard Ranges" />, which was usable back in the C++14 era. If your project is still stuck on C++14/17, you can practice directly with range-v3 — its API is highly similar to the standard library's Ranges, so a future migration costs very little.

Then why did the standard library version wait until C++20? **Because landing Ranges depends heavily on concepts**<RefLink :id="3" preview="cppreference, Concepts library (C++20) — constraints enable Ranges" />. Ranges needs to state constraints like "what exactly counts as a range" or "which iterators count as random-access" precisely. Before concepts existed, those constraints could only be implemented with SFINAE (substitution failure is not an error) — and the result was that the moment you passed the wrong type, the compiler would spit out dozens of lines of unreadable template gibberish. Concepts let constraints be named and evaluated early — that was the final puzzle piece that let Ranges into the standard.

## Constrained Algorithms: One Fewer Argument, One Fewer Way to Go Wrong

The most immediately tangible improvement Ranges brings is the **constrained algorithms** — the official name cppreference uses. They share names with the classic algorithms but live under the `std::ranges::` namespace. The difference: **classic algorithms want you to pass a pair of iterators `(first, last)`, while the ranges versions take just a container (or any range)**<RefLink :id="4" preview="cppreference, Constrained algorithms — pass the whole range, not iterator pair" />.

```cpp
#include <algorithm>
#include <ranges>
#include <vector>

std::vector<int> v{3, 1, 4, 1, 5, 9};

std::sort(v.begin(), v.end());   // classic: pass a pair of iterators
std::ranges::sort(v);            // ranges: pass the whole container
```

`ranges::sort(v)` does exactly the same thing as `sort(v.begin(), v.end())`, but with two fewer arguments. And the benefit is not just less typing — recall pitfall #2 from the previous article, "mismatched begin/end": **the classic algorithms let you pair up iterators from two different containers, while the ranges version never even gives you that chance**, because it accepts only a single object. One fewer way to go wrong is a real, tangible safety gain.

The constrained algorithms also work with `span`, with custom containers — with anything that satisfies the `std::ranges::range` concept:

```cpp
int arr[] = {3, 1, 4};
std::ranges::sort(arr);                       // native arrays work too

std::ranges::find_if(v, [](int i) { return i > 4; });
// ranges::find_if likewise returns an iterator (pointing at the found element);
// compare against ranges::end(v) to test for "not found"
```

:::tip Iterator knowledge is not obsolete
Note that `ranges::find_if` still returns an iterator — **which means everything about iterators from the previous article still applies**. Iterator invalidation and pairing problems still exist under ranges; the Ranges interface just makes those mistakes harder to commit (harder, not gone). We still need iterators in C++26.
:::

## views: Lazy Evaluation, the Soul of Ranges

Constrained algorithms are only the appetizer; the real killer feature of Ranges is **views**. A view is a **lazy** way to access a range — it copies no data and precomputes no results; instead, it **processes one element at a time** as you iterate over it<RefLink :id="5" preview="cppreference, Ranges library — views are lazy" />.

Compare the two styles. `std::ranges::sort(v)` is **eager** — it sorts the entire range immediately, on the spot, and returns only when it is done. `std::views::filter(...)` is **lazy** — it merely rigs up a "filtering pipeline" and does no computation at all until you actually iterate over it; each element that satisfies the predicate is handed to you only as the traversal reaches it.

```cpp
#include <ranges>
#include <vector>
#include <iostream>

std::vector<int> v{1, 2, 3, 4, 5, 6};

// Rig the pipeline: at this point filter has processed nothing yet
auto gt3 = v | std::views::filter([](int x) { return x > 3; });

// The filtering only really runs when we iterate
for (int x : gt3) {
    std::cout << x << ' ';   // 4 5 6
}
```

That `|` is the **pipe operator**, borrowed from Unix pipes — it feeds the range on the left into the view adaptor (range adaptor) on the right. You can chain several views together and compose them like a pipeline:

```cpp
auto result = v
    | std::views::filter([](int x) { return x > 1; })    // filter
    | std::views::transform([](int x) { return x * x; }) // transform
    | std::views::take(3);                                // take only the first 3
// When result is iterated: 3²=9, ... lazily evaluated all the way
```

## Experiment: eager vs. lazy, How Big Is the Difference

Just asserting "lazy is cheaper" is not very intuitive, so let's benchmark it. Build a `vector` with ten million elements and compare two approaches: **eager** — first materialize the filtered result into a temporary `vector` with `ranges::to`, then iterate and sum it; **lazy** — iterate `views::filter` directly and build no temporary container.

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

    // EAGER: materialize the filtered result into a temporary vector, then sum it
    long long se = 0;
    auto t0 = std::chrono::high_resolution_clock::now();
    {
        auto tmp = v | std::views::filter(pred) | std::ranges::to<std::vector<int>>();
        for (int x : tmp) se += x;
    }
    auto t1 = std::chrono::high_resolution_clock::now();

    // LAZY: iterate the view directly, no temporary container
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

Both approaches produce exactly the same sum (`37499992500000` — the checksum passes), but **the eager version took 23 ms while the lazy version took only 7 ms — more than 3x faster** — and on top of that, the lazy version **never allocated that multi-million-element temporary `vector`**. The eager version is slow for two reasons. First, it has to copy the five million matching elements into the temporary vector (a pile of `push_back` calls plus possible reallocations). Second, it makes one extra full traversal (materialize first, then sum — two passes over the data). The lazy version traverses once, filtering and summing as it goes; filtered-out elements are simply skipped, without so much as a hint of copying.

:::tip How to see "lazy" with your own eyes
Want a direct feel for "the pipeline gets built but nothing runs until you iterate it"? Here is a simple trick: add a `std::cout` to each of the filter and transform lambdas, then **build the pipeline without iterating it** — you will find that nothing gets printed. The moment you write `for (auto x : pipeline)`, each element **walks the entire pipeline before the next one is touched**: the first element goes through `filter`, enters `transform` only if it survives, then moves on to `take`... it is one element flowing all the way through, not "filter everything first, then transform everything". That is the lazy execution model — and it is why the "short-circuiting" later in this article works.
:::

## Infinite ranges: The Magic Laziness Unlocks

Lazy evaluation unlocks a very cool capability — **infinite ranges**. If evaluation were eager, an infinite sequence simply could not be expressed (you cannot precompute infinitely many elements). With laziness, it can exist as long as you never actually traverse "the infinity".

`std::views::iota(x)` generates an **infinitely increasing** sequence starting from `x`<RefLink :id="6" preview="cppreference, std::views::iota — infinite counting range factory (C++20)" />. Pair it with `take` to truncate, and you can use it safely:

```cpp
// Generate the first 5 of 0², 1², 2², ...
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

`iota(0)` itself is infinite (0, 1, 2, 3, ...), but `take(5)` truncates it to five elements. Lazy evaluation guarantees that the infinite portion beyond `take` **is never evaluated**. This pattern — define an infinite source, then use a view to bound how much of it you consume — is extremely handy when processing streaming data or generating sequences. `iota` is a range factory that has existed since C++20.

## Pipeline Short-Circuiting: Efficiency That lazy Buys You

Another direct payoff of laziness is **short-circuiting**. When you chain several filters together, an element knocked out at one stage **is never touched by the later stages at all** — because of the execution model where a single element flows all the way through.

Shah's example is filtering a collection of strings: first keep the ones "starting with M", then the ones "longer than 4 characters". A string that does not start with M is stopped at the very first filter, and the second filter's predicate **is never invoked at all**. Let's quantify the effect — add a counter to the filter predicate and compare how many times the predicate is called between a "full traversal" and a version that adds `take(5)` for early termination:

```cpp
long long calls_all = 0, calls_take = 0;
auto cp_all  = [&](int) { ++calls_all;  return true; };
auto cp_take = [&](int) { ++calls_take; return true; };

for ([[maybe_unused]] int x : v | std::views::filter(cp_all)) {}
for ([[maybe_unused]] int x : v | std::views::filter(cp_take) | std::views::take(5)) {}

std::cout << "filter 谓词调用次数: 全量=" << calls_all
          << "  加 take(5)=" << calls_take << "\n";
```

On a `v` holding ten million elements:

```bash
filter 谓词调用次数: 全量=10000000  加 take(5)=6
```

**Ten million versus 6.** With `take(5)` added, the predicate was called only 6 times (fetching 5 elements requires 6 checks) and then everything stopped; the remaining ten million evaluations were all short-circuited away by laziness. If all you care about is "the first few elements that match", this style is more than an order of magnitude faster than "filter out a complete list first, then take the first 5" — because the latter (eager) has to run every element through the predicate.

## `ranges::to`: Materializing Lazy Results Back into a Container (C++23)

Views are lazy, but very often what you want at the end is a **real container** (say, you need repeated random access, or you need to pass it to an interface that only accepts containers). Turning a view into a container is `std::ranges::to`'s job:

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

:::warning There is a version trap here that Shah left unlabeled
In the talk, Shah says "we have `ranges::to`" in a tone that suggests it arrived together with the constrained algorithms back in C++20. **It did not.** `std::ranges::to` only entered the standard with **C++23** (proposal P1206R7, feature-test macro `__cpp_lib_ranges_to_container=202202L`)<RefLink :id="7" preview="cppreference, std::ranges::to (since C++23) — P1206R7" /> — one standard later than the C++20 constrained algorithms.

I compiled the same program under both standards, and the result is plain to see:

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

With `-std=c++20` you get `'to' is not a member of 'std::ranges'` as a hard error; only `-std=c++23` compiles. So if your project is still on C++20, `ranges::to` is not available — you have to `reserve` and loop `push_back` by hand, or use `std::copy` with an inserter. The minimum toolchain versions are roughly GCC 14 / Clang 18 + libc++ / MSVC VS2022 17.5.

:::tip Pipe support is also C++23, not a later add-on
The pipe spelling `r | ranges::to<C>()` comes from proposal P2387R3. It landed **in the same C++23 batch** as P1206 — it is not "ranges::to first, piping patched in afterwards". So there is no need to worry that the pipe version is some kind of bolt-on: it has been a full part of C++23 from day one.
:::
:::

## Views Cheat Sheet: Which Standard Did Each One Come From

This is another place where this write-up goes beyond the talk. Views kept growing after C++20: C++23 added a whole batch, and C++26 is still adding more. In the talk, Shah loosely called `drop_while`, `chunk_by`, `zip`, and `zip_transform` "new stuff" **without labeling the versions** — those actually belong to different standards, and mixing them up means the code will not compile. Here is the version attribution, cross-checked against cppreference:

| Standard | Views (representative) |
|------|------|
| **C++20** | `filter`, `transform`, `take`, `drop`, `take_while`, `drop_while`, `reverse`, `join`, `split`, `keys`, `values`, `elements`, `iota` (infinite), `lazy_split`, `common`, `counted`, `all` |
| **C++23** | `zip`, `zip_transform`, `chunk`, `chunk_by`, `slide`, `join_with`, `stride`, `cartesian_product`, `as_const`, `as_rvalue`, `enumerate`, `adjacent`, `adjacent_transform`, `pairwise`, `pairwise_transform`, `repeat` (factory) |
| **C++26** | `cache_latest` (with `concat`, `as_input`, `indices`, and more still in the works) |

:::warning A few versions that are easy to misremember

- **`drop_while` is C++20**, not C++23 — do not file it under 23 just because it "looks new".
- **`chunk_by`, `zip`, and `zip_transform` are C++23** (`zip`/`zip_transform` come from P2210, `chunk_by` from P2442)<RefLink :id="8" preview="cppreference, std::views::zip / chunk_by — C++23, P2210 / P2442" /> and need `-std=c++23`.
- **`as_rvalue` is C++23** — it gets misremembered as C++26 remarkably often, because it sounds "very new", but it actually came in with the zip batch.
- **`join` is C++20, but `join_with` is C++23** — do not take the `_with`-suffixed version for C++20.
:::

Let's actually run a few of the C++23 views and feel their power. `chunk_by` groups runs of consecutive equal elements:

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

Each run of consecutive equal elements lands in its own group. `zip`, in turn, traverses multiple ranges in parallel, "zipper-style", with the length being the shortest one:

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

Traversing two containers in parallel used to mean hand-writing two subscripts and worrying about running off the end; `zip` turns that into a one-line pipeline, and you can even unpack directly with structured bindings. These new C++23 views greatly widen the reach of "expressing a data-processing pipeline with pipes".

## Custom Iterators: An Iterator Is Just a "Pseudo-Pointer with Replaceable Forward Logic"

:::tip This section is advanced and skippable
If you want a firmer grip on "what an iterator really is", write one yourself. Below is a minimal singly linked list node iterator — it demonstrates that **the essence of an iterator is just an object that supports `++`, `*`, and comparison, with completely replaceable forward logic.**
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

Once those four operations are in place (dereference, prefix `++`, inequality comparison, plus default-constructibility/copyability), it can serve as a forward iterator — you can drop it into a range-based `for` or into the constrained algorithms. Whether the container is internally a linked list, a tree, or a graph, outwardly it can masquerade as "a pseudo-pointer you can walk one step at a time". That is the power of the iterator abstraction — and it is why Ranges chose to build on top of iterators instead of starting over from scratch.

## The Pitfall Checklist: Stay Sharp Even with Ranges

Finally, let's gather the pitfalls scattered across this three-part series into one place for review. Ranges has made many mistakes **harder to commit**, but has not eliminated them:

1. **`std::advance` does no bounds checking** — stepping past the end is a segfault; in generic code, check with `std::distance` first.
2. **`begin`/`end` must come from the same container** — `process(f().begin(), f().end())` is UB; store them in named variables.
3. **`list`/`set` iterators do not support `+n`/`-n`** — sort with the member `sort()`; do not force `std::sort` onto them.
4. **A view does not own its data** — it is only a window onto the underlying range; once the underlying container is invalidated (reallocation, rehash, destruction), the view dangles. **Never let a view outlive the container it observes.**
5. **`ranges::to` without a `take` backstop will eat all your memory** — piping an infinite `iota` straight into `ranges::to<vector>()` materializes forever and blows up memory; always bound it with `take` first.
6. **`reverse` on a view of single-pass iterators may fail to compile** — some views require bidirectional iterators; using `reverse` on a view of a unidirectional `forward_list` is a compile error.
7. **Algorithm diagnostics are not necessarily shorter** — ranges intercepts errors earlier and more precisely with concepts, but diagnostics from deeply nested constraints can still be very long; the real gain is "certain bugs become unwritable", not "fewer lines of error output".

## Three Articles In: What We Have Figured Out

From the subscript loops of part one to the view pipelines of this part, we have traced the whole evolution of C++'s abstractions for "traversing and processing data". The core of this article compresses into a few points: constrained algorithms mean **fewer arguments passed and fewer mismatched iterator pairs**; lazy evaluation is the soul of Ranges — **no copying, no precomputation, one element threaded through the entire pipeline as you iterate** — measured at more than 3x faster than eager materialization (7 ms vs 23 ms) while also saving memory; laziness enables **infinite ranges** (`iota`) and **short-circuiting** (adding `take(5)` drops predicate calls from ten million to 6); `ranges::to` materializes lazy results back into containers, but **it is C++23** — do not be misled by the "we have ranges::to" tone; and views are still evolving, with `chunk_by`/`zip`/`zip_transform` in C++23 and `cache_latest` and friends in C++26.

Looking back at Shah's line that "algorithms are essentially loops" — now we can finish the thought: the goal of modern C++ is precisely **to keep you from writing those loops by hand**. Replace hand-written sort/search loops with constrained algorithms, and replace the multi-pass "filter → transform → collect" loops with view pipelines, so the code describes "what you want" rather than "how to do it". That is the design philosophy of Ranges.

If you want to push deeper, a few directions: the concepts articles in vol4 will help you understand the constraint system behind ranges; the perfect-forwarding and SIMD material in the vol6 performance volume shares the same lineage as views' "avoid unnecessary copies"; and cppreference's [Ranges library](https://en.cppreference.com/w/cpp/ranges) and [Constrained algorithms](https://en.cppreference.com/w/cpp/algorithm/ranges) are the most authoritative cheat sheets. Ranges is not perfect — for problems like iterator invalidation it only makes them harder to commit — but it genuinely makes "writing better, safer, higher-performance data-processing code" a whole lot smoother than in the C++11 era.

<ReferenceCard title="References">
  <ReferenceItem
    :id="1"
    author="cppreference.com"
    title="Ranges library (since C++20)"
    :year="2026"
    url="https://en.cppreference.com/w/cpp/ranges"
    chapter="sentinel may differ in type from the iterator"
  />
  <ReferenceItem
    :id="2"
    author="Eric Niebler"
    title="range-v3 (C++14 library)"
    :year="2014"
    url="https://github.com/ericniebler/range-v3"
    chapter="prototype of the standard Ranges"
  />
  <ReferenceItem
    :id="3"
    author="cppreference.com"
    title="Concepts library (since C++20)"
    :year="2026"
    url="https://en.cppreference.com/w/cpp/concepts"
    chapter="concepts are the key puzzle piece for landing Ranges"
  />
  <ReferenceItem
    :id="4"
    author="cppreference.com"
    title="Constrained algorithms (since C++20)"
    :year="2026"
    url="https://en.cppreference.com/w/cpp/algorithm/ranges"
    chapter="pass the whole range, not an iterator pair"
  />
  <ReferenceItem
    :id="5"
    author="cppreference.com"
    title="Ranges library — Views (lazy)"
    :year="2026"
    url="https://en.cppreference.com/w/cpp/ranges"
    chapter="lazy evaluation of views"
  />
  <ReferenceItem
    :id="6"
    author="cppreference.com"
    title="std::views::iota (since C++20)"
    :year="2026"
    url="https://en.cppreference.com/w/cpp/ranges/iota_view"
    chapter="infinite counting range factory"
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
