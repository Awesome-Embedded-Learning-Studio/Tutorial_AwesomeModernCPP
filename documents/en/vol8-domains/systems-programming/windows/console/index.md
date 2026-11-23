---
title: "Windows Console"
sidebar_order: 50
description: "The input/output side of the Windows console: opening real handles on CONIN$/CONOUT$ directly, the full mode-bit map, the 120x9001 character grid, the input event queue, and before/after readback comparisons of VT sequences"
platform: host
tags:
  - cpp-modern
  - host
  - intermediate
  - 系统编程
  - Win32
translation:
  source: documents/vol8-domains/systems-programming/windows/console/index.md
  source_hash: beb2b06aaa059dbf54fb50f5138df40a22bdee0824d54a8bcba072aca719000f
  translated_at: '2026-11-22T15:31:51+00:00'
  engine: glm-5.3
  token_count: 1240
---

# Windows Console

The [console events and APC](../process/02-console-apc.md) article in the processes chapter covers the event side — how Ctrl+C turns into a function call. This chapter covers the input/output side: the screen is a character grid with an attribute byte per cell, the keyboard feeds in a queue of event records, and VT sequences are a third path that only reached conhost in 2015. The methodology inherits from the interop article's experience: with stdio wired to pipes, we open real CONIN$/CONOUT$ handles with CreateFile, and for acceptance checks we always read the characters and attributes back with ReadConsoleOutput and compare them against the table — no help from human eyes.

<ChapterNav variant="sub">
  <ChapterLink num="1" href="01-console-api" desc="The single article of the Windows-side terminal track: taking the console back from the pipes with CONIN$/CONOUT$ (GetConsoleMode on stdio gets gle=6 outright), the full mode-bit map translated bit by bit (CONIN$ ships 0x1f7, CONOUT$ 0x3, the VT bit off by default, both CPs 936), the screen as a 120x9001 CHAR_INFO grid against a 120x30 visible window, WriteConsoleOutput writing whole blocks without touching the cursor (60/60 read back), WriteConsoleA following the cursor, clipping past the right edge at 119, scrolling to fill, input as a queue of INPUT_RECORDs (one press and one release as two records with all six fields logged; count/peek/take/flush as the four tools; injected Ctrl+C is just a key record; WINDOW_BUFFER_SIZE_EVENT cannot be triggered on this machine, so the system-side path is recorded as-is: even resetting the buffer to the same size reports 87, while a window resize succeeds with srWindow reading back changed and the queue at 0 records), VT sequences compared read-back before and after the switch (nine literal characters with attribute 07 when off, RED with attribute 04 when on, 2J clears the screen but leaves the cursor alone, ANSI ED semantics against older conhost zeroing — dated note), mixing boundaries (VT positioning and direct-write coordinates leave each other alone; WRAP_AT_EOL off overwrites the line end repeatedly; the two interpretations of DISABLE_NEWLINE_AUTO_RETURN's \n), closing with comparisons against the Linux-side tty line discipline, the conhost host process, and VT's close cousins">Windows Console: Character Grid, Input Events, and VT Sequences</ChapterLink>
</ChapterNav>
