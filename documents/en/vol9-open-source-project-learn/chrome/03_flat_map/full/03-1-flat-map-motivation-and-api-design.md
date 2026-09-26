---
chapter: 1
cpp_standard:
- 17
- 20
description: "Starting from the config table's cache-miss pain point, clarifying the holes flat_map has to fill, and pinning down the complete target API, including key decisions such as CHECK in at, the transparent default comparator, and sorted_unique construction"
difficulty: intermediate
order: 1
platform: host
prerequisites:
- 'flat_map prerequisite (0): ordered associative containers and std::map''s red-black tree'
- 'flat_map prerequisite (II): complexity and amortized analysis'
reading_time_minutes: 11
related:
- 'flat_map hands-on (II): the flat_tree core skeleton'
tags:
- host
- cpp-modern
- intermediate
- 容器
- map
- 优化
title: "flat_map hands-on (I): motivation and API design"
translation:
  source: documents/vol9-open-source-project-learn/chrome/03_flat_map/full/03-1-flat-map-motivation-and-api-design.md
  source_hash: 73f0ebc4e941bfa124f2d689ebec14086808c4db32c8fa189040fa240da44664
  translated_at: '2026-09-26T02:32:39+00:00'
  engine: anthropic
  token_count: 5600
---
# flat_map hands-on (I): motivation and API design

In [prerequisite (0)](./pre-00-flat-map-ordered-assoc-container-intro.md) we left a question hanging: every node in `std::map`'s red-black tree costs one malloc, and a lookup chases pointers all the way down, stepping on a likely cache miss the whole way. The asymptotic complexity is `O(log n)`, no argument there — but once you drop into the small-to-medium, read-heavy regime, the constant factor drags performance into the gutter. This piece goes back to that pain point, works out exactly which holes flat_map is meant to fill, and pins down the target API in one pass while we're at it.

What flat_map wants to do fits in one sentence: give the write-once-read-many ordered map a cache-friendly implementation. It has no designs on std::map's job — for the "large and constantly mutated" territory, std::map still wins. What it fills is the read-mostly gap. Over this series we tear down how Chromium implements it while hand-rolling a teaching version of our own; reading the two side by side makes the trade-offs much easier to see through.

---

## Start with a performance pain point: the config table

Suppose we're writing a command dispatch table. The program loads a pile of command-to-callback mappings from a config file at startup, and from then on it only looks things up, never mutates:

```cpp
std::map<std::string, Handler> commands;
for (auto& [name, handler] : load_commands()) {
    commands.emplace(name, handler);   // built once at startup
}

// runtime: every command is a lookup only
auto it = commands.find(cmd_name);
if (it != commands.end()) it->second(args);
```

This code looks faultless at a glance, and `std::map`'s `O(log n)` lookup even sounds "fast enough". Then you run perf and your jaw drops: a fat slice of time piles up in `std::map::find`, on a table that might hold a few dozen entries. What asymptotic `O(log n)` doesn't tell you is that each of the `log n` steps inside a `find` is a pointer dereference with a high chance of a cache miss — a few dozen elements means `log n` is six or seven steps, and with each step missing, the lookup is wrecked.

We took the root cause apart in [pre-00](./pre-00-flat-map-ordered-assoc-container-intro.md): red-black tree nodes are scattered across the heap, each carrying 32 bytes of metadata plus a heap allocation, and `node = node->left_` during a lookup is a data-dependent dereference — even the prefetcher can't guess where the next address will be. Once the data volume is small, this "constant factor" dominates far more than `log n` does. That is exactly where std::map stabs you in this scenario.

---

## Why none of the three ready-made solutions is enough

The pain is clear. Can we get by on someone else's wheel? Let's take them in turn.

`std::map` goes without saying: the cache misses above are its handiwork, and the one-malloc-per-node ailment has no cure.

What about `std::unordered_map`? A hash table averages `O(1)` lookup, which sounds gorgeous. But at small data volumes its constant factor doesn't necessarily come out ahead — hash computation plus collision-handling overhead is sitting right there. More fatal still, it is unordered: you can't iterate in key order, can't `lower_bound` into a range, can't serve ordered range queries. The day the product asks the command dispatch table to "list all the commands" or "filter by prefix", unordered_map is caught flat-footed. Chromium's own container guide says it outright: `std::unordered_map` is not recommended, and its performance loses to absl's line of hash maps.

The one road left is hand-rolling: keep your own `vector<pair<K,V>>`, `sort` after every insert, look up with `std::lower_bound`. Functionally that's no different from flat_map — except you get to handle deduplication yourself, keep the order yourself, remember iterator invalidation yourself, and if you want the sorted_unique optimization, bolt the tag dispatch on yourself too... Pure reinvention of the wheel, and every step is an invitation to get it wrong. We hand-rolled a version ourselves once, and later realized we were re-stepping, one by one, into the very pits flat_map exists to spare you.

None of the three holds up: map hurts on cache, unordered_map is unordered and officially discouraged, and hand-rolling is duplicate work that invites bugs. Chromium's answer is blunt — package these capabilities into one container with a std::map-style interface, and call it `flat_map`.

---

## Chromium's answer: the flat_map design philosophy

flat_map's design philosophy boils down to two things. First, storage is one contiguous sorted array (by default a `vector<pair<K,V>>`), and lookup goes through binary search (`lower_bound`) — contiguity buys cache friendliness, binary search buys `O(log n)` lookup. Second, it doesn't chase insert performance at all: it swallows the `O(n)` shift of a single insert, and what it buys back is a one-shot sort at construction time (bulk construction at `O(N log N)`) and that cache-friendly low constant factor at lookup time.

These two points draw flat_map's applicability boundary: write-once-read-many ordered maps, or ordered maps whose data volume stays tiny. If your scenario is "large and constantly mutated", `O(n)` inserts will hurt badly enough to make you question your life choices — that is std::map's home turf, don't go crash the party.

### Architecture overview: flat_tree is the only implementation

There's an elegant layering inside flat_map's implementation that stopped us cold the first time we read it: the core is really a single class, `flat_tree<Key, GetKeyFromValue, KeyCompare, Container>`, a generic "sorted-array associative container". `flat_map` and `flat_set` are both thin shells over it.

How thin exactly? `flat_map<Key, Mapped, ...>` inherits from `flat_tree<Key, internal::GetFirst, ...>`, where `GetFirst` is an extractor that digs `first` out of `pair<Key, Mapped>` to serve as the key (flat_map.h:194-195, 24-29). `flat_set<Key, ...>` is even more direct: it is literally an alias of `flat_tree<Key, std::identity, ...>`, a single `using =`, with `std::identity` taking the value as the key as-is (flat_set.h:159-163).

Savor this design for a second — one flat_tree implementation, just by swapping that one "key extractor" line, puts on both a map face and a set face. This is the classic strategy-object play: once you understand flat_tree, flat_map and flat_set come free of charge. So the hands-on half of this series mostly dissects flat_tree; everything that differs between flat_map and flat_set lives on that one extractor line.

---

## Designing the target API

Motivation covered. Now we pin down the target API in one pass, then circle back and dig out the decision hidden in each signature. Naming stays in the `tamcpp::chrome` namespace, snake_case style, consistent with the OnceCallback and WeakPtr series.

### Construction

```cpp
#include "flat_map/flat_map.hpp"
using namespace tamcpp::chrome;

// construct from unordered data (sorted and deduplicated internally)
flat_map<int, std::string> m1 = {{1, "a"}, {3, "c"}, {2, "b"}};

// construct by moving in an existing vector (bulk construction, efficient)
std::vector<std::pair<int, std::string>> raw = {{1,"a"}, {2,"b"}, {3,"c"}};
flat_map<int, std::string> m2(std::move(raw));

// sorted_unique construction (data already sorted, skip the sort)
flat_map<int, std::string> m3(sorted_unique, std::vector<std::pair<int,std::string>>{{1,"a"},{2,"b"},{3,"c"}});
```

### Lookup and modification

```cpp
flat_map<int, Config> m;
m[1] = load(1);              // operator[]: insert if missing
m.insert_or_assign(2, x);    // insert or overwrite
m.try_emplace(3, arg1, arg2);// construct mapped only when the key is absent

auto it = m.find(1);         // O(log n) binary search
if (it != m.end()) use(it->second);

m.at(99);                    // out of range → CHECK crash (not throw, see the decision analysis)
```

### Heterogeneous lookup (the transparent comparator)

```cpp
flat_map<std::string, Config> sm;        // default Compare = std::less<> (transparent)
sm.find("timeout");                       // look up directly with const char*, no temporary std::string
```

---

## Analyzing the API design decisions

The API is settled, but every signature hides a trade-off inside. Let's dig the "why" out of them one by one.

### Why at() uses CHECK instead of throw

`std::map::at(key)` throws `std::out_of_range` on a missing key — that is the standard library's rule. But `flat_map::at(key)` on a missing key goes straight to `CHECK` failure and program abort (flat_map.h:293/302), without leaving the slightest room for negotiation. Why so ruthless?

Because an out-of-range access usually means the caller's logic is broken — either you should have checked with `find` first, or you ought to be certain the key is in there. A bug like this needs to blow up on the spot in release builds too, instead of being tossed out as an exception that some upstream `try/catch` papers over — the latter all too often buries a real logic error under fallback handling. This is Chromium's consistent error-handling style: definite logic errors get CHECK; exceptions are not the safety net. [WeakPtr's `operator*` using CHECK to guard against dereferencing an invalidated pointer](../../02_weak_ptr/full/02-1-weak-ptr-motivation-and-api-design.md) is the same philosophy, and we talked it over in that piece too.

### Why the default comparator is the transparent std::less<>

`std::map` defaults to `Compare = std::less<Key>`, which is opaque; flat_map swaps in `std::less<>` by default, which is transparent (flat_map.h:192). That one swap cracks the door open for heterogeneous lookup — you can `find` in a map keyed by `std::string` using a `const char*`, no temporary `std::string` construction required. On hot paths the accumulation of these little temporaries is substantial; see [pre-03](./pre-03-flat-map-comparator-and-transparent.md) for the details. The recommended default in modern C++ is precisely a transparent comparator, and flat_map simply follows through.

### Why the storage is pair<K,V> and not pair<const K,V>

The underlying storage is `std::vector<std::pair<Key, Mapped>>`, with a non-const key (flat_map.h:193). This counterintuitive trade-off is forced by the vector's shifting — insert/erase relocates whole pairs, which requires move-assigning the pair as a whole, and `pair<const K, V>` flatly refuses to be move-assignable. The cost is that the key is exposed as mutable: in principle an iterator could rewrite a key and break the sorted invariant, and nothing but user discipline stands in the way. The itemized bill for this trade is in [pre-05](./pre-05-flat-map-enua-ebo-and-pair-storage.md).

### Why sorted_unique construction is provided

If you can guarantee the data is already sorted, constructing with the `sorted_unique` tag skips the `O(N log N)` sort outright and drops to `O(N)`. Better still, in debug builds a `DCHECK` verifies on your behalf whether you lied — you claim it is sorted, and it really goes and checks. This is a bona fide zero-cost abstraction; see [pre-04](./pre-04-flat-map-tag-dispatch-and-sorted-unique.md) for the details.

---

## Our implementation and the trade-offs against Chromium

As with the previous two series, our teaching version keeps the core machinery (flat_tree adapter + sorted vector + sorted_unique + transparent comparison) and simplifies where we reasonably can. A preview of the trade-offs now; 03-6 closes the loop with measured comparisons:

| Dimension | Chromium's implementation | Our teaching version |
|---|---|---|
| Underlying Container | `std::vector` | Same |
| Sorting | `std::stable_sort` + unique + erase | Same |
| Transparent comparison | `KeyT<K>` + `KeyValueCompare` two overloads | Simplified to a direct template |
| `DCHECK(is_sorted_and_unique)` | Full | Emulated with `assert` |
| `[[no_unique_address]]` comparator | Annotated | Annotated (supported by both GCC and Clang) |
| `replace`/`extract` | Full | Omitted (left as a future extension) |

We build the core out of nothing but the standard library (`std::vector`, `std::sort`, `std::lower_bound`), copy Chromium's design philosophy wholesale, and chop out all the Chromium-specific complexity — `raw_ptr_exclusion`, the `NO_UNIQUE_ADDRESS` macro machinery, all of it. That is house-special plumbing from their engineering practice; a teaching version has no use for it.

---

## Environment setup

flat_map needs C++20 concepts (`requires`, `std::convertible_to`), ranges (`std::ranges::lower_bound`), and the `[[no_unique_address]]` attribute. So the minimum bar is C++20.

### Compiler requirements

GCC 11+ or Clang 12+ both work; compile with `-std=c++20`. `[[no_unique_address]]` is supported on both GCC and Clang, and its EBO behavior for empty types is equivalently correct — you can rest easy on that point.

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

If this passes on your machine, the environment is ready to go. The companion project scaffold is still `code/volumn_codes/vol9/full_tutorial_codes/chrome_design/`; starting from 03-2, we will drop this batch of flat_map examples, `19_` through `22_`, into it.

That squares away the motivation and the API. But squared away on paper is one thing; actually writing flat_tree out line by line — how to rig the sorted vector adapter, how to defend the sorted invariant, how to slot the key extractor in — those are all pits waiting in the next piece. Let's get to work in the next one.

## References

- [Chromium `base/containers/flat_map.h`](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/flat_map.h)
- [Chromium `base/containers/flat_tree.h`](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/flat_tree.h)
- [Chromium `base/containers/README.md` — the container selection guide](https://source.chromium.org/chromium/chromium/src/+/main:base/containers/README.md)
- [flat_map prerequisite (0): ordered associative containers and std::map's red-black tree](./pre-00-flat-map-ordered-assoc-container-intro.md)
