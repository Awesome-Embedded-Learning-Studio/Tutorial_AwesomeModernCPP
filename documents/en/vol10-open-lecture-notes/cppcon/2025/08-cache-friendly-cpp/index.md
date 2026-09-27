---
title: "Cache-Friendly C++"
description: "CppCon 2025 talk notes — Jonathan Müller on CPU cache mechanics and cache-friendly C++ code, with GCC 16.1.1 measurements run locally: how a vector crushes an unordered_set, memory access being 100x slower, and shrinking a type not always being faster"
conference: cppcon
conference_year: 2025
talk_title: 'Cache-Friendly C++'
speaker: "Jonathan Müller"
tags:
  - cpp-modern
  - host
  - intermediate
  - 优化
difficulty: intermediate
platform: host
cpp_standard: [20]
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/08-cache-friendly-cpp/index.md
  source_hash: 9ea0ef0c08b8b6e60e782aca8fe3eb47f269ad08ab75fa1f96561fe7461054bf
  translated_at: '2026-09-26T00:22:22+00:00'
  engine: anthropic
  token_count: 1100
---

<TalkInfoCard
  talkTitle="Cache-Friendly C++"
  speaker="Jonathan Müller"
  conference="cppcon"
  :year="2025"
  videoYoutube="https://www.youtube.com/watch?v=g_X5g3xw43Q"
/>

These are notes from Jonathan Müller's CppCon 2025 talk. Jonathan has spent years in low-latency C++ (at think-Cell when the talk was given, later at LSEG working on HFT market-data feeds), and this time he opens with a counter-intuitive phenomenon that sends quite a few blood pressures soaring: `std::unordered_set` lookup is O(1), `std::vector` linear search is O(n), yet run them on a real machine and, at small data volumes, vector ends up crushing unordered_set. There is only one direction the answer can point in: the CPU cache.

This talk is a companion piece to [the microbenchmark one](../07-microbenchmarks-that-lie/). [Part three](../07-microbenchmarks-that-lie/03-branch-prediction-cheats.md) of that series already pointed at the branch predictor and the cache conspiring to cheat, but never expanded on the cache itself. This talk takes the cache apart from the roots: why it exists, what unit it moves data in, how much latency differs level to level, and how every decision you make in C++ — which container you pick, which data type, how you lay out a struct — lands on cache behavior.

The notes are split into four parts, following the arc of "first break the complexity superstition, then build cache intuition, and finally land it in code." All experiments were run on the same machine: **Arch Linux / WSL2, AMD Ryzen 7 9700X (Zen 5), GCC 16.1.1, `-std=c++20`**, with a cache hierarchy of L1d 48 KiB per core, L2 1 MiB per core, and a shared 32 MiB L3. The numbers you get will be different, but the direction of the conclusions is the same.

## Contents

<ChapterNav variant="sub">
  <ChapterLink href="01-complexity-is-not-everything">Complexity Is Not Everything: How O(1) Lost to O(n)</ChapterLink>
  <ChapterLink href="02-memory-is-100x-slower">Memory Is 100x Slower — For Real: The Cache Hierarchy and Cache Lines</ChapterLink>
  <ChapterLink href="03-data-types-and-cache">Data Types Are a Cache Variable Too: Shrinking the Type Isn't Always Faster</ChapterLink>
  <ChapterLink href="04-writing-cache-friendly-code">Writing Cache-Friendly Code: Layout, Alignment, and Decisions</ChapterLink>
</ChapterNav>
