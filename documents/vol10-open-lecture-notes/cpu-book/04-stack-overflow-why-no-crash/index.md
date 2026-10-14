---
title: "栈上数组越界为什么有时不崩"
description: "数组越界写花了栈帧，程序却经常不崩。这个主题把「为什么不崩」的几种真实结局逐个摆上台面：写进了填充、写坏了好脾气的邻居、被金丝雀当场按住、以及在 -O2 下干脆写了个寂寞。"
sidebar_order: 4
---

# 栈上数组越界为什么有时不崩

数组越界写花了栈帧，程序却经常不崩。这个主题把「为什么不崩」的几种真实结局逐个摆上台面：写进了填充、写坏了好脾气的邻居、被金丝雀当场按住、以及在 -O2 下干脆写了个寂寞。

<ChapterNav variant="sub">
  <ChapterLink href="01-stack-frame-neighbors">栈帧里的邻居：越界写落在了谁头上</ChapterLink>
  <ChapterLink href="02-hitting-canary-saved-rbp-and-return-address">往远处打：金丝雀、saved rbp 与返回地址</ChapterLink>
  <ChapterLink href="03-canary-defense-and-the-o2-story">金丝雀防线与 -O2 的另一个故事</ChapterLink>
  <ChapterLink href="04-stack-headroom-asan-and-luck">栈的余量、ASan 与运气</ChapterLink>
</ChapterNav>
