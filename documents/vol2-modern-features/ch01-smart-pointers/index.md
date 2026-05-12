---
title: "智能指针与 RAII"
description: "用 RAII 和智能指针实现自动资源管理"
---

# 智能指针与 RAII

手动管理资源（new/delete、fopen/fclose、lock/unlock）是 C++ 程序员的噩梦之源。RAII 原则告诉我们：把资源的获取绑定到对象的构造，释放交给析构，让作用域替你管理生命周期。这一章我们先深入理解 RAII，然后逐一掌握 unique_ptr、shared_ptr、weak_ptr 的设计哲学和正确用法，最后看看自定义删除器和 scope_guard 怎么处理更复杂的资源场景。

## 本章内容

<ChapterNav variant="sub">
  <ChapterLink href="01-raii-deep-dive">RAII 深入理解：资源管理的基石</ChapterLink>
  <ChapterLink href="02-unique-ptr">unique_ptr 详解：独占所有权的零开销智能指针</ChapterLink>
  <ChapterLink href="03-shared-ptr">shared_ptr 详解：共享所有权与引用计数</ChapterLink>
  <ChapterLink href="04-weak-ptr">weak_ptr 与循环引用</ChapterLink>
  <ChapterLink href="05-custom-deleter">自定义删除器与侵入式引用计数</ChapterLink>
  <ChapterLink href="06-scope-guard">scope_guard 与 defer：通用作用域守卫</ChapterLink>
</ChapterNav>
