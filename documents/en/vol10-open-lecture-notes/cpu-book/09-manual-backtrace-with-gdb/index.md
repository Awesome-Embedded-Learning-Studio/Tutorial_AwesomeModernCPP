---
title: "A Manual Backtrace in gdb: Rebuilding the Call Chain Yourself"
description: "Type bt in gdb and the call stack appears, but how does gdb read it? This topic climbs the frame-pointer chain, the unwind tables, and symbol recovery by hand. With the tool's mechanism taken apart, you can compute every frame address at a crash scene yourself."
sidebar_order: 9
translation:
  source: documents/vol10-open-lecture-notes/cpu-book/09-manual-backtrace-with-gdb/index.md
  source_hash: 70f538b1abbd8b8ac5bfa70fb3e80d35e0b6128c0fcb642446451ad32fb1b83f
  translated_at: '2026-10-14'
  engine: manual
---

# A Manual Backtrace in gdb: Rebuilding the Call Chain Yourself

Type bt in gdb and the call stack appears, but how does gdb read it? This topic climbs the frame-pointer chain, the unwind tables, and symbol recovery by hand. With the tool's mechanism taken apart, you can compute every frame address at a crash scene yourself.

<ChapterNav variant="sub">
  <ChapterLink href="01-bt-baseline-and-rbp-chain">bt's Normal Output and a Manual Backtrace Along the rbp Chain</ChapterLink>
  <ChapterLink href="02-no-frame-pointer-no-symbols">Frame Pointer Omitted, Symbols Stripped: Unwind Tables and addr2line</ChapterLink>
  <ChapterLink href="03-inline-and-crash-forensics">Inlining and Crash Forensics: Three Layers, Each Covering Its Own Stretch</ChapterLink>
</ChapterNav>
