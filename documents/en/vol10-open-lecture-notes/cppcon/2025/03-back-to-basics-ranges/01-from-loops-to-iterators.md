---
title: 'From Loops to Iterators: The Path to Abstracting Data Traversal'
description: 'CppCon 2025 Talk Notes — Mike Shah: From for Loops and Pointer Traversal
  to Iterator Abstractions, Completing the Iterator Category Hierarchy, and Benchmarking
  Legacy Tags vs. C++20 Concepts Using GCC 16.1.1'
conference: cppcon
conference_year: 2025
talk_title: 'Back to Basics: C++ Ranges'
speaker: Mike Shah
video_youtube: https://www.youtube.com/watch?v=Q434UHWRzI0
tags:
- cpp-modern
- host
- beginner
- Ranges
- 容器
difficulty: beginner
platform: host
cpp_standard:
- 11
- 17
- 20
chapter: 3
order: 1
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/03-back-to-basics-ranges/01-from-loops-to-iterators.md
  source_hash: 91550fa1d1d266e526d5c6e4b17b99b311f751048bd984193688b9b984dc07bf
  translated_at: '2026-06-13T02:12:46.363648+00:00'
  engine: anthropic
  token_count: 4006
---
# From Loops to Iterators: The Abstraction Path of Data Traversal

:::tip
This article is an in-depth adaptation of Mike Shah's "Back to Basics: C++ Ranges" from CppCon 2025. The YouTube link is above. This series is planned in three parts: this part clarifies the "data traversal" thread (loops → pointers → iterators → range-based for), the second part covers STL algorithms and iterator pitfalls, and the third part officially dives into Ranges, Views, and pipeline composition. The experimental environment is Arch Linux WSL, GCC 16.1.1, with compiler flag `-std=c++20`.
:::

Mike Shah opened his talk with a very plain statement that I found increasingly reasonable the more I thought about it: **an algorithm is essentially a loop**. He mentioned reading a 2012 paper on empirical performance evaluation of algorithms during his graduate studies, which inspired the realization that when facing an unfamiliar codebase and wanting to figure out "where the computation actually happens," the fastest way is to look for the loops. Because as engineers, half of our work is **transforming data**, and the other half is **storing data**, and loops are the most direct vehicle for "transforming data."

:::warning Take Shah's statement with a grain of salt
"Algorithm = loop" is a "gross oversimplification" that he himself repeatedly emphasized. Just get the gist of it. Strictly speaking, an algorithm is a finite sequence of steps to solve a problem—recursive algorithms, parallel algorithms (`<execution>`), and coroutine-based algorithms don't necessarily look like `for`. Loops are just one of the most common vehicles. But as an entry point for understanding STL and Ranges, this simplification works well: **first understand loops, then see how STL abstracts loops away.**
:::

In this article, we start from the most primitive index-based loop and see step by step how C++ abstracts "data traversal" layer by layer. Our destination isn't Ranges (that's part three), but **iterators**—the bridge connecting "loops" and "algorithms."

Let's lay out the experimental environment first; all subsequent output is based on it:

```bash
❯ g++ --version
g++ (GCC) 16.1.1 20260430

❯ uname -sr
Linux 6.18.33.1-microsoft-standard-WSL2
```

## The Most Basic Traversal: Index-Based for Loops

Everything starts here. Suppose we have a string of characters to print one by one. Most people would subconsciously write the three-part `for`:

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

This code actually hides two implicit assumptions that we use so habitually we never think about them. First, it assumes the container supports `operator[]` index-based access; second, it assumes the container knows its own `size()`. `std::array`, `std::vector`, and `std::string` all satisfy these two conditions, so it runs fine. But switch to `std::list` or `std::set`—which don't have index-based access—and this code won't compile. The same "traversal" logic needs to be rewritten for a different container, which is exactly the sign of insufficient abstraction.

But let's not rush to abstract. Whether index-based loops should be used, and when, is a nuanced question, but it's not the focus here. What we care about is: **it expresses "traversal," but it tightly couples traversal with "the container happens to use contiguous storage and happens to support indexing."** We want to extract the former on its own.

## A Different Perspective: Traversal with Pointers

Shah showed an alternative approach on his slides, and I was momentarily surprised—this works too? Instead of using indices, he gets the starting address of the array and walks through it with pointers:

```cpp
char* begin = message.data();
char* end   = message.data() + message.size();
for (char* p = begin; p != end; ++p) {
    std::cout << *p;
}
```

Here, `data()` returns the address of the first element of the underlying array, and `end` is the starting address plus the number of elements—pointer arithmetic. Then inside the loop body, `*p` dereferences and `++p` advances one step. The output is exactly the same as the index-based version, but the perspective is completely different: **we no longer rely on the "index" abstraction, but directly manipulate "addresses."**

Why switch to this perspective? Shah's motivation is straightforward—**generalization**. Indexing assumes "contiguous storage + random access," but in reality, many data structures aren't contiguous: linked lists, trees, graphs. How do you `tree[i]` a binary tree? You can't index it with an integer. But "starting from some point and stepping to the next element" is the common core of all data structure traversals. Pointer `++` is just the simplest implementation of "stepping to the next."

:::tip A brief note on the origins of STL
Abstracting "incrementing a pointer" into a replaceable object was the work of Alexander Stepanov and Meng Lee at HP Labs in the 1990s—this was the prototype of STL, submitted to the committee in 1993–94, and later incorporated into the C++98 standard. Iterators were born from the very beginning to "decouple algorithms from data structures," not added as an afterthought.
:::

## Iterators: Generalizing Pointers

Since "stepping to the next element" can have different implementations, we might as well abstract it into a type—this is the **iterator**. The first sentence about iterators on cppreference is: **"Iterators are a generalization of pointers"**<RefLink :id="1" preview="cppreference, Iterator library — iterators are a generalization of pointers" />.

We use the `std::begin` and `std::end` free function pair to get the begin and end iterators of a container:

```cpp
for (auto it = std::begin(message); it != std::end(message); ++it) {
    std::cout << *it;
}
```

See? The code looks almost identical to the pointer version—`begin`, `end`, `!=`, `++`, `*`. The only difference is that the type of `it` is no longer `char*`, but an object that "behaves like a pointer." Switch to `std::list` or `std::set`, and this code runs without changing a single word (as long as their iterators support these operations). Abstraction starts paying us back here.

There are two details worth pausing on. The first is that `begin()` points to the first element, while `end()` points to **one past the last element** (one-past-the-end), and it cannot be dereferenced itself. This half-open interval `[begin, end)` convention wasn't chosen arbitrarily: **it makes checking for an "empty container" extremely natural**—an empty container is simply `begin == end`, the loop condition is directly false, and no special case is needed. If `end` pointed to the last element itself, then an empty container wouldn't have a "last element," making it awkward to handle.

The second detail is the difference between the **free function** form of `std::begin` / `std::end` and the **member function** form of `.begin()` / `.end()` on containers.

:::warning Shah wasn't quite accurate here
In his talk, Shah said "only some containers have `.begin()` and `.end()`, but not all containers do, so free functions are more universal"—this statement is actually **inaccurate**. The fact is: **all STL containers have `.begin()` / `.end()` member functions**, without exception.

The true value of the free functions `std::begin` / `std::end` lies in three things: first, they provide overloads for **raw arrays** (like `int arr[5]`)—arrays don't have member functions, so you can only get begin/end pointers through free functions; second, they make **generic code** more uniform (no need to distinguish between "this is a container or an array" in templates); third, C++20's `std::ranges::begin` can also handle sentinels and proxy types (like `vector<bool>`). So a more accurate statement would be: **free functions are more uniform for built-in arrays and custom types, not "some containers lack member functions."**
:::

## The Iterator Category Hierarchy: Not All Iterators Are Created Equal

At this point, Shah said in his talk, "I won't go into iterator categories," and skipped it. But this is exactly where beginners stumble the most. Since this article is an in-depth adaptation, let's fill in that gap—this is the **main event** of this article.

Not all iterators have the same capabilities. An iterator of `std::vector` can `it + 5` to jump five positions at once, but an iterator of `std::list` can't—it can only `++` step by step. The standard divides iterators into several **categories** by capability, from weakest to strongest roughly: input → forward → bidirectional → random access → contiguous (added in C++20).

The key question is: **how do you know which category a given iterator belongs to?** Before C++20, it relied on a type trait called `std::iterator_traits<T>::iterator_category` (a tag type); after C++20, it changed to a set of **concepts**, such as `std::random_access_iterator<T>` and `std::contiguous_iterator<T>`. These two systems coexist in C++20, but they can give **different** answers for the same iterator—behind this lies a very important evolution.

I wrote a small program using GCC 16.1.1 to print both sets of results for common containers:

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

// 旧的 C++98 风格：从 iterator_traits 取 tag
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

// 新的 C++20 风格：用 concept 探测
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

See the pattern? **The most interesting parts are the first few lines and the last line.** `std::array`, `std::vector`, `std::string`, and the raw pointer `int*`—their old tags are all `random_access`, but the C++20 concept probe reveals them as `contiguous_iterator`.

This is the problem: **the old tag system simply didn't have a `contiguous` (contiguous) tier** (`contiguous_iterator_tag` was only added in C++20). Before C++20, the `iterator_category` of `int*` could only be tagged as `random_access`, with no way to express the stronger property that "this memory is not only randomly accessible but also physically contiguous." Why does this distinction matter? Because "contiguous storage" means you can safely treat the underlying data of the iterator as a contiguous block of memory and feed it to a C interface (like `memcpy`, CUDA kernels, or SIMD instructions)—whereas `std::deque` also supports `it + 5`, but its internal storage is chunked and **not contiguous**, so its concept is `random_access_iterator` rather than `contiguous`.

:::tip This is where concepts outshine tags
The old tags form an inheritance chain (`random_access_iterator_tag` inherits from `bidirectional_iterator_tag` inherits from...), with limited expressive power that can only layer. C++20 concepts are a set of **orthogonal, composable constraints** that can precisely express that "randomly accessible" and "contiguously stored" are two independently satisfiable properties. This is also why the entire Ranges system had to wait for C++20 concepts to land before entering the standard—without concepts, many constraints simply couldn't be expressed. For a more systematic explanation of concepts, see the relevant articles in vol4, and we'll also use them when we cover Ranges in part three.
:::

## Iterator Arithmetic and std::advance

With the category concept in mind, iterator arithmetic operations become clear. For random access iterators, you can directly `it + 5`, `it - 2`, and `it1 - it2` (compute distance), all in O(1). But for bidirectional or forward iterators, `it + 5` simply won't compile—they only understand `++` and `--`.

So if I'm writing generic code and want to "advance n steps" without restricting the iterator category, what do I do? The standard library provides `std::advance`<RefLink :id="2" preview="cppreference, std::advance — advances an iterator by n positions" />:

```cpp
auto it   = std::begin(message);
auto last = std::end(message);
std::ptrdiff_t available = std::distance(it, last);
if (5 < available) {
    std::advance(it, 5);   // 安全：确认走得到
}
```

The beauty of `std::advance` is that it **automatically selects the implementation** based on the iterator category: pass it a `vector::iterator`, and it uses `it + n` (O(1)); pass it a `list::iterator`, and it degrades to n calls of `++` (O(n)). The same calling interface, but different algorithmic complexity behind the scenes—this is the sweet spot of generic programming.

:::warning advance doesn't do bounds checking
But one thing must be noted: **`std::advance` doesn't check bounds on its own**. If you tell it to advance 100 steps but the container only has five elements, it won't raise an error—it'll just go out of bounds, and dereferencing it means a segfault (UB). That's why in the code above, I first used `std::distance` to calculate the remaining length and made a check. In practice, if you want iterators with bounds checking, GCC/Clang can add the `-D_GLIBCXX_DEBUG` compile macro, which makes standard library iterators carry bounds detection in debug mode—we'll use it to catch a real out-of-bounds bug in the next article. The MSVC equivalent is `_ITERATOR_DEBUG_LEVEL=2`.
:::

## range-based for: Syntactic Sugar for Loops

After all this talk about iterators, let's return to everyday coding—most of the time, we don't hand-write `for (auto it = begin; it != end; ++it)`, but instead use the **range-based for loop** from C++11:

```cpp
for (char c : message) {
    std::cout << c;
}
```

Clean, hard to get wrong, no need to worry about `end`. But what's really behind this syntactic sugar? It's actually an equivalent rewrite of the hand-written iterator loop above. Per the standard<RefLink :id="3" preview="cppreference, Range-based for loop — equivalent expansion" />, it's roughly equivalent to:

```cpp
{
    auto&& __range = message;
    auto  __begin  = std::begin(__range);   // 或 __range.begin()
    auto  __end    = std::end(__range);     // 或 __range.end()
    for (; __begin != __end; ++__begin) {
        char c = *__begin;
        std::cout << c;                      // 你的循环体
    }
}
```

This explains a common confusion: **how does range-based for know to call `begin`/`end`?** The answer is that the compiler inserts these two calls for you behind the scenes. It first takes `__range`, then gets the begin and end iterators, and then it's just a normal iterator loop. So range-based for has no additional requirements on iterator categories—as long as your type can provide `begin`/`end` (member or free functions both work), it can be used. This is also why, later on, our custom types only need to implement these two functions to directly work with range-based for.

If you're traversing a key-value container like `std::map`, C++17's **structured binding** combined with range-based for is extremely handy:

```cpp
const std::map<std::string, int> scores{
    {"alice", 90}, {"bob", 85}
};

for (const auto& [name, score] : scores) {
    std::cout << name << ": " << score << '\n';
}
```

:::warning Adding a version number for structured binding
Shah used structured binding in his talk but **didn't mark which standard it belongs to**—let's fill that in: **structured binding was introduced in C++17 (proposal P0217)**<RefLink :id="4" preview="cppreference, Structured binding declaration (since C++17)" />. If your project is still on C++14, this code won't compile.

Also, Shah mentioned that "ellipsis syntax can further unpack," but this description is actually a bit vague. Structured binding itself doesn't support variadic unpacking (the number of elements it binds is fixed and must match the number of members in the right-hand type); ellipses in C++ belong to the context of template parameter pack expansion and fold expressions, which are not the same thing as structured binding. I'd suggest treating that remark as a slip of the tongue and not reading too much into it.
:::

## Experiment: Do range-based for and Hand-Written Loops Compile to the Same Thing?

Whenever I tell people "range-based for is just syntactic sugar," some are skeptical—do those `__range`, `__begin`, and `__end` temporary variables slow things down? Let's test it. I wrote the same "sum" operation in four different styles:

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

Then I turned on `-O2` to have the compiler generate assembly:

```bash
❯ g++ -std=c++20 -O2 -S codegen.cpp -o codegen.s
```

If you dig into the `.s` file and look at the hot loops of these four functions, you'll find they all uniformly look like this (using `sum_rangefor` as an example):

```asm
.L19:
    addl    (%rax), %edx      ; s += *p
    addq    $4, %rax          ; p++  (int 占 4 字节)
    cmpq    %rcx, %rax        ; p == e ?
    jne     .L19              ; 不等就继续
```

The loop bodies generated by all four styles are **nearly identical at the byte level**—at `-O2`, the compiler reduces all those temporary variables, index calculations, and pointer arithmetic to the same `add / cmp / jne`. In other words, **range-based for has zero additional overhead when optimization is enabled**, so you can confidently use it for readability. The cost only appears at `-O0` (no optimization): those `__begin`/`__end` temporaries dutifully exist on the stack, but who pursues performance at `-O0` anyway?

:::tip A small pitfall fixed in C++17
By the way, a brief note on the history of range-based for itself: it entered the standard in C++11 (proposal N2930). But the C++11 version's expansion rules had a flaw—it would re-evaluate `__end` on every loop iteration (or rather, the caching strategy for `.end()` was unfriendly to certain proxy types). C++17 (proposal P0184) specifically fixed this, making `__end` evaluated only once at the start of the loop. So the range-based for you use today is the C++17 revised version, which is more robust. This also reminds us: use the newest standard you can, as many "syntactic sugars" have been quietly polished in subsequent versions.
:::

## A Pair of Iterators Is a Range

At this point, we can draw a complete line for "traversal": **a begin iterator `begin`, plus an end marker `end`, stepping through with `++`**—this pair of iterators defines a traversable piece of data. The standard library calls this "pair of iterators" a **range**<RefLink :id="5" preview="cppreference, Ranges library — a range is defined by begin and end" />.

Why is this concept important? Because it completely decouples "where the data is" from "how to process the data." If I write a sum function that accepts a pair of iterators, it works for `vector`, `list`, `set`, or even a hand-rolled linked list—as long as these containers can provide iterators that meet the requirements. Algorithms are no longer tied to a specific container type.

And the iterator abstraction itself is actually a classic design pattern—the **Iterator pattern**, a behavioral pattern from GoF's *Design Patterns*. Its core idea is "providing a way to access the elements of an aggregate object sequentially without exposing its underlying representation." C++ made it a language-level facility (the conventions of `begin`/`end`/`operator++`/`operator*`), so that any type following this convention can plug into the entire STL algorithm ecosystem.

This definition of "a pair of iterators is a range" is precisely the predecessor of the `std::ranges::range` concept we'll cover in part three. The difference is that C++20's range concept allows `end` to return a sentinel of a **different type from `begin`**—this unlocks some interesting capabilities (for example, when traversing a C string ending with `'\0'`, you don't need to calculate the length first). We'll save that for part three.

## What We've Clarified So Far

Starting from the most primitive index-based `for`, we saw how "traversal" was abstracted step by step: index-based loops tightly coupled traversal with "contiguous storage + random access"; pointer traversal liberated it to the "address" level; iterators further abstracted it into "an object that can `++` and `*`," thereby decoupling algorithms from data structures. We also filled in the iterator category hierarchy that Shah skipped, and used GCC 16.1.1 to empirically verify a key fact: **the old tags broadly label `vector`/`string`/raw pointers as `random_access`, while C++20 concepts can precisely state that they're actually the stronger `contiguous_iterator`**—this is exactly why concepts outshine tags, and why Ranges had to wait for C++20 to land.

The core takeaway is one sentence: **a pair of iterators (one `begin`, one `end`) defines a range, and STL algorithms are built on top of this pair of iterators.**

In the next article, we'll hand this pair of iterators to STL algorithms—seeing how `std::sort`, `std::partition`, and `std::transform` work as "loop replacements," and what hard requirements they have on iterator categories (for example, why `std::sort` can't be used on `std::list`). There are also a few classic iterator pitfalls waiting for us there: iterator invalidation, mismatched `begin`/`end`, and reversed argument order. If you want to review container memory layouts first, vol3's [span: A View That Doesn't Own Data](../../../vol3-standard-library/02-span.md) and the container-related articles are excellent prerequisite reading.

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
    chapter="Automatically selects implementation complexity by iterator category"
  />
  <ReferenceItem
    :id="3"
    author="cppreference.com"
    title="Range-based for loop (since C++11)"
    :year="2026"
    url="https://en.cppreference.com/w/cpp/language/range-for"
    chapter="Equivalent expansion to begin/end iterator loop"
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
    chapter="C++20 introduces the contiguous category and concept system"
  />
  <ReferenceItem
    :id="7"
    author="Mike Shah"
    title="Back to Basics: C++ Ranges — CppCon 2025"
    :year="2025"
    url="https://www.youtube.com/watch?v=Q434UHWRzI0"
  />
</ReferenceCard>
