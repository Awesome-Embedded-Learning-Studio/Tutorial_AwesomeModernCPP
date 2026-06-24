---
chapter: 3
conference: cppcon
conference_year: 2025
cpp_standard:
- 11
- 17
- 20
description: 'CppCon 2025 Talk Notes — Mike Shah: From for loops and pointer traversal
  to iterator abstractions, completing the iterator category hierarchy and benchmarking
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
  translated_at: '2026-06-24T00:32:15.327574+00:00'
  engine: anthropic
  token_count: 4012
---
# From Loops to Iterators: The Path to Abstracting Data Traversal

:::tip
This article is based on a deep adaptation of Mike Shah's "Back to Basics: C++ Ranges" from CppCon 2025. The YouTube link is above. This series is planned to be split into three parts: this part focuses on the thread of "traversing data" (loops → pointers → iterators → range-based for), the second part covers STL algorithms and iterator pitfalls, and the third part officially dives into Ranges, Views, and pipeline composition. The experimental environment is Arch Linux WSL, GCC 16.1.1, with compiler flag `-std=c++20`.
:::

Mike Shah opened his talk with a simple statement that I've come to appreciate more the more I think about it: **an algorithm is essentially a loop**. He mentioned reading a 2012 paper on empirical algorithm performance evaluation during his graduate studies, which gave him a key insight: when facing an unfamiliar codebase and wanting to figure out "where the computation actually happens," the fastest way is to look for the loops in the program. Since we as engineers spend half our time **transforming data** and the other half **storing data**, loops are the most direct vehicle for that "data transformation" work.

:::warning A caveat for Instructor Shah
"Algorithm = Loop" is a "gross oversimplification" that he emphasizes repeatedly, so we should just get the gist of it. Strictly speaking, an algorithm is a finite sequence of steps to solve a problem—recursive algorithms, parallel algorithms (`<execution>`), and coroutine-based algorithms don't necessarily look like a `for` loop. Loops are just one of the most common vehicles. However, as an entry point to understanding the STL and Ranges, this simplification is useful: **understand loops first, then see how the STL abstracts them away.**
:::

In this article, we will start with the most primitive indexed loops and step-by-step examine how C++ abstracts "data traversal" layer by layer. Our destination is not Ranges (that's the third part), but **iterators**—the bridge connecting "loops" and "algorithms."

First, let's lay out the experimental environment; all subsequent outputs are based on it:

```bash
❯ g++ --version
g++ (GCC) 16.1.1 20260430

❯ uname -sr
Linux 6.18.33.1-microsoft-standard-WSL2
```

## The Most Basic Iteration: Index-based for Loop

Everything starts here. Suppose we have a string of characters to print one by one. Most people would instinctively write the three-part `for` loop:

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

There are actually two implicit assumptions hidden in this code that we often overlook because we use them so frequently. First, it assumes the container supports `operator[]` for subscript access. Second, it assumes the container knows its own `size()`. `std::array`, `std::vector`, and `std::string` satisfy both requirements, so the code works fine. However, as soon as we switch to `std::list` or `std::set`—which do not support subscript access—this code fails to compile. The same "traversal" logic requires rewriting just by changing the container, which is a clear signal that the abstraction is insufficient.

However, let's not rush to abstract just yet. Whether index-based loops should be used, and when, is a nuanced topic, but it is not the focus here. What we care about is this: **it expresses the concept of "traversal," but it tightly couples traversal with the specific fact that "the container happens to use contiguous storage and happens to support subscripts."** We want to extract the former concept separately.

## A Different Perspective: Traversal with Pointers

In his presentation, Shah used a different approach. At first, I was surprised—does this actually work? Instead of using subscripts, he obtained the address of the first element of the array and walked through it using a pointer:

```cpp
char* begin = message.data();
char* end   = message.data() + message.size();
for (char* p = begin; p != end; ++p) {
    std::cout << *p;
}
```

Here, `data()` returns the address of the underlying array, and `end` is that address plus the number of elements—pointer arithmetic. Inside the loop, `*p` dereferences the pointer, and `++p` advances it. The result is identical to the indexed version, but the perspective is completely different: **we no longer rely on the "index" abstraction, but instead manipulate "addresses" directly.**

Why switch perspectives? Shah's motivation is straightforward—**generalization**. Indexing assumes "contiguous storage + random access," but many real-world data structures are not contiguous: linked lists, trees, graphs. How do you `tree[i]` on a binary tree? You cannot index it with an integer. However, the act of "starting from a point and stepping to the next element" is the common kernel of traversing all data structures. Pointer `++` is just the simplest implementation of "go to next."

:::tip A note on the origin of the STL
Abstracting "incrementing a pointer" into a replaceable object was the work done by Alexander Stepanov and Meng Lee at HP Labs in the 90s—this was the prototype of the STL, submitted to the committee in 1993–94, and later merged into the C++98 standard. Iterators were born from the start to "decouple algorithms from data structures," not added as an afterthought.
:::

## Iterators: Generalization of Pointers

Since "going to the next element" can have different implementations, we might as well abstract it into a type—this is the **iterator**. The first sentence on cppreference regarding iterators is: **"Iterators are a generalization of pointers"**<RefLink :id="1" preview="cppreference, Iterator library — iterators are a generalization of pointers" />.

We use the `std::begin` and `std::end` free functions to obtain the iterators for the beginning and end of a container:

```cpp
for (auto it = std::begin(message); it != std::end(message); ++it) {
    std::cout << *it;
}
```

You see, the syntax is almost identical to the pointer version—`begin`, `end`, `!=`, `++`, `*`. The only difference is that the type of `it` is no longer `char*`, but rather an object that "behaves like a pointer." If we swap this for `std::list` or `std::set`, this code runs without changing a single word (as long as their iterators support these operations). Abstraction begins to pay us back here.

There are two details worth pausing for. First, `begin()` points to the first element, while `end()` points to **one past the last element**. It cannot be dereferenced itself. This half-open range `[begin, end)` convention wasn't chosen arbitrarily: **it makes checking for an "empty container" extremely natural**—an empty container is simply `begin == end`, so the loop condition evaluates to false immediately, requiring no special-case logic. If `end` pointed to the last element itself, an empty container wouldn't have a "last element," making handling it awkward.

The second detail is the difference between the **free function** form, `std::begin` / `std::end`, and the member function form, `.begin()` / `.end()`.

:::warning Shah's inaccuracy here
In his talk, Shah says, "Only some containers have `.begin()` and `.end()`, but not all, so free functions are more generic"—this statement is actually **inaccurate**. The fact is: **all STL containers have `.begin()` / `.end()` member functions**, without exception.

The real value of the free functions `std::begin` / `std::end` lies in three things: first, they provide overloads for **native arrays** (e.g., `int arr[5]`)—arrays have no member functions, so we must rely on free functions to get pointers to the beginning and end; second, they make writing **generic code** more uniform (no need to distinguish between "container vs. array" in templates); and third, C++20's `std::ranges::begin` can even handle sentinels and proxy types (like `vector<bool>`). So a more accurate statement would be: **free functions are more uniform for built-in arrays and custom types, not "some containers lack member functions."**
:::

## Iterator Category Hierarchy: Not All Iterators Are Created Equal

At this point in the talk, Shah simply said, "I won't go into detail about iterator categories," and skipped over it. However, this is exactly where beginners are most likely to trip up. Since this article is a "re-creation," let's fill in that gap—this is also the **main event** of this post.

Not all iterators have the same capabilities. A `std::vector` iterator can jump five spots at once with `it + 5`, but a `std::list` iterator cannot; it can only advance step-by-step with `++`. The standard divides iterators into several **categories** based on their capabilities, from weakest to strongest: Input → Forward → Bidirectional → Random Access → Contiguous (added in C++20).

The key question is: **how do you know which category a given iterator belongs to?** Before C++20, we relied on a type trait called `std::iterator_traits<T>::iterator_category` (a tag type); after C++20, this changed to a set of **concepts**, such as `std::random_access_iterator<T>` and `std::contiguous_iterator<T>`. Both mechanisms coexist in C++20, but they might yield **different** answers for the same iterator—behind this lies a very important evolution.

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

See the pattern? **The most interesting parts are the first few lines and the last line.** `std::array`, `std::vector`, `std::string`, and the raw pointer `int*`—their legacy tags are all `random_access`, yet the C++20 concept detects them as `contiguous_iterator`.

This is the root of the issue: **the old tag system simply lacked a `contiguous` tier** (the `contiguous_iterator_tag` was only added in C++20). Before C++20, the `iterator_category` of `int*` could only be marked as `random_access`, making it impossible to express the stronger property that "this memory is not only randomly accessible, but also physically contiguous." Why does this distinction matter? Because "contiguous storage" means we can safely treat the underlying data of the iterator as a block of contiguous memory and pass it to a C interface (like `memcpy`, CUDA kernels, or SIMD instructions)—whereas `std::deque`, despite supporting `it + 5`, stores data internally in segmented chunks, which are **not contiguous**. Therefore, its concept is `random_access_iterator`, not `contiguous`.

:::tip Why concepts are superior to tags
Legacy tags form an inheritance chain (`random_access_iterator_tag` inherits from `bidirectional_iterator_tag`, which inherits from...), which limits their expressiveness to a simple hierarchy. C++20 concepts are a set of **orthogonal, composable constraints**, allowing us to precisely state that "random access" and "contiguous storage" are two independent properties. This is also why the entire Ranges framework had to wait for C++20 concepts to land in the standard—without concepts, many constraints simply cannot be expressed. For a more systematic explanation of concepts, check out the related articles in Vol. 4; we will also use them in Part 3 when we discuss Ranges.
:::

## Iterator Arithmetic and `std::advance`

With these categories in mind, let's look at iterator arithmetic operations. For random access iterators, we can directly use `it + 5`, `it - 2`, or `it1 - it2` (to calculate distance), all of which are O(1). However, for bidirectional or forward iterators, `it + 5` simply won't compile—they only recognize `++` and `--`.

So, if we are writing generic code and want to "advance n steps" without constraining the iterator category, what do we do? The standard library provides `std::advance`<RefLink :id="2" preview="cppreference, std::advance — advances an iterator by n positions" />:

```cpp
auto it   = std::begin(message);
auto last = std::end(message);
std::ptrdiff_t available = std::distance(it, last);
if (5 < available) {
    std::advance(it, 5);   // 安全：确认走得到
}
```

The beauty of `std::advance` lies in its ability to **automatically select the implementation** based on the iterator category: if you pass a `vector::iterator`, it uses `it + n` (O(1)); if you pass a `list::iterator`, it falls back to `n` times `++` (O(n)). The same call interface, but different algorithmic complexity behind the scenes—this is the sweet spot of generic programming.

:::warning advance does not perform bounds checking
However, one thing must be made clear: **`std::advance` does not check bounds itself**. If you ask it to advance 100 steps, but the container only has five elements, it won't report an error; instead, it will go out of bounds—dereferencing it results in a segmentation fault (UB). That is why, in the code above, I first used `std::distance` to calculate the remaining length and performed a check. In practice, if you want iterators with bounds checking, GCC/Clang allow adding the `-D_GLIBCXX_DEBUG` compiler macro, which enables standard library iterators to perform bounds detection in debug mode—we will use this in the next article to catch a real out-of-bounds bug. For MSVC, the corresponding setting is `_ITERATOR_DEBUG_LEVEL=2`.
:::

## range-based for: Syntactic Sugar for Loops

After discussing iterators for so long, let's return to daily coding—we rarely write `for (auto it = begin; it != end; ++it)` by hand. Instead, we use the **range-based for loop** introduced in C++11:

```cpp
for (char c : message) {
    std::cout << c;
}
```

Clean, less error-prone, and we don't need to worry about `end`. But what exactly is behind this syntactic sugar? In reality, it is equivalent to the hand-written iterator loop shown above. According to the standard<RefLink :id="3" preview="cppreference, Range-based for loop — equivalent expansion" />, it is roughly equivalent to:

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

This explains a common confusion: **how does the range-based for loop know to call `begin`/`end`?** The answer is that the compiler implicitly inserts these calls for you. It first captures the `__range`, then obtains the beginning and ending iterators, and finally proceeds with a standard iterator loop. Therefore, the range-based for loop imposes no additional requirements on iterator categories—as long as your type provides `begin`/`end` (either as member functions or free functions), it works. This is also why, later on, we can use custom types directly in a range-based for loop simply by implementing these two functions.

When iterating over key-value containers like `std::map`, using C++17's **structured binding** with the range-based for loop is extremely convenient:

```cpp
const std::map<std::string, int> scores{
    {"alice", 90}, {"bob", 85}
};

for (const auto& [name, score] : scores) {
    std::cout << name << ": " << score << '\n';
}
```

:::warning Adding a version note for structured binding
Shah used structured binding in his talk, but **didn't specify which standard introduced it**—so let's add that here: **Structured binding was introduced in C++17 (proposal P0217)**<RefLink :id="4" preview="cppreference, Structured binding declaration (since C++17)" />. If your project is still on C++14, this code won't compile.

Also, Shah mentioned that "ellipsis syntax can further unpack," which is actually a bit vague. Structured binding itself doesn't support variadic unpacking (the number of elements it binds is fixed and must match the number of members in the type on the right); ellipses in C++ belong to the context of template parameter pack expansion and fold expressions, which are not the same as structured binding. We suggest treating this as a slip of the tongue and not digging too deep.
:::

## Experiment: Are range-based for and hand-written loops compiled the same?

Every time we tell people that "range-based for is just syntactic sugar," some are skeptical—won't those temporary variables like `__range`, `__begin`, and `__end` slow down performance? Let's test this empirically. We wrote the same "summation" logic in four different ways:

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

Next, let's enable `-O2` and have the compiler generate assembly:

```bash
❯ g++ -std=c++20 -O2 -S codegen.cpp -o codegen.s
```

Let's examine the hot loops in the `.s` file for these four functions. We will see that they all share the exact same structure (using `sum_rangefor` as an example):

```asm
.L19:
    addl    (%rax), %edx      ; s += *p
    addq    $4, %rax          ; p++  (int 占 4 字节)
    cmpq    %rcx, %rax        ; p == e ?
    jne     .L19              ; 不等就继续
```

The loop bodies generated by these four methods are **nearly identical at the byte level**—the compiler, under `-O2`, reduces all those temporary variables, index calculations, and pointer arithmetic to the exact same sequence of `add` / `cmp` / `jne` instructions. In other words, **range-based for incurs zero overhead once optimizations are enabled**, so we can confidently use it for the sake of readability. The cost only appears at `-O0` (no optimization): those `__begin`/`__end` temporaries dutifully reside on the stack, but who chases performance while compiling without optimization?

:::tip A pitfall fixed in C++17
While we are on the topic of range-based for history: it entered the standard in C++11 (proposal N2930). However, the C++11 expansion rule had a flaw—it would re-evaluate `__end` in every iteration (or rather, the caching strategy for `.end()` was unfriendly to certain proxy types). C++17 (proposal P0184) specifically fixed this by ensuring `__end` is evaluated only once at the start of the loop. So the range-based for we use today is the revised C++17 version, which is much more robust. This also reminds us: always prefer the latest standard where possible; many "syntactic sugars" have been quietly refined in subsequent versions.
:::

## A Pair of Iterators is a Range

At this point, we can draw a complete line under "traversal": **a starting iterator `begin`, plus an end marker `end`, stepping through with `++`**—this pair of iterators defines a span of traversable data. The Standard Library calls this "pair of iterators" a **range**<RefLink :id="5" preview="cppreference, Ranges library — a range is defined by begin and end" />.

Why is this concept important? Because it thoroughly decouples "where the data is" from "how to process the data." If we write a summation function that accepts a pair of iterators, it works for `vector`, `list`, `set`, and even custom linked lists—as long as those containers provide compliant iterators. Algorithms are no longer bound to specific containers.

Furthermore, the iterator abstraction itself is a classic design pattern—**the Iterator pattern**—a behavioral pattern from GoF's *Design Patterns*. Its core idea is to "provide a method to access the elements of an aggregate object sequentially without exposing its underlying representation." C++ implements this as a language-level facility (the conventions of `begin`/`end`/`operator++`/`operator*`), allowing any type that adheres to this contract to plug into the entire STL algorithm ecosystem.

This definition of "a pair of iterators equals a range" is the precursor to the `std::ranges::range` concept we will discuss in Part 3. The difference is that the C++20 range concept allows `end` to return a **sentinel of a different type than `begin`**—this unlocks some interesting capabilities (for example, when traversing a C-style string ending in `'\0'`, we don't need to calculate the length beforehand). We'll save that deep dive for Part 3.

## What Have We Clarified So Far?

Starting from the most primitive indexed `for`, we saw how "traversal" was abstracted step-by-step: index loops tied traversal to "contiguous storage + random access"; pointer traversal liberated it to the "address" level; iterators further abstracted it into "an object that can `++` and `*`," thereby decoupling algorithms from data structures. We also filled in the iterator category system that Shah skipped, and verified a key fact with GCC 16.1.1: **old tags broadly labeled `vector`/`string`/raw pointers as `random_access`, whereas C++20 concepts can precisely state that they are actually stronger `contiguous_iterator`s**—this is exactly why concepts are superior to tags, and why Ranges had to wait for C++20 to land.

The core takeaway is one sentence: **A pair of iterators (one `begin`, one `end`) defines a range, and STL algorithms are built upon this pair.**

In the next part, we will hand this pair of iterators to STL algorithms—seeing how "loop substitutes" like `std::sort`, `std::partition`, and `std::transform` are used, and what hard requirements they have for iterator categories (for example, why `std::sort` cannot be used on `std::list`). There are also classic iterator pitfalls waiting for us there: iterator invalidation, mismatched `begin`/`end`, and reversed argument order. If you want to review the memory layout of containers first, vol3's [span: A view that doesn't own data](../../../../vol3-standard-library/containers/08-span.md) and related articles are excellent prerequisite reading.

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
