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
  source_hash: 1f487380ec8bb8ff28899726e24b557443abca6abf77d7ce0c02b8f534e4735c
  translated_at: '2026-09-26T17:05:28+00:00'
  engine: anthropic
  token_count: 830
---
# Deep Dive into C/C++ Compilation and Linking

This volume is about what the compiler and the linker are actually doing behind the scenes: how source code turns into an executable, how static and dynamic libraries differ, how symbols get found — and how they fail to be — and at which step errors like `undefined reference` are actually stuck. By the end you'll be able to track down linker errors, design the ABI of your own dynamic libraries, and understand low-level machinery such as GOT/PLT.

> New to C++? Work through the [Getting Started volume](/getting-started/) first and get your environment running. This volume is a deep dive into mechanisms, written for readers who have been through the Volume 1 basics and want to understand the "why". Read it alongside the advanced CMake material in [Volume 7 · Engineering Practice](/vol7-engineering/) and you'll lose nothing on either the mechanisms or the tooling.

## Chapter Navigation

<ChapterNav>
  <ChapterLink num="1" href="01-compilation-and-linking-overview">Compilation and Linking Overview: Where undefined reference Comes From</ChapterLink>
  <ChapterLink num="2" href="02-reuse-concept">The Essence of Reuse: From Source Level to Binary Level</ChapterLink>
  <ChapterLink num="3" href="03-creating-and-using-static-libs">Static Libraries: Packaged with ar, Linked with -l/-L</ChapterLink>
  <ChapterLink num="4" href="04-dynamic-libraries-1">Dynamic Libraries (Part 1): Why -fPIC Is Mandatory</ChapterLink>
  <ChapterLink num="5" href="05-dynamic-library-design">Dynamic Library Design: ABI and Cross-Toolchain Interfaces</ChapterLink>
  <ChapterLink num="6" href="06-symbol-visibility">Symbol Visibility: Controlling What a Dynamic Library Exports</ChapterLink>
  <ChapterLink num="7" href="07-symbol-missing-and-runtime-loading">Missing Symbols and Runtime Loading: dlopen and LoadLibrary</ChapterLink>
  <ChapterLink num="8" href="08-library-search-logic">Library Search Logic: How Libraries Are Found at Link Time and Run Time</ChapterLink>
  <ChapterLink num="9" href="09-dynamic-library-details">Dynamic Library Details: PLT/GOT Lazy Binding and Symbol Interposition</ChapterLink>
  <ChapterLink num="10" href="10-dynamic-lib-as-executable">Side Story: Can a Dynamic Library Run as an Executable?</ChapterLink>
</ChapterNav>
