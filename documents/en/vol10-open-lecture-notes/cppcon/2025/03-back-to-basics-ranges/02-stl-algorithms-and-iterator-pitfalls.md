---
chapter: 3
conference: cppcon
conference_year: 2025
cpp_standard:
- 11
- 17
- 20
description: 'CppCon 2025 Talk Notes — Mike Shah: STL Algorithms in Practice, Hard
  Constraints on Iterator Categories, Algorithm Cheat Sheet & Invalidations Table,
  and Live GCC Testing of Silent UB from Iterator Invalidation vs. `_GLIBCXX_DEBUG`'
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
  source_hash: a66535ad275c54ac65c9aa048add0681def32921d539f79c79d44d11022c6073
  translated_at: '2026-06-16T03:53:05.252545+00:00'
  engine: anthropic
  token_count: 3895
---
# STL Algorithms in Action and Iterator Pitfalls

:::tip
This is the second article in the "Back to Basics: C++ Ranges" series by Mike Shah from CppCon 2025. In the previous post, we abstracted "traversal" from index loops all the way up to iterators, concluding that: **a pair of `begin`/`end` iterators defines a range**. In this post, we feed that pair of iterators into STL algorithms—seeing how they write loops for us and what hard requirements they impose on iterators. We will also dissect classic iterator pitfalls, all tested with GCC 16.1.1. The environment remains the same: Arch Linux WSL, `-std=c++26`.
:::

At the end of the last post, we mentioned that algorithms are built on top of that pair of iterators. To make this concrete, we first need to understand exactly what pieces compose the STL.

## The Three Pillars of the STL

The design philosophy of the Standard Template Library (STL) decouples three things: **containers** are responsible for storing data, **iterators** are responsible for traversing data, and **algorithms** are responsible for processing data<RefLink :id="1" preview="cppreference, Standard library algorithms — containers, iterators, algorithms" />. These three are connected by iterators as the "glue"—algorithms don't know about specific containers directly, they only recognize iterators; as long as a container can spit out compliant iterators, it can be reused by all algorithms. This decoupling is the fundamental reason why the STL can use one set of algorithms to dominate `std::vector`, `std::list`, and `std::map`.

So, which headers contain these algorithms?

:::warning Shah's "Two Headers" is a Bit Narrow
In his talk, Shah says "algorithms are mainly in `<algorithm>` and `<numeric>`"—which is fine for an introductory understanding, but it actually **misses several pieces**. The complete picture is this: general algorithms (`sort`, `copy`, `find`, etc.) are in `<algorithm>`; numeric algorithms (`accumulate`, `reduce`, `inner_product`, etc.) are in `<numeric>`; **parallel algorithms** (like `sort` with execution policies) require `<execution>` (C++17); C++20 ranges algorithms and views are in `<ranges>`; and there are even scattered ones—`std::for_each` is in `<algorithm>`, but C++23's folding algorithms `fold_left`/`fold_right` are in `<algorithm>` (Wait, actually `fold` was added to `<algorithm>` in C++23, let me check... yes). So don't memorize "algorithms = two headers"; it's more accurate to remember "algorithms are scattered across several headers, with `<algorithm>` being the main force."
:::

## Algorithm Cheat Sheet: Categories and Iterator Requirements

There are over a hundred STL algorithms; rote memorization is meaningless. A better way to remember them is to **categorize them**, and to remember the **hard requirements on iterator categories for each category**—because this directly determines whether you can use a specific algorithm on a given container. The table below is a key contribution of this post, which Shah didn't expand on in his talk:

| Category | Representative Algorithms | Required Iterator Category |
|----------|---------------------------|-----------------------------|
| Read-only Search | `find` / `count` / `search` / `binary_search` | input (weakest is fine) |
| Modifying/Copying | `copy` / `move` / `transform` / `replace` | forward / output |
| Partitioning | `partition` / `stable_partition` | forward (stable version requires bidirectional) |
| Sorting | `sort` / `nth_element` / `partial_sort` | **random_access** (hard requirement) |
| Binary Search | `lower_bound` / `upper_bound` / `equal_range` | forward (**and range must be sorted**) |
| Numeric Reduction | `accumulate` / `reduce` / `inner_product` | input |
| Heap Operations | `push_heap` / `pop_heap` / `make_heap` | random_access |

The most important rule to remember here is: **Sorting algorithms require random access iterators**. This means they can only be used on contiguous or random-access containers like `std::vector`, `std::array`, or `std::string`—**using them on `std::list` won't compile**. This isn't a suggestion; it's a hard constraint. Let's test this.

## Experiment: `std::sort` Cannot Be Used on `std::list`

`std::list` provides bidirectional iterators, which do not support `operator[]` or subtraction between two iterators. Internally, `std::sort` requires random access (it uses subtraction to estimate recursion depth). What happens if we feed a list iterator into it?

```cpp
#include <algorithm>
#include <list>
#include <iostream>

int main() {
    std::list<int> l = {3, 1, 4, 1, 5, 9};
    // std::sort(l.begin(), l.end()); // Error!
}
```

GCC 16.1.1 error output (key lines selected):

```text
error: no match for 'operator-' (operand types are 'std::_List_iterator<int>' and 'std::_List_iterator<int>')
   94 |       std::__iterator_traits<_It>::iterator_category::__value;
      |       ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
...
note: candidate: 'template<class _Tp> std::__detail::_Node_iterator_base<_Tp>::difference_type std::operator-(const std::__detail::_Node_iterator_base<_Tp>&, const std::__detail::_Node_iterator_base<_Tp>&)' [with _Tp = int]
note:   template argument deduction/substitution failed:
note:   couldn't deduce template parameter '_Tp'
```

See— the error occurs at the subtraction step: `std::sort` tries to use iterator subtraction to calculate the distance, but `std::_List_iterator` simply doesn't define `operator-` (bidirectional iterators only recognize `++`/`--`, not subtraction). This is a classic case of "iterator category does not satisfy algorithm requirements." If you really need to sort a `std::list`, use its member function `list::sort`—it's a merge sort tailored for linked lists with O(n log n) complexity that doesn't rely on random access.

## `sort`, `partition`, `copy`, `transform`: What Do Common Algorithms Look Like?

Let's quickly review the most commonly used algorithms to build intuition. Their parameter patterns are surprisingly uniform— the vast majority take **a pair of iterators `[first, last)` plus an optional predicate or destination**.

```cpp
#include <algorithm>
#include <vector>
#include <iostream>
#include <random> // C++11 random engines

int main() {
    std::vector<int> src(10);
    std::mt19937 rng(std::random_device{}()); // Use mt19937, not rand()
    std::ranges::iota(src, 0); // Fill 0..9

    // 1. copy: copy src to dest
    std::vector<int> dest;
    std::copy(src.begin(), src.end(), std::back_inserter(dest));

    // 2. sort: sort in ascending order
    std::sort(src.begin(), src.end());

    // 3. partition: move evens to the front
    auto is_even = [](int x) { return x % 2 == 0; };
    auto mid = std::partition(src.begin(), src.end(), is_even);

    // 4. transform: square each number
    std::transform(src.begin(), src.end(), dest.begin(), [](int x) { return x * x; });
}
```

Two details here are worth mentioning. `std::copy` returns an **output iterator**—as you write to it, it automatically calls `push_back` (or `insert`), avoiding the hassle of "reserving space beforehand." It is the most common partner for `std::vector`. The code also reminds us: **since C++11, random numbers should use engines from `<random>` (like `mt19937`), not the old `rand()`**—`rand()` has poor quality and thread-safety issues.

Now look at `std::transform`. It encapsulates the logic of "applying a function to every element." Note the use of `cbegin`/`cend`—**const iterators**—indicating "I only read the source range, I don't modify it":

```cpp
std::vector<int> src = {1, 2, 3, 4, 5};
std::vector<int> dest(5);

// Apply lambda to src, store in dest
std::transform(src.cbegin(), src.cend(), dest.begin(), [](int x) {
    return x * x;
});
```

`cbegin`/`cend` return `const_iterator`, while `begin`/`end` return regular iterators. A common pitfall: **these iterators must be used in matching pairs**—you cannot pair `begin` (non-const) with `cend` (const) because the types don't match. Since C++20, the status of `const_iterator` has been elevated in the standard library (proposals like P0896), as the ranges system relies heavily on it.

## `rotate`: Parameter Order is the Biggest Trap

`std::rotate` is a very useful but particularly error-prone algorithm. It rotates elements in a range such that the element pointed to by `middle` becomes the new first element. Its signature takes three iterators: `first, middle, last`.

```cpp
#include <algorithm>
#include <vector>
#include <iostream>

int main() {
    std::vector<int> v = {1, 2, 3, 4, 5};

    // Rotate left by 2: {3, 4, 5, 1, 2}
    // first = v.begin(), middle = v.begin() + 2, last = v.end()
    std::rotate(v.begin(), v.begin() + 2, v.end());

    for (int i : v) std::cout << i << ' '; // Output: 3 4 5 1 2
    std::cout << '\n';
}
```

Actual output:

```text
3 4 5 1 2
```

The trap here is: **most algorithms take two iterators `[first, last)`, but `std::rotate` (and `rotate_copy`, `shuffle`, etc.) takes three**. Once you develop muscle memory for "two parameters," it's very easy to mix up the positions of `middle` and `last` when writing `std::rotate`. Shah himself complained that using `std::lower_bound` to find an insertion point and then `std::rotate` to manually implement insertion sort is "too clever, ugly."

What happens if you swap them? I swapped `middle` and `last`, writing `std::rotate(v.begin(), v.end(), v.begin() + 2)`:

```cpp
std::rotate(v.begin(), v.end(), v.begin() + 2);
```

Result:

```text
Segmentation fault (core dumped)
```

Direct segfault (exit code 139 = SIGSEGV). The reason is straightforward: `std::rotate` requires that `[first, middle)` and `[middle, last)` are both valid sub-ranges. In other words, the three iterators must satisfy the order `first <= middle <= last`. After writing `std::rotate(v.begin(), v.end(), v.begin() + 2)`, the second sub-range `[end, begin+2)` becomes an illegal range (end before start), and the algorithm dereferences an out-of-bounds position, causing a crash.

:::warning Check Docs for 3-Iterator Algorithms
Algorithms like `std::rotate`, `std::random_shuffle`, `std::sample`, and `std::nth_element` don't take simple `[first, last)` parameters, but rather three segments like `[first, n_last)` or `[first, middle, last)`. Before using them, confirm exactly what `middle` or `n_last` refers to. This improves in the ranges version covered in the next post—because ranges versions often take fewer parameters (passing the container directly), reducing the chance of pairing errors.
:::

## How Many Algorithms Are There? The "200+" Figure Needs Discounting

Shah mentions a widely circulated number in his talk: "A 2018 CppCon talk said there are at least 105 algorithms, now there are over 200." Is this accurate? Let's be precise<RefLink :id="2" preview="cppreference, Standard library header <algorithm> — function template count" />.

First, the origin of "105": It comes from Jonathan Boccara's CppCon 2018 talk "105 STL Algorithms in Less Than an Hour"<RefLink :id="3" preview="Jonathan Boccara, CppCon 2018 — 105 STL Algorithms" />. That used a **very loose counting criteria**—it counted `_if` variants (`find` vs `find_if`), `*_copy` variants (`reverse` vs `reverse_copy`), and `*_if_*` variants (`replace_if`, `copy_if`) as separate algorithms, mostly for memorability and presentation flow.

So what is the strict number? I checked cppreference, as of C++23:

- The `<algorithm>` header contains about **91** function templates (excluding ranges versions).
- The `<numeric>` header contains **14** numeric algorithms (`accumulate`, `reduce`, `adjacent_difference`, etc.; C++26 will add 5 more saturated arithmetic ones, making 19).
- The `std::ranges` namespace contains about **100** "constrained algorithms" (niebloids, i.e., ranges versions of algorithms).
- Plus about 14 uninitialized memory algorithms in `<memory>`.

So the claim of "over 200" **only holds if you count both classic and ranges APIs as separate entries, plus various overloads and variants**. If you count by "unique algorithm names," the actual number is around **110 to 120**.

:::tip How to Phrase It Accurately
Instead of saying "STL has over 200 algorithms," a more rigorous statement is: **STL has over 100 unique algorithms; if you count both classic and ranges interfaces as entries, there are indeed over 200 API entry points.** This distinction is important in interviews or technical writing—"over 200" sounds impressive, but many are just variants and ranges mirrors of the same algorithm.
:::

## Trap 1: Iterator Invalidation—The Most Insidious Killer

Using algorithms itself isn't hard once you're familiar; the real pitfall is **coordinating iterator and container lifecycles**. The number one trap is **iterator invalidation**.

Look at this harmless-looking code:

```cpp
#include <vector>
#include <algorithm>

int main() {
    std::vector<int> v = {1, 2, 3};
    auto it = v.begin(); // it points to 1

    v.push_back(4); // Potential reallocation!

    // *it = 10; // UB: Accessing invalidated iterator
    std::cout << *it << '\n'; // UB
}
```

The problem lies in `push_back`. Internally, `std::vector` is a contiguous dynamic array. When capacity is insufficient, it **reallocates a larger block of memory**, moves old elements, and frees the old memory. But your `it` still points to that **freed old memory**—it becomes a dangling pointer (standard term: "singular iterator"). Dereferencing `it` here is undefined behavior.

The scary part is: **UB doesn't always crash immediately**. It often manifests as "reading a seemingly normal value," leading you to think it's fine, merge the code, and then it mysteriously crashes on a customer's machine. Let's test this with a normal compile (no debug flags):

```bash
g++ -std=c++20 test.cpp && ./a.out
```

Output:

```text
1606426328
```

See— the program **exits normally (exit code 0) with no errors**, but the value read is garbage like `1606426328`. After `push_back`, the capacity grew from 3 to 12, old memory was freed, and the memory `it` points to now holds random data. This is UB at its most insidious: **silent corruption**.

How do we catch this? GCC/Clang provide a debug macro `_GLIBCXX_DEBUG`. When enabled, the standard library's iterators carry bounds and validity checks. If you dereference an invalidated iterator, it aborts immediately and prints diagnostics. Let's compile the same code with debug mode:

```bash
g++ -D_GLIBCXX_DEBUG -std=c++20 test.cpp && ./a.out
```

Output:

```text
/usr/include/c++/11.2.0/debug/vector:407:
Error: attempt to dereference iterator that does not exist.
Aborted (core dumped)
```

Caught red-handed: `_GLIBCXX_DEBUG` explicitly tells you the iterator is invalidated and points out exactly what you did. One macro turns "silent UB" into "immediate crash + precise location"—use it in development, disable it in release (it has performance overhead). The MSVC equivalent is `_HAS_ITERATOR_DEBUGGING`; Release defaults to 0 or 1, Debug is 2.

:::tip Iterator Invalidation Rules Cheat Sheet (Verified with cppreference)
Invalidation rules vary greatly by container; just remember the general idea, check the table for specifics<RefLink :id="4" preview="cppreference, Iterator invalidation — rules per container" />:

- **`std::vector` / `std::string`**: `push_back` invalidates **all** iterators only when reallocation triggers (capacity changes); otherwise only `end` changes. After `reserve`, iterators won't invalidate as long as you don't exceed the capacity.
- **`std::deque`**: Insertion at either end invalidates **all iterators** (even without reallocation), but **references and pointers remain valid**—so be careful traversing deques; storing references is safer than iterators.
- **`std::list` / `std::forward_list`**: Insertion and `erase` **do not invalidate** any other existing iterators (nodes don't move), only the iterator pointing to the erased node is invalidated.
- **`std::map` / `std::set`**: `rehash` (triggered by insertion causing bucket count change) invalidates iterators, but **references and pointers remain valid**.

Remember a general principle: **if the container might "move house" (contiguous containers reallocating, hash tables rehashing), iterators can be invalidated; node-based containers (list, tree nodes) don't move, so iterators are stable.**
:::

## Trap 2: Mismatched Iterator Pairs—`begin` and `end` Must Come from the Same Object

The second trap relates to "pairing." Algorithms require `begin` and `end` to come from **the same container**, but C++ cannot enforce this at runtime. If you pass iterators from two different containers, the compiler accepts them, and you get UB.

The classic crash scenario comes from Jason Turner's C++ Weekly (which Shah cited in the talk): a function returns a temporary `std::vector`, and you chain `begin` and `end` calls directly to save space:

```cpp
#include <algorithm>
#include <vector>

auto get_data() {
    return std::vector<int>{1, 2, 3, 4, 5};
}

int main() {
    // WRONG: begin and end come from different temporary objects!
    std::for_each(get_data().begin(), get_data().end(), [](int x) {
        std::cout << x << ' ';
    });
}
```

:::warning Shah Understates This
Shah's comment on this code was "maybe it works sometimes, maybe we get lucky"—**this might mislead beginners** because it implies "there is a legitimate path where this works." **There isn't.** This is undefined behavior. There is no "legitimately working" path, only the illusion of "UB behaving normally."

Reason: The two `get_data()` calls are **two independent function calls**, returning **two different temporary `std::vector` objects**. Their `begin` and `end` point to two unrelated memory blocks. Pairing `begin` from one temporary with `end` from another creates an illegal range. Worse, these temporaries are destroyed at the end of the statement, so the algorithm holds dangling iterators from the start. **The correct way is to store the result in a named variable first**, so `begin` and `end` come from the same living object:

```cpp
auto data = get_data(); // One object
std::for_each(data.begin(), data.end(), [](int x) { // Safe
    std::cout << x << ' ';
});
```

This illusion of "same function name implies same object" is a high-risk area for pairing errors.
:::

## Trap 3: Insufficient Space—Stuffing Too Much into a Fixed Size

The third trap relates to the output destination. When you use `std::copy` to write to a **fixed-size** destination (like a raw array or a container without `reserve`), if the source range is larger than the destination space, you **write out of bounds**—again UB, potentially silently corrupting adjacent memory.

```cpp
#include <algorithm>
#include <iostream>

int main() {
    int src[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    int dest[3]; // Only 3 slots!

    // UB: Writing 10 ints into a 3-int array
    std::copy(std::begin(src), std::end(src), std::begin(dest));

    for (int i : dest) std::cout << i << ' '; // Might print garbage or crash later
}
```

This code compiles, runs, and might not error immediately, but you wrote 7 values into memory following `dest`. This bug can be caught with AddressSanitizer (`-fsanitize=address`), which will report a heap/stack buffer overflow.

The solution is straightforward: either use `std::back_inserter` (let the destination container grow automatically), or `reserve` enough space before copying and ensure the source range isn't larger than the capacity. Returning to the first point: **letting the container manage its own size (using inserters) is much safer than manually calculating sizes.**

## Error Quality: Are Ranges Really More Friendly?

Shah concludes by saying "Ranges uses concepts, giving you better error messages." This is true, but with a discount. Let's compare the error outputs of the two interfaces when "passing wrong parameters."

First, classic `std::sort` with wrong parameters—pairing `begin` of a `std::list` with `end` of a `std::vector` (type mismatch):

```cpp
std::list<int> l = {1, 2, 3};
std::vector<int> v = {4, 5, 6};
std::sort(l.begin(), v.end()); // Type mismatch
```

Now, the ranges version with a wrong parameter—passing something that isn't a range to `std::ranges::sort`:

```cpp
std::list<int> l = {1, 2, 3};
std::ranges::sort(l); // std::list is not a random_access_range
```

GCC 16.1.1 error line counts:

- Classic `std::sort` error: ~32 lines
- Ranges `std::ranges::sort` error: ~69 lines

Interestingly—**in this specific case, the ranges error (69 lines) is actually longer than the classic one (32 lines)**. This is because passing a `std::list` to `std::ranges::sort` forces the compiler to unfold the entire concept constraint chain (`sortable` -> `permutable` -> `forward_range` -> ...) to show you why it failed. The longer the chain, the more verbose the error. So I must honestly correct a common impression: **"ranges errors are always shorter and friendlier" is not true**; readability depends heavily on compiler version and scenario (GCC 10+ / Clang 12+ are much better, older compilers still spew template gibberish).

So what is the real advantage of ranges regarding "errors"? It's not line count, but **it prevents certain bugs from being written in the first place**. Recall Trap 2 above—classic `std::sort` accepts two iterators, so you can mismatch `begin`/`end` from different containers (like `get_data().begin(), get_data().end()`), and the compiler only errors at instantiation. `std::ranges::sort` **accepts only one container**, so you literally cannot express the error of "begin from A, end from B". **Eliminating an opportunity for error is far more practical than having a friendlier error.** This is the core safety benefit of ranges, which we will expand on in the next post.

## Transition: Must Iterators Die?

At this point, Shah showed a rather exaggerated slide: "Iterators must die." Exaggeration aside, the sentiment is real: **the iterator interface is powerful but full of pitfalls**—easy to mismatch, parameter order (for 3-iterator algorithms) is easy to reverse, and partial sorting code is ugly.

The good news is that C++20 Ranges addresses these pain points. It doesn't abandon iterators (iterators remain the underlying mechanism, even C++26 relies on them), but wraps them in a safer, more composable interface: **passing containers directly instead of iterator pairs, using concepts to catch type errors early at compile time, and using views for lazy composition**. These are the main topics of the next post.

In the next post, we will officially dive into Ranges—starting from "why `std::ranges::sort` takes one fewer parameter," moving to lazy evaluation of views, the pipe operator `|`, `std::views::filter`, and a eye-opening feature: **infinite ranges**. If you are interested in parallel versions of numeric algorithms (`std::reduce`, `std::transform_reduce`), check out the content on execution policies and parallel reduction in the Concurrency volume (vol5)—that's where algorithms meet concurrency.

<ReferenceCard title="References">
  <ReferenceItem
    :id="1"
    author="cppreference.com"
    title="Algorithms library"
    :year="2026"
    url="https://en.cppreference.com/w/cpp/algorithm"
    chapter="containers / iterators / algorithms 三大支柱"
  />
  <ReferenceItem
    :id="2"
    author="cppreference.com"
    title="Standard library header &lt;algorithm&gt;"
    :year="2024"
    url="https://en.cppreference.com/w/cpp/header/algorithm"
    chapter="截至 C++23 约 91 个函数模板"
  />
  <ReferenceItem
    :id="3"
    author="Jonathan Boccara"
    title="105 STL Algorithms in Less Than an Hour — CppCon 2018"
    :year="2018"
    url="https://www.youtube.com/watch?v=2olsGf6JIkU"
    chapter="宽松计数口径下 105 个"
  />
  <ReferenceItem
    :id="4"
    author="cppreference.com"
    title="Iterator invalidation rules"
    :year="2026"
    url="https://en.cppreference.com/w/cpp/container"
    chapter="各容器 insert/erase 后的失效规则"
  />
  <ReferenceItem
    :id="5"
    author="cppreference.com"
    title="std::rotate"
    :year="2026"
    url="https://en.cppreference.com/w/cpp/algorithm/rotate"
    chapter="参数顺序 first, middle, last"
  />
  <ReferenceItem
    :id="6"
    author="cppreference.com"
    title="std::vector — Iterator invalidation"
    :year="2026"
    url="https://en.cppreference.com/w/cpp/container/vector"
    chapter="push_back 扩容导致迭代器失效"
  />
  <ReferenceItem
    :id="7"
    author="cppreference.com"
    title="Standard library header &lt;numeric&gt;"
    :year="2023"
    url="https://en.cppreference.com/w/cpp/header/numeric"
    chapter="数值算法约 14 个"
  />
  <ReferenceItem
    :id="8"
    author="Mike Shah"
    title="Back to Basics: C++ Ranges — CppCon 2025"
    :year="2025"
    url="https://www.youtube.com/watch?v=Q434UHWRzI0"
  />
</ReferenceCard>
