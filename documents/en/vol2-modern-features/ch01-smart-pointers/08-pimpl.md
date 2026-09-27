---
chapter: 1
cpp_standard:
- 11
- 14
- 17
description: 'The idiom that moves implementation details into a .cpp: the language rules for incomplete types, how unique_ptr and shared_ptr differ at destruction, a compilation firewall benchmark, and where const stops holding'
difficulty: intermediate
order: 8
platform: host
prerequisites:
- 'Chapter 1: Deep Dive into unique_ptr: The Zero-Overhead Smart Pointer with Exclusive Ownership'
- 'Chapter 1: Deep Dive into shared_ptr: Shared Ownership and Reference Counting'
- 'Chapter 1: Custom Deleters and Intrusive Reference Counting'
reading_time_minutes: 16
related:
- 'Bridge Pattern: Decoupling Abstraction from Implementation and Introducing pImpl Along the Way'
- 'scope_guard and defer: A General-Purpose Scope Guard'
tags:
- host
- cpp-modern
- intermediate
- 智能指针
- unique_ptr
- shared_ptr
title: 'The PIMPL Idiom: unique_ptr, shared_ptr, and Incomplete Types'
translation:
  source: documents/vol2-modern-features/ch01-smart-pointers/08-pimpl.md
  source_hash: a921998a3bbbfdd72755d3f29f5fae6cef108be06deb04b38b123b6638dfcbbe
  translated_at: '2026-09-27T05:01:24+00:00'
  engine: anthropic
  token_count: 4500
---
# The PIMPL Idiom: unique_ptr, shared_ptr, and Incomplete Types

PIMPL is, in my view, the single most widely used design pattern in real-world, multi-collaborator engineering. If you are a university student grinding through a group assignment, or building a joint project (the kind where a whole crowd of people have to integrate with one another), please take PIMPL seriously. It is so widespread that friends have told me the only design pattern you really need to learn is PIMPL (though, as I always joke, I work on GUI frameworks, so I still need the Observer pattern, ha!)

All right, back on track! Last time we talked about scope_guard, extending “do one thing on scope exit” from resource release to state rollback. This is the final article of the chapter, and we will close on a different note: no new tools this time. Instead, we drag everything the chapter has stockpiled into one engineering idiom and put it through its paces. unique_ptr's deleter, shared_ptr's control block—everything gets used. The idiom is called PIMPL (Pointer to Implementation), and Qt's famous d-pointer (Qt's in-house name for the technique) is a variant of it.

You have surely met the problem itself. You write a `class Widget` in a header and dutifully place `std::vector<std::string> cache_` under `private:`. Logically we cannot touch it, but physically there is no hiding it: pick any translation unit that includes it (one .cpp together with all the headers it includes, compiled into one object file) and the member definitions are all sitting there in plain view. For the compiler to compute `sizeof(Widget)` and lay out an object on the stack, it must see the complete definition of every member, so heavyweight headers like `<vector>` and `<string>` get dragged along to every includer. The day you add an `int` next to `cache_`, every file in the project that ever included widget.h recompiles in solidarity.

Our solution is—the PIMPL idiom (side-eye grin). Fine, no more cliffhanger. The whole recipe fits in one sentence.

> Move the entire private section into a struct named `Impl`, leaving nothing in the header but a forward declaration (a declaration that gives the type's name without a definition) plus a single pointer to it.

That is all there is to it! Readers who want the full details can follow the complete design-pattern evolution (swapping the raw pointer for `unique_ptr`, restoring copyability with `clone()`, marking the move operations `noexcept`) in the vol4 article on the [Bridge pattern](/vol4-advanced/vol4-generics-patterns/06-bridge), which walks through it step by step; we will not retrace it here.

## What It Looks Like in Use

```cpp
// widget.h
#pragma once
#include <memory>

class Widget {
public:
    Widget();
    ~Widget();                    // declared only; the definition lives in widget.cpp
    int  value() const;
    void bump();
private:
    struct Impl;                  // forward declaration: all member details live in widget.cpp
    std::unique_ptr<Impl> impl_;
};
```

```cpp
// widget.cpp
#include "widget.h"

struct Widget::Impl {
    int count = 0;
    int padding[7]{};
};

Widget::Widget() : impl_(std::make_unique<Impl>()) {}
Widget::~Widget() = default;
int  Widget::value() const { return impl_->count; }
void Widget::bump()        { ++impl_->count; }
```

In the header, `Impl` is down to a bare name: we cannot compute its `sizeof`, and we do not need to—`Widget` itself is now exactly one pointer in size. The real members and the real logic are all locked away in widget.cpp. Seeing `~Widget()` declared but not defined, you will probably ask: can we just write `= default` inline in the class, since the destructor is default anyway? The vol4 article already answered “no” with a compiler error; here we chase the question one level deeper: by what right does the standard stop us?

## delete Needs to Know How Big the Deleted Thing Is

Let's pull the counterexample out and compile it on its own, with the destructor written as an in-class `= default`—at which point `Impl` is still just a forward-declared name:

```cpp
#include <memory>

class Widget {
public:
    Widget();
    ~Widget() = default;   // in-class defaulted destructor: Impl is only forward-declared at this point
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

int main() {
    Widget w;   // an object must actually be constructed so the destructor is used and the compiler generates it
}
```

GCC 16 (x86_64 Linux) turns us down; the error below is excerpted to the first few lines:

```text
/usr/include/c++/16/bits/unique_ptr.h: In instantiation of 'void std::default_delete<_Tp>::operator()(_Tp*) const [with _Tp = Widget::Impl]':
/usr/include/c++/16/bits/unique_ptr.h:408:17:   required from 'std::unique_ptr<_Tp, _Dp>::~unique_ptr() [with _Tp = Widget::Impl; _Dp = std::default_delete<Widget::Impl>]'
a_unique_err.cpp:6:5:   required from here
/usr/include/c++/16/bits/unique_ptr.h:90:23: error: invalid application of 'sizeof' to incomplete type 'Widget::Impl'
   90 |         static_assert(sizeof(_Tp)>0,  <- look right here!!!
```

The error's last line lands inside libstdc++'s `default_delete`; that `static_assert(sizeof(_Tp)>0)` is the check it installed. Digging further: `delete p` has two jobs—call `Impl`'s destructor, then hand the memory back according to the object's size. Both jobs need the type's complete definition—the destructor's definition and the value of `sizeof`—and a forward declaration provides neither.

By what right does the standard stop us? Here is what the clause says: if the type being deleted is incomplete, and its complete definition has a non-trivial destructor or declares its own deallocation function (an in-class `operator delete`), the behavior is undefined (wording in [expr.delete] since C++11; earlier standards carried similar requirements). And we have already seen the awkward part: precisely because the type is incomplete, the compiler cannot even ask whether its destructor is trivial, so it could not wave us through even if it wanted to. libstdc++ simply demands a complete type across the board—one `sizeof` probe and you are stopped—trading a slice of undefined behavior for a compiler error we can actually read.

There is another detail worth stopping for. Delete the `Widget w;` line from main and compile again: 0 errors, 0 warnings, as if nothing had ever happened. Not every function enjoys this treatment: member functions of class templates instantiate on demand and generate no code until called; in-class `= default` special members behave the same way—defined only where used. Plain functions are the exact opposite: their bodies compile whether or not anyone calls them, so errors cannot hide. Our example happens to hit both escape hatches at once: `unique_ptr`'s destructor is a template member, and `Widget`'s destructor is an in-class `= default`; if we never construct a `Widget`, neither destructor is ever called, and the code containing that `static_assert` is never generated at all. This also explains why someone can type PIMPL into an empty project following a blog post, build it just fine, and then flip the car the moment the code moves into a real project: the write-up was not wrong—it just had not yet reached the step that forces the compiler's hand.

cppreference's wording is blunt: `unique_ptr` may be constructed with an incomplete type—the standard library left that door open precisely for pImpl—but with the default deleter, T must be complete at the point where the deleter is invoked. There are three places we can run into the deleter: `unique_ptr`'s destructor, move assignment, and `reset()`. We have just met the destructor; what about the other two? Let's try move assignment next.

## Move Assignment: Another Doorway to the Same Requirement

This time we arrange the project the way it looks in real code: the destructor dutifully moves into widget.cpp, and only move assignment stays in-class.

```cpp
// widget.h
#pragma once
#include <memory>

class Widget {
public:
    Widget();
    ~Widget();                              // the destructor moved out
    Widget& operator=(Widget&&) = default;  // move assignment still in-class
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
```

```cpp
// widget.cpp
#include "widget.h"

struct Widget::Impl { int count = 0; };

Widget::Widget() : impl_(std::make_unique<Impl>()) {}
Widget::~Widget() = default;
```

```cpp
// main.cpp
#include "widget.h"
#include <utility>

int main() {
    Widget a, b;
    a = std::move(b);   // the in-class = default move assignment is instantiated here
}
```

We compile widget.cpp on its own: smooth sailing. But when we compile main.cpp, the error is back—the same familiar line:

```text
/usr/include/c++/16/bits/unique_ptr.h: In instantiation of 'void std::default_delete<_Tp>::operator()(_Tp*) const [with _Tp = Widget::Impl]':
/usr/include/c++/16/bits/unique_ptr.h:204:16:   required from 'void std::__uniq_ptr_impl<_Tp, _Dp>::reset(pointer) [with _Tp = Widget::Impl; _Dp = std::default_delete<Widget::Impl>; pointer = Widget::Impl*]'
/usr/include/c++/16/bits/unique_ptr.h:184:2:   required from 'std::__uniq_ptr_impl<_Tp, _Dp>& std::__uniq_ptr_impl<_Tp, _Dp>::operator=(std::__uniq_ptr_impl<_Tp, _Dp>&&)'
/usr/include/c++/16/bits/unique_ptr.h:236:24:   required from here
/usr/include/c++/16/bits/unique_ptr.h:90:23: error: invalid application of 'sizeof' to incomplete type 'Widget::Impl'
   90 |         static_assert(sizeof(_Tp)>0,
```

Let's walk the call chain. `Widget`'s move assignment moves `impl_`, the `unique_ptr`; and `unique_ptr`'s move assignment resets the old pointer it holds before taking over the new one—and reset internally calls the deleter. Look at the second line of the error: required from `reset`, exactly. So this is not just “destructors are special”—wherever the deleter might be invoked, `Impl` has to be present. Look at the same class: in widget.cpp, `Impl` is complete, so compilation passes; in main.cpp, `Impl` is a bare name, and it is stopped on the spot. Whoever instantiates where the type is complete sails through the build.

## The Same = default Line, and the shared_ptr Version Passes Everything

Now for the fun part. We swap `std::unique_ptr<Impl>` for `std::shared_ptr<Impl>` and change nothing else: the constructor stays declaration-only with its definition moved to where `Impl` is complete, and the destructor boldly stays in-class. Then we split the project into two translation units, making sure the side containing main never sees `Impl`'s definition from start to finish:

```cpp
// sh.h
#pragma once
#include <memory>

class Widget {
public:
    Widget();
    ~Widget() = default;   // the destructor stays in the header: Impl is never complete in this translation unit
    int  value() const;
    void bump();
private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
```

```cpp
// sh.cpp — Impl becomes complete only here
#include "sh.h"

struct Widget::Impl { int count = 0; };

Widget::Widget() : impl_(std::make_shared<Impl>()) {}
int  Widget::value() const { return impl_->count; }
void Widget::bump()        { ++impl_->count; }
```

```cpp
// main.cpp
#include "sh.h"
#include <cstdio>

int main() {
    Widget w;   // w's destructor is used in this translation unit, where Impl is incomplete
    w.bump();
    w.bump();
    std::printf("count = %d\n", w.value());
}
```

And in main.cpp, `Widget w`'s destructor is genuinely used, while the header's `Impl` remains nothing but a name on that side from start to finish. By the previous section's logic, this should be doomed, right? We compiled, linked, and ran it—everything passed:

```text
$ g++ -std=c++17 -Wall -Wextra sh.cpp main.cpp -o app && ./app
count = 2
```

We have also placed a single-file version below—click “Try it yourself” and it runs on the spot, with the constructor's and destructor's positions already arranged for you:

<OnlineCompilerDemo
  title="Hands-On Verification: The shared_ptr Version Keeps the Destructor In-Class"
  source-path="code/examples/vol2/49_pimpl_shared_destructor.cpp"
  description="Verify online that the same in-class = default destructor works: with the constructor's definition moved to where Impl is complete, the destructor can stay in the header; compilation, linking, and running all pass, printing count = 2."
  run-options="-std=c++17"
  allow-run
/>

Both versions contain the very same line `~Widget() = default`, yet the unique_ptr version is rejected by the compiler while the shared_ptr version compiles, links, and runs clean. The difference is not in how the header is written—it is in how the two smart pointers store their deleters. We drew this line in the [custom deleters](06-custom-deleter.md) article: `unique_ptr`'s deleter is part of the type, packed into the object itself via the Empty Base Optimization (EBO), with calls wired directly at compile time. `shared_ptr`'s deleter is type-erased and hidden in the heap-allocated control block.

The deleter storage locations and destruction paths of the two smart pointers line up like this:

![Where unique_ptr and shared_ptr store their deleters, and the corresponding destruction paths](./08-pimpl-deleter-path.drawio)

What does that direct wiring mean? Look at `delete impl_` inside `unique_ptr`'s destructor: it must produce machine code in the translation unit that instantiates it. Producing machine code requires computing `sizeof` and locating the destructor; with `Impl` incomplete, neither is possible, so the compiler has no choice but to refuse us. `shared_ptr`'s destructor does far less: decrement the control block's reference count, and when it hits zero, call the destruction function prepared inside the control block. That destruction code was generated at construction time—the location of `make_shared<Impl>()` is where `Impl` is complete, and that is where the deletion action was recorded into the control block; the destructor site merely hands over. Search the whole path from end to end: nowhere is `sizeof(Impl)` needed, so nowhere does completeness get demanded.

So here is how to remember the two pImpl recipes. For the unique_ptr version, move every member function that can trigger deletion (the destructor and move assignment) into the .cpp, and you are done. For the shared_ptr version, move the constructor's definition into the .cpp, and the destructor may stay in the header. Sounds like the shared_ptr version is less hassle, doesn't it? Let's see whether it can afford that convenience.

## Where shared_ptr Also Gets Stuck: Construction

Try to save effort by writing the constructor in the header too, and shared_ptr refuses on the spot. We put two classes in one file: the first, `Widget`, keeps both constructor and destructor in-class; the second, `BadWidget`, calls `make_shared` where `Impl` is not yet complete:

```cpp
#include <memory>

class Widget {
public:
    Widget() = default;   // in-class default constructor: only creates an empty shared_ptr, never touches Impl
    ~Widget() = default;  // destructor also in-class; the previous section verified this one passes
    void show() const;
private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

class BadWidget {
public:
    BadWidget() : impl_(std::make_shared<Impl>()) {}  // make_shared where Impl is incomplete
    ~BadWidget() = default;
private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

int main() {
    Widget w;
    BadWidget b;   // forces BadWidget's constructor to be instantiated
}
```

GCC stops us again. The error below is excerpted to its first and last lines; the line number 15 in the source echo matches the `BadWidget` constructor line in the listing you just read:

```text
/usr/include/c++/16/ext/aligned_buffer.h: In instantiation of 'struct __gnu_cxx::__aligned_buffer<BadWidget::Impl>':
/usr/include/c++/16/bits/shared_ptr_base.h:658:50:   required from 'class std::_Sp_counted_ptr_inplace<BadWidget::Impl, std::allocator<void>, __gnu_cxx::_S_atomic>::_Impl'
  658 |         __gnu_cxx::__aligned_buffer<__remove_cv_t<_Tp>> _M_storage;
/usr/include/c++/16/bits/shared_ptr_base.h:722:13:   required from 'class std::_Sp_counted_ptr_inplace<BadWidget::Impl, std::allocator<void>, __gnu_cxx::_S_atomic>'
  722 |       _Impl _M_impl;
b2_shared_ctor_err.cpp:15:47:   required from here
   15 |     BadWidget() : impl_(std::make_shared<Impl>()) {}
/usr/include/c++/16/ext/aligned_buffer.h:99:58: error: invalid application of 'sizeof' to incomplete type 'BadWidget::Impl'
   99 |       alignas(__alignof__(_Tp)) unsigned char _M_storage[sizeof(_Tp)];
```

It is `sizeof` waiting for us again, just ambushing from a different spot. As we covered in [Deep Dive into shared_ptr](04-shared-ptr.md), `make_shared`'s selling point is allocating the object and control block in one shot—the object is embedded directly in the control block's memory, and that `_M_storage` in the error is exactly the space reserved for the object. Embedding requires reserving space; reserving space requires computing the size; and `Impl` is precisely incomplete—that is where we are stuck. cppreference's wording for shared_ptr is symmetrically neat: shared_ptr may be used with incomplete types, but every entry point that actually creates or takes over an object—constructing from a raw pointer, reset, and the like—requires the type to be complete. The requirement does not care about the pointer's value: pass a null raw pointer and it still stops you. The entries genuinely free of this requirement are default construction and null-pointer construction: neither creates an object at all. Our default-constructed `shared_ptr<Impl>` created nothing, so whether `Impl` is complete stops mattering. That is exactly how the first `Widget` in the listing—constructor and destructor both in-class—earns not a single line in the error. Its constructor passes precisely by riding on this.

Let's put the costs on the table too. The shared_ptr version's object is 16 bytes—double the unique_ptr version—and every copy and every destruction pays an atomic operation plus one heap allocation for the control block; we measured these item by item in article 04. All that buys is one small convenience: “the destructor may stay in the header.” And a PIMPL `Impl` is born to be exclusive: what a `Widget` owns is one `Impl`, shared with no one, which is why pImpl members in real projects are held almost exclusively by exclusive pointers. Using the shared_ptr version for pImpl is not forbidden—it is just that you would be trading doubled size, atomic refcounting, and a control-block allocation for one sliver of convenience, and in most settings the arithmetic does not work out.

## The Compilation Firewall: Measuring It with g++

Rules covered; back to the build directory for a hands-on incremental-build measurement. The project is the same set of files from the top of this article, plus a main.cpp:

```cpp
// main.cpp
#include "widget.h"

int main() {
    Widget w;
    w.bump();
    return w.value() == 1 ? 0 : 1;
}
```

This time, no Makefile and no CMake—we drive g++ command by command. Who needs recompiling and who gets to skip is entirely our call. The first build is naturally a full one: the two .cpp files each compile to an object file, then link into app:

```text
=== First build: compile everything ===
g++ -std=c++17 -O2 -c main.cpp -o main.o
g++ -std=c++17 -O2 -c widget.cpp -o widget.o
g++ -std=c++17 -O2 main.o widget.o -o app
```

Then we modify `Impl`'s internals, adding an `int new_field = 0;`. This step touches only widget.cpp: widget.h has not changed by a single byte, and all main.cpp can see is widget.h, so we have no reason whatsoever to rerun that main.cpp compile command—recompiling widget.o and relinking is enough:

```text
=== After adding a new member to Impl: only widget.o needs recompiling ===
g++ -std=c++17 -O2 -c widget.cpp -o widget.o
g++ -std=c++17 -O2 main.o widget.o -o app
```

Look at the second run's commands: widget.cpp was recompiled, and the compile command for main.cpp had no reason to appear at all. The updated app runs as usual; main.o is still the one produced by the first build, timestamp untouched. A field was added inside `Impl`, and from the outside we cannot tell at all. The industry's customary name for this technique of blocking compile dependencies is the compilation firewall. Its practical meaning: however the implementation changes, the compile dependencies stay bottled up inside the single file widget.cpp. As projects grow, incremental build tools like make and CMake remember this dependency for us—which file changed, who includes whom, they know at a glance—using exactly the judgment we just applied by hand.

And the version with members laid out directly, no PIMPL? widget.h now contains `int count` and `std::vector<std::string> cache_`. We add a single `int` under `private:` and run the same flow again:

```text
=== After adding just one private member to widget.h ===
g++ -std=c++17 -O2 -c main.cpp -o main.o   <- main.o must be recompiled too
g++ -std=c++17 -O2 -c widget.cpp -o widget.o
g++ -std=c++17 -O2 main.o widget.o -o app
```

This time main.cpp goes through the rerun as well. The header changed; whether the member layout moved, the compiler cannot prove, so the main.o built against the old layout is forfeit. In a two-file project you may not care—but if that header is included by hundreds of .cpp files, your one added `int` buys hundreds of recompiles, and that is exactly how CI time climbs, second by second.

Count both logs side by side: in the direct-members version, adding one `int` means recompiling two targets—main.o and widget.o—plus rerunning the link; with PIMPL, the same change leaves exactly one target to recompile, widget.o, and main.o need not be touched by a single byte. The contrast between these two scenes is animated here:

<Anim id="pimpl-rebuild-scope" />

There is one more layer of payoff: `sizeof(Widget)` is from now on permanently equal to one pointer. Add members to `Impl`, change member types—the external binary interface (Application Binary Interface, ABI for short below: the layout and calling conventions between already-compiled binary code) stays fixed, and object files compiled by users keep linking as before. The before/after sizeof measurements were done in the vol4 article; we will not repeat them.

## The const Guarantee Breaks Right Here

One more language-level side effect, far quieter than a compile error and much easier to miss: inside a pImpl class, a const member function can still modify `Impl`'s members. Hard to believe? Let's write one:

```cpp
#include <memory>
#include <cstdio>

class Counter {
public:
    Counter() : impl_(std::make_unique<Impl>()) {}

    // inc is declared const yet mutates state; compiles and runs
    void inc() const { ++impl_->count; }

    int get() const { return impl_->count; }

private:
    struct Impl { int count = 0; };
    std::unique_ptr<Impl> impl_;
};

int main() {
    const Counter c;   // a const object
    c.inc();           // and it really does mutate
    c.inc();
    std::printf("const 对象 get() = %d\n", c.get());
}
```

The demo is below—click “Try it yourself” to run it directly:

<OnlineCompilerDemo
  title="Hands-On Verification: const Propagation Breaks on pImpl"
  source-path="code/examples/vol2/50_pimpl_const_leak.cpp"
  description="Verify online that const loses its grip: a const object is mutated twice by a const member function; compilation and running both pass, printing const 对象 get() = 2. The source file also keeps the control-group compiler error from the direct-member version."
  run-options="-std=c++17"
  allow-run
/>

The output we get:

```text
const 对象 get() = 2
```

There it is: a const object, mutated twice by a const member function, with compilation and runtime waving us through the whole way. In a class with members written directly, the same `inc()` gets stopped by the compiler on the spot:

```text
error: increment of member 'NaiveCounter::count_' in read-only object
```

Let's walk the mechanism step by step. Inside a const member function, `this` has type `const Counter*`. Reading `impl_` through `this` yields a `const std::unique_ptr<Impl>&`: the pointer itself is indeed read-only. But `unique_ptr`'s `operator->` returns `Impl*`—never `const Impl*`. The pointer itself is read-only; the pointee is not. We added one layer of indirection, and const breaks precisely on that layer.

Broken here means the interface's promise is now guarded by humans alone. You declare `void inc() const` in the header; readers reasonably expect it not to mutate state, yet the count inside `Impl` gets incremented all the same. We have two ways to keep the promise. Either exercise discipline when writing pImpl classes—const methods do not touch `impl_`'s mutable members—or give `Impl` two accessor sets, const and non-const, so the const constraint propagates down. The compiler no longer checks this for you; that is something we need to keep firmly in mind.

## Costs and Choices

We pay one heap allocation for `Impl`, plus one extra pointer dereference on every member access—`impl_->count` makes one more memory trip than a plain `count`. The implementation has moved into a .cpp, so cross-translation-unit inlining is gone; to win it back we have to enable LTO (short for Link Time Optimization—optimization performed at link time). As for object size, we measured on GCC 16, x86_64:

```text
sizeof(unique_ptr pImpl Widget) = 8
sizeof(shared_ptr pImpl Widget) = 16
sizeof(NaiveWidget with members directly in the header) = 192
```

Put the direct-members version and the pImpl version side by side: this direct version carries two heavyweight members of 96 bytes each—four `double`s plus a 64-byte buffer apiece—and pImpl swaps all of that for one pointer. It is not the same class as the naive control in the firewall section: that one laid out `int count` plus `std::vector<std::string> cache_` directly, far lighter; here we deliberately picked a heavier direct-members version to measure, so the 192-versus-8 gap would be striking enough. A small class used inside a single .cpp has a header nobody includes, so there is no firewall to speak of—just one wasted heap allocation. We also advise against pImpl for small hot-path types accessed inline: one dereference and one cross-unit call are real, honest losses. On the flip side, interface classes included widely across the project, facade classes hiding `<unordered_map>` or heavyweight third-party dependencies, and classes shipped in dynamic libraries whose binary interface must stay stable across versions—these are cases where the costs earn their keep. When in doubt, measure compile times before and after touching the header, then decide.

And with that, the chapter wraps. We started from RAII's acquire-and-release, grounded the three ownership relationships with unique_ptr, shared_ptr, and weak_ptr, let custom deleters take over C APIs and hardware handles, used scope_guard to generalize “do one thing on exit” to broader scenarios, and finally this PIMPL article strung the whole chapter's toolkit into one engineering idiom. Next time you look at a library header that exposes nothing but a pointer, you should recognize it—and be able to explain where the destructor landed, where const broke, and where the compile dependencies were walled off. The next chapter takes on constexpr and compile-time computation: we will revisit the old habit of “leave it for runtime to compute” and see which computations can be moved up to compile time.

## References

- [cppreference: std::unique_ptr (the Notes section covers the incomplete-type requirements)](https://en.cppreference.com/w/cpp/memory/unique_ptr)
- [cppreference: std::shared_ptr (Notes and Implementation notes)](https://en.cppreference.com/w/cpp/memory/shared_ptr)
- [cppreference: std::make_shared](https://en.cppreference.com/w/cpp/memory/shared_ptr/make_shared)
- [C++ Core Guidelines: R.20-24](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#ss-smart)
- [C++ Core Guidelines: I.27 For stable library ABI, consider the Pimpl idiom](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#ri-pimpl)
- Herb Sutter, *GotW #100: Compilation Firewalls* and *GotW #28: The Fast Pimpl Idiom*
