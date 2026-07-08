---
chapter: 1
cpp_standard:
- 17
- 20
description: "Build the WeakPtr quartet layer by layer: RefCountedThreadSafe, AtomicFlag, Flag, WeakReference, scoped_refptr, WeakPtr, and WeakPtrFactory, with sequence checking and lazy binding. Code-dense, talk-light."
difficulty: advanced
order: 2
platform: host
prerequisites:
- 'weak_ptr Design Guide (I): Motivation, API, and the Control Block'
- 'WeakPtr prerequisite (I): intrusive refcounting and scoped_refptr'
- 'WeakPtr prerequisite (II): std::atomic and memory_order'
reading_time_minutes: 13
related:
- 'weak_ptr design guide (III): test strategy and performance'
- 'WeakPtr hands-on (II): the core skeleton and control block'
tags:
- host
- cpp-modern
- advanced
- 智能指针
- weak_ptr
- atomic
- 引用计数
title: "weak_ptr Design Guide (II): Step-by-Step Implementation"
---
# weak_ptr Design Guide (II): Step-by-Step Implementation

> Hands-on track, code-dense. For the full argument behind each line, see [full/02-2](../full/02-2-weak-ptr-core-skeleton-and-control-block.md) and [full/02-3](../full/02-3-weak-ptr-factory-and-last-member.md). The companion compilable project lives in `code/volumn_codes/vol9/full_tutorial_codes/chrome_design/` (`16_weak_ptr_skeleton.cpp`, `17_weak_ptr_factory.cpp`).

The previous piece nailed down the architecture and the why behind it: four layers stacking cleanly into Flag, WeakReference, WeakPtr, and WeakPtrFactory. Getting it straight on paper is one thing; writing the thing line by line is another, and the gotchas outnumber what you'd expect. While putting this together I kept asking myself how the Chromium folks landed on details like "make the destructor private to block external delete," why `WeakPtrFactory` has to sit as the last member, and where exactly the `uintptr_t` template-slimming trick pays off. We'll write the code and pick those apart as we go, from the bottommost refcount all the way up to the factory.

## Layer 0: refcount + atomic flag

You don't build a house without a foundation. The bottom two bricks we already named last time: a thread-safe intrusive refcount base, and a one-shot release/acquire atomic flag. Don't rush past them; these two have their own subtleties.

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
        return ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 1;   // true when decremented to 0
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

The bit worth dwelling on is why `release` uses `acq_rel` and not `release`. The decrement has to read the freshest count, otherwise two threads racing the subtraction can drive it negative. At the moment it hits zero it also has to publish every pre-destructor write to whichever thread takes ownership of the delete. Both ends matter, so `acq_rel`. `AtomicFlag` deliberately narrows `std::atomic<uint_fast8_t>` down to a one-shot semantic: no public clear, release/acquire paired. The meaning is "writes before Set are visible to reads after IsSet," and that is the foundation every WeakPtr relies on to see the invalidation synchronously after `Invalidate`.

## Layer 0.5: the sequence checker (debug-only)

Foundation done. Before we raise the Flag we need a debug-only sequence checker. Chromium's real thing is SequenceToken, a finer notion than thread id that can tell "two sequences running on the same thread" apart. The teaching version here fakes it with thread id, enough to make the idea clear. The neat trick is that under `NDEBUG` the whole class collapses into two no-ops and costs zero bytes. We'll lean on that move repeatedly.

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

## Layer 1: Flag, the refcounted liveness

Foundation in place. Time to raise the Flag we kept invoking last piece. It derives from `RefCountedThreadSafe`, holds one `AtomicFlag` as the liveness bit, plus a lazily bound `SequenceChecker`. Liveness checks, invalidation, and cross-sequence teardown all hang off this single object.

```cpp
namespace tamcpp::chrome::internal {

class Flag : public RefCountedThreadSafe {
public:
    Flag() { seq_.detach_from_sequence(); }              // unbound at construction (lazy)

    void Invalidate() noexcept {
        // Same sequence, or only one ref left (cross-thread teardown allowed)
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
    template <typename> friend class scoped_refptr;      // allow delete when refcount hits zero
    ~Flag() = default;                                   // private: controlled destruction
    mutable SequenceChecker seq_;
    AtomicFlag invalidated_;
};

}  // namespace tamcpp::chrome::internal
```

This block is the heart of the whole mechanism. A few spots I kept tripping over.

Look at the constructor first. That `seq_.detach_from_sequence()` in `Flag()` reads counter-intuitively; detach right after construction? It's lazy binding. A Flag can be constructed on any sequence. The sequence only gets recorded the first time someone touches it (calling `IsValid` or `Invalidate`). The reason is that a Flag is often constructed on one sequence and then handed to WeakPtrs running on another, so binding eagerly at construction time would be wrong.

Making `~Flag()` private is the move that blocks "someone grabbed a raw pointer and delete'd it." A Flag's life is governed solely by its refcount; nobody else gets a say. The `assert`s inside `Invalidate` and `IsValid` mirror Chromium's `DCHECK`, asserting that these calls default to landing on the bound sequence, otherwise the lazy binding breaks. The `assert` in `Invalidate` carries an extra `|| has_one_ref()` escape hatch to permit the legitimate case of "the thread holding the last reference tears it down cross-sequence."

The easy one to miss is `MaybeValid`, which deliberately never touches `seq_`. Why? Because the whole point of that entry point is "let any sequence peek at a rough answer." If it went through the same acquire-plus-sequence-assert path as `IsValid`, a cross-sequence call would mis-bind `seq_`, and every future liveness answer would become untrustworthy. So it walks a looser channel: read the atomic bit, skip the sequence contract. Hold on to that distinction; we come back to it in the testing piece.

## Layer 2: WeakReference + scoped_refptr

With Flag standing, something has to manage its refcount. `scoped_refptr` is the standard intrusive-refcount idiom; here's a minimal version, the complete one lives in the prerequisites. `WeakReference` is the reference side of Flag: it holds a `scoped_refptr<const Flag>` and re-exposes three operations, `IsValid`, `MaybeValid`, `Reset`, basically forwarding Flag's interface.

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

## Layer 3: WeakPtr\<T\>, the user handle

We finally reach the API users actually touch. WeakPtr holds two things internally: a `WeakReference` and a raw pointer `ptr_`. The first decides liveness, the second is what you dereference. The `[[clang::trivial_abi]]` we mentioned last time sits on this class.

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
        assert(ptr);   // only the factory can reach this
    }
    internal::WeakReference ref_;
    T* ptr_ = nullptr;
};

}  // namespace tamcpp::chrome
```

The real trap is the `[[clang::trivial_abi]]` annotation. It tells the compiler: this type has a non-trivial destructor, but you may pass it as if it were trivial, stuff it into registers, memcpy it around. That carries risk. Destructor timing moves forward, and a moved-from object can be observed invalid earlier than you'd expect. Chromium can afford the annotation because `ptr_` is a raw trivial pointer and the `scoped_refptr` half is trivially relocatable, so the whole thing relocates without breaking its invariant. The full argument for that safety precondition lives in [full/pre-06](../full/pre-06-weak-ptr-trivial-abi.md); I'll only flag it here: don't get dazzled and start pasting this onto your own types. Get it wrong and you have a use-after-free.

One more thing to call out: the private constructor plus `friend class WeakPtrFactory` means only the factory can mint a WeakPtr. Nobody outside can conjure one out of thin air. That's the seam where the "minting authority" gets locked down.

## Layer 4: WeakPtrFactory\<T\>

The top layer, and the one users actually instantiate. It owns a `WeakReferenceOwner` (the Flag issuer) plus a member holding the observed object's pointer. The two cooperate on minting, batch invalidation, and destructor-time cleanup.

```cpp
namespace tamcpp::chrome {

class WeakReferenceOwner {    // Flag issuer
public:
    WeakReferenceOwner() : flag_(new internal::Flag()) {}
    ~WeakReferenceOwner() { if (flag_) flag_->Invalidate(); }    // destruction invalidates all
    internal::WeakReference GetRef() const { return internal::WeakReference(flag_); }
    void Invalidate() { flag_->Invalidate(); flag_ = internal::scoped_refptr<internal::Flag>(new internal::Flag()); }  // invalidate + fresh Flag
    void InvalidateAndDoom() { flag_->Invalidate(); flag_ = nullptr; }        // invalidate + stop minting
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
    uintptr_t ptr_;    // non-template-dependent pointer storage (can sink into a base to shrink bloat)
};

}  // namespace tamcpp::chrome
```

Three details here are worth stopping on.

First, the `WeakReferenceOwner` destructor calls `Invalidate`. That single line is the root of the "factory must be the last member" rule. C++ constructs members in declaration order and destroys them in reverse; put the factory last and it's destroyed first, while every other member of the object is still alive. Flag flips, every WeakPtr drops, and that all happens before the rest of the members start falling apart. Put the factory earlier and you get a window: the factory destructs, Flag invalidates, but then `buf_` and friends destruct too, and somewhere in between a WeakPtr can still read as valid while the object's insides are already coming apart. This is not stylistic preference. It's memory safety. The full race argument is in [full/02-3](../full/02-3-weak-ptr-factory-and-last-member.md).

Second, `ptr_` is stored as `uintptr_t`. Looks redundant when it's conceptually a `T*`, so why the `reinterpret_cast` detour? The goal is to sink pointer storage into a non-template base. Think about it: `WeakPtrFactory<Controller>`, `WeakPtrFactory<Service>`, `WeakPtrFactory<a dozen types>`, each instantiating an identical copy of the pointer-manipulation code. That's serious template bloat. Move that part into a non-template base and each T is left with a thin derived slice; the binary savings add up. Chromium has a handful of these "trade `uintptr_t` for template slimming" tricks.

Third, the difference between `Invalidate` and `InvalidateAndDoom`. The former invalidates and immediately mints a fresh Flag, so the factory keeps handing out new WeakPtrs. The latter nulls the Flag pointer outright and stops minting. It's for the "this factory will never be used again" case and saves one heap allocation. The `invalidate_weak_ptrs_and_doom` line also zeroes `ptr_`, so any later call to it trips the `assert`. That guards against misuse after a use-after-free.

## Wiring it together

All four layers up. Let's snap them together and see what using it actually looks like. The Controller below is a typical case: a member function that gets called back asynchronously, a factory parked as the last member, WeakPtrs handed out to callers.

```cpp
struct Controller {
    void on_done(int v) { /* ... */ }
    std::vector<int> buf_;
    WeakPtr<Controller> get_weak() { return weak_factory_.get_weak_ptr(); }
    WeakPtrFactory<Controller> weak_factory_{this};   // last member
};

// After the object dies, the callback no-ops (ties into 01's OnceCallback, see full/02-5)
auto task = bind_weak_once(&Controller::on_done, ctrl.get_weak(), 42);
std::move(task).run();      // ctrl alive → calls; ctrl dead → silent no-op
```

That `bind_weak_once` line is where the OnceCallback thread from the previous piece finally ties off. The core idea is a direct translation of the industrial `InvokeHelper<true>::MakeItSo`: before the callback runs, `if (!receiver) return;`. The receiver is a WeakPtr, so `operator bool` calls `get()`, `get()` checks `IsValid`, and the whole chain lands on an accurate same-sequence liveness read. Object alive means a real call; object dead means a silent no-op. The full callback integration story, how compile-time `kIsWeakMethod` wires up, why `MaybeValid` needs its own channel, where the void-return constraint comes from, lives in [full/02-5](../full/02-5-weak-ptr-bind-integration.md), where I take it apart properly.

The code is all written now. Flag's acquire/release keeps the deref lock-free, lazy sequence binding works, the factory destructor catches teardown, `TRIVIAL_ABI` lands in registers. Every promise landed. But this piece only cared about writing it correctly; it never seriously verified whether the thing holds up at runtime. Does the `MaybeValid` side channel actually race? Is the cross-sequence teardown window really closed? Does a misordered factory actually blow up? Those need tests and TSan to answer, and that's exactly where the next piece goes for the throat.

## References

- [Chromium `base/memory/weak_ptr.{h,cc}`](https://source.chromium.org/chromium/chromium/src/+/main:base/memory/weak_ptr.h)
- [weak_ptr design guide (III): test strategy and performance](./03-weak-ptr-testing.md)
- [WeakPtr hands-on (II): the core skeleton and control block](../full/02-2-weak-ptr-core-skeleton-and-control-block.md)
