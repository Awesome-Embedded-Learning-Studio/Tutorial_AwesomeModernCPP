---
title: "Wake Everyone Up, Recheck the Truth Individually: Event Groups, Direct Notifications, and Full Ring Backpressure"
description: "92a9aae(feat: event group, mailbox, sync wait) wraps up the synchronization family: EventBase uses a uint32 bitmap and an intrusive wait chain, the wait mask supports ANY and AND reading, set() sets the bit and broadcasts to wake up all waiters without judging who's condition is met. The task control block embeds a single-slot mailbox, notify overwrites writes, the latest value wins, and the target sleeps on whichever primitive it's waiting on. The queue extracts the never-sleeping post() and backpressure post_for(), sending the sender to sleep until there's a slot available. I ran all 14 host test targets on the /tmp copy, Renode presses e and f to demonstrate dual-key AND mutual lock, presses n to see the timer directly deliver messages in the tick context, and shrinks the event task's stack back to 64 words to reproduce a live catch, the canary report shows eaten=6, the new stack depth of 96 words is measured this way"
chapter: 6
order: 2
tags:
  - stm32f1
  - advanced
  - 嵌入式
  - 并发
  - cpp-modern
difficulty: advanced
platform: stm32f1
cpp_standard: [23]
reading_time_minutes: 22
prerequisites:
  - "Events come early but must not be lost: Counting Semaphores and Eight Operations of SystemContext"
  - "Interrupts only send notes: Ring Buffers, Worker Threads, and Bottom Halves Deferred"
related:
  - "Count to Ten and Switch: Peer Time Slices and Non-Inherited Slice Debts"
  - "Before the chip dies, it leaves a will: HardFault context, stack canary, and zero allocation log"
current_status:
  title: Recheck the Truth Individually
  detail: The ZerOS chapter is being polished
  variant: polishing
  icon: 📣
translation:
  source: documents/vol8-domains/embedded/zeros/06-fairness-and-closure/02-event-group-and-notify.md
  source_hash: d26540d1ba9eb5aebbc3bd73318a7a9fd7d1aff72f8bec1fafc0e73e3a1bdc8b
  translated_at: '2026-11-22T13:52:35+00:00'
  engine: qwen3:14b
  token_count: 18885
---
# Wake Everyone Up, Recheck the Truth Individually: Event Groups, Direct Notifications, and Full Ring Backpressure

The previous article completed the fairness of the scheduler, and our synchronization family still needs the final breath. Semaphores count how many times an event has occurred, mutexes determine who is currently allowed, and queues send messages byte by byte, but they all wait for only one condition. When we want to express "all three materials are ready before starting", with only semaphores, we have to use three semaphores and wait for each one in a series of three sleep cycles. Another category of requests is even more straightforward: I just want to pass a number to a task without creating a kernel object for it. These requirements are covered by the same commit, `92a9aae`, with the subject "feat: event group, mailbox, sync wait". I counted the diff, and it affected 21 files, adding 752 lines and removing 8 lines. Event groups, direct notifications, and full ring backpressure were all completed in one go.

This article is the final piece of the entire synchronization line. When you look back after reading it, you'll find that each primitive in the family wakes people up in the same way: the publisher does not judge who's condition is met, but wakes up everyone who might be relevant, and each person who wakes up rechecks their own truth. If they find out they're not ready yet, they go back to sleep. This rule is followed by semaphores, mutexes, queues, and event groups, and the new direct notification also follows it. We consider this as the concluding statement of this site. Every time we introduce a new primitive in the body, you will see this rule again.

## Two More Verbs Added to the Seam

Before looking at the primitives, we follow the convention and see where the seam has moved. `SystemContext` is the list of requirements that synchronization primitives recognize from the system. In the semaphore article, we counted its verbs, and later the mutex added `prio` and `reprioritize`, and the timer article added `arm_timer` and `cancel_timer`. In `92a9aae`, the list has two more verbs. Let's look at the changes in `include/ZerOS/kernel/system_concept.hpp`:

```cpp
{ s.notify(t, 42u) } -> std::same_as<bool>;                    // mailbox drop
{ s.wait_notify(span) } -> std::same_as<std::expected<std::uint32_t, sync::SyncError>>;
```

`notify` is responsible for delivering, it puts a 32-bit value into the mailbox of the specified task, and returns true upon success. `wait_notify` is responsible for receiving, the current task waits for its own message, returns the value upon receiving, and returns `SyncError::TimedOut` if it doesn't receive anything. The concept has expanded the parameters, and all six test files' `FakeSys` proxies have been upgraded accordingly, each proxy has implemented this pair of verbs, and missing any one of them would fail to compile. This is the evolutionary pattern we've seen in the mutex article, every time the concept expands the parameters, the compiler helps us count all the implementations.

## Event Groups: A Bitmap and a Wait Chain

The main character appears, we open `include/ZerOS/kernel/sync/event_group.hpp`, it's only 92 lines long, and it's one of the main dishes of this article. The event group has only two members:

```cpp
    uint32_t bits_{};
    base::SelfList<EventWaiter> waiters_{};
```

Let's look at the components of the two members: `bits_` is a 32-bit event bitmap, each bit is a light that can be turned on or off. `waiters_` is an intrusive wait chain, tasks waiting for events hang a lightweight node on it, and the node only records a pointer to a task:

```cpp
    struct EventWaiter : base::SelfNode<EventWaiter> {
        constexpr explicit EventWaiter(sched::TCB* task) noexcept : task_(task) {}
        sched::TCB* task_;
    };
```

There are two overloads for the wait interface, we read the permanent version in full, it's the key to understanding event groups. Before reading it, we establish the task of `wait`: the mask is held by the caller, and it waits until the bitmap is satisfied. A naive approach would be to check the bitmap once and return failure if it's not satisfied, and the caller would have to spin and retry themselves. However, if checking and sleeping are split into two steps, there might be a gap between checking and sleeping, during which someone else might have turned the light on, and the sleep would have been too long. Therefore, the code puts "check, link, sleep" into a loop, and the actions of checking and linking are still within the same critical section. The comment's "one lock — no gap" refers to this gap not being allowed to exist:

```cpp
    std::expected<uint32_t, SyncError> wait(uint32_t mask, bool all_of) noexcept {
        auto& system = System::self();

        while (1) {
            irq::CriticalGuard g{system};
            if (auto hit = satisfied_or(mask, all_of); hit.has_value()) {
                return hit;
            }
            EventWaiter me{system.current_task().get()};
            waiters_.insert(&me);
            system.block(system.current_task()); // park + sleep, one lock — no gap
        }
    }
```

Let's clarify the components of `mask`: it's the set of bits the caller is interested in, `all_of` determines how the mask is read. The judgment of the reading method is in the private `satisfied_or`:

```cpp
    [[nodiscard]] std::expected<uint32_t, SyncError> satisfied_or(uint32_t mask,
                                                                  bool all_of) const noexcept {
        const uint32_t hit = bits_ & mask;
        if (hit != 0 && (!all_of || hit == mask)) {
            return hit;
        }
        return std::unexpected(SyncError::TimedOut); // "not satisfied yet"
    }
```

`bits_ & mask` retrieves the bits we are interested in. When `all_of` is false, the result retrieved is considered satisfied if any one bit is on, this is the ANY reading method, waiting for "any event to happen". When `all_of` is true, the result retrieved must be exactly equal to the mask, and it's not acceptable if any bit is missing, this is the AND reading method, waiting for "all materials to be ready". The comment also clarifies the meaning of returning `std::unexpected`, it only represents "not yet satisfied" here, the two situations of timeout and not satisfied share the same channel in the return type, and the loop of waiting depends on it to decide whether to continue sleeping.

Note the shape of the loop: after waking up, it starts checking from the top of the loop again, and the memory from the previous loop is not carried over. The return type is `uint32_t` instead of a boolean, it returns the actual bits that are on, and when multiple conditions are satisfied at the same time, the caller can see which ones are satisfied.

### Events are States, Not Consumptions

Semaphores use a consumption semantics, the count is reduced by one each time it's taken. Event groups are different, they use a level semantics: the bit is on and stays on, the same bit can be waited on repeatedly before `clear` is called. The warehouse has provided a dedicated test, the name of the test case says it all, we record it as follows: `events are states, not consumptions: same bit waits twice`:

```cpp
    e.set(0b0101);
    auto first = e.wait(0b0101, true, Milliseconds{0});
    auto second = e.wait(0b0101, true, Milliseconds{0}); // nobody set anything again
    REQUIRE(first.has_value());
    REQUIRE(second.has_value()); // still lit — no take-one-lose-one here
    CHECK(*first == 0b0101);
    CHECK(*second == 0b0101);
```

No one calls `set` afterwards, the second wait still gets `0b0011`. We won't read the timer version overload in full, its shape is the same as the semaphore's `try_acquire`: it calculates the deadline, each loop checks once, and if the remaining time is zero, it removes its own node and returns timeout, otherwise it sleeps with the remaining time.

## set() Broadcast: The Publisher Does Not Judge, the Awakened Recheck Their Own Truth

The code for `set` is short, but it's the source of the title of this article, we read it in full, and while reading it, we also establish the trade-offs in the design. Its name is broadcast. When a bit is turned on, there may be more than one person waiting for it, `set` should wake up who? A naive approach would have `set` make the judgment for the waiting people, only waking up those whose masks are already satisfied, which seems to save a few unnecessary runs, but in that case, `set` would have to understand each person's waiting condition, and each node on the wait chain would have to carry its own mask, and the two primitives would be intertwined internally. The code chooses broadcast, the person who sets the bit doesn't judge anyone, and wakes up everyone on the wait chain:

```cpp
    // Light bits. TASK/ISR safe. Wakes EVERY waiter — broadcast: the setter
    // never judges who is satisfied; each woken one re-checks its own mask
    // at the loop head (a spurious wake-up costs one loop turn, nothing more).
    void set(uint32_t bits) noexcept {
        auto& system = System::self();
        irq::CriticalGuard g{system};
        bits_ |= bits;
        while (auto w = waiters_.pop_head()) {
            system.ready(w->task_);
        }
    }
```

We read the entire action of `set`: `set` turns on the bit, and then wakes up everyone on the wait chain and puts them back into the ready queue. The trade-off is realized in the code, the judgment is given back to the waiters themselves, `set` only handles the task of turning on the bit and waking people up, whoever's condition is satisfied, they have to recheck their own truth when they return to the top of the `wait` loop. The cost is the addition of spurious wake-ups, people who are awakened but whose conditions are not satisfied spin through the loop once and go back to sleep, the comment gives this cost a price: "a spurious wake-up costs one loop turn, nothing more", just one loop, and the interface is that the setter has zero knowledge of the waiters.

The action of OR-ing the `bits` into the bitmap occurs within a critical section, so this function can be called by both tasks and interrupts, the sentence we see at the beginning of the comment, "TASK/ISR safe", refers to it.

We've actually seen this shape before. The `release` of the semaphore, after adding the count, says in the comment, "woken sleepers race for it (loser re-sleeps, that is POSIX-semantics)", the loser re-sleeps. The `lock` of the mutex rechecks `owner_` after waking up from the loop, and if the transfer hasn't happened, it requeues. The waiting on the queue and the awakening of the previous sleep also follow the same path. Here we can complete the statement: broadcast wake-up, and after waking up, each person rechecks their own truth. It's not written in any header file, it's an invariant that spans semaphores, mutexes, queues, workers, timers, event groups, and direct notifications. Workers sleep on the wait of semaphores, timer callbacks run in the tick context, and after they are awakened, they also have to recheck their own truth. Each primitive's implementation details are different, but the division of labor between waking people up and checking the truth is the same skeleton. The fulfillment of the concluding statement is here, when you write your own synchronization primitives in the future, you might want to take this as your first self-check.

## Direct Notification: A Single-Slot Mailbox Embedded in the Task

The second main character's scenario is as follows: we just want to pass a number to a certain task, but we don't want to create a semaphore, a queue, or take up a chunk of RAM for it. The Blue Pill (C8T6) has only 20KB of RAM, and each kernel object saved is one less. ZerOS's answer is to directly embed the mailbox into the task control block, we look at the two new fields added to the TCB in `task_control_block.hpp`:

```cpp
    // the task's private mailbox: direct notifications, no kernel object.
    // Single slot, LATEST value wins (overwrite) — see Scheduler::notify.
    std::uint32_t notify_{};
    bool has_notify_{};
```

The mailbox has only one slot, a 32-bit value plus a flag indicating "there is a message", totaling 5 bytes. The implementation of the delivery is in the scheduler, we read it in full, and before reading it, we establish the trade-off of the single slot: delivery does not queue, new messages are directly overwritten on old ones, and the latest value wins.

A naive idea is to make the mailbox a small queue for each task, where messages are not lost, but the queue is on each TCB, and this board has only 20KB of RAM. What we want is precisely not to create a kernel object for passing a single value. Another difficulty we also need to establish before the code: the target may be sleeping on any primitive at the moment, the deliverer doesn't know and doesn't need to know where it's sleeping, we see how the code solves it:

```cpp
    // Direct-to-task mailbox drop. Task/ISR safe. Single slot, OVERWRITE:
    // the latest value wins. The target may be parked ANYWHERE (a semaphore,
    // a queue, a sleep) — the ready() below may wake it "by mistake", and
    // that is fine: every waiter family re-checks its own truth on waking
    // and simply goes back to sleep; the mailbox owner checks has_notify_.
    bool notify(base::BorrowedPtr<TCB> t, std::uint32_t value) noexcept {
        irq::CriticalGuard guard{driver_};
        if (!t || t == &idle_) {
            return false;
        }
        t->notify_ = value;
        t->has_notify_ = true;
        if (t->state_ == TaskState::Blocked) {
            ready(t); // nested guard; pends the switch, never switches here
        }
        return true;
    }
```

Delivery does the job of overwriting writes, the comment's "OVERWRITE, the latest value wins" states the semantics itself. The trade-off and the queue are complementary: we want a queue that doesn't lose anything, and we want the latest information with a notification. Scenarios like telemetry data and heartbeat counters, the few outdated values in the middle are not needed by anyone.

The real point is in the middle of the comment. The target task may be sleeping anywhere at the moment, sleeping on the wait of a semaphore, sleeping on the queue's queue, or sleeping on its own timer sleep, and `notify` only calls one `ready`. When the target is sleeping on a semaphore and the message is delivered into the mailbox, this wake-up is considered "calling the wrong person" by the semaphore world, but it doesn't matter, the family invariant we've extracted above catches it here: the target wakes up, rechecks the condition it fell asleep on, finds out that the required semaphore is still zero, and goes back to sleep. The mailbox owner checks one more step `has_notify_`, finds the message, and the task is done. Miswaking is not a design flaw, it's actually the premise of the entire design.

On the receiving side, we look at the probe in the scheduler. We establish its shape before reading the code: receiving is an atomic probe, if there's a message, the value is taken and the "there is a message" flag is cleared, if there's no message, it immediately returns a timeout error, it doesn't wait itself. The real "wait" is not in these few lines we see now, it's in the loop outside of these lines. As to why the loop is outside, the comment has its reason, we'll read it after:

```cpp
    // the current task reads its mailbox; taking the letter clears the flag.
    // ONE atomic probe only — the re-check loop lives in the system face,
    // whose sleep goes through the door the Mock can see (an inner
    // Scheduler-side loop would park via kernel-direct block and dodge it)
    std::expected<std::uint32_t, sync::SyncError> wait_notify() noexcept {
        irq::CriticalGuard guard{driver_};
        if (current_ != nullptr && current_->has_notify_) {
            current_->has_notify_ = false;
            return current_->notify_;
        }
        return std::unexpected(sync::SyncError::TimedOut);
    }
```

The message is read, the mailbox is empty, the same value cannot be read again. After the comment's "ONE atomic probe only", there's the most interesting trade-off in this commit: the rechecking loop is not placed in the scheduler, but in the system seam layer. The reason is written in the parentheses, if the kernel-side loop uses `block` to directly stop, the host test proxy can't see this sleep, the proxy's hook is bypassed, and the test can't arrange changes in the world at the moment the task is asleep. The loop is moved up to `CortexM3System`, the sleep goes through the seam's own `sleep_for`, and the proxy can see it. Testability thus forced the layering, this is the second time we've seen this in the semaphore article, and this is the second time we've seen it here. The implementation on the board in `src/arch/arm_cortex_m3/system.cpp` is a loop of probe and a short sleep:

```cpp
std::expected<std::uint32_t, sync::SyncError>
CortexM3System::wait_notify(clock::Ticks::tick_t span) {
    // re-check loop in the face: probe the mailbox, sleep through THIS
    // object's sleep_for (singletons stay private to the arm .cpp family)
    const auto ddl = system_time.current().tick_ + span;
    for (;;) {
        if (auto letter = system_sched.wait_notify(); letter.has_value()) {
            return letter;
        }
        const auto left =
            static_cast<clock::Ticks::tick_diff_t>(ddl - system_time.current().tick_);
        if (left <= 0) {
            return std::unexpected(sync::SyncError::TimedOut);
        }
        system_sched.sleep_for(system_sched.current_task(), system_time,
                               static_cast<clock::Ticks::tick_t>(left), wake_from_sleep);
    }
}
```

The entry point for the application is `ThisTask::wait_notify(timeout)`, we look at `src/system/this_task_impl.hpp`, it forwards the call to the backend, zero means only try once, a positive number means wait for up to that many milliseconds. `notify` therefore doesn't need to do any interrupt-safe work itself: the underlying `ready` only puts the task back into the ready queue, and in more urgent cases, it hangs the PendSV switch request, the agreement that the scheduler never does the switch itself in interrupts is a full-kernel invariant established on the day the scheduler was born, and the benefit is inherited without cost.

## Full Ring Backpressure: The Sender Sleeps Until There's a Slot Available

The queue side also had a change. Before this commit, `msg_queue.hpp` only had ISR-friendly `post`, and the ring was full directly rejected. It's appropriate for interrupts to use it, because the processing code in interrupts can't sleep. However, when tasks communicate with each other, if the consumer is temporarily slow, the producer treats "rejected" as the norm, and the data is really lost. This commit added backpressure sending for the task side, we look at the two semaphores of the queue:

```cpp
    SemaphoreBase<System> semaphore{0};       // units: "there is data"
    SemaphoreBase<System> spaces_{capacity};  // slots: "there is room"
```

One semaphore counts "data available", the initial value is zero, the receiver waits on it. The other counts "space available", the initial value is the capacity, the sender waits on it. The comment writes the semantics of the two counters at the end of the line. We read the new version of the send in full:

```cpp
    // Task-side post with back-pressure: on a full ring, SLEEP until a slot
    // frees or the time runs out. Never call this from an interrupt.
    QueueError post_for(const Message& m, clock::Milliseconds timeout) noexcept {
        if (spaces_.try_acquire(timeout) != SyncError::Ok) {
            return timeout.count == 0 ? QueueError::Full : QueueError::Timeout;
        }
        auto& sys = System::self();
        ZerOS::irq::CriticalGuard g{sys};
        ring_[(head_ + count_) & (capacity - 1)] = m; // slot is already reserved
        ++count_;
        semaphore.release(); // the unit is handed out only AFTER the data landed
        return QueueError::Ok;
    }
```

We split the process into three steps: the action of getting a slot from "space available" comes first, only after getting a slot do we move the ring, and only after the data is written do we allow one "data available" to be released. This step of getting a slot will sleep, and when the ring is full, `try_acquire` sleeps on `spaces_` until someone frees a slot, the comment's "SLEEP until a slot frees" is the backpressure itself: the producer slows down, rather than the data being lost. The action of freeing a slot is in `pop_ring`, it takes a message out, and then calls `spaces_.release()`, the comment says "a slot just freed — sleeping senders may take it", sleeping senders are awakened according to the family invariant, each rechecks, and those who get the slot proceed, those who don't get it continue to sleep.

The old version of `post` retains its original behavior: no sleep, returns `Full` when the ring is full, the implementation is changed to forward to `post_for` with zero timeout as an equivalent write. The comment also has a line that clearly draws the boundary, "the ONLY post an ISR may call". There's a small naming issue I record for you as is: the constant name used for zero timeout is `TRY_RECEIVE_TIME`, a sending path reused the constant name of the receiving side, the behavior is correct, the name is a bit off. I've left it as material in the body, when we read the code, who decides between behavior and naming, this is a ready example.

The same commit also moved the `SyncError` enum from `semaphore.hpp` into the new `sync_error.hpp`, a small file of 11 lines, we read its header comment, the reason for the move is written inside, "the shared verdict of the synchronization family (and of anything that sleeps with a deadline — hence its own dependency-free home)", shared verdict with its own home. The implementation of the event group needs to include it, the notification path also needs it, the semaphore has been using it for a long time, no one should include the semaphore header for a single enum, a dependency-free shared declaration with its own header file, the family's common vocabulary now has its own household registration.

## Host Exam: Fourteen Targets and One Confession

The mechanism is explained, we go to the exam. I set up a host build in the `/tmp` copy (`git archive 92a9aae` exported, with CMSIS headers added), my local GCC is 16.2.1, the build command is still our three lines:

```shell
cmake -B build-host -DZEROS_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host -j
ctest --test-dir build-host --output-on-failure
```

The result we ran is that all 14 test targets passed, the total time only took 0.36 seconds:

```text
100% tests passed out of 14

Total Test time (real) =   0.36 sec
```

The individual scores of the three new test files, I read them one by one, the event's score is seven test cases and twenty-four assertions, the notification's is five test cases and nineteen assertions, the queue's is ten test cases and eighty-two assertions:

```text
All tests passed (24 assertions in 7 test cases)
All tests passed (19 assertions in 5 test cases)
All tests passed (82 assertions in 10 test cases)
```

We pick a few test cases to see how they correspond to the body. There is a dedicated test case for the AND semantics, its name is "AND stays asleep until the WHOLE mask is lit", it doesn't return when only half of the mask is lit, it gets `0b0011` after the second bit is lit. The "latest value wins" of the notification also has a dedicated test, two values are sent in a row but only one is taken, the value read is 2:

```cpp
    REQUIRE(FakeSys::sched_->notify(&w.task, 1));
    REQUIRE(FakeSys::sched_->notify(&w.task, 2)); // overwrites — by design
    auto got = w.wait_notify(Milliseconds{0});
    REQUIRE(got.has_value());
    CHECK(*got == 2);
```

The core test case for backpressure is "post_for sleeps until a receive frees a slot (back pressure)": we fill the ring, the sender sleeps in the call to `post_for`, the proxy's hook frees a slot while it's sleeping, the sender wakes up and rechecks, gets the slot and returns `Ok`. The rechecking path for spurious wake-ups is really run through in the test.

The confession part I give you as is, the end of `test_event.cpp` has a sentence written by the author himself:

```cpp
// Multi-waiter broadcast (two tasks waking from ONE set) cannot park two
// waiters on this single-threaded Mock — left to the Renode demo / silicon.
```

Two tasks wake up from the same `set` broadcast, this scenario can't be demonstrated on a single-threaded proxy, the proxy can only serve one person at a time. The author clearly left this path for simulation, we'll go to Renode next, but I need to say it upfront: the demo has only one waiting task on this event chain, we can see the broadcast of a single waiting task, a strict multi-waiter broadcast hasn't been really run on either host or Renode, we'll be responsible for this in the verification section.

## Renode Theater: Dual Keys and Three Relays

The board's work was done in the same `/tmp` copy, the cross-toolchain used is arm-none-eabi-g++ 16.2.0, the firmware size is real:

```text
   FLASH:        7788 B        64 KB     11.88%
             RAM:        4480 B        20 KB     21.88%
   text	   data	    bss	    dec	    hex
    7112	     676	   3800	  11588	   2d44
```

RAM used 4480 bytes, the main part is the receive queue, several new task stacks, and a longer wait chain, FLASH also exceeded the 6KB budget line, the cost of expanding the service is clearly shown in the volume table, I report the numbers as is. Renode 1.17.0 can't recognize repl's `I2C.STM32F4_I2C`, I deleted i2c1 and i2c2 in full, the demo path doesn't go through i2c, the behavior is unaffected, the handling is consistent with our old example. The injection of keys goes through Renode's `sysbus.usart1 WriteLine`, the serial output is written into files like run-keys.log, each line below is a real output.

### Press e and f: The Mutual Lock of Two Keys

The `e` and `f` in the demo are a double-key lock. We pressed `e`, it hatched the waiting task, its waiting mask is `0b0011`, AND reading, the timeout is given enough 5 seconds, then `e` itself lit the 0th bit:

```cpp
constinit ZerOS::sync::Event g_keys{};
alignas(8) constinit std::uint32_t stack_evt[96] = {}; // wait chain + format buffer
                                                       // ate 6 canary words at 64 — caught live

void key_waiter(void*) {
    auto got = g_keys.wait(0b0011, true, Milliseconds{5000});
    if (got.has_value()) {
        ZerOS::board::format("evt: both keys in (bits=0x{})\r\n", ZerOS::log::Hex{*got});
    } else {
        ZerOS::board::print("evt: nobody pressed the second key\r\n");
    }
    ThisTask::block(); // self-retire
}
```

The real output from the serial window, we cut it into two lines per pair:

```text
[INFO] usart1: [host:  1.42s (+4.55ms)|virt:    1.3s (+2.78ms)] c: waiter out, needs BOTH keys (got bit0)
[INFO] usart1: [host:   2.71s (+0.9ms)|virt:   2.3s (+16.62µs)] evt: both keys in (bits=0x00000003)
```

The first line is the dialogue of task `c`, it's the task in the demo that reads the key, it reported that the waiting task has taken the stage and got the 0th bit. The waiting task was hung on the wait chain at virtual time 1.3 seconds, at this time the 0th bit was on, the 1st bit was still off, `satisfied_or` checked once, `hit != mask`, the AND condition wasn't met, it went back to sleep. My action of pressing `f` was recorded on the serial port around virtual time 2.3 seconds, `set(1u << 1)` lit the 1st bit, the broadcast woke up the waiting task, it returned to the top of the loop to recheck, `hit == mask`, returned `0x00000003`. The 8 hexadecimal digits in the print are zero-padded by the formatter according to the `Hex` width, the value is the mask `0b0011`. Only pressing half the keys doesn't let it out, after 5 seconds, it reports "nobody pressed the second key", this branch we didn't hit, it's left for you to try yourself.

### Press n: The Timer Directly Delivers Messages in the Tick Context

The three-part relay of the `n` key is a joint performance of the three primitives in the same script, we look at the source code's wiring before reading the output:

```cpp
void mail_filler(void*) { // runs in TICK context — notify is safe there
    ZerOS::system::os().notify(g_nt, 42);
}

void mailbox_task(void*) {
    auto letter = ThisTask::wait_notify(Milliseconds{1000});
    if (letter.has_value()) {
        ZerOS::board::format("nt: letter says {}\r\n", ZerOS::log::Dec{*letter});
    } else {
        ZerOS::board::print("nt: mailbox stayed empty\r\n");
    }
    ThisTask::block(); // self-retire
}
```

After pressing `n`, task `c` hatched a receiving task, it called `wait_notify` and stopped on its mailbox, the waiting time limit is 1000 milliseconds. `c` then hung a single-time timer of 300 milliseconds, the callback is the `mail_filler` we just saw. When the 300-millisecond deadline is reached, the kernel internally calls the callback in the SysTick interrupt, it runs in the tick context, one sentence delivers the message into the mailbox of task `g_nt`, the value of the message is 42. `notify` is safe in interrupts, we confirmed all its paths in the direct notification section. The real serial output is two lines:

```text
[INFO] usart1: [host:  3.42s (+7.26ms)|virt:   3.31s (+7.02ms)] c: mailbox task out, timer writes in 300ms
[INFO] usart1: [host:  3.72s (+7.24ms)|virt:      3.61s (+7ms)] nt: letter says 42
```

We read along the virtual timeline, the two lines are exactly 300 milliseconds apart, the message arrived at the right time. This script has no part with semaphores or queues, a `uint32_t` directly falls from the timer callback into the mailbox of the receiving task, the three-part relay each does their own job: the task only waits for the message, the timer only does the job of firing at the right time, `notify` only does the job of delivering. It's also a sample of how the tick context can directly deliver data to a task, if you want to deliver a message in an interrupt, you can follow this writing style.

## Canary Live Catch: Six Words Force Out 96

The stack guard from the previous station is still on duty in the demo, the patrol is still on the required path of `context_switch`. It has another achievement in this station, the merit is written in the end-of-line comment of `stack_evt`: "wait chain + format buffer ate 6 canary words at 64 — caught live", the wait chain plus the format buffer ate 6 canary words on the 64-word stack, caught live on the spot. This sentence is the result of the reproduction, the process we can go through ourselves, I shrank `stack_evt` from 96 words back to 64 words in the `/tmp` copy, rebuilt the cross-compile, and pressed `e` and `f` again:

```text
[INFO] usart1: [host:  3.43s (+13.5ms)|virt:   3.31s (+13.24ms)] evt: both keys in (bits=0x00000003)
[INFO] usart1: [host:   3.43s (+0.3ms)|virt:    3.31s (+1.01µs)] [STACK OVERFLOW] task=evt eaten=6
```

The overflow report appeared 1 microsecond after the "both keys in" line, `eaten=6`, the number of canary words eaten matches exactly with the number written in the comment. The reproduction script is worth our reflection: the overflow itself didn't trigger any exception, the waiting task printed the good news of both keys, the program looked normal, until the next context switch, the patrol checked the bottom of the stack, the 6 sentinel words turned into 0, the overflow showed its form. The number 96 for stack depth is measured this way, 64 real words used plus 6 words of overstepping, plus a bit of margin, it's not a guess. The canary patrol can only detect it, but it can't save it, after the report, the demo stops on the spot with `wfi`, this is the old handling set in the previous station, and it hasn't changed in this station.

## Verification

Let's summarize the verifiable facts of this article, one by one.

The first is the host exam. You checkout to `92a9aae`, set up the build with the three commands above, all 14 test targets passed, the three new scorecards for events, notifications, and queues are respectively seven test cases and twenty-four assertions for events, five test cases and nineteen assertions for notifications, ten test cases and eighty-two assertions for queues. Three core behaviors have their own dedicated tests: the half mask of AND doesn't go out, the latest value overwrites the old value, and the full ring sends sleep until a slot is freed. The hooks of the proxy act in the world at the moment the task is asleep, and the rechecking path is in the production code.

The second is the board theater. The firmware size is 7788 bytes of FLASH and 4480 bytes of RAM, the output of the dual keys is at virtual time 2.3 seconds, `bits=0x00000003`, the output of the three-part relay is at virtual time 3.31 and 3.61 seconds, with a 300-millisecond interval, the value of the message is 42. When you reproduce it, remember to delete the two i2c blocks in the repl, the log uses the command `renode --console --disable-xwt -e "logFile @run-keys.log" -e '$bin=@build/src/board/stm32f103_bluepill/zeros-bluepill.elf' -e "include @src/board/stm32f103_bluepill/sim/renode/bluepill.resc" -e "start"` to start, the keys are fed into the serial port in the shape of `sysbus.usart1 WriteLine "e"`, e, f, n are fed in one second apart after `start`, the output falls into the run-keys.log we left for it.

The third is unverified, I report it as is. The broadcast of multiple waiters, two tasks waking up from the same `set` broadcast, this path is not available in the proxy, the board's reality also can't reach it, the source code reading sees the broadcast loop itself, but it hasn't really run two waiters in any environment. In the error mapping of `post_for`, when the timeout is zero, it returns `Full`, when the timeout is non-zero, it returns `Timeout`, the behavior is covered by dedicated tests, the reuse of the constant name `TRY_RECEIVE_TIME` is the real situation in the source code, I report it as is. The interrupt safety of the event group broadcast, the comment claims TASK/ISR safe, the existence of the critical section supports its claim, but the `set` in the board demo all happen in the task context, the path of `set` in the ISR hasn't been tested in reality.

## Next Article

The synchronization family is complete with this article, we now have semaphores, mutexes, queues, workers, timers, event groups, and direct notifications, seven primitives in a row, the family invariant is their shared foundation. The sister article "Count to Ten and Switch: Peer Time Slices and Non-Inherited Slice Debts" runs in parallel with this article, it handles the fairness of the scheduler, and this article handles the convergence of the synchronization side, the two together make the name "Fairness and Convergence" of this site stand. In the future, examples will be split into individual example projects, and CI will be integrated into our process, those are the next steps, we'll see you there.
