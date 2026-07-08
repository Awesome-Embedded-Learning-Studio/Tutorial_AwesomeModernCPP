---
chapter: 0
cpp_standard:
- 11
- 17
- 20
description: "Breaking down [[clang::trivial_abi]] / TRIVIAL_ABI: the semantics, the cost, and why a refcount-holding type like WeakPtr can safely carry the annotation because the design positions every part to qualify, not because the property is inherent."
difficulty: intermediate
order: 6
platform: host
prerequisites:
- 'WeakPtr prerequisite (I): intrusive refcount and scoped_refptr'
- 'WeakPtr prerequisite (V): template friend and uintptr_t type erasure'
reading_time_minutes: 7
related:
- 'weak_ptr design guide (I): motivation, API, and the control block'
- 'weak_ptr design guide (III): test strategy and performance'
tags:
- host
- cpp-modern
- intermediate
- 零开销抽象
- 优化
- weak_ptr
title: "WeakPtr prerequisite (VI): TRIVIAL_ABI and trivial relocatability"
---
# WeakPtr prerequisite (VI): TRIVIAL_ABI and trivial relocatability

Across the last few pieces we mapped out WeakPtr's members: a `WeakReference` (holding a `scoped_refptr<const Flag>`, the refcounted handle) and a `T* ptr_`. It has a non-trivial destructor, because destruction has to dec the flag's refcount. Under C++'s default ABI rules, that bars the type from passing by value in a register on a function call. It has to round-trip through memory, either the stack or a hidden reference parameter.

Then you open Chromium's `weak_ptr.h` and spot an attribute called `TRIVIAL_ABI` hanging off both `WeakPtr` and `WeakReference` (`weak_ptr.h:101,203`). With it on, a type that has a non-trivial destructor gets treated as trivial for the calling convention, and that means register passing, cheap by-value handoff. WeakPtr is exactly two pointers wide; once annotated, handing one between functions costs about the same as handing two pointers.

The first time we saw this, it did not add up. A refcount-holding type, tagged "trivial"? There is a counterintuitive point here worth a whole piece: what `trivial_abi` actually is, what it costs, and how WeakPtr meets its safety conditions. The answer is not "the refcount does not matter." The answer is that the design deliberately put every part where it could be safely annotated.

---

## Trivial vs non-trivial types: the ABI view

C++ sorts types into "trivial" and "non-trivial", and the calling convention treats them differently.

Trivial types are things like `int`, `std::pair<int,int>`, POD structs. Copy, move, and destruction "do nothing", or are bitwise-equivalent to memcpy. At a call site these can be stuffed straight into a register and passed or returned by value, with overhead small enough to ignore.

Non-trivial types are anything with a non-trivial copy constructor, move constructor, or destructor. Passing one usually goes through memory: a slot on the stack, or a hidden reference parameter. The compiler has to make sure those non-trivial operations (dec a refcount on destruction, say) actually run where they are supposed to, so a plain memcpy will not do.

WeakPtr holds a `scoped_refptr` (its destructor decs the refcount), so it is non-trivial. Under the default ABI, passing one means going through memory; no register.

---

## What [[clang::trivial_abi]] is

`[[clang::trivial_abi]]` is a Clang type attribute, and Chromium wraps it in a `TRIVIAL_ABI` macro. When `__has_cpp_attribute(clang::trivial_abi)` is true the macro expands to the attribute, otherwise to nothing (`base/compiler_specific.h`).

Stated plainly, it does one thing: make a non-trivial type get treated as trivial by the calling convention. Two effects follow. First, by-value parameters and return values can go through registers, on the same footing as a trivial type. Second, the object becomes "relocatable", meaning the two steps "move plus destroy the source" collapse into a single memcpy.

There is a precondition the Clang documentation is explicit about: the annotated type must be trivially relocatable. That means: bitwise-moving the object from one place to another and then skipping the source's destructor must be semantically equivalent to running a move construction plus destroying the source. When that holds, the compiler can substitute memcpy for move-plus-destroy and reach for a register without worry.

---

## The cost: destructor timing and location change

`trivial_abi` is not a free lunch. It moves where and when the destructor runs.

For an ordinary non-trivial type, the ABI pins down exactly which stack frame the temporary destructs in. Slap `trivial_abi` on it and the object may get constructed in the caller's frame, packed into a register, handed over, and destructed in the callee's frame. The landing site of the destructor shifts. If the type really is trivially relocatable, that does not matter (move-plus-destroy is equivalent to memcpy anyway). If it is not, say the destructor has a side effect that cannot be skipped, then annotating it anyway will produce bugs.

That is the cost. The Clang documentation warns about it on purpose: `trivial_abi` only goes on types that genuinely qualify. It is not something to paste on at random.

---

## Why WeakPtr can carry the annotation safely

So WeakPtr holds a refcount (through `scoped_refptr<Flag>`). How does it satisfy "trivially relocatable"? Let us take it apart piece by piece.

First, `T* ptr_`. It is a raw pointer. Copy, move, and destruction are all trivial (bitwise), and they do not touch inc or dec at all. A raw pointer is trivially relocatable on its own, no obstacle to `trivial_abi`.

The real question is the `scoped_refptr<const Flag>` inside `WeakReference ref_`. It is not trivial: copy incs the refcount, destruction decs it. Is it trivially relocatable?

Walk through it. What does scoped_refptr's move constructor do? It steals the source's raw pointer (`ptr_ = other.ptr_; other.ptr_ = nullptr;`) without inc-ing. When the source is then destroyed, its `ptr_` is already nullptr, so `release()` walks out as a no-op. Net effect across the two steps: the raw pointer is bitwise moved over, no inc, no dec, the refcount does not budge.

That is exactly the definition of "trivially relocatable." So scoped_refptr is non-trivial but trivially relocatable. WeakPtr holds one and inherits the property; together with the always-trivial `ptr_`, the whole WeakPtr satisfies the `trivial_abi` precondition.

There is one more guarantee that is easy to miss. `trivial_abi` makes the destruction location indeterminate, so a given WeakPtr instance may end up being destroyed on an "unexpected" thread (the caller's frame, say), and the scoped_refptr it holds will dec the flag's refcount there. That requires the flag's refcount operations to be cross-thread safe, which is exactly what `RefCountedThreadSafe` (atomic inc/dec) backs up, as covered in [prerequisite (I)](./pre-01-weak-ptr-intrusive-refcount-and-scoped-refptr.md). Add the `HasOneRef()` cross-thread destruction exemption inside `Flag::Invalidate`, and the flag dropping to zero and destructing on any thread is safe. Without that layer, `trivial_abi` on top of "destructor site drift" would eventually bite in a cross-thread scenario.

---

## This is design avoidance, not an inherent property

There is a conclusion worth stressing here, so you do not take the same idea to some other type and break things.

`trivial_abi` is not inherently safe for types that hold a refcount handle. WeakPtr can be annotated because its members were deliberately made trivially relocatable. That is different from "any refcounted type can slap this attribute on."

A counterexample. Suppose you write a class that manages its own refcount by hand (`++count_` in the copy constructor, `--count_` in the destructor with cleanup on zero) and you do not guarantee "move plus destroy-source equals memcpy." Forcing `trivial_abi` onto it lets the compiler drop a destructor at the wrong place or time. The refcount drifts, and at the mild end you leak, at the bad end you double-free. The Clang documentation and Chromium's own comments both warn about this.

Back to WeakPtr. Its safety stands on three pillars. `ptr_` is a raw pointer, trivial by nature. `scoped_refptr` is non-trivial but trivially relocatable, with move-plus-destroy equivalent to memcpy. `Flag` uses `RefCountedThreadSafe`, with `HasOneRef()` covering the cross-thread destruction case. Pull any pillar and the whole thing falls. So when you see WeakPtr tagged `TRIVIAL_ABI` and reaping the register-passing win, that "zero overhead" is what the design work up front paid for. The overhead did not vanish, it got dissolved by design ahead of time. This is Chromium's zero-overhead abstraction philosophy.

That closes out the seven prerequisites: pre-00 on weak references in general, plus pre-01 through pre-06 on the six parts, intrusive refcount, atomics and memory order, sequences and DCHECK/CHECK, concepts, template friend and uintptr_t, and TRIVIAL_ABI. The parts are collected. Next we start bolting them together into the core skeleton of a real WeakPtr, and see how the seven pieces mesh into one industrial-grade weak pointer.

## References

- [Clang documentation: the `trivial_abi` attribute](https://clang.llvm.org/docs/AttributeReference.html#trivial-abi)
- [cppreference: TrivialType and triviality](https://en.cppreference.com/w/cpp/named_req/TrivialType)
- [Chromium `base/compiler_specific.h`, the TRIVIAL_ABI macro](https://source.chromium.org/chromium/chromium/src/+/main:base/compiler_specific.h)
- [Chromium `base/memory/weak_ptr.h`, annotations at lines 101/203](https://source.chromium.org/chromium/chromium/src/+/main:base/memory/weak_ptr.h)
