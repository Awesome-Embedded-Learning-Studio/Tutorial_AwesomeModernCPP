---
chapter: 1
cpp_standard:
- 17
- 20
description: Implement the WeakPtr quartet layer by layer — RefCountedThreadSafe/AtomicFlag/Flag/WeakReference/WeakPtr/WeakPtrFactory, with sequence checking and lazy binding; code-dense, talk-light
difficulty: advanced
order: 2
platform: host
prerequisites:
- 'weak_ptr Design Guide (I): motivation, API, and the control block'
- 'WeakPtr prerequisite (I): intrusive reference counting and scoped_refptr'
- 'WeakPtr prerequisite (II): std::atomic and memory_order'
reading_time_minutes: 13
related:
- 'weak_ptr Design Guide (III): test strategy and performance comparison'
- 'WeakPtr hands-on (II): the core skeleton and control block'
tags:
- host
- cpp-modern
- advanced
- 智能指针
- weak_ptr
- atomic
- 引用计数
title: "weak_ptr Design Guide (II): step-by-step implementation"
translation:
  source: documents/vol9-open-source-project-learn/chrome/02_weak_ptr/hands_on/02-weak-ptr-implementation.md
  source_hash: b1eade797795600c94c34b8b57c5a3ac8f0ac4c8030fc146f781af2ae3b0359f
  translated_at: '2026-09-26T02:19:39+00:00'
  engine: anthropic
  token_count: 5500
---
# weak_ptr Design Guide (II): step-by-step implementation

> Hands-on track, code-dense. For the detailed arguments, see [full/02-2](../full/02-2-weak-ptr-core-skeleton-and-control-block.md) and [full/02-3](../full/02-3-weak-ptr-factory-and-last-member.md); the companion compilable project lives in `code/volumn_codes/vol9/full_tutorial_codes/chrome_design/` (`16_weak_ptr_skeleton.cpp`, `17_weak_ptr_factory.cpp`).

Last time we got the architecture and the "why" straight, and the four layers stack up rather nicely: Flag, WeakReference, WeakPtr, WeakPtrFactory. But laying it out clearly on paper is one thing; typing the whole thing out line by line is another, and the pits outnumber what you would expect. While writing this piece we kept wondering how the Chromium folks dug out details like "make the destructor private to block outside delete", on what grounds `WeakPtrFactory` absolutely must sit as the last member, and where exactly that `uintptr_t` template-slimming move saves. In this piece we write and dissect at the same time, building from the bottommost reference count all the way up to the factory.

## Layer 0: refcount + atomic flag

You pour the foundation before raising the house. The bottom two bricks we already named last time: a cross-sequence-safe intrusive refcount base class, and a one-shot release/acquire atomic flag. Don't rush to stack on top yet — these two have their own subtleties.

```cpp
// Platform: host | C++ Standard: C++17
#pragma once
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <thread>

namespace tamcpp::chrome::internal {

class RefCountedThreadSafe {
public:
    void add_ref() const noexcept { ref_count_.fetch_add(1, std::memory_order_relaxed); }
    bool release() const noexcept {
        return ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 1;   // true when the count drops to 0
    }
    bool has_one_ref() const noexcept { return ref_count_.load(std::memory_order_acquire) == 1; }
protected:
    RefCountedThreadSafe() = default;
    ~RefCountedThreadSafe() = default;
private:
    mutable std::atomic<int> ref_count_{0};
};

// Mirrors base::AtomicFlag: one-shot, release-Set / acquire-IsSet
class AtomicFlag {
public:
    void Set() noexcept { flag_.store(1, std::memory_order_release); }
    bool IsSet() const noexcept { return flag_.load(std::memory_order_acquire) != 0; }
private:
    std::atomic<uint_fast8_t> flag_{0};
};

}  // namespace tamcpp::chrome::internal
```

Here we want to dwell on why `release` uses `acq_rel` rather than plain `release`. The decrement has to read the freshest count (otherwise two threads subtracting at the same time drive it negative and it is game over), and at the instant the count hits zero it also has to publish "every write made before the destructor" to whichever thread takes over the delete. Both ends are needed, hence `acq_rel`. On the `AtomicFlag` side, `std::atomic<uint_fast8_t>` is deliberately narrowed down to one-shot semantics: no public clear, release/acquire paired. The meaning is "writes before Set are guaranteed visible to reads after IsSet" — and that is precisely the foundation that lets every WeakPtr observe the invalidation synchronously after `Invalidate` later on.

## Layer 0.5: the sequence checker (debug-only)

Foundation poured. Before raising the Flag we still need a debug-only "sequence checker". What Chromium actually uses is SequenceToken, conceptually finer than a thread id — it can tell apart "two sequences running on the same thread"; our teaching version just fakes it with a thread id, enough to get the idea across. Its neatest property is that under release compilation the whole class degenerates into two no-ops and does not occupy a single byte. We will lean on that optimization again and again.

```cpp
#if defined(NDEBUG)
class SequenceChecker {
public:
    void detach_from_sequence() noexcept {}
    bool called_on_valid_sequence() const noexcept { return true; }
};
#else
class SequenceChecker {
public:
    void detach_from_sequence() noexcept { bound_ = std::thread::id{}; }
    bool called_on_valid_sequence() const noexcept {
        if (bound_ == std::thread::id{}) { bound_ = std::this_thread::get_id(); return true; }
        return bound_ == std::this_thread::get_id();
    }
private:
    mutable std::thread::id bound_;
};
#endif
```

## Layer 1: Flag — the refcounted liveness

With the foundation ready, it is time to raise the Flag we kept invoking last piece. It derives from `RefCountedThreadSafe`, holds a single `AtomicFlag` as the liveness bit, plus a lazily bound `SequenceChecker`. The whole business of liveness checking, invalidation, and cross-sequence destruction rests on this one object's shoulders.

```cpp
namespace tamcpp::chrome::internal {

class Flag : public RefCountedThreadSafe {
public:
    Flag() { seq_.detach_from_sequence(); }              // unbound at construction (lazy)

    void Invalidate() noexcept {
        // Same sequence, or only one ref left (cross-thread destruction allowed)
        assert(seq_.called_on_valid_sequence() || has_one_ref());
        invalidated_.Set();                              // release-store
    }
    bool IsValid() const noexcept {
        assert(seq_.called_on_valid_sequence());         // first touch → binds
        return !invalidated_.IsSet();                    // acquire-load
    }
    bool MaybeValid() const noexcept {
        return !invalidated_.IsSet();                    // no sequence assert, callable from any sequence
    }
private:
    template <typename> friend class scoped_refptr;      // allow delete when the refcount hits zero
    ~Flag() = default;                                   // private: controlled destruction
    mutable SequenceChecker seq_;
    AtomicFlag invalidated_;
};

}  // namespace tamcpp::chrome::internal
```

This block is the heart of the whole mechanism; let's look closely at a few spots we tripped over repeatedly.

At first glance the constructor is counter-intuitive — that `seq_.detach_from_sequence()` inside `Flag()`: detach right after construction? It is actually lazy binding: a Flag can be constructed on any sequence, and only when someone truly touches it for the first time (calling `IsValid` or `Invalidate`) does it record the current sequence. The reason is that a Flag is often constructed on one sequence and then carried off by WeakPtrs living on another, so binding hard at construction time would be reckless.

Writing the destructor as `private ~Flag()` is the move that blocks "someone grabbed a raw pointer and delete'd it themselves". A Flag's life is governed by its reference count alone — nobody outside gets a hand in. The `assert`s inside `Invalidate` and `IsValid` correspond to Chromium's `DCHECK`, meaning that by default these two calls must land on the bound sequence — otherwise the lazy binding is broken. The `assert` in `Invalidate` carries an extra `|| has_one_ref()` escape hatch, there to allow the legitimate scenario of "the thread holding the last reference destroys it cross-sequence".

The genuinely easy one to miss is `MaybeValid`, which deliberately never touches `seq_`. Why? Because this entry point's whole design intent is "any sequence may call it for a rough peek" — once it also goes through the same acquire-plus-sequence-assert path as `IsValid`, a cross-sequence call would mis-bind `seq_`, and the liveness answers on the front channel could never be trusted again. So it walks another, looser channel: read the atomic bit only, impose no sequence constraint. Remember this distinction — we will come back to it when the next piece covers the testing strategy.

## Layer 2: WeakReference + scoped_refptr

With the Flag standing, something has to look after its reference count for it. `scoped_refptr` is the standard move for intrusive refcounting; we give a minimal version here — the complete one was covered in the prerequisites. `WeakReference`, then, is the reference-side wrapper around the Flag: it holds a `scoped_refptr<const Flag>` and exposes three operations — `IsValid` / `MaybeValid` / `Reset` — essentially forwarding the Flag's interface.

```cpp
namespace tamcpp::chrome::internal {

template <typename T>
class scoped_refptr {    // simplified, full version in pre-01
public:
    scoped_refptr() noexcept = default;
    explicit scoped_refptr(T* p) noexcept : ptr_(p) { if (ptr_) ptr_->add_ref(); }
    scoped_refptr(const scoped_refptr& o) noexcept : ptr_(o.ptr_) { if (ptr_) ptr_->add_ref(); }
    scoped_refptr(scoped_refptr&& o) noexcept : ptr_(o.ptr_) { o.ptr_ = nullptr; }
    ~scoped_refptr() { if (ptr_ && ptr_->release()) delete ptr_; }
    scoped_refptr& operator=(scoped_refptr r) noexcept { T* t = ptr_; ptr_ = r.ptr_; r.ptr_ = t; return *this; }
    T* get() const noexcept { return ptr_; }
    explicit operator bool() const noexcept { return ptr_ != nullptr; }
private:
    T* ptr_ = nullptr;
};

class WeakReference {
public:
    WeakReference() = default;
    explicit WeakReference(const scoped_refptr<Flag>& flag) : flag_(flag) {}
    bool IsValid() const noexcept { return flag_ && flag_->IsValid(); }
    bool MaybeValid() const noexcept { return flag_ && flag_->MaybeValid(); }
    void Reset() noexcept { flag_ = nullptr; }
private:
    scoped_refptr<const Flag> flag_;
};

}  // namespace tamcpp::chrome::internal
```

## Layer 3: WeakPtr\<T\> — the user handle

Finally, the API users actually get to touch. Internally a WeakPtr holds exactly two things: a `WeakReference` and a raw pointer `ptr_`. Liveness checking rides entirely on the former; the actual dereference uses the latter. And the `[[clang::trivial_abi]]` we mentioned last piece sits right on top of this class.

```cpp
namespace tamcpp::chrome {

template <typename T> class WeakPtrFactory;

template <typename T>
class [[clang::trivial_abi]] WeakPtr {
public:
    WeakPtr() = default;
    WeakPtr(std::nullptr_t) noexcept {}

    template <typename U> requires(std::convertible_to<U*, T*>)   // upcast
    WeakPtr(const WeakPtr<U>& o) noexcept : ref_(o.ref_), ptr_(o.ptr_) {}
    template <typename U> requires(std::convertible_to<U*, T*>)
    WeakPtr(WeakPtr<U>&& o) noexcept : ref_(std::move(o.ref_)), ptr_(o.ptr_) {}

    T* get() const noexcept { return ref_.IsValid() ? ptr_ : nullptr; }
    T& operator*() const { assert(ref_.IsValid()); return *ptr_; }     // Chromium uses CHECK
    T* operator->() const { assert(ref_.IsValid()); return ptr_; }
    explicit operator bool() const noexcept { return get() != nullptr; }
    void reset() noexcept { ref_.Reset(); ptr_ = nullptr; }

    bool maybe_valid() const noexcept { return ref_.MaybeValid(); }
    bool was_invalidated() const noexcept { return ptr_ && !ref_.IsValid(); }
private:
    template <typename U> friend class WeakPtr;
    friend class WeakPtrFactory<T>;
    WeakPtr(internal::WeakReference&& ref, T* ptr) noexcept : ref_(std::move(ref)), ptr_(ptr) {
        assert(ptr);   // only the factory can call this
    }
    internal::WeakReference ref_;
    T* ptr_ = nullptr;
};

}  // namespace tamcpp::chrome
```

The real pit is the `[[clang::trivial_abi]]` annotation. It tells the compiler: this type's destructor is non-trivial, but you may safely pass it as if it were trivial — stuff it into registers, move it around with memcpy, all fine. That in itself is risky: destructor timing gets moved earlier, and a moved-from object may lapse earlier than you expect. Chromium dares to mark it because `ptr_` is a bare trivial pointer and the `scoped_refptr` half is also trivially relocatable, so the whole thing relocates without breaking the invariant. The full argument for that safety precondition lives in [full/pre-06](../full/pre-06-weak-ptr-trivial-abi.md); we will only drop one line here: don't get dazzled and start slapping this onto your own types — get it wrong and you get a UAF.

One more spot to mention: the private constructor plus `friend class WeakPtrFactory` means only the factory can mint a WeakPtr; the outside world cannot conjure one out of thin air. That is the key seam where the "minting authority" gets locked down.

## Layer 4: WeakPtrFactory\<T\>

The last layer, and the thing users actually instantiate. It contains a `WeakReferenceOwner` (the Flag's issuer) plus a member storing the observed object's pointer. The two cooperate on minting, batch invalidation, and destructor-time cleanup.

```cpp
namespace tamcpp::chrome {

class WeakReferenceOwner {    // the Flag's issuer
public:
    WeakReferenceOwner() : flag_(new internal::Flag()) {}
    ~WeakReferenceOwner() { if (flag_) flag_->Invalidate(); }    // destruction invalidates everything
    internal::WeakReference GetRef() const { return internal::WeakReference(flag_); }
    void Invalidate() { flag_->Invalidate(); flag_ = internal::scoped_refptr<internal::Flag>(new internal::Flag()); }  // invalidate + mint a fresh Flag
    void InvalidateAndDoom() { flag_->Invalidate(); flag_ = nullptr; }        // invalidate + never mint again
    bool HasRefs() const { return !flag_->has_one_ref(); }
private:
    internal::scoped_refptr<internal::Flag> flag_;
};

template <typename T>
class WeakPtrFactory {
public:
    WeakPtrFactory() = delete;
    explicit WeakPtrFactory(T* ptr) : ptr_(reinterpret_cast<uintptr_t>(ptr)) { assert(ptr); }
    WeakPtrFactory(const WeakPtrFactory&) = delete;
    WeakPtrFactory& operator=(const WeakPtrFactory&) = delete;

    WeakPtr<const T> get_weak_ptr() const {
        return WeakPtr<const T>(owner_.GetRef(), reinterpret_cast<const T*>(ptr_));
    }
    WeakPtr<T> get_weak_ptr() requires(!std::is_const_v<T>) {
        return WeakPtr<T>(owner_.GetRef(), reinterpret_cast<T*>(ptr_));
    }
    void invalidate_weak_ptrs() { assert(ptr_); owner_.Invalidate(); }
    void invalidate_weak_ptrs_and_doom() { assert(ptr_); owner_.InvalidateAndDoom(); ptr_ = 0; }
    bool has_weak_ptrs() const { return ptr_ && owner_.HasRefs(); }
private:
    WeakReferenceOwner owner_;
    uintptr_t ptr_;    // non-template-dependent pointer storage (can sink into a base to tame bloat)
};

}  // namespace tamcpp::chrome
```

Three details hiding in this code are, we think, the most worth talking about.

The first is the `WeakReferenceOwner` destructor, which calls `Invalidate`. That single line is the root of the iron rule "the factory must sit as the last member". C++ constructs members in declaration order and destroys them in reverse; put the factory last and it gets destroyed first — at which moment every other member of the object is still alive, and the Flag flips over and invalidates all the WeakPtrs, just in time before those members get destroyed one by one. And what if the factory sits earlier? It destructs first, the Flag invalidates, but members like `buf_` are destructed right after — and in between lies a window where a WeakPtr still reads as valid while the object's insides are already coming apart. So this ordering is not a style preference; it is memory safety. The full destruction-time race argument is in [full/02-3](../full/02-3-weak-ptr-factory-and-last-member.md).

The second is `ptr_` being stored as a `uintptr_t`. It looks redundant — it is conceptually a `T*`, so why the reinterpret_cast round trip? The goal is to sink the pointer storage into a non-template base class. Think about it: `WeakPtrFactory<Controller>`, `WeakPtrFactory<Service>`, `WeakPtrFactory<a dozen types>` — each instantiates an identical copy of the pointer-manipulation code, and the template bloat gets scary. Move that part into a non-template base and each T is left with only a thin derived layer; the binary-size savings are considerable. Chromium has plenty of these little "trade a uintptr_t for template slimming" tricks.

The third is the difference between `Invalidate` and `InvalidateAndDoom`. The former invalidates and immediately mints a fresh Flag, meaning the factory can keep issuing new WeakPtrs; the latter nulls the Flag pointer outright and never mints again — that one is prepared for the "this factory will never be used again" scenario, saving one heap allocation. The `invalidate_weak_ptrs_and_doom` line also zeroes `ptr_` while it is at it, so any later call trips the `assert` immediately — a guard against misuse after a use-after-free.

## Putting it all together

All four layers are up. Let's snap them together for one run and see what using this actually looks like. The Controller below is a typical case: a member function that receives async callbacks, a factory parked as the last member, and WeakPtrs handed out to the world.

```cpp
struct Controller {
    void on_done(int v) { /* ... */ }
    std::vector<int> buf_;
    WeakPtr<Controller> get_weak() { return weak_factory_.get_weak_ptr(); }
    WeakPtrFactory<Controller> weak_factory_{this};   // last member
};

// After the object dies the callback no-ops automatically (ties into 01's OnceCallback, see full/02-5)
auto task = bind_weak_once(&Controller::on_done, ctrl.get_weak(), 42);
std::move(task).run();      // ctrl alive → calls; ctrl dead → silent no-op
```

That `bind_weak_once` line is where the loose end we left in the previous OnceCallback piece finally gets tied off. Its core idea is a direct translation of the industrial-grade `InvokeHelper<true>::MakeItSo`: before the callback runs, do `if (!receiver) return;`. This receiver is a WeakPtr, so `operator bool` calls `get()`, `get()` checks `IsValid`, and the chain lands all the way down on an accurate same-sequence liveness read. Object alive means a normal call; object dead means a silent no-op. The full callback-integration machinery — how the compile-time `kIsWeakMethod` gets wired, why `MaybeValid` walks a separate channel, where the void-return constraint comes from — lives in [full/02-5](../full/02-5-weak-ptr-bind-integration.md), where we take it apart in full.

The code is all written out at this point: the Flag's acquire/release guarantees lock-free deref, lazy sequence binding works, the factory destructor catches teardown, and `TRIVIAL_ABI` gets us into registers — every promise that needed to land has landed. But this piece was busy getting the thing right, and never properly verified how it holds up at runtime. Does the `MaybeValid` side channel actually race? Is the cross-sequence destruction window really shut? Does a factory parked in the wrong position actually blow up? Those questions need tests and TSan to answer, and in the next piece we go for exactly those spots with everything we have got.

## References

- [Chromium `base/memory/weak_ptr.{h,cc}`](https://source.chromium.org/chromium/chromium/src/+/main:base/memory/weak_ptr.h)
- [weak_ptr Design Guide (III): test strategy and performance comparison](./03-weak-ptr-testing.md)
- [WeakPtr hands-on (II): the core skeleton and control block](../full/02-2-weak-ptr-core-skeleton-and-control-block.md)
