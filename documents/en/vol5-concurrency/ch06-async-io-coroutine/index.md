---
title: Asynchronous I/O and Coroutines
description: From the evolution of asynchronous programming paradigms to the C++20
  coroutine mechanism, master `co_await`/`co_yield`/`co_return` and coroutine lifecycle
  management.
translation:
  source: documents/vol5-concurrency/ch06-async-io-coroutine/index.md
  source_hash: a4cf5e57c4ee644c861399ab81f60e50026ad0dd184417bc681b0b9759eb826b
  translated_at: '2026-05-20T04:47:13.183087+00:00'
  engine: anthropic
  token_count: 259
---
# Asynchronous I/O and Coroutines

In previous chapters, we built the foundation for concurrent programs using threads, mutexes, atomics, and futures. However, when we face "I/O-bound" scenarios—such as a network server handling thousands of connections simultaneously—the traditional "one thread per connection" model reveals serious resource waste. Threads consume memory and scheduling resources while simply waiting for I/O. We need a lighter-weight way to express "go do something else, and come back when the I/O is done."

In this chapter, we start with the evolution of asynchronous programming paradigms, comparing the motivations and pain points of callbacks, future chains, and coroutines to understand why coroutines are considered the "right way to do async." We then dive into the internal mechanisms of C++20 coroutines—the compiler's state machine transformation of coroutine functions, the allocation and destruction of coroutine frames, and the lifetime management of `coroutine_handle`—and implement a complete generator from scratch to tie all the concepts together. Next, we turn our attention to the two major customization extension points of coroutines (`promise_type` and awaitables), connecting coroutines with the operating system's I/O multiplexing mechanisms to build a coroutine-driven event loop. Finally, we use a complete coroutine Echo Server to tie all the knowledge points together in practice.

## Chapter Contents

<ChapterNav variant="sub">
  <ChapterLink href="01-async-programming-evolution">Async Programming Evolution: From Callback Hell to Coroutines</ChapterLink>
  <ChapterLink href="02-coroutine-basics">C++20 Coroutine Basics</ChapterLink>
  <ChapterLink href="03-promise-type-and-awaitable">promise_type and Awaitable</ChapterLink>
  <ChapterLink href="04-async-io-and-event-loop">Asynchronous I/O and Event Loop</ChapterLink>
  <ChapterLink href="05-coroutine-echo-server">Hands-on: Coroutine Echo Server</ChapterLink>
</ChapterNav>
