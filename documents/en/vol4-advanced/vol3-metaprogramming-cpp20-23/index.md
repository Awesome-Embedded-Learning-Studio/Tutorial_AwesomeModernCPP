---
title: "Metaprogramming Essentials (C++20-23)"
description: "How C++20/23 does compile-time computation and type deduction, and how concepts replaced the SFINAE dark arts with readable constraints."
---

# Metaprogramming Essentials (C++20-23)

This part picks up where Volume 1's template basics left off. Volume 1 covered the "mechanism": the compilation model of templates, specialization, two-phase lookup. This part covers how to use those mechanisms for **compile-time computation and type deduction**, and how C++20 concepts rewrote the old SFINAE and `enable_if` drudgery into readable constraints.

Three threads run through it: concepts and requires expressions (C++20), classic template metaprogramming (TMP) techniques and their modernization, and compile-time strings plus C++26 reflection.

Runnable examples live in [code/examples/vol4/vol3-metaprogramming-cpp20-23/](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/examples/vol4/vol3-metaprogramming-cpp20-23). Each file compiles with `g++ -std=c++20 xxx.cpp`, except the reflection examples in piece 06 which need Godbolt's clang-p2996 (the local compilers don't support P2996 yet).

<ChapterNav variant="sub">
  <ChapterLink href="01-concepts">Concepts: Putting Constraints in the Signature</ChapterLink>
  <ChapterLink href="02-constraining-templates">Constraining Templates with Concepts: Subsumption and Overloading</ChapterLink>
  <ChapterLink href="03-requires-expressions">Requires Expressions, In Depth: The Four Kinds</ChapterLink>
  <ChapterLink href="04-tmp-core-techniques">TMP Core Techniques: The World Before Concepts</ChapterLink>
  <ChapterLink href="05-compile-time-strings">Compile-Time Strings: NTTP Class Type and fixed_string</ChapterLink>
  <ChapterLink href="06-static-reflection-basics">Static Reflection Basics: The Reflection Operator and Splice</ChapterLink>
  <ChapterLink href="07-template-instantiation-control">Template Instantiation Control: extern template and Compile Time</ChapterLink>
  <ChapterLink href="08-templates-and-exception-safety">Templates and Exception Safety: move_if_noexcept and Reallocation</ChapterLink>
  <ChapterLink href="09-mini-stl-with-concepts">Comprehensive Project: A mini-STL Algorithm Library with Concepts</ChapterLink>
</ChapterNav>

The concepts trio (01-03) is the foundation, covering how to write constraints, how they participate in overloading, and the four kinds of requires expressions with their two traps. Piece 04 steps back to TMP's old toolkit (the internals of `type_traits`, template recursion, SFINAE, `void_t`, fold expressions) and the migration of SFINAE onto concepts. Piece 05 covers compile-time strings (C++20 NTTP class type and `fixed_string`). Piece 06 looks ahead at C++26 static reflection (P2996's reflection operator and splice). Piece 07 covers template instantiation control (`extern template` and compile time). Piece 08 covers templates and exception safety (`move_if_noexcept` and container reallocation). Piece 09 closes with a concepts-constrained mini-STL algorithm library that welds the volume together.
