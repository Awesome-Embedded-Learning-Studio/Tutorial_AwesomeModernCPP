---
title: "How `co_await` Joins the Wait"
description: "The protocol a co_await expression expands under, which question each piece of the awaiter trio answers, how to write a value-carrying gate, where operator co_await and await_transform enter, and how symmetric transfer detaches stack depth from chain depth."
chapter: 5
order: 3
tags:
  - host
  - cpp-modern
  - intermediate
  - coroutine
  - 异步编程
difficulty: intermediate
platform: host
cpp_standard: [20]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
translation:
  source: documents/vol5-concurrency/chapter07-coroutines/03-co-await-and-awaiters.md
  source_hash: 36b41751b1e98a0a8515ec51fa08875b569a029c100def7e69e4e4ea26201c17
  translated_at: '2026-11-22T14:04:00+00:00'
  engine: glm-5.3
  token_count: 20573
---

# How `co_await` Joins the Wait

This article circles around `co_await` alone: you write it down, and the compiler asks a string of questions on your behalf — whom to ask, what to ask, and how the answers rewrite the control flow. We look at them one by one.

For the four experiments, the code blocks are the entire content; assemble them into a single .cpp file to rerun. The two probes only seek error messages; the original error texts are already pasted in the body, and they produce no runnable output.

## The awaiter and awaitable Protocol

`co_await expr`, read in one sentence: reaching it, the compiler obtains an object from expr and has it answer three questions. Are you ready? If not, what happens after suspending? And when resuming, what do you bring back? The three questions map to three member functions — `await_ready`, `await_suspend`, `await_resume` — and that is the trio.

The terminology aligns here too. A type that can appear after `co_await` is an **awaitable**. The one that actually carries the trio and takes over the expansion is an **awaiter**. A type can hold both roles at once — Park, the standard library's suspension bodies, and the Gate we are about to write all wear both identities.

The standard's account is finer than the one-sentence reading. For expanding an await expression, the draft [expr.await] lists a set of numbered steps; let's walk them in order (item numbers aligned with the current draft accessed 2026-11-17):

1. The operand is evaluated as usual. If the promise's type has `await_transform`, expr must pass through its hands, and only what comes out counts. The initial suspension point, the final suspension point, and the await-expressions produced by `co_yield` do not go through it — the promise cannot intercept the three suspensions the language itself arranges ([expr.await]/(3.2)).
2. Look for `operator co_await`. The member and non-member versions enter one overload resolution together; if no unique one can be selected, the program is ill-formed. If there is not a single viable candidate, the operand itself serves as the awaiter ([expr.await]/(3.3); the candidate-composition rules are in [over.match.oper]/3 and /7).
3. Evaluate `await_ready()`, contextually converted to bool ([expr.await]/(3.6)). If it returns true, jump straight to step 6 — nothing suspends.
4. Returns false: the coroutine is considered suspended, and then `await_suspend(h)` is evaluated, with h the handle referring back to this coroutine ([expr.await]/(5.1)). The suspension happens before await_suspend enters; the coroutine you see inside it is already hung up.
5. The return type of `await_suspend` has only three legal forms: void, bool, and `coroutine_handle<Z>` ([expr.await]/(3.7)). Returning void hands control back to this coroutine's caller or resumer. Returning bool: when the result is false, the coroutine resumes on the spot. Returning a handle: its `.resume()` is then evaluated — the draft states in note (5.1.1) that a string of coroutines can relay resumption all the way down, finally returning control to the original caller or resumer. If await_suspend throws midway, the exception is caught, the coroutine is resumed, and the exception is not thrown to the caller or resumer on the spot but rethrown inside the coroutine body ([expr.await]/(5.1)).
6. On resumption, `await_resume()` is evaluated — immediately, if ready at step 3 was true — and its return value is the value of the whole `co_await` expression ([expr.await]/(5.2)); the expression's type and value category match it ((4)).

The parts are laid out; bring back Park from the first article — now you can read it line by line:

<OnlineCompilerDemo
  title="Park's trio: suspend and register, wait for someone else to resume"
  source-path="code/examples/vol5/chapter07/06_park_awaiter.cpp"
  description="The snippet completed into a runnable file: worker reaches co_await Park{}, suspends and registers; main counts the handle and resumes it; worker continues from the suspension point and finishes."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// park_awaiter.cpp —— Park：三件套的最小实现，挂起登记，等别人来恢复
#include <coroutine>
#include <cstdio>
#include <exception>
#include <vector>

struct Park {
  static std::vector<std::coroutine_handle<>> parked;
  bool await_ready() { return false; }
  void await_suspend(std::coroutine_handle<> h) { parked.push_back(h); }
  void await_resume() {}
};
std::vector<std::coroutine_handle<>> Park::parked;

// 最小任务类型，撑起 co_await（急启动，7.1 的同款）
struct Task {
  struct promise_type {
    Task get_return_object() {
      return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
    }
    std::suspend_never initial_suspend() { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    void return_void() noexcept {}
    void unhandled_exception() noexcept { std::terminate(); }
  };

  std::coroutine_handle<> h;
  explicit Task(std::coroutine_handle<> hh) : h(hh) {}
  Task(const Task&) = delete;
  Task& operator=(const Task&) = delete;
  ~Task() { if (h) h.destroy(); }
};

Task worker() {
  std::printf("  worker: 跑到 co_await Park{}，挂起登记\n");
  co_await Park{};
  std::printf("  worker: 被恢复，从挂起点接着跑\n");
}

int main() {
  Task t = worker();
  std::printf("main: 登记了 %zu 个句柄\n", Park::parked.size());
  Park::parked.back().resume();
  std::printf("main: worker 完工，程序结束\n");
  return 0;
}
```

</OnlineCompilerDemo>

Three member lines, exactly the answers to the three questions; let's match them one by one. `await_ready` always returns false — Park does not know when the thing will come, so it simply suspends every time. `await_suspend` registers the handle into the list and does nothing more; control returns to the resumer, and the coroutine sleeps in the list. `await_resume` is empty — all Park awaits is "my turn has come", with nothing else to carry. In the first article's act one, the driver thread resumed it; who resumes and when, Park does not care at all. The void return type's use sits right here: the handover is left entirely to the outside.

The standard library's suspension bodies sit on the same protocol — the previous article used them throughout: `suspend_always`'s `await_ready` is always false, `suspend_never`'s is always true, and both of their `await_suspend` and `await_resume` do nothing ([coroutine.trivial.awaitables]). You can now see for yourself: they are minimal implementations of the trio, the same kind of thing as Park.

There is a spot easy to misread; let's stop and clear it up. `await_ready` returning true means "don't suspend, carry on". `await_suspend` returning true means "suspend". Both functions answer the suspend-or-not question, but their answers point in opposite directions. The first time I read the bool version of await_suspend, I paused right here too; the mnemonic that finally stuck: ready asks "are you done?" — done, and naturally no suspension needed. suspend asks "suspend?" — only true suspends.

The bool return type is not a strange taste either; it has legitimate work. Consider a typical scene: await_suspend plans to register the handle, and the registration might race with the completion event — another thread happens to finish the job at exactly this moment; return false, the coroutine continues on the spot, and a pointless suspend-and-resume pair is saved entirely. Lewis Baker's operator co_await article gives a complete manual-reset event implementation — the bool version is made precisely for this race window.

## Implementing a Minimal Awaitable

With the protocol well read, let's write an object that waits for something, with our own hands. The goal stays small: a gate that carries a value. A coroutine doing `co_await gate` suspends and registers when the gate holds no value, gets woken when a value arrives, and wakes with the value in hand. If the value is already inside, it takes the fast path and never suspends at all.

<OnlineCompilerDemo
  title="A value-carrying gate: the minimal awaitable Gate"
  source-path="code/examples/vol5/chapter07/07_exp_c_gate.cpp"
  description="Act one: the value arrives late, consumer suspends and registers, set(42) wakes it and brings the value back through await_resume. Act two: the value is already in the gate, and await_ready's fast path never suspends at all."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// exp_c_gate.cpp —— 最小可等待对象：一扇带值的门
#include <coroutine>
#include <cstdio>
#include <exception>
#include <optional>

// 门：还没有值时，co_await 它的协程挂起登记，有值了，恢复时把值带回来
struct Gate {
  std::optional<int> value;            // 门里的值
  std::coroutine_handle<> waiter{};    // 登记在门口的协程，至多一位

  bool await_ready() { return value.has_value(); }  // 值已经在，不必挂起
  void await_suspend(std::coroutine_handle<> h) { waiter = h; }
  int await_resume() { return *value; }             // 恢复时把门里的值带回去

  void set(int v) {  // 送值进门，顺路叫醒登记的人
    value = v;
    if (waiter) {
      auto w = waiter;
      waiter = {};
      w.resume();
    }
  }
};

struct Task {
  struct promise_type {
    Task get_return_object() {
      return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
    }
    std::suspend_never initial_suspend() noexcept { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    void return_void() noexcept {}
    void unhandled_exception() noexcept { std::terminate(); }
  };

  std::coroutine_handle<> h;
  explicit Task(std::coroutine_handle<promise_type> hh) : h(hh) {}
  Task(const Task&) = delete;
  Task& operator=(const Task&) = delete;
  ~Task() { if (h) h.destroy(); }
};

Task consumer(Gate& gate) {
  std::printf("  consumer: 来到门口，准备等一个值\n");
  int v = co_await gate;
  std::printf("  consumer: 恢复了，从 await_resume 拿到 %d\n", v);
}

int main() {
  { // 幕一：值后到，协程挂起登记，set 把它叫醒
    Gate gate;
    Task t = consumer(gate);
    std::printf("main: consumer 登记了吗？waiter=%s\n", gate.waiter ? "有" : "没有");
    std::printf("main: 送 42 进门\n");
    gate.set(42);
    std::printf("main: 回到 main，一幕结束\n");
  }
  { // 幕二：值提前在门里，await_ready 走快路径，协程根本不挂起
    Gate gate;
    gate.set(7);
    std::printf("main: 幕二开场，7 已经在门里\n");
    Task t = consumer(gate);
    std::printf("main: 幕二结束，waiter=%s\n", gate.waiter ? "有" : "没有");
  }
  return 0;
}
```

</OnlineCompilerDemo>

```bash
g++ -std=c++20 -O2 -pthread exp_c_gate.cpp -o exp_c_gate
./exp_c_gate
```

```text
  consumer: 来到门口，准备等一个值
main: consumer 登记了吗？waiter=有
main: 送 42 进门
  consumer: 恢复了，从 await_resume 拿到 42
main: 回到 main，一幕结束
main: 幕二开场，7 已经在门里
  consumer: 来到门口，准备等一个值
  consumer: 恢复了，从 await_resume 拿到 7
main: 幕二结束，waiter=没有
```

Read line against line; both acts' orderings are as expected, and both deserve a glance. Act one: consumer reaches `co_await gate`, the gate is empty, `await_ready` returns false, the coroutine suspends, and `await_suspend` registers the handle into waiter. Control returns to main — consumer started working at creation and ran until the first suspension point, the eager-start behavior you observed in the previous article's hands-on item 2, matching exactly here. So the answer to the immediately following "did it register?" print is "yes". set(42) puts the value into the gate, fishes the registered handle out, and resumes it; consumer continues from the `co_await` position, `await_resume` brings `*value` back, and 42 lands on v. Look at the print order: consumer's second line sits between "send 42 into the gate" and "back to main" — the resumption happens inside set's function body.

Act two: 7 entered the gate ahead of time. When consumer reaches `co_await`, `await_ready` sees the value already there and returns true; steps 4 and 5 are skipped entirely, the coroutine suspends not one line, and it takes the value straight from `await_resume`. Look at waiter — empty to the very end; consumer was never registered at any point.

Let's pause one step and see what the gate itself discloses. Gate writes no `operator co_await`, yet `co_await gate` stands, because when step 2 of the protocol finds no operator co_await, the operand itself is the awaiter — and Gate happens to carry the full trio. A truly asynchronous version differs only in where the registration goes: inside `await_suspend`, the handle should not be stored on one's own member but handed to a timer, an event loop, or a completion queue, letting them resume it at the right time. Who is responsible for executing the resumption is settled at the moment `await_suspend` writes down the registration. The mechanism itself does not govern the timing of resumption; it governs the channel of the handover.

> One more small detail worth noting: where the awaiter lives. In `co_await gate`, gate is a local variable of main, so housing is not a question. If you write `co_await Gate{}`, the awaiter is a temporary whose lifetime must cross the suspension point, and the standard arranges for it to move into the coroutine frame (as the previous article covered, local variables and temporaries crossing suspension points all live in the frame). Library writers often borrow this in reverse: put each operation's intermediate state into the awaiter and let the coroutine frame supply the space — one allocation, reused over and over.

## operator co_await and the ADL Customization Point (Extension)

This section carries the (Extension) marker; skip it and this article's main line stays complete. If you want to know how third-party types get awaiting support, and why `co_await 250` compiles, please stay.

Step 2 of the protocol has not been examined closely; let's fill that in here. `operator co_await` is the customization point of awaiting, and you choose where it lands:

- Write it as a member function, `a.operator co_await()`, living in the type's own body. When you write a task type you mostly choose this: the task gets a member operator co_await returning its own awaiter.
- Write it as a non-member function, `operator co_await(a)`. The candidates are not picky about origin: whatever ordinary lookup finds, plus what ADL (argument-dependent lookup) finds along the argument's namespaces, enters the same overload resolution together with the member version ([over.match.oper]/3 and /7). Where it shines for us is on types whose source we cannot change: someone else's type cannot be touched, but the customization point can move into that type's namespace and still be found along its provenance.
- Write nothing at all, and the operand carries the trio itself. Gate and Park use exactly this, as you saw in the previous section.

Let's try the non-member version on a type that cannot house the customization point. The scenario is fictional; the mechanism is real: a queueing Ticket lives in the vendor namespace — pretend it is someone else's library whose source cannot be touched. The way to give it awaiting support is to go back into its namespace and place a free operator co_await returning an awaiter carrying the trio:

<OnlineCompilerDemo
  title="Fitting operator co_await onto a third-party type you cannot change"
  source-path="code/examples/vol5/chapter07/08_exp_e_adl.cpp"
  description="Ticket in the vendor namespace cannot have its source touched; the free operator co_await is found through ADL, TicketAwaiter takes over the trio, and queue number 7 comes back through await_resume."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// exp_e_adl.cpp —— 给改不了的第三方类型配 operator co_await
#include <coroutine>
#include <cstdio>
#include <exception>

namespace vendor {  // 假装这是别人的库，源码动不了
  struct Ticket {
    int number;
    explicit Ticket(int n) : number(n) {}
  };

  struct TicketAwaiter {
    Ticket t;
    bool await_ready() { return false; }
    void await_suspend(std::coroutine_handle<> h) {
      std::printf("  [awaiter] 协程挂好了，真异步在这里交给调度器\n");
      h.resume();   // 演示从简：登记完当场恢复
    }
    int await_resume() { return t.number; }
  };

  // 定制点住进 vendor 命名空间，ADL 顺着 Ticket 的来路就找到它
  TicketAwaiter operator co_await(Ticket t) { return TicketAwaiter{t}; }
}

struct Task {
  struct promise_type {
    Task get_return_object() {
      return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
    }
    std::suspend_never initial_suspend() noexcept { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    void return_void() noexcept {}
    void unhandled_exception() noexcept { std::terminate(); }
  };
  std::coroutine_handle<> h;
  explicit Task(std::coroutine_handle<promise_type> hh) : h(hh) {}
  Task(const Task&) = delete;
  Task& operator=(const Task&) = delete;
  ~Task() { if (h) h.destroy(); }
};

Task caller() {
  std::printf("  caller: 写下 co_await vendor::Ticket(7)\n");
  int n = co_await vendor::Ticket(7);
  std::printf("  caller: await_resume 把排队号 %d 带了回来\n", n);
}

int main() {
  Task t = caller();
  std::printf("main: 回到 main\n");
  return 0;
}
```

</OnlineCompilerDemo>

```text
  caller: 写下 co_await vendor::Ticket(7)
  [awaiter] 协程挂好了，真异步在这里交给调度器
  caller: await_resume 把排队号 7 带了回来
main: 回到 main
```

You can now recite how `co_await vendor::Ticket(7)` walks. Ticket itself lacks the trio; the compiler follows its namespace to the free function, TicketAwaiter takes over, the trio runs the full protocol course, and `await_resume` brings queue number 7 back. The whole content of the customization lands on "making an awaiter from a Ticket" — the three questions remain the three questions.

I probed two corners too, both verified with probes. With member and non-member both present and both matching, overload resolution rules ambiguity outright and the program is ill-formed; GCC's error is `ambiguous overload for ‘operator co_await’`. The other corner is awaiting a fundamental type: `co_await 1000` does not compile — int has no class scope to house a member version, no associated namespaces to house a non-member version, and the operand itself lacks the trio; GCC's error is `awaitable type ‘int’ is not a structure`. To make `co_await 250` stand, the landing point returns to step 1 of the protocol, await_transform: the promise defines it, int passes through its hands, and becomes an awaiter.

<OnlineCompilerDemo
  title="await_transform: giving co_await on an int a landing point"
  source-path="code/examples/vol5/chapter07/09_exp_g_transform.cpp"
  description="The promise defines await_transform; co_await 250 is translated into SleepAwaiter, and the countdown fires after three rounds."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// exp_g_transform.cpp —— promise 配了 await_transform，co_await 一个 int 就有了落点
#include <coroutine>
#include <cstdio>
#include <exception>

struct SleepAwaiter {
  int ms;
  bool await_ready() { return false; }
  void await_suspend(std::coroutine_handle<> h) {
    std::printf("  [awaiter] 该等 %d 毫秒，真异步在这里交给定时器\n", ms);
    h.resume();   // 演示从简：登记完当场恢复
  }
  void await_resume() {}
};

struct Task {
  struct promise_type {
    Task get_return_object() {
      return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
    }
    std::suspend_never initial_suspend() noexcept { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    void return_void() noexcept {}
    void unhandled_exception() noexcept { std::terminate(); }
    SleepAwaiter await_transform(int ms) { return SleepAwaiter{ms}; }
  };
  std::coroutine_handle<> h;
  explicit Task(std::coroutine_handle<promise_type> hh) : h(hh) {}
  Task(const Task&) = delete;
  Task& operator=(const Task&) = delete;
  ~Task() { if (h) h.destroy(); }
};

Task countdown() {
  for (int i = 3; i > 0; --i) {
    std::printf("  倒计时 %d\n", i);
    co_await 250;
  }
  std::printf("  点火\n");
}

int main() {
  Task t = countdown();
  std::printf("main: 回到 main\n");
  return 0;
}
```

</OnlineCompilerDemo>

```text
  倒计时 3
  [awaiter] 该等 250 毫秒，真异步在这里交给定时器
  倒计时 2
  [awaiter] 该等 250 毫秒，真异步在这里交给定时器
  倒计时 1
  [awaiter] 该等 250 毫秒，真异步在这里交给定时器
  点火
main: 回到 main
```

await_transform has another face; let's keep looking. Once the promise defines it, every ordinary `co_await` must pass through its hands, and it becomes the coroutine type's gatekeeper: banning certain types, adding logging uniformly, inserting cancellation checks — the hooks all go in here. The initial and final suspension points are not its business; when the promise picks suspension bodies for `initial_suspend` and `final_suspend` it bypasses the transform, and the draft spells out the skipped scope in [expr.await]/(3.2).

## Symmetric Transfer: Handing Over Control While Resuming (Extension)

This section also carries the (Extension) marker; skip it and the main line stays complete. But it holds this article's writing position, plus a preview of the next article, [Wrapping Results: A Teaching-Edition Task](04-task-type); skipping it leaves the next article missing one link of handover groundwork — whether to stay is your call.

Up to Gate, resumption always happened in someone else's hands. There is another class of waiting; let's bring it out: resumption happens inside await_suspend itself — coroutine A awaits coroutine B; after A suspends, await_suspend starts B running on the spot. Chained awaiting in task types is all this one shape. Short chains, no problem at all. Once the chain deepens, the trouble on the stack can no longer be held down.

Let's manufacture the trouble and measure it. Write a chain: chain(N) awaits chain(N-1), all the way down to chain(0). The handover posture is a switch — one code file carrying both shapes:

<OnlineCompilerDemo
  title="Deep-chain coroutines: the naive and sym handover postures"
  source-path="code/examples/vol5/chapter07/10_exp_d_chain.cpp"
  description="The program takes two command-line arguments (naive or sym, plus chain depth); running directly on the page without arguments prints usage, and the five sets of readings in the article were produced with arguments."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// exp_d_chain.cpp —— 深链协程：两种控制权交接姿势，量栈的读数
// 用法：./exp_d_chain <naive|sym> <链深>
#include <coroutine>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>

static bool g_sym = false;   // sym：await_suspend 返回句柄；naive：当场 resume

struct ChainTask {
  struct promise_type {
    std::coroutine_handle<> continuation{};   // 等我做完的上一级协程，根协程没有

    ChainTask get_return_object() {
      return ChainTask{std::coroutine_handle<promise_type>::from_promise(*this)};
    }
    std::suspend_always initial_suspend() noexcept { return {}; }
    void return_void() noexcept {}
    void unhandled_exception() noexcept { std::terminate(); }

    // 完成时的交接：有人等就把控制权交回去，没人是根，就交还给最初的恢复者
    auto final_suspend() noexcept {
      struct Done {
        bool await_ready() noexcept { return false; }
        std::coroutine_handle<> await_suspend(
            std::coroutine_handle<promise_type> me) noexcept {
          auto c = me.promise().continuation;
          if (!c) return std::noop_coroutine();
          if (g_sym) return c;
          c.resume();                // naive：上一级在栈的更深处接着跑
          return std::noop_coroutine();
        }
        void await_resume() noexcept {}
      };
      return Done{};
    }
  };

  std::coroutine_handle<promise_type> h;
  explicit ChainTask(std::coroutine_handle<promise_type> hh) : h(hh) {}
  ChainTask(const ChainTask&) = delete;
  ChainTask& operator=(const ChainTask&) = delete;
  ~ChainTask() { if (h) h.destroy(); }

  bool await_ready() noexcept { return false; }
  std::coroutine_handle<> await_suspend(std::coroutine_handle<> waiter) noexcept {
    h.promise().continuation = waiter;
    if (g_sym) return h;   // sym：挂起自己，把句柄交出去，让编译器接手
    h.resume();            // naive：当场把子协程跑完，才回到这里
    return std::noop_coroutine();
  }
  void await_resume() noexcept {}
};

ChainTask chain(int depth) {
  if (depth % 100000 == 0) {
    std::printf("    到达深度 %d\n", depth);
    std::fflush(stdout);
  }
  if (depth > 0) {
    co_await chain(depth - 1);
  }
}

int main(int argc, char** argv) {
  if (argc < 3) {
    std::printf("用法: %s <naive|sym> <链深>\n", argv[0]);
    return 1;
  }
  g_sym = (std::strcmp(argv[1], "sym") == 0);
  int depth = std::atoi(argv[2]);
  std::printf("== %s 模式，链深 %d ==\n", g_sym ? "sym" : "naive", depth);
  ChainTask root = chain(depth);
  root.h.resume();
  std::printf("== 根协程完工，控制权回到 main ==\n");
  return 0;
}
```

</OnlineCompilerDemo>

Three places deserve a slow look. First, the awaiter's `await_suspend` with its two branches: the naive branch calls `h.resume()` on the spot and only returns here after the child coroutine finishes running; the sym branch hands the handle out and returns. Second, the promise's `final_suspend`, whose returned Done is also an awaiter: when the coroutine finishes and hangs in the terminal station, Done hands control back to the level above awaiting it — someone waiting, hand over; nobody, and it is the root coroutine, so it hands over `std::noop_coroutine()`. The noop handle's resume is a no-op ([coroutine.handle.noop]), amounting to returning the control flow to the original resumer. Third, ChainTask itself carries the trio — no operator co_await written — so `co_await chain(depth - 1)` takes the "the operand itself is the awaiter" road.

We run five sets of readings: three at -O2, plus two at -O0 as controls:

```bash
g++ -std=c++20 -O2 -pthread exp_d_chain.cpp -o exp_d_chain
./exp_d_chain naive 100000
./exp_d_chain naive 1000000
./exp_d_chain sym 1000000

g++ -std=c++20 -O0 -pthread exp_d_chain.cpp -o exp_d_o0
./exp_d_o0 naive 100000
./exp_d_o0 sym 1000000
```

```text
== naive 模式，链深 100000 ==
    到达深度 100000
    到达深度 0
== 根协程完工，控制权回到 main ==

== naive 模式，链深 1000000 ==
    到达深度 1000000
    到达深度 900000
    到达深度 800000
```

At -O2, naive at one million levels: after passing the 800,000 milestone, nothing remains but a Segmentation fault, exit code 139. For sym's same numbers, we continue:

```text
== sym 模式，链深 1000000 ==
    到达深度 1000000
    到达深度 900000
    到达深度 800000
    到达深度 700000
    到达深度 600000
    到达深度 500000
    到达深度 400000
    到达深度 300000
    到达深度 200000
    到达深度 100000
    到达深度 0
== 根协程完工，控制权回到 main ==
```

Let's go through the readings set by set. At -O2, naive at one hundred thousand levels ran through. An honest disclosure is due: GCC 16.2.1 at -O2 presses the nested resumptions extremely flat — working backwards from the later crash point, each level accounts for only a dozen or so bytes. Small scenes, it can save. At -O2, naive at one million levels did not hold: the 8 MiB stack ran out between 700,000 and 800,000 levels. The same program in the sym posture walked all one million levels — stack depth and chain depth unrelated.

The two -O0 sets we use to shine a brighter light on the compiler's role:

```text
== naive 模式，链深 100000 ==
    到达深度 100000
```

At -O0, naive at one hundred thousand levels: we watched it walk in, and no milestone was ever reported again — exited by segfault. And sym? It did not get away unscathed either:

```text
== sym 模式，链深 1000000 ==
    到达深度 1000000
    到达深度 900000
```

sym at one million levels got stuck behind the 900,000 milestone — likewise a segfault, exit code 139. With optimization off, the tail call was not made, and the handover still stacked frame upon frame. Let's match the readings against a still-open GCC case (bug 100897, still under discussion as of 2026-04): symmetric transfer relies on indirect tail calls; some calling conventions cannot supply the corresponding support, and -O0 and sanitizer builds are especially prone to missing.

Back to the mechanism. An ordinary resume is an ordinary function call: the caller's frame stays on the stack waiting for it to return, and naive's await_suspend uses exactly that: A's resumer calls await_suspend, await_suspend calls B's resume, B's function body, B's await_suspend — one layer pressed on another, none of the calls returned, and so all the frames still hold the stack. Under the sym posture, the shape the compiler sees is "resume a new coroutine, then immediately return from this function" — precisely a tail call's shape: this level's frame exits, control jumps over, and the stack depth does not move. The standard itself does not write "tail call" as a language obligation; the note at (5.1.1) says only that the relay can continue in a string — how the stack avoids growing depends on the compiler recognizing the handover as a tail call. Lewis Baker's symmetric-transfer article lists the tail-call conditions clearly: matching calling conventions, identical return types, no stack objects left undestroyed, and the call not inside a try/catch — the coroutine shape happens to satisfy them all. But conditions satisfied does not mean the promise delivered; the readings on my machine are the proof — -O2 caught it, -O0 did not, and a different compiler or a different ABI wears yet other faces.

So I have settled the writing: when building task types, both the await_suspend handover and the final-suspension-point handover are written in the sym posture. With the shape right, the compiler catches it at common optimization levels, and decoupling stack depth from chain depth stands. The remaining variables — -O0, sanitizers, special ABIs — belong to the implementation-quality side; library writers should keep them in mind, and running a deep-chain scenario on your own toolchain is more solid than memorizing clauses.

## Your Turn

1. Change Gate into a version that registers multiple guests: replace waiter with a list, and on set, resume them one by one in registration order. Who should decide the resumption order — set's caller, or the registration order itself?
2. Keep only exp_d's naive branch, doubling the chain depth upward from one hundred thousand, and find naive's life-and-death line under your compiler and optimization level; fold `ulimit -s`'s value into bytes per level and compare with my reading around 800,000 levels.
3. Change TicketAwaiter's await_suspend to return bool, returning false when the Ticket is already processed, and watch the coroutine take the value without suspending. While at it, change await_ready to check the same thing; with two fast paths coexisting, think about what each guards against.
4. Change Gate's value to `std::coroutine_handle<>`, so that `co_await gate` waits for another coroutine's completion. Once written, verify it with exp_d's chain.

## Next Step

The trio and both handover postures are in hand, yet our Gate still owes things: it cannot fetch results, cannot pass exceptions, and destruction must be remembered by hand. Before turning the page, actually run hands-on item 2. Naive's life-and-death line floats with compiler, optimization level, and stack configuration — around 800,000 levels on my machine, perhaps 50,000 or 2,000,000 on yours. Measure it out, and the value of symmetric transfer needs no further describing from me.

## References

This article's material comes from the old directory ch06's promise_type and awaitable, where the awaiter protocol, operator co_await, and await_transform already had one telling; we rewrote all the examples and re-verified the behavior claims against the current draft. The approach drew on two pieces of Lewis Baker's coroutine series, operator co_await and symmetric transfer; the examples and words are my own. Language-mechanism claims were checked against the current working draft on eel.is (accessed 2026-11-17); clause and paragraph numbers may shift as the draft updates.

- [Await expression (current draft expr.await)][draft-await] — the expansion steps of the three questions, the three return types of await_suspend, the coroutine resuming and immediately rethrowing on an exception, the expression type matching await_resume, and the note on relayed resumption.
- [Operators in expressions (current draft over.match.oper)][draft-oper] — the member candidates for operator co_await, and how ordinary lookup and ADL's non-member candidates compose one overload resolution.
- [Trivial awaitables (current draft coroutine.trivial.awaitables)][draft-trivial] — the trio definitions of suspend_always and suspend_never.
- [Resumption (current draft coroutine.handle.resumption)][draft-resumption] — the preconditions of resume and destroy, and the implementation-defined behavior of resuming across execution agents.
- [No-op coroutines (current draft coroutine.handle.noop)][draft-noop] — the noop handle's resume being a no-op, and done being constantly false.
- [C++ Coroutines: Understanding operator co_await — Lewis Baker][baker-coawait] — the origin of the awaitable and awaiter terms, the race-window use of the bool return type, and the reference telling of the manual-reset event and borrowing memory from the coroutine frame.
- [C++ Coroutines: Understanding Symmetric Transfer — Lewis Baker][baker-sym] — the shape of the stack-overflow problem, the conditions for a tail call, and the origins of P0913R0 and noop_coroutine.
- [GCC bug 100897][gcc-100897] — the recorded case of symmetric transfer not becoming a tail call under some options and ABIs; status and discussion dates per the page.

[draft-await]: https://eel.is/c++draft/expr.await
[draft-oper]: https://eel.is/c++draft/over.match.oper
[draft-trivial]: https://eel.is/c++draft/coroutine.trivial.awaitables
[draft-resumption]: https://eel.is/c++draft/coroutine.handle.resumption
[draft-noop]: https://eel.is/c++draft/coroutine.handle.noop
[baker-coawait]: https://lewissbaker.github.io/2017/11/17/understanding-operator-co-await
[baker-sym]: https://lewissbaker.github.io/2020/05/11/understanding_symmetric_transfer
[gcc-100897]: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=100897
