---
title: "Template Basics (C++11-14)"
description: "The core foundation of C++ template programming: a complete introduction from function templates to CRTP."
---

# Template Basics (C++11-14)

C++ templates are the core mechanism of generic programming. This part moves from "using templates" to "writing libraries, reading STL source," covering the compilation model, specialization and partial specialization, non-type parameters, two-phase name lookup, hidden friends, alias templates, and CRTP, finally welding the first nine pieces together with a `fixed_vector<T, N>` project.

Every piece ships with "run it and see" real compile output, traps written up as `::: warning` blocks, and key compile-time behavior backed by assembly.

Runnable examples live in [code/examples/vol4/vol1-basics-cpp11-14/](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/examples/vol4/vol1-basics-cpp11-14): the four most reusable ones (fixed_vector, CRTP static polymorphism, the Comparable mixin, and type_traits from scratch), each compiles with `g++ -std=c++20 xxx.cpp`.

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
  <ChapterLink href="10-fixed-vector">Project: fixed_vector</ChapterLink>
</ChapterNav>
