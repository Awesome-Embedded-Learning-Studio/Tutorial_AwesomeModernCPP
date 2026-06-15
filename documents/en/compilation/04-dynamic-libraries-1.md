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
title: 'In-Depth Understanding of C/C++ Compilation and Linking Part 4: Shared Library
  A1: Basic Discussion of `-fPIC`'
translation:
  engine: anthropic
  source: documents/compilation/04-dynamic-libraries-1.md
  source_hash: ea133fb871fb203dc57822edf6c9d9bc1fe1c6a35f84e6a9705272f4ba437436
  token_count: 476
  translated_at: '2026-05-26T10:10:22.009614+00:00'
description: ''
---
# Deep Dive into C/C++ Compilation and Linking Part 4: Dynamic Libraries A1: Basic Discussion on `-fPIC`

## Preface

It has been an exhausting few weeks, juggling a bunch of tasks and preparing to start a new job. I finally found a moment to catch my breath and continue updating this blog series.

This article primarily covers the basics of dynamic libraries. Specifically, we discuss how to build a dynamic library (focusing on Linux; building on Windows via the MSVC toolchain from the command line is rather painful, and plenty of mature build systems already abstract away those details, so we will skip a deep dive into Windows dynamic library builds here), along with some issues related to symbol name mangling.

## How to Create a Dynamic Library on Linux

Creating a dynamic library is not complicated, but it generally requires following these steps:

- The integrated binary relocatable files must be compiled with the position-independent flag (`-fPIC`, i.e., the Position Independent Code flag)
- Integrate these PIC binary relocatable files, and then pass the `-shared` flag

## Let's Talk About -fPIC

This option is quite interesting. Of course, there is not much to say about the `-shared` option—it simply tells our compiler to link a dynamic library. But why do these relocatable files need to be compiled as position-independent code?

In *Advanced C/C++ Compilation Technology*, three progressively deeper questions are raised:

- What is `-fPIC`?
- Is `-fPIC` strictly required to create a dynamic library (.so)?
- Is `-fPIC` only used when compiling dynamic libraries?

Below, I have organized the explanations from that book, combined with some of my own perspectives, and laid them out here.

#### What is `-fPIC`?

`-fPIC` stands for **`Position-Independent Code`** (generating position-independent code). In other words, the compiled machine instructions **do not rely on a fixed load address** and can be loaded into any memory location at runtime without modifying the code itself. This aligns perfectly with our understanding of how dynamic libraries function. Ultimately, we need to export symbols from a dynamic library for use by third-party applications or other libraries. Therefore, we obviously cannot assign an absolute mapped address to these dynamic library symbols. Instead, at the point of reuse, we dynamically provide an offset address mapped into the consumer's process address space, which is how we achieve symbol reuse. Breaking it down step by step:

- `-fPIC` maps symbols using **relative addresses** rather than absolute addresses
- Global variables are accessed indirectly through the **GOT (Global Offset Table)**
- Function calls jump through the **PLT (Procedure Linkage Table)**

------

#### **Is `-fPIC` strictly required to create a dynamic library (.so)?**

Strictly speaking, not necessarily. Of course, if we consider that 32-bit PCs are practically extinct today (forgive my ignorance, but I have never actually seen a physical 32-bit PC, though I have tinkered a bit with MCUs), then we might affirm the above proposition.

Let's think about it: modern dynamic libraries are synonymous with shared libraries, where multiple processes share the code segment of a dynamic library. For different processes, it is perfectly reasonable to require that the code be loaded at any virtual address. Otherwise, the loader would have to perform **relocation patching** on the code at load time, which prevents the code segment from being shared and slows down loading.

However, on x86-64, it is still possible to compile a usable dynamic library without `-fPIC`, but we lose the sharing property, and loading becomes slower (because addresses for all symbols must be fixed up at load time). So, if we think about it seriously, my conclusion is:

> **Today, compiling a dynamic library must include the -`fPIC` flag; the benefits far outweigh the drawbacks (unless you are deeply concerned about minor performance penalties, in which case we are simply considering different scenarios).**

#### Is `-fPIC` exclusive to dynamic libraries? Can we use `-fPIC` with static libraries?

Obviously not; otherwise, there would be no need to make this flag independent. In fact, we can absolutely apply `-fPIC` to relocatable files that are destined to be compiled into a static library. This is very common.

For example, I have a fairly large project on hand that generates a static library for each sub-module, and then packages all the generated static libraries in a directory into a single dynamic library. As we discussed in previous articles, a static library is simply a collection of relocatable files. So, it naturally follows that in this scenario, we must compile the source files with the `-fPIC` flag for the relocatable files contained within those static libraries.
