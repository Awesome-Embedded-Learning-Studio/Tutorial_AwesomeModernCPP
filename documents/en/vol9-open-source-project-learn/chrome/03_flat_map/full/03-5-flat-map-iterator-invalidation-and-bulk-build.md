---
chapter: 1
cpp_standard:
- 11
- 17
- 20
description: "Spells out flat_map's iterator-invalidation rules, stricter than std::map's (the conservative 'Assume every operation invalidates' stance), plus the extract/replace bulk-rebuild pattern."
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
translation:
  source: documents/vol9-open-source-project-learn/chrome/03_flat_map/full/03-5-flat-map-iterator-invalidation-and-bulk-build.md
  source_hash: 5ff450583a8a25a0b8af6384e490837642de5a6767d93f4fa3c4a93eda6ca588
  translated_at: '2026-09-26T02:35:27+00:00'
  engine: anthropic
  token_count: 5500
---
# flat_map hands-on (V): iterator invalidation and bulk construction

The first time we swapped `std::map` for `flat_map`, we tripped over something almost embarrassingly plain: we held on to an iterator, touched the container in the middle, came back to use it, and it was dangling. That kind of thing simply doesn't happen with `std::map` — node containers mind their own business, and inserting one node never moves anyone else. But `flat_map` is a `vector` underneath: one grow relocates the whole block, one erase shifts everything after it, and the iterator is just gone.

Our first reaction at the time was to hit the docs and pin down exactly which operations invalidate and which don't. Chromium's answer stopped us cold: it doesn't lay out fine-grained rules for you at all — it just throws one blunt line at you: "assume every operation invalidates all iterators." In this piece we take that rule apart to see why it has to be written that way, and along the way we'll work through the bulk-rebuild APIs that ship with `flat_map` (`extract`/`replace`), so that when you need to overhaul the container you're not stuck inserting one element at a time.

## std::map vs flat_map: iterator stability

`std::map` is a node container: each element gets its own heap allocation, and the tree nodes are strung together with pointers. Insert or erase one node and every other node stays put — only pointers move. So the iterators, pointers, and references aimed at them all stay alive:

```cpp
std::map<int, Config> m = /* ... */;
auto it = m.find(3);
m[99] = load(99);   // insert a new element; it stays valid (the node didn't move)
it->second;          // OK
```

Reference stability is a genuine, tangible benefit of `std::map`: you can hold a reference to an element, let the container add and remove entries behind your back, and the reference never goes bad.

`flat_map` is exactly the other way around. The storage is a contiguous `vector`: an insert can trigger a grow (the whole block relocates), and an erase shifts the elements after it forward. Any of these operations turns the iterators, pointers, and references aimed at elements into dangling ones:

```cpp
flat_map<int, Config> m = /* ... */;
auto it = m.find(3);
m[99] = load(99);   // may grow → it invalidated!
it->second;          // UB! possibly dangling
```

Cache friendliness is paid for with a semantic cost: contiguous storage buys you performance, and the price is that reference stability is gone. Keep that ledger firmly in your head.

## flat_tree's conservative rule

So what does `flat_map`'s invalidation rule actually look like? Deriving it precisely from `vector`'s behavior gets awfully fine-grained: `reserve` only invalidates when `n > capacity`, `insert` invalidates only from the insertion point onward, `push_back` doesn't invalidate when it doesn't grow... That rulebook is a memory test for the caller — before every insert you'd have to weigh "is this one going to grow?"

flat_tree doesn't hand you that exam at all. It takes a cleaver to every mutation and marks them all invalid (flat_tree.h:151/217/231/273/306/319/374). The source comment, verbatim, is a single line:

> Assume that every operation invalidates iterators and references.

The operations covered: `reserve`, `shrink_to_fit`, `insert`, `erase`, `swap`, move construction, move assignment, `extract`, `replace`, `clear`. One sentence: once it's mutated, treat everything as invalidated.

Our first reaction on reading that rule was "isn't that terribly wasteful." When `push_back` doesn't grow, the iterator is plainly still valid — on what grounds do you call it invalidated? It took being bitten once by a stretch of similar code in code review for it to click: the fine-grained rules are the real trap. Callers can't keep them straight, misjudge easily, assume something doesn't invalidate when it does, and go straight to UB. Nobody misremembers a blunt rule — "mutated means don't reuse the old iterator" is always safe. This deliberate safety slack is, engineering-wise, a very sound trade: better that you "wastefully" throw away an iterator that still worked than that you guess wrong once.

Chromium pastes the UB counterexample right into the source comment (flat_map.h:57-60), a single line:

```cpp
container["new element"] = it.second;   // UB: operator[] may trigger a grow, it invalidated
```

This kind of "mutate while iterating" code is flat-out undefined behavior in `flat_map`. flat_tree states the rule bluntly precisely so you never even ask "will this particular mutation grow?" You assume invalidation up front, and this whole class of bug gets choked off at the root.

Operationally, there are two rules we'd tell you to burn into muscle memory. One: never hold iterators, pointers, or references across a mutation. Take the result of `find`, use it this once, throw it away, and `find` again for the next operation — drop the thought "I'll be needing this iterator again in a moment." Two: if you genuinely have a "hold a stable reference" need — say a callback that has to keep a long-term grip on a pointer to one element — then `flat_map` is the wrong tool; do the sensible thing and use `std::map`. Node containers are where reference stability lives, and that's a scenario `flat_map` simply cannot serve.

---

## The bulk-construction pattern (worth repeating)

[03-4](./03-4-flat-map-sorted-unique-construction.md) covered bulk construction already. Here we take the same topic from another angle and look at it once more through the lens of "dodging iterator invalidation." If you want to stuff a batch of elements into a `flat_map`, never iterate-and-insert as you go: every insert invalidates all iterators, and on top of that the complexity rockets to O(N²) — you take the beating from both sides. The right posture is to gather everything into a `vector` first, then move it in, in one shot:

```cpp
// 1. Gather into a vector (push_back is amortized O(1), no iterator-invalidation problem — you hold only the vector, not a flat_map iterator)
std::vector<std::pair<int, Config>> batch;
batch.reserve(N);
for (...) batch.emplace_back(k, v);

// 2. Move into the flat_map in one shot (bulk construction, one O(N log N) sort)
flat_map<int, Config> m(std::move(batch));
```

`push_back` on a `vector` is amortized O(1), and what you're holding is a `vector`, not a `flat_map` iterator, so the whole tedious invalidation business never touches you. If it's a large batched update against an existing `flat_map`, you need the pair of APIs below: extract the contents out, modify them, then replace them back.

---

## extract() and replace(): bulk rebuild

flat_tree gives you two APIs that prop up a "pull the data out, overhaul it, hand it back" bulk-rebuild pattern. The first time we met this pair, the design struck us as rather clever: it unloads `flat_map`'s ordering constraint entirely, lets you run wild on a raw `vector`, and when you've thrashed it to your heart's content you hand it back.

### extract()&&(flat_tree.h:894)

```cpp
container_type extract() && {
    return std::exchange(body_, container_type{});   // hands the internal vector out whole, body_ left empty
}
```

`extract()` is rvalue-qualified: you can only call it on a dying `flat_map` (an rvalue). It `std::exchange`s the underlying `vector` out to you whole, and the original `flat_map` is left empty. Once you hold that `vector`, you can thrash it however you like — `push_back`, `sort`, `unique`, mutate elements. None of those operations on a `vector` carry `flat_map`'s ordering constraint; you're far freer. When you're done, just `replace` it back.

### replace(container_type&&)(flat_tree.h:899-905)

```cpp
void replace(container_type&& body) {
    DCHECK(is_sorted_and_unique(body, comp_));   // verify the new data is sorted and duplicate-free
    body_ = std::move(body);                      // take over
}
```

`replace(body)` is the inverse of `extract`: it hands a new `vector` back for `flat_map` to take over. It first runs `DCHECK(is_sorted_and_unique)` to verify the new data is sorted and duplicate-free (the same contract as sorted_unique construction) and only then takes ownership. With this one out-and-back you get "pull out the vector → modify freely → sort and dedupe → hand back," sidestepping both the O(n) shift cost and the iterator invalidation of `flat_map`'s single-element operations the entire way.

### A typical bulk-rebuild flow

```cpp
flat_map<int, Config> m = /* ... */;

// 1. extract the vector out (called on an rvalue)
std::vector<std::pair<int, Config>> raw = std::move(m).extract();

// 2. Modify in bulk freely on the vector (no ordering constraint, no shift cost)
for (...) raw.emplace_back(k, v);

// 3. Sort and dedupe
std::sort(raw.begin(), raw.end(), by_key);
raw.erase(std::unique(raw.begin(), raw.end(), equiv), raw.end());

// 4. replace back into the flat_map (sorted_unique-style check)
m.replace(std::move(raw));
```

This route suits scenarios where you need to make heavy structural modifications to a `flat_map`; it's dramatically more efficient than one `m.insert`/`m.erase` at a time (each an O(n) shift plus a pile of invalidated iterators). One thing we do have to flag: `replace` requires the new data to be sorted and duplicate-free, and guaranteeing that is on you. It only runs the `DCHECK` verification in debug builds; in release nothing will catch you. The contract is the same as sorted_unique — an honest contract, and you have to be the honest party.

All that's left is to put `flat_map`, `std::map`, and `absl::btree_map` side by side and measure them for real — that's the next piece.

## References

- [Chromium `base/containers/flat_tree.h` — iterator-invalidation comment + extract/replace](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/flat_tree.h)
- [Chromium `base/containers/flat_map.h` — the UB example](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/flat_map.h)
- [flat_map prerequisite (I): std::vector internals and growth](./pre-01-flat-map-vector-internals-and-growth.md)
- [flat_map hands-on (IV): sorted_unique construction optimization](./03-4-flat-map-sorted-unique-construction.md)
