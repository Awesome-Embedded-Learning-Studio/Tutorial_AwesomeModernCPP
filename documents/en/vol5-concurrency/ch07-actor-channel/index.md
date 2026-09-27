---
title: "Actor Model and CSP"
description: "Exploring the \"share-nothing\" concurrency paradigm — message passing in the Actor model and channel communication in CSP"
translation:
  source: documents/vol5-concurrency/ch07-actor-channel/index.md
  source_hash: 3e41c426a720badc99a32416c29acdb379053566adaa7f22ff98abcf7c77ce70
  translated_at: '2026-09-26T07:26:14+00:00'
  engine: anthropic
  token_count: 220
---

# Actor Model and CSP

In the previous chapters we reached for tools like mutexes, atomics, and futures to protect shared state and coordinate the order in which threads run. But shared memory plus locks is just one paradigm of concurrent programming — another school argues for "don't share memory at all", replacing locks with message passing.

In this chapter we dig into two "share-nothing" concurrency models: the Actor model and CSP (Communicating Sequential Processes). The Actor model, proposed by Carl Hewitt in 1973, organizes concurrency around Actors that have identity and communicate through asynchronous message passing; it has been validated at industrial scale in Erlang and Akka. CSP, proposed by Tony Hoare in 1978, connects independent sequential processes through anonymous channels — Go's goroutine + channel is its classic implementation.

Using C++, we will build the core components of an Actor framework (mailbox, message loop, supervisor) and a Go-style channel pipeline (buffered/unbuffered, close semantics, select) from scratch, understand the design motivations and implementation principles behind them, and discuss how to choose the right concurrency abstraction in a real project.

## In This Chapter

<ChapterNav variant="sub">
  <ChapterLink href="01-actor-model">Actor Model and Message Passing</ChapterLink>
  <ChapterLink href="02-channel-and-csp">Channels and the CSP Model</ChapterLink>
</ChapterNav>
