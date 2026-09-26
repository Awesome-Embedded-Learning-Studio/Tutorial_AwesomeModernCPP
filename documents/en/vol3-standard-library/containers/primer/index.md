---
title: "Data Structures Primer: The Structures Underneath the Containers"
sidebar_order: 0
translation:
  source: documents/vol3-standard-library/containers/primer/index.md
  source_hash: ef88c2a28f31448ae41bda5df86dd9f6ed8043d76a2c0edab2e3a43f5e28d206
  translated_at: '2026-09-25T09:06:29+00:00'
  engine: anthropic
  token_count: 450
---

# Data Structures Primer: The Structures Underneath the Containers

This volume goes straight for the memory layouts and invalidation rules of the STL containers — but containers don't appear out of thin air: behind `vector` sits a dynamic array, behind `map` a red-black tree, behind `unordered_map` a hash table. If you've never laid hands on these structures themselves, reading the implementation-level articles is like touring the second floor of a building before you've ever seen its foundation. This sub-series sorts them out one by one: each piece covers exactly one structure — which problem it solves, where the costs hide — with evidence from real runs and diagrams; once a structure has had its turn, we hand it back to the corresponding container deep dive and to the hand-rolling practice.

<ChapterNav variant="sub">
  <ChapterLink href="01-dynamic-array">Dynamic Arrays: A Block of Memory That Moves House</ChapterLink>
  <ChapterLink href="02-linked-list">Linked Lists: Never Move House — the Cost Is Asking the Way</ChapterLink>
</ChapterNav>

## Related Content

- [the vol3 containers volume](../index.md): the conceptual layer over the std containers, downstream of this series
- [the vol8 mini STL series](../../../vol8-domains/data-structure/index.md): hand-roll the containers with your own hands — the hands-on extension of this series
