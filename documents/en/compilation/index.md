---
title: "Deep Dive into C/C++ Compilation and Linking"
description: "The low-level mechanics of C/C++ compilation, linking, static libraries, dynamic libraries, and symbol visibility — once these click, you can debug linker errors, design libraries, and optimize performance"
platform: host
tags:
  - cpp-modern
  - host
  - intermediate
translation:
  source: documents/compilation/index.md
  source_hash: 9fc8de1ed8ce675d3e723a81ecfff4eff686060b647c7116abbcdebe7e0b0c0e
  translated_at: '2026-10-02T10:08:00+00:00'
  engine: glm
  token_count: 413
---
# Deep Dive into C/C++ Compilation and Linking

This volume is about what the compiler and the linker are actually doing behind the scenes: how source code turns into an executable, how static and dynamic libraries differ, how symbols get found — and how they fail to be — and at which step errors like `undefined reference` are actually stuck. By the end you'll be able to track down linker errors, design the ABI of your own dynamic libraries, and understand low-level machinery such as GOT/PLT.

> New to C++? Work through the [Getting Started volume](/getting-started/) first and get your environment running. This volume is a deep dive into mechanisms, written for readers who have been through the Volume 1 basics and want to understand the "why". Read it alongside the advanced CMake material in [Volume 7 · Engineering Practice](/vol7-engineering/) and you'll lose nothing on either the mechanisms or the tooling.

## Chapter Navigation

> This volume has been fully restructured: articles 01-16 have all been rewritten.

<ChapterNav>
  <ChapterLink num="1" href="01-compilation-pipeline">Compilation and Linking: Four Stages from .cpp to Executable</ChapterLink>
  <ChapterLink num="2" href="02-preprocessing">Preprocessing: How a Translation Unit Comes to Be</ChapterLink>
  <ChapterLink num="3" href="03-what-the-compiler-does">What the Compiler Does: From C++ to Assembly</ChapterLink>
  <ChapterLink num="4" href="04-object-file-anatomy">Object File Anatomy: What's Inside a .o</ChapterLink>
  <ChapterLink num="5" href="05-linking-symbol-resolution">The Linker: Symbol Resolution and undefined reference</ChapterLink>
  <ChapterLink num="6" href="06-static-libraries">Static Libraries: Packaging with ar and Linking with -l/-L</ChapterLink>
  <ChapterLink num="7" href="07-templates-inline-odr">Templates, inline, and ODR: Ten Translation Units, One Entity</ChapterLink>
  <ChapterLink num="8" href="08-dynamic-libraries-overview">Dynamic Libraries: An Overview of Build-Time and Run-Time</ChapterLink>
  <ChapterLink num="9" href="09-pic-got-plt">Position-Independent Code and GOT/PLT: Addresses Settled at Load Time</ChapterLink>
  <ChapterLink num="10" href="10-library-search-logic">Library Search Logic: How Libraries Are Found at Link Time and Run Time</ChapterLink>
  <ChapterLink num="11" href="11-symbol-visibility">Symbol Visibility: Controlling What a Dynamic Library Exports</ChapterLink>
  <ChapterLink num="12" href="12-abi-design">ABI Design: Defining the Interface, Managing Its Versions</ChapterLink>
  <ChapterLink num="13" href="13-runtime-loading-plugins">Runtime Loading and Plugins: The Program Finds Libraries, Fetches Symbols, and Unloads</ChapterLink>
  <ChapterLink num="14" href="14-program-startup">Program Startup: From exec to main</ChapterLink>
  <ChapterLink num="15" href="15-lto-and-modern-linkers">LTO and Modern Linkers: Moving the Optimization Boundary to Link Time</ChapterLink>
  <ChapterLink num="16" href="16-dynamic-lib-as-executable">Side Story: Can a Dynamic Library Run as an Executable?</ChapterLink>
</ChapterNav>
