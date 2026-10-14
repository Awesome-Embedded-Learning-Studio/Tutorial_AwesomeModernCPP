---
title: "你的类在汇编里长什么样：this、构造函数与虚表"
description: "类是源码层的抽象，到了汇编层只剩下内存里的字节和几条 call。这个主题从 this 指针的下落讲到构造析构的插入点，再拆开虚函数背后的函数指针表，把对象在机器层的全部内容看完。"
sidebar_order: 5
---

# 你的类在汇编里长什么样：this、构造函数与虚表

类是源码层的抽象，到了汇编层只剩下内存里的字节和几条 call。这个主题从 this 指针的下落讲到构造析构的插入点，再拆开虚函数背后的函数指针表，把对象在机器层的全部内容看完。

<ChapterNav variant="sub">
  <ChapterLink href="01-this-pointer-and-member-offsets">你的类在汇编里长什么样：this 指针与成员偏移</ChapterLink>
  <ChapterLink href="02-constructor-and-destructor-calls">你的类在汇编里长什么样：构造与析构的插入点</ChapterLink>
  <ChapterLink href="03-virtual-functions-and-vtables">你的类在汇编里长什么样：虚函数与虚表</ChapterLink>
  <ChapterLink href="04-devirtualization-under-o2">你的类在汇编里长什么样：-O2 的去虚拟化</ChapterLink>
</ChapterNav>
