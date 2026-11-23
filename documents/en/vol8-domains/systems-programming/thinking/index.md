---
title: "Thinking Foundations"
sidebar_order: 5
description: "The conceptual groundwork for systems programming: the RAII paradigm for OS resources (the move-only skeleton for fds, HANDLEs, and mappings) and the error-handling paradigm"
platform: host
tags:
  - cpp-modern
  - host
  - intermediate
  - 系统编程
translation:
  source: documents/vol8-domains/systems-programming/thinking/index.md
  source_hash: 23f77d417942ed4dc3e81925afb0bb79c1243b1fba5be44b5073b1df88660d96
  translated_at: '2026-11-22T15:32:14+00:00'
  engine: glm-5.3
  token_count: 910
---

# Thinking Foundations

Before we start work on both the Linux and Windows sides, we lay down the foundation shared across both camps: how to guarantee that resources never leak, and how to guarantee that errors are reported accurately. The toolkit shared across the whole series — unique_fd, unique_handle, mapped_region — is defined here; the articles on both sides only reference these definitions and never redefine them.

<ChapterNav variant="sub">
  <ChapterLink num="1" href="01-raii-paradigm" desc="fds, HANDLEs, and memory mappings are all things you must hand back once you take them; measured comparisons of a raw fd leaking 1000 times on the exception path against unique_fd's zero leaks, the close return value in a destructor discarded per the man 2 close contract, Windows keeping two failure values -1 and NULL side by side, CloseHandle silently returning TRUE on pseudo-handles, the release/reset/swap hand-back semantics, vector relocation shuffling 7 times with zero moves, and a measured correction: a move-only type does not fall back to copying even when not marked noexcept">The RAII Paradigm for OS Resources: One Skeleton for fds, HANDLEs, and Mappings</ChapterLink>
  <ChapterLink num="2" href="02-error-paradigm" desc="The thread-local nature of errno and GetLastError, the boundary discipline of boxing a failure into an error_code immediately after it happens, the full story of the sys_call template, and the two-exit convention of expected at the toolkit layer and system_error at the application's top level">The Error-Handling Paradigm: From errno to expected</ChapterLink>
</ChapterNav>
