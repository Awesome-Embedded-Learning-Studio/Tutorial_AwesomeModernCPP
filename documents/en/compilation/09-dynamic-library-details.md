---
chapter: 13
difficulty: intermediate
order: 9
platform: host
reading_time_minutes: 13
tags:
- cpp-modern
- host
- intermediate
title: "Deep Dive into C/C++ Compilation and Linking · Part 9: Dynamic Library Details (Finale)"
description: 'From PIC and GOT/PLT to symbol interposition — a thorough account of why the address of a dynamic library is "indeterminate" at runtime, and how the modern linker and loader cooperate to pull it off'
cpp_standard: [11, 14, 17, 20]
translation:
  source: documents/compilation/09-dynamic-library-details.md
  source_hash: 5580896e12e5bb415e61219070cc6a20f62f14a4a0def1b222a3ca9fbc73f637
  translated_at: '2026-09-26T00:05:28+00:00'
  engine: anthropic
  token_count: 9200
---
# Deep Dive into C/C++ Compilation and Linking · Part 9: Dynamic Library Details (Finale)

## Preface

Next up, we're going to dig into the details of dynamic libraries. To be honest, day-to-day engineering work rarely drags you down to this level, but knowing how dynamic libraries actually work beats not knowing. So here, drawing on *Advanced C and C++ Compiling*, let's revisit some of the finer points of dynamic libraries.

## **8.1 Why Resolving Memory Addresses Is Necessary**

Don't rush ahead just yet — first, a little more assembly.

Clearly enough, the basic model of a modern computer is a Turing machine: we know where the operands live, we fetch them, do the arithmetic, and put them back.

Take x86 as an example. We need to know the address of a memory operand, otherwise we can't shuttle data back and forth between memory and the CPU.

```cpp

mov eax, ds:0xBAD10000 ; load address 0xBAD10000 into eax
add eax, 0x1 ; increment the loaded value
mov ds:0xBAD10000, eax; write it back

```

Great. With that settled, here is the point to make: a function call boils down to the same thing — finding the function's address in the code segment. Say we want to call a plain old `add` function: we have to tell our `call` instruction where `add` is (in other words, supply the code-segment address of `add`'s entry point).

```cpp

add <0x11451400>:
 ... ; Add Procedure

main:
 ... ; Main Procedure
 call 11451400 ; add absolute

```

Of course, sometimes we `call` a relative address instead, which is a bit more convenient.

## Common Problems in Reference Resolution

Let's look at the simplest case! Suppose the executable can only get further work done after loading a single dynamic library. A few things here are self-evident:

- The client binary provides the part of the process memory map whose address range is fixed and can be predetermined
- Only after dynamic loading completes does that part become a valid portion of the process
- The connection only gets wired up naturally when the executable calls one or several feature implementations the dynamic library exposes to the outside (the library's interface, say)

From these basic facts we can tell one thing: the absolute core of the dynamic-library problem is that **the library code's location is indeterminate at runtime**. Whether it's a Windows DLL, a Linux `.so`, or a macOS dylib, they all share one trait: **a dynamic library cannot pin down its final load address at compile time.**

Why? Mainly for these reasons:

#### **(1) Multiple Dynamic Libraries Can Collide on Addresses**

Suppose two `.so` files both want to map into the `0x400000` region of virtual memory — that's a collision.
To avoid it, the OS loader has to go back and pick a different, suitable base address.

#### **(2) ASLR (Address Space Layout Randomization)**

Modern operating systems enable address randomization for security, so a dynamic library lands at a different address every time it loads.
Which means: the compiler and linker cannot assume the dynamic library will run at a fixed address.

#### **(3) The Same Dynamic Library Loads at Different Positions in Different Processes**

Process address spaces are independent of one another, and the library's load position can be completely different in each process.

## Address Translation Is the Solution

#### Case: We Really Do Want to Use the Exported Binary Symbols

Say we really do want to use those exported symbols — the ones the library provides, such as `create_window`, `init_all`, `deinit_all`, and the like. That is what using exported binary symbols looks like, and at that point the client program obviously needs to know right away where the successfully loaded address ended up — not the dynamic library's original symbol address (those are offsets starting from 0!). So the old way, where the linker alone settled all symbol resolution up front, plainly stops working. Pinning down symbol addresses has to be done together with the loader.

#### Case: A Library Calling Its Own Private Symbols

In any case, some private symbols can never be found by the client program. But a thornier problem shows up — what if those symbols are being called *by the exported symbols*? Now what?

## Linker–Loader Collaboration: The Old Technique

Now let's talk through linker–loader collaboration in detail. With all the constraints described above understood, the collaboration between linker and loader can be built on the following rules:

- The linker recognizes the limits of its own symbol resolution.
- The linker tallies the broken symbol references precisely, prepares fix-up hints for them, and embeds those hints into the binary.
- The loader follows the linker's relocation hints exactly, and patches things up after completing the address translation.

### The Linker Recognizes the Limits of Its Own Symbol Resolution

When creating a dynamic library, the linker has to do more than clearly sort out the relationships between the different chunks of code — it also needs to identify, accurately enough, the symbol references that would break if the code segment were loaded at a different address range.

First, unlike an executable, the address range of a dynamic library's memory map starts from zero; when the linker processes an executable, in most cases it does not set the start of the address range to zero. Second, before the load stage, if the linker finds that some symbol's address cannot be resolved, it stops resolving and instead fills the unresolved symbol with a temporary value (usually a blatantly wrong one, such as 0). That does not mean the linker gives up on symbol resolution entirely, though. On the contrary, it only gives up on the symbols it truly cannot handle.

### Next Step: The Linker Precisely Tallies the Broken References and Prepares Fix-up Hints

We can know with certainty which already-resolved references will be invalidated by the loader's address translation: whenever an assembly instruction needs an absolute address, the reference inside it breaks. During the link stage that completes the dynamic library build, the linker can flag the spots where absolute addresses appear and, by some means, let the loader know about them. To support the linker–loader collaboration, the linker reserves a set of hints for the loader — hints pointing out how to repair the errors that address translation causes during dynamic loading. The binary format spec accommodates some new sections dedicated to holding such hints, and a specific, simple syntax was designed on top so the linker can state precisely what action the loader needs to perform.

These sections are called "relocation sections" in the binary, and the `.rel.dyn` section is the oldest relocation section of them all. Generally speaking, the linker writes the relocation hints into the binary so the loader can read them back. The hints specify the addresses the loader must patch once the final memory-map layout of the entire process is settled, and the correct action the loader must perform to fix up the unresolved references properly.

### The Loader Faithfully Follows the Linker's Relocation Hints

The last stage belongs to the loader. The loader reads the dynamic library created by the linker, reads the loader segments inside the library (each segment holds several linker sections), and places all of it into the process memory map, near the original executable's code.

Finally, the loader locates the `.rel.dyn` section, reads the hints the linker left behind, and patches the original dynamic library code according to them. Once the patching is done, the memory map is ready to be used to start the process. Compared with handling the basic tasks, handling dynamic library loading means we have to hand the loader quite a bit more information.

## Implementing the Modern Linker–Loader Collaboration: PLT/GOT

#### The Inner Workings of GOT / PLT

The GOT (Global Offset Table) exists so that code does not depend on a fixed address but instead pulls the final address out of a table. Of course, this plainly requires compiling our code with `-fPIC` (now you see why step one of building a dynamic library is to use PIC, position-independent code!).

Our call now becomes something like `call [GOT + foo]`, so once `foo`'s address is pinned down, the `foo` entry in the GOT gets written with the real address. That way, we've updated it directly.

The PLT, combined with the GOT, implements lazy binding:

- First call to a function → the PLT jumps to the resolver → the GOT gets updated → subsequent calls jump straight to the correct address (no more resolving)

What the PLT buys you:

- Faster program startup
- Symbols resolved only when actually needed

------

## **Lazy Binding, Step by Step**

Put simply, lazy binding means holding off on actually setting the GOT's addresses until the very last moment; until then, all the resolvable symbols get resolved in a polling fashion.

1. `call foo` → jump to `PLT[foo]`
2. `PLT[foo]` calls the resolver `_dl_runtime_resolve`
3. The resolver searches all the dynamic libraries for the symbol `foo`
4. Update `GOT[foo]` = the real address of `foo`
5. Return to `foo`
6. Subsequent calls jump straight to `GOT[foo]`

------

## Duplicate Symbols in Dynamic Linking

In static linking, if two global symbols with the same name show up, the linker usually errors out right away (Multiple Definition Error). In the world of **dynamic linking**, though, the rules are completely different — which is exactly why this deserves its own talk.

#### Duplicate Symbol Definitions

In a large project, we often link several third-party libraries. Suppose your program links `libA.so` and `libB.so`, and by coincidence the developers of both libraries defined a global function `void init()`, or a global variable `int g_config`.

When your main program starts up and loads both libraries, there will be two symbols named `init` sitting in memory.

#### Why This Happens

1. **Common naming**: using overly generic names (like `utils`, `log`, `init`) without `static` to rein in the scope.
2. **Diamond dependency**: the project depends on library A and library B, while A and B each statically link the same base library C (an old version of OpenSSL, say). That leaves one copy of C's symbols inside A and another inside B.
3. **Header-file implementations**: defining a global variable or a non-inline function in a header file that then gets included by multiple `.c/.cpp` files.

------

## Default Handling of Duplicate Symbols

Linux's dynamic linker (`ld-linux`) follows a specific set of rules to handle this kind of conflict, generally known as **symbol interposition**.

#### Rule: First Match Wins

By default, the dynamic linker looks up symbols in **breadth-first (BFS)** order. It walks the global symbol table in order, binds to the **first** matching symbol it finds, and **ignores** every same-named symbol after that.

#### Load Order Decides Everything

What this means is that **link order** or **load order** decides whose code your program actually ends up calling.

Suppose `app` depends on `libA` and `libB`, and both of them define `func()`:

- If the link command is `gcc main.c -lA -lB`: when the main program calls `func()`, it usually binds to `libA`'s version.
- **The dangerous case**: if code inside `libB` calls `func()`, then by ELF's global symbol binding rules, `libB` ends up calling `libA`'s `func()` too! This is known as "symbol hijacking". `libB` believes it is calling its own code, but execution actually lands inside `libA` — which leads to logic errors or even crashes.

> **Use case:** the `LD_PRELOAD` environment variable leans on exactly this mechanism. By preloading a library that carries an implementation of `malloc`, we can override libc's standard `malloc`, which is how memory-leak detection tools (such as Valgrind or jemalloc) get built.

------

## Handling Duplicate Symbols When Linking Dynamic Libraries

Since the default behavior is this dangerous, how do we protect our own symbols from being hijacked — or keep from hijacking anyone else's — when developing a dynamic library?

#### 1. The Linker Flag: `-Bsymbolic`

When building a dynamic library, you can pass the linker flag `-Wl,-Bsymbolic`.

- **What it does:** forces the dynamic library to resolve its global symbol references inside itself first.
- **Effect:** if `libB` was built with this flag, then whenever code inside `libB` calls `func()`, it is guaranteed to call `libB`'s own version, never one overridden by `libA` or the main program.

#### 2. Symbol Visibility

This is the best practice of modern C++ development. With GCC/Clang's `-fvisibility=hidden` flag, all symbols are hidden by default and only the interfaces you actually need get exported.

- **Code example:**

  ```C
  // Only symbols marked DEFAULT get exported to the dynamic symbol table
  __attribute__((visibility("default"))) void public_api();

  // Even a global function like this is invisible from the outside, avoiding conflicts
  void internal_helper();

  ```

#### 3. Scope Control with `dlopen`

If you load libraries manually with `dlopen`, you can specify the `RTLD_LOCAL` flag (which is the default). It keeps the loaded library's symbols **out of** the global symbol table, so they cannot interfere with other libraries.

------

### A Few Classic Cases

#### Custom Memory Allocators

Many high-performance services (Redis, MySQL) link against `jemalloc` or `tcmalloc`.

- **Symptom:** these libraries define the same `malloc`, `free`, `realloc` symbols as glibc.
- **Mechanism:** because they are explicitly linked or preloaded, their symbols sit ahead of glibc's in the global table.
- **Result:** every memory allocation in the entire process — including those from other third-party libraries that depend on glibc — gets automatically forwarded to `jemalloc`. This is a benign, deliberate symbol conflict.

#### C++ STL Version Clashes

This one is the malignant case.

- **Scenario:** the main program is compiled with GCC 4.8 and depends on `libStdOld.so`; a plugin is compiled with GCC 9.0 and depends on `libStdNew.so`.
- **Problem:** the internal implementations of `std::string` or `std::vector` may differ across versions, yet their symbol names (mangled names) may stay identical through partial compatibility — or outright collide.
- **Consequence:** when objects are passed across libraries, the memory layouts differ while the symbols match, so the program can hit undefined behavior — usually showing up as an inexplicable segfault.

------

#### Tip: Linking Provides No Namespace Inheritance of Any Kind

This one is worth repeating! Plenty of people think: "If I put my functions inside `namespace MyLib { ... }` in C++ code, or if I compile my code into `libMyLib.so`, then the library acts like a sealed container, and the variable name `count` inside it will never clash with anything outside."

But in reality, **the linker is "type-blind" and "structure-blind."** We all know **a C++ namespace is just syntactic sugar:** through **name mangling**, the compiler turns `MyLib::foo()` into the string `_ZN5MyLib3fooEv`. As far as the linker is concerned, that's just a long string. If two libraries happen to produce the same mangled name, the collision happens anyway. And **a dynamic library is not a namespace:** a dynamic library is merely a form of file organization. The moment it gets loaded into process memory, every exported symbol dumps into one flat, unlayered global symbol pool (the Global Symbol Table). The global variable `g_context` in `libA.so` and the `g_context` in `libB.so` are the exact same thing in the linker's eyes — unless you have applied visibility hiding or local binding.

## The Modern CMake Perspective

All those flags above — `-fPIC`, `-fvisibility=hidden`, `-Wl,-Bsymbolic`, `$ORIGIN`, and friends — you basically never type by hand anymore; CMake has wrapped them up in a few lines of `add_library` / `set_target_properties`.

`add_library(foo SHARED)` does two things for you: it automatically adds `-fPIC` to every `.o` in the library (SHARED turns it on by default), then bundles them into a `.so` with `gcc -shared` — in effect rerunning the PIC flow we walked through earlier, automatically. Symbol visibility is handed over to `CMAKE_CXX_VISIBILITY_PRESET hidden` and `CMAKE_VISIBILITY_INLINES_HIDDEN`: once set, every symbol is hidden by default, and only the interfaces you explicitly tag with `__attribute__((visibility("default")))` make it into the dynamic symbol table — exactly matching the "symbol visibility" best practice from the previous section. `target_link_libraries` takes over `-l`/`-L`, and dependency relationships propagate automatically through CMake (the three tiers: PUBLIC/PRIVATE/INTERFACE); just leaning on that lets you sidestep a good half of the duplicate-symbol pain that transitive dependencies bring.

The two remaining runtime pitfalls have proper homes of their own too. That whole "install it, then remember to `export` `LD_LIBRARY_PATH`" ritual — these days you pair `CMAKE_INSTALL_RPATH` with `$ORIGIN` so the executable itself remembers where the `.so` lives, and it can find the library no matter what relative path you deploy it to. And a need like `-Wl,-Bsymbolic` — "I want my library's internal symbols to resolve against themselves" — hooks up just the same through `target_link_options(foo PRIVATE "-Wl,-Bsymbolic")`. In other words, the underlying linker–loader collaboration has not changed one bit; it is just that what you write today is no longer `gcc -shared -fPIC -Wl,-Bsymbolic -o libfoo.so ...` but `add_library(foo SHARED)` plus a few `set_target_properties`, and CMake does the dirty work for you.
