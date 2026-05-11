# Deep Dive into C/C++ Compilation and Linking Part 4: Shared Libraries A1: Basic Discussion on `-fPIC`

## Preface

It has been an exhausting few weeks, juggling a multitude of tasks and preparing to start a new job. I finally found a moment to catch my breath and continue updating this blog series.

This article primarily covers the fundamentals of shared libraries. Specifically, we discuss how to create them (focusing on Linux; building shared libraries on Windows via the MSVC toolchain from the command line is rather painful, and plenty of mature build systems already abstract away those details, so we will skip a deep dive into Windows shared library creation). We also touch on issues related to symbol name mangling.

## How to Create Shared Libraries on Linux

Creating a shared library is not complicated, but it generally requires following these steps:

- The integrated binary relocatable files must be compiled with the position-independent flag (``-fPIC``, i.e., the Position Independent Code compiler flag).
- Integrate these PIC binary relocatable files and pass the `-shared` flag.

## Let's Talk About -fPIC

This option is quite interesting. Of course, there is not much to say about the `-shared` option—it simply tells our compiler to link a shared library. But why do these relocatable files need to be compiled as position-independent code?

In *Advanced C/C++ Compilation Technology*, three progressively deeper questions are raised:

- What is ``-fPIC``?
- Is ``-fPIC`` strictly required to create a shared library (`.so`)?
- Is ``-fPIC`` only used when compiling shared libraries?

Below, I have organized the explanations from the book, combined with some of my own perspectives, and listed them here.

#### What is ``-fPIC``?

``-fPIC`` stands for **``Position-Independent Code``** (generating position-independent code). In other words, the compiled machine instructions **do not rely on a fixed load address** and can be loaded into any memory location at runtime without modifying the code itself. This aligns perfectly with our understanding of how shared libraries work. Ultimately, we need to export symbols from a shared library for use by third-party applications or other libraries. Therefore, we obviously cannot assign an absolute mapping address to these shared library symbols. Instead, during reuse, a dynamic offset address is mapped into the consumer's process address space, enabling symbol reuse. Breaking it down step by step:

- `-fPIC` maps symbols using **relative addresses** rather than absolute addresses.
- Global variables are accessed indirectly through the **GOT (Global Offset Table)**.
- Function calls jump through the **PLT (Procedure Linkage Table)**.

------

#### **Is ``-fPIC`` strictly required to create a shared library (`.so`)?**

Strictly speaking, not necessarily. Of course, if we consider that 32-bit PCs are practically extinct today (forgive my ignorance, but I have never actually seen a physical 32-bit PC, though I have tinkered with a few microcontrollers), then we might agree with the above proposition.

Let's think about it: modern shared libraries and dynamic libraries are synonymous, with multiple processes sharing the code segment of a dynamic library. For different processes, it makes perfect sense that the code should be loadable at any virtual address. Otherwise, the loader would have to perform **relocation patching** on the code at load time, which prevents the code segment from being shared and slows down loading.

However, on x86-64, it is still possible to compile a usable shared library without ``-fPIC``. But doing so throws away the sharing capability and slows down loading (since addresses for all symbols must be fixed up at load time). So, if we think about it seriously, my conclusion is:

> **Today, compiling a shared library must include the `-`fPIC`` flag; the benefits far outweigh the drawbacks (unless you are deeply concerned about minor performance overhead, in which case we are simply considering different scenarios).**

#### Is ``-fPIC`` exclusive to shared libraries? Can we use ``-fPIC`` for static libraries?

Obviously not; otherwise, there would be no need to make this flag independent. In fact, we can absolutely apply ``-fPIC`` to relocatable files that are destined to be compiled into static libraries. This is very common.

For example, I have a fairly large project on hand that generates a static library for each sub-module, and then packages all the generated static libraries in a directory into a single shared library. As we discussed in previous articles, a static library is simply a straightforward collection of relocatable files. So, it naturally follows that in this scenario, we must compile the source files with the ``-fPIC`` flag to ensure the relocatable files contained within the static libraries are position-independent.
