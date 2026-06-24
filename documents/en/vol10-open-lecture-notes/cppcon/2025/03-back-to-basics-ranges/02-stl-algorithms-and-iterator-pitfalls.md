---
chapter: 3
conference: cppcon
conference_year: 2025
cpp_standard:
- 11
- 17
- 20
description: 'CppCon 2025 Talk Notes — Mike Shah: STL Algorithms in Practice, Hard
  Constraints on Iterator Categories, Algorithm Cheat Sheet & Invalidatation Rules,
  Measuring Silent UB from Iterator Invalidation with GCC and Capturing It with _GLIBCXX_DEBUG'
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
video_youtube: https://www.youtube.com/watch?v=Q434UHWRzI0
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/03-back-to-basics-ranges/02-stl-algorithms-and-iterator-pitfalls.md
  source_hash: 1f066da2ba88ebe5dee5f23cbaa2cd4af1b2b83e513a4069ab2f42c7ade2bd52
  translated_at: '2026-06-24T00:33:14.607364+00:00'
  engine: anthropic
  token_count: 3895
---
# STL Algorithms in Action and Iterator Pitfalls

:::tip
This is the second article in the CppCon 2025 "Back to Basics: C++ Ranges" series by Mike Shah. In the previous post, we abstracted "traversal" from index loops all the way up to iterators, concluding that: **a pair of `begin`/`end` iterators defines a range**. In this post, we feed that pair of iterators into STL algorithms—to see how they write loops for us, and what hard requirements they impose on iterators. We will also dissect classic iterator pitfalls, all verified with GCC 16.1.1. The environment remains the same: Arch Linux WSL, `-std=c++20`.
:::

At the end of the last post, we mentioned that algorithms are built on top of that pair of iterators. To make this concrete, we first need to understand exactly what components make up the STL.

## The Three Pillars of the STL

The design philosophy of the Standard Template Library (STL) is to decouple three things: **containers** are responsible for storing data, **iterators** are responsible for traversing data, and **algorithms** are responsible for processing data <RefLink :id="1" preview="cppreference, Standard library algorithms — containers, iterators, algorithms" />. These three are connected by iterators, which act as the "glue"—algorithms don't know about specific containers directly, they only recognize iterators; as long as a container can spit out iterators that meet the requirements, it can be reused by all algorithms. This decoupling is the fundamental reason why STL can use a single `std::sort` to handle `vector`, `array`, and `deque`.

So, which header files do these algorithms actually live in?

:::warning Shah's "Two Headers" is a Bit Narrow
In his talk, Shah says "algorithms are mainly in `<algorithm>` and `<numeric>`"—this is fine for an introductory understanding, but it actually **misses several pieces**. The complete picture is this: general algorithms (`sort`, `find`, `copy`, `transform`, etc.) are in `<algorithm>`; numeric algorithms (`accumulate`, `reduce`, `inner_product`, etc.) are in `<numeric>`; **parallel algorithms** (like `sort(std::execution::par, ...)` with execution policies) require `<execution>` (C++17); C++20 ranges algorithms and views are in `<ranges>`; and there are even scattered ones—`std::midpoint` is in `<numeric>`, but C++23's folding algorithm `std::fold_left` is in `<algorithm>`. So don't rote memorize "algorithms = two headers"; it's more accurate to remember "algorithms are scattered across several headers, with `<algorithm>` being the main one."
:::

## Algorithm Cheat Sheet: Categories and Iterator Requirements

There are over a hundred STL algorithms, so rote memorization is meaningless. A better way is to **categorize them**, and remember the **hard requirements each category has on iterator categories**—because this directly determines whether you can use a specific algorithm on a given container. The table below is a key contribution of this post, which Shah didn't expand on in the talk:

| Category | Representative Algorithms | Required Iterator Category |
|----------|---------------------------|----------------------------|
| Read-only search | `find` / `find_if` / `count` / `accumulate` | input (weakest is fine) |
| Modifying copy | `copy` / `transform` / `replace` / `fill` | forward / output |
| Partitioning | `partition` / `stable_partition` | forward (stable version requires bidirectional) |
| Sorting | `sort` / `stable_sort` / `partial_sort` | **random_access** (hard requirement) |
| Binary search | `lower_bound` / `upper_bound` / `binary_search` | forward (**and range must be sorted**) |
| Numeric reduction | `reduce` / `transform_reduce` / `inner_product` | input |
| Heap operations | `push_heap` / `pop_heap` / `sort_heap` | random_access |

The most important rule to remember here is: **sorting algorithms require random access iterators**. This means they can only be used on containers with contiguous or random access like `vector`, `array`, and `deque`. **Using them on `std::list` will fail to compile outright**. This isn't a suggestion; it's a hard constraint. Let's test this.

## Experiment: std::sort cannot be used on std::list

`std::list` has bidirectional iterators, which do not support `it + n` or subtraction between two iterators. Meanwhile, `std::sort` internally requires random access (it needs to calculate `__last - __first` to estimate recursion depth). What happens if we feed a list's iterator into it?

```cpp
#include <algorithm>
#include <list>

int main()
{
    std::list<int> l{3, 1, 2};
    std::sort(l.begin(), l.end());  // 编不过！
}
```

Here are the key lines from the GCC 16.1.1 error output:

```bash
❯ g++ -std=c++20 list_sort.cpp -o list_sort
/usr/include/c++/16.1.1/bits/stl_algo.h:1914:50: error: no match for ‘operator-’
   (operand types are ‘std::_List_iterator<int>’ and ‘std::_List_iterator<int>’)
 1914 |                                 std::__lg(__last - __first) * 2,
   |                                           ~~~~~~~^~~~~~~~~
```

See? The error occurs right at `__last - __first`: `std::sort` attempts to use iterator subtraction to calculate the range length, but `_List_iterator` simply doesn't define `operator-` (bidirectional iterators only understand `++` and `--`, not subtraction). This is a classic case of "iterator category not meeting algorithm requirements." If you really need to sort a `list`, use its member function `l.sort()`. That's a merge sort tailored specifically for linked lists; it retains the O(n log n) complexity but doesn't rely on random access.

## sort, partition, copy, transform: What do common algorithms look like?

Let's quickly run through the most commonly used algorithms to build some intuition. Their parameter patterns are surprisingly consistent—most of them take **a pair of iterators `(first, last)` plus an optional predicate or destination**.

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

There are two details worth mentioning. `std::back_inserter(v)` returns an **output iterator**. When we write to it, it automatically calls `v.push_back()`. This avoids the hassle of pre-calculating the element count to `reserve` space, making it the most common partner for `copy`. `std::shuffle` reminds us that **since C++11, we should use the engines from the `<random>` header (like `std::mt19937`) for random numbers, instead of the old `rand()`**—`rand()` has poor quality and thread-safety issues.

Next, let's look at `std::transform`, which encapsulates the logic of "applying a function to each element." Note that we use `cbegin` and `cend` here—**const versions of iterators**—to indicate that "we only read from the source range and do not modify it":

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

`cbegin`/`cend` return a `const_iterator`, while `rbegin`/`rend` return reverse iterators. A common pitfall is that **these iterators must be used in pairs**—you cannot pair `cbegin()` with `end()` (one is const, the other is non-const, resulting in a type mismatch). Since C++20, the status of `const_iterator` in the standard library has been elevated (via proposals like P0896), because the ranges library relies heavily on it.

## rotate: The parameter order is the biggest pitfall

`std::rotate` is a very useful algorithm, but it is particularly easy to get wrong. Its function is to "cyclically shift elements within a range so that the element pointed to by `middle` becomes the new first element." Its signature takes three iterators: `std::rotate(first, middle, last)`.

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

The trap here is that **most algorithms take two iterators `(first, last)`, while `rotate` (along with `partial_sort`, `nth_element`, etc.) takes three `(first, middle, last)`**. Once we develop the muscle memory for "two arguments," it is very easy to reverse the positions of `middle` and `last` when writing `rotate`. Shah himself has complained about this; he used `upper_bound` to find an insertion point and then `rotate` to manually implement insertion sort, describing the result as "too clever, ugly."

What happens if we get them mixed up? I swapped `middle` and `last` to write `rotate(first, last, middle)`:

```cpp
std::vector<int> w{1, 2, 3, 4, 5};
std::rotate(w.begin(), w.end(), w.begin() + 2);  // 参数顺序错了
```

```bash
❯ g++ -std=c++20 rot_bad.cpp -o rot_bad && ./rot_bad
about to call rotate(begin, end, begin+2)...
[程序崩溃，退出码 139 — SIGSEGV]
```

Direct segmentation fault (exit code 139 = SIGSEGV). The reason is straightforward: `std::rotate` requires that both `[first, middle)` and `[middle, last)` are valid sub-ranges. In other words, the three iterators must satisfy the order `first <= middle <= last`. Once written as `(first, last, middle)`, the second sub-range `[middle_arg=last, last_arg=middle)` becomes an illegal range (the end is before the start). The algorithm dereferences an out-of-bounds position and crashes.

:::warning Check documentation for three-iterator algorithms
For algorithms like `rotate`, `partial_sort`, `nth_element`, and `stable_partition`, the parameters are not simply `(first, last)`, but rather three-part sequences like `(first, middle, last)`. Before using them, confirm exactly what `middle` refers to. This improves with the ranges version covered in Part Three—because ranges versions often require fewer arguments (passing the container directly), reducing the chance of pairing errors.
:::

## How Many Algorithms Are There Really? "Over 200" Needs a Discount

In his talk, Shah mentions a widely circulated number: "A 2018 CppCon talk said there were at least 105 algorithms, now there are over 200." Is this accurate? Let's fact-check this <RefLink :id="2" preview="cppreference, Standard library header <algorithm> — function template count" />.

First, the origin of "105": It comes from Jonathan Boccara's CppCon 2018 talk, "105 STL Algorithms in Less Than an Hour" <RefLink :id="3" preview="Jonathan Boccara, CppCon 2018 — 105 STL Algorithms" />. That used a **very loose counting criteria**—it counted `_if` variants (`find` / `find_if`), `_n` variants (`copy` / `copy_n`), and `_copy` variants (`remove` / `remove_copy`) as separate algorithms. The purpose was to make them memorable and easy to explain during the talk.

So, what is the strict number? I checked cppreference, and as of C++23:

- The `<algorithm>` header contains approximately **91** `std::` function templates (excluding ranges versions).
- The `<numeric>` header contains **14** numeric algorithms (`accumulate`, `reduce`, `inner_product`, etc.; C++26 will add 5 more saturated arithmetic ones, making it 19).
- Under the `std::ranges::` namespace, there are approximately **100** "constrained algorithms" (niebloids, which are the ranges versions of algorithms).
- Additionally, there are about 14 uninitialized memory algorithms in `<memory>`.

Therefore, the claim of "over 200" **only holds true if we count both the `std::` and `std::ranges::` API sets separately, plus various variant overloads.** If we count by "unique algorithm names," the actual number is approximately **110 to 120**.

:::tip How to state it accurately
Rather than saying "STL has over 200 algorithms," a more rigorous statement is: **STL has over 100 unique algorithms; if we count both `std::` and `std::ranges::` interfaces as entries, there are indeed over 200 API entry points.** This distinction is quite important in interviews or technical writing—"over 200" sounds impressive, but many are simply variants and ranges mirrors of the same algorithm.
:::

## Trap #1: Iterator Invalidation—The Most Insidious Killer

Using the algorithms themselves isn't hard once you are familiar with them; the real pitfall is **coordinating the lifecycles of iterators and containers**. The number one trap is **iterator invalidation**.

Let's look at a snippet of code that seems harmless enough:

```cpp
std::vector<int> v{1, 2, 3};
auto it = v.begin();        // it 指向 v 的第一个元素
v.push_back(4);             // 如果触发扩容，it 就悬空了！
std::cout << *it << '\n';   // 解引用悬空迭代器 —— UB
```

The problem lies with `push_back`. Internally, a `vector` is a contiguous dynamic array. When capacity is exceeded, it **reallocates a larger block of memory**, moves the old elements over, and then frees the old memory. However, your `it` still points to that **freed old memory**—it has become a dangling pointer (standard terminology calls this a "singular iterator"). Dereferencing `*it` at this point is undefined behavior (UB).

The scary part is: **UB doesn't always crash immediately**. It often manifests as "reading a value that looks perfectly normal," leading you to believe everything is fine. You merge the code into the main branch, and then one day, it crashes inexplicably on a customer's machine. Let's test this with a normal compilation (debugging disabled):

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

See what happened? The program **exits normally (exit code 0) without any errors**, but the value read is garbage like `-40771459`. After the `vector` reallocated, its capacity grew from three to 12, the old memory was freed, and the memory `it` pointed to now contains random data. This is the most insidious form of **undefined behavior (UB)**: **silent failure**.

So how do we catch it? GCC and Clang provide a debug macro `-D_GLIBCXX_DEBUG`. When enabled, standard library iterators carry bounds and validity checks. If you dereference an invalid iterator, it will immediately abort and print diagnostics. Let's compile the same code with debugging enabled:

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

Now we've caught it red-handed: `state = singular` explicitly tells you that this iterator is invalid, and `attempt to dereference a singular iterator` precisely points out what you just did. A single `-D_GLIBCXX_DEBUG` macro turns "silent UB" into "instant crash + precise location" — enable it during development, disable it for release (it incurs a performance overhead). The corresponding switch for MSVC is `_ITERATOR_DEBUG_LEVEL=2`; Release configurations default to 0 or 1, while Debug configurations use 2.

:::tip Quick Reference for Iterator Invalidation Rules (Verified against cppreference)
Invalidation rules vary significantly between containers, so just remember the general principles and check the specific table <RefLink :id="4" preview="cppreference, Iterator invalidation — rules per container" />:

- **`vector` / `string`**: `push_back` invalidates **all** iterators only when a reallocation is triggered (capacity changes); otherwise, only `end()` changes. After `reserve`, iterators remain valid as long as you don't exceed the reserved capacity.
- **`deque`**: Inserting at either end invalidates **all iterators** (even without reallocation), but **references and pointers remain valid** — so be careful when traversing a deque; storing references is safer than storing iterators.
- **`list` / `forward_list`**: Insertion and `splice` do **not** invalidate any existing iterators (list nodes don't move); only the iterator corresponding to the node removed by `erase` becomes invalid.
- **`unordered_*`**: `rehash` (triggered when insertion changes the bucket count) invalidates **iterators, but references and pointers remain valid**.

Remember one overarching principle: **If the container internals might "move house" (contiguous containers reallocating, hash tables rehashing), iterators may become invalid; node-based containers (list, tree nodes) don't move, so their iterators are stable.**
:::

## Pitfall 2: Mismatched Iterator Pairs — `begin` and `end` Must Come from the Same Object

The second pitfall relates to "pairing." Algorithms require that `first` and `last` come from **the same container**, but C++ cannot enforce this check at runtime — if you pass iterators from two different containers, the compiler accepts them without complaint, leading straight to UB.

The classic failure scenario comes from Jason Turner's C++ Weekly (which Shah specifically cited in his talk): a function returns a temporary `vector`, and to save a line of code, you chain `.begin()` and `.end()` calls directly:

```cpp
std::vector<int> download_data();  // 每次调用返回一个全新的临时 vector

// 危险写法：
// process(download_data().begin(), download_data().end());
```

:::warning Shah is being too mild here
Shah commented on this code snippet saying it "might work sometimes, if we are lucky"—this statement **might mislead beginners**, as it implies "there are legitimate scenarios where this works." **No.** This is undefined behavior; there is no legitimate path where it works, only the illusion of "UB coincidentally behaving as expected."

The reason: the two `download_data()` calls are **two independent function calls**, returning **two different temporary `vector`s**. Their `.begin()` and `.end()` point to two completely unrelated memory blocks. Pairing the `begin` of one temporary with the `end` of another to feed into an algorithm results in an invalid range. Even worse, these temporaries are destroyed at the end of this statement, so the iterators held by the algorithm are dangling from the very start. **The correct approach is to store the result in a named variable first**, ensuring `begin` and `end` come from the same living object:

```cpp
auto data = download_data();          // 一个具名变量，一份内存
process(data.begin(), data.end());    // begin/end 来自同一个 data —— 安全
```

This illusion that "functions with the same name refer to the same object" is a common hotspot for pairing errors.
:::

## Trap Three: Insufficient Space—Cramming Too Much into a Fixed Size

The third trap relates to the output destination. When you use `std::copy` to write data to a **fixed-size** destination (such as a raw array, or a container without a `back_inserter`), if the source range is larger than the destination space, it results in an **out-of-bounds write**—which is also undefined behavior (UB) and can silently corrupt adjacent memory.

```cpp
int src[10] = {0,1,2,3,4,5,6,7,8,9};
int dst[3];   // 只有 3 个位置！
std::copy(std::begin(src), std::end(src), std::begin(dst));  // 越界写 —— UB
```

This code compiles, runs, and doesn't crash immediately, but you have written seven out-of-bounds values into the memory following `dst`. AddressSanitizer (`-fsanitize=address`) can catch this bug, reporting a heap or stack buffer overflow.

The fix is straightforward: either use `std::back_inserter` (letting the target container grow automatically), or `reserve` enough space before copying and ensure the source range does not exceed the destination's capacity. This brings us back to our first rule of thumb: **letting the container manage its own size (using an inserter) is much safer than manually calculating sizes.**

## Error Quality: Do Ranges Really Provide Better Error Messages?

Shah mentioned in his summary that "Ranges uses concepts, which gives you better error messages." This is true, but with a caveat. Let's compare the error messages from both interfaces when we pass incorrect arguments.

First, let's look at passing the wrong arguments to the classic `std::sort`—mixing the `begin` of a `vector` with the `end` of a `list` (type mismatch):

```cpp
std::vector<int> v{1,2,3};
std::list<int>   l{4,5,6};
std::sort(v.begin(), l.end());   // 两个不同容器的迭代器
```

Let's look at what happens when we pass something that isn't a range to `std::ranges::sort`:

```cpp
int not_a_range = 42;
std::ranges::sort(not_a_range);
```

Both are error line numbers from GCC 16.1.1:

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

Here comes the interesting part—**in this specific case, the error message for the ranges version (line 69) is actually longer than the classic version (line 32)**. This is because when you pass an `int` to `ranges::sort`, the compiler has to unfold the entire chain of concept constraints (`sortable` → `random_access_iterator` → ...) for you to see. The longer the chain, the more verbose the error. So, I must honestly correct a common misconception: **"ranges errors are always shorter and friendlier" does not hold true**. Its readability depends heavily on the compiler version and the specific scenario (it only became relatively mature after GCC 10+ / Clang 12+, and older compilers still spit out a screenful of template gibberish).

So, what is the *real* advantage of ranges when it comes to "errors"? It's not the line count, but **that it prevents you from writing certain bugs in the first place**. Recall Trap #2 above—the classic `std::sort` takes two iterators, and you can easily mismatch the `begin`/`end` of two different containers (like in `err_classic`). The compiler doesn't report the error until instantiation. However, `std::ranges::sort` **accepts only one range**. You simply cannot express the error where "begin comes from A and end comes from B". **Eliminating an opportunity for error is far more practical than a friendlier error message.** This is the core safety benefit of ranges, which we will expand on in Part 3.

## Transition: Must Iterators Go Away?

At this point, Shah showed a rather provocative slide—"Iterators must die". Hyperbole aside, the sentiment he expressed is real: **while the iterator interface is powerful, it is full of pitfalls**—easy to mismatch pairs, easy to reverse argument order (for three-iterator algorithms), and ugly syntax for partial sorting.

The good news is that C++20 Ranges directly addresses these pain points. It doesn't discard iterators (iterators remain the underlying mechanism, even C++26 can't do without them), but wraps a safer, more composable interface layer on top of them: **passing containers directly instead of iterator pairs, using concepts to intercept type errors early at compile-time, and using views for lazy composition**. These are the main threads of Part 3.

In the next post, we will formally dive into Ranges—starting from "why `ranges::sort` takes one fewer argument," all the way to the lazy evaluation of views, the pipe operator, `ranges::to`, and a feature that is truly eye-opening: **infinite ranges**. If you are interested in parallel versions of numeric algorithms (`reduce`, `transform_reduce`), you can check out the content regarding `<execution>` policies and `std::reduce` parallel reduction in vol5 (Concurrency)—that is where algorithms and concurrency intersect.

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
    chapter="Approx. 91 function templates as of C++23"
  />
  <ReferenceItem
    :id="3"
    author="Jonathan Boccara"
    title="105 STL Algorithms in Less Than an Hour — CppCon 2018"
    :year="2018"
    url="https://www.youtube.com/watch?v=2olsGf6JIkU"
    chapter="105 algorithms under loose counting criteria"
  />
  <ReferenceItem
    :id="4"
    author="cppreference.com"
    title="Iterator invalidation rules"
    :year="2026"
    url="https://en.cppreference.com/w/cpp/container"
    chapter="Invalidation rules after insert/erase for various containers"
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
    chapter="Iterator invalidation caused by push_back reallocation"
  />
  <ReferenceItem
    :id="7"
    author="cppreference.com"
    title="Standard library header &lt;numeric&gt;"
    :year="2023"
    url="https://en.cppreference.com/w/cpp/header/numeric"
    chapter="Approx. 14 numeric algorithms"
  />
  <ReferenceItem
    :id="8"
    author="Mike Shah"
    title="Back to Basics: C++ Ranges — CppCon 2025"
    :year="2025"
    url="https://www.youtube.com/watch?v=Q434UHWRzI0"
  />
</ReferenceCard>
