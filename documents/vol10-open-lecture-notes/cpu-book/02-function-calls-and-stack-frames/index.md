---
title: "函数的调用与返回：栈帧、call 与 ret"
description: "函数调用在 CPU 眼里就是几个约定动作的组合：call 跳进去，prologue 搭台，epilogue 收拾，ret 跳回来。这个主题把每个动作逐条指令走一遍，叶子函数省了什么、栈往哪边长、-O2 又改写了什么，全部拿汇编说话。"
sidebar_order: 2
---

# 函数的调用与返回：栈帧、call 与 ret

函数调用在 CPU 眼里就是几个约定动作的组合：call 跳进去，prologue 搭台，epilogue 收拾，ret 跳回来。这个主题把每个动作逐条指令走一遍，叶子函数省了什么、栈往哪边长、-O2 又改写了什么，全部拿汇编说话。

<ChapterNav variant="sub">
  <ChapterLink href="01-call-and-stack-frame-walkthrough">一次函数调用，CPU 眼里发生了什么</ChapterLink>
  <ChapterLink href="02-leaf-functions-stack-direction-and-o2">叶子函数、栈的方向与 -O2 的调用点</ChapterLink>
  <ChapterLink href="03-kept-frames-and-stack-exhaustion">保住栈帧的函数，与递归把栈用满</ChapterLink>
</ChapterNav>
