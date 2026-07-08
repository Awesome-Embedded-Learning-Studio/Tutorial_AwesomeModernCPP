---
chapter: 0
cpp_standard:
- 20
description: "Builds on the 01 series concepts foundation and zooms in on how WeakPtr uses std::convertible_to and a member-function requires clause to weld const correctness and upcast legality into the type signature"
difficulty: intermediate
order: 4
platform: host
prerequisites:
- OnceCallback prerequisites (IV): Concepts and requires constraints
- WeakPtr prerequisite (0): weak references and the lifetime puzzle
reading_time_minutes: 10
related:
- WeakPtr hands-on (I): motivation and API design
- WeakPtr prerequisite (V): template friend and uintptr_t type erasure
tags:
- host
- cpp-modern
- intermediate
- concepts
- 类型安全
- weak_ptr
title: "WeakPtr prerequisite (IV): concepts and requires inside WeakPtr"
---
# WeakPtr prerequisite (IV): concepts and requires inside WeakPtr

## Lay the tools on the table first

The [previous piece](../../01_once_callback/full/pre-04-once-callback-concepts-and-requires.md) already covered the basics of concepts: how a `requires` clause attaches, how constraints short-circuit. We won't repeat that here. Instead we go straight to the two real jobs WeakPtr delegates to concepts: policing whether an upcast is legal (can a `WeakPtr<Derived>` feed a `WeakPtr<Base>`), and enforcing const correctness (a const factory is not allowed to hand out a mutable weak pointer).

Both spots are only a handful of lines in Chromium's `weak_ptr.h`, and we nearly slid past them on the first read. They are the most typical engineering use of concepts. The reader glances at the `requires(...)` on the signature and immediately knows under what type relationships that constructor exists, with no comment-chasing or implementation-diving. We recap the idea briefly, then take each in turn.

One-line recap (details back in the 01 piece): a concept is a compile-time predicate, and `requires(expr)` hangs it on a template parameter or member function, meaning "this template / overload only exists when the predicate is true." WeakPtr leans on two predicates here. `std::convertible_to<U*, T*>` asks whether `U*` can implicitly convert to `T*`. Same type and derived-to-public-base both count, which is exactly the upcast case. `std::is_const_v<T>` asks whether `T` has a top-level const, and paired with `!` it separates `WeakPtrFactory<T>` from `WeakPtrFactory<const T>`.

---

## The converting constructor: WeakPtr\<U\> to WeakPtr\<T\> upcast

Start with the plainest need. You hold a `WeakPtr<Derived>` and need a `WeakPtr<Base>`. The instinct is that it should just work. `Derived*` already converts to `Base*`. The reverse does not (`Base*` cannot stretch into `Derived*`), and nonsense like `WeakPtr<int>` to `WeakPtr<Foo>` is even less thinkable.

Chromium carves that rule into the signature with one `requires` clause (`weak_ptr.h:211-214`):

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

A few details to catch. First, this is a member template, not an ordinary constructor. The outer `WeakPtr<T>` already fixed `T`; the inner layer templates out a fresh `U`, meaning "construct me from any `WeakPtr<U>`." Then `requires(std::convertible_to<U*, T*>)` hangs on that member template, and the overload only participates when the pointers convert. One more spot that's easy to miss: this is a separate path from the default copy and move constructors (the comment spells out "separate from the (implicit) copy and move constructors"). Don't tangle them together.

The effect looks like this:

```cpp
struct Base { virtual ~Base() = default; };
struct Derived : Base {};

WeakPtr<Derived> wd = factory_derived.get_weak_ptr();
WeakPtr<Base> wb = wd;           // OK: Derived* -> Base* is legal

WeakPtr<Base> wb2 = wb;          // OK: same type, also passes convertible_to (B* -> B*)
WeakPtr<Derived> wd2 = wb;       // FAIL: Base* -> Derived* is illegal, this constructor drops out, compile error
WeakPtr<int> wi = wb;            // FAIL: Base* -> int* is illegal, compile error
```

The last two errors die at compile time. Because this uses concepts rather than SFINAE, the compiler points straight at "constraints not satisfied" instead of dumping a template-substitution stack on you. Move the type contract onto the signature, and whether a conversion is allowed is readable right there.

### Why not SFINAE

The old way looked like this:

```cpp
// Old-style SFINAE: poor readability, awful error messages
template <typename U,
          typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
WeakPtr(const WeakPtr<U>& other) : ref_(other.ref_), ptr_(other.ptr_) {}
```

Functionally equivalent. But the `typename = std::enable_if_t<...>` trick of jamming a fake default template parameter in reads far worse than `requires(...)`, and on a constraint failure the compiler can only dig through substitution-failure details for you. Once Chromium moved to C++20, new code went concepts across the board, and these few lines in WeakPtr are the migrated product.

---

## Member-function requires: const correctness and the mutable overload

WeakPtrFactory has a more interesting move. It hangs `requires` directly on a member function and picks the overload based on whether `T` is const. Look at `GetWeakPtr` (`weak_ptr.h:374-384`):

```cpp
template <class T>
class WeakPtrFactory : public internal::WeakPtrFactoryBase {
public:
    // const overload: factory is const, can only hand out WeakPtr<const T>
    WeakPtr<const T> GetWeakPtr() const {
        return WeakPtr<const T>(weak_reference_owner_.GetRef(),
                                reinterpret_cast<const T*>(ptr_));
    }

    // non-const overload: factory is not const, hands out WeakPtr<T> (mutable)
    WeakPtr<T> GetWeakPtr()
        requires(!std::is_const_v<T>)
    {
        return WeakPtr<T>(weak_reference_owner_.GetRef(),
                          reinterpret_cast<T*>(ptr_));
    }
    // ...
};
```

There's a small trick here we did not catch on the first pass. `WeakPtrFactory<T>` carries two `GetWeakPtr` overloads at once. One is a `const` member function returning `WeakPtr<const T>`; the other is a non-`const` member function returning `WeakPtr<T>`, and that second one carries `requires(!std::is_const_v<T>)`.

Why does it need that `requires`? Think about `WeakPtrFactory<const Foo>`. Now `T = const Foo`, so `std::is_const_v<T>` is true. Without the constraint, the non-const `GetWeakPtr()` would instantiate as `WeakPtr<const Foo> GetWeakPtr()` (a non-const member) with the same return type as the const version but different constness, and overload resolution either goes ambiguous or picks wrong. `requires(!std::is_const_v<T>)` cuts this non-const overload out when `T` is itself const, leaving only the const version. The semantics clean up: if the factory is const, or `T` is const, you only ever get `WeakPtr<const T>`; only when the factory is non-const and `T` is non-const do you get a mutable `WeakPtr<T>`.

This constraint shoves const correctness deep into the type system. From a const object, you cannot obtain a `WeakPtr<T>` that points at its mutable state at the type level. No runtime discipline needed, the compiler minds it for you. `GetMutableWeakPtr()` (`weak_ptr.h:386-391`) uses the same `requires(!std::is_const_v<T>)` to guarantee the "mutable" path exists only when the type allows it.

### A minimal reproduction

Let's roll our own minimal version and verify the constraint really bites at compile time:

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
    MiniWeakPtr<Base> wb = wd;          // OK
    // MiniWeakPtr<Derived> wd2 = wb;   // FAIL: Base* -> Derived* does not satisfy convertible_to
    return 0;
}
```

Uncomment that line and the compiler jumps on it (Clang reports `constraints not satisfied`, GCC reports `conversion from ... to non-scalar type ... requested`; the wording differs, the meaning is the same: "constraint not satisfied"). Which type relationships are legal and which are not has moved out of comments and into the compiler's checklist.

---

## Why this deserves its own piece

You might be muttering: it's two lines of `requires`, does it really need a whole piece? It does. These two spots are the safety net sitting on WeakPtr's type signature, and they carry more weight than they look.

The converting-constructor constraint blocks "doing an unsafe downcast through WeakPtr." This is an overlooked UAF entry point. Downcast to the wrong type and one dereference is an out-of-bounds access or UB, and at runtime you won't catch it. Hang `requires(std::convertible_to<U*, T*>)` on it and that class of error dies at compile time. The const-overload constraint minds the other end: it makes the rule "a const object cannot be mutated through its weak reference" something the type system holds up for us, with nobody standing guard.

The deeper value is that they model the engineering use of concepts. Not a parlor trick. Semantic constraints written out in the type system so the interface explains itself. When we build the skeleton in 02-2 we copy these two `requires` clauses almost verbatim from the Chromium source.

---

That covers both uses of concepts inside WeakPtr. The converting constructor carries `requires(std::convertible_to<U*, T*>)`, making `WeakPtr<Derived> -> WeakPtr<Base>` legal and rejecting the reverse and unrelated conversions at compile time. The member functions `GetWeakPtr` / `GetMutableWeakPtr` carry `requires(!std::is_const_v<T>)`, pushing const correctness into the type system so a const object cannot obtain a mutable weak pointer. Both demonstrate the same point: the real payoff of concepts is moving semantic contracts onto the signature, so the compiler minds the rules a human used to have to.

WeakPtr has one more template trick up its sleeve, using `template friend` to solve cross-type private access, and there's the question of why `WeakPtrFactory` stores its pointer as a `uintptr_t`. That's in the piece right after this one.

## References

- [cppreference: std::convertible_to](https://en.cppreference.com/w/cpp/concepts/convertible_to)
- [cppreference: requires clause](https://en.cppreference.com/w/cpp/language/constraints)
- [OnceCallback prerequisites (IV): Concepts and requires constraints](../../01_once_callback/full/pre-04-once-callback-concepts-and-requires.md)
- [Chromium `base/memory/weak_ptr.h`](https://source.chromium.org/chromium/chromium/src/+/main:base/memory/weak_ptr.h)
