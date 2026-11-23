---
translation:
  source: documents/vol5-concurrency/cookbook/index.md
  source_hash: 64ff4d06690a1059842d35655ab5cb40d93cc0714652c21e39bbc849d66c4754
  translated_at: '2026-11-22T13:29:27+00:00'
  engine: glm-5.3
  token_count: 721
---
# cookbook: The Volume 5 Quick-Reference Section

The main text teaches concurrent program design itself; concepts, mechanisms, design, and verification all sit on the required line of Units 1 through 8. The cookbook is a different book, for readers who already understand concurrency concepts and want to know how to write them in C++: you are not here to learn concepts, but to look up the precise usage of an API while writing code and, along the way, dodge the common wrong ways. So the recipes are short and direct — read one and paste it into your project to try.

Section A follows the recipe-format conventions strictly: each recipe poses one question plus one piece of minimal code, every piece of code is compiled and run (each entry notes the compiler version and standard flag), followed by key points and common mistakes, ending with a link back to the corresponding chapter in the main text. Recipes are lookup documents, not tutorials; mechanism explanations belong to the main text, and recipes give only usage, boundaries, and error shapes — dig deeper by following the links.

There is exactly one link-back convention: every recipe ends with a link to the corresponding chapter in the main text, the chapter title given in full and the position named down to the section. If that chapter left an IOU saying "the cookbook covers this", we pick up that baton once the recipe is written.

Section A holds the API recipes, 10 pieces, at [a-API/](a-API/). The mutex family and RAII locks, the recursive and timed variants, call_once, shared_mutex, multi-lock tools, the jthread stop protocol, atomic_wait, atomic_ref, atomic_signal_fence, and shared_future — look them up by name.

Section B holds the topical readings, full articles moved in from beyond the required scope, in nine directions: thread-safe container strategies, lock-free introduction, SPSC and MPMC lock-free queues, a teaching-grade thread pool, SeqLock and spinlocks, asynchronous I/O and event loops, a coroutine echo server, the Actor model, and Channel with CSP. All nine live in [b-topics/](b-topics/) — click in and read.
