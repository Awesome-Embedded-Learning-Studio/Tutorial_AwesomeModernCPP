---
title: "The performance cost of C++ abstractions"
description: "ch06 is the performance mirror of vol3's 'why components are designed this way' — what happens on hardware once you use them. Measured one by one: virtual functions (devirtualization often makes them free), exceptions (the zero-cost model, free on the normal path / throwing costs 3400x), std::function (SBO and heap allocation), a cost cheat sheet (sizeof plus three supplements), and RVO/NRVO with move (return by value at zero cost). The thesis: there are no zero-cost abstractions, but measure first, then optimize"
---

# The performance cost of C++ abstractions

This chapter is vol3's **performance mirror**. vol3 covers "why components like vector/string/function are **designed the way they are**" (design motivation); this ch06 chapter covers "**after you use them, what happens on hardware that makes things fast or slow**". The thesis is Carruth's *There Are No Zero-Cost Abstractions*: **no zero-cost abstractions**, every C++ abstraction maps to a hardware cost.

But the chapter has a consistent contrarian spirit: "has a cost" **does not equal** "happens every time". The compiler often eliminates the cost for you: devirtualization turns virtual calls into direct calls, the zero-cost model keeps the normal path of exceptions free, and RVO makes return-by-value zero-copy. So the advice that runs through this chapter is repeatedly "**measure first, don't hand-write around it prematurely**".

Five articles:

- **06-01 Virtual functions and devirtualization**: a virtual call through a pointer is 0.55ns (2.5x CRTP), but the compiler often devirtualizes. Don't CRTP-ify prematurely.
- **06-02 The zero-cost model of exceptions**: the normal path is 0.25ns (zero cost nailed down), throwing is 857ns (3400x). Reserve exceptions for genuinely exceptional cases.
- **06-03 std::function's SBO**: the call is 6x slower, construction takes the SBO path when small and heap-allocates when big (8.5x). Watch out for repeated construction plus a big capture on the hot path.
- **06-04 The cost cheat sheet**: a roll-up plus variable storage types, bitfields, the zero cost of enum class, and a sizeof table.
- **06-05 RVO, NRVO, and move**: return by value is zero-copy and zero-move (verified with the copy/move counter method); `return std::move(local)` is an anti-pattern.

> Boundary: the **design mechanism** of components (vector's three pointers, SSO implementation, EBO) belongs to vol3/vol4; vol6 only covers "the cost of running them on hardware". High-frequency Zhihu questions (are virtual functions slow / are exceptions slow / function heap allocation / the move counter-example / return std::move) feed into each article's entry point, none get a standalone article.
