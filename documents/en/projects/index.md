---
title: 'Hands-On Projects: Putting It All Together'
description: String the scattered knowledge from each volume into complete projects — from coroutine servers and mini runtimes to studying industrial-grade components
platform: host
tags:
  - cpp-modern
  - host
  - intermediate
translation:
  source: documents/projects/index.md
  source_hash: 42eb53b42495d01aa1fb318f2084582052831d6c383d0cf22598181cb3c8fd75
  translated_at: '2026-09-27T03:10:35+00:00'
  engine: anthropic
  token_count: 700
---

# Hands-On Projects: Putting It All Together

> This section is not a pile of new knowledge. It takes the fragments you picked up across the volumes — concurrency, coroutines, templates, memory management — and strings them into a complete project that can run, be tested, and be delivered. Below we first list the projects that have already landed in other volumes and are ready for you to pick up right away, then the long-term goals that are still in planning.

## Projects That Already Have a Foundation

These projects already have tutorials or runnable skeletons in other volumes, and each one is a thread you can pull to go deeper:

- **Coroutine Echo Server**: In [Volume 5: Coroutine Echo Server](../vol5-concurrency/ch06-async-io-coroutine/05-coroutine-echo-server.md) we build our way up from `co_await` to a working echo service that can send and receive — the most hands-on project there is for understanding coroutine scheduling.
- **Mini Concurrent Runtime (capstone)**: [Volume 5: Mini Runtime Capstone](../vol5-concurrency/exercises/06-capstone-mini-runtime.md) blends a thread pool, timers, and task queues into a minimal scheduler — a ready-made starting point for the later "Mini Concurrent Runtime".
- **OnceCallback Component Study**: [Volume 9: OnceCallback](../vol9-open-source-project-learn/chrome/01_once_callback/index.md) uses 16 articles to dissect Chromium's callback mechanism by hand, a model for moving from reading source code to "designing industrial-grade components yourself".
- **INI Parser**: As the first complete project in C++ engineering, it lives in its own repository, [Tutorial_cpp_SimpleIniParser](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_cpp_SimpleIniParser) — from lexical analysis to error handling, it is well worth building along once.

## Projects in Planning

None of these have been started yet; they are long-term goals, ordered by "how ready the source material is":

- **Hand-Written STL Components**: write vector / string / unique_ptr / optional / function / variant each from scratch, paired with the standard-library source reading in Volume 3.
- **Mini HTTP Server**: from TCP sockets to coroutine-based asynchrony, building on Volume 5's concurrency and Volume 8's network programming.
- **Mini GUI Framework**: event loop, widget system, layout engine, rendering backend.
- **Embedded Mini OS**: scheduler, synchronization primitives, memory management, driver framework, continuing Volume 8's embedded track.

> None of these projects will come together in one stroke; each will start step by step as the corresponding volume matures. If there is a project you would like to take on, you are welcome to propose it in the Discussions.
