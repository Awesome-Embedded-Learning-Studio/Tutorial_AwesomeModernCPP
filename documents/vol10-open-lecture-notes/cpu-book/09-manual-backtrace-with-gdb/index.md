---
title: "gdb 手工回溯调用链：自己把 bt 爬一遍"
description: "gdb 的 bt 一敲就出调用栈，但栈是怎么被它读出来的。这个主题把帧指针链、展开表与符号还原手工爬一遍，工具的机制拆开之后，崩溃现场的每帧地址您都能自己算。"
sidebar_order: 9
---

# gdb 手工回溯调用链：自己把 bt 爬一遍

gdb 的 bt 一敲就出调用栈，但栈是怎么被它读出来的。这个主题把帧指针链、展开表与符号还原手工爬一遍，工具的机制拆开之后，崩溃现场的每帧地址您都能自己算。

<ChapterNav variant="sub">
  <ChapterLink href="01-bt-baseline-and-rbp-chain">bt 的正常形态与 rbp 链手工回溯</ChapterLink>
  <ChapterLink href="02-no-frame-pointer-no-symbols">帧指针省了，符号剥了：unwind 表与 addr2line</ChapterLink>
  <ChapterLink href="03-inline-and-crash-forensics">内联与崩溃取证：三层依赖各管一段</ChapterLink>
</ChapterNav>
