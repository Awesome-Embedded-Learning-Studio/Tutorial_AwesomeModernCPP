---
title: 'From Loops to Iterators: The Path to Data Traversal Abstraction'
description: 'CppCon 2025 Talk Notes — Mike Shah: From for loops and pointer traversal
  to iterator abstractions, completing the iterator category hierarchy, and benchmarking
  legacy tags versus C++20 concepts with GCC 16.1.1'
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
  source_hash: 0af4b0bc780951002c86a5ff9e3f3696ce0f9d21f0aa5b0bf97370a1e08e0f8a
  translated_at: '2026-06-15T09:08:29.981722+00:00'
  engine: anthropic
  token_count: 4007
---
# From Loops to Iterators: The Path to Abstracting Data Traversal

:::tip
This article is based on a deep dive into CppCon 2025's "Back to Basics: C++ Ranges" by Mike Shah. The YouTube link is above. This series is planned to be split into three parts: this part clarifies the thread of "traversing data" (loops → pointers → iterators → range-based for), the second part covers STL algorithms and iterator pitfalls, and the third part officially enters Ranges, Views, and pipeline composition. The experimental environment is Arch Linux WSL, GCC 16.1.1, compiler flag `-std=c++20`.
:::

Mike Shah opened his talk with a simple statement that feels more profound the more I think about it: **an algorithm is essentially a loop**. He mentioned reading a 2012 paper on the empirical evaluation of algorithm performance during his graduate studies, which gave him a realization: when facing an unfamiliar codebase and wanting to figure out "where the computation actually happens," the fastest way is to look for the loops in the program. Since we as engineers spend half our time **transforming data** and the other half **storing data**, loops are the most direct vehicle for "transforming data."

:::warning A caveat on Shah's statement
"Algorithm = Loop" is a "gross oversimplification" that he repeatedly emphasized, so just take it for what it's worth. Strictly speaking, an algorithm is a finite sequence of steps to solve a problem—recursive algorithms, parallel algorithms (`<execution>`), and coroutine-based algorithms don't necessarily look like `for`. Loops are just one of the most common carriers. However, as an entry point to understanding STL and Ranges, this simplification is useful: **understand loops first, then see how STL abstracts them away.**
:::

In this article, we will start with the most primitive indexed loop and see step-by-step how C++ abstracts "traversing data" layer by layer. Our destination is not Ranges (that's the third part), but **iterators**—the bridge connecting "loops" and "algorithms."

First, let's lay out the experimental environment; all subsequent outputs are based on this:

```bash
❯ g++ --version
g++ (GCC) 16.1.1 20260430

❯ uname -sr
Linux 6.18.33.1-microsoft-standard-WSL2
```

## The Most Primitive Traversal: Indexed for Loop

Everything starts here. Suppose we have a string of characters to print one by one. Most people subconsciously write the three-part `for`:

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

This code actually hides two implicit assumptions that we use so habitually we don't think about them. First, it assumes the container supports `operator[]` subscript access; second, it assumes the container knows its own `size()`. `std::array`, `std::vector`, and `std::string` satisfy these two conditions, so it runs fine. But as soon as you switch to `std::list` or `std::set`—which don't have subscript access—this code won't compile. The same "traversal" logic requires rewriting when the container changes, which is a signal of insufficient abstraction.

But let's not rush to abstract. Whether indexed loops should be used and when is a nuanced issue, but not the focus here. We care about this: **it expresses "traversal," but it binds traversal to the fact that "the container happens to be contiguous storage and happens to support subscripts."** We want to extract the former separately.

## Changing Perspective: Traversing with Pointers

Shah switched to a different style on his slides, and I paused for a moment—this actually works? Instead of subscripts, he gets the address of the first element of the array and uses pointers to walk:

```cpp
char* begin = message.data();
char* end   = message.data() + message.size();
for (char* p = begin; p != end; ++p) {
    std::cout << *p;
}
```

Here, `data()` returns the address of the first element of the underlying array, and `end` is the first address plus the number of elements—pointer arithmetic. Then inside the loop, `*p` dereferences and `++p` advances one step. The result is identical to the indexed version, but the perspective is completely different: **we no longer rely on the "subscript" abstraction, but directly manipulate "addresses."**

Why switch perspectives? Shah's motivation is direct—**generalization**. Subscripts assume "contiguous storage + random access," but in reality, many data structures are not contiguous: linked lists, trees, graphs. How do you `tree[i]` a binary tree? You can't use an integer to index it. But "starting from a certain point and walking step-by-step to the next element" is the common core of all data structure traversals. Pointer `++` is just the simplest implementation of "go to next."

:::tip A brief history of STL
Abstracting "incrementing a pointer" into a replaceable object was the work done by Alexander Stepanov and Meng Lee at HP Labs in the 90s—this is the prototype of STL, submitted to the committee in 1993–94, and later merged into the C++98 standard. Iterators were born from the start to "decouple algorithms from data structures," not added as an afterthought.
:::

## Iterators: Generalization of Pointers

Since "going to the next element" can have different implementations, let's abstract it into a type—this is the **iterator**. The first sentence on cppreference about iterators is: **"Iterators are a generalization of pointers"**<RefLink :id="1" preview="cppreference, Iterator library — iterators are a generalization of pointers" />.

We use the `std::begin` and `std::end` free functions to get the iterators for the beginning and end of the container:

```cpp
for (auto it = std::begin(message); it != std::end(message); ++it) {
    std::cout << *it;
}
```

See, the writing is almost identical to the pointer version—`begin`, `end`, `!=`, `++`, `*`. The only difference is that the type of `it` is no longer `char*`, but an object that "behaves like a pointer." Switch to `std::list` or `std::set`, and this code runs without changing a single word (as long as their iterators support these operations). Abstraction starts to pay off here.

There are two details worth stopping for. First, `begin()` points to the first element, while `end()` points to **one past the last element**; it cannot be dereferenced itself. This half-open interval `[begin, end)` convention wasn't chosen arbitrarily: **it makes checking for an "empty container" extremely natural**—an empty container is just `begin == end`, the loop condition is directly false, and no special handling is needed. If `end` pointed to the last element itself, then an empty container wouldn't have a "last element," making handling awkward.

The second detail is the difference between these **free functions** `std::begin` / `std::end` and the container's **member functions** `.begin()` / `.end()`.

:::warning Shah wasn't quite accurate here
Shah said in the talk, "Only some containers have `.begin()`, `.end()`, but not all containers have them, so free functions are more generic"—this statement is actually **inaccurate**. The fact is: **all STL containers have `.begin()` / `.end()` member functions**, without exception.

The true value of the free functions `std::begin` / `std::end` lies in three things: first, they are overloaded for **raw arrays** (like `int arr[5]`)—arrays have no member functions, so you must rely on free functions to get the beginning and end pointers; second, they make writing **generic code** more uniform (no need to distinguish between "container vs array" in templates); third, C++20's `std::ranges::begin` can also handle sentinels and proxy types (like `vector<bool>`). So a more accurate statement is: **free functions are more uniform for built-in arrays and custom types, not "some containers lack member functions."**
:::

## Iterator Category Hierarchy: Not All Iterators Are Created Equal

At this point, Shah in the talk simply said, "I won't go into iterator categories," and skipped it. But this is exactly where beginners are most likely to stumble, so since this article is a deep dive, we'll fill it in—this is the **highlight** of this part.

Not all iterators have the same capabilities. `std::vector`'s iterator can `it + 5` jump five steps at once, but `std::list`'s iterator cannot; it can only `++` step by step. The standard divides iterators into several **categories** by capability, from weak to strong: Input → Forward → Bidirectional → Random Access → Contiguous (added in C++20).

The key question is: **how do you know which category a certain iterator belongs to?** Before C++20, it relied on a type trait called `std::iterator_traits<T>::iterator_category` (a tag type); after C++20, it changed to a set of **concepts**, like `std::random_access_iterator<T>` and `std::contiguous_iterator<T>`. These two systems coexist in C++20, but they may give **different** answers for the same iterator—this hides a very important evolution.

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

See the trick? **The most interesting parts are the first few lines and the last line.** `std::array`, `std::vector`, `std::string`, and raw pointers `int*`—their old tags are all `random_access`, but the C++20 concept detects them as `contiguous_iterator`.

This is the problem: **in the old tag system, there was no `contiguous` (contiguous) level at all** (`contiguous_iterator_tag` was only added in C++20). Before C++20, `int*`'s `iterator_category` could only be marked as `random_access`, unable to express the stronger property that "this memory is not only randomly accessible but also physically contiguous." Why is this distinction important? Because "contiguous storage" means you can safely treat the iterator's underlying data as a block of contiguous memory and feed it to a C interface (like `memcpy`, CUDA kernels, or SIMD instructions)—while `std::deque` also supports `it + 5`, its internal storage is segmented, **not contiguous**, so its concept is `random_access_iterator` rather than `contiguous`.

:::tip This is where concepts beat tags
Old tags are an inheritance chain (`random_access_iterator_tag` inherits from `bidirectional_iterator_tag` inherits from...), with limited expressive power, only able to layer. C++20 concepts are a set of **orthogonal, composable constraints**, capable of precisely stating that "random access" and "contiguous storage" are two things that can exist independently. This is also why the entire Ranges system had to wait for C++20's concepts to land before entering the standard—without concepts, many constraints simply cannot be expressed. For a more systematic explanation of concepts, see the relevant articles in vol4; we will also use them in the third part when discussing Ranges.
:::

## Iterator Arithmetic and std::advance

With the concept of categories, let's look at iterator arithmetic operations again. For random access iterators, you can directly `it + 5`, `it - 2`, and `it1 - it2` (calculate distance), all O(1). But for bidirectional or forward iterators, `it + 5` simply won't compile—they only recognize `++` and `--`.

So if I'm writing generic code and want to "move forward n steps" without limiting the iterator category, what do I do? The standard library provides `std::advance`<RefLink :id="2" preview="cppreference, std::advance — advances an iterator by n positions" />:

```cpp
auto it   = std::begin(message);
auto last = std::end(message);
std::ptrdiff_t available = std::distance(it, last);
if (5 < available) {
    std::advance(it, 5);   // 安全：确认走得到
}
```

The beauty of `std::advance` is that it **automatically selects the implementation** based on the iterator category: pass it `vector::iterator`, it uses `it + n` (O(1)); pass it `list::iterator`, it degrades to n times `++` (O(n)). The same call interface, different algorithmic complexity behind the scenes—this is the sweetness of generic programming.

:::warning advance does not check boundaries
But one thing must be reminded: **`std::advance` does not check boundaries itself**. If you ask it to move forward 100 steps and there are only 5 elements in the container, it won't error; it will just go out of bounds—dereferencing is a segfault (UB). That's why in the code above, I first used `std::distance` to calculate the remaining length and made a judgment. In practice, if you want iterators with boundary checking, GCC/Clang can add the `-D_GLIBCXX_DEBUG` compile macro, making standard library iterators carry bounds checking in debug mode—we'll use this in the next part to catch a real out-of-bounds bug. MSVC's equivalent is `_ITERATOR_DEBUG_LEVEL=2`.
:::

## range-based for: Syntactic Sugar for Loops

After talking about iterators for so long, let's return to daily coding—we rarely hand-write `for (auto it = begin; it != end; ++it)` anymore, instead using the **range-based for loop** introduced in C++11:

```cpp
for (char c : message) {
    std::cout << c;
}
```

Clean, hard to get wrong, no need to worry about `end`. But what exactly is behind this syntactic sugar? It's actually the equivalent rewrite of the hand-written iterator loop above. According to the standard<RefLink :id="3" preview="cppreference, Range-based for loop — equivalent expansion" />, it is roughly equivalent to:

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

This explains a common confusion: **how does range-based for know to call `begin`/`end`?** The answer is the compiler inserts these two lines for you behind the scenes. It first gets `__range`, then takes the beginning and end iterators, and then it's just a normal iterator loop. So range-based for has no additional requirements for iterator categories—as long as your type can provide `begin`/`end` (member or free functions both work), it can be used. This is why later we can customize types as long as they implement these two functions, and they can be plugged directly into range-based for.

If traversing a key-value container like `std::map`, C++17's **structured binding** combined with range-based for is very handy:

```cpp
const std::map<std::string, int> scores{
    {"alice", 90}, {"bob", 85}
};

for (const auto& [name, score] : scores) {
    std::cout << name << ": " << score << '\n';
}
```

:::warning Adding a version number for structured binding
Shah used structured binding in the talk, but **didn't mark which standard feature it was**—let's add that here: **structured binding was introduced in C++17 (proposal P0217)**<RefLink :id="4" preview="cppreference, Structured binding declaration (since C++17)" />. If your project is still on C++14, this code won't compile.

Also, Shah mentioned "ellipsis syntax can further unpack," which is actually a bit vague. Structured binding itself doesn't support variadic unpacking (the number of elements it binds is fixed and must match the number of members of the type on the right); ellipses in C++ belong to the context of template parameter pack expansion and fold expressions, not the same thing as structured binding. It's recommended to treat this as a slip of the tongue and not dig too deep.
:::

## Experiment: Do range-based for and Hand-written Loops Compile the Same?

Every time I tell people "range-based for is just syntactic sugar," some are skeptical—won't those `__range`, `__begin`, and `__end` temporary variables slow down performance? Let's test it. I wrote the same "summation" in four ways:

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

Then turn on `-O2` to let the compiler generate assembly:

```bash
❯ g++ -std=c++20 -O2 -S codegen.cpp -o codegen.s
```

Go to the `.s` file and look for the hot loops of these four functions, and you'll find they uniformly look like this (taking `sum_rangefor` as an example):

```asm
.L19:
    addl    (%rax), %edx      ; s += *p
    addq    $4, %rax          ; p++  (int 占 4 字节)
    cmpq    %rcx, %rax        ; p == e ?
    jne     .L19              ; 不等就继续
```

The loop bodies generated by the four methods are **byte-level almost identical**—the compiler, under `-O2`, reduced all those temporary variables, subscript calculations, and pointer arithmetic to the same `add / cmp / jne`. This means **range-based for has no additional overhead once optimization is enabled**, so you can use it freely for readability. The cost only appears at `-O0` (no optimization): those `__begin`/`__end` temporaries will faithfully exist on the stack, but who pursues performance under `-O0`?

:::tip A small pitfall fixed in C++17
By the way, a brief history of range-based for itself: it entered the standard in C++11 (proposal N2930). But the C++11 version's expansion rule had a flaw—it would re-evaluate `__end` every loop (or rather, the caching strategy for `.end()` was unfriendly to some proxy types). C++17 (proposal P0184) specifically fixed this, making `__end` evaluated only once at the start of the loop. So the range-based for you use today is the version revised in C++17, more stable. This also reminds us: use the new standard as much as possible; many "syntactic sugars" have been quietly polished in subsequent versions.
:::

## A Pair of Iterators is a Range

Here we can draw a complete line for "traversal": **a start iterator `begin`, plus an end marker `end`, stepping through with `++`**—this pair of iterators defines a traversable span of data. The standard library calls this "pair of iterators" a **range**<RefLink :id="5" preview="cppreference, Ranges library — a range is defined by begin and end" />.

Why is this concept important? Because it completely decouples "where the data is" from "how to process the data." If I write a summation function that can accept a pair of iterators, it applies to `vector`, `list`, `set`, and even a hand-written linked list—as long as those containers can provide compliant iterators. Algorithms are no longer bound to a specific container.

And the iterator abstraction itself is actually a classic design pattern—**Iterator pattern**, belonging to the behavioral patterns in GoF's *Design Patterns*. Its core idea is to "provide a method to access the elements of an aggregate object sequentially without exposing its internal representation." C++ makes it a language-level facility (the conventions of `begin`/`end`/`operator++`/`operator*`), allowing any type that follows this convention to plug into the entire STL algorithm ecosystem.

This definition of "a pair of iterators as a range" is the predecessor of the `std::ranges::range` concept we will discuss in the third part. The difference is that C++20's range concept allows `end` to return a **sentinel of a different type than `begin`**—this unlocks some interesting capabilities (for example, when traversing a C string ending in `'\0'`, you don't need to calculate the length first). We'll leave this for the third part.

## What Have We Cleared Up So Far?

Starting from the most primitive indexed `for`, we saw how "traversal" was abstracted step by step: indexed loops bind traversal to "contiguous storage + random access"; pointer traversal liberated it to the "address" level; iterators further abstracted it into "an object that can `++` and `*`," decoupling algorithms from data structures. We also filled in the iterator category system that Shah skipped, and used GCC 16.1.1 to verify a key fact: **old tags broadly label `vector`/`string`/raw pointers as `random_access`, while C++20 concepts can precisely state they are actually stronger `contiguous_iterator`**—this is exactly why concepts are better than tags, and why Ranges had to wait for C++20 to land.

The core is one sentence: **a pair of iterators (one `begin`, one `end`) defines a range, and STL algorithms are built on this pair of iterators.**

In the next part, we will hand this pair of iterators to STL algorithms—seeing how "loop substitutes" like `std::sort`, `std::partition`, and `std::transform` are used, and what hard requirements they have for iterator categories (e.g., why `std::sort` cannot be used on `std::list`). There are also classic iterator pitfalls waiting for us: iterator invalidation, mismatched `begin`/`end`, reversed parameter order. If you want to review the memory layout of containers first, vol3's [span: A View That Doesn't Own Data](../../../../vol3-standard-library/08-span.md) and container-related articles are good prerequisite reading.

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
    chapter="Automatically selects implementation complexity based on iterator category"
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
    chapter="C++20 introduced contiguous category and concept system"
  />
  <ReferenceItem
    :id="7"
    author="Mike Shah"
    title="Back to Basics: C++ Ranges — CppCon 2025"
    :year="2025"
    url="https://www.youtube.com/watch?v=Q434UHWRzI0"
  />
</ReferenceCard>
