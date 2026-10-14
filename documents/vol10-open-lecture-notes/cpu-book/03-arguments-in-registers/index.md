---
title: "参数是怎么进寄存器的：System V AMD64 ABI 传参实录"
description: "您把参数写在括号里，CPU 从寄存器里读它们。这个主题按 System V AMD64 约定把前几个整型与浮点参数的路线逐个验一遍，参数多了放哪、结构体怎么拆、又怎么回来。"
sidebar_order: 3
---

# 参数是怎么进寄存器的：System V AMD64 ABI 传参实录

您把参数写在括号里，CPU 从寄存器里读它们。这个主题按 System V AMD64 约定把前几个整型与浮点参数的路线逐个验一遍，参数多了放哪、结构体怎么拆、又怎么回来。

<ChapterNav variant="sub">
  <ChapterLink href="01-integer-args-register-allocation">参数是怎么进寄存器的：整数参数的分配表</ChapterLink>
  <ChapterLink href="02-stack-args-and-return-values">第 7 个参数上栈与返回值的两条通道</ChapterLink>
  <ChapterLink href="03-floats-structs-and-abi-docs">浮点参数、结构体装箱与 ABI 文档的出处</ChapterLink>
</ChapterNav>
