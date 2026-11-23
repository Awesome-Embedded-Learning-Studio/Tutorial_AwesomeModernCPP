---
title: "A First std::thread Program"
description: "Write your first multithreaded program by hand: three ways to construct a std::thread, finishing a thread with join or detach, experiments that reveal interleaved output and nondeterministic results, checking how many cores your machine has, getting to know thread identifiers, and understanding why an exception escaping a thread function terminates the program outright."
chapter: 5
order: 3
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
difficulty: intermediate
platform: host
cpp_standard: [11, 20, 23]
translation:
  source: documents/vol5-concurrency/chapter01-start-concurrency/03-first-std-thread.md
  source_hash: 7938daa2cfa2d436340a82b72ce2b051930e76b92148a8df930076f549ad1dda
  translated_at: '2026-11-22T13:28:59+00:00'
  engine: glm-5.3
  token_count: 16310
---
# A First `std::thread` Program

Hey hey hey! What's going on! We're charging straight into code! Yes — as this author stressed earlier, program design demands that you get your hands dirty. Haha! By now I trust you have picked up, however fuzzily, some key points of concurrent program design. You got it.

## First, Let Two Threads Each Do One Thing

First, please allow us to bring out `std::println` (C++23) — something the previous volume already introduced, more or less — to print a few things. Then use `std::thread` (ooh, here comes our protagonist) to start a worker thread, and the job it does is your `finish_download`.

> Of course, if the library you use is on the older side (I mean ones released roughly before January 1, 2026!), you may need to enable experimental features; with a recent GCC you need none of that!

All right. Guess which line appears last.

```cpp
#include <print>
#include <thread>

void finish_download() {
    std::println("Download finished"); // Wow
}

int main() {
    std::thread worker(finish_download);
    std::println("List scrolled");
    worker.join();
    std::println("Showing the completion message");
}
```

<OnlineCompilerDemo
  title="Start a worker thread, then wait for it to finish"
  source-path="code/examples/vol5/chapter01/00_first_thread.cpp"
  run-options="-O2 -std=c++23 -pthread"
  allow-run
/>

Now click it a couple more times! I know you only clicked once!

Thanks to AI: I asked an LLM to pull the executable and run it 250 times by itself (I honestly couldn't be bothered to write a script and tally the numbers). It reported back to me, brimming with enthusiasm:

> We compiled with GCC 16.2.1 using `-O2 -std=c++23 -pthread` and ran it 250 times in a row. 249 of them produced the order below; in one run the first two lines swapped places.

A typical output looks like this:

```text
List scrolled
Download finished
Showing the completion message
```

> Puzzled by the program? No problem! Let's just walk along `main`. The first statement hands the worker thread a job: execute the code in `finish_download`. The main thread can carry on and print `List scrolled`. At `worker.join()` (since C++11), if the worker is still busy, the main thread waits right there for it to finish. Only after the wait does it move on, able to print the completion message.

Why is that last message able to stay at the end? Look back and find which line makes the main thread wait for the worker to finish before moving on. It's `join()`. Earlier we drew that arrow on paper; now you have written this happens-before relationship into the program. [The thread waiting clause in the C++ working draft](https://eel.is/c++draft/thread.thread.member)

The experiments that follow can also be run online directly. The originals were tested under GCC 16.2.1 on WSL2 (16 logical cores, 2026-11-14). Standard-library behavior claims were checked against cppreference, with versions noted. Output order may vary between runs, and timings drift with the machine.

## Three Ways to Construct a Thread

`std::thread` is a standard-library class available since C++11, defined in the `<thread>` header. Its most-used constructor takes a callable object plus a set of arguments to pass to that object. Whatever you give it, it calls it in a new flow of execution. If you want to chase one more layer down into the machine, read this unit's optional article after finishing the experiments. [What a Thread Is on the Machine](./09-what-is-a-thread)

There is more than one way to express "callable"; in order from plainest to state-carrying, let's run all three.

### Level One: Function Pointer

Give it an ordinary function plus a set of arguments, and the thread comes up:

```cpp
// 三种构造方式之一:函数指针
#include <print>
#include <thread>

void print_hello(int id) {
    std::println("hello from thread {}", id);
}

int main() {
    std::thread t(print_hello, 42); // 传递了函数指针，还有他需要的参数
    t.join();
}
```

<OnlineCompilerDemo
  title="Construction method 1 of 3: function pointer"
  source-path="code/examples/vol5/chapter01/10_hello_thread.cpp"
  description="The thread function and its arguments are passed in together at construction; join waits for it to finish."
  run-options="-O2 -std=c++23 -pthread"
  allow-run
/>

We got one line: `hello from thread 42`. The statement `std::thread t(print_hello, 42)` does three things: it packs the function and arguments into the thread object's internal storage, asks the operating system for a new flow of execution, and that new flow calls `print_hello(42)` with the stored arguments.

The arguments are **copied in**. The copy happens in the spawning thread — right now that is the main thread executing `main` — and any exception thrown by a failed copy stays right there too. How the copy happens and how references get passed along is the topic of the next article, Who Owns the Data's Lifetime; here, catching hold of the fact "arguments get copied" is enough to move on.

### Level Two: Lambda

Ah, it can also be a lambda — nothing new there. Sometimes this author takes one look at a job that can be stuffed straight into a lambda, and, well, why not take the little shortcut~

```cpp
// 三种构造方式之二:lambda,按引用捕获要在 join 之后才能读结果
#include <print>
#include <thread>
#include <vector>

int main() {
    std::vector<int> data = {1, 2, 3, 4, 5};
    long long sum = 0;

    std::thread t([&data, &sum] {
        for (int v : data)
            sum += v;
    });
    t.join();
    std::println("sum = {}", sum);
}
```

<OnlineCompilerDemo
  title="Construction method 2 of 3: lambda"
  source-path="code/examples/vol5/chapter01/11_lambda_sum.cpp"
  description="The capture-by-reference sum runs in the thread; only after join does the main thread read the result."
  run-options="-O2 -std=c++23 -pthread"
  allow-run
/>

We got `sum = 15`. This code is safe, but the reason it is safe deserves a clear look: only one thread writes `sum += v`, and the main thread reads `sum` only after `t.join()`. cppreference states on the [`std::thread::join`][cppref-join] page (since C++11) that a synchronization relationship exists between the thread's completion and join's successful return, so the value the main thread reads cannot be a half-finished one. Remove the join, or move the read to before it, and this program turns into exactly the data race that the later article on races will dissect.

The `[&data, &sum]` capture-by-reference has another face too: what you capture is a reference, and if the reference outlives the object, the thread is left holding a dangling reference. The two repair paths wait for the next article.

### Carrying State: Function Objects

The third thing we hand it is an object with an overloaded `operator()` — useful when the task is complex and needs to carry its own state:

```cpp
class Accumulator {
  public:
    explicit Accumulator(const std::vector<int>& data) : data_(data) {}

    void operator()() const {
        long long s = 0;
        for (int v : data_)
            s += v;
        std::println("functor: sum = {}", s);
    }

  private:
    const std::vector<int>& data_; // 引用成员：拷贝进线程的只是引用本身
};

int main() {
    std::vector<int> data = {1, 2, 3, 4, 5};

    Accumulator acc(data);
    std::thread t(acc); // acc 被复制一份进线程
    t.join();
}
```

<OnlineCompilerDemo
  title="Construction method 3 of 3: function object"
  source-path="code/examples/vol5/chapter01/12_functor_thread.cpp"
  description="The object is copied into the thread, and a reference member copies only the reference itself — the dangling risk is left for the next article."
  run-options="-O2 -std=c++23 -pthread"
  allow-run
/>

We got `functor: sum = 15`. The object passed to `std::thread` also gets copied, and that fact must be read together with its reference member: what gets copied into the thread is only the `data_` reference itself, not the vector. As long as `data` outlives the thread, all is well; if it doesn't, the thread is clutching a dangling reference. Judging and fixing this class of problem is also the next article's business.

The function-object route has an old C++ syntactic phenomenon lying in wait for you. Suppose you merge the last two steps into one statement:

```cpp
int main() {
    std::vector<int> data = {1, 2, 3, 4, 5};

    // std::thread t(Accumulator(data)); // 这一行声明的是名叫 t 的函数,不是线程对象

    std::thread t{Accumulator(data)}; // 花括号统一初始化,才是线程对象
    t.join();
}
```

> Written the way the compiler sees it, the parentheses version is `std::thread t(Accumulator data);`: here `Accumulator(data)` reads as a parameter declaration whose type is `Accumulator` and whose parameter name happens to also be `data`, with a redundant pair of parentheses wrapped around the name. So `t` becomes the name of a function — one taking an `Accumulator` and returning a `std::thread`, declared but never defined — and no thread is started at all.

<OnlineCompilerDemo
  title="Most vexing parse: parentheses or braces"
  source-path="code/examples/vol5/chapter01/13_vexing_parse.cpp"
  description="std::thread t(Accumulator(data)) declares a function named t; the wrong form is kept in a comment, and enabling it triggers the -Wvexing-parse warning — the error transcript is in the text."
  run-options="-O2 -std=c++23 -pthread"
  allow-run
/>

This is the infamous "most vexing parse": anything that can be read as a function declaration is treated as one ([\[dcl.ambig.res\]/1](https://eel.is/c++draft/dcl.ambig.res)). We compiled that statement under GCC 16.2.1; the compiler emits a warning, and the next line that uses `t` errors out:

```text
warning: parentheses were disambiguated as a function declaration [-Wvexing-parse]
note:   replace parentheses with braces to declare a variable
error: request for member 'join' in 't',
       which is of non-class type 'std::thread(Accumulator)'
```

The compiler's note already spells out the escape route: swap the parentheses for braces and write `std::thread t{Accumulator(data)}`. The other two ways out are constructing a named object and passing it in (our version above), or simply switching to a lambda. Recent GCC will flag this line for you, but other compilers may not; as a habit, steer clear of it yourself.

With that, we have run all three methods successfully. From here on, examples in this volume favor lambdas, with function objects stepping in when more state needs carrying.

## Start, Wait, Finish: `join` and `detach`

The moment the thread object finishes constructing, the new flow of execution has already been handed to the operating system; when it gets scheduled and how fast it runs are beyond your control. The one thing you do control is how you settle up when the thread ends. `std::thread` offers two routes, `join()` and `detach()` (both since C++11); let's walk them one at a time.

### join: Wait for It to Finish

`join()` is a blocking call: the current thread stands still, waiting for the target thread to finish before moving on. Let's look at an example with a timeline:

```cpp
// join:发起线程的一方等它收工,再继续往下走
#include <chrono>
#include <print>
#include <thread>

void slow_work() {
    std::println("worker  : starting");
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    std::println("worker  : done");
}

int main() {
    std::println("main    : launching a thread");
    std::thread t(slow_work);
    std::println("main    : waiting");
    t.join();
    std::println("main    : thread finished, continuing");
}
```

<OnlineCompilerDemo
  title="join: wait for it to finish"
  source-path="code/examples/vol5/chapter01/14_join_wait.cpp"
  description="The spawning side waits at join; the main thread continues only after the worker wraps up."
  run-options="-O2 -std=c++23 -pthread"
  allow-run
/>

We ran it three times; the output had the same order every time:

```text
main    : launching a thread
main    : waiting
worker  : starting
worker  : done
main    : thread finished, continuing
```

There are two layers of ordering to keep apart. `waiting` appearing before `starting` shows that the main thread didn't stop after releasing the thread — it went on printing, and only then was the worker scheduled in. Both lines happen after construction completes, with no synchronization point in between, so their order is decided by the scheduler; our three runs happened to agree, but a different machine or a different load could change it. The order of `done` and `continuing`, however, is guaranteed by the standard: cppreference's join page states that the thread's completion synchronizes with join's successful return, so `continuing` always follows `done`. The habit of separating which orderings are coincidence and which are standard-guaranteed starts right here with this round of timeline experiments.

### detach: Let It Go

`detach()` takes the other road: it separates the flow of execution from the thread object. The thread keeps running in the background, while the object from then on is associated with no thread at all (in cppreference's words, after detaching `*this` no longer owns any thread — see [the cppreference page for detach][cppref-detach], since C++11). Look at this example:

```cpp
// detach:放开线程自己收尾,主线程先走,进程等全部线程结束才退
#include <chrono>
#include <print>
#include <thread>

void background_task() {
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::println("background task finished");
}

int main() {
    std::thread t(background_task);
    t.detach();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    std::println("main: exiting");
}
```

<OnlineCompilerDemo
  title="detach: let the thread finish on its own"
  source-path="code/examples/vol5/chapter01/15_detach.cpp"
  description="After detach the thread lives and dies on its own; the main thread leaves first, and the process exits only when every thread has ended."
  run-options="-O2 -std=c++23 -pthread"
  allow-run
/>

The background task needs 500 milliseconds; the main thread sleeps only 200 before exiting. We ran it three times, and each time saw only one line, `main: exiting` — `background task finished` never appeared. The reason sits at the process level: when the main thread returns from `main`, the whole process ends, and threads that haven't finished are reaped along with it; that line of output never gets its chance. That is exactly the weight of detach: once you have let go, there is no way left to wait for it. When it finishes — whether it finished at all — the program exits without knowing.

When does detach genuinely get used? In my experience, rarely in application code. It suits tasks whose result nobody waits for and that clean up after themselves; but most tasks that look like that — background logging, say — actually need the program to confirm the data was written before exiting, which calls for an explicit shutdown path rather than letting go. How to turn winding down into an orderly, protocol-driven stop is the dedicated subject of this unit's last article, Letting Work End in an Orderly Way, where you will also meet C++20's `std::jthread`.

### Taking Neither Road: std::terminate

You can call neither join nor detach; the standard set the price long ago. Take away the finishing line that every previous example had, walk straight out of `main` after constructing, and see what happens:

```cpp
// 既不 join 也不 detach:线程对象析构时 std::terminate 直接收走整个进程
#include <print>
#include <thread>

void some_work() {
    std::println("working...");
}

int main() {
    std::thread t(some_work);
    // 既不 join 也不 detach
}
```

<OnlineCompilerDemo
  title="Neither join nor detach: the process gets taken down"
  source-path="code/examples/vol5/chapter01/16_no_join_terminate.cpp"
  description="When the thread object is destroyed, std::terminate takes down the whole process; this example is expected to end in a crash."
  run-options="-O2 -std=c++23 -pthread"
  allow-run
/>

We compiled and ran it once. Not even `working...` appeared on screen; standard error carried one line from libstdc++ (the C++ standard library shipped with GCC), and the process exit code was 134. That number comes from the process being terminated by the SIGABRT signal: 128 plus SIGABRT's number 6 makes exactly 134:

```text
terminate called without an active exception
```

The main thread finishes `main`, destroys `t`, and when a `std::thread` object still associated with a thread is destroyed, the standard's behavior is to call `std::terminate()` (since C++11) — formally documented on cppreference's [destructor page][cppref-dtor]. In our run, terminate got ahead of the worker reaching its output, so not even the `working...` line appeared; on your machine, it may or may not. Objects that are default-constructed, moved-from, already joined, or already detached are all safe to destroy — cppreference lists all four cases on the same page.

Why is the standard so ruthless? If destruction implicitly joined for you, an object leaving scope could stall an entire thread — unexpected blocking inside a destructor is even harder to investigate than a crash. If destruction implicitly detached for you, the thread might keep accessing things that no longer exist after the object is gone, where even a crash would count as a good ending. So the standard flatly refuses to choose for you, forcing you to take a stance on every single path. That stance includes exception paths: when a function throws midway and the stack exits level by level up the call stack, destroying local objects along the way (a process called stack unwinding), an unjoined thread object gets destroyed all the same. Handing the join responsibility to a wrapper that waits automatically in its destructor is the mature fix; we write one in the next article.

## Firsthand Experiments: Interleaved Output, Nondeterminism, and Core Counts

You have seen construction, waiting, and finishing. Now do what this article really wants from you: witness the chaos with your own eyes. The experiments aren't hard; one run beats ten paragraphs of explanation.

### Four Threads Writing to the Screen at Once

Let's take the most intuitive version: four threads each print four log lines, while the main thread waits for all of them to finish:

```cpp
using namespace std::chrono_literals; // C++14 起的时间字面量（如 30ms），靠这行 using 声明引入

void work(int id) {
    for (int step = 0; step < 4; ++step) {
        std::println("worker {} step {}", id, step);
        std::this_thread::sleep_for(30ms);
    }
}

int main() {
    std::vector<std::thread> threads;
    for (int id = 1; id <= 4; ++id) {
        threads.emplace_back(work, id);
    }
    for (auto& t : threads) {
        t.join();
    }
}
```

<OnlineCompilerDemo
  title="Four threads, each advancing at its own pace"
  source-path="code/examples/vol5/chapter01/17_interleaved_workers.cpp"
  description="Four threads each take four steps at 30 milliseconds per step; the flows of execution advance independently and the interleaving of lines differs every run."
  run-options="-O2 -std=c++23 -pthread"
  allow-run
/>

We ran it three times. All three were scrambled, each in a different way; here is the first run's output:

```text
worker 1 step 0
worker 2 step 0
worker 3 step 0
worker 4 step 0
worker 2 step 1
worker 3 step 1
worker 4 step 1
worker 1 step 1
worker worker 4 step 2
2 step 2
worker 3 step 2
worker 1 step 2
worker worker 42worker  step 3 step 3 step 33


worker 1 step 3
```

The first four lines are fairly tidy, each one intact. Look toward the middle: `worker worker 4 step 2` and `2 step 2` are worker 2 and worker 4 tangled together — worker 2 had just written the line-starting `worker` and the space after it when worker 4's whole line, newline included, cut in; afterwards worker 2 went on writing its remaining `2 step 2`. The bottom line is shattered even harder: three threads melted into the same physical line, and the three line-end newlines queued up in a row at the end of the fused line — that is where the two blank lines you see come from. The other two runs scrambled differently: lines stayed intact but the order changed, and worker 1 wasn't necessarily the first to finish printing. Count the lines: all sixteen are there, not one missing, the content is all correct — only the order and shape have completely changed.

One more observation to hand you honestly. We first ran a fast version without `sleep_for`: same four threads, and on our own 16-core machine five out of six runs printed identically — it looked "quite stable." That wasn't the threads behaving themselves; each task was simply so short that the second thread got scheduled only after the first had finished. The shorter the task and the faster the machine, the deeper the chaos hides — but hidden is not the same as absent, and that one differing output from the fast version was it poking its head out. Adding the 30-millisecond sleep in the experiment stretches each task long enough that overlap must happen; write it the same way when you reproduce this on your own machine.

### Different on Every Run

What do you mean, the result is different on every single run?! Yes — this is the jack-in-the-box of concurrency verification. You might run it hundreds or thousands of times and all of them are correct, and then one run goes wrong. Or the scrambling isn't quite the same each time. I believe anyone who has done some vibe coding while chasing performance gains has noticed this now and then.

No more teasing here; this author says it directly: `std::cout` is one object shared program-wide, and four threads are all writing to it. When one output statement chains several `<<` operations together, the standard does not guarantee they form an indivisible whole — whoever finishes writing a small piece can have someone else cut in for a stretch. So everything you observed — line order changing, lines fragmenting, not a single line of content missing — is a manifestation of the same fact: when multiple flows of execution write to the same object with no synchronization in between, the byte stream they produce is interleaved.

We then switched to another formulation to verify the "cutting in" itself. Three threads each sleep a full 50 milliseconds and then start writing simultaneously; each fragment carries no newline, and a newline is printed only after everything is written:

```cpp
using namespace std::chrono_literals;

void chatter(int id) {
    std::this_thread::sleep_for(50ms); // 让三个线程几乎同时醒来
    for (int step = 0; step < 6; ++step) {
        std::print("[w{}:{}]", id, step);
    }
    std::println("");
}

int main() {
    std::vector<std::thread> threads;
    for (int id = 1; id <= 3; ++id) {
        threads.emplace_back(chatter, id);
    }
    for (auto& t : threads) {
        t.join();
    }
}
```

<OnlineCompilerDemo
  title="Interleaved output: nondeterministic results"
  source-path="code/examples/vol5/chapter01/18_chatter_interleave.cpp"
  description="Three threads wake at the same time and each prints six markers back to back; the output order differs on every run — this is the nondeterminism of concurrent programs."
  run-options="-O2 -std=c++23 -pthread"
  allow-run
/>

By your single-threaded intuition, this should be three tidy lines of fragments. We ran it over a thousand rounds; when it scrambles, it looks like below — this is one particular lucky draw from those rounds:

```text
[w[w1:0][w1:1][w1:2][w1:3][w1:4][w1:5]
2:0][w2:1][w2:2][w2:3][w2:4][w2:5]
[w3:0][w3:1][w3:2][w3:3][w3:4][w3:5]
```

We counted deliberately: 85 of 400 rounds were scrambled; the remaining rounds all came out as three tidy lines. Read the captured sample line by line. Worker 2's first fragment had written only the two characters `[w` when worker 1's entire line, newline included, cut in; afterwards worker 2 resumed from `2:0]` and wrote out its remaining fragments. The three newlines, at least, all stayed at the end of their own content — what got spliced in was whole lines of content. When it scrambles and how badly can differ from run to run; this is the live version of that line from the first article: in a concurrent program, "what value does this variable hold right now" needs proof, and `std::cout` output is just its most directly observable form.

> What, you're asking me how to fix it? Wait, wait, wait — that comes later, when we introduce the concept of locks.

## What Happens When a Thread Function Throws

Exceptions must not escape a thread function. Let's just run a program that violates the rule:

```cpp
// 异常逃出线程函数:main 的 try 接不住,std::terminate 收走整个进程
#include <print>
#include <stdexcept>
#include <thread>

void unsafe_worker() {
    throw std::runtime_error("oops, something went wrong");
}

int main() {
    try {
        std::thread t(unsafe_worker);
        t.join();
    } catch (const std::exception& e) {
        std::println("caught: {}", e.what());
    }
}
```

<OnlineCompilerDemo
  title="An exception escaping the thread function: uncatchable"
  source-path="code/examples/vol5/chapter01/20_exception_escapes.cpp"
  description="main's try cannot catch the exception thrown by the worker; std::terminate takes down the whole process. This example is expected to crash."
  run-options="-O2 -std=c++23 -pthread"
  allow-run
/>

The main thread's `try` is right there, and so is its `catch`, yet what we got was exit code 134 and two lines in standard error:

```text
terminate called after throwing an instance of 'std::runtime_error'
  what():  oops, something went wrong
```

Not one line of the `catch` executed. Every thread has its own independent call stack, and exception handling unwinds only within the current thread's stack — stack unwinding being that level-by-level exit-and-destroy process described earlier. The exception thrown by the worker finds no `catch` in the worker's stack able to take it, so `std::terminate()` fires. The main thread's `try` is a whole thread away and out of reach. Change the same program to handle the exception inside the thread, and it ends properly:

```cpp
// 异常在线程函数内部接住:main 正常继续
#include <print>
#include <stdexcept>
#include <thread>

void safe_worker() {
    try {
        throw std::runtime_error("oops, something went wrong");
    } catch (const std::exception& e) {
        std::println("worker caught: {}", e.what());
    } catch (...) {
        std::println("worker caught unknown exception");
    }
}

int main() {
    std::thread t(safe_worker);
    t.join();
    std::println("main continues normally");
}
```

<OnlineCompilerDemo
  title="The exception is caught inside the thread"
  source-path="code/examples/vol5/chapter01/21_exception_caught.cpp"
  description="The try/catch lives inside the thread function; failure is digested on the spot and main continues normally."
  run-options="-O2 -std=c++23 -pthread"
  allow-run
/>

We ran it: both lines present, exit code 0, the program walked away alive. Catching exceptions inside the thread — at minimum a `catch (...)` as the fallback — is basic defense when writing `std::thread` by hand. If you want to faithfully relay a thread's failure back to the main thread for handling, the standard library has a proper route: `std::exception_ptr` can pack the exception up and carry it away, and the ready-made facilities that turn failure-relay into plumbing are `std::async` and `std::promise`/`std::future`; the later articles on task organization cover exactly how to pass them.

## Next Steps

Today's programs stayed on manual the whole way: threads start by construction, finish by join or detach, one missed join on an exception path means terminate, and a scrambled screen has to wait for the locks in the later article on mutexes. The next two articles trade this manual gearbox for deliberate design: Who Owns the Data's Lifetime covers how arguments get passed and when references dangle; Who Owns the Thread's Lifetime covers how to use RAII to take the join responsibility off your memory — that wrapper is the auto-waiting-in-the-destructor thing mentioned earlier, and you will write one by hand. What you hold now is firsthand phenomena; the disorder and terminations you saw today all come back when we discuss races and synchronization.

## Reference Resources

While writing this article I drew on Anthony Williams' C++ Concurrency in Action for its chapter progression. The examples and prose are my own. Claims about C++ behavior were either compiled and tested or verified against cppreference as noted in the text, and every thread facility used in this article has been available since C++11.

- [std::thread (cppreference)][cppref-thread] — an overview of the members, including `get_id` and `native_handle`.
- [std::thread::thread (cppreference)][cppref-ctor] — constructor semantics; the copying of arguments happens in the spawning thread.
- [std::thread::join (cppreference)][cppref-join] — blocking semantics and synchronization guarantees.
- [std::thread::detach (cppreference)][cppref-detach] — detach semantics.
- [std::thread::~thread (cppreference)][cppref-dtor] — the rule that destroying a joinable thread calls `std::terminate`.
- [std::thread::hardware_concurrency (cppreference)][cppref-hw] — hint-value semantics and the case of returning 0.
- [std::thread::native_handle (cppreference)][cppref-native] — the implementation-defined native handle.

[cppref-thread]: https://en.cppreference.com/w/cpp/thread/thread
[cppref-ctor]: https://en.cppreference.com/w/cpp/thread/thread/thread
[cppref-join]: https://en.cppreference.com/w/cpp/thread/thread/join
[cppref-detach]: https://en.cppreference.com/w/cpp/thread/thread/detach
[cppref-dtor]: https://en.cppreference.com/w/cpp/thread/thread/~thread
[cppref-hw]: https://en.cppreference.com/w/cpp/thread/thread/hardware_concurrency
[cppref-native]: https://en.cppreference.com/w/cpp/thread/thread/native_handle
