---
title: "Cache-Friendly C++"
description: "CppCon 2025 notes — Jonathan Müller on CPU cache mechanics and cache-friendly C++ code, with GCC 16.1.1 measurements run locally: how vector beats unordered_set, memory is 100x slower, shrinking types is not always faster"
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
---

<TalkInfoCard
  talkTitle="Cache-Friendly C++"
  speaker="Jonathan Müller"
  conference="cppcon"
  :year="2025"
  videoYoutube="https://www.youtube.com/watch?v=g_X5g3xw43Q"
/>

These are notes from Jonathan Müller's CppCon 2025 talk. Jonathan has long worked on low-latency C++ (at think-Cell at the time of the talk, later at LSEG on HFT market-data feeds). He starts from a counter-intuitive phenomenon that raises everyone's blood pressure: `std::unordered_set` lookup is O(1), `std::vector` linear lookup is O(n), yet on a real machine, at small sizes vector beats unordered_set. There is only one direction for the answer: the CPU cache.

This talk is the sister of [the microbenchmark one](../07-microbenchmarks-that-lie/). [Part three](../07-microbenchmarks-that-lie/03-branch-prediction-cheats.md) of that series already pointed at the branch predictor and cache conspiring, but did not expand on the cache itself. This talk takes the cache apart from the ground up: why it exists, the unit it moves data in, how much each level costs, and how every decision you make in C++ — picking a container, a data type, a struct layout — lands on cache behavior.

The notes are split into four parts, moving from "breaking the complexity religion" through "building cache intuition" to "landing it in code." All experiments were run on the same machine: **Arch Linux / WSL2, AMD Ryzen 7 9700X (Zen 5), GCC 16.1.1, `-std=c++20`**, with cache hierarchy L1d 48 KiB/core, L2 1 MiB/core, L3 32 MiB shared. Your numbers will differ; the direction of the conclusions will not.

## Contents

<ChapterNav variant="sub">
  <ChapterLink href="01-complexity-is-not-everything">Complexity Is Not Everything: How O(1) Lost to O(n)</ChapterLink>
  <ChapterLink href="02-memory-is-100x-slower">Memory Is 100x Slower — For Real: The Cache Hierarchy and Cache Lines</ChapterLink>
  <ChapterLink href="03-data-types-and-cache">Data Types Are a Cache Variable Too: Shrinking the Type Isn't Always Faster</ChapterLink>
  <ChapterLink href="04-writing-cache-friendly-code">Writing Cache-Friendly Code: Layout, Alignment, and Decisions</ChapterLink>
</ChapterNav>
