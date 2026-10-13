---
title: "Week 3 · Move Semantics and Ownership"
description: "A std::move special: exclusive ownership with unique_ptr, a zero-copy swap, the moved-from 'valid but unspecified' state, the rule that a handwritten destructor makes move operations vanish, and the SSO fact that moving a short string is actually no faster."
chapter: 16
order: 4
dateRange: "2026-09-28 ~ 10-04"
difficulty: intermediate
platform: host
weeklyThanks:
  - github: Charliechen114514
    role: This week's problem provider · Self-composed problems for the move-semantics special
tags:
  - host
  - cpp-modern
  - intermediate
translation:
  source: documents/weekly-problems/week-03.md
  source_hash: 4d0203effca79dbe13be86799f7d1093b02aa32e1c3cec4f8ebb12530dda2713
  translated_at: '2026-10-09T16:30:00+00:00'
  engine: anthropic
  token_count: 430
---

# Week 3 · Move Semantics and Ownership

The first two rounds had you practicing recursion and language details; this one turns to a core mechanism of modern C++: move semantics. All five problems circle `std::move`, so one thing should be said up front: it moves nothing by itself. It just turns the expression into an rvalue; the move constructor and move assignment do the real work. This week's problems walk that whole chain: why `unique_ptr` can't be copied (ownership exists in exactly one copy), how a swap written with moves gets away with zero copies (that is exactly how `std::swap` is implemented), what a moved-from object has left (the "valid but unspecified" state), when `std::move` does nothing even though you wrote it (the rule that a handwritten destructor suppresses the move constructor). And the headliner asks: is moving really free? Short strings hand you a counterintuitive answer.

The format lineup stays mixed, as before. It opens with a multiple-choice warm-up about what compiles, then an online-judge problem — this time the judge checks your copy count, so let one stray copy sneak into your swap and it fails — then a single-choice free-response problem, then a root-cause hunt. The closer is an ungraded thought problem, left for you to take to Godbolt and see with your own eyes what code the compiler actually generated. Difficulty runs from one star to three, and the card order is the suggested tackling order. The first two rounds drew their problems from old books and friends' idle musings; this week we composed them ourselves around the tutorial's [vol2 Move Semantics chapter](../../vol2-modern-features/ch00-move-semantics/index.md) — our first in-house special.

<QuizProblem src="code/volumn_codes/weekly-problems/week-03/01-unique-ownership" />

<QuizProblem src="code/volumn_codes/weekly-problems/week-03/02-move-swap" />

<QuizProblem src="code/volumn_codes/weekly-problems/week-03/03-moved-from-state" />

<QuizProblem src="code/volumn_codes/weekly-problems/week-03/04-vanished-move" />

<QuizProblem src="code/volumn_codes/weekly-problems/week-03/05-sso-move-cost" />

---

**Where the problems come from**: All five problems this week were composed by [us](https://github.com/Charliechen114514) around the tutorial's [vol2 Move Semantics chapter](../../vol2-modern-features/ch00-move-semantics/index.md). Problems 1 and 2 test points from cppreference's specifications of `std::unique_ptr` and `std::swap` (the copy constructor is deleted; from C++11 on, swap is implemented in terms of moves). Problem 3's wording, "valid but unspecified", comes from the C++ standard library's [lib.types.movedfrom] and lines up with Item 23 of *Effective Modern C++*. Problem 4 turns on C++ Core Guidelines C.21 (the rule of five) and the language rule that a declared destructor stops implicit moves from being generated. Problem 5's SSO (short string optimization) inline capacity — 15 characters in libstdc++ and MSVC, 22 in libc++ — is an implementation fact of the three mainstream standard libraries, taken apart in full in the [Deep Dive into string](../../vol3-standard-library/containers/04-string-memory-deep-dive.md) article. Every judge case and every assertion inside the problems was verified by local testing on two compilers (GCC 16 / Clang 23).
