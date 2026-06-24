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
title: 'In-depth Understanding of C/C++ Compilation and Linking 4: Dynamic Libraries
  A1: Basic Discussion on `-fPIC`'
description: ''
translation:
  source: documents/compilation/04-dynamic-libraries-1.md
  source_hash: b035c5b652786dbf2edbb5e094d0cc2100f2250e17c9a5b34394b4f20092feaa
  translated_at: '2026-06-24T00:24:41.582619+00:00'
  engine: anthropic
  token_count: 481
---
# Deep Dive into C/C++ Compilation and Linking: Part 4 - Dynamic Libraries A1: Basic Discussion on `-fPIC`

## Preface

I have been quite exhausted lately, juggling a pile of tasks while preparing to start work. I finally found a moment to catch my breath and continue updating this series.

This article focuses on the basics of dynamic libraries. Specifically, we will discuss how to create dynamic libraries (primarily on Linux; using the MSVC toolchain at the command line on Windows is rather tedious, and mature build systems already handle the details there, so I won't go into detail on building dynamic libraries on Windows), as well as issues related to symbol name mangling.

## How to Create Dynamic Libraries on Linux

Creating a dynamic library isn't difficult, but it generally requires following these steps:

- The integrated binary relocatable files must be compiled with the Position Independent Code flag (`-fPIC`).
- Link these PIC relocatable files and pass the `-shared` flag.

## Let's Talk About `-fPIC`

This option is quite interesting. Of course, there isn't much to say about the `-shared` option; it simply tells the compiler/linker to produce a dynamic library. However, why must these relocatable files be compiled as position-independent code?

In the book *Advanced C/C++ Compilation*, three progressive questions are raised:

- What is `-fPIC`?
- Is `-fPIC` mandatory for creating dynamic libraries (`.so`)?
- Is `-fPIC` used only when compiling dynamic libraries?

Below, I have summarized the book's arguments, combined with my own perspectives.

#### What is `-fPIC`?

`-fPIC` stands for **Position-Independent Code**. In other words, the generated machine instructions **do not rely on a fixed load address**. They can be loaded into any memory location at runtime without modifying the code itself. This aligns perfectly with our understanding of dynamic libraries. Ultimately, we export symbols from a dynamic library for use by third-party applications or other libraries. Therefore, we cannot assign a fixed mapping address to these dynamic library symbols. Instead, at load time, we dynamically assign an offset address mapped to the user's process address space to achieve symbol reuse. To break it down:

- `-fPIC` causes symbols to use **relative addresses** rather than absolute addresses for mapping.
- Global variables are accessed indirectly via a **GOT (Global Offset Table)**.
- Function calls are made through jumps via a **PLT (Procedure Linkage Table)**.

------

#### **Is `-fPIC` mandatory for creating dynamic libraries (.so)?**

Strictly speaking, not necessarily. Of course, if we consider that 32-bit PCs are virtually extinct today (forgive my ignorance; I haven't seen a physical 32-bit PC in years, though I have dabbled a bit with MCUs), one might hold an affirmative attitude toward the proposition above.

Let's think about this: modern dynamic libraries and shared libraries are synonymous, with multiple processes sharing the code segment of the dynamic library. It is perfectly reasonable for the code to reside at any virtual address for different processes. Otherwise, the loader would have to perform **relocation patching** on the code during loading, preventing the code segment from being shared and slowing down the loading process.

However, on x86-64, it is still possible to compile usable dynamic libraries without `-fPIC`. However, you lose the benefits of sharing, and loading becomes slower (since addresses for all symbols must be fixed at load time). Therefore, if we think seriously about it, my conclusion is:

> **Today, compiling dynamic libraries must include the `-fPIC` flag. The benefits far outweigh the drawbacks (unless you are worried about negligible performance overhead, which implies a different scenario).**

#### Is `-fPIC` exclusive to dynamic libraries? Can we use `-fPIC` with static libraries?

Obviously not; otherwise, there would be no need to make this flag independent. In fact, we can absolutely apply `-fPIC` to relocatable files intended to be compiled into static libraries. This is very common.

For example, I have a large project on hand that generates a static library for each sub-module, and then packages all generated static libraries in a directory into a single dynamic library. As we discussed in previous articles, a static library is simply a collection of relocatable files. Therefore, it naturally follows that in the scenario described above, we must compile the source files contained in these static libraries with the `-fPIC` flag.
