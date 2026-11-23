---
title: "When Does the Task Run: Launch Policies and Waiting Behavior"
description: "Where async, deferred, and the default policy each schedule the task to run; a deferred task never executes unless invoked, wait_for merely spins, and an async future's destructor waits out the task — with the reason landing on the shared state's reference counting."
chapter: 5
order: 2
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
  source: documents/vol5-concurrency/chapter04-organizing-tasks/02-launch-policies.md
  source_hash: 11e7e138c51cf3fc0a3bb23f745dfd93dbbc786bb5fa3f2c59da6c0402f20354
  translated_at: '2026-11-22T13:29:16+00:00'
  engine: glm-5.3
  token_count: 20837
---

# When Does the Task Run: Launch Policies and Waiting Behavior

There are three answers, and for one of them the standard hands the choice to the implementation. The **launch policy** is `std::async`'s launch argument, and it governs when the task starts work.

## async, deferred, and the Default Policy

`std::async` has two overloads; let's meet them one by one. The policy-taking overload takes `std::launch` as its first parameter. For the policy-less overload, the standard defines it as equivalent to passing `std::launch::async | std::launch::deferred` ([futures.async]/3). The two flags may be combined with bitwise or — that is exactly how the default policy is spelled.

`std::launch::async` means: the call to `std::async` invokes the function in a new thread of execution, with the return value and exception stored into the shared state. The clause has one added sentence worth your notice: the thread object itself is also stored in the shared state ([futures.async]/3.1). Its use isn't visible yet; in the extension section it is half the answer. One more piece of wording deserves a close look — the standard describes asynchronous execution, verbatim, as "as if in a new thread of execution represented by a thread object". Read it literally: a thread object means an honest new thread every time, not one fished out of a pool. The opening summary on cppreference says might be a part of a thread pool — the page leaves the pool possibility open — so flipping through the reference pages will show you both statements. The clause is written in terms of a thread object, and we read as the clause reads. Which road the local machine takes, the source excerpt below will lay out: a freshly started thread every time.

`std::launch::deferred` is the opposite: the function and its arguments are stored into the shared state, and nobody touches them. Only when a non-timed waiting function is first called on the returned future (`wait` and `get` both count) does the function execute — in the thread calling the waiting function ([futures.async]/3.2). Never call, and it stays stored forever.

When both flags are written, the standard does not dictate which one the implementation picks, leaving only a recommended practice: when concurrency is no longer of use, the implementation should defer execution, or defer the choice (the Recommended practice at [futures.async]/3.2). cppreference's Notes add that some implementations add their own bits to the default policy, citing sync and task as examples of custom policies. Reading this far, note it down: which one runs is the implementation's freedom.

Let's run the same task once per policy and watch thread ids and timestamps:

<OnlineCompilerDemo
  title="The Same Task Under Three Policies: Thread Ids and Timestamps"
  source-path="code/examples/vol5/chapter04/06_demo_three_policies.cpp"
  description="The task sleeps 50 ms then prints its thread id and the time; the async group switches threads and starts at once, the deferred group waits for get and runs on main's thread, and the default group looks just like async on this machine."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_three_policies.cpp —— 同一任务三种策略各跑一遍，看线程号和时刻
#include <chrono>
#include <cstdio>
#include <functional>
#include <future>
#include <thread>

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

std::size_t id_of() {
  return std::hash<std::thread::id>{}(std::this_thread::get_id());
}

double since(Clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

void probe(const char* tag, Clock::time_point t0) {
  std::this_thread::sleep_for(50ms);   // 任务本体算作 50 ms 的活
  std::printf("  %s 在线程 %zu 上执行，距 async 返回 %.1f ms\n",
              tag, id_of(), since(t0));
}

void case_of(const char* tag, std::launch policy) {
  std::printf("[%s] main 线程是 %zu\n", tag, id_of());
  auto t0 = Clock::now();
  std::future<void> f = std::async(policy, probe, tag, t0);
  std::this_thread::sleep_for(100ms);  // 给后台任务留出 100 ms 的窗口
  std::printf("[%s] main 睡了 100 ms，现在调 get\n", tag);
  f.get();
  std::printf("[%s] get 返回，全程 %.1f ms\n\n", tag, since(t0));
}

int main() {
  case_of("async   ", std::launch::async);
  case_of("deferred", std::launch::deferred);
  case_of("default ", std::launch::async | std::launch::deferred);
  return 0;
}
```

</OnlineCompilerDemo>

```text
[async   ] main 线程是 12072240331452761199
  async    在线程 3459647975372803470 上执行，距 async 返回 50.2 ms
[async   ] main 睡了 100 ms，现在调 get
[async   ] get 返回，全程 101.6 ms

[deferred] main 线程是 12072240331452761199
[deferred] main 睡了 100 ms，现在调 get
  deferred 在线程 12072240331452761199 上执行，距 async 返回 157.8 ms
[deferred] get 返回，全程 157.8 ms

[default ] main 线程是 12072240331452761199
  default  在线程 3459647975372803470 上执行，距 async 返回 50.5 ms
[default ] main 睡了 100 ms，现在调 get
[default ] get 返回，全程 102.9 ms
```

Read the output group by group. The task's print happens after its own 50 ms sleep, so print-time minus 50 is its startup delay. Async group: the task moved to a new thread with a startup delay of about 0.2 ms, finished the work while main was still sleeping, and `get` returned instantly. Deferred group: the task shares main's thread id, its output appears after main woke and called `get`, and 157.8 ms is roughly the 100 ms sleep plus the 50 ms work. The default group on this machine looks exactly like async: thread switched, work started at once.

> The default group's thread id is identical to the async group's — glance twice and you'll ask. The previous thread had long ended; the system reclaimed the id and reused it. What we see is not one thread still alive.

The local default policy chose async — libstdc++'s way, not a standard requirement. The choosing logic is written in the experiment machine's own `/usr/include/c++/16/future`:

```cpp
// /usr/include/c++/16/future（节选，注释为笔者所加；try 为原文内部宏 __try 的展开式，异常宏分支省略）
if ((__policy & launch::async) == launch::async)
  {
    try
      {
        __state = std::make_shared<_As>(...);   // _As 是 _Async_state_impl，起线程
      }
    catch(const system_error& __e)
      {
        if (__e.code() != errc::resource_unavailable_try_again
            || (__policy & launch::deferred) != launch::deferred)
          throw;                                // 起不了线程，也没给 deferred 位，照常抛
      }
  }
if (!__state)
  __state = std::make_shared<_Ds>(...);         // 走到这里，退成 deferred。_Ds 是 _Deferred_state，deferred 的状态类
```

Translate the source into plain words. The default overload passes `async | deferred` straight down. In the policy overload, as long as the async bit is present, it picks the thread-starting state object. Only when starting a thread fails with `resource_unavailable_try_again` and the deferred bit is also present does it fall back to the deferred state object. cppreference wrote out the throwing boundary, sourced to Library Working Group issue 2078 (LWG 2078)'s resolution: `system_error` is thrown only when policy equals `std::launch::async`; a combined policy falls back to deferred without throwing. The standard's body carries the same error conditions ([futures.async]/6-7).

## The deferred Traps

Every behavior of deferred is written in the standard; the standard itself is nowhere vague. What gets vague is our expectations. I split them into three traps, each with an experiment.

### Trap One: Never Called, Never Executed

A deferred task occupies no thread. It is nothing more than the function and arguments stored in the shared state — the standard's note at [futures.state]/2 says the result may be "a function to be used to calculate the result", which is exactly what deferred stores. Only when a non-timed waiting function is first called does it execute, on the calling thread ([futures.async]/3.2). Call neither `get` nor `wait`, and it stays stored. The draft spells out the consequence for us (the note at [futures.async]/4): the function may never be invoked at all.

You might think: surely it runs at destruction time? No. deferred has no thread to wait for; the future's destructor simply lets go. Watch the experiment:

<OnlineCompilerDemo
  title="deferred Never Runs Unless Called, and Destruction Doesn't Make Up for It"
  source-path="code/examples/vol5/chapter04/07_demo_deferred_never_runs.cpp"
  description="The deferred future held in hand runs its course in 0.0 ms and the bread never enters the oven; async merely discards a temporary future, yet its destructor waits 200 ms. With -Wall locally you'll see the nodiscard warning, covered in the destructor section."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_deferred_never_runs.cpp —— deferred 不调用就永不执行，析构也不补跑
#include <chrono>
#include <cstdio>
#include <future>
#include <thread>

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

double since(Clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

void bake() {
  std::this_thread::sleep_for(200ms);
  std::printf("  bake: 面包出炉了\n");
}

int main() {
  {
    auto t0 = Clock::now();
    std::future<void> f = std::async(std::launch::deferred, bake);
    std::printf("[deferred] future 一直在手里，没调 get 也没调 wait\n");
    std::printf("[deferred] 到作用域结尾丢弃，共 %.1f ms，上面没有 bake 的输出\n",
                since(t0));
  }
  {
    auto t0 = Clock::now();
    std::async(std::launch::async, bake);   // 临时 future 在分号处析构
    std::printf("[async] 丢弃临时 future，整个表达式 %.1f ms\n", since(t0));
  }
  std::printf("main 到头了，bake 只烤成过一次\n");
  return 0;
}
```

</OnlineCompilerDemo>

```text
[deferred] future 一直在手里，没调 get 也没调 wait
[deferred] 到作用域结尾丢弃，共 0.0 ms，上面没有 bake 的输出
  bake: 面包出炉了
[async] 丢弃临时 future，整个表达式 201.9 ms
main 到头了，bake 只烤成过一次
```

In the deferred group, not one line of bake's output appears. Whether the future stayed in hand or was dropped at the scope's end, the program clocked through in 0.0 ms — the bread never entered the oven. The async group merely discarded a temporary future, yet its destructor waited 201.9 ms, with bake's output appearing before the timing line. One discarding action, two policies: on one side the task never ran; on the other you were forced to wait out its run. Why the destructor waits belongs to the destructor section and its extension; here, just record deferred's result: no wait, no make-up run. If you want to be strict, check against [futures.state]/5.3: the deferred state actually does satisfy the three conditions the destructor section will list (state created by async, not yet ready, last reference being released) — the clause says "may block", and an implementation choosing not to block and not to run is equally compliant.

> One guess we answered for you: hand a deferred future to another thread, and whoever calls `get` in the future is the thread the task runs on. The execution site follows the waiting function, not the thread that originally launched the async.

### Trap Two: wait_for Just Spins

`wait_for` and `wait_until` are timed waiting functions returning `std::future_status`, with the three values `ready`, `timeout`, and `deferred`. The standard writes their Effects bluntly: when the shared state holds a deferred function, they do nothing, and the return is fixed at `future_status::deferred` ([futures.unique.future]/22, /23.1; the wait_until twins at /25, /26.1). You call it; it neither runs the task nor waits — it comes straight back.

<OnlineCompilerDemo
  title="deferred's wait_for Returns Immediately — No Execution, No Waiting"
  source-path="code/examples/vol5/chapter04/08_demo_wait_for_spins.cpp"
  description="Three wait_for(100ms) calls each take 0.000 ms and each return deferred; only switching to get() executes the task. Writing a timeout poll with it spins and burns a full core."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_wait_for_spins.cpp —— deferred 的 wait_for 立刻返回，不执行也不等待
#include <chrono>
#include <cstdio>
#include <future>
#include <thread>

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

double since(Clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

const char* name_of(std::future_status s) {
  switch (s) {
    case std::future_status::deferred: return "deferred";
    case std::future_status::ready:    return "ready";
    default:                           return "timeout";
  }
}

int work() {
  std::this_thread::sleep_for(50ms);
  return 7;
}

int main() {
  std::future<int> f = std::async(std::launch::deferred, work);
  for (int i = 1; i <= 3; ++i) {
    auto t0 = Clock::now();
    std::future_status st = f.wait_for(100ms);
    std::printf("第 %d 次 wait_for(100ms) -> %-8s 实际用时 %.3f ms\n",
                i, name_of(st), since(t0));
  }
  std::printf("改用 get()，任务这才执行 -> %d\n", f.get());
  return 0;
}
```

</OnlineCompilerDemo>

```text
第 1 次 wait_for(100ms) -> deferred 实际用时 0.000 ms
第 2 次 wait_for(100ms) -> deferred 实际用时 0.000 ms
第 3 次 wait_for(100ms) -> deferred 实际用时 0.000 ms
改用 get()，任务这才执行 -> 7
```

The three `wait_for` calls each took 0.000 ms, all three returning deferred. Write a timeout poll on `wait_for` with the condition "keep going unless ready", and the loop starts spinning: every call returns instantly, the condition never satisfies, the CPU burns a full core, and the task hasn't moved a step.

The fix is to treat deferred as one of the answers. A deferred from the poll means the task hasn't started: either call `get` directly and let it finish on the current thread, or relaunch with `launch::async`. Spinning is more infuriating than deadlock — a deadlocked process you spot at a glance, but a spinning one is perfectly alive, just producing nothing.

### Trap Three: thread_local and Thread Identity Miss Their Step

deferred executes in the thread that calls the waiting function — which need not be the thread that launched the async, as cppreference's deferred section points out explicitly. If your code depends on thread identity, deferred lands the dependency in the wrong place.

<OnlineCompilerDemo
  title="deferred Runs on the Thread Calling get, and thread_local Misses"
  source-path="code/examples/vol5/chapter04/09_demo_deferred_thread_local.cpp"
  description="The same bump: the async version starts from zero on a new thread and counts to 1; the deferred version executes inside main's get, sees the instance main already raised to 3, counts to 4, and main's own counter becomes 4."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_deferred_thread_local.cpp —— deferred 在调用 get 的线程里跑，thread_local 踩空
#include <cstdio>
#include <functional>
#include <future>
#include <thread>

thread_local int counter = 0;   // 每条线程各有一份

std::size_t id_of() {
  return std::hash<std::thread::id>{}(std::this_thread::get_id());
}

int bump() {
  counter += 1;
  std::printf("  bump 在线程 %zu 上，counter 算到 %d\n", id_of(), counter);
  return counter;
}

int main() {
  std::printf("main 线程是 %zu，counter 加到 3\n", id_of());
  counter = 3;
  std::future<int> fa = std::async(std::launch::async, bump);
  std::future<int> fd = std::async(std::launch::deferred, bump);
  int a = fa.get();
  int d = fd.get();
  std::printf("async 的答案 %d，deferred 的答案 %d\n", a, d);
  std::printf("main 手里的 counter 是 %d\n", counter);
  return 0;
}
```

</OnlineCompilerDemo>

```text
main 线程是 1278684541068337386，counter 加到 3
  bump 在线程 13407857457959151560 上，counter 算到 1
  bump 在线程 1278684541068337386 上，counter 算到 4
async 的答案 1，deferred 的答案 4
main 手里的 counter 是 4
```

The same `bump`: the async version runs on a new thread where counter starts from zero and counts to 1. The deferred version executes inside main's `get`, sees the instance main already raised to 3, counts to 4 — and the counter in main's hand turns 4 as well. We wanted each task tallying its own; deferred didn't give us that.

thread_local instances follow the thread, and so does everything else that leans on thread identity. Use thread::id as a map key or thread ids in logging, and deferred makes the task share the caller's identity; the "initialization that only happens in a background thread" we were counting on is nowhere at all. Which thread it runs on is decided by whoever calls the waiting function — that is how the standard writes it ([futures.async]/3.2).

## Legitimate Uses of deferred (Extension)

> This section is an extension; skipping it does not affect the main line. What remains is when deferred is worth choosing on purpose.

Read the traps backwards. Every deferred trap, in a different scene, is a feature. Never called, never executed is exactly what lazy evaluation wants. The task running on the calling thread saves the thread-creation cost too. As for `wait_for` returning deferred immediately, that is precisely a probe for a task that hasn't started.

I have seen it serve well on two occasions.

One, lazy evaluation. Parsing a configuration file takes hundreds of milliseconds, and only one branch of the program uses it. Wrap it as deferred, `get` only where it is truly needed, and pay nothing on paths that never arrive:

```cpp
// 示意，不是能直接编译的完整程序
std::future<Config> lazy_config =
    std::async(std::launch::deferred, load_config);

if (mode == Mode::Fancy) {
    Config cfg = lazy_config.get();   // 走到这里才真正解析
    apply(cfg);
} else {
    // 不走 Fancy 分支，load_config 一次都不会执行
}
```

Two, giving a light task the shape of a future. The task itself is two or three milliseconds, thread creation costs more than the work, yet you still want callers to uniformly hold a future, uniformly wait, uniformly catch exceptions. deferred removes the thread cost while the interface stays unchanged. We will meet the same idea again in 4.4, *Wrapping a Computation into a Storable, Dispatchable Task*: separate execution from result-carrying — the former swappable, the latter unchanged.

One boundary you must keep yourself: a deferred function runs on the `get` thread, so "it is already running in the background" must not enter any of your assumptions. The rule of thumb is plain. Needed now and concurrently, choose `launch::async`. Computed only when used and fast when run, choose `launch::deferred`. Unsure? Write the policy explicitly and don't let the default decide for you.

## The Destructor Behavior of a Returned future

The rule's original text is at [futures.state]/5; start from the skeleton. When a future (or any other asynchronous object) is destroyed, it "releases" the shared state — normally letting go and walking away. The exception requires three conditions at once: the state was created by `std::async`, the state is not yet ready, and the reference being released is the last one. All three gathered, the release may block. The note at [futures.async]/5 adds a human sentence: a future obtained from async, if moved out of a local scope, may have its destructor block waiting for the shared state to become ready. cppreference writes the other half too: futures obtained any other way never block on destruction.

<OnlineCompilerDemo
  title="Discard-Style async Waits One by One; Launch Everything, Then Collect, for Parallelism"
  source-path="code/examples/vol5/chapter04/10_demo_dtor_blocks.cpp"
  description="Three discard-style lines each wait a full 300 ms, accumulating 900-odd ms; store the futures in a container, launch all, then get together — three at once cost only 300 ms. Discarded deferred and a promise's future destructor are both 0.0 ms, waiting on nobody."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_dtor_blocks.cpp —— 丢弃式 async 逐个等待，启动完再收集才并行
#include <chrono>
#include <cstdio>
#include <future>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

double since(Clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

void nap(const char* tag) {
  std::this_thread::sleep_for(300ms);
  std::printf("  %s 干完了\n", tag);
}

int main() {
  auto t0 = Clock::now();
  std::async(std::launch::async, nap, "A1");
  double a1 = since(t0);
  std::async(std::launch::async, nap, "A2");
  double a2 = since(t0);
  std::async(std::launch::async, nap, "A3");
  std::printf("[丢弃式] A1 用 %.1f ms，A2 累计 %.1f ms，A3 累计 %.1f ms\n",
              a1, a2, since(t0));

  t0 = Clock::now();
  std::vector<std::future<void>> fs;
  for (const char* tag : {"B1", "B2", "B3"}) {
    fs.push_back(std::async(std::launch::async, nap, tag));
  }
  for (auto& f : fs) f.get();
  std::printf("[启动完再收集] 三个一起 %.1f ms\n", since(t0));

  t0 = Clock::now();
  std::async(std::launch::deferred, nap, "C");
  std::printf("[deferred 丢弃] %.1f ms，上面没有 C 干完的输出\n", since(t0));

  t0 = Clock::now();
  {
    std::promise<void> p;   // promise 是 4.3《自行传递结果》的主角，这里只借它的 future 当对照
    std::future<void> f = p.get_future();
    (void)f;
  }
  std::printf("[promise 的 future 析构] %.1f ms，谁也不等\n", since(t0));
  return 0;
}
```

</OnlineCompilerDemo>

```text
  A1 干完了
  A2 干完了
  A3 干完了
[丢弃式] A1 用 300.8 ms，A2 累计 601.1 ms，A3 累计 904.7 ms
  B1 干完了
  B2 干完了
  B3 干完了
[启动完再收集] 三个一起 300.6 ms
[deferred 丢弃] 0.0 ms，上面没有 C 干完的输出
[promise 的 future 析构] 0.0 ms，谁也不等
```

The three discard-style lines each waited a full 300 ms, accumulating 904.7 ms, with A1 through A3's outputs queued in order. To the eye it is three parallel lines of code; to the machine it is strictly serial: only after the previous temporary future's destructor finishes does the next task start. For parallel tasks, store the futures in a container, launch them all, then `get` together — group B's three-at-once took just 300.6 ms.

4.1's close left one line hanging: whether the task's thrown exception will ever be seen by anyone also hangs on the destructor's waiting behavior. We can now answer: in discard-style async, the task throws, the destructor waits out the task's completion, the exception is stored into the shared state, the last reference lets go, the state is destroyed — and the exception goes with it ([futures.state]/5.1). For it to be seen, someone must `get`. I read this corollary out of the clauses without a separate experiment: storing the exception was already verified in 4.1; what we add here is only the destruction step.

Discarded deferred takes 0.0 ms, matching trap one's reading. A promise's future destructor is also 0.0 ms, waiting on nobody. promise is the protagonist of 4.3, *Passing Results Yourself: `std::promise` and `std::future`*; here it only serves as the control. With the comparison done we can say: the same future type, different origins, different destructor temperaments.

Of the three conditions, the most easily overlooked is "the last reference". future cannot be copied, so the references cannot spread out; `shared_future` can. Let's try:

<OnlineCompilerDemo
  title="The Wait Happens When the Last Reference Lets Go"
  source-path="code/examples/vol5/chapter04/11_demo_last_reference.cpp"
  description="The extra copy's destructor takes 0 ms, dropping one reference takes 0 ms, and only when the last reference lets go does it wait a full 300 ms: the wait hangs on the reference count hitting zero."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_last_reference.cpp —— 等待发生在最后一个引用放手时，不在每一次析构
#include <chrono>
#include <cstdio>
#include <future>
#include <thread>

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

double since(Clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

void nap() { std::this_thread::sleep_for(300ms); }

int main() {
  std::future<void> f = std::async(std::launch::async, nap);
  std::shared_future<void> s1 = f.share();
  std::shared_future<void> s2 = s1;   // 同一状态现在挂在两个 shared_future 上

  auto t0 = Clock::now();
  { std::shared_future<void> s3 = s2; }
  std::printf("多出来的副本析构 %.3f ms\n", since(t0));

  t0 = Clock::now();
  s2 = std::shared_future<void>();    // 放掉一个引用，手里还剩 s1
  std::printf("放掉一个引用 %.3f ms\n", since(t0));

  t0 = Clock::now();
  s1 = std::shared_future<void>();    // 最后一个引用走人，析构开始等
  std::printf("最后一个引用 %.1f ms\n", since(t0));
  return 0;
}
```

</OnlineCompilerDemo>

```text
多出来的副本析构 0.000 ms
放掉一个引用 0.000 ms
最后一个引用 300.4 ms
```

Line the three readings up: copy destructor 0 ms, dropping one reference 0 ms, and the last reference's release waiting 300.4 ms. The wait happens at the instant the reference count hits zero, not at every destruction.

One more helper sits in the compiler's hands. libstdc++ marks `std::async` `[[nodiscard]]`; compile one discard-style call with default options and the warning comes out on its own:

```text
warning: ignoring return value of 'std::future<...> std::async(launch, _Fn&&, _Args&& ...)',
    declared with attribute 'nodiscard' [-Wunused-result]
```

I truncated the warning text; pasted whole it would fill half a screen. What it warns about is precisely discard-style async: the return value carries nodiscard, and throwing it away makes the compiler speak up. On seeing the warning, don't rush to suppress it with `(void)` — ask whether the task is one you wanted to wait for or one you forgot to. One more judgment reads straight from the clauses: after `get`, the state is already ready, so later destruction falls outside the exception's conditions — let go and go; "not yet ready" is the threshold ([futures.state]/5.3). That judgment likewise comes from the clauses alone, without a separate experiment.

## Why the Destructor Waits: References and Release Timing in the Shared State (Extension)

> This section is an extension; skipping it does not affect the main line. The previous section gave the behavior and the conditions; this one answers why it was designed so, from the shared state's reference counting to the fate of the thread object.

Spread the shared state out. The standard defines it at [futures.state]/1: status information plus a result that may not be computed yet, where the result may be a value or an exception. The note adds a third: it may also be "a function to be used to calculate the result" — which is exactly what async's deferred stores. future and promise are merely objects hanging off the state, one reading, one writing.

What a future's destructor does is written at [futures.unique.future]/10: release the shared state, then destroy itself. Releasing means, per [futures.state]/5, letting go — and if the reference in hand is the last one, the state is destroyed with it. Normally no blocking; the exception is the previous section's three conditions, which we have already recited.

Why must async's state destruction wait for the task to finish? Pick up the foreshadowing from the policy section: the thread object is stored in the shared state ([futures.async]/3.1). A thread object is no ordinary member; its destruction has a temper of its own. Destroy a joinable `std::thread` and it calls `std::terminate` ([thread.thread.destr]/1). If the state were destroyed before the task finished, the thread object inside would be destroyed with it and the program would terminate on the spot. So before releasing the last reference we must take the thread back, and taking it back means join — and join waits for the task to finish.

libstdc++'s source matches the reasoning above; let's look side by side. `_Async_state_commonV2` carries a `thread _M_thread`, and the comment beside it quotes the draft clause directly — the waiting-function waiting rule: wait for the thread to finish, as if by join. `_Async_state_impl`'s destructor is three lines:

```cpp
// /usr/include/c++/16/future（节选）
~_Async_state_impl()
{
  if (_M_thread.joinable())
    _M_thread.join();
}
```

Let me also read you the original comment above that destructor: the members storing the result and the function must not be destroyed before the task finishes, and this destructor calls `join` directly rather than the `call_once`-guarded `_M_join`, because no other thread can still reference the state at its destruction. The reference counting is implemented with `shared_ptr` — a future holds a `std::shared_ptr<_State_base>`. Store futures in a vector and the references live with the futures, which is why group B's tasks dared to run together. Spreading references across multiple holders relies on `shared_future`'s copies — the experiment's 0 ms copy destruction corresponds to exactly that.

The deferred state object has no thread member — a flip through the header confirms it: `_Deferred_state` has exactly two data members, one for the function and one for the result. No thread to take back, so destruction is naturally 0 ms.

The promise side of supplying values deserves a glance too — it completes the last piece of "other futures never block". The objects that put results into the shared state got their standard name in 4.1: asynchronous providers ([futures.state]/4). promise is a provider. When a provider is destroyed while the state is not yet ready, the standard calls it **abandon**: it stores a `future_error` with the error condition `broken_promise` into the state and makes the state ready ([futures.state]/7). Let's run one:

<OnlineCompilerDemo
  title="A promise Destroyed Without a Value: get Receives broken_promise"
  source-path="code/examples/vol5/chapter04/12_probe_broken_promise.cpp"
  description="The promise is destroyed before fulfilling anything; the abandoned shared state turns ready anyway, and get throws future_error: Broken promise."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// probe_broken_promise.cpp —— promise 没给值就销毁，get 收到 broken_promise
#include <cstdio>
#include <future>

int main() {
  std::future<int> f;
  {
    std::promise<int> p;
    f = p.get_future();
  }   // promise 没给值就销毁，共享状态被弃
  try {
    f.get();
  } catch (const std::future_error& e) {
    std::printf("get 抛了 future_error：%s（broken_promise？%s）\n", e.what(),
                e.code() == std::future_errc::broken_promise ? "是" : "否");
  }
  return 0;
}
```

</OnlineCompilerDemo>

```text
get 抛了 future_error：std::future_error: Broken promise（broken_promise？是）
```

Abandonment is not a leak. The abandoned state becomes ready — in fact — and whoever waits at `get` immediately receives a clear error, knowing the providing side left without delivering. References, readiness, abandonment: with the three words assembled, the destructor behavior all falls within what we can explain. async's state houses a thread, and the last reference must take the thread back, hence the wait. Other states hold no thread — let go and go — and if nobody delivers, a `broken_promise` is sent instead.

## Your Turn to Try

Before writing the next async, I walk these questions through:

1. Does your async call write a policy? If not, you are betting on the implementation's choice of the moment.
2. After taking the future, is there a path that certainly reaches `get` or `wait`? A task with none never runs.
3. For the timed wait's return value, is the deferred branch handled?
4. Does the task involve thread_local, thread::id, or "background thread" assumptions? deferred lands the dependency on whichever thread calls the waiting function.
5. Is discard-style async the effect you want? For true parallelism, store the futures in a container, launch them all, then `get` together.

## Reference Resources

While writing this article I followed the progression of Anthony Williams's *C++ Concurrency in Action*; its arrangement of teaching async and future together tracks ours most closely. The examples and prose are my own; outputs are pasted into the text as they ran, with thread ids being hash values that differ every time. Clause citations in the text put the clause name in brackets and the paragraph after the slash; clause names match the official standard, and paragraph numbers may drift. cppreference's `std::async` page was fetched in full, and both statements — discard-style async and "other futures never block" — match its Notes. libstdc++ implementation details were read from the local GCC 16 `<future>` (path `/usr/include/c++/16/future`); there is no web page to cite. The corresponding article in the old ch05 directory, the first one, *std::async and future*, covered the default policy's risks once; this article's experiments and clause checks were done fresh.

- [std::async (cppreference)][cppref-async], the two overloads and the default policy, the boundary of policy selection, the Notes on discard-style async, and LWG 2078 in the defect-resolution table.
- [LWG 2078 (Library Working Group issue)][lwg-2078], the resolution narrowing the `system_error` throw on thread-creation failure to policy == `launch::async`.
- [Function template async (current draft, futures.async)][draft-async], the default policy's equivalent spelling, the Effects of async and deferred, the synchronization clauses, the error conditions, and the "function may never be invoked" note.
- [Shared state (current draft, futures.state)][draft-state], the shared state's definition, release's three conditions, abandon and broken_promise.
- [Class template future (current draft, futures.unique.future)][draft-unique], the Effects of `wait_for` doing nothing on deferred, and how the destructor and `get` each release the shared state.
- [Destructor (current draft, thread.thread.destr)][draft-threaddestr], a joinable thread object's destruction calls terminate.
- [C++ Concurrency in Action, 2nd Edition (Anthony Williams)][williams], the reference for the async chapter's progression.

[cppref-async]: https://en.cppreference.com/w/cpp/thread/async
[lwg-2078]: https://cplusplus.github.io/LWG/issue2078
[draft-async]: https://eel.is/c++draft/futures.async
[draft-state]: https://eel.is/c++draft/futures.state
[draft-unique]: https://eel.is/c++draft/futures.unique.future
[draft-threaddestr]: https://eel.is/c++draft/thread.thread.destr
[williams]: https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition
