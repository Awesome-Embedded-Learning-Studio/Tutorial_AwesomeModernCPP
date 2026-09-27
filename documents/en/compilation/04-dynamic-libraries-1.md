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
title: 'Deep Dive into C/C++ Compilation and Linking · Part 4: Dynamic Libraries A1 — The Basics of `-fPIC`'
description: 'Why dynamic libraries must be compiled with -fPIC: GOT/PLT indirection is what makes the code segment shareable, plus the real engineering reason static libraries sometimes need -fPIC too'
cpp_standard: [11, 14, 17, 20]
translation:
  source: documents/compilation/04-dynamic-libraries-1.md
  source_hash: c24033bb187e00c4309d829a66ea2f622c0cf439f1f03e8d2933c3438a19e979
  translated_at: '2026-09-25T23:44:53+00:00'
  engine: anthropic
  token_count: 2600
---
# Deep Dive into C/C++ Compilation and Linking · Part 4: Dynamic Libraries A1 — The Basics of `-fPIC`

## Preface

It's been a pretty exhausting stretch lately — juggling a pile of things at once and getting ready to start a new job — but these past few days we finally got a small breather, so let's pick this series back up.

This piece covers the fundamentals of dynamic libraries. In particular, we'll look at how to actually build one (with the focus on Linux; on Windows, the MSVC toolchain is honestly a bit punishing to drive from the command line, and plenty of mature build systems have already papered over the basic details, so we won't walk through building dynamic libraries on Windows in depth here), plus a few questions around symbol decoration and signatures.

## How to Create a Dynamic Library on Linux

Creating a dynamic library is not much trouble, but you do basically have to guarantee a couple of steps:

- The binary relocatable files that get folded into it must be compiled with the position-independent flag (`-fPIC`, the flag for Position Independent Code)
- Combine those PIC relocatable files, then pass the `-shared` flag

## Let's Talk About `-fPIC`

This option is an interesting one. The `-shared` option, by contrast, has nothing much worth saying about it — it plainly tells our compiler to link a dynamic library, full stop. But why do those relocatable files have to be compiled as position-independent code?

In *Advanced C and C++ Compiling*, three questions are raised, each digging one layer deeper than the last:

- What is `-fPIC`?
- Do you have to use `-fPIC` to create a dynamic library (`.so`)?
- Is `-fPIC` used only when compiling dynamic libraries?

Below, we've sorted out the book's line of reasoning, mixed in a bit of our own take, and laid it out.

#### What Is `-fPIC`

`-fPIC` stands for `Position-Independent Code` (generating position-independent code). In other words, the compiled machine instructions **do not depend on any fixed load address** and can be loaded into an arbitrary memory location at runtime without modifying the code itself. That lines up neatly with how we intuit what dynamic libraries are for. In the end, we always want to export a dynamic library's symbols so that third-party applications or other libraries can use them — so clearly we can't arrange a fixed absolute mapping address for those symbols. Instead, when the code is reused, an offset address is handed out dynamically and mapped onto the user's process address space; that is what makes symbol reuse possible. Step by step:

- `-fPIC` maps symbols by **relative address** instead of absolute address
- Global variables are accessed indirectly through the **GOT (Global Offset Table)**
- Function calls go through **PLT (Procedure Linkage Table)** jumps

------

#### **Is `-fPIC` Mandatory for Building a Dynamic Library (.so)**

To answer in full seriousness: honestly, not necessarily. Of course, if we're talking about a world where 32-bit PCs are already on the verge of extinction (forgive our limited horizons — we have genuinely never laid eyes on a physical 32-bit PC, though we have tinkered a tiny bit with microcontrollers), then we might as well take that proposition as true.

Think about it: nowadays "dynamic library" and "shared library" are synonymous — multiple processes are expected to share the dynamic library's code segment. For different processes, requiring that the code be placeable at any virtual address is perfectly reasonable. Otherwise the loader has to perform **relocation patching** on the code at load time, which means the code segment can no longer be shared and loading gets slower.

But x86-64 is not like that — you can still build a usable dynamic library without `-fPIC`. It's just that you lose the sharing property, and loading gets slower (all symbol addresses have to be fixed up at load time). So if we think about it seriously, our conclusion is:

> **Today, compiling a dynamic library absolutely must carry the `-fPIC` flag — all upside, no downside. (If you are genuinely worried about the slight performance loss, pretend we never said that — we are weighing different scenarios.)**

#### Is `-fPIC` Exclusive to Dynamic Libraries? What About Using It for Static Libraries

Clearly not — otherwise there would have been no reason to make this a separate flag at all. In practice, we can perfectly well apply `-fPIC` to the relocatable files that are going to become a static library, and this is very common.

For example, we have a fairly large project on hand where every submodule is built into a static library, and then all the static libraries generated in that directory are bundled into one dynamic library. We discussed this in an earlier article: a static library is just a plain collection of relocatable files. So it comes naturally to realize that in the situation above, we must compile those source files with the `-fPIC` flag, for the relocatable files the static library contains.

## The Modern CMake Perspective

This whole manual "`-fPIC` + `-shared`" routine has basically been taken over by CMake today. `add_library(foo SHARED foo.cpp)` automatically feeds `-fPIC` to the compiler and `-shared` to the linker on Linux — no manual fiddling required. The more general lever is `set(CMAKE_POSITION_INDEPENDENT_CODE ON)` or `set_target_properties(foo PROPERTIES POSITION_INDEPENDENT_CODE ON)` — and this one applies to static libraries too, which is exactly the real-world scenario from above ("static libraries folded into a dynamic library"): turn PIC on for the static library targets as well, then `target_link_libraries(big_so PRIVATE foo)`, and CMake makes sure the `.o` files the downstream dynamic library receives are already position-independent. As for the GOT/PLT indirection details themselves, CMake will not magic them away for you — it just hands the right flags to the compiler on time; the underlying ELF machinery is still exactly what this article has been describing.
