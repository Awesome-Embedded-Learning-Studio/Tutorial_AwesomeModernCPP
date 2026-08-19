---
title: "The Evolution of std::optional: From Boost to C++26"
description: "CppCon 2025 notes — Steve Downey on how std::optional evolved from Boost to C++26, focused on why optional&lt;T&> (P2988) waited twenty years to enter the standard"
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
---

<TalkInfoCard
  talkTitle="The Evolution of std::optional: From Boost to C++26"
  speaker="Steve Downey"
  conference="cppcon"
  :year="2025"
/>

These are notes from Steve Downey's CppCon 2025 talk. He is the main author of P2988, the proposal that pushed `std::optional<T&>` into C++26. The talk answers a single question: a feature that looks like "just an optional that holds a reference" — why did it take from its first proposal in 2005 until the Sofia meeting in June 2025 to finally pass? The answer runs through the three identities a reference has in C++, the twenty-year assign-through vs. rebind debate, and the final conclusion that "it is, essentially, a constrained pointer."

The notes are split into six parts, moving from the value-version foundation into the reference-version core, then stepping back to look at standardization. All code involving `optional<T&>` was run on GCC 16.1.1 (`-std=c++26`) — this is not paper talk.

## Contents

<ChapterNav variant="sub">
  <ChapterLink href="01-why-optional-reference-took-20-years">Why the optional reference took twenty years</ChapterLink>
  <ChapterLink href="02-value-semantics-of-optional">The Value-Semantics Foundation of std::optional</ChapterLink>
  <ChapterLink href="03-optional-reference-and-assignment">What an optional reference is, and why assignment is always a rebind</ChapterLink>
  <ChapterLink href="04-shallow-traps-const-value-or-dangling">Shallow traps of optional references: const, value_or, and dangling</ChapterLink>
  <ChapterLink href="05-move-semantics-traps">The Move-Semantics Traps Hiding Inside Optional References</ChapterLink>
  <ChapterLink href="06-standardization-and-beman">The Standardization Truth: The Beman Project and a Reference Implementation That Actually Runs</ChapterLink>
</ChapterNav>
