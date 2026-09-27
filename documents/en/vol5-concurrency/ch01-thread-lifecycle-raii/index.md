---
title: "Thread Lifecycle and RAII"
description: "From creating a std::thread to wrapping it in RAII — master the ownership semantics and lifecycle management of C++ threads"
translation:
  source: documents/vol5-concurrency/ch01-thread-lifecycle-raii/index.md
  source_hash: 653cc333414c1466b68241cbda51f3fc65232ca813b7124a040073521163c516
  translated_at: '2026-09-26T06:28:07+00:00'
  engine: anthropic
  token_count: 230
---

# Thread Lifecycle and RAII

In the previous chapter we looked at the low-level mechanics of the CPU cache and OS threads, and now we can finally write our first multithreaded program. But before typing `std::thread t(...)`, we need to get one thing straight: a thread is a **resource**. It occupies OS kernel objects, stack space, TLS storage, and more. Just like file handles and dynamic memory, a thread must be properly acquired and released — otherwise what awaits you is `std::terminate` and resource leaks.

In this chapter we start with the basic usage of `std::thread`, work our way through the pitfalls of passing arguments and the semantics of ownership transfer, and finally wrap all that complexity up with RAII. The goal is for multithreaded code to have the same clear ownership and deterministic resource release as single-threaded code.

## In This Chapter

<ChapterNav variant="sub">
  <ChapterLink href="01-std-thread">std::thread Basics</ChapterLink>
  <ChapterLink href="02-thread-arguments-and-lifetime">Thread Arguments and Lifetime</ChapterLink>
  <ChapterLink href="03-thread-ownership-and-raii">Thread Ownership and RAII</ChapterLink>
  <ChapterLink href="04-thread-local-and-call-once">thread_local and call_once</ChapterLink>
</ChapterNav>
