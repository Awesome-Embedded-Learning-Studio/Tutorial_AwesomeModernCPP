---
title: "Walking the Multi-Level Page Table: Translating a Virtual Address to a Physical Address"
description: "Between a pointer value and a real location in memory stands a multi-level page table. This topic walks all four levels of a real translation, with page faults, huge pages, and the TLB each taking their place in live measurements."
sidebar_order: 6
translation:
  source: documents/vol10-open-lecture-notes/cpu-book/06-page-table-walk/index.md
  source_hash: 7b01f6dcc13b20edcab945ff4e4ff84b6604193e041aebea41ba7d2456df3630
  translated_at: '2026-10-14'
  engine: manual
---

# Walking the Multi-Level Page Table: Translating a Virtual Address to a Physical Address

Between a pointer value and a real location in memory stands a multi-level page table. This topic walks all four levels of a real translation, with page faults, huge pages, and the TLB each taking their place in live measurements.

<ChapterNav variant="sub">
  <ChapterLink href="01-virtual-address-and-4k-pages">Virtual Addresses and 4 KiB Pages: ASLR and the Arithmetic of the Flat Table</ChapterLink>
  <ChapterLink href="02-four-level-walk-and-pagemap">Four Levels of Tables: Splitting a 48-bit Address and Reading the PTE from pagemap</ChapterLink>
  <ChapterLink href="03-page-fault-demand-paging-and-tlb">Two Fates of a Page Fault: Demand Paging and the TLB</ChapterLink>
</ChapterNav>
