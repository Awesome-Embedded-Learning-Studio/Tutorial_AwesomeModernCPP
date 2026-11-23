---
title: "Switching After Ten Counts: Equal Time Slices and Non-Inherited Slice Debt"
description: "1006648(feat: scheduler finished) Adding fairness to the scheduler: on_tick counts ticks in the critical section for the current task, kSliceTicks=10, when the count is full, the current task is pushed back to the tail of the same-level queue and a context switch is requested. If there are no same-level tasks waiting, no idle context switch is performed. pick_next resets the slice count so that the new task does not inherit the remaining share from the previous one. I ran three new unit tests on the /tmp copy, all 14 assertions in the slice test case passed. Renode, when pressed 'r', spawns two pure spinners at the same level, rr1 and rr2 alternate in the serial port window every 500 milliseconds. The interleaved output is itself a feature."
chapter: 6
order: 1
tags:
  - stm32f1
  - advanced
  - embedded
  - cpp-modern
difficulty: advanced
platform: stm32f1
cpp_standard: [23]
reading_time_minutes: 20
prerequisites:
  - "Who is the Most Urgent: Ready Bitmap, Task States, and Selection Arbitration"
  - "M's Three Seconds of Silence: Mutex, Priority Inheritance, and Ownership Transfer"
related:
  - "Forging a Birth Certificate at the Stack Top: PendSV, SVC, and the First Switch"
  - "Interrupts Only Submit Tickets: Circular Queue, Worker Threads, and Bottom-Half Delay"
current_status:
  title: Switching After Ten Counts
  detail: The ZerOS chapter is currently being polished
  variant: polishing
  icon: 🤝
translation:
  source: documents/vol8-domains/embedded/zeros/06-fairness-and-closure/01-time-slice-round-robin.md
  source_hash: 0622dfeb65aba89704740590b4c400ec78d3a9c7706ec3b72fff791c937076c5
  translated_at: '2026-11-22T13:50:56+00:00'
  engine: qwen3:14b
  token_count: 14227
---
# Switching After Ten Counts: Equal Time Slices and Non-Inherited Slice Debt

We've reviewed the service face ourselves (the previous index called it the economics of the service face), and found two old debts waiting for us to repay. In the debt of the synchronization family, our semaphores, mutexes, and queues only wait for a single condition, while multi-party waiting and the fastest path are still empty. This part belongs to the next article of this site. The scheduler's debt seems to not exist: we've covered the arbitration face, and the switching mechanism face, you can review the acceptance of the two articles, the selection is correct, and the switching is also possible, you seem to find no flaws. What is missing is actually hidden in the same level, we will dig it out today and see how it was repaid.

Actually, one of the service face's old debts has already been collected: the task that needs to see the cause of the crash belongs to the will report of "Before the Chip Dies: HardFault Scene, Stack Canary, and Zero Allocation Log", and the unloading of heavy work in interrupts belongs to the worker of "Interrupts Only Submit Tickets: Circular Queue, Worker Threads, and Bottom-Half Delay". So the remaining two debts of this article, one belongs to the scheduler, and the other belongs to the next article of the synchronization family, we will collect one by one.

The subject of `1006648` is "feat: scheduler finished". I counted its diff, the changes are in four files, adding 146 lines and deleting 1 line. Although the number of lines is not many, the tone is quite bold. The scheduler only dares to call itself finished in its subject when it reaches this point. Why does the scheduler still owe a leg even after the priority preemption has run for five stations? Let's look at the debt clearly and see how it was repaid.

## Debt: No One in the Same Level Cares About Fairness

The debt starts from a test in the arbitration article. Do you remember the test case in `test_sched.cpp`, which we call "same level round-robins via yield-to-tail": three people of the same level gather in one level, whoever calls `yield`, is pushed to the tail of the same level queue, and the next one will be the next. It was green at that time, but if we look at the script again, the original driving force of the rotation is the task itself calling `yield`.

Now let's assume that the task on stage does not yield. A pure spinning task, its function body does not find a `sleep_for`, nor does it find a shadow of `block`, not even a word of `yield`, it is a dead loop that never turns back. After it comes on stage, `pick_next` can't invite anyone: it is in the `Running` state, the same-level partners are lined up in FIFO, but the scheduler has no opportunity to interrupt it. Other bits on the bitmap are not lit up because the starved partners are on the same level. I tried this script in Renode and on the scheduler before `1006648`: two such tasks are released, only one name flashes on the serial port window, and the other does not appear at all from beginning to end. It is alive, waiting in the queue, but it is not its turn to appear.

This is the content of the debt. Priority is the main axis of the scheduler, the smaller number suppresses the larger number, everything we have built in the five stations is based on it, but the allocation within the same level, the scheduler at that time only relied on the task's own yield. Whether to yield or not is the task's self-awareness, the kernel handed over fairness to self-awareness. Self-awareness is unreliable, what is reliable is the clock: SysTick hits a beat every millisecond, it has the obligation to host the rotation between the same level. `1006648` does exactly this, let's see how it hosts.

## on_tick: Count to Ten, Push to the Tail, Request a Switch

The kernel's approach is embodied in a new member function `on_tick` of the `Scheduler`. Before we read its code, let's establish its identity and duties: the beat of SysTick hits once every millisecond, and the processing function for each beat must call `on_tick` once. It counts the beats for the current task on stage, and when it reaches ten beats, it pushes this person back to the tail of the same-level queue and sends out a switch request. Self-awareness rotation is unreliable, this was judged in the previous section, from here on, the original driving force of the rotation has been replaced by the clock, whether the task yields or not is its own business, when the slice is up, the kernel itself takes action. The first half of the function is responsible for counting the beats, let's see it:

```cpp
    void on_tick() {
        irq::CriticalGuard g{driver_};
        if (!current_ || current_ == &idle_) {
            return;
        }
        // Ticks Served
        ++ticks_served_;

        if (ticks_served_ < kSliceTicks) {
            return; // OK, back
        }
```

The function immediately grabs the critical section, we are familiar with this writing style: `on_tick` is to be called from the interrupt context of SysTick, it modifies `ticks_served_`, the bitmap, and FIFO, which are shared with the task side, and without locking, it will eventually be written incorrectly. The first judgment after entering the function lets idle go: if idle is running on the CPU, it means there are no real tasks awake, and rotation is not possible, so counting beats is not needed. `ticks_served_` is a new member added in this commit, an integer of type `std::uint32_t`, it is the number of beats the current task has already used in this slice, it increases by one every beat, and if it hasn't reached `kSliceTicks`, the function does nothing.

Let's look at the declaration of `kSliceTicks` separately, it's actually only one line:

```cpp
    static constexpr clock::Ticks::tick_t kSliceTicks = 10;
```

The unit of the constant is beats. In the demo, `init_time(SystemCoreClock / 1000)` sets the beat to 1kHz, one beat takes one millisecond, so ten beats is a slice, a time slice on the board is 10 milliseconds long. The number 10 written in the code is unitless, the unit is explained by the name of the constant `kSliceTicks`. I see this as a small example of naming: when the number itself cannot explain, the name explains it for it.

Let's continue to look at the latter half after the beats are full, the three actions where fairness truly lands:

```cpp
        ticks_served_ = 0; // reset, time to sched
        if (fifo_[current_->task_priority_].empty()) {
            return; // Dont dry run self
        }

        push_back(current_);
        current_->state_ = TaskState::Ready;
        driver_.request_switch(); // Ask For a switch, wait PendSV
    }
```

The line that zeros and the judgment of the empty queue will be specifically addressed in the next section, here we finish reading the three lines that are allowed to proceed. `push_back(current_)` pushes the current task back to the tail of its level queue, it retreats from `Running` to `Ready`, the 10 milliseconds it just used is its share for this round, if it wants to run again, it has to rejoin the queue. After `state_` is changed to `Ready`, it becomes an ordinary ready task in the eyes of `pick_next`, there is a detail worth looking at here, we will expand it in the next section. `request_switch()` requests a switch, and our line of thinking has not broken it: context switches are never directly performed in interrupt service routines, it only writes a 1 to the PendSV suspension bit, the real switch waits for the PendSV exception to happen itself.

Here we pick up the foreshadowing left in the article "Forging a Birth Certificate at the Stack Top". We return to the main text of that article: `start_scheduler` places PendSV and SysTick at the same `0xFF` in SHPR3, the question at that time was why the heartbeat of the time base was ranked with the switch, half of the answer was in the switching process of that article, now we complete the other half: `on_tick` requests a switch in the processing function of SysTick, and the one who takes over is PendSV. The two interrupts are at the same level, so PendSV will not interrupt and take over before SysTick has finished its beat, the beat is finished and the stage is left, the queued PendSV then starts the switch. If PendSV's level is more urgent than SysTick, there is a risk of being interrupted halfway through the slice judgment, the same-level arrangement of `0xFF` is finally completed in this article.

## No One is Waiting in the Same Level, No Idle Switching

The empty queue judgment skipped in the previous section is now addressed, it saves every switch:

```cpp
        ticks_served_ = 0; // reset, time to sched
        if (fifo_[current_->task_priority_].empty()) {
            return; // Dont dry run self
        }
```

After the beats are full, it asks: are there still people in this level's queue? We pay attention to the object of the question, which is `fifo_[current_->task_priority_]`, it is the queue of the current task's own level. The current task is in the `Running` state at this moment, it is not in the queue, so the queue has only its same-level partners. If there is no one in the queue, the function returns, the note says "Don't dry run self", which is a plain language, don't do meaningless idle switching. If there is only one same-level partner in the system, pushing it out and selecting it back would select it back again, paying the price of a context switch for nothing. The purpose of the slice rotation is to divide time among the same level, if there is no second person in the same level, there is no fairness to divide, and switching should not happen.

Another detail that is easy to overlook is hidden in the line before the `return`: after the beats are full, regardless of whether the switch is made or not, `ticks_served_` is zeroed. If the switch is made, it is easy to understand that the new on-stage person starts from zero. If the switch is not made, it means that the slice is used up and is discarded, the count starts again, a single running task reopens a slice every 10 milliseconds, it will not be mysteriously switched due to the accumulation of counts. The line of zeroing and the following judgment are on the same entrance, they have their own responsibilities: the empty queue judgment is guarded by the second test case. The zeroing of pick_next is guarded by the third test case. As for the zeroing in on_tick when the beats are full, it does not have a dedicated test, its effect is hidden in the readings of the first two test cases, we will see it in the test section.

## pick_next Resets the Count: Slice Debt is Not Inherited

The second place where the count is maintained is in `pick_next`, we have established the "slice debt", and the changes are easy to read: the number of beats used by the task in the current slice is its debt, the deeper the debt, the closer it is to the time to switch people. `pick_next` is the path that all on-stage tasks must go through, whether they are preempted, yielded, or awakened, they all receive the CPU from its hands. If it forgets to reset the count, a task that used 8 beats and was preempted by a higher priority task would have its count continue to increase in the hands of others, when it comes back, the remaining beats depend on luck, the time the person who preempted it occupied the CPU is all folded into its remaining debt. It should have a full slice, but it only gets a small part of it. More sinister is that this error is not visible: each task is still running, but it's running in a biased way, the bias is drifting with the timing of the preemption, and it appears on your board as an occasional starvation, with no ready clues to investigate. So the zeroing should be done at the step of switching people, we see its changes, it actually has only two lines:

```cpp
        current_ = next;
        ticks_served_ = 0; // a fresh pick is a fresh slice: no inheriting
                           // the previous runner's leftover share
        next->state_ = TaskState::Running;
```

The comment explains the intention, we take the focus: a fresh pick is a fresh slice, the newly selected person gets a new slice. The semantics of the count is "how many beats the current task has used in this slice", and "this slice" starts when someone comes on stage and ends when they leave, the only place where someone comes on stage is `pick_next`, and the reset should only happen there. The zeroing in `on_tick` manages "the slice is used up and discarded", and the zeroing in `pick_next` manages "the person changes the slice and also changes", the two zeroings, one guards the clean boundary, one guards the clean switch, the combined effect is that the count says the truth.

## Host Exam: Our Three Test Cases Each Guard One Invariant

After the mechanism is explained, we go to the exam. `test/test_sched.cpp` adds 68 lines in this commit, adding three new test cases, each test case guards one invariant, we look at them one by one.

We look at the first test case that guards "switching after the beats are full", its script:

```cpp
TEST_CASE("time slice: same level round-robins by the clock, not by yield", "[sched][slice]") {
    g_switch_requests = 0;
    ZerOS::sched::Scheduler<HostSwitchPort> s;
    Rig a(1, "a"), b(1, "b");

    s.add(&a.tcb);
    s.add(&b.tcb);
    REQUIRE(s.pick_next().get() == &a.tcb); // a runs, b waits at the same level

    for (int i = 0; i < 9; ++i) {
        s.on_tick(); // nine beats: the share is not spent yet
    }
    CHECK(g_switch_requests == 0);

    s.on_tick(); // the tenth beat: the slice is spent
    CHECK(g_switch_requests == 1);        // a switch is pended
    REQUIRE(s.pick_next().get() == &b.tcb); // and b takes the CPU
}
```

a, b are two tasks of the same level, we let a go on stage, b is waiting in the queue. Nine beats are hit, the count of the switch request is zero, the tenth beat is hit, the count increases to one, and then `pick_next` verifies that the baton is passed to b. The boundary is cleanly cut: nine beats are not enough, the tenth beat is when the person is switched, the semantics of `kSliceTicks` is thus pressed into the test by the number itself.

We continue to look at the test case "no idle switching when no one is waiting in the same level", there is only one same-level task a left:

```cpp
    for (int i = 0; i < 50; ++i) { // five whole slices with nobody waiting
        s.on_tick();
    }
    CHECK(g_switch_requests == 0); // no dry-run switching
    REQUIRE(s.pick_next().get() == &a.tcb);
```

Fifty beats are hit, we calculate ten milliseconds per slice, so five slices in total, the count of the switch request remains unchanged. If `on_tick` forgets that empty queue judgment, there should be a virtual request every ten beats in this slice, the count reading would increase to five, and `CHECK` would flip immediately.

The third test case guards the most delicate invariant, which is the slice debt not being inherited. Its script is longer, I will repeat it in three acts according to the comments. The first act is the setting: level 0 h goes on stage, it suppresses a pair of same-level partners, after it sleeps, a returns to the CPU, and b is waiting in the queue:

```cpp
    // a runs 8 of its 10 beats, gets preempted by someone higher; when it
    // returns it must NOT inherit its own leftover share. b is the same-level
    // companion waiting in the ring (a slice only yields when someone waits)
    s.add(&a.tcb);
    s.add(&b.tcb);
    s.add(&h.tcb);
    REQUIRE(s.pick_next().get() == &h.tcb); // higher level took over
    s.block(&h.tcb);                        // (this pends — wash it off)
    REQUIRE(s.pick_next().get() == &a.tcb); // a is back on stage, b waits
    g_switch_requests = 0;
```

There is a detail of stage direction in the first act that I need to point out to you. The line above `s.block(&h.tcb)` has a comment that says "(this pends — wash it off)". `block` itself will request a switch, h is the current task, it sleeps, the kernel naturally needs to invite someone to take over, the `request_switch` in the host's substitute only increments the counter, but it dirties the count. The test author clears `g_switch_requests` to zero before the main play begins, so that the assertions after count the requests for the slice. One line of comment teaches the host a lesson on the stage: every request in the substitute has really happened, clear the count to zero before counting.

In the second act, a returns to the stage, we give it eight beats, eight beats are not enough for ten beats, it leaves the stage for other reasons (the test uses `block` plus `ready` to simulate the plot of queuing up), and when selecting again, it is b who comes out:

```cpp
    for (int i = 0; i < 8; ++i) {
        s.on_tick();
    }
    CHECK(g_switch_requests == 0); // 8 beats in, share still good
    s.block(&a.tcb);               // ...but now a leaves for other reasons
    s.ready(&a.tcb);               // a rejoins at the TAIL...
    REQUIRE(s.pick_next().get() == &b.tcb); // ...so b takes over (fresh pick)
    g_switch_requests = 0;
```

The third act is the good play we are waiting for: after b takes over, nine beats are hit, there should be no request, and the tenth beat is the one that should be:

```cpp
    for (int i = 0; i < 9; ++i) { // 9 beats must NOT trigger: b inherited
        s.on_tick();              // NOTHING of a's leftover 8
    }
    CHECK(g_switch_requests == 0);
    s.on_tick(); // ...the 10th does
    CHECK(g_switch_requests == 1);
    REQUIRE(s.pick_next().get() == &a.tcb); // and the ring turns to a
```

Let's check the intention of this act: if `pick_next` does not reset the count, the 8 beats accumulated by a before leaving would remain in the counter and be inherited by b, b would have to leave after two beats. The test blocks both paths: b must enjoy a full ten beats, and when a returns (the last line of `pick_next` selects a back), it also starts from zero.

I configured the host build in the /tmp copy with GCC 16.2.1, all twelve targets of `ctest` passed, the total time taken was 0.24 seconds:

```text
100% tests passed out of 12

Total Test time (real) =   0.24 sec
```

The scheduler's own test results, we ran 13 test cases and 59 assertions, which is 4 more test cases and 17 more assertions than the first time it submitted, the additional part is exactly the three slice test cases we just saw and their 14 assertions. We run the `[slice]` group separately, the results are as follows:

```text
All tests passed (14 assertions in 3 test cases)
```

## Renode Theater: Two Tasks That Never Sleep Alternate in Output

The host checks the count and requests, the tasks are really switched, we also need to see it in the simulation. `example/main.cpp` adds an `r` key play in this commit, we look at both parts of it, the first one is the actor:

```cpp
// 'r' spawns two NEVER-SLEEPING spinners on the SAME level for ~3 seconds.
// Without time slicing one of them would starve the other; the slice forces
// them to take turns — the interleaved output IS the feature.
void spinner(void* tag) {
    const auto* line = static_cast<const char*>(tag);
    const auto deadline = ThisTask::now().tick_ + 3000;
    auto next_report = ThisTask::now().tick_;
    for (;;) {
        const auto now = ThisTask::now().tick_;
        if (now >= deadline) {
            break; // three seconds of proof is enough
        }
        if (now >= next_report) {
            ZerOS::board::print(line);
            next_report += 500;
        }
        // pure spinning, zero sleeps — the scheduler does the fairness
    }
    ThisTask::block(); // self-retire: sleep with nobody to wake me
}
```

The comment itself reveals the key point of the play, it writes pure spinning plus zero sleeps. Two such actors at the same level, they have no self-awareness of yielding or not, fairness can only be done by the scheduler, and if their outputs are interleaved, the interleaved output is itself a feature. The last line of `ThisTask::block()` is the usual way to step down: after the task body returns, it has no other place to go, sleeping means no one will wake it up, and it steps down like that. I read it to you: the comment says "sleep with nobody to wake me", it's its own words.

We look at the `r` key branch that hatches the actors: both tasks are ranked at level 3, each is given 64 bytes of stack, static `TCBStorage` plus static stack arrays, we didn't spend a single particle of heap memory:

```cpp
                auto& r1 = ZerOS::task::named("rr1")
                               .prio(3)
                               .stack(stack_rr1)
                               .entry(spinner, const_cast<char*>("rr1 alive\r\n"))
                               .spawn_into(tcb_rr1);
                auto& r2 = ZerOS::task::named("rr2")
                               .prio(3)
                               .stack(stack_rr2)
                               .entry(spinner, const_cast<char*>("rr2 alive\r\n"))
                               .spawn_into(tcb_rr2);
```

My own verification chain is the same: `git archive 1006648` exports the source code tree, we add CMSIS headers, use arm-none-eabi-g++ 16.2.0 for cross-compilation, the size is real:

```text
   text	   data	    bss	    dec	    hex	filename
   5684	    224	   3256	   9164	   23cc	build/src/board/stm32f103_bluepill/zeros-bluepill.elf
```

Renode has an old script, the local 1.17.0 still can't recognize `bluepill.repl`'s `I2C.STM32F4_I2C`, I have commented out the two nodes i2c1 and i2c2 along with their connections, the demo does not go through i2c, the behavior is unaffected, which is consistent with the handling of the previous few articles. The serial port analyzer is a read-only window, characters cannot be entered, I have moved `sysbus.usart1` into the monitor, after the simulation runs for one second, I fed it a `0x72` (the encoding of the character `r`), let it run for six more seconds, and collected the output from the serial port window:

```text
[INFO] usart1: [host: 1.45s (+51.75ms)|virt:    1.35s (+48.3ms)] c: spinner pair out, 3s of fairness
[INFO] usart1: [host:  1.45s (+0.31ms)|virt:   1.35s (+17.17µs)] rr1 alive
[INFO] usart1: [host:  1.46s (+9.42ms)|virt:    1.36s (+9.68ms)] rr2 alive
[INFO] usart1: [host: 2.13s (+76.16ms)|virt:      1.85s (+50ms)] rr1 alive
[INFO] usart1: [host:  2.14s (+11.59ms)|virt:      1.86s (+10ms)] rr2 alive
[INFO] usart1: [host: 2.81s (+69.86ms)|virt:   2.35s (+49.99ms)] rr1 alive
[INFO] usart1: [host:  2.83s (+11.98ms)|virt:      2.36s (+10ms)] rr2 alive
[INFO] usart1: [host: 3.41s (+62.26ms)|virt:      2.85s (+50ms)] rr1 alive
[INFO] usart1: [host:  3.42s (+10.91ms)|virt:      2.86s (+10ms)] rr2 alive
[INFO] usart1: [host: 3.86s (+39.67ms)|virt:   3.35s (+50ms)] rr1 alive
[INFO] usart1: [host:  3.87s (+5.72ms)|virt:      3.36s (+10ms)] rr2 alive
[INFO] usart1: [host: 4.25s (+42.43ms)|virt:   3.85s (+50ms)] rr1 alive
[INFO] usart1: [host:   4.26s (+7.7ms)|virt:      3.86s (+10ms)] rr2 alive
```

We count along the virt timeline: rr1 speaks once at 1.35, 1.85, 2.35, 2.85, 3.35, 3.85 seconds, and rr2 speaks once at 1.36, 1.86, 2.36, 2.86, 3.36, 3.86 seconds, each actor speaks six times, we count and find that there is not one more or one less, the 500 millisecond rhythm is perfectly tight. Two dead spinning tasks that never sleep, within three seconds, they take turns using the CPU, no one is starved.

What's more interesting is the late arrival of rr2. Each of its sounds is about ten milliseconds later than rr1, the interval in the virt column of the log is 9.68, 10, 10, 10, 10, 10 milliseconds, around ten milliseconds, which is the length of a slice. My reading is as follows, I read it to you: when the 500 millisecond mark is reached, whoever has the CPU immediately shouts once, the other has to wait for the same-level slice to expire (at most another slice, 10 milliseconds) to take over the CPU before it can speak. So that constant ten-millisecond delay is not noise, it is the on-site reading of the slice length, the rotation rhythm of the two tasks is pressed by SysTick beat by beat into the 10-millisecond grid.

## Acceptance

We summarize the three verifiable facts of this article.

The first is the host's exam. You checkout to `1006648`, configure the host build and run `ctest`, all twelve targets will pass, the score of `test_sched` when run alone is 13 test cases and 59 assertions, the score of the `[slice]` group is 3 test cases and 14 assertions. Each of the three test cases guards one invariant: requesting a switch after ten beats, not requesting after 50 beats if no one is waiting in the same level, counting starts from zero after switching, each item can be matched back to the corresponding lines in `on_tick` and `pick_next`.

The second is the board's theater. The cross-compiled size is text 5684, data 224, bss 3256, in Renode's log, the two spinners each speak six times, rr1 and rr2 are strictly interleaved, and each sound of rr2 is about ten milliseconds late. To reproduce, remember to comment out the two i2c nodes in repl of Renode 1.17.0, feed `sysbus.usart1 WriteChar 0x72` in the monitor (feed it one second after the simulation starts), the complete command line to start the simulation, I paste it to you as is:

```text
renode --console --disable-xwt -e "$bin=@/tmp/zeros-t1006648/build/src/board/stm32f103_bluepill/zeros-bluepill.elf" -e "include @/tmp/zeros-t1006648/src/board/stm32f103_bluepill/sim/renode/bluepill.resc" -e "start" -e "sleep 1" -e "sysbus.usart1 WriteChar 0x72" -e "sleep 6" -e "quit"
```

The third is the boundary explanation, I state it truthfully. The host test case checks the count and switch request, the real context switch is not visible in the host substitute, the real interleaving is given by the serial port log in Renode, the two ends of the host and the simulation together, we have completed the verification. The length of the time slice, 10 milliseconds, depends on the 1kHz beat, and the beat is set by one line of configuration in `main`'s `init_time`, if you change the beat, the slice length will change accordingly, `kSliceTicks` counts the beats, milliseconds need to be converted using the beat. The reading of the ten-millisecond delay is my inference, the numbers are real, if you have other explanations, feel free to discuss with me using the log.

The debt of the synchronization family is waiting for us in the next article: multi-party waiting needs a "two keys to open the door" primitive, the fastest path needs a route that does not go through the queue and directly puts the value into the task's hands, event groups and direct notifications will complete them, broadcast wakeups and wakeups will recheck the truth, becoming an invariant across all synchronization primitives. We'll see you there.
