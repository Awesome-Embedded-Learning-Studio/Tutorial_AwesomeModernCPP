---
title: "Passing Results Yourself: `std::promise` and `std::future`"
description: "promise puts the supplying end in your hands: get_future pairs the two, set_value supplies the value, set_exception packs the exception for a verbatim re-throw at get, broken_promise cleans up unfulfilled scenes, and multi-stage relays plus shared_future broadcasts draw the result's route map."
chapter: 5
order: 3
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
difficulty: intermediate
platform: host
cpp_standard: [11, 14]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
translation:
  source: documents/vol5-concurrency/chapter04-organizing-tasks/03-promise-and-future.md
  source_hash: c82ed139f6041a028178b6ca62994d76e87b6c5d644123dc5ea9441abfab4997
  translated_at: '2026-11-22T13:29:16+00:00'
  engine: glm-5.3
  token_count: 14682
---

# Passing Results Yourself: `std::promise` and `std::future`

The contrast with async stands on one sentence: async manages both ends for you — launch the task, then just wait. promise puts the supplying end in your hands: when to give the value, what to give, and what to pack on failure are all your arrangements. The execution end, in turn, grows freer — start a thread or don't; the experiments below will show both.

## Who Executes the Work, Who Supplies the Result

Know the division of labor by name. The objects that put results into the shared state the standard calls asynchronous providers — introduced in 4.1 — with async, promise, and packaged_task all in the provider column; [futures.state]/4's example sentence literally lists promises and tasks. future and shared_future are asynchronous return objects, handling the reading. promise and future pair up through `get_future`, the two ends hanging off the same shared state. The bridge you built by hand over the first three units was the foundation shape — only back then the lock, the flag, and the notify were all yours to make; now the library does the underlaying.

Let's assemble the smallest pair. main builds a promise, takes the future via `get_future` and keeps it, moves the promise with `std::move` into the working thread; the thread finishes its work and calls `set_value` to supply the result, and main's `get` waits for the value and returns:

<OnlineCompilerDemo
  title="promise Supplies, future Takes: Both Ends in Our Hands"
  source-path="code/examples/vol5/chapter04/13_demo_pair.cpp"
  description="The worker thread counts the evens and supplies them via set_value; main's get waits for the value and returns. In the second half the promise never moves to a thread — supplied on the spot, taken on the spot, with no second thread the whole way."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_pair.cpp —— promise 供值，future 取值：两端都在咱们手里
#include <cstdio>
#include <future>
#include <thread>
#include <vector>

int count_even(const std::vector<int>& xs) {
  int n = 0;
  for (int x : xs) {
    if (x % 2 == 0) ++n;
  }
  return n;
}

void worker(std::promise<int> out) {
  std::vector<int> xs{7, 2, 9, 4, 6, 1, 8, 3};
  out.set_value(count_even(xs));   // 干完活，把结果供进 promise
}

int main() {
  std::promise<int> p;
  std::future<int> f = p.get_future();   // 供值端移动给线程，future 留在 main
  std::thread t(worker, std::move(p));

  int n = f.get();
  std::printf("main: get 到 %d 个偶数，valid() = %d\n", n, f.valid() ? 1 : 0);
  t.join();

  // 供值端不开线程也能用：当场供、当场取
  std::promise<int> here;
  std::future<int> fh = here.get_future();
  here.set_value(42);
  std::printf("main: 当场供的值 %d，全程没动过一条线程\n", fh.get());
  return 0;
}
```

</OnlineCompilerDemo>

```text
main: get 到 4 个偶数，valid() = 0
main: 当场供的值 42，全程没动过一条线程
```

Read the division of labor against the output. The work is executed by the thread you started yourself — Unit 1's craft in unchanged use; starting and joining the thread are still your business. The result is supplied by the promise clenched in the thread function's hand, which packs the even-count into the shared state. The waiting-and-taking happens on the future in main's hand; `get` blocks until ready, and after returning, `valid()` flips to 0 — the one-shot consumption covered in 4.1 still holds. Three jobs to three roles, and who does what reads at a glance in the code.

That the supplying end carries no thread deserves its own paragraph. In the demo's second half, the promise is moved to nobody: main calls `set_value` on the spot and `get` on the spot, with no second thread the whole way. No need to overthink it — promise and future are simply the two ends of one shared state; supplying and taking are each end's own capability, while who executes the work is an externally attached arrangement. In the async spelling, both ends plus the execution are handled by the library alone. In the promise spelling, the supplying end is handed to you, and the execution end you schedule as you please.

Both ends' quotas are watched by the standard. `get_future` once, `set_value` once; exceed either and a `future_error` flies. I ran the probe:

<OnlineCompilerDemo
  title="get_future and set_value Each Get a One-Shot Quota"
  source-path="code/examples/vol5/chapter04/14_probe_rules.cpp"
  description="The second get_future throws future_already_retrieved, the second set_value throws promise_already_satisfied — both of libstdc++'s messages state the cause."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// probe_rules.cpp —— get_future 与 set_value 都只有一回的额度
#include <cstdio>
#include <future>

int main() {
  std::promise<int> p;
  std::future<int> f = p.get_future();
  (void)f;
  try {
    std::future<int> again = p.get_future();   // 第二回
    (void)again;
  } catch (const std::future_error& e) {
    std::printf("第二回 get_future 抛 future_error：%s（future_already_retrieved？%s）\n",
                e.what(),
                e.code() == std::future_errc::future_already_retrieved ? "是" : "否");
  }

  p.set_value(1);
  try {
    p.set_value(2);                            // 第二回
  } catch (const std::future_error& e) {
    std::printf("第二回 set_value 抛 future_error：%s（promise_already_satisfied？%s）\n",
                e.what(),
                e.code() == std::future_errc::promise_already_satisfied ? "是" : "否");
  }
  return 0;
}
```

</OnlineCompilerDemo>

```text
第二回 get_future 抛 future_error：std::future_error: Future already retrieved（future_already_retrieved？是）
第二回 set_value 抛 future_error：std::future_error: Promise already satisfied（promise_already_satisfied？是）
```

The clauses are clear; let's check them one by one. `get_future` returns a future sharing the same shared state with the promise ([futures.promise]/13). Called again after a future has already been taken from the same state, it throws `future_error` with the error condition `future_already_retrieved` (/14, /15.1). `set_value` atomically stores the value into the shared state and makes it ready (/16); storing again when the state already holds a value or exception gives `promise_already_satisfied` (/18.1). The local libstdc++ threw at both spots per the recommended practice, with messages stating the cause, as printed in the output. The quotas are perfectly symmetric: one supply per end, one take per end, and together the two ends walk exactly one result's road. A pairing freedom in passing: `get_future` and `set_value` don't constitute a data race with each other ([futures.promise]/12), so taking the future a little earlier or later is fine.

promise is also a move-only type. The copy constructor is marked `= delete` in the class definition ([futures.promise]'s class definition); move construction transfers ownership of the shared state (/5), and the moved-from side holds no shared state (/6). The first time I passed one to a thread, I forgot the `std::move`, and the compiler laid the deleted copy constructor right before my eyes — no fudging possible. Calling `set_value` on an emptied promise throws `future_error` with the error condition `no_state` (/18.2). Authority transfers with the move, same temperament as 4.1's future: the supplying authority lives entirely in one hand, which is what gives "has it been supplied?" a clean answer.

## The Delivery Path for Failure

You remember 3.6's two conditions: a botched upstream must be recorded somewhere, and the waiting side must be able to read it. The answers back then were all handmade — a `failed` flag with a predicate, and `main` raising the flag on the quitter's behalf. The library's proper road takes over the recording side, with `set_exception` as the entrance: the exception object travels from the working thread into the shared state and is re-thrown verbatim at `get`'s call site.

Walk the path once. The working thread `throw`s, its own `catch (...)` catches, `std::current_exception()` packs the exception in hand into an `exception_ptr`, and that goes to `set_exception`, whose clause-written effect is to store it atomically into the shared state and make the state ready ([futures.promise]/20). The waiting side's `get` waits for readiness and re-throws the stored exception — the very exception that went in ([futures.unique.future]/19, the clause verified in 4.1, reused here). Type and message unchanged the whole way; verbatim is what the standard guarantees.

We keep division as the target, reusing 4.1's exception message so the two articles' outputs can be matched:

<OnlineCompilerDemo
  title="set_exception: Failure Travels from the Working Thread into get's Hand"
  source-path="code/examples/vol5/chapter04/15_demo_exception.cpp"
  description="The invalid_argument thrown by the bad division in the worker is packed into the shared state via current_exception and re-thrown verbatim at get's call site, message word for word; the good division supplies its value as usual."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_exception.cpp —— set_exception：失败从工作线程走到 get 手里
#include <cstdio>
#include <exception>
#include <future>
#include <stdexcept>
#include <thread>

void divide(std::promise<int> out, int a, int b) {
  try {
    if (b == 0) {
      throw std::invalid_argument("除数为零，除法做不下去");
    }
    out.set_value(a / b);
  } catch (...) {
    out.set_exception(std::current_exception());   // 手里的异常原样装进共享状态
  }
}

void run(const char* tag, int a, int b) {
  std::promise<int> p;
  std::future<int> f = p.get_future();
  std::thread t(divide, std::move(p), a, b);
  try {
    std::printf("%s: %d / %d = %d\n", tag, a, b, f.get());
  } catch (const std::invalid_argument& e) {
    std::printf("%s: 捕获 invalid_argument，what() = %s\n", tag, e.what());
  }
  t.join();
}

int main() {
  run("坏除法", 6, 0);
  run("好除法", 6, 3);
  return 0;
}
```

</OnlineCompilerDemo>

```text
坏除法: 捕获 invalid_argument，what() = 除数为零，除法做不下去
好除法: 6 / 3 = 2
```

Both output lines happen at `get`'s call site. Bad division: the `invalid_argument` thrown in `divide` ran into main's `catch`, type matching, message word for word — the same road async walked in 4.1, only the supplying hand has changed from the library to you. Good division as usual: the value `set_value` supplied comes out of `get`. What we wrote by hand is two lines: one `catch (...)`, one `set_exception(std::current_exception())`. The whole handmade bridge of 3.6 — lock, flag, predicate, notify — retires here.

A few boundaries to mind. The pointer packed into `set_exception` must not be null — the clause writes it as a precondition: p non-null ([futures.promise]/19); `current_exception()` inside a `catch` gives exactly a true pointer, and writing it the way above won't miss. Value and exception share the same quota: calling `set_value` when the state already holds an exception, or the reverse, is `promise_already_satisfied` — /18.1's wording is "value or exception"; the probe verified the `set_value` case, and the `set_exception` case's same ruling is written at /22.1. One more direction to see clearly: an exception stored but never taken stays parked in the shared state and is destroyed with it. As said for the async version in 4.1: recording happens on the library's side, reading is forever on yours. The promise version holds unchanged.

For scenes that can never be fulfilled, the library arranges the cleanup too. When the providing side is destroyed while the state is not yet ready, the standard calls it **abandon**: it stores a `future_error` with the error condition `broken_promise` into the state and makes the state ready ([futures.state]/7). The waiting side's `get` immediately receives a clear error instead of hanging for someone who will never come. The consequence was demonstrated in 4.2's extension — `get` throwing `future_error`, the condition comparison coming out `broken_promise` — with the readings in 4.2's text, so we won't restage it. On the failure path it holds a real job: when the working side forgets to supply before leaving, the waiting side receives a complaint it can trace.

> `set_value_at_thread_exit` and `set_exception_at_thread_exit` are two variants: packing the result happens on the spot, but the state's readiness is deferred until after the thread exits and its thread-local objects are destroyed ([futures.promise]/23, /27, [futures.state]/10). They only take the stage where cleanup order matters; the main line doesn't need them — just make their acquaintance.

## Organizational Patterns for Result Delivery

With the supplying end in hand, the result's route map is yours to draw.

### Multi-Stage Handoff

Let's lay the chain's shape straight out. Stage A supplies a set of data; stage B takes A's future in one hand and supplies C's promise with the other; stage C closes with a print:

<OnlineCompilerDemo
  title="A Result Delivery Chain: A Supplies Data, B Sums It, C Prints at the End"
  source-path="code/examples/vol5/chapter04/16_demo_chain.cpp"
  description="Three threads, two shared states: B's get waits on A by birth, C's get waits on B by birth, and main only starts and joins them all — no lock or notify ever takes the stage."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_chain.cpp —— 结果传递链：A 供数据，B 接上算总和，C 收尾打印
#include <cstdio>
#include <future>
#include <numeric>
#include <thread>
#include <vector>

void stage_a(std::promise<std::vector<int>> out) {
  out.set_value(std::vector<int>{3, 1, 4, 1, 5, 9, 2, 6});
}

void stage_b(std::future<std::vector<int>> in, std::promise<int> out) {
  std::vector<int> xs = in.get();   // 一手接上游的 future
  out.set_value(std::accumulate(xs.begin(), xs.end(), 0));   // 一手供下游的 promise
}

void stage_c(std::future<int> in) {
  std::printf("stage C: 收到总和 %d\n", in.get());
}

int main() {
  std::promise<std::vector<int>> ab;
  std::future<std::vector<int>> f_ab = ab.get_future();
  std::promise<int> bc;
  std::future<int> f_bc = bc.get_future();

  std::thread ta(stage_a, std::move(ab));
  std::thread tb(stage_b, std::move(f_ab), std::move(bc));
  std::thread tc(stage_c, std::move(f_bc));
  ta.join();
  tb.join();
  tc.join();
  return 0;
}
```

</OnlineCompilerDemo>

```text
stage C: 收到总和 31
```

The middle B is the chain's pivot, written plainly in its parameter list: `std::future<std::vector<int>>` in, `std::promise<int>` out. Its `get` awaits A's goods, and the finished sum goes into its own promise. You don't schedule the waiting order — B's `get` is born waiting on A, C's `get` is born waiting on B, and `main` merely starts all three threads and joins all three. Unit 3's locks, predicates, and notifies never appear; the synchronization guarantee sits in the clauses: the operation storing a result into the shared state synchronizes with the successful return of a waiting function on that state ([futures.state]/9). The value A stores is visible to B, the value B stores is visible to C, and the very thing in charge is the shared state met in 4.1. Wait — whether the whole chain can hand off in order depends precisely on it!

Failure travels the chain too. If B's `get` receives the exception A packed in, you have two roads: catch and handle in place, or pack it downstream verbatim, carrying over the `catch (...)` plus `set_exception(std::current_exception())` posture. Which road is the chain's design decision; the only thing that cannot be cut is the waiting side's `catch` — whoever the exception reaches must be able to catch it.

Once the stages multiply, the hand-built chain's costs show. Every stage is a promise plus a future, with value-packing and exception-packing all handwritten — you'll start missing a shell that takes over the chores. That is exactly what `std::packaged_task` does; this article won't unfold it — see 4.4.

### One Supply, Many Takers

4.1 covered shared_future's taking rules: copyable, `get` hands out a const reference, the value remains after taking, exceptions re-throw as usual. The supplying end there was an async task; swap in the promise in your hand and the scene completes instantly: one supply, many takers, with the supplying moment yours to set.

<OnlineCompilerDemo
  title="One Supply, Many Takers: promise Paired with shared_future"
  source-path="code/examples/vol5/chapter04/17_demo_broadcast.cpp"
  description="Three workers each clasp a shared_future copy and get; the promise stays in main's hand, and the moment set_value lands, the three waiters wake together. The three worker lines' order is up to the scheduler."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_broadcast.cpp —— 一处供值，多处取用：promise 配 shared_future
#include <chrono>
#include <cstdio>
#include <future>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

int main() {
  std::promise<std::string> ready;
  std::shared_future<std::string> config = ready.get_future().share();

  std::vector<std::thread> pool;
  for (int i = 0; i < 3; ++i) {
    pool.emplace_back([config, i] {            // 每条线程一份自己的拷贝
      const std::string& line = config.get();  // 没就绪就等，就绪了一起醒
      std::printf("worker %d: 读到 %s\n", i, line.c_str());
    });
  }

  std::this_thread::sleep_for(150ms);   // 顶替一段准备工作
  ready.set_value("pool_size = 4");     // 供值端在 main 手里，什么时候给由咱们定
  for (auto& t : pool) t.join();
  return 0;
}
```

</OnlineCompilerDemo>

```text
worker 0: 读到 pool_size = 4
worker 2: 读到 pool_size = 4
worker 1: 读到 pool_size = 4
```

main starts three workers, each clasping a shared_future copy and calling `get`, waiting until readiness. The promise stays in main's hand; a 150 ms sleep stands in for preparation work, and the moment `set_value` lands, the three waiters wake together, each receiving `pool_size = 4`. The three worker lines' order is the scheduler's; I re-ran three times, reading 0, 1, 2 and 0, 2, 1 — the three lines always all present, since they all block until `set_value` lets them through.

Set 4.1's version beside it and the difference lands on the supplying end. In the async version, when the result becomes ready is decided by the task's working time — `main` can only wait. In the promise version, the supplying end is clenched in your hand; when to give and what to give are your arrangements. `promise<void>` is more blunt still: `set_value()` takes no argument at all — a pure starting gun; the main template and the void specialization differ only in the parameters of `set_value` and `set_value_at_thread_exit` ([futures.promise]/2). Config ready, initialization done, the go signal — any one-shot broadcast scene suits it. Don't press it into a repeatedly-supplying channel: the quota exhausts and it's spent; repeated work belongs to condition variables and queues, and Unit 3 is waiting for you.

### Drawing the Boundary with the Previous Two Articles

With the unit this far, the territories of execution are drawn. `std::thread` governs the thread itself — starting, waiting, stopping all live in Unit 1's craft. async governs both ends — launch is execution, supplying and exception-passing are taken over by the library, and when it runs belongs to 4.2's policies. promise governs only the supplying end, handing you the whole authority to pack results into the shared state, with the execution end as you arrange. The three tools each keep their own jurisdiction; mixed into one stretch of code they don't fight, because each interface manages only its own end.

packaged_task's whereabouts are also settled. It stands on promise's shoulders, automating your handwritten `try`, `catch`, `set_value`, `set_exception`, turning the task into an object that can sit in containers and be handed to others — the base plate of thread pools is exactly it. It is the protagonist of 4.4, *Wrapping a Computation into a Storable, Dispatchable Task*; the dedicated article is ready — it is the [next article](./04-packaged-task). The source article in the old ch05 directory squeezed it into the same piece as promise; after the migration each went to its own section. The thread-pool article moved wholesale into the cookbook electives, out of the main text, and [A Teaching-Grade Thread Pool: A Five-Step Evolution](../cookbook/b-topics/04-teaching-threadpool) is that complete version. So this article's main line closes on promise and future — one end supplying, one end taking — and the layout is complete here.

The destructor temperaments also fold back into origins. In 4.2's experiment, a promise's future destructed in 0.0 ms, waiting on nobody — and that foreshadowing pays off here: a shared state built by promise has no thread object to take back, so it lets go and goes, leaving only a `broken_promise` word of explanation when nobody fulfilled. async's state houses a thread and must wait out the task before collecting it; promise's state holds only a result and waits for nothing. You have now met both origins.

## Your Turn to Try

Before putting pen to paper, I line up this article's banked questions; walk them against your own code:

1. In whose hand is the supplying end? Before the working side leaves, is someone certain to call `set_value` or `set_exception`? If nobody does, the waiting side receives `broken_promise` — do you plan to report it as an error, or as a normal stop in the flow?
2. Is the exception-packing posture right? `catch (...)` plus `std::current_exception()` — and is the receiving side's `catch` type specific enough to match `what()`?
3. Both `get_future` and `set_value` get one shot — is there any path in the code that could reach a second? Check branches and loops especially.
4. At each chain stage, is the upstream exception handled in place or packed downstream? At the chain's end, somebody must catch it — have you found them?
5. For one-supply-many-takers scenes, are you using shared_future or an async apiece? The latter runs the same work several times over; is it worth it?

Pull out a copy of the waiting program from 3.6, *[A Wait Also Needs a Way to End](../chapter03-wait-and-coordinate/06-ending-a-wait)*, the one that disbanded via a `failed` flag, and reroute its failure leg through promise: the upstream catches the exception, `set_exception` packs it into the shared state, and the waiting side switches to `try` plus `catch`. When done, count side by side: flag, lock, predicate — which ones left the stage, and did the exception's type and message reach the catcher verbatim? Count them and you'll know how far down the proper road 3.6's parting words have been built.

## Reference Resources

While writing this article I followed the progression of Anthony Williams's *C++ Concurrency in Action*; its arrangement of promise and packaged_task in adjacent chapters informed this article's scoping, while the boundary pushing packaged_task to 4.4 was the established division of work. The examples and prose are my own; compilation added `-Wall -Wextra`, with no warnings. The two `future_error` messages in the probe come from the local implementation; other implementations may word them differently — the error-condition comparison is what the standard guarantees. In demo_broadcast, the three worker lines' order is the scheduler's; I ran it thirteen times in total — the first three cited in the text read 0, 1, 2 and 0, 2, 1, while the ten added during revision all read 0, 1, 2; the three lines always block until `set_value` releases them, and worker 0 led in all thirteen runs, a local observation only. Clause citations put the clause name in brackets and the paragraph after the slash; clause names match the official standard, and paragraph numbers may drift. cppreference's promise and future_errc pages were fetched in full and read against the draft with no discrepancies. shared_future's combination clauses were checked against the draft's [futures.shared.future]; the taking rules verified in 4.1 were not re-verified. The clause on `future::get` re-throwing carries over 4.1's verification. The old ch05 directory's *promise and packaged_task* is this article's source material; examples, experiments, and clause checks were done fresh, and the packaged_task portion is left for 4.4.

- [std::promise (cppreference)][cppref-promise], since C++11, move-only, the supplying end, and the synchronization between storing a value and returning from a wait.
- [std::future_errc (cppreference)][cppref-errc], the four error codes: broken_promise, future_already_retrieved, promise_already_satisfied, no_state.
- [Class template promise (current draft, futures.promise)][draft-promise], the class definition and move-only nature, get_future's one-shot quota, the effects and error conditions of set_value and set_exception, and the two at_thread_exit variants.
- [Shared state (current draft, futures.state)][draft-state], the asynchronous-provider definition, abandon and broken_promise, and the synchronization between storing a result and returning from a wait.
- [Class template shared_future (current draft, futures.shared.future)][draft-shared], copies sharing the same state, conversion from future, and get handing out a const reference with its lifetime note.
- [Class template future (current draft, futures.unique.future)][draft-unique], the clause on get re-throwing, verified in 4.1 and reused here.
- [C++ Concurrency in Action, 2nd Edition (Anthony Williams)][williams], the reference for the progression.

[cppref-promise]: https://en.cppreference.com/w/cpp/thread/promise
[cppref-errc]: https://en.cppreference.com/w/cpp/thread/future_errc
[draft-promise]: https://eel.is/c++draft/futures.promise
[draft-state]: https://eel.is/c++draft/futures.state
[draft-shared]: https://eel.is/c++draft/futures.shared.future
[draft-unique]: https://eel.is/c++draft/futures.unique.future
[williams]: https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition
