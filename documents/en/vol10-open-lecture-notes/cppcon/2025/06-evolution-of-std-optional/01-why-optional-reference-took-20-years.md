---
title: "Why the optional reference took twenty years"
description: "CppCon 2025 notes — Steve Downey on why std::optional&lt;T&> (P2988) went from 2005 to the 2025 Sofia meeting before finally making it into C++26 — the triple identity of references, the assign-through vs rebind fight, and the final landing on a pointer"
chapter: 6
order: 1
conference: cppcon
conference_year: 2025
talk_title: 'The Evolution of std::optional: From Boost to C++26'
speaker: Steve Downey
cpp_standard: [17, 23, 26]
difficulty: intermediate
platform: host
reading_time_minutes: 10
tags:
  - cpp-modern
  - host
  - intermediate
  - optional
prerequisites:
  - 'optional: Making "Possibly None" a Type'
related:
  - 'optional: Making "Possibly None" a Type'
  - "The Value-Semantics Foundation of std::optional"
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/06-evolution-of-std-optional/01-why-optional-reference-took-20-years.md
  source_hash: 03a40fbf539f4961a6761a10fe44c80d0f88271983b6613f8668ba2750fb9700
  translated_at: '2026-07-29T00:33:58.679864+00:00'
  engine: anthropic
  token_count: 2740
---

# Why the optional reference took twenty years

:::tip
This series of notes is a second-order dive based on Steve Downey's CppCon 2025 talk *The Evolution of std::optional: From Boost to C++26*. The speaker is Steve Downey of Bloomberg, also the primary author of P2988, the proposal that pushed `std::optional<T&>` into C++26. The original talk video can be found on the official CppCon channel; viewers in China can look for a Bilibili mirror.
:::

`std::optional<T&>` — something that looks like "just a reference that can hold a null value" — was first proposed in 2005, and only finally voted into C++26 at the Sofia meeting in June 2025. Twenty years. Enough to go through a whole round from C++11 to C++23.

When I first touched C++ in 2022, I naively thought that whatever was missing from the standard library was simply because the committee couldn't be bothered to add it. Only after I really dug in did I realize that some things weren't added because they're genuinely hard to add correctly. In this piece we follow Steve Downey's framing and unpack the question of "why a reference that can hold a null value is so hard."

## Setting up the environment first

Every runnable piece of code later in this series, I verified with the same setup: Arch Linux WSL, GCC 16.1.1.

```bash
$ g++ --version
g++ (GCC) 16.1.1 20260625
```

There's one premise you need to keep in mind here: `optional<T&>` is a feature that only entered the standard in C++26, proposal number P2988. GCC 16.1.1 already implements it under `-std=c++26`, so we can actually run the code rather than theorize on paper. Switch to `-std=c++23` or earlier, and the following won't even compile:

```cpp
#include <optional>
int main() {
    int x = 42;
    std::optional<int&> opt = x;   // P2988, only supported from C++26
    *opt = 100;
}
```

I ran it for real: under `-std=c++23` it errors out, with that string of template instantiation errors about the `union` inside `optional` not being able to hold a reference type; switch to `-std=c++26` and it compiles, outputting `x=100`. This dividing line is itself live evidence that "reference optional is only supported from C++26," and we'll come back to it repeatedly.

## A reference actually does three things in C++

Many people stop at "an alias, a nickname for a variable" in their understanding of references. Steve Downey breaks the responsibilities of a reference into three, and I find this breakdown very clear.

The first is the calling convention. When you write `void foo(const std::string& s)`, the reference here is saying "don't copy, just operate on the original object." This matters especially for operator overloading — you can't have `operator+` copy the entire operand every single time.

The second is giving a local alias to a complex expression. For a long chain like `obj.get_container()[index].get_sub().value()`, writing `auto& x = obj.get_container()...` lets the compiler record "you gave this thing a name." It takes up no space; it's purely an aliasing relationship. In both of these, "a reference can't be rebound" is a good property — once you've named it, it always points at that thing.

The third is where things go wrong. You can stuff a reference into a `struct`.

The moment you do, its nature changes. A reference as a member starts taking up space — typically the size of a pointer — but it still can't be rebound, so the compiler has no idea how to define "copy this struct." Copy the reference itself? Can't — references can't be rebound. Copy the object it references? That's not what a `struct` should be doing.

Let's run a minimal example to see this clearly:

```cpp
// ref_in_struct.cpp
#include <cstdio>
#include <type_traits>

struct HoldsRef   { int& ref; };   // reference member
struct HoldsValue { int val; };    // plain value member

int main() {
    std::printf("HoldsValue default_ctor=%d copy_assign=%d\n",
        std::is_default_constructible_v<HoldsValue>,
        std::is_copy_assignable_v<HoldsValue>);
    std::printf("HoldsRef   default_ctor=%d copy_assign=%d\n",
        std::is_default_constructible_v<HoldsRef>,
        std::is_copy_assignable_v<HoldsRef>);
}
```

Compile and run; `-std=c++20` is enough:

```bash
$ g++ -std=c++20 ref_in_struct.cpp -o ref_in_struct && ./ref_in_struct
HoldsValue default_ctor=1 copy_assign=1
HoldsRef   default_ctor=0 copy_assign=0
```

All zeros. Just from adding a reference member, this struct loses its default constructor and copy assignment. Think about it: if `optional<T&>` really did hold a reference member internally, it couldn't even default-construct, let alone sort out the chaos of assignment semantics. So the decision to "use a pointer internally" isn't laziness — it's the only viable option.

## assign-through or rebind

Alright, suppose we really do build a `std::optional<T&>` that holds a reference member internally. Now you assign to it — what should actually happen?

There are two options, which Steve Downey calls **assign-through** and **rebind**.

assign-through means the assignment "passes through" the optional and directly modifies the referenced object. Your `optional<int&>` currently references variable `x`; you assign a `y` to it; the result is that `x`'s value becomes `y`, while the optional itself still references `x`.

rebind is the opposite: after the assignment the optional no longer references `x`, and instead references `y`.

If you treat the optional as "a `struct` that internally holds a reference," then by the rules of `struct`, assign-through is the only thing that makes sense — after all, you can't rebind a reference member. There is indeed a faction arguing exactly this, and their motivation is entirely defensible.

:::warning
Here's the trap: once the optional is currently empty (disengaged), assign-through stops making sense. There's no underlying object to "pass through" to — so should the assignment in the empty state silently switch to rebind? Now the behavior of a single assignment operator depends on the optional's runtime state.
:::

This is the deadlock at the heart of the whole debate. Steve Downey relayed the key observation of another committee member, JeanHeyd: **if the assignment behavior depends on the optional's current state, this type cannot be statically reasoned about**.

What does "cannot be reasoned about" mean? You look at a line `opt = value`, and just from this line of code alone, you have no idea what it does. You have to know whether `opt` holds a value at runtime before you can determine whether this line passes through or rebinds. And C++'s entire type system, its concept constraints, its template metaprogramming, all rest on the premise that "knowing the type means knowing the behavior." Once behavior depends on runtime state, all static reasoning breaks down at once.

Every previous implementation that tried to go down the assign-through road ended up stepping into this pit. There's a Sofia meeting dispatch on Tencent Cloud that puts it bluntly: when `std::optional` was standardized in the C++17 cycle, a fierce fight broke out over "should reference optionals be supported at all," and the flashpoints were exactly "can we just use `T*` instead" and "should `operator=` assign-through or rebind" — it got heated enough that someone stormed off to the C standards committee (WG14). And so the feature was shelved.

## The end: stop dancing around, it's a pointer

The conclusion after twenty years of arguing is actually quite simple. Inside `optional<T&>`, just store a pointer.

Not a reference — a pointer. Then impose a pile of constraints on that pointer so it behaves like "an optional reference." Now assignment is clear: no matter whether the optional currently holds a value, assignment always rebinds the pointer. Behavior no longer depends on state, and the reasoning problem disappears completely.

My first reaction on hearing this conclusion was "that's it? Twenty years of fighting to arrive at 'use a pointer'?" But stepping back, this "use a pointer" isn't arbitrary. Behind it sits a whole semantics that has to be defined precisely and kept consistent in some way with the value version `optional<T>` — those details are where the real time went, and they're what the next few pieces will cover.

## P2988's twenty years

Lay the timeline out, and you'll see where those twenty years went.

In 2005, optional was first proposed, and the original draft actually carried reference semantics. But by the time `std::optional` officially entered C++17 in 2017, only the value version made it in — the reference version had been stripped out because of the assign-through/rebind fight. There was a proposal in the C++20 cycle that got quite far, but it too was ultimately not adopted.

The turning point came with JeanHeyd. Having failed for so long to push an optional reference into the standard, he did a thorough archaeological dig — pulling up everything that had actually been discussed historically and what each side's reasoning was. That dig directly inspired Steve Downey's P2988, which argued "stop agonizing, just build it." Of course, once you actually start building, there are far more details than you'd imagine. Finally, at the Sofia meeting in June 2025, P2988 was voted through and entered C++26.

So when you write `std::optional<int&>` on GCC 16.1.1 with `-std=c++26` and it actually compiles, behind that is twenty years of back-and-forth tug-of-war.

## What's next

In this piece we've sorted out why the optional reference is hard, and how it finally landed as "a constrained pointer." But "use a pointer internally" is only the starting point — there's a whole pile of questions still circling that pointer: how does it differ from a raw pointer? How does it differ from `optional<T*>`? What exactly do operations like assignment, `operator*`, and `value()` return?

Before any of that, I think it's worth thoroughly understanding the foundations of plain `optional<T>` first. Because once `T` becomes a reference, the premises of "owning ownership" and "value semantics" all stop holding, and it becomes a completely different story. [The next piece](./02-value-semantics-of-optional.md) starts from the value version, `optional<T>`.
