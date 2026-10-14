---
title: "new 在汇编里的两步走：一次分配的完整旅程"
description: "一次 new 背后是分配与构造两步。这个主题从 operator new 的落点讲到 delete 的两步走，再用数组 new 把 cookie 与逆序析构讲完，放置 new 收尾。"
sidebar_order: 10
---

# new 在汇编里的两步走：一次分配的完整旅程

一次 new 背后是分配与构造两步。这个主题从 operator new 的落点讲到 delete 的两步走，再用数组 new 把 cookie 与逆序析构讲完，放置 new 收尾。

<ChapterNav variant="sub">
  <ChapterLink href="01-allocation-from-operator-new">new 在汇编里的两步走：内存从 operator new 手里来</ChapterLink>
  <ChapterLink href="02-delete-two-steps-and-placement-new">delete 的两步与 placement new：归程和省掉的分配</ChapterLink>
  <ChapterLink href="03-array-new-cookie-and-reverse-destruction">new[] 的三个新面孔：块头 cookie、循环构造与倒序析构</ChapterLink>
</ChapterNav>
