---
title: "Sleeping Slows the Heartbeat: Tickless, Reading and Clearing Race Conditions, and 24-bit分期续杯"
description: "The second half of commit fb9072b: when all tasks are sleeping, the 1kHz heartbeat is still spinning, we let idle sleep by reprogramming SysTick to one-shot aligned with the next due point. The main body explains how next_due_in asks the expiry queue for remaining time, the economics of a 2-pulse threshold, 24-bit LOAD clamping and分期续杯, and the three-piece set of race conditions: reading and clearing COUNTFLAG, clearing pending ICPR, and manually settling elapsed time. In Renode, the same firmware runs 2000 periodic interrupts with the macro closed, and 0 periodic plus 12 one-shot with the macro open, four small sleeps with no discrepancies. CI only compiles the default configuration, the macro-open version is left for your experiments."
chapter: 9
order: 3
tags:
  - stm32f1
  - intermediate
  - 嵌入式
  - 优化
  - cpp-modern
difficulty: intermediate
platform: stm32f1
cpp_standard: [23]
reading_time_minutes: 22
prerequisites:
  - "The Definition of a Second: ticks, Wrap-around Arithmetic, and Expiry Queue"
  - "Waiting 300 milliseconds, Not Raising a Task: Software Timers and fn+void* Hard Currency"
related:
  - "Filling the Stack with Magic Numbers: Canaries Paint the Whole Stack, Stack Depth Determined by Evidence"
current_status:
  title: Heartbeat Learns to Nap
  detail: The ZerOS chapter is being polished
  variant: polishing
  icon: 🌙
translation:
  source: documents/vol8-domains/embedded/zeros/09-second-board-and-release/03-tickless-idle.md
  source_hash: 289fec0d1fdb19451111edb6bb4df4a10f8855600de1da2a23d9a2eef94ad162
  translated_at: '2026-11-22T14:09:25+00:00'
  engine: qwen3:14b
  token_count: 15627
---
# Sleeping Slows the Heartbeat: Tickless, Reading and Clearing Race Conditions, and 24-bit分期续杯

When we wrapped up the previous section, the software timer was already able to call people on the expiry queue at the right time. Our tasks slept for 500 milliseconds, and when the time came, the callback would speak up, and the cost of waiting was reduced to three flags and an embedded waiter. However, if you think a bit deeper about the firmware at that time, you will find a component that is still spinning: during the 500 milliseconds when the task is sleeping, the SysTick periodic heartbeat has not stopped at all. The 1kHz heartbeat still comes in every millisecond, and we counted 500 milliseconds, which means we had to endure 500 interrupts. And each time we woke up, only the idle task could take on the work. When someone is waiting, the heartbeat is a metronome for the tasks, but when no one is waiting, it becomes a spinning machine.

`fb9072b` changed this. It was a commit that introduced two features, with the subject being "feat: stack watermark, tickless", modifying 13 files, adding 431 lines, and deleting 7 lines. The half related to stack watermark belongs to the previous article "Filling the Stack with Magic Numbers", and today we are dealing with the other half: tickless idle. The approach is simple, but the implementation is full of race conditions. Before sleeping, the idle task asked the kernel a question: when is the next expiry due? Then it reprogrammed SysTick from periodic mode to one-shot (single trigger, stops after one trigger) aligned with that expiry point. After the one-shot triggered, it settled the elapsed ticks into the expiry queue in one go, and then restored the periodic ticks. When no one is waiting, it simply stops the counter, saving the heartbeat as well. As you read further, you will find that the most valuable part of this code is the three actions the author used to settle the race conditions when the defense and expiry are chasing each other.

## Calculating the Cost of the Spinning Heartbeat

The cost calculation is not complicated. Let's take the task from the previous article as an example. After calling `ThisTask::sleep_for(Milliseconds{500})`, the task enters a blocked state and is hung on the expiry queue. When the scheduler finds no one to run, it brings out the idle task. The idle task's body, as we saw in "Who is the Most Urgent, Who Takes the Stage", was only one line: `wfi`. When executed, the CPU stops and waits for any interrupt. When the interrupt comes, the SysTick service routine runs as usual, the kernel advances the tick, checks the expiry queue, finds that the head of the queue is still more than 400 milliseconds away from the expiry time, and returns without doing anything. PendSV looks at the ready queue, sees only idle, and switches back to continue its `wfi`. A complete cycle costs two exception entries and exits, but the work done is zero. And such a cycle repeats 500 times in 500 milliseconds.

Let's look at the cost in terms of frequency. From power-on to the end of the demo, the firmware goes through four small sleeps, each lasting 500 milliseconds, and all the heartbeat is handled by idle. Two seconds of sleep means 2000 cycles of spinning interrupts. Interrupts have a cost: the exception frame is pushed onto the stack, the service routine runs once, and after that, we have to restore the context again. Worse still, it pulls the CPU out of deep sleep repeatedly, and deep sleep on Cortex-M depends on a longer stay in `wfi` to fall asleep. The saved interrupts are the saved power, which is the original motivation for tickless appearing in every RTOS textbook. ZerOS cannot escape the same calculation, but its implementation is worth looking at line by line, because all the race conditions are hidden in the details we need to read.

## Asking the Expiry Queue for Remaining Time: next_due_in

Before sleeping, idle has only one question to answer: how long until the next expiry. Previously, the kernel could not answer this question, but `fb9072b` added a query interface to `Kernel<Driver>`. Let's look at the 15 lines added in `include/ZerOS/kernel/clock/kernel.hpp`:

```cpp
    // How far away the head of the wait queue fires, counted from now —
    // the answer an idle system wants before picking how deep to sleep.
    // false means nothing is waiting: no deadline exists at all.
    bool next_due_in(Ticks::tick_t& out) {
        irq::CriticalGuard guard{lowlevel_driver_};
        auto head = queue_.head();
        if (!head) {
            return false;
        }
        const auto remaining =
            static_cast<Ticks::tick_diff_t>(head->deadline_.tick_ - clock_.current().tick_);
        out = remaining > 0 ? static_cast<Ticks::tick_t>(remaining) : 0;
        return true;
    }
```

The comment accurately describes its identity: how far is the head of the waiting queue from now, which is the answer a sleeping system needs before deciding how deep to sleep. The return type is a boolean, and the remaining ticks are taken by reference. If the queue is empty, it returns false, meaning there is no expiry time at all, and there is no need to ask. The calculation of the difference should be familiar to you: `deadline_ - now` is converted to `tick_diff_t` and the sign is checked, which is the same as the wrap-around arithmetic in `is_after` from "The Definition of a Second". Even if the head of the queue has just crossed the wrap-around line, the sign of the difference still gives the correct order. If the head of the queue has already expired, the remaining value is clamped to zero and returned. The old rule of not discarding expired items is still in place here.

As usual, we also go through the reasons for entering the critical section: the queue and the current time are shared states between the ISR and the task. The tick interrupt can cut in at any time. If we read the head of the queue and someone changes the queue halfway, the number we get back will be broken. This interface is designed to give a self-consistent answer in one query, and the caller does not need to disable interrupts themselves, just like `current()`.

## Precautions Before Sleeping: The First Half of idle_sleep

With the interface in place, the other half of the changes is in `src/arch/arm_cortex_m3/time_kernel_cm3.cpp`. In `switch.cpp`, the idle task body is replaced from bare `wfi` to `idle_sleep()`, and the comment gives it the name of a bed. This function opens two critical sections, one for the precautions before sleeping, and one for the settlement after waking up. We look at them one by one. When `ZEROS_TICKLESS` is off, the entire function is just one bare `wfi`, equivalent to the old behavior bit by bit. What we look at below is the macro-on version. The precautions at the entrance have only two actions:

```cpp
    // ---- arming: one atomic section, races settled by draining ----
    asm volatile("cpsid i");
    drain_pending_expiry(1); // a periodic tick may have expired on our way in
```

As soon as the entrance is entered, interrupts are disabled, and then `drain_pending_expiry(1)` is called immediately. This step may seem redundant, but it is the easiest to miss in the precautions: when we return from the previous tick interrupt to this line, the time spent on the way may have already let a periodic tick expire, the counter has wrapped around, the interrupt has been suspended, but we have not had a chance to look at it yet. If the precautions skip this step, this expired but unprocessed time will be left hanging in the air. You will see later how the settlement catches it up. In terms of cost, it is actually not a loss. The amount of settlement passed in is one tick, and if one tick has expired, it is settled, and if not, we have only read the register once more.

We continue to ask the head of the queue, and the main branch of the precautions follows the rewriting:

```cpp
    std::uint32_t due = 0;
    const bool has_deadline = system_time.next_due_in(due);
    if (has_deadline && due > kTicklessThreshold) {
        // clamp to the 24-bit LOAD width; longer spans are split by the
        // idle loop itself — each expiry wakes us once to re-consult
        const std::uint32_t max_per_shot = 0xFFFFFFu / cycles_per_tick_;
        if (due > max_per_shot) {
            due = max_per_shot;
        }
        one_shot_ticks_ = due;
```

After getting `due`, the path splits into three branches. Only if the remaining time is greater than `kTicklessThreshold` is it worth reprogramming. The value of this threshold is 2, and the comment clearly explains the economics: it is not worth reprogramming a one-shot for an expiry point that is too close. The periodic tick itself is an extreme form of a one-shot split into one tick per period. Anyway, within one tick, there will be an interrupt to take our place, and the cost of reprogramming the register is wasted. If the threshold is passed, there is still a hardware barrier, and the comment gives it: `clamp to the 24-bit LOAD width`. The SysTick counter has only 24 bits, and the maximum value that can be written is `0xFFFFFF`. On this board, the main frequency is 72MHz, and one tick takes 72000 cycles. Dividing `0xFFFFFF` by that gives an upper limit of 233 ticks, which translates to 233 milliseconds. If the desired sleep is longer than 233 milliseconds, one one-shot cannot be loaded, and the code clamps it to the upper limit. The remaining span is not handled by hardware, but by the idle loop itself, which is divided into periods: one trigger, one wake-up, re-asking the head of the queue, and loading a new one. The comment calls this approach "分期续杯", and our title borrows the original wording from the comment. The honest cost is more wake-ups between periods, but each wake-up only does one thing: re-precaution. Compared to the periodic heartbeat that hits once every millisecond, it still saves two orders of magnitude.

We continue with the tail of the branch, which is three lines of register writes, followed by the two unselected branches and `wfi`:

```cpp
        *kSystRvr = due * cycles_per_tick_ - 1;
        *kSystCvr = 0;
        *kSystCsr = kSystEnable | kSystTickInt | kSystClkSource;
    } else if (!has_deadline) {
        *kSystCsr = kSystClkSource; // stop: nothing ever comes due
    }
    // else: due in 1..threshold ticks — leave the periodic table, wfi
    // wakes within one tick anyway
    asm volatile("cpsie i");

    asm volatile("wfi");
```

The action of reprogramming itself is three lines of register writes: the reload value register RVR is written with `due * cycles_per_tick_ - 1`, the current value register CVR is written with zero, and the control and status register CSR is written with the enable, interrupt, and clock source bits. The remaining two branches in the branch are also read. When no one is waiting, the branch for stopping the timer is taken, `*kSystCsr = kSystClkSource` clears the ENABLE bit, and the counter is completely stopped. The comment says that time has no debt to repay, and the periodic ticks are restored upon waking up. The situation in the middle, which is due in one or two ticks, does nothing, and the periodic table is left as is, because `wfi` will have an interrupt to take our place within one tick.

## The Three-Piece Set of Race Conditions: Reading and Clearing, Clearing Pending, and Manual Settlement

Now we take `drain_pending_expiry` out separately; it is the most important part of the entire tickless implementation:

```cpp
// Drain a possibly-pending SysTick expiry: withdraw the NVIC pending bit
// and, if the counter really wrapped, settle that elapsed time on the
// ledger by hand. Without this, an expiry racing our (atomic) re-arm
// would be misread: the handler would see one_shot_ticks_ set and settle
// a whole one-shot for what was a single periodic tick.
[[maybe_unused]] void drain_pending_expiry(std::uint32_t settle_ticks) {
    const bool wrapped = (*kSystCsr & kSystCountFlag) != 0; // read-clears
    *kNvicIcpr = kSystickIrqBit;                           // un-pend it
    if (wrapped && settle_ticks != 0) {
        ZerOS::arch::cortex_m3::system_time.on_elapsed(settle_ticks);
    }
}
```

To understand what race conditions it is preventing, we arrange the incident process. When the precautions are halfway through, a periodic tick expires: the counter counts to zero, COUNTFLAG is raised, and the pending bit in the NVIC for SysTick is set. However, we are currently holding the critical section, and interrupts cannot enter. We can only wait. The precautions continue with the second half, and `one_shot_ticks_` is written as 500, and RVR is changed to the reload value of 500 ticks. When our critical section is released, the pending SysTick interrupt enters, and the service routine sees the value of `one_shot_ticks_` as 500, treating it as a one-shot of 500 ticks that has expired, and settles 500 ticks in one go for the kernel. However, only one periodic tick has actually elapsed. Time is arbitrarily fast-forwarded by 499 ticks, the head of the waiting queue expires early, and the task that should sleep is awakened early. This error is deadly to find on the board, as it occurs with an unpredictable amount each time, and only occurs when the precautions and the expiry are squeezed into the same window.

The three-piece set is to block the path step by step. The first is to read bit 16 of CSR, which is where COUNTFLAG is located. Its semantics are defined by the ARMv7-M architecture manual: the counter has counted to zero since the last read. We read it and return 1, and the read itself clears it. So the local variable `wrapped` receives a one-time answer. This question of whether the tick has counted to zero is answered once, and the evidence will not be taken by others. We state the boundary of the口径 honestly: reading and clearing is the semantics defined in the manual, writing CSR and CVR also clears it, and our restore order depends on it. As for whether it is cleared during exception entry, the manual does not list exception entry as one of the behaviors that clear it. I understand it as "only read and write affect it", and this is the part of the entire article where I most want you to see it with your own eyes on a real development board. The NVIC model of the simulator may not replicate every edge case as per the manual.

The second is to clear pending. We continue to look. SysTick is numbered 15 in the NVIC exception list, and the ICPR register pointed to by `kNvicIcpr` clears the pending status bit by bit. Writing `1u << 15` clears the pending SysTick interrupt that has already been triggered. Why clear it? Because this expired tick is not going to take the normal interrupt path anymore, and its consequences are handled by the third piece: manual settlement. As long as the counter has actually wrapped around, we manually settle the elapsed ticks into the kernel, and `on_elapsed(settle_ticks)` is called once. The head of the queue expires, and the task that should be awakened is awakened, and the result is exactly the same as if it had taken the interrupt path itself. Clearing pending and manually settling ensures that nothing remains on the interrupt path, and there is no reason for later service routines to exist, and the misread entry is sealed.

Any one of the three pieces is missing, and it won't work. Let's imagine the consequences of missing any one of them. If we don't read COUNTFLAG, we can't tell whether the pending interrupt is one tick or a full one-shot. Clearing pending becomes swallowing time out of the blue. If we don't clear pending, after manually settling, the pending interrupt will still come in, and the service routine will settle it again based on the current state, and time will be settled twice. If we don't do manual settlement, the cleared pending will erase the real elapsed time, and the sleeping task will be overslept. Reading clears the evidence, clearing blocks the path, and settlement fills in the facts. Only when the three actions are combined can the complete judgment of the race between precautions and expiry be made.

## The Half Critical Section on Waking Up

After `wfi` wakes us up, the second half of the function takes over. Let's look at the original text:

```cpp
    // ---- wake: restore the periodic table, settle what we withdrew ----
    asm volatile("cpsid i");
    if (one_shot_ticks_ != 0) {
        // in flight (a peripheral woke us first, or it never fired):
        // settle honestly if it had fired, then take it off the books
        drain_pending_expiry(one_shot_ticks_);
        one_shot_ticks_ = 0;
    } else {
        // the handler already settled our one-shot — but a fresh
        // periodic tick may be pending since the restore
        drain_pending_expiry(1);
    }
    periodic_table_restore();
    asm volatile("cpsie i");
#endif
```

There are two reasons for waking up, and the function needs to distinguish their respective outcomes. One is when the peripheral wakes us up, such as when a byte comes in via UART, and the one-shot is still in flight. `one_shot_ticks_` is non-zero as evidence that it is still in flight. At this point, we go to `drain_pending_expiry(one_shot_ticks_)`: if the one-shot has already counted to zero, the entire number of ticks is honestly settled. If it hasn't counted to zero yet, at least the number of ticks that have expired during this period are settled, and `one_shot_ticks_` is cleared to zero, and the one-shot in flight is discarded. The other is when the one-shot itself wakes us up on time, and the service routine has already settled it. `one_shot_ticks_` is already zero, and we fall into the else branch. It seems redundant to drain again, but the comment explains the reason: after the service routine settles, it restores the periodic table. During the few microseconds between restoration and waking up, another periodic tick may have expired and been triggered. If we don't catch it, this tick will be lost.

There is one more point worth your attention: the two branches of drain are both placed before `periodic_table_restore()`. This is not written at random. The action of restoring the periodic table writes to CSR and CVR, and according to the manual, writing to CSR and CVR will clear COUNTFLAG. If we restore first and then read, the evidence will be destroyed by our own writes before we read it, and the settlement will never be clear. Reading and clearing is placed before the writes that clear it, and the basis of the discipline is the architecture manual itself, not the accidental behavior of any platform.

The restoration itself is a mirrored action of three lines, and we also finish reading `periodic_table_restore`:

```cpp
void periodic_table_restore() {
    if (cycles_per_tick_ == 0) {
        return;
    }
    *kSystRvr = cycles_per_tick_ - 1;
    *kSystCvr = 0; // any VAL write restarts the counter
    *kSystCsr = kSystEnable | kSystTickInt | kSystClkSource;
}
```

The three lines of restoration bring the periodic table registers back to the values written in `init_time`. The comment explains a Cortex-M common sense: writing any value to CVR will restart the counter from the reload value from the beginning. So `*kSystCvr = 0` does not clear the counter to zero, but it makes the beat start from a clean starting point again. The 1kHz heartbeat returns this way, and when the task is awakened from sleep, the timeline of the world has not lost a single tick.

## Diversion of SysTick_Handler

The one-shot expiration goes through the same service routine entry, but there is an extra branch at the entrance. Let's look:

```cpp
extern "C" void SysTick_Handler() {
    if (one_shot_ticks_ != 0) {
        // one-shot expiry: settle the whole span, back to periodic. No
        // on_tick() — the only runnable thing here is idle (nobody to
        // slice); if the settlement readied someone, PendSV is already
        // pending and the handover happens on our way out
        const auto n = one_shot_ticks_;
        one_shot_ticks_ = 0;
        periodic_table_restore();
        zeros_tick_oneshot = zeros_tick_oneshot + 1;
        ZerOS::arch::cortex_m3::system_time.on_elapsed(n);
        return;
    }
    zeros_tick_periodic = zeros_tick_periodic + 1;
    ZerOS::arch::cortex_m3::system_time.on_elapsed(1); // report the time...
    ZerOS::arch::cortex_m3::system_sched.on_tick();    // ...and the beat (slice)
}
```

The periodic path is still the old way, time is reported to the kernel, and the beat is handed over to the scheduler to switch tasks. The one-shot path does different things: it takes the number of ticks in flight, clears it to zero, restores the periodic table, and settles the entire number of ticks into the kernel in one go, and returns after that. The periodic path's `on_tick()` is intentionally skipped, and the comment gives three reasons: at this moment, only idle can run, and no one needs us to slice. If the settlement wakes up a sleeping task, the ready bitmap moves, and the PendSV pending bit is already set. The handover of switching does not need the service routine to worry about it, and PendSV itself takes over on our way out. This is the division of labor between timing and scheduling under tickless: the one-shot is an alarm clock, only responsible for settling the time and waking up the sleeping people, and it does not schedule.

The two counters `zeros_tick_periodic` and `zeros_tick_oneshot` are also added by this commit, and `extern "C"` hangs them out, so any demo can read their values by the bare symbol. They do not do any work for the kernel, their existence is to serve as evidence for observers, and we rely on them for our comparative experiments.

## Host Exam and Its Boundaries

The commit also submitted the white-box testing of the host, `test/test_stack_watermark.cpp` has four test cases and 41 assertions, verifying the stack watermark part, and the details are left to "Filling the Stack with Magic Numbers" to explain. We ran the full test on our local machine at `fb9072b`, using the toolchain GCC 16.2.1, and all 15 targets passed:

```text
100% tests passed out of 15

Total Test time (real) =   0.30 sec
```

However, the host exam cannot reach the tickless part, and we honestly draw the boundary: all the logic of `idle_sleep` is on inline assembly and MMIO registers, and the host surrogate has no shadow of SysTick, no trace of NVIC, and no corresponding object for `cpsid i`. Such code, the exam is on the simulator and the silicon chip, and the next comparative experiment is its acceptance.

::: warning
The settlement of the one-shot runs in the context of the SysTick interrupt, `on_elapsed(n)` settles hundreds of ticks in one go, and the head of the queue may execute several callbacks in one go. They are still subject to the old constraints from "Waiting 300 milliseconds": callbacks must be short, and heavy work is still handed over to the worker's queue as usual. Tickless changes the rhythm of interrupts, but does not relax the discipline of callbacks, and we must not forget it when writing callbacks.
:::

## Renode Comparison: Numbers on the Table

Our experimental chain remains the same, the approach is to export the tree from `git archive fb9072b` into a copy in /tmp, and copy STM32CubeF1 from the local third_party. The cross-toolchain used is arm-none-eabi-g++ 16.2.0, and we use four cmake commands to configure each other, all switches are concentrated on the same CMake option:

```shell
cmake -B build-off -DCMAKE_TOOLCHAIN_FILE=cmake/arch/arm-none-eabi.cmake -DCMAKE_BUILD_TYPE=Release
cmake -B build-on  -DCMAKE_TOOLCHAIN_FILE=cmake/arch/arm-none-eabi.cmake -DCMAKE_BUILD_TYPE=Release -DZEROS_TICKLESS=ON
cmake --build build-off --target zeros-demo-tickless
cmake --build build-on  --target zeros-demo-tickless
```

The size comparison is real, the cost of the macro-on is displayed in two size reports, we look:

```text
FLASH:        4656 B        64 KB      7.10%      # 默认配置
FLASH:        4864 B        64 KB      7.42%      # ZEROS_TICKLESS=ON
```

The 208-byte difference brings the full set of mechanisms for precautions, settlements, and restorations. We think this cost is worth it in the context of saving 2000 interrupts in two seconds, but whether it is worth it, you have to calculate it with your own budget.

The demo script is read from the source code: task napper sleeps four times, each time for 500 milliseconds, wakes up and stamps the time with the kernel's `now()`, and finally hands over the values of the two counters. The simulator is still Renode 1.17.0, and we run the firmware with the macro closed for six seconds, the real output of the serial port window is as follows:

```text
[INFO] usart1: [host: 0.12s (+0.5ms)|virt: 0s (+0s)] ZerOS demo: tickless
[INFO] usart1: [host: 0.59s (+0.48s)|virt: 0.5s (+0.5s)] nap 1 woke @ 500ms
[INFO] usart1: [host: 1.08s (+0.49s)|virt:   1s (+0.5s)] nap 2 woke @ 1000ms
[INFO] usart1: [host: 1.58s (+0.5s)|virt: 1.5s (+0.5s)] nap 3 woke @ 1500ms
[INFO] usart1: [host: 2.08s (+0.5s)|virt:   2s (+0.5s)] nap 4 woke @ 2000ms
[INFO] usart1: [host: 2.08s (+0.3ms)|virt:     2s (+0s)] tickless: witnessed
[INFO] usart1: [host: 2.08s (+0.3ms)|virt:     2s (+0s)] mode: periodic 1kHz (ZEROS_TICKLESS off)
[INFO] usart1: [host: 2.08s (+0.3ms)|virt:     2s (+0s)] periodic ticks: 2000
[INFO] usart1: [host: 2.08s (+0.2ms)|virt:     2s (+0s)] one-shot fires: 0
```

We count for two seconds and get 2000 periodic interrupts, and the one-shot never rings on our head, this is the scene of the heartbeat spinning. Switch to the macro-on firmware, the script is still our familiar script, and the simulator is still the one we used, the output becomes as follows:

```text
[INFO] usart1: [host: 0.13s (+1.4ms)|virt: 0s (+0s)] ZerOS demo: tickless
[INFO] usart1: [host:  0.6s (+0.47s)|virt: 0.5s (+0.5s)] nap 1 woke @ 500ms
[INFO] usart1: [host:  1.09s (+0.49s)|virt:   1s (+0.5s)] nap 2 woke @ 1000ms
[INFO] usart1: [host:  1.59s (+0.5s)|virt: 1.5s (+0.5s)] nap 3 woke @ 1500ms
[INFO] usart1: [host:  2.09s (+0.5s)|virt:   2s (+0.5s)] nap 4 woke @ 2000ms
[INFO] usart1: [host: 2.09s (+0.5ms)|virt:     2s (+0s)] tickless: witnessed
[INFO] usart1: [host: 2.09s (+0.3ms)|virt:     2s (+0s)] mode: tickless idle (one-shot to next deadline)
[INFO] usart1: [host: 2.09s (+0.4ms)|virt:     2s (+0s)] periodic ticks: 0
[INFO] usart1: [host: 2.09s (+0.2ms)|virt:     2s (+0s)] one-shot fires: 12
```

The periodic interrupts drop from 2000 to zero, and the one-shot fires 12 times. We break this number down for you: a 500-tick sleep, the single-shot maximum is 233 ticks, when it cannot be loaded, it is divided into periods. The first two shots each take 233 ticks, and we add a short 34-tick shot, three shots make up a full sleep, and we add up the four sleeps, exactly 12 shots. The saved is 2000, the cost is 12, the reduction is more than 99%. More importantly, the rhythm of the lines above: four small sleeps stamped with 500, 1000, 1500, 2000ms, no discrepancy with the macro-closed firmware. The timeline of sleep is not scattered by the分期, the settlement is realized here, and if the error of fast-forwarding 499 ticks is really there, these stamps would have been messed up.

We turn back to the simulator side and have two things to report honestly. The first is that my local Renode 1.17.0 cannot recognize the `I2C.STM32F4_I2C` type in `bluepill.repl`, and we comment out the two nodes i2c1 and i2c2 connected to the interrupt wiring in the copy. The demo does not go through the i2c path, and the behavior is unaffected. This hurdle was also encountered in the acceptance of "The Definition of a Second", and the same solution was used. The second is that all the numbers today come from the Renode model, and I have not run this layer of comparison on the real development board yet. The narrow window race condition where the precautions and expiry are squeezed into the same window may not be replicated by the simulator, and I mark this honestly.

We also honestly draw the boundary of CI. `scripts/ci.sh` added two smoke marks, one of which is "tickless: witnessed" for tickless, but ci.sh's configure never passed `-DZEROS_TICKLESS=ON`, so the tickless demo run in CI is actually in periodic mode, and the macro-on path is not covered by CI. This is intentionally left as your homework by the repository, not a mistake: you run the four cmake commands above, compare the two serial port outputs, and the difference between 2000 and 12 is right in your hands, the numbers are more persuasive than any of our verbal descriptions.

## Acceptance

The verifiable facts of this article are summarized into three items, and we go through them one by one.

The first is the two sets of builds and full testing. You check out to `fb9072b`, and compile the firmware with both switches on and off using the four commands above. The size reports should give you two numbers: 4656 and 4864 bytes. The host-side build runs with `-DZEROS_BUILD_TESTS=ON`, and all 15 targets pass, with `test_stack_watermark` having a scorecard of four test cases and 41 assertions.

The second is the Renode comparison. We are still in the same copy, and the macro-closed firmware runs 2000 periodic ticks and 0 one-shot fires, while the macro-open firmware runs 0 periodic ticks and 12 one-shot fires, with the four small sleeps stamped with no discrepancies. If you also encounter an I2C type error on Renode 1.17.0, comment out the two i2c nodes, and it will start. The composition of the 12 one-shots is 233 plus 233 plus 34. You can verify it yourself by calculating `0xFFFFFF / 72000`.

The third is the source code of the three-piece set of race conditions. You check the original text line by line in `src/arch/arm_cortex_m3/time_kernel_cm3.cpp`: COUNTFLAG is bit 16 of CSR, ICPR clears the pending bit for exception 15, and the settlement goes through `on_elapsed`. If you want to see the consequences of missing one of the three, delete the `drain_pending_expiry(1)` at the entrance of the precautions path and run it, the suspended periodic ticks will start to be lost, and this kind of change's destructive impact can be seen in Renode.

## Next Article

The heartbeat can now sleep, and the kernel's capabilities are complete. The next article, "Listing v0.1.0", is the final chapter of our handcrafted journey. `6c0a4f7` does not change any line of the kernel, but deals with the door-facing engineering before leaving: the structure of the README, bilingual and badges, the boundary of dual licenses, and the full-chain verification of the v0.1.0 tag. The host's ctest must run, and it must also run on Renode, QEMU, and the real development board, and all four exams must be covered. The endpoint is waiting for us there, see you there.
