---
chapter: 3
conference: cppcon
conference_year: 2025
cpp_standard:
- 11
- 17
- 20
description: 'CppCon 2025 talk notes — Mike Shah: STL algorithms in practice and the hard constraints of iterator categories, plus an algorithm cheat sheet and an invalidation-rules table, with GCC experiments demonstrating the silent UB of iterator invalidation and how _GLIBCXX_DEBUG catches it'
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
  translated_at: '2026-09-26T15:52:52+00:00'
  engine: anthropic
  token_count: 5200
---
# STL Algorithms in Practice and Iterator Pitfalls

:::tip
This is the second article in the CppCon 2025 Mike Shah "Back to Basics: C++ Ranges" series. Last time we abstracted "traversal" from index-based loops all the way up to iterators, landing on the conclusion that **a pair of `begin`/`end` iterators defines a range**. This time we feed that pair of iterators to the STL algorithms—to see how they write the loops for you, and what hard requirements they impose on iterators. This article also dissects several classic iterator pitfalls in detail, each verified live with GCC 16.1.1. Same environment as before: Arch Linux WSL, `-std=c++20`.
:::

At the end of the last article we said that algorithms are built on top of that pair of iterators. To make that concrete, we first need to get clear on which pieces the STL is actually assembled from.

## The Three Pillars of the STL

The design philosophy of the Standard Template Library (STL) is to decouple three things: **containers** store the data, **iterators** traverse it, and **algorithms** process it<RefLink :id="1" preview="cppreference, Standard library algorithms — containers, iterators, algorithms" />. The three are glued together by iterators—an algorithm never gets to know any concrete container, it only understands iterators; as long as a container can hand out iterators that meet the requirements, every algorithm works on it. This decoupling is the fundamental reason a single `std::sort` covers `vector`, `array`, and `deque` alike.

So which headers do the algorithms actually live in?

:::warning Shah's "two headers" is a bit narrow
In the talk, Shah says "the algorithms live mainly in the `<algorithm>` and `<numeric>` headers"—fine as a first approximation, but it actually **misses several chunks**. The full picture: general-purpose algorithms (`sort`, `find`, `copy`, `transform`, and so on) are in `<algorithm>`; numeric algorithms (`accumulate`, `reduce`, `inner_product`, and so on) are in `<numeric>`; the **parallel algorithms** (things like `sort(std::execution::par, ...)` driven by an execution policy) need `<execution>` (C++17); C++20's ranges algorithms and views live in `<ranges>`; and there are even scattered strays—`std::midpoint` is in `<numeric>`, yet C++23's folding algorithm `std::fold_left` sits in `<algorithm>`. So don't hard-code "algorithms = two headers" into memory; the more accurate summary is "algorithms are spread across several headers, with `<algorithm>` as the workhorse."
:::

## Algorithm Cheat Sheet: By Category, and the Iterator Category Each Algorithm Requires

There are over a hundred STL algorithms; rote memorization is pointless. The better mnemonic is to **group them by category** and remember **each category's hard requirement on iterator category**—because that directly decides whether you can use an algorithm on a given container. The table below is the centerpiece of this article's own added material; Shah didn't unpack any of it in the talk:

| Category | Representative algorithms | Required iterator category |
|------|------|------|
| Read-only search | `find` / `find_if` / `count` / `accumulate` | input (the weakest suffices) |
| Mutating copy | `copy` / `transform` / `replace` / `fill` | forward / output |
| Partitioning | `partition` / `stable_partition` | forward (the stable version needs bidirectional) |
| Sorting | `sort` / `stable_sort` / `partial_sort` | **random_access** (a hard requirement) |
| Binary search | `lower_bound` / `upper_bound` / `binary_search` | forward (**and the range must already be sorted**) |
| Numeric reduction | `reduce` / `transform_reduce` / `inner_product` | input |
| Heap operations | `push_heap` / `pop_heap` / `sort_heap` | random_access |

The one row most worth memorizing: **the sorting family requires random access iterators**. That means they only work on containers with contiguous or random-access storage—`vector`, `array`, `deque`—and **flat-out fail to compile on `std::list`**. This is not advice; it is a hard constraint. Let's verify it live.

## Experiment: std::sort Does Not Work on std::list

A `std::list` has bidirectional iterators: no `it + n`, and no subtracting two iterators either. But `std::sort` needs random access internally (it computes `__last - __first` to estimate recursion depth). So what happens when we stuff a list's iterators into it?

```cpp
#include <algorithm>
#include <list>

int main()
{
    std::list<int> l{3, 1, 2};
    std::sort(l.begin(), l.end());  // Won't compile!
}
```

The error GCC 16.1.1 prints (key lines only):

```bash
❯ g++ -std=c++20 list_sort.cpp -o list_sort
/usr/include/c++/16.1.1/bits/stl_algo.h:1914:50: error: no match for ‘operator-’
   (operand types are ‘std::_List_iterator<int>’ and ‘std::_List_iterator<int>’)
 1914 |                                 std::__lg(__last - __first) * 2,
   |                                           ~~~~~~~^~~~~~~~~
```

See that? The failure happens exactly at the `__last - __first` step: `std::sort` wants to compute the range length with iterator subtraction, but `_List_iterator` simply defines no `operator-` (bidirectional iterators understand `++`/`--`, not subtraction). This is what "the iterator category doesn't meet the algorithm's requirements" looks like in practice. If you genuinely need to sort a `list`, call its member function `l.sort()`—a merge sort tailor-made for linked lists, still O(n log n), but with no dependence on random access.

## sort, partition, copy, transform: What the Common Algorithms Look Like

Let's walk quickly through the most-used algorithms to build intuition. Their parameter shapes are astonishingly uniform—the vast majority are **a pair of iterators `(first, last)` plus an optional predicate or destination**.

```cpp
#include <algorithm>
#include <vector>
#include <iterator>
#include <random>

void demo(std::vector<int>& v, const std::vector<int>& src)
{
    // Sort the whole range
    std::sort(v.begin(), v.end());

    // Partial sort: only [begin, begin+3) gets sorted; the trailing elements are in unspecified order but all >= the first 3
    // std::partial_sort(v.begin(), v.begin() + 3, v.end());

    // Partition: move elements satisfying the predicate to the front; returns the dividing point
    auto it = std::partition(v.begin(), v.end(), [](int x) { return x < 4; });

    // Copy: back_inserter push_backs automatically, no need to size things up front
    std::copy(src.begin(), src.end(), std::back_inserter(v));

    // Shuffle: a random number engine is mandatory (rand() has been discouraged since C++11)
    std::shuffle(v.begin(), v.end(), std::mt19937{std::random_device{}()});
}
```

Two details here deserve an extra word. `std::back_inserter(v)` returns an **output iterator**—whatever you write into it goes through `v.push_back()`—which spares you the "count how many elements I'm copying, then `reserve` up front" dance and makes it `copy`'s most common companion. `std::shuffle` is the reminder that **since C++11, random numbers should come from the engines in `<random>` (`std::mt19937` and friends), not from the old `rand()`**—`rand()` is low quality and has thread-safety problems.

Next, `std::transform`, which packages up "apply a function to every element". Note the `cbegin`/`cend` here—**the const flavors of iterators**—signaling "I only read the source range, I don't modify it":

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

`cbegin`/`cend` return `const_iterator`; `rbegin`/`rend` return reverse iterators. One easy trap: **these iterators must be used in matched pairs**—you can't pair `cbegin()` with `end()` (one is const, the other isn't; the types don't match). After C++20, `const_iterator`'s standing inside the standard library got another promotion (P0896 and related proposals), because the ranges machinery leans on it heavily.

## rotate: Argument Order Is the Biggest Trap

`std::rotate` is a very useful algorithm—and also a spectacularly easy one to get wrong. What it does is "rotate the elements of the range so that the element `middle` points to becomes the new first element." The signature takes three iterators: `std::rotate(first, middle, last)`.

```cpp
std::vector<int> v{1, 2, 3, 4, 5};
std::rotate(v.begin(), v.begin() + 2, v.end());
// Result: {3, 4, 5, 1, 2}  — middle (begin+2, i.e. 3) became the new first element
```

Observed output:

```bash
❯ g++ -std=c++20 rot_ok.cpp -o rot_ok && ./rot_ok
rotate(begin, begin+2, end) on {1,2,3,4,5} -> { 3 4 5 1 2 }
```

The trap here: **the overwhelming majority of algorithms take two iterators `(first, last)`, but `rotate` (along with `partial_sort`, `nth_element`, and friends) takes three: `(first, middle, last)`**. Once "two arguments" hardens into muscle memory, writing `rotate` makes it remarkably easy to swap the positions of `middle` and `last`. Shah himself has grumbled about this: he once hand-rolled insertion sort by finding the insertion point with `upper_bound` and then rotating, and his verdict was "too clever, ugly."

And what happens when you write it backwards? I swapped `middle` and `last`, calling `rotate(first, last, middle)`:

```cpp
std::vector<int> w{1, 2, 3, 4, 5};
std::rotate(w.begin(), w.end(), w.begin() + 2);  // Wrong argument order
```

```bash
❯ g++ -std=c++20 rot_bad.cpp -o rot_bad && ./rot_bad
about to call rotate(begin, end, begin+2)...
[Program crashed, exit code 139 — SIGSEGV]
```

An immediate segmentation fault (exit code 139 = SIGSEGV). The reason is direct: `std::rotate` requires both `[first, middle)` and `[middle, last)` to be valid sub-ranges—in other words, the three iterators must satisfy the order `first <= middle <= last`. Written as `(first, last, middle)`, the second sub-range `[middle_arg=last, last_arg=middle)` becomes invalid (its end lies before its beginning), the algorithm dereferences out-of-bounds positions, and down it goes.

:::warning For three-iterator algorithms, always check the docs for argument order
`rotate`, `partial_sort`, `nth_element`, `stable_partition`—these algorithms don't take a simple `(first, last)` but a three-part form like `(first, middle, last)`. Before using one, always confirm what `middle` actually denotes. This improves in the ranges versions covered in Part 3—ranges versions usually take fewer arguments (you pass the container directly), which shrinks the room for pairing mistakes.
:::

## How Many Algorithms Are There? Taking the "More Than 200" Claim with a Grain of Salt

In the talk, Shah cited a widely circulated figure: "a 2018 CppCon talk said at least 105 algorithms; now there are more than 200." Is that claim accurate? Let's be strict about it<RefLink :id="2" preview="cppreference, Standard library header <algorithm> — function template count" />.

First, where "105" comes from: Jonathan Boccara's CppCon 2018 talk, *105 STL Algorithms in Less Than an Hour*<RefLink :id="3" preview="Jonathan Boccara, CppCon 2018 — 105 STL Algorithms" />. That is a **very loose counting scheme**—it tallies each `_if` variant (`find` / `find_if`), `_n` variant (`copy` / `copy_n`), and `_copy` variant (`remove` / `remove_copy`) as its own independent algorithm, all for the sake of being easy to remember and to present in a talk.

So what is the strict number? I cross-checked cppreference; as of C++23:

- The `<algorithm>` header holds roughly **91** `std::` function templates (not counting the ranges versions).
- The `<numeric>` header holds **14** numeric algorithms (`accumulate`, `reduce`, `inner_product`, and so on; C++26 adds 5 more saturating-arithmetic ones, bringing the count to 19).
- Under the `std::ranges::` namespace there are roughly **100** "constrained algorithms" (niebloids—simply put, the ranges versions of the algorithms).
- On top of that, about 14 uninitialized-memory-related algorithms live in `<memory>`.

So the "more than 200" claim **only holds under a counting scheme that books the `std::` and `std::ranges::` API families separately and throws in all the variant overloads**. Count distinct algorithm names and the real figure is roughly **110 to 120**.

:::tip How to phrase it accurately
Rather than "the STL has more than 200 algorithms," the more rigorous statement is: **the STL has a hundred-plus distinct algorithms; if both the `std::` and `std::ranges::` interfaces count as entries, there are indeed more than 200 API entry points.** The distinction matters quite a bit in interviews and technical writing—"more than 200" sounds impressive, but a large share of it is variants of the same algorithm plus ranges mirror copies.
:::

## Pitfall 1: Iterator Invalidation — the Most Insidious Killer

The algorithms themselves aren't hard once you've used them enough; what really bites is **the interplay between iterator lifetime and container lifetime**. The number-one trap is **iterator invalidation**.

Take this harmless-looking snippet:

```cpp
std::vector<int> v{1, 2, 3};
auto it = v.begin();        // it points at v's first element
v.push_back(4);             // if this triggers a reallocation, it is left dangling!
std::cout << *it << '\n';   // dereferencing a dangling iterator — UB
```

The problem is `push_back`. Under the hood a `vector` is one contiguous dynamic array; when capacity runs out it **allocates a larger block of memory**, moves the old elements over, and frees the old block. But your `it` still points into that **already-freed old memory**—it has become a dangling pointer (the standard's term is "singular iterator"). Dereferencing `*it` at this point is undefined behavior.

The frightening part: **UB does not necessarily crash immediately**. It often shows up as "reads a value that looks perfectly normal," so you assume everything is fine, merge the code into the mainline, and then one day it crashes inexplicably on a customer's machine. Let's test a plain build (no debug instrumentation):

```cpp
#include <vector>
#include <iostream>
int main()
{
    std::vector<int> v{1, 2, 3};
    auto it = v.begin();
    std::cout << "before push_back: *it=" << *it << ", cap=" << v.capacity() << "\n";
    v.push_back(4); v.push_back(5); v.push_back(6); v.push_back(7);  // guaranteed to reallocate
    std::cout << "after  push_back: cap=" << v.capacity() << "\n";
    std::cout << "deref stale it: " << *it << "\n";   // UB: reading freed memory
}
```

```bash
❯ g++ -std=c++20 -O0 inval.cpp -o inval && ./inval; echo "退出码=$?"
before push_back: *it=1, cap=3
after  push_back: cap=12
deref stale it: -40771459
退出码=0
```

See that? The program **exits normally (exit code 0) without a single complaint**, yet the value it read out is garbage like `-40771459`. After the growth the vector's capacity went from 3 to 12, the old memory was freed, and what `it` points at holds leftover random data. This is UB at its most treacherous: **a silent error**.

So how do we catch it? GCC/Clang provide a debug macro, `-D_GLIBCXX_DEBUG`; with it enabled, standard library iterators carry bounds and validity checks, and the moment you dereference an invalidated iterator, the program aborts immediately and prints a diagnostic. Same code, compiled with the debug mode on:

```bash
❯ g++ -std=c++20 -O0 -g -D_GLIBCXX_DEBUG inval.cpp -o inval_dbg && ./inval_dbg; echo "退出码=$?"
before push_back: *it=1, cap=3
after  push_back: cap=12
/usr/include/c++/16.1.1/debug/safe_iterator.h:352:
Error: attempt to dereference a singular iterator.
Objects involved in the operation:
    iterator "this" @ 0x7fff6bd63820 {
      type = gnu_cxx::normal_iterator<int*, std::vector<int>>(mutable iterator);
      state = singular;   ← iterator invalidated
      references sequence with type 'std::debug::vector<int>' @ 0x7fff6bd63850
    }
退出码=134   ← 134 = SIGABRT, a deliberate abort by the debug library
```

Caught red-handed: `state = singular` tells you outright that the iterator is invalidated, and `attempt to dereference a singular iterator` pinpoints exactly what you did. One `-D_GLIBCXX_DEBUG` macro turns "silent UB" into "instant explosion plus a precise location"—turn it on during development, turn it off for release (it costs performance). On the MSVC side the corresponding switch is `_ITERATOR_DEBUG_LEVEL=2`; Release configurations default to 0 or 1, and only the Debug configuration uses 2.

:::tip Iterator invalidation rules at a glance (checked against cppreference)
Invalidation rules vary a lot from container to container; remember the gist and look up the specifics in the table<RefLink :id="4" preview="cppreference, Iterator invalidation — rules per container" />:

- **`vector` / `string`**: `push_back` invalidates **all** iterators only when it triggers a reallocation (capacity changes); without reallocation only `end()` changes. After a `reserve`, iterators stay valid as long as you don't exceed the reserved capacity.
- **`deque`**: insertion at either end invalidates **all iterators** (even without reallocation), but **references and pointers stay valid**—so be careful when iterating a deque: storing references beats storing iterators.
- **`list` / `forward_list`**: insertion and `splice` invalidate **no** existing iterators (list nodes don't move house); only the iterator pointing at the erased node is invalidated by `erase`.
- **`unordered_*`**: a `rehash` (triggered when an insertion changes the bucket count) invalidates **iterators, but not references and pointers**.

Keep one master rule in mind: **whenever the container's internals might "move house" (a contiguous-storage container reallocating, a hash table rehashing), iterators may be invalidated; node-based containers (list nodes, tree nodes) don't move house, so their iterators are stable.**
:::

## Pitfall 2: Mismatched Iterator Pairs — begin and end Must Come from the Same Object

The second trap is about pairing. Algorithms require `first` and `last` to come from **the same container**, but C++ has no way to enforce that at runtime—pass two iterators from two different containers and the compiler accepts them without blinking; what follows is UB.

The classic faceplant comes from Jason Turner's C++ Weekly (which Shah specifically cited in the talk): a function returns a temporary `vector`, and you save a little effort by chaining `.begin()` and `.end()` right on the call:

```cpp
std::vector<int> download_data();  // each call returns a brand-new temporary vector

// The dangerous version:
// process(download_data().begin(), download_data().end());
```

:::warning Shah puts this too lightly
Shah's commentary on this code was "maybe sometimes it works, maybe we get lucky"—a phrasing that **can mislead newcomers**, because it implies "there are legitimate cases where this thing works." **There are none.** This is undefined behavior; no "legitimately working" path exists, only the illusion of "UB that happens to look normal."

The reason: the two `download_data()` calls are **two independent function calls**, returning **two different temporary `vector`s**. Their `.begin()` and `.end()` point into two blocks of memory that have nothing to do with each other. Pair one temporary's `begin` with another temporary's `end` and feed that to an algorithm—the "range" isn't even legal. Worse, both temporaries are destroyed at the end of that statement, so the iterators the algorithm holds are dangling from the very start. **The correct way is to store the result in a named variable first**, so that `begin` and `end` come from the same living object:

```cpp
auto data = download_data();          // one named variable, one block of memory
process(data.begin(), data.end());    // begin/end come from the same data — safe
```

The illusion that "same function name means same object" is exactly where pairing mistakes breed.
:::

## Pitfall 3: Not Enough Space — Cramming Too Much into a Fixed-Size Destination

The third trap concerns the output destination. When `std::copy` writes data into a **fixed-size** target (a native array, say, or a container without a `back_inserter`), and the source range is larger than the destination space, you get an **out-of-bounds write**—UB again, and one that can silently corrupt neighboring memory.

```cpp
int src[10] = {0,1,2,3,4,5,6,7,8,9};
int dst[3];   // only 3 slots!
std::copy(std::begin(src), std::end(src), std::begin(dst));  // out-of-bounds write — UB
```

This code compiles, runs, and won't report anything on the spot—but you just wrote 7 values you had no business writing into the memory after `dst`. AddressSanitizer (`-fsanitize=address`) catches this kind of bug and reports a heap/stack buffer overflow.

The escape is straightforward: either use `std::back_inserter` (letting the destination container grow on its own), or `reserve` enough space before the copy and confirm the source range is no larger than the destination capacity. Back to the first lesson: **letting the container manage its own size (via an inserter) is far safer than hand-computing sizes yourself.**

## Error Quality: Do Ranges Really Give Friendlier Errors

In his wrap-up, Shah said "Ranges uses concepts, so it will give you better error messages." That's true, but it needs qualifying—let's actually compare what the two interfaces report when you "pass the wrong arguments."

First, classic `std::sort` misused—pairing a `vector`'s `begin` with a `list`'s `end` (mismatched types):

```cpp
std::vector<int> v{1,2,3};
std::list<int>   l{4,5,6};
std::sort(v.begin(), l.end());   // iterators from two different containers
```

Then the ranges version misused—handing `std::ranges::sort` something that isn't a range at all:

```cpp
int not_a_range = 42;
std::ranges::sort(not_a_range);
```

Error line counts from GCC 16.1.1 for both:

```bash
❯ # Classic version
❯ g++ -std=c++20 err_classic.cpp 2>err_c.txt; wc -l < err_c.txt
32
❯ head -3 err_c.txt
err_classic.cpp:7:14: error: no matching function for call to
  'sort(std::vector<int>::iterator, std::__cxx11::list<int>::iterator)'

❯ # Ranges version
❯ g++ -std=c++20 err_ranges.cpp 2>err_r.txt; wc -l < err_r.txt
69
```

Here's the interesting part: **in this concrete example, the ranges error (69 lines) is actually longer than the classic one (32 lines)**. That's because when you pass an `int` to `ranges::sort`, the compiler unrolls the entire concept-constraint chain (`sortable` → `random_access_iterator` → ...) for you to read—the longer the chain, the more lavish the spew. So I have to honestly correct a common impression: **"ranges errors are always shorter and friendlier" simply doesn't hold**; their readability depends heavily on compiler version and the concrete scenario (things only became reasonably mature after GCC 10+ / Clang 12+; older compilers still hand you a full screen of template gibberish).

So what is ranges' real advantage when it comes to "errors"? Not line count—it's that **certain bugs become impossible to write in the first place**. Recall Pitfall 2 above: classic `std::sort` takes two iterators, so you can perfectly well cross-pair `begin`/`end` from two different containers (as in `err_classic`), and the compiler only complains at instantiation time. `std::ranges::sort` **takes a single container**—the mistake "begin comes from A, end comes from B" isn't even expressible. **One fewer opportunity to go wrong is worth far more than friendlier errors.** That is ranges' core safety payoff, and we'll expand on it in Part 3.

## Transition: Iterators Must Die

At this point, Shah put up a rather dramatic slide—"Iterators must die." Hyperbole aside, the feeling behind it is real: **the iterator interface, powerful as it is, is riddled with pitfalls in use**—pairs are easy to mismatch, argument order (for the three-iterator algorithms) is easy to reverse, and the partial-sort spelling is ugly.

The good news is that C++20's Ranges exists precisely to attack these pain points. It doesn't throw iterators away (iterators remain the underlying machinery—not even C++26 can do without them); instead it wraps a safer, more composable interface on top of them: **pass containers directly instead of iterator pairs, intercept type errors early with concepts, and compose lazily with views**. Those are the main threads of Part 3.

In the next article we formally enter Ranges—starting from "why `ranges::sort` takes one fewer argument", going all the way through lazy evaluation of views, the pipe operator, `ranges::to`, and one genuinely eye-opening feature: **infinite ranges**. If you're interested in the parallel versions of the numeric algorithms (`reduce`, `transform_reduce`), you can read ahead in Volume 5's coverage of `<execution>` policies and parallel reduction with `std::reduce`—that's where algorithms and concurrency meet.

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
    chapter="About 91 function templates as of C++23"
  />
  <ReferenceItem
    :id="3"
    author="Jonathan Boccara"
    title="105 STL Algorithms in Less Than an Hour — CppCon 2018"
    :year="2018"
    url="https://www.youtube.com/watch?v=2olsGf6JIkU"
    chapter="105 algorithms under the loose counting scheme"
  />
  <ReferenceItem
    :id="4"
    author="cppreference.com"
    title="Iterator invalidation rules"
    :year="2026"
    url="https://en.cppreference.com/w/cpp/container"
    chapter="Invalidation rules per container after insert/erase"
  />
  <ReferenceItem
    :id="5"
    author="cppreference.com"
    title="std::rotate"
    :year="2026"
    url="https://en.cppreference.com/w/cpp/algorithm/rotate"
    chapter="Argument order: first, middle, last"
  />
  <ReferenceItem
    :id="6"
    author="cppreference.com"
    title="std::vector — Iterator invalidation"
    :year="2026"
    url="https://en.cppreference.com/w/cpp/container/vector"
    chapter="push_back reallocation invalidates iterators"
  />
  <ReferenceItem
    :id="7"
    author="cppreference.com"
    title="Standard library header &lt;numeric&gt;"
    :year="2023"
    url="https://en.cppreference.com/w/cpp/header/numeric"
    chapter="About 14 numeric algorithms"
  />
  <ReferenceItem
    :id="8"
    author="Mike Shah"
    title="Back to Basics: C++ Ranges — CppCon 2025"
    :year="2025"
    url="https://www.youtube.com/watch?v=Q434UHWRzI0"
  />
</ReferenceCard>
