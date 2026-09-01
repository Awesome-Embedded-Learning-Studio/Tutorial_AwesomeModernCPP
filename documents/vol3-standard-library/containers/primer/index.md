---
title: "数据结构 primer：容器底下的结构"
sidebar_order: 0
---

# 数据结构 primer：容器底下的结构

本卷直接讲 STL 容器的内存布局与失效规矩，可容器不是凭空来的：`vector` 背后是动态数组，`map` 背后是红黑树，`unordered_map` 背后是哈希表。没摸过这些结构本身，读实现层的文章就像没见过地基先看二楼。这个子系列把它们一个个讲清楚：每篇只讲一个结构，讲它解决什么问题、代价在哪里，配真跑证据与示意图；讲完把它交还给对应的容器深讲与手搓实战。

<ChapterNav variant="sub">
  <ChapterLink href="01-dynamic-array">动态数组：一块会搬家的内存</ChapterLink>
  <ChapterLink href="02-linked-list">链表：从不搬家，代价是问路</ChapterLink>
</ChapterNav>

## 相邻内容

- [vol3 容器卷](../index.md)：std 容器的概念层，本系列的下游
- [vol8 mini STL 实战](../../../vol8-domains/data-structure/index.md)：亲手手搓容器，本系列的实战延伸
