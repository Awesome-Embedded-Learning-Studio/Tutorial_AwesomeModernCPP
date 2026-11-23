---
title: "Watch M's Three Seconds of Silence: Mutexes, Priority Inheritance, and Ownership Transfer"
description: "7dbbcdf added the second piece of furniture to the synchronization family: MutexBase arranges the waiting chain by priority, unlock does not re-competes, and ownership is handed over directly to the head of the waiting queue. A single layer of priority inheritance is performed by the scheduler's new reprioritize function. I ran the host stage model's promotion, transfer, and recovery in the /tmp copy, all 25 assertions passed, and in Renode's L/M/H theater, M's task (the middle-priority intruder) was silent for 2.3 seconds instead of 2.0 seconds. The window of silence is the window where the lock holder is running with a high priority."
chapter: 4
order: 2
tags:
  - stm32f1
  - advanced
  - 嵌入式
  - mutex
  - cpp-modern
difficulty: advanced
platform: stm32f1
cpp_standard: [23]
reading_time_minutes: 24
prerequisites:
  - "Who is the most urgent, who takes the stage: Ready bitmaps, task states, and selection arbitration"
  - "Sleeping that bug: Lost wakeups, park_for, and builders that prevent incorrect usage"
related:
  - "Events come early but must not be lost: Counting semaphores and eight operations of SystemContext"
current_status:
  title: M's three-second performance was successful
  detail: The ZerOS chapter is currently being polished
  variant: polishing
  icon: 🎭
translation:
  source: documents/vol8-domains/embedded/zeros/04-sync-and-inheritance/02-mutex-priority-inheritance.md
  source_hash: fcd0f1c00420325c0b3c689eb402a76289fa1a795374ddd87db3a0b95b44e0d6
  translated_at: '2026-11-22T13:40:54+00:00'
  engine: qwen3:14b
  token_count: 16918
---
# Watch M's Three Seconds of Silence: Mutexes, Priority Inheritance, and Ownership Transfer

The previous article on semaphores left us with a natural follow-up question: semaphores count "how many times an event has occurred," but in situations where two tasks share the same hardware register or buffer, counting alone is not enough. We also need to answer "who is using it now." The furniture that answers this question is the mutex, which manages two additional aspects beyond a count: who owns it, and who decides when it is handed over. In this article, we will break down the mutex, with the commit `7dbbcdf` as our anchor, whose subject is "feat: mutex," and which involves 14 files, adding 438 lines and removing 17 lines. The main character is the 104-line `include/ZerOS/kernel/sync/mutex.hpp`.

What it brings, we summarize as three things: a waiting chain arranged by priority, an unlock that directly transfers ownership without re-competition, and a single layer of priority inheritance. The third item requires modifying the scheduler's data, so the same commit also added a new surgical tool called `reprioritize` to the scheduler and added two verbs to the `SystemContext` seam. Today's story has a specific picture down to the millisecond: in Renode's serial port window, M's task used to speak every 100 milliseconds, but from virtual time 2.0 seconds to 2.3 seconds, it didn't say a word. That silence is the trace of the lock holder running with the elevated priority.

## Two new verbs added to the seam: prio and reprioritize

Before locking, let's look at where the seam was changed. `SystemContext` is the list of requirements that synchronization primitives must meet. In the previous article, we counted its verbs, which included `current_task`, `ready`, `block`, `sleep_for`, `yield`, `now`, `self`, and a pair of `lock` and `unlock`. The commit `7dbbcdf` added two new members to the list. Let's look at the changes in `include/ZerOS/kernel/system_concept.hpp`:

```cpp
template <typename System>
concept SystemContext = requires(System& s, base::BorrowedPtr<sched::TCB> t,
                                 clock::Ticks::tick_t span,
                                 sched::TaskPriority_t prio) {
    { s.current_task() } -> std::same_as<base::BorrowedPtr<sched::TCB>>;
    { s.ready(t) };
    { s.block(t) };
    { s.sleep_for(t, span) };
    { s.yield() };
    { s.now() } -> std::same_as<clock::Ticks>;
    { s.self() } -> std::same_as<System&>;
    { s.prio(t) } -> std::same_as<sched::TaskPriority_t>;  // PI wants to read
    { s.reprioritize(t, prio) };                            // ...and to lift
    s.lock();
    s.unlock();
};
```

The comments briefly describe the roles of the two verbs. We'll translate it into plain language: the judgment of inheritance asks "what is the current priority of the lock holder," which is `prio`. Once the judgment is made, we also need to change the holder's priority to the waiting task's level, which is done by `reprioritize`. The arrangement of reading and writing verbs ensures that the lock code does not touch the TCB's fields, and the writing key remains in the scheduler's hands. The implementation on the board is in `src/arch/arm_cortex_m3/system.cpp`, and both functions are one-line forwards to the scheduler. We'll take a quick look:

```cpp
sched::TaskPriority_t CortexM3System::prio(base::BorrowedPtr<sched::TCB> t) {
    return system_sched.prio(t);
}

void CortexM3System::reprioritize(base::BorrowedPtr<sched::TCB> t,
                                  sched::TaskPriority_t new_prio) {
    system_sched.reprioritize(t, new_prio);
}
```

Another point I need to point out to you: synchronization primitives no longer hold the critical section themselves, but instead use the seam's `lock` and `unlock`. The implementation of `CortexM3System` is based on the `CortexM3CriticalSection` we discussed in the previous article. It still writes the mask level of 0x40 into BASEPRI, and interrupts with a level lower than 0x40 can still come in. We've encountered this before, and in the previous article's "Balancing interrupt priorities, then letting interrupts knock" section, we had this: the level assigned to peripheral interrupts in NVIC must be higher than this, otherwise our critical section will be torn apart. How to balance the levels of locks and interrupts has already been discussed in that article, and we'll return our attention to the new surgical tool in the scheduler.

## reprioritize: Changing the level is a surgery

You might think that changing a priority is just assigning a value. But the scheduler will tell you with its structure that assigning a value is not enough. Ready tasks live in 32 FIFO linked lists divided by level, and the bitmap records which levels have people. If you directly reduce the `task_priority_` of a task that is still in the queue, it will be lying in a linked list that the bitmap can no longer find, and we'll never select it again in the future. Such a task is effectively buried. Therefore, `reprioritize` must operate on the task's three states separately. Let's look at the new addition in `include/ZerOS/kernel/sched/scheduler.hpp`:

```cpp
    void reprioritize(base::BorrowedPtr<TCB> t, TaskPriority_t new_prio) {
        irq::CriticalGuard guard{driver_};
        if (t->task_priority_ == new_prio) {
            return;
        }
        if (t->state_ == TaskState::Ready) {
            fifo_[t->task_priority_].remove(t);
            if (fifo_[t->task_priority_].empty()) {
                present_.clear(t->task_priority_);
            }
            t->task_priority_ = new_prio;
            push_back(t.get());
        } else {
            t->task_priority_ = new_prio;
        }
    }
```

As usual, it grabs the critical section before entering. Requests that don't change the level are simply dismissed. The real divergence lies in the task's state. If the task is still in the ready queue, we need to remove it from the old level's linked list. If the linked list is empty, we also need to clear the corresponding bit in the bitmap. Then we change the level and reinsert it into the queue with the new level. The bitmap and linked list are rearranged from the inside out, and they are tightly aligned. When the task is not in the queue, its state can only be `Running` or `Blocked`. There are no linked lists to maintain, so changing a number is enough. The `Running` task immediately participates in the next selection with the new level. The `Blocked` task will be naturally placed into the new level's queue when `ready` wakes it up, and the `push_back` function on the other end of the bitmap is responsible for setting the bit, and we don't need to add anything.

The same commit also added a small read-only function to `Scheduler`, and we'll note its name as well:

```cpp
    // a plain read; diagnostics and the boost-or-not decision both want it
    [[nodiscard]] TaskPriority_t prio(base::BorrowedPtr<TCB> t) const { return t->task_priority_; }
```

The two customers mentioned in the comment are one for diagnostics and the other for the lock's "should we promote" judgment. The agreement on priority is still the one we set in the previous article: the smaller the number, the more urgent, and `0xFF` is reserved for idle. The inheritance logic is all built on this direction, and we'll repeatedly use the reading "greater means slower" in the lock code.

## Waiting Chain: The Linked List for Seating Order

Before the main character appears, we'll clarify the reason for the existence of the waiting chain. Tasks that can't get the lock have to queue up on the chain, and the way they queue up matters. The handover of the mutex is one-time, and the `unlock` directly transfers the ownership to the designated recipient, so the holder is fixed. It doesn't have a counter like a semaphore to re-decide in the middle, so the seating order must be arranged when queuing. If the queue is arranged purely as FIFO like a semaphore, urgent tasks might end up waiting behind slower ones. If the queue is not arranged at all and everyone re-competes after unlocking, an urgently awakened task might see the lock taken by a slower task that just happened to pass by, resulting in a wasted wait. Therefore, each node in the waiting chain must carry its own level, and the chain itself arranges the seating order by level.

Now the main character appears. Let's open `mutex.hpp`. It starts by defining the waiting task's profile and error codes:

```cpp
enum class MutexError : std::uint8_t { Ok, NotYourLock, SelfLocked };

struct MutexWaiter : base::SelfNode<MutexWaiter> {
    constexpr explicit MutexWaiter(sched::TCB* task, sched::TaskPriority_t prio) noexcept
        : task_(task), prio_(prio) {}
    sched::TCB* task_;
    sched::TaskPriority_t prio_;
};
```

Each waiting task registers two things here: who it is and what level it was at when it arrived. We'll note the identity of `prio_`, which is a snapshot at the time of registration. Don't treat it as a live reference. The judgment for the transfer uses the number from the registration. The body of the lock has several more items than a semaphore, and the ownership, the inherited level, and how to return it must all be carried on the lock itself:

```cpp
template <ZerOS::system::SystemContext System> struct MutexBase {
    constexpr MutexBase() noexcept = default;

  private:
    sched::TCB* owner_{};
    sched::TaskPriority_t saved_prio_{}; // saved
    bool boosted_{false};

    struct ByPrio {
        bool operator()(const MutexWaiter& fresh, const MutexWaiter& parked) const noexcept {
            return fresh.prio_ < parked.prio_;
        }
    };
    base::SelfList<MutexWaiter, ByPrio> waiters_{};
};
```

The private items are four: a pointer to the holder, the original level before promotion, a flag indicating that a promotion is in progress, and the waiting chain itself, which is used to arrange the seating order. The type of the waiting chain is the `SelfList` template paired with the `ByPrio` comparator. We've seen `SelfList` from the previous article, which is purely FIFO without a comparison strategy, but with a comparison strategy, it changes its behavior. I'll move the insertion function here for you to see:

```cpp
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

It walks from the head of the chain down, and if the new one is not more urgent than the one it passes, it continues down. `less_` compares the `prio_` of the less than, so the task with a smaller number (more urgent) is inserted in front. This condition has a subtle benefit. Let's walk through the case of same-level tasks: when two waiting tasks are at the same level, the comparison `fresh < parked` does not hold, so the new one will be inserted behind the same-level waiting task. The order of same-level tasks is still arranged by the order of arrival. With a linked list and a comparator, we've arranged the two-layer semantics of urgent tasks cutting in and same-level tasks maintaining order. The head of the queue is always the most urgent waiting task, and `pop_head` pops the head, which is the one we need for the transfer.

## lock: Ask once at the door, wait for the transfer in the loop

The locking process is the longest function in this file. We'll read it in segments. Before diving into the code, let's count the accidents it needs to prevent. Having a map in mind while reading will help. The first accident to prevent is self-locking: a task trying to lock a lock it already holds. The door will block such a request, otherwise, in the loop, `owner_` equals to itself would have two meanings: it's the one that transferred the ownership to us, or we originally held it. The code would not be able to distinguish. The second accident is in the recognition of the transfer: after `unlock` transfers the ownership, the awakened person doesn't check `owner_` again, and the result of the transfer is not claimed by anyone. Therefore, the check must be done at the top of the loop each time. The third source of accidents is the level: the holder is running with a higher level, while the waiting task outside has a lower level. Without promotion, the urgent task has to wait with the slow task and unrelated intermediate tasks, and the damage of priority inversion comes from here. Therefore, after inserting into the waiting chain, we also need to perform a promotion. The three prevention methods have their own positions in the code, and we'll match them one by one:

```cpp
    MutexError lock() noexcept {
        auto& system = System::self();
        const auto current_task_ = system.current_task();

        {
            // ask ONCE at the door: re-locking what I already hold.
            // Inside the loop, owner==me can only mean "handed to me while parked"
            ZerOS::irq::CriticalGuard g{system};
            if (owner_ == current_task_.get()) {
                return MutexError::SelfLocked;
            }
        }

        MutexWaiter me{current_task_.get(), system.prio(current_task_)};
```

The initial isolated critical section, we'll read the intention of the comment: a task trying to lock a lock it already holds is a deadlock, and the door blocks it, returning `SelfLocked`. Why only ask once? Because after entering the loop, `owner_` equals to itself has only one possibility: we were sleeping when the holder unlocked, and the transfer gave us the ownership. At this point, the same name is a good thing, not a problem. The author uses the position of two checks to clarify this distinction, and the comment's "ask ONCE at the door" draws the boundary line.

```cpp
        while (1) {
            ZerOS::irq::CriticalGuard g{system};

            if (owner_ == current_task_.get()) {
                waiters_.remove(&me);  // pop_head already got me; this is belt and braces
                return MutexError::Ok; // handed over while I parked
            }

            if (!owner_) {
                owner_ = current_task_.get();
                waiters_.remove(&me);
                return MutexError::Ok;
            }
```

The loop body does four things after grabbing the critical section each time. Let's count them one by one, the first two are in the code above. The first thing is to check for the transfer. If it's found, we remove ourselves from the waiting chain and return. The comment self-deprecatingly calls `remove` as "belt and braces" because the transfer's `pop_head` usually already removes us, and we do it again as a double insurance. The second thing is to check for an empty spot. If the lock has no owner, we directly take the lock.

```cpp
            waiters_.insert(&me);

            if (system.prio(owner_) > system.prio(current_task_)) {
                if (!boosted_) {
                    // OK, owners self level
                    saved_prio_ = system.prio(owner_);
                    boosted_ = true;
                }

                system.reprioritize(owner_, system.prio(current_task_));
            }

            system.block(current_task_);
        }
    }
```

The third thing is the main course today: the lock has an owner, and the owner's level is higher than ours (i.e., slower). We then store the owner's original level in `saved_prio_`, set the `boosted_` flag, and call `reprioritize` to promote the owner to our level. The single-layer `boosted_` is embodied in this flag. It ensures that the action of storing the original level only happens once. If the owner has already been promoted once, and another more urgent waiting task comes, we will continue to call `reprioritize` to promote it further, not storing the already promoted level as the original level again. The original level is always only one copy here. The fourth thing is the `block` at the end: we sleep, and after waking up, we check again from the top of the loop.

Here, we'll also state the boundaries clearly: inheritance only promotes the direct holder of the lock. If the holder happens to wait for another lock, this layer of code will not recursively promote up the chain of holders. `saved_prio_` and `boosted_` are also recorded separately for each lock. The complete version of the inheritance protocol in textbooks also handles the recovery order of nested locks, but ZerOS does a single-layer version in this commit. We won't expand on it, and the boundary will appear again in the verification section.

## unlock: No re-competition, ownership is handed over directly

`unlock` has three things to do in one go, we'll read them in segments: check if the lock belongs to you, return the inherited level, and hand over the ownership directly to the head of the waiting queue. Why not return the lock to the unowned state and let everyone re-competes? In the re-competition writing, the newly awakened waiting task has to compete with all the running tasks, and the urgent task may see the lock taken by a slow task that just happened to pass by, resulting in a wasted wait. The direct handover sets the ownership at the moment of release, and the handed-over ownership is the pre-arranged order:

```cpp
    MutexError unlock() noexcept {
        auto& system = System::self();
        const auto current_task_ = system.current_task();
        ZerOS::irq::CriticalGuard g{system};

        if (owner_ != current_task_.get()) {
            return MutexError::NotYourLock;
        }
        if (boosted_) {
            system.reprioritize(current_task_, saved_prio_);
            boosted_ = false;
        }
```

If it's not your lock, `NotYourLock` will reject you. Releasing the lock again will also be rejected for the same reason, because after releasing, `owner_` no longer points to you. You were promoted, so the first thing to do when releasing the lock is to lower the level back to the original level stored in `saved_prio_`. The cost of inheritance is settled here.

```cpp
        if (auto head = waiters_.pop_head()) {
            owner_ = head->task_;
            if (auto next = waiters_.head(); // Is the new headers request
                next != nullptr && next->prio_ < system.prio(owner_)) {
                saved_prio_ = system.prio(owner_);
                boosted_ = true;
                system.reprioritize(owner_, next->prio_);
            }
            system.ready(head->task_);
        } else {
            owner_ = nullptr;
        }
        return MutexError::Ok;
    }
```

If there are people waiting, `pop_head` pops the head of the queue and writes it directly into `owner_`. The most urgent waiting task gets the inheritance, and the ownership is set at the moment of release. After writing the value of `owner_`, our code has not yet let go. It continues to look at the next waiting task on the chain: if the next one's level is more urgent than the new owner, the new owner is immediately promoted, and `saved_prio_` records its own level. The inheritance changes the bat at the moment of transfer. After the transfer is complete, `ready` is called to put the new owner back into the ready queue. Its subsequent competition only happens on the CPU, not on the lock anymore. If no one is waiting, it's simple: we clear `owner_` and return the lock to the unowned state.

## host stage model: promotion, transfer, and recovery in full arc

After explaining the mechanism, we go to the exam. The main body of `test/test_mutex.cpp` has 210 lines, facing a real difficulty: the host's proxy doesn't have real context switching, `request_switch` only increments a counter, and the task doesn't really "sleep and wake up." Its solution is to build a stage, let's see how the stage is built:

```cpp
// Mock side of the D22 doctrine, mutex flavor: prio/reprioritize forward to
// the REAL scheduler (PI surgery gets exercised for real); during_sleep runs
// "the world while I park" — the host trick that makes the re-check loop
// converge without a real context switch.
struct FakeSys {
    static inline Scheduler<HostSwitchPort>* sched_;
    static inline Kernel<SleepPort>* time_;
    static inline std::function<void()> during_sleep = nullptr;
```

The proxy system forwards `prio` and `reprioritize` directly to the real scheduler, so the inheritance surgery on the host is done on the real scheduler's structure. Paper-based deductions can't give this. The real magic is `block`: after the task declares blocking, the proxy continues to execute the closure in `during_sleep`, which plays the role of "what happens in the world after I fall asleep." The `lock` loop in the lock needs the transfer from others to converge, and the stage lets others perform the play while we are asleep. When the loop wakes up, it sees that the transfer has already happened. This concurrency is fake, it's a stage play following the script, but it follows the path of the production code, and the assertions are on the real scheduler's state.

Let's look at the full arc test case in full. It performs the entire arc of promotion, transfer, and recovery in one go:

```cpp
TEST_CASE("boost, hand-over and restore: the whole inheritance arc", "[mutex][pi]") {
    World w;
    Mtx m;

    w.become(w.low.tcb); // low (2) takes the lock...
    REQUIRE(m.lock() == MutexError::Ok);

    w.become(w.high.tcb); // ...then high (0) comes for it
    FakeSys::during_sleep = [&] {
        CHECK(TCBKeys::prio(w.low.tcb) == 0);  // holder wears my level now
        w.become(w.low.tcb);                   // world: the holder runs...
        CHECK(m.unlock() == MutexError::Ok);   // ...releases (restore + hand over)
        w.become(w.high.tcb);                  // ...and the waiter takes the CPU back
    };
    REQUIRE(m.lock() == MutexError::Ok); // handed to me while parked

    CHECK(TCBKeys::prio(w.low.tcb) == 2); // restored, not left boosted
    CHECK(m.unlock() == MutexError::Ok);  // and yes, the lock is mine now
}
```

Let's go through the plot with the assertions. The 2-level `low` takes the lock, the 0-level `high` comes to compete. At the moment `lock` reaches `block`, the stage's closure starts: the first assertion checks for promotion, and at this moment, `low`'s level must be 0, it is wearing `high`'s level, which is evidence that `reprioritize` has operated. Then `low` is invited to the stage to run, and `unlock` is called, which simultaneously performs recovery (returning to level 2) and transfer (`owner_` is written as `high`). `high` returns to the stage, the `lock` loop wakes up, and sees that `owner_` is written as itself, the transfer has been successfully received. At the end of the test case, there are two more assertions: `low`'s level is checked back to 2, the promoted level has been returned, and after `high` gets the lock again and releases it, it returns `Ok`, because the lock is indeed its, and the ownership transfer has been successfully completed.

Other test cases each guard one point. The `SelfLocked` at the door and the `NotYourLock` from another hand are verified in one test case together, and double release is also verified, and all are rejected. `reprioritize` has its own test case, named "keep the ready ring honest in all three forms," with each of the three states (in the queue, running, sleeping) coming once. After promoting the one in the queue from level 3 to 0, we can immediately select it with `pick_next`, and the surgery has not buried anyone in an unsearchable queue. I truthfully report an untested corner: the matter of multiple waiters being arranged by urgency, the host test case only did the transfer mentioned in the author's comment (the author's comment left the sorting of multiple waiters to Renode's demo), and the board's demo actually only arranged one waiter. The sorting path has not been truly run on the host or the board, and I leave this sentence to you as is.

The overall test result is as follows: I built the host with GCC 16.2.1, and all nine targets of `ctest` passed, with a total time of 0.19 seconds. I'll post the results below:

```text
100% tests passed out of 9

Total Test time (real) =   0.19 sec
```

I'll read the mutex's own report separately. Four test cases, 25 assertions passed:

```text
All tests passed (25 assertions in 4 test cases)
```

## Renode Theater: M's Silent Window

The host stage proved the mechanism, but the visible priority inversion must be seen in the real scheduling rhythm. `example/main.cpp` this time submitted six tasks, we only disassemble the theater group, the part that collects bytes is handled by the sister article, I won't go out of bounds. The three actors' household registration is as follows. The lock holder L is called `task_holder`, level is 2, it takes the lock every 2000 milliseconds, after taking it, it doesn't sleep, it does 300 milliseconds of busy waiting before releasing. The one who wants the lock H is called `task_urgent`, level is 0, it wants the lock every 2100 milliseconds, and its arrival time is naturally 100 milliseconds later than L. The troublemaker M is called `task_bully`, level is 1, it has no relation to the lock, and its only line of script is `M: running`, printing every 100 milliseconds.

Before we look at L's task body, we need to clarify the intention of the busy waiting. The damage the theater wants to perform depends on this busy waiting: if the lock holder holds the lock and sleeps for a long time, sleeping doesn't take up CPU, H comes and waits for a while, and no one is harmed by the inversion. Only L, who is awake and occupies the CPU for 300 milliseconds of busy waiting, is the one who is truly pressed, and M's troublemaking has a play to perform:

```cpp
void task_holder(void*) { // L
    for (;;) {
        ThisTask::sleep_for(Milliseconds{2000});
        if (g_theater.lock() != ZerOS::sync::MutexError::Ok) {
            continue;
        }
        ZerOS::board::print("L: locked, busy 300ms\r\n");
        const auto until = ThisTask::now().tick_ + 300;
        while (ThisTask::now().tick_ < until) { // busy hold: the whole point
        }
        ZerOS::board::print("L: unlocked\r\n");
        g_theater.unlock();
    }
}
```

The comment's `busy hold` refers to the deliberate part mentioned above. Let's go through the order after the play starts: H arrives and `block` on the lock immediately, inheritance takes effect, L is `reprioritize` to level 0, and from then on, it's above level 1 M, M's printing is stopped, L's busy waiting is completed in a millisecond, `unlock` is immediately demoted and transferred, and H announces it's in hand after three microseconds. M's silent window and L's window with the high level are the same window.

The path I ran in my /tmp copy is as follows: I exported the tree with `git archive 7dbbcdf`, added the CMSIS header, and did a cross-compile with arm-none-eabi-g++ 16.2.0, and the volume is real:

```text
   FLASH:        3580 B        64 KB      5.46%
             RAM:        2248 B        20 KB     10.98%
   text	   data	    bss	    dec	    hex
    3388	     192	   2056	   5636	   1604
```

Then we ran it on Renode for 9 seconds. My local Renode 1.17.0 doesn't recognize the `I2C.STM32F4_I2C` type in `bluepill.repl`, so I commented out the two nodes i2c1 and i2c2 entirely, and the demo doesn't go through i2c, so the behavior is unaffected, and this handling is consistent with the previous articles. The real output of the serial port window, we cut the part where the theater starts:

```text
[INFO] usart1: [host: 2.14s (+0.24ms)|virt:       2s (+2.27µs)] M: running
[INFO] usart1: [host:  2.14s (+0.33ms)|virt:       2s (+3.08µs)] L: locked, busy 300ms
[INFO] usart1: [host:    2.34s (+0.2s)|virt:    2.1s (+99.99ms)] H: wants the lock
[INFO] usart1: [host:   2.71s (+0.37s)|virt:       2.3s (+0.2s)] L: unlocked
[INFO] usart1: [host:  2.71s (+0.67ms)|virt:        2.3s (+3µs)] H: LOCKED (inheritance paid off)
[INFO] usart1: [host:  2.71s (+0.42ms)|virt:     2.3s (+5.38µs)] B
[INFO] usart1: [host:  2.71s (+0.23ms)|virt:     2.3s (+2.26µs)] M: running
```

We read the virt time axis. M's last shout is at 2.0 seconds, according to its rhythm, there should be one at 2.1 seconds and another at 2.2 seconds, but the log shows H saying it wants the lock at 2.1 seconds, the next line jumps directly to 2.3 seconds where L releases the lock, and it's a period of silence. I counted the original log, and between 2.0 and 2.3 seconds, M has zero lines. At 2.3 seconds, the line where L releases the lock and the line where H gets it are only 3 microseconds apart. The transfer is at the level of `pop_head` plus a few pointer writes, because the inheritance has already taken effect, and H is waiting in the chain for the promoted L, not wasting time with M's slow rhythm. M resumes speaking at 2.3 seconds, the demotion and transfer happen in the same critical section, and we see M's feeling, the transition is seamless. The second round at 4.3 seconds is exactly the same, the one who gets the lock is still L, and M has zero lines between 4.3 and 4.6 seconds, and H gets it within 3 microseconds after L releases the lock. The two rounds of the theater are separated by 2.3 seconds, H's 2100 milliseconds sleep plus the 300 milliseconds it was pressed, and one round is repeated once.

I need to give you an explanation for the "three seconds" in the title: M's silent window was measured to be 300 milliseconds, the missing two beats are 2.1 and 2.2, the title uses the dramatic term "three," the numbers are based on our logs, don't use the title to match the test report.

## Acceptance

We'll summarize the three verifiable facts from this article.

The first is the host's exam. You check out to `7dbbcdf`, set up the host build and run `ctest` once, all nine targets passed. The `test_mutex` single run result is four test cases and 25 assertions. The assertions for "promotion, transfer, recovery" are on the real scheduler's `task_priority_`, the promotion was checked at 0, and the recovery was checked back to 2, the two numbers come from the source code's behavior, I didn't do any rhetorical embellishment.

The second is the board's theater. The cross-compile size is 3580 bytes of flash and 2248 bytes of RAM. In the 9-second log of Renode, M was silent for 300 milliseconds in the two windows from 2.0 to 2.3 seconds and from 4.3 to 4.6 seconds, and H got the lock within 3 microseconds after L released it in both cases. If you want to reproduce it, remember to comment out the two i2c nodes in the repl of Renode 1.17.0.

The third is the untested and unimplemented parts, I'll truthfully present them. The ordering of multiple waiters, the host test case and the board demo each have only one waiter, and the ordering path has not been truly run. The inheritance is single-layer, it only promotes the direct holder of the lock, and does not recursively promote up the chain of holders, this is the boundary of the source code, the more complete protocol in the textbook is not seen in the warehouse, and we won't write it. `MutexWaiter::prio_` is a snapshot at the time of registration, and during the waiting period, if the task's level changes, the transfer judgment still uses the old number, the source code is written this way, I report it as is.

In the next article, we'll ask this kernel a different question: what if the chip dies? The archive of the fault scene, the canary on the stack, and the log that doesn't take up a single byte are the main dishes of the next article, and there's still the story of stack water level and tickless further ahead. We'll see you there.
