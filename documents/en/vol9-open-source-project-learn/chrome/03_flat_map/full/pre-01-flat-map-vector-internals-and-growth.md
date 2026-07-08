---
chapter: 0
cpp_standard:
- 11
- 17
description: "flat_map stores its data in a vector by default: this piece walks through vector's three-pointer layout, the cache win of contiguous storage, growth and amortized analysis, and the iterator-invalidation rules that flat_map inherits"
difficulty: intermediate
order: 1
platform: host
prerequisites:
- flat_map prerequisite (0): ordered associative containers and std::map's red-black tree
reading_time_minutes: 9
related:
- flat_map prerequisite (2): complexity and amortized analysis
- flat_map in practice (2): the flat_tree core skeleton
tags:
- host
- cpp-modern
- intermediate
- 容器
- vector
- 优化
title: "flat_map prerequisite (1): std::vector internals and growth"
---
# flat_map prerequisite (1): std::vector internals and growth

In [pre-00](./pre-00-flat-map-ordered-assoc-container-intro.md) we sketched flat_map's shape as "a sorted array plus binary search." What is that "array," really? Peek at Chromium's template signature, `flat_map<Key, Mapped, Compare, Container>`, and the default `Container` is `std::vector<std::pair<Key, Mapped>>` (flat_map.h:193). Put bluntly, flat_map sits on top of a vector. How vector stores data, when it grows, when it invalidates iterators, flat_map follows along. No escape hatch.

So this piece tears `std::vector`'s internal representation apart. If three pointers, growth, and invalidation are old hat to you, jump straight to "Back to flat_map." If they're not, work through it. Every complexity claim in the later flat_tree chapters has its roots right here.

## Three pointers: vector's internal layout

Across libstdc++, libc++, and MSVC, `std::vector<T>` looks nearly identical: three pointers and one contiguous block of memory. The pointers live in a header struct whose details differ slightly between implementations, but the concept is uniform.

```text
        begin        end          end_of_storage
          |            |                |
memory:  [ | | | | | | | | | | | | | | | ]
          <-- size -->  <--  free  -->
          <---------- capacity ---------->
```

`begin` points at the first element. `end` points one past the last element, the past-the-end slot, so `end - begin` equals `size()`. `end_of_storage` points at the end of the allocated memory, so `end_of_storage - begin` equals `capacity()`, the most this block can hold without growing.

Two quantities you should keep straight: `size` is how many elements are actually in there right now, `capacity` is how many the block could hold. The stretch between `end` and `end_of_storage` is allocated but unused free space. `push_back` constructs a new element in place on that free ground, no extra allocation needed.

### Contiguous storage: the root of cache friendliness

This memory is contiguous, elements packed one after another with no gaps (true for trivially copyable types; alignment padding exists but the layout is still contiguous). This is the root of vector's cache friendliness. The CPU pulls data from memory in cache lines, 64 bytes at a time. Touch `data[0]` and `data[1]`, `data[2]` ride along into L1 for free. Touch them next and you hit L1 in one cycle. [pre-00](./pre-00-flat-map-ordered-assoc-container-intro.md) claimed flat_map's constant factor beats `std::map` by an order of magnitude. That contiguous block is the reason.

---

## Growth: what happens when capacity runs out

When `push_back` finds `size < capacity`, it constructs the new element right at the `end` slot, bumps `end` forward by one, and goes home in `O(1)`. Happy path.

When `size == capacity`, the block is full and growth kicks in. The full dance: allocate a larger block (mainstream implementations double up, so new `capacity = old capacity * 2`); move every old element over, one by one, with a subtlety, move versus copy depends on whether the element's move is `noexcept`, safe to move when it is, fall back to copy when it isn't to keep the strong exception guarantee; then destruct the old elements, free the old memory, and repoint all three pointers at the new block.

All of that is `O(n)`. You move n elements. So a single `push_back` can hit `O(n)` in the worst case. No way around it.

### Amortized O(1): why push_back is still "fast"

Worst case `O(n)` sounds scary, but `push_back` is amortized O(1) in engineering terms (amortized constant time). The intuition is simple. The one growth step is `O(n)`, true, but afterward capacity has doubled, so the next n `push_back`s all land on the happy path and none of them grow. Spread that single `O(n)` across those n calls and each one averages `O(1)`.

This is the classic geometric-growth capacity analysis. Doubling pins push_back's amortized complexity at `O(1)`. Day to day, push_back on a vector is fast. You don't need to fear the `O(n)` worst case.

### Back to flat_map: no amortization for insert

The single-element insert on flat_map never gets to cash in on that amortization.

The problem is the insertion point. flat_map must stay sorted, so `insert(key)` runs `lower_bound` to find the slot, then inserts right there. That slot is usually somewhere in the middle of the array. Insert in the middle and every element after it shifts one slot back, a real `O(n)` shift, and this happens on every insert. Unlike push_back, which pays the cost only on the rare growth step and is `O(1)` the rest of the time. flat_map's single insert is `O(n)`, full stop, with no amortization to speak of. File this away: it's the origin of the "read-heavy, write-light" judgment later on. Write a lot to a flat_map and it gets slow, and this is the source.

---

## Iterator, pointer, and reference invalidation

vector's invalidation rules are a C++ interview chestnut, but for flat_map they genuinely carry weight. Let's walk through them precisely.

`push_back` is the interesting one. When it doesn't trigger growth, the elements never move, so every iterator, pointer, and reference stays valid. The moment growth triggers and new memory is allocated, the old block is freed wholesale and every iterator pointing at an old address goes dangling, all invalid. In practice, the conservative move is to treat iterators as invalidated after `push_back` and not bet on whether it grew. `reserve(n)` works the same way: if `n` exceeds the current capacity, growth fires and everything invalidates; otherwise nothing changes. `insert` and `erase` invalidate from the operation point through `end` (elements got shifted), and insert can also trigger growth and invalidate everything on top of that. `clear` invalidates all iterators but usually keeps capacity; to give the memory back, call `shrink_to_fit`.

### flat_map deliberately coarsens the rules

In Chromium's flat_tree source, the invalidation rules are stated conservatively on purpose. Every mutation (insert / erase / reserve / shrink_to_fit / swap / move ctor / move assign) gets the same line: "Assume that every operation invalidates iterators and references." The comments in flat_tree.h read exactly that way (lines 151/217/231/273/306/319/374).

Why not follow vector's fine-grained rules quietly, where reserve only invalidates on a real realloc and insert only from the insertion point on? We turned this over for a while, and the answer is pragmatic. The fine-grained rules are hard on callers. While writing code you'd have to keep asking, "will this insert grow? is this reserve big enough?" The mental load piles up and people misremember. flat_tree just draws a hard line: mutate, treat everything as dead. A coarse rule is less "accurate" than a fine one, but nobody misremembers it, and that's what actually matters in engineering. We'll come back to this in 03-5, which covers iterator invalidation in detail.

The source even ships a direct UB example (flat_map.h:57-60):

```cpp
container["new element"] = it.second;   // UB: operator[] may trigger growth, it invalidates
```

This "iterate and mutate at the same time" pattern is flat-out undefined behavior in flat_map, far stricter than `std::map`, where nodes are stable and iterators survive mutation.

## reserve and shrink_to_fit

vector exposes two capacity-management interfaces, and flat_tree passes them through unchanged.

`reserve(n)` preallocates enough memory for n elements. If you know roughly how many you'll end up with, reserve up front and skip the relocation cost of every later growth step. This matters a lot when bulk-constructing a flat_map (03-5 covers the bulk-construction patterns in full). `shrink_to_fit()` goes the other way, shrinking capacity down to size and handing the excess memory back. It's a non-binding request; the standard lets an implementation ignore it, but mainstream implementations usually cooperate and do a realloc.

Both invalidate iterators, since either can trigger a realloc.

---

Armed with this, the next piece builds out the complexity toolkit: `O(lg n)` lookup against `O(n)` insert, amortized against single-shot, so the flat_tree complexity conclusions later have a foundation to stand on.

## References

- [cppreference: std::vector](https://en.cppreference.com/w/cpp/container/vector)
- [cppreference: vector's iterator invalidation rules](https://en.cppreference.com/w/cpp/container/vector#Iterator_invalidation)
- [Chromium `base/containers/flat_tree.h`: iterator invalidation comments](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/flat_tree.h)
- [Bjarne Stroustrup: vector and cache performance experiments](https://www.stroustrup.com/Software-for-infrastructure.pdf)
