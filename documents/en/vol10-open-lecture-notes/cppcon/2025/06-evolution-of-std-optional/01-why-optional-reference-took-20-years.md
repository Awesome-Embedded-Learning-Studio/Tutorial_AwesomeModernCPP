---
title: Why the Optional Reference Took Twenty Years of Wrangling
description: "CppCon 2025 notes — Steve Downey on why std::optional<T&> (P2988) took from 2005 all the way to the 2025 Sofia meeting before making it into C++26: the triple identity of references, the assign-through vs rebind fight, and the final landing on a pointer"
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
  - 'optional: Making "Maybe Nothing" a Type'
related:
  - 'optional: Making "Maybe Nothing" a Type'
  - "The Value-Semantics Foundation of std::optional"
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/06-evolution-of-std-optional/01-why-optional-reference-took-20-years.md
  source_hash: 03a40fbf539f4961a6761a10fe44c80d0f88271983b6613f8668ba2750fb9700
  translated_at: '2026-09-26T16:23:09+00:00'
  engine: anthropic
  token_count: 4900
---

# Why the Optional Reference Took Twenty Years of Wrangling

:::tip
This series of notes is a second-order riff on Steve Downey's CppCon 2025 talk *The Evolution of std::optional: From Boost to C++26*. The speaker is Steve Downey of Bloomberg, also the primary author of P2988, the proposal that pushed `std::optional<T&>` into C++26. The original talk video can be found on the official CppCon channel; viewers in China can look for a re-uploaded Bilibili version.
:::

`std::optional<T&>` — something that looks like "surely it's just a reference that can hold a null value, right?" — was first proposed in 2005 and only finally voted into C++26 at the Sofia meeting in June 2025. Twenty years. Enough time to walk through an entire round from C++11 to C++23.

When I first touched C++ back in 2022, I naively assumed that whatever the standard library was missing was simply something the committee couldn't be bothered to add. Only after really digging in did I understand: some things aren't there because they are genuinely hard to get right. In this piece, following Steve Downey's framing, we take apart the question of why "a reference that can be empty" is so hard.

## First, Let's Get the Environment Straight

I verified every runnable snippet later in this series on the same setup: Arch Linux WSL, GCC 16.1.1.

```bash
$ g++ --version
g++ (GCC) 16.1.1 20260625
```

One premise to keep in mind up front: `optional<T&>` is a feature that only entered the standard with C++26, proposal number P2988. GCC 16.1.1 has already implemented it under `-std=c++26`, so we can actually run the code instead of just theorizing on paper. Switch to `-std=c++23` or earlier, and the following snippet flat-out fails to compile:

```cpp
#include <optional>
int main() {
    int x = 42;
    std::optional<int&> opt = x;   // P2988, supported only from C++26
    *opt = 100;
}
```

I actually ran it: with `-std=c++23` it errors out with the pile of template-instantiation errors saying that `optional`'s internal `union` cannot store a reference type; switch to `-std=c++26` and it compiles and prints `x=100`. That dividing line is itself living proof that "optional of references is C++26-only," and we will lean on it again and again later.

## References Actually Do Three Jobs in C++

Many people's understanding of references stops at "an alias, a nickname for a variable." Steve Downey splits the jobs of a reference into three, and I find that decomposition wonderfully clear.

The first job is calling conventions. When you write `void foo(const std::string& s)`, the reference there is saying "don't copy, operate on the original object directly." This matters enormously for operator overloading — you can't have `operator+` copying its entire operand every single time.

The second job is giving a complicated expression a local alias. With a long chain like `obj.get_container()[index].get_sub().value()`, writing `auto& x = obj.get_container()...` has the compiler note down "you've given this thing a name." It takes up no storage; it is purely an aliasing relationship. For these two jobs, "a reference cannot be rebound" is a good property: once you've given it a name, it points at that thing and keeps pointing at it.

The third job is where the trouble starts. You can stuff a reference into a `struct`.

The moment you do, the nature of the thing changes. As a member, a reference starts taking up space — typically the size of a pointer — yet it still cannot be rebound, so the compiler has no idea how "copying this struct" should be defined. Copy the reference itself? Impossible; references cannot be rebound. Copy the object it refers to? That is not what copying a `struct` is supposed to do.

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

Compile and run — `-std=c++20` is enough:

```bash
$ g++ -std=c++20 ref_in_struct.cpp -o ref_in_struct && ./ref_in_struct
HoldsValue default_ctor=1 copy_assign=1
HoldsRef   default_ctor=0 copy_assign=0
```

All zeros. Just by carrying one reference member, the struct loses both its default constructor and its copy assignment. Think about it: if `optional<T&>` really stored a reference member internally, it couldn't even be default-constructed, to say nothing of the mess its assignment semantics would be in. So the decision to "use a pointer internally" was not laziness — it was the only way out.

## Assign-Through or Rebind

Alright, suppose we really do build a `std::optional<T&>` with a reference member inside. Now you assign to it — what exactly is supposed to happen?

There are two options here, which Steve Downey calls **assign-through** and **rebind**.

assign-through means the assignment "passes through" the optional and modifies the referenced object directly. Your `optional<int&>` currently refers to the variable `x`; you assign a `y` to it, and as a result the value of `x` becomes `y`, while the optional itself still refers to `x`.

rebind is the opposite: after the assignment, the optional no longer refers to `x`, and refers to `y` instead.

If you treat the optional as "a `struct` holding a reference internally," then by struct rules assign-through is the only story that makes sense — after all, you cannot rebind a reference member. There really was a faction arguing exactly this, and their motivation was entirely defensible.

:::warning
Here's the trap: the moment the optional is currently empty (disengaged), assign-through stops making sense. There is no underlying object to "pass through" to — so should assignment in the empty state silently switch to rebind? Now the behavior of one and the same assignment operator depends on the optional's current runtime state.
:::

And that is the dead knot the whole argument was tied in. Steve Downey relayed the key observation of another committee member, JeanHeyd: **if the assignment behavior depends on the optional's current state, the type can no longer be statically reasoned about**.

What does "cannot be reasoned about" mean? You look at a line like `opt = value`, and the line by itself tells you nothing about what it does. You have to know whether `opt` holds a value at runtime before you can tell whether this line assigns through or rebinds. But the entire C++ type system — concepts, constraints, template metaprogramming — is built on the premise that "knowing the type means knowing the behavior." Once behavior depends on runtime state, all static reasoning collapses together.

Every earlier implementation that tried to walk the assign-through road ended up stepping into this pit. There is a bulletin from the Sofia meeting on Tencent Cloud that puts it plainly: back when `std::optional` was being standardized during the C++17 cycle, a fierce fight had already broken out over "should optional support references," and the exact bones of contention were "can we just use a `T*` instead" and "should `operator=` assign through or rebind" — a fight bitter enough that someone stormed off to the C standard committee (WG14). And that is how the feature came to be shelved.

## The Endgame: Stop Going in Circles — It's Just a Pointer

The conclusion after twenty years of fighting is actually dead simple. Inside `optional<T&>`, store a pointer.

Not a reference — a pointer. Then you lay a set of constraints over that pointer so that it behaves like an "optional reference." Assignment now becomes unambiguous: whether or not the optional currently holds a value, assignment always rebinds the pointer. Behavior no longer depends on state, and the deduction problem disappears entirely.

My first reaction on hearing this conclusion was "that's it? Twenty years of wrangling to arrive at 'use a pointer'?" But on reflection, this "use a pointer" was not a casual call. Behind it sits a whole set of semantics that had to be pinned down, plus the need to stay in some way consistent with the value version, `optional<T>`. Those details are where the real time went — and they are what the next few pieces cover.

## The Twenty Years of P2988

Lay the timeline out flat and you can see exactly where those twenty years went.

In 2005, optional was proposed for the first time, and the original draft actually carried reference semantics. But by the time `std::optional` officially entered C++17 in 2017, only the value version made it in; the reference version had been dropped because of the assign-through/rebind fight described above. One proposal in between made it quite far during the C++20 cycle, but in the end it was not adopted either.

The turning point was JeanHeyd. Having never managed to push optional references into the standard, he went ahead and did a thorough piece of archaeological scholarship, digging up everything that had actually been discussed over the years and what each side's reasons were. That dig directly gave rise to Steve Downey's P2988, whose position was "stop agonizing over it, just build the thing." Of course, once the building started, the details turned out to be far more numerous than imagined. Finally, at the Sofia meeting in June 2025, P2988 was voted through, into C++26.

So when you write `std::optional<int&>` under `-std=c++26` on GCC 16.1.1 and it actually compiles, what stands behind that line is twenty years of back-and-forth tug-of-war.

## What Comes Next

In this piece we have worked out why optional references are hard, and how they finally landed as "a pointer with constraints." But "use a pointer internally" is only the starting point; a whole cluster of questions still orbits that pointer: How does it differ from a bare pointer? How does it differ from `optional<T*>`? What exactly do operations like assignment, `operator*`, and `value()` return?

Before that, I think it is worth thoroughly grounding ourselves in plain `optional<T>` first. Because the moment `T` becomes a reference, premises like "ownership" and "value semantics" all stop holding — that would be a completely different story. [The next piece](./02-value-semantics-of-optional.md) starts from the value version, `optional<T>`.
