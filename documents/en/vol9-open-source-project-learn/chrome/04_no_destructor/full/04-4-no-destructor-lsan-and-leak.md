---
chapter: 1
cpp_standard:
- 11
- 17
- 20
description: "Work through NoDestructor's deliberate-leak tradeoff: LeakSanitizer false-positives it trips, and the storage_ptr_ reachability hack Chromium uses to stay LSan-clean (crbug/40562930)"
difficulty: intermediate
order: 4
platform: host
prerequisites:
- 'NoDestructor hands-on (II): the core implementation'
- 'NoDestructor hands-on (III): when to use it, and when not to'
reading_time_minutes: 8
related:
- 'NoDestructor hands-on (I): motivation and API design'
tags:
- host
- cpp-modern
- intermediate
- 内存管理
- 内存安全
title: "NoDestructor hands-on (IV): the LSan leak tradeoff and the reachability hack"
---
# NoDestructor hands-on (IV): the LSan leak tradeoff and the reachability hack

In [04-2](./04-2-no-destructor-core-impl.md) we broke NoDestructor's core down into "placement new + never call `~T()`." Writing that, something kept nagging at us. The heap resources T holds, the elements a `NoDestructor<vector<int>>`'s vector allocates on the heap, never get released once `~T()` is skipped. They "leak," all the way to process exit. For a global singleton that lives until the program ends, that's fine: the process is gone, the OS reclaims the whole process's memory. The problem is that LeakSanitizer (LSan) sees it as a genuine memory leak and sounds the alarm.

That pushes us into a tradeoff. Let's lay the cost out first.

T's destructor normally does two things. One, hand back the resources T itself holds, the vector's heap memory, a file's `close`, that kind of thing. Two, run side effects that matter to the outside world, flushing logs to disk, notifying another process. NoDestructor skips `~T()`, and both stop happening. The first one doesn't matter, the OS covers it. The second is the one to watch: wrap a type whose destructor has to flush logs, and the data is gone. So a fairly hard line: NoDestructor suits types whose destructor just returns resources; it doesn't suit types whose destructor has external side effects.

What that cost buys, in our view, is the thing most worth having: no destruction-order problem. Shutdown races, in a program the size of Chromium where the shutdown paths twist like spaghetti, are something anyone who's been bitten by them knows about. Skipping the whole category makes the "we don't free those resources" tradeoff easy to accept. As for the LSan question—whether an alarm even fires—we'll put that to the test next.

---

## How LeakSanitizer works

To answer whether it fires at all, we first have to look at how LSan decides something leaked. It's the leak specialist in the AddressSanitizer family, and the technique is **reachability analysis**. The name sounds intimidating; the idea is plain. At program exit it walks every "root," roots being global variables, stack slots, and registers holding pointers. From those roots it follows pointers along, and any heap memory it can reach this way counts as reachable. The heap blocks no live pointer can find are leaks.

One more crucial detail: this "following" is a **conservative byte-level scan**. LSan ignores types and declarations; it treats every aligned machine word in a root region as a candidate pointer and tries it—better to over-recognize than to miss. That detail is exactly what saves the NoDestructor case below.

Note LSan doesn't care at all whether this memory was "supposed to be destructed." It checks one thing: is anyone still pointing at this block. If nobody is, it's a leak, even when you know full well it's the harmless "intentional leak, OS covers it" case.

---

## NoDestructor through LSan's eyes

Take the plainest code:

```cpp
static const base::NoDestructor<std::vector<int>> v({1, 2, 3});
```

Inside `v` is `alignas(vector) char storage_[sizeof(vector)]`, a char array, with the vector placed on top via placement new. The vector itself allocates the `{1,2,3}` storage on the heap. The layout is fine, and it looks fine to a human.

A widely repeated claim goes: "`storage_` is a char array; LSan can't recognize the pointer hiding inside it, the reachability chain breaks, and `NoDestructor<vector<int>>` inevitably gets falsely reported as a leak under an LSan build." [crbug.com/40562930](https://crbug.com/40562930) records exactly that old pit, and Chromium even added the hack from the next section for it—on the LSan of that era, the claim held.

But run it on a modern toolchain (g++ 16.1.1 / clang++ 22.1.8, `-fsanitize=leak`) and the false positive **no longer happens**: a static `NoDestructor<vector<int>>` passes the leak check cleanly (`__lsan_do_recoverable_leak_check()` returns 0, no report at all), while in the same environment a genuine-leak control case (a `new int[100]` whose pointer is wiped) is duly caught (400 bytes, exit 23). The reason is the detail from the previous section: LSan's root scan is a conservative byte-level scan. `storage_` being a char array doesn't matter—the pointer-shaped bytes of the vector object inside it are followed as roots just the same, and the heap `{1,2,3}` stays reachable. A char array can't "hide" a pointer from modern LSan; it never gets the chance.

---

## The reachability hack: storage_ptr_

Chromium's workaround, the first time we read it, stopped us cold. We genuinely did not see this coming (no_destructor.h:132-142):

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

Under an LSan build it slips an extra `T* storage_ptr_` member in, pointing at the T object placed via placement new. Plainly put, this address is the same one as `storage_`; only the type changes, from `char*` to `T*`.

In the original pit, this line was a real fix: the root scanning of older LSan versions—on some platforms—wasn't conservative enough, and the pointer bytes inside `storage_` really did get missed. Swap in an explicit `T*` root and LSan reaches the T object, then follows the T object's pointers all the way to the vector's heap `{1,2,3}`, and the alarm goes quiet. Today, on mainstream Linux toolchains, this line is mostly asleep on the job—but the idea is still elegant: rather than betting that every runtime on every platform scans conservatively, hand it an explicit root whose type is already `T*`, turning "can it be found" from an implementation detail into a written guarantee. Chromium needs to be clean under LSan builds on all platforms; keeping this line is the safest call.

One more detail worth flagging: this member only exists when `LEAK_SANITIZER` is defined. A normal build doesn't have it at all, zero overhead. Standard conditional-compilation practice; you pay this small cost only in the builds that need it, the test/debug builds with LSan on.

---

## The teaching version's simplification

Our teaching version drops the LSan hack (implementation in [04-2](./04-2-no-destructor-core-impl.md)). The reasons we worked through came down to a few.

It only has any effect in an LSan build; a normal compile never touches it. And it's fundamentally an engineering detail about "making a tool not false-report," not a part of NoDestructor's core mechanism. The core is still the placement-new-plus-no-destructor setup; this hack is just a peripheral patch. More practically, if your project genuinely needs to run clean under LSan, you don't necessarily have to change the code: LSan supports suppression files (say `lsan_suppressions.txt`), and listing the NoDestructor leak in there to be explicitly ignored gets you the same result.

Of course, if you'd rather have the code report zero alarms under an LSan build and not maintain a suppression file, there's nothing wrong with following Chromium and adding the line `#if defined(LEAK_SANITIZER) T* storage_ptr_; #endif`. It's just conditional compilation; whether you add it or not doesn't change NoDestructor's semantics. It's purely for LSan to read.

---

With this, the NoDestructor thread, motivation, implementation, when to use it, LSan compatibility, is complete. Looking back at the whole thing is genuinely interesting: with just placement new plus no destructor, you trade for a fairly solid piece of a solution to the long-standing C++ pain of "global/static objects have no destruction-order problem," and you patch the historical LSan reachability hole along the way with a single `T*`. In the vol9/chrome series it sits alongside OnceCallback (callbacks), WeakPtr (weak references), and flat_map (containers), making up the four pieces of Chromium's `//base` infrastructure: callbacks, weak references, containers, static lifetime. Each minds its own shop.

## References

- [Chromium `base/no_destructor.h`: the LSan hack comment (no_destructor.h:132-142)](https://source.chromium.org/chromium/chromium/src/+/main:base/no_destructor.h)
- [crbug.com/40562930: NoDestructor LSan false positive](https://crbug.com/40562930)
- [LeakSanitizer documentation](https://clang.llvm.org/docs/LeakSanitizer.html)
- [AddressSanitizer family](https://clang.llvm.org/docs/AddressSanitizer.html)
- [NoDestructor hands-on (II): the core implementation](./04-2-no-destructor-core-impl.md)
