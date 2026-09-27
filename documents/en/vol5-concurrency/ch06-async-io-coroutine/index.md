---
title: "Asynchronous I/O and Coroutines"
description: "From the evolution of asynchronous programming paradigms to the C++20 coroutine mechanism — master co_await/co_yield/co_return and coroutine lifecycle management"
translation:
  source: documents/vol5-concurrency/ch06-async-io-coroutine/index.md
  source_hash: a4cf5e57c4ee644c861399ab81f60e50026ad0dd184417bc681b0b9759eb826b
  translated_at: '2026-09-26T09:02:10+00:00'
  engine: anthropic
  token_count: 875
---

# Asynchronous I/O and Coroutines

In the preceding chapters we built the infrastructure of concurrent programs with tools such as threads, `mutex`, `atomic`, and `future`. But when we face an I/O-bound scenario — say, a network server that has to handle thousands of connections at the same time — the traditional one-thread-per-connection model reveals a serious waste of resources. A thread doing nothing but waiting for I/O still holds on to memory and scheduling resources; we need a lighter-weight way to express "go do something else first, and come back once the I/O is done."

This chapter starts from the evolution of asynchronous programming paradigms, comparing the motivations and pain points of the three models — callbacks, future chains, and coroutines — so we can understand why coroutines are regarded as the right way to do async. We then dive into the internal machinery of C++20 coroutines — the compiler's state-machine transformation of coroutine functions, the allocation and destruction of the coroutine frame, and the lifetime management of `coroutine_handle` — and implement a complete generator from scratch to tie all the concepts together. Next we turn to the two major customization extension points of coroutines (`promise_type` and awaitables), plug coroutines into the operating system's I/O multiplexing, and build a coroutine-driven event loop. Finally, a complete coroutine Echo Server built in practice strings all of these knowledge points together.

## In This Chapter

<ChapterNav variant="sub">
  <ChapterLink href="01-async-programming-evolution">The Evolution of Asynchronous Programming: From Callback Hell to Coroutines</ChapterLink>
  <ChapterLink href="02-coroutine-basics">C++20 Coroutine Fundamentals</ChapterLink>
  <ChapterLink href="03-promise-type-and-awaitable">promise_type and awaitable</ChapterLink>
  <ChapterLink href="04-async-io-and-event-loop">Asynchronous I/O and Event Loops</ChapterLink>
  <ChapterLink href="05-coroutine-echo-server">Coroutine Echo Server in Practice</ChapterLink>
</ChapterNav>
