---
title: "Who Owns the Thread's Lifetime"
description: "See clearly std::thread's move-only ownership and the three failure paths around join, and write the wind-down into a destructor with thread_guard."
chapter: 5
order: 5
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
difficulty: intermediate
platform: host
cpp_standard: [11, 20]
translation:
  source: documents/vol5-concurrency/chapter01-start-concurrency/05-thread-lifetime-and-raii.md
  source_hash: 7c72caf908c9dbdd783c41ddaf995bc649df6663cc301fa3834310689a430d15
  translated_at: '2026-11-22T13:28:59+00:00'
  engine: glm-5.3
  token_count: 10501
---
# Who Owns the Thread's Lifetime

All right! Don't actually forget what we set out to do! What we're handling now is this: join sits at the end of the function, one exception and it gets skipped — so who is responsible for catching that, eh?

The answer is revealed: hand it to the destructor.

## Ownership and RAII: thread_guard and Exception Safety

The whole article needs just one tool, the RAII (Resource Acquisition Is Initialization) you used back in Volume 2: write the resource's wind-down into the destructor, it runs automatically when the object leaves scope, and not even a passing exception slips past it.

### The Thread Object Is Itself a Resource

Before laying hands on managing it, answer one question first: after `std::thread t(work);`, what exactly is `t` itself? Answer: a resource, backed by one operating-system thread. And this resource recognizes exactly one owner — move only, no copying. We tried copying it once:

<OnlineCompilerDemo
  title="std::thread is not copyable"
  source-path="code/examples/vol5/chapter01/30_thread_not_copyable.cpp"
  description="The copy constructor is deleted and it doesn't compile; the erroneous form is kept in a comment, and ownership can only move via std::move."
  run-options="-O2 -std=c++23 -pthread"
  allow-run
>

```cpp
// std::thread 不可拷贝:拷贝构造被删除,编译不过
// 错误写法留在注释里,放开后报错见正文;所有权只能 move
#include <chrono>
#include <print>
#include <thread>

void work() {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

int main() {
    std::thread a(work);
    // std::thread b = a;  // 编译不过
    std::thread b = std::move(a); // 所有权转移
    b.join();
    std::println("a joinable after move: {}", a.joinable());
}
```

</OnlineCompilerDemo>

```text
error: use of deleted function ‘std::thread::thread(const std::thread&)’
```

The deleted copy constructor is deliberate design: one operating-system thread **can be managed by exactly one object at any given moment**, and allowing copies would produce the chaos of two objects racing to join the same thread.

So the thread can't be copied; ownership can only move. After the move, what is the source object's relationship to the thread? Look at joinable:

<OnlineCompilerDemo
  title="joinable and move: the source is empty after the transfer"
  source-path="code/examples/vol5/chapter01/31_joinable_move.cpp"
  description="After the move, asking joinable again: the source thread is already false, and the new object takes over the flow of execution."
  run-options="-O2 -std=c++23 -pthread"
  allow-run
>

```cpp
// joinable 与 move:转移之后,源线程不再关联执行流
#include <print>
#include <thread>

void work() {}

int main() {
    std::thread t1(work);
    std::println("t1 joinable: {}", t1.joinable());
    std::thread t2 = std::move(t1);
    std::println("after move: t1 joinable {}, t2 joinable {}", t1.joinable(), t2.joinable());
    t2.join();
}
```

</OnlineCompilerDemo>

We got `t1 joinable: 1`, and after the move `t1 joinable 0` and `t2 joinable 1` — the source object handed over authority completely. This shape is no stranger to you; `std::unique_ptr` is exactly it, an old friend from Volume 2. The difference lies in the wind-down: `unique_ptr` releases the old resource on assignment, while `std::thread` goes straight to terminate when the target is still joinable (that temper gets its own section below). The standard library's `std::fstream` and `std::future` belong to this same family.

### join on the Exception Path: thread_guard

First spot: an exception skips the join. In A First std::thread Program we saw two kinds of terminate — the no-join kind, and the exception-escaping-the-thread-function kind. The third kind we haven't seen yet: the main thread itself throws, jumping past the `join()` written after it. Picture the scene: join sits on the function's last line, some line in the middle blows up, and stack unwinding (the process of the exception exiting level by level up the call stack, destroying locals along the way) walks right up to that still-unjoined thread object — terminate all the same.

The fix was foreshadowed at the end of A First std::thread Program: that wrapper which waits automatically in its destructor. Here we give the version that holds a reference and does not own the thread (joining in the destructor by default). The book calls it thread_guard; in our code we spell it ThreadGuard — two spellings of the same thing. Anthony Williams' C++ Concurrency in Action gave this classic form; following its progression we wrote our own version, presented here together with a verification program:

<OnlineCompilerDemo
  title="ThreadGuard: even an exception can't spoil the ending"
  source-path="code/examples/vol5/chapter01/32_thread_guard.cpp"
  description="During stack unwinding the guard's destructor joins for you, and no thread is lost even when an exception passes through."
  run-options="-O2 -std=c++23 -pthread"
  allow-run
>

```cpp
// ThreadGuard:栈展开时析构函数替您 join,异常也收得了场
#include <chrono>
#include <print>
#include <stdexcept>
#include <thread>

class ThreadGuard {
  public:
    enum class Action { kJoin, kDetach };
    explicit ThreadGuard(std::thread& t, Action action = Action::kJoin)
        : thread_(t), action_(action) {}
    ~ThreadGuard() {
        if (thread_.joinable()) {
            if (action_ == Action::kJoin)
                thread_.join();
            else
                thread_.detach();
        }
    }
    ThreadGuard(const ThreadGuard&) = delete;
    ThreadGuard& operator=(const ThreadGuard&) = delete;

  private:
    std::thread& thread_; // 持有引用，不拥有线程
    Action action_;
};

void risky() {
    throw std::runtime_error("boom");
}

void run_with_guard() {
    std::thread t([] {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        std::println("  worker finished its work");
    });
    ThreadGuard guard(t);
    risky(); // 抛出，栈展开时 guard 的析构函数 join 线程
}

int main() {
    try {
        run_with_guard();
    } catch (const std::exception& e) {
        std::println("caught: {}", e.what());
    }
    std::println("main continues normally");
}
```

</OnlineCompilerDemo>

It prints three lines, and the order is a little interesting — read them yourself first before reading on:

```text
  worker finished its work
caught: boom
main continues normally
```

`risky()` throws the exception, `run_with_guard`'s stack starts unwinding, `guard`'s destructor gets called, join blocks for 150 milliseconds waiting for the worker to finish its job, and only then does main's catch take the exception. Remove the guard and this program ends in terminate, with the worker's output never getting its chance to print. With the guard, you have written the exception-path join into a destructor: it no longer depends on your remembering to call it — the destructor executes itself when the scope exits.

This version is a half-finished piece, with two weaknesses in plain view. First, the guard holds a reference; ownership of the thread stays in the outer `t`'s hands, and the code carries no answer for who winds things down, `t` or `guard`. Second, the referenced thread object must outlive the guard: if it is destroyed a step earlier (a heap delete, member destruction order, that sort of thing) while still joinable, the thread's own destructor terminates on the spot, never reaching the guard's destructor; if already joined or detached, then what the guard's destructor goes to read is a destroyed object — undefined behavior. This version proves exactly one thing: a destructor can catch the join. Who owns it — unanswered. Let that suspense hang for now.

### join Itself Can Throw Too

Second spot: join itself throws. You might think, isn't join just waiting? How can waiting go wrong?

Man! It really can. cppreference states on [the join page][cppref-join] that on error it throws `std::system_error` (an exception carrying an error code; `code()` fetches the code, `message()` gives the text). It lists three conditions: `joinable()` false, no such thread, and a thread joining itself. That last one is the most eye-catching — the condition is named `resource_deadlock_would_occur`. Of the three, we picked two to test for real: double join, and self join:

<OnlineCompilerDemo
  title="join's error paths: system_error"
  source-path="code/examples/vol5/chapter01/33_join_throws.cpp"
  description="Self-join throws resource_deadlock_would_occur, double join throws invalid_argument — both catchable."
  run-options="-O2 -std=c++23 -pthread"
  allow-run
>

```cpp
// join 的错误路径:join 自己、重复 join,各抛一个 system_error
#include <chrono>
#include <print>
#include <system_error>
#include <thread>

std::thread* g_self = nullptr;

void self_joiner() {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    try {
        g_self->join(); // 线程 join 自己
    } catch (const std::system_error& e) {
        std::println("  self join threw: {}", e.code().message());
    }
}

void work() {}

int main() {
    std::thread t(work);
    t.join();
    try {
        t.join(); // 已经 join 过，joinable() 为假
    } catch (const std::system_error& e) {
        std::println("  second join threw: {}", e.code().message());
    }
    std::thread s(self_joiner);
    g_self = &s;
    s.join();
    std::println("main continues");
}
```

</OnlineCompilerDemo>

```text
  second join threw: Invalid argument
  self join threw: Resource deadlock avoided
main continues
```

That `Resource deadlock avoided` on the second line is a literal rendering of the EDEADLK errno (resource deadlock avoided); it reads awkwardly, but it means the thread joined itself and the implementation stopped it on the spot. The trouble follows right behind: we just wrote join into a destructor, and destructors must not throw outward — a second exception escaping mid-destruction is terminate. So the pragmatic way is to wrap join in a try, note the failure, and throw nothing. Written for real, with `thread_` being the `std::thread` in the wrapper's hand, it looks like this:

> Oh, don't rush to copy this — the more modern C++20 has already built jthread into the standard library. The next article introduces exactly that!
> Reading this one is more about understanding what is happening.

<OnlineCompilerDemo
  title="JoiningThread's destructor: in focus"
  source-path="code/examples/vol5/chapter01/34_joining_thread_dtor.cpp"
  description="Join only when joinable, and never let the exception escape the destructor — the whole point of auto-join lives in these few lines."
  run-options="-O2 -std=c++23 -pthread"
  allow-run
>

```cpp
// JoiningThread 的析构:focus 在析构函数上——joinable 才 join,失败不逃出析构
#include <chrono>
#include <print>
#include <system_error>
#include <thread>
#include <utility>

class JoiningThread {
  public:
    explicit JoiningThread(std::thread t) noexcept : thread_(std::move(t)) {}

    ~JoiningThread() {
        if (thread_.joinable()) { // 如果是可以被join的
            try {
                thread_.join();
            } catch (const std::system_error& e) {
                std::println("join failed in destructor: {}", e.what());
            }
        }
    }

    JoiningThread(const JoiningThread&) = delete;
    JoiningThread& operator=(const JoiningThread&) = delete;

  private:
    std::thread thread_;
};

int main() {
    std::println("entering scope");
    {
        std::thread raw([] {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            std::println("  worker done");
        });
        JoiningThread t(std::move(raw));
        std::println("scope body, nobody calls join by hand");
    } // 析构在这里替我们 join
    std::println("scope left, worker already joined");
}
```

</OnlineCompilerDemo>

### move-Assigning a Bare Thread Is terminate

Third spot: move assignment. Earlier the move constructor was smooth sailing; assignment hides a big one: when the target object is still associated with a thread (`joinable() == true`) and gets move-assigned, the standard directly prescribes calling `std::terminate()` ([the operator= page][cppref-opassign], since C++11). Sounds exaggerated — we tested it; keep your eye on the exit code:

<OnlineCompilerDemo
  title="move-assigning onto a still-associated thread: terminate"
  source-path="code/examples/vol5/chapter01/35_move_assign_terminate.cpp"
  description="The target is still joinable when assigned over, and std::terminate wraps it up on the spot; this example is expected to crash."
  run-options="-O2 -std=c++23 -pthread"
  allow-run
>

```cpp
// move 赋值给已关联线程的对象:std::terminate 直接收场,本示例预期崩溃
#include <chrono>
#include <print>
#include <thread>

void slow_work() {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
}

int main() {
    std::println("starting two threads, then move-assign over a joinable one");
    std::thread t1(slow_work);
    std::thread t2(slow_work);
    t2 = std::move(t1); // t2 还关联着线程
}
```

</OnlineCompilerDemo>

Compile and run: standard error has a single line, exit code 134. An old acquaintance, brothers — we did the arithmetic of 128 plus 6 back in A First std::thread Program: SIGABRT:

```text
terminate called without an active exception
```

By this point the bare `std::thread` has shown its full face: move construction is smooth, while move-assigning onto an unfinished object terminates on the spot. The three trouble spots are done; looking back, they are three segments of one line: an exception skipping join, join throwing by itself, assignment terminating — all of it staked out by "the wind-down rides on your memory." The way out was pointed out long ago: hand things to a wrapper that takes over ownership, winds down in its destructor, and closes out the old one before an assignment. C++20 went ahead and paved this road into the standard library: `std::jthread`'s destructor requests a stop first and then joins, and its move assignment closes out the old before taking on the new. It is the protagonist of the next article.

## Reference Resources

While writing this article I drew on Anthony Williams' C++ Concurrency in Action (Manning, second edition): the thread_guard progression comes from it, and the destructor-focused JoiningThread snippet was written along its lines; the examples and prose are my own. Claims about C++ behavior were compiled and tested or verified against cppreference as noted in the text. The thread and thread_guard facilities have been available since C++11; jthread and the stop protocol are C++20.

- [std::thread::~thread (cppreference)][cppref-dtor] — the rule that destroying a joinable thread calls terminate.
- [std::thread::join (cppreference)][cppref-join] — synchronization guarantees and the three system_error error conditions.
- [std::thread::operator= (cppreference)][cppref-opassign] — the rule that move-assigning over a joinable thread calls terminate.
- [std::thread::detach (cppreference)][cppref-detach] — detach semantics.
- [std::jthread (cppreference)][cppref-jthread] — C++20's auto-joining thread and the stop protocol.
- [C++ Core Guidelines CP.24][cp24] and [CP.25][cp25] — two engineering recommendations, thinking of a thread as a global container and preferring gsl::joining_thread; the same idea as our hand-written JoiningThread (the GSL README notes that type is not yet implemented).

[cppref-dtor]: https://en.cppreference.com/w/cpp/thread/thread/~thread
[cppref-join]: https://en.cppreference.com/w/cpp/thread/thread/join
[cppref-opassign]: https://en.cppreference.com/w/cpp/thread/thread/operator%3D
[cppref-detach]: https://en.cppreference.com/w/cpp/thread/thread/detach
[cppref-jthread]: https://en.cppreference.com/w/cpp/thread/jthread
[cp24]: https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#cp24-think-of-a-thread-as-a-global-container
[cp25]: https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#cp25-prefer-gsljoining_thread-over-stdthread
