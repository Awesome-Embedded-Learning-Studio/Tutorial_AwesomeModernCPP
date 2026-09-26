---
chapter: 1
cpp_standard:
- 11
- 17
- 20
description: The tradeoff behind NoDestructor's deliberate leak — LeakSanitizer false-positives on it, and Chromium keeps LSan compatibility with the storage_ptr_ reachability hack (crbug/40562930)
difficulty: intermediate
order: 4
platform: host
prerequisites:
- 'NoDestructor hands-on (II): the core implementation'
- 'NoDestructor hands-on (III): when to use it, and when not to'
reading_time_minutes: 8
related:
- 'NoDestructor Design Guide (I): motivation, API, and implementation'
tags:
- host
- cpp-modern
- intermediate
- 内存管理
- 内存安全
title: "NoDestructor hands-on (IV): the LSan leak tradeoff and the reachability hack"
translation:
  source: documents/vol9-open-source-project-learn/chrome/04_no_destructor/full/04-4-no-destructor-lsan-and-leak.md
  source_hash: 2acd04e81782d319f2cf0f0e7c58e054cd330b2d282ed5ccecfbe74f946a2599
  translated_at: '2026-09-26T03:19:46+00:00'
  engine: anthropic
  token_count: 4200
---
# NoDestructor hands-on (IV): the LSan leak tradeoff and the reachability hack

In [04-2](./04-2-no-destructor-core-impl.md) we broke NoDestructor's core down into "placement new + never call `~T()`." Even as we finished that section, something kept weighing on us: the heap resources T holds — say, the elements the vector inside a `NoDestructor<vector<int>>` allocates on the heap — will never be released once `~T()` no longer runs. They "leak" all the way to the end of the process. For a global singleton that lives until the program exits, that honestly does not matter: the process is gone, and the OS reclaims the whole process memory anyway. The problem is that in the eyes of LeakSanitizer (LSan), this thing is a genuine, no-two-ways-about-it memory leak, and the alarm goes off.

That pushes us into a tradeoff. Let's lay the cost out first.

T's destructor normally does two things. One, it gives back the resources T itself holds — the vector's heap memory, a file's close, all of that counts. Two, it runs side effects with external visibility, such as flushing logs to disk or notifying another process. NoDestructor skips `~T()`, and both of those are cut off. The first does not matter, the OS backstops it anyway; the second is what you really have to watch — wrap a type whose destructor must flush its logs, and the data is lost. Hence a fairly hard boundary: NoDestructor suits types whose destructor only returns resources, not types whose destructor has external side effects.

But what that price buys is, in our view, the most valuable thing of all: no destruction-order problems. Shutdown races — anyone who has stepped on one in a large program with shutdown paths as convoluted as Chromium's knows exactly what that pain is like. Sidestepping the entire class of trouble makes that little bit of "never released" resources perfectly acceptable, and as for the LSan alarm, we'll go deal with it right below.

---

## How LeakSanitizer works

To see why the alarm fires, we first need a glance at how LSan decides what counts as a leak. It is the leak-dedicated member of the AddressSanitizer family, and what it does is called **reachability analysis** — an intimidating name for a plain idea: at program exit, it first walks every "root," where roots are the pointers in global variables, on the stack, and in registers. Then it starts from those roots and follows pointers along the way; every piece of heap memory it can chase down counts as "reachable." The heap blocks left over — the ones no live pointer can find — are the leaks.

Note that LSan cares not at all about whether a piece of memory "should have been destructed" — it checks exactly one thing: is anything still pointing at this memory. If nothing is, it is a leak, even when you know full well this is the harmless case of "deliberate leak + the OS will backstop it."

---

## NoDestructor's problem from LSan's point of view

Take the plainest possible code:

```cpp
static const base::NoDestructor<std::vector<int>> v({1, 2, 3});
```

Inside `v` sits `alignas(vector) char storage_[sizeof(vector)]`, a char array, with the vector constructed onto it via placement new. The vector in turn allocates the `{1,2,3}` storage on the heap. The layout is fine, and everything looks fine to a human eye.

When LSan scans the roots it does see `v`'s `storage_` — but the crux is that `storage_` has the type of a **char array**. LSan will not chase it as a "pointer to the vector's heap memory"; in its eyes that is just a lump of raw bytes, even if those bytes happen to be precisely the vector's internal pointers. The chain breaks at this step: for the `{1,2,3}` the vector allocated on the heap, nothing reachable from LSan's root set can touch it — the reachability analysis walks into a dead end here and, naturally, reports a leak.

That is the pit recorded in [crbug.com/40562930](https://crbug.com/40562930): under an LSan build, a `NoDestructor<vector<int>>` gets reported as a leak — a false report. Though calling it "false" hardly means LSan judged wrong — its rule is reachability, and reachability is genuinely broken here; it is only from an engineering standpoint that we know this leak is deliberate and harmless.

---

## The reachability hack: storage_ptr_

Chromium's workaround stopped us cold on first read — we truly had not considered that this was even possible (no_destructor.h:132-142):

```cpp
#if defined(LEAK_SANITIZER)
    // TODO(crbug.com/40562930): This is a hack to work around the fact
    // that LSan doesn't seem to treat NoDestructor as a root for reachability
    // analysis. ...
    // hold an explicit pointer to the placement-new'd object in leak sanitizer
    // mode to help LSan realize that objects allocated by the contained type
    // are still reachable.
    T* storage_ptr_ = reinterpret_cast<T*>(storage_);
#endif
```

In an LSan build, it stuffs in one extra member, `T* storage_ptr_`, pointing at the T object that placement new constructed. To put it plainly: this is the same address as `storage_`; only the type changes, from `char*` to `T*`.

Swap that one type, and the problem is solved. Where were we stuck before? On LSan reading `storage_` as raw bytes and refusing to acknowledge the pointers inside. But `storage_ptr_` has type **`T*`** — and with that, LSan treats it as a legitimate "root pointing at a T object." Starting from it, LSan can chase down the T object, then follow the pointers inside the T object all the way to the vector's heap-allocated `{1,2,3}`. The whole reachability chain reconnects, that memory goes back to being "reachable," and the alarm disappears.

Put bluntly, the maneuver is this: LSan cannot make sense of the pointers hiding inside a char array, so we hand it one more thing it can make sense of — an explicit `T*` — making the placement-new'd object explicit one more time from LSan's point of view. The address did not change, the object did not change; the only thing that changed is "whether LSan can recognize this root."

One more detail worth flagging: this member exists only when `LEAK_SANITIZER` is defined; ordinary builds do not carry it at all — zero overhead. Textbook conditional compilation: pay this tiny cost only in the builds that need it (the test/debug builds with LSan enabled).

---

## What our teaching version leaves out

Our teaching version leaves this LSan hack out (for the implementation, see [04-2](./04-2-no-destructor-core-impl.md)). Thinking it over, our reasons boil down to a few.

It only has any effect in builds with LSan enabled; ordinary compilation never touches it. And in essence it is an engineering detail about "keeping the tool from false alarms," not a core mechanism of NoDestructor — the core is still the placement-new-plus-no-destructor business, and this hack is only a peripheral patch. More practically, if your project truly needs to run clean under LSan, changing code is not the only way: LSan itself supports suppression files (for example `lsan_suppressions.txt`), where you explicitly ignore the NoDestructor leak in question — the effect is the same.

Of course, if what you want is code that stays warning-free under an LSan build and you would rather not maintain a suppression file, following Chromium and adding the line `#if defined(LEAK_SANITIZER) T* storage_ptr_; #endif` is entirely fine too — it is plain conditional compilation; whether you add it or not, NoDestructor's semantics are unaffected. It exists purely for LSan to look at.

---

With this, the NoDestructor thread — motivation, implementation, when to use it, LSan compatibility — is complete. Looking back at the whole picture is genuinely interesting: with nothing more than placement new plus "don't destruct," it wins a solid piece of the solution to that old chronic C++ problem, "global/static objects with no destruction-order trouble," and, as a free bonus, patches the LSan reachability pit with a single line of `T*`. Within the vol9/chrome series, it pairs up with OnceCallback (callbacks), WeakPtr (weak references), and flat_map (containers) to form exactly the four pieces of Chromium's `//base` infrastructure — callbacks, weak references, containers, static lifetime — each covering its own corner.

## References

- [Chromium `base/no_destructor.h` — the LSan hack comment (no_destructor.h:132-142)](https://source.chromium.org/chromium/chromium/src/+/main:base/no_destructor.h)
- [crbug.com/40562930 — the NoDestructor LSan false positive](https://crbug.com/40562930)
- [LeakSanitizer documentation](https://clang.llvm.org/docs/LeakSanitizer.html)
- [The AddressSanitizer family](https://clang.llvm.org/docs/AddressSanitizer.html)
- [NoDestructor hands-on (II): the core implementation](./04-2-no-destructor-core-impl.md)
