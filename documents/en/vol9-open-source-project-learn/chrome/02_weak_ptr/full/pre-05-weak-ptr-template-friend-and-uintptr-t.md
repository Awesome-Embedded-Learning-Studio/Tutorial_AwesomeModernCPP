---
chapter: 0
cpp_standard:
- 11
- 14
- 17
- 20
description: "Two template-engineering tricks behind WeakPtr, unpacked: template friend for cross-type private access, and uintptr_t to sink pointer storage into a non-template base and curb template bloat, plus the RAW_PTR_EXCLUSION tradeoff."
difficulty: intermediate
order: 5
platform: host
prerequisites:
- 'WeakPtr prerequisite (IV): concepts and requires in WeakPtr'
- 'WeakPtr prerequisite (I): intrusive refcount and scoped_refptr'
reading_time_minutes: 10
related:
- 'WeakPtr hands-on (II): the core skeleton and control block'
tags:
- host
- cpp-modern
- intermediate
- 模板
- 泛型
- 内存管理
- weak_ptr
title: "WeakPtr prerequisite (V): template friend and uintptr_t type erasure"
---
# WeakPtr prerequisite (V): template friend and uintptr_t type erasure

Two details in WeakPtr have been sitting off to the side while we covered everything else, and now they need their own turn. One hides inside the converting constructor: when `WeakPtr<U>` gets promoted to `WeakPtr<T>`, the constructor reaches straight into the other type's private members, and what makes that legal is a single `template friend` line. The other sits in `WeakPtrFactory`'s base class, which stores the pointer as `uintptr_t` rather than `T*`. That sounds pointless until you chase it down, and then it turns out to be a real move for keeping template bloat in check.

Neither of these is language-level showmanship. Once we are through them, the slightly twisted class hierarchy of WeakPtr falls into place.

---

## Cross-type friendship: `WeakPtr<U>` reaching into `WeakPtr<T>`

Here again is the converting constructor from [prerequisite (IV)](./pre-04-weak-ptr-concepts-and-requires.md):

```cpp
template <typename U>
    requires(std::convertible_to<U*, T*>)
WeakPtr(const WeakPtr<U>& other) : ref_(other.ref_), ptr_(other.ptr_) {}
```

The first time through we did not see anything off about it, until it clicked that it reads `other.ref_` and `other.ptr_` directly, and those are private members of `WeakPtr<U>`. Private means only the class itself and its friends can touch them; and `WeakPtr<T>` and `WeakPtr<U>` (when `U != T`) are two completely different types that cannot see into each other by default.

So how does this compile? Through one line of template friendship (`weak_ptr.h:291-292`):

```cpp
template <typename T>
class WeakPtr {
public:
    // ...
private:
    template <typename U>
    friend class WeakPtr;   // every WeakPtr<U> is a friend of WeakPtr<T>

    internal::WeakReference ref_;
    RAW_PTR_EXCLUSION T* ptr_ = nullptr;
};
```

The line `template <typename U> friend class WeakPtr;` says: whatever `U` you plug in, the resulting `WeakPtr<U>` counts as a friend of this class. One stroke, and the private walls between every instantiation come down.

This is a routine move in templated code. Whenever "the same template, different instantiation" needs to reach across and touch private state (converting constructors, `WeakPtr<Base>` and `WeakPtr<Derived>` copying internals back and forth), template friend is the answer. The only thing separating it from a plain `friend class Foo;` is the extra `template <typename U>`, and the meaning is "list every instantiation of this template as a friend."

For completeness, Chromium also lists `WeakPtrFactory<T>` as a friend (`weak_ptr.h:293-294`), because the factory has to directly construct `WeakPtr` and write its private members when it mints one. That friend list is, in essence, the roster of "who needs to touch the internals."

---

## uintptr_t: storing a pointer as an integer

The class hierarchy of `WeakPtrFactory` looks like this (`weak_ptr.h:332-340`):

```cpp
namespace internal {
class BASE_EXPORT WeakPtrFactoryBase {
protected:
    WeakPtrFactoryBase(uintptr_t ptr);
    ~WeakPtrFactoryBase();
    internal::WeakReferenceOwner weak_reference_owner_;
    uintptr_t ptr_;
};
}  // namespace internal

template <class T>
class WeakPtrFactory : public internal::WeakPtrFactoryBase {
public:
    explicit WeakPtrFactory(T* ptr)
        : WeakPtrFactoryBase(reinterpret_cast<uintptr_t>(ptr)) {}
    // ...
};
```

Look at the `uintptr_t ptr_;` line. The base class does not store the pointer as `T*`; it stores it as an integer. The derived constructor flattens the `T*` into an integer with `reinterpret_cast<uintptr_t>(ptr)` on the way in, and later, when `GetWeakPtr` reads it back, it casts the other way with `reinterpret_cast<T*>(ptr_)` (`weak_ptr.h:375,383`).

Could it just store `T*`? Functionally, yes. So why the detour? To curb template bloat.

`WeakPtrFactory<T>` is a template, and every distinct `T` gets its own full instantiation of the class. If `ptr_` were `T*`, then "store pointer, read pointer", an operation that has nothing to do with `T`, would get generated once per `T`. And the generated machine code would be identical every time, because a pointer is a pointer and `T*` at the machine level is just an address.

Swap `ptr_` for `uintptr_t` (an integer type that says nothing about `T`) and sink it into a non-template base `WeakPtrFactoryBase`, and the trick lands: the "store a pointer" logic now belongs to a non-template base, so no matter what `T` is, that code is generated exactly once. The template-derived class keeps only the work that actually involves `T`: type conversion, the return type of `GetWeakPtr`. Pulling "type-agnostic grunt work" out of the template and dropping it into a non-template base is an old lever for shrinking binary size. In something like a browser, with thousands of `WeakPtrFactory<X>` instantiations, the bytes saved are visible.

The safety side is fine too. `reinterpret_cast` round-tripping between `T*` and `uintptr_t` is recognized by the standard; converting between pointers and a sufficiently large integer type is exactly what `uintptr_t` was put in the language to do. The one precondition is that the `T` cast back out matches the `T` originally stored, and that is enforced by `WeakPtrFactory<T>`'s own type.

---

## RAW_PTR_EXCLUSION: why `ptr_` does not use raw_ptr

This section is a Chromium peculiarity, but we think it earns its own turn, because it lays out the case of "a general-purpose tool that actively backfires in one specific spot" very cleanly.

When Chromium hardened `//base` for memory safety, it swapped a lot of raw `T*` for `raw_ptr<T>`, a smart-pointer wrapper that hangs a PartitionAlloc backup-ref count off the pointee. The win is use-after-free detection: freed memory does not get reused right away, it sits in a quarantine, and if anything reaches in after free it gets caught on the spot.

But `WeakPtr::ptr_` does not use `raw_ptr<T>`. It keeps the raw `T*`, and it carries a `RAW_PTR_EXCLUSION` annotation to say so out loud (`weak_ptr.h:311`):

```cpp
// This pointer is only valid when ref_.is_valid() is true. Otherwise, its
// value is undefined (as opposed to nullptr). The pointer is allowed to
// dangle as we verify its liveness through `ref_` before allowing access to
// the pointee. We don't use raw_ptr<T> here to prevent WeakPtr from keeping
// the memory allocation in quarantine, as it can't be accessed through the
// WeakPtr.
RAW_PTR_EXCLUSION T* ptr_ = nullptr;
```

The comment says it all: `ptr_` is allowed to dangle by design. WeakPtr is built on "the object can die first; as long as the flag flips, deref gets gated by `IsValid()` before it lands." So after the object destructs, `ptr_` legitimately points at freed memory for some stretch of time. That is the design asking for the dangle, not a bug.

What would happen with `raw_ptr<T>`? The dangling pointer would make PartitionAlloc hold that memory in quarantine without reclaiming it (the backup-ref count stays nonzero) until every last WeakPtr is destroyed. A type that is allowed to dangle by design, wrapped that way, drags along a whole swath of memory for nothing. So WeakPtr falls back to the raw pointer and uses `RAW_PTR_EXCLUSION` to spell it out: there is a dangling risk here, we know, it is part of the design, do not wrap it in raw_ptr.

We chewed on this one for a while. A safety tool does not get safer the more you stack it; what matters is whether it fits the object's lifetime model. WeakPtr's "dangling allowed, flag at the gate" and raw_ptr's "never dangles, quarantine on free" are two models that push against each other. Forcing them together buys no extra safety, and the memory cost shows up first.

---

## A minimal reproduction: non-template base plus template derived

Talking through it is one thing; pulling the layering out as a minimal skeleton and running it makes it stick:

```cpp
// Platform: host | C++ Standard: C++17
#include <cstdint>
#include <iostream>

namespace internal {
class FactoryBase {
protected:
    FactoryBase(uintptr_t p) : ptr_(p) {}
    ~FactoryBase() { ptr_ = 0; }            // non-template base: this code is generated once
    uintptr_t ptr_;                          // pointer stored as an integer, independent of T
};
}  // namespace internal

template <typename T>
class Factory : public internal::FactoryBase {
public:
    explicit Factory(T* p) : FactoryBase(reinterpret_cast<uintptr_t>(p)) {}

    T* get() const {
        return reinterpret_cast<T*>(ptr_);   // cast back; the type is upheld by the template
    }
};

struct Foo { int x = 42; };

int main() {
    Foo f;
    Factory<Foo> fac(&f);
    std::cout << fac.get()->x << '\n';       // 42
    return 0;
}
```

`FactoryBase` has no idea what `T` is; it only "stores an integer." What is left of template bloat is the thin `Factory<T>` derived layer, while the storage logic is shared by everyone. The real WeakPtr is more involved than this (it also carries the refcount flag), but the layering idea is exactly the same.

That closes out the two template-engineering tricks. `template<typename U> friend class WeakPtr;` lets different instantiations of the same template (`WeakPtr<Base>` and `WeakPtr<Derived>`) reach into each other's privates, which is what lets the converting constructor read `other.ref_` and `other.ptr_` directly. `WeakPtrFactoryBase` stores its pointer as `uintptr_t` and sinks it into a non-template base, so the type-agnostic "store a pointer" logic is generated once and template bloat stays down. And `RAW_PTR_EXCLUSION` is the reverse trade: the model where `ptr_` is allowed to dangle runs head-on into raw_ptr's quarantine, so WeakPtr deliberately steps back to a raw pointer.

One prerequisite left: `TRIVIAL_ABI`, the attribute that lets a WeakPtr with a non-trivial destructor still get passed in registers like a trivial type.

## References

- [cppreference: friend declaration and template friend](https://en.cppreference.com/w/cpp/language/friend)
- [cppreference: uintptr_t](https://en.cppreference.com/w/cpp/types/integer)
- [Chromium `base/memory/weak_ptr.h`, class hierarchy](https://source.chromium.org/chromium/chromium/src/+/main:base/memory/weak_ptr.h)
- [Chromium MiraclePtr / raw_ptr design document](https://chromium.googlesource.com/chromium/src/+/main/docs/unsafe_relocations.md)
