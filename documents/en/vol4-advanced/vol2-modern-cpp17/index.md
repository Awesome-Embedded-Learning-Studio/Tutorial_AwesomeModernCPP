---
title: "Modern Template Techniques (C++17)"
description: "The modern tools C++17 brings to template engineering: if constexpr, variadic templates, perfect forwarding, CTAD, and a type-erasure capstone project"
translation:
  source: documents/vol4-advanced/vol2-modern-cpp17/index.md
  source_hash: 13f38c5046e5a7de080e42b6d8fd6492622275b09af86f5091d0a0acd0e98187
  translated_at: '2026-09-26T03:07:37+00:00'
  engine: anthropic
  token_count: 450
---

# Modern Template Techniques (C++17)

Volume 1 covered the "mechanics" of templates: the compilation model, specialization, CRTP. This part picks up from there with the modern tools C++17 brought to template engineering: `if constexpr` lets a template dispatch by type without leaning on a stack of overloads, variadic templates handle any number of arguments, perfect forwarding lets a generic factory pass arguments through exactly as they arrived, and CTAD saves you from typing out a string of angle brackets. At the end, a type-safe `any` welds these tools together.

One arrangement to flag up front: the classic TMP tricks — type traits, SFINAE, `void_t`, fold expressions — live in the vol3 metaprogramming sub-volume, in the "TMP Core Techniques" piece (the forerunner of concepts), and this part doesn't repeat them.

The companion runnable examples live at [code/examples/vol4/vol2-modern-cpp17/](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/examples/vol4/vol2-modern-cpp17); every file runs directly with `g++ -std=c++17 xxx.cpp`.

<ChapterNav variant="sub">
  <ChapterLink href="01-if-constexpr">if constexpr: Compile-Time Branching</ChapterLink>
  <ChapterLink href="02-variadic-templates">Variadic Templates: Expanding Parameter Packs</ChapterLink>
  <ChapterLink href="03-perfect-forwarding">Perfect Forwarding: Forwarding References and Reference Collapsing</ChapterLink>
  <ChapterLink href="04-ctad">CTAD: Class Template Argument Deduction</ChapterLink>
  <ChapterLink href="05-type-safe-any">Capstone Project: A Type-Safe any</ChapterLink>
  <ChapterLink href="06-designated-initializers">Designated Initializers</ChapterLink>
  <ChapterLink href="07-ranges-basics-and-views">C++20 Ranges: Ranges and Views</ChapterLink>
  <ChapterLink href="08-ranges-pipeline-in-practice">C++20 Ranges: Pipelines in Practice</ChapterLink>
</ChapterNav>

Pieces 01 through 05 are the spine of the C++17 template line: 01 opens with `if constexpr`, making clear why compile-time branching can replace a stack of overloads; 02 takes the parameter-pack expansion mechanism apart (recursion, `if constexpr` termination, and fold expressions, three styles compared side by side); 03 covers perfect forwarding and reference collapsing — machinery no generic factory can dodge; 04 is CTAD, how the compiler back-deduces template arguments from constructor arguments; 05 ties the previous four together with a hand-written type-safe `any`. Pieces 06 through 08 are a few other modern features in this volume (designated initializers and Ranges); they are earlier in style and don't fully match how 01 through 05 are written — a cleanup left for later.
