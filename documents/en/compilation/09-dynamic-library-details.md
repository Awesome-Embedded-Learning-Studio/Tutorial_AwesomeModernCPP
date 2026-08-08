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
title: "Deep Dive into C/C++ Compilation and Linking, Part 9: Dynamic Library Details (Finale)"
description: 'From PIC, GOT/PLT, to symbol interposition, properly explaining why dynamic libraries have "indeterminate addresses" at runtime and how the modern linker-loader collaboration actually works'
cpp_standard: [11, 14, 17, 20]
---
# Deep Dive into C/C++ Compilation and Linking, Part 9: Dynamic Library Details (Finale)

## Foreword

Next up, we're going to talk through the details of dynamic libraries. Honestly, day-to-day engineering work rarely drags you into this stuff, but knowing how dynamic libraries actually tick is better than not knowing. So here I'm leaning on *Advanced C and C++ Compiling* to walk through some of the finer points of dynamic libraries one more time.

## **8.1 Why Resolving Memory Addresses Is Necessary**

Don't rush ahead just yet, let me throw in a bit more assembly.

The basic model of a modern computer is, obviously enough, a Turing machine. We know where the operands live, we fetch them, do the math, and put them back.

Take x86 as the example. We need to know the address of a memory operand, otherwise we can't move data back and forth between memory and the CPU.

```cpp

mov eax, ds:0xBAD10000 ; load address 0xBAD10000 into eax
add eax, 0x1 ; increment the loaded value
mov ds:0xBAD10000, eax; write it back

```

Great. With that out of the way, here's the point I want to make: a function call boils down to the same thing — finding the function's address in the code segment. Say we want to call a plain old `add` function, we have to tell our `call` instruction where `add` is (in other words, hand it the code-segment address of `add`'s entry point).

```cpp

add <0x11451400>:
 ... ; Add Procedure

main:
 ... ; Main Procedure
 call 11451400 ; add absolute

```

Of course, sometimes we `call` a relative address instead, which is a bit more convenient.

## Common Problems in Reference Resolution

Let's look at the simplest case. Say the executable can only do real work after loading a single dynamic library. The following things are pretty self-evident:

- The client binary provides a fixed, pre-determinable address range in the process memory map
- Only after dynamic loading is finished does that range become a valid part of the process
- Only when the executable calls one or several feature implementations exposed by the dynamic library (its interface, say) does the connection get wired up naturally

From the above, one thing becomes clear: the heart of the dynamic-library problem is that **the library code's location is indeterminate at runtime**. Whether it's a Windows DLL, a Linux `.so`, or a macOS dylib, they all share one trait: **a dynamic library cannot fix its final load address at compile time.**

Why? Mostly for these reasons:

#### **(1) Multiple dynamic libraries can collide on addresses**

Suppose two `.so` files both want to map into the `0x400000` region of virtual memory. That's a collision.

To avoid it, the OS loader has to pick a fresh, suitable base address.

#### **(2) ASLR (Address Space Layout Randomization)**

Modern OSes turn on address randomization for security, so a dynamic library lands at a different address every time it loads.

That means: the compiler and linker cannot assume the dynamic library will run at a fixed address.

#### **(3) The same dynamic library loads at different positions in different processes**

Process address spaces are independent, and the library's load position can be completely different in each one.

## Translating Addresses Is the Solution

#### Case: We really do want to use the exported binary symbols

Say we genuinely want to use those exported symbols, the ones the library hands us — `create_window`, `init_all`, `deinit_all`, that kind of interface. This is using exported binary symbols, and clearly the client program needs to know right away where the successfully loaded address is, not the dynamic library's original symbol address (those are offset from 0!), so the old approach of letting the linker resolve everything upfront obviously doesn't cut it anymore. Pinning down the symbol address has to be a joint effort with the loader.

#### Case: Calling your own private symbols

Either way, some private symbols can't be found by the client program at all. But there's a thornier problem — what if those symbols are being called by the *exported* symbols? Now what?

## Linker–Loader Collaboration: The Old Technique

Now let's talk carefully about linker–loader collaboration. Once we understand all the constraints above, we can frame the collaboration between linker and loader with these rules:

- The linker recognizes the limits of its own symbol resolution.
- The linker tallies up the references that will break, prepares relocation hints, and embeds those hints in the binary.
- The loader faithfully follows the linker's relocation hints and patches things up after completing the address translation.

### The Linker Recognizes the Limits of Its Own Symbol Resolution

When building a dynamic library, the linker has to do more than clearly sort out the relationships between different chunks of code — it also has to identify, accurately, which symbol references would break if the code segment were loaded at a different address range.

First, unlike an executable, a dynamic library's memory map starts from zero. When the linker processes an executable, in most cases it does not set the start of the address range to zero. Second, before the load stage, if the linker finds it can't resolve some symbol's address, it stops trying to resolve it and instead fills the unresolved symbol with a placeholder (usually a blatantly wrong value like 0). But that doesn't mean the linker gives up on symbol resolution entirely. It only gives up on the symbols it genuinely can't handle.

### Next Step: The Linker Tallies the Broken References and Prepares Fix-up Hints

We can fully tell which resolved references will be invalidated by the loader's address translation. As long as an assembly instruction needs an absolute address, the reference inside it will break. During the link stage that finishes building the dynamic library, the linker can flag the spots where absolute addresses appear and, through some mechanism, let the loader know about them. To support this linker–loader collaboration, the linker reserves a set of hints for the loader, pointing out how to fix the errors caused by address translation during dynamic loading. The binary format spec accommodates this with new sections dedicated to holding such hints. There's also a specific, simple syntax designed so the linker can state precisely what action the loader needs to perform.

These sections are called "relocation sections" in the binary, and `.rel.dyn` is the oldest of them. Generally, the linker writes the relocation hints into the binary so the loader can read them. The hints specify the addresses the loader needs to patch — once the final memory-map layout of the entire process is settled — and the correct action the loader must take to properly fix up the unresolved references.

### The Loader Faithfully Follows the Linker's Relocation Hints

The last stage belongs to the loader. The loader reads the dynamic library produced by the linker, reads the loader segments inside the library (each segment holds several linker sections), and places all of it into the process memory map, near the original executable's code.

Finally, the loader locates the `.rel.dyn` section, reads the hints the linker left behind, and patches the original dynamic library code according to those hints. Once patching is done, the memory map is ready to be used to start the process. Compared to the basic tasks, when it comes to dynamic library loading we have to feed the loader a lot more information.

## Modern Linker–Loader Collaboration: PLT/GOT

#### The Inner Workings of GOT / PLT

The GOT (Global Offset Table) exists so code doesn't depend on a fixed address, but instead pulls the final address out of a table. Of course, this obviously requires us to compile our code with `-fPIC` (do you now get why step one of building a dynamic library is to use PIC, position-independent code?).

Now our call turns into something like `call [GOT + foo]`, so once `foo`'s address is pinned down, the `foo` entry in the GOT gets overwritten with the real address. That way we've updated it directly.

PLT, combined with GOT, implements lazy binding:

- First call to a function → PLT jumps to the resolver → updates the GOT → next time jumps straight to the correct address (no more resolution)

Benefits of PLT:

- Speeds up program startup
- Resolves symbols only when they're actually needed

------

## **Lazy Binding, Step by Step**

Put simply, lazy binding means we hold off on really setting the GOT entry until the very last moment, and until then we keep polling to resolve the symbols.

1. `call foo` → jump to `PLT[foo]`
2. `PLT[foo]` calls the resolver `_dl_runtime_resolve`
3. The resolver hunts for the symbol `foo` across all the dynamic libraries
4. Update `GOT[foo]` = the real address of `foo`
5. Return to `foo`
6. Subsequent calls jump straight to `GOT[foo]`

------

## Duplicate Symbols in Dynamic Linking

In static linking, if two global symbols share the same name, the linker usually just bails with an error (Multiple Definition Error). But in the world of **dynamic linking**, the rules are completely different. That's why this deserves its own section.

#### Duplicate Symbol Definitions

In a large project we frequently link against several third-party libraries. Suppose your program links `libA.so` and `libB.so`, and by coincidence both libraries' authors defined a global function `void init()` or a global variable `int g_config`.

When your main program starts up and loads both libraries, there will be two symbols named `init` sitting in memory.

#### Why does this happen?

1. **Common names**: using overly generic names (like `utils`, `log`, `init`) without `static` to limit the scope.
2. **Diamond dependency**: the project depends on library A and library B, and A and B each statically link the same base library C (an older OpenSSL, say). That leaves C's symbols with one copy inside A and another inside B.
3. **Header-file implementations**: defining a global variable or a non-inline function in a header file that then gets included by multiple `.c/.cpp` files.

------

## Default Handling of Duplicate Symbols

Linux's dynamic linker (`ld-linux`) follows a specific set of rules to handle this kind of conflict, generally known as **symbol interposition**.

#### Rule: First Match Wins

By default, the dynamic linker searches for symbols in **breadth-first (BFS)** order. It walks the global symbol table in order, binds to the **first** matching symbol it finds, and **ignores** every same-named symbol after that.

#### Load order decides everything

What this means is that **link order** or **load order** decides whose code your program actually calls.

Suppose `app` depends on `libA` and `libB`, and both define `func()`:

- If your link command is `gcc main.c -lA -lB`: when the main program calls `func()`, it usually binds to `libA`'s version.
- **The dangerous case**: if code inside `libB` calls `func()`, by ELF's global symbol binding rules `libB` will also end up calling `libA`'s `func()`! This is called "symbol hijacking." `libB` thinks it's calling its own code but actually jumps into `libA`, which causes logic errors or even crashes.

> **Use case:** the `LD_PRELOAD` environment variable leans on exactly this mechanism. By preloading a library that implements `malloc`, we can override libc's standard `malloc`, which is how memory-leak detection tools (Valgrind, jemalloc) get built.

------

## Handling Duplicate Symbols When Linking Dynamic Libraries

Since the default behavior is this dangerous, how do we protect our own symbols from being hijacked (or avoid hijacking someone else's) when developing a dynamic library?

#### 1. The linker flag: `-Bsymbolic`

When compiling a dynamic library, you can pass the linker flag `-Wl,-Bsymbolic`.

- **What it does:** forces the dynamic library to resolve its own global symbol references internally first.
- **Effect:** if `libB` was compiled with this flag, then when code inside `libB` calls `func()`, it is guaranteed to call `libB`'s own version, never the one overridden by `libA` or the main program.

#### 2. Symbol Visibility

This is the modern C++ best practice. With GCC/Clang's `-fvisibility=hidden` flag, you hide all symbols by default and only export the interfaces you actually need.

- **Code example:**

  ```C
  // Only symbols marked DEFAULT get exported to the dynamic symbol table
  __attribute__((visibility("default"))) void public_api();

  // Even though this is a global function, it's invisible from the outside, avoiding conflicts
  void internal_helper();

  ```

#### 3. Scope Control with `dlopen`

If you load libraries manually with `dlopen`, you can pass the `RTLD_LOCAL` flag (which is the default). That keeps the loaded library's symbols **out of** the global symbol table, so it can't interfere with other libraries.

------

### A Few Classic Cases

#### Custom Memory Allocators

A lot of high-performance services (Redis, MySQL) link against `jemalloc` or `tcmalloc`.

- **Symptom:** these libraries define the same `malloc`, `free`, `realloc` symbols as glibc.
- **Mechanism:** since they're explicitly linked or preloaded, their symbols sit ahead of glibc's in the global table.
- **Result:** every memory allocation in the entire process — including third-party libraries that depend on glibc — automatically gets routed to `jemalloc`. This is a benign, intentional symbol conflict.

#### C++ STL Version Clashes

This one is the malignant case.

- **Scenario:** the main program is compiled with GCC 4.8 and depends on `libStdOld.so`; a plugin is compiled with GCC 9.0 and depends on `libStdNew.so`.
- **Problem:** the internal implementation of `std::string` or `std::vector` may differ between versions, but their symbol names (mangled names) may stay consistent through partial compatibility, or outright collide.
- **Consequence:** when objects get passed across libraries, the memory layout differs but the symbol is the same, so the program can hit undefined behavior — usually showing up as a baffling segfault.

------

#### Tip: No Namespace Inheritance in Linking

This one is worth repeating! A lot of people think: "I put my function inside `namespace MyLib { ... }` in C++ code, or I compiled my code into `libMyLib.so`, so now the library acts like an isolated container, and the variable name `count` inside it won't clash with anything outside."

But in reality **the linker is "type-blind" and "structure-blind."** We all know **a C++ namespace is just syntactic sugar:** the compiler turns `MyLib::foo()` into the string `_ZN5MyLib3fooEv` through **name mangling**. To the linker, that's just a long string. If two libraries happen to generate the same mangled name, the collision still happens. And **a dynamic library is not a namespace:** a dynamic library is just a way of organizing files. The moment it gets loaded into a process's memory, every exported symbol dumps into one flat, global symbol pool (the Global Symbol Table). The global variable `g_context` in `libA.so` and `g_context` in `libB.so` are the exact same thing in the linker's eyes — unless you've hidden them with visibility or bound them as local.

## Through a Modern CMake Lens

All those flags — `-fPIC`, `-fvisibility=hidden`, `-Wl,-Bsymbolic`, `$ORIGIN` and friends — you basically never type by hand anymore. CMake has packed them into a few lines of `add_library` / `set_target_properties`.

`add_library(foo SHARED)` basically does two things for you: it automatically adds `-fPIC` to every `.o` inside the library (SHARED turns it on by default), then uses `gcc -shared` to bundle them into a `.so`, which amounts to automatically running through the PIC flow we just talked about. Symbol visibility is handed off to `CMAKE_CXX_VISIBILITY_PRESET hidden` and `CMAKE_VISIBILITY_INLINES_HIDDEN`: once you set those, every symbol is hidden by default, and only the interfaces you explicitly tag with `__attribute__((visibility("default")))` make it into the dynamic symbol table — exactly matching the "symbol visibility" best practice from the previous section. `target_link_libraries` takes over `-l`/`-L`, and dependency relationships get propagated by CMake automatically (the three tiers PUBLIC/PRIVATE/INTERFACE); a good chunk of the duplicate-symbol pain from transitive dependencies gets sidestepped just by leaning on that.

The two remaining runtime pitfalls also have proper homes. That whole "you have to `export` after installing" dance with `LD_LIBRARY_PATH` — nowadays you pair `CMAKE_INSTALL_RPATH` with `$ORIGIN` so the executable remembers itself where the `.so` lives, and it'll find the library no matter what relative path you deploy it to. And a need like `-Wl,-Bsymbolic` for "I want my library to resolve its internal symbols against itself" can be hooked up just the same through `target_link_options(foo PRIVATE "-Wl,-Bsymbolic")`. In other words, the underlying linker–loader mechanism hasn't changed, but what you write today isn't `gcc -shared -fPIC -Wl,-Bsymbolic -o libfoo.so ...`, it's `add_library(foo SHARED)` plus a couple of `set_target_properties`, and CMake takes care of the dirty work for you.
