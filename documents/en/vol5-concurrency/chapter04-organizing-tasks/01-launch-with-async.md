---
title: "Running a Computation with std::async"
description: "Hand launching a computation and collecting its result to the library: async starts the task, future's get blocks until the result is ready, both values and exceptions come out of its hand, one get empties the future, and multiple consumers go through shared_future."
chapter: 5
order: 1
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
  source: documents/vol5-concurrency/chapter04-organizing-tasks/01-launch-with-async.md
  source_hash: 042a5d4150b6399f0b79ff89d2677b963de493e374c3fb94193c79e87387813f
  translated_at: '2026-11-22T13:29:16+00:00'
  engine: glm-5.3
  token_count: 13987
---

# Running a Computation with std::async

From this article on, the bridge can be handed to the library. `std::async` sends the computation out, `std::future` brings the result back — that goes for values, and it goes for failures too.

## Handing the Computation Over: async and future

Start from the smallest shape. `std::async` lives in the `<future>` header, since C++11 (per cppreference's labeling), and is a function template: give it a callable and a set of arguments, and it hands back a `std::future<R>`, where R is the task's return type ([futures.async]/5). The standard positions it in one sentence: it provides a mechanism to launch a function possibly on a new thread, with the function's result placed into a future object that shares a shared state with it ([futures.async]/1). The word "possibly" is the standard's own; an implementation is entitled not to start a thread at all — how that works unfolds in 4.2.

The **shared state** is the relay station of the whole mechanism, with a clause of its own in the standard. The definition is plain: a bit of status information plus one result, where the result may be a value or an exception ([futures.state]/1). When the state counts as ready is also written clearly: only when it holds a retrievable value or exception — ready is the standard's name for it ([futures.state]/8). The bridges we built ourselves over the first three units carried shared variables plus notifications on their deck; here all of it becomes a library-managed shared state, with who may put in and who may retrieve governed clause by clause.

> Two terms to learn here. The objects that put results into the shared state the standard calls **asynchronous providers** — async, promise, and packaged_task all belong here ([futures.state]/4). The future and shared_future that read the result are **asynchronous return objects** ([futures.state]/3). When the text below says "the putting side" and "the taking side", these are the ones.

Let's lay the code out. The task is still the old stand-in for real computation: sleep 300 ms and return a square. We let `main` stay busy after launching — doing five 10 ms chores of its own — then come for the result:

<OnlineCompilerDemo
  title="Launch, Do a Small Stretch of Work, get Waits the Result Back"
  source-path="code/examples/vol5/chapter04/01_demo_get.cpp"
  description="async launches square(21), main does five 10 ms chores, and get waits until the 300 ms result is ready before returning."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_get.cpp —— 发起、自己干一小段活、get 把结果等回来
#include <chrono>
#include <cstdio>
#include <future>
#include <thread>

using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

double ms_since(Clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

int square(int x) {
  std::this_thread::sleep_for(300ms);   // 顶替一段真实的计算
  return x * x;
}

int main() {
  auto t0 = Clock::now();
  std::future<int> result = std::async(square, 21);
  std::printf("main  : %5.1f ms 计算发起，future 在手\n", ms_since(t0));

  for (int i = 0; i < 5; ++i) {         // main 自己的一小段活
    std::this_thread::sleep_for(10ms);
  }
  std::printf("main  : %5.1f ms 自己的活干完，来取结果\n", ms_since(t0));

  int value = result.get();             // 结果没就绪就等，就绪才返回
  std::printf("main  : %5.1f ms get 返回，square(21) = %d\n", ms_since(t0), value);
  std::printf("main  : get 之后 valid() = %d\n", result.valid() ? 1 : 0);
  return 0;
}
```

</OnlineCompilerDemo>

```text
main  :   0.1 ms 计算发起，future 在手
main  :  51.3 ms 自己的活干完，来取结果
main  : 301.2 ms get 返回，square(21) = 441
main  : get 之后 valid() = 0
```

What `get` does, the standard writes as two steps: wait for the shared state to be ready, then retrieve the value stored in it ([futures.unique.future]/16). Retrieving comes with a side step — releasing the shared state — and `valid()` turns `false` accordingly (/16.2 and /17). From the standard's description we get one guarantee: when `get` returns, the result is in hand; until the work is done, it simply blocks. The 301.2 ms line in the output is that guarantee in action — `main`'s five chores took only 51 ms, and the rest of the time was `get` waiting on your behalf. A note on the return's shape: it is delivered by move (/18.1). With something as small as `int` you won't feel the difference; with a large `std::string`, the value is carried out of the shared state, with no second copy.

One more thing to say plainly: from the output above, you cannot tell whether the work ran alongside `main`'s own chores or only started when `get` was called. The readings cannot answer that question — both arrangements land around 300 ms. The demo didn't fail; unreadable is exactly right: the default spelling leaves the choice to the implementation, and 4.2 gives the answer. If you only want to wait without taking the value, `wait()` is also there ([futures.unique.future]/21); this article doesn't need it — knowing it exists is enough.

## Exceptions Ride the Same Train

3.3's failure scene established two conditions: a botched upstream must have somewhere to be recorded, and the waiting side must be able to read it. A flag bit was our hands-on answer at the time. `async` takes over the recording half: an exception thrown during the task's execution is, per the standard, stored in the shared state, dwelling in the same place as a value, under the name exceptional result ([futures.async]/3.1; the same holds when the deferred policy is chosen explicitly, /3.2). `future` sits on the taking side, and its `get` declares what it throws: the exception stored in the shared state ([futures.unique.future]/19). Put the two ends together and failure gets a road of its own: thrown in the task, re-thrown by `get` on your side — the very exception that was stored, type unchanged, message intact.

> Both future and shared_future ship reference and void specializations ([futures.unique.future]/5, [futures.shared.future]/5). The difference lies only in `get`'s return: the reference specialization hands back the stored reference, and the void specialization returns nothing — the waiting and exception semantics carry over unchanged. We follow the main template's main line; when we truly meet the two specializations, we'll get acquainted then.

The taking side must also have someone to catch the failure. We let `divide` throw `invalid_argument` on a zero divisor, with `main` doing its usual `try` plus `catch`:

<OnlineCompilerDemo
  title="An Exception Thrown in the Task, Re-thrown by get on the Calling Thread"
  source-path="code/examples/vol5/chapter04/02_demo_exception.cpp"
  description="divide throws invalid_argument on a zero divisor; what main's catch receives through get is the same exception, message intact, and with a normal divisor the value comes out of get as usual."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_exception.cpp —— 任务里抛的异常，get 在调用线程重抛
#include <cstdio>
#include <future>
#include <stdexcept>

int divide(int a, int b) {
  if (b == 0) {
    throw std::invalid_argument("除数为零，除法做不下去");
  }
  return a / b;
}

int main() {
  std::future<int> bad = std::async(divide, 6, 0);
  try {
    int value = bad.get();              // 这一行抛出任务里存下的那个异常
    std::printf("main: 拿到 %d\n", value);
  } catch (const std::invalid_argument& e) {
    std::printf("main: 捕获 invalid_argument，what() = %s\n", e.what());
  }

  std::future<int> good = std::async(divide, 6, 3);
  std::printf("main: divide(6, 3) = %d\n", good.get());
  return 0;
}
```

</OnlineCompilerDemo>

```text
main: 捕获 invalid_argument，what() = 除数为零，除法做不下去
main: divide(6, 3) = 2
```

The first output line is exactly what `get` threw: the `invalid_argument` thrown inside `divide` ran straight into `main`'s `catch`, message untouched. We wrote not one line of cross-thread error-code shuffling — the library built the bridge. The second line is the control: the work succeeded, and the value comes out of `get` as usual. In 3.3 it took a `failed` flag and a predicate to break up the scene; here, `try` plus `catch` catches the failure.

get is the exception's only exit; the other direction we must also see clearly: if nobody calls `get`, it stays parked in the shared state — printing nothing, terminating nothing — and is finally destroyed along with the shared state. Of 3.3's two conditions, the library has taken over the recording half; "readable" remains your business — you must actually come take it. As for the spelling that drops the future entirely, its destructor trails a special wait, which decides whether the program waits out the task for you when you keep no future, and whether the task's thrown exception will ever be seen by anyone — the rest belongs to 4.2.

This article hasn't touched the packing half: the entry point for packing an exception by hand is `std::promise`'s `set_exception`, whose home ground is 4.3.

## One-Shot Consumption: After One get, the future Is Empty

The name future easily suggests repeated reads. The standard's class definition shuts that notion down in its opening. It states: a future is a return object that does not share its shared state with any other asynchronous return object ([futures.unique.future]/1). In the class declaration, the copy constructor and copy assignment slots read `= delete` — try to duplicate one and the compiler refuses on the spot. One shared state, owned exclusively by one future.

Exclusivity is cashed in by `get`. Beyond waiting and retrieving, `get`'s effects include releasing the shared state ([futures.unique.future]/16), and the postcondition reads `valid() == false` (/17). Once you have taken the result, this future is disassociated from the shared state; calling `get` again or `wait` again gets no behavior from the standard. It puts it as a precondition: on a future whose `valid()` is `false`, calling any member other than the destructor, move assignment, `share`, and `valid` itself is undefined behavior (/3).

Undefined behavior cannot be demoed; what can be demoed is what the machine at hand did upon meeting it. The standard leaves a recommended practice at the same spot: implementations should detect such calls and throw a `std::future_error` with the error condition `no_state` (the Recommended practice at /3). I ran the probe:

<OnlineCompilerDemo
  title="A Future After One get — What Happens on a Second get"
  source-path="code/examples/vol5/chapter04/03_probe_double_get.cpp"
  description="The second get is undefined behavior at the standard level; the local libstdc++ follows the standard's recommended practice and throws future_error with the message No associated state."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// probe_double_get.cpp —— get 过一次的 future，再 get 一次会怎样
#include <cstdio>
#include <future>

int answer() { return 42; }

int main() {
  std::future<int> f = std::async(answer);
  int first = f.get();
  std::printf("第一回 get = %d，valid() = %d\n", first, f.valid() ? 1 : 0);
  try {
    std::printf("第二回 get = %d\n", f.get());   // 标准层面是未定义行为
  } catch (const std::exception& e) {
    std::printf("第二回 get 抛了异常：%s\n", e.what());
  }
  return 0;
}
```

</OnlineCompilerDemo>

```text
第一回 get = 42，valid() = 0
第二回 get 抛了异常：std::future_error: No associated state
```

The local libstdc++ followed the recommended practice, throwing `std::future_error` with the message printed in the output: No associated state. Throwing `future_error` is libstdc++'s own courtesy, not a standard requirement — a different implementation could equally take the process down. What our code can lean on is only the precondition itself, plus the inquiry tool it carries: `valid()` returns true if and only if the object is still associated with a shared state (/20); when unsure, ask it, and stop the moment it says `false`. future is also move-only: move construction transfers the shared state to the new object and the original's `valid()` turns `false` (/8, /9). To hand the result to someone else, move — the standard offers no copy route.

Put the two contrasting designs side by side. A copyable future, to let several people each hold a result, would need reference counting on the shared state — incremented by every arrival, decremented by every departure — with the maintenance following every call. The exclusive future carries no such burden: who took the result always has exactly one answer, so `get` dares to hand the value out by move and dares to release the shared state once taken. For the many-readers scene, the library stocked another tool, invited on stage in the next section.

## Several People Want the Result: shared_future

A scene we often meet: one result, several threads wanting it. future is exclusive and cannot be copied; force the same future to be moved to several people, and every earlier holder is left holding an empty shell. The library's answer is `std::shared_future`, also since C++11. Its class definition differs from future's by one word — an added "may": it may share its shared state with other asynchronous return objects ([futures.shared.future]/1). The copy constructor is a proper member, and every copy points at the same shared state (/8).

Two roads lead over. `future::share()` hands the shared state out and returns a `shared_future`, invalidating itself ([futures.unique.future]/13, /14). `shared_future` also has a converting constructor from a future rvalue ([futures.shared.future]/10), so a spelling like `std::shared_future<std::string> sf = std::async(load_config);` stands directly — verified with a probe:

<OnlineCompilerDemo
  title="A shared_future Constructed Straight from async's Return Value"
  source-path="code/examples/vol5/chapter04/04_probe_shared_from_async.cpp"
  description="Without share(), the future rvalue returned by async converts directly into a shared_future, and the same copy yields the result on two consecutive gets."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// probe_shared_from_async.cpp —— shared_future 直接从 async 的返回值构造
#include <chrono>
#include <cstdio>
#include <future>
#include <string>

using namespace std::chrono_literals;

std::string load_config() {
  std::this_thread::sleep_for(150ms);   // 顶替一次真实的读取
  return "port = 8080";
}

int main() {
  // 不经 share()，从 async 返回的 future 右值直接转换构造
  std::shared_future<std::string> sf = std::async(load_config);
  std::printf("main: 第一回 get 到 %s\n", sf.get().c_str());
  std::printf("main: 第二回 get 到 %s\n", sf.get().c_str());
  return 0;
}
```

</OnlineCompilerDemo>

Now the scene of three threads sharing one configuration, straight up. `main` launches the load, `share`s it out, three readers each take a copy, and each calls `get`:

<OnlineCompilerDemo
  title="One Configuration, Three Threads Want It: shared_future"
  source-path="code/examples/vol5/chapter04/05_demo_shared.cpp"
  description="After share, the original future's valid() reads 0; three readers each take a shared_future copy and get on their own, and at the end the same copy is read twice in a row."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_shared.cpp —— 一份配置，三条线程都要：shared_future
#include <chrono>
#include <cstdio>
#include <future>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

std::string load_config() {
  std::this_thread::sleep_for(150ms);   // 顶替一次真实的读取
  return "port = 8080";
}

int main() {
  std::future<std::string> one_shot = std::async(load_config);
  std::shared_future<std::string> config = one_shot.share();
  std::printf("main: share 之后，future 的 valid() = %d\n",
              one_shot.valid() ? 1 : 0);

  std::vector<std::thread> readers;
  for (int i = 0; i < 3; ++i) {
    readers.emplace_back([config, i] {          // 按值拷进线程，一人一份
      const std::string& line = config.get();   // 各自的拷贝，各自 get
      std::printf("reader %d: %s\n", i, line.c_str());
    });
  }
  for (auto& t : readers) t.join();

  std::printf("main: 第一回 get 到 %s\n", config.get().c_str());
  std::printf("main: 第二回 get 到 %s\n", config.get().c_str());
  return 0;
}
```

</OnlineCompilerDemo>

```text
main: share 之后，future 的 valid() = 0
reader 1: port = 8080
reader 0: port = 8080
reader 2: port = 8080
main: 第一回 get 到 port = 8080
main: 第二回 get 到 port = 8080
```

After `share`, the original future is instantly empty — `valid()` is 0 — but the result is not lost: it is now under `shared_future`'s management. The three readers each hold their own copy; whoever's copy `get` is called on waits for readiness. Once the 150 ms job finishes, the three readings come out together, order up to the scheduler — on my re-run it read 0, 1, 2. The last two lines are two consecutive gets on the same copy, and `shared_future` doesn't flinch. With future, this scene couldn't happen: emptied by one take, the second person gets nothing.

Collect the differences between `shared_future::get` and future's — two spots, both written out in the standard. It waits for readiness and takes the value but leaves the shared state untouched, so taking can repeat (/19). `get` hands back a const reference pointing at the value stored in the shared state — the value stays where it was — unlike future's handing over the value itself (/20.1). Exceptions still re-throw from it (/21). The standard's added note says it plainly: accessing through the reference after the shared state is destroyed is undefined behavior, so don't store the reference anywhere that outlives the `shared_future` itself (the note at /20.1). For long-term keeping, copy from the reference into your own variable.

May several threads read at once? One sentence from cppreference answers: it is safe for multiple threads to access the same shared state, each through its own copy. The standard's note draws the boundary too: `shared_future`'s member functions don't synchronize with each other, but they do synchronize with the shared state (the note at /2). Concurrent access to the value through the reference returned by `get` must be either read-only or given extra synchronization of your own (the note at [futures.state]/11). Just do as it says: one copy per person, reading the result. For writing, hire a mutex — that is Unit 2's turf.

## Your Turn to Try

Before putting pen to paper, I line up the questions this article has banked; walk them against your own code:

1. Between async's launch and get's return, what is your program doing in the gap? get blocks, and how long is decided by the work's length — can the call site afford the wait?
2. At every get call site, is someone catching the exception? Reported to whom after catching, and clearly?
3. On the same future, how many times does get run? In loops and branches that take repeatedly, which road does the second take follow? Where unsure, have you asked valid()?
4. One result wanted by several people — shared_future, or an async apiece? The launch-each spelling runs the same work several times over; is it worth it?
5. Has the reference returned by shared_future::get ever been stored somewhere that outlives it?

## Next Steps

Before turning the page, pull out a copy of that waiting program from 3.3 — the one with lock, flag, notify, and join all present — and try rewriting it as async plus get, with the goods waiter waits for becoming a future's result. Count the deleted lines when done: lock, flag, notify, join — which ones could go, which ones turned into library-built bridges; count them and you'll know.

## Choosing Between future and thread (Extension)

> This section is an extension; skipping it does not affect the main line. What remains is the work of launching asynchronously, and the scenes that suit async and thread respectively.

This volume has walked both roads. `std::thread` is Unit 1's craft, with all three burdens on you. How results travel is your arrangement — shared variables with a mutex, globals with atomics, or Unit 3's promise; any road works. Exceptions are your own business: an exception thrown in a child thread doesn't come back by itself; you must catch it on the spot and find a way to pass it to the one waiting. Thread management is in your hands too — `join` or `detach`, pick one, and forgetting to pick triggers `std::terminate` right in the destructor.

`std::async` takes these burdens over. Return values travel back automatically through future; exceptions are stored in the shared state automatically and re-thrown at `get`'s call site; the destructor of a discarded future even waits the task out for you — orphan threads get no footing. The price sits at the other end: the thread itself is out of your reach — no priority to set, no affinity to bind, no name to give — and on which thread the task actually runs, the standard even hedges its wording; 4.2 checked that passage.

How to choose — divide by scene. If what you're running is a computation with clear inputs and outputs, tasks independent of each other, exceptions needing propagation, and no care for which thread it lands on, choose `async`. Processing a batch of data in parallel, offloading a long computation from the main thread — both are its home field: throw out a task, take back a result. If you want a resident background thread — listening, an event loop — or must set thread attributes, choose `thread`. Priority, affinity, naming — only it can reach those settings, and the synchronization and error-passing code is yours to fill in. When short tasks grow many and launches come fast, choose neither — that is thread-pool territory: a set of pre-built worker threads plus a task queue, saving the repeated thread-creation cost with the concurrency in your grip. The standard library ships no thread pool; 4.4's task wrapper pads underneath its submit interface, and a complete one sits in the elective area, [A Teaching-Grade Thread Pool: A Five-Step Evolution](../cookbook/b-topics/04-teaching-threadpool).

One more boundary. `async` may truly start a new thread every time; with two-to-three-millisecond tasks launched dozens of times a second, the thread-creation cost swamps the work itself. In that scene `async` doesn't pay either — thread-pool territory again.

## Reference Resources

This article's experiment code totals five programs: four hang in the text, and the shared_future-from-async spelling was verified separately by the fifth probe, all collected in the examples directory introduced at the volume's front page. Compilation added `-Wall -Wextra`, with no warnings. In demo_get, the get-return reading lands around 300 ms; I re-ran once and read 300.5, the spare change being wakeup and scheduling. The three reader lines' order is the scheduler's — the two passes read 1, 0, 2 and 0, 1, 2 respectively. The structural results match every time: valid's flips, the exception's type and message, and shared_future's multi-way takes. The second get is undefined behavior at the standard level; the local libstdc++ follows the recommended practice and throws `std::future_error`, and another implementation may choose differently. Cited clause names match the official standard; paragraph numbers may drift. This time cppreference served the full async and shared_future pages, matching the draft with no discrepancies. Version labels follow its writing: async, future, shared_future are all C++11. The progression follows Anthony Williams's *C++ Concurrency in Action*; the examples and prose are my own.

- [std::async (cppreference)][cppref-async], since C++11; the default spelling is equivalent to the combination of async and deferred.
- [std::future (cppreference)][cppref-future], since C++11, move-only.
- [std::future::get (cppreference)][cppref-get], waits for readiness, delivers the value or re-throws; valid becomes false after the call.
- [std::shared_future (cppreference)][cppref-shared], since C++11, copyable; multiple threads each accessing the same shared state through their own copies is safe.
- [Function template async (current draft, futures.async)][draft-async], the default policy's definition, exceptions stored into the shared state, and the synchronization between launch and completion.
- [Class template future (current draft, futures.unique.future)][draft-future], get's effects and postconditions, the undefined behavior (and recommended practice) of calling other members when valid is false, and share's semantics.
- [Class template shared_future (current draft, futures.shared.future)][draft-shared], get returning a const reference with its lifetime note, conversion from future, and the member-synchronization note.
- [Shared state (current draft, futures.state)][draft-state], the definitions of shared state and ready, asynchronous providers and asynchronous return objects, the synchronization between storing a result and returning from a wait, and the note on concurrent access to the same result.
- [C++ Concurrency in Action, 2nd Edition (Anthony Williams)][williams], the reference for the progression.

[cppref-async]: https://en.cppreference.com/w/cpp/thread/async
[cppref-future]: https://en.cppreference.com/w/cpp/thread/future
[cppref-get]: https://en.cppreference.com/w/cpp/thread/future/get
[cppref-shared]: https://en.cppreference.com/w/cpp/thread/shared_future
[draft-async]: https://eel.is/c++draft/futures.async
[draft-future]: https://eel.is/c++draft/futures.unique.future
[draft-shared]: https://eel.is/c++draft/futures.shared.future
[draft-state]: https://eel.is/c++draft/futures.state
[williams]: https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition
