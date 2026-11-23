---
translation:
  source: documents/vol5-concurrency/cookbook/a-API/index.md
  source_hash: 63e85c7a8c3f349b5ebdebc25c751497ce1db4e9543995c5f8f1a92c246874e4
  translated_at: '2026-11-22T13:29:27+00:00'
  engine: glm-5.3
  token_count: 2100
---
# Cookbook Section A: API Recipes

A quick-reference section organized by API: each recipe covers one question, one piece of minimal code, key points, and common mistakes, ending with a link back to the corresponding chapter in the main text. The reader profile and the full conventions for the recipe format live in the cookbook's positioning statement: [positioning statement](../index). Recipes are lookup documents; mechanism explanations belong to the main text, and you follow the links from here.

| No. | Recipe | Origin | Back to the Main Text |
|------|------|------|----------|
| 01 | [Choosing Between mutex and RAII Locks](01-mutex-and-raii) | Old article "mutex and RAII Locks" (ch02/01) | [Recovering the Lost Numbers — std::mutex and the Three Scoped Locks](../../chapter02-sharing-data/02-mutex-and-locks) |
| 02 | [recursive_mutex and timed_mutex](02-recursive-and-timed-mutex) | Split from the old article "mutex and RAII Locks" into a standalone piece (ch02/01) | [Recovering the Lost Numbers — std::mutex and the Three Scoped Locks](../../chapter02-sharing-data/02-mutex-and-locks) |
| 03 | [call_once and once_flag, and the Trade-off with the Meyers Singleton](03-call-once) | Split from the old article "thread_local and call_once" into a standalone piece (ch01/04) | [The Four Levels of Memory Ordering: From Sequential Consistency to relaxed](../../chapter05-atomic-memory-model/04-choosing-memory-orders) |
| 04 | [shared_mutex Read-Write Locks](04-shared-mutex) | Old article "Read-Write Locks and shared_mutex" (ch02/04) | [Waiting for a Condition: From Polling to Condition Variables](../../chapter03-wait-and-coordinate/01-wait-for-a-condition) |
| 05 | [Multi-Lock Acquisition Tools: std::lock, try_lock Fallback, and Hierarchical Locks](05-multi-lock) | Split from the old article "Deadlock and Lock Ordering" into a standalone piece (ch02/02) | [Nobody Lets Go — Now What: The Scene and Defenses of Deadlock](../../chapter02-sharing-data/06-deadlock-defenses) |
| 06 | [jthread and the Stop Protocol: A Quick Reference](06-jthread-and-stopping) | Old article "jthread and Stop Tokens" (ch05/03) | [Letting Work End in Order: std::jthread and Cooperative Cancellation](../../chapter01-start-concurrency/06-jthread-and-stopping) |
| 07 | [atomic_wait and atomic_notify](07-atomic-wait-notify) | Split from the old article "atomic_wait and atomic_ref" into a standalone piece (ch03/04) | [How Far Atomic Operations Take You](../../chapter05-atomic-memory-model/01-what-atomics-solve) |
| 08 | [atomic_ref: Atomic Operations on Non-Atomic Variables](08-atomic-ref) | Split from the old article "atomic_wait and atomic_ref" into a standalone piece (ch03/04) | [How Far Atomic Operations Take You](../../chapter05-atomic-memory-model/01-what-atomics-solve) |
| 09 | [atomic_signal_fence](09-atomic-signal-fence) | Split from the old article "Fences and Compiler Barriers" into a standalone piece (ch03/03) | [The Four Levels of Memory Ordering: From Sequential Consistency to relaxed](../../chapter05-atomic-memory-model/04-choosing-memory-orders) |
| 10 | [shared_future](10-shared-future) | Split from the old article "promise and packaged_task" into a standalone piece (ch05/02) | [Running Computations with std::async](../../chapter04-organizing-tasks/01-launch-with-async) |

Every piece of code in these recipes was compiled and run by us in `/tmp/concurrency-cookbook/` (GCC 16.2.1, `g++ -std=c++20 -O2 -pthread`); the outputs appear in each piece. The topical readings section (Section B) is at [../b-topics/](../b-topics/).
