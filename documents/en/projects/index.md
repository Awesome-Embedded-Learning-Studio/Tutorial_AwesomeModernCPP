---
title: Comprehensive Hands-on Project
description: Synthesize scattered knowledge from various volumes into complete projects—from
  coroutine servers, mini runtimes, to industrial-grade component analysis.
platform: host
tags:
- cpp-modern
- host
- intermediate
translation:
  source: documents/projects/index.md
  source_hash: 42eb53b42495d01aa1fb318f2084582052831d6c383d0cf22598181cb3c8fd75
  translated_at: '2026-06-13T11:40:38.476774+00:00'
  engine: anthropic
  token_count: 324
---
# Comprehensive Hands-on Projects

> This section is not merely a pile of new concepts, but an effort to weave together the fragments learned across various volumes—concurrency, coroutines, templates, memory management—into a complete project that runs, tests, and can be delivered. Below, we first list projects that have already been implemented in other volumes and are ready for you to continue, followed by long-term goals still in the planning phase.

## Projects with a Foundation

These projects already have tutorials or runnable skeletons in other volumes, providing a path for you to dive deeper:

- **Coroutine Echo Server**: In [Volume 5: Coroutine Echo Server](../vol5-concurrency/ch06-async-io-coroutine/05-coroutine-echo-server.md), we built a fully functional echo service from `io_uring` to data transmission. This is the most practical project for understanding coroutine scheduling.
- **Mini Concurrent Runtime (Capstone)**: [Volume 5: Mini Runtime Capstone](../vol5-concurrency/exercises/06-capstone-mini-runtime.md) combines thread pools, timers, and task queues into a minimal scheduler, serving as a ready-made starting point for the "Mini Concurrent Runtime".
- **OnceCallback Component Study**: [Volume 9: OnceCallback](../vol9-open-source-project-learn/chrome/01_once_callback/index.md) dissects Chromium's callback mechanism across 16 articles, serving as a paradigm for transitioning from reading source code to designing industrial-grade components yourself.
- **INI Parser**: As the first complete project for C++ engineering, this is located in a separate repository [Tutorial_cpp_SimpleIniParser](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_cpp_SimpleIniParser)—covering lexical analysis to error handling, it is perfect for following along from start to finish.

## Planned Projects

These have not yet started and are long-term goals, sorted by "readiness of materials":

- **Hand-rolled STL Components**: Implement `vector`, `string`, `unique_ptr`, `optional`, `function`, and `variant` from scratch, complementing the source code reading in Volume 3 (Standard Library).
- **Mini HTTP Server**: From TCP sockets to coroutine-based asynchrony, building upon Volume 5 (Concurrency) and Volume 8 (Network Programming).
- **Mini GUI Framework**: Event loops, widget systems, layout engines, and rendering backends.
- **Embedded Mini OS**: Scheduler, synchronization primitives, memory management, and driver framework, extending the main thread of Volume 8 (Embedded Systems).

> These projects will not be completed overnight; they will be launched gradually as the content in their respective volumes is finalized. If you have a project you would like to see, feel free to propose it in the Discussions.
