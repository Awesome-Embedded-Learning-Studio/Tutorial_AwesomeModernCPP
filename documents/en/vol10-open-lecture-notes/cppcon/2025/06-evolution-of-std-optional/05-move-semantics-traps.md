---
title: "The Move-Semantics Traps Hiding Inside Optional References"
description: "CppCon 2025 notes — the \"stolen cat\" bug where optional&lt;T&> meets move semantics: the return type of operator* on an rvalue optional, why *std::move(opt) is dangerous, and why you should write std::move as little as possible"
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
  - "Shallow traps of optional references: const, value_or, and dangling"
related:
  - "Shallow traps of optional references: const, value_or, and dangling"
  - "The Standardization Truth: The Beman Project and a Reference Implementation That Actually Runs"
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/06-evolution-of-std-optional/05-move-semantics-traps.md
  source_hash: b997dd9840e99281ccb82758842dad3205f5e39e3deaccbc2eef4585fe856df7
  translated_at: '2026-07-29T00:33:58.679864+00:00'
  engine: anthropic
  token_count: 2293
---

# The Move-Semantics Traps Hiding Inside Optional References

[The previous article](./04-shallow-traps-const-value-or-dangling.md) covered the usage-level pitfalls of `optional<T&>`. This one shifts dimensions and looks at the territory where it meets move semantics. This is where C++ produces its most insidious bugs — one `std::move` in the wrong place and you can "steal someone else's cat."

## First, admit that the most dangerous kind of "it works" exists

Honestly, I used to have a terrible coding habit: as long as the code produced the expected output, I considered it done and committed it. I took a bad fall once over an install script. The script looked like it ran correctly — output was all right — so I figured it was done and set it aside. A few days later I ran it in a different environment and it blew up. Later someone much better than me looked at it and found the script was never supposed to work in the first place. It just happened to work.

This is the worst kind of "it works," because it gives you false confidence. You think you understand that part thoroughly, when in reality the underlying logic is completely skewed.

These "happens to work" traps are everywhere in C++ templates and move semantics. This article is about a bug at the intersection of optional references and move semantics that can produce a one-in-a-thousand bizarre crash.

## What "the cat gets stolen" actually means

Steve Downey gives a particularly vivid example. Suppose there's a cat named Finn, and I create a reference to Finn and wrap it in an optional. Now if I move-assign that optional reference to another optional, in some implementations something absurd happens: Boost.Optional will "steal" the cat itself, rather than simply copying the reference.

You may be confused — how can a reference be "stolen"? A reference doesn't own the object. The problem lies in how the return value category of `operator*` interacts with move semantics.

Let's look at a key fact first: the return type of `operator*` on an rvalue optional differs between the `T` and `T&` specializations. Let's run it:

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
    std::cout << "*std::move(optional<Cat>)  is Cat&& ? "
              << std::is_same_v<Rval, Cat&&> << "\n";

    Cat c{"Loki"};
    std::optional<Cat&> or_ = c;
    using Rref = decltype(*std::move(or_));          // reference version, rvalue optional
    std::cout << "*std::move(optional<Cat&>) is Cat&  ? "
              << std::is_same_v<Rref, Cat&> << "\n";
}
```

```bash
$ g++ -std=c++26 move_category.cpp -o move_category && ./move_category
*std::move(optional<Cat>)  is Cat&& ? 1
*std::move(optional<Cat&>) is Cat&  ? 1
```

Read those two output lines. For an rvalue `optional<Cat>` (the value version), the type of `*std::move(opt)` is `Cat&&`, because the optional is about to be destroyed and the Cat inside it can be moved out — this is a sensible optimization. But for an rvalue `optional<Cat&>` (the reference version), the type of `*std::move(opt)` is still `Cat&` — it does not become `Cat&&`.

## What's really going on underneath

That distinction is the heart of "the cat gets stolen."

For an rvalue `optional<T>`, having `operator*` return `T&&` is correct. The optional is about to die, the `T` inside can be moved, and writing `some_type dest = *std::move(opt)` moves the `T` out — no problem.

But for `optional<T&>`, the optional being about to die does not mean the object it references is about to die. Finn is still alive and well; it's just being referenced by an optional that's about to be destroyed. If an implementation has `operator*` return `T&&` for an rvalue `optional<T&>` (which is exactly the bug Boost.Optional used to have), then writing `*std::move(opt)` moves Finn itself. The cat has been stolen.

P2988 fixes this: `operator*` on `optional<T&>` always returns `T&`, regardless of whether the optional is an rvalue. Because we're emulating reference semantics, and the value category of a reference is independent of the value category of "the container holding this reference" — once a reference is bound to an object, the value category you reach through it is determined by that object itself. The GCC 16.1.1 implementation measured above is correct: `*std::move(optional<Cat&>)` is `Cat&`, not `Cat&&`.

## A rule I set for myself

Seeing this, I set a rule for myself, and I'll share it with you.

Don't try to infer what you're allowed to move.

What does that mean? Don't think "I know what this function returns, so I can wrap a `std::move` around the outside and it's fine." You might be right today, but tomorrow someone changes that function's return type, or a template instantiates into a different specialization, and your `std::move` can go from harmless to stealing someone else's cat.

The right approach is: only move objects you're certain you own the right to move, and put the `std::move` on that object itself, not on the outside of the container holding it.

```cpp
// Dangerous: you don't know whether *rhs after dereference should be moved at all
some_type dest = *std::move(rhs);

// Safe: dereference first to get the reference, then move that reference
some_type dest = std::move(*rhs);
```

The difference between these two lines is subtle, and the semantics are completely different. `*std::move(rhs)` first turns the optional into an rvalue and then dereferences it; the result type of the dereference depends on how the optional's `operator*` is defined for rvalues — which is exactly the unpredictable part described above. `std::move(*rhs)` first dereferences to get a reference and then turns that reference into an rvalue; the semantics are clear: I want to move the referenced object itself.

Taking it further, the best `std::move` you can write is the one you didn't need to write at all. Take returning a local variable:

```cpp
Cat make_cat() {
    Cat c{"Finn"};
    return c;                       // correct — NRVO or implicit move
    // return std::move(c);         // redundant, and can even block NRVO
}
```

The compiler already knows `c` is local and about to be destroyed, and it handles that automatically. Writing `std::move` by hand can actually kill NRVO, because `std::move(c)` returns an rvalue reference, while NRVO requires the return to be the named local variable itself. Writing `std::move` is essentially explaining things to the compiler, and that's always risky, because you're not necessarily smarter than the compiler. On a good day you might be; on a Friday afternoon, I'm not.

## What problem is optional reference actually trying to solve

We've spent all this time on bugs, so let's step back — what is `optional<T&>` actually for?

The canonical use case is: look something up, and "not found" is not an exception.

I used to write code that looked things up in a map and, on a miss, either threw an exception or returned the end iterator for the caller to deal with. But honestly, "not found" is often a perfectly normal result, and not worth expressing with an exception at all. Exceptions are expensive, and their semantics are wrong here — "key doesn't exist" is not a program error, it's just one possible query result.

With `optional<T&>`, we can give a map a getter that returns an optional reference: if found, you get a reference and can modify it directly; if not found, it's empty. The standard library doesn't yet build this interface directly into the associative containers (that's P3091's job); for now you can wrap a layer of `reference_wrapper` from [the earlier article](./03-optional-reference-and-assignment.md) as a stopgap.

## What's next

The intersection of move semantics and references comes down to one thing: an optional about to be destroyed does not mean the referenced object is about to be destroyed. So don't write `*std::move(opt)` — write `std::move(*opt)`, and the best move is no move at all. In the next article we step away from optional's own details and look at the part of this talk that excited me the most: how standardization actually happens, and the role The Beman Project plays in it.
