---
chapter: 1
cpp_standard:
- 11
- 17
- 20
description: "flat_map's iterator-invalidation rules are stricter than std::map's: it takes the conservative line that every mutation invalidates everything. We unpack why that bluntness is the right call, then walk through the extract/replace bulk-rebuild pattern."
difficulty: intermediate
order: 5
platform: host
prerequisites:
- "flat_map hands-on (IV): sorted_unique construction optimization"
- "flat_map prerequisite (I): std::vector internals and growth"
reading_time_minutes: 10
related:
- "flat_map hands-on (VI): testing and performance comparison"
tags:
- host
- cpp-modern
- intermediate
- 容器
- map
- 内存管理
title: "flat_map hands-on (V): iterator invalidation and bulk construction"
---
# flat_map hands-on (V): iterator invalidation and bulk construction

The first time we swapped `std::map` for `flat_map`, we tripped on something embarrassingly plain: held onto an iterator, touched the container in the middle, came back to use it, and it was dangling. That kind of thing basically never happens with `std::map`. Node containers keep to themselves; inserting one node doesn't move anyone else's. But `flat_map` is a `vector` underneath. One grow relocates the whole block, one erase shifts everything after it, and the iterator is just gone.

Our first instinct was to hit the docs and figure out exactly which operations invalidate and which don't. Chromium gave us an answer that stopped us cold: it doesn't lay out fine-grained rules for you at all. It just throws one blunt line at you, "assume every operation invalidates all iterators." This piece unpacks why a rule like that has to be written this way, and along the way we'll work through the bulk-rebuild API (`extract`/`replace`) that ships with `flat_map`, so that when you need to overhaul the container you're not stuck doing insert one at a time.

## std::map vs flat_map: iterator stability

`std::map` is a node container. Each element gets its own heap allocation, and the tree nodes are strung together with pointers. Insert or erase a node and the other nodes don't budge; only pointers move. So the iterators, pointers, and references pointing at them all stay alive:

```cpp
std::map<int, Config> m = /* ... */;
auto it = m.find(3);
m[99] = load(99);   // insert a new element; it still valid (node didn't move)
it->second;          // OK
```

Reference stability is a genuine, concrete benefit of `std::map`: you can hold a reference to an element, the container adds and removes things behind your back, and the reference stays valid.

`flat_map` is the other way around. The storage is a contiguous `vector`, so an insert can trigger a grow (the whole block relocates), and an erase shifts everything after it forward. Either of these turns iterators, pointers, and references into dangling ones:

```cpp
flat_map<int, Config> m = /* ... */;
auto it = m.find(3);
m[99] = load(99);   // may grow -> it invalidated!
it->second;          // UB! possibly dangling
```

Cache friendliness is bought at the expense of reference stability. Contiguous storage buys you performance and costs you stable references. That's the trade, and you want it firmly in your head.

## flat_tree's conservative rule

So what does `flat_map`'s invalidation rule actually look like? If you tried to nail it down precisely by `vector`'s behavior, it gets intricate fast: `reserve` only invalidates when `n > capacity`, `insert` invalidates only from the insertion point onward, `push_back` doesn't invalidate when it doesn't grow. That rulebook is a memory test for the caller; before every insert you'd have to weigh "is this one going to grow or not."

flat_tree doesn't hand you that exam at all. It takes a cleaver to every mutation and marks them all invalid (flat_tree.h:151/217/231/273/306/319/374). The source comment says, verbatim:

> Assume that every operation invalidates iterators and references.

The operations covered: `reserve`, `shrink_to_fit`, `insert`, `erase`, `swap`, move construction, move assignment, `extract`, `replace`, `clear`. One line. Mutated means treat it all as dead.

Our first reaction to that rule was "isn't that wasteful." A `push_back` that doesn't grow clearly leaves iterators valid, so why call it invalidated? It took getting bitten by a stretch of code just like this in code review before it clicked: the fine-grained rules are the real trap. Callers can't hold them straight, they guess wrong, assume something doesn't invalidate when it does, and it's straight to UB. Nobody misremembers the blunt rule. "Mutated means don't reuse the old iterator" is always safe. This deliberate safety slack is a genuinely cheap engineering tradeoff: better to let you "throw away" an iterator that still works than to let you guess wrong once.

Chromium pastes the UB counterexample right into the source comment (flat_map.h:57-60), one line:

```cpp
container["new element"] = it.second;   // UB: operator[] may grow, it invalidated
```

This kind of "mutate while iterating" code is straight-up undefined behavior in `flat_map`. flat_tree states the rule bluntly precisely so you never even start down the path of "will this particular mutation grow or not." You assume invalidation, and that whole class of bug gets choked off at the root.

Operationally, there are two habits we'd suggest you burn into muscle memory. One: don't hold iterators, pointers, or references across a mutation. Get a `find` result, use it this once, throw it away, and `find` again next time. Drop the thought "I'll hang onto this iterator, I'll need it again later." Two: if you genuinely need to hold a stable reference, say a callback that has to grip a pointer to an element long-term, then `flat_map` is the wrong tool. Reach for `std::map`; only node containers give you reference stability, and that's a scenario `flat_map` just can't serve.

---

## The bulk-construction pattern (revisited)

[03-4](./03-4-flat-map-sorted-unique-construction.md) covered bulk construction. Here we'll take the same idea from a different angle, looking at it again through the lens of "dodging iterator invalidation." If you want to dump a batch of elements into a `flat_map`, do not iterate-and-insert as you go. Each insert invalidates every iterator, and the complexity balloons to O(N²); you get hit from both sides. The right shape is to gather into a `vector` first, then move it in all at once:

```cpp
// 1. Gather into a vector (push_back amortized O(1), no invalidation issue
//    because you hold a vector, not a flat_map iterator)
std::vector<std::pair<int, Config>> batch;
batch.reserve(N);
for (...) batch.emplace_back(k, v);

// 2. Move into flat_map in one shot (bulk construction, sort once O(N log N))
flat_map<int, Config> m(std::move(batch));
```

`push_back` on a `vector` is amortized O(1), and what you're holding is a `vector`, not a `flat_map` iterator, so that whole invalidation headache can't reach you. If you're doing a large batched update against an existing `flat_map`, then you want the next pair of APIs: extract the contents out, modify them, then replace them back.

---

## extract() and replace(): bulk rebuild

flat_tree gives you two APIs that prop up a "pull the data out, overhaul it, hand it back" bulk-rebuild pattern. The first time we ran into this pair it struck us as a clever design. It unloads `flat_map`'s ordering constraint entirely, lets you run wild on a raw `vector`, and when you're done thrashing on it you hand it back.

### extract() && (flat_tree.h:894)

```cpp
container_type extract() && {
    return std::exchange(body_, container_type{});   // hand out the internal vector whole, body_ cleared
}
```

`extract()` is rvalue-qualified, callable only on a dying `flat_map` (an rvalue). It `std::exchange`s the underlying `vector` out to you whole and leaves the original `flat_map` empty. Once you have that `vector`, do whatever you like with it. `push_back`, `sort`, `unique`, mutate elements. Those operations on a `vector` don't carry `flat_map`'s ordering constraint, so you're far freer. Sort it out, then `replace` it back.

### replace(container_type&&) (flat_tree.h:899-905)

```cpp
void replace(container_type&& body) {
    DCHECK(is_sorted_and_unique(body, comp_));   // verify new data is sorted and unique
    body_ = std::move(body);                      // take ownership
}
```

`replace(body)` is the inverse of `extract`: it hands a new `vector` back for `flat_map` to take over. It first runs `DCHECK(is_sorted_and_unique)` to verify the new data is sorted and free of duplicates (the same contract as sorted_unique construction), and only then takes ownership. In and out this way, you get "pull the vector, modify freely, sort and dedupe, hand back," and the whole path sidesteps the O(n) shift cost and iterator invalidation of `flat_map`'s single-element operations.

### A typical bulk-rebuild flow

```cpp
flat_map<int, Config> m = /* ... */;

// 1. extract the vector (call on an rvalue)
std::vector<std::pair<int, Config>> raw = std::move(m).extract();

// 2. Modify freely on the vector (no ordering constraint, no shift cost)
for (...) raw.emplace_back(k, v);

// 3. Sort and dedupe
std::sort(raw.begin(), raw.end(), by_key);
raw.erase(std::unique(raw.begin(), raw.end(), equiv), raw.end());

// 4. Replace back into flat_map (sorted_unique-style check)
m.replace(std::move(raw));
```

This shape suits scenarios where you need to make heavy structural modifications to a `flat_map`. It's far more efficient than `m.insert`/`m.erase` one at a time (each O(n) shift, plus a pile of invalidated iterators). One thing we have to flag: `replace` requires the new data to be sorted and unique, and that's on you to guarantee. It only runs the `DCHECK` under debug builds; release won't catch you. The contract is the same as sorted_unique, an honest contract, and you have to be honest about it.

The rest is lining `flat_map` up against `std::map` and `absl::btree_map` and measuring them. That's the next piece.

## References

- [Chromium `base/containers/flat_tree.h`: iterator-invalidation comment + extract/replace](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/flat_tree.h)
- [Chromium `base/containers/flat_map.h`: UB example](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/flat_map.h)
- [flat_map prerequisite (I): std::vector internals and growth](./pre-01-flat-map-vector-internals-and-growth.md)
- [flat_map hands-on (IV): sorted_unique construction optimization](./03-4-flat-map-sorted-unique-construction.md)
