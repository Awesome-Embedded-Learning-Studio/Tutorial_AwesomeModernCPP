---
title: "The Sleep-Dead Bug: Lost Wakeup, park_for, and the Builder Chain That Blocks Bad Usage"
description: "6308dd0 wraps up the previous article's hand-rolled task_sleep with a lost-wakeup race condition: in the /tmp copy, I placed the timer at the point between the defense and blocking, watching the task fall asleep; park_for brings the defense and blocking into the same critical section, the type-state builder chain blocks the step skipping from compiling, the friend keyhole takes away the TCB's write permission, placement new becomes the only birth method; after fixing the include path typo according to the answer in 5c62c0d, the rewritten host scheduling test passes all seven targets"
chapter: 3
order: 3
tags:
  - stm32f1
  - advanced
  - embedded
  - register
  - cpp-modern
difficulty: advanced
platform: stm32f1
cpp_standard: [23]
reading_time_minutes: 24
prerequisites:
  - "Who's the Most Urgent, Who Goes First: Ready Bitmap, Task Three States, and Selection Arbitration"
  - "Faking a Birth Certificate at the Stack Top: PendSV, SVC, and the First Switch"
related:
  - "Who Owns the CPU: From Ready Bitmap to Interrupt Wakeup"
current_status:
  title: The Sleep-Dead Bug Has Been Handled
  detail: The ZerOS chapter is being polished
  variant: polishing
  icon: 💤
translation:
  source: documents/vol8-domains/embedded/zeros/03-first-scheduler/03-lost-wakeup-and-typestate.md
  source_hash: 3adde677660820f39c629ccfe59e38c8b061544bd41e436779ff24793e974a12
  translated_at: '2026-11-22T13:35:50+00:00'
  engine: qwen3:14b
  token_count: 23965
---
# The Sleep-Dead Bug: Lost Wakeup, park_for, and the Builder Chain That Blocks Bad Usage

At the end of the previous article, I laid the trap: the hand-rolled `task_sleep` in the example is a three-step separation, setting the callback, setting up the defense, and blocking the task, with no locks between the three steps. If the timer hits the point between the defense and the blocking, the task falls asleep and no one calls it back. In fact, the earlier promise from the arbitration article said that when we discussed `ready`, we mentioned that it only accepts tasks in the Blocked state, and it ignores any tasks that are not sleeping when they come to `ready`. Among the wakeups it blocks, there is a real lost-wakeup race condition. I said then that I would reproduce it in front of it and fix it. Today, we will settle these two debts: we will reproduce the race condition in front of everyone, then fix it, and the possibility of writing errors is also eliminated by the type system.

The commit we are going to break down is `6308dd0`, with the subject "optimized: better sched explanation type", it touches 13 files, adds 418 lines, and deletes 188 lines. You may not rush to think it's a performance optimization. Let's be honest about its actual content: half of it is a rework of type expressiveness, all fields are private, and the birth method is also determined. The other half is the real bug fix: `park_for` brings the defense and blocking into the same critical section, SVC first cut adds `movs r0, #0`, and the host test stub also adds the pair of empty functions we manually added in the previous article. The "type" in the title refers to the main subject, but the title doesn't say everything it does, and we will see it all in person later.

## What the Window Looks Like: Three-Step Separation of task_sleep

Let's bring the crime scene back to the table. The code is the previously posted `task_sleep` in `example/main.cpp` of `0bf9461`:

```cpp
void task_sleep(std::uint32_t ms) {
    auto cur = system_sched.current_task();
    cur->action.OnTime = wake;
    system_time.call_after_span(&cur->action, ms);
    system_sched.block(cur);
}
```

You are already familiar with the division of the three steps. Let's quickly go through them: setting the callback, it attaches the wake-up action to the TimeWaiter embedded in the TCB. `call_after_span` is responsible for the defense, it registers the waiter in the time kernel and sets up the defense for the hardware timer. Finally, `block` marks the task as Blocked and requests a switch. The trouble is that the three steps are independent calls, with no critical section covering them in between, and once the defense is effective, we can't stop the timer from hitting the point.

Let's go through the worst timeline. Right after the defense is done, the task is still in the state that hasn't been blocked by `block`, and SysTick comes in. `on_elapsed` finds the expired waiter, and the callback `wake` runs, calling `ready` to put the task back into the ready queue. However, the beginning of `ready` has a judgment, which we read in the arbitration article:

```cpp
    void ready(base::BorrowedPtr<TCB> t) {
        irq::CriticalGuard guard{driver_};
        if (!t || t->state_ != TaskState::Blocked) {
            return;
        }
        push_back(t.get());
        ...
```

At this moment, the task is still running on the CPU, its state is still Running, and it hasn't yet obtained the Blocked state. `ready` turns away, and this wake-up is silently lost. After the interrupt exits, `task_sleep` continues to execute its third step, `block` marks the task as Blocked, and the task falls asleep. The expired record in the time queue is still hanging, but it has been popped, and no one will ever call `ready` again. The task sleeps until the end of time. You notice the conditions for the lost wake-up, which depend precisely on `ready`'s caution: it doesn't accept tasks that are not sleeping, which is correct, otherwise repeated wake-ups would mess up the queue. The mistake is not in it, but in the window left between the defense and the blocking.

## Reproducing: Let the Timer Fall Exactly in the Window

The window on the board is squeezed out by the hardware's timing, and the probability of hitting it is all up to luck. We can't afford to gamble, but on the host, we can make it deterministic: in the reproduction program, I placed the call to `on_elapsed` between the defense and the blocking, which is equivalent to choosing the moment when SysTick comes in for us, and the interleaving is set up by us.

The reproduction venue still follows the old path of the /tmp copy. We bypass the `.git` in the gitfile format with `git archive`. I exported the tree of `0bf9461` to `/tmp/zeros-t0bf9-repro`, and wrote a `lost_wakeup_repro.cpp`, which manually rolled out the three steps in the same way as the example, only adding one line of `tk.on_elapsed(5)` in the middle of the window. Let me explain the source of the symbols to avoid you getting lost while reading: `tk` is the object of the time kernel `Kernel`, `sched` is the scheduler that wraps the proxy port, and `kWake` corresponds to the `wake` callback in the example:

```cpp
    sched.add(&tcb);
    assert(sched.pick_next().get() == &tcb);

    // the hand-rolled task_sleep of the 0bf9461 example, three separate steps
    auto* cur = sched.current_task().get();
    cur->action.OnTime = kWake;          // step 1: set the reaction
    tk.call_after_span(&cur->action, 5); // step 2: arm the timer
    // ---- the window: on the board SysTick fires right here ----
    std::printf("-- tick 5 fires inside the window --\n");
    tk.on_elapsed(5);                    // wake runs: task is still Running
    std::printf("task state after the dropped wake: %d (Blocked=%d, Running=%d)\n",
                static_cast<int>(tcb.state_), static_cast<int>(TaskState::Blocked),
                static_cast<int>(TaskState::Running));
    // ---- end of window ----
    sched.block(cur);                    // step 3: block, now it sleeps

    // wait far past the deadline: nobody will ever call ready again
    for (int i = 0; i < 1000; ++i) tk.on_elapsed(1);
```

The `wake` callback is still the same skeleton as in the example. It uses `offsetof` to backtrack from the waiter to the TCB and calls `ready`. We discussed its trick in the previous article, so we won't repeat it here. After the task falls asleep, I let it idle for a thousand ticks, a number far exceeding the five-tick deadline. I want to see if anyone calls it. The compilation and running commands are two lines, and I compiled it directly with my local GCC 16.2.1:

```shell
g++ -std=c++23 -Iinclude lost_wakeup_repro.cpp -o /tmp/lost_wakeup_repro
/tmp/lost_wakeup_repro
```

The actual output is as follows, please read it carefully:

```text
-- tick 5 fires inside the window --
wake fired: ready() called
task state after the dropped wake: 1 (Blocked=2, Running=1)
after 1000 more ticks: state=2 (0=Ready 1=Running 2=Blocked)
LOST WAKEUP REPRODUCED: task sleeps forever
```

Let's read it line by line: the wake-up did run, `ready` was indeed called, but the task's state is 1, which corresponds to Running. The wake-up was blocked at the state check. After the blocking is executed, the task sleeps into Blocked, and the output is 2. After a thousand ticks, the state remains unchanged, and it sleeps forever. If you run the same program on your machine, the window is where the sleep-dead happens, which is the real defect left in the example by `0bf9461`, and `6308dd0` fixes it.

## park_for: Bringing the Defense and Blocking Under the Same Lock

The idea of the fix can be summed up in one sentence: there is no window between the defense and the blocking. `6308dd0` added a `park_for` to the time kernel. Let's look at `include/ZerOS/kernel/clock/kernel.hpp`:

```cpp
    // Parking: arm AND block inside ONE guard. A wake slipping in between
    // the two steps would find the task un-blocked, drop it, and the task
    // sleeps forever — the order here IS the fix
    template <typename ParkBlock>
    void park_for(base::BorrowedPtr<TimeWaiter> task, Ticks::tick_t span,
                  TimeWaiter::OnTimeAction wake, ParkBlock&& block_now) {
        irq::CriticalGuard guard{lowlevel_driver_};
        enqueue_locked(task, span, wake);
        block_now(); // still under the very same lock
    }
```

The comments write the entire race condition into the code, even the outcome, `sleeps forever`. Let's look at its structure. `park_for` takes four things: the waiter, the deadline, the wake-up callback, and a callable `block_now`, which encapsulates the action of "blocking the task now." The two steps in the function body are covered by the same `CriticalGuard`. The defense is handed over to `enqueue_locked`, and the blocking is handed over to `block_now()`. The comment's `still under the very same lock` refers to the state that the lock is still in hand. Why does the window disappear? Let's think from the perspective of the interrupt: when the critical section is open, the interrupt cannot enter, and SysTick cannot fall between the defense and the blocking. When the critical section is released, both steps have already been completed. The task is either already asleep and the wake-up is normally registered, or it hasn't been defended yet, and the interrupt has no chance to intervene. "The order here IS the fix" can be read as: the fix relies on the order of the two steps being covered by the same lock. The defense and blocking no longer expose themselves individually, and the window naturally closes.

Let's look at the next function, `enqueue_locked` is a private function extracted from the original `call_after_span`. It sets the callback, writes the deadline, inserts the waiter into the queue, and updates the alarm. The original set of functions is intact, only renamed, and clearly marked that the caller must hold the lock:

```cpp
    // caller must hold the guard; wake is optional (nullptr keeps the born-with reaction)
    void enqueue_locked(base::BorrowedPtr<TimeWaiter> task, Ticks::tick_t span,
                        TimeWaiter::OnTimeAction wake) {
        if (wake != nullptr) {
            task->OnTime = wake;
        }

        // set the deadline
        task->deadline_ = clock_.current() + span;
        queue_.insert(task);
        update_alarm();
    }
```

The scheduler has a member function that ties the whole thing together. Let's look at `include/ZerOS/kernel/sched/scheduler.hpp`:

```cpp
    // Park the task on the clock: arm and block share ONE guard, so a
    // too-early wake always finds us already blocked — the wake then
    // lands, it never gets dropped. Hand-rolling this from call_after_span
    // + block reopens the gap (arm, wake, block, sleep forever).
    template <clock::TimeKeeper Time>
    void sleep_for(base::BorrowedPtr<TCB> t, Time& time, clock::Ticks::tick_t span,
                   clock::TimeWaiter::OnTimeAction wake) {
        time.park_for(&t->action, span, wake, [this, t] { block(t); });
    }
```

The comment `Hand-rolling this ... reopens the gap` is a warning to future readers: whoever bypasses it and manually puts together `call_after_span` and `block`, the window reopens. The comment lists the death order `arm, wake, block, sleep forever` in order. The reproduction in the main text follows the same path. The lambda captures `this` and the task pointer, `park_for` calls it after the defense, and the blocking happens while the lock is still held. The end of the call chain is in the arch layer, `src/arch/arm_cortex_m3/switch.cpp` adds a free function `sleep_for`, which connects the two singletons together:

```cpp
namespace {
void wake_from_sleep(base::BorrowedPtr<clock::TimeWaiter> waiter) {
    system_sched.ready(sched::TCB::owner_of(waiter));
}
} // namespace

void sleep_for(std::uint32_t ms) {
    system_sched.sleep_for(system_sched.current_task(), system_time, ms, wake_from_sleep);
}
```

`wake_from_sleep` is the official version of the `wake` in the example, and `owner_of` has also been upgraded to a static member of the TCB itself. We'll revisit it in the keyhole section. The entire three-step hand-rolled segment in the example is deleted, and the task body only has one line `sleep_for(1000)`.

::: warning
`park_for` blocks the window between the defense and the blocking, but it does not change the semantics of the wake-up: the wake-up callback is still `ready`, and the wake-up at the same level still only queues without requesting a switch, the deadline still adds up from the current time, and the source of jitter is consistent with the previous station. If you compare it with FreeRTOS's `vTaskDelay`, the same point is the composite atomicity, and the different point is that there is no agreement here on aligning with absolute time.
:::

The repository itself provides a regression test for this fix. We can find a test case in `test/test_sched.cpp` that specifically targets zero-deadline sleep:

```cpp
TEST_CASE("a zero-span sleep still wakes: no gap between arm and block", "[sched][time]") {
    // regression: hand-rolled sleep (arm, then block) loses a wake that
    // fires in between and the task sleeps forever. The compound must
    // not have that gap, not even for span 0.
```

The comment is honest: the hand-rolled version will crash even for a span of 0, and the composite version does not allow any window even for a span of 0. Zero deadline is the most extreme case, the shorter the deadline, the closer the deadline and the blocking are, the more likely the hand-rolled version is to crash, and the composite version is indifferent because there is no gap between the two steps that can be inserted. We will run this test case for it in the verification section.

## Builder Chain: Skipping a Step Won't Compile

After fixing the race condition, `6308dd0` turned around to deal with another more hidden issue. We also follow it to see: the fields of TCB can be written by anyone. In the code of the previous station, the example assigned values to `tcb_a.task_priority_` and `tcb_a.name_` one by one, and the fields were all public. If the order was wrong or a field was missed, the compiler was silent, and the crash point was pushed to runtime. `6308dd0`'s answer has three layers: the outermost layer is a type-state builder chain, the middle layer is the birth ceremony of placement new, and the innermost layer is the friend keyhole that takes away the write permission of the fields. We look at them layer by layer. The outermost layer is the new `include/ZerOS/kernel/sched/task.hpp`, which is a type-state builder chain. The type-state approach, in plain language, is a way of letting the type carry the progress on its back: every step filled, the function returns a new type, and the next step only recognizes that new type. If a step is skipped, there is no type to receive it, and the compiler stops immediately. Let's look at this file:

```cpp
#pragma once

// The task factory: the chain IS the contract. Skip a step and the rest
// of the chain simply does not exist — no comment can save you then.
//   named("blink").prio(1).stack(stack).entry(fn, arg).spawn_into(box)

#include "ZerOS/kernel/sched/task_control_block.hpp"
#include "ZerOS/kernel/sched/types.hpp"

#include <concepts>
#include <cstdint>
#include <span>

namespace ZerOS::task {

using sched::TaskPriority_t;

// stages: each struct is what the chain knows so far, nothing more
struct Named {
    const char* name;
};
struct Priorized {
    const char* name;
    TaskPriority_t prio;
};
struct Stacked {
    const char* name;
    TaskPriority_t prio;
    std::span<std::uint32_t> stack;
};
```

The comment `the chain IS the contract` is the declaration of the entire design: the chain itself is the contract, and the comment cannot save the person who skips the step. Let's look at the three-stage structures, which compile the fact that the chain knows what it knows so far into the type's body: Named has only a name, Priorized adds priority, and Stacked adds the stack. Every step the chain moves forward, the type gains a member, and the type becomes our progress bar.

The transfer functions are the real checkpoints. Let's look at the body of `TaskBuilder`:

```cpp
template <typename Stage>
struct TaskBuilder {
    Stage stage;

    explicit TaskBuilder(Stage filled) : stage(filled) {}

    // one road per stage: asking for a step from the wrong spot is a
    // compile error, exactly the point of this whole dance
    TaskBuilder<Priorized> prio(TaskPriority_t value) && requires std::same_as<Stage, Named> {
        return TaskBuilder<Priorized>{Priorized{stage.name, value}};
    }

    TaskBuilder<Stacked> stack(std::span<std::uint32_t> body) && requires std::same_as<Stage, Priorized> {
        return TaskBuilder<Stacked>{Stacked{stage.name, stage.prio, body}};
    }

    // entry carries its argument with it: "an arg without an entry"
    // is not a state the chain can be in
    sched::TCBCreator entry(sched::UserTaskWrapper::UserTask function, void* argument) &&
        requires std::same_as<Stage, Stacked> {
        return sched::TCBCreator{
            .name = stage.name,
            .prio = stage.prio,
            .stack = stage.stack,
            .run = {function, argument},
        };
    }
};

// the road starts here
[[nodiscard]] inline TaskBuilder<Named> named(const char* name) {
    return TaskBuilder<Named>{Named{name}};
}

} // namespace ZerOS::task
```

Each transfer function has two gates. Let's explain the logic of the gates. The `&&` qualifier requires the caller to take a right value, and the chain can only proceed along the path of the return value. Left values stored in intermediate variables cannot call them. Constraints such as `requires std::same_as<Stage, Named>` write the fact that a function is only allowed to start from a certain stage into the compile-time. `prio` only recognizes Named, `stack` only recognizes Priorized, and `entry` only recognizes Stacked. Skipping steps results in a hard compile error, and the comment `an arg without an entry is not a state the chain can be in` explains why `entry` takes the parameter together: the function and the parameter are a pair, and if they are split, the chain is in a state that should not exist.

What the error looks like, I have compiled a real small experiment, `named("a")` after directly pointing to `stack`, skipping `prio`:

```cpp
int main() {
    alignas(8) static std::uint32_t stack[64] = {};
    static ZerOS::sched::TCBStorage storage{};
    // skips prio(): straight from named() to stack()
    auto& t = ZerOS::task::named("a").stack(stack).entry(+[](void*){}, nullptr).spawn_into(storage);
    (void)t;
    return 0;
}
```

The original error message from GCC 16.2.1 is as follows, let's read it together:

```text
/tmp/skip_step.cpp:8:44: error: no matching function for call to 'ZerOS::task::TaskBuilder<ZerOS::task::Named>::stack(uint32_t [64])'
    8 |     auto& t = ZerOS::task::named("a").stack(stack).entry(+[](void*){}, nullptr).spawn_into(storage);
      |               ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~^~~~~~~
  • there is 1 candidate
In file included from /tmp/skip_step.cpp:2:
  • candidate 1: 'ZerOS::task::TaskBuilder<ZerOS::task::Stacked> ZerOS::task::TaskBuilder<Stage>::stack(std::span<unsigned int>) && requires  same_as<Stage, ZerOS::task::Priorized> [with Stage = ZerOS::task::Named]'
      include/ZerOS/kernel/sched/task.hpp:44:26:
```

The error explains the reason itself: the only candidate has the constraint `same_as<Stage, Priorized>`, and the Stage you are currently holding is exactly the shape of the Named type. Skipping steps is no longer a deep runtime bug, it is a first-eye-seeable compile error, which is exactly what "letting bad usage not compile" means.

The legal way is in `example/main.cpp`, let's look at the birth section after the revision:

```cpp
// zero storage, meaning comes only from the factory chain
constinit ZerOS::sched::TCBStorage tcb_a{};
constinit ZerOS::sched::TCBStorage tcb_b{};

    auto& a =
        ZerOS::task::named("a").prio(1).stack(stack_a).entry(task_a, nullptr).spawn_into(tcb_a);
    auto& b =
        ZerOS::task::named("b").prio(1).stack(stack_b).entry(task_b, nullptr).spawn_into(tcb_b);
```

The previous station's method of assigning each field individually is gone, and now each line reads like the resume of the task: what's the name, how urgent, where is the stack, and what to do. `TCBStorage` is a new type, it is the stage of the birth ceremony, let's look at it.

## Birth Ceremony: Placement new and Four Checks

The stage of the birth ceremony `TCBStorage`, its definition and guards are all in `task_control_block.hpp`. Birth must have a place to land, and the old way of declaring a TCB and filling in the fields one by one leaves room for half-finished states: the name is there, but the stack is not yet complete. If the scheduler takes such a half-finished product, an accident is buried. The answer from the repository is to separate the birth location and the birth procedures. The location is a piece of meaningless raw bytes, and the procedures are handled by the factory. The half-finished product does not exist in the design. Let's look together:

```cpp
// Meaning-free RAM: zero it, park it. The only pen that writes meaning
// into it is TCBCreator::spawn_into, so a half-built task is not a thing.
struct TCBStorage {
    alignas(alignof(TCB)) std::byte bytes[sizeof(TCB)];
};
```

`TCBStorage` is a piece of raw bytes. Let's look at the comment, it's very straightforward: `meaning-free RAM`, zeroed out and left as is. The bytes themselves have no meaning, the meaning must be written by a qualified pen, and the pen that writes the meaning is the following `TCBCreator`:

```cpp
// The factory's final word: every field is in, now it may write a TCB.
// The chain fills it, spawn_into spends it.
class TCBCreator {
  public:
    // The one legal pen: fresh zero bits + meaning, hands back the handle
    TCB& spawn_into(TCBStorage& storage) const {
        auto& tcb = *new (storage.bytes) TCB{}; // private birth, but we hold the pen
        tcb.name_ = name;
        tcb.task_priority_ = prio;
        tcb.task_wrapper_ = run;
        tcb.stack_view_ = stack;
        return tcb;
    }

    // filled by the builder chain, nothing to hide in a receipt
    const char* name{};
    TaskPriority_t prio{};
    std::span<std::uint32_t> stack{};
    UserTaskWrapper run{};
};
```

That line in `spawn_into`, `new (storage.bytes) TCB{}` is placement new (the new usage that constructs an object at a given address without allocating memory), its role in this set of code is the birth ceremony: the object is formally constructed, not pieced together by assignment. Why must we do it this way? Because the fields of TCB are about to be fully private, and TCB has embedded members and alignment requirements. We store raw bytes and construct them on site when needed, the alignment is handled by `alignas(alignof(TCB))`, and the birth is handled by the constructor, both legs are complete.

At the end of the file, there is a group of `static_assert`, let's look at them all:

```cpp
static_assert(std::is_trivially_copyable_v<TCB>);
static_assert(std::is_trivially_destructible_v<TCB>);
static_assert(sizeof(TCBStorage) == sizeof(TCB));
static_assert(alignof(TCBStorage) == alignof(TCB));
```

Let's look at what each one is checking. The first two correspond to the reality of PendSV: the assembly for switching takes the TCB in the original bytes, the saving and restoring of the stack pointer never goes through the destructor, so the TCB must be trivially copyable and trivially destructible. If either of these is broken, the premise of the assembly path is also broken. The last two check that the size of the storage and the body are aligned, `spawn_into` constructs into the bytes, and no misalignment is allowed. It's interesting that the adjacent comment in the repository admits half a truth: the default construction of TCB is not trivial (libstdc++'s span default construction initializes its own members), but it doesn't matter, because the birth only goes through `spawn_into`'s placement new, and the zero bytes themselves are never used as TCB. Behind each assertion is a piece of assembly that would explode, or a misaligned construction.

## Friend Keyhole: Write Permission Reclaimed by Three Keys

Now let's look at the encapsulation itself, TCB has changed from a public structure to a locked house:

```cpp
class TCB : public base::SelfNode<TCB> {
  public:
    using StackPointer_t = std::uint32_t*;

    // Wait... who owns this waiter? The node sleeps inside its owner,
    // waking walks the road back from node to TCB
    [[nodiscard]] static TCB* owner_of(base::BorrowedPtr<clock::TimeWaiter> waiter) {
        #pragma GCC diagnostic push
        #pragma GCC diagnostic ignored "-Winvalid-offsetof"
        return reinterpret_cast<TCB*>(
            reinterpret_cast<std::byte*>(waiter.get()) - offsetof(TCB, action));
        #pragma GCC diagnostic pop
    }

  private:
    // Locked fields: walkers talk to the scheduler or the creator chain.
    // Naked mechanics (the PendSV road, whitebox tests) pass through TCBKeys.
    // A comment contract cannot stop anyone, the type system can.
    template <typename> friend struct Scheduler;
    friend class TCBCreator;
    friend struct zeros_impl::TCBKeys;

    // Birth stays public (the triviality contract needs it) but a born TCB
    // holds zero bits and no pen: only the three names above can write it.
    // Everyone else gets a TCBStorage and hands it to the factory chain.

    StackPointer_t stack_pointer_{}; // always be the first
    TaskPriority_t task_priority_{};
    TaskState state_{};

    const char* name_{}; // Constant name's for debugings

    clock::TimeWaiter action{};
    UserTaskWrapper task_wrapper_{};
    std::span<std::uint32_t> stack_view_{};
};
```

The comment `A comment contract cannot stop anyone, the type system can` is the sentence I most want you to take away from this article: comments cannot stop anyone, but the type system can. All fields are private, and the write permission is only given to three friends. Let's go through the division of the three keys: `Scheduler` template manages the runtime state transition, `TCBCreator` manages the birth, and `zeros_impl::TCBKeys` manages the raw mechanism. `owner_of` has been upgraded from the black magic in the example to a public static member, since it's read-only, it's convenient to put it in the open for everyone to use, and the person who writes the `offsetof` trick no longer needs to `reinterpret_cast` themselves.

`TCBKeys` is the keyhole for the raw mechanism, let's look at its preview in the namespace:

```cpp
namespace zeros_impl {
struct TCBKeys; // the keyhole, declared early so the lock knows its guard
}
```

The entire body of `TCBKeys` is as follows, let's continue:

```cpp
namespace zeros_impl {
// The keyhole for naked mechanics: the PendSV path and the whitebox tests.
// If you are neither, you are just walking by, please use the scheduler.
struct TCBKeys {
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Winvalid-offsetof"
    static_assert(offsetof(TCB, stack_pointer_) == sizeof(base::SelfNode<TCB>),
                  "stack_pointer_ must stay immediately after the SelfNode base");
    #pragma GCC diagnostic pop

    static TCB::StackPointer_t& sp(TCB& t) { return t.stack_pointer_; }
    static UserTaskWrapper& wrapper(TCB& t) { return t.task_wrapper_; }
    static std::span<std::uint32_t>& stack_view(TCB& t) { return t.stack_view_; }
    static TaskPriority_t prio(TCB& t) { return t.task_priority_; }
    static TaskState state(TCB& t) { return t.state_; }
    static void set_state(TCB& t, TaskState s) { t.state_ = s; }
    static const char* name(TCB& t) { return t.name_; }
};
} // namespace zeros_impl
```

The comment clearly writes the access: the path of PendSV and the white-box testing, everyone else is considered as passing by, and the passers-by are asked to take the regular path of the scheduler. The `static_assert` that guards the stack pointer layout from the arbitration article has been moved into the keyhole, and the position of the gate is placed with the person who uses the gate. The original raw field access in `switch.cpp` has all been changed to the shape of the keyhole. Let's compare the changes in `fabricate_frame` and `context_switch`:

```cpp
void fabricate_frame(ZerOS::sched::TCB& t) {
    auto view = sched::zeros_impl::TCBKeys::stack_view(t);
    auto* top = view.data() + view.size();
    auto* hw = reinterpret_cast<ExceptionFrame*>(top) - 1;
    auto* sw = reinterpret_cast<SoftwareFrame*>(hw) - 1;
    ...
    TCBKeys::sp(t) = reinterpret_cast<std::uint32_t*>(sw);
}

extern "C" std::uint32_t* context_switch(std::uint32_t* saved_sp) {
    if (saved_sp != nullptr) {
        TCBKeys::sp(*system_sched.current_task()) = saved_sp;
    }
    return TCBKeys::sp(*system_sched.pick_next());
}
```

The assembly path still works on raw bytes, and the C++ encapsulation has not slowed it down by a finger, and the application code can no longer touch `stack_pointer_`. The boundary of encapsulation and the boundary of performance each have their own place, which is a rare two-way writing method in embedded C++. When we write our own kernel in the future, it's worth learning from.

The privatization of the fields has also extended to both ends, let's look at both ends. One end is `SelfNode`'s `next_`, which has also been privatized in `include/ZerOS/base/self_list.hpp`, and the comment is quite fierce: `a stray hand wiring nodes behind our back is how ready queues die quietly`, whoever wires the links behind our back, the ready queue dies quietly. The right to move the chain is reclaimed by `SelfList` itself, even the test code has to be changed, the white-box peeking `CHECK(a.next_ == nullptr)` in `test_self_list.cpp` has been deleted, replaced with the comment `the drain below proves re-insertion lands it back in order`, the issue of whether the linked list is normal is now proven by the assertion of behavior, and we no longer peek into its insides. The other end is the privatization of `TimeWaiter`, its `deadline_` and `OnTime` are also hidden, and the friend is only given to `Kernel` and `TimerQueue`, leaving only a read-only `deadline()` accessor outside, and the parallelism of the comment is written into the code: `reading is free for diagnosis, writing is not`.

The presentation of compile-time constraints has also changed in this commit, let's see what it has changed to. The original writing put the concept on the template parameter, `template <Switchable Driver> struct Scheduler`, and when the constraints are not met, the error is a whole page of concept derivation stack. `6308dd0` has taken the concept on the template parameter and changed it to a `static_assert` inside the class:

```cpp
template <typename Driver> struct Scheduler {
    // the picker only rides a Switchable driver
    static_assert(Switchable<Driver>, "Scheduler wants a Switchable driver");
```

The time kernel is the same approach, let's compare the two places: `Kernel`'s parameter has changed from `TimePortable Driver` to `static_assert(TimePortable<Driver>, "Kernel wants a TimePortable driver")`. The constraints are still the same constraints, but the error has changed, one line of human language has replaced a page of stack, and the explanation power of the API itself has also become an object of design. The accompanying `TimeKeeper` concept has solidified the ability of the time kernel to promise the scheduler: `park_for` and `current`, missing either one, `Scheduler`'s `sleep_for` will not compile.

The foreshadowing is also fulfilled here. In the previous article, we admitted that no one on the first cut path of SVC clears r0, `context_switch` takes the residual value as the stack pointer, and it didn't explode by luck. `6308dd0`'s `SVC_Handler` adds a line:

```cpp
extern "C" [[gnu::naked]] void SVC_Handler() {
    asm volatile("cpsid   i\n"
                 "movs    r0, #0\n" // first switch ever: nobody is running, nobody to save
                 "bl      context_switch\n"
                 "ldmia   r0!, {r4-r11}\n"
                 "msr     psp, r0\n"
                 "ldr     lr, =0xFFFFFFFD\n"
                 "cpsie   i\n"
                 "bx      lr\n");
}
```

`movs r0, #0` turns the fact that no one needs to save into an explicit `nullptr`, and the comment `nobody is running, nobody to save` says exactly this. In the previous article, we looked at the vulnerability and sighed, today we see it filled in by one instruction, and the first cut path has no undefined behavior from now on.

## Confession: The Rewritten Test Won't Compile Again

As usual, there is a part that needs to be honestly reported. `6308dd0` has rewritten `test_sched.cpp`, with a 232-line diff, and the test tasks have changed to use the same factory path as the application. Let's look at the Rig in the test, which is the shared test fixture for these cases:

```cpp
// tasks are built the legal way: the same factory road the app walks
struct Rig {
    ZerOS::sched::TCBStorage storage{};
    TCB& tcb;

    explicit Rig(TaskPriority_t prio, const char* name)
        : tcb(named(name).prio(prio).stack(std::span<std::uint32_t>{})
                          .entry(kNoopEntry, nullptr)
                          .spawn_into(storage)) {}
};
```

After all the fields are privatized, the test takes the state and changes the state through `TCBKeys`, and the comment at the top of the file says `whitebox key, tests only`. The `HostSwitchPort` that couldn't compile in the arbitration article this time comes with `lock` and `unlock`, and we don't need to add them anymore.

However, I checked out to `6308dd0`, set up the host build with the same three commands as in the previous station, but the build stopped halfway:

```text
/tmp/zeros-t6308/test/test_sched.cpp:8:10: fatal error: ZerOS/task.hpp: No such file or directory
    8 | #include "ZerOS/task.hpp"
      |          ^~~~~~~~~~~~~~~~
compilation terminated.
make[3]: *** [test/CMakeFiles/test_sched.dir/build.make:79: test/CMakeFiles/test_sched.dir/test_sched.cpp.o] Error 1
```

The reason is obvious when you match it with the source code: the new test includes `ZerOS/task.hpp` on line 8, but we can't find this file in the repository at all, and the builder chain is in `include/ZerOS/kernel/sched/task.hpp`. I went through the history of the submission, and this path has never appeared with any header file. The include was written wrong, and it wasn't fixed until two commits later in `5c62c0d`, and the official answer is just one line:

```diff
-#include "ZerOS/task.hpp"
+#include "ZerOS/kernel/sched/task.hpp"
```

It's almost the same story as the arbitration article: the commit message says optimized, the type system, the race condition fix, and the birth ceremony are all there, but the rewritten test itself won't compile, and most of the verification is done on the board. The green light for the host is temporarily missing in the repository's history. If we want to go through it ourselves, we change this line according to the answer in `5c62c0d`, rebuild, and run all the tests:

```shell
cmake -B build-host -DZEROS_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

On my machine, it's GCC 16.2.1 plus CMake 4.4.4, and all seven test targets passed:

```text
100% tests passed out of 7

Total Test time (real) =   0.12 sec
```

The test results for the scheduler are out, and we look at them after the rewrite: it gives ten test cases and forty-five assertions, one more regression test case for zero-deadline sleep than the nine test cases and forty-two assertions in the arbitration article, and the original sleep test cases have also been rewritten to use the factory path:

```text
All tests passed (45 assertions in 10 test cases)
```

## Acceptance

Let's gather the verifiable facts and sort them out, totaling three, and I will present them to you one by one.

The first is the reproduction of the sleep-dead bug. You export `0bf9461` with `git archive` to `/tmp`, write the reproduction program as described in the main text, and place `on_elapsed` between the defense and the blocking. You will see that the wake-up does run, the state is indeed still Running, and after a thousand ticks, it is still Blocked, and the four-line output is exactly the same as what I posted. If you move the line `tk.on_elapsed(5)` in the window to after `block`, the program will print the word `task woke up`, and the same defect is hidden with a different timing, which is exactly why it is hard to catch.

The second is the type checkpoint. The original error message of the skip test is in the main text, the line `same_as<Stage, Priorized>` is the restriction that the compiler reads for us. I also left the probe test for private members for you: you write `t.action.OnTime = nullptr` in the header file of `6308dd0`, and GCC gives you the error `'...TCB::action' is private within this context`, except for the three key holders, no one can write a single byte. Changing back to the legal way of the factory chain, the task is born normally, and I ran `named("a").prio(1).stack(...).entry(...).spawn_into(storage)` through the entire chain on the host.

The third is the full pass after the fix. After the include is corrected, `ctest` passes all seven items, and the scheduler test gives ten test cases and forty-five assertions. Among them, the regression test case for zero-deadline sleep is the official witness of the lost-wakeup fix, and it passes. The composite sleep window in the repository's acceptance is closed. I will also be honest about the parts I haven't verified: the printing rhythm of dual tasks in Renode, we haven't rerun it in this article, and according to its own description, `6308dd0` has made no changes to the behavioral semantics, the pattern of the board's interleaving should be consistent with the previous article according to our judgment, but our judgment doesn't count, if you want to be strict, just run the command from the previous article once.

## Next Article

The type shell is complete, but the scheduler still owes the application a face. When the task wants to sleep, it still has to go through the free function of the arch layer, and there is no proper entry to block. The next article talks about balancing the priority of interrupts, and then let the interrupt knock on the task's door. It gives the application a public face of "the current task", and sequentially positions `ThisTask::sleep_for`, `yield`, and `block`, then connects the first real interrupt wake-up chain. USART1 receives a byte, and the sleeping task is called back. Along the way, there is also a hard lesson waiting for us: the NVIC priority value must be greater than the BASEPRI of the critical section, the default 0 will tear open all critical sections, and we will change 0x80 back to 0 on site, and watch the queue being torn apart. We'll see you there.
