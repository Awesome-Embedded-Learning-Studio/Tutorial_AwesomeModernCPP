---
title: "Template Basics (C++11-14)"
description: "The core foundations of C++ template programming: a complete introduction from function templates to CRTP"
translation:
  source: documents/vol4-advanced/vol1-basics-cpp11-14/index.md
  source_hash: 6ee1733cd829e5dccd08caf038dcb6e72e15906ea5f6ac06d0f2a00a3aff7411
  translated_at: '2026-09-26T03:58:44+00:00'
  engine: anthropic
  token_count: 280
---

# Template Basics (C++11-14)

C++ templates are the core mechanism of generic programming. This part moves you from "knowing how to use templates" to the perspective of "wanting to write libraries and being able to read STL sources": we thoroughly cover the template compilation model, specialization and partial specialization, non-type parameters, two-phase name lookup, hidden friends, alias templates, and CRTP, and finish with a `fixed_vector<T, N>` capstone project that welds the previous nine pieces together.

The companion runnable examples live at [code/examples/vol4/vol1-basics-cpp11-14/](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/examples/vol4/vol1-basics-cpp11-14): the four with the most reuse value (fixed_vector, CRTP static polymorphism, the Comparable mixin, and hand-written type_traits), where every file runs directly with `g++ -std=c++20 xxx.cpp`.

<ChapterNav variant="sub">
  <ChapterLink href="01-templates-introduction">Templates, From Scratch: A Code Recipe with Placeholders</ChapterLink>
  <ChapterLink href="02-function-templates-deep">Function Templates, In Depth: Compilation Model and the No-Partial-Specialization Trap</ChapterLink>
  <ChapterLink href="03-class-templates">Class Templates: Members, Dependent Names, and Lazy Instantiation</ChapterLink>
  <ChapterLink href="04-specialization-partial">Template Specialization and Partial Specialization: The Art of Pattern Matching</ChapterLink>
  <ChapterLink href="05-non-type-parameters">Non-Type Template Parameters: From Integers to C++20 Floats and Class Types</ChapterLink>
  <ChapterLink href="06-name-lookup-and-adl">Name Lookup and ADL: How Two-Phase Lookup Works</ChapterLink>
  <ChapterLink href="07-friends-and-barton-nackman">Template Friends and Barton-Nackman: The Hidden Friends Trick</ChapterLink>
  <ChapterLink href="08-alias-and-using">Alias Templates and using Declarations: Short Names for Types</ChapterLink>
  <ChapterLink href="09-crtp">CRTP: Static Polymorphism with the Curiously Recurring Template Pattern</ChapterLink>
  <ChapterLink href="10-fixed-vector">Capstone Project: fixed_vector</ChapterLink>
</ChapterNav>
