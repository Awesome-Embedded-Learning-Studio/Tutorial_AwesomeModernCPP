---
title: "Modern Template Techniques (C++17)"
description: "C++17's modern tools for template engineering: if constexpr, variadic templates, perfect forwarding, CTAD, and a type-erasure capstone."
---

# Modern Template Techniques (C++17)

Volume 1 covered the "mechanism" of templates: the compilation model, specialization, CRTP. This part covers the modern tools C++17 brought to template engineering. `if constexpr` lets a template dispatch by type without a pile of overloads, variadic templates handle any number of arguments, perfect forwarding lets a generic factory pass arguments through untouched, and CTAD saves you from writing out every angle bracket. A type-safe `any` at the end welds these together.

One thing to flag up front: the old TMP toolkit (type traits, SFINAE, `void_t`, fold expressions) lives in the metaprogramming sub-volume, in the "TMP Core Techniques" piece (it's the prelude to concepts). This part doesn't repeat it.

Runnable examples live in [code/examples/vol4/vol2-modern-cpp17/](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/examples/vol4/vol2-modern-cpp17). Each file compiles with `g++ -std=c++17 xxx.cpp`.

<ChapterNav variant="sub">
  <ChapterLink href="01-if-constexpr">if constexpr: Compile-Time Branching</ChapterLink>
  <ChapterLink href="02-variadic-templates">Variadic Templates: Expanding Parameter Packs</ChapterLink>
  <ChapterLink href="03-perfect-forwarding">Perfect Forwarding: Forwarding References and Reference Collapsing</ChapterLink>
  <ChapterLink href="04-ctad">CTAD: Class Template Argument Deduction</ChapterLink>
  <ChapterLink href="05-type-safe-any">Capstone Project: A Type-Safe any</ChapterLink>
  <ChapterLink href="06-designated-initializers">Designated Initializers</ChapterLink>
  <ChapterLink href="07-ranges-basics-and-views">C++20 Ranges Library Basics and Views</ChapterLink>
  <ChapterLink href="08-ranges-pipeline-in-practice">Pipeline Operations and Ranges in Practice</ChapterLink>
</ChapterNav>

Pieces 01 through 05 are the spine of the C++17 template line. `if constexpr` opens, on why compile-time branching replaces a stack of overloads; 02 takes apart the expansion of parameter packs (recursion, `if constexpr` termination, and fold, compared side by side); 03 covers perfect forwarding and reference collapsing, machinery no generic factory can avoid; 04 is CTAD, how the compiler back-infers template arguments from constructor arguments; 05 closes with a hand-written type-safe `any` that ties the previous four together. Pieces 06 through 08 are a few other modern features in this volume (designated initializers and Ranges); they're earlier in style and don't quite match 01 through 05, left for a later cleanup pass.
