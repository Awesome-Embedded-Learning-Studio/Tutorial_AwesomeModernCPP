---
chapter: 1
cpp_standard:
- 17
- 20
description: "Start from the cache-miss pain in a config table, work out exactly what flat_map has to fill in, and pin down the target API in one pass: CHECK-based at, a transparent comparator by default, a sorted_unique constructor, and the rest of the load-bearing decisions"
difficulty: intermediate
order: 1
platform: host
prerequisites:
- 'flat_map prerequisite (0): ordered associative containers and std::map''s red-black tree'
- 'flat_map prerequisite (2): complexity and amortized analysis'
reading_time_minutes: 11
related:
- 'flat_map in practice (II): the flat_tree core skeleton'
tags:
- host
- cpp-modern
- intermediate
- 容器
- map
- 优化
title: "flat_map in practice (I): motivation and API design"
---
# flat_map in practice (I): motivation and API design

In [prerequisite (0)](./pre-00-flat-map-ordered-assoc-container-intro.md) we left a question hanging: every node in `std::map`'s red-black tree costs one malloc, and a lookup chases pointers all the way down, each step a likely cache miss. The asymptotic complexity is `O(log n)`, no argument there, but once you land in the small-to-medium, read-heavy regime the constant factor runs away with the performance. This piece picks that pain back up, works out what flat_map actually has to fill in, and pins down the target API in one pass.

What flat_map is after fits in a sentence: give the write-once-read-many ordered map a cache-friendly implementation. It has no ambition to take std::map's job; "large and churned constantly" stays std::map's territory. What it fills is the read-heavy gap. Across this series we tear down how Chromium implements it and hand-roll a teaching version alongside, so the tradeoffs land more easily when you see both sides.

---

## Starting from a real pain: the config table

Say we're writing a command dispatch table. The program loads a pile of command-to-callback mappings from a config file at startup, and after that it only ever looks them up:

```cpp
std::map<std::string, Handler> commands;
for (auto& [name, handler] : load_commands()) {
    commands.emplace(name, handler);   // built once at startup
}

// runtime: each command is a lookup only
auto it = commands.find(cmd_name);
if (it != commands.end()) it->second(args);
```

Nothing obviously wrong here, and `std::map`'s `O(log n)` lookup sounds "fast enough." Run perf on it and you'll stare: a lot of time piles up in `std::map::find`, and the table might hold a few dozen entries. What `O(log n)` doesn't tell you is that each of those `log n` steps in a `find` is a pointer dereference that probably cache-misses. A few dozen elements means `log n` is six or seven steps, each one a miss, and the lookup is shot.

We tore down the root cause in [pre-00](./pre-00-flat-map-ordered-assoc-container-intro.md): the red-black tree nodes are scattered across the heap, each carrying 32 bytes of metadata plus a heap allocation, and `node = node->left_` during a lookup is a data-dependent dereference. The CPU can't prefetch reliably because the next address depends on data it hasn't read yet. At small sizes this constant factor outweighs `log n` by a lot. That is exactly where std::map bites you in this scenario.

---

## Why three off-the-shelf answers all fall short

The pain is clear. Can we get away with an existing wheel? Let's take them in turn.

`std::map` needs no more said; the cache miss above is its work, and one-malloc-per-node is incurable.

What about `std::unordered_map`? A hash table averages `O(1)` lookup, which sounds great. At small sizes its constant factor doesn't necessarily win either; hash computation and collision handling still cost. Worse, it's unordered, so you can't iterate keys in sorted order, can't `lower_bound` into a range, can't do ordered interval queries. The day the product asks "list all commands" or "filter by prefix," unordered_map is stuck on the spot. Chromium's own container guide says as much: it doesn't recommend `std::unordered_map`, and its performance loses to absl's hash maps.

The remaining path is to roll your own: keep a `vector<pair<K,V>>`, sort after each insert, and `std::lower_bound` for lookup. Functionally that's flat_map. But you're left to handle deduplication yourself, keep things sorted yourself, remember iterator invalidation yourself, and if you want a sorted_unique optimization you bolt on a tag dispatch yourself. It's reinventing the wheel, and every step is a place to get it wrong. I rolled one years ago and realized I was just re-stepping through the traps flat_map was built to handle.

All three fall short: map cache-hurts, unordered_map is unordered and officially discouraged, and hand-rolling is duplicate work that's easy to get wrong. Chromium's answer is blunt: bundle this functionality into one container with a std::map-style interface and call it `flat_map`.

---

## Chromium's answer: the flat_map design philosophy

The design philosophy comes down to two things. First, storage is one contiguous sorted array (by default a `vector<pair<K,V>>`), and lookup is a binary search (`lower_bound`). Contiguity buys cache friendliness; binary search buys `O(log n)` lookup. Second, it does not chase insert performance. A single insert is `O(n)` of shifting, and it accepts that; what it buys in return is a one-shot sort at construction (bulk construction is `O(N log N)`) and that cache-friendly low constant factor at lookup time.

Those two lines draw flat_map's fit: write-once-read-many, or ordered maps whose size stays small. If your scenario is "large and churned constantly," `O(n)` inserts will hurt enough to make you question your choices. That's std::map's home turf; don't crash the party.

### Architecture overview: flat_tree is the only implementation

The implementation has an elegant layering that stopped me cold the first time I read it: there's really only one class at the core, `flat_tree<Key, GetKeyFromValue, KeyCompare, Container>`, a general "sorted array associative container." `flat_map` and `flat_set` are both thin shells over it.

How thin? `flat_map<Key, Mapped, ...>` inherits from `flat_tree<Key, internal::GetFirst, ...>`, where `GetFirst` is an extractor that pulls `first` out of a `pair<Key, Mapped>` to use as the key (flat_map.h:194-195, 24-29). `flat_set<Key, ...>` is blunter still: a `using =` alias for `flat_tree<Key, std::identity, ...>`, where `std::identity` treats the value itself as the key (flat_set.h:159-163).

Sit with that design for a second. One flat_tree implementation, just by swapping the "key extractor" line, produces both a map face and a set face. This is the classic strategy-object play. Once you understand flat_tree, flat_map and flat_set come for free. So the hands-on half of this series is mostly about tearing down flat_tree; everything that differs between flat_map and flat_set lives on that one extractor line.

---

## The target API

Motivation covered. Let's pin down the target API in one pass, then come back and dig out the decision behind each signature. Naming stays in the `tamcpp::chrome` namespace, snake_case, matching the OnceCallback and WeakPtr series.

### Construction

```cpp
#include "flat_map/flat_map.hpp"
using namespace tamcpp::chrome;

// from unordered data (sorted and deduped internally)
flat_map<int, std::string> m1 = {{1, "a"}, {3, "c"}, {2, "b"}};

// move-construct from an existing vector (bulk construction, efficient)
std::vector<std::pair<int, std::string>> raw = {{1,"a"}, {2,"b"}, {3,"c"}};
flat_map<int, std::string> m2(std::move(raw));

// sorted_unique construction (data already sorted, skip the sort)
flat_map<int, std::string> m3(sorted_unique, std::vector<std::pair<int,std::string>>{{1,"a"},{2,"b"},{3,"c"}});
```

### Lookup and modification

```cpp
flat_map<int, Config> m;
m[1] = load(1);              // operator[]: inserts if missing
m.insert_or_assign(2, x);    // insert or overwrite
m.try_emplace(3, arg1, arg2);// construct mapped only if key absent

auto it = m.find(1);         // O(log n) binary search
if (it != m.end()) use(it->second);

m.at(99);                    // out of range -> CHECK crash (not throw, see decision analysis)
```

### Heterogeneous lookup (transparent comparator)

```cpp
flat_map<std::string, Config> sm;        // default Compare = std::less<> (transparent)
sm.find("timeout");                       // look up with const char* directly, no temporary std::string
```

---

## Decision analysis behind the API

The API is pinned down, but each signature hides a tradeoff. Let's pull the "why" out of each one.

### Why at() uses CHECK instead of throw

`std::map::at(key)` throws `std::out_of_range` on a missing key; that's the standard library's rule. `flat_map::at(key)` on a missing key fails a `CHECK` and aborts the program outright (flat_map.h:293/302), no negotiation. Why so harsh?

Because an out-of-range access usually means the caller's logic is wrong: either you should have checked with `find` first, or you're certain the key has to be in there. That kind of bug needs to blow up in release too, not surface as an exception that some upstream `try/catch` papers over, which usually buries a real logic error under fallback handling. This is Chromium's consistent error-handling style: definite logic errors get CHECK, exceptions are not the safety net. [WeakPtr's `operator*` using CHECK to guard a dereference of an invalidated handle](../../02_weak_ptr/full/02-1-weak-ptr-motivation-and-api-design.md) is the same philosophy; we covered it there too.

### Why the default comparator is the transparent std::less<>

`std::map` defaults to `Compare = std::less<Key>`, non-transparent; flat_map swaps in `std::less<>`, transparent (flat_map.h:192). That one swap opens the door to heterogeneous lookup: you can `find` a `std::string` map with a `const char*` without constructing a temporary `std::string`. On a hot path the accumulated cost of those temporaries is real; see [pre-03](./pre-03-flat-map-comparator-and-transparent.md). Modern C++ recommends transparent comparators as the default, and flat_map just does it.

### Why we store pair<K,V> and not pair<const K, V>

The underlying storage is `std::vector<std::pair<Key, Mapped>>`, with a non-const key (flat_map.h:193). This counterintuitive choice is forced by vector's shifting: insert and erase relocate whole pairs, which means the pair has to be move-assignable, and `pair<const K, V>` is not move-assignable. The cost is that the key is exposed as mutable; an iterator could in principle rewrite a key and break the sorted invariant, and only user discipline stops that. The full accounting is in [pre-05](./pre-05-flat-map-enua-ebo-and-pair-storage.md).

### Why we provide a sorted_unique constructor

If you can guarantee the data is already sorted, constructing with the `sorted_unique` tag skips the `O(N log N)` sort and drops to `O(N)`. Better still, in debug builds a `DCHECK` verifies you didn't lie: you claimed sorted, it actually goes and checks. This is a real zero-cost abstraction; see [pre-04](./pre-04-flat-map-tag-dispatch-and-sorted-unique.md).

---

## Our implementation vs Chromium's tradeoffs

Like the previous two series, the teaching version keeps the core machinery (the flat_tree adapter, sorted vector, sorted_unique, transparent comparison) and simplifies where it can. A preview of the tradeoffs; 03-6 closes the loop with measurements:

| Axis | Chromium | Our teaching version |
|---|---|---|
| Underlying Container | `std::vector` | same |
| Sort | `std::stable_sort` + unique + erase | same |
| Transparent comparison | `KeyT<K>` + `KeyValueCompare` two overloads | simplified to a direct template |
| `DCHECK(is_sorted_and_unique)` | full | approximated with `assert` |
| `[[no_unique_address]]` comparator | annotated | annotated (both GCC and Clang support it) |
| `replace` / `extract` | full | omitted (left as an extension) |

We build the core out of the pure standard library (`std::vector`, `std::sort`, `std::lower_bound`) and copy Chromium's design philosophy wholesale. The Chromium-specific complexity, the `raw_ptr_exclusion` annotations and `NO_UNIQUE_ADDRESS` macro machinery, gets cut entirely. That's their engineering plumbing; the teaching version doesn't need it.

---

## Environment setup

flat_map leans on C++20 concepts (`requires`, `std::convertible_to`), ranges (`std::ranges::lower_bound`), and the `[[no_unique_address]]` attribute. So the floor is C++20.

### Compiler requirements

GCC 11+ or Clang 12+ both work; compile with `-std=c++20`. `[[no_unique_address]]` is supported on both GCC and Clang, and its EBO behavior for empty types is equivalent and correct, so you can stop worrying about that one.

### Verification code

```cpp
#include <concepts>
#include <ranges>
#include <vector>

static_assert(__cpp_lib_ranges >= 201911L);   // ranges available

constexpr bool check_nua_works() {
    struct Empty {};
    struct H { [[no_unique_address]] Empty e; int i; };
    return sizeof(H) == sizeof(int);   // EBO folds Empty away
}
static_assert(check_nua_works());
```

If this compiles clean on your machine, the environment is ready. The project scaffold stays in `code/volumn_codes/vol9/full_tutorial_codes/chrome_design/`; starting from 03-2 we drop in the `19_` through `22_` batch of flat_map samples.

Motivation and API are straight on paper now. But straight on paper and actually writing flat_tree line by line are different things. How the sorted vector adapter hangs together, how the sorted invariant is defended, how the key extractor slots in, all of that is the trapfield the next piece walks through. We start writing in the next piece.

## References

- [Chromium `base/containers/flat_map.h`](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/flat_map.h)
- [Chromium `base/containers/flat_tree.h`](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/flat_tree.h)
- [Chromium `base/containers/README.md` — container selection guide](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/README.md)
- [flat_map prerequisite (0): ordered associative containers and std::map's red-black tree](./pre-00-flat-map-ordered-assoc-container-intro.md)
