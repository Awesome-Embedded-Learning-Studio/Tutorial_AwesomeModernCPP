---
title: "The Standardization Truth: The Beman Project and a Reference Implementation That Actually Runs"
description: 'CppCon 2025 Notes — assembly evidence for optional<T&> assignment, why The Beman Project reference implementation matters, the big-and-complete vs small-and-focused tradeoff, and why optional<T> and optional<T&> must be one coherent whole'
chapter: 6
order: 6
conference: cppcon
conference_year: 2025
talk_title: 'The Evolution of std::optional: From Boost to C++26'
speaker: Steve Downey
cpp_standard: [17, 23, 26]
difficulty: intermediate
platform: host
reading_time_minutes: 15
tags:
  - cpp-modern
  - host
  - intermediate
  - optional
related:
  - "The Move-Semantics Traps Hiding Inside Optional References"
  - "Why the optional reference took twenty years"
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/06-evolution-of-std-optional/06-standardization-and-beman.md
  source_hash: 57958397e6324f7425fcafd3fc3c8cd1da7741d1bb022198416530deff44bf82
  translated_at: '2026-07-29T00:33:58.679864+00:00'
  engine: anthropic
  token_count: 3237
---

# The Standardization Truth: The Beman Project and a Reference Implementation That Actually Runs

[The previous piece](./05-move-semantics-traps.md) walked through the move-semantics traps. This one steps back to look at the part of the talk that excited me the most: how standardization actually works, and the role The Beman Project plays in it. Then we will tie the whole thread together.

## First, Let the Assembly Prove One Thing: Assignment Is a Pointer Copy

The last few pieces kept repeating that `optional<T&>` assignment is equivalent to a pointer copy. Talk is cheap, so let us look at the assembly. Write a function that does a single `optional<int&>` assignment and compile it at `-O2`:

```cpp
// assign_codegen.cpp
#include <optional>
void assign_opt(std::optional<int&>& a, const std::optional<int&>& b) {
    a = b;
}
```

```bash
g++ -std=c++26 -O2 -S assign_codegen.cpp -o assign_codegen.s
```

Pull out the body of `assign_opt`:

```asm
_Z10assign_optRSt8optionalIRiERKS1_:
        movq    (%rsi), %rax      # read 8 bytes from b, just a pointer
        movq    %rax, (%rdi)      # write to a
        ret
```

Two `movq` instructions: read a pointer from `b`, write it to `a`, return. No function calls, no branches. That is exactly what Steve Downey meant when he said "assignment is done through a single function, and from the compiler's optimization standpoint it is very transparent." When the standard pins the semantics down to something this simple, implementers have room to push code generation to the limit — the compiler can inline, do dead-code elimination, all the things it is good at.

:::warning
One thing to flag. Dereferencing an empty `optional<T&>` has the same consequence as dereferencing a null pointer: undefined behavior, and the compiler is not obliged to warn you. Do not assume optional will protect you. It only makes the "is there a value?" information explicit; if you dereference without checking, it crashes just the same. The terrible problems with null and invalid pointers mentioned in the previous talk apply here unchanged.
:::

## What The Beman Project Actually Solves

Here comes the part of the talk that excited me the most.

The Beman Project was launched at CppNow 2024, and its name comes from Beman Dawes, one of the founders of Boost. At first I thought it was a compiler-optimization project; only later did I realize it is a reference-implementation project. For components currently being proposed for the C++ standard library, it provides a textbook-grade clean implementation.

You might think this is no big deal — libstdc++, libc++, and MSVC STL all have standard-library implementations, so can you not just pick one and read it?

You cannot. There is a key distinction I had completely missed before: vendor implementations are stuffed full of historical baggage.

Take an example from my own experience. I once wanted to understand the exact behavior of a particular `std::optional` constructor, so I went digging through the libstdc++ source, only to find a bunch of constructor overloads I had never even seen. Some were there for compatibility with older standards, some to cooperate with specific compiler extensions, and some had comments along the lines of "this might not be needed but removing it breaks some internal ABI." I was bewildered — which one am I supposed to look at? Which one is "the one the standard talks about"?

That is exactly what The Beman Project sets out to fix. It aims to be a clean implementation that contains only what the standard proposal specifies — no historical baggage, no vendor-specific extras, none of the "mistakes we made back then and now do not dare remove." LEWG (the Library Evolution Working Group) has stated plainly that this implementation will carry only one set of names: the ones the standard ultimately designates.

## Why "Having Something That Runs" Matters So Much

This part, I think, is the most valuable thing in the whole talk. Steve Downey said something to the effect that he is not smart enough to write standard wording without an actual implementation to refer to.

Can you believe that? Someone who writes standard proposals admitting he is not smart enough? But think about it and the remark holds up. I run into the same thing writing technical docs: I think I have it figured out, the prose reads well, and the moment I sit down to write the code I find an edge case I never considered here, an interaction I did not anticipate there.

Standard wording is the same. You think you can mark a function `const`, it looks completely reasonable on paper, and the moment you implement it five test cases break. The scenarios those five tests cover might be ones you could never have imagined while writing the proposal — a combination with some other standard component, say, or a deduction result in some particular template context.

Now what if those discoveries happen after the proposal is accepted and the standard is published? Then it is painful: you have to go through another full meeting cycle to fix it, and that can take years. But if you catch them in the reference implementation? Just change it, run the tests, and in five minutes you know whether it works. Steve Downey described marking something const and having five tests fail — at which point he knew the idea was not that great, or conversely that the idea was fine and the tests themselves were the problem. Either way, that beats going through a whole meeting cycle and coming back with "oh, by the way, that thing you asked me to do — turns out it doesn't work."

He gave a good example. At the Tokyo meeting someone proposed making optional a range. Is that proposal feasible or not? You do not settle it by talking, or by drawing type-deduction trees on a whiteboard. His approach was to add the proposed functions into the Beman implementation, pull out a batch of tests originally written for handling ranges of zero or one element, and run them — all green. That shifted the discussion from "can this even work?" to "do we actually want to do this?" The technical obstacle is removed, and what is left is a question of design taste. This way of working is vastly more efficient than arguing back and forth on a mailing list for a hundred rounds.

:::warning
This also means code written against The Beman Project will have an unstable ABI. Code that compiles today might not compile next month, because the committee might rename a function, mark a parameter const, or flag a constructor explicit. That is not a bug — it is the point. The whole idea is to stress-test the correctness of the standard wording through exactly this kind of churn. So Beman is positioned as a standardization reference, not a stable library for you to depend on in production.
:::

## Big-and-Complete, or Small-and-Focused

Steve Downey noted that the C++ standard library tends to provide one "big and complete" thing, rather than offering several similar-but-different types for you to choose among, the way other language ecosystems do. Once optional becomes a range, the standard ends up with a single "optional-like thing," instead of two or three close cousins each optimized for a different use case. This follows the same philosophy as `std::string`: cram every feature into one interface so you never have to agonize over "should I use this one or that one."

I am ambivalent about this approach. On one hand I understand it — fewer choices really does reduce cognitive load, especially for newcomers. On the other hand, the lessons of `std::string` are right there: the interface is bloated, a lot of the functions have dubious value, and because it has to serve every use case, none of them get an optimal result.

How do other languages handle this? Rust has `Option<T>` along with all kinds of zero-cost iterator adapters; Go leans on its "zero value" philosophy and does not need optional at all; Swift has `Optional`, but its language-level integration is entirely different from C++'s. Every ecosystem makes its own tradeoff, and C++'s tradeoff is "give you one do-everything type," at the cost of "this do-everything type is probably not optimal in any particular scenario." As a user, you at least need to be aware the tradeoff exists.

## optional\<T> and optional\<T&> Must Be One Coherent Whole

Having covered The Beman Project, let us look at a deeper question, one I had not appreciated at first: if `optional<T&>` makes it into the standard library, it has to interoperate seamlessly with the existing `optional<T>`.

The most typical scenario is monadic operations (`transform`, `and_then`, and friends — P0798, since C++23). Suppose you have an `optional<string>`, you call `transform` on it, and the function you pass in returns `optional<string&>`. What happens then? You would hope for automatic flattening, but if `optional<T>` and `optional<T&>` are implemented as separate halves that do not know about each other, you most likely get back a nested optional and have to flatten it by hand — deeply unintuitive.

The popular polyfill library `tl::optional` does not have this problem, because it does not support `optional<T&>` at all, so its monadic functions never have to handle this cross-type interaction. But the moment it enters the standard library, `optional<T>` and `optional<T&>` have to form a coherent set (Steve Downey's phrase) — they have to know about each other and do the right thing across type conversions, monadic operations, and comparison operators in every scenario. That is also why you cannot simply patch the existing optional: you have to unify the `T` and `T&` specializations at the design level.

A quick word on where `reference_wrapper` stands. It is not going away; it is still useful in tuple and bind scenarios, it is already in the standard library, and people surely depend on it. But it is not, and should not be, the answer to "optional reference." It was originally built as an adapter for tuple's DSL, its API has quirks, the implicit conversion to `T&` it carries will bite you in overload-resolution scenarios, and it does not care about dangling at all. `optional<T&>` is a brand-new design driven from semantics, not something you can get by patching up `reference_wrapper`. The underlying storage might look similar — both are essentially a pointer — but the semantics and safety guarantees are on completely different levels, the same way you might ask, if we already have raw pointers, why do we need `unique_ptr`?

## Closing the Thread

Let us pull the whole thread together. `std::optional` was proposed in 2005; the value version entered C++17 in 2017; the reference version was shelved because of the assign-through versus rebind quarrel; a lot of detours were taken along the way; and finally P2988 was voted through at the 2025 Sofia meeting and entered C++26. Under the hood it is just a constrained pointer — assignment always rebinds, const is shallow, value_or always returns a value, and `operator*` does not propagate moves for the reference version.

Each of these decisions looks a little counterintuitive on its own, but as long as you remember one thing: we are modeling a pointer. Assignment changes what the pointer points at, const only locks the pointer itself, and moves do not pierce through to the pointed-to object. Transplant the rules for pointers, mind a few edge cases, and the whole of `optional<T&>` falls into place.

As for why it took twenty years — it was not that it could not be done technically. It is that the semantics of those edge cases are too easy to get wrong, and they needed a carefully designed proposal to straighten out every detail, plus a reference implementation that actually runs — The Beman Project — to verify that the standard wording really holds up. Standardization is not a bunch of people arguing in a conference room and then throwing a document over the wall. It is the loop of "think it through, write it down, implement it and run it, change it if it does not work." Such a plain truth, dropped into something as lofty as standardization, turns out to be the most effective approach there is.
