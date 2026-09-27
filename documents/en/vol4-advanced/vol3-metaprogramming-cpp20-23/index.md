---
title: "Metaprogramming Essentials (C++20-23)"
description: "C++20/23 metaprogramming: concepts, requires expressions, core TMP techniques, compile-time strings, and C++26 reflection"
translation:
  source: documents/vol4-advanced/vol3-metaprogramming-cpp20-23/index.md
  source_hash: f27b220c270fb377cad9b7cd6b6272a05d95ef737712ccbb3d943d387800936a
  translated_at: '2026-09-26T04:52:18+00:00'
  engine: anthropic
  token_count: 950
---

# Metaprogramming Essentials (C++20-23)

This part picks up where Volume 1's template basics left off. Volume 1 taught the "mechanisms" — the compilation model of templates, specialization, two-phase lookup. This part is about how to use those mechanisms for **compile-time computation and type deduction**, and how C++20 concepts rewrite the drudgery that used to depend on SFINAE and `enable_if` into readable constraints.

Three threads run through it: concepts and requires expressions (C++20), classic template metaprogramming (TMP) techniques and their modernization, and compile-time strings plus C++26 reflection.

The companion runnable examples live at [code/examples/vol4/vol3-metaprogramming-cpp20-23/](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/examples/vol4/vol3-metaprogramming-cpp20-23); every file runs directly with `g++ -std=c++20 xxx.cpp`.

<ChapterNav variant="sub">
  <ChapterLink href="01-concepts">Concepts: Putting Constraints in the Signature</ChapterLink>
  <ChapterLink href="02-constraining-templates">Constraining Templates with Concepts: Subsumption and Overloading</ChapterLink>
  <ChapterLink href="03-requires-expressions">Requires Expressions, In Depth: The Four Kinds of Requirements</ChapterLink>
  <ChapterLink href="04-tmp-core-techniques">TMP Core Techniques: The World Before Concepts</ChapterLink>
  <ChapterLink href="05-compile-time-strings">Compile-Time Strings: NTTP Class Type and fixed_string</ChapterLink>
  <ChapterLink href="06-static-reflection-basics">Static Reflection Basics: The Reflection Operator and Splice Recomposition</ChapterLink>
  <ChapterLink href="07-template-instantiation-control">Template Instantiation Control: extern template and Compile Times</ChapterLink>
  <ChapterLink href="08-templates-and-exception-safety">Templates and Exception Safety: move_if_noexcept and Reallocation</ChapterLink>
  <ChapterLink href="09-mini-stl-with-concepts">Capstone Project: A mini-STL Algorithm Library Constrained by Concepts</ChapterLink>
</ChapterNav>

The concepts trio (01-03) lays the foundation: how to write constraints, how they participate in overloading, and the four kinds of requirements in a requires expression along with its two traps. Piece 04 steps back to the old TMP tricks (the internals of `type_traits`, template recursion, SFINAE, `void_t`, fold expressions) and the migration from SFINAE to concepts; 05 covers compile-time strings (the C++20 NTTP class type and `fixed_string`); 06 takes a look at C++26 static reflection (the reflection operator and splice from P2996); 07 covers template instantiation control (`extern template` and compile time); 08 covers templates and exception safety (`move_if_noexcept` and container reallocation); and finally 09 welds everything before it together with a concepts-constrained mini-STL algorithm library.
