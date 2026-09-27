---
title: "CMake Fundamentals"
description: "Where CMake fits, the two-stage pipeline, the target mental model, find_package and the C++ standard, and CMakePresets — from copy-pasting to actually understanding"
chapter: 7
order: 0
tags:
  - host
  - cpp-modern
  - intermediate
  - CMake
translation:
  source: documents/vol7-engineering/ch00-cmake-fundamentals/index.md
  source_hash: daabfe08b1d614a6e0e5a6fe9ef3773b689b982b2df4d3d6cd55550783b9f4ef
  translated_at: '2026-09-25T09:21:49+00:00'
  engine: anthropic
  token_count: 200
---

# CMake Fundamentals

This sub-volume starts from "what CMake actually is" and lands on the target mental model, dependency management, and reproducible configuration. The goal is for you to stop copy-pasting `CMakeLists.txt` and instead understand the design intent behind every command.

<ChapterNav variant="sub">
  <ChapterLink href="01-what-is-cmake">What is CMake — the two-stage pipeline of a build system generator</ChapterLink>
  <ChapterLink href="02-target-and-usage-requirements">The target mental model — treat a target as an object, PUBLIC/PRIVATE/INTERFACE are usage requirements</ChapterLink>
  <ChapterLink href="03-find-package-and-cxx-standard">Dependencies and the C++ standard — modern ways to write find_package and cxx_std_NN</ChapterLink>
  <ChapterLink href="04-cmake-presets">CMakePresets.json — from the old cmake -D way to reproducible --preset builds</ChapterLink>
</ChapterNav>
