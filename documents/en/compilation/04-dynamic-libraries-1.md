---
chapter: 13
difficulty: intermediate
order: 4
platform: host
reading_time_minutes: 4
tags:
- cpp-modern
- host
- intermediate
title: 'Deep Dive into C/C++ Compilation and Linking, Part 4: Dynamic Libraries A1, the Basic Discussion around `-fPIC`'
description: 'Get straight on why dynamic libraries must be compiled with -fPIC: GOT/PLT indirection is what lets the code segment be shared, plus the real engineering reason a static library sometimes has to carry -fPIC too.'
cpp_standard: [11, 14, 17, 20]
---
# Deep Dive into C/C++ Compilation and Linking, Part 4: Dynamic Libraries A1, the Basic Discussion around `-fPIC`

## Preface

Things have been pretty tiring lately, juggling a pile of stuff and getting ready to start a new job, so these past few days I finally got a little breather and picked this blog series back up.

This piece mostly covers the basics of dynamic libraries. In particular, how you actually build one (focused on Linux; on Windows the MSVC toolchain is honestly a bit punishing from the command line, and a lot of mature build systems have already papered over the basic details, so I won't go into building dynamic libraries on Windows in depth here), along with a few questions around symbol decoration and mangling.

## How to Create a Dynamic Library on Linux

Creating a dynamic library is not that hard, but you basically have to follow a couple of steps:

- The relocatable object files that go into it have to be compiled with the position-independent flag (`-fPIC`, i.e. flags Position Independent Code).
- Gather those PIC relocatable object files together, and pass the `-shared` flag at link time.

## Let's Talk About `-fPIC`

This option is interesting. The `-shared` option has nothing much to say about it, it just plainly tells the compiler/linker to link a dynamic library. But why do those relocatable files have to be compiled as position-independent code?

In *Advanced C/C++ Compiling Techniques*, three progressively deeper questions are raised:

- What is `-fPIC`?
- Do you have to use `-fPIC` to build a dynamic library (`.so`)?
- Is `-fPIC` only ever used when building dynamic libraries?

Below, I'll lay out the book's reasoning, mixed with a bit of my own take.

#### What is `-fPIC`?

`-fPIC` stands for `Position-Independent Code` (generating position-independent code). In other words, the machine instructions that come out **do not depend on a fixed load address**, and at runtime they can be loaded into any memory location without the code itself having to be patched. That lines up nicely with how we intuit a dynamic library to work. In the end, we always want a dynamic library to export its symbols for other third-party applications or libraries to use, so obviously we cannot pin an absolute mapping address onto those dynamic library symbols ahead of time. Instead, when it gets reused, a relative offset is handed out dynamically and mapped into the consumer process's address space, which is what makes symbol reuse possible in the first place. Step by step:

- `-fPIC` makes the compiler map symbols through **relative addresses** rather than absolute ones.
- Global variables are accessed indirectly through the **GOT (Global Offset Table)**.
- Function calls jump through the **PLT (Procedure Linkage Table)**.

------

#### **Do you have to use `-fPIC` to build a dynamic library (`.so`)?**

Honestly, and said very seriously, not necessarily. Of course, if we are talking about today, where 32-bit PCs are basically on their way out (forgive my ignorance, I have genuinely never seen a physical 32-bit PC, though I have fiddled with MCUs a tiny bit), then we can probably affirm the proposition above.

Let's think about it. In modern terms "dynamic library" and "shared library" are synonyms: several processes want to share a dynamic library's code segment. For different processes, requiring that the code be droppable at any virtual address is perfectly reasonable. Otherwise the loader has to do **relocation patching** on the code at load time, which means the code segment can no longer be shared and loading gets slower.

But x86-64 is not like that, you can still build a working dynamic library without `-fPIC`. It's just that you lose the sharing property, and loading gets slower (every symbol's address has to be fixed up at load time). So thinking about it seriously, my conclusion is:

> **Today, compiling a dynamic library must carry the `-fPIC` flag, it does nothing but good. (If you are really worried about that tiny performance hit, pretend I said nothing, you are optimizing for a different scenario.)**

#### Is `-fPIC` exclusive to dynamic libraries? Can a static library use `-fPIC`?

Obviously not, otherwise there would be no reason to break this flag out on its own. In practice, we can absolutely also slap `-fPIC` onto relocatable files that are destined to be a static library, and this is very common.

For example, I have a fairly large project on hand where each submodule first builds a static library, and then all the static libraries generated under that directory get packaged into a single dynamic library. We discussed earlier that a static library is just a simple collection of relocatable files, so naturally we realize that in this situation we **must** compile the source files for the relocatable files inside that static library with the `-fPIC` flag.

## A Modern CMake Perspective

The whole "manually `-fPIC` plus `-shared`" flow above is basically taken over by CMake today. `add_library(foo SHARED foo.cpp)` on Linux automatically feeds `-fPIC` to the compiler and `-shared` to the linker, no manual fiddling needed. More general is `set(CMAKE_POSITION_INDEPENDENT_CODE ON)` or `set_target_properties(foo PROPERTIES POSITION_INDEPENDENT_CODE ON)`, and this one also applies to static libraries, which lines up exactly with the "static library gets packed into a dynamic library" scenario I mentioned above: turn PIC on for the static library target too, then `target_link_libraries(big_so PRIVATE foo)`, and CMake makes sure the `.o` the downstream dynamic library receives is already position-independent. As for the GOT/PLT indirection details, CMake does not paper those over for you, it just hands the right flag to the compiler on time. The underlying ELF mechanics are still exactly what this piece has been talking about.
