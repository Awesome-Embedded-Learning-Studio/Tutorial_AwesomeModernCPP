---
title: "How a Minimal Coroutine Comes Together"
description: "How three keywords turn a function into a pausable object, what lives in the coroutine frame, how it is allocated and when destroyed, what promise_type and the coroutine handle each govern, and finally assembling a minimal working generator."
chapter: 5
order: 2
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
  source: documents/vol5-concurrency/chapter07-coroutines/02-minimal-coroutine.md
  source_hash: 3a3b6d5a29756153190b127e422208dedb5af609f4e4b769928d7d32ca96c4cd
  translated_at: '2026-11-22T13:58:30+00:00'
  engine: glm-5.3
  token_count: 24269
---

# How a Minimal Coroutine Comes Together

For the three experiments in the main text, the code blocks are the entire content; assemble them into a single .cpp file to rerun.

## Three Keywords Turn a Function into a Coroutine

`co_await`, `co_yield`, `co_return` — C++20 added three keywords beginning with co_. The compiler's basis for recognizing a coroutine is exactly one rule: write any one of them in a function body, and the function is treated as a coroutine ([dcl.fct.def.coroutine]/1). No modifier needed, no registration needed; the keyword itself is the identity declaration. Conversely, if the function body has none of them, then no matter how much the return type's name resembles Task, it is just an ordinary function.

In the division of labor, `co_await` handles "hold on a moment": reaching it, the coroutine evaluates whether to suspend; suspending hands the control out, and after resumption it receives a value back from it. How to customize the awaited object belongs to the next article, How `co_await` Joins the Wait — the suspension body is the simplest kind of awaiter, and the protocol is explained there too. This article uses only the two ready-made suspension bodies the standard library ships, `std::suspend_always` and `std::suspend_never`: the former's `await_ready()` always returns false, meaning it suspends every time; the latter returns true, meaning it never suspends ([coroutine.trivial.awaitables]). `co_yield` handles "produce a value, then suspend" — generators live entirely on it. `co_return` handles the wrap-up; the coroutine's ending is completed by it.

`co_yield` has a definite expansion in the standard: `co_yield e` is equivalent to `co_await promise.yield_value(e)` ([expr.yield]/1). Where the promise object comes from is explained in the promise_type section. All you need to see for now is the shape: the value-producing act is translated into one suspension wait; the promise takes the value onto itself via `yield_value` and then returns the suspension body.

`co_return` has two forms. `co_return;` calls `promise.return_void()`. `co_return expr;` looks at expr's type: non-void calls `promise.return_value(expr)`; a void type still goes through `return_void()`, sourced from cppreference's coroutines page. A function body that runs to the end without writing `co_return` is equivalent to having a `co_return;` appended at the end. One loose spot must be disclosed to you clearly: if the promise lacks `return_void`, the compiler reports no error, and per the current draft, a body that runs to the end is undefined behavior ([stmt.return.coroutine]/3). In local testing, neither the default options nor -Wall -Wextra produced a single warning; the run-to-the-end coroutine suspended into the final suspension point as usual on our machine, and the program did not crash. The standard classifies it as undefined behavior — the consequences follow the implementation, and a different compiler may wear a different face; do not take one non-crash as all-clear. Only an explicitly written `co_return;` gets intercepted by the compiler with no member named `return_void`. Under the C++20 rules, `return_void` and `return_value` cannot both be declared in a promise_type — declaring both makes the program ill-formed, and the standard does not relax the mutual exclusion until C++29 (P3950R1); this article follows C++20.

A few more restrictions we cannot dodge when writing code. An ordinary `return` statement cannot appear in a coroutine. constexpr functions, consteval functions, constructors, destructors, variadic functions, and `main` cannot be coroutines. The return type cannot be auto either — cppreference collects these in its Restrictions section. The variadic restriction the standard writes concretely: a coroutine whose parameter list ends with an unnamed ellipsis makes the program ill-formed ([dcl.fct.def.coroutine]/1). Variadic templates with parameter packs are unaffected; the standard itself gives the `g2(Args&&...)` example.

## The Compiler Swaps the Function Body for a State Machine

The compiler does not accept your function body as written. The standard spells the coroutine's behavior out as a replacement body. The function body you declare gets loaded into a skeleton the compiler builds ([dcl.fct.def.coroutine]/5). The skeleton looks like this, pseudocode laid out following the standard's structure:

```text
{
  promise 构造（promise 若有接收协程参数的构造函数，实参用参数拷贝）
  try {
    co_await promise.initial_suspend();   // 初始挂起点
    您写的函数体
  } catch (...) {
    if (!initial-await-resume-called) throw;   // 开工前抛的，原样交回调用者
    promise.unhandled_exception();
  }
final-suspend:
  co_await promise.final_suspend();       // 最终挂起点
}
```

The skeleton contains two suspension points you did not write. `initial_suspend` stands ahead of the function body and decides whether the coroutine is lazy-start or eager-start: returning `suspend_always`, the body has not run a single line when the caller receives the return object — it takes a resume to start work. Returning `suspend_never`, the coroutine runs all the way to the first real suspension point before stopping; the Task in the previous article, [Why Suspend and Resume Are Needed](01-why-suspend-resume), chose eager-start, with a thousand coroutines running straight to their Park suspension after creation. `final_suspend` stands after the function body: once `co_return` has executed and the local variables have been destroyed in reverse construction order, the coroutine arrives before it.

The `await_ready`, `await_suspend`, `await_resume` trio on Park — the protocol's details belong to the next article, How `co_await` Joins the Wait; take them apart there.

The timing of `get_return_object` is fixed too: its call is ordered ahead of `initial_suspend`, at most once ([dcl.fct.def.coroutine]/7). So by the time the return object reaches your hand, the coroutine body has necessarily not started — even if the promise is configured for eager-start. The return object usually houses a handle; how the handle is made is detailed in the promise_type section.

Let's run the replacement body's chain for real with a printing coroutine. Trace's coroutine body is just two print lines; the promise's `initial_suspend` and `final_suspend` are both `suspend_always`, prints are inserted at both suspension points' entries, and the destructor prints a word when it calls destroy:

<OnlineCompilerDemo
  title="Lazy start and the terminal station: every step of a coroutine from birth to death"
  source-path="code/examples/vol5/chapter07/03_lazy_start.cpp"
  description="When demo() is called, not one line of the body has run; the first resume starts the work, after co_return it hangs at the final suspension point awaiting destroy, and the frame's collection prints too."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// lazy_start.cpp —— 懒启动与终点站：协程体什么时候真的跑，帧什么时候收回
#include <coroutine>
#include <cstdio>
#include <exception>

struct Trace {
  struct promise_type {
    Trace get_return_object() {
      return Trace{std::coroutine_handle<promise_type>::from_promise(*this)};
    }
    std::suspend_always initial_suspend() noexcept {
      std::printf("  [协程] initial_suspend 挂起，控制权交回调用者\n");
      return {};
    }
    std::suspend_always final_suspend() noexcept {
      std::printf("  [协程] final_suspend 挂起，等 destroy\n");
      return {};
    }
    void return_void() noexcept {}
    void unhandled_exception() { std::terminate(); }
  };

  explicit Trace(std::coroutine_handle<promise_type> h) : handle_(h) {}
  ~Trace() {
    if (handle_) {
      std::printf("  [main ] Trace 析构，调 destroy 收回协程帧\n");
      handle_.destroy();
    }
  }
  Trace(const Trace&) = delete;
  Trace& operator=(const Trace&) = delete;

  void resume() { handle_.resume(); }
  bool done() const { return handle_.done(); }

private:
  std::coroutine_handle<promise_type> handle_;
};

Trace demo() {
  std::printf("  [协程] 函数体第一行\n");
  std::printf("  [协程] 函数体第二行\n");
  co_return;
}

int main() {
  std::printf("[main ] 调用 demo()\n");
  {
    Trace t = demo();
    std::printf("[main ] 拿到返回对象，函数体一行没跑，done=%d\n", t.done() ? 1 : 0);
    std::printf("[main ] 第一次 resume\n");
    t.resume();
    std::printf("[main ] 回到 main，done=%d\n", t.done() ? 1 : 0);
  }
  std::printf("[main ] Trace 析构完，程序结束\n");
  return 0;
}
```

</OnlineCompilerDemo>

The actual run's output:

```text
[main ] 调用 demo()
  [协程] initial_suspend 挂起，控制权交回调用者
[main ] 拿到返回对象，函数体一行没跑，done=0
[main ] 第一次 resume
  [协程] 函数体第一行
  [协程] 函数体第二行
  [协程] final_suspend 挂起，等 destroy
[main ] 回到 main，done=1
  [main ] Trace 析构，调 destroy 收回协程帧
[main ] Trace 析构完，程序结束
```

The output lines up with the replacement body one for one; let's read them against each other. At the moment `demo()` is called, the frame is allocated, the promise constructed, `get_return_object` hands out the Trace carrying the handle, `initial_suspend` holds the coroutine at the function body's door, and control returns to main. At this point `done()` is 0, because it returns true only at the final suspension point. After the first resume, the body's two print lines come out in order, `co_return` walks through `return_void`, the local variables are destroyed, `final_suspend` hangs the coroutine in the terminal station, done flips to 1, and Trace's destructor calls destroy to collect the frame. From birth to death, every step of the coroutine is visible.

## The Coroutine Frame: The State Lives in a Block of Heap Memory

That a coroutine can resume after suspending rests on its state staying fully alive — the premise sits here, and we must own it. The standard calls it the **coroutine state**; the industry more often says **coroutine frame** — two names for the same thing: a block of storage holding the coroutine's entire estate ([dcl.fct.def.coroutine]/9). cppreference lists four kinds of content: the promise object, copies of all parameters, data representing the current suspension point (relied on at resumption to know where to continue, and at destruction to know which local variables are alive at the suspension point), and the local variables and temporaries whose lifetimes cross suspension points.

We met a relative in Unit 4's [Running Computations with `std::async`](../chapter04-organizing-tasks/01-launch-with-async): behind a future sits a shared state, where values and exceptions both live — the asynchronous provider puts them in, the future takes them out. The coroutine frame is another dwelling built on the same idea: the state does not live on the call stack but in an object, and the object outlives the function call. The difference is equally clear: the shared state relays results, while the coroutine frame stores the coroutine's own estate — parameters, local variables, suspension points, all inside.

Allocation goes through `operator new`. The standard fixes the lookup order too; let's follow along: if `operator new` exists in the promise_type's scope, it is used, and its arguments may carry the coroutine parameters in addition to the size ([dcl.fct.def.coroutine]/9.1). If the scope has none, the search goes to the global scope for an ordinary non-array `operator new`. Deallocation uses `operator delete`, likewise starting from the promise's scope; when both the single-argument version and the sized version are found, the sized version wins (/12). For a promise that wants a custom allocation strategy, defining a member `operator new` is enough — exactly what the frame_alloc experiment below does.

The default way out on allocation failure is throwing `std::bad_alloc`. If you declare the static member `get_return_object_on_allocation_failure` in the promise_type, the language changes face: allocation switches to the non-throwing nothrow form, and on failure no exception is thrown — instead it is called to obtain a return object handed straight back to the caller ([dcl.fct.def.coroutine]/10). Where the standard's coroutine-definitions clause discusses allocation failure, it gives a complete generator example using precisely this.

cppreference discloses one more layer: the heap allocation can be elided outright by the implementation. Two conditions: the coroutine state's lifetime is strictly nested within the caller's lifetime, and the size is computable at the call site. When both hold, the frame can move into the caller's stack frame, and `operator new` is not called even once. The optimization has a name — **heap allocation elision** (often abbreviated HALO in the industry). I counted with frame_alloc; the readings are below.

<OnlineCompilerDemo
  title="Counting the coroutine frame's allocations and frees"
  source-path="code/examples/vol5/chapter07/04_frame_alloc.cpp"
  description="The operator new and delete in the promise's scope each count once per call: 3 in the nested scenario, plus 1 more in the escaping scenario, with identical readings at -O0 and -O2."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// frame_alloc.cpp —— 数一数协程状态的分配与释放，-O2 与 -O0 各跑一遍
#include <coroutine>
#include <cstdio>
#include <exception>
#include <new>

int g_allocs = 0;
int g_deallocs = 0;

template <typename T>
class Generator {
public:
  struct promise_type {
    T current_value;

    Generator get_return_object() {
      return Generator{std::coroutine_handle<promise_type>::from_promise(*this)};
    }
    std::suspend_always initial_suspend() noexcept { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    std::suspend_always yield_value(T value) noexcept {
      current_value = value;
      return {};
    }
    void return_void() noexcept {}
    void unhandled_exception() { std::terminate(); }

    // promise 作用域里的分配函数，标准从这里找起
    void* operator new(std::size_t size) {
      ++g_allocs;
      return ::operator new(size);
    }
    void operator delete(void* ptr, std::size_t size) {
      ++g_deallocs;
      ::operator delete(ptr, size);
    }
  };

  explicit Generator(std::coroutine_handle<promise_type> h) : handle_(h) {}
  ~Generator() { if (handle_) handle_.destroy(); }
  Generator(const Generator&) = delete;
  Generator& operator=(const Generator&) = delete;

  bool next() {
    handle_.resume();
    return !handle_.done();
  }
  T value() const { return handle_.promise().current_value; }

private:
  std::coroutine_handle<promise_type> handle_;
};

Generator<int> seq(int limit) {
  for (int i = 1; i <= limit; ++i) co_yield i;
}

// 嵌套场景：三个生成器都活不到 run_nested 返回之后
void run_nested() {
  Generator<int> a = seq(2);
  Generator<int> b = seq(2);
  Generator<int> c = seq(2);
  while (a.next()) (void)a.value();
  while (b.next()) (void)b.value();
  while (c.next()) (void)c.value();
}

// 逃逸场景：生成器活着离开 make_escape，协程状态比调用者命长
Generator<int> make_escape() { return seq(2); }

int main() {
  run_nested();
  std::printf("嵌套场景之后：operator new %d 次，operator delete %d 次\n", g_allocs, g_deallocs);

  {
    Generator<int> g = make_escape();
    int taken = 0;
    while (g.next()) { ++taken; (void)g.value(); }
    std::printf("逃逸场景取到 %d 个值，随后：", taken);
  }
  std::printf("operator new %d 次，operator delete %d 次\n", g_allocs, g_deallocs);
  return 0;
}
```

</OnlineCompilerDemo>

We ran it once each at -O2 and -O0; the readings were identical:

```text
嵌套场景之后：operator new 3 次，operator delete 3 次
逃逸场景取到 2 个值，随后：operator new 4 次，operator delete 4 次
```

The escaping scenario is easy to understand: the generator leaves `make_escape` alive, the coroutine state outlives the caller, the nesting condition does not hold, and the heap allocation cannot be dodged. The nested scenario's three allocations could have been elided per the conditions cppreference gives, but the GCC 16.2.1 on my desk did not elide them — the readings are the same with optimizations on or off. The readings represent only the compiler at hand; a different implementation or a different compiler version may read differently. Run it once on your own machine and you will know which side you stand on.

Parameters entering the frame have their own rules; let's walk through them with the standard. Every coroutine parameter gets a copy made at the start of the replacement body ([dcl.fct.def.coroutine]/13): a by-value parameter copies the value, moving it into the frame, and when the coroutine resumes it uses the copy in the frame. A reference parameter copies the reference itself — the frame records only a name, and the object pointed to belongs to the caller's world. The copies' initialization is ordered before the promise's construction, and their destruction after the promise's destruction (/13.5, /13.6).

For the dangling risk, think it through with `S{0}.f()`: `S{0}` is a temporary gone the moment the statement ends; the coroutine launched by the member function records only a reference in its frame, and by the time you resume, it points at memory already returned to the world. cppreference's coroutines page gives a set of dangling examples of the same shape (bad1 is written exactly this way), and the standard's note under /13 warns about it specifically. A reference parameter letting the coroutine outlive the object is the most common source of dangling in coroutines. When a parameter must be used across suspension points, passing by value is the cheapest insurance we can buy ourselves.

## When the Frame Is Destroyed: The Dangers All Live Here

There are two entrances to destruction. When control flow passes through the coroutine's end — that is, `final_suspend` does not suspend either and execution walks out of the function — the coroutine state is destroyed automatically. The other entrance is `destroy` on the handle ([dcl.fct.def.coroutine]/11). Take the most common lazy-start spelling, with both `initial_suspend` and `final_suspend` returning `suspend_always`, and check the timing: after `co_return` the coroutine hangs at the final suspension point; control flow has not left the coroutine; the frame is still fully alive — promise, parameter copies, local variables all present — waiting precisely for destroy. In lazy_start's output above, the frame was still collectable after done flipped to 1: that is the frame hanging in the terminal station.

If `final_suspend` returns `suspend_never`, the replacement body's `co_await promise.final_suspend()` does not suspend; control flow passes straight through the end, and the coroutine state destroys itself on the spot. It looks like one destroy step saved, but the price is a dangling handle: if the outside still clutches a `coroutine_handle`, the memory it points to has already been returned to the allocator, and both resume and destroy afterwards are undefined behavior. That is why teaching and library types almost always let `final_suspend` suspend, leaving the decision to destroy to whoever holds the handle. As you will see shortly, Generator made the same choice.

A coroutine hanging at the final suspension point has nothing left to do but be destroyed. resume's precondition is written plainly: the handle refers to a suspended coroutine not at its final suspension point ([coroutine.handle.resumption]/2) — resuming a handle whose `done()` is true is undefined behavior. destroy's precondition likewise says it must refer to a suspended coroutine (/4). `done()` serves as the gauge here; the resumption loops we write take the shape of advancing until done flips true, as the Generator section will show.

The error in the other direction is forgetting to destroy. The coroutine frame comes from the heap; if we never call destroy and control flow cannot walk out the end (final_suspend is holding), the frame keeps living on the heap. `coroutine_handle` is non-owning — its destructor does nothing — and the handle does not carry the blame for the leak. So the handle almost always gets wrapped in a layer of RAII whose destructor calls destroy; Generator's destructor is written exactly so.

The cleanup that follows destroy — cppreference's Completion section gives the ordering: the promise object destructs, the parameter copies destruct, and `operator delete` returns the frame's memory. Check the order against the parameters section's rule: the parameter copies outlive the promise, and at destruction they walk behind it too.

## promise_type and the Coroutine Handle

promise_type does not come from nowhere; it is decided by the coroutine's return type: `std::coroutine_traits<R, Args...>::promise_type`, with R the return type and Args the parameter list ([dcl.fct.def.coroutine]/3). Written as an ordinary function, what gets taken is the return type's nested promise_type. In Unit 5's [How Far Do Atomic Operations Get You?](../chapter05-atomic-memory-model/01-what-atomics-solve) we met a relation of the same shape: `std::atomic` is a set of types plus a set of operations, and the type decides which operations you get. On the coroutine side, the return type decides the promise_type, the promise_type decides the coroutine's behavior at each node, and the return type is the coroutine's identity declaration. `Generator<int>` and `Task<int>` can wear the same keywords yet behave completely differently — the fork lies entirely in promise_type.

The behavior hooks the compiler calls number six in total, with return_void and return_value as an either-or; let's walk them in call order (the allocation and deallocation functions also live in the promise's scope — their lookup order was covered in the coroutine-frame section):

1. `get_return_object()`: constructs the return object, which is exactly what the caller receives; called at most once ([dcl.fct.def.coroutine]/7).
2. `initial_suspend()`: whether to suspend before starting work — the fork between lazy-start and eager-start.
3. `yield_value(e)`: the landing point of `co_yield` — takes the value and decides whether to suspend ([expr.yield]/1).
4. `return_void()` or `return_value(v)`: the landing point of `co_return` — an either-or; declaring both makes the program ill-formed in C++20.
5. `unhandled_exception()`: called when the function body throws an exception nobody catches — the exception's way out.
6. `final_suspend()`: the terminal station; it must not be potentially-throwing — plainly, it must promise not to throw, and its signature carries `noexcept` ([dcl.fct.def.coroutine]/15).

There are two schools for where `unhandled_exception` sends the exception. The terminate school calls `std::terminate()` outright — blunt and simple, suited to places where exceptions should never appear at all. The storage school stores `std::current_exception()` into an `exception_ptr` member of the promise, waiting for the outside to fetch it; the Generator in gen_min.cpp below chooses the storage school, and you will meet it during the line-by-line walkthrough. You can also `throw;` inside `unhandled_exception` to rethrow the exception — the standard addresses this explicitly: when unhandled_exception rethrows, the coroutine is considered suspended at the final suspension point, and the exception propagates to the caller or resumer ([dcl.fct.def.coroutine]/14).

The name promise easily suggests Unit 4's `std::promise`. cppreference's coroutine page carries an explicit disclaimer: promise objects have no relationship with `std::promise` whatsoever. `std::promise` is the asynchronous provider from `<future>` that places results into a shared state. A coroutine's promise_type is a behavior customization point the compiler calls. Beyond the name, the two share no kinship. In this article we write promise_type; where we write promise alone, it always means the promise object inside the coroutine frame.

The coroutine handle `std::coroutine_handle<Promise>` is the entrance for operating a coroutine from the outside; cppreference positions it as a non-owning handle: it refers to the coroutine state without owning it. The commonly used members are few. `resume()` (with an equivalent `operator()`) lets a suspended coroutine continue. `done()` reports whether the coroutine hangs at the final suspension point. `destroy()` destroys the coroutine state. `promise()` obtains a reference to the promise object inside the frame. The static `from_promise(promise)` builds a handle from a promise reference in reverse — the `from_promise(*this)` line you write in `get_return_object` is exactly this use.

The standard names the resumption action: the evaluation that calls a resumption member function is the resumer ([dcl.fct.def.coroutine]/8). A coroutine carries no thread of its own; whoever calls resume, the coroutine body continues on that one's call stack. In Unit 3's [Choosing Tools by the Collaboration You Need](../chapter03-wait-and-coordinate/07-choosing-tools), when covering the barrier's completion function, we met the same affair: the completion function runs on one of the arriving threads, taking no extra thread. Coroutine resumption is kin to that. One boundary of cross-thread resumption deserves mention. Resuming or destroying on an execution agent that did not suspend it is implementation-defined behavior, unless all participants are `std::thread` or `std::jthread` instances, or the thread executing main ([coroutine.handle.resumption]/1). Whoever resumes is responsible — we return to this in the fourth article, Wrapping Results: A Teaching-Edition Task, when discussing task types.

## The First Complete Example: Generator

The machinery is all here; let's assemble it into a real component. The goal is the following generator: the coroutine produces an integer sequence with `co_yield`, the caller takes one value per `next()`, values are read off the promise, and the destructor collects the frame when done. The pausable work the previous article spoke of — grown into code, this is what it becomes.

<OnlineCompilerDemo
  title="A minimal generator: co_yield producing squares"
  source-path="code/examples/vol5/chapter07/05_gen_min.cpp"
  description="At squares(5)'s creation not one line of the body has run; next() produces one square at a time; after the fifth is taken done flips true, and the destructor collects the frame."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// gen_min.cpp —— 能跑通的最小生成器，co_yield 产出平方数
#include <coroutine>
#include <cstdio>
#include <exception>

template <typename T>
class Generator {
public:
  struct promise_type {
    T current_value;
    std::exception_ptr failure;

    Generator get_return_object() {
      return Generator{std::coroutine_handle<promise_type>::from_promise(*this)};
    }
    std::suspend_always initial_suspend() noexcept { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    std::suspend_always yield_value(T value) noexcept {
      current_value = value;
      return {};
    }
    void return_void() noexcept {}
    void unhandled_exception() { failure = std::current_exception(); }
  };

  explicit Generator(std::coroutine_handle<promise_type> handle) : handle_(handle) {}
  ~Generator() {
    if (handle_) handle_.destroy();
  }
  Generator(const Generator&) = delete;
  Generator& operator=(const Generator&) = delete;

  bool next() {
    handle_.resume();
    if (handle_.promise().failure) {
      std::rethrow_exception(handle_.promise().failure);
    }
    return !handle_.done();
  }

  T value() const { return handle_.promise().current_value; }

private:
  std::coroutine_handle<promise_type> handle_;
};

Generator<int> squares(int limit) {
  for (int i = 1; i <= limit; ++i) {
    co_yield i * i;
  }
}

int main() {
  std::printf("创建 squares(5)，此刻函数体一行没跑\n");
  Generator<int> gen = squares(5);
  while (gen.next()) {
    std::printf("拿到 %d\n", gen.value());
  }
  std::printf("生成器耗尽，destroy 交给析构函数\n");
  return 0;
}
```

</OnlineCompilerDemo>

The actual run's output:

```text
创建 squares(5)，此刻函数体一行没跑
拿到 1
拿到 4
拿到 9
拿到 16
拿到 25
生成器耗尽，destroy 交给析构函数
```

Let's walk it line by line. The promise_type's first two members are `current_value` and `failure`; they live on the promise, sharing life and death with the frame: `current_value` stores the value handed over by `co_yield`, and `failure` stores the exception pointer.

The `from_promise(*this)` you write in `get_return_object()` builds a handle from the promise reference, wraps it into the Generator, and hands it back to the caller. The coroutine frame already exists the moment the coroutine is called; Generator merely receives the handle.

`initial_suspend()` returns `suspend_always` — we chose lazy-start: creation does not run the body; the caller's `next()` starts the work. Generators almost always choose lazy-start, because values are meant to be produced on demand; running everything at creation would defeat the point.

`yield_value(T value)` is the landing point of `co_yield i * i`. The value goes into `current_value`, `suspend_always` is returned to suspend, and control goes back to `next()`. Reading the value goes through `handle_.promise().current_value` in `value()` — from the handle to the promise, from the promise to the value.

`return_void()` corresponds to the implicit `co_return;` at the end of the `squares` body, so the promise needs only an empty `return_void`. For `unhandled_exception()` we chose the storage school here: an exception thrown in the coroutine body is captured into `failure` by `std::current_exception()`, `next()` checks it after the resume, and if there is an exception, `rethrow_exception` throws it to the caller as it was. A failure inside the coroutine thus reaches the caller's hands — same type, same message — the same idea as the async-task exception traveling through the shared state in Unit 4, only the storage point changes from shared state to promise.

`final_suspend()` returns `suspend_always` with noexcept: hanging in the terminal station, the frame waits for the destructor to collect it. The Generator class itself is the handle's RAII wrapper — the destructor calls destroy, and copying is deleted wholesale. The reason for deleting copies is written in the handle's positioning: it is non-owning, and two copies would point at the same frame, destroying the same memory twice at destruction. Move semantics are useful for generators (carrying one out of a factory function); the minimal version omits them, and you can add them following the usual RAII practice when you try it yourself.

On the output side, we watch the five squares come out one after another; then `done()` inside `next()` flips to true, the loop closes, and gen's destructor collects the frame. How many values `squares(5)` produces is decided by the coroutine's own loop; the caller takes as many as wanted, and when done wanting, the destructor wraps up at any moment. The suspension and resumption of the previous article, landed on code, is the cooperation of three links: `co_yield` suspends, the handle resumes, the destructor destroys.

## range-for Iterators and Infinite Sequences (Extension)

This section carries the (Extension) marker and covers two things: fitting the Generator above with a range-for iterator so it can be written directly into a `for (int val : gen)` loop that leaves the traversal to the compiler to expand; and producing an infinite Fibonacci sequence. Skipping this section does not affect the article's main line — the next article's awaiter does not depend on the code here. The code block is the entire content; assemble it into a single .cpp file to rerun, with the same compile command as the three experiments.

gen_min's way of consuming has the caller asking step by step: `next()` advances once, `value()` reads one, and the loop is written by hand. This shape hands all the pacing to the caller, but it is verbose to write. C++ has a convenient consumption port ready — range-for: a one-line loop where begin, comparison, advance, and dereference are arranged for you by the compiler. For a generator to walk through this port, exactly three things are owed: an iterator type, a `begin()`, and an `end()`. Let's fit them now.

The iterator goes into Generator's public section, with `begin()` and `end()` following `value()`. The complete file is laid out; assemble it into a single .cpp and you can rerun:

```cpp
// gen_range.cpp —— 生成器补上迭代器：range-for 遍历平方数，再取无限的斐波那契
#include <coroutine>
#include <cstdio>
#include <exception>
#include <iterator>

template <typename T>
class Generator {
public:
  struct promise_type {
    T current_value;
    std::exception_ptr failure;

    Generator get_return_object() {
      return Generator{std::coroutine_handle<promise_type>::from_promise(*this)};
    }
    std::suspend_always initial_suspend() noexcept { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    std::suspend_always yield_value(T value) noexcept {
      current_value = value;
      return {};
    }
    void return_void() noexcept {}
    void unhandled_exception() { failure = std::current_exception(); }
  };

  explicit Generator(std::coroutine_handle<promise_type> handle) : handle_(handle) {}
  ~Generator() {
    if (handle_) handle_.destroy();
  }
  Generator(const Generator&) = delete;
  Generator& operator=(const Generator&) = delete;

  bool next() {
    handle_.resume();
    if (handle_.promise().failure) {
      std::rethrow_exception(handle_.promise().failure);
    }
    return !handle_.done();
  }

  T value() const { return handle_.promise().current_value; }

  class Iterator {
  public:
    using iterator_category = std::input_iterator_tag;
    using value_type = T;
    using difference_type = std::ptrdiff_t;
    using pointer = T*;
    using reference = T&;

    Iterator() : generator_(nullptr) {}
    explicit Iterator(Generator* gen) : generator_(gen) { advance(); }

    T operator*() const { return generator_->value(); }

    Iterator& operator++() {
      advance();
      return *this;
    }
    void operator++(int) { advance(); }

    bool operator==(const Iterator& other) const { return generator_ == other.generator_; }
    bool operator!=(const Iterator& other) const { return !(*this == other); }

  private:
    Generator* generator_;

    void advance() {
      if (!generator_->next()) {
        generator_ = nullptr;   // 下一个值取不到了，迭代器变成 end
      }
    }
  };

  Iterator begin() { return Iterator(this); }
  Iterator end() { return Iterator(); }

private:
  std::coroutine_handle<promise_type> handle_;
};

Generator<int> squares(int limit) {
  for (int i = 1; i <= limit; ++i) {
    co_yield i * i;
  }
}

// 无限序列：while (true) 里不停产出，函数体永远走不到底
Generator<int> fibonacci() {
  int a = 0, b = 1;
  while (true) {
    co_yield a;
    int next = a + b;
    a = b;
    b = next;
  }
}

int main() {
  std::printf("前 8 个完全平方数，range-for 直接遍历:\n");
  for (int val : squares(8)) {
    std::printf("  %d\n", val);
  }

  std::printf("斐波那契前 15 项，取多少算多少:\n");
  Generator<int> fib = fibonacci();
  int taken = 0;
  for (int val : fib) {
    std::printf("  fib(%d) = %d\n", taken, val);
    if (++taken == 15) break;   // 无限序列，取够就 break，不然取不完
  }
  return 0;
}
```

On the local GCC 16.2.1, `-Wall -Wextra` compiles clean; the actual run's output:

```text
前 8 个完全平方数，range-for 直接遍历:
  1
  4
  9
  16
  25
  36
  49
  64
斐波那契前 15 项，取多少算多少:
  fib(0) = 0
  fib(1) = 1
  fib(2) = 1
  fib(3) = 2
  fib(4) = 3
  fib(5) = 5
  fib(6) = 8
  fib(7) = 13
  fib(8) = 21
  fib(9) = 34
  fib(10) = 55
  fib(11) = 89
  fib(12) = 144
  fib(13) = 233
  fib(14) = 377
```

Let's walk through against the code. The iterator identifies itself first: five using aliases, with `iterator_category` filled as `std::input_iterator_tag`, declaring this a single-pass input iterator, and `value_type` reporting the type it reads out. These five aliases live in the `<iterator>` header. The first time I assembled this file I left it out, and g++ promptly reported `input_iterator_tag` as an unrecognized type; only adding the include got it through. range-for itself does not check these aliases — the ones checking are the algorithms, which recognize iterators through iterator_traits; these five aliases are the identity registration.

begin()'s action hides inside Iterator's constructor, which immediately calls advance() once at construction. Generator is lazy-start — at creation not one line of the body has run — and begin()'s push advances it to the first `co_yield`, the first value already landed in the promise, which is exactly what `operator*` reads. Every step afterward is `operator++`, and advance() is just one `next()`. When the next value cannot be fetched, the generator_ pointer is set to null, the iterator equals `end()` from then on, range-for's `!=` no longer holds, and the loop closes. The equality comparison compares the Generator pointers held by the two iterators — one nullptr and one not, one comparison settles it. The iterator points at the generator itself, the generator lives through the whole loop, and so the pointer cannot dangle.

One limitation to state in the open: this iterator is single-use — after one traversal there is no turning back. The reason sits in the handle's capability list: `coroutine_handle`'s members are just this handful — resume, destroy, done — and resumption always walks forward from the current suspension point; there is no entrance back to the previous one. Iterate once and want to go back, and the handle offers you no road home. The standard grades iterators by capability: input is the most basic tier, then forward and bidirectional above it, and only then random access. `std::sort`'s page states it wants exactly the random-access tier (RandomIt must satisfy LegacyRandomAccessIterator); two tiers stand in between, and feeding this in leaves no behavior to count on. To really sort, first gather the values into a `std::vector`, then sort the vector. To traverse again, build a new generator.

With the iterator in place, the infinite sequence comes along conveniently. squares is a finite sequence: the body ends naturally at the loop's end, with the implicit `co_return` waiting at the tail. Swap the loop condition for `while (true)` and the coroutine becomes the provider of an infinite sequence — the body never reaches its end, `co_return` never comes; the `fibonacci()` in the file's second half is written exactly so.

Inside fibonacci()'s frame you will find no array — just two ints, a and b. The sequence has infinitely many terms, yet the frame needs only these two slots, because values are computed at request time: `co_yield` hands over one term, the coroutine suspends, and the next term is not produced until the caller asks again. This is lazy evaluation — values produced on demand, neither computed nor stored ahead. Infinitely many terms cannot be pre-loaded into any container; on-demand production is the only road here. Finite sequences benefit too: the caller wanting only the first 15 terms means the body advances only 15 steps, and not one of the 16th term's computations happens. In main, taking 15 terms breaks the loop, fib destructs, destroy collects the frame, and this infinite sequence leaves without occupying any memory.

## Your Turn

1. Replace `squares` with your own sequence — Fibonacci, lines read from a file, anything; once it runs, switch limit to a large number and feel what on-demand production of values means.
2. Switch `initial_suspend`'s return value to `suspend_never` and run again; watch the body run straight to the first `co_yield` at creation, and compare against lazy_start's output.
3. Use frame_alloc's counters to count your own generator, once at -O0 and once at -O2, and see whether your compiler elides the heap allocation.
4. Delete the destroy in Generator's destructor and run under valgrind or AddressSanitizer; see with your own eyes where the leak comes out.

Before turning the page, actually run item 2 of the hands-on list: an eager-start generator has already produced its first value at creation; observe that clearly, and the next article's awaiter has a baseline to compare against.

## References

Beyond the three experiments, frame_alloc was additionally run at -O0 as a control. The verification of the missing return_void, the explicit interception, and the double declaration came from another probe used during revision — verified separately, not entering the main text and not counted among the three experiments; the readings follow implementation and options. Behavior claims were checked against the current working draft on eel.is; clause and paragraph numbers may shift as the draft updates. cppreference's coroutines page was fetched in full and checked against the draft with no discrepancy. The old directory ch06's C++20 Coroutine Fundamentals and promise_type and awaitable supplied material for this article; the examples and words were rewritten entirely. The approach drew on Lewis Baker's coroutine series; the examples and words are my own.

- [Coroutines (C++) (cppreference)][cppref-coro] — the sources for the coroutine definition, restrictions, the four kinds of coroutine-state content, parameter copies and the dangling example, the conditions for eliding allocation, promise_type deduction, and co_return semantics.
- [Coroutine definitions (current draft dcl.fct.def.coroutine)][draft-def] — the replacement body's structure, get_return_object's timing, the lookup rules for allocation and deallocation functions, get_return_object_on_allocation_failure, the parameter-copy rules, and final_suspend's noexcept requirement.
- [Yielding a value (current draft expr.yield)][draft-yield] — co_yield is equivalent to co_await promise.yield_value(e).
- [Resumption (current draft coroutine.handle.resumption)][draft-resumption] — the preconditions of resume and destroy, and the implementation-defined behavior of resuming across execution agents.
- [Trivial awaitables (current draft coroutine.trivial.awaitables)][draft-trivial] — the await_ready semantics of suspend_always and suspend_never.
- [Synopsis of the coroutine support-library header (current draft coroutine.syn)][draft-syn] — the structure of the coroutine library support; coroutine_handle and the two suspension bodies both live here.
- [The co_return statement (current draft stmt.return.coroutine)][draft-coret] — the source of the undefined behavior for running to the end and for return_void overload resolution.
- [P3950R1: return_value & return_void Are Not Mutually Exclusive][p3950] — the source for the forward-looking claim that the standard relaxes the mutual exclusion only at C++29.
- [std::coroutine_handle (cppreference)][cppref-handle] — the non-owning handle's member list: resume, done, destroy, promise, from_promise.
- [Understanding the Compiler Transform — Lewis Baker][baker-transform] — the explanation of the compiler transform, the reference for this article's approach; the examples are my own.

[cppref-coro]: https://en.cppreference.com/w/cpp/language/coroutines
[draft-def]: https://eel.is/c++draft/dcl.fct.def.coroutine
[draft-yield]: https://eel.is/c++draft/expr.yield
[draft-resumption]: https://eel.is/c++draft/coroutine.handle.resumption
[draft-trivial]: https://eel.is/c++draft/coroutine.trivial.awaitables
[draft-syn]: https://eel.is/c++draft/coroutine.syn
[draft-coret]: https://eel.is/c++draft/stmt.return.coroutine
[p3950]: https://open-std.org/jtc1/sc22/wg21/docs/papers/2026/p3950r1.pdf
[cppref-handle]: https://en.cppreference.com/w/cpp/coroutine/coroutine_handle
[baker-transform]: https://lewissbaker.github.io/2022/08/27/understanding-the-compiler-transform
