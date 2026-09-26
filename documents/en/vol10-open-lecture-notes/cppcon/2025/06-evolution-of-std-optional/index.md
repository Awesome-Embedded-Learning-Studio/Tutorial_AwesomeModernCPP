---
title: "The Evolution of std::optional: From Boost to C++26"
description: "CppCon 2025 talk notes — Steve Downey on the evolution of std::optional from Boost to C++26, focusing on why the optional reference (P2988) waited twenty years to enter the standard"
conference: cppcon
conference_year: 2025
talk_title: 'The Evolution of std::optional: From Boost to C++26'
speaker: "Steve Downey"
tags:
  - cpp-modern
  - host
  - intermediate
  - optional
difficulty: intermediate
platform: host
cpp_standard: [17, 23, 26]
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/06-evolution-of-std-optional/index.md
  source_hash: bc5fbee2a5810b568dead3ddd002772e98f4812769d0d02ddd64bb8621c6d320
  translated_at: '2026-09-26T00:22:39+00:00'
  engine: anthropic
  token_count: 700
---

<TalkInfoCard
  talkTitle="The Evolution of std::optional: From Boost to C++26"
  speaker="Steve Downey"
  conference="cppcon"
  :year="2025"
/>

These are notes from Steve Downey's (Bloomberg) CppCon 2025 talk. He is the primary author of P2988, the proposal that carried `std::optional<T&>` into C++26. The heart of the talk is one question: a feature that looks like "just a reference that can also be empty" — why was it first proposed in 2005, then held up all the way until the vote at the Sofia meeting in June 2025? The answer runs through the three identities a reference carries in C++, the twenty-year fight between assign-through and rebind, and the final conclusion: "it's just a constrained pointer."

The notes are split into six parts, ordered as "the value-version foundation first, then the reference-version core, then a step back to look at standardization." All the code involving `optional<T&>` was actually run on GCC 16.1.1 (`-std=c++26`) — this is not armchair theory.

## Contents

<ChapterNav variant="sub">
  <ChapterLink href="01-why-optional-reference-took-20-years">Why the optional reference took twenty years</ChapterLink>
  <ChapterLink href="02-value-semantics-of-optional">The Value-Semantics Foundation of std::optional</ChapterLink>
  <ChapterLink href="03-optional-reference-and-assignment">What an optional reference is, and why assignment is always a rebind</ChapterLink>
  <ChapterLink href="04-shallow-traps-const-value-or-dangling">Shallow traps of optional references: const, value_or, and dangling</ChapterLink>
  <ChapterLink href="05-move-semantics-traps">The Move-Semantics Traps Hiding Inside Optional References</ChapterLink>
  <ChapterLink href="06-standardization-and-beman">The Standardization Truth: The Beman Project and a Reference Implementation That Actually Runs</ChapterLink>
</ChapterNav>
