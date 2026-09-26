---
title: "Networking"
description: "From Linux socket foundations to epoll/Reactor, then Boost.Asio, coroutines, and std::execution — modern C++ network programming"
platform: host
tags:
  - cpp-modern
  - host
  - intermediate
translation:
  source: documents/vol8-domains/networking/index.md
  source_hash: 940feb2f7df261544e7b6951c5b9d304f087fa780539ebfc9e8b42fffa1e3c2e
  translated_at: '2026-09-26T04:32:48+00:00'
  engine: anthropic
  token_count: 1250
---

# Networking

C++ has spent its life in performance-first territory, and one of its main battlefields is network programming. Since most servers run Linux (most — we have to stress *most*, because some services still run on Windows Server), a core focus of this sub-volume's plan is to ground everything in **the BSD Socket heritage and the famous epoll mechanism later derived from it**, and from that foundation work up, step by step, to modern C++ networking.

The current plan is **Linux first**, but Windows IOCP still matters — once this volume's framework is fully in place, we'll slot in the asynchronous programming model Windows offers. From there we gradually move to Boost.Asio's async abstraction (a very common solution before coroutines were officially supported), revisit the coroutine abstraction born in C++20, and reach Boost.Beast. And we won't forget std::execution — a proposal that, as of late June 2026, is still exciting.


## Linux foundations

- [00 · Traditional socket programming: the server's five steps and TCP connection setup](./00-traditional-socket-basics.md) — the classic C-style five-step BSD socket flow, the TCP three-way handshake timeline, byte order, `listen`'s two queues and backlog, SIGPIPE/SO_REUSEADDR
- [01 · Modern socket wrapping: RAII and `std::expected`](./01-modern-socket-wrapping.md) — using Modern C++ to mop up 00's raw fds and scattered errno, plus the measured "thread-per-connection" cost of eating 24GB at 2000 concurrent connections — the C10K problem
- [02 · epoll: Linux I/O multiplexing](./02-epoll-io-multiplexing.md) — interest list + ready list + wait queues, the kernel-level ET vs LT difference, why ET mandates non-blocking sockets + reading in a loop until EAGAIN, and a reproduction of ET-read-once dropping 87KB of data
- [03 · The Reactor pattern: wrapping epoll into an event loop + callbacks](./03-reactor-pattern.md) — the four POSA2 roles, "synchronous non-blocking", and the load-bearing Reactor (readiness notification) ↔ Proactor (completion notification) axis

## What's next (in progress)

io_uring (Linux completion-driven, completing the backend tour) → Boost.Asio (from sync to async callback chains) → Asio executors + completion tokens → C++20 coroutines on Asio → Boost.Beast (HTTP/WebSocket) → std::execution (P2300) outlook. The companion **Lab: mini Reactor echo server** (adversarial acceptance + TSan) is in progress.

> For the source-reading layer (deep dives into Boost.Asio / Beast source code), see [Volume 9: Learning from Open Source Projects](../../vol9-open-source-project-learn/).
