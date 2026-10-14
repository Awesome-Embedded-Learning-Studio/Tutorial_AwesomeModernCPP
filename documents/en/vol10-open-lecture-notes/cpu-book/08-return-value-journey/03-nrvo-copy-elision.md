---
title: "NRVO: The Copy on the Return Path Never Existed"
chapter: 8
order: 3
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17, 20]
description: "The named local p is constructed straight into the sret slot, the frame holds no slot for p, return p is a single movq, and the counter reports 0; four pieces of evidence establish NRVO, then seven builds count 2 copies under C++14 against 1 under C++20, and C++17 writes the receiving-side elision into the syntax"
reading_time_minutes: 8
translation:
  source: documents/vol10-open-lecture-notes/cpu-book/08-return-value-journey/03-nrvo-copy-elision.md
  source_hash: 29c8563bf7c7d98d244c420f43d216ab385c10e52d4060d6a13b071c196cb4fa
  translated_at: '2026-10-14'
  engine: manual
current_status:
  title: Being polished
  detail: Freshly written in this batch; still being read through and polished. Wording and measured claims may change.
  variant: polishing
---

# NRVO: The Copy on the Return Path Never Existed

The previous two pieces verified the three physical routes and the criteria that decide which route a value takes; what comes next are the C++ language's own rules. The environment is the same one [the first piece](01-rax-family-and-register-pairs.md) used; if you want to rerun anything, follow its setup. Back in topic 01, when we compared i++ with ++i, one strange thing turned up on the postfix increment's return path: the textbook says a return should produce two copies, yet in the assembly we saw a single construction, and its target was the return slot itself. We hung the name NRVO on it at the time and moved on; this piece is where we give it a proper treatment. Here is the source: Point is a type with a copy counter, and make_point returns a local object p, the most typical situation named return value optimization (NRVO) applies to:

```cpp
// NRVO's assembly evidence + copy-construction count (the default build and the no-elide build share this source)
#include <cstdio>

struct Point {
    long x, y;
    static long copies;
    Point(long x_, long y_) : x(x_), y(y_) {}
    Point(const Point& o) : x(o.x), y(o.y) { ++copies; }
};
long Point::copies = 0;

Point make_point(long seed) __attribute__((noinline));
Point make_point(long seed) {
    Point p{seed, seed * 2};   // local object
    return p;                  // named return -> where NRVO applies
}

int main() {
    Point q = make_point(7);
    volatile long sink = q.x + q.y;   // member reads only, triggers no copy
    std::printf("copies = %ld, q = (%ld, %ld), sink = %ld\n",
                Point::copies, q.x, q.y, sink);
    return 0;
}
```

## The Default Behavior: Construction Lands Straight in the Receiver's Stack Slot

The compile command is `g++ -O0 -S -fno-asynchronous-unwind-tables r4_nrvo.cpp -o r4_nrvo.s`. Here is make_point's function body, verbatim, with the `.cfi_*` lines omitted:

```asm
_Z10make_pointl:
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$16, %rsp
	movq	%rdi, -8(%rbp)        ; spills the sret pointer
	movq	%rsi, -16(%rbp)       ; spills seed
	movq	-16(%rbp), %rax
	leaq	(%rax,%rax), %rdx     ; seed*2
	movq	-16(%rbp), %rcx
	movq	-8(%rbp), %rax
	movq	%rcx, %rsi
	movq	%rax, %rdi            ; this = the sret slot!
	call	_ZN5PointC1Ell        ; Point::Point(long,long) constructs straight into the slot
	nop
	movq	-8(%rbp), %rax        ; return p = hand the slot address back
	leave
	ret
```

Here is the call and the receiving segment on main's side as well:

```asm
	leaq	-32(%rbp), %rax
	movl	$7, %esi
	movq	%rax, %rdi
	call	_Z10make_pointl
	movq	-32(%rbp), %rdx       ; reads q directly
	movq	-24(%rbp), %rax
	addq	%rdx, %rax
```

For the claim that no copy happened on the return path to stand, we need four pieces of evidence in hand:

- make_point's own stack frame is a single `subq $16`, and its two slots hold the spilled sret pointer and seed; the local p has no slot anywhere in the frame.
- The constructor's this is taken from the sret address stored at `-8(%rbp)`, which is to say p lives in the caller's stack slot from the moment it is born. You saw this position put to use when topic 05 covered constructors and the this pointer; the difference this time is that the this value points into space that belongs to the caller.
- `return p;` compiles to a single `movq -8(%rbp), %rax` that hands the slot's address back. There is no second construction, and no copy call of any kind.
- The count the program prints is 0.

Here is the run's output:

```text
copies = 0, q = (7, 14), sink = 21
```

There is one more layer of corroboration: with zero calls to the copy constructor, the symbol does not even survive linking. The output of `nm` shows only `Point::Point(long,long)` and no copy-constructor name, and counting the copy-constructor calls in objdump comes out at 0:

```text
$ nm r4_nrvo | grep Point
0000000000004028 B _ZN5Point6copiesE
00000000000011f8 W _ZN5PointC1Ell
00000000000011f8 W _ZN5PointC2Ell
$ objdump -d r4_nrvo | grep -c 'call.*PointC1ERKS_'
0
```

An inline member function nobody calls never gets instantiated in the first place, so no calls and no instantiation hold at the same time. The count of 0 and the assembly evidence back each other up, and the counter itself is not broken; the no-elide build will serve as the counter-proof in a moment.

As for where the standard says all this, we found it on cppreference's copy elision page. Its definition of NRVO reads:

```text
In a return statement in a function with a class return type, when the operand
is the name of a non-volatile object obj with automatic storage duration
(other than a function parameter or a handler parameter), the
copy-initialization of the result object can be omitted by constructing obj
directly into the function call's result object. This variant of copy elision
is known as named return value optimization (NRVO).
```

Translated: the function's return type is a class type, the operand of return is the name of a non-volatile object with automatic storage duration rather than a function parameter, and then the copy-initialization of the result object may be omitted by constructing obj directly into the function call's result object. This variant goes by the name named return value optimization. What we saw in the assembly is exactly that move, p constructed straight into the sret slot. I was surprised by this once already back in topic 01: GCC does it at -O0, in its default configuration with not one optimization level switched on. In this compiler that is the default shape of code generation, not something a particular optimization level digs out.

## Switching the Elision Off: a Mirror Image of the Same Function

If you want to watch the copies come back with your own eyes, add `-fno-elide-constructors` and copy elision switches off. From the same r4_nrvo.cpp we built seven binaries. The first keeps the name r4_nrvo, the default -O0 build. The other six take an r5 prefix, which was just the batch label I used while running the experiments; you can match every difference to its flag in the commands. Default and no-elide are crossed with each other, and two language dialects and two optimization levels are stacked on top:

```bash
g++ -O0 r4_nrvo.cpp -o r4_nrvo
g++ -O0 -fno-elide-constructors r4_nrvo.cpp -o r5_O0_noelide
g++ -O2 r4_nrvo.cpp -o r5_O2_default
g++ -O2 -fno-elide-constructors r4_nrvo.cpp -o r5_O2_noelide
g++ -std=c++14 -O0 r4_nrvo.cpp -o r5_cpp14_O0_default
g++ -std=c++14 -O0 -fno-elide-constructors r4_nrvo.cpp -o r5_cpp14_O0_noelide
g++ -std=c++14 -O2 -fno-elide-constructors r4_nrvo.cpp -o r5_cpp14_O2_noelide
```

The output of the seven runs, verbatim:

```text
== c++20 -O0 default  ==          (./r4_nrvo)
copies = 0, q = (7, 14), sink = 21
== c++20 -O0 no-elide ==          (./r5_O0_noelide)
copies = 1, q = (7, 14), sink = 21
== c++20 -O2 default  ==          (./r5_O2_default)
copies = 0, q = (7, 14), sink = 21
== c++20 -O2 no-elide ==          (./r5_O2_noelide)
copies = 1, q = (7, 14), sink = 21
== c++14 -O0 default  ==          (./r5_cpp14_O0_default)
copies = 0, q = (7, 14), sink = 21
== c++14 -O0 no-elide ==          (./r5_cpp14_O0_noelide)
copies = 2, q = (7, 14), sink = 21
== c++14 -O2 no-elide ==          (./r5_cpp14_O2_noelide)
copies = 2, q = (7, 14), sink = 21
```

Let's gather the numbers into a table:

| Standard / flags | -O0 default | -O0 no-elide | -O2 default | -O2 no-elide |
|---|---|---|---|---|
| c++20 (GCC 16 default) | 0 | 1 | 0 | 1 |
| c++14 | 0 | 2 | — | 2 |

The default builds reporting all zeros is old news by now. The news sits in the no-elide column: under C++14 semantics the count comes out at 2, while under our default gnu++20 semantics it is only 1. The old textbook line about seeing two copies once you switch the elision off describes the C++14 world, and it has stopped holding from C++17 on, because the language promoted one of the two elisions into something mandatory, and the switch cannot buy it back. Here is the no-elide make_point's assembly for comparison; the compile command is `g++ -O0 -S -fno-asynchronous-unwind-tables -fno-elide-constructors r4_nrvo.cpp`, with the `.cfi_*` lines and the stack protector omitted:

```asm
_Z10make_pointl:
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$48, %rsp
	movq	%rdi, -40(%rbp)       ; sret pointer
	movq	%rsi, -48(%rbp)       ; seed
	movq	-48(%rbp), %rax
	leaq	(%rax,%rax), %rdx
	movq	-48(%rbp), %rcx
	leaq	-32(%rbp), %rax       ; p now has a slot of its own (-32 in this frame)
	movq	%rcx, %rsi
	movq	%rax, %rdi
	call	_ZN5PointC1Ell        ; construct the local p in this frame first
	leaq	-32(%rbp), %rdx
	movq	-40(%rbp), %rax
	movq	%rdx, %rsi
	movq	%rax, %rdi
	call	_ZN5PointC1ERKS_      ; then copy p -> the sret slot (this is that 1 copy)
	movq	-40(%rbp), %rax
	leave
	ret
```

The two versions in front of us are perfect mirror images of the same function. The default build's frame is 16 bytes; the no-elide build's swells to 48, and p now has a slot of its own at -32 in the frame. Once p has been constructed, the copy constructor invoked by `call _ZN5PointC1ERKS_` carries the whole object into the sret slot, and that is the 1 copy we counted. Constructing the local and then copying it out is not a sequence the optimizer removed; in GCC's assembly output it never showed up in the first place.

Under C++20 semantics this one copy is all that remains; under C++14 there is still a second one, and the extra copy happens on the receiving side, in main's initialization of q. The C++17 rule has a prvalue such as `make_point(7)` constructed directly into q's storage, so the copy does not exist even at the level of syntax, and the `-fno-elide` switch in our hands can do nothing about it. A prvalue, the term in play here, is a value expression that has not yet been given a place in memory, and materialization is the act of it finally occupying one.

## C++17 Wrote One of the Elisions into the Syntax

We will take two passages from the original. The first covers the mechanism of the guaranteed elision, and it comes from cppreference's copy elision page:

```text
Since C++17, a prvalue is not materialized until needed, and then it is
constructed directly into the storage of its final destination. This sometimes
means that even when the language syntax visually suggests a copy/move (e.g.
copy initialization), no copy/move is performed — which means the type
need not have an accessible copy/move constructor at all.
```

Translated: since C++17, a prvalue is not materialized before it is needed, and when that moment arrives it is constructed directly into the storage of its final destination. There are cases where the syntax visually suggests a copy or a move, copy initialization being the example at hand, yet the number that actually happens is zero, and the type does not even need an accessible copy or move constructor.

I find the second passage the more interesting one: the standard declines to give it even the name of an optimization:

```text
Note: This rule does not specify an optimization, and the Standard does not
formally describe it as 'copy elision' (because nothing is being elided).
Instead, the C++17 core language specification of prvalues and temporaries is
fundamentally different from that of earlier C++ revisions: there is no longer
a temporary to copy/move from. Another way to describe C++17 mechanics is
'unmaterialized value passing' or 'deferred temporary materialization':
prvalues are returned and used without ever materializing a temporary.
```

Translated, the point is this: it specifies no optimization, and the standard never formally calls this part of C++17 copy elision, because the thing that would be elided does not exist. C++17's core-language specification of prvalues and temporaries and that of earlier C++ revisions are two different sets; the temporary is no longer there, so naturally there is no copy or move out of it either. The alternative names are unmaterialized value passing, or deferred temporary materialization.

The treatment on the two sides of C++17 is not the same. In the semantics before C++17, the receiving-side elision and NRVO for a named return were both optimizations the standard permitted, and whether to perform them was entirely the compiler's call; the C++14 no-elide build counting 2 is what both sides look like with everything restored to its original state. From C++17 on, the receiving-side elision is written into the language and has become the syntax itself, while NRVO is still an optional optimization, which is why GCC 16's C++20 dialect under no-elide leaves only that 1 copy on the callee's side. The feature-test macro `__cpp_guaranteed_copy_elision` has the value 201606L, and you can use it to check how far the toolchain in your hands has caught up.

The Notes section on the same cppreference page carries one practical reminder, which we will quote in passing:

```text
Because some compilers do not perform copy elision in every situation where it
is allowed (e.g., in debug mode), programs that rely on the side-effects of
copy/move constructors and destructors are not portable.
```

Translated: some compilers do not perform the elision in every situation where it is allowed, debug mode being the example, and a program that relies on the side effects of copy constructors, move constructors or destructors is not portable. Read the other way, it tells us not to hang our logic on how many times the copy constructor runs. Today's GCC elides even at -O0, and as the reproduction notes in topic 01 already said, older compilers skip NRVO at -O0, and then the very same program counts out 2. That does not mean the program is wrong; it counts as a difference between versions. And with that we have seen the whole of the elision machinery.
