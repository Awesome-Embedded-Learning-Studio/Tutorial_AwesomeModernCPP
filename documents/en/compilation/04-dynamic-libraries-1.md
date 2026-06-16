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
title: 'Deep Dive into C/C++ Compilation and Linking: Part 4 — Dynamic Libraries A1:
  Basics of `-fPIC`'
description: ''
translation:
  source: documents/compilation/04-dynamic-libraries-1.md
  source_hash: 3c6ec9fab93bb3300298643b5d47ddce2ac3368ce66145d71e4e3ac9f35c7c59
  translated_at: '2026-06-16T03:26:37.276305+00:00'
  engine: anthropic
  token_count: 482
---
# In-Depth Understanding of C/C++ Compilation and Linking Techniques 4: Dynamic Libraries A1: Basic Discussion

## Preface

I have been quite tired lately, busy with a pile of things and preparing to start a new job. I can finally take a short break here and continue updating this series of blog posts.

This article mainly discusses the basics of dynamic libraries. Specifically, it will cover how to create dynamic libraries (focusing on Linux; on Windows, using the MSVC toolchain at the command line is quite torturous, and since many mature build systems already cover the basic details, I will not detail how to build dynamic libraries on Windows here), as well as some issues regarding symbol name decoration.

## How to Create a Dynamic Library on Linux

Creating a dynamic library is not difficult, but it generally requires following these steps:

- The integrated binary relocatable files must be compiled with the Position Independent Code flag (`-fPIC`).
- Integrate these PIC binary relocatable files, then pass the `-shared` flag.

## Let's Talk About `-fPIC`

This option is quite interesting. Of course, there is nothing much to say about the `-shared` option; it simply tells our compiler to link a dynamic library. But why do these relocatable files need to be compiled with Position Independent Code?

In *Advanced C/C++ Compilation Techniques*, three progressive questions are raised:

- What is `-fPIC`?
- Is `-fPIC` mandatory for creating a dynamic library (`.so`)?
- Is `-fPIC` used only when compiling dynamic libraries?

Below, I have summarized the book's arguments, combined with some of my own views, and presented them.

#### What is `-fPIC`?

The meaning of `-fPIC` is **generate Position Independent Code**. In other words, the generated machine instructions **do not rely on a fixed load address**. At runtime, they can be loaded to any memory location without modifying the code itself. This aligns perfectly with our understanding of dynamic library functionality. Ultimately, we need to export symbols from a dynamic library for use by third-party applications or other libraries. Therefore, we obviously cannot assign an absolute mapping address to these dynamic library symbols. Instead, during reuse, we dynamically assign an offset address mapped to the user's process address space, thus enabling symbol reuse. To put it step-by-step:

- `-fPIC` will map symbols using **relative addresses** rather than absolute addresses.
- Global variables are accessed indirectly via the **GOT (Global Offset Table)**.
- Function calls are made through jumps via the **PLT (Procedure Linkage Table)**.

------

#### **Is `-fPIC` mandatory for creating a dynamic library (`.so`)?**

Strictly speaking, not necessarily. Of course, if we say that 32-bit PCs are already extinct (forgive my ignorance; I have never seen a physical 32-bit PC computer, though I have played a bit with microcontrollers), then we might hold a positive attitude towards the above proposition.

Let's think about it: modern dynamic libraries and shared libraries are synonymous, where multiple processes prepare to share the code segment of the dynamic library. For different processes, it is entirely reasonable to require that the code be placed at any virtual address. Otherwise, the loader must perform **relocation patching** on the code during loading, preventing the code segment from being shared and slowing down the loading speed.

However, on x86-64, it is still possible to compile usable dynamic libraries without `-fPIC`, but the sharing characteristic is lost, and the loading speed becomes slower (correcting addresses for all symbols during loading). So, if we think seriously about it, my conclusion is:

> **Today, compiling dynamic libraries must carry the `-fPIC` flag; it does more good than harm (if you are worried about slight performance loss, just pretend I didn't say that; the scenarios considered are different).**

#### Is `-fPIC` exclusive to dynamic libraries? Can `-fPIC` be used with static libraries?

Obviously not; otherwise, there would be no need to make this flag independent. In fact, we can absolutely apply `-fPIC` to relocatable files intended to be compiled into static libraries. This is very common.

For example, I have a large project on hand that generates a static library for each sub-module, and then packages all the generated static libraries in that directory into one dynamic library. As we discussed in previous articles, a static library is simply a collection of relocatable files. Therefore, it is natural for us to realize that in the situation described above, we must compile the source files contained in these static libraries with the `-fPIC` flag.
