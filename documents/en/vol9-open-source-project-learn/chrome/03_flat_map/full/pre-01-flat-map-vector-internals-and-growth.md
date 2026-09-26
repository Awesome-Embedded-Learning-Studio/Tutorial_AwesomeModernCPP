---
chapter: 0
cpp_standard:
- 11
- 17
description: "flat_map stores its data in a vector by default; this piece works through vector's three-pointer representation, the cache advantage of contiguous storage, growth and amortized analysis, and the iterator invalidation rules, laying the groundwork for flat_map's behavior"
difficulty: intermediate
order: 1
platform: host
prerequisites:
- 'flat_map prerequisite (0): ordered associative containers and std::map''s red-black tree'
reading_time_minutes: 9
related:
- 'flat_map prerequisite (II): complexity and amortized analysis'
- 'flat_map hands-on (II): the flat_tree core skeleton'
tags:
- host
- cpp-modern
- intermediate
- 容器
- vector
- 优化
title: "flat_map prerequisite (I): std::vector internals and growth"
translation:
  source: documents/vol9-open-source-project-learn/chrome/03_flat_map/full/pre-01-flat-map-vector-internals-and-growth.md
  source_hash: 794d402e09ad97688afdf635d79f3a396bfd523a567ec00a5aa636e2a6189744
  translated_at: '2026-09-26T02:46:38+00:00'
  engine: anthropic
  token_count: 4000
---
# flat_map prerequisite (I): std::vector internals and growth

In [pre-00](./pre-00-flat-map-ordered-assoc-container-intro.md) we sketched flat_map's shape as "a sorted array plus binary search." So who exactly is that "array"? Take one look at Chromium's template signature, `flat_map<Key, Mapped, Compare, Container>`: the default `Container` is `std::vector<std::pair<Key, Mapped>>` (flat_map.h:193). Put plainly, vector is the base flat_map sits on. How vector stores its data, when it grows, when it invalidates iterators — flat_map does the same, and there is no opting out.

So this piece takes `std::vector`'s internal representation apart, all the way down. If three pointers, growth, and iterator invalidation are already old hat to you, jump straight to the later "Back to flat_map" section. If they are not, we suggest working through this until it clicks — every complexity conclusion in the later flat_tree chapters has its roots buried right here.

## Three pointers: vector's internal representation

In libstdc++, libc++, and MSVC alike, `std::vector<T>` looks almost identical: three pointers plus one contiguous block of memory. The three pointers live in a small header struct; the details differ slightly from implementation to implementation, but the concept is uniform.

```text
        begin        end          end_of_storage
          ↓            ↓                ↓
memory:  [ | | | | | | | | | | | | | | | ]
          ←── size ──→←── free ──→
          ←────────── capacity ────────→
```

`begin` points at the first element; `end` points at the slot one past the last element — the past-the-end position — so `end - begin` is exactly `size()`; `end_of_storage` points at the end of the allocated memory, and `end_of_storage - begin` is `capacity()`, the most this block can hold without growing.

A reminder to keep the two quantities apart: `size` is how many elements are genuinely stored right now; `capacity` is how many this block can hold at most. The stretch in between (`end` up to `end_of_storage`) is free ground — already allocated, not yet put to use. `push_back` constructs a new element in place right on that ground, with no need to go ask for more memory.

### Contiguous storage: the root of cache friendliness

This block of memory is contiguous: elements sit one after another with no gaps in between (that is how it works for trivially copyable types; alignment padding exists, but the layout is still contiguous). This is the root of vector's cache friendliness. The CPU fetches data from memory by the cache line, 64 bytes per fetch. When you touch `data[0]`, the neighboring `data[1]` and `data[2]` ride along into L1 for free; touch them next and it is a direct hit in 1 cycle. [pre-00](./pre-00-flat-map-ordered-assoc-container-intro.md) said flat_map's constant factor is an order of magnitude smaller than `std::map`'s — this contiguous block of memory is where that comes from.

---

## Growth: what to do when capacity runs out

When `push_back` finds `size < capacity`, it constructs the new element right in the `end` slot, slides `end` forward by one, and wraps up in `O(1)`. That is the happy path.

But what if `size == capacity` and the block is full? Then it has to grow. The whole sequence goes like this: first request a larger block of new memory — mainstream implementations go with 2x, so the new `capacity = old capacity * 2`; then relocate the elements from the old memory one by one. Here is a detail: whether the relocation uses move or copy depends on whether the element's move is noexcept — if it is, move with a clear conscience; if not, fall back to copy to preserve the strong exception guarantee. Once the move is done, destruct the old elements, free the old memory, and dial the three pointers over onto the new memory.

This whole procedure is `O(n)` — n elements have to be relocated. So a single `push_back` is `O(n)` in the worst case. No way around it.

### Amortized O(1): why push_back is still fast

A worst case of `O(n)` per call sounds scary, but in engineering terms `push_back` is amortized `O(1)` (amortized constant time). The intuition is simple: true, that one growth step costs `O(n)`, but once it is done the capacity has doubled, and the next n `push_back`s all land on the happy path without growing even once. Spread that single `O(n)` across those n calls, and each averages out to `O(1)`.

This is the classic geometric-growth capacity analysis — doubling pins push_back's amortized complexity at `O(1)`. So day to day, `push_back` on a vector is fast; you do not need to fear that `O(n)` worst case.

### Back to flat_map: insertion gets no amortization

But flat_map's single-element insert does not get a single bite of that amortized benefit.

The problem is the insertion point. flat_map must keep its data ordered, so `insert(key)` first runs `lower_bound` to find the position and then inserts right there — and that position is more often than not somewhere in the middle of the array. Insert one in the middle, and every element after it has to shift back one slot: a solid, real `O(n)` shift, and **every single insert pays it** — unlike push_back, which pays the price only on that one growth step and is `O(1)` all the rest of the time. So a single insert on flat_map is `O(n)`, full stop; there is no amortization to speak of. Keep this fact in mind: it is where the later "read-heavy, write-light" criterion comes from. Write to a flat_map a lot and it slows down — this is the source.

---

## Iterator, pointer, and reference invalidation

vector's invalidation rules are a C++ interview chestnut, but for flat_map they genuinely carry load. Let's go through them precisely.

`push_back` is the most interesting one: when no growth is triggered, the elements never moved an inch, so all iterators, pointers, and references remain valid across the board; the moment growth is triggered and new memory is allocated, the old block is released as a whole and every iterator pointing at an old address goes dangling in an instant — all invalid. So, to stay conservative in engineering practice, treat iterators as invalidated after `push_back`; do not gamble on it not having grown. `reserve(n)` works the same way: if `n` exceeds the current capacity, growth fires and everything invalidates; otherwise nothing changes. `insert` and `erase` invalidate everything from the operation point all the way to `end` (elements were shifted), and insert stacks one more on top: a possible growth, invalidating everything. `clear` invalidates all iterators but usually keeps the capacity; to hand the memory back you have to call `shrink_to_fit`.

### flat_map deliberately states the rules coarsely

In Chromium's flat_tree source, the invalidation rules are stated conservatively on purpose — every mutation (insert / erase / reserve / shrink_to_fit / swap / move ctor / move assign) gets the same line tossed at it: "Assume that every operation invalidates iterators and references", that is, assume every operation invalidates all iterators and references. That batch of comments in flat_tree.h all carries this tone (lines 151/217/231/273/306/319/374).

Why not quietly follow vector's fine-grained rules — reserve invalidates only on an actual realloc, insert invalidates only from the insertion point onward? We have chewed on this one, and the answer is quite pragmatic: that fine-grained set is too unfriendly to callers. While writing code you would constantly have to wonder "will this insert trigger a growth? is this reserve big enough?" — pile up that mental load and people start misremembering. flat_tree simply cuts it clean: once anything is mutated, treat everything as invalidated. A coarse rule is less "accurate" than a fine-grained one, but nobody misremembers it — and that is what actually matters in engineering. We will come back to this in 03-5, which covers iterator invalidation in detail.

The source even tosses out a UB example outright (flat_map.h:57-60):

```cpp
container["new element"] = it.second;   // UB: operator[] may trigger growth, it becomes invalid
```

This "mutate while iterating" pattern is flat-out undefined behavior in flat_map — a good deal stricter than `std::map` (whose nodes are stable and whose iterators survive mutations).

## reserve and shrink_to_fit

vector provides two capacity-management interfaces, and flat_tree passes them through unchanged.

`reserve(n)` preallocates memory that can hold n elements. If you know roughly how much you will end up storing, reserve up front and the relocation cost of all those later growth steps is saved entirely. This is especially useful when bulk-constructing a flat_map (03-5 covers the bulk-construction patterns in detail). `shrink_to_fit()` goes the other way: it shrinks capacity down to size and hands the excess memory back — though it is a non-binding request; the standard allows an implementation to ignore it outright, but mainstream implementations generally play along and do a realloc.

Both of these invalidate iterators, because either can trigger a realloc.

---

With these in hand, the next piece gets the complexity toolkit straight — `O(lg n)` lookup set against `O(n)` insert, amortized set against single-shot — laying the groundwork for the flat_tree complexity conclusions to come.

## References

- [cppreference: std::vector](https://en.cppreference.com/w/cpp/container/vector)
- [cppreference: vector's iterator invalidation rules](https://en.cppreference.com/w/cpp/container/vector#Iterator_invalidation)
- [Chromium `base/containers/flat_tree.h` — iterator invalidation comments](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/flat_tree.h)
- [Bjarne Stroustrup — vector and cache performance experiments](https://www.stroustrup.com/Software-for-infrastructure.pdf)
