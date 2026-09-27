---
translation:
  source: documents/vol8-domains/data-structure/index.md
  source_hash: a961f63d747ddb0cc111840a26d095acd07e67b5616b6882117d88c7031164b3
  translated_at: '2026-09-25T08:50:43+00:00'
  engine: anthropic
  token_count: 1300
---
# Hand-Rolling mini STL: A Container Library in Practice

We hand-roll a teaching-oriented mini container library of our own (namespace `tamcpp::ministl`): starting from a raw buffer that "manages capacity, not objects", we work our way up to an LRU cache. Each time we finish a piece, we hold it up against a real industrial implementation as a mirror — the dynamic array against `std::vector` and Chromium's `vector_buffer`, the classic linked list against libstdc++'s `stl_list`, hashing against absl's swiss table, and the ring buffer, intrusive containers, and LRU cache against Chromium's `base/containers`. Whichever mirror we borrow, its error handling comes along: the std mirror throws exceptions, the Chromium mirror crashes on the spot.

Prerequisites: you have read the conceptual layer of the vol3 containers volume, you can read template specializations and variadic templates, and ideally you have dealt with placement new at least once. The companion code lives in `code/volumn_codes/vol8-labs/ministl/` and advances stage by stage in separate directories (currently `stage1_rawbuf_vector/`: RawBuffer and Vector); every article's output can be reproduced verbatim in there.

## Prerequisites

- [pre-00 Introduction: why hand-rolling containers is worth it](pre-00-mini-stl-why-handroll.md)

## Hands-On Series

- [01 RawBuffer: capacity, not objects](01-raw-buffer.md) — decoupling memory from object lifetimes, with `placement new` and explicit destruction taking the stage in pairs
- [02 Vector: growth and relocation](02-vector-growth-and-relocation.md) — a minimal two-member layout, the doubling strategy, and amortized analysis
- [03 Vector: the Rule of Five, exception safety, and concepts](03-vector-copy-move-and-concepts.md) — copy-and-swap, move_if_noexcept, and a retrospective on a real bug

We keep pushing this series forward module by module. Upcoming articles will revolve around the ring buffer, linked lists (singly linked, doubly linked, intrusive), ordered and hash maps, heaps, and the LRU cache; each one gets its link added here as soon as it lands.

## Related Content

- [the vol8 algorithms subdomain](../algorithms/index.md): sorting, searching, and the rest of "the algorithms half" belong over there; hand-written container implementations belong over here
- [the vol3 containers volume](../../vol3-standard-library/containers/index.md): the conceptual layer of std containers, the prerequisite for this series
- [the flat_map series](../../vol9-open-source-project-learn/chrome/03_flat_map/index.md): a complete deep dive into sorted-vector containers; the later FlatMap assembly article will stand directly on its shoulders
- [the vol4 mini-STL algorithm library](../../vol4-advanced/vol3-metaprogramming-cpp20-23/09-mini-stl-with-concepts.md): over there it's concepts practice; over here it's systematic hand-rolling
