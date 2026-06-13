---
title: Actor Model and CSP
description: Exploring the "shared-nothing" concurrency paradigm—message passing in
  the Actor model and channel communication in CSP
translation:
  source: documents/vol5-concurrency/ch07-actor-channel/index.md
  source_hash: 3e41c426a720badc99a32416c29acdb379053566adaa7f22ff98abcf7c77ce70
  translated_at: '2026-06-13T11:55:25.310489+00:00'
  engine: anthropic
  token_count: 197
---
# The Actor Model and CSP

In previous chapters, we used tools like mutexes, atomics, and futures to protect shared state and coordinate execution order between threads. However, shared memory with locks is just one paradigm of concurrent programming—another school of thought advocates "don't share memory," using message passing to replace locks.

In this chapter, we dive into two "shared-nothing" concurrency models: the Actor model and CSP (Communicating Sequential Processes). The Actor model, proposed by Carl Hewitt in 1973, organizes concurrency using identified Actors and asynchronous message passing, and has been proven at scale in Erlang and Akka. CSP, proposed by Tony Hoare in 1978, connects independent sequential processes using anonymous channels; Go's goroutine + channel is the classic implementation of CSP.

We will use C++ to implement the core components of an Actor framework (mailbox, message loop, supervisor) and Go-like communication pipelines (buffered/unbuffered, close semantics, select mode) from scratch. We will understand their design motivations and implementation principles, and discuss how to choose the appropriate concurrency abstraction in real-world projects.

## Chapter Contents

<ChapterNav variant="sub">
  <ChapterLink href="01-actor-model">The Actor Model and Message Passing</ChapterLink>
  <ChapterLink href="02-channel-and-csp">Channel and the CSP Model</ChapterLink>
</ChapterNav>
