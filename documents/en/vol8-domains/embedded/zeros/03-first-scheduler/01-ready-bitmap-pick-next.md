---
title: "Who's the Most Urgent, Who Steps Up: Ready Bitmaps, Task Three States, and Selection Arbitration"
description: "We reproduce the real compile error of commit 0bf9461 on the host: the commit message says 'Sched OK', but the test stub is missing lock/unlock, and the Switchable concept immediately triggers a static_assert failure; we manually add two empty functions according to the concept, and ctest turns all green. The main body writes about the arbitration surface: TaskState's three states, 32-level ready bitmaps with FIFO per level, asymmetric enqueueing of pick_next and yield (preempted tasks go back to the front of the queue, yielding tasks go to the end), and RBIT+CLZ single-cycle selection. The selection mechanism is directly confronted in objdump disassembly."
chapter: 3
order: 1
tags:
  - stm32f1
  - advanced
  - embedded
  - registers
  - cpp-modern
difficulty: advanced
platform: stm32f1
cpp_standard: [23]
reading_time_minutes: 22
prerequisites:
  - "The Definition of a Second: Tick, Wrap Arithmetic, and Expiry Queue"
related:
  - "Faking a Birth Certificate at the Stack Top: PendSV, SVC, and the First Context Switch"
translation:
  source: documents/vol8-domains/embedded/zeros/03-first-scheduler/01-ready-bitmap-pick-next.md
  source_hash: 2542663ae9803b5e7e416813d3e13bb0caa24ff9a34805985118889e74831ed2
  translated_at: '2026-11-22T13:32:54+00:00'
  engine: qwen3:14b
  token_count: 14092
---
# Scheduler is Here! Ready Bitmaps, Task Three States, and Selection Arbitration

When we ended the previous stop, the internal kernel was already running on both the host and the board, and Renode's ticker printed a line every second. However, if you recall the previous `main`, you will find a fact: when the period is up, the callback function is executed directly in the SysTick interrupt context, the program has no concept of tasks, and the CPU prints and returns to `main` after finishing. The answer to who is using the CPU is not found in the code we had at that time.

## Three States of a Task and Its File Card

The scheduler has arrived, and the question it needs to answer is always the same: who will get the CPU next. To answer reliably, we need to clearly state the current situation of each task: whether it is queued and waiting to take the stage, whether it is currently using the CPU to do work, or whether it is waiting for an event that has not occurred.

The simplest way to record this is with a `bool`, where `true` means the task is using the CPU and `false` means it is not. However, `false` cannot distinguish between two types of tasks that are not running: one is ready but not yet scheduled, and the other is waiting for an event to occur. The scheduler handles these two groups differently, so a single `bool` cannot accommodate them, and thus we have three states. Our new `types.hpp` has only 11 lines, and its responsibility is to define the vocabulary for the task states:

```cpp
#pragma once
#include <cstdint>

namespace ZerOS::sched {
/**
 * @brief State is the Task State
 *
 */
enum class TaskState : std::uint8_t { Ready, Running, Blocked };
using TaskPriority_t = std::uint8_t;
} // namespace ZerOS::sched
```

Here we list three states, and we will explain each one in detail. `Ready` means the task is fully prepared and only needs an available CPU. `Running` means it is currently running on the CPU. `Blocked` means it is waiting for an event that has not occurred, such as waiting for a time to elapse or for a byte to arrive, and even if the CPU is idle, it is not assigned to it. `TaskPriority_t` uses `std::uint8_t`, a one-byte priority number, where smaller numbers indicate higher urgency. We must remember this convention, as all subsequent judgments are based on it.

Every decision the scheduler makes must be able to find its basis, including who the task is, how urgent it is, and which of the three states it is currently in. The most straightforward approach is to assign a set of global variables to each piece of information. However, when the number of tasks increases, the names of the variables become different, and the selection code must recognize each one. The answer in the repository is the file card: all the personal information of a task is stored in the same structure, and the scheduler holds its address. Everything about the task is visible to the scheduler. To determine what the state is attached to, we look at the TCB (Task Control Block, the task's file card within the kernel, which we will refer to as TCB) in `task_control_block.hpp`:

```cpp
struct TCB : public base::SelfNode<TCB> {
    using StackPointer_t = std::uint32_t*;
    StackPointer_t stack_pointer_{}; // always be the first

    TaskPriority_t task_priority_{};
    TaskState state_{};

    const char* name_{}; // Constant name's for debugings

    clock::TimeWaiter action{};
    UserTaskWrapper task_wrapper_{};
    std::span<std::uint32_t> stack_view_{};
};

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
static_assert(offsetof(TCB, stack_pointer_) == sizeof(base::SelfNode<TCB>),
              "stack_pointer_ must stay immediately after the SelfNode base");
#pragma GCC diagnostic pop
```

We go through the members from top to bottom, each with its purpose. `stack_pointer_` is the task's stack pointer, and the comment shouts out the slogan "always be the first". The `static_assert` below fixes its position at the beginning of the TCB: it must immediately follow the `SelfNode` base class, and `SelfNode` is the `next_` pointer that is carried on the task's back. At this point, someone might ask, what is the meaning of this `static_assert` operation?

It is because the assembly code for PendSV context switching needs to access the stack pointer at a fixed offset, and if the layout is wrong, the assembly will not report an error, and the task will die in a mysterious way. Therefore, we let the compiler guard the layout for us. If someone changes the order of the fields, the firmware will not even compile.

`task_priority_` and `state_` are the implementations of the two types above. `name_` is a constant string for debugging. `action` is a `TimeWaiter` type, which we saw in the previous stop. When a task needs to sleep, we embed its waiter into the expiry queue of the time kernel, and the callback will wake the task when the time is up. In terms of memory layout, it is attached to the TCB itself, so backtracking from the expiry record to the task is just a pointer subtraction, and no additional mapping table is needed. We will see this backtracking technique during verification.

`task_wrapper_` contains the task function and parameters, and `stack_view_` is the `span` view of the task's stack. `UserTaskWrapper` has a static function worth your attention:

```cpp
struct UserTaskWrapper {
    using UserTask = void (*)(void*);
    UserTask function;
    void* argument;

    // When Marking Static, it is free, plain ABI
    static void InvokeUserTask(UserTaskWrapper& wrapper) {
        if (wrapper.function) {
            wrapper.function(wrapper.argument);
        }
    }
};
```

The comment explains the intention: it is deliberately made a static member, using the regular ABI, without a hidden `this` parameter, so that the assembly for exception returns can directly jump into it as a bridge. How to build the bridge and how to forge the exception frame are the main dishes of the next article, and we won't take the stage today.

## The Contract for the Scheduler: Two Concepts

The official name of the scheduler is `Scheduler<Driver>`, where the `Driver` in the angle brackets is the thing it relies on. Tasks such as disabling interrupts and requesting context switches must be done through its hand. When the reliance fails, the answer in the repository is C++'s concept: a list of requirements written in plain language, and the compiler checks them for us one by one. If the driver is not compatible, it won't even compile. The explosion is blocked at compile time, saving trial and error on the board. It has two requirements for the driver it receives, and we can see them in `is_scheduler.hpp`:

```cpp
template <typename SwitchPort>
concept Switchable =
    irq::CriticalSection<SwitchPort> && requires(SwitchPort& p) { p.request_switch(); };

template <typename Stuff>
concept IsScheduler = requires(Stuff& stuff, base::BorrowedPtr<TCB> aTask) {
    // A Scheduler can add a task
    { stuff.add(aTask) };
    // A Scheduler can tell current task is what
    { stuff.current_task() } -> std::same_as<base::BorrowedPtr<TCB>>;
    // A Scheduler can tell what task should be executed next
    { stuff.pick_next() } -> std::same_as<base::BorrowedPtr<TCB>>;
    { stuff.ready(aTask) }; // Wake up task
    { stuff.block(aTask) }; // Block the task
    { stuff.yield() };      // Yield or hang a task
};
```

`Switchable` is a composite requirement: we require the driver to call `lock` and `unlock` (it combines the `CriticalSection` concept we discussed in the previous stop), and it must be able to make a `request_switch` request, which is a task switch request. By combining the concept from the previous stop without redeclaring it, the contract has no second copy, and the lock recognized by the time kernel and the lock recognized by the scheduler are always the same. `IsScheduler` is more like a document that can be checked by `static_assert`: it lists five actions and two return types, and the comments write them one by one. Reading the concept is like reading an interface description. These two are the guards for the commit's test, and we will see how they block the compilation in the confession section.

## 32-Level Ready Queue: A Bitmap Plus 32 FIFOs

The concept is set! Let's look at the scheduler's assets. Selection is a question the scheduler has to answer many times a day, and the speed of the answer depends entirely on the structure in which the ready tasks are waiting. The most straightforward idea is to link the ready tasks into a single chain, and when selecting, scan from the head to the tail, and the one with the smallest priority number goes first. However, when the number of tasks increases, each selection becomes a full search, and the speed of selection drops. Let's see how `scheduler.hpp` answers it. The first few lines are constants used for setup, plus a compile-time guard:

```cpp
    static constexpr TaskPriority_t kPriorityLevels = 32;
    static_assert(kPriorityLevels <= 32, "one-word bitmap fast path requires <= 32 levels");
    static constexpr TaskPriority_t kMaxPrio = kPriorityLevels;
    static constexpr TaskPriority_t kIdlePrio = 0xFF;

    DISABLE_COPY_MOVE(Scheduler);
```

The `static_assert` line is worth pausing on: the fast path of the bitmap requires the number of levels to be no more than 32, because a single machine word can hold all the markers. The author has written the design premise as a compile-time assertion, so if someone ever expands the number of levels to 64, the compiler will block them immediately, and the change to the fast path will be unavoidable. The design constraints and the implementation are tied together on the same rope.

Below are the two major components of the ready structure, and we will look at them one by one:

```cpp
    TCB idle_{};
    TCB* current_{nullptr};
    base::Bitmap<kPriorityLevels> present_{};
    base::SelfList<TCB> fifo_[kPriorityLevels]{};
    [[no_unique_address]] Driver driver_{};
```

The underlying `fifo_` is a table that contains 32 `SelfList<TCB>` entries, one for each priority level. `SelfList` was discussed in the previous stop, and when no comparison strategy is specified, it is a pure FIFO, with earlier tasks coming first. So tasks at the same level are rotated in the order of arrival. The upper `present_` is a 32-bit `Bitmap`, where the bit is 1 if there are people in the queue at a certain level, and 0 if it is empty. When we need to know which levels still have people, we don't have to ask each of the 32 chains one by one; the bitmap answers it in one sentence, and the details that allow selection to be fast are all here. The remaining three in the assets, `idle_` is the embedded idle task, `current_` records who is currently using the CPU, and `driver_` is the driver that the contract needs to work with. We will look at them in more detail when we get to the selection part.

`push_back` is the entry point for tasks entering the ready queue. Let's look at it:

```cpp
    void push_back(TCB* t) {
        if (t->task_priority_ >= kMaxPrio) {
            return;
        }
        t->state_ = TaskState::Ready;
        fifo_[t->task_priority_].insert(t);
        present_.set(t->task_priority_);
    }
```

The initial judgment in `push_back` is key to the idle task's script. Idle is the task embedded in the scheduler itself, and its priority is given as `0xFF`. Any priority not less than 32 will be rejected by `push_back`, so idle will never enter the ready queue. Where does it appear? Let's look at the selection:

```cpp
    TCB* pop_highest() {
        const auto level = present_.find_first_set();
        if (level == decltype(present_)::npos) {
            return nullptr;
        }
        auto t = fifo_[level].pop_head();
        if (fifo_[level].empty()) {
            present_.clear(level);
        }
        return t.get();
    }
```

`pop_highest` asks the bitmap for the lowest non-empty level, pops a person from the head of that queue, and clears the bit if the level is empty. When all are empty, it returns a null pointer, and `pick_next` returns the embedded idle when it receives a null pointer. In short, idle does not occupy a queue position. It is only brought out by `pick_next` when all real tasks are sleeping. The idle task body is `wfi` (wait for interrupt instruction), and when the CPU has no work to do, it goes to sleep, and is pulled up again when an interrupt comes. At the end of the previous stop, the main loop in `main` was left with just one WFI, which we did not expand at that time. It is now the official owner here.

### How the Bitmap Finds the Lowest 1 in One Sentence

The implementation of `find_first_set` lives in `bitmap.hpp`. We'll look at the two related functions together:

```cpp
    /// Index of the lowest set bit; x must be non-zero.
    /// GCC/Clang lower this to RBIT+CLZ on Cortex-M3/M4.
    constexpr std::size_t ctz(std::uint32_t x) {
#if defined(__GNUC__) || defined(__clang__)
        return static_cast<std::size_t>(__builtin_ctz(x));
#else
        return zeros_impl::_ctz(x);
#endif
    }

    /// First set bit, npos if none. Tail word is naturally safe: padding 0s never fake-hit.
    [[nodiscard]] constexpr std::size_t find_first_set() const {
        for (std::size_t w = 0; w < WORDS; ++w) {
            if (words_[w] != 0) {
                return (w << 5) + ctz(words_[w]);
            }
        }
        return npos;
    }
```

`__builtin_ctz` counts the number of zeros after the lowest 1, which in the bitmap is the lowest non-empty level number. The comment预告s the hardware outcome: on Cortex-M3/M4, GCC and clang will reduce it to two instructions: RBIT and CLZ. RBIT reverses the entire 32 bits, turning the lowest 1 into the highest 1. We then let CLZ count the number of zeros before the highest 1, which is exactly the position of the lowest 1 in the original number. Both instructions are single-cycle on M3, so the action of "asking the bitmap for the most urgent level" takes two cycles on the board.

We accept the comments as given, and the assembly on the board still needs to be verified. I wrapped `__builtin_ctz` in a minimal function with a single `unsigned int` parameter and passed it to arm-none-eabi-g++ 16.2.0, setting the target to Cortex-M3 with `-mcpu=cortex-m3 -mthumb -O2`. The objdump output symbol is `_Z3ctzj`. We state this truthfully: the disassembled output is not from the original repository file, but from a function I wrapped myself, with parameters and return types matching the repository's `ctz`. The objdump output is as follows:

```text
00000000 <_Z3ctzj>:
   0:	fa90 f0a0 	rbit	r0, r0
   4:	fab0 f080 	clz	r0, r0
   8:	4770      	bx	lr
   a:	bf00       	nop
```

We count three instructions, where rbit and clz are the two working ones, and bx lr is responsible for returning. The comment matches the silicon. nop is just a placeholder for alignment, and we did not count it as an instruction. The comment in the bitmap about "one-word bitmap fast path" refers to this fast path.

## pick_next: Preempted Tasks Go to the Front, Yielding Tasks Go to the End

Our protagonist makes an appearance. The most important thing in the arbitration surface is fairness, and the rotation of tasks at the same level depends entirely on where the returning person stands. If all returning people go to the end of the queue, the yielding person has nothing to say, but the preempted person is wronged. It did nothing wrong, it was just bumped down by a more urgent person, and upon returning, it has to start from the beginning again. If all returning people go to the front of the queue, the yielding person turns around and gets their turn again, and the same-level partners are never rotated, starving to death. The answer in the repository is that the position where the returning person returns depends on the reason for returning. `pick_next` and `yield` together form the essence of this design, and we will look at them separately.

`pick_next` carries the half of the preempted tasks. Let's look at it:

```cpp
    [[nodiscard]] base::BorrowedPtr<TCB> pick_next() {
        irq::CriticalGuard guard{driver_};
        if (current_ != nullptr && current_ != &idle_ && current_->state_ == TaskState::Running) {
            push_front(current_);
        }
        TCB* next = pop_highest();
        if (next == nullptr) {
            next = &idle_;
        }
        current_ = next;
        next->state_ = TaskState::Running;
        return next;
    }
```

The initial judgment in `pick_next` is the source of the asymmetry. If the current task's state is still `Running`, it means it has not voluntarily yielded the CPU, and it has been preempted. We push it back to the front of the queue for its level using `push_front`, and when it is its turn, it continues from where it left off, without needing to recalculate anything.

`yield` takes the other half, the yielding part. Let's look at it:

```cpp
    void yield() {
        irq::CriticalGuard guard{driver_};
        if (current_ == nullptr || current_ == &idle_) {
            return;
        }
        push_back(current_);
        driver_.request_switch();
    }
```

We look at `yield` again. The current task uses `push_back` to go to the end of the queue, and same-level partners are rotated in order. Yielding once puts it at the end of the queue. The position where it returns is entirely determined by the reason for returning. Similarly, returning to the ready queue, the preempted task continues its work, while the yielding task has to rejoin the queue.

```cpp
    void push_front(BorrowedPtr<Stuff> new_stuff) {
        new_stuff->next_ = stuff;
        stuff = new_stuff.get();
    }
```

The demand for fairness has led to the creation of new container interfaces. As we read the diff, we can see the correspondence between the requirements and the interfaces, which is more interesting than reading a container designed out of nowhere.

The selection decision is made, but what about the switch? You'll notice that `pick_next` and `yield` never touch any registers throughout the entire process. At most, they call `driver_.request_switch()`, and on the board, this function simply sets the PendSV suspend bit to 1. The scheduling API never directly switches at the call site in our implementation. The actual switch is postponed until the PendSV exception is executed, so any call site (including interrupt service routines) only issues a request and does not touch anyone else's stack. The assembly side is the subject of "Faking a Birth Certificate at the Stack Top," and today we only need to know that the request is safe.

`add`, `ready`, and `block` are the three verbs paired with the three-state vocabulary: `add` puts a new task into the ready queue, `ready` puts a sleeping task back, and `block` sends a currently running task to sleep. We'll quickly go through the three verbs, each with its own conditions. Let's start with `add`:

```cpp
    void add(base::BorrowedPtr<TCB> t) {
        irq::CriticalGuard guard{driver_};
        if (!t) {
            return;
        }
        push_back(t.get());
        if (current_ != nullptr && t->task_priority_ < current_->task_priority_) {
            driver_.request_switch();
        }
    }
```

After `add` enqueues, it checks the currently running task. If the new task is more urgent than the current one, a switch request is issued. If it's not urgent, it just enqueues and doesn't disturb anyone.

`ready` is responsible for putting a sleeping task back. Let's look at it:

```cpp
    void ready(base::BorrowedPtr<TCB> t) {
        irq::CriticalGuard guard{driver_};
        if (!t || t->state_ != TaskState::Blocked) {
            return;
        }
        push_back(t.get());
        if (current_ != nullptr && t->task_priority_ < current_->task_priority_) {
            driver_.request_switch();
        }
    }
```

All three functions hold the critical section before manipulating the queue. We need to think about why this is done: `ready` can be called from an interrupt, and the ISR and tasks share the bitmap and FIFO. An unlocked queue will eventually be written to incorrectly. `ready` only accepts tasks in the `Blocked` state at the beginning, and any task that is not sleeping will be ignored when calling `ready`. This judgment has an important story later: it blocks a scenario where a real missed wake-up race condition occurs. We will reproduce and fix it in the article on missed wake-up, which is "Sleeping That Bug: Missed Wake-Up, park_for, and the Builder Chain That Won't Compile."

`block` does the reverse. Let's look at it:

```cpp
    void block(base::BorrowedPtr<TCB> t) {
        irq::CriticalGuard guard{driver_};
        if (!t) {
            return;
        }
        t->state_ = TaskState::Blocked;
        if (t == current_) {
            driver_.request_switch();
        }
    }
```

The judgment in `block` is that the task to sleep must be the current one. If it's someone else, the queue just has one fewer, and no switch request is needed. The three `request_switch` calls are the same logic: a switch request is only issued if the new task is more urgent or if the current task is sleeping. We avoid switching when we can, as each switch has a cost.

## Host Exam: Three Test Cases Validate the Arbitration Surface

The test cases in the test match our main text one by one, and we'll pick three to look at, each covering one pillar of the arbitration surface.

The first pillar we count is the agreement that smaller numbers have priority. We prepared three tasks with priorities of 1, 3, and 0, added them to the scheduler, and called `pick_next` once. The result must be the task c with priority 0:

```cpp
TEST_CASE("pick_next takes the highest priority; add marks Ready", "[sched]") {
    g_switch_requests = 0;
    Scheduler<HostSwitchPort> s;
    TCB a = task(1, "a"), b = task(3, "b"), c = task(0, "c");

    s.add(&a);
    s.add(&b);
    s.add(&c);
    CHECK(a.state_ == TaskState::Ready);
    CHECK(b.state_ == TaskState::Ready);
    CHECK(c.state_ == TaskState::Ready);

    auto t1 = s.pick_next();
    REQUIRE(t1.get() == &c);
    CHECK(c.state_ == TaskState::Running);
    CHECK(s.current_task().get() == &c);

    s.block(&c);
    auto t2 = s.pick_next();
    REQUIRE(t2.get() == &a);
}
```

Looking at the latter part of the test case, it also verifies the order: after task 0's c sleeps, the next one is task 1's a, not task 3's b, and the level order given by the bitmap matches the priority number agreement.

The second pillar is the part of asymmetric enqueueing where yielding tasks go to the end of the queue. Let's look at the test case:

```cpp
TEST_CASE("same level round-robins via yield-to-tail", "[sched]") {
    g_switch_requests = 0;
    Scheduler<HostSwitchPort> s;
    TCB a = task(1, "a"), b = task(1, "b"), d = task(1, "d");

    s.add(&a);
    s.add(&b);
    s.add(&d);

    REQUIRE(s.pick_next().get() == &a);
    s.yield();
    REQUIRE(s.pick_next().get() == &b);
    s.yield();
    REQUIRE(s.pick_next().get() == &d);
    s.yield();
    REQUIRE(s.pick_next().get() == &a);
}
```

Three tasks at the same level, whoever yields, the next one is whoever. After a full rotation, it returns to a. Imagine what would happen if we changed `yield` to return to the front: a would always be the one to go, and b and d would starve forever. So those five lines of `push_front` must never be borrowed by `yield`.

The third pillar we count is the practice of returning preempted tasks to the front of the queue. The name of the test case is "preempted task resumes from the head of its level":

```cpp
    s.add(&a);
    REQUIRE(s.pick_next().get() == &a);

    auto before = g_switch_requests;
    c.state_ = TaskState::Blocked;
    s.ready(&c);
    CHECK(switches_after(before) == 1);

    REQUIRE(s.pick_next().get() == &c);
    CHECK(a.state_ == TaskState::Ready);

    s.block(&c);
    REQUIRE(s.pick_next().get() == &a);
    CHECK(a.state_ == TaskState::Running);
```

Let's go through the plot: a has a priority of 1 and is already on the CPU. Task 0's c is awakened by `ready`, and a switch request is counted once because the new one is more urgent, and it's right to preempt. a is pushed back to the front of the queue by `pick_next`, so after c sleeps, the next one is still a, and its state assertion is `Running`, continuing from where it left off on the CPU. Of the nine test cases, there are also scenarios covering waking up same-level tasks without requesting a switch, idle taking over when all are blocked, and edge cases where only non-current tasks are blocked without requesting a switch. One of the remaining test cases specifically verifies sleep-wake, where we use pointer subtraction from `owner_of` to backtrack the expiry record back to the TCB, fulfilling the promise left in the file card section. We won't go through them all in detail, but you can see them all by running them once.

## Next Article

We've covered the arbitration surface, and now we have a scheduler that can select, but it has never really handed the CPU from one task to another. The next article, "Faking a Birth Certificate at the Stack Top," enters the mechanism surface of the same diff: the eight words of hardware exception frames, how `fabricate_frame` forges the first context on the top of the new task's stack, `svc 0` first switch bridge and save/restore in naked assembly, and the question we left hanging in the previous stop, why the priority of SysTick in `init_time` was changed from `0xF0` to `0xFF`, placing it at the same level as PendSV. We'll see you there.
