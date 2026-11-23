---
title: "The Right to Define a Second: Ticks, Wraparound Arithmetic, and Expiry Queues"
description: "This article discusses the time subsystem: the Ticks value type uses signed differences to compare uint32 values even after wraparound, SelfList intrusive linked list sorts by deadlines and FIFO for same deadlines, Kernel<Driver> template with BASEPRI nested critical sections, the same code runs on host mock and Blue Pill; the host test for commit 54817f3 failed due to missing self_list.hpp, after checking out 4f12867, all 340 lines of time tests passed, in Renode, the periodic ticker simulation prints one line per second"
chapter: 2
order: 1
tags:
  - stm32f1
  - intermediate
  - 嵌入式
  - 寄存器
  - cpp-modern
difficulty: intermediate
platform: stm32f1
cpp_standard: [23]
reading_time_minutes: 24
prerequisites:
  - "No Heap World · Part 4: Make/Destroy: Type-Specific Birth and Death and Compile-Time Defense"
related:
  - "No Heap World: First Assign Memory Correctly"
translation:
  source: documents/vol8-domains/embedded/zeros/02-time-kernel/01-ticks-and-timer-queue.md
  source_hash: dbae35d3ae908ed291a57aec121cfff26065f9a9ba78779daa9c1198db786493
  translated_at: '2026-11-22T13:31:50+00:00'
  engine: qwen3:14b
  token_count: 22137
---
# The Right to Define a Second: Ticks, Wraparound Arithmetic, and Expiry Queues

What we're going to do next is time. Let's think: the RTOS scheduler will definitely ask the hardware friend: "Hey, my hardware friend, tell me, have you reached the **200ms time** set by the user? You can't be silent. Right? So memory is there, the next real responsibility of the kernel is time. However, before writing about time, we'll do a small experiment on our hands, which ties up the tail of the memory stack. We'll complete it before moving on to the main topic, because the problem in the experiment is exactly answered by the code we're going to present in this article.

## Ticks: Putting a Second into a Type

The new type we're going to create is called Ticks, and its new home is `include/ZerOS/kernel/clock/ticks.hpp`. The entire file has 33 lines, and we'll split it into three parts, going through them one by one. However, before entering the file, there are three questions that need to be raised upfront, and they are the reasons for the existence of this type.

The first question: what is a second on the chip? The chip has no clock, the timer hardware only does one thing: count beats. We give the clock a beat, and each count of the beat is a fixed duration. So the "one second" we talk about is an agreement of 1000 beats. Counting beats is the job of our new type Ticks.

The second question: where will the raw `uint32_t` go wrong? Its problem is that it **doesn't remember its identity**. Whether it's storing a moment or a span, it's not visible from the numerical value. Users can only rely on comments and brain to remember.

The third question: why does this error become a hard-to-find bug when it wraps around? The counter for counting beats is 32 bits, and when it reaches the top, it returns to zero. So wraparound is unavoidable. After wraparound, how to determine who is earlier or later is the question we give to `is_after`. The answer to this question will be discussed in detail in the wraparound section. After establishing these three questions, we move into the file. The first part is the shell of the type, and we look at it together with the header comment:

```cpp
#pragma once

#include <cstdint>
namespace ZerOS::clock {
/*
    Well Well, Clock Ticks everytime, how long is it?
    or we say, how should we define a second :)
*/
struct Ticks {
  public:
    using tick_t = uint32_t;
    using tick_diff_t = int32_t;

    constexpr Ticks() = default;
    constexpr Ticks(tick_t tick) : tick_(tick) {}
```

The first sentence of the header comment puts the big question on the table: the clock is always moving, so how long is a second? We give it a 1 kHz beat, and each count is a millisecond. If we switch to 100 Hz, it's ten milliseconds. The type only needs to remember a certain number of ticks, and the period is determined by the user.

The type itself has a stance worth noting: raw `uint32_t` is not allowed to fly freely. If we want to express "passed 500 beats", we need to write code like `Ticks{500}`. The benefit is visible at the call site: code like `call_after_span(w, 1000)` makes the reader know that the unit of 1000 is ticks, even saving comments. `tick_t` and `tick_diff_t` are two aliases that appear in pairs, one is a 32-bit unsigned counter value, and the other is a 32-bit signed difference. The question of which one to use will be answered in the next section's `is_after`, and we'll see it right away.

### Wraparound: The Decision Made by the Register Width

Why must the tick counter wrap around? Because the hardware timer is 32 bits, and after counting to `0xFFFFFFFF`, the next step returns to zero. The register width makes this decision for us. The comment in the code is straightforward: "Register can be Overflow." We'll reveal the answer below:

```cpp
    // Or, we say is_due, but i think this is much hard to understand :(
    constexpr bool is_after(Ticks now) const noexcept {
        // We say, if the now ROLL back to 0(Register can be Overflow)
        return static_cast<tick_diff_t>(now.tick_ - tick_) >= 0;
    }

    void forward(tick_t step) noexcept { tick_ += step; }
```

Returning to zero is not scary, but what's scary is that after returning to zero, it's impossible to compare who is earlier or later. We'll go through the `is_after` mechanism. It answers a question: is the current moment already reached or has it passed the deadline? Pay attention to the direction: the difference is obtained by subtracting `deadline` from `now`, and it is considered to have reached the point when it is greater than or equal to zero. The problem is that `now.tick_ - tick_` is an unsigned subtraction. If the deadline is earlier and `now` has returned to zero, the result is a huge number.

`static_cast<tick_diff_t>` does exactly that: it reinterprets this huge number as a signed number. Let's go through a real example: the deadline stores `0xFFFFFFFC`, and `now` has wrapped around to `0x00000002`. We calculate the difference with unsigned subtraction: `0x00000002 - 0xFFFFFFFC` modulo 32 bits gives 6, and interpreting it as `int32_t` still gives 6. The result is greater than or equal to zero, and we judge that it has reached the point. We try the case where `now` is `0xFFFFFFFB`, which hasn't wrapped around yet, and the difference is -1, and we judge that it hasn't reached yet. So as long as the real interval is within the range that `int32_t` can express, this comparison gives the same answer before and after wraparound.

Let's calculate this range: under a 1 kHz beat, `int32_t` can hold 2,147,483,647 ticks, which is about 24.8 days. When two deadlines are closer than 24.8 days, we won't make a mistake in comparing their order. For example, two moments, one 20 days later and the other 40 days later, are 20 days apart, and the judgment is still valid. Once the real interval exceeds 24.8 days, the difference wraps back to a negative number, and the later moment is judged as "already expired" when inserted into the queue, and it is popped out and executed before the deadline. This is the mechanism that causes the problem. In embedded systems, we rarely let tasks wait this long, and we just need to remember this boundary in our minds, without writing code to prevent it.

We also take a look at `forward` on the side. It does the job of moving the counter forward by a step, and the clock uses it to advance the clock.

This arithmetic of sequence numbers is also common in protocol stacks. TCP's sequence number comparison follows the same path. We write this for the first time into our own kernel here, and the scheduler and watchdog in the future will rely on it.

The end of the file has a small segment left, which we'll wrap up:

```cpp
  public:
    tick_t tick_;
};

constexpr Ticks operator+(Ticks ticks, Ticks::tick_t move_steps) {
    return {ticks.tick_ + move_steps};
}

} // namespace ZerOS::clock
```

The `operator+` at the end is written as a free function, marked as `constexpr`, indicating that it adds a span to the current moment, and `clock_.current() + span` produces a new deadline moment. The left value is unchanged. We put this function outside the type, and the mixed operations of `Ticks` on the left and raw `tick_t` on the right are natural. We don't need to open a bunch of overloads in the class.

## Clock and TimeWaiter: Current Time and When to Act

Ticks put a beat into a type, and the time kernel still needs two things: one answers the question of what time it is now, and the other records what to do when the time comes. In the same directory, `clock.hpp`, 54817f3 handed over two roles here: Clock holds the current Ticks, and the answer to the time is in its hands. TimeWaiter has a deadline and an action to perform when the time comes. All the entries in the queue are it. We'll split the file into two parts, and first we'll look at Clock:

```cpp
#pragma once

#include "ZerOS/base/borrowed_ptr.hpp"
#include "ZerOS/base/self_list.hpp"
#include "ZerOS/kernel/clock/ticks.hpp"
namespace ZerOS::clock {

struct Clock {
    [[nodiscard("Hey boy, dont throw me away!")]]
    constexpr Ticks current() const noexcept {
        return now_;
    };

    constexpr void tick_forward(Ticks::tick_t steps) { now_.forward(steps); }

  private:
    Ticks now_{};
};
```

We look at `Clock`, it's a type as small as a variable, and it only has one job: what time is it now. `current()` returns `Ticks`, and it has a custom discarded warning on it, `[[nodiscard("Hey boy, don't throw me away!")]]`. Why is it so strict? Let's think: if we get the current time but don't catch the return value, the call has no meaning at all. Letting the compiler shout at the place where the return value is discarded is much cheaper than us debugging after the fact.

It's TimeWaiter's turn now. Let's look at its appearance:

```cpp

// Wait... oh it's time to take action!
struct TimeWaiter : public base::SelfNode<TimeWaiter> {
    using OnTimeAction = void (*)(base::BorrowedPtr<TimeWaiter> self);
    Ticks deadline_;
    OnTimeAction OnTime;

    constexpr TimeWaiter() = default;
    constexpr TimeWaiter(Ticks deadline, OnTimeAction on_time)
        : deadline_(deadline), OnTime(on_time) {};
};

} // namespace ZerOS::clock
```

`TimeWaiter` is a line of record on the expiry: a deadline, plus an action to perform when the time comes. We'll look at two design points. One is that it inherits `SelfNode<TimeWaiter>`, carrying its own `next_` pointer on its back, which is the initiation ceremony of the intrusive container. The `SelfList` doesn't need to allocate extra memory for the nodes. The second is that the callback is written as a raw function pointer `void (*)(BorrowedPtr<TimeWaiter>)`. `std::function` can't be used here because it may allocate the heap, and the entire kernel has no heap. The combination of raw pointers and non-capturing lambdas can be converted into function pointers, and the zero-cost pairing will be seen in the examples later.

The callback and node are up to you to arrange. The kernel doesn't manage the life and death of `TimeWaiter` for you: you can define one statically, or create one in the pool, the kernel only borrows its address. The name `BorrowedPtr` clearly writes the measure on its face.

## How These Base Tools Came About

`clock.hpp` includes `borrowed_ptr.hpp`, and 54817f3 also handed over several base layer small tools together. We'll look at them one by one.

We look at `BorrowedPtr`, which is a wrapper for non-owning pointers. The sentence in the header comment, "Bro I borrowed this :)", clearly states the stance:

```cpp
template <typename BorrowedSrc> struct BorrowedPtr {
    constexpr BorrowedPtr() = default;
    constexpr BorrowedPtr(BorrowedSrc* src) noexcept : source(src) {}

    constexpr explicit operator bool() const { return source != nullptr; }
    constexpr BorrowedSrc* operator->() const { return source; }
    [[nodiscard]] constexpr BorrowedSrc* get() const { return source; }
    constexpr BorrowedSrc& operator*() const {
        debug::Check(source, "Deref NulPtr");
        return *source;
    }

    friend constexpr bool operator==(BorrowedPtr a, BorrowedSrc* b) { return a.source == b; }
    friend constexpr bool operator==(BorrowedPtr a, BorrowedPtr b) { return a.source == b.source; }

  private:
    BorrowedSrc* source{nullptr};
};

// Make A Compile Time Assumptions that we COST NOTHING
static_assert(sizeof(BorrowedPtr<int>) == sizeof(int*));
static_assert(std::is_trivially_copyable_v<BorrowedPtr<int>>);
```

What it adds over a raw pointer is only one thing: an extra `debug::Check` to intercept null pointers before dereferencing. What's the cost? The two `static_assert` at the end of the file prove that its size is the same as a raw pointer, and it is trivially copyable, copied and passed without hiding any behavior. We want to prove zero cost, and the assertion is written into the compile time, harder than ten sentences in the documentation.

`debug::Check` itself is very short, but there's a part worth looking at:

```cpp
inline constexpr void Check(bool ShouldBeTrue, [[maybe_unused]] const char* msg,
                            [[maybe_unused]] std::source_location loc = std::source_location::current()) {
    if (ShouldBeTrue) {
        return;
    }

    // auto global_channel = ZerOS::log::FetchGlobalChannel();
    // global_channel();

    // Oh we sucks!
    while (1) {
        // Halt here in debug mode
    }
}
```

When the assertion fails, it originally sends the message into the global log channel, but those two lines are commented out because the log channel hasn't been born yet. The current behavior is to hang in a dead loop, and when the assertion fails, it stops there. We can attach a debugger, and the scene is not running at all. The parameter `std::source_location` has a default argument, and when the log channel is connected in the future, no lines need to be changed at the call site, and the filename and line number will be automatically in place. `global_channel.hpp` also leaves the same seam: only `RegisterGlobalChannel` and `FetchGlobalChannel` are declared, and the implementation is empty, left for the fault/log line to fill in.

`traits.hpp` has only one `AlwaysFalse`, which is our first default strategy value: a function object that returns false for all comparisons. It will appear in the linked list immediately.

## SelfList and TimerQueue: Expiry Records Sorted by Deadline

The container for expiry records is `SelfList`, an intrusive linked list with a strategy comparator. The incident on this header file is discussed here because it directly determines how our acceptance is calculated.

We look at the interface:

```cpp
template <typename Stuff> struct SelfNode {
    Stuff* next_{};
};

template <typename Stuff>
concept SelfLinked = std::is_base_of_v<SelfNode<Stuff>, Stuff>;

template <SelfLinked Stuff, typename Less = ZerOS::traits::AlwaysFalse> struct SelfList {
    SelfList() = default;
    SelfList(const SelfList&) = delete;
    SelfList& operator=(const SelfList&) = delete;
    [[nodiscard]] BorrowedPtr<Stuff> head() const { return stuff; }

    void insert(BorrowedPtr<Stuff> new_stuff) {
        Stuff** slot = &stuff; // Lets walk!

        while (*slot && !less_(*new_stuff, **slot)) {
            slot = &(*slot)->next_;
        }

        new_stuff->next_ = *slot;
        *slot = new_stuff.get();

        // OK, quit here
    }
```

`concept SelfLinked` writes the initiation as a type constraint: if we want to live in `SelfList`, we need to inherit our own `SelfNode`. The insertion method is a pointer to a pointer, and we let `slot` move along `next_` until it is smaller than the current element, then connect it in one step, and the new node is placed in front of it. The entire process has no `new` or temporary packaging, and the node is the object itself on the linked list.

The sorting behavior is entirely dependent on the value of `Less` this strategy. When not specified, the default is `AlwaysFalse`, which returns false for all comparisons, so the insertion always goes to the end of the list, and the linked list degenerates into a pure FIFO. When the strategy is specified, it becomes an ordered table. One of the three assertions at the end of the file has a failure message that I really like, and I'll quote it for you:

```cpp
static_assert(std::is_trivially_destructible_v<SelfList<zeros_impl::Probe>>);
static_assert(sizeof(SelfList<zeros_impl::Probe>) == sizeof(zeros_impl::Probe*),
              "SelfList is not Zero cost");
```

The assertion itself proves that `SelfList` is the size of a raw pointer, and even checks for the traces of empty base class optimization for us. The strategy object is squeezed in with `[[no_unique_address]]`, not taking up its own space. The failure message says "not Zero cost", you can savor this humor: if you don't believe this, the compiler will tell you face-to-face that `SelfList` is not zero cost, and the evidence you can find in the previous assertion.

`SelfList` gives the mechanism of "how to sort", and as for what kind of order and when to dequeue, we need to find the answer from `TimerQueue`. It has two questions to answer, we'll look at them one by one: one is the question of which deadline is earlier, and our answer must withstand wraparound, and the other is whether the head of the queue has arrived, and if it has, we hand over the node, and if it hasn't, we return empty. The first answer is given by `DeadLineOrder`, let's see what it looks like:

```cpp
struct TimerQueue {
    struct DeadLineOrder {
        bool operator()(const TimeWaiter& lhs, const TimeWaiter& rhs) const noexcept {
            return static_cast<Ticks::tick_diff_t>(rhs.deadline_.tick_ - lhs.deadline_.tick_) > 0;
        }
    };
```

`DeadLineOrder` is the second time wraparound arithmetic is on duty: we convert the difference into `tick_diff_t` and judge the sign, and it follows the same idea as `Ticks::is_after`. So the head of the queue we see is always the earliest deadline in real terms, even if it has just wrapped around. The order of the same deadline also has its own rules: the comparator returns false for two equal moments, and the while loop continues to move forward, placing the new node at the end of all equal nodes, and the earlier one is placed in front.

The other answer on the exit, let's look at the remaining part together:

```cpp

    void insert(base::BorrowedPtr<TimeWaiter> timer_task) { list_.insert(timer_task); }
    [[nodiscard]] base::BorrowedPtr<TimeWaiter> head() const { return list_.head(); }
    bool remove(base::BorrowedPtr<TimeWaiter> w) { return list_.remove(w); }

    [[nodiscard]] base::BorrowedPtr<TimeWaiter> pop_due(Ticks now) {
        // Do WE have task? and is there something we need to do by given NOW ticks
        if (list_.head() && list_.head()->deadline_.is_after(now)) {
            return list_.pop_head();
        }
        return nullptr;
    }

  private:
    base::SelfList<TimeWaiter, DeadLineOrder> list_;
};
```

`pop_due` sets the practice of only dequeuing from the head: we only pop when the head's deadline has arrived, and return an empty pointer when it hasn't. Because the linked list is ordered, we only need to judge one, and the whole queue doesn't need to be scanned.

## Kernel\<Driver\>: One Template, Two Worlds

4f12867 was submitted on the same day at 10:10 AM, with the subject "feat: clock subsys finished", and the time subsystem took shape in this commit. The main item we need to look at is `include/ZerOS/kernel/clock/kernel.hpp`'s `Kernel<Driver>` template. Why is the kernel made into a template? Because we want it to run the same code in two worlds: the computer we use for development is one world, and the other world is where Blue Pill is located. The two worlds provide time in different ways, and the kernel's way of trusting is to hand over the defense and disable interrupt actions to a replaceable driver parameter, and the kernel itself doesn't need to change a single line. The parameters are not given at will, and the access conditions are `TimePortable`. Drivers that don't meet the conditions can't even compile, and we see the contract from the implementation.

### The Contract Comes First

```cpp
template <typename Portable>
concept Armable = requires(Portable p, Ticks::tick_t ticks) {
    p.arm(ticks);
    p.disarm();
};

template <typename IsTimePort>
concept TimePortable = Armable<IsTimePort> && irq::CriticalSection<IsTimePort>;
```

`TimePortable` is the hardware porting contract: drivers that want to serve the time kernel must be able to arm (remind the kernel after a period of time) and disarm (disarm), and must also be able to lock/unlock the critical section provided. We write the contract as a concept, and drivers that don't meet the conditions will report an error at the line of the template parameter, and the error location is closest to the cause, saving us from trouble.

### Critical Section: Nested Counting BASEPRI

We look at the latter half of the contract, its home is in the file `include/ZerOS/kernel/irq/critical_section.hpp`. lock and unlock serve another kind of trouble for the time kernel: `now`, the expiry queue, and the defense status are states that both the task side and the interrupt side need to modify together, and the tick interrupt may cut in at any time, and if it sees a state that is being modified halfway, the queue will be messed up. So the kernel needs a critical section that no one can interrupt, blocking interrupts when entering and allowing them again when exiting.

```cpp
template <typename IsCriticalSection>
concept CriticalSection = requires(IsCriticalSection& t) {
    t.lock();
    t.unlock();
};

template <CriticalSection Lockable> class CriticalGuard {
  public:
    [[nodiscard]] explicit CriticalGuard(Lockable& target) : lockable_(target) { lockable_.lock(); }
    ~CriticalGuard() { lockable_.unlock(); }

  private:
    DISABLE_COPY_MOVE(CriticalGuard);

  private:
    Lockable& lockable_;
};
```

This is a RAII guard template: lock when constructed, unlock when destructed, copy and move are all deleted, and the ownership of the lock is not allowed to be copied away. `[[nodiscard]]` is attached to the constructor to prevent you from writing a temporary object that destructs the guard on the spot.

We look at the CM3 implementation, which lives in the file `include/ZerOS/arch/arm_cortex_m3/cm3_irq.hpp`. CM3 gives us two switches to disable interrupts, and we choose the selective masking BASEPRI here. Why not use the all-or-nothing PRIMASK, we'll see after looking at the code:

```cpp
struct CortexM3CriticalSection {
    void lock() {
        if (nest_++ == 0) {
            saved_basepri_ = __get_BASEPRI();
            __set_BASEPRI(kMaskLevel);
        }
    }
    void unlock() {
        if (--nest_ == 0) {
            __set_BASEPRI(saved_basepri_);
        }
    }

  private:
    static constexpr std::uint32_t kMaskLevel = 0x40;
    std::uint32_t nest_{0};
    std::uint32_t saved_basepri_{0};
};

static_assert(ZerOS::irq::CriticalSection<CortexM3CriticalSection>);
```

This code uses BASEPRI instead of PRIMASK. BASEPRI is a register for selective masking. We write `0x40` into it, and all interrupts with a numerical value greater than or equal to `0x40` are blocked, while those with smaller values are allowed to come in. So the most urgent batch of interrupts can still come in. PRIMASK is different, it's an all-or-nothing approach, even the most urgent interrupts are dragged by us, so we choose BASEPRI to leave a path for the urgent interrupts. However, BASEPRI can't mask interrupts with level 0, this sentence we remember in our minds, and the next article will use it for the confrontation.

Nested counting `nest_` solves the problem of locks within locks, and locks within locks really happen in this kernel: the callback of the board's ticker is executed within the critical section of `on_elapsed`, and the callback calls `call_after_span`, which itself opens a guard, nesting one layer after another. If we don't layer when locking, the inner guard will release the lock when it is destructed, and the outer one still thinks it's in the critical section. So lock only moves the hardware when the count changes from 0 to 1, and saves the old BASEPRI value. Unlock only restores the original value when the count returns to 0. The original value is restored, not written as 0, and this detail also stands in the nested case: if the outer layer was originally in a higher masking level, the inner layer shouldn't lower the masking when it exits.

### Kernel Body

After the contract is checked, we look at the body of `Kernel<Driver>`. It manages three things externally: scheduling, cancellation, and asking for the current time, and internally it leaves an entry for the ISR side, and the `on_elapsed` is used by the interrupt service routine to advance the clock. The three actions on the task side, we look at the code:

```cpp
template <TimePortable Driver> struct Kernel {
    // A Timer fetch w for span
    void call_after_span(base::BorrowedPtr<TimeWaiter> task, Ticks::tick_t span) {
        irq::CriticalGuard guard{lowlevel_driver_};

        // set the deadline
        task->deadline_ = clock_.current() + span;
        queue_.insert(task);
        update_alarm();
    }

    // if true, we cancel success
    bool cancel(base::BorrowedPtr<TimeWaiter> task) {
        irq::CriticalGuard guard{lowlevel_driver_};
        bool result = queue_.remove(task);
        update_alarm();
        return result;
    }

    Ticks current() const { return clock_.current(); }
```

There are three public actions, we'll go through the call site. `call_after_span` is the entry on the task side: in the critical section, it adds the span to the current time to set the deadline and puts it into the expiry record. Next, we need to consider whether to move the hardware. We wrap the queue operations in the critical section to prevent the scene where the interrupt cuts in. The queue state read by the task side is the truth in the ISR's eyes, and the queue state is messed up.

The entry on the interrupt side is handed over to `on_elapsed`:

```cpp

    // ISR side call
    void on_elapsed(Ticks::tick_t n) {
        if (n == 0) {
            return;
        }
        irq::CriticalGuard guard{lowlevel_driver_};
        clock_.tick_forward(n);
        while (auto due = queue_.pop_due(clock_.current())) {
            due->OnTime(due);
        }
        update_alarm();
    }
```

`on_elapsed` is the entry on the ISR side, and the function name means "passed n beats". It advances the clock by n beats, pops out all the expired nodes in the expiry record one by one, and still goes through the `update_alarm` process. The early return for `n == 0` is placed before taking the lock, and zero beats are not worth moving the lock.

The callback signature `due->OnTime(due)` is worth stopping for a moment: when it's time to execute, it passes the node's own `BorrowedPtr` back to the callback. This achieves the self-referential hanging mode of the board demo, and the callback can hold itself to schedule the next cycle, saving the global variable.

### Greedy Defense: The Precision of update_alarm

The defense strategy is in the private function `update_alarm`, and the question it answers is very practical: the expiry queue is alive, and earlier deadlines can be inserted by tasks at any time, and cancellations can come at any time. When is it worth reprogramming the hardware alarm? The naive approach is to rearm every time the queue moves, but reprogramming has real costs: writing registers, clearing counters, each step is a real cost. The more times the defense is set, the more the precision of the next beat is eaten up by the management overhead. The answer here is called greedy defense, which only recognizes the current earliest deadline each time, and the already set alarm is still valid, so the hardware is not moved. We'll split it into two halves, the first half is when the queue is empty:

```cpp
  private:
    void update_alarm() {
        auto head = queue_.head();
        if (!head) {
            if (arm_valid_) {
                lowlevel_driver_.disarm();
                arm_valid_ = false;
            }
            return;
        }
```

When the head doesn't exist, it disarms, and `arm_valid_` ensures that disarming is only done once. The remaining half is when the head exists, we'll look at it:

```cpp

        const auto now = clock_.current().tick_;
        bool armed_pending =
            arm_valid_ && static_cast<Ticks::tick_diff_t>(armed_.tick_ - now) > 0;
        bool head_earlier =
            arm_valid_ && static_cast<Ticks::tick_diff_t>(armed_.tick_ - head->deadline_.tick_) > 0;

        if (armed_pending && !head_earlier) {
            return;
        }

        auto remaining = static_cast<Ticks::tick_diff_t>(head->deadline_.tick_ - now);
        if (remaining < 1) {
            remaining = 1; // Clamp to 1, call it next
        }
        lowlevel_driver_.arm(static_cast<Ticks::tick_t>(remaining));
        armed_ = head->deadline_;
        arm_valid_ = true;
    }
```

When the head exists, it calculates two booleans: `armed_pending` asks whether the already set defense is still valid (whether the already set alarm is still in the future), and `head_earlier` asks whether the new head is earlier than the already set one. When the already set defense is still valid and the new head is not earlier than it, the function returns directly, and the hardware timer won't move a single byte. The already set alarm is allowed to ring, and after `on_elapsed` finishes, it will naturally rearm for the new head. Only when a earlier deadline appears is it worth rewriting the alarm, which is the greedy we mentioned at the beginning: each time, only the current earliest is set, and never for later ones.

The sentence in the comment "Clamp to 1, call it next" is another precision in our eyes: when the head is actually expired (remaining time is less than 1), the defense value is clamped to 1, which is equivalent to calling it next beat. Expired tasks are not discarded, they are delayed by one beat and executed, and the timeout callback will still be executed, but it's one beat late.

### CM3 Porting Layer: arm and disarm are temporarily empty

We continue to look at `include/ZerOS/arch/arm_cortex_m3/cm3_time_port.hpp`, which brings the two halves together:

```cpp
// For simple and toy, ARM and disarm currently using systicks
struct CortexM3Armable {
    void arm([[maybe_unused]] ZerOS::clock::Ticks::tick_t f) { return; }
    void disarm() {}
};

struct CortexM3Portable : public CortexM3Armable, CortexM3CriticalSection {};
static_assert(ZerOS::clock::TimePortable<CortexM3Portable>);
```

The current `arm` and `disarm` are both no-ops, and the comment states: we are in the stage of a simple toy, and the defense is temporarily handed over to the periodic SysTick. The time kernel runs by asking the head of the queue every time a beat passes, and the precision of `arm` is not yet fulfilled. The real single-time defense will come when tickless appears, and at that time, the two empty functions will be filled, and the defense action will be managed by the hardware timer, and we won't rely on periodic inquiries anymore. The last line `static_assert` is a self-check of the contract: this assembly satisfies `TimePortable`, and if it doesn't satisfy it one day, the firmware won't even compile.

## The Exam on the Host: The Same Kernel Runs on Mock

The direct benefits of the porting contract are realized in the test. We look at the excerpt from `test/test_time_kernel.cpp` (the full file is 187 lines), where a host surrogate is defined:

```cpp
struct HostTimePort {
    void arm(Ticks::tick_t d) { g_arms.push_back(d); }
    void disarm() { ++g_disarms; }
    void lock() {}
    void unlock() {}
};

static_assert(ZerOS::clock::TimePortable<HostTimePort>);
```

The calls to `arm` are logged by it, the number of calls to `disarm` is counted, and the lock is empty, we don't need a real lock on the host. It satisfies the same concept, and the verification passes, so `Kernel<HostTimePort>` and the board's `Kernel<CortexM3Portable>` are two instances of the same template. On the desktop side, the defense strategy, wraparound, and cancellation logic are all run through, and on the board, only the porting layer needs to be verified. The four targets: host, Renode, QEMU, and the real development board, all eat the same kernel code, and this goal is realized for the first time on the timeline.

We'll pick a few tests to look at. The first test pushes the clock to `0xFFFFFF00`, only 0x100 beats away from the top of the 32-bit counter, and the wraparound line is right in front of us (excerpt, `Waiter`, `reset_logs`, `fired_deadlines` these auxiliary items are in the same file in front):

```cpp
TEST_CASE("wraparound across the rollover", "[timekernel]") {
    reset_logs();
    Kernel<HostTimePort> k;
    k.on_elapsed(0xFFFFFF00u);

    Waiter a;
    k.call_after_span(&a.node, 0x80u);

    k.on_elapsed(0x7Fu);
    CHECK(g_fired_nodes.empty());

    k.on_elapsed(0x01u);
    CHECK(fired_deadlines() == std::vector<std::uint32_t>{0xFFFFFF80u});
}
```

The clock is pushed to `0xFFFFFF00`, and the deadline is set to `0xFFFFFF00 + 0x80 = 0xFFFFFF80`, only 0x80 away from the top of the counter value. We go 0x7F beats to `0xFFFFFF7F`, one beat short, and it's not allowed to ring. After one more beat, it exactly reaches `0xFFFFFF80`, and it should ring. If you compare by numerical value, `0xFFFFFF80` is huge in the unsigned sense, and it seems like it will never come up. We really compare the sign of the difference, and this judgment gives the same answer before and after wraparound.

There is also a dedicated test for greedy defense, which we'll quote for you:

```cpp
TEST_CASE("greedy arm: only a strictly earlier head re-arms", "[timekernel]") {
    reset_logs();
    Kernel<HostTimePort> k;
    Waiter a, b, c;
    k.call_after_span(&a.node, 100);
    REQUIRE(g_arms.back() == 100);

    k.call_after_span(&b.node, 50);
    REQUIRE(g_arms.back() == 50);

    auto arms_before = g_arms.size();
    k.call_after_span(&c.node, 70);
    CHECK(g_arms.size() == arms_before);

    CHECK(k.cancel(&b.node));
    CHECK(g_arms.size() == arms_before);
}
```

We set a defense of 100, and then a newer one of 50, we rearm. Then we come with 70, which is behind 50, and the number of defenses doesn't change. Then we cancel the node with 50, and the head becomes c with 70. Intuitively, the already set alarm protects the 50 of b, which is canceled, and it should be re-armed. However, `update_alarm` calculates, and the already set 50 hasn't arrived yet, and c's 70 is not earlier than it, the function returns directly, and the defense record doesn't increase. The assertion counts the number of `arm` calls in the entire sequence, and we count the number of `arm` calls to know whether the strategy is correct.

The test for expiry records is in `test/test_timequeue.cpp` (153 lines), which specifically tests sorting and expiry, and FIFO with the same deadline has its own independent test case, and the boundary of exactly on time also has its own test case, and the sorting across the wraparound line also has its own test case. The two files together write 340 lines, which is our acceptance target.

## On the Board: Self-Hanging Periodic Ticker

On the board, we look at `example/main.cpp`, which has switched from the memory demo to the time demo:

```cpp
std::uint32_t tick_count = 0;

constexpr auto on_tick = +[](ZerOS::base::BorrowedPtr<ZerOS::clock::TimeWaiter> w) {
    ZerOS::board::print("tick ");
    ZerOS::board::printdec(++tick_count);
    ZerOS::board::print("\r\n");
    ZerOS::arch::cortex_m3::system_time.call_after_span(w, 1000);
};

constinit ZerOS::clock::TimeWaiter ticker{ZerOS::clock::Ticks{0}, on_tick};
```

This is the two clues we've planted earlier coming together: the unary `+` turns the non-capturing lambda into a raw function pointer, which fits perfectly into the function pointer slot of `OnTimeAction`. There's no heap, and we don't see the shadow of `std::function`. In `call_after_span(w, 1000)`, that `w` is the self received by the callback, and we use it to schedule the next 1000 beats, and the periodic timer spins up on its own, and the kernel doesn't have a dedicated type for periodic timers. `ticker` itself is `constinit`, and it's in place at compile time, and the discipline set in the previous experiment is applied to the time object here.

The initialization and ISR code are each one line. We open `src/arch/arm_cortex_m3/time_kernel_cm3.cpp`:

```cpp
void ZerOS::arch::cortex_m3::init_time(std::uint32_t cycles_per_tick) {
    NVIC_SetPriority(SysTick_IRQn, 0xF0); // Lowest Level
    SysTick_Config(cycles_per_tick);
}

extern "C" void SysTick_Handler() {
    ZerOS::arch::cortex_m3::system_time.on_elapsed(1);
}
```

We look at `main` in `example/main.cpp`, where it calls `init_time(SystemCoreClock / 1000)`. SystemCoreClock on this board is 72 MHz main frequency, and dividing by 1000 gives 72,000 clock cycles per tick, and the calculated beat is exactly 1 kHz, one tick is one millisecond. The definition of "one second" is here: 1000 beats. The interrupt service routine of SysTick is only one line, it only transfers the fact that one beat has passed to the time kernel. We can see that there are no extra actions in the ISR, the work of parsing the expiry records and running the callbacks is all done in `on_elapsed`, and this writing makes the time boundary of the interrupt service routine visible at a glance.

The first line of `init_time` is `NVIC_SetPriority(SysTick_IRQn, 0xF0)`, and the comment says "Lowest Level". I'll leave it for now, you just need to know that this line is related to the previous statement in the BASEPRI section that "level 0 can't be masked", and in the next article, we'll look at CMSIS one by one, and confront this line to see what level SysTick is set to.

The board-level CMake is also updated, `src/arch/arm_cortex_m3/time_kernel_cm3.cpp` is added to the source file list of the firmware target, and we can match it in `src/board/stm32f103_bluepill/CMakeLists.txt`.

## Try the Effect?

On the board, we cross-compile the firmware:

```text
   FLASH:         768 B        64 KB      1.17%
     RAM:          44 B        20 KB      0.21%
   text	   data	    bss	    dec	    hex
    752	     16	     28	    796	    31c
```

The time kernel and demo are fully loaded into the firmware, and the flash only uses 768 bytes, and the RAM only takes 44 bytes, and most of the runtime state is in the 28 bytes of bss, because the global objects in the kernel are `constinit`, and the zero-initialized things don't take up flash. The intuitive cost we mentioned in the previous experiment has now been given a number here.

Then we go to Renode, we run the simulation for 6 seconds, and the output of the serial port window is as follows:

```text
[INFO] usart1: [host: 0.15s (+1.03ms)|virt: 0s (+0s)] ZerOS time demo: SysTick -> TimeKernel @ Blue Pill
[INFO] usart1: [host:  1.11s (+0.96s)|virt: 1s (+1s)] tick 1
[INFO] usart1: [host:  2.11s (+1s)|virt: 2s (+1s)] tick 2
[INFO] usart1: [host:  3.11s (+1s)|virt: 3s (+1s)] tick 3
[INFO] usart1: [host:  4.11s (+1s)|virt: 4s (+1s)] tick 4
[INFO] usart1: [host:  5.11s (+1s)|virt: 5s (+1s)] tick 5
[INFO] usart1: [host:  6.11s (+1s)|virt: 6s (+1s)] tick 6
```

You can see the "virt" column, the simulation prints one line of tick every time it completes a second, and this is the rhythm we want for the acceptance. To be honest, my local Renode 1.17.0 doesn't recognize the `bluepill.repl`'s `I2C.STM32F4_I2C` peripheral type, and the `EventInterrupt`, `ErrorInterrupt` connections on the two i2c nodes also report errors with it. We comment out the two i2c nodes, and the simulation starts. The exti, gpio connections in this version are not changed, and they don't report errors. The demo's SysTick and UART1 don't go through i2c, and their behavior is not affected. If you also encounter the same error in this version, comment out the two i2c nodes in the same way.
