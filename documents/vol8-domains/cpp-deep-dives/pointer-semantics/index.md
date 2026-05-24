---
title: "指针语义与弱引用设计"
description: "从 T* 到 Borrowed、ObserverPtr、Chrome-like WeakPtr，理解非拥有指针的语义边界与安全实现"
---

# 指针语义与弱引用设计

C++ 里到处都是指针，但并不是所有指针都"拥有"它指向的对象。裸指针 `T*` 可以是拥有的也可以是非拥有的，`T&` 表达借用但不可空，`std::weak_ptr` 解决了 shared ownership 下的弱引用问题——但如果你不用 `shared_ptr` 管理对象呢？Chromium 的 `WeakPtr` 又是怎么设计的？为什么 `T* + raw Flag*` 看起来像 WeakPtr 其实不是？

这个专题我们从头手搓各种非拥有指针类型，一边实现一边搞清楚它们的语义边界、安全条件和工程取舍。

## 本章内容

<ChapterNav variant="sub">
  <ChapterLink href="01-non-owning-pointer-overview">非拥有指针全景：从 T* 到 Borrowed 到 ObserverPtr</ChapterLink>
  <ChapterLink href="02-unsafe-weakptr-ub">WeakPtr 反模式：T* + raw Flag* 的致命陷阱</ChapterLink>
  <ChapterLink href="03-simple-weakptr">SimpleWeakPtr：T* + shared_ptr&lt;Flag&gt; 的安全改进</ChapterLink>
  <ChapterLink href="04-chrome-weakptr">Chrome-like WeakPtr：引用计数控制块与 WeakPtrFactory</ChapterLink>
  <ChapterLink href="05-weakptr-comparison-and-async">std::weak_ptr 对比与异步回调实战</ChapterLink>
  <ChapterLink href="06-design-principles">跨线程安全、性能取舍与设计原则总结</ChapterLink>
</ChapterNav>
