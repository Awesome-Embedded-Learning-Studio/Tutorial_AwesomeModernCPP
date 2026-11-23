---
title: "Registration Races and an Exit Checklist: The Stop Protocol's Fine Details"
description: "When stop_callback registration races with the request, the callback may execute in place; a destroyed callback has its own story. We finish with a checklist for auditing exit paths item by item."
chapter: 5
order: 7
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
difficulty: intermediate
platform: host
cpp_standard: [11, 20]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
translation:
  source: documents/vol5-concurrency/chapter01-start-concurrency/07-stop-callback-and-checklist.md
  source_hash: 6771386646e91fd3a2c92fd18674ca347a385c1392d56f780b59465bf055f023
  translated_at: '2026-11-22T13:28:59+00:00'
  engine: glm-5.3
  token_count: 7471
---
# Registration Races and an Exit Checklist: The Stop Protocol's Fine Details

The last article finished counting the trio's pieces; this one settles two fine accounts: the timing and races of callback registration, and an exit checklist worth walking through item by item before opening any thread.

## stop_callback's Registration Timing and Races

Registering a callback has two paths, and the standard writes them plainly. If `stop_requested()` is still false at the moment of registration, the callback enters the list and executes when the request arrives. If it is already true at that moment, the callback executes immediately on the registering thread and never enters the list. In the last article, Letting Work End in an Orderly Way, the callback registered last (named `late` in the code) took exactly that second path.

What happens when registration and the request race each other? And what if the request only arrives after the callback object is deregistered? We put the three cases A, B, and C into one program and run it:

<OnlineCompilerDemo
  title="stop_callback's registration race"
  source-path="code/examples/vol5/chapter01/45_registration_race.cpp"
  description="When registration and the request race, the callback may execute in place; the online sandbox caps the thread count, so it is narrowed to 50 rounds — the readings in the text come from a local 2000-round run."
  run-options="-O2 -std=c++23 -pthread"
  allow-run
/>

```text
A: 2000 rounds, 1999 invocations
B: live callback at request -> 2000/2000
C: request after deregistration -> 0/2000
```

Look at the output, noting these are one run's readings. A is registration racing the request, with the callback object living until just before the work function returns. B is the callback object living past the request. C is the callback object deregistered before the request. B shows one end of the rule: while the callback object is alive, it executes the moment the request arrives. C shows the other end: the request arrives a step late, the callback object is already gone, and it does not run even once.

A's number is raced out, following the scheduler. Run it repeatedly and A lands between 1997 and 2000, lower still under heavier load — so we treat it as an observation, not a proof. As long as the callback object isn't deregistered early, whether registration or the request is faster only decides where that one execution happens. Request before registration: the callback executes once, at construction. Registration before request, object not deregistered: it executes once when the request arrives. Deregistration before request: zero times. Put A, B, and C together, and the three endings are complete.

The synchronization relationship has a practical use here. Registration and that callback's execution are synchronized, so data written before registration is visible inside the callback. `request_stop()` returning true and `stop_requested()` returning true are likewise synchronized, so data written before the request is also visible to the thread that sees the request. An atomic flag under relaxed semantics guarantees only that bool's own reads and writes; for other data to be visible, you depend on memory-ordering agreements. The default seq_cst loads and stores carry synchronization of their own. On the stop-token side we needn't worry — visibility follows the protocol.

The fine print of destruction deserves its own look. A `std::stop_callback` deregisters itself on destruction. If, at the moment of deregistration, the callback is executing on another thread, the destructor blocks until it returns (the deregistration clause of [stoptoken.concepts], since C++20). If the callback is executing on the current thread — that is, the callback destroys its own callback object — the destructor does not block. Let's build a scene for the first situation: the callback is executing on another thread while the callback object is destroyed in the worker's hands.

<OnlineCompilerDemo
  title="Work inside the callback blocks request_stop"
  source-path="code/examples/vol5/chapter01/46_callback_blocks_request.cpp"
  description="The callback sleeps 200 milliseconds, and request_stop waits 200 milliseconds before returning — the callback runs on the requester's thread."
  run-options="-O2 -std=c++23 -pthread"
  allow-run
/>

```text
[main] request_stop at 20 ms
  [callback] starts at 20 ms
[worker] leaving the inner scope at 21 ms
  [callback] ends at 222 ms
[main] request_stop returned at 222 ms
[worker] past the destructor at 222 ms
```

The worker left the inner scope at 21 milliseconds, destroying that callback object. Yet the moment it printed "past the destructor" was 222 milliseconds. During those 200 milliseconds in between, the main thread was sleeping inside the callback. The worker's destructor waited for the callback to return before moving on, and the main thread's `request_stop()` was tied up for the same 200 milliseconds. Both ends were leashed by this one callback, because the callback ran on the main thread while the callback object died on the worker thread.

Two sentences to take away. Keep callbacks short: they run on `request_stop()`'s caller, and the longer they run, the more they drag out the other side's shutdown — they may even tie up another thread trying to destroy the callback object. Keep the callback object outside the worker's loop: one registration covers the whole job. Registering and deregistering inside the loop pokes the shared stop state on every iteration — purely wasted time.

For waiting while blocked, the standard has one piece of ready-made support: `std::condition_variable_any`'s wait has a set of overloads taking a `std::stop_token`. Its two extra abilities over `condition_variable`: wait can pair with any lock type, and the stop_token overloads exist only on it. For the duration of the call, it registers itself onto the stop request's notification relationship, and when the request arrives the wait returns immediately ([thread.condvarany.intwait], since C++20). Let's build a stoppable task queue with it:

<OnlineCompilerDemo
  title="TaskQueue: a wait that takes a stop token"
  source-path="code/examples/vol5/chapter01/47_taskqueue_stop_cv.cpp"
  description="condition_variable_any's token overloads: the moment the stop request arrives, the worker sleeping on the queue wakes at once."
  run-options="-O2 -std=c++23 -pthread"
  allow-run
/>

```text
[worker] processed 0
[worker] processed 1
[worker] processed 2
[main] requesting stop
[main] request_stop returned
[worker] wait returned false, stopping
```

The request arrives, the worker's wait immediately returns false, and the thread wakes and prints its exit. No timeout, no polling, no hand-written notify. That false still only says the predicate was false — the queue was empty. Whether a stop request was received is the outer loop condition's business. Don't treat the return value as a stop marker. This family of wait functions reports only the predicate's truth, not who did the waking — the same principle as the earlier `wait_for` observation.

Only `condition_variable_any` has these overloads; `condition_variable` does not. Write this wait with `std::condition_variable` and you get a no-matching-function error; the first lines we saw under GCC 16.2.1 were:

```text
error: no matching function for call to
'std::condition_variable::wait(std::unique_lock<std::mutex>&,
 std::stop_token&, main()::<lambda()>)'
note: candidate 1: 'template<class _Predicate> void
 std::condition_variable::wait(std::unique_lock<std::mutex>&, _Predicate)'
note: candidate expects 2 arguments, 3 provided
```

When all you hold is `condition_variable`, registering our own `stop_callback` to wake the wait gets it done too. The callback does the waking; the predicate folds `std::stop_token`'s `stop_requested()` in so the wait returns. `condition_variable::wait` has no return value, so after the wait returns, the loop condition checks the token again. The details of waiting and notification belong to the later unit on waiting. This is only needed when a blocked wait must be wakeable by a stop request; without it, a wait can only end by timeout or an external event.

## Auditing Exit Paths Item by Item

The last item of Unit 1's question chain is letting work end in an orderly way; the tools are all here now — what's missing is a checklist. Every item below is a question I have asked myself while writing concurrent programs; walk through them when you open a new thread. It's cheaper than investigating a deadlock afterwards.

1. Which line holds the exit condition? Does the loop condition include `std::stop_token`'s `stop_requested()`, and are there checkpoints inside the loop body? A loop whose iteration takes ten seconds, checked only at the loop head, still makes you wait ten seconds after the request arrives.
2. What wakes a blocked wait? `std::condition_variable_any` has overloads taking `std::stop_token`. Reads blocked on a file descriptor or the network do not — the standard library doesn't manage that for you; arrange it yourself by closing the descriptor, using timeouts, or a non-blocking design.
3. What happens to what's left in the queue at shutdown? Drop it, finish it, or hand it back to the caller — pick one, and the code must say it explicitly.
4. Who waits for whom, and in what order? jthread's destructor waits. Member declaration order dictates destruction order; when the outer object is destroyed, the threads it manages must already be able to exit.
5. Does the exception path take the same exit route? An exception thrown in the worker cannot escape — verified in A First std::thread Program — and throwing again during stop cleanup is terminate.
6. How long is the work done in a callback? It runs on `request_stop()`'s calling thread; the longer it is, the more it slows the other side's shutdown.
7. Is the object still usable after the stop? The request is one-way and irrevocable; tasks submitted afterwards (to the earlier `TaskQueue`'s `submit`, say) go unanswered — your interface should either refuse them or spell the behavior out.

Item 3 deserves a run, because many people assume a stop request drains the queue automatically. Let's try it with 10 tasks:

<OnlineCompilerDemo
  title="Counting remaining tasks at shutdown"
  source-path="code/examples/vol5/chapter01/48_taskqueue_drain_or_drop.cpp"
  description="After stop_requested, count what's left in the queue, drop it, and report — the ending path must be accounted for by you."
  run-options="-O2 -std=c++23 -pthread"
  allow-run
/>

```text
[worker] ran task 0
[worker] ran task 1
[main] request_stop
[worker] stop requested, 8 tasks left in the queue, dropping them
```

The main thread requested a stop at 60 milliseconds; the worker finished the task in hand before noticing the loop condition no longer held, and by then 8 remained in the queue. That `q_ = {}` line in the code is the decision I made: drop them. Choose finish-then-exit instead, and write the draining after the break, executing the tasks one by one. Choose hand-back-to-caller, and move the queue out. All three work — just write it clearly. Leave it unwritten, and you leave the next maintainer guessing.

Line up the tools from this and the previous articles side by side; the difference lies in who is responsible for the wind-down:

| Tool | What its destructor does | Stop request | Where it fits |
|------|--------------|----------|----------|
| `std::thread` | Calls `std::terminate()` if joinable | None | The wind-down is fully yours to control, missing nowhere |
| Hand-written `JoiningThread` (see Who Owns the Thread's Lifetime) | Joins only | None | C++11 through C++17, tasks that end on their own |
| `std::jthread` | Calls `request_stop()`, then joins | Yes, issued automatically in the destructor | C++20, tasks that must be stoppable |
| stop_source plus `std::thread` | Manual `request_stop()`, manual join | Yes, you issue it yourself | One-to-many, or when the requesting end (the stop_source) must be shared across components |

The last two rows are often used together. jthread manages one thread's wind-down; stop_source manages a unified stop for a group of threads. Which row you pick depends on whether the task needs to be stoppable.

## Next Steps

Unit 1 wraps up here. My own habit is to think the exit path through before opening a thread; if I can't think it through, I don't open it.

Next comes the unit on races, starting from broken programs. The stop protocol governs "when to stop"; it cannot govern "two threads reading and writing the same data at the same time." In A First std::thread Program's experiments we saw output tangle together — that was merely the visible chaos. The next unit looks at the invisible chaos, and why it may hand you a different answer on every run.

## Reference Resources

While writing this article I drew on one book, C++ Concurrency in Action by Anthony Williams, taking its chapter progression around threads and cooperative cancellation. The examples and prose are my own; the experiments were compiled and run on GCC 16.2.1. Claims about standard behavior were checked against cppreference and the working draft, with clause numbers attached to the entries.

- [std::jthread (cppreference)][cppref-jthread] — the auto-joining thread, since C++20.
- [std::jthread::~jthread (cppreference)][cppref-jthread-dtor] — requests a stop on destruction, then joins.
- [std::stop_source (cppreference)][cppref-source] — the writing end: issues the stop request and derives tokens.
- [std::stop_token (cppreference)][cppref-token] — the reading end: queries the request and the state.
- [std::stop_callback (cppreference)][cppref-callback] — registering and unregistering stop callbacks.
- [std::condition_variable_any::wait (cppreference)][cppref-cvany] — the interruptible wait that takes a stop_token.
- [C++ working draft: jthread construction and destruction (thread.jthread.cons)][draft-jthread] — the two constructor call forms and the destructor's ordering.
- [C++ working draft: stop-token concepts clause (stoptoken.concepts)][draft-stop] — registration, unregistration, synchronization relationships, and the semantics of request_stop.
- [C++ working draft: interruptible waits (thread.condvarany.intwait)][draft-cvany] — the rule for registering stop notification during a wait.

[cppref-jthread]: https://en.cppreference.com/w/cpp/thread/jthread
[cppref-jthread-dtor]: https://en.cppreference.com/w/cpp/thread/jthread/~jthread
[cppref-source]: https://en.cppreference.com/w/cpp/thread/stop_source
[cppref-token]: https://en.cppreference.com/w/cpp/thread/stop_token
[cppref-callback]: https://en.cppreference.com/w/cpp/thread/stop_callback
[cppref-cvany]: https://en.cppreference.com/w/cpp/thread/condition_variable_any/wait
[draft-jthread]: https://eel.is/c++draft/thread.jthread.cons
[draft-stop]: https://eel.is/c++draft/stoptoken.concepts
[draft-cvany]: https://eel.is/c++draft/thread.condvarany.intwait
