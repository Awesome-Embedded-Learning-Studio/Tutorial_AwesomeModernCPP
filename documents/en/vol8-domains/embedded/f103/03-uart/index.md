---
title: 'UART: interrupt-driven, ring buffers, expected'
description: 'Interrupt-driven serial send and receive, a ring buffer absorbing the rate difference, and the error path of command parsing handed to expected — a failed parse returns carrying its reason instead of silently swallowing it'
chapter: 3
order: 0
tags:
  - stm32f1
  - intermediate
  - 循环缓冲区
  - expected
difficulty: intermediate
platform: stm32f1
translation:
  source: documents/vol8-domains/embedded/f103/03-uart/index.md
  source_hash: dc8f3e8a8f1a60f497b798f777278001fef2162e26b7f2273198a5af769b821a
  translated_at: '2026-11-01T05:18:00+00:00'
  engine: anthropic
  token_count: 470
---

# UART: interrupt-driven, ring buffers, expected

## Overview

Two articles on the UART theme have landed at this stop, and we start with the blocking one. The 117 lines of examples/05_uart wire USART1 into a command console: the Uart template writes the configuration into the type, the consteval pin_map assigns one pin group per instance, check keeps the void contract with `__builtin_trap`, and try_send derives a bounded timeout from the baud rate. The baud rate itself we work out by hand from RM0008's formula, starting from PCLK2 at 64MHz: integer 34 plus fraction 12, BRR holds 556, the line actually runs 115107.9 baud, and the error sits on the order of 0.08%. The main loop reads lines one blocking byte at a time, with CRLF normalization, backspace echo, and an overlong-line error that waits for Enter all inside; help, ping, echo, a backspace-corrected line, and the 49-character overlong line were all captured live in Renode's PTY and checked, and the close lays out the blocking boundary plainly: once the main loop stays busy for 300 milliseconds, ORE starts dropping the extra bytes.

The second article is its interrupt-driven counterpart. The 221 lines of examples/08_uart_ring move the receive-a-byte work into the USART1 interrupt: start(3) sets the preemption priority, and CR1's RXNEIE goes up. irq() takes one snapshot of SR and splits the judgment three ways: RXNE reads DR and pushes the received byte into the ring, FE and NE drop the bytes whose line bits came out wrong or whose sampling was disturbed, and ORE reads DR to clear the overrun. The bytes pass through a 64-slot lock-free ring — power-of-two mask wraparound, one wasted slot buying an unambiguous empty test — which leaves 63 slots of buffering, about 5.5 milliseconds of headroom. Parsing switches to std::expected plus std::variant, and errors return carrying their evidence into the warn log. The ping and echo typed during the busy 300 milliseconds get answered in their original order the moment the main loop wakes. flash grows from 5488 to 20044 bytes and RAM from 88 to 460, with the cost and the gain laid into the same table.

## Chapter Navigation

<ChapterNav variant="sub">
  <ChapterLink href="01-blocking-console">A blocking command console: the divider arithmetic of UART frames and the 48-byte line buffer</ChapterLink>
  <ChapterLink href="02-interrupt-ring-console">The blocking console's counterpart: the RXNE interrupt and the 64-byte ring buffer</ChapterLink>
</ChapterNav>
