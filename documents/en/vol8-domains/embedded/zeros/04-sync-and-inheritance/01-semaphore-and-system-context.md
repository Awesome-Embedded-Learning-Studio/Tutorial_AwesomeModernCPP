---
title: "However Early the Event Arrives, It Must Not Be Lost: Counting Semaphores and the Eight-Operation SystemContext Seam"
description: "Pinned to 5c62c0d, we give the one-byte mailbox from the previous installment a memory that never drops events: the release of SemaphoreBase lands the unit in the counter before waking anyone, woken sleepers re-check the count on their own, and the losers of the race go back to sleep by themselves. The same semaphore code runs on both ends through the SystemContext concept — FakeSys on host, CortexM3System on the real board — and timed waits re-arm with the remaining budget so drift never accumulates. The 188-line host test_sync passes with seven test cases and twenty assertions, and the Renode UART echo has switched from a hand-written flag to semaphore drive: I stuffed three characters into the port, and the board echoed them back one by one."
chapter: 4
order: 1
tags:
  - stm32f1
  - advanced
  - 嵌入式
  - mutex
  - concepts
difficulty: advanced
platform: stm32f1
cpp_standard: [23]
reading_time_minutes: 24
prerequisites:
  - "谁最急,谁上场:就绪位图、任务三态与选人仲裁"
  - "CPU 归谁:从就绪位图到中断唤醒"
related:
  - "看 M 闭嘴那三秒:互斥锁、优先级继承与所有权移交"
current_status:
  title: 事件不再丢包
  detail: ZerOS的篇章正在打磨ing
  variant: polishing
  icon: 🎟
translation:
  source: documents/vol8-domains/embedded/zeros/04-sync-and-inheritance/01-semaphore-and-system-context.md
  source_hash: 99d03310ae0468a2ccedb33b24a2f58ddec6ba4750a83fd222dd76585f56ac29
  translated_at: '2026-11-22T15:08:36+00:00'
  engine: glm-5.3
  token_count: 33220
---

# However Early the Event Arrives, It Must Not Be Lost: Counting Semaphores and the Eight-Operation SystemContext Seam

When the previous installment wrapped up, we had already opened the first real-interrupt wake-up chain: the moment USART1 received a byte, the sleeping task was woken. But recall that wiring once more and you will notice its memory was poor — the wake-up traveled through a hand-written flag. If the byte arrived before the task went to sleep, the registration fell through; once the task had gone to sleep, nobody was left to wake it. In embedded work we see events that arrive earlier than the task all the time, so what we need is simple: something that records the fact that something happened as a number, and honors it when the person finally shows up.

The first commit of this installment, `5c62c0d` — subject "feat: add Semaphore and system free funcs collects to Sys Concept" — delivers exactly the memory we were missing. It hands over the first member of the synchronization-primitive family, `SemaphoreBase`, and at the same time gathers the answer to "what is the system" into a `SystemContext` concept of eight groups of operations. From here on, the same semaphore code runs on host through a stand-in and on the real development board through `CortexM3System`. I walked both paths in my /tmp copy: the host-side `test_sync` is 188 lines, and its seven test cases with twenty assertions all pass; the Renode UART echo also changed drivers — I stuffed three characters into the serial port, and the board echoed them back one by one. The hard lesson about interrupt priorities and the public face of `ThisTask` were both covered in the previous installment, so we will not repeat them today. Mutexes and priority inheritance are the topic of the next installment, *The Three Seconds M Goes Silent* (《看 M 闭嘴那三秒》) — it is about ownership, and the counting we lay down today is its foundation.

## Where the One-Byte Mailbox Loses

To be convinced of why it loses, we have to argue from the code. The hand-rolled wiring from the previous installment had the following skeleton (it is no longer in the repository; I am reconstructing it from the structure of the time — for details, trust the source quoted later in the text):

```cpp
// 手写的接法(示意):标志位一根,读了就清
volatile bool g_ready = false;
char g_byte = 0;

extern "C" void USART1_IRQHandler() {
    g_byte = uart1_getc();
    g_ready = true; // 事件登记:只记得"有",不记得"几次"
}

void task_c(void*) {
    for (;;) {
        if (g_ready) { // 任务还没睡下去的时候字节就到了,这里照样是 false
            consume(g_byte);
            g_ready = false;
        }
        ThisTask::sleep_for(...);
    }
}
```

The problem lives in a timing gap, so let us lay it on the table. The interrupt's registration and the task's registration are two separate actions, and what separates them is one scheduling decision. If the byte bursts in before the task manages to sleep, the flag gets set after the check, the task then goes to sleep, and nobody ever comes to wake it. Even if the timing luck holds, the flag has the capacity of a single slot: of two back-to-back bytes, the first is overwritten by the second. `6308dd0` used `park_for` to lock the arming and the blocking into the same critical section, which closed the sleep-forever window — but the "event arrives before the person" memory problem is beyond that fix, because it treats the atomicity of arming, not the capacity of the memory.

The wiring that `5c62c0d` swapped into the example states its own position right in the comments (these are the repository's words, quoted here; we will cash them in one by one below):

```cpp
// the event side lives in the semaphore (a unit arriving BEFORE the task
// sleeps survives — the hand-rolled ready() flag could not do that);
// the byte itself is only the payload, still a one-slot mailbox
```

A unit that arrives before the task goes to sleep survives anyway — the hand-rolled `ready` flag could never do that. Why can the semaphore? Because the evidence that something happened is kept in the counter, and the counter does not follow the task's sleeping and waking. Let us go read the implementation.

## SystemContext: Gathering "What Is the System" into Eight Groups of Operations

What the semaphore code needs done includes putting someone to sleep, waking someone up, and asking what time it is now — none of which it can do by itself, so the object it has to find is this thing called the system. But what is the system? On a real development board, it is the combination of two singletons, the scheduler and the time kernel; in the host tests, a stand-in takes over instead. `5c62c0d` wrote the question of what counts as the system into a concept. It lives at `include/ZerOS/kernel/system_concept.hpp`, and only once we understand it does the rest of the code become readable.

The naive approach would have the semaphore header reach directly for the scheduler and time-kernel singletons, recognizing the names `system_sched` and `system_time`. We already saw the consequence once in the previous installment: `this_task.cpp` followed exactly that approach — it knew the two singletons itself and assembled its own wake-up reaction. Move to a different world and the list of acquaintances has to be edited again; generic synchronization code gets welded to one specific board, and a single implementation can no longer run on both ends. The concept inverts the direction of the lookup: the synchronization primitive does not call anyone by name, it posts a list of requirements — whatever satisfies the list is the system it recognizes. Here is the spelling:

```cpp
template <typename System>
concept SystemContext = requires(System& s, base::BorrowedPtr<sched::TCB> t,
                                 clock::Ticks::tick_t span) {
    { s.current_task() } -> std::same_as<base::BorrowedPtr<sched::TCB>>;
    { s.ready(t) };
    { s.block(t) };
    { s.sleep_for(t, span) };   // how a sleeper gets woken is the system's secret
    { s.yield() };
    { s.now() } -> std::same_as<clock::Ticks>;
    { s.self() } -> std::same_as<System&>;
    s.lock();
    s.unlock();
};
```

Counted one by one, the requirements come to eight groups. Here is the tally for you: ask for the current task, make a task ready, block a task, sleep for a span, yield the CPU, ask what time it is now, get a reference to oneself, plus the pair of lock operations `lock` and `unlock` (we count the pair as one group). Every action has the shape of something the scheduler and the time kernel already knew how to do; the concept invents no new capability — it merely bundles what the two singletons can do into a single face. The comment after `sleep_for` deserves a second look from you: "how a sleeper gets woken is the system's secret". The semaphore only hands the sleeper over; the wake-up path (alarm, callback, walking back from the expiry record to the task) belongs entirely to the system's implementation, and the synchronization primitive's code never touches hardware.

The board's answer sheet sits at `include/ZerOS/arch/arm_cortex_m3/system.hpp`. Look at its header: everything in it is a declaration; the bodies all live in `system.cpp`:

```cpp
class CortexM3System {
  public:
    // the static door generic code walks in through (SystemContext: self())
    [[nodiscard]] static CortexM3System& self();

    base::BorrowedPtr<sched::TCB> current_task();
    void ready(base::BorrowedPtr<sched::TCB> t);
    void block(base::BorrowedPtr<sched::TCB> t);
    void sleep_for(base::BorrowedPtr<sched::TCB> t, clock::Ticks::tick_t span);
    void yield();
    [[nodiscard]] clock::Ticks now();
    void lock();
    void unlock();

  private:
    // guards the system face itself; BASEPRI, nest-friendly like every
    // other lock in the family
    CortexM3CriticalSection guard_{};
};
```

Walk through the class skeleton once and all we see are lines of declarations — none of the eight actions carries a body; the method bodies are all homed in `system.cpp`. The header's own comments say as much: the class is a collection of declarations, and the secrets of registers and singletons cannot leak out of this header. The assertion that follows the class deserves a separate look:

```cpp
static_assert(ZerOS::system::SystemContext<CortexM3System>);
```

It is my favorite line: whether the class truly satisfies the eight groups of operations is accepted by the compiler at compile time — leave out any single function and the firmware will not even build. The top of `system.cpp` also states it plainly: it is the only place in the whole repository that reaches for both singletons at once:

```cpp
// defined in switch.cpp / time_kernel_cm3.cpp, declared no further
extern SystemScheduler system_sched;
extern SystemTimeKernel system_time;
```

Let us watch it forward the eight groups of operations one by one. We will pick the groups that have something to say; the plain forwarders all look the same, so they do not need the space:

```cpp
namespace {
// the wake reaction every timed sleeper on this chip shares: walk back
// from the alarm node to its owner and make it runnable again
void wake_from_sleep(ZerOS::base::BorrowedPtr<ZerOS::clock::TimeWaiter> waiter) {
    system_sched.ready(ZerOS::sched::TCB::owner_of(waiter));
}
} // namespace

void CortexM3System::sleep_for(base::BorrowedPtr<sched::TCB> t, clock::Ticks::tick_t span) {
    system_sched.sleep_for(t, system_time, span, wake_from_sleep);
}

clock::Ticks CortexM3System::now() {
    return system_time.current();
}

void CortexM3System::lock() {
    guard_.lock();
}
```

`sleep_for` is the only one of the eight that hands a reaction function over to the scheduler — it passes the task, the time kernel, and wake_from_sleep across together. This wake_from_sleep has moved house a few times, so let us lay out its résumé: in the two scheduler articles, it was hand-rolled in the example's application code; in the previous installment on ThisTask and interrupt wake-ups, it moved into arch's this_task.cpp; in today's commit it moved again, into system.cpp, becoming the wake-up reaction shared by every timed sleeper on this chip. What it does has not changed: walk back from the alarm node to the TCB (`owner_of` takes the address of the embedded member and subtracts the offset), then put the task back on the ready queue. The real change on the application side is that the ISR no longer touches `system_sched`: the `g_task_c` global pointer from the previous revision was deleted, and the interrupt no longer cares where task c sleeps — it simply hands the unit to the semaphore.

With the seam in place, this commit also tore down the old declarations (the two `extern`s in `switch.hpp` and `time_kernel_cm3.hpp` were deleted). The comment spells it out as "the extern is nobody's business now": runtime users all go through the `system::os()` handover point. Look at `this_task.cpp` too — it changed as well. It used to know the two singletons itself and assemble its own wake-up reaction; now the only name the whole file knows is `os()`. The header moved house as well: this_task.hpp left the doorway of include/ZerOS and settled in kernel/sched's territory, which is where last installment's include-path fix finally lands. From now on the singletons are tucked into the arch .cpp family, and generic code that wants the system has exactly one entrance:

```cpp
namespace ZerOS::system {
// the handover point: generic code asks for THE system, never the parts
[[nodiscard]] inline arch::cortex_m3::CortexM3System& os() {
    return arch::cortex_m3::the_system;
}
} // namespace ZerOS::system
```

The host side's answer sheet is validated against the same concept: the stand-in inside `test_sync.cpp` is called `FakeSys`, it satisfies the same `SystemContext`, and standing guard over it in the file, guarding the very same thing, is a line reading `static_assert(ZerOS::system::SystemContext<FakeSys>)`. The stand-in makes sleep a host specialty, and the comment says it quite plainly: on real silicon this business runs in the gap while PendSV has the task stopped; on host it runs inside the belly of the blocking hook. How the stand-in gets used, we will see in the exam section.

How does a user of the synchronization primitive designate the system? The answer hides in `include/ZerOS/arch/arm_cortex_m3/sync.hpp` — the entire file holds a single alias:

```cpp
// The chip's own flavor of the sync family: systems travel in the TYPE,
// so applications spell a plain name and never a template argument.

using Semaphore = SemaphoreBase<arch::cortex_m3::CortexM3System>;
```

"Systems travel in the TYPE": on the board, writing `Semaphore` is enough — the template argument is fixed inside the alias; in the tests, what you write has to be `SemaphoreBase<FakeSys>`. One implementation, with each of the two worlds claiming its own system. The semaphore code we write runs on both ends, host and board, without changing a single character, and the confidence for that claim rests on the two answer sheets plus one concept we have seen in this section.

## P/V: The Counter Is the Only Truth

Before touching the code, let us state the job clearly — once it is clear, the code reads easily. The P and V in the heading are the semaphore's old names: P corresponds to `acquire`, whose job is taking one unit out of the counter; V corresponds to `release`, whose job is putting one unit back into the counter. The opening paragraph's demand — "record the fact that something happened as a number" — lands on code as the cooperation of these two. The hard half is the taking: if the count is still zero, the taker has to go to sleep and wait for a return. The naive version is "take one look, sleep if empty" — split into two free-standing actions, the look and the sleep leave a gap in between: at the moment of the look the unit is still there, and a heartbeat before the sleeper actually sleeps someone else claims it, so the wake-up comes to nothing. The lost-byte accident from the opening paragraph is born in exactly that gap. So taking a unit must be done holding a critical section: we make checking the count, decrementing the unit, linking onto the wait chain, and going to sleep — all four steps — happen inside the same critical section. And waking up does not count as getting one: the woken person has to re-check the count, and only a successful check counts. The subtlety in there will become clear when we reach the `release` paragraph.

Now the protagonist walks on stage: `acquire` from `include/ZerOS/kernel/sync/semaphore.hpp`, the whole thing:

```cpp
    void acquire() noexcept {
        auto& system = System::self();
        const auto& current_task_ = system.current_task();
        SemaWaiter me{current_task_.get()};

        while (1) {
            ZerOS::irq::CriticalGuard g{system};
            if (count_ > 0) {
                --count_; // down self
                waiters_.remove(&me);
                return; // quit the issue
            } else {
                waiters_.insert(&me);
                system.block(current_task_);
            }
        }
    }
```

Let us follow one sleeping task through its fate. The count is zero, so it enters the `else` branch, wraps itself into a `SemaWaiter`, links onto the wait chain, then hands the job of blocking over to the system — and the task sleeps. At some later moment a unit arrives, the task is woken, and it resumes from where `block` returns. Notice where it runs back to: the top of `while (1)` — it never left the belly of the function. The woken person has to re-contend for the critical section and look at the counter again; if the count is still zero, it re-inserts itself into the wait chain and goes back to sleep. Being woken does not mean having gotten a unit: whether you got one is adjudicated by the counter, always. That is what "the loser of the race re-sleeps" means — the repository names it in the `release` comment as well; it is POSIX semaphore semantics.

Why might a woken person walk away empty-handed? One look at `release` settles it:

```cpp
    void release() noexcept {
        auto& system = System::self();
        ZerOS::irq::CriticalGuard g{system};
        ++count_; // the unit ALWAYS lands in the counter — woken sleepers
                  // race for it (loser re-sleeps, that is POSIX-semantics)
        if (auto head = waiters_.pop_head()) {
            // Some one is waiting!
            system.ready(head->task_);
        }
    }
```

The first action of `release` is `++count_`, and it is done inside the critical section. The comment shouts the design stance out loud: landing the unit in the counter is always the first step; the woken sleepers race for it, and the losers go back to sleep. This ordering is the watershed between losing and not losing. Landing the unit in the counter comes first, registering the wake-up comes second — so even if the wake-up registration goes wrong for any reason, the unit is already lying in the counter, and a later `acquire` can claim it just by checking the count. The event-arrives-before-the-task scenario survives this way. Reverse the order — wake people without landing the unit — and once a wake-up comes to nothing, the unit evaporates with it. If the wait chain holds anyone, `pop_head` wakes the head of the queue; if not, the unit lands and nobody is called. `release` never asks a single word about who is calling it: calling it from an interrupt is just as safe. We will cash that in the UART section.

There is one more detail I would ask you to notice: `SemaWaiter me` is a stack local, freshly built on every entry into `acquire`. The wait chain is intrusive (`SemaWaiter` carries `SelfNode`'s `next_` on its back); while the person sleeps the stack does not move, so the node hangs securely on the chain; once woken, `waiters_.remove(&me)` unhooks itself, and the moment the frame pops, the node is gone with it. The zero-heap discipline is not broken here either.

## Timed Waiting: Re-arm with the Remaining Amount, Accumulate No Drift

That concludes the unbounded wait; a wait with a deadline is the same loop plus an alarm clock. The naive approach sets the alarm to the parameter's original value: get interrupted once during the sleep, and it sleeps the whole span again at the original value — played twice, the actual wait drifts toward ten milliseconds (taking the five milliseconds from the test case as the example; the numbers are hypothetical). The repository's answer is to give the wait a deadline: the moment of entering the function plus the duration, and from then on every round sleeps only the amount that remains. The deadline never moves from beginning to end, so drift has nowhere to pile up — its name has already appeared in this section's title: "re-arm with the remaining amount". The function's head fixes the deadline; let us look:

```cpp
    SyncError try_acquire(const clock::Milliseconds t) noexcept {
        auto& system = System::self();
        const auto& current_task_ = system.current_task();
        SemaWaiter me{current_task_.get()};
        const auto ddl = system.now().tick_ + t.count;
```

`ddl` is settled at the moment of entry: it records the "now plus duration" deadline and never moves afterward. But it is only the finish line — how long each round of the loop should sleep depends on how far we currently are from the deadline, and computing the remainder happens entirely inside the loop. Let us read on:

```cpp
        while (1) {
            ZerOS::irq::CriticalGuard g{system};
            if (count_ > 0) {
                --count_;
                waiters_.remove(&me);
                return SyncError::Ok;
            }
            const auto left = static_cast<clock::Ticks::tick_diff_t>(ddl - system.now().tick_);
            if (left <= 0) {
                waiters_.remove(&me);
                return SyncError::TimedOut;
            }
            waiters_.insert(&me);
            system.sleep_for(system.current_task(), static_cast<clock::Ticks::tick_t>(left));
        }
    }
```

Inside the loop, `left` computes exactly that remainder. Look at its expression: take the deadline minus now, so each sleep shrinks to the part that is left. Why insist on the remaining amount? Because a round of waiting can be interrupted midway — no unit arrived, yet the system woke the task anyway (perhaps someone else's `release` happened to wake it along the way, perhaps some other cause fished it out of its sleep). It returns to the top of the loop, checks, and the count is still zero. If the duration it then sleeps were the original parameter all over again, the total deadline would be quietly stretched; the more interruptions, the larger the accumulated error grows. Re-arming with the remaining amount means: no matter how many times it gets fished out along the way, the deadline never budges, the expiry verdict is always `left <= 0`, and drift has nowhere to accumulate.

One spot in the loop remains untold, and it pairs with one more line this commit changed. The time kernel's `enqueue_locked` gained one line in this commit; I moved the relevant lines over from the diff verbatim — the shape of the signature you can match against the repository at a glance:

```diff
     void enqueue_locked(base::BorrowedPtr<TimeWaiter> task, Ticks::tick_t span,
                         TimeWaiter::OnTimeAction wake) {
+        queue_.remove(task);
         if (wake != nullptr) {
             task->OnTime = wake;
         }
```

What this line deletes is the old alarm. Let us walk the scenario once. A task in a timed wait may still have the alarm from its previous round unexpired when the unit arrives; the task wins this round and immediately starts a new round of timed waiting. If the old alarm node were still left in the queue, it would fire once more at its appointed time and jolt whoever happens to be waiting on something else. The re-arm path takes this line in: remove the old one, then enqueue the new one — for any single task in the wait queue, there is never more than one node under its name.

::: warning
The computation of `left` contains a type conversion worth keeping an eye on when you write this kind of code: `ddl - now` yields an unsigned tick difference, and we cast it to `tick_diff_t` (signed) before testing `<= 0`, so the verdict does not go wrong even when wraparound arithmetic carries the deadline comparison around. Compare directly as unsigned, and one wraparound flips the verdict. The wraparound arithmetic we spoke of in the timekeeping article is at work here again.
:::

## The Host Exam: 188 Lines of test_sync

Every corner of the mechanism comes with a test case in the repository. Check out `5c62c0d` and bring the host build up:

```shell
cmake -B build-host -DZEROS_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

My machine runs GCC 16.2.1 with CMake 4.4.4; no compilation surprises this time. The `ctest` report card is all eight targets passing:

```text
100% tests passed out of 8

Total Test time (real) =   0.16 sec
```

The newly delivered `test_sync.cpp` totals 188 lines; here is its own report card:

```text
All tests passed (20 assertions in 7 test cases)
```

What makes the test file interesting is its stand-in design. Here are the key lines of `FakeSys`:

```cpp
    static inline std::function<void()> during_sleep = nullptr;

    void block(BorrowedPtr<TCB> t) {
        sched_->block(t);
        if (during_sleep != nullptr) {
            during_sleep();
        }
    }
```

`during_sleep` is a hook the test fills in itself: the moment the task goes to sleep, the hook runs the rest of the world on its behalf. It lets a single-task unit test stage a two-hander — the task sleeps, the "whole world" inside the hook releases one unit, the task is woken back up, and the assertion confirms the unit it got is that unit. The beauty of this style is that the unit test never spawns a real thread; the entire timeline is held in our hands.

The test cases line up one-to-one with the body of the text; we will pick four. The `World` in the test cases is a fixture built fresh for each case: it assembles the scheduler and the time kernel together, provisions a ready-made task, wires them into FakeSys at construction, and selects that task as current — so when we write a test case, all that is left is to play out the plot. The first one validates the "release first" ordering:

```cpp
TEST_CASE("release first: acquire takes the fast path, nobody sleeps", "[sync]") {
    World w;
    Sem sem{0};

    sem.release();
    CHECK(sem.try_acquire() == SyncError::Ok);
    sem.release();
    CHECK(sem.try_acquire(Milliseconds{7}) == SyncError::Ok); // fast path ignores time
    CHECK(sem.try_acquire() == SyncError::TimedOut);           // drained
}
```

When the unit was released, nobody was waiting, so it lay in the counter; the later `try_acquire` claimed it straight off the fast path without touching the clock even for a zero deadline. Once claimed, the count returns to zero, and the next attempt gets `TimedOut` — one in, one out, the numbers balance to the digit. What we just tallied is the host-side testimony of the very sentence we were after: however early the event arrives, it must not be lost.

The second plays out the full arc of sleeping and waking; here is the test case:

```cpp
TEST_CASE("acquire sleeps, release lands while asleep, wake and take", "[sync]") {
    World w;
    Sem sem{0};

    FakeSys::during_sleep = [&sem] { sem.release(); }; // the world releases while I park
    sem.acquire();                                     // returns only after the world gave a unit
    // host: the task object sits Ready until someone picks it again
    REQUIRE(w.sched.pick_next().get() == &w.task.tcb);
    CHECK(ZerOS::sched::zeros_impl::TCBKeys::state(w.task.tcb) == TaskState::Running);
}
```

Watch the plot of this one: the task sleeps, the world inside the hook releases one unit, the task is woken, and `acquire` returns; the assertions confirm the task is back to `Running` and has indeed returned to the queue. The comment "returns only after the world gave a unit" is the test's own verdict on `acquire` — the moment it returns is the moment the unit is honored.

The third is the timed wait's alarm clock; here it is:

```cpp
TEST_CASE("timed acquire times out through the alarm", "[sync]") {
    World w;
    Sem sem{0};

    FakeSys::during_sleep = [&w] { w.time.on_elapsed(5); }; // the clock runs out
    CHECK(sem.try_acquire(Milliseconds{5}) == SyncError::TimedOut);
}
```

This time what the hook pushes is the clock: five ticks elapse, the alarm reaches its mark, and `try_acquire` honestly returns `TimedOut`. The remaining test cases sweep the corners too; let us name them once. A zero-deadline `try_acquire` is not allowed to sleep at all — the assertion checked that the task's state still sits at `Running`. When a unit and an alarm arrive at the same instant, the right to judge the outcome is in the counter's hands. The unbounded `acquire` leaves no seat for the alarm at all: the test case advanced one hundred ticks and the task is still `Blocked`, and the comment is blunt enough — even a century going by should not bring anyone to call it. And there is dedicated testimony for this commit's `queue_.remove(task)` line: the old alarm node must be forgotten for the new round of timed waiting to be clean. We will not recite these one by one — open the file once and you will see them all.

## Renode: The UART Echo Switches to Semaphore Drive

With the mechanism accepted, let us run the board side in Renode. The example's `main.cpp` gained a third task in this commit; we will read it in three parts. The first part is the global estate, the registry of event and payload:

```cpp
// the event side lives in the semaphore (a unit arriving BEFORE the task
// sleeps survives — the hand-rolled ready() flag could not do that);
// the byte itself is only the payload, still a one-slot mailbox
constinit ZerOS::sync::Semaphore g_rx_sem{0};
constinit ZerOS::arch::cortex_m3::CortexM3CriticalSection mailbox_lock{};
char g_rx_byte = 0;
```

Event and payload separate here: the event goes to the semaphore, while the byte itself remains a one-slot mailbox — the division of labor is written out plainly in the three comment lines at the top. With the registry in place, let us meet the one who collects the goods:

```cpp
// event-sleeper: woken by the RX interrupt, not by the clock
void task_c(void*) {
    for (;;) {
        g_rx_sem.acquire();
        char c;
        {
            ZerOS::irq::CriticalGuard guard{mailbox_lock};
            c = g_rx_byte;
        }
        ZerOS::board::print("got '");
        ZerOS::board::uart1_putc(c);
        ZerOS::board::print("'\r\n");
    }
}
```

Only after `acquire` yields a unit does it go to the mailbox to collect the goods; the moment of collection is guarded by a critical section, and once collected, the byte is echoed back verbatim. The remaining question is the interrupt's end: what lets it call `release` with confidence? Here:

```cpp
// RXNE (and ORE, sharing RXNEIE): park the byte, hand the event over.
// release() is ISR-safe by construction (BASEPRI inside, switch pended,
// never switched here)
extern "C" void USART1_IRQHandler() {
    if ((USART1->SR & USART_SR_RXNE) == 0) {
        return;
    }
    const char c = ZerOS::board::uart1_getc();
    {
        ZerOS::irq::CriticalGuard guard{mailbox_lock};
        g_rx_byte = c;
    }
    g_rx_sem.release();
}
```

The comment gives the answer: the lock is implemented with BASEPRI, the switch is pended to PendSV, and an ISR never actually switches tasks. It parks the byte in the mailbox, then `release`s one unit's worth — and the event's registration is complete. There is one more layer, which our body text already covered: `release` lands the unit in the counter before waking anyone. So even if the byte arrives while the task is still busy running something else, not yet asleep inside `acquire`, the unit is already waiting in the counter for it — not a single one can be lost.

I exported the tree at `5c62c0d` into a /tmp copy with `git archive` and built the firmware with the cross toolchain:

```shell
cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/arch/arm-none-eabi.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The size report is real; I paste it below:

```text
FLASH:        2564 B        64 KB      3.91%
  RAM:        1208 B        20 KB      5.90%
size report (D12 预算:核心内核 flash<=6KB RAM<=1KB)
   text	   data	    bss	    dec	    hex	filename
   2372	     192	   1016	   3580	    dfc	/tmp/zeros-t5c62/build/src/board/stm32f103_bluepill/zeros-bluepill.elf
```

I counted: the semaphore, the system face, and the third task all moved in — flash used 2564 bytes, RAM took 1208 bytes, still far from the D12 budget line (flash 6KB, RAM 1KB).

At the Renode step, I have to confess to some wrestling, honestly. My local Renode 1.17.0 does not recognize the `I2C.STM32F4_I2C` in `bluepill.repl` — we met that in the previous installment already, and I commented out the i2c1 and i2c2 nodes (the demo does not go through i2c, so behavior is unaffected). This round added one new problem: task c's echo only plays out if someone stuffs bytes into the serial port, and nowhere in 1.17.0's monitor commands could I find a way to inject received bytes into a UART — the `showAnalyzer` window is look-only, not write. My way out was to wire the emulated serial port to a socket terminal, then push characters into the socket with a few lines of Python; the board then treats the socket's peer as the other end of the serial line:

```text
emulation CreateServerSocketTerminal 34567 "term" false
connector Connect sysbus.usart1 term
```

After that we `start`, wait for A's and B's rhythm to settle, then connect to 127.0.0.1:34567 from the machine we develop on. The sending script is the few lines below — take it, change the characters, and you can reproduce the run:

```python
import socket, time

s = socket.create_connection(("127.0.0.1", 34567))
for ch in b"hZ!":
    s.send(bytes([ch]))  # h、Z、! 一个一个来
    time.sleep(1.0)      # 彼此隔一秒
```

The characters I sent were, in order, `h`, `Z`, `!`, spaced one second apart. An excerpt of the real serial-window output follows; the send times land at roughly 5.1, 6.1, and 7.1 seconds of emulation:

```text
[INFO] usart1: [host:    4.22s (+0.5s)|virt:         4s (+0.5s)] A
[INFO] usart1: [host:    4.42s (+0.2s)|virt:       4.2s (+0.2s)] B
[INFO] usart1: [host:    5.12s (+0.7s)|virt:       4.9s (+0.7s)] B
[INFO] usart1: [host:   5.22s (+99.95ms)|virt:        5s (+100ms)] A
[INFO] usart1: [host:  5.31s (+90.09ms)|virt:   5.09s (+85.35ms)] got 'h'
[INFO] usart1: [host:   5.82s (+0.51s)|virt:      5.6s (+0.51s)] B
[INFO] usart1: [host:    6.22s (+0.4s)|virt:         6s (+0.3s)] A
[INFO] usart1: [host:  6.3s (+81.32ms)|virt:   6.08s (+81.16ms)] got 'Z'
[INFO] usart1: [host:   6.52s (+0.22s)|virt:      6.3s (+0.22s)] B
[INFO] usart1: [host:    7.22s (+0.7s)|virt:       7s (+0.7s)] A
[INFO] usart1: [host:  7.22s (+0.14ms)|virt:       7s (+1.96µs)] B
[INFO] usart1: [host:  7.3s (+81.09ms)|virt:   7.08s (+81.37ms)] got '!'
```

Here are the acceptance facts we read out of that output. Count them: three characters matched three `got`s, not one was lost, and each slipped back inside a gap between A and B. For the echo latency, look at the virt column: `h` was sent at 5.09 seconds and the echo appeared almost simultaneously — the moment the byte arrived, USART1's interrupt fired at once, `release` landed a unit in the counter, and priority-0 task_c, more urgent than A and B with their priority-1 badges, was made `ready`; the next `pick_next` belonged to it, it took the byte out and echoed it, then went back to sleep in its `acquire`. The whole chain is an interrupt waking a task; the clock never took part, from beginning to end. One more match with our body text: none of the three echo moments disturbed A and B — they kept printing a line every 0.7 and 1 seconds as before. task_c grabbed only its one slice, and handed the CPU straight back.

## Acceptance

Let us gather this article's verifiable facts: three in total, one at a time.

The first is the full pass on host. Check out `5c62c0d` (the working tree under third_party is a gitfile; export the tree with `git archive 5c62c0d` and you are set), configure the host build with the three commands from the body, and `ctest` should hand you all eight targets passing. `test_sync`'s own report card is seven test cases and twenty assertions — the same numbers I got.

The second is one codebase across two worlds. The concept lives in `system_concept.hpp`; the board's answer sheet carries `static_assert(ZerOS::system::SystemContext<CortexM3System>)`, and host's answer sheet carries `static_assert(ZerOS::system::SystemContext<FakeSys>)`. Delete any single function from either sheet and those two assertion lines stop you on the compiler's behalf. The alias in `sync.hpp` is the spot where the two ends share one implementation: `SemaphoreBase<FakeSys>` and `Semaphore` run the very same `acquire` and `release`.

The third is the echo on the board. Produce the firmware with the same cross-build commands; once Renode is up, we wire the serial port to the socket terminal, and whatever we send in comes back. Following the same steps, you should get exactly the shape of the serial-window excerpt we pasted: three characters exchanged for three echoes, A's and B's rhythm untouched. What I did not verify, I say honestly as well: what we verified today is that events are not lost. The mutex half of it (ownership, handover, priority inheritance) is in `7dbbcdf`, the territory of the sister article *The Three Seconds M Goes Silent* (《看 M 闭嘴那三秒》) — the counter is only the foundation beneath its feet, and we will take it apart there.

## Next Installment

Counting records events, but it does not recognize people: who ends up acquiring a unit is something the one calling `release` can never know. When two tasks fight over the same peripheral, counting alone cannot stop them from reaching in at the same time. The next installment, *The Three Seconds M Goes Silent* (《看 M 闭嘴那三秒》), adds ownership on top of the count: the wait chain is ordered by priority, unlocking hands ownership directly to the waiter at the head of the queue (no return to open competition), plus single-level priority inheritance that temporarily raises the low task's priority, so the highest task stuck outside waits a little shorter. The finale is the UART theater staging the three tasks L, M, and H — the three seconds when M's printing suddenly falls silent is inheritance doing its work. See you there.
