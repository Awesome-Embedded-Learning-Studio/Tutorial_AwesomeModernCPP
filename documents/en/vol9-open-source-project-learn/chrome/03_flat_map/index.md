---
translation:
  source: documents/vol9-open-source-project-learn/chrome/03_flat_map/index.md
  source_hash: bb86f7225e46d191243143ad58cf5470db6ef5b9c27e5137135cb1bb691990ea
  translated_at: '2026-09-26T03:07:35+00:00'
  engine: anthropic
  token_count: 700
---
# flat_map: ordered-container design lessons from Chromium

This directory takes apart Chromium's `flat_map` / `flat_tree` and works through the industrial-grade design of implementing an associative container on a sorted vector: why an array beats a tree at small data sizes, the read-heavy write-light workloads that are its home turf, the zero-cost sorted_unique construction, transparent comparators, and EBO. It is the sister series to [OnceCallback](../01_once_callback/) and [WeakPtr](../02_weak_ptr/), rounding out the container-and-performance dimension of vol9/chrome.

## Complete tutorial (full/)

Prerequisites (6 articles):

- [flat_map prerequisite (0): ordered associative containers and std::map's red-black tree](./full/pre-00-flat-map-ordered-assoc-container-intro.md)
- [flat_map prerequisite (I): std::vector internals and growth](./full/pre-01-flat-map-vector-internals-and-growth.md)
- [flat_map prerequisite (II): complexity and amortized analysis](./full/pre-02-flat-map-complexity-and-amortized.md)
- [flat_map prerequisite (III): comparators, strict_weak_order, and transparent lookup](./full/pre-03-flat-map-comparator-and-transparent.md)
- [flat_map prerequisite (IV): tag dispatch and sorted_unique_t](./full/pre-04-flat-map-tag-dispatch-and-sorted-unique.md)
- [flat_map prerequisite (V): NO_UNIQUE_ADDRESS, EBO, and pair storage](./full/pre-05-flat-map-enua-ebo-and-pair-storage.md)

Hands-on practice (6 articles):

- [flat_map hands-on (I): motivation and API design](./full/03-1-flat-map-motivation-and-api-design.md)
- [flat_map hands-on (II): the flat_tree core skeleton](./full/03-2-flat-map-flattree-skeleton.md)
- [flat_map hands-on (III): lookup and insert](./full/03-3-flat-map-lookup-and-insert.md)
- [flat_map hands-on (IV): sorted_unique construction optimization](./full/03-4-flat-map-sorted-unique-construction.md)
- [flat_map hands-on (V): iterator invalidation and bulk construction](./full/03-5-flat-map-iterator-invalidation-and-bulk-build.md)
- [flat_map hands-on (VI): testing and performance comparison](./full/03-6-flat-map-testing-and-perf.md)

## Advanced design guide (hands_on/)

Aimed at readers with template and performance experience:

- [flat_map Design Guide (I): motivation, API, and the flat_tree architecture](./hands_on/01-flat-map-design.md)
- [flat_map Design Guide (II): step-by-step implementation](./hands_on/02-flat-map-implementation.md)
- [flat_map Design Guide (III): test strategy and performance comparison](./hands_on/03-flat-map-testing.md)
