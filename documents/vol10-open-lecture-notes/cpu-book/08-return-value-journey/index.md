---
title: "函数返回值的回家路：从 rax 到返回值优化"
description: "返回值怎么从被调函数回到调用现场，看着只是一句 return，底下有 rax、隐藏指针、RVO 三条路。这个主题把三条路的触发条件与汇编形态逐一实测，大对象什么时候走哪条路，看完就有判断。"
sidebar_order: 8
---

# 函数返回值的回家路：从 rax 到返回值优化

返回值怎么从被调函数回到调用现场，看着只是一句 return，底下有 rax、隐藏指针、RVO 三条路。这个主题把三条路的触发条件与汇编形态逐一实测，大对象什么时候走哪条路，看完就有判断。

<ChapterNav variant="sub">
  <ChapterLink href="01-rax-family-and-register-pairs">函数返回值的回家路：rax 家族与成对寄存器</ChapterLink>
  <ChapterLink href="02-sret-and-routing-criteria">大件走隐藏指针：sret 的全链路与分路判据</ChapterLink>
  <ChapterLink href="03-nrvo-copy-elision">NRVO：返回路径上的拷贝从未存在</ChapterLink>
  <ChapterLink href="04-multivalue-return-and-channel-recap">多值返回：pair 打包与三条通道的全景</ChapterLink>
</ChapterNav>
