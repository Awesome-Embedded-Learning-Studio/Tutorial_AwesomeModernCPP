---
title: "The Move-Semantics Traps Hiding Inside Optional References"
description: 'CppCon 2025 notes — the "stolen cat" bug where optional<T&> meets move semantics: the return type of operator* on an rvalue optional, why *std::move(opt) is dangerous, and why the best std::move is the one you never write'
chapter: 6
order: 5
conference: cppcon
conference_year: 2025
talk_title: 'The Evolution of std::optional: From Boost to C++26'
speaker: Steve Downey
cpp_standard: [17, 23, 26]
difficulty: intermediate
platform: host
reading_time_minutes: 9
tags:
  - cpp-modern
  - host
  - intermediate
  - optional
prerequisites:
  - "Shallow Traps of Optional References: const, value_or, and Dangling"
related:
  - "Shallow Traps of Optional References: const, value_or, and Dangling"
  - "The Standardization Truth: The Beman Project and a Reference Implementation That Actually Runs"
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/06-evolution-of-std-optional/05-move-semantics-traps.md
  source_hash: b997dd9840e99281ccb82758842dad3205f5e39e3deaccbc2eef4585fe856df7
  translated_at: '2026-09-26T16:31:45+00:00'
  engine: anthropic
  token_count: 2000
---
# The Move-Semantics Traps Hiding Inside Optional References

[The previous part](./04-shallow-traps-const-value-or-dangling.md) covered the usage-level traps of `optional<T&>`. This part switches dimensions and walks into the territory where it crosses paths with move semantics. This is where C++ grows its most insidious bugs: put one `std::move` in the wrong place, and you may well have "stolen someone else's cat."

## First, Admit That the Most Dangerous Kind of "It Works" Exists

Honestly, I used to have a terrible coding habit: as long as the code produced the expected result, I called it done and committed straight away. I took a hard fall over an installation script once. The script looked like it ran through — all the output checked out — so I figured it was finished and tossed it aside. A few days later I ran it in a different environment and it blew up on the spot. It took someone far more experienced looking at it to discover that the script was never supposed to run at all; it only ran by coincidence.

That is the worst kind of "it works," because it hands you a false sense of security: you believe you have that part fully understood, while the underlying logic is actually bent out of shape.

These "happens to run" traps are everywhere in C++ templates and move semantics. What this part covers is a bug at the crossing of optional references and move semantics that can produce a bizarre crash with roughly one-in-a-thousand probability.

## What "The Cat Gets Stolen" Actually Means

Steve Downey gives a wonderfully vivid example. Suppose there is a cat named Finn; I create a reference to Finn and wrap it in an optional. Now, if that optional reference is move-assigned to another optional, some implementations do something outrageous: Boost.Optional will "steal" the cat itself instead of simply copying the reference.

You may be baffled — how can a reference be "stolen"? A reference does not own the object. The problem sits in the interaction between the value category that `operator*` returns and move semantics.

Let's start with a key fact: the return type of `operator*` on an rvalue optional differs between the `T` and `T&` specializations. Run it:

```cpp
// move_category.cpp
#include <iostream>
#include <optional>
#include <string>
#include <type_traits>

struct Cat {
    std::string name;
    Cat(std::string n) : name(std::move(n)) {}
};

int main() {
    std::optional<Cat> ov = Cat{"Finn"};
    using Rval = decltype(*std::move(ov));          // value version, rvalue optional
    std::cout << "*std::move(optional<Cat>)  是 Cat&& ? "
              << std::is_same_v<Rval, Cat&&> << "\n";

    Cat c{"Loki"};
    std::optional<Cat&> or_ = c;
    using Rref = decltype(*std::move(or_));          // reference version, rvalue optional
    std::cout << "*std::move(optional<Cat&>) 是 Cat&  ? "
              << std::is_same_v<Rref, Cat&> << "\n";
}
```

```bash
$ g++ -std=c++26 move_category.cpp -o move_category && ./move_category
*std::move(optional<Cat>)  是 Cat&& ? 1
*std::move(optional<Cat&>) 是 Cat&  ? 1
```

Read those two output lines. For an rvalue `optional<Cat>` (the value version), the type of `*std::move(opt)` is `Cat&&`: the optional is about to be destroyed, so the Cat inside may legitimately be moved out — a reasonable optimization. But for an rvalue `optional<Cat&>` (the reference version), the type of `*std::move(opt)` is still `Cat&`; it never became `Cat&&`.

## What Is Actually Happening Underneath

This distinction is the core of "the cat gets stolen."

For an rvalue `optional<T>`, `operator*` returning `T&&` is the right thing. The optional is about to die, the `T` inside can be moved, and when you write `some_type dest = *std::move(opt)` you are moving the `T` out — no problem there.

But with `optional<T&>`, the optional being about to die does not mean the object it references is about to die. Finn is still alive and well; it just happens to be referenced by an optional that is about to be destroyed. If an implementation has `operator*` return `T&&` for an rvalue `optional<T&>` too (which is precisely the bug Boost.Optional carried back then), then writing `*std::move(opt)` moves Finn itself. The cat has been stolen.

P2988 fixed this: `operator*` on `optional<T&>` always returns `T&`, no matter whether the optional is an rvalue. We are emulating reference semantics here, and the value category of a reference has nothing to do with the value category of "the container holding that reference": once a reference is bound to an object, the value category you reach through it is determined by that object itself. The GCC 16.1.1 implementation tested above does the right thing — `*std::move(optional<Cat&>)` is `Cat&`, not `Cat&&`.

## A Rule I Set for Myself

Having seen this, I set a rule for myself, and I'll share it with you.

Do not try to reason out what you are allowed to move.

What does that mean? Drop the thought "I know what this function returns, so I can wrap a `std::move` around the outside and it will be fine." You may be right today; tomorrow somebody changes that function's return type, or a template instantiates into a different specialization, and your `std::move` can go from harmless to stealing someone else's cat.

The correct approach: only move objects you are certain you have the right to move, and write the `std::move` on the object itself, not on the outside of the container that holds it.

```cpp
// Dangerous: you do not know whether what *rhs dereferences to should be moved at all
some_type dest = *std::move(rhs);

// Safe: dereference first to get the reference, then move that reference
some_type dest = std::move(*rhs);
```

The difference between these two lines is subtle, and the semantics are entirely different. `*std::move(rhs)` first turns the optional into an rvalue and then dereferences; the result type of the dereference depends on how the optional's `operator*` is defined for rvalues — exactly the unpredictable part from before. `std::move(*rhs)` first dereferences to get the reference and then turns that reference into an rvalue — crisp semantics: what I want to move is the referenced object itself.

Going one step further: the best `std::move` you can ever write is the one that never needed writing at all. Take returning a local variable:

```cpp
Cat make_cat() {
    Cat c{"Finn"};
    return c;                       // correct — NRVO or implicit move
    // return std::move(c);         // redundant, and can even block NRVO
}
```

The compiler already knows `c` is local and about to be destroyed, and it handles that automatically. Hand-writing the `std::move` can actually kill NRVO, because `std::move(c)` yields an rvalue reference, while NRVO requires the return expression to be the named local variable itself. Writing `std::move` is essentially explaining things to the compiler, and that is always risky, because you are not necessarily smarter than the compiler. On a good day you might be; by Friday afternoon, I certainly am not.

## What Problem Is the Optional Reference Actually Trying to Solve

After all this talk about bugs, step back for a moment: what is `optional<T&>` actually for?

The canonical use case: look something up, where "not found" does not count as an exception.

I used to write code that looked things up in a map and, on a miss, either threw an exception or returned the end iterator for the caller to sort out. But honestly, "not found" is very often a perfectly ordinary outcome that does not deserve an exception at all. Exceptions are expensive, and the semantics are wrong: "key does not exist" is not a program error — it is simply one possible query result.

With `optional<T&>`, we can give a map a getter that returns an optional reference: a hit hands you the reference to modify directly, a miss comes back empty. The standard library does not yet build this interface directly into the associative containers (that is P3091's job); for now you can bridge the gap with the `reference_wrapper` wrapping from [the earlier part](./03-optional-reference-and-assignment.md).

## What Comes Next

The crossing zone of move semantics and references boils down to a single line: an optional about to be destroyed does not mean the referenced object is about to be destroyed. So do not write `*std::move(opt)` — write `std::move(*opt)` — and the best move is no move at all. In the next part we step out of optional's own details and look at what excited me most in this whole talk: how standardization actually gets done, and the role The Beman Project plays in it.
