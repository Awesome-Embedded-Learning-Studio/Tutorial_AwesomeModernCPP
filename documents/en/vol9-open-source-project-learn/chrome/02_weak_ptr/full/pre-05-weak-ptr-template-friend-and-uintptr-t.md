---
chapter: 0
cpp_standard:
- 11
- 14
- 17
- 20
description: "Unpacks two template-engineering tricks in WeakPtr — template friend for cross-type private access, and uintptr_t sinking pointer storage into a non-template base to curb template bloat — plus the RAW_PTR_EXCLUSION tradeoff"
difficulty: intermediate
order: 5
platform: host
prerequisites:
- 'WeakPtr prerequisite (IV): applying concepts and requires'
- 'WeakPtr prerequisite (I): intrusive reference counting and scoped_refptr'
reading_time_minutes: 10
related:
- 'WeakPtr Hands-on (II): The Core Skeleton and Control Block'
tags:
- host
- cpp-modern
- intermediate
- 模板
- 泛型
- 内存管理
- weak_ptr
title: "WeakPtr prerequisite (V): template friend and uintptr_t type erasure"
translation:
  source: documents/vol9-open-source-project-learn/chrome/02_weak_ptr/full/pre-05-weak-ptr-template-friend-and-uintptr-t.md
  source_hash: 15631d87c669db9833733b0b00bdfceb61e68b1543d5552c1cbaf11d6510f523
  translated_at: '2026-09-26T01:33:14+00:00'
  engine: anthropic
  token_count: 5300
---
# WeakPtr prerequisite (V): template friend and uintptr_t type erasure

Reading WeakPtr this far, we have been steering around two details, and now they have to be pulled out and given their own turn. One hides in the converting constructor — when `WeakPtr<U>` is promoted to `WeakPtr<T>`, the code reaches straight in and reads the other object's private members, and a single `template friend` line is what holds that up. The other hides in `WeakPtrFactory`'s base class, which stores the pointer as a `uintptr_t` rather than a `T*`. That sounds like a pointless detour, but chase it down and it turns out to be a practical move for keeping template bloat down.

Neither of these is language-level showmanship. Once we are through them, WeakPtr's slightly contorted class hierarchy falls into place.

---

## Cross-type friendship: WeakPtr\<U\> accessing WeakPtr\<T\>

Here is the converting constructor from [prerequisite (IV)](./pre-04-weak-ptr-concepts-and-requires.md) once more:

```cpp
template <typename U>
    requires(std::convertible_to<U*, T*>)
WeakPtr(const WeakPtr<U>& other) : ref_(other.ref_), ptr_(other.ptr_) {}
```

On the first look nothing seemed off — until it dawned on us that it reads `other.ref_` and `other.ptr_` directly, and those two are private members of `WeakPtr<U>`. Private means, by the rules, that only the class itself and its friends may touch them; and `WeakPtr<T>` and `WeakPtr<U>` (when `U != T`) are two completely distinct types that cannot see into each other by default.

So how did this constructor compile? It rides on one line of template friendship (`weak_ptr.h:291-292`):

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

The line `template <typename U> friend class WeakPtr;` means: whatever `U` you plug in, the resulting `WeakPtr<U>` counts as a friend of this class — every last one of them. One stroke of the brush tears down the private walls between all instantiated versions.

This is a routine move in template code. Whenever "different instantiations of the same template need to access each other" — a converting constructor, `WeakPtr<Base>` and `WeakPtr<Derived>` copying each other's internal state — the template friend is the answer. The only difference from the non-template `friend class Foo;` is one added layer, `template <typename U>`, and the semantics are "list every instantiation of this template as a friend."

Incidentally, Chromium also lists `WeakPtrFactory<T>` as a friend (`weak_ptr.h:293-294`) — when the factory mints a pointer it has to construct `WeakPtr` directly and write into its private members. That friend list is, in essence, the roster of "who needs to touch the innards directly."

---

## uintptr_t: storing the pointer as an integer

The `WeakPtrFactory` class hierarchy looks like this (`weak_ptr.h:332-340`):

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

Stare at the `uintptr_t ptr_;` line for a moment. The base class does not store the pointer as a `T*`; it stores it as an integer. The derived class's constructor takes `reinterpret_cast<uintptr_t>(ptr)` to flatten the `T*` into an integer and stuffs it in; later, when it is used (`GetWeakPtr`), `reinterpret_cast<T*>(ptr_)` converts it back (`weak_ptr.h:375,383`).

Would storing a plain `T*` work? Functionally, perfectly well. So why the detour — to curb template bloat.

`WeakPtrFactory<T>` is a template, so every distinct `T` gets its own full instantiation of the class code. If `ptr_` were a `T*`, then this "store the pointer, read the pointer" routine — which has absolutely nothing to do with `T` — would be generated once per `T` by the compiler. Yet the machine code generated would be identical every time: a pointer is a pointer, and at the machine level a `T*` is nothing more than an address.

Swap `ptr_` for a `uintptr_t` (an integer type with no relation to `T`) and sink it into a non-template base class, `WeakPtrFactoryBase`, and the trick pays off: the "store the pointer" logic belongs to the non-template base, and no matter what `T` is, the base class code is generated exactly once. The template-derived class keeps only the work that genuinely involves `T` — the type conversions and `GetWeakPtr`'s return type. Scraping the "type-agnostic dirty work" out of the template and dumping it into a non-template base is a classic technique for shaving binary size. In a project like a browser, with thousands of `WeakPtrFactory<X>` instantiations, the bytes saved are plainly visible.

And don't worry about safety. Round-tripping `reinterpret_cast` between `T*` and `uintptr_t` is sanctioned by the standard — pointers may convert to and from integer types large enough to hold them, and `uintptr_t` was designed for exactly this job. The only precondition is that the `T` converted back to must match the one originally stored, and that is guarded by `WeakPtrFactory<T>`'s own type.

---

## RAW_PTR_EXCLUSION: why `ptr_` is not a raw_ptr

This part is specific to the Chromium family, but we think it deserves its own section — it lays out very clearly how a general-purpose tool can work against you in one particular scenario.

While hardening `//base` for memory safety, Chromium replaced large numbers of raw `T*` with `raw_ptr<T>` — a smart-pointer wrapper carrying a PartitionAlloc backup-ref count. The benefit is catching use-after-free: freed memory is not immediately reused but held in quarantine, and anyone who sneaks an access in gets caught on the spot.

Yet `WeakPtr::ptr_` pointedly does not use `raw_ptr<T>`; it stays a bare `T*`, explicitly tagged `RAW_PTR_EXCLUSION` (`weak_ptr.h:311`):

```cpp
// This pointer is only valid when ref_.is_valid() is true. Otherwise, its
// value is undefined (as opposed to nullptr). The pointer is allowed to
// dangle as we verify its liveness through `ref_` before allowing access to
// the pointee. We don't use raw_ptr<T> here to prevent WeakPtr from keeping
// the memory allocation in quarantine, as it can't be accessed through the
// WeakPtr.
RAW_PTR_EXCLUSION T* ptr_ = nullptr;
```

The comment spells it out: `ptr_` is allowed to dangle, full stop. WeakPtr's design is precisely "the object may die first; once the flag flips, `IsValid()` blocks the dereference before it happens." In other words, after the object is destroyed, `ptr_` will for a while legitimately point at freed memory — this is dangling the design itself asked for, not a bug.

And what would happen with `raw_ptr<T>` instead? The dangling pointer would make PartitionAlloc hold that memory in quarantine without reclaiming it (the backup-ref count is still nonzero) until every last WeakPtr is destroyed. Wrap a type that is allowed to dangle by design this way, and you end up dragging a whole swath of memory around for nothing. So WeakPtr retreats to the raw pointer and says it out loud with `RAW_PTR_EXCLUSION`: there is dangling risk here, we know it, it is part of the design — do not wrap this one in raw_ptr.

We sat on this one for a good while. Safety tooling is not a the-more-the-safer proposition; what matters is whether it fits the object's lifetime model. WeakPtr's "dangling allowed, flag at the gate" and raw_ptr's "never dangles, quarantine on free" are two models that push against each other from the start. Force them together and safety gains not one bit, while memory gets dragged down first.

---

## Minimal reproduction: non-template base + template derived

All talk and no practice leaves a shallow impression, so let's strip the layering down to a minimal skeleton and run it once:

```cpp
// Platform: host | C++ Standard: C++17
#include <cstdint>
#include <iostream>

namespace internal {
class FactoryBase {
protected:
    FactoryBase(uintptr_t p) : ptr_(p) {}
    ~FactoryBase() { ptr_ = 0; }            // non-template base: this code is generated only once
    uintptr_t ptr_;                          // the pointer stored as an integer, independent of T
};
}  // namespace internal

template <typename T>
class Factory : public internal::FactoryBase {
public:
    explicit Factory(T* p) : FactoryBase(reinterpret_cast<uintptr_t>(p)) {}

    T* get() const {
        return reinterpret_cast<T*>(ptr_);   // cast back; the type is guaranteed by the template
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

`FactoryBase` has no idea what `T` is; its whole job is "store an integer." The only template bloat left is the thin `Factory<T>` derived layer, while the core storage logic is one copy shared by everyone. WeakPtr's real structure is more complicated than this (it also carries the refcount's flag), but the layering idea is exactly the same.

That closes out the two template-engineering tricks. `template<typename U> friend class WeakPtr;` lets different instantiations of the same template (`WeakPtr<Base>` and `WeakPtr<Derived>`) visit each other's private members — the precondition for the converting constructor to read `other.ref_`/`other.ptr_` directly. `WeakPtrFactoryBase` stores the pointer as a `uintptr_t` sunk into a non-template base class, so the type-agnostic "store the pointer" logic is generated only once, curbing template bloat. And `RAW_PTR_EXCLUSION` is the reverse tradeoff — WeakPtr's model of allowing `ptr_` to dangle runs head-on into `raw_ptr`'s quarantine, so it deliberately steps back to the raw pointer.

One last block of prerequisites remains: `TRIVIAL_ABI` — how WeakPtr, despite having a non-trivial destructor, still gets passed in registers like a trivial type.

## References

- [cppreference: friend declaration and template friends](https://en.cppreference.com/w/cpp/language/friend)
- [cppreference: uintptr_t](https://en.cppreference.com/w/cpp/types/integer)
- [Chromium `base/memory/weak_ptr.h` — the class hierarchy](https://source.chromium.org/chromium/chromium/src/+/main:base/memory/weak_ptr.h)
- [Chromium MiraclePtr / raw_ptr design document](https://chromium.googlesource.com/chromium/src/+/main/docs/unsafe_relocations.md)
