---
title: "Who Owns the Data's Lifetime"
description: "See clearly how std::thread arguments undergo decay-copy and how move-only types get passed, and learn the two repair paths for dangling references."
chapter: 5
order: 4
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17, 20]
translation:
  source: documents/vol5-concurrency/chapter01-start-concurrency/04-lifetime-and-ownership.md
  source_hash: 89c93f51a72d7c27e3f2f6ce161754b988ab71c96a52d4d23bbc53452e6cc6a1
  translated_at: '2026-11-22T13:28:59+00:00'
  engine: glm-5.3
  token_count: 15005
---
# Who Owns the Data's Lifetime

Oi — promises on the ticket have to be honored, right? At the end of the last article we made promises, and this one delivers two of them: how argument copying happens, and when references dangle. As for the second matter — er, all right, we like decomposing problems, or to put it less politely, brain capacity runs short, so we choose the divide-and-conquer approach.

## How Arguments Get Passed: decay-copy and move-only Types

### The Copy Happens in the Spawning Thread

The last article left a loose end: in `std::thread t(print_hello, 42)`, "the arguments get copied" — how does that copy actually happen? Let's dig out what cppreference specifies on [the `std::thread` constructor page][cppref-ctor] (since C++11): the new thread starts by executing a call that looks like this:

```text
INVOKE(decay-copy(std::forward<F>(f)),
       decay-copy(std::forward<Args>(args))...)
```

INVOKE is simply the English word for "invoke"; plainly put, it means a thread is being started to run something. Run what? And what does the thing being run need? Naturally, a function, and the arguments the function needs to execute. Seen this way, we have explained our way to `INVOKE(f, args)`. std::forward is no surprise — it expresses forwarding that preserves lvalue/rvalue-ness. The one that deserves a careful look is `decay-copy`.

decay-copy means "decayed copy" — the formal name for what happens here. In plain words: every argument is run through the same treatment that function-template argument passing applies — **references are stripped off, top-level const and volatile are dropped, arrays decay into pointers, and functions decay into function pointers**.

All right, I know some kid is asking — even though zero people asked me. Hey, I say, are the standard-library folks just freeloading? What's with wrapping on this extra layer — is C++ not troublesome enough for you?

Not at all. We must keep tying back to what we mentioned earlier: from now on, our code no longer operates synchronously, and you yourself don't know when these variables you're using will actually be used. Put another way: "**if our variables' lifetimes were not passed along the decay-copy way, we would have no way to guarantee our program runs safely**." The function holding our `arg` may return at any moment, the variable may be destroyed at any moment, and the new thread only gets around to using the argument once it actually runs — with an unguaranteed stretch of time in between. So **the default action is to copy on the spot, into the thread object's internal storage, and the new thread uses only its own copy from start to finish**. Pass-by-value is the safe default; if you don't want to copy and want to share instead, you must write `std::ref` explicitly — next subsection.

Copy explained — what about decay? Because there are two kinds of things that "copy by value" simply cannot do. Arrays have no value semantics: you can't pass an array by value to any function, nor initialize one array from another (we tried; the compiler refuses on the spot). A function type isn't even an object — what can be stored is a function pointer, and the closest copyable substitute is exactly the decayed pointer. References, in turn, must be stripped first; otherwise what gets stored in the internal storage is the reference itself, and the moment the spawning side's object is destroyed, a dangle walks in — the copy would have been made for nothing.

<OnlineCompilerDemo
  title="decay-copy: the thread gets a copy"
  source-path="code/examples/vol5/chapter01/22_decay_copy.cpp"
  description="The copy constructor runs on the thread side and the copies live at different addresses; a pointer passes only the address itself, and the array is not copied."
  run-options="-O2 -std=c++23 -pthread"
  allow-run
>

```cpp
// decay-copy:传给线程的对象是副本,指针和引用传的是地址本身
#include <cstddef>
#include <print>
#include <thread>

class Tracker {
  public:
    explicit Tracker(int v) : value_(v) {}
    Tracker(const Tracker& other) : value_(other.value_) {
        std::println("  copy ctor on thread {}, copy lives at {}", std::this_thread::get_id(),
                     (const void*)this);
    }
    int value() const { return value_; }

  private:
    int value_;
};

void work(Tracker t) {
    std::println("  worker got its own object at {}, value {}", (const void*)&t, t.value());
}

void read_array(int* data, std::size_t n) {
    std::println("  worker sees an int* holding {} elements", n);
    long long s = 0;
    for (std::size_t i = 0; i < n; ++i)
        s += data[i];
    std::println("  sum {}", s);
}

int main() {
    Tracker original(7);
    std::println("main thread id {}", std::this_thread::get_id());
    std::println("original lives at {}", (const void*)&original);
    std::thread t(work, original);
    t.join();

    int arr[5] = {1, 2, 3, 4, 5};
    std::thread t2(read_array, arr, 5);
    t2.join();
}
```

</OnlineCompilerDemo>

Our output looks like this; the thread IDs are values from our machine (yours will differ), and the addresses will vary too:

```text
main thread id 127628787791744
original lives at 0x7fff145e0bb4
  copy ctor on thread 127628787791744, copy lives at 0x5bfa3b942038
  copy ctor on thread 127628780500672, copy lives at 0x7413e41fedd4
  worker got its own object at 0x7413e41fedd4, value 7
  worker sees an int* holding 5 elements
  sum 15
```

The first copy's thread ID matches main's exactly — live evidence that "the copy happens in the spawning thread": decay-copy copied `original` into the thread object's internal storage. The second copy happens on the new thread: it takes the stored copy to call `work`, `work` takes its parameter by value, so it gets copied once more; the address reported by `worker got its own object` is the copy living in the function parameter. Count the addresses: original object, internal storage, function parameter — three distinct places, and what the thread holds is always its own copy.

The array half of the output has its own story — it corresponds to `read_array`: inside the decay-copy, `arr` became an `int*`, and the length information was lost on the spot, which is why we have to carry an extra `n` into the function signature. References? A non-const reference won't bind — you'll see that right away. A const reference compiles, but it refers to the thread-internal copy. To truly share, you must write `std::ref` — next subsection.

### To Share, Write ref Explicitly

Suppose the thread function genuinely wants a reference — what happens if you pass one directly? We compiled it once:

<OnlineCompilerDemo
  title="Passing a reference directly: does not compile"
  source-path="code/examples/vol5/chapter01/23_ref_compile_error.cpp"
  description="After decay-copy the copy cannot bind back to a non-const reference, and the compiler refuses outright; the erroneous form is kept in a comment (error in the text), and the correct form uses std::ref."
  run-options="-O2 -std=c++23 -pthread"
  allow-run
>

```cpp
#include <print>
#include <thread>

void update_value(int& x) {
    x = 42;
}

int main() {
    int value = 0;
    // std::thread t(update_value, value);  // 编译不过
    std::thread t(update_value, std::ref(value));
    t.join();
    std::println("value = {}", value);
}
```

</OnlineCompilerDemo>

```text
error: static assertion failed: std::thread arguments must be invocable after conversion to rvalues
```

`update_value` wants an `int&`, but after decay-copy the thread holds only an rvalue `int`, and an rvalue won't bind to a non-const reference (the error comes from libstdc++ 16.2.1's static_assert; the other error lines are its knock-on effects). This compile error is the standard library enforcing its "default to by-value" stance: sharing is a dangerous move, so if you want to share, you must write the word "share" into the code, where everyone reviewing it sees it at a glance. The tools for writing it are `std::ref` and `std::cref` (both since C++11, defined in `<functional>`): they wrap the reference into a copyable reference_wrapper, the thread unwraps it when calling the function, and the function receives a reference to the original object after all.

<OnlineCompilerDemo
  title="std::ref and std::cref: a real reference"
  source-path="code/examples/vol5/chapter01/24_ref_wrap.cpp"
  description="With the reference wrapper, the worker touches the very same object as main: same address, and the modification is visible."
  run-options="-O2 -std=c++23 -pthread"
  allow-run
>

```cpp
// std::ref 与 std::cref:让线程真的引用主线程的对象
#include <functional>
#include <print>
#include <string>
#include <thread>

void append_suffix(std::string& str, const std::string& suffix) {
    std::println("  worker touches str at {}", (void*)&str);
    str += suffix;
}

int main() {
    std::string message = "Hello";
    std::string suffix = " World";
    std::println("main's message lives at {}", (void*)&message);
    std::thread t(append_suffix, std::ref(message), std::cref(suffix));
    t.join();
    std::println("message = {}", message);
}
```

</OnlineCompilerDemo>

We ran it: the addresses in `worker touches str at` and `main's message lives at` are exactly the same, and `message` really did become `Hello World` — the thread modified the very object in main. This example is safe because `join()` is present: the object is still alive while the thread uses `message`. Swap join for detach, and nobody is left in charge of when the thread's reference to `message` expires; the dangling references dissected in the next section are exactly this kind of thing.

### move-only Types: Ownership Goes with the Thread

Not every type can be copied. The `std::unique_ptr` you met back in Volume 2's smart-pointer sections is one such. For these move-only types (copy constructor deleted, move supported only), moving into the thread is the proper route:

<OnlineCompilerDemo
  title="move-only: a unique_ptr goes into the thread"
  source-path="code/examples/vol5/chapter01/25_move_unique.cpp"
  description="Ownership is handed into the thread via std::move, main's side becomes a null pointer, and each side does its own work."
  run-options="-O2 -std=c++23 -pthread"
  allow-run
>

```cpp
// move-only 类型:unique_ptr 的所有权随 std::move 交进线程
#include <cstddef>
#include <memory>
#include <print>
#include <thread>

void process_data(std::unique_ptr<int[]> data, std::size_t size) {
    for (std::size_t i = 0; i < size; ++i)
        data[i] *= 2;
    std::println("  worker: data[0] = {}", data[0]);
}

int main() {
    constexpr std::size_t kSize = 5;
    auto data = std::make_unique<int[]>(kSize);
    for (std::size_t i = 0; i < kSize; ++i)
        data[i] = static_cast<int>(i + 1);
    std::thread t(process_data, std::move(data), kSize);
    std::println("main: data after move is {}", data ? "not null" : "null");
    t.join();
}
```

</OnlineCompilerDemo>

We got `main: data after move is null`: `std::move(data)` transferred ownership of the whole block of memory into the thread's internal storage, the pointer in main's hand went null on the spot, and the worker side modified the data as usual. The thread function `process_data` exclusively owns the data moved into the thread — no hand-off needed after use; when the function returns, the `unique_ptr` releases it automatically. In this code, the thread is the sole owner of that data; no second flow of execution reads or writes it.

## Dangling References: Two Repair Paths

A dangling reference names one specific thing: the thread is still gripping the reference, while the object it points to is already gone. That swap-from-join in the reference-sharing discussion above is precisely the high-incidence zone. We'll run both crime scenes for you, then give the two repair paths.

### Scene One: A Local Variable After detach

<OnlineCompilerDemo
  title="Dangling reference: reading garbage off the old stack"
  source-path="code/examples/vol5/chapter01/26_dangling_ref.cpp"
  description="A local variable captured by reference cannot outlive the function; after the function returns the stack gets overwritten, and what the thread reads is pure luck — it may also look perfectly normal."
  run-options="-O2 -std=c++23 -pthread"
  allow-run
>

```cpp
// 悬垂引用:按引用捕获活不过本函数的局部变量,线程读到的是旧栈上的垃圾
#include <chrono>
#include <cstring>
#include <print>
#include <thread>

void burn_stack() { // 往栈上灌别的数据，看能不能把旧值覆盖出来
    volatile char area[16384];
    std::memset(const_cast<char*>(area), '#', sizeof(area));
}

void spawn() {
    int local_value = 42;
    std::thread t([&local_value] { // 按引用捕获一个活不过本函数的局部变量
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        std::println("thread reads: {}", local_value);
    });
    t.detach();
}

int main() {
    spawn();
    burn_stack();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    std::println("main exits");
}
```

</OnlineCompilerDemo>

The `local_value` in `spawn` is a stack local; the lambda captures it by reference, and the thread gets detached. The moment `spawn` returns, `local_value` is destroyed — while the detached thread still means to read it 150 milliseconds later. On main's side we arranged `burn_stack`, deliberately pouring sixteen thousand bytes of other data onto the stack.

We ran it ten times. The -O2 runs all printed `thread reads: 42`. Switch to -O0 (optimizations off) and run five more, and all five printed `thread reads: 589505315`. That number has a history: it is 0x23232323 in hexadecimal — exactly the integer formed by four bytes of the hash signs `burn_stack` poured onto the stack (the ASCII code for # is 0x23). What the thread read was data someone else had written into that memory. Same code, one change of optimization level, and the read goes from the original value to someone else's data; behind it lies the different landing spots of function stack frames under the two levels. We didn't go check the disassembly — the mechanism gets a pointer and no more. This result must be handed to you as it is: it does not mean those -O2 runs were safe. Reading an object after it's destroyed gets zero guarantees from the standard — reading the original value, reading garbage, or taking down the process are all within the permitted range. My feeling after running it: this round of experiments showed a dangling read changing its data with the optimization level, but never showed it crash on the spot; to actually force a crash out, you'd need a different call depth or a different machine.

### Scene Two: The this Pointer Captured by a Lambda

When you someday start threads inside classes, the most common shape is a member function starting a thread whose lambda captures `this`. Capturing `this` captures a pointer: only as long as the object lives does that pointer count as alive.

<OnlineCompilerDemo
  title="Destroying the object after capturing this: undefined behavior"
  source-path="code/examples/vol5/chapter01/27_this_capture_ub.cpp"
  description="The thread runs gripping the object's pointer while the object leaves first. Reading garbage, looking normal, or crashing — all of them are its faces."
  run-options="-O2 -std=c++23 -pthread"
  allow-run
>

```cpp
// 捕获 this 指针后销毁对象:未定义行为,读到垃圾或崩溃都是它的样子
#include <chrono>
#include <print>
#include <thread>

class Worker {
  public:
    void start() {
        t_ = std::thread([this] { // 捕获的是指针，不是对象
            for (int i = 0; i < 8; ++i) {
                std::println("tick {}, sees running_ = {}", i, running_);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });
        t_.detach();
    }

  private:
    bool running_ = true;
    std::thread t_;
};

int main() {
    Worker* w = new Worker;
    w->start();
    delete w; // 对象立刻销毁，线程还在跑
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    std::println("main exits");
}
```

</OnlineCompilerDemo>

This program is deliberately wrong; running it counts as observing the shape of the error. Right after `start` we `delete w`, and the thread is left holding only a `this` pointing at a destroyed object. We ran it three times; across each run's eight output lines, the readings of `running_` went like this: one run read 114, one read 39, one read 82 — stable within a run, different across runs. `running_`'s true value is `true`, which would normally read as 1, and none of the three numbers is 1. The thread was reading memory the allocator had repurposed after the `delete`. As for who wrote the 114 and who wrote the 39 — unknown, and no need to know; that is no longer your object anyway.

Even if you set the flag to false in the destructor (a common self-consolation in this kind of code), you still can't close the timing window: after detach there is no mechanism waiting for the thread to exit; at the moment the flag is flipped, the thread may be sleeping halfway through, and on waking it will still go read a block of memory that belongs to no one.

There is one more shape with the same root, one you have already seen in the previous article A First std::thread Program: the `Accumulator` is copied wholesale into the thread, and its `data_` is a reference member — only the reference itself gets copied in. The judgment rule is the same as for a lambda's by-reference capture: the pointed-to vector must live until the thread is done with it, and both repair paths apply to it equally. How to choose? Let's start with the cheap one.

### Repair Path One: Copy the Data into the Thread

The first repair path fixes both scenes; we'll demonstrate on scene one, where it's cheapest: swap the reference for a value, and the thread gets its own copy.

<OnlineCompilerDemo
  title="Capture by value: the thread gets its own copy"
  source-path="code/examples/vol5/chapter01/28_value_capture_safe.cpp"
  description="The same scenario switched to capture by value; the parent function returning early is no longer feared, and the thread prints intact as ever."
  run-options="-O2 -std=c++23 -pthread"
  allow-run
>

```cpp
// 值捕获修复悬垂:线程拿自己的副本,父函数先走也不怕
#include <chrono>
#include <print>
#include <string>
#include <thread>

void safe_version() {
    std::string message = "Hello from parent";
    std::thread t([message] { // 值捕获：线程拿自己的副本
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::println("thread sees: {}", message);
    });
    t.detach();
}

int main() {
    safe_version();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    std::println("main exits");
}
```

</OnlineCompilerDemo>

With `[message]` capturing by value, the lambda holds its own copy, and at `std::thread` construction the entire closure object is decay-copied into internal storage. From start to finish the thread reads only its own data; the destruction of the outer `message` has nothing to do with it, and detach is safe here. We ran it three times, and all three printed `thread sees: Hello from parent`.

Capture by value is copying: for small objects like `int`s and pointers it doesn't matter, but a several-hundred-megabyte `vector` deserves some weighing. When we can't afford the copy, take the second path.

### Repair Path Two: Extend the Lifetime with shared_ptr

The second repair path mainly fixes scene two — the situation where object and thread live their own separate lives yet must share the same data. If the data can't be copied, or your weighing concludes the copy is unaffordable, `std::shared_ptr` (a shared-ownership smart pointer that uses an internal reference count to decide when the object is destroyed) is the ready-made compromise: as long as one `shared_ptr` still points at the object, the object is not destroyed.

<OnlineCompilerDemo
  title="shared_ptr plus an atomic flag: a safe ending"
  source-path="code/examples/vol5/chapter01/29_shared_stop_flag.cpp"
  description="After the object is destroyed, the thread exits cleanly via the shared stop flag; the reference count manages the lifetime for you."
  run-options="-O2 -std=c++23 -pthread"
  allow-run
>

```cpp
// shared_ptr 加 atomic 标志:对象销毁后线程仍能安全收场
#include <atomic>
#include <chrono>
#include <memory>
#include <print>
#include <thread>

class BackgroundWorker {
  public:
    BackgroundWorker() : running_(std::make_shared<std::atomic<bool>>(true)) {}

    void start() {
        auto running = running_; // 复制 shared_ptr，引用计数加一
        std::thread t([running] {
            while (running->load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
            std::println("worker exits cleanly");
        });
        t.detach();
    }

    void stop() { running_->store(false); }
    ~BackgroundWorker() { stop(); }

  private:
    std::shared_ptr<std::atomic<bool>> running_;
};

int main() {
    {
        BackgroundWorker worker;
        worker.start();
    }
    std::println("worker object already destroyed");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::println("main exits");
}
```

</OnlineCompilerDemo>

The three lines from that run of ours look like this:

```text
worker object already destroyed
worker exits cleanly
main exits
```

What we read is the record of one particular run; this code has two possible timelines, and each one's output is legitimate. The worker's first read of the flag may happen before the destructor: it reads true, enters the 200-millisecond sleep, the destructor sets the flag false, the thread wakes to read false, leaves the loop, and prints `worker exits cleanly`. If the thread first reads the flag only after stop, it exits immediately, printing the same line. Which timeline is running, and in what order the three lines land — none of that is guaranteed, so we just read it by the record we saw. The shared_ptr guarantees the `atomic<bool>` object lives until the thread's last read of it; the object's lifetime follows the last shared pointer, not the class that owns it.

cppreference states on [the shared_ptr page][cppref-shrptr]: different `shared_ptr` objects may have their member functions called concurrently, even when they share ownership of the same object; the implementation notes say the reference count is maintained with atomic operations. shared_ptr only guarantees the object won't be destroyed too early — what it manages is ownership and the control block (that internal block of data recording the reference count); concurrent access inside the managed object is still yours to arrange. This example chose `atomic<bool>` as the shared object precisely so the flag can stand on its own; swap in a `shared_ptr<std::vector<int>>` and let two threads `push_back` at once, and it will scramble just the same — that is the business of the data-race articles in the next unit.

### Capture Syntax and Its Era

Beyond the two repair paths, the lambda capture syntax itself has a few things worth saying. `[&]` drags every local variable in by reference with one broad stroke, including the ones you never meant to share; once the function grows long, reviewers can no longer tell which reference was left in on purpose. Write the capture list explicitly — name what you take — and both intent and risk sit in plain view.

Capturing the whole object has its own dedicated syntax, `[=, *this]`: we capture `*this` by value, and the thread gets a copy of the entire object.

> It only entered the standard in C++17. We tried it under GCC 16.2.1: `-std=c++17` and `-std=c++20` compile outright; under `-std=c++14` GCC accepts it as an extension and gives a `-Wc++17-extensions` warning, and only adding `-pedantic-errors` truly fails the compile.

The other is a deprecation warning. Since C++20, implicitly capturing `this` via `[=]` is deprecated (proposal P0806; link collected in the reference resources). We tested with a small snippet: under `-std=c++20` with `-Wdeprecated`, GCC 16.2.1 gives:

```text
warning: implicit capture of ‘this’ via ‘[=]’ is deprecated in C++20 [-Wdeprecated]
    4 |         return [=] { return n; };
note: add explicit ‘this’ or ‘*this’ capture
```

However the syntax changes, only one principle stands behind it: the referenced object must live until the thread is done with it. The two repair paths are your manual ways of keeping that sentence true; handing that maintenance over to a destructor to do automatically is the topic of the next article, Who Owns the Thread's Lifetime.

## Next Steps

This article gathered up the data side: either copy into the thread (which the default decay-copy has been doing all along), or use `shared_ptr` to extend the lifetime until the last shared pointer goes away. You now hold two pieces of repair code, both portable straight into your own programs. With the data settled, the thread itself still hangs: join sits at the end of the function, one exception and it gets skipped — who catches that? Taking the join responsibility out of your memory and handing it to a destructor is the topic of the next article, Who Owns the Thread's Lifetime; thread_guard and a hand-written JoiningThread are waiting for you there.

## Reference Resources

While writing this article I drew on Anthony Williams' C++ Concurrency in Action (Manning, second edition); the examples and prose are my own. Claims about C++ behavior were compiled and tested or verified against cppreference as noted in the text. std::thread, ref/cref, and shared_ptr have been available since C++11; make_unique is C++14; [=, *this] is C++17; the deprecation of implicit this capture via `[=]` is C++20. Dangling-reference outputs follow real run records; results of undefined behavior may differ on your machine.

- [std::thread::thread (cppreference)][cppref-ctor] — constructor semantics and the rule that the decay-copy is evaluated in the spawning thread.
- [std::ref and std::cref (cppreference)][cppref-ref] — producing and unwrapping the reference_wrapper.
- [std::shared_ptr (cppreference)][cppref-shrptr] — the scope of guarantees for concurrent calls and reference counting.
- [P0806: Deprecate Implicit Capture of this][p0806] — the proposal that deprecated implicit this capture via `[=]` in C++20.

[cppref-ctor]: https://en.cppreference.com/w/cpp/thread/thread/thread
[cppref-ref]: https://en.cppreference.com/w/cpp/utility/functional/ref
[cppref-shrptr]: https://en.cppreference.com/w/cpp/memory/shared_ptr
[p0806]: https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2017/p0806r0.html
