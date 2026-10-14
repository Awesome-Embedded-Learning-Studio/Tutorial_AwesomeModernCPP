---
title: "多级页表走一遍：虚拟地址到物理地址的翻译"
description: "指针值怎么变成内存条上的真实位置，中间隔着一整套多级页表。这个主题拿着真实地址把四级翻译逐层走一遍，缺页、大页与 TLB 的角色也都在实测里各就各位。"
sidebar_order: 6
---

# 多级页表走一遍：虚拟地址到物理地址的翻译

指针值怎么变成内存条上的真实位置，中间隔着一整套多级页表。这个主题拿着真实地址把四级翻译逐层走一遍，缺页、大页与 TLB 的角色也都在实测里各就各位。

<ChapterNav variant="sub">
  <ChapterLink href="01-virtual-address-and-4k-pages">虚拟地址与 4 KiB 切页：ASLR 与平铺大表的算术</ChapterLink>
  <ChapterLink href="02-four-level-walk-and-pagemap">四张表接力：48 位地址的拆解与 pagemap 里的页表项</ChapterLink>
  <ChapterLink href="03-page-fault-demand-paging-and-tlb">缺页的两种命运：按需分页与 TLB</ChapterLink>
</ChapterNav>
