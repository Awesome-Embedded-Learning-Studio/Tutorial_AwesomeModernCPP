---
chapter: 1
cpp_standard:
- 17
- 20
description: "Implementing WeakPtrFactory — minting, the difference between InvalidateWeakPtrs and AndDoom, and why it must be the last member (a reverse-destruction-order argument), plus the composition-vs-inheritance tradeoff"
difficulty: intermediate
order: 3
platform: host
prerequisites:
- 'WeakPtr hands-on (II): the core skeleton and control block'
- 'WeakPtr prerequisite (V): template friend and uintptr_t type erasure'
reading_time_minutes: 13
related:
- 'WeakPtr hands-on (IV): sequence affinity and lazy binding'
- 'WeakPtr hands-on (I): motivation and API design'
tags:
- host
- cpp-modern
- intermediate
- 智能指针
- weak_ptr
- 内存管理
title: "WeakPtr hands-on (III): WeakPtrFactory and the last-member idiom"
translation:
  source: documents/vol9-open-source-project-learn/chrome/02_weak_ptr/full/02-3-weak-ptr-factory-and-last-member.md
  source_hash: 1684758694fa21aa6b5bd257d57570eb77af0e56523ff878a03f7e61088b5ec7
  translated_at: '2026-09-26T01:44:05+00:00'
  engine: anthropic
  token_count: 6000
---
# WeakPtr hands-on (III): WeakPtrFactory and the last-member idiom

In [02-2](./02-2-weak-ptr-core-skeleton-and-control-block.md) we hand-rolled a Flag to serve as the raw material for minting. But honestly, you can't be expected to `new` a Flag every time you want to hand out a WeakPtr and then babysit the reference count yourself. That would be exhausting. Chromium packages all that drudgery into `WeakPtrFactory<T>` — a mint hanging off the observed object: you want WeakPtrs, it mints them; when the object's time is up, it invalidates every WeakPtr it ever minted in one shot.

In this piece we build the factory, and along the way chew through its most famous rule of use: `WeakPtrFactory<T> weak_factory_{this}` must be declared as the last member of the class. The idiom looks like nitpicking at first glance, but it is in fact the line between using this right and using it wrong. The first time I saw it I wondered whether it really had to be this fussy; only after stepping into the pit myself did I understand why they made a point of writing it into the EXAMPLE at the top of the header. We will use reverse destruction order to explain it thoroughly.

## The Flag inside WeakPtrFactory

The factory holds exactly one thing: a single Flag, shared by every WeakPtr minted from it. So internally the factory wraps a `WeakReferenceOwner` — the Flag's issuer and holder (`weak_ptr.cc:82-89`):

```cpp
// Issuer: holds one Flag, responsible for invalidation
class WeakReferenceOwner {
public:
    WeakReferenceOwner() : flag_(make_ref<Flag>()) {}   // construction mints a fresh Flag
    ~WeakReferenceOwner() {
        if (flag_) flag_->Invalidate();                 // on destruction, invalidates all WeakPtrs
    }
    WeakReference GetRef() const { return WeakReference(flag_); }   // minting
    // ...
private:
    scoped_refptr<Flag> flag_;
};
```

This passage reads plain, but every line has a role to play. The constructor mints a fresh Flag; `GetRef()` spits out a `WeakReference` pointing at that same Flag every time; and the destructor's `flag_->Invalidate()` is the detonator actually buried here — it turns "the factory died, all WeakPtrs go invalid immediately" into an automatic side effect of the factory's destructor, so you never have to remember to call `invalidate` by hand. My reaction the first time I read this was: what a worry-free design — it tucks the most easily forgotten step into the destruction chain.

On top of `WeakReferenceOwner`, `WeakPtrFactory<T>` adds exactly one thing — a raw pointer to the observed object:

```cpp
// Platform: host | C++ Standard: C++20
namespace tamcpp::chrome {

template <typename T>
class WeakPtrFactory : public internal::WeakPtrFactoryBase {
public:
    WeakPtrFactory() = delete;
    explicit WeakPtrFactory(T* ptr)
        : WeakPtrFactoryBase(reinterpret_cast<uintptr_t>(ptr)) {}

    WeakPtrFactory(const WeakPtrFactory&) = delete;
    WeakPtrFactory& operator=(const WeakPtrFactory&) = delete;

    // Minting: a const factory hands out WeakPtr<const T>
    WeakPtr<const T> get_weak_ptr() const {
        return WeakPtr<const T>(weak_reference_owner_.GetRef(),
                                reinterpret_cast<const T*>(ptr_));
    }

    // Non-const overload: hands out WeakPtr<T> (pre-04's requires)
    WeakPtr<T> get_weak_ptr()
        requires(!std::is_const_v<T>)
    {
        return WeakPtr<T>(weak_reference_owner_.GetRef(),
                          reinterpret_cast<T*>(ptr_));
    }

    // Active batch invalidation (object still alive, but you want all WeakPtrs invalid)
    void invalidate_weak_ptrs() {
        assert(ptr_);
        weak_reference_owner_.Invalidate();   // invalidate the old Flag + mint a fresh one
    }

    void invalidate_weak_ptrs_and_doom() {
        assert(ptr_);
        weak_reference_owner_.InvalidateAndDoom();   // invalidate + mint no new Flag ever again
        ptr_ = 0;
    }

    bool has_weak_ptrs() const { return ptr_ && weak_reference_owner_.HasRefs(); }

private:
    internal::WeakReferenceOwner weak_reference_owner_;
    // ptr_ lives in the non-template base WeakPtrFactoryBase, as uintptr_t (see pre-05)
};

}  // namespace tamcpp::chrome
```

There are a few spots in this code I want to point out specifically — all of them tricks covered in the [pre-05](./pre-05-weak-ptr-template-friend-and-uintptr-t.md) piece. `reinterpret_cast<uintptr_t>(ptr)` stores the `T*` as an integer so it can sink into the non-template base `WeakPtrFactoryBase`, sparing us from regenerating a copy of the template code for every `T`; inside `get_weak_ptr`, the `reinterpret_cast<T*>(ptr_)` converts it back. Then there are the two `get_weak_ptr` overloads, which use the member-function `requires(!std::is_const_v<T>)` from [pre-04](./pre-04-weak-ptr-concepts-and-requires.md) — const correctness hangs right on the signature: a const factory produces `WeakPtr<const T>`, a non-const one produces `WeakPtr<T>`, and the split is settled crystal clear at compile time.

## invalidate_weak_ptrs and invalidate_weak_ptrs_and_doom, what's the difference

The factory offers two invalidation methods, and from the names alone you may be as baffled as I was back then — aren't they both just invalidation? The difference hides in one question: after invalidating, can the factory keep minting? Let's look at the two `WeakReferenceOwner` implementations side by side (`weak_ptr.cc:103-113`):

```cpp
void WeakReferenceOwner::Invalidate() {
    assert(flag_);
    flag_->Invalidate();                 // invalidate the old Flag
    flag_ = make_ref<Flag>();            // mint a fresh Flag, the factory can keep minting
}

void WeakReferenceOwner::InvalidateAndDoom() {
    assert(flag_);
    flag_->Invalidate();                 // invalidate the old Flag
    flag_.reset();                       // hold no new Flag, the factory enters its "dead" state
}
```

The whole difference is the single line after invalidating the old Flag. In `invalidate_weak_ptrs()`, right after invalidating the old Flag, `flag_ = make_ref<Flag>()` mints a fresh one — every existing WeakPtr expires together, but the factory itself is still breathing: you can keep calling `get_weak_ptr()` to mint new ones, and the newly minted WeakPtrs share that new Flag. This fits the scenario of "the object is entering a new phase, the old observers should be retired, but new observers will still attach later".

`invalidate_weak_ptrs_and_doom()` is the harsher one: after invalidating the old Flag it mints no new one, and while it's at it zeroes out `ptr_` — the factory goes straight into its "dead" state, and any `get_weak_ptr()` call you make afterwards gets an invalid result. It is even cheaper than the previous option, saving the one Flag allocation. As the name doom suggests, this is for the wrap-up scenario of "this object is thoroughly done being used".

The difference between these two will make another appearance in the [02-6](./02-6-weak-ptr-testing-and-perf.md) performance comparison. But to tell the truth, in nine out of ten everyday scenarios you will never call either of them explicitly — the factory's automatic destructor-time invalidation we are about to cover is enough on its own.

---

## The main event: the last-member idiom

What comes next is the thing this piece truly wants to teach. The EXAMPLE block at the top of Chromium's `weak_ptr.h` puts this rule in the most prominent spot on purpose (`weak_ptr.h:22-26`):

> Member variables should appear before the WeakPtrFactory, to ensure that any WeakPtrs to Controller are invalidated before its members variable's destructors are executed.

Translated into one sentence: member variables must be declared before `WeakPtrFactory`, with the factory placed last. When I first read this rule I muttered to myself — really? Can member order actually affect correctness? It truly can. Let's take it apart one piece at a time.

### First, the groundwork: C++ destroys in reverse order

This is a basic C++ rule, but one that is easy to forget when working with WeakPtr: when an object is destroyed, its members are destroyed in the **reverse** of declaration order — the first declared dies last, the last declared dies first. So if `WeakPtrFactory` is the last-declared member, it is destroyed **first**; conversely, if you put it at the very front, it becomes the one destroyed **last**. Keep this "reverse order" in mind — the whole argument below rides on it.

### Another piece of groundwork: factory destruction = invalidating all WeakPtrs

That `flag_->Invalidate()` line inside `WeakReferenceOwner::~WeakReferenceOwner()` from earlier is the key — the moment the factory is destroyed, every WeakPtr it minted expires along with it. Put differently, "when the factory dies" directly determines "when all the WeakPtrs become invalid".

### Stack the two: why the factory must go last

Put those two facts together and the conclusion pops out on its own. Suppose `Controller` has a few ordinary members plus a factory, and let's deliberately write out both declaration orders and compare:

```cpp
// ✗ Wrong order: factory at the front
class BadController {
public:
    void on_work_done() { /* uses buf_ */ }
private:
    WeakPtrFactory<BadController> weak_factory_{this};   // declared first → destroyed last
    std::vector<int> buf_;                                // declared later → destroyed first
};

// ✓ Right order: factory last
class GoodController {
public:
    void on_work_done() { /* uses buf_ */ }
private:
    std::vector<int> buf_;                                // declared first → destroyed last
    WeakPtrFactory<GoodController> weak_factory_{this};   // declared later → destroyed first
};
```

Look at the `BadController` counterexample first. Destruction walks in reverse: `buf_` dies first, and only then is it `weak_factory_`'s turn — only then does anyone remember to invalidate all the WeakPtrs. The problem lives in the window between those two events: `buf_` is already destroyed, yet every WeakPtr is still cheerfully "valid". And if, precisely in that window, some async task holding a WeakPtr dereferences the `Controller`, it strolls straight into `on_work_done()` and slams headfirst into the already-destroyed `buf_`. A use-after-free, guaranteed. Back in the day I chased an intermittent ASAN report in a similar scenario, and the root cause was exactly this ordering.

`GoodController` flips the order: `weak_factory_` is declared last, so it is destroyed first, and the instant it is destroyed every WeakPtr is invalidated; only after that does `buf_` get its turn to die. By the time `buf_` actually begins its destruction, no "valid" WeakPtr on the outside can reach it anymore — a later dereference gets at most a `nullptr`. Safe.

The last-member idiom boils down to this one sentence: let the factory be destroyed before the other members, and borrow that one automatic invalidation at its destruction to shield the other members through their own destruction windows.

### One easy-to-trip boundary: it guards the member destruction window, not the destructor body

There is a detail I specifically want to make clear here, because it is the easiest one to misjudge — we confirmed it deliberately during our verification pass. Putting the factory last shields the **member destruction window**; it does **not** stop you from using WeakPtrs inside the object's own destructor body. Let's spread the timeline out:

```text
GoodController destruction:
  ① destructor body runs (all members still alive at this point, WeakPtrs still valid)
  ② members destroyed in reverse order:
       weak_factory_ destroyed first → all WeakPtrs invalidated  ← the gate fires here
       buf_        destroyed after
```

In other words, during ①, while the destructor body is executing, the WeakPtrs are still valid. That actually matches intuition — inside the destructor body you often still want to reference the object's other members (to notify observers "I'm on my way out", say), and having WeakPtrs valid at that point is convenient. The real invalidation happens after ② begins member destruction, and its purpose is precisely to ensure that any later deref can never touch a half-destroyed member. Chromium's source comments do not spell this boundary out; I derived it myself while reading the code. Note it down, so that you don't someday misjudge by assuming "the moment an object enters destruction, its WeakPtrs immediately go invalid".

---

## Why Chromium kept only the composition path

Search all of Chromium's current `//base` and you will find exactly one official way to obtain a WeakPtr: stuff a `WeakPtrFactory<Controller> weak_factory_{this}` member into `Controller`. That is the carrier of the last-member idiom, and Chromium chose it for good reasons — you decide when invalidation happens, it works on types you don't own (at the extreme, even `WeakPtrFactory<bool>` is fine), and it doesn't pollute the inheritance chain.

Historically there was also an inheritance-based `SupportsWeakPtr<T>` — inherit from it and you got a `GetWeakPtr()` for free. Sounds more convenient, right? But it led people toward unsafe usage, and Chromium eventually moved it out of `//base` altogether; today grepping `SupportsWeakPtr` in `weak_ptr.h` comes up empty. I mention it for one reason only: so that you don't freeze up when you bump into the name in old code or old docs. New code always goes the composition route, and once you understand composition, that old mechanism is nothing more than the factory hidden inside a base class — no difference in essence.

---

## Rewriting the 02-1 pitfall with the factory

All talk and no practice gets us nowhere, so let's rewrite the dangling-callback scenario from [02-1](./02-1-weak-ptr-motivation-and-api-design.md) with the factory we just built, and watch how it plugs the UAF hole dead:

```cpp
// Platform: host | C++ Standard: C++20
#include <functional>
#include <iostream>
#include <vector>

class Controller {
public:
    void on_work_done(int v) {
        buf_.push_back(v);
        std::cout << "got " << v << ", buf size=" << buf_.size() << '\n';
    }
    WeakPtr<Controller> get_weak() { return weak_factory_.get_weak_ptr(); }

    ~Controller() = default;
private:
    std::vector<int> buf_;                                  // declared first
    WeakPtrFactory<Controller> weak_factory_{this};         // the last member!
};

int main() {
    using namespace tamcpp::chrome;
    WeakPtr<Controller> wp;                                 // declared first, filled in shortly

    {
        Controller c;
        wp = c.get_weak();
        std::cout << (wp ? "alive" : "dead") << '\n';       // alive
        if (wp) wp->on_work_done(7);                        // got 7, buf size=1
    }   // c leaves scope: weak_factory_ destroyed first → wp invalidated → only then is buf_ destroyed

    std::cout << (wp ? "alive" : "dead") << '\n';           // dead
    if (wp) {
        wp->on_work_done(8);                                // never gets in here
    } else {
        std::cout << "controller gone, skip\n";             // this branch runs
    }
    return 0;
}
```

Let's run it and see. The output pops out in order: `alive` → `got 7, buf size=1` → `dead` → `controller gone, skip`. Keep your eyes on the last line — after `Controller` is destroyed, `wp` has already auto-invalidated, and the `if (wp)` gate turns away any access to the destroyed object. The dangling callback that gave us such a headache in 02-1 is plugged for good right here by the factory's destructor-time automatic invalidation, without you writing a single line of defensive code.

At this point WeakPtr's core mechanisms — minting, invalidation, and the dereference gate — are all in place. But there is one usage contract we have kept circling without stating head-on: **dereferencing and invalidating a WeakPtr must happen on the same sequence it was bound on**. That contract deserves a straight-up treatment, and we will cover the factory's lazy sequence binding along the way. See you in the next piece.

## References

- [Chromium `base/memory/weak_ptr.h` — WeakPtrFactory and the top-of-file EXAMPLE](https://source.chromium.org/chromium/chromium/src/+/main:base/memory/weak_ptr.h)
- [Chromium `base/memory/weak_ptr.cc` — WeakReferenceOwner](https://source.chromium.org/chromium/chromium/src/+/main:base/memory/weak_ptr.cc)
- [WeakPtr hands-on (II): the core skeleton and control block](./02-2-weak-ptr-core-skeleton-and-control-block.md)
- [WeakPtr prerequisite (V): template friend and uintptr_t type erasure](./pre-05-weak-ptr-template-friend-and-uintptr-t.md)
