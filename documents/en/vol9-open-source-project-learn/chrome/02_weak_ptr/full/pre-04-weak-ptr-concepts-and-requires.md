---
chapter: 0
cpp_standard:
- 20
description: "Builds on the 01 series' concepts groundwork and zooms in on how WeakPtr uses std::convertible_to and a member-function requires clause to weld const correctness and upcast constraints into the type signature"
difficulty: intermediate
order: 4
platform: host
prerequisites:
- 'OnceCallback prerequisite (IV): Concepts and requires constraints'
- 'WeakPtr prerequisite (0): weak references and the lifetime puzzle'
reading_time_minutes: 10
related:
- 'WeakPtr hands-on (I): motivation and API design'
- 'WeakPtr prerequisite (V): template friend and uintptr_t type erasure'
tags:
- host
- cpp-modern
- intermediate
- concepts
- 类型安全
- weak_ptr
title: "WeakPtr prerequisite (IV): applying concepts and requires"
translation:
  source: documents/vol9-open-source-project-learn/chrome/02_weak_ptr/full/pre-04-weak-ptr-concepts-and-requires.md
  source_hash: c5f666b4b4f2f4beb7491001830b1100c10401575b4be208cbbe31d4ea1147f4
  translated_at: '2026-09-26T01:24:07+00:00'
  engine: anthropic
  token_count: 2200
---
# WeakPtr prerequisite (IV): applying concepts and requires

## First, lay the tools on the table

The [previous piece](../../01_once_callback/full/pre-04-once-callback-concepts-and-requires.md) covered the fundamentals of concepts — how a `requires` clause attaches, how constraints short-circuit. We won't repeat any of that here; instead we go straight to the two real jobs WeakPtr puts concepts to: policing the legality of upcasts (can a `WeakPtr<Derived>` be handed to a `WeakPtr<Base>`?), and const correctness (a const factory is not allowed to hand out a mutable weak pointer).

Both spots amount to a mere handful of lines in Chromium's `weak_ptr.h`, and we nearly slid right past them on the first read. Yet they are precisely the most typical engineering use of concepts: a reader glances at the `requires(...)` on the signature and knows under which type relationships the constructor exists — no digging through comments, no chasing implementations. Let's recap the idea first, then take the two in turn and see where each lands.

A one-line recap (details are back in the 01 piece): a concept is a compile-time predicate, and `requires(expr)` hangs it on a template parameter or a member function, meaning "this template / overload exists only when the predicate is true." WeakPtr leans on two predicates this time. `std::convertible_to<U*, T*>` asks whether `U*` can implicitly convert to `T*` — same type and derived-to-public-base both count, which covers exactly the upcast business. `std::is_const_v<T>` asks whether `T` carries a top-level const; paired with a `!`, it separates `WeakPtrFactory<T>` from `WeakPtrFactory<const T>`.

---

## The converting constructor: upcasting WeakPtr\<U\> to WeakPtr\<T\>

Start with the plainest requirement. You hold a `WeakPtr<Derived>` and need a `WeakPtr<Base>`; instinct says you should just be able to pass it over — `Derived*` converts to `Base*` anyway. The reverse does not work (`Base*` cannot be stretched into `Derived*`), and nonsense like converting `WeakPtr<int>` to `WeakPtr<Foo>` is not even worth thinking about.

Chromium carves that rule into the signature with a single `requires` clause (`weak_ptr.h:211-214`):

```cpp
template <typename T>
class WeakPtr {
public:
    // ...
    template <typename U>
        requires(std::convertible_to<U*, T*>)
    WeakPtr(const WeakPtr<U>& other) : ref_(other.ref_), ptr_(other.ptr_) {}

    template <typename U>
        requires(std::convertible_to<U*, T*>)
    WeakPtr(WeakPtr<U>&& other)
        : ref_(std::move(other.ref_)), ptr_(std::move(other.ptr_)) {}
    // the corresponding operator= follows the same pattern
};
```

A few details to catch. First, this is a member template, not an ordinary constructor — the outer `WeakPtr<T>` has already pinned `T` down, and the inner layer templates out a fresh `U`, meaning "construct me from any `WeakPtr<U>`." Then `requires(std::convertible_to<U*, T*>)` hangs on that member template, and it participates in overload resolution only when the pointers convert. One more spot that is easy to miss: this is a separate path from the default copy and move constructors (the comment spells it out: "separate from the (implicit) copy and move constructors") — don't tangle the two together.

The effect looks like this:

```cpp
struct Base { virtual ~Base() = default; };
struct Derived : Base {};

WeakPtr<Derived> wd = factory_derived.get_weak_ptr();
WeakPtr<Base> wb = wd;           // ✓ Derived* → Base* is legal

WeakPtr<Base> wb2 = wb;          // ✓ same type, also goes through convertible_to (B* → B*)
WeakPtr<Derived> wd2 = wb;       // ✗ Base* → Derived* is illegal, this constructor drops out, compile error
WeakPtr<int> wi = wb;            // ✗ Base* → int* is illegal, compile error
```

The last two errors are stopped right at compile time. Because this is concepts rather than SFINAE, the compiler's diagnostic points bluntly at "constraints not satisfied" instead of dumping a faceful of template-substitution stack on you. With the type contract moved onto the signature, whether a conversion should happen and whether it can happen are readable right off the signature.

### Why not SFINAE

The old way looked like this:

```cpp
// Old-style SFINAE: poor readability, terrible error messages
template <typename U,
          typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
WeakPtr(const WeakPtr<U>& other) : ref_(other.ref_), ptr_(other.ptr_) {}
```

It is functionally equivalent, but the `typename = std::enable_if_t<...>` trick — jamming a made-up default template argument in out of thin air — reads far more awkwardly than `requires(...)`, and when the constraint fails, the compiler can only grind through the details of the substitution failure for you. Once Chromium migrated to C++20, new code went with concepts across the board; these few lines in WeakPtr are a product of that migration.

---

## Member-function requires: const correctness and the mutable overload

Over on the WeakPtrFactory side there is a more interesting move: hanging `requires` directly on a member function and choosing the overload based on whether `T` is const. Look at `GetWeakPtr` (`weak_ptr.h:374-384`):

```cpp
template <class T>
class WeakPtrFactory : public internal::WeakPtrFactoryBase {
public:
    // const version: the factory is const, can only hand out WeakPtr<const T>
    WeakPtr<const T> GetWeakPtr() const {
        return WeakPtr<const T>(weak_reference_owner_.GetRef(),
                                reinterpret_cast<const T*>(ptr_));
    }

    // non-const version: the factory is not const, hands out WeakPtr<T> (mutable)
    WeakPtr<T> GetWeakPtr()
        requires(!std::is_const_v<T>)
    {
        return WeakPtr<T>(weak_reference_owner_.GetRef(),
                          reinterpret_cast<T*>(ptr_));
    }
    // ...
};
```

There is a small piece of cleverness tucked in here that we did not catch on the first read either. `WeakPtrFactory<T>` carries two `GetWeakPtr` overloads at once: a `const` member function returning `WeakPtr<const T>`, and a non-`const` member function returning `WeakPtr<T>` — and the latter insists on carrying `requires(!std::is_const_v<T>)`.

Why does that `requires` deserve to be there? Think about `WeakPtrFactory<const Foo>` — now `T = const Foo`, and `std::is_const_v<T>` is true. If the non-const `GetWeakPtr()` were unconstrained, it would be instantiated as `WeakPtr<const Foo> GetWeakPtr()` (a non-const member), with a return type identical to the const version's but different constness — overload resolution either goes ambiguous or picks wrong. `requires(!std::is_const_v<T>)` chokes this non-const overload off when `T` itself is const, leaving only the const version, and the semantics turn clean: if the factory is const, or `T` is const, you can only get `WeakPtr<const T>`; only when the factory is non-const and `T` is non-const do you get a mutable `WeakPtr<T>`.

This set of constraints shoves const correctness into the type system, and deep. From a const object, at the type level, you simply cannot obtain a `WeakPtr<T>` that points at its mutable state — no runtime discipline required, the compiler watches it for you. `GetMutableWeakPtr()` (`weak_ptr.h:386-391`) uses the same `requires(!std::is_const_v<T>)`, guaranteeing that the "mutable" path exists only when the type allows it.

### A minimal reproduction

Let's roll our own minimal version and verify that the constraint really bites at compile time:

```cpp
// Platform: host | C++ Standard: C++20
#include <concepts>
#include <type_traits>

struct Base { virtual ~Base() = default; };
struct Derived : Base {};

template <typename T>
class MiniWeakPtr {
public:
    MiniWeakPtr() = default;
    // upcast converting constructor
    template <typename U>
        requires(std::convertible_to<U*, T*>)
    MiniWeakPtr(const MiniWeakPtr<U>&) {}
};

int main() {
    MiniWeakPtr<Derived> wd;
    MiniWeakPtr<Base> wb = wd;          // ✓
    // MiniWeakPtr<Derived> wd2 = wb;   // ✗ compile error: Base* → Derived* does not satisfy convertible_to
    return 0;
}
```

Uncomment that line and the compiler jumps on the spot (Clang reports `constraints not satisfied`, GCC reports `conversion from ... to non-scalar type ... requested`; the wording differs, but the meaning is the same: "constraint not satisfied"). Which type relationships are legal and which are not has moved out of comments and into the compiler's checklist.

---

## Why this deserves its own piece

You might mutter: it's just two lines of `requires`, does it really need a whole piece? It does. These two spots are the safety net on WeakPtr's type signature, and they carry more weight than they look.

The converting-constructor constraint blocks "doing an unsafe downcast through WeakPtr." That is a commonly overlooked UAF entry point — downcast to a pointer of the wrong type, and the first access is out-of-bounds or UB, something you cannot catch at runtime. Hang `requires(std::convertible_to<U*, T*>)` on it, and that class of error dies at compile time. The const-overload constraint minds the other end: the rule that "a const object cannot have its state mutated through its weak reference" no longer needs a human standing guard — the type system backs us up.

One level deeper, their value is as a model of the engineering use of concepts — not a showy flourish, but semantic constraints written out in the type system so the interface explains itself. When we build the skeleton in 02-2, we will copy these two `requires` clauses, and you will find they match the Chromium source almost word for word.

---

That wraps up the two uses of concepts inside WeakPtr. The converting constructor carries `requires(std::convertible_to<U*, T*>)`, making `WeakPtr<Derived> → WeakPtr<Base>` legal and rejecting the reverse and unrelated conversions at compile time; the member functions `GetWeakPtr` / `GetMutableWeakPtr` carry `requires(!std::is_const_v<T>)`, pushing const correctness into the type system so a const object cannot obtain a mutable weak pointer. Both demonstrate the same thing — the real value of concepts is moving the semantic contract onto the signature and letting the compiler enforce the rules a human used to have to watch.

WeakPtr holds one more template trick — using `template friend` to solve cross-type private access, plus why `WeakPtrFactory` stores its pointer as a `uintptr_t`. Both are taken apart in the piece right after this one.

## References

- [cppreference: std::convertible_to](https://en.cppreference.com/w/cpp/concepts/convertible_to)
- [cppreference: requires clause](https://en.cppreference.com/w/cpp/language/constraints)
- [OnceCallback prerequisite (IV): Concepts and requires constraints](../../01_once_callback/full/pre-04-once-callback-concepts-and-requires.md)
- [Chromium `base/memory/weak_ptr.h`](https://source.chromium.org/chromium/chromium/src/+/main:base/memory/weak_ptr.h)
