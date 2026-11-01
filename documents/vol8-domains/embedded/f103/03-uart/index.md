---
title: "UART:中断驱动、环形缓冲、expected"
description: "中断驱动的串口收发,环形缓冲扛住速率差,命令解析的错误路径交给 expected——解析失败带着原因返回,而不是悄悄吞掉。"
chapter: 3
order: 0
tags:
  - stm32f1
  - intermediate
  - 循环缓冲区
  - expected
difficulty: intermediate
platform: stm32f1
---

# UART:中断驱动、环形缓冲、expected

## 概述

本站目前落地的是 UART 主题的两篇，咱们从阻塞的那篇说起。examples/05_uart 的 117 行把 USART1 装成一台命令控制台，Uart 模板把配置写进了类型，consteval 的 pin_map 给三个实例各配一组引脚，check 用 __builtin_trap 保住 void 的合同，try_send 按波特率算出了有界超时。波特率咱们照 RM0008 的公式从 PCLK2 的 64MHz 手算出整数 34 加小数 12，BRR 摆的是 556，实际跑的是 115107.9 波特，误差是 0.08% 的量级。主循环一字节一停地读行，CRLF 归一、退格回显、超长行到回车才报的行编辑都在里面，Renode 的 PTY 里 help、ping、echo、退格修字与 49 字符的超长行全部实抓对拍，收尾摆清了阻塞的边界：主循环一旦忙上 300 毫秒，ORE 就要把多出来的字节丢掉。

第二篇是它的中断化对照。examples/08_uart_ring 的 221 行把收字节的活挪进了 USART1 中断，start(3) 摆好了抢占等级，CR1 的 RXNEIE 立了起来。irq() 一次读 SR 的快照，判断分的是三路：RXNE 读 DR 把收到的字节送进环，FE 与 NE 把线位摆错了或采样被干扰的字节丢掉，ORE 读 DR 把过载清掉了。字节过 64 格的无锁环，2 的幂掩码回绕，空一格换空判的设计，63 格约合 5.5 毫秒的缓冲余量。解析换成了 std::expected 加 std::variant，错误带着原文回到了 warn 日志里。忙碌 300 毫秒里敲进来的 ping 与 echo，主循环一醒过来就把两行按原序回话了。flash 从 5488 涨到了 20044 字节，RAM 也从 88 涨到了 460 字节，代价与收益咱们摆进了同一张表。

## 章节导航

<ChapterNav variant="sub">
  <ChapterLink href="01-blocking-console">阻塞收发的命令控制台：UART 帧的分频算术与 48 字节行缓冲</ChapterLink>
  <ChapterLink href="02-interrupt-ring-console">阻塞收发的对照篇：RXNE 中断与 64 字节环形缓冲</ChapterLink>
</ChapterNav>
