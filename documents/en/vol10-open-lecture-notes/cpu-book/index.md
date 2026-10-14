---
title: "C/C++ in the CPU's Eyes"
description: "From a single line of code down to instructions, stack frames, and page tables: every layer of C++ abstraction, looked at once more from the CPU's point of view"
translation:
  source: documents/vol10-open-lecture-notes/cpu-book/index.md
  source_hash: 523f76e5fa827a12dc0a033c00cbb35418bf215bb8f7f56a66344bbccd857587
  translated_at: '2026-10-14'
  engine: manual
---

# C/C++ in the CPU's Eyes

In day-to-day C++, abstractions like classes, references, and iterators wrap up the low-level details so cleanly that we rarely have to think about them, and writing code really is easier that way. This series turns the viewpoint around: I'd like you to come stand on the CPU's side with me and look back from there, to see what happens at the instruction level when a line of `i++` runs, what a function call leaves behind on the stack, what an object actually looks like in memory, and how a pointer value becomes a real location on the memory chips, one step at a time.

The series covers 11 topics, written up as 35 short articles, and any one of them takes about ten minutes to read. Every example and experiment is original to this site. Every block of assembly, every debugger session, and every line of program output you see in these articles comes verbatim from real runs on my machine; type the commands in yourself and you will reproduce them. The environment section in the first article of each topic records the compiler version that was running at the time.

## Contents

<ChapterNav variant="sub">
  <ChapterLink href="01-prefix-vs-postfix-increment/">Do i++ and ++i Really Produce the Same Instructions?</ChapterLink>
  <ChapterLink href="02-function-calls-and-stack-frames/">Function Calls and Returns: Stack Frames, call, and ret</ChapterLink>
  <ChapterLink href="03-arguments-in-registers/">How Arguments Get Into Registers: Passing Under the System V AMD64 ABI</ChapterLink>
  <ChapterLink href="04-stack-overflow-why-no-crash/">Why an Out-of-Bounds Array Write on the Stack Sometimes Doesn't Crash</ChapterLink>
  <ChapterLink href="05-classes-in-assembly/">What Your Class Looks Like in Assembly: this, Constructors, and the vtable</ChapterLink>
  <ChapterLink href="06-page-table-walk/">Walking the Multi-Level Page Table: Translating a Virtual Address to a Physical Address</ChapterLink>
  <ChapterLink href="07-what-volatile-guarantees/">What volatile Actually Governs: It Holds Back the Compiler, Not Data Races</ChapterLink>
  <ChapterLink href="08-return-value-journey/">How Function Return Values Get Back: From rax to Return Value Optimization</ChapterLink>
  <ChapterLink href="09-manual-backtrace-with-gdb/">A Manual Backtrace in gdb: Rebuilding the Call Chain Yourself</ChapterLink>
  <ChapterLink href="10-new-in-assembly/">The Two Steps of new in Assembly: One Allocation, End to End</ChapterLink>
  <ChapterLink href="11-template-instantiation/">Template Instantiation: The Code the Compiler Writes for You</ChapterLink>
</ChapterNav>
