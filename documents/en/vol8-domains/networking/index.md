---
title: "Networking"
description: "From Linux socket foundations to epoll/Reactor, then Boost.Asio, coroutines, and std::execution — modern C++ network programming"
platform: host
tags:
  - cpp-modern
  - host
  - intermediate
---

# Networking

C++ has long lived in performance-first territory, and one of its main battlefields is network programming. Since most servers run on Linux (most — I have to stress *most*, because some still run on Windows Server), this sub-volume centers on **the BSD Socket heritage and the epoll mechanism that grew out of it**, and builds up from there toward modern C++ networking.

The plan is **Linux first**, but Windows IOCP still matters, so we'll slot it in once this volume's framework is solid. From there we work toward Boost.Asio's async abstraction (the common answer before coroutines were standardized), revisit the C++20 coroutine model, and reach Boost.Beast. We won't skip std::execution — as of late June 2026, still an exciting one.


## Linux foundations

- [00 · Traditional socket programming: the server five-step and TCP handshake](./00-traditional-socket-basics.md) — the classic C-style BSD socket five steps, the TCP three-way handshake timeline, byte order, `listen`'s two queues and backlog, SIGPIPE/SO_REUSEADDR
- [01 · Modern socket wrapping: RAII and `std::expected`](./01-modern-socket-wrapping.md) — cleaning up 00's raw fds and scattered errno with Modern C++, with a measured "thread-per-connection" C10K cost of 24GB at 2000 idle connections
- [02 · epoll: Linux I/O multiplexing](./02-epoll-io-multiplexing.md) — interest list + ready list + wait queue, the kernel-level difference between ET and LT, why ET mandates non-blocking + read-to-EAGAIN, and a reproduction of ET-read-once dropping 87KB
- [03 · The Reactor pattern: wrapping epoll into an event loop + callbacks](./03-reactor-pattern.md) — the four POSA2 roles, "synchronous non-blocking", and the load-bearing Reactor (ready notification) ↔ Proactor (completion notification) contrast

## What's next (in progress)

io_uring (Linux completion-driven, completing the backend tour) → Boost.Asio (sync to async callback chains) → Asio executors + completion tokens → C++20 coroutines on Asio → Boost.Beast (HTTP/WebSocket) → std::execution (P2300) outlook. Companion **Lab: mini Reactor echo server** (adversarial acceptance + TSan) is in progress.

> For the source-reading layer (Boost.Asio / Beast internals), see [vol9, open-source project study](../../vol9-open-source-project-learn/).
