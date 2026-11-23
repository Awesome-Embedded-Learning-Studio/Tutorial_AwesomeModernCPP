---
title: "Can It Stop When Asked: Stop-Path Testing and Three Diagnostic Tools"
description: "Exception injection and stop paths test the ending paths, while three extension sections each claim one diagnostic tool: GDB, Helgrind, and structured logging."
chapter: 5
order: 2
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
  source: documents/vol5-concurrency/chapter06-validating-designs/02-stop-path-and-diagnostics.md
  source_hash: 0b18a1204d1ffe24f6e10c640df4610eed7565f1cb39f39ae4b1f76868a3d7d7
  translated_at: '2026-11-22T13:35:49+00:00'
  engine: glm-5.3
  token_count: 14412
---

# Can It Stop When Asked: Stop-Path Testing and Three Diagnostic Tools

Whether races can be smoked out was the previous article's tally. This one switches to a different class of problems: have the stopping and failure paths been tested, and which diagnostic tools exist beyond TSan — three extension sections, each claiming one.

## Exception Injection and Stop-Path Testing

Failures in concurrent programs mostly hide near the end. An exception thrown inside a thread function has nowhere to go, an async task's exception lies in the future waiting for someone to fetch it, and a wait loop can be stuck with a consumer that will not shut down. We lay the tests down one path at a time, and every path walked to its end leaves a reading.

An exception cannot walk out of the thread function on its own. Let's run an experiment:

<OnlineCompilerDemo
  title="An exception escapes the thread function, and std::terminate takes the process on the spot"
  source-path="code/examples/vol5/chapter06/05_throw_thread.cpp"
  description="The runtime_error thrown in the thread has no one to catch it; std::terminate takes the process, and the printf at the end is never reached. Running it shows terminate called plus the what() message, with exit code 134."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
#include <cstdio>
#include <stdexcept>
#include <thread>

int main() {
  std::thread t([] { throw std::runtime_error("job failed"); });
  t.join();
  std::printf("never reached\n");
  return 0;
}
```

</OnlineCompilerDemo>

```bash
g++ -std=c++20 -O0 -pthread throw_thread.cpp -o throw_thread   # 普通版配方，-O0 让栈好读
./throw_thread; echo "exit=$?"
```

```text
terminate called after throwing an instance of 'std::runtime_error'
  what():  job failed
exit=134
```

Where the exit code comes from was covered in Section 1.3 ([The First `std::thread` Program](../chapter01-start-concurrency/03-first-std-thread)): 128 plus the number of SIGABRT, 6 — the shell uses it to report a death by signal, which here makes 134. The printf at the end is never reached. Once an exception escapes the thread function, std::terminate simply takes the whole process. So a thread function either catches its own exceptions, or hands the failure out before wrapping up.

For a path outward, we laid one down in Section 4.1 ([Running Computations with `std::async`](../chapter04-organizing-tasks/01-launch-with-async)): the exception the task throws is stored in the shared state, and future.get() rethrows it in the thread that fetches the result. The scene of the failure stays the same; we just give it a different delivery route:

<OnlineCompilerDemo
  title="An async task's exception, rethrown in the calling thread via future.get()"
  source-path="code/examples/vol5/chapter06/06_async_throw.cpp"
  description="The exception the task throws is stored in the shared state; main's catch receives the same exception through get(), message intact, and the process exits with code 0. Compared with the previous demo, nobody gets terminated here."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
#include <cstdio>
#include <future>
#include <stdexcept>

int main() {
  auto f = std::async(std::launch::async,
                      []() -> int { throw std::runtime_error("task failed"); });
  try {
    f.get();
  } catch (const std::runtime_error& e) {
    std::printf("caught from future: %s\n", e.what());
  }
  return 0;
}
```

</OnlineCompilerDemo>

```bash
g++ -std=c++20 -O0 async_throw.cpp -o async_throw   # 普通版配方，-O0 且未加 -pthread
./async_throw; echo "exit=$?"
```

```text
caught from future: task failed
exit=0
```

The failure was handed from the worker thread to the thread waiting for the result, and the process wrapped up gracefully. We rehearse this class of test as a fixed item in the repertoire: when a failure really is thrown, the side that catches it must receive the message intact.

For the injection experiment, we give failure a real point of entry: the producer throws an exception while loading the third item, and we watch whether the consumer side can receive the notice and exit cleanly.

<OnlineCompilerDemo
  title="Exception injection: the producer throws while loading item three; the consumer gets the notice and exits"
  source-path="code/examples/vol5/chapter06/07_inject_fail.cpp"
  description="The producer throws at item three, sets the failure flag, and calls notify_all. After taking delivery of two items, the consumer receives the failure notice and exits cleanly — nobody is left stuck in wait."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>

std::deque<int> items;
std::mutex m;
std::condition_variable cv;
bool closed = false;
bool failed = false;

int main() {
  std::thread producer([] {
    try {
      for (int i = 1; i <= 5; ++i) {
        if (i == 3) throw std::runtime_error("device busy while packing item 3");
        {
          std::lock_guard lk(m);
          items.push_back(i);
        }
        cv.notify_one();
      }
    } catch (...) {
      std::lock_guard lk(m);
      failed = true;
      cv.notify_all();
    }
  });
  std::thread consumer([] {
    int got = 0;
    for (;;) {
      std::unique_lock lk(m);
      cv.wait(lk, [] { return !items.empty() || failed; });
      if (!items.empty()) {
        items.pop_front();
        ++got;
        continue;
      }
      std::printf("consumer received %d items, then the failure notice\n", got);
      break;
    }
  });
  producer.join();
  consumer.join();
  return 0;
}
```

</OnlineCompilerDemo>

```bash
g++ -std=c++20 -O1 -pthread inject_fail.cpp -o inject_fail   # 普通版配方，-O1 折中
./inject_fail
```

```text
consumer received 2 items, then the failure notice
```

The reading has exactly the shape I wanted to see: two items delivered, the failure notice right behind them, the program exiting normally, and nobody left stuck in `wait`. Two points carry the design: the failure flag and the queue state are both written into the same predicate, so the consumer's two ways out (new item, failure) wait in the same `wait`. The throwing side sets the flag and then calls `notify_all`, waking everyone who waits — miss one, and you gain one more suspended thread.

For stop paths we test with the C++20 stop protocol. Section 1.6 established the three-piece stop kit (`stop_source`, `stop_token`, `stop_callback`, [Letting Work End in Order](../chapter01-start-concurrency/06-jthread-and-stopping)), and Section 3.6 covered the ways a wait can end ([Waits Need an Exit Path Too](../chapter03-wait-and-coordinate/06-ending-a-wait)). The `wait` of `condition_variable_any` can take a `stop_token` (since C++20, see [cppreference][cppref-cv-wait]); the moment a stop request arrives, it is woken from its sleep:

<OnlineCompilerDemo
  title="Stop paths: both request_stop and close can wake the wait"
  source-path="code/examples/vol5/chapter06/08_stop_paths.cpp"
  description="The wait of condition_variable_any takes a stop_token: B is woken by request_stop, A is woken by setting closed plus notify_all, and both exit-path latencies land in millisecond-level readings."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

std::deque<int> items;
std::mutex m;
std::condition_variable_any cv_any;
bool closed = false;

static std::string exit_reason(bool stop_seen) {
  if (stop_seen) return "stop requested";
  if (closed) return "closed";
  return "items left";
}

int main() {
  std::jthread b([](std::stop_token st) {
    std::unique_lock lk(m);
    cv_any.wait(lk, st, [&] { return !items.empty() || closed; });
    std::printf("consumer B exits on: %s\n",
                exit_reason(st.stop_requested()).c_str());
  });
  std::jthread a([](std::stop_token st) {
    std::unique_lock lk(m);
    cv_any.wait(lk, st, [&] { return !items.empty() || closed; });
    std::printf("consumer A exits on: %s\n",
                exit_reason(st.stop_requested()).c_str());
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  auto t0 = std::chrono::steady_clock::now();
  b.request_stop();
  b.join();
  auto ms_b = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - t0).count();
  std::printf("request_stop() to B exit: %lld ms\n", (long long)ms_b);

  auto t1 = std::chrono::steady_clock::now();
  {
    std::lock_guard lk(m);
    closed = true;
  }
  cv_any.notify_all();
  a.join();
  auto ms_a = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - t1).count();
  std::printf("close() to A exit: %lld ms\n", (long long)ms_a);
  return 0;
}
```

</OnlineCompilerDemo>

```bash
g++ -std=c++20 -O1 -g -pthread stop_paths.cpp -o stop_paths   # 普通版配方，-O1 折中，-g 让栈好定位
./stop_paths
```

```text
consumer B exits on: stop requested
request_stop() to B exit: 0 ms
consumer A exits on: closed
close() to A exit: 0 ms
```

We walked both paths: B was woken by `request_stop`, A was woken by `close`, and both waits exited in zero milliseconds. The close here means the wrap-up action of setting `closed` and then calling `notify_all`. The new-item path was already walked by the producer program in the injection experiment above, so failure, stop, and close have all been triggered by now. Among the ways a wait can end there is also the timeout, which belongs to Section 3.6's reach ([Waits Need an Exit Path Too](../chapter03-wait-and-coordinate/06-ending-a-wait)), so this article does not rerun it. Every reachable condition written into the predicate, we triggered once, and we measured how many milliseconds the exit took — over budget, and you know some wait cannot be closed.

## Runtime Diagnosis of Deadlock: Attaching GDB and Thread Call Stacks (Extension)

This section carries the (Extension) marker; skipping it does not affect the main thread of this article. When a program is already deadlocked, do not guess — look at the scene. The plain approach, laid out: find the stuck process's PID with `ps`, attach with `gdb -p PID`, then run `thread apply all bt` to print every thread's call stack in one shot. Put the two stacks side by side, and the question of who waits on whom you can settle at a glance.

For the experimental program we bring over the minimal AB-BA reversal from Section 2.6 ([Nobody Lets Go — Now What](../chapter02-sharing-data/06-deadlock-defenses)):

<OnlineCompilerDemo
  title="A minimal deadlock scene from the AB-BA reversal"
  source-path="code/examples/vol5/chapter06/09_deadlock.cpp"
  description="Demo purposes only, deadlocks on purpose: two threads take two locks in opposite orders and the program hangs without exiting — perfect material for attaching GDB to inspect the thread stacks (commands below). Running it on-site hangs until timeout; pair it with timeout when you rerun locally."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>

std::mutex ma;
std::mutex mb;

void work_a() {
  std::lock_guard<std::mutex> la(ma);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  std::lock_guard<std::mutex> lb(mb);   // A 后 B 的次序
  std::printf("a got both\n");
}

void work_b() {
  std::lock_guard<std::mutex> lb(mb);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  std::lock_guard<std::mutex> la(ma);   // B 后 A 的次序
  std::printf("b got both\n");
}

int main() {
  std::thread a(work_a);
  std::thread b(work_b);
  a.join();
  b.join();
  return 0;
}
```

</OnlineCompilerDemo>

```bash
g++ -std=c++20 -O0 -g -pthread deadlock.cpp -o deadlock   # 普通版配方，-O0 加 -g，栈好读
./deadlock &            # 卡住不退出
ps aux | grep deadlock  # 找到 PID
gdb -p <PID> -batch -ex "thread apply all bt"
```

An honest disclosure is due here: ptrace is restricted in the environment where this article was written; the sandbox does not allow ptrace attachment, gdb reported Operation not permitted, and the command did not run through on my machine. The interpretation method follows the GDB manual — run it once in your own terminal, and you will have real evidence. I can describe the expected shape for you: both worker threads' stacks stop at the top inside a kernel wait on mutex locking (glibc's `__lll_lock_wait` and kin), and one frame down are their respective work functions. Both stacks stopped on locking; set that against the two locking orders in the source — one side takes A then B, the other takes B then A — and once it matches, the deadlock scene is established. Section 2.6 gave the fix directions: the take-them-all-at-once route with std::lock, or a layered lock ordering.

## Helgrind: Valgrind's Thread Checker (Extension)

This section carries the (Extension) marker; skipping it does not affect the main thread of this article. A ready-built binary can be examined as-is — worth your acquaintance. TSan's route demands a rebuild; Helgrind does not. It is the thread checker in the Valgrind toolset, working through dynamic binary instrumentation, so we can run the existing binary directly:

```bash
valgrind --tool=helgrind ./your_program
```

The manual lists three classes of checks. The first is misuse of the pthread interface — unlocking a lock that was never locked, or destroying a held lock, both count. The second is inconsistency of lock order: it maintains a lock-order graph internally, and a cycle in the graph is reported as a potential deadlock, even if the deadlock has never actually happened once. Data-race detection is the third class, resting on happens-before plus lock sets. We test it with a small program of two threads each incrementing twenty thousand times:

<OnlineCompilerDemo
  title="Helgrind's fodder: a race from two threads each incrementing twenty thousand times"
  source-path="code/examples/vol5/chapter06/10_hg_race.cpp"
  description="Demo purposes only; it contains a data race: a plain long long is incremented by two threads, the reading counter=40000 comes out fully correct, and Valgrind Helgrind reports possible data race with the command below."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
#include <cstdio>
#include <thread>

long long counter = 0;

void work() {
  for (int i = 0; i < 20000; ++i) ++counter;
}

int main() {
  std::thread a(work);
  std::thread b(work);
  a.join();
  b.join();
  std::printf("counter=%lld (expected 40000)\n", counter);
  return 0;
}
```

</OnlineCompilerDemo>

```bash
g++ -std=c++20 -O1 -g -pthread hg_race.cpp -o hg_race   # 普通版配方，-O1 折中，-g 对行号
valgrind --tool=helgrind ./hg_race
```

With the local Valgrind 3.25.1, here is an excerpt of the report:

```text
==86131== Possible data race during read of size 8 at 0x4004068 by thread #3
==86131== Locks held: none
==86131==    at 0x40011C0: work() (hg_race.cpp:7)
==86131==    by 0x40013CC: _M_invoke<0> (std_thread.h:303)
...
==86131== This conflicts with a previous write of size 8 by thread #2
==86131== Locks held: none
==86131==    at 0x40011EC: work() (hg_race.cpp:7)
```

The shape is isomorphic to TSan's report — two stack sections plus the locks each side held and the locations. Getting the line numbers in the report to match up relies on compiling with `-g`. The difference in cost is large: every instruction passes through a translation, so it is not just a little slow — the manual itself lists performance on its to-do list and admits that being two orders of magnitude slower is not rare. Custom synchronization likewise needs annotations fed in; the interface lives in the helgrind.h header, and the names all carry the ANNOTATE prefix. On the choice, I stand with TSan: when you can get the source, use it — the instrumentation overhead is a notch smaller. When you cannot get the source, or you want to check lock ordering before a deadlock ever happens, that is Helgrind's moment.

## Structured Logging: Making Output Traceable (Extension)

This section carries the (Extension) marker; skipping it does not affect the main thread of this article. The problem it solves, though, you will most likely meet soon. When chasing concurrency issues, many people's first reflex is to reach for printf; three things we have to own up to.

Data races come first; look at this: the standard's clauses state plainly that using the standard stream objects synchronously introduces no data race (see the draft-standard link in the references), and cppreference's cout page says the same thing.

Character interleaving comes second: a note to the clause adds that unsynchronized use can interleave characters — a line of text torn in half by another thread genuinely happens. Even if you always use it synchronously, a log line assembled over several `<<` operations still leaves a gap between two writes where others can cut in.

Third, and most troublesome: bare output carries no timestamp and no thread id — given two lines, we cannot line up which came first.

For a minimal scheme, we set three rules. Each log line is assembled into one complete string locally, and writing takes the lock only once. The timestamp uses `steady_clock` nanoseconds, and the thread identifier comes along. Formatting stays outside the lock — inside the lock there is only one write action; those waiting are queuing for the write, and layout is not the lock's business.

<OnlineCompilerDemo
  title="Structured logging: the whole line assembled, one write inside the lock"
  source-path="code/examples/vol5/chapter06/11_logger.cpp"
  description="Every log line carries a steady_clock nanosecond timestamp and the thread id; formatting happens outside the lock, the write inside it. Two threads each print three lines, every line intact, never torn."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
#include <chrono>
#include <cstdio>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

std::mutex log_mu;

static long long now_ns() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

static void log_line(const std::string& event) {
  std::ostringstream line;
  line << '[' << std::setw(18) << now_ns() << " ns] [thread "
       << std::this_thread::get_id() << "] " << event << '\n';
  std::string s = line.str();
  std::lock_guard<std::mutex> lk(log_mu);
  std::fwrite(s.data(), 1, s.size(), stdout);
}

int main() {
  std::thread t1([] {
    log_line("t1 begin");
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    log_line("t1 halfway");
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    log_line("t1 done");
  });
  std::thread t2([] {
    log_line("t2 begin");
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    log_line("t2 halfway");
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    log_line("t2 done");
  });
  t1.join();
  t2.join();
  log_line("main: both workers joined");
  return 0;
}
```

</OnlineCompilerDemo>

```bash
g++ -std=c++20 -O2 -pthread logger.cpp -o logger   # 普通版配方
./logger
```

```text
[     6448665790006 ns] [thread 127378770622144] t1 begin
[     6448665823209 ns] [thread 127378762229440] t2 begin
[     6448670917121 ns] [thread 127378762229440] t2 halfway
[     6448670922732 ns] [thread 127378770622144] t1 halfway
[     6448676026944 ns] [thread 127378762229440] t2 done
[     6448676026954 ns] [thread 127378770622144] t1 done
[     6448676109375 ns] [thread 127378778429312] main: both workers joined
```

Look at the readings — there is quite a story in there: t2's halfway printed at 6448670917121, t1's halfway at 6448670922732, about 5600 nanoseconds apart; the two threads run offset from each other, each half a beat behind. Every line is complete; not one line torn. Writing logs to a file works the same way: write the whole line as one block, and never let two threads share an unlocked stream. When volume grows, consider C++20's `std::osyncstream`; this article does not expand on it.

One reminder: the timestamps are this machine's own record and do not count as ordering evidence in the standard's sense. For judging visibility, go back to Section 5.3 and count the happens-before paths. Logs help us string the scene together; the basis of the verdict remains the path.

## Your Turn

Before you take this away, work through two questions:

1. How do you trigger each condition in the predicate? An empty queue, a new item, close, stop — manufacture them one by one, and measure how many milliseconds each exit takes.
2. How do you choose between Helgrind and TSan? If you can get the source, use TSan; if you cannot, or you want to check lock ordering in advance, use Helgrind.

## References

The overall approach drew on the testing-and-debugging chapters of Anthony Williams' C++ Concurrency in Action — what I borrowed is the organization and the way of explaining; the examples and the words are my own, and the experiments were compiled and verified on GCC 16.2.1 and clang 23.1.3. Claims about tool behavior were checked against the following manuals and documents (accessed 2026-11-17).

- [Helgrind manual (Valgrind)][hg-manual] — the three classes of checks, the lock-order graph, and the ANNOTATE annotations.
- [GDB online manual][gdb-manual] — the source for `thread apply all bt`.
- [condition_variable_any::wait (cppreference)][cppref-cv-wait] — the wait taking a `stop_token` (C++20).
- [std::cout (cppreference)][cppref-cout] — the safety notes on concurrent access to the standard streams.
- [Current working draft: Standard iostream objects][draft-iostream] — the clause introducing no data race, and the note on character interleaving.
- [C++ Concurrency in Action, 2nd Edition (Anthony Williams)][williams] — the testing and debugging chapters.

[hg-manual]: https://valgrind.org/docs/manual/hg-manual.html
[gdb-manual]: https://sourceware.org/gdb/onlinedocs/gdb/
[cppref-cv-wait]: https://en.cppreference.com/w/cpp/thread/condition_variable_any/wait
[cppref-cout]: https://en.cppreference.com/w/cpp/io/cout
[draft-iostream]: https://eel.is/c++draft/iostream.objects.overview
[williams]: https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition
