---
chapter: 0
cpp_standard:
- 11
- 17
- 20
description: Break down placement new (no allocation, construct only) and aligned storage (alignas/alignof), and how NoDestructor uses char storage_[sizeof(T)] plus reinterpret_cast to manage object lifetime by hand
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
title: "NoDestructor prerequisite (I): placement new and aligned storage"
translation:
  source: documents/vol9-open-source-project-learn/chrome/04_no_destructor/full/pre-01-placement-new-and-aligned-storage.md
  source_hash: 856b38c2422527c141d890c916051d9df21a4ca61c7a11d5f7a6b88f3c3ac2e5
  translated_at: '2026-09-26T03:17:04+00:00'
  engine: anthropic
  token_count: 1800
---
# NoDestructor prerequisite (I): placement new and aligned storage

In [pre-00](./pre-00-static-storage-and-init.md) we said NoDestructor "constructs a T but never lets it destruct." That sounds mystical, but on the code level it really rests on two low-level mechanisms: placement new (construct an object at an address you designate, with no memory allocated), plus a `char` array aligned for `T` serving as T's home. In this piece we take the two apart and grind them down — they are not just the core of NoDestructor, they are everyday tools for manual lifetime management in C++. When you later write memory pools or containers, you will be dealing with them all the time.

---

## Plain new vs placement new

An ordinary `new T(args)` is really two steps folded into one: first `operator new(sizeof(T))` carves a `sizeof(T)` block out of the heap, then it runs `T`'s constructor on that block. `delete ptr` is the reverse — destruct first, then `operator delete(ptr)` hands the memory back. `new` bundles "allocate" and "construct" together, and the vast majority of the time that is exactly what you want. But sometimes you already hold a block of memory (a stack array, a memory pool, something that came from mmap) and you only want the constructor to run over it once, without fetching fresh memory on your behalf. That is the problem placement new solves.

### placement new: construct only, no allocation

The syntax just stuffs an extra `(addr)` after `new`, telling it where to construct: `new (addr) T(args)`.

```cpp
#include <new>   // required for placement new

alignas(int) unsigned char buf[sizeof(int)];   // existing memory (a char array)
int* p = new (buf) int(42);                      // construct the int in buf, no allocation!
*p == 42;
```

It does only the "construct" step, running `T`'s constructor on the `addr` you handed over; `operator new` is never called at all. The memory is yours, and the lifetime is yours to manage.

On the destruction side you have to do it yourself — call the destructor manually, `p->~T()`. Never write `delete p`: `delete` would go on and try to release the memory, but this block was never allocated from the heap in the first place, and releasing it is an error:

```cpp
using I = int;
I* p = new (buf) I(42);
p->~I();        // manual destruction (really unnecessary for a trivial type like int, but that's the mechanism)
// buf itself is a stack array, reclaimed automatically — none of placement new's business
```

(A small compiler gotcha hides here: for a bare built-in type name, the pseudo-destructor call has to go through a typedef alias — mainstream compilers reject `p->~int()`; you must write `using I=int; p->~I();`. The first time we ran into that, we stared at it for a good while.)

This is where placement new really earns its keep — it splits "when is the object born, when does it die" from "whose memory is this, and when does it go back" into two separate things. You can construct on stack memory, or on a memory pool, shared memory, an mmap'd block; construct whenever you want, and when you want destruction, make one manual destructor call. Almost every manual-lifetime-management job starts from this one line.

---

## Alignment: alignof and alignas

placement new also has a precondition: the address you hand over must satisfy `T`'s alignment requirement. Alignment means "the object's address must be a multiple of some value" — CPUs access aligned addresses faster, and on some architectures an access to an unaligned address hands you a hardware exception outright; the code doesn't even get to run.

Two keywords split the work. `alignof(T)` queries how many bytes `T`'s alignment requirement is: `alignof(int)` usually comes back 4, `alignof(double)` comes back 8. `alignas(N)` goes the other way — you proactively impose an alignment on a variable or type: `alignas(16) int x;` forces `x` into 16-byte alignment. One asks, one answers.

If you hand placement new an unaligned address, the behavior is undefined:

```cpp
unsigned char buf[13];           // the address may not be 4-byte aligned
new (buf) int(42);               // UB! buf's alignment may not be enough for int
```

So in this hand-over-the-memory step, the alignment must satisfy `T`. Non-negotiable.

### How NoDestructor writes it: `alignas(T) char storage_[sizeof(T)]`

This is how NoDestructor gets past the alignment gate (no_destructor.h:122):

```cpp
alignas(T) char storage_[sizeof(T)];
```

One line, two jobs. `char storage_[sizeof(T)]` first opens a char array of `sizeof(T)` bytes, capacity just enough to hold one T — `char` is the most "tolerant" type, happy to hold any byte pattern, which makes it the best fit for a general-purpose buffer. `alignas(T)` then lifts that array's alignment from char's default of 1 up to `T`'s level. Put the two together and the address of `storage_` is guaranteed to be a multiple of `alignof(T)`, so placement new can be called right on top of it — no more worrying about stepping on an alignment landmine.

This is the standard way to write buffer storage by hand in C++. In older code you will also often see the `std::aligned_storage<sizeof(T), alignof(T)>` template — it was deprecated in C++23 (see LWG3867/P2967), and `alignas(T) char buf[sizeof(T)]` is the now-recommended form: more direct, no detour through templates.

### Access: `reinterpret_cast<T*>(storage_)`

Once construction is done, you still have to use that char memory as a `T` — done by casting the address to `T*` with `reinterpret_cast<T*>(storage_)`. This step is legal: after placement new has run, a genuine, bona fide T object really does live inside that char memory, so pointing a `reinterpret_cast` at it is well-defined. NoDestructor's `get()` is written exactly this way (no_destructor.h:118-119):

```cpp
T* get() { return reinterpret_cast<T*>(storage_); }
```

---

## Manual lifetime: constructed but never destructed

Put the previous pieces together and you can see what NoDestructor is doing. It clutches a raw `alignas(T) char storage_[sizeof(T)]` buffer, and at construction time it placement-news T on with `new (storage_) T(args...)`. And then — here comes the key point — it never leaves T a path to destruction at all. `~NoDestructor()` is `= default`; what it destroys is that char array, and a char array is a trivial type that does nothing. `~T()` will never be called on this path.

That is the whole secret of "constructed but never destructed": once placement new has shaped T into being, it just sits in that `storage_` until process exit, when the operating system reclaims the entire process memory — the T inside it included — as ordinary memory, all in one sweep. Note that the reclaimer here is not T's destructor. It is the OS.

### Is that safe

What about the resources T itself holds? Take `NoDestructor<vector<int>>` and the pile of elements the vector allocated on the heap. Frankly, those resources are not released by `~T()` — because `~T()` never ran in the first place. What they rely on is the OS reclaiming the entire address space in one sweep at process exit. During the program's run this memory counts as "leaked," but the program is about to end — who is left to see the leak? The OS will catch it either way.

What actually goes wrong is the other case: T's destructor carries side effects. Say a destructor is in charge of flushing logs to disk, or notifying another process "I'm leaving." Those side effects will not happen, because the destructor didn't run. So NoDestructor only suits the kind of type where "destructing is nothing but releasing resources" — for types where side effects are lost because the destructor never runs, don't use it.

---

## A minimal reproduction

All talk and no practice is empty kung fu — let's hand-roll a minimal version ourselves and get a first-hand feel for what placement new plus "no destruction" is like:

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
    ~MiniNoDestructor() = default;   // does not call ~T()!
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
        static const MiniNoDestructor<Noisy> nd;   // constructed once
        // leaving scope / program exit: ~MiniNoDestructor runs (trivial), ~Noisy does not
    }
    std::puts("(程序退出前 ~Noisy 不会打印)");
    return 0;
}
```

Run it and you will see: `Noisy()` prints once, but the `~Noisy()` line — not a single line prints. That is NoDestructor's "no destruction," solid and real.

---

The parts are all here. placement new gives us "construct only, don't allocate"; `alignas(T) char storage_[sizeof(T)]` gets us past the alignment gate; and `~NoDestructor()=default` quietly walls off the destruction path — put the three together and T just squats in `storage_`, refusing to leave, until the OS cleans everything up at process exit. In the next piece we get to actually assemble NoDestructor. Parts alone are not enough — we still have to see how it covers the corners: initialization ordering, the legal path through `reinterpret_cast`, and the like.

## References

- [cppreference: placement new](https://en.cppreference.com/w/cpp/language/new#Placement_new)
- [cppreference: alignof / alignas](https://en.cppreference.com/w/cpp/language/alignas)
- [cppreference: std::aligned_storage (deprecated since C++17)](https://en.cppreference.com/w/cpp/types/aligned_storage)
- [Chromium `base/no_destructor.h` — storage_ and get()](https://source.chromium.org/chromium/chromium/src/+/main:base/no_destructor.h)
