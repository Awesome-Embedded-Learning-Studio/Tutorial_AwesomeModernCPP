---
chapter: 3
conference: cppcon
conference_year: 2025
cpp_standard:
- 11
- 17
- 20
description: 'CppCon 2025 Talk Notes — Mike Shah: STL Algorithm Family in Practice,
  Hard Constraints on Iterator Categories, with an Algorithm Cheat Sheet and Invalidation
  Rules Table, Using GCC to Test Silent UB from Iterator Invalidation and Capture
  with _GLIBCXX_DEBUG'
difficulty: beginner
order: 2
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
title: STL Algorithms in Practice and Iterator Pitfalls
translation:
  engine: anthropic
  source: documents/vol10-open-lecture-notes/cppcon/2025/03-back-to-basics-ranges/02-stl-algorithms-and-iterator-pitfalls.md
  source_hash: 1cae51e3ffb476ae69dfde3639fb5f16f98e7c907185b4cb8216ef73fb3a213d
  token_count: 3893
  translated_at: '2026-06-13T02:13:47.465561+00:00'
video_youtube: https://www.youtube.com/watch?v=Q434UHWRzI0
---
# STL Algorithms in Practice and Iterator Pitfalls

:::tip
This is the second article in the CppCon 2025 Mike Shah "Back to Basics: C++ Ranges" series. In the previous article, we abstracted "traversal" from index-based loops all the way down to iterators, concluding that: **a pair of `begin`/`end` iterators defines a range**. In this article, we feed that iterator pair to STL algorithms—seeing how they write loops for us, and what hard requirements they impose on iterators. We will also dissect several classic iterator pitfalls, all tested live with GCC 16.1.1. The environment is the same as before: Arch Linux WSL, `-std=c++20`.
:::

At the end of the previous article, we said that algorithms are built on top of that iterator pair. To make this concrete, we first need to understand what pieces the STL is actually composed of.

## The Three Pillars of the STL

The design philosophy of the Standard Template Library (STL) decouples three things: **containers** are responsible for storing data, **iterators** are responsible for traversing data, and **algorithms** are responsible for processing data<RefLink :id="1" preview="cppreference, Standard library algorithms — containers, iterators, algorithms" />. The three are connected through iterators as the "glue"—algorithms don't know any specific container directly, they only recognize iterators; as long as a container can produce iterators that meet the requirements, it can be reused by all algorithms. This decoupling is the fundamental reason why the STL can use a single `std::sort` to handle `vector`, `array`, and `deque`.

So, which header files actually contain the algorithms?

:::warning Shah's "two headers" is a bit too narrow
In his talk, Shah says "algorithms are mainly in the `<algorithm>` and `<numeric>` headers"—this is fine for a beginner's understanding, but it actually **misses several pieces**. The full picture looks like this: general algorithms (`sort`, `find`, `copy`, `transform`, etc.) are in `<algorithm>`; numeric algorithms (`accumulate`, `reduce`, `inner_product`, etc.) are in `<numeric>`; **parallel algorithms** (like `sort(std::execution::par, ...)` with execution policies) require `<execution>` (C++17); C++20 ranges algorithms and views are in `<ranges>`; and there are even scattered ones—`std::midpoint` is in `<numeric>`, but C++23's fold algorithms `std::fold_left` are in `<algorithm>`. So don't memorize "algorithms = two headers"; it's more accurate to remember "algorithms are spread across several headers, with `<algorithm>` as the main one."
:::

## Algorithm Cheat Sheet: By Category and Required Iterator Type

There are over a hundred STL algorithms, and memorizing them is pointless. A better way to remember them is to **group them by category**, and to keep in mind the **hard requirements each category places on iterator types**—because this directly determines whether you can use a given algorithm on a particular container. The following table is a key creative addition of this article; Shah didn't expand on it in his talk:

| Category | Representative Algorithms | Required Iterator Category |
|------|------|------|
| Read-only search | `find` / `find_if` / `count` / `accumulate` | input (weakest acceptable) |
| Modifying copy | `copy` / `transform` / `replace` / `fill` | forward / output |
| Partitioning | `partition` / `stable_partition` | forward (stable version requires bidirectional) |
| Sorting | `sort` / `stable_sort` / `partial_sort` | **random_access** (hard requirement) |
| Binary search | `lower_bound` / `upper_bound` / `binary_search` | forward (**and the range must already be sorted**) |
| Numeric reduction | `reduce` / `transform_reduce` / `inner_product` | input |
| Heap operations | `push_heap` / `pop_heap` / `sort_heap` | random_access |

The single most important thing to remember here is: **sorting algorithms require random access iterators**. This means they can only be used on contiguous or random-access containers like `vector`, `array`, and `deque`. **Using them on `std::list` simply won't compile**. This isn't a suggestion; it's a hard constraint. Let's test this.

## Experiment: std::sort Cannot Be Used on std::list

`std::list` provides bidirectional iterators, which don't support `it + n` or subtracting two iterators. Meanwhile, `std::sort` internally requires random access (it needs to do `__last - __first` to estimate recursion depth). What happens if we feed it a list's iterators?

```cpp
#include <algorithm>
#include <list>

int main()
{
    std::list<int> l{3, 1, 2};
    std::sort(l.begin(), l.end());  // 编不过！
}
```

GCC 16.1.1 error output (key lines extracted):

```bash
❯ g++ -std=c++20 list_sort.cpp -o list_sort
/usr/include/c++/16.1.1/bits/stl_algo.h:1914:50: error: no match for ‘operator-’
   (operand types are ‘std::_List_iterator<int>’ and ‘std::_List_iterator<int>’)
 1914 |                                 std::__lg(__last - __first) * 2,
   |                                           ~~~~~~~^~~~~~~~~
```

See that—the error occurs right at the `__last - __first` step: `std::sort` wants to use iterator subtraction to calculate the range length, but `_List_iterator` simply doesn't define `operator-` (bidirectional iterators only understand `++`/`--`, not subtraction). This is the classic manifestation of "iterator category doesn't satisfy algorithm requirements." If you really need to sort a `list`, use its member function `l.sort()`—that's a merge sort tailored for linked lists with O(n log n) complexity, but it doesn't rely on random access.

## sort, partition, copy, transform: What Common Algorithms Look Like

Let's quickly run through the most commonly used algorithms to build intuition. Their parameter shapes are remarkably consistent—the vast majority take **a pair of iterators `(first, last)` plus an optional predicate or destination**.

```cpp
#include <algorithm>
#include <vector>
#include <iterator>
#include <random>

void demo(std::vector<int>& v, const std::vector<int>& src)
{
    // 排序整个区间
    std::sort(v.begin(), v.end());

    // 局部排序：只排 [begin, begin+3)，后面元素顺序不定但都 >= 前 3 个
    // std::partial_sort(v.begin(), v.begin() + 3, v.end());

    // 分区：把满足谓词的元素挪到前面，返回分界点
    auto it = std::partition(v.begin(), v.end(), [](int x) { return x < 4; });

    // 拷贝：用 back_inserter 自动 push_back，不用预先算大小
    std::copy(src.begin(), src.end(), std::back_inserter(v));

    // 打乱：必须传一个随机数引擎（C++11 起 rand() 不推荐）
    std::shuffle(v.begin(), v.end(), std::mt19937{std::random_device{}()});
}
```

Two details here are worth elaborating on. `std::back_inserter(v)` returns an **output iterator**; as you write to it, it automatically calls `v.push_back()`—this avoids the hassle of "needing to know how many elements to copy and reserving space in advance," making it the most common partner for `copy`. `std::shuffle` reminds us: **after C++11, random numbers should use the engines from the `<random>` header (like `std::mt19937`), not the old `rand()`**—`rand()` has poor quality and thread-safety issues.

Now look at `std::transform`, which encapsulates the "apply a function to each element" pattern. Note the use of `cbegin`/`cend` here—**const versions of the iterators**, indicating "I only read from the source range, I don't modify it":

```cpp
#include <algorithm>
#include <string>
#include <iterator>

std::string s = "hello";
std::string out;
std::transform(s.cbegin(), s.cend(), std::back_inserter(out),
               [](char c) { return std::toupper(static_cast<unsigned char>(c)); });
// out == "HELLO"
```

`cbegin`/`cend` return `const_iterator`, while `rbegin`/`rend` return reverse iterators. An easy pitfall: **these iterators must be used in pairs**—you can't pair `cbegin()` with `end()` (one is const, the other isn't; the types don't match). After C++20, the status of `const_iterator` in the standard library was elevated further (proposals like P0896), because the ranges system relies heavily on it.

## rotate: Parameter Order Is the Biggest Pitfall

`std::rotate` is a very useful but particularly easy-to-get-wrong algorithm. Its job is to "cyclically shift elements in a range so that the element pointed to by `middle` becomes the new first element." The signature takes three iterators: `std::rotate(first, middle, last)`.

```cpp
std::vector<int> v{1, 2, 3, 4, 5};
std::rotate(v.begin(), v.begin() + 2, v.end());
// 结果：{3, 4, 5, 1, 2}  —— middle(begin+2，即 3) 变成了新首元素
```

Actual output:

```bash
❯ g++ -std=c++20 rot_ok.cpp -o rot_ok && ./rot_ok
rotate(begin, begin+2, end) on {1,2,3,4,5} -> { 3 4 5 1 2 }
```

The trap here is: **the vast majority of algorithms take two iterators `(first, last)`, but `rotate` alone (along with `partial_sort`, `nth_element`, etc.) takes three `(first, middle, last)`**. Once you develop muscle memory for "two parameters," it's extremely easy to swap the positions of `middle` and `last` when writing `rotate`. Shah himself complained about this—he used `upper_bound` to find an insertion point and then `rotate` to manually implement insertion sort, calling it "too clever, ugly."

So what happens if you get the order wrong? I swapped `middle` and `last`, writing it as `rotate(first, last, middle)`:

```cpp
std::vector<int> w{1, 2, 3, 4, 5};
std::rotate(w.begin(), w.end(), w.begin() + 2);  // 参数顺序错了
```

```bash
❯ g++ -std=c++20 rot_bad.cpp -o rot_bad && ./rot_bad
about to call rotate(begin, end, begin+2)...
[程序崩溃，退出码 139 — SIGSEGV]
```

Immediate segfault (exit code 139 = SIGSEGV). The reason is straightforward: `std::rotate` requires both `[first, middle)` and `[middle, last)` to be valid sub-ranges; in other words, the three iterators must satisfy the `first <= middle <= last` ordering. After writing it as `(first, last, middle)`, the second sub-range `[middle_arg=last, last_arg=middle)` becomes an invalid range (the end is before the start), and the algorithm dereferences an out-of-bounds position and crashes.

:::warning For three-iterator algorithms, always check the documentation for parameter order
Algorithms like `rotate`, `partial_sort`, `nth_element`, and `stable_partition` don't take simple `(first, last)` parameters, but rather three-segment forms like `(first, middle, last)`. Before using them, you must confirm what `middle` actually refers to. This will improve in the ranges versions we cover in part three—because ranges versions often require fewer parameters (passing the container directly), reducing the chance of pairing errors.
:::

## How Many Algorithms Are There Really? The "Over 200" Claim Needs an Asterisk

In his talk, Shah mentions a widely circulated number: "A 2018 CppCon talk said there are at least 105 algorithms, and now there are over 200." Is this accurate? Let's fact-check this<RefLink :id="2" preview="cppreference, Standard library header <algorithm> — function template count" />.

First, the origin of the "105" figure: it comes from Jonathan Boccara's CppCon 2018 talk, "105 STL Algorithms in Less Than an Hour"<RefLink :id="3" preview="Jonathan Boccara, CppCon 2018 — 105 STL Algorithms" />. That used a **very loose counting criteria**—it counted `_if` variants (`find` / `find_if`), `_n` variants (`copy` / `copy_n`), and `_copy` variants (`remove` / `remove_copy`) as separate algorithms, for the purpose of making the talk easier to follow and present.

So what's the strict number? I checked against cppreference, and as of C++23:

- The `<algorithm>` header contains approximately **91** `std::` function templates (not counting ranges versions).
- The `<numeric>` header contains **14** numeric algorithms (`accumulate`, `reduce`, `inner_product`, etc.; C++26 will add 5 more saturated arithmetic ones, bringing it to 19).
- The `std::ranges::` namespace contains approximately **100** "constrained algorithms" (niebloids, which are the ranges versions of algorithms).
- Additionally, there are about 14 uninitialized memory algorithms in `<memory>`.

So the "over 200" claim **only holds true if you count both the `std::` and `std::ranges::` APIs as separate entries, plus various variant overloads**. If you count by "unique algorithm names," the actual number is approximately **110 to 120**.

:::tip How to phrase it accurately
Rather than saying "the STL has over 200 algorithms," a more rigorous statement is: **the STL has over 100 unique algorithms; if you count both the `std::` and `std::ranges::` interfaces as entries, there are indeed over 200 API entry points.** This distinction is quite important in interviews or technical writing—"over 200" sounds impressive, but a large portion of that consists of variants and ranges mirrors of the same algorithm.
:::

## Pitfall 1: Iterator Invalidation—The Most Insidious Killer

Once you're familiar with the algorithms themselves, they aren't hard to use. What really trips people up is **coordinating the lifecycles of iterators and containers**. The number one pitfall is **iterator invalidation**.

Consider this code that looks perfectly innocent:

```cpp
std::vector<int> v{1, 2, 3};
auto it = v.begin();        // it 指向 v 的第一个元素
v.push_back(4);             // 如果触发扩容，it 就悬空了！
std::cout << *it << '\n';   // 解引用悬空迭代器 —— UB
```

The problem lies in `push_back`. Internally, `vector` is a contiguous dynamic array; when capacity is insufficient, it **reallocates a larger block of memory**, moves the old elements over, and then frees the old memory. But your `it` still points to that **now-freed old memory**—it becomes a dangling pointer (the standard term is "singular iterator"). Dereferencing `*it` at this point is undefined behavior (UB).

The scary part is: **UB doesn't necessarily crash immediately**. It often manifests as "reading a seemingly normal value," so you think everything is fine, merge the code into main, and then one day it inexplicably crashes on a customer's machine. Let's test this with a normal compilation (no debug flags):

```cpp
#include <vector>
#include <iostream>
int main()
{
    std::vector<int> v{1, 2, 3};
    auto it = v.begin();
    std::cout << "before push_back: *it=" << *it << ", cap=" << v.capacity() << "\n";
    v.push_back(4); v.push_back(5); v.push_back(6); v.push_back(7);  // 必然扩容
    std::cout << "after  push_back: cap=" << v.capacity() << "\n";
    std::cout << "deref stale it: " << *it << "\n";   // UB：读已释放内存
}
```

```bash
❯ g++ -std=c++20 -O0 inval.cpp -o inval && ./inval; echo "退出码=$?"
before push_back: *it=1, cap=3
after  push_back: cap=12
deref stale it: -40771459
退出码=0
```

See that—the program **exits normally (exit code 0) with no errors**, but the value read out is garbage like `-40771459`. After `vector` expands, the capacity jumps from 3 to 12, the old memory is freed, and the memory `it` points to contains random residual data. This is UB at its most insidious: **silent errors**.

So how do you catch it? GCC/Clang provide a debug macro, `-D_GLIBCXX_DEBUG`. When enabled, standard library iterators carry bounds and validity checks; the moment you dereference an invalidated iterator, it immediately aborts and prints diagnostics. Let's compile the same code with debug mode enabled:

```bash
❯ g++ -std=c++20 -O0 -g -D_GLIBCXX_DEBUG inval.cpp -o inval_dbg && ./inval_dbg; echo "退出码=$?"
before push_back: *it=1, cap=3
after  push_back: cap=12
/usr/include/c++/16.1.1/debug/safe_iterator.h:352:
Error: attempt to dereference a singular iterator.
Objects involved in the operation:
    iterator "this" @ 0x7fff6bd63820 {
      type = gnu_cxx::normal_iterator<int*, std::vector<int>>(mutable iterator);
      state = singular;   ← 迭代器已失效
      references sequence with type 'std::debug::vector<int>' @ 0x7fff6bd63850
    }
退出码=134   ← 134 = SIGABRT，被调试库主动 abort
```

Caught red-handed this time: `state = singular` explicitly tells you the iterator is invalid, and `attempt to dereference a singular iterator` precisely identifies what you did. A single `-D_GLIBCXX_DEBUG` macro turns "silent UB" into "instant crash + precise location"—enable it during development, disable it for release (it has a performance cost). The MSVC equivalent switch is `_ITERATOR_DEBUG_LEVEL=2`; Release configurations default to 0 or 1, while Debug configurations use 2.

:::tip Iterator invalidation rules cheat sheet (verified against cppreference)
Invalidation rules vary significantly between containers; just remember the general principles and look up the specifics<RefLink :id="4" preview="cppreference, Iterator invalidation — rules per container" />:

- **`vector` / `string`**: `push_back` invalidates **all** iterators only when it triggers a reallocation (capacity change); when no reallocation occurs, only `end()` changes. After `reserve`, as long as you don't exceed the reserved capacity, iterators won't invalidate.
- **`deque`**: Insertions at either end invalidate **all iterators** (even without reallocation), but **references and pointers do not invalidate**—so be careful when traversing a deque; storing references is safer than storing iterators.
- **`list` / `forward_list`**: Insertions and `splice` **do not invalidate** any existing iterators (linked list nodes don't move); only the iterator corresponding to the erased node is invalidated.
- **`unordered_*`**: `rehash` (triggered when insertion causes the bucket count to change) invalidates **iterators, but references and pointers do not invalidate**.

Remember one overarching principle: **whenever a container might "move house" internally (contiguous storage containers reallocating, hash tables rehashing), iterators may invalidate; node-based containers (list, tree nodes) don't move, so their iterators are stable.**
:::

## Pitfall 2: Mismatched Iterator Pairs—begin and end Must Come from the Same Object

The second pitfall relates to "pairing." Algorithms require `first` and `last` to come from **the same container**, but C++ can't enforce this at runtime—if you pass iterators from two different containers, the compiler accepts them without complaint, and the result is UB.

The classic crash scenario comes from Jason Turner's C++ Weekly (which Shah specifically referenced in his talk): a function returns a temporary `vector`, and to save trouble, you chain `.begin()` and `.end()` calls directly:

```cpp
std::vector<int> download_data();  // 每次调用返回一个全新的临时 vector

// 危险写法：
// process(download_data().begin(), download_data().end());
```

:::warning Shah understates this here
Shah's commentary on this code is "maybe it works sometimes, maybe we get lucky"—this statement **could mislead beginners** because it implies "there are legitimate cases where this works." **There aren't.** This is undefined behavior; there is no "legitimately working" path, only the illusion of "UB accidentally behaving normally."

The reason: the two `download_data()` calls are **two independent function calls**, returning **two different temporary `vector` objects**. Their `.begin()` and `.end()` point to two completely unrelated memory blocks. Pairing one temporary's `begin` with another temporary's `end` and feeding them to an algorithm—the range isn't valid at all. Worse, both temporaries are destroyed at the end of that statement, so the iterators the algorithm holds are dangling from the start. **The correct approach is to first store the result in a named variable**, so that `begin` and `end` come from the same living object:

```cpp
auto data = download_data();          // 一个具名变量，一份内存
process(data.begin(), data.end());    // begin/end 来自同一个 data —— 安全
```

This illusion of "same function name means same object" is a high-frequency area for pairing errors.
:::

## Pitfall 3: Insufficient Space—Cramming Too Much into a Fixed-Size Destination

The third pitfall relates to output destinations. When you use `std::copy` to write data to a **fixed-size** destination (like a raw array, or a container without a prior `back_inserter`), and the source range is larger than the destination space, you get an **out-of-bounds write**—again UB, and it can silently corrupt adjacent memory.

```cpp
int src[10] = {0,1,2,3,4,5,6,7,8,9};
int dst[3];   // 只有 3 个位置！
std::copy(std::begin(src), std::end(src), std::begin(dst));  // 越界写 —— UB
```

This code compiles, runs, and doesn't immediately report errors, but you've written 7 values that shouldn't be there into the memory after `dst`. This kind of bug can be caught with AddressSanitizer (`-fsanitize=address`), which will report a heap/stack buffer overflow.

The workaround is straightforward: either use `std::back_inserter` (letting the destination container grow automatically), or `reserve` sufficient space before copying and confirm the source range doesn't exceed the destination capacity. Circling back to our first lesson: **letting the container manage its own size (using an inserter) is much safer than manually calculating sizes.**

## Error Quality: Are Ranges Really More Friendly?

In his summary, Shah says "Ranges use concepts and give you better error messages." This is true, but with a caveat. Let's compare the errors from both interfaces when "passing the wrong parameters."

First, the classic `std::sort` with wrong parameters—pairing `begin` from a `vector` with `end` from a `list` (type mismatch):

```cpp
std::vector<int> v{1,2,3};
std::list<int>   l{4,5,6};
std::sort(v.begin(), l.end());   // 两个不同容器的迭代器
```

Now the ranges version with wrong parameters—passing something that isn't a range at all to `std::ranges::sort`:

```cpp
int not_a_range = 42;
std::ranges::sort(not_a_range);
```

Error line counts from both under GCC 16.1.1:

```bash
❯ # 经典版
❯ g++ -std=c++20 err_classic.cpp 2>err_c.txt; wc -l < err_c.txt
32
❯ head -3 err_c.txt
err_classic.cpp:7:14: error: no matching function for call to
  'sort(std::vector<int>::iterator, std::__cxx11::list<int>::iterator)'

❯ # ranges 版
❯ g++ -std=c++20 err_ranges.cpp 2>err_r.txt; wc -l < err_r.txt
69
```

Here's the interesting part—**in this specific example, the ranges version's error (69 lines) is actually longer than the classic version (32 lines)**. This is because passing a `int` to `ranges::sort` forces the compiler to unfold the entire concept constraint chain (`sortable` → `random_access_iterator` → ...) for you to see; the longer the chain, the more verbose the error. So I have to honestly correct a common impression: **"ranges errors are always shorter and friendlier" doesn't hold up**. Their readability depends heavily on compiler version and specific scenario (GCC 10+ / Clang 12+ are more mature; older compilers still spit out a screenful of template gibberish).

So what's the real advantage of ranges when it comes to "errors"? It's not the line count, but **that it prevents you from writing certain bugs in the first place**. Recall pitfall two from above—the classic `std::sort` accepts two iterators, so you can easily mismatch `begin`/`end` from two different containers (like in `err_classic`), and the compiler only errors at instantiation time. But `std::ranges::sort` **accepts only one container**, so you can't even express the error of "begin from A, end from B." **Having one fewer opportunity to make a mistake is far more practical than friendlier error messages.** This is the core safety benefit of ranges, which we'll expand on in part three.

## Transition: Must Iterators Die?

At this point in the talk, Shah put up a rather exaggerated slide—"Iterators must die." Exaggeration aside, the sentiment he wanted to express is real: **while the iterator interface is powerful, it's full of pitfalls**—pairing is error-prone, parameter order (for three-iterator algorithms) is easy to get backwards, and partial sort syntax is ugly.

The good news is that C++20 Ranges directly addresses these pain points. It doesn't abandon iterators (iterators remain the underlying mechanism, and even C++26 can't do without them), but it wraps a safer, more composable interface layer on top of iterators: **passing containers directly instead of iterator pairs, using concepts to intercept type errors early at compile time, and using views for lazy composition**. These are the main threads of part three.

In the next article, we'll formally dive into Ranges—starting from "why `ranges::sort` takes one fewer parameter," moving through lazy evaluation of views, the pipe operator, and `ranges::to`, and finally a feature that will make your eyes light up: **infinite ranges**. If you're interested in parallel versions of numeric algorithms (`reduce`, `transform_reduce`), you can check out the content on `<execution>` execution policies and `std::reduce` parallel reduction in the vol5 concurrency volume—that's where algorithms and concurrency intersect.

<ReferenceCard title="References">
  <ReferenceItem
    :id="1"
    author="cppreference.com"
    title="Algorithms library"
    :year="2026"
    url="https://en.cppreference.com/w/cpp/algorithm"
    chapter="The three pillars: containers / iterators / algorithms"
  />
  <ReferenceItem
    :id="2"
    author="cppreference.com"
    title="Standard library header &lt;algorithm&gt;"
    :year="2024"
    url="https://en.cppreference.com/w/cpp/header/algorithm"
    chapter="Approximately 91 function templates as of C++23"
  />
  <ReferenceItem
    :id="3"
    author="Jonathan Boccara"
    title="105 STL Algorithms in Less Than an Hour — CppCon 2018"
    :year="2018"
    url="https://www.youtube.com/watch?v=2olsGf6JIkU"
    chapter="105 under loose counting criteria"
  />
  <ReferenceItem
    :id="4"
    author="cppreference.com"
    title="Iterator invalidation rules"
    :year="2026"
    url="https://en.cppreference.com/w/cpp/container"
    chapter="Invalidation rules after insert/erase for each container"
  />
  <ReferenceItem
    :id="5"
    author="cppreference.com"
    title="std::rotate"
    :year="2026"
    url="https://en.cppreference.com/w/cpp/algorithm/rotate"
    chapter="Parameter order: first, middle, last"
  />
  <ReferenceItem
    :id="6"
    author="cppreference.com"
    title="std::vector — Iterator invalidation"
    :year="2026"
    url="https://en.cppreference.com/w/cpp/container/vector"
    chapter="push_back reallocation causing iterator invalidation"
  />
  <ReferenceItem
    :id="7"
    author="cppreference.com"
    title="Standard library header &lt;numeric&gt;"
    :year="2023"
    url="https://en.cppreference.com/w/cpp/header/numeric"
    chapter="Approximately 14 numeric algorithms"
  />
  <ReferenceItem
    :id="8"
    author="Mike Shah"
    title="Back to Basics: C++ Ranges — CppCon 2025"
    :year="2025"
    url="https://www.youtube.com/watch?v=Q434UHWRzI0"
  />
</ReferenceCard>
