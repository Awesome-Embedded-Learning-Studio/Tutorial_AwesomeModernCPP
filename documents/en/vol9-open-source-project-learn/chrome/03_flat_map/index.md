# flat_map: ordered-container design, learned from Chromium

This directory takes apart Chromium's `flat_map` / `flat_tree` and works through the industrial-strength design of implementing an associative container on a sorted vector: why an array beats a tree at small N, the read-heavy write-light home turf, the zero-cost sorted_unique construction, transparent comparators, and EBO. It sits alongside [OnceCallback](../01_once_callback/) and [WeakPtr](../02_weak_ptr/), rounding out the container-and-performance piece of vol9/chrome.

## Full tutorial (full/)

Prerequisites (6 chapters):

- [ordered associative containers, introduced](./full/pre-00-flat-map-ordered-assoc-container-intro.md)
- [vector internals and growth](./full/pre-01-flat-map-vector-internals-and-growth.md)
- [complexity and amortization](./full/pre-02-flat-map-complexity-and-amortized.md)
- [comparators and transparent lookup](./full/pre-03-flat-map-comparator-and-transparent.md)
- [tag dispatch and sorted_unique](./full/pre-04-flat-map-tag-dispatch-and-sorted-unique.md)
- [no_unique_address, EBO, and pair storage](./full/pre-05-flat-map-enua-ebo-and-pair-storage.md)

Hands-on (6 chapters):

- [motivation and API design](./full/03-1-flat-map-motivation-and-api-design.md)
- [the flat_tree skeleton](./full/03-2-flat-map-flattree-skeleton.md)
- [lookup and insert](./full/03-3-flat-map-lookup-and-insert.md)
- [sorted_unique construction](./full/03-4-flat-map-sorted-unique-construction.md)
- [iterator invalidation and bulk build](./full/03-5-flat-map-iterator-invalidation-and-bulk-build.md)
- [testing and performance](./full/03-6-flat-map-testing-and-perf.md)

## Hands-on design guide (hands_on/)

For readers comfortable with templates and performance:

- [design](./hands_on/01-flat-map-design.md)
- [implementation](./hands_on/02-flat-map-implementation.md)
- [testing and performance](./hands_on/03-flat-map-testing.md)
