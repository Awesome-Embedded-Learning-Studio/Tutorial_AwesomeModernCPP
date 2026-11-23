---
title: "Wait 300ms Without Spawning a Task: Software Timers and fn+void* Hard Currency"
description: "commit 766c462 removed waiting from tasks: base::Callback uses fn + void* as bare-metal hard currency, trivially copyable static assertions block std::function's heap and capture, TimerBase's single-shot cold on expiration, periodic self-rotation rehanging, stop idempotency, even race conditions in self-termination within callbacks are tested, six timer test cases on the host all pass, I built a cross-compile copy in /tmp, Renode with t triggered a single-shot timer for 300ms, virtual time 2.3 seconds armed, 2.6 seconds callback spoke, while task c armed and then left, worker's heavy work and timer's reminder now use the same coin"
chapter: 5
order: 3
tags:
  - stm32f1
  - intermediate
  - embedded
  - callback mechanism
  - cpp-modern
difficulty: intermediate
platform: stm32f1
cpp_standard: [23]
reading_time_minutes: 22
prerequisites:
  - "The Definition of a Second: Ticks, Wrap-Around Arithmetic and Expiry Queues"
  - "Faking a Birth Certificate on the Stack: PendSV, SVC and the First Context Switch"
related:
  - "The Last Will of the Chip: HardFault Context, Stack Canary and Zero Allocation Logging"
  - "Interrupts Only Send Notes: Circular Queues, Worker Threads and Deferred Bottom Halves"
current_status:
  title: 300ms Without Spawning a Task
  detail: The ZerOS chapter is being polished
  variant: polishing
  icon: ⏰
translation:
  source: documents/vol8-domains/embedded/zeros/05-context-economics/03-software-timer-and-callback.md
  source_hash: d22edb73455bdc5c4a2d9c46224c9522646c99a811aeac16dea5804a0ee7a394
  translated_at: '2026-11-22T13:46:42+00:00'
  engine: qwen3:14b
  token_count: 14155
---
# Wait 300ms Without Spawning a Task: Software Timers and fn+void* Hard Currency

When we concluded the article "Interrupts Only Send Notes," we left a question: the worker does immediate work, but what if we just want to be reminded after 300ms? To get a reminder, we would have to spawn a task, which would require a task control block, an aligned stack, and a full set of procedures for its birth. However, all it does is sleep, wake up at the right time, and then sleep again. Waiting should be the cheapest service in the kernel, and `766c462` made it so, with the subject being "feat: timer setup," changing 13 files, adding 309 lines, and removing 16 lines, software timers have now entered the kernel. The theme of our site is **which context the code runs in**, "The Last Will of the Chip" handles the context after a crash, "Interrupts Only Send Notes" handles who the interrupt hands the heavy work to, and timers handle the final part: how to wait without using a task or touching the heap.

## Let's Calculate the Cost of a Reminder

We've already counted the cost of tasks in the scheduler section: a TCB, an aligned stack, and the kernel has to schedule it. The smallest stack in a spawned task is 64 words, 256 bytes of RAM, which is just the stack itself, not counting the management structures. The idle stack is a different story, it doesn't go through the spawn procedure, it's 32 words long, 128 bytes, and it's the dedicated stack that `start_scheduler` receives. If the system's requirement is just "remind me periodically," then having a task resident for that is like having a full-time employee sitting at their desk, whose only job is to ring a bell at noon.

What is the background of the timer provided by `766c462`? Let's look at its declaration in the demo:

```cpp
// 't' arms a one-shot reminder — no task is consumed while it waits
constinit ZerOS::clock::Timer g_demo_timer{};

void timer_ping(void*) {
    ZerOS::board::print("timer: fired (tick context, no task spent)\r\n");
}
```

With the `constinit` declaration, everything is in place at compile time, the comments write the promises next to the name, and throughout the process, no task is consumed. The comments are not just slogans, we can verify them in the source code: what the timer looks like, who it uses for timing, and who executes the callback after it expires, all three things are supported by real code.

## base::Callback: fn + void*, the Bare-Metal Hard Currency

The user's code that the timer executes after expiration needs a place to be stored to run. The container for callbacks is `std::function` in the desktop world, which can store everything: lambdas, function objects, and calls with a bunch of bound parameters. The cost is hidden in the fact that it can store everything. When a lambda that captures variables is put into `std::function`, the state needs to be stored somewhere, and when the state doesn't fit into the small buffer inside the implementation, the implementation goes to the heap to get a block of memory. Copying a callback, we're managing the lifecycle of an unknown type on the heap, which makes it not trivially copyable, and we can't even stuff it into a queue or TCB by bytes. `std::function` is the rich-world approach of the desktop, and the bare-metal can't afford it.

`include/ZerOS/base/callback.hpp` provides the answer for the bare-metal, the whole file is only 28 lines long, we'll read it in two parts, the first part establishes the shape of the currency:

```cpp
#pragma once

#include <type_traits>

namespace ZerOS::base {

// the bare-metal currency: a function plus its context. No closures, no
// heap — whenever something must "call back later", this is the coin.
struct Callback {
    using Fn = void (*)(void*);

    Fn fn{};
    void* arg{};

    constexpr Callback() = default;
    constexpr Callback(Fn f, void* a) noexcept : fn(f), arg(a) {}
```

The author's own comments establish its status, we'll translate it into plain language: this is the currency of the bare-metal, a combination of a function pointer and a context pointer, no closures, no heap, and wherever the kernel needs to call you back, it's this one. All members are in compile-time shape, even the constructor is marked `constexpr`.

We also pause on the design of `void* arg`. When the callback is executed, the function pointer receives the `arg` pointer, and the interpretation of the context is up to the caller. For callbacks that need to carry state, the posture is to put the address of the state into `arg` (the test case where the timer commits suicide is done this way, we'll look at it later), for stateless callbacks, just put `nullptr` and that's it. We lose the convenience of lambda captures, but gain the certainty of two pointers. This trade-off is considered worthwhile in projects where every byte in flash counts.

Once the shape is established, the second half has its actions and assertions, we'll continue to look:

```cpp
    void invoke() const noexcept {
        if (fn != nullptr) {
            fn(arg);
        }
    }
    explicit operator bool() const noexcept { return fn != nullptr; }
};

static_assert(std::is_trivially_copyable_v<Callback>); // must ride queues and TCBs

} // namespace ZerOS::base
```

`invoke` checks for null before calling, `operator bool` lets you know whether it contains something. The real threshold is the last line `static_assert`: it must be trivially copyable, the comment says, being able to ride the queue and TCB is its hard condition. This assertion excludes `std::function` in an interesting way, it doesn't argue with you about whether the heap should be used, it only requires that this type can be copied by bytes, and `std::function` is inherently unable to pass, saving the argument.

## TimerBase: Timers with Inherent Reactions

The real body of the timer is in `include/ZerOS/kernel/clock/timer.hpp`, before we reveal the skeleton, we need to address the issue of executing at expiration. The timer needs to run the user's code when it expires, and to run it, someone has to call it. The person who calls is the time kernel seen in "The Definition of a Second": hang a waiter in the expiry queue, and when it's time, the kernel calls the reaction carried by the waiter. In the ticker section, we handled this procedure ourselves, the reaction was extracted from a lambda with no captures into a function pointer, custom on-site. The procedure itself is not difficult, the trouble is that it's all done manually, every time you make a timer, you have to go through the whole process of stripping the lambda and hanging the queue, if you miss any step, no one will answer when it's time. `766c462`'s approach is to put this procedure into the timer's own constructor, we'll look at the skeleton:

```cpp
template <ZerOS::system::SystemContext System> struct TimerBase {
    constexpr TimerBase() noexcept : waiter_{Ticks{0}, &TimerBase::fired} {}
```

The first line is worth pausing on. The timer has an embedded `TimeWaiter`, and when it's born, it sets its reaction, the reaction is its own `TimerBase::fired` static function, the user no longer needs to strip the lambda themselves. This is two uses of the same kernel component: the kernel manages the expiry queue, and the timer is the user-side standing on the queue.

We also recognize the trick of `owner_of`, in "Faking a Birth Certificate on the Stack," the TCB used it to infer its owner from the embedded waiter, we'll look at the timer version:

```cpp
    [[nodiscard]] static TimerBase* owner_of(base::BorrowedPtr<clock::TimeWaiter> waiter) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
        return reinterpret_cast<TimerBase*>(reinterpret_cast<std::byte*>(waiter.get()) -
                                            offsetof(TimerBase, waiter_));
#pragma GCC diagnostic pop
    }
```

The address of the embedded member minus its offset in the structure gives the timer's own address, the kernel only receives the pointer to the waiter from the expiry queue, the timer's identity is recovered through this subtraction, no additional mapping tables need to be maintained. `offsetof` has opinions on non-standard layout types, so there's a compilation warning here, we'll tell you honestly, this suppression is the price we pay for the trick, there's no free convenience here.

The three entry points for arming are in the same file, we'll look at them one by one:

```cpp
    // once: fn(arg) fires after `span`, then the timer goes cold
    void oneshot(Milliseconds span, base::Callback::Fn fn, void* arg) noexcept {
        arm_me(0, span, base::Callback{fn, arg});
    }

    // forever: every `span` — until stop(), even from inside fn
    void periodic(Milliseconds span, base::Callback::Fn fn, void* arg) noexcept {
        arm_me(span.count, span, base::Callback{fn, arg});
    }

    void stop() noexcept {
        auto& s = System::self();
        ZerOS::irq::CriticalGuard g{s};
        period_ = 0;
        running_ = false;
        s.cancel_timer(&waiter_); // idempotent: not booked = false, harmless
    }
```

The only difference between single-shot and periodic is whether the period passed to `arm_me` is zero or non-zero. `stop` enters the critical section immediately, `period_` is cleared to zero, `running_` is set to false, and then it asks the system for `cancel_timer`, removing the waiter registered in the time kernel. The comment explains the idempotent semantics: if the waiter is not registered, `cancel` returns false, no noise, no one will worry about its return value. You stop a timer that has never been armed, it quietly lets you stop, that's the idempotent approach.

The actual arming is the private `arm_me`, its order is important, we'll look at it:

```cpp
    void arm_me(Ticks::tick_t period, Milliseconds span, base::Callback callback) {
        auto& s = System::self();
        ZerOS::irq::CriticalGuard g{s}; // fields only move under the lock —
        period_ = period;               // a firing tick may be reading them
        call_ = callback;
        running_ = true;
        s.arm_timer(&waiter_, span.count);
    }
```

We'll recite its discipline: field changes can only be made under the lock, the comment explains the reason next to it, the expiration may be reading them at that moment. `period_`, `call_`, and `running_` are the shared states between the timer and the tick, every write is done with interrupts disabled, this is the common discipline of all shared states in this file. The critical section on the board uses BASEPRI, the masked level is 0x40, and the priority of SysTick is pressed to the lowest in `init_time` (the source code writes 0xFF to the high byte of SHPR3, NVIC only implements the high four bits of the priority, the effective level on the silicon is equivalent to 0xF0, the lowest level), when writing the fields, the tick can't even get in, the read and write don't interfere.

## After Expiration: The Two Branches of `fired`

The executor at expiration is the `fired` that is born with it, it is the engine of the entire timer, we'll look at it completely:

```cpp
    static void fired(base::BorrowedPtr<TimeWaiter> w) {
        auto* t = owner_of(w);
        t->running_ = false;    // oneshot cools off by default
        t->call_.invoke();      // user fn (tick context!)
        if (t->period_ != 0) {  // periodic: re-book — a stop() INSIDE fn
            t->running_ = true; // already zeroed period_ and walks free
            System::self().arm_timer(w, t->period_);
        }
    }
```

Its three actions we'll recite in order. The first action is `running_` being set to false, single-shot timers cool down at expiration, the comment says this is the default outcome. Then it executes the user's callback, the comment's line "tick context!" is a fact you need to keep in mind, the callback runs in the tick context, not on any task, we'll verify it with real output in the verification section. Finally, it checks `period_`, if it's non-zero, it rehangs for the next round, periodic timers spin up this way, the manual self-rotation of the ticker in "The Definition of a Second" has become the standard behavior of the kernel component, there is still no specific type of periodic timer in the kernel, the essence of periodic is still "registering again in the callback," but the registration step is done by the kernel on behalf.

And the weight of the two lines in the comment can only be understood with the original text of `stop`. We imagine this scene: the periodic timer has reached its time, `fired` runs into the user's callback, but the callback does is to stop the timer itself. After the callback returns, `fired` comes back to read `period_`, it reads zero because `stop` cleared it under the lock, the branch to rehang doesn't go in, the timer exits the stage like that. The action of self-termination within the callback looks like it's competing with the time kernel for an object that's executing itself, but it's only competing for three flags, and the read and write of the flags are all done under the critical section with interrupts disabled, the order is fixed, clearing comes first, checking follows, the person checking always sees the world after clearing. If this guarantee is missing, the self-terminating timer will revive once, the period will be an extra round, this kind of error is deadly on the board, it happens every few hundred milliseconds, the investigator may not catch it even after turning white-haired.

## The Same Coin: Work Also Became Callback

The second destination of `base::Callback` in this commit is also worth looking at, that is the worker in "Interrupts Only Send Notes." The original `Work` structure in `user_worker.hpp` defined its own set of function pointers and `void*` fields, and was accompanied by a static method `InvokeSelf`. `766c462` deleted it entirely and replaced it with:

```cpp
using Work = base::Callback;
```

This is a light line of alias, we'll weigh its weight, the work in the worker's queue has now become the same type as the timer's callback. The signature of `defer` has changed, `Work::InvokeSelf(*user_work)` in `Loop` has been replaced with `user_work->invoke()`, the calling logic in our eyes hasn't changed a bit, because `Callback::invoke` does exactly what `InvokeSelf` used to do. The significance of the change is at the type level, we'll look at it together: all "execute later" things in the kernel now share the same simple, byte-copyable shape, the worker receives its heavy work, the timer receives its reminder, the currency is unified. In the demo, the worker's `heavy_job` and the timer's `timer_ping`, we can now measure them with the same ruler.

With the currency unified, the system's interface also needs to expand. The `SystemContext` concept is the system's list, we'll look at the two requirements added by `766c462`:

```cpp
    { s.arm_timer(w, span) };   // book a waiter on the clock ledger (timers)
    { s.cancel_timer(w) };      // ...and strike it off (idempotent)
```

The timer doesn't directly know any specific system, it only requires that the system will arm and also disarm. The implementation on the board is in `system.cpp`, each is just a single line of function call to the time kernel. This expansion has a small collateral cost, we'll tell you honestly: three old test files with manually crafted `FakeSys` (mock systems, those that replace real systems on the host for testing) all need to add `arm_timer` and `cancel_timer`, each file adds two lines of identical relay code. The interface has expanded, the implementer also needs to follow up, the discipline of the concept is strict.

The demo's button experiment also adds a role, we'll look at task c when it receives `t`:

```cpp
        } else if (c == 't') {
            ZerOS::board::print("c: timer armed, 300ms\r\n");
            g_demo_timer.oneshot(Milliseconds{300}, timer_ping, nullptr);
        }
```

We'll look at this arming: it's done in one line, task c says hello and then continues running its own loop, 300ms later `timer_ping` speaks in the tick context. Throughout the waiting period, c does what it needs to do, other tasks are not disturbed, this is the effect promised by the title.

## Host Exam: Six Cases Verify the Timer

The behavior of the timer is pure logic, we're perfectly suited to use the host as an exam hall. The exam paper is the written `test/test_timer.cpp`, 170 lines long, with six `TEST_CASE`s and eleven assertions. They guard each vital point, we'll pick three to look at.

The first one we pick is responsible for the single-shot accuracy, it won't even let a single tick pass early:

```cpp
TEST_CASE("oneshot fires exactly on time, not a tick earlier", "[timer]") {
    World w;
    Tmr t;

    t.oneshot(Milliseconds{5}, kCounting, nullptr);
    w.time.on_elapsed(4);
    CHECK(g_fires == 0); // one tick early: silence
    w.time.on_elapsed(1);
    CHECK(g_fires == 1); // right on the deadline
}
```

We armed a 5-tick defense, turned 4 ticks, the count should be zero, then turned one more tick, the count should be 1. The boundary is so strict for a reason, the sorting and wrap-around judgment of the expiry queue in "The Definition of a Second" was checked one by one, the timer's accuracy is based on that judgment, ringing early by one tick or late by ten ticks is equally wrong in our eyes.

The second one we look at is the dedicated test for the race condition in the title:

```cpp
TEST_CASE("a periodic may stop ITSELF from inside its own fn", "[timer]") {
    World w;
    Tmr t;

    constexpr auto self_destruct = +[](void* self) {
        ++g_fires;
        static_cast<Tmr*>(self)->stop(); // suicide note, honored
    };
    t.periodic(Milliseconds{4}, self_destruct, &t);

    w.time.on_elapsed(4);
    CHECK(g_fires == 1);
    w.time.on_elapsed(40); // dead by its own hand: no further fires
    CHECK(g_fires == 1);
}
```

The callback carries its own address, executes halfway and stops itself, the comment writes it as a will, the will is respected. The assertion checks two moments: the first round it definitely rings, then turns 40 ticks, the count is unchanged. We explained the mechanism in the source code, the zeroing is done under the lock, the check comes after the zeroing, and it's verified here. If the rehang branch isn't blocked, 40 ticks of time are enough for it to ring ten more rounds, the count would have been more than 1 long ago.

The third one we look at is responsible for whether a cooled timer can be used again:

```cpp
TEST_CASE("a cold oneshot can be armed again", "[timer]") {
    World w;
    Tmr t;

    t.oneshot(Milliseconds{2}, kCounting, nullptr);
    w.time.on_elapsed(2);
    CHECK(g_fires == 1);

    t.oneshot(Milliseconds{9}, kCounting, nullptr); // second life
    w.time.on_elapsed(9);
    CHECK(g_fires == 2);
}
```

After the timer cools down, it arms again, it should work as good as new, the comment gives it the name of a second spring. Timers are reusable objects, just arm again after use, this test case establishes the semantics of reuse. In the remaining test cases, our gaze also sweeps over the cold-state verification of "after expiration, time continues to move, it no longer rings," the idempotent verification of "after stop, the registration is erased and it stays silent for 50 ticks," and the verification of periodic ringing three rounds, each on time, I won't go through them one by one, you can run it once and see them all.

We look at the host's full score sheet, it's real:

```text
100% tests passed out of 12

Total Test time (real) =   0.22 sec
```

The timer's own score sheet, we look at:

```text
All tests passed (11 assertions in 6 test cases)
```

My local GCC 16.2.1, six test cases with eleven assertions, not a single assertion has been moved by us.

::: warning
The callback runs in the tick context, and the time kernel's critical section is still held, interrupts on the board are masked. So the user code executed in `fired` must be short, putting a line of log or setting a flag is acceptable, but sleeping for 200ms is not its job, that kind of heavy work should be handed over to the worker as per the approach in "Interrupts Only Send Notes," the worker has its own task context, and sleeps its own sleep. The timer is responsible for the reminder, the to-do list is handled by the worker's queue.
:::

## Renode Acceptance: Press t to Arm 300ms

I reproduce the same chain: use `git archive 766c462` to export the tree to a copy in /tmp, the real STM32CubeF1 is copied from the local third_party into the copy (it's just a reference text in the warehouse, empty in the archive), the build commands and score sheet on the host side are the two segments above. The work on the board side uses the cross-toolchain arm-none-eabi-g++ 16.2.0, the two commands used are the same as in the previous article:

```shell
cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/arch/arm-none-eabi.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The size is real, we'll look at it:

```text
Memory region         Used Size  Region Size  %age Used
           FLASH:        5440 B        64 KB      8.30%
             RAM:        2880 B        20 KB     14.06%
   text	   data	    bss	    dec	    hex	filename
   5220	     220	   2656	   8096	   1fa0	.../zeros-bluepill.elf
```

Flash has increased from 5152 bytes in the previous article to 5440 bytes, the timer and callback's belongings have all been accommodated, there's still about 700 bytes left of the 6KB budget line of D12. The budget is still within reach, but the space is so tight that you can hear the ceiling. I report the numbers honestly.

For the Renode part, I'll repeat the experience honestly: my local Renode 1.17.0 can't recognize the `I2C.STM32F4_I2C` type in `bluepill.repl`, I commented out the two nodes i2c1 and i2c2 connected to the interrupt wiring in the copy, and the simulation started, the demo doesn't go through the i2c path, the behavior is unaffected. Injecting the button still uses the monitor's `WriteChar`: after the simulation runs, I feed a `0x74` to the receive side of usart1 from the monitor, it's the encoding of the character t. The real output of the serial window is as follows, I only extracted the lines related to the timer from the many M, L, H characters, they were all present in the original log:

```text
[INFO] usart1: [host:  2.13s (+0.32ms)|virt:       2s (+3.58µs)] L: locked, busy 300ms
[INFO] usart1: [host:  2.64s (+0.34s)|virt:     2.3s (+0.2s)] L: unlocked
[INFO] usart1: [host: 2.64s (+0.37ms)|virt:     2.3s (+3.49µs)] c: timer armed, 300ms
[INFO] usart1: [host:  2.64s (+0.36ms)|virt:     2.3s (+6.09µs)] H: LOCKED (inheritance paid off)
[INFO] usart1: [host: 2.65s (+11.07ms)|virt:    2.4s (+99.98ms)] M: running
[INFO] usart1: [host:  2.66s (+8.57ms)|virt:      2.5s (+100ms)] M: running
[INFO] usart1: [host: 2.73s (+65.94ms)|virt:      2.6s (+100ms)] timer: fired (tick context, no task spent)
[INFO] usart1: [host:  2.83s (+99.5ms)|virt:    2.7s (+99.99ms)] M: running
[INFO] usart1: [host:    3.13s (+0.1s)|virt:         3s (+0.1s)] A
```

You can see the verification facts in the virt column: I fed in t at virtual time 2.3 seconds, task c immediately reported "c: timer armed, 300ms," then it left, M ran normally at 2.5 seconds, at 2.6 seconds, which is the sum of 2.3 and 0.3, `timer_ping` reported "timer: fired (tick context, no task spent)." There was no error in the 300ms interval, even a single tick didn't go wrong during the waiting period. Task c armed and went on to do other things, the calm we see is the physical evidence left by the title on the board.

## Acceptance

The verifiable facts of this article are summarized into four items, we'll go through them one by one.

The first is the host exam. You check out to `766c462`, configure the build according to its commands, `ctest` should give you a score of all twelve targets passed, the score sheet of `test_timer` should be six test cases with eleven assertions.

The second is the expansion traces of the three old tests. In `test_sync.cpp`, `test_queue.cpp`, and `test_mutex.cpp`, the `FakeSys` in each has added two lines of relay code for `arm_timer` and `cancel_timer`, they are the products of adding two requirements to `SystemContext`, you can easily match them in the diff.

The third is the on-board `t` experiment. After cross-compiling, run Renode (the two i2c nodes need to be commented out as I said, or wait for the upstream to add the F1 I2C model), after the simulation runs, you feed a `t` to usart1 from the monitor, you should see the line "c: timer armed, 300ms," after exactly 300ms, "timer: fired (tick context, no task spent)" speaks, and during the virtual time in between, M, A, B's roles didn't miss a single tick.

The fourth is the source verification of the race condition test. `fired`'s rehang branch checks `period_`, `stop` clears `period_` to zero in the critical section, the test case that self-terminates in the callback passes due to this mechanism, if you move the position where `fired` checks `period_` in front of calling the user's callback (equivalent to checking first and then executing), the test case would turn red, the mechanism is robust enough to withstand your hands-on changes.

## Conclusion

To be reminded at the right time, this waiting has no task, no heap, only three flags and an embedded waiter, the shape of the callback is fn + `void*`, the same coin as the heavy work in the worker's queue. Our site's trilogy comes to a close here: "The Last Will of the Chip" handles the context after a crash, "Interrupts Only Send Notes" handles who the interrupt hands the heavy work to, and the timer handles waiting without a task. For an application to stand on its own kernel, it needs to be visible, removable, and waitable, all three things are now covered, the next stop we'll continue heading towards the user side.
