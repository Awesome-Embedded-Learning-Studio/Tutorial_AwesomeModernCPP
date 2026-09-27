---
title: "The Standardization Truth: The Beman Project and a Reference Implementation That Actually Runs"
description: "CppCon 2025 notes — assembly evidence for optional<T&> assignment, why The Beman Project reference implementation matters, the big-and-complete vs small-and-focused tradeoff, and why optional<T> and optional<T&> must be one coherent whole"
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
  - "Why the Optional Reference Took Twenty Years of Wrangling"
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/06-evolution-of-std-optional/06-standardization-and-beman.md
  source_hash: 57958397e6324f7425fcafd3fc3c8cd1da7741d1bb022198416530deff44bf82
  translated_at: '2026-09-26T16:29:53+00:00'
  engine: anthropic
  token_count: 3200
---

# The Standardization Truth: The Beman Project and a Reference Implementation That Actually Runs

[The previous article](./05-move-semantics-traps.md) finished covering the move-semantics traps. This one steps back to look at the part of the talk that excited me the most: how standardization actually happens, and the role The Beman Project plays in it. Finally, we tie the whole thread together.

## First, verify one thing with assembly: assignment is just a pointer copy

The previous few articles kept saying that assignment on `optional<T&>` is equivalent to a pointer copy. Claims need proof, so let's look at the assembly. Write a function that does exactly one `optional<int&>` assignment and compile it at `-O2`:

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
        movq    (%rsi), %rax      # read 8 bytes from b — just a pointer
        movq    %rax, (%rdi)      # write it to a
        ret
```

Two `movq` instructions: read a pointer from `b`, write it to `a`, return. No function calls, no branches. This is what Steve Downey meant by "assignment is done through a single function and is completely transparent from the compiler-optimization point of view." When the standard defines the semantics simply enough, implementers have room to push code generation to the limit — the compiler can inline, eliminate dead code, do everything it is good at.

:::warning
One thing to flag. Dereferencing an empty `optional<T&>` has the same consequences as dereferencing a null pointer — undefined behavior — and the compiler will not necessarily give you any warning. Never assume optional will protect you; all it does is make the "is there a value" information explicit. If you dereference without checking, it crashes all the same. The terrifying problems with null and invalid pointers mentioned in the previous talk apply here unchanged.
:::

## What The Beman Project actually solves

Now we get to the part of the talk that excited me the most.

The Beman Project was launched at CppNow 2024 and is named after Beman Dawes, one of the founders of Boost. At first I assumed it was a compiler-optimization project; only later did I work out that it is a reference-implementation project: for components being proposed for inclusion in the C++ standard library, it provides a textbook-grade clean implementation.

You might think this is no big deal — don't libstdc++, libc++, and MSVC STL already exist? Can't you just pick one and read it?

No. And here is a key distinction I had never appreciated before: vendor implementations are stuffed with historical baggage.

Take an example from my own experience. I once wanted to pin down the exact behavior of one of `std::optional`'s constructors, so I went digging through the libstdc++ source, only to find a number of constructor overloads I had never seen before. Some exist for compatibility with older standards, some to cooperate with specific compiler extensions, and some carry comments along the lines of "this might not be needed, but removing it would break some internal ABI." I was completely lost. Which one should I be reading? Which one is "the one the standard talks about"?

That is exactly what The Beman Project solves. It aims to produce a clean implementation containing only what the standard proposal specifies: no historical baggage, no vendor extras, no "mistakes from years ago that nobody dares delete." The LEWG (Library Evolution Working Group) has made it explicit that this implementation will contain exactly one set of names — the ones the standard finally specifies.

## Why "having something that runs" matters so much

To me this is the most valuable part of the whole talk. Steve Downey said something to the effect that he is not smart enough to write standard wording without a real implementation to consult.

Can you believe that? Someone who writes standard proposals saying he is not smart enough? But think it over and it is a very honest point. My technical writing goes the same way: I think I have it figured out, the prose looks great, and the moment I start writing code I discover an edge case I never considered here and an interaction I never anticipated there.

Standard wording is no different. You think you can mark a function `const` — perfectly reasonable on paper — and the moment you implement it, five test cases fail. The scenarios those five tests cover may be ones you would never think of while writing the proposal, such as a combination with some other standard component, or a deduction result in a particular template context.

And what if those discoveries happen after the proposal is accepted and the standard is published? That is painful — you have to go through another full committee cycle to fix it, which can take years. But if you discover them in the reference implementation? Change it, run the tests, and five minutes later you know whether it works. Steve Downey said he made something const and five tests failed, so he knew the idea was not so good — or, the other way around, the idea was fine and the test cases themselves were wrong. Either way, that beats going through an entire committee cycle and coming back to say "oh, right, that thing you asked me to do — turns out it doesn't work."

He gave a good example. At the Tokyo meeting someone proposed making optional a range. Was the proposal actually feasible? Not settled by argument, and not by drawing type-deduction trees on a whiteboard. What he did was add the proposed functions to the Beman implementation, pull out a batch of test cases previously written for handling ranges of zero or one elements, and run them — all passed. That shifted the discussion from "can this even work" to "do we actually want to do this." The technical obstacle was cleared away; what remained was a question of design taste. That way of working beats a hundred rounds of back-and-forth debate on the mailing list.

:::warning
This also means that code written against The Beman Project has an unstable ABI. Code that compiles today may fail to compile next month, because the committee may have renamed a function, made a parameter const, or marked a constructor explicit. That is not a bug — it is the point. This kind of churn is exactly how they validate the correctness of standard wording. So Beman's role is a standardization reference, not a stable library for you to depend on in production.
:::

## Big-and-complete, or small-and-focused

Steve Downey noted that the C++ standard library tends to provide one "big and complete" thing, rather than offering several similar-but-different types to choose from the way other language ecosystems do. Once optional becomes a range, there is exactly one "optional-like thing" in the standard, not two or three near-relative types each optimized for a different use case. This follows the same philosophy as `std::string`: pack every feature into one interface, so you never have to agonize over "which one should I use."

My feelings about this approach are mixed. On one hand I understand it: fewer choices genuinely lowers cognitive load, especially for beginners. On the other hand, the `std::string` lesson is sitting right there — the interface became bloated, many functions are of questionable value, and because it has to serve every use case, it is optimal for none of them.

How do other languages do it? Rust has `Option<T>` and all kinds of zero-cost-abstraction iterator adaptors; Go has its "zero value" philosophy and does not need optional at all; Swift has `Optional`, but its integration at the language level is entirely different from C++. Every ecosystem makes its own tradeoff, and C++'s tradeoff is "here is one universal thing" — at the price of "this universal thing may be suboptimal in every specific scenario." As a user, you should at least be aware that the tradeoff exists.

## optional\<T> and optional\<T&> must be one coherent whole

With The Beman Project covered, let's look at a deeper question, one I had not appreciated at first either: if `optional<T&>` goes into the standard library, it must interoperate seamlessly with the existing `optional<T>`.

The most typical scenario is the monadic operations (`transform`, `and_then`, and friends — P0798, since C++23). Suppose you have an `optional<string>` and call `transform` on it with a function that returns `optional<string&>` — what happens? You would want it to flatten automatically. But if `optional<T>` and `optional<T&>` are implemented as disconnected pieces that do not know about each other, this will most likely return a nested optional, and you would have to flatten it by hand — deeply counterintuitive.

The popular polyfill library `tl::optional` does not have this problem, because it does not support `optional<T&>` at all — its monadic functions never have to handle this cross-type interaction. But once it is in the standard library, `optional<T>` and `optional<T&>` must form a coherent set (Steve Downey's phrase), aware of each other's existence and doing the right thing in type conversions, monadic operations, and comparison operators alike. That is also why you cannot simply patch the existing optional: the `T` and `T&` specializations have to be unified at the design level.

A side note on where `reference_wrapper` stands. It is not going away — it is still useful in tuple and bind scenarios, it is already in the standard library, and people certainly depend on it. But it is not, and should not be, the answer to "optional reference." It was originally an adaptor for the tuple DSL, its API has quirks, its built-in implicit conversion to `T&` bites in overload-resolution scenarios, and it does not care about dangling at all. `optional<T&>` is a brand-new, semantics-first design — not something you can achieve by patching `reference_wrapper`. The underlying storage may look similar, just a pointer either way, but the semantics and safety guarantees are on entirely different levels — the same reason `unique_ptr` exists even though raw pointers already did.

## Pulling the whole thread together

Let's pull the whole thread together. `std::optional` was proposed in 2005; the value version landed in C++17 in 2017; the reference version was shelved amid the assign-through versus rebind fight and took plenty of detours along the way, until P2988 finally passed the vote at the 2025 Sofia meeting and entered C++26. Under the hood it is just a constrained pointer: assignment always rebinds, const is shallow, value_or always returns a value, and `operator*` does not propagate moves in the reference version.

Each of these decisions looks a little counterintuitive on its own, but you only need to remember one thing: we are modeling a pointer. Assignment changes which object the pointer points to, const locks only the pointer itself, and moves do not reach through to the pointed-to object. Transplant the rules of pointers, watch out for a few edge cases, and the whole of `optional<T&>` makes sense.

As for why it took twenty years — it was not that it was technically impossible; it is that the semantics of these edge cases are just too easy to get wrong. It took a carefully designed proposal to straighten out every detail, plus a running reference implementation like The Beman Project to verify that the standard wording actually holds up. Standardization is not a bunch of people arguing in a meeting room and then flinging a document over the wall; it is a loop of think it through, write it down, implement it and run it, and change it if it does not work. Such a plain idea — and yet even in a setting as lofty as standardization, it turns out to be the most effective approach there is.
