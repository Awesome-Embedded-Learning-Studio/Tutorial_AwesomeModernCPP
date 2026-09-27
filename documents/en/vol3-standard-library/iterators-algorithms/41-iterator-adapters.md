---
chapter: 7
cpp_standard:
- 11
- 20
description: 'A thorough walkthrough of the three kinds of STL iterator adapters — how
  `back_inserter` turns assignment into `push_back`, why `front_inserter` cannot be
  used with `vector`, why `reverse_iterator`''s `base()` is off by one, and the essence
  of adapters: "if it looks like an iterator, it fits into an algorithm"'
difficulty: intermediate
order: 41
platform: host
prerequisites:
- 'Iterator Basics and Categories: The Glue Between Containers and Algorithms'
- 'Deep Dive into vector: Three Pointers, Reallocation, and Iterator Invalidation'
reading_time_minutes: 12
related:
- 'Container Selection Guide: Picking the Right One by Operations, Memory, and Invalidation Rules'
tags:
- host
- cpp-modern
- intermediate
- Ranges
title: 'Iterator Adapters: Reverse, Insertion, and Stream — Teaching Old Iterators
  New Tricks'
translation:
  source: documents/vol3-standard-library/iterators-algorithms/41-iterator-adapters.md
  source_hash: acd4594db78684370784bc140e71a489393159c7be525419b5356ed850bf1887
  translated_at: '2026-09-26T01:33:35+00:00'
  engine: anthropic
  token_count: 2500
---
# Iterator Adapters: Reverse, Insertion, and Stream — Teaching Old Iterators New Tricks

In the previous article we walked through iterators and their categories: iterators are the unified interface layer between containers and algorithms, graded by how strong they are. This article picks up from there to solve a practical pain point you are guaranteed to run into.

Suppose you want to append the elements of one `deque` to the end of another. Your first instinct is probably `std::copy`:

```cpp
std::deque<int> d1{1, 2, 3, 4, 5};
std::deque<int> d2;   // empty
std::copy(d1.begin(), d1.end(), d2.end());   // want to append to the end?
```

That line is flat-out **undefined behavior**. `d2.end()` is a "past-the-end" position, and `copy` will dutifully write elements to that out-of-bounds spot — its job is strictly "assign the element to the position the destination iterator points to," and whether the destination container has room for that is none of its business. Algorithms don't grow containers; that is the iron law of the STL.

So what now — hand-write a `for` loop doing `push_back`? It works, but it isn't elegant: we were using algorithms, yet just because "the destination won't grow" we get shoved back into hand-written loops. The standard library has a smarter answer: **don't swap the algorithm, swap the iterator**. Give it an iterator that "pushes into the container the moment it receives an assignment," and `copy` stays the same old `copy` — the pain point is gone.

That is precisely what **iterator adapters** do: no new containers get built — they wrap an existing iterator (or container) in a layer and reshape it into new behavior. The STL ships three ready-made kinds: reverse, insertion, and stream. In this article we will take all three apart and run them, and along the way pin down the essence of "what lets adapters get away with this."

## Reverse Iterators: Turning `++` into `--`

The most intuitive kind. `rbegin()` / `rend()` return a `reverse_iterator`, which flips the underlying iterator's `++` / `--` semantics completely: `++` steps toward the front, `--` moves toward the back. So a full end-to-start traversal falls out of a one-line loop:

```cpp
std::vector<int> v{1, 2, 3, 4, 5};
std::cout << "rbegin/rend 反向遍历: ";
for (auto it = v.rbegin(); it != v.rend(); ++it) std::cout << *it << ' ';
std::cout << '\n';
```

Running it with `g++ -std=c++20 -O2` (local GCC 16.1.1) gives:

```text
rbegin/rend 反向遍历: 5 4 3 2 1
```

The most practical pairing for reverse iterators is sorting. `std::sort` defaults to ascending order, but feed it reverse iterators and the ascending-sorted elements get "written back in reverse" — the net effect is descending order, with no custom comparator required:

```cpp
std::vector<int> s{3, 1, 4, 1, 5, 9, 2, 6};
std::sort(s.rbegin(), s.rend());
// s is now: 9 6 5 4 3 2 1 1
```

Here is a piece of foreshadowing to plant: a `reverse_iterator` internally stores a "forward position," but when you dereference it, what gets accessed is not that position — it is the **previous** one. That design is exactly what produces the `base()` off-by-one trap we will cover later. Note it for now; we will verify it with a real run shortly.

## Insertion Iterators: Turning "Assignment" into "Insertion"

Back to the out-of-bounds `copy` pain from the opening. Swap the destination from `d2.end()` to `std::back_inserter(d2)` and the problem vanishes:

```cpp
std::deque<int> d1{1, 2, 3, 4, 5};
std::deque<int> d3;   // empty
std::copy(d1.begin(), d1.end(), std::back_inserter(d3));
// d3 is now: 1 2 3 4 5
```

What `back_inserter` returns is an "insertion iterator": it translates the act of "assigning to it" into the container's `push_back`. An empty container can receive it too, because every assignment grows the container by one slot. `copy` through it again and you are **appending** on top of what is already there, not overwriting:

```text
back_inserter 追加到空 d3: 1 2 3 4 5
再 back_inserter 一次: 1 2 3 4 5 1 2 3 4 5
```

Insertion iterators come as three siblings, differing only in "where the element gets stuffed":

- `back_inserter(c)` — calls `push_back`, stuffing at the end;
- `front_inserter(c)` — calls `push_front`, stuffing at the beginning;
- `inserter(c, it)` — calls `insert`, stuffing in **before** `it`.

`front_inserter` has a counterintuitive side: because every new element is inserted at the very front, the ones inserted later end up ordered earlier, so the whole sequence comes out reversed:

```cpp
std::deque<int> d4;
std::copy(d1.begin(), d1.end(), std::front_inserter(d4));
// d4 is now: 5 4 3 2 1 (d1 is 1 2 3 4 5 — reversed)
```

`inserter`, meanwhile, inserts before the position you hand it. Note "before": if `it` points at 20, the new element lines up in front of that 20:

```cpp
std::deque<int> d5{10, 20, 30};
auto pos = d5.begin() + 1;   // points at 20
std::copy(d1.begin(), d1.end(), std::inserter(d5, pos));
// d5 is now: 10 1 2 3 4 5 20 30
```

### Each Sibling's Container Requirements

Here is a real trap. `back_inserter` calls `push_back` and `front_inserter` calls `push_front` — but not every container has those members. `push_back` is nearly universal (`vector`, `deque`, and `list` all qualify), while `push_front` exists only on `deque` and `list`; `vector` doesn't get one.

So wrap `front_inserter` around a `vector` and it won't even compile:

```cpp
std::vector<int> v;
int src[]{1, 2, 3};
std::copy(std::begin(src), std::end(src), std::front_inserter(v));
```

```text
/usr/include/c++/16.1.1/bits/stl_iterator.h:819:20:
  error: ‘class std::vector<int>’ has no member named ‘push_front’
```

The error says it plainly: `vector` has no `push_front`, period. This actually follows from what the previous article explained — `vector` is contiguous storage, inserting at the head means shuffling every element behind it, an O(n) operation too expensive to justify, so the standard library simply withholds the interface. If you want head insertion, switch to `deque` or `list`.

`inserter` has no such restriction: any container with `insert` will do (which is essentially all sequence containers), with the trade-off that the cost of a middle insertion is set by the container (`vector` is O(n), `list` is O(1)).

### A Small Application: Order-Preserving Insertion

Insertion iterators plus algorithms make for very clean code. A common requirement: "insert a new element into a sorted `vector` so it stays sorted afterward." The idea is to use `std::lower_bound` to find the first position "not less than the new value," then insert there with `inserter` (or plain `insert`):

```cpp
std::vector<int> sorted{1, 3, 5, 7, 9};
int new_val = 4;
auto it = std::lower_bound(sorted.begin(), sorted.end(), new_val);
sorted.insert(it, new_val);
// sorted is now: 1 3 4 5 7 9
```

This is a classic little technique of `<algorithm>` and containers working together — it compresses the O(n) "walk and compare to find the position" into an O(log n) binary search (the O(n) element-shoveling you can't dodge, because the storage is contiguous). We will unfold the full algorithm survey in the next article; for now, borrow it to get a feel for how "algorithm + adapter + container" mesh.

## Stream Iterators: Walking a Stream as a Sequence

The third kind wraps I/O streams into iterators as well.

`ostream_iterator` translates "assigning to it" into "write one value to the stream, plus a delimiter." So printing a container's contents to `cout` is one line of `copy`:

```cpp
std::cout << "ostream_iterator 打印: ";
std::copy(d1.begin(), d1.end(), std::ostream_iterator<int>(std::cout, ", "));
std::cout << '\n';
```

```text
ostream_iterator 打印: 1, 2, 3, 4, 5,
```

Notice the extra delimiter at the end — the delimiter is appended **after each write**, so the last element gets one trailing it too. For a clean ending you have to deal with the tail yourself, or use `std::format` / a range `for` loop instead.

In the other direction, `istream_iterator` treats an input stream as a "readable sequence." Its neat trick is pairing with a **default-constructed sentinel** that stands for end-of-stream (EOF): you don't need to know up front how many elements the stream holds — when reading hits EOF, the sentinel terminates things automatically. Below, we read a bunch of `int`s out of a string stream into a `vector`:

```cpp
std::istringstream iss("10 20 30 40 50");
std::vector<int> from_stream{
    std::istream_iterator<int>(iss),
    std::istream_iterator<int>()};   // default-constructed = EOF sentinel
// from_stream: 10 20 30 40 50
```

::: warning Don't get led astray by outdated material
Some tutorials and notes write the input stream iterator as `istream_adapter` — **no** such name exists in the standard library; the correct one is `istream_iterator`. This particular typo is common in copy-pasted articles online, and copying it along verbatim will fail to compile.
:::

This "iterator + sentinel" pattern is exactly what the previous article touched on when discussing categories: `istream_iterator` is the textbook **input_iterator**, single-pass and forward-only. The sentinel mechanism is what lets algorithms handle sequences "whose length isn't known in advance" — a stream's length is known only once you read to the end, and that is precisely what the EOF sentinel is for.

## How Adapters Get Away With It: Peeling Back the Layer

By now you might be wondering: what entitles the object `back_inserter` returns to be stuffed into `std::copy` as the destination? `copy` doesn't know anything about "insertion iterators."

The answer extends the core claim from the previous article — **algorithms recognize only the iterator interface, not concrete types**. Everything `copy` demands of a destination iterator is "you can dereference-assign, and you can `++`" (that is, it satisfies output_iterator semantics). Any object supporting those two operations is an iterator as far as `copy` is concerned; whether that object is backed by a real memory location or is secretly calling `push_back`, `copy` could not care less.

Peel the standard library's wrapper off, and the whole of `back_insert_iterator`'s "magic" amounts to this:

```cpp
// Standard: C++20
template <typename Container>
class BackInsertIterDemo {
    Container* c_;
public:
    explicit BackInsertIterDemo(Container& c) : c_{&c} {}
    // Assignment = push_back: that is the entire secret of "to assign is to insert"
    BackInsertIterDemo& operator=(const typename Container::value_type& v) {
        c_->push_back(v);
        return *this;
    }
    BackInsertIterDemo& operator*() { return *this; }      // dereference returns itself
    BackInsertIterDemo& operator++() { return *this; }     // ++ is a no-op
    BackInsertIterDemo operator++(int) { return *this; }
};
```

`operator=` is overloaded into `push_back`, while `*` and `++` are no-ops that return themselves — together they assemble the three-piece kit an output_iterator demands. So any algorithm that wants an output_iterator can use it directly, **without the algorithm changing a single word**:

```cpp
std::vector<int> v;
int src[]{1, 2, 3, 4, 5};
std::copy(std::begin(src), std::end(src), BackInsertIterDemo(v));
// v is now: 1 2 3 4 5
```

Run it and out comes exactly `1 2 3 4 5`. That is the essence of an adapter: **an object that "looks like an iterator while hanging a different behavior behind it."** This is where the STL's original design decision — decoupling containers and algorithms through iterators — really starts to show its power: not only do containers' own iterators slot into algorithms, so do little objects "masquerading as iterators."

Following the same thread, the standard library also has `move_iterator` (introduced in C++11, reworked with ranges in C++20): it turns "dereference yields an lvalue reference" into "dereference yields an rvalue reference," so wrapped over a source range, `copy` becomes `move` — elements get carried off instead of copied. The mechanism underneath is identical to what we just saw: wrap a layer, swap in a different dereference behavior. We will give it a dedicated treatment in the move-semantics volume; for now, just know this relative exists.

## A Few Pitfalls You Will Actually Hit

Let's gather up the crash sites along this route — each one verified by a real run above:

::: warning A reverse iterator's base() is off by one
`reverse_iterator` has a `base()` member that returns the forward iterator it wraps. But `*rit` accesses **not** `rit.base()` — it accesses `rit.base() - 1`:

```text
*rit            = 40
*rit.base()     = 50
*(rit.base()-1) = 40
```

That is the foreshadowing we planted at the start. The consequence: when you want to erase a range bounded by reverse iterators using forward iterators, the endpoint has to be written `(rit+1).base()` rather than `rit.base()`, or you are off by one slot. No worries if you can't memorize that — remember "a reverse dereference accesses the position one before base" and you can't go wrong.
:::

::: warning front_inserter is picky about its container
`front_inserter` only works on containers that have `push_front` — that means `deque` and `list`. `vector`, `array`, and `string` all lack `push_front`; wrapping one fails to compile on the spot (see the real error above). If you want head insertion, change containers.
:::

::: warning inserter inserts "before"
`inserter(c, it)` inserts the element **before** `it`; it does not replace the element `it` points to. And under consecutive `inserter` insertions the insertion point drifts along afterward (things were inserted in front of it), so the behavior differs from `back_inserter`'s "append" — keep that straight when you use it.
:::

::: warning ostream_iterator leaves an extra delimiter at the end
The delimiter is appended after each write, so the output ends with one extra. For cleanly comma-separated output, skip it — use `std::format` or handle the boundary by hand in a loop.
:::

## Summary

The idea behind iterator adapters really comes down to one sentence — **don't change the algorithm, don't build a new container; swap in an iterator that can "shape-shift."** The key takeaways:

- Three ready-made kinds: `reverse_iterator` (`rbegin`/`rend` — reverse traversal, or descending order when paired with `sort`), insertion iterators (`back_inserter`/`front_inserter`/`inserter` — assignment becomes insertion), and stream iterators (`ostream_iterator`/`istream_iterator` — converting between streams and sequences).
- Each insertion iterator wants something different: `back_inserter` needs `push_back` (nearly universal), `front_inserter` needs `push_front` (only `deque`/`list`), `inserter` needs `insert` (every sequence container has it).
- An adapter's essence is "looks like an iterator, hangs a different behavior behind it" — satisfy output_iterator semantics (dereference-assign + `++`) and it plugs into any algorithm, which needs not a single character changed.
- Four high-frequency traps: `reverse_iterator::base()` off by one, `front_inserter` refusing `vector`, `inserter` inserting before the position, and `ostream_iterator`'s trailing delimiter.

In the next article we formally move into algorithms — we will organize the whole `<algorithm>` family by "non-modifying / modifying / sorting / searching" and look at how to pick the right tool when a concrete problem stares back.

## References

- [cppreference: Iterator adaptors](https://en.cppreference.com/w/cpp/iterator#Iterator_adaptors) — an overview of the three adapter kinds
- [cppreference: std::back_insert_iterator](https://en.cppreference.com/w/cpp/iterator/back_insert_iterator) — `back_inserter`'s return type and its "assignment is push_back" mechanism
- [cppreference: std::reverse_iterator](https://en.cppreference.com/w/cpp/iterator/reverse_iterator) — the off-by-one relationship between `base()` and dereferencing
- [cppreference: std::istream_iterator](https://en.cppreference.com/w/cpp/iterator/istream_iterator) — stream iterators and the EOF sentinel
