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
  <ChapterLink href="01-what-is-cmake">What Is CMake — The Two-Stage Pipeline of a Build System Generator</ChapterLink>
  <ChapterLink href="02-target-and-usage-requirements">The Target Mental Model — Treat a Target as an Object, PUBLIC/PRIVATE/INTERFACE Are Usage Requirements</ChapterLink>
  <ChapterLink href="03-find-package-and-cxx-standard">Dependencies and the C++ Standard — Modern Ways to Write find_package and cxx_std_NN</ChapterLink>
  <ChapterLink href="04-cmake-presets">CMakePresets.json: From the cmake -D Old Way to Reproducible --preset</ChapterLink>
</ChapterNav>
