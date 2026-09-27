---
chapter: 3
conference: cppcon
conference_year: 2025
cpp_standard:
- 11
- 17
- 20
description: 'CppCon 2025 talk notes — Mike Shah: from for loops and pointer traversal
  to the iterator abstraction, completing the iterator category hierarchy and testing
  legacy tags versus C++20 concepts with GCC 16.1.1'
difficulty: beginner
order: 1
platform: host
reading_time_minutes: 20
speaker: Mike Shah
tags:
- cpp-modern
- host
- beginner
- Ranges
- 容器
talk_title: 'Back to Basics: C++ Ranges'
title: 'From Loops to Iterators: The Path to Data Traversal Abstraction'
video_youtube: https://www.youtube.com/watch?v=Q434UHWRzI0
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/03-back-to-basics-ranges/01-from-loops-to-iterators.md
  source_hash: e82c0e3d7fa67dcba55be13eb78dd784687788e2afa90ffe4942f574a0adb25c
  translated_at: '2026-09-26T15:56:01+00:00'
  engine: anthropic
  token_count: 4100
---
# From Loops to Iterators: The Path to Data Traversal Abstraction

:::tip
This article is a deep-dive adaptation of Mike Shah's "Back to Basics: C++ Ranges" from CppCon 2025 — the YouTube link is above. The series is planned as three parts: this part nails down the thread of "traversing data" (loops → pointers → iterators → range-based for), the second part covers STL algorithms and iterator pitfalls, and only in part three do we properly get into Ranges, Views, and pipeline composition. The experimental environment is Arch Linux WSL, GCC 16.1.1, compiled with `-std=c++20`.
:::

Mike Shah opened the talk with a remark so plain that I keep finding more truth in it the more I think it over: **an algorithm is, at its core, a loop**. He said that back in grad school he read a 2012 paper doing an empirical evaluation of algorithm performance, and the takeaway he got was this — when facing an unfamiliar codebase and trying to figure out "where the computation actually happens," the fastest way is to hunt down the loops in the program. Half of our job as engineers is **transforming data**, the other half is **storing data**, and the loop is the most direct vehicle for that "transforming data" work.

:::warning Take Shah's claim with a grain of salt
"Algorithm = loop" is what he himself repeatedly calls "a gross oversimplification," so take it in that spirit. Strictly speaking, an algorithm is a finite sequence of steps that solves a problem — recursive algorithms, parallel algorithms (`<execution>`), coroutine-style algorithms don't necessarily wear the shape of a `for`. Loops are merely one of the most common vehicles. But as an entry point into the STL and Ranges, this simplification works well: **understand the loop first, then watch how the STL abstracts loops away.**
:::

In this part we start from the most primitive index-based loop and watch, step by step, how C++ abstracts "traversing data" layer by layer. Our destination is not Ranges (that's part three) but the **iterator** — the bridge connecting "loops" and "algorithms."

First, the experimental environment — everything printed later is based on it:

```bash
❯ g++ --version
g++ (GCC) 16.1.1 20260430

❯ uname -sr
Linux 6.18.33.1-microsoft-standard-WSL2
```

## The Most Primitive Traversal: The Index-Based for Loop

Everything starts here. Suppose we have a sequence of characters to print one by one; what most people instinctively write is the three-part `for`:

```cpp
#include <iostream>
#include <array>

int main()
{
    std::array<char, 5> message{'H', 'e', 'l', 'l', 'o'};

    for (std::size_t i = 0; i < message.size(); ++i) {
        std::cout << message[i];
    }
    std::cout << '\n';
}
```

This code actually hides two implicit assumptions — we've just used it so smoothly for so long that we never think about them. First, it assumes the container supports `operator[]` subscript access; second, it assumes the container knows its own `size()`. `std::array`, `std::vector`, and `std::string` all satisfy both, so it runs fine. But swap in a `std::list` or a `std::set` — neither has subscript access — and this code no longer compiles. The same "traversal" logic has to be rewritten for a different container, and that is exactly the signal that the abstraction is insufficient.

But let's not rush into abstracting just yet — whether you should use an index loop, and when, is a nuanced question, just not the point here. What we care about is this: **it expresses "traversal," but it welds traversal to "the container happens to be contiguously stored and happens to support subscripting."** We want to pull the former out on its own.

## A Change of Perspective: Traversing with Pointers

On a slide, Shah switched to a different formulation, and for a second I froze — wait, that works too? Instead of an index, he takes the array's first address and walks it with a pointer:

```cpp
char* begin = message.data();
char* end   = message.data() + message.size();
for (char* p = begin; p != end; ++p) {
    std::cout << *p;
}
```

Here `data()` returns the first address of the underlying array, and `end` is that address plus the number of elements — pointer addition. Then inside the loop body, `*p` dereferences and `++p` steps forward. The output is identical to the index version, but the perspective has completely shifted: **we no longer lean on the "index" abstraction; we operate on "addresses" directly.**

Why change perspective? Shah's motivation is direct — **generalization**. Subscripting assumes "contiguous storage + random access," but plenty of real-world data structures are not contiguous: linked lists, trees, graphs. How would you `tree[i]` a binary tree? You can't index it with an integer. But "start from some point and step to the next element, one at a time" is the common kernel of traversal across all data structures. Pointer `++` is merely the simplest implementation of "go to the next."

:::tip A side note on where the STL came from
Abstracting "increment a pointer" into a swappable object is the work Alexander Stepanov and Meng Lee completed at Hewlett-Packard (HP) Labs in the 90s — that is the prototype of the STL, submitted to the committee in 1993–94 and later folded into the C++98 standard. Iterators were born to "decouple algorithms from data structures" from day one; they were not an afterthought bolted on later.
:::

## Iterators: Generalized Pointers

Since "go to the next element" can have different implementations, why not abstract it into a type — and that is the **iterator**. The very first sentence cppreference has to say about iterators is: **"iterators are a generalization of pointers"**<RefLink :id="1" preview="cppreference, Iterator library — iterators are a generalization of pointers" />.

We use the pair of free functions `std::begin` and `std::end` to grab iterators to the container's start and end:

```cpp
for (auto it = std::begin(message); it != std::end(message); ++it) {
    std::cout << *it;
}
```

Look how it is nearly identical to the pointer version — `begin`, `end`, `!=`, `++`, `*`. The only difference is that `it`'s type is no longer `char*` but an object that "behaves like a pointer." Swap in a `std::list` or a `std::set`, and this code runs without changing a single character (as long as their iterators support these operations). The abstraction starts paying us back here.

Two details deserve a pause. First, `begin()` points at the first element, while `end()` points **one position past the last element** (one-past-the-end) and is not itself dereferenceable. This half-open interval `[begin, end)` convention was not picked at random: **it makes detecting an "empty container" utterly natural** — an empty container is just `begin == end`, the loop condition is false from the start, no special-casing needed. If `end` pointed at the last element itself, an empty container would have no "last element," and handling that gets awkward.

The second detail is the difference between the **free function** form `std::begin` / `std::end` and the containers' `.begin()` / `.end()` **member function** form.

:::warning Shah is not quite accurate here
In the talk, Shah said "only some containers have `.begin()`, `.end()`, but not all containers do, so the free functions are more general" — that claim is actually **inaccurate**. The fact is: **every STL container has `.begin()` / `.end()` member functions**, without exception.

The real value of the free functions `std::begin` / `std::end` lies in three things. One, they are overloaded for **raw arrays** (say `int arr[5]`) — arrays have no member functions, so free functions are the only way to get their first and one-past-last pointers. Two, they make **generic code** more uniform to write (inside a template you don't have to distinguish "is this a container or an array"). Three, C++20's `std::ranges::begin` additionally handles sentinels and proxy types (think `vector<bool>`). So the more accurate statement is: **the free functions are more uniform across built-in arrays and custom types — it is not that "some containers lack the member functions."**
:::

## The Iterator Category Hierarchy: Not All Iterators Are Equally Capable

At this point in the talk, Shah simply said "I won't expand on iterator categories" and skipped ahead. But this is precisely where beginners trip the hardest, and since this is an adaptation, we are going to fill the gap in — it is the **centerpiece** of this part.

Not all iterators are equally capable. A `std::vector` iterator can `it + 5` and jump five slots at once; a `std::list` iterator cannot — it can only `++` its way forward one step at a time. The standard splits iterators into **categories** by capability, roughly from weakest to strongest: input → forward → bidirectional → random access → contiguous (new in C++20).

The key question: **how do you know which category an iterator falls into?** Before C++20, you relied on a type trait called `std::iterator_traits<T>::iterator_category` (a tag type); from C++20 on, it is a set of **concepts**, such as `std::random_access_iterator<T>` and `std::contiguous_iterator<T>`. The two coexist in C++20, yet they can give **different** answers for the same iterator — and behind that hides a very important evolution.

I wrote a small program that prints both sets of results for the common containers, using GCC 16.1.1:

```cpp
#include <array>
#include <vector>
#include <string>
#include <deque>
#include <list>
#include <forward_list>
#include <set>
#include <map>
#include <iterator>
#include <type_traits>
#include <cstdio>

// Old C++98 style: pull the tag from iterator_traits
template<class Iter>
const char* legacy_tag()
{
    using cat = typename std::iterator_traits<Iter>::iterator_category;
    if constexpr (std::is_same_v<cat, std::contiguous_iterator_tag>) return "contiguous";
    else if constexpr (std::is_same_v<cat, std::random_access_iterator_tag>) return "random_access";
    else if constexpr (std::is_same_v<cat, std::bidirectional_iterator_tag>) return "bidirectional";
    else if constexpr (std::is_same_v<cat, std::forward_iterator_tag>) return "forward";
    else if constexpr (std::is_same_v<cat, std::input_iterator_tag>) return "input";
    else return "?";
}

// New C++20 style: probe with concepts
template<class Iter>
const char* cpp20_concept()
{
    if constexpr (std::contiguous_iterator<Iter>) return "contiguous_iterator";
    else if constexpr (std::random_access_iterator<Iter>) return "random_access_iterator";
    else if constexpr (std::bidirectional_iterator<Iter>) return "bidirectional_iterator";
    else if constexpr (std::forward_iterator<Iter>) return "forward_iterator";
    else if constexpr (std::input_iterator<Iter>) return "input_iterator";
    else return "(none)";
}

template<class Iter>
void row(const char* name)
{
    std::printf("%-26s legacy_category=%-15s cpp20_concept=%s\n",
                name, legacy_tag<Iter>(), cpp20_concept<Iter>());
}

int main()
{
    row<std::array<int, 5>::iterator>("std::array<int,5>");
    row<std::vector<int>::iterator>("std::vector<int>");
    row<std::string::iterator>("std::string");
    row<std::deque<int>::iterator>("std::deque<int>");
    row<std::list<int>::iterator>("std::list<int>");
    row<std::forward_list<int>::iterator>("std::forward_list<int>");
    row<std::set<int>::iterator>("std::set<int>");
    row<std::map<int, int>::iterator>("std::map<int,int>");
    row<int*>("int* (raw pointer)");

    static_assert(std::contiguous_iterator<int*>);
    static_assert(std::random_access_iterator<std::vector<int>::iterator>);
    static_assert(!std::contiguous_iterator<std::deque<int>::iterator>);
    static_assert(!std::random_access_iterator<std::list<int>::iterator>);
    std::printf("static_assert checks: PASS\n");
}
```

Compile and run:

```bash
❯ g++ -std=c++20 -O2 -Wall iter.cpp -o iter && ./iter
std::array<int,5>          legacy_category=random_access   cpp20_concept=contiguous_iterator
std::vector<int>           legacy_category=random_access   cpp20_concept=contiguous_iterator
std::string                legacy_category=random_access   cpp20_concept=contiguous_iterator
std::deque<int>            legacy_category=random_access   cpp20_concept=random_access_iterator
std::list<int>             legacy_category=bidirectional   cpp20_concept=bidirectional_iterator
std::forward_list<int>     legacy_category=forward         cpp20_concept=forward_iterator
std::set<int>              legacy_category=bidirectional   cpp20_concept=bidirectional_iterator
std::map<int,int>          legacy_category=bidirectional   cpp20_concept=bidirectional_iterator
int* (raw pointer)         legacy_category=random_access   cpp20_concept=contiguous_iterator
static_assert checks: PASS
```

See the pattern? **The most interesting rows are the first few and the last one.** `std::array`, `std::vector`, `std::string`, plus the raw pointer `int*` — their legacy tags all say `random_access`, yet the C++20 concept probe says `contiguous_iterator`.

And that is exactly the problem: **the legacy tag hierarchy simply has no `contiguous` tier** (`contiguous_iterator_tag` was only added in C++20). Before C++20, the `iterator_category` of `int*` could only be labeled `random_access`; there was no way to express the stronger property of "this memory is not just randomly accessible but physically stored contiguously." Why does the distinction matter? Because "contiguous storage" means you can safely feed the data under the iterator to C interfaces as one contiguous block (say `memcpy`, a CUDA kernel, or SIMD instructions) — while `std::deque`, though it also supports `it + 5`, is internally chunked storage, segment by segment, **not contiguous**, so its concept is `random_access_iterator` rather than `contiguous`.

:::tip This is where concepts outshine tags
The legacy tags form an inheritance chain (`random_access_iterator_tag` inherits from `bidirectional_iterator_tag` which inherits from ...), and their expressive power is limited to that layering. C++20 concepts are a set of **orthogonal, composable constraints** that can state precisely that "randomly accessible" and "contiguously stored" are two independently satisfiable properties. This is also why the whole Ranges machinery had to wait for C++20's concepts to land before it could enter the standard — without concepts, many constraints simply cannot be expressed. For a more systematic treatment of concepts, see the relevant articles in vol4; we will lean on them again in part three when we cover Ranges.
:::

## Iterator Arithmetic and std::advance

With categories in hand, iterator arithmetic becomes clear. For random access iterators you can write `it + 5`, `it - 2`, `it1 - it2` (distance) directly — all O(1). But for bidirectional or forward iterators, `it + 5` flat-out fails to compile — they only know `++` and `--`.

So what if you are writing generic code, want to "walk forward n steps," yet don't want to pin down the iterator category? The standard library gives you `std::advance`<RefLink :id="2" preview="cppreference, std::advance — advances an iterator by n positions" />:

```cpp
auto it   = std::begin(message);
auto last = std::end(message);
std::ptrdiff_t available = std::distance(it, last);
if (5 < available) {
    std::advance(it, 5);   // safe: we confirmed the range is long enough
}
```

The beauty of `std::advance` is that it **picks its implementation automatically** based on the iterator category: hand it a `vector::iterator` and it does `it + n` (O(1)); hand it a `list::iterator` and it degrades to n applications of `++` (O(n)). One call interface, different algorithmic complexity underneath — that is the sweet taste of generic programming.

:::warning advance does no bounds checking
One thing must be said plainly: **`std::advance` itself never checks bounds**. Tell it to walk 100 steps forward in a container holding 5 elements and it will not report an error — it walks straight out of bounds, and dereferencing there is a segfault (UB). That is why the snippet above first computes the remaining length with `std::distance` and checks it. In real projects, if you want bounds-checked iterators, on GCC/Clang you can add the `-D_GLIBCXX_DEBUG` compile macro so the standard library's iterators carry lower and upper bound detection in debug mode — in the next part we will use it to catch a real out-of-bounds bug. On the MSVC side the counterpart is `_ITERATOR_DEBUG_LEVEL=2`.
:::

## range-based for: Syntactic Sugar for Loops

After all this iterator talk, back to everyday code — the vast majority of the time we do not hand-write `for (auto it = begin; it != end; ++it)`; we use the **range-based for loop** that C++11 gave us:

```cpp
for (char c : message) {
    std::cout << c;
}
```

Clean, hard to get wrong, no fussing over `end`. But what is really behind this sugar? It is simply an equivalent rewriting of the hand-written iterator loop above. As the standard specifies<RefLink :id="3" preview="cppreference, Range-based for loop — equivalent expansion" />, it is roughly equivalent to:

```cpp
{
    auto&& __range = message;
    auto  __begin  = std::begin(__range);   // or __range.begin()
    auto  __end    = std::end(__range);     // or __range.end()
    for (; __begin != __end; ++__begin) {
        char c = *__begin;
        std::cout << c;                      // your loop body
    }
}
```

This clears up a common confusion: **how does range-based for know to call `begin`/`end`?** The answer: the compiler inserts those two lines for you behind the scenes. It takes `__range`, obtains the start and end iterators, and from there it is an ordinary iterator loop. So range-based for imposes no extra requirements on iterator category — as long as your type can provide `begin`/`end` (member or free function, either works), it is usable. That is also why, later on, our custom types slot straight into a range-based for the moment they implement these two functions.

If what you are traversing is a key-value container like `std::map`, C++17's **structured bindings** pair beautifully with range-based for:

```cpp
const std::map<std::string, int> scores{
    {"alice", 90}, {"bob", 85}
};

for (const auto& [name, score] : scores) {
    std::cout << name << ": " << score << '\n';
}
```

:::warning Pinning down the standard version for structured bindings
Shah used structured bindings in the talk but **never labeled which standard they belong to** — so let's add it: **structured bindings were introduced in C++17 (proposal P0217)**<RefLink :id="4" preview="cppreference, Structured binding declaration (since C++17)" />. If your codebase is still on C++14, this snippet will not compile.

Also, Shah dropped a line about "ellipsis syntax enabling further unpacking," and that phrasing is a bit fuzzy. Structured bindings themselves do not support variadic unpacking (the number of bound elements is fixed and must match the member count of the right-hand type); ellipses in C++ belong to the worlds of template parameter pack expansion and fold expressions — not the same thing as structured bindings. Best to treat that sentence as a slip of the tongue and not dig into it.
:::

## An Experiment: Do range-based for and Hand-written Loops Compile Identically

Every time I tell someone "range-based for is just sugar," somebody squints in doubt — do those `__range`, `__begin`, `__end` temporaries drag performance down? Let's measure it. Here is the same "sum" written four ways:

```cpp
#include <vector>

int sum_index(const std::vector<int>& v)
{
    int s = 0;
    for (std::size_t i = 0; i < v.size(); ++i) s += v[i];
    return s;
}

int sum_ptr(const std::vector<int>& v)
{
    int s = 0;
    for (const int* p = v.data(), *e = p + v.size(); p != e; ++p) s += *p;
    return s;
}

int sum_iter(const std::vector<int>& v)
{
    int s = 0;
    for (auto it = v.begin(), e = v.end(); it != e; ++it) s += *it;
    return s;
}

int sum_rangefor(const std::vector<int>& v)
{
    int s = 0;
    for (int x : v) s += x;
    return s;
}
```

Then turn on `-O2` and have the compiler emit assembly:

```bash
❯ g++ -std=c++20 -O2 -S codegen.cpp -o codegen.s
```

Dig through the `.s` file for these four functions' hot loops and you will find every one of them shaped like this (`sum_rangefor` as the example):

```asm
.L19:
    addl    (%rax), %edx      ; s += *p
    addq    $4, %rax          ; p++  (an int is 4 bytes)
    cmpq    %rcx, %rax        ; p == e ?
    jne     .L19              ; not equal, so loop again
```

The loop bodies generated for all four forms are **byte-for-byte nearly identical** — at `-O2` the compiler folds those temporaries, the index computation, and the pointer arithmetic all down to the same stretch of `add / cmp / jne`. In other words, **with optimizations on, range-based for carries zero extra cost**, and you can reach for it for the sake of readability without a second thought. The cost only shows up at `-O0` (no optimization): the `__begin`/`__end` temporaries then sit dutifully on the stack — but who chases performance at `-O0` anyway?

:::tip A small trap that was only fixed in C++17
A quick note on range-based for's own history: it entered the standard in C++11 (proposal N2930). But that C++11 version's expansion rules had a flaw — it re-evaluated `__end` on every iteration (or put differently, the caching strategy for `.end()` was unkind to certain proxy types). C++17 (proposal P0184) fixed exactly this, making `__end` evaluated only once at loop start. So the range-based for you write today is the C++17-revised version, and it is the sturdier one. Which is a reminder: use the newer standard when you can — plenty of "syntactic sugar" has been quietly polished in later versions.
:::

## A Pair of Iterators Is a range

At this point we can draw the complete line through "traversal": **a starting iterator `begin`, plus an ending marker `end`, with `++` stepping from one to the other** — this pair of iterators defines a stretch of traversable data. The standard library calls such a "pair of iterators" a **range**<RefLink :id="5" preview="cppreference, Ranges library — a range is defined by begin and end" />.

Why does this concept matter? Because it fully decouples "where the data lives" from "how the data is processed." If my sum function can accept a pair of iterators, then it applies to `vector`, `list`, `set`, even a linked list you wrote by hand — as long as those containers can supply iterators that meet the requirements. Algorithms are no longer welded to one concrete container.

And the iterator abstraction itself is a classic design pattern — the **Iterator pattern**, one of the behavioral patterns in the GoF *Design Patterns*. Its core idea: "provide a way to access the elements of an aggregate object sequentially without exposing the object's underlying representation." C++ turned it into a language-level facility (the `begin`/`end`/`operator++`/`operator*` convention), so any type that honors this convention plugs straight into the entire STL algorithm ecosystem.

This "a pair of iterators is a range" definition is precisely the forerunner of the `std::ranges::range` concept we will cover in part three. The difference is that the C++20 range concept allows `end` to return a sentinel **of a different type than `begin`** — which unlocks some very interesting capabilities (for example, traversing a `'\0'`-terminated C string without computing its length first). We will expand on that in part three.

## What We Have Figured Out So Far

Starting from the most primitive index-based `for`, we watched "traversal" get abstracted step by step: the index loop welds traversal to "contiguous storage + random access"; pointer traversal frees it at the "address" level; the iterator abstracts it further into "an object that can `++` and can `*`," and from there algorithms and data structures come apart. We also filled in the iterator category hierarchy Shah skipped, and verified a key fact with GCC 16.1.1: **the legacy tags lump `vector`/`string`/raw pointers together as `random_access`, while the C++20 concept can say precisely that they are in fact the stronger `contiguous_iterator`** — which is exactly why concepts beat tags, and why Ranges had to wait for C++20 to land.

The core of it in one sentence: **a pair of iterators (one `begin`, one `end`) defines a range, and STL algorithms are built on top of that pair.**

In the next part we hand this pair of iterators over to the STL algorithms — how to use these "loop replacements" such as `std::sort`, `std::partition`, and `std::transform`, and what hard requirements they place on iterator category (for example, why `std::sort` cannot be used on `std::list`). A few classic iterator traps will be waiting for us there too: iterator invalidation, mismatched `begin`/`end` pairs, and swapped argument order. If you want to brush up on container memory layout first, vol3's [span: A Non-owning Contiguous View](../../../../vol3-standard-library/containers/08-span.md) and the other container articles make excellent pre-reading.

<ReferenceCard title="References">
  <ReferenceItem
    :id="1"
    author="cppreference.com"
    title="Iterator library"
    :year="2026"
    url="https://en.cppreference.com/w/cpp/iterator"
    chapter="Iterators are a generalization of pointers"
  />
  <ReferenceItem
    :id="2"
    author="cppreference.com"
    title="std::advance, std::distance"
    :year="2026"
    url="https://en.cppreference.com/w/cpp/iterator/advance"
    chapter="Implementation complexity is selected automatically based on iterator category"
  />
  <ReferenceItem
    :id="3"
    author="cppreference.com"
    title="Range-based for loop (since C++11)"
    :year="2026"
    url="https://en.cppreference.com/w/cpp/language/range-for"
    chapter="Expands equivalently into a begin/end iterator loop"
  />
  <ReferenceItem
    :id="4"
    author="cppreference.com"
    title="Structured binding declaration (since C++17)"
    :year="2026"
    url="https://en.cppreference.com/w/cpp/language/structured_binding"
    chapter="P0217"
  />
  <ReferenceItem
    :id="5"
    author="cppreference.com"
    title="Ranges library (since C++20)"
    :year="2026"
    url="https://en.cppreference.com/w/cpp/ranges"
    chapter="A range is defined by begin and end"
  />
  <ReferenceItem
    :id="6"
    author="cppreference.com"
    title="std::contiguous_iterator, iterator tags"
    :year="2026"
    url="https://en.cppreference.com/w/cpp/iterator"
    chapter="C++20 introduced the contiguous category and the concept system"
  />
  <ReferenceItem
    :id="7"
    author="Mike Shah"
    title="Back to Basics: C++ Ranges — CppCon 2025"
    :year="2025"
    url="https://www.youtube.com/watch?v=Q434UHWRzI0"
  />
</ReferenceCard>
