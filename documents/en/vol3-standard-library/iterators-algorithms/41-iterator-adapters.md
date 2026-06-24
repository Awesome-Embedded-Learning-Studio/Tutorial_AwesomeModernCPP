---
chapter: 7
cpp_standard:
- 11
- 20
description: 'Deep dive into the three categories of STL iterator adapters—how `back_inserter`
  turns assignment into `push_back`, why `front_inserter` cannot be used with `vector`,
  why `reverse_iterator`''s `base()` is off by one, and the fundamental nature of
  adapters: "if it looks like an iterator, it fits into an algorithm.'
difficulty: intermediate
order: 41
platform: host
prerequisites:
- 迭代器基础与 category
- vector 深入：三指针、扩容与迭代器失效
reading_time_minutes: 12
related:
- 容器选择指南：按操作、内存与失效规则挑对容器
tags:
- host
- cpp-modern
- intermediate
- Ranges
title: 'Iterator Adapters: Reverse, Insert, and Stream — Repurposing Existing Iterators
  with New Behaviors'
translation:
  source: documents/vol3-standard-library/iterators-algorithms/41-iterator-adapters.md
  source_hash: acd4594db78684370784bc140e71a489393159c7be525419b5356ed850bf1887
  translated_at: '2026-06-24T00:44:25.182644+00:00'
  engine: anthropic
  token_count: 2133
---
# Iterator Adapters: Reverse, Insert, and Stream — Adapting Existing Iterators for New Behaviors

In the previous post, we reviewed iterators and their categories: iterators serve as a unified interface between containers and algorithms, categorized by their strength and capabilities. In this post, we will address a practical pain point you are bound to encounter.

Suppose we want to append elements from one `deque` to the end of another. The first instinct might be to use `std::copy`:

```cpp
std::deque<int> d1{1, 2, 3, 4, 5};
std::deque<int> d2;   // 空的
std::copy(d1.begin(), d1.end(), d2.end());   // 想追加到末尾？
```

This line is straight-up **undefined behavior**. `d2.end()` is a "past-the-end" position. `copy` will dutifully write elements to this out-of-bounds location—it only handles "assigning elements to the location pointed to by the destination iterator," completely disregarding whether the destination container actually has that space. Algorithms do not resize containers; this is the iron law of the STL.

So, what do we do? Should we write a manual loop with `for` and `push_back`? It works, but it isn't elegant—we are using algorithms, yet we are forced back to manual loops because "the destination won't grow." The standard library offers a smarter solution: **don't change the algorithm, change the iterator**. Give it an iterator that "automatically pushes into the container upon assignment," and `copy` remains `copy`, but the pain point is gone.

This is exactly what **iterator adapters** do: without creating new containers, they wrap existing iterators (or containers) to modify their behavior. The STL comes with three built-in types—reverse, insert, and stream. In this post, we will break down all three and explain the essence of "how adapters pull this off."

## Reverse Iterators: Turning `++` into `--`

This is the most intuitive category. `rbegin()` and `rend()` return a `reverse_iterator`, which completely inverts the `++` and `--` semantics of the underlying iterator: `++` moves backward, and `--` moves forward. Thus, a reverse traversal from beginning to end requires just one line of code:

```cpp
std::vector<int> v{1, 2, 3, 4, 5};
std::cout << "rbegin/rend 反向遍历: ";
for (auto it = v.rbegin(); it != v.rend(); ++it) std::cout << *it << ' ';
std::cout << '\n';
```

Here are the results from running `g++ -std=c++20 -O2` (local GCC 16.1.1):

```text
rbegin/rend 反向遍历: 5 4 3 2 1
```

The most practical use for reverse iterators is sorting. `std::sort` sorts in ascending order by default, but if we feed it reverse iterators, the sorted elements are written back in "reverse", effectively achieving a descending sort—no need for a custom comparator:

```cpp
std::vector<int> s{3, 1, 4, 1, 5, 9, 2, 6};
std::sort(s.rbegin(), s.rend());
// s 现在: 9 6 5 4 3 2 1 1
```

Let's plant a seed here: a `reverse_iterator` actually stores a "forward position" internally, but when it dereferences, it doesn't access that position—it accesses the **previous** one. This design directly dictates the `base()` off-by-one pitfall we will discuss later. Let's keep this in mind and verify it with actual tests shortly.

## Insert Iterators: Turning "Assignment" into "Insertion"

Let's return to the pain point of the `copy` out-of-bounds error at the beginning. If we swap the destination from `d2.end()` to `std::back_inserter(d2)`, the problem disappears:

```cpp
std::deque<int> d1{1, 2, 3, 4, 5};
std::deque<int> d3;   // 空的
std::copy(d1.begin(), d1.end(), std::back_inserter(d3));
// d3 现在: 1 2 3 4 5
```

`back_inserter` returns an "insert iterator" that translates the "assign to it" action into the container's `push_back`. It works with empty containers because each assignment grows the container by one element. If we `copy` again, it **appends** to the existing content rather than overwriting it:

```text
back_inserter 追加到空 d3: 1 2 3 4 5
再 back_inserter 一次: 1 2 3 4 5 1 2 3 4 5
```

There are three types of insert iterators, differing only in "where to insert":

- `back_inserter(c)` — calls `push_back`, inserting at the end;
- `front_inserter(c)` — calls `push_front`, inserting at the beginning;
- `inserter(c, it)` — calls `insert`, inserting **before** `it`.

`front_inserter` behaves counter-intuitively: because each new element is inserted at the very front, later elements appear before earlier ones, reversing the overall order:

```cpp
std::deque<int> d4;
std::copy(d1.begin(), d1.end(), std::front_inserter(d4));
// d4 现在: 5 4 3 2 1（d1 是 1 2 3 4 5，反过来了）
```

`inserter` inserts elements *before* the specified position. Note the emphasis on "before"—if `it` points to 20, the new element is placed in front of 20:

```cpp
std::deque<int> d5{10, 20, 30};
auto pos = d5.begin() + 1;   // 指向 20
std::copy(d1.begin(), d1.end(), std::inserter(d5, pos));
// d5 现在: 10 1 2 3 4 5 20 30
```

### Container Requirements for the Three Brothers

Here is a real pitfall. `back_inserter` calls `push_back`, and `front_inserter` calls `push_front`—but not every container has these members. `push_back` is nearly universal (available on `vector`, `deque`, and `list`), but `push_front` is only available on `deque` and `list`, not `vector`.

Therefore, applying `front_inserter` to a `vector` will fail to compile:

```cpp
std::vector<int> v;
int src[]{1, 2, 3};
std::copy(std::begin(src), std::end(src), std::front_inserter(v));
```

```text
/usr/include/c++/16.1.1/bits/stl_iterator.h:819:20:
  error: ‘class std::vector<int>’ has no member named ‘push_front’
```

The error is straightforward: `vector` simply doesn't have `push_front`. This aligns with the logic discussed in the previous post—since `vector` uses contiguous storage, inserting at the head requires moving all subsequent elements. This O(n) operation is too expensive, so the standard library simply doesn't provide this interface. If you need front insertion, switch to `deque` or `list`.

`inserter` doesn't have this limitation; it works with any container that has an `insert` method (basically all sequence containers). The trade-off is that the complexity of insertion in the middle is determined by the container (O(n) for `vector`, O(1) for `list`).

### Mini-Application: Order-Preserving Insertion

Combining insert iterators with algorithms allows for very clean code. A common requirement is "insert a new element into a sorted `vector` while keeping it sorted." The approach is to use `std::lower_bound` to find the first position that is "not less than the new value," and then use `inserter` (or directly call `insert`) to place it there:

```cpp
std::vector<int> sorted{1, 3, 5, 7, 9};
int new_val = 4;
auto it = std::lower_bound(sorted.begin(), sorted.end(), new_val);
sorted.insert(it, new_val);
// sorted 现在: 1 3 4 5 7 9
```

This is a classic technique for collaboration between `<algorithm>` and containers—compressing the O(n) "sequential search for position" into an O(log n) binary search (the O(n) move is unavoidable because of contiguous storage). We will cover a full algorithm overview in the next post, but for now, let's use this to feel how "algorithms + adapters + containers" mesh together.

## Stream Iterators: Treating Streams as Sequences

The third category involves wrapping I/O streams as iterators.

`ostream_iterator` translates "assigning to it" into "writing a value to the stream + a delimiter". This allows us to print container contents to `cout` with just one line of `copy`:

```cpp
std::cout << "ostream_iterator 打印: ";
std::copy(d1.begin(), d1.end(), std::ostream_iterator<int>(std::cout, ", "));
std::cout << '\n';
```

```text
ostream_iterator 打印: 1, 2, 3, 4, 5,
```

Note the extra delimiter at the end—the delimiter is appended **after every write**, so it follows the last element as well. To get a clean ending, we need to handle the tail manually, or use `std::format` or a range-based `for` loop instead.

The reverse `istream_iterator` treats an input stream as a "readable sequence." Its beauty lies in pairing it with a **default-constructed sentinel** to represent the end of the stream (EOF): we don't need to know the element count in advance; reading stops automatically when the EOF sentinel is reached. The following code reads a bunch of `int`s from a string stream into a `vector`:

```cpp
std::istringstream iss("10 20 30 40 50");
std::vector<int> from_stream{
    std::istream_iterator<int>(iss),
    std::istream_iterator<int>()};   // 默认构造 = EOF 哨兵
// from_stream: 10 20 30 40 50
```

::: warning Don't be misled by outdated resources
Some tutorials and notes mistakenly write the input stream iterator as `istream_adapter`—there is **no** such name in the standard library; the correct name is `istream_iterator`. This typo is common in reposted articles online, and copying it verbatim will result in compilation errors.
:::

This "iterator + sentinel" pattern is exactly what we discussed in the previous article regarding categories: `istream_iterator` is a typical **input_iterator**, which can only be read once in a single pass. The sentinel mechanism allows algorithms to handle sequences of "indeterminate length"—the length of a stream is only known when the end is reached, and this relies on the EOF sentinel.

## How Adapters Work: A Look Under the Hood

By now, you might be curious: why can the object returned by `back_inserter` be used as the destination for `std::copy`? `copy` doesn't know anything about "insert iterators."

The answer is an extension of the core point from the last article—**algorithms only recognize iterator interfaces, not concrete types**. The only requirement `copy` has for a destination iterator is that it "supports dereference assignment and `++`" (i.e., satisfies the semantics of an output_iterator). As long as an object supports these two operations, `copy` will treat it as an iterator. Whether the object is actually a real memory location or secretly calls `push_back` is of no concern to `copy`.

If we peel away the standard library's wrapper, the entire "magic" of `back_insert_iterator` boils down to this:

```cpp
// Standard: C++20
template <typename Container>
class BackInsertIterDemo {
    Container* c_;
public:
    explicit BackInsertIterDemo(Container& c) : c_{&c} {}
    // 赋值 = push_back：这就是"赋值即插入"的全部秘密
    BackInsertIterDemo& operator=(const typename Container::value_type& v) {
        c_->push_back(v);
        return *this;
    }
    BackInsertIterDemo& operator*() { return *this; }      // 解引用返回自己
    BackInsertIterDemo& operator++() { return *this; }     // ++ 是空操作
    BackInsertIterDemo operator++(int) { return *this; }
};
```

We overloaded `operator=` to act as `push_back`, while `*` and `++` are no-ops that simply return `*this`. This satisfies the trio of requirements for an `output_iterator`. Consequently, it can be used directly by any algorithm requiring an `output_iterator`, **without modifying a single line of the algorithm code**:

```cpp
std::vector<int> v;
int src[]{1, 2, 3, 4, 5};
std::copy(std::begin(src), std::end(src), BackInsertIterDemo(v));
// v 现在: 1 2 3 4 5
```

The output is exactly `1 2 3 4 5`. This captures the essence of an adapter: **an object that "looks like an iterator but delegates to a different behavior."** The STL's original design decision to "decouple containers and algorithms via iterators" truly shines here—not only can container iterators be used with algorithms, but even these "iterator impersonators" work as well.

Following this logic, the standard library also provides `move_iterator` (introduced in C++11, improved in C++20 with ranges): it transforms "dereferencing to an lvalue reference" into "dereferencing to an rvalue reference". When wrapped around a source range, `copy` effectively becomes `move`—elements are moved rather than copied. The underlying mechanism is exactly the same as above: wrap a layer and swap the dereference behavior. We will cover this in detail in the chapter on move semantics; for now, just know that it belongs to the same family.

## Common Pitfalls

Let's consolidate the common places where things can go wrong; each of these has been verified through testing:

::: warning reverse_iterator's base() is Off-by-One
`reverse_iterator` has a `base()` member that returns the underlying forward iterator it wraps. However, `*rit` accesses **not** `rit.base()`, but `rit.base() - 1`:

```text
*rit            = 40
*rit.base()     = 50
*(rit.base()-1) = 40
```

This brings us back to the foreshadowing at the beginning. The consequence is this: when you want to `erase` a range defined by reverse iterators using a forward iterator, the endpoint must be written as `(rit+1).base()` instead of `rit.base()`, otherwise you will be off by one. If you can't remember this, just keep in mind that "dereferencing a reverse iterator accesses the element *before* its `base`," and you won't go wrong.
:::

::: warning front_inserter is picky about containers
`front_inserter` can only be used on containers that have `push_front`, namely `deque` and `list`. `vector`, `array`, and `string` do not have `push_front`, so using it on them will result in a compilation failure (see the real error message above). If you need to insert at the front, switch containers.
:::

::: warning inserter inserts "before"
`inserter(c, it)` inserts elements **before** `it`, it does not replace the element pointed to by `it`. Furthermore, during consecutive insertions with `inserter`, the insertion point moves forward (because stuff was inserted in front), so its behavior differs from `back_inserter`'s "append" mode. Keep this in mind when using it.
:::

::: warning ostream_iterator trailing delimiter
The delimiter is appended after every write, so the output will have an extra one at the end. If you want clean comma separation, don't use this; use `std::format` or handle the boundaries manually in a loop.
:::

## Summary

The core idea behind iterator adapters can be summed up in one sentence—**don't change the algorithm, don't create new containers, just swap in an iterator that "shapeshifts."** Let's wrap up with a few key takeaways:

- Three categories of ready-made adapters: `reverse_iterator` (`rbegin`/`rend`, for reverse traversal or descending order with `sort`), insert iterators (`back_inserter`/`front_inserter`/`inserter`, turning assignment into insertion), and stream iterators (`ostream_iterator`/`istream_iterator`, converting between streams and sequences).
- Insert iterators have specific requirements: `back_inserter` requires `push_back` (almost everyone has it), `front_inserter` requires `push_front` (only `deque`/`list`), and `inserter` requires `insert` (all sequence containers have it).
- The essence of an adapter is "looks like an iterator, different behavior under the hood"—as long as it satisfies the semantics of an `output_iterator` (dereference assignment + `++`), it can be plugged into any algorithm without changing a single line of algorithm code.
- Four common pitfalls: `reverse_iterator::base()` is off-by-one, `front_inserter` doesn't work with `vector`, `inserter` inserts *before* the position, and `ostream_iterator` adds a trailing delimiter.

In the next section, we will officially dive into algorithms—we'll organize the massive `<algorithm>` library into categories like "Non-modifying / Modifying / Sorting / Finding," and see how to pick the right tool for a specific problem.

## References

- [cppreference: Iterator adaptors](https://en.cppreference.com/w/cpp/iterator#Iterator_adaptors) — Overview of the three adapter categories
- [cppreference: std::back_insert_iterator](https://en.cppreference.com/w/cpp/iterator/back_insert_iterator) — Return type of `back_inserter` and the "assignment is push_back" mechanism
- [cppreference: std::reverse_iterator](https://en.cppreference.com/w/cpp/iterator/reverse_iterator) — The off-by-one relationship between `base()` and dereferencing
- [cppreference: std::istream_iterator](https://en.cppreference.com/w/cpp/iterator/istream_iterator) — Stream iterators and the EOF sentinel
