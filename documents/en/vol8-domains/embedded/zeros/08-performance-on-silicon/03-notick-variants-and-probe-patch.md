---
title: "Removing the Heartbeat for Experimentation: NO_TICK, Zombie Tasks, and Probes as Patches"
description: "fe270d9(feat: rollback perf codings as patches) wraps up the performance line: demo::setup removes SysTick for variability testing, uncovering two latent bugs on the spot — the trap that immediately wfi monopolizes the CPU (masked by time slices when tick is present), and the unsigned wrap-around of a large deadline in wait_notify turning into the past (almost forever becomes immediate wake); the PendSV probe is stripped from the mainline into scripts/patches/pendsv-probe.patch, forgetting to apply it when toggling switches causes the build to fail immediately; I reproduced the failure and wrap-around in /tmp, the sixth round's board readings matched the archive verbatim: outlier 5 returned to zero, PendSV itself measured 93 cycles with zero variance"
chapter: 8
order: 3
tags:
  - stm32f1
  - advanced
  - embedded
  - cpp-modern
  - optimization
difficulty: advanced
platform: stm32f1
cpp_standard: [23]
reading_time_minutes: 22
prerequisites:
  - "Textbooks Lose to Prefetch Buffers: .ramfunc Reversal and an Honest Experiment Notebook"
related:
  - "Count to Ten and Switch: Peer Time Slices and Non-Inherited Slice Debt"
  - "Wake Everyone Up, Reinvestigate the Truth: Event Groups, Direct Notifications, and Full Ring Backpressure"
  - "The Definition of a Second: Tick, Wrap-Around Arithmetic, and Expiry Queues"
current_status:
  title: "Zombie Tasks Revealed"
  detail: "The ZerOS chapter is currently being polished"
  variant: polishing
  icon: 🧟
translation:
  source: documents/vol8-domains/embedded/zeros/08-performance-on-silicon/03-notick-variants-and-probe-patch.md
  source_hash: 5db06bd79e7dd358c4a9dc1893ac9969b8cf4b8aaaa6c19a6190f5fee53ff3ad
  translated_at: '2026-11-22T14:04:45+00:00'
  engine: qwen3:14b
  token_count: 14881
---
# Removing the Heartbeat for Experimentation: NO_TICK, Zombie Tasks, and Probes as Patches

After finishing the previous article, we now have an honest experiment notebook in our hands, with Flash and tail pointers reducing yield to 333 cycles throughout. However, next to the max tail of 529 in the fifth round, we both know there's a small line written in the experiment notebook: "max has delayed samples, possibly due to SysTick interference, unverified." Suspected issues cannot be left overnight, and `fe270d9` is here to close the case. I counted its diff, and the changes are spread across 8 files, adding 311 lines and removing 43 lines, with the subject being "feat: rollback perf codings as patches." It didn't leave the PendSV probe in the mainline, but instead peeled it off into a library file `scripts/patches/pendsv-probe.patch`.

What we're doing in this article is called variability testing, and its method is to deliberately change a property of the system, and see where it breaks — the place where it breaks is where the latent bugs live. The property we're changing is the heartbeat itself. We turn on the `ZEROS_PERF_NO_TICK` switch, and SysTick is completely disabled, and the entire time service is absent. By removing the heartbeat, we immediately uncovered two latent bugs. The first is the trap that falls into after the task entry function returns, which monopolizes the CPU with an in-place wfi. The second is the unsigned wrap-around of the deadline, where the near-forever wish turns into an immediate wake. After the bugs were uncovered, the probe was attached to the library as a patch, and the sixth round's readings brought the outliers back to zero, PendSV's body measured 93 cycles, and the min, avg, and max were exactly the same, a zero-variance result. Let's go through them one by one.

## The Switch to Remove the Heartbeat: with_tick

The entry point for removing the heartbeat is in the opening ceremony of the demo. This commit added parameters to `demo_setup.hpp`, and we look at the modified function in full:

```cpp
// with_tick=false skips init_time entirely: no SysTick, nothing ticks.
// Only callers that depend on no time service may do this (the perf demo
// uses it to measure with tick interference switched off).
inline void setup(const char* name, bool with_tick = true) {
    board::clock_init();  // HSI 8MHz → PLL 72MHz — real silicon needs this
    board::uart1_init();  // after clock_init: BRR assumes 72MHz
    board::register_uart_channel(); // this board talks over UART1
    log::print("\r\nZerOS demo: {}\r\n", name);
    arch::cortex_m3::install_default_testaments(); // obligations, explicitly
    if (with_tick) {
        arch::cortex_m3::init_time(72'000'000 / 1000); // 1kHz tick
    }
}
```

Standing outside the line of `init_time`, when `with_tick` is set to false, the 1kHz SysTick cannot be activated, the kernel's expiry queue has no one to trigger, and the time slices have no one to count. The only interrupt that can wake up on its own along the entire chain is gone. The comment line "Only callers that depend on no time service may do this" draws a usage boundary, meaning that the caller who dares to remove the heartbeat must really not depend on time. The perf demo exactly meets this condition, and we'll see shortly how it changes the reporter from being woken up by a timer to being woken up by a notification.

Looking at the `main.cpp` side, the `main` function splits into branches based on the switch:

```cpp
#ifdef ZEROS_PERF_NO_TICK
    ZerOS::demo::setup("performance (no tick)", false);
#else
    ZerOS::demo::setup("performance");
#endif
```

We leave the switch unopened, and everything runs the same as in the previous article, and the demo remains a regular citizen in the CI. When the switch is opened, it becomes a system without a time dimension, and such a system is precisely the magnifying glass for testing latent bugs.

## The Address of the Zombie Task: Trap in-place wfi

The first bug uncovered by removing the heartbeat lives on the path of task exit. Remembering the "Fabricating a Birth Certificate at the Top of the Stack," the `fabricate_frame` function forges an exception frame at the top of the new task stack, and the `lr` is filled with the address of the Trap. Once the task entry function returns, the execution flow falls into the Trap. The Trap before this commit looked like this:

```cpp
extern "C" [[noreturn]] inline void Trap() {
    while (1) {
        asm volatile("wfi");
    }
}
```

Its meaning seems quite dignified: it's time to leave, no more work to do, sleep. However, looking at the `wfi` sleep method line by line, it is the ARMv7-M wait for interrupt instruction, the core sleeps down, and it can only wake up when an interrupt or exception comes. Only SysTick can wake it up. In a system with ticks, there is a tick every millisecond, and when the time slice is full, `on_tick` will request a switch. We've read the logic line by line in "Count to Ten and Switch," which will take the CPU away from the task and give the opportunity to others. The zombie's occupation of the CPU is masked by the time slice round by round, so no one has ever suspected.

Here we need to pause and accurately describe the boundary of the mask, because there is a judgment of an empty queue in `on_tick`, and it does not do a no-load switch when there are no others in the peer queue. So the complete condition for the time slice to save the day is that there are other tasks in the queue where the zombie is. The ping, pong, and reporter of the perf demo are squeezed into the same few levels, and the mask is valid. However, as long as the system is quiet enough to have no interrupts and no other ready tasks, this task is really not moveable by anyone.

The NO_TICK build removes the mask entirely, there is no SysTick, `on_tick` is also gone, no one will request a switch, and once the task's entry returns, it will permanently occupy the CPU, and the entire field will be silent. The archive records the reproduction, I read the original text to you: "Renode reproduction, after hanging, PC directly falls into the wfi loop of Trap." I did not rerun this scene in Renode, but we have already gathered enough evidence from the source code at the mechanism level, and I will classify it properly in the acceptance section.

The fix is in `switch.cpp`, and the Trap has changed from a small function in the header file to a formal function, we look at:

```cpp
// A returned task must LEAVE the cpu, not squat on it: a plain wfi here
// would only ever be pried loose by the next tick's time slice, so a
// system without SysTick wedges outright (caught live by the NO_TICK
// perf build, 2026-09-06). Block self — the scheduler moves on, and if
// nothing else is ready, the idle task's wfi is the honest terminal
// state. A stray wake re-enters the wfi loop below, equally harmless.
extern "C" [[noreturn]] void Trap() {
    system_sched.block(system_sched.current_task());
    while (true) {
        asm volatile("wfi");
    }
}
```

The change is actually only the line `block(current)` at the entrance. The task that has exited marks itself as Blocked, and the scheduler thus has a way to proceed, selecting the next person to take the stage. The comment defines the semantics of exiting as yielding. If there is no one else in the entire field, `pick_next` will bring out idle, and the idle task body is originally wfi, we've seen its origin in "Fabricating a Birth Certificate at the Top of the Stack," that kind of sleeping is the correct final state, because it indicates "no work to do," while the zombie's wfi indicates "occupying the position and pretending to sleep." Another line of comment worth your attention is: "A stray wake re-enters the wfi loop below, equally harmless," if an interrupt shakes the blocked task out of wfi, it wakes up and still stays in the loop, and sleeps again, so it is harmless.

## Deadline Wrap-Around: Near Forever Becomes Immediate Wake

The second bug's residence is closer to the time kernel. For waiting with timeout, we record a deadline, and the first thing we do every time we wake up is to check the mailbox and calculate the remaining time. When the remaining time runs out, we report a timeout, and if the message arrives, we receive it. The version of `wait_notify` with timeout is located in `system.cpp`, and it follows this loop. We move it here for you to read, and pay attention to the step where `ddl` is calculated, the problem is there.

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

The method of judging timeout is to judge the remaining time by the deadline. The first thing done upon entry is to set `ddl` to "current reading + span." We subtract `now` from `ddl` every time to calculate how much is left, and if the remaining time falls below zero, we report a timeout. `tick_t` is an unsigned 32-bit, `tick_diff_t` is a signed 32-bit, and the subtraction itself is wrap-around safe. After interpreting the difference as a signed number, the negative number means "ddl is in the past." We practiced this arithmetic in "The Definition of a Second," but the problem is in the addition step. When `span` becomes large enough to `0xFFFFFFFF`, `now + span` wraps around in 32 bits, and `ddl` falls one tick before `now`. What we want is "near forever," but what we calculate is "one tick before," and the remaining time immediately becomes negative, and the function returns a timeout immediately. The archive's judgment is quoted verbatim: "Wanting 'near forever,' but actually 'immediate wake'," and it is marked as a live case of the pending issue (pending 6) in the kernel. The question of whether the kernel should add saturation or assertions is left unresolved.

The mechanism of this bug lies in arithmetic, and it is unrelated to the speed of the clock, so we can run it on the host. I moved three lines of arithmetic from `system.cpp` into a small program, compiled with GCC 16.2.1, and the output is as follows:

```text
span=600000     ddl=601000 left=600000 -> 正常等待
span=4294967295 ddl=999 left=-1 -> 立刻超时返回
```

`now` is also taken as 1000, and the span of 10 minutes is exactly 600,000 ticks. The `ddl` calculated with a span of `0xFFFFFFFF` is 999, and the remaining time drops to -1, and the timeout is immediately judged. The avoidance method in the demo is also written as a comment in `main.cpp`, we look at the numbers it selects:

```cpp
// 10 minutes — far past this demo's lifetime. NOT the full 0xFFFFFFFF:
// the kernel arms a deadline as now + span, and that addition wraps
// around to "strictly in the past", firing the timeout at once (caught
// live on silicon + Renode). In ZEROS_PERF_NO_TICK builds the clock never
// advances anyway — the notify from pong is the only way out
constexpr Milliseconds kWaitForPong{600'000};
```

The reporter waits for the pong's completion notification, and the span given is 10 minutes, which is far beyond the lifespan of the demo, and avoids the wrap-around. The comment also explains another fact in the NO_TICK build, the clock reading is always static, and notify is the only way out, so the reporter's waiting relies on the direct notification primitive we installed in "Wake Everyone Up, Reinvestigate the Truth," and the timer is of no help here. This bug is not picky about the build, and it will also occur in systems with ticks, but the perf demo usually doesn't need such a large span, so it has never surfaced.

## Why the Probe Lives in a Patch, Not in the Mainline

After the bugs were uncovered, the other half of the work for this commit is to arrange a place for the measurement facilities. The probe is build-gated, the production build is zero-cost, but the measurement hooks in the hot path are still annoying to maintain. The archive's "Measurement Facilities' Fate" section writes this trade-off as a decision, I quote it for you: "Strip the mainline code clean, the probe lives in a patch with the library (`scripts/patches/pendsv-probe.patch`), the machine that needs to measure `git apply` again to turn on `ZEROS_MEASURE_PENDSV`, if you forget to apply, turning on the switch = link error, loud failure. All data has been archived, the mission of the patch is to be ready for retesting next time."

"Loud failure" is a deliberate design, turning configuration dependencies into build errors, not relying on documentation to remind. We look at the header of the patch file, it is itself a ready-made manual:

```text
# PendSV-only probe (documents/notes/perf_1.md, round 6)
#
# The mainline keeps the switch hot path measurement-clean; this patch adds
# two branch-free DWT snapshots around the PendSV handler body (~24 cycles
# of fixed probe overhead riding the measured path — subtract it when
# reading, the perf demo already does).
#
# Usage:
#   git apply scripts/patches/pendsv-probe.patch
#   cmake -B build-meas -DCMAKE_TOOLCHAIN_FILE=cmake/arch/arm-none-eabi.cmake \
#         -DZEROS_MEASURE_PENDSV=ON [-DZEROS_PERF_NO_TICK=ON]
#   ... measure ... then:  git apply -R scripts/patches/pendsv-probe.patch
#   (reverse-apply to undo — NOT git checkout, which would also wipe any
#    uncommitted work beside the patch)
#
# Turning ZEROS_MEASURE_PENDSV on WITHOUT applying this patch fails to
# build (the demo references the stamps, nothing defines them) —
# loudly, on purpose.
```

Even the warning for revoking the patch is written at the beginning, revoking the patch uses `git apply -R`, and don't use `git checkout`, because checkout will wipe out your uncommitted changes in the working directory. A patch file writes its usage, revocation method, and failure behavior all at the beginning, it is a document when it lies in the library, and it is code when it is applied.

The wiring on the switch side is in the perf's `CMakeLists.txt`, we look at the default values of the two options, each of them is factory-set to OFF:

```cmake
option(ZEROS_MEASURE_PENDSV "perf: probe the PendSV handler with DWT stamps" OFF)
option(ZEROS_PERF_NO_TICK "perf: run without SysTick to isolate tick interference" OFF)
if(ZEROS_MEASURE_PENDSV)
    target_compile_definitions(zeros-demo-perf PRIVATE ZEROS_MEASURE_PENDSV)
endif()
if(ZEROS_PERF_NO_TICK)
    target_compile_definitions(zeros-demo-perf PRIVATE ZEROS_PERF_NO_TICK)
endif()
```

The comment writes the prerequisite for turning on the probe, it says "REQUIRES patch applied first," and the consequence of violating it, we've tested it on site.

## I Personally Triggered the Loud Failure in /tmp

I exported a source tree in /tmp using `git archive fe270d9`, I didn't apply the patch, `ZEROS_MEASURE_PENDSV` was turned on, and when I issued the command to build the perf demo, the error came immediately, the original text is pasted for you:

```text
/tmp/zeros-tfe270d9/src/board/stm32f103_bluepill/example/perf/main.cpp:102:30:
    error: 'zeros_pendsv_exit' was not declared in this scope
  102 |         const auto handler = zeros_pendsv_exit - zeros_pendsv_enter;
      |                              ^~~~~~~~~~~~~~~~~
/tmp/zeros-tfe270d9/src/board/stm32f103_bluepill/example/perf/main.cpp:102:50:
    error: 'zeros_pendsv_enter' was not declared in this scope
  102 |         const auto handler = zeros_pendsv_exit - zeros_pendsv_enter;
      |                                                  ^~~~~~~~~~~~~~~~~~
make[3]: *** [src/board/stm32f103_bluepill/example/perf/CMakeFiles/zeros-demo-perf.dir/main.cpp.obj] Error 1
```

Here I need to truthfully add a line. The archive and the patch header say "link error," but my actual test failure occurred during the compilation phase, `main.cpp` references two stamp symbols, and there is no declaration of them in the mainline, the compiler immediately reported the name of the symbol. The original text in the patch header says "fails to build," this behavior of build failure is deliberately arranged, it is loud, immediate, and reports the symbol name. It ensures that you cannot quietly measure a set of fake data with probes, and the purpose is achieved. The direction is correct, the phase is during the compilation phase, I put both statements in front of you, you weigh them yourself.

We add the patch and rebuild, and it goes green. `git apply scripts/patches/pendsv-probe.patch` is applied cleanly, both switches are turned on, cross-compiled with arm-none-eabi-g++ 16.2.0, and the resulting size is real:

```text
   text	   data	    bss	    dec	    hex	filename
   5684	    492	   2132	   8308	   2074	build-probe/src/board/stm32f103_bluepill/example/perf/zeros-demo-perf.elf
```

By the way, I need to inform you of one more thing, the CMSIS vendor headers in the source tree are completed by checking out the main branch, and the submodule directory itself is empty, this step is consistent with the handling in the previous few articles.

## Dissecting the Patch: Two Unbranched Snapshots

The body of the probe, we look at the core fragments in the patch:

```c
extern "C" {
volatile std::uint32_t zeros_pendsv_enter = 0;
volatile std::uint32_t zeros_pendsv_exit = 0;
}

#ifdef ZEROS_MEASURE_PENDSV
#    define ZEROS_PENDSV_PROBE_IN           \
        "ldr     r1, =0xE0001004\n"         \
        "ldr     r1, [r1]\n"                \
        "ldr     r2, =zeros_pendsv_enter\n" \
        "str     r1, [r2]\n"
#    define ZEROS_PENDSV_PROBE_OUT         \
        "ldr     r1, =0xE0001004\n"        \
        "ldr     r1, [r1]\n"               \
        "ldr     r2, =zeros_pendsv_exit\n" \
        "str     r1, [r2]\n"
#else
#    define ZEROS_PENDSV_PROBE_IN ""
#    define ZEROS_PENDSV_PROBE_OUT ""
#endif
```

`0xE0001004` is the address of the CYCCNT register of DWT, we unlocked it in "Welding the Stopwatch into the Silicon: Clock Tree Climbs to 72MHz, Measuring the First Switch." Four assembly actions do one read and one store, with no branches in between, so the probe's overhead is fixed, about 24 cycles, and these 24 cycles ride on the path being measured, we need to subtract them when reading, fortunately, the demo does the subtraction for you. Why are the two stamps wrapped in `extern "C"`? Because bare assembly is addressed by the name of the symbol, and C++ ordinary symbols are renamed by name mangling, and assembly can't find them, `extern "C"` preserves the name. The declaration is at the end of `switch.hpp`, and the bare assembly writing is in `switch.cpp`, the declaration and definition are split into two files, this is also the mechanism for the mandatory explosion when the patch is forgotten, because the demo only includes the header file, and the declaration is only in the patch.

The position where the bare assembly is inserted, the patch uses a macro string concatenation method, we look at the modified PendSV:

```c
extern "C" ZEROS_RAMFUNC [[gnu::naked]] void PendSV_Handler() {
    asm volatile("cpsid   i\n" ZEROS_PENDSV_PROBE_IN "mrs     r0, psp\n"
                 "cbz     r0, 1f\n"
                 "stmdb   r0!, {r4-r11}\n"
                 "1:\n"
                 "bl      context_switch\n" ZEROS_PENDSV_PROBE_OUT "ldmia   r0!, {r4-r11}\n"
                 "msr     psp, r0\n"
                 "ldr     lr, =0xFFFFFFFD\n"
                 "cpsie   i\n"
                 "bx      lr\n");
}
```

The snapshot enters after `cpsid i` and before reading PSP, and exits after selecting the person and before popping the register, enclosing the body of the handler. When the switch is not opened, the two macros degenerate into empty strings, and the assembly is preserved exactly as in the mainline, this is the machine-level meaning of the "production build zero overhead" in build-gated, you look at the macro expansion, and the empty string adds nothing.

## The Sixth Round's Readings: Outliers Return to Zero, 93 Cycles Zero Variance

It's time for the numbers to take the stage, we put the numbers in front of you. The following readings are from the archive's real measurement `documents/notes/perf_1.md` sixth round, it's the author's operation at that time, applying the probe, turning on the `ZEROS_PERF_NO_TICK` build on the Blue Pill, and burning it into the real development board to run. I don't have a board on hand now, so each number has only been verified on the real development board, if you don't have a board, read the numbers and match the source as in the previous few articles.

```text
=== ZerOS Performance @ 72MHz ===
rounds: 1000
tick: OFF (no SysTick)

[1] yield full path (external DWT):
    min: 375 cycles (~5us)
    avg: 375 cycles (~5us)
    max: 385 cycles (~5us)
    outliers(>avg+128): 0 of 1000

[2] PendSV handler (probe, 24 cycles subtracted):
    net min: 93 cycles (~1us)
    net avg: 93 cycles (~1us)  budget: 72 = 1us
    net max: 93 cycles (~1us)
    raw avg: 117 cycles

[3] accounting: yield[1] - handler[2] = 282 cycles
    (API + queue + pend + exception entry/exit + waker prologue)
```

The archive's judgment has four points, I pass them on to you one by one, and I keep the numbers as they are. The tail of max has converged, from the archive's end retest table with the tick's 532 (the 529 of the fifth round's real measurement is in the same tail) down to 385, the outliers have changed from 5 to 0, with a 5 millisecond measurement window under 1kHz tick, the expected collision count is about 5 times, matching the 5/1000 reading of the outliers, the source of the tail has changed from guessing to matching arithmetic, the attribution of SysTick interference has now closed with experimental evidence, the archive itself calls it "final attribution established," I pass it on verbatim. 375 contains 24 cycles of probe overhead, subtracting it leaves about 351, which is within the range of jitter with the 336 of the tick version without the probe, the two measurements have thus testified to each other. PendSV's body is 93 cycles, and the min, avg, and max are exactly the same, a zero-variance result, the archive's original words are "zero variance itself is evidence: steady-state switching is a completely deterministic path, the measurement method is credible," raw 117 minus the probe's 24, the remaining is exactly 93. We look separately, the total of 375 is equal to 93 plus 282, the body of the handler only accounts for about a quarter, the main part is in API calls, queuing, suspension, exception entry and exit, and the first few instructions of the waker, if you want to optimize yield, the lever is not in the hands of PendSV.

We also need to honestly read the budget judgment, D12 requires that the switch does not exceed 1 microsecond (i.e., 72 cycles), 93 exceeds the budget by about 29%. The archive lists the D12's judgment as a to-do, according to the yield's full journey, it is FAIL, according to the handler's body, whether to relax the budget is left to the subsequent commit. The retest of the longest BASEPRI window is still pending, the method is written in the archive, the probe's patch can be reused, its mission of being ready for retesting next time is waiting there.

## Acceptance

We summarize the verifiable facts of this article into three items, I go through them one by one.

The first is the reproduction of the loud failure. You follow the steps in this article, keep the patch unapplied in any exported tree of `fe270d9`, and run the following configuration and build command:

```shell
cmake -B build-meas -DCMAKE_TOOLCHAIN_FILE=cmake/arch/arm-none-eabi.cmake -DZEROS_MEASURE_PENDSV=ON
cmake --build build-meas --target zeros-demo-perf
```

The compiler will report the undeclared `zeros_pendsv_enter` and `zeros_pendsv_exit`, and the original text of the error is consistent with what is in this article. After adding `git apply scripts/patches/pendsv-probe.patch`, the same build will pass cleanly, the size of my /tmp tree is text 5684, data 492, bss 2132, your numbers should match them.

The second is the reproduction of the wrap-around, its advantage is that it doesn't require any hardware, we can run it on the host. The arithmetic of `ddl = now + span` and `left = (int32_t)(ddl - now)` is unchanged from the original text in `system.cpp`, if the span is taken as `0xFFFFFFFF`, the remaining time becomes negative, and the timeout is immediately judged, the output is in the third section of this article.

The third is the board readings, I truthfully state the boundaries. The sixth round's 93/93/93, raw 117, 375, 385, and outliers 0/1000, along with the max 532 and outliers 5/1000 of the tick version, all come from the sixth round of the archive `perf_1.md`, verified only on the real development board, I can't reproduce it now, and you don't have a board either. The parts I can verify for you have been verified in the original text, the patch is applied flat, the switch is turned on, the failure error is triggered, the wrap-around arithmetic is run through on the host, and they all fall into the output of /tmp and the host.

The things I still have on my mind are also recorded here. The scene where the trap zombie monopolizes is not rerun in Renode, the evidence is a combination of source code mechanics and archive records. The judgment of D12's口径, whether wait_notify should be saturated or have assertions, and the retest of the BASEPRI window, the archive itself leaves them as to-dos, whoever takes them over is responsible for closing them, and on the day they are closed, the probe's patch is still waiting to be applied in `scripts/patches/`.
