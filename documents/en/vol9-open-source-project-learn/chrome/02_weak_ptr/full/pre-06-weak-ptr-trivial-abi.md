---
chapter: 0
cpp_standard:
- 11
- 17
- 20
description: "Breaking down [[clang::trivial_abi]] / TRIVIAL_ABI: the semantics, the cost, and why a type with a non-trivial destructor like WeakPtr can safely carry the annotation — an outcome of deliberate design, not an inherent property"
difficulty: intermediate
order: 6
platform: host
prerequisites:
- 'WeakPtr prerequisite (I): intrusive reference counting and scoped_refptr'
- 'WeakPtr prerequisite (V): template friend and uintptr_t type erasure'
reading_time_minutes: 7
related:
- 'WeakPtr hands-on (I): motivation and API design'
- 'WeakPtr Hands-on (VI): Tests and Performance Comparison'
tags:
- host
- cpp-modern
- intermediate
- 零开销抽象
- 优化
- weak_ptr
title: "WeakPtr prerequisite (VI): TRIVIAL_ABI and trivial relocatability"
translation:
  source: documents/vol9-open-source-project-learn/chrome/02_weak_ptr/full/pre-06-weak-ptr-trivial-abi.md
  source_hash: 57e5d269e6daedd23a5c491928aa0fab26b2466b98009551a6957f63c0ed9172
  translated_at: '2026-09-26T01:34:40+00:00'
  engine: anthropic
  token_count: 4700
---
# WeakPtr prerequisite (VI): TRIVIAL_ABI and trivial relocatability

Over the past few pieces we have taken WeakPtr's members apart: a `WeakReference` (holding a `scoped_refptr<const Flag>`, the refcounted handle) and a `T* ptr_`. It has a non-trivial destructor — destroying one has to dec the flag's refcount. Under C++'s default ABI rules, such a type cannot be passed by value in a register at a call boundary; it has to detour through memory (the stack, or a hidden reference parameter).

But open Chromium's `weak_ptr.h` and you will find an attribute named `TRIVIAL_ABI` hanging on both `WeakPtr` and `WeakReference` (`weak_ptr.h:101,203`). With it attached, a type that has a non-trivial destructor is treated as trivial under the calling convention: it can ride in registers and be handed over by value efficiently. WeakPtr is exactly two pointers wide in total, so once annotated, passing one between functions is as cheap as passing two pointers.

The first time we saw this, it made no sense to us. A refcount-holding type, daring to call itself "trivial"? There is a counterintuitive point here worth spending a whole piece on: what `trivial_abi` actually is, what it costs, and — the crux — how WeakPtr manages to satisfy its safety conditions. The answer is not "the refcount does not matter"; it is that this design deliberately placed every part exactly where it could be safely annotated.

---

## Trivial vs non-trivial types: the ABI view

C++ sorts types into two buckets, "trivial" and "non-trivial", and the calling convention (the ABI) treats them differently.

Trivial types — `int`, `std::pair<int,int>`, POD structs — have copies, moves, and destruction that "do nothing", or more precisely are bitwise equivalent to a memcpy. At a call, such a type can be stuffed straight into a register and passed by value, and returns can ride registers too; the overhead is small enough to all but ignore.

A non-trivial type — it qualifies as one if any of its copy constructor, move constructor, or destructor is non-trivial — is usually passed through memory under the ABI: a copy gets made on the stack, or a hidden reference parameter gets slipped in. The reason is that the compiler must guarantee those non-trivial operations (such as decrementing a refcount at destruction) run where they are supposed to run, so a plain memcpy will not do.

WeakPtr holds a `scoped_refptr` (whose destruction decs the refcount), so it is a non-trivial type. Under the default ABI, passing a WeakPtr has to go through memory; it cannot get into a register.

---

## What [[clang::trivial_abi]] is

`[[clang::trivial_abi]]` is a Clang type attribute; Chromium wraps it up as the `TRIVIAL_ABI` macro — it expands to the attribute when `__has_cpp_attribute(clang::trivial_abi)` is true, and to nothing otherwise (`base/compiler_specific.h`).

Its semantics boil down to one sentence: have this non-trivial type treated as trivial under the calling convention. Two concrete effects. First, by-value parameters and by-value returns can go through registers, on equal footing with trivial types. Second, the object can be "relocated" — the two steps of "move + destroy the source object" get fused into a single memcpy haul.

There is one precondition attached here (the Clang documentation stresses it): the annotated type must be trivially relocatable. What that means: bitwise moving the object from one place to another and then skipping the destructor at the original location must be fully equivalent, semantically, to "running one move construction on it + destroying the source object". If that holds, the compiler can substitute a memcpy for move+destroy and pass the type in registers with a clear conscience.

---

## The cost: destruction timing and location change

`trivial_abi` is not a free lunch; it disturbs the timing and the location of destruction.

For an ordinary non-trivial type, the ABI strictly prescribes "which stack frame this temporary gets destroyed in". Once `trivial_abi` is applied, an object might be constructed in the caller's frame, loaded into a register, handed across, and destroyed in the callee's frame — the destruction landing point has moved. If the type really is trivially relocatable, the change is harmless (move+destroy is equivalent to a memcpy anyway). But if the type is not truly trivially relocatable — say the destructor has side effects that must not be skipped — forcing the attribute on it will breed bugs.

That is the price. This is why the Clang documentation specifically warns: `trivial_abi` is only for types that genuinely meet the condition; do not slap it on casually.

---

## Why WeakPtr is safe to annotate

So WeakPtr holds a refcount (through `scoped_refptr<Flag>`) — on what grounds does it satisfy "trivially relocatable"? Let's take it apart piece by piece.

First, `T* ptr_`. It is a raw pointer; copy, move, and destruction are all trivial (bitwise) and never touch inc/dec at all. A raw pointer is trivially relocatable by nature, posing no obstacle whatsoever to `trivial_abi`.

The real crux is the `scoped_refptr<const Flag>` inside `WeakReference ref_`. It is not a trivial type — copying incs the refcount, destroying decs it. So is it "trivially relocatable"?

Let's reason it through. What does scoped_refptr's move constructor do? It steals the source's raw pointer (`ptr_ = other.ptr_; other.ptr_ = nullptr;`) without inc-ing. When the source object is destroyed afterwards, its `ptr_` is already nullptr, so its `release()` walks out as a no-op. Put the two steps together and the net effect is a bitwise move of the raw pointer — no inc, no dec, the refcount does not budge.

That is precisely the definition of "trivially relocatable". So scoped_refptr, while non-trivial, is trivially relocatable. WeakPtr holds one and inherits the property; together with the naturally trivial `ptr_` part, the whole WeakPtr satisfies `trivial_abi`'s precondition.

And there is one more guarantee that is easy to miss. `trivial_abi` makes the destruction location unpredictable — a particular WeakPtr instance may end up destroyed on an "unexpected" thread (in the caller's stack frame, say), where its scoped_refptr will dec the flag's refcount on that very thread. That requires the flag's refcount operations to be thread-safe, which is exactly the backstop provided by `RefCountedThreadSafe` (atomic inc/dec) from [prerequisite (I)](./pre-01-weak-ptr-intrusive-refcount-and-scoped-refptr.md). Add on top the cross-thread destruction exemption via `HasOneRef()` in `Flag::Invalidate`, and the flag can safely hit zero and be destroyed on any thread. Without this layer, `trivial_abi` stacked on "destruction location drift" is a combination that sooner or later goes off the rails in cross-thread scenarios.

---

## This is design avoidance, not an inherent property

At this point there is a conclusion we must stress, so that you do not take this line of thinking to some other type and drive into a ditch.

`trivial_abi` is not inherently safe for types that hold a refcount handle. WeakPtr can be annotated because its members were deliberately designed to be trivially relocatable — not because "any refcounted type can casually wear this attribute".

A counterexample. Suppose you write a class that manages its refcount by hand (`++count_` in the copy constructor, `--count_` in the destructor with cleanup when it hits zero) but never guarantee "move+destroy-source is equivalent to a memcpy". Forcing `trivial_abi` onto it may let the compiler skip a destructor at the wrong place or the wrong time. Once the refcount drifts, the mild outcome is a leak, the severe one a double-free — both the Clang documentation and Chromium's own comments call this out explicitly.

Back to WeakPtr: its safety is propped up by three layers together. `ptr_` is a raw pointer, trivial by nature; `scoped_refptr` is non-trivial yet trivially relocatable, with move+destroy equivalent to a memcpy; `Flag` uses `RefCountedThreadSafe`, with `HasOneRef()` covering cross-thread destruction. Miss any one of the three layers and the whole thing falls. So when you see in 02-6 that WeakPtr is tagged `TRIVIAL_ABI` and reaping the register-passing benefit, that "zero overhead" was paid for by plenty of design up front — the overhead did not disappear, it was dissolved ahead of time by design. This is exactly Chromium's zero-overhead abstraction philosophy.

With this, the seven prerequisite pieces are complete: pre-00's introduction to weak references, plus the six parts of pre-01 through pre-06 — intrusive reference counting, atomics and memory order, sequences and DCHECK/CHECK, concepts, template friend and uintptr_t, and TRIVIAL_ABI. The parts are all on the table; time to roll up our sleeves and build WeakPtr's core skeleton. In the next piece we watch how these seven parts mesh into one industrial-grade weak pointer.

## References

- [Clang documentation: the `trivial_abi` attribute](https://clang.llvm.org/docs/AttributeReference.html#trivial-abi)
- [cppreference: TrivialType and triviality](https://en.cppreference.com/w/cpp/named_req/TrivialType)
- [Chromium `base/compiler_specific.h` — the TRIVIAL_ABI macro](https://source.chromium.org/chromium/chromium/src/+/main:base/compiler_specific.h)
- [Chromium `base/memory/weak_ptr.h` — annotations at lines 101/203](https://source.chromium.org/chromium/chromium/src/+/main:base/memory/weak_ptr.h)
