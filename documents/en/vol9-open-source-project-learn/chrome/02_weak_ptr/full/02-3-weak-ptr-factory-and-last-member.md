---
chapter: 1
cpp_standard:
- 17
- 20
description: "Implementing WeakPtrFactory: the mint, the difference between InvalidateWeakPtrs and AndDoom, and why it must be the last member (a destruction-order argument), plus the composition-vs-inheritance tradeoff."
difficulty: intermediate
order: 3
platform: host
prerequisites:
- 'weak_ptr hands-on (II): the core skeleton and control block'
- 'weak_ptr prerequisite (V): template friends and uintptr_t type erasure'
reading_time_minutes: 13
related:
- 'weak_ptr hands-on (IV): sequence affinity and lazy binding'
- 'weak_ptr hands-on (I): motivation and API design'
tags:
- host
- cpp-modern
- intermediate
- 智能指针
- weak_ptr
- 内存管理
title: "weak_ptr hands-on (III): WeakPtrFactory and the last-member idiom"
---
# weak_ptr hands-on (III): WeakPtrFactory and the last-member idiom

In [02-2](./02-2-weak-ptr-core-skeleton-and-control-block.md) we hand-rolled a Flag as the raw material for the mint. But honestly, you can't be expected to `new` a Flag yourself every time you want a WeakPtr and then babysit the refcount. That gets old fast. Chromium bundles all that bookkeeping into `WeakPtrFactory<T>`, a small mint that hangs on the observed object: you ask it for WeakPtrs, and when the object is done living, it invalidates every WeakPtr it ever minted in one shot.

This piece builds the factory and then takes on its most famous usage rule, that `WeakPtrFactory<T> weak_factory_{this}` has to be the last member of the class. The idiom looks pedantic on first read. It isn't. It is the line between correct and broken code. The first time I saw it I wondered whether member ordering could really matter for correctness, and it was only after a real bug that I understood why Chromium puts it at the top of the header in an EXAMPLE block. We will work through it with a destruction-order argument.

## The Flag inside WeakPtrFactory

The factory holds exactly one thing: a Flag, shared by every WeakPtr minted from it. So internally it wraps a `WeakReferenceOwner`, the issuer and holder of that Flag (`weak_ptr.cc:82-89`):

```cpp
// Issuer: holds one Flag, responsible for invalidation
class WeakReferenceOwner {
public:
    WeakReferenceOwner() : flag_(make_ref<Flag>()) {}   // mint a fresh Flag on construction
    ~WeakReferenceOwner() {
        if (flag_) flag_->Invalidate();                 // invalidate all WeakPtrs on destruction
    }
    WeakReference GetRef() const { return WeakReference(flag_); }   // the mint
    // ...
private:
    scoped_refptr<Flag> flag_;
};
```

This reads plainly, but every line is doing work. Construction mints a fresh Flag. `GetRef()` hands back a `WeakReference` pointing at that same Flag each time. The destructor's `flag_->Invalidate()` is the actual detonator. It turns "factory died, all WeakPtrs drop now" into an automatic side effect of the factory's destructor, so you never have to remember to call `invalidate` yourself. My reaction the first time I read this was: that is a genuinely considerate design. It stuffs the step you are most likely to forget into the destruction chain.

On top of `WeakReferenceOwner`, `WeakPtrFactory<T>` adds exactly one thing: a raw pointer back to the observed object.

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

    // Mint: const factory hands out WeakPtr<const T>
    WeakPtr<const T> get_weak_ptr() const {
        return WeakPtr<const T>(weak_reference_owner_.GetRef(),
                                reinterpret_cast<const T*>(ptr_));
    }

    // Non-const overload: hands out WeakPtr<T> (pre-04 requires)
    WeakPtr<T> get_weak_ptr()
        requires(!std::is_const_v<T>)
    {
        return WeakPtr<T>(weak_reference_owner_.GetRef(),
                          reinterpret_cast<T*>(ptr_));
    }

    // Active batch invalidation (object still alive, but you want all WeakPtrs gone)
    void invalidate_weak_ptrs() {
        assert(ptr_);
        weak_reference_owner_.Invalidate();   // invalidate the old Flag + mint a fresh one
    }

    void invalidate_weak_ptrs_and_doom() {
        assert(ptr_);
        weak_reference_owner_.InvalidateAndDoom();   // invalidate + never mint again
        ptr_ = 0;
    }

    bool has_weak_ptrs() const { return ptr_ && weak_reference_owner_.HasRefs(); }

private:
    internal::WeakReferenceOwner weak_reference_owner_;
    // ptr_ lives in the non-template base WeakPtrFactoryBase (see pre-05)
};

}  // namespace tamcpp::chrome
```

A few spots here are worth calling out, all techniques from the [pre-05](./pre-05-weak-ptr-template-friend-and-uintptr-t.md) piece. `reinterpret_cast<uintptr_t>(ptr)` stores the `T*` as an integer so the member can sink into the non-template base `WeakPtrFactoryBase`, which saves you from regenerating the template body for every `T`. Inside `get_weak_ptr` the reverse `reinterpret_cast<T*>(ptr_)` brings it back. The two `get_weak_ptr` overloads use a member function `requires(!std::is_const_v<T>)` from [pre-04](./pre-04-weak-ptr-concepts-and-requires.md), so const-correctness sits directly on the signature: a const factory produces `WeakPtr<const T>`, a non-const one produces `WeakPtr<T>`, and the split is settled at compile time.

## invalidate_weak_ptrs vs invalidate_weak_ptrs_and_doom, what's the difference

The factory exposes two invalidation methods, and from the names alone you may be as puzzled as I was. Aren't both of them just invalidation? The difference hides in one question: after invalidating, can the factory keep minting? Look at the two `WeakReferenceOwner` implementations side by side (`weak_ptr.cc:103-113`):

```cpp
void WeakReferenceOwner::Invalidate() {
    assert(flag_);
    flag_->Invalidate();                 // invalidate the old Flag
    flag_ = make_ref<Flag>();            // mint a fresh one, factory can keep minting
}

void WeakReferenceOwner::InvalidateAndDoom() {
    assert(flag_);
    flag_->Invalidate();                 // invalidate the old Flag
    flag_.reset();                       // hold no new Flag, factory enters the "doomed" state
}
```

The entire difference is the line after invalidating the old Flag. `invalidate_weak_ptrs()` invalidates the old Flag, then immediately `flag_ = make_ref<Flag>()` mints a fresh one. All existing WeakPtrs drop together, but the factory itself is still breathing, and you can call `get_weak_ptr()` again. The new WeakPtrs share the new Flag. This is the right call when an object is entering a new phase and the old observers should clear out, but new observers still need to attach later.

`invalidate_weak_ptrs_and_doom()` is the meaner sibling. After invalidating the old Flag it does not mint a new one, and it zeroes `ptr_` for good measure. The factory enters a doomed state, and any `get_weak_ptr()` you call after that hands back an invalid result. It is cheaper than the first option by exactly one Flag allocation. The name says it: doom is for the "this object is being retired" cleanup path.

These two show up again in the [02-6](./02-6-weak-ptr-testing-and-perf.md) performance comparison. Truth is, in nine out of ten day-to-day cases you never call either one explicitly. The factory's destructor-time auto-invalidation we are about to cover is enough on its own.

---

## The main event: the last-member idiom

This is the part the piece is really here for. The EXAMPLE block at the top of Chromium's `weak_ptr.h` puts the rule in the most visible spot it can (`weak_ptr.h:22-26`):

> Member variables should appear before the WeakPtrFactory, to ensure that any WeakPtrs to Controller are invalidated before its members variable's destructors are executed.

In one sentence: declare member variables before `WeakPtrFactory`, and put the factory last. My first thought reading this was, really? Member order affecting correctness? It does. Let's take it apart one piece at a time.

### First, a foundation: C++ destroys members in reverse order

This is a basic C++ rule, but it is easy to lose track of when you are working with WeakPtr. When an object is destroyed, its members are destroyed in the reverse of declaration order. The first declared dies last; the last declared dies first. So if `WeakPtrFactory` is declared last, it is destroyed first. Put it at the front and it becomes the last thing destroyed. Hold onto that word, reverse. The whole argument below rides on it.

### Another foundation: factory destruction = invalidate all WeakPtrs

That `flag_->Invalidate()` inside `WeakReferenceOwner::~WeakReferenceOwner()` is the key. The moment the factory is destroyed, every WeakPtr it ever minted drops. In other words, when the factory dies determines when all its WeakPtrs become invalid.

### Stack the two: why the factory has to go last

Put those two facts together and the conclusion writes itself. Say `Controller` has a few ordinary members plus a factory. Let's write both declaration orders and compare:

```cpp
// ✗ Wrong order: factory first
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

Take `BadController` first. Destruction walks in reverse: `buf_` dies, then `weak_factory_` gets its turn, and only then does it remember to invalidate the WeakPtrs. The problem is the window between those two events. `buf_` is already gone, but every WeakPtr is still cheerfully "valid". If an async task holding one of those WeakPtrs dereferences the `Controller` right then, it sails into `on_work_done()` and slams into a destroyed `buf_`. UAF, clean and inevitable. I once chased an intermittent ASAN red on a setup exactly like this, and member order was the root cause.

`GoodController` flips the order. `weak_factory_` is declared last, so it is destroyed first, and the moment it dies it invalidates every WeakPtr. Only after that does `buf_` get destroyed. By the time `buf_`'s destructor runs, no "valid" WeakPtr can reach it from outside. A late dereference gets a `nullptr` at worst. Safe.

The last-member idiom, in one line: let the factory die before the other members, and lean on its destructor-time invalidation to cover the rest of the members through their own destruction.

### One edge worth pinning down: it guards the member destruction window, not the destructor body

There is a detail I want to nail down here because it trips people up the most. We confirmed it specifically when we were doing the verification pass. Putting the factory last guards the member destruction window. It does not stop you from using WeakPtrs inside the object's own destructor body. Spread the timeline out:

```text
GoodController destruction:
  ① destructor body runs (all members still alive, WeakPtrs still valid)
  ② members destroyed in reverse order:
       weak_factory_ destroyed first → all WeakPtrs invalidated  ← the gate fires here
       buf_        destroyed after
```

So during ①, while the destructor body is running, WeakPtrs are still valid. This is the intuitive behavior. Inside the destructor body you often want to touch sibling members (notifying observers "I'm on my way out", say), and having WeakPtrs still valid there is the convenient choice. The actual invalidation happens once member destruction begins at ②. The point is to guarantee that any dereference after this point cannot reach a half-destroyed member. Chromium's source comments do not spell this boundary out. I read it off the code, and you should keep it in mind so you do not later assume "WeakPtrs invalidate the instant the object enters destruction". That misread will cost you.

---

## Why Chromium kept only the composition path

If you grep current `//base`, there is exactly one blessed way to get a WeakPtr: stuff a `WeakPtrFactory<Controller> weak_factory_{this}` member into `Controller`. That is the carrier of the last-member idiom, and Chromium picked it for a reason. You control the invalidation timing, it works on types you do not own (you can go as far as `WeakPtrFactory<bool>`), and it does not pollute the inheritance chain.

There used to be an inheritance-based variant, `SupportsWeakPtr<T>`. Inherit from it and you got a `GetWeakPtr()` for free. Sounds more convenient, right? It nudged people toward unsafe patterns, and Chromium eventually pulled it out of `//base`. Today `grep SupportsWeakPtr weak_ptr.h` comes back empty. I bring it up only because you will run into the name in old code and old docs and it helps to know what it was. New code uses composition. Once you understand the composition form, the old mechanism is just the factory hidden in a base class. Same idea underneath.

---

## Rewriting the 02-1 hazard with the factory

Talk is cheap. Let's rewrite the dangling-callback scenario from [02-1](./02-1-weak-ptr-motivation-and-api-design.md) with the factory we just built, and watch it close the UAF hole:

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
    WeakPtrFactory<Controller> weak_factory_{this};         // last member!
};

int main() {
    using namespace tamcpp::chrome;
    WeakPtr<Controller> wp;                                 // declared first, filled in shortly

    {
        Controller c;
        wp = c.get_weak();
        std::cout << (wp ? "alive" : "dead") << '\n';       // alive
        if (wp) wp->on_work_done(7);                        // got 7, buf size=1
    }   // c leaves scope: weak_factory_ destroyed first → wp invalidates → buf_ destroyed after

    std::cout << (wp ? "alive" : "dead") << '\n';           // dead
    if (wp) {
        wp->on_work_done(8);                                // does not enter here
    } else {
        std::cout << "controller gone, skip\n";             // takes this branch
    }
    return 0;
}
```

Run it and the output lands in order: `alive`, then `got 7, buf size=1`, then `dead`, then `controller gone, skip`. The line to watch is the last one. After `Controller` is destroyed, `wp` has already auto-invalidated, and the `if (wp)` gate turns away the access to the destroyed object. The dangling callback from 02-1, the one that gave us headaches, is closed off entirely by the factory's destructor-time invalidation. You did not write a single line of defensive code.

That gathers the core WeakPtr mechanisms into one place: the mint, invalidation, and the dereference gate. There is one usage contract we have been circling around without stating directly: dereferencing and invalidating a WeakPtr must happen on the same sequence it was bound on. That contract deserves a straight treatment, and we will also cover the factory's lazy sequence binding along the way. Next piece.

## References

- [Chromium `base/memory/weak_ptr.h` — WeakPtrFactory and the top-of-file EXAMPLE](https://source.chromium.org/chromium/chromium/src/+/main:base/memory/weak_ptr.h)
- [Chromium `base/memory/weak_ptr.cc` — WeakReferenceOwner](https://source.chromium.org/chromium/chromium/src/+/main:base/memory/weak_ptr.cc)
- [weak_ptr hands-on (II): the core skeleton and control block](./02-2-weak-ptr-core-skeleton-and-control-block.md)
- [weak_ptr prerequisite (V): template friends and uintptr_t type erasure](./pre-05-weak-ptr-template-friend-and-uintptr-t.md)
