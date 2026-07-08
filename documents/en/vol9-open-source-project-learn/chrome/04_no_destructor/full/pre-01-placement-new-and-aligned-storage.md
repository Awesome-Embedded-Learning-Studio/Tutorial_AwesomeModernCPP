---
chapter: 0
cpp_standard:
- 11
- 17
- 20
description: "Breaking down placement new (construct without allocating) and aligned
  storage (alignas/alignof), and how NoDestructor uses char storage_[sizeof(T)] plus
  reinterpret_cast to manage object lifetime by hand."
difficulty: intermediate
order: 1
platform: host
prerequisites:
- 'NoDestructor prerequisite (0): static storage duration, initialization, and destruction'
reading_time_minutes: 9
related:
- 'NoDestructor hands-on (II): the core implementation'
tags:
- host
- cpp-modern
- intermediate
- 内存管理
- RAII
title: "NoDestructor Prerequisite (I): placement new and aligned storage"
---
# NoDestructor Prerequisite (I): placement new and aligned storage

In [pre-00](./pre-00-static-storage-and-init.md) we said NoDestructor "constructs a `T` but never lets it destruct." Sounds cryptic, but on the page it rests on two plain mechanisms: placement new (construct an object at an address you already own, with no allocation), and a `char` buffer aligned for `T` to live in. This piece takes both apart. They are the backbone of NoDestructor, and they are also the everyday tools for any manual lifetime management in C++. You will meet them again the moment you write a memory pool or a container.

---

## Plain `new` vs placement new

An ordinary `new T(args)` is two steps folded into one. First `operator new(sizeof(T))` carves a `sizeof(T)` block out of the heap, then the constructor of `T` runs on that block. `delete ptr` reverses it: destruct first, then `operator delete(ptr)` hands the memory back. `new` bundles "allocate" and "construct" together, and most of the time that is exactly what you want. But sometimes you already hold a block of memory (a stack array, a pool, an mmap region) and you only want the constructor to run on it, not to fetch fresh memory. That is the problem placement new solves.

### placement new: construct only, no allocation

The syntax just tacks `(addr)` onto `new` to say where to build: `new (addr) T(args)`.

```cpp
#include <new>   // placement new needs this

alignas(int) unsigned char buf[sizeof(int)];   // existing memory (a char array)
int* p = new (buf) int(42);                      // construct an int in buf, no allocation!
*p == 42;
```

It does only the "construct" step, calling `T`'s constructor on the `addr` you passed. `operator new` never wakes up. The memory is yours, and so is the lifetime.

Destruction is on you now. You call the destructor by hand, `p->~T()`. Never write `delete p` here: `delete` would also try to release memory, and this block was never heap-allocated, so freeing it blows up.

```cpp
using I = int;
I* p = new (buf) I(42);
p->~I();        // manual destructor (pointless for a trivial type like int, but this is the mechanism)
// buf itself is a stack array; it gets reclaimed automatically, outside placement new's concern
```

(Small compiler snag: for a bare built-in type name, the pseudo-destructor call needs a typedef alias. `p->~int()` is rejected by the mainstream compilers; you write `using I=int; p->~I();` instead. The first time I hit that, I stared at it for a while.)

Here is where placement new really earns its keep. It splits "when does the object live and die" from "whose memory is this, and when does it go back." You can construct on stack memory, on a pool, on a shared segment, on an mmap block, whenever you like, and destruct with one manual call. Almost every manual-lifetime trick in C++ starts from this one line.

---

## Alignment: alignof and alignas

Placement new comes with a precondition: the address you hand it must satisfy `T`'s alignment requirement. Alignment means "the object's address has to be a multiple of some value." CPUs reach aligned addresses faster, and on some architectures an unaligned access is not slow but flat-out illegal, raising a hardware exception.

Two keywords split the job. `alignof(T)` asks what `T`'s alignment requirement is in bytes: `alignof(int)` is usually 4, `alignof(double)` is 8. `alignas(N)` goes the other way, letting you impose an alignment on a variable or type: `alignas(16) int x;` forces `x` to 16-byte alignment. One queries, one commands.

Pass placement new an unaligned address and the behavior is undefined:

```cpp
unsigned char buf[13];           // address might not be 4-byte aligned
new (buf) int(42);               // UB! buf's alignment may be too weak for int
```

So when you hand over memory, the alignment must satisfy `T`. Non-negotiable.

### How NoDestructor writes it: `alignas(T) char storage_[sizeof(T)]`

NoDestructor gets past the alignment gate like this (no_destructor.h:122):

```cpp
alignas(T) char storage_[sizeof(T)];
```

One line, two jobs. `char storage_[sizeof(T)]` first opens a char array of `sizeof(T)` bytes, just enough to hold one `T`. `char` is the most permissive type, happy to hold any byte pattern, which makes it the natural choice for a generic buffer. Then `alignas(T)` lifts the array's alignment from char's default of 1 up to `T`'s requirement. Put together, the address of `storage_` is guaranteed to be a multiple of `alignof(T)`, so placement new can build straight on top with no alignment landmine to step on.

This is the standard idiom for hand-written buffer storage in C++. In older code you will often see `std::aligned_storage<sizeof(T), alignof(T)>` instead. That template was deprecated in C++23 (see LWG3867 / P2967), and `alignas(T) char buf[sizeof(T)]` is now the recommended form: more direct, no template detour.

### Access: `reinterpret_cast<T*>(storage_)`

Once construction is done, you still have to treat that char memory as a `T`, and that means casting the address to `T*` with `reinterpret_cast<T*>(storage_)`. This is legal: after placement new has run, a real, honest `T` object lives in that char memory, so a `reinterpret_cast` pointing at it is well-defined. NoDestructor's `get()` is exactly this (no_destructor.h:118-119):

```cpp
T* get() { return reinterpret_cast<T*>(storage_); }
```

---

## Manual lifetime: construct but never destruct

With those pieces in hand, you can see what NoDestructor is doing. It holds a raw `alignas(T) char storage_[sizeof(T)]` buffer, and at construction time it placement-news `T` onto it: `new (storage_) T(args...)`. Then comes the point. It never gives `T` a path to destruct. `~NoDestructor()` is `= default`, and what that destroys is the char array, which is a trivial type that does nothing. `~T()` is never called on this path.

That is the whole secret of "construct but never destruct." After placement new brings `T` into being, it sits in `storage_` for the rest of the program, until process exit, when the OS reclaims the entire process address space, `T` included, as ordinary memory. Note the reclaimer here is not `T`'s destructor. It is the OS.

### Is that safe?

What about the resources `T` itself owns? Take `NoDestructor<vector<int>>`: the vector's heap-allocated elements. Frankly, those are not released by `~T()`, because `~T()` never runs. They ride on the OS reclaiming the whole address space at process exit. During the program's lifetime that memory counts as "leaked," but the program is about to end, so who is there to see the leak? The OS catches it regardless.

What actually breaks is a different case: when `T`'s destructor has side effects. A destructor that flushes a log to disk, or signals another process "I'm leaving," won't fire, because the destructor doesn't run. So NoDestructor is only safe for types whose destructor is pure resource release. If the destructor carries an observable side effect, don't use it.

---

## A minimal reproduction

Talk is cheap. We can hand-roll a minimal version and feel placement new plus "no destructor" directly:

```cpp
// Platform: host | C++ Standard: C++17
#include <cassert>
#include <cstdio>
#include <new>
#include <string>

template <typename T>
class MiniNoDestructor {
public:
    template <typename... Args>
    explicit MiniNoDestructor(Args&&... args) {
        new (storage_) T(std::forward<Args>(args)...);   // placement new
    }
    ~MiniNoDestructor() = default;   // does NOT call ~T()!
    MiniNoDestructor(const MiniNoDestructor&) = delete;

    T& operator*() { return *get(); }
    T* operator->() { return get(); }
    T* get() { return reinterpret_cast<T*>(storage_); }

private:
    alignas(T) char storage_[sizeof(T)];
};

struct Noisy {
    Noisy() { std::puts("Noisy()"); }
    ~Noisy() { std::puts("~Noisy()"); }   // this destructor never runs
};

int main() {
    {
        static const MiniNoDestructor<Noisy> nd;   // construct once
        // leaving scope / program exit: ~MiniNoDestructor runs (trivial), ~Noisy does not
    }
    std::puts("(before program exit, ~Noisy is not printed)");
    return 0;
}
```

Run it and you will see it: `Noisy()` prints once, but the `~Noisy()` line never appears. That is NoDestructor's "no destructor," in the flesh.

---

The parts are all here. Placement new gives us "construct without allocating," `alignas(T) char storage_[sizeof(T)]` clears the alignment hurdle, and `~NoDestructor() = default` quietly walls off the destruction path. Put the three together and `T` just sits in `storage_`, refusing to leave, until the OS reclaims everything at process exit. In the next piece we assemble NoDestructor for real. Parts alone aren't enough; we still have to see how it covers initialization ordering and the legal path through `reinterpret_cast`.

## References

- [cppreference: placement new](https://en.cppreference.com/w/cpp/language/new#Placement_new)
- [cppreference: alignof / alignas](https://en.cppreference.com/w/cpp/language/alignas)
- [cppreference: std::aligned_storage (deprecated since C++23)](https://en.cppreference.com/w/cpp/types/aligned_storage)
- [Chromium `base/no_destructor.h`: storage_ and get()](https://source.chromium.org/chromium/chromium/src/+/main:base/no_destructor.h)
