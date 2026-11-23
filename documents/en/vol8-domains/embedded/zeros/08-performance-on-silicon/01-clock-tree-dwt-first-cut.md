---
title: "Welding the Stopwatch into Silicon: The Clock Tree Climbs to 72MHz, Measuring the First Context Switch"
description: "e2b7dc0(feat: optimize the sched performance) puts the project for the first time on real silicon: clock.hpp manually climbs the clock tree (HSE 8MHz via PLL×9 to 72MHz, with a detector that identifies the fake RCC in Renode), dwt.hpp uses the Cortex-M3's CYCCNT as a stopwatch, blink lets the scheduler perform its first context switch on real silicon. perf demo moves the DWT timestamp out of the kernel's hot path, letting the yielding thread write and the waking thread read, the average yield time in the warehouse archive drops from 489 cycles to 354, a 28% reduction. I ran through the 14 host-side tests, the cross-compiled size list, and the detector in Renode, while the simulation reads all CYCCNT values as 0, the only source of cycle counts is the archive measured on the real development board."
chapter: 8
order: 1
tags:
  - stm32f1
  - advanced
  - embedded
  - optimization
  - debugging
difficulty: advanced
platform: stm32f1
cpp_standard: [23]
reading_time_minutes: 24
prerequisites:
  - "One Room, One Scene: Demo Registry Center, Task Creation Three Paths and the Oath of the Last Words"
related:
  - "Forging a Birth Certificate on the Stack: PendSV, SVC, and the First Context Switch"
  - "The Textbook Loses to the Prefetch Buffer: .ramfunc Reversal and an Honest Experiment Notebook"
current_status:
  title: Stopwatch Installed in Silicon
  detail: The ZerOS chapter is currently being polished
  variant: polishing
  icon: ⏱
translation:
  source: documents/vol8-domains/embedded/zeros/08-performance-on-silicon/01-clock-tree-dwt-first-cut.md
  source_hash: fab97b2f32f7be049e8cc6d47c6c82840276b19e68ad280cf04252aa92c662c6
  translated_at: '2026-11-22T14:00:48+00:00'
  engine: qwen3:14b
  token_count: 16939
---
# Welding the Stopwatch into Silicon: The Clock Tree Climbs to 72MHz, Measuring the First Context Switch

At the end of "One Room, One Scene," the demo registry center was already in place, but it only had eight demos at the time. Blink and perf were newly registered by `e2b7dc0`, and we could generate firmware targets for them with just one line of `zeros_add_demo`. However, if you review the situation at that time, you'll find that we were missing something critical: the scheduler had switched several times in Renode, but we had no data on how many CPU cycles each switch actually took. Whether the switch was expensive or not, and whether the budget was met, we had to rely on our feelings, and feelings are the least reliable witnesses in performance matters.

`e2b7dc0` comes to answer this question. Its subject is "feat: optimize the sched performance," and I counted its diff, it touched 18 files, adding 548 lines and removing 14 lines. It brought three new components to the board level: `clock.hpp` for manually climbing the clock tree, `dwt.hpp` for using the CYCCNT as a stopwatch, and a concepts-based GPIO stack with the blink demo. It also made changes on the kernel side, moving the measurement code out of the hot path. The numbers are stored in the warehouse's own measurement archive, and the average yield time dropped from 489 cycles to 354.

Before writing, I need to clarify one point with you. The cycle counts on this site only come from the warehouse's measurement archive `documents/notes/perf_1.md`, which records the numbers measured on the Blue Pill at the time. I don't have a board on my desk now, so I use the original quotes from the archive in the perspective of "the author's operation" for burning, OpenOCD, and LED on/off. I don't fabricate anything that I can't compile. Everything I've truly run on your side, including host tests, cross-compilation, and parts that can run in Renode, I've pasted the real output. I've also verified the part where the simulation can't provide cycle counts in front of you.

## Climbing the Clock Tree Manually: HSE 8MHz to PLL×9

At the moment of power-on, the STM32F103 uses its internal 8MHz oscillator, known as HSI. The 72MHz we need doesn't fall from the sky; we have to connect the clock tree step by step: the external 8MHz crystal is called HSE, which is fed to the phase-locked loop (PLL, a circuit that multiplies the frequency), and after multiplying by 9, it produces exactly 72MHz. The newly added `clock.hpp` writes the entire tree as a manual configuration, and we'll look at it in several parts:

```cpp
inline void clock_init() {
    auto* const rcc_cr   = reinterpret_cast<volatile std::uint32_t*>(0x40021000);
    auto* const rcc_cfgr = reinterpret_cast<volatile std::uint32_t*>(0x40021004);
    auto* const flash_acr = reinterpret_cast<volatile std::uint32_t*>(0x40022000);
```

The first three lines of the function are the addresses of three registers, and we'll identify them one by one: the first two are the control and configuration registers of RCC (Reset and Clock Control), and RCC is the department in the chip that controls all clock switches, its Chinese name is Reset and Clock Controller. The remaining one is the ACR register of Flash itself. All our register accesses go through `volatile`, and the compiler is not allowed to optimize our reads and writes, because each of them carries a hardware side effect.

Next are the six steps of the tree, and we'll go through them one by one. The first two steps are to turn on the crystal and wait for it to stabilize:

```cpp
    // 1) 开 HSE(外部 8MHz 晶振),等就绪
    *rcc_cr |= (1u << 16); // HSEON
    while ((*rcc_cr & (1u << 17)) == 0) { // HSERDY
    }
```

Writing bit 16 of the control register to 1 starts the HSE oscillator, and the crystal takes some time to stabilize from power-on. The hardware sets a ready flag on bit 17, and our loop waits for it. Since the hardware decides when it's ready, we can only wait.

After turning on the crystal, the next step is to adjust the Flash, and we'll look at it:

```cpp
    // 2) Flash 等待周期:72MHz 需要 2 wait states
    *flash_acr = (*flash_acr & ~0x7u) | 0x2u;
```

The Flash's read speed can't keep up with the 72MHz CPU. As the frequency increases, the cost of fetching an instruction is that the CPU has to wait two clock cycles, which we call two wait states. This step must be placed before increasing the frequency. If the order is reversed, the CPU will use the high frequency to read the Flash that can't keep up, and the firmware will die on the spot.

Next, we divide the bus and peripheral frequencies, and we'll continue:

```cpp
    // 3) AHB 不分频(72MHz),APB1 ÷2(36MHz,最大),APB2 不分频(72MHz)
    //    CFGR: HPRE=0 (÷1), PPRE1=100 (÷2), PPRE2=0 (÷1)
    *rcc_cfgr = (*rcc_cfgr & ~(0xFu << 4) & ~(0x7u << 8) & ~(0x7u << 11))
              | (0x4u << 8); // PPRE1 = ÷2
```

The 72MHz CPU is directly supplied to the AHB bus. We see that the low-speed peripherals hanging on APB1 can only take up to 36MHz, so it goes ÷2 there, while the UART 1 is on APB2, and it runs at full 72MHz.

The remaining three steps are to bring out the PLL and switch the system clock:

```cpp
    // 4) PLL:HSE × 9 = 72MHz
    //    CFGR: PLLSRC=1 (HSE), PLLXTPRE=0 (不÷2), PLLMUL=0111 (×9)
    *rcc_cfgr = (*rcc_cfgr & ~((1u << 16) | (1u << 17) | (0xFu << 18)))
              | (1u << 16)    // PLLSRC = HSE
              | (0x7u << 18); // PLLMUL = ×9

    // 5) 开 PLL,等就绪
    *rcc_cr |= (1u << 24); // PLLON
    while ((*rcc_cr & (1u << 25)) == 0) { // PLLRDY
    }

    // 6) 切系统时钟到 PLL 输出,等切换完成
    *rcc_cfgr = (*rcc_cfgr & ~0x3u) | 0x2u; // SW = PLL
    while ((*rcc_cfgr & (0x3u << 2)) != (0x2u << 2)) { // SWS = PLL
    }
```

The PLL's clock source is selected as HSE, and the multiplication factor is selected as 9, resulting in 8 × 9 = 72. The PLL also needs time to start oscillating, so we wait for its ready bit again. The system clock selection field is written as 2 to switch to the PLL, and the final read confirms the SWS (system clock switch status), and the switch is complete only when the mark changes. The write and read match, and the tree is considered climbed. I've cross-checked the bit numbers with the STM32F1 reference manual's RCC chapter, and the `clock.hpp` comments also write the meaning of each bit segment, and both sides are consistent.

What happens if we skip it? The file header comment in `clock.hpp` is straightforward: this file is only used in real silicon. If we don't call `clock_init`, all assumptions about 72MHz (SysTick, UART baud rate, delay) will run at 8MHz, and everything will be 9 times slower. The `demo_setup.hpp` also has small notes where it calls it, and the note we read, "real silicon needs this," means that real silicon needs it. Another note reminds us that the baud rate is calculated based on 72MHz, and if the baud rate is miscalculated by 9 times, the output will be full of garbled text. If you see garbled text when you burn the firmware for the first time, you should check whether the tree is climbed.

## The RCC in Renode is Fake: A Detector Helps Us Identify the Path

After the tree is climbed, we shouldn't forget our old friend, the simulator Renode. It learns the memory mapping of Blue Pill, but its simulation of RCC is a bit sloppy, so there is a detector in the `clock.hpp` comments that helps us identify this:

```cpp
    // Simulation detector: Renode's RCC model has ready bits ALWAYS set
    // (P5: HSERDY=1 even when HSEON=0). On real silicon, HSERDY stays 0
    // until HSE is actually running — so seeing it high BEFORE we ask
    // for HSE means we're in a simulator with a fake clock tree. Skip
    // the whole thing; the sim already runs at whatever frequency it wants.
    if ((*rcc_cr & (1u << 17)) != 0) {
        return; // simulation — clock tree is fake, don't touch it
    }
```

The P5 in the comment matches the 5th item in the warehouse's pending list, which talks about the fact that the simulator's RCC is fake. Our only criterion is: we haven't requested HSE yet, but the ready bit (HSERDY) is already set. This is something that can't happen on real silicon. The crystal hasn't been turned on, and the ready flag should be 0. In the simulator, it's 1, indicating that the clock tree in our hands is fake, and writing six steps of configuration is a waste of effort. The function directly returns, and the simulator runs at its own frequency.

The detector wasn't guessed by us, I verified it with you in Renode. My local Renode 1.17.0 can't load the I2C and EXTI models referenced in this `.repl`, so I removed the i2c1 and i2c2 lines, added the entire exti block, the two nvicInput lines on it, the EventInterrupt and ErrorInterrupt of the cpu, and the wiring of each GPIO port to exti (blink and perf don't need them, so this verification is unaffected), and the rest of the platform remains as is. Then we started the machine according to the usage written in the warehouse's `bluepill.resc`, my complete command line is:

```shell
renode --console --disable-xwt -e "logFile @/tmp/rcc-probe.log" \
  -e '$bin=@build-fw/src/board/stm32f103_bluepill/example/blink/zeros-demo-blink.elf' \
  -e "include @src/board/stm32f103_bluepill/sim/renode/bluepill.resc" \
  -e "sysbus ReadDoubleWord 0x40021000" -e "quit"
```

The line we read in the monitor is printed before the firmware starts running:

```text
[21:50:47.0652] [WARNING] sysbus: (tag: 'RCC_CR') ReadDoubleWord from non existing
    peripheral at 0x40021000, returning 0x0A020083.
```

The value read back is `0x0A020083`. We'll spread it out in binary to verify: bit 17 is set to 1, and bit 16 (HSEON, the bit we used to request HSE) is still 0. The firmware hasn't touched its registers yet, and the ready flag has already been set. The premise of the detector is fulfilled in the simulation. According to the manual and the archive, the same read on real silicon would read 0, and I can't verify the board part since I don't have a board, so the only conclusion is the archive and the manual.

## DWT: The Stopwatch Built into the Silicon

We have the clock, but we still need a stopwatch. Measuring context switches requires a strict requirement on the stopwatch: the scale of the stopwatch must be finer than the thing being measured to be able to measure it. Switching is a job of hundreds of cycles, and the heartbeat we have on hand is one millisecond per beat, which is 72,000 cycles when converted. Using it to measure switching would always read zero. If we let the software count, the counting instructions themselves would consume cycles, and the counting action would mix with the object being measured, making the read number virtual. The stopwatch we finally chose is built into the CPU: DWT (Debug Watchpoint and Trace, Debug Watchpoint and Trace unit) is a debugging module built into the Cortex-M3, and its CYCCNT register is a counter that increments by one for each clock cycle. The hardware does the counting for us, and the software only needs to read the number. The actions of turning on the stopwatch and reading the number are ordinary register reads and writes, and they don't occupy any peripherals. The full text of `dwt.hpp` is about twenty lines, and outside the pragmas, includes, and namespace shells, there are only two functions that do the work, and we'll read them separately:

```cpp
inline void dwt_init() {
    auto* const demcr     = reinterpret_cast<volatile std::uint32_t*>(0xE000EDFC);
    auto* const dwt_ctrl  = reinterpret_cast<volatile std::uint32_t*>(0xE0001000);
    auto* const dwt_cyccnt = reinterpret_cast<volatile std::uint32_t*>(0xE0001004);

    *demcr |= (1u << 24);    // TRCENA: unlock the DWT block
    *dwt_cyccnt = 0;          // reset the counter
    *dwt_ctrl |= 1u;          // CYCCNTENA: start counting
}
```

The action of turning on the stopwatch has three steps, and we'll go through them one by one. DWT is usually off, and we need to set bit 24 of the debug exception and monitor register DEMCR (TRCENA) to turn on the entire trace module, then clear the counter, and then write bit 0 of the control register, and the stopwatch starts. The unlocking of TRCENA comes before enabling CYCCNT. I've cross-checked with the ARMv7-M architecture manual's debugging chapter, and the order written in `dwt.hpp` matches the manual. CYCCNT has a problem of not running on some early silicon versions, and the manual has a correction, but on this Blue Pill, the stopwatch is running, and the numbers in the archive are the evidence.

```cpp
[[nodiscard]] inline std::uint32_t dwt_cycles() {
    return *reinterpret_cast<volatile std::uint32_t*>(0xE0001004);
}
```

The action of reading the number is to read CYCCNT. Using the 72MHz conversion, every 72 counts correspond to a microsecond, and the archive's conversion is consistent with this.

Does our stopwatch run in simulation? I've verified it in front of you again. The same perf firmware ran a round of measurements in Renode, and the report is as follows:

```text
[1] yield full path (external DWT):
    min: 0 cycles (~0us)
    avg: 0 cycles (~0us)
    max: 0 cycles (~0us)
```

The min, avg, and max in the report are all 0. The Cortex-M model in Renode does not implement CYCCNT, and the counter is completely static, so the measured difference is naturally zero. When we relied on Renode's virtual time measurement at the previous station, we received its reply here, and the full 0 report is the body of the reply. I've specially pasted this section for you to see, because it clearly draws a boundary: simulation can help us verify logic, detectors, and boot, but it can't give any cycle numbers at all. So all the cycle numbers after this, the only source is the archive measured on the real development board in the warehouse.

## Blink: The Scheduler's First Beat on Real Silicon

With the clock and GPIO in hand, we've welcomed the first real silicon demo in the warehouse. The first new board arrived in our hands, and lighting up the LED is a traditional practice in embedded systems. However, the blinking of the LED in blink is not for the LED itself: in the previous stations, the evidence we had for the scheduler was all the serial port prints in the simulator. Blink's verification is larger, and the entire chain includes the clock tree, GPIO, SysTick, and PendSV. When they bite together on real silicon, we can see if the kernel can still beat the millisecond in time. The judgment we make can be done with our eyes, and the small light on the board lights up once every second, and the beat is the scheduler itself. The simple approach is to use `HAL_Delay` to spin for a second and light the LED, and the LED will flash, but the kernel is not present, and it will flash the same way, and the verification is impossible to talk about. The comment in the file header itself writes the words clearly. We'll read the code in two parts:

```cpp
// PC13: onboard LED, active low (pull low to light up)
using LedPin =
    ZerOS::board::Gpio<ZerOS::board::GpioPort::C, 1u << 13, ZerOS::gpio::Direction::Output>;
using Led = ZerOS::gpio::LED<LedPin, ZerOS::gpio::Polarity::ActiveLow>;

void blinker(void*) {
    for (;;) {
        Led::toggle();
        ZerOS::ThisTask::sleep_for(ZerOS::Milliseconds{1000});
    }
}
```

We see that the board's LED is connected to PC13, and it uses the low-level polarity to light up. The low-level polarity to light up is not written into the task's code, it lives in the polarity parameter of the `LED` template, and the polarity branch is dissolved by `if constexpr` at compile time, and the code running on the board has no such judgment, so the zero runtime cost is free. The `Gpio` template provided by the board layer writes directly to the register, and the lighting and extinguishing use the atomic single write of BSRR. Only the toggle (flip) operation has to read and write. The three concepts defined in `gpio_base.hpp` (`GpioPin`, `GpioOutputPin`, `GpioInputPin`) are the face of the board-agnostic, and when changing the board, the implementation that satisfies the concept can also be connected to the same `LED`.

The highlight of the task body is in the comment at the top of the file. Blink doesn't blink with a dead loop of `HAL_Delay` spinning for a second. Every flash you see is a beat of your own scheduler on real silicon. The task sleeps for a second, and the time kernel's SysTick wakes up, the scheduler selects a person, PendSV completes the switch, and the LED flips once, and the rhythm of the flash is the task's beat itself.

```cpp
int main() {
    ZerOS::demo::setup("blink");
    LedPin::init();
    ZerOS::arch::cortex_m3::launch(
        ZerOS::task::named("blink").prio(1).words(64).entry(blinker, nullptr));
    ZerOS::arch::cortex_m3::start_scheduler();
}
```

We look at the queue in `main`. `demo::setup` performs the boot ceremony, preparing the clock, serial port, log channel, and 1kHz beat. `LedPin::init` is responsible for setting up the LED pin. The job of `launch` is to hang the blinker in the task's registry, and finally, `start_scheduler` takes over the CPU, and the task of lighting the LED is now under the control of the task's beat.

For the burning process, the author's operation I quoted from the archive is: adding a `flash-<name>` burning target in the configuration, one line of OpenOCD command pushes the ELF from the ST-Link into Flash, resets after burning, and the small light on PC13 flashes once every second. I can't see if the light is on or off for you now, and I don't have a board on hand, but I've run the firmware in simulation, and Renode's serial window printed the startup message of blink, and the startup path is open.

## Perf: The Yielder Writes the Timestamp, the Waker Reads

With the stopwatch in hand, it's time to measure the context switch. The method is clearly written in the header comment of the perf demo, and we'll read it: two tasks of the same priority, ping and pong, switch back and forth. The yielding task reads the counter before yielding, stores it in a variable, and the waking task reads it again, subtracts the two numbers, and the difference is the complete cost from the moment of yielding to the other task running. The key design is that the measurement is completely within the task body:

```cpp
// external yield measurement: timestamp stored by the yielder, read by the
// waker — zero kernel involvement, DWT only touched in task bodies
volatile std::uint32_t g_t0 = 0;
volatile bool g_ping_turn = true;
```

Why is this so meticulous? Because the archive records the first lesson. The first version directly inserted the DWT read into the switch function and the critical section to track the maximum in real time, but each read of the register was a bus access, and the two reads, comparisons, and storages added about 80 cycles to the hot path. The original words in the archive are "equivalent to weighing the stopwatch with the stopwatch." We've had a firsthand insight into the observer effect in embedded systems: the stopwatch itself is too heavy, and the measured result is all virtual. The new approach is to take the stopwatch out of the kernel's hands, and the kernel's hot path has no measurement hook at all, and the stopwatch is only touched by the task itself.

The code for the two task bodies is not long, and we'll read them one by one. Ping is the yielding side, it reads the stopwatch, stores the departure time, and yields, all within the loop:

```cpp
void ping(void*) {
    for (std::uint32_t i = 0; i < kRounds; ++i) {
        while (!g_ping_turn) {
        }
        g_t0 = ZerOS::board::dwt_cycles();
        g_ping_turn = false;
        ThisTask::yield();
    }
}
```

We see the global `g_ping_turn` as the baton, and whoever gets the baton runs. Ping waits for the baton to arrive, reads the stopwatch once to record the departure time, puts down the baton, and then yields.

We'll read pong, which is the receiving side, and all the numbers measured are collected by it:

```cpp
void pong(void*) {
    for (std::uint32_t i = 0; i < kRounds; ++i) {
        while (g_ping_turn) {
        }
        const auto elapsed = ZerOS::board::dwt_cycles() - g_t0;
        if (elapsed > g_yield_max) { g_yield_max = elapsed; }
        if (elapsed < g_yield_min) { g_yield_min = elapsed; }
        g_yield_sum += elapsed;
        g_ping_turn = true;
        ThisTask::yield();
    }
}
```

Pong wakes up and reads its stopwatch, subtracts the departure time stored by ping, and the difference is the complete cost from the moment of yielding to the other task running, which is recorded in the three categories of maximum, minimum, and total, and then the baton is handed back. Each group yields a thousand times, and we divide `g_yield_sum` by `kRounds` to get the average. All the statistics are on the task's side, and no one touches the kernel.

The report is handled by the third task, reporter, which sleeps for half a second, waits for the two groups to finish, and then prints it out. It has a section of estimation logic that's worth your attention:

```cpp
    // estimate PendSV-only: yield_path includes our DWT read (~4 cycles)
    // + the spin-loop check (~3 cycles) + full yield/PendSV/context_switch
    // a reasonable floor for the asm+entry overhead is ~40 cycles (fixed)
    constexpr std::uint32_t kAsmOverhead = 40; // stmdb+ldmia+exception entry/exit
```

The algorithm reporter wants to use is to subtract 40 cycles from the full yield, and estimate a pure switch overhead. However, the archive later declared this estimation route: that 40 is a guess, and the estimated number is not an independent measurement, and the judgment is not done by it. The code was left, and the version number of the perf demo was stopped at v0.2, and the archive itself marked it as a counterexample, and we'll meet it again when we look at the report.

## Report: Two Rounds of Numbers, Word-for-Word Match the Archive

Now we'll bring out the numbers from the archive. The following two reports are the original text from `perf_1.md`, from the real development board, and I've taken them all as they are, without changing a single number. The first report is from the time when the measurement code was still in the kernel:

```text
=== ZerOS Performance @ 72MHz ===
rounds: 1000

[1] PendSV single switch (context_switch only):
    worst: 210 cycles (2.92µs)  budget: 72 = 1µs

[2] yield full path (yield call -> other task runs):
    min: 479 cycles (6.65µs)
    avg: 489 cycles (6.79µs)
    max: 729 cycles (10.13µs)

[3] worst BASEPRI window (longest critical section):
    worst: 678 cycles (9.42µs)

D12 verdict (switch <=1µs): FAIL
```

The average yield time is 489 cycles, which is 6.79 microseconds when converted. The budget line D12 requires that a single switch does not exceed one microsecond, and one microsecond is 72 cycles, the report immediately judged as FAIL. However, we need to read it with the archive's disclaimer: the 210 in item [1] and the 678 in item [3] are mixed with about 80 cycles of the measurement code itself, which cannot be cited as true values. The only reliable one is item [2], its measurement is on the task side, but the kernel was still carrying the measurement code at the time.

And where is the root of the slowness? We follow the archive to find three reasons. The first reason is the measurement itself, which is the approximately 80 cycles mentioned above.

The second reason is an extra layer of protection in the switch function: we set another BASEPRI on the C side, but the raw assembly of PendSV enters the first sentence `cpsid i` already disables all configurable priority exceptions, protecting the same thing, which is a case of double locking.

The third reason, we see that the ready queue uses a linked list, and when a task yields, it has to be put back at the end of the queue, and the linked list doesn't remember where the tail is, so we have to go back from the head to the tail every time. Although the two tasks only add one step, the memory access for pointer tracking adds a cost of about 30 cycles.

`e2b7dc0` made a cut on each of the three reasons, and we thus had the second round:

```text
=== ZerOS Performance @ 72MHz ===
rounds: 1000

[1] yield full path (external DWT):
    min: 354 cycles (4.92µs)
    avg: 354 cycles (4.92µs)
    max: 543 cycles (7.54µs)

[2] non-assembly remainder (rough estimate, not independently measured):
    est: 314 cycles  budget: 72 = 1µs

D12 verdict: see [1] avg — that's the number users feel
```

The average dropped from 489 to 354, and the archive's algorithm is a 28% reduction. You'll notice that the min and avg show the same number, and the archive itself declared that this is the display effect of integer division truncation, and the tail on max has delayed samples mixed in, and the archive declared that it cannot be cited as the worst-case (WCET) reference, and WCET is the abbreviation for worst-case execution time in English. The est 314 in item [2] is the product of the counterexample mentioned above, and the English declaration in the report (not independently measured) matches the archive's words. The archive also changed the口径 for the judgment of D12, which item should we look at? The report itself answers, look at the average of [1], that's the number that users can feel.

The effectiveness of the three reasons, according to the archive's own changes, is as follows: moving the measurement code out of the hot path, the largest saving, is a 28% reduction, followed by the lock-free scheduling, and the O(1) queue with the tail pointer saves about 6%. One thing I need to clarify for you: the two numbers, approximately 15 and approximately 30, are diagnostic numbers given by the archive, and the actual measured benefits per item will be cleanly decomposed in the next article's four-grid experiment matrix, `e2b7dc0` is a commit that lands three things together, and today's 489 and 354 are the two ends of the batch of changes.

## What Changed on the Kernel Side: Lock-Free Selection and the Tail of the Linked List

Of the three cuts, two were made on the kernel side, and we'll look at the code. The first cut was in `scheduler.hpp`, where `pick_next` was split into two:

```cpp
    [[nodiscard]] base::BorrowedPtr<TCB> pick_next() {
        irq::CriticalGuard guard{driver_};
        return pick_next_locked();
    }

    // Same as pick_next, but the caller PROMISES interrupts are already
    // masked (PendSV context: cpsid i in the naked asm does it). Skipping
    // the redundant BASEPRI round-trip saves ~15 cycles per switch.
    // always_inline so it folds into the caller's section (.ramfunc when
    // called from the RAM-resident context_switch — zero flash wait states)
    [[gnu::always_inline]] [[nodiscard]] base::BorrowedPtr<TCB> pick_next_locked() {
```

Ordinary callers go through `pick_next`, and we see that it still enters the critical section to do the work, while the PendSV path goes through `pick_next_locked`, and the caller promises that interrupts have been disabled, which is what the raw assembly `cpsid i` does. So the entry and exit of the critical section (one set and clear of BASEPRI) can be completely skipped, and according to the warehouse's own algorithm, about 15 cycles are saved. The always_inline attribute folds it into the caller's code segment, and there's no jump left.

The second cut was in `self_list.hpp`, where we see that the linked list added a tail pointer, and the enqueue action changed from traversing the list to directly connecting the tail:

```cpp
    // O(1) append — no walk, tail is right there. Use when order doesn't
    // matter (round-robin queues); the sorted insert above is for deadlines.
    void push_back(BorrowedPtr<Stuff> new_stuff) {
        new_stuff->next_ = nullptr;
        if (tail_ == nullptr) {
            stuff = new_stuff.get(); // list was empty
        } else {
            tail_->next_ = new_stuff.get();
        }
        tail_ = new_stuff.get();
    }
```

The linked list changed from one pointer to two, and the cost of this was written into the message of the compile-time assertion, and we'll read it:

```cpp
static_assert(sizeof(SelfList<zeros_impl::Probe>) == 2 * sizeof(zeros_impl::Probe*),
              "SelfList = head + tail (tail buys O(1) push_back)");
```

The message of the assertion itself is a document: the structure with head and tail, the tail buys O(1) push_back. Whoever changes the structure in the future, and the compiler will print this sentence into the error message as is, and we can't blame anyone, the premise of the design and the implementation are tied together. The archive's diagnosis says that the cost of traversing the list is about 30 cycles, and the saved part is the segment of traversing the list from the head. The scheduler's `push_back`, `push_front`, and `pop_highest` are also marked with `always_inline`, and they are only called on the hot path, and after inlining, the call overhead is zero.

The third cut was on the placement of the code, and we'll look at `switch.cpp` marking `context_switch`, `SVC_Handler`, and `PendSV_Handler` with a macro:

```cpp
// RAM-RESIDENT: the hot switch path runs with zero flash wait states
// (72MHz + 2WS means every flash fetch is 3 cycles; RAM is 1 cycle)
#define ZEROS_RAMFUNC __attribute__((section(".ramfunc"), noinline))
```

The reasoning seems straightforward: Flash takes three cycles to fetch an instruction with two wait states, while RAM only takes one, and the forty-some instructions on the hot path theoretically save about 80 cycles. The linker script was also modified, the `.ramfunc` segment's load address remains in Flash, and the execution address is in RAM, and it is copied over at startup. I need to remind you that the archive records it as the third round's bet, and the third round's real measurement gave it a cold shower, moving to RAM was actually slower, that reversal is the main character of the next article, today we just need to know that the switch is here.

## Acceptance

The verifiable facts in this article, I've listed four, and we'll go through them one by one.

The first thing to verify is the host-side tests. I cloned the warehouse to a copy in `/tmp`, checked out to `e2b7dc0`, and followed the README's instructions to configure the host-side test build, and the real output of `ctest` was all 14 tests passed, taking only 0.22 seconds. The GPIO concepts stack is pure header file code, and it compiles on the host, so the concept's face is established.

The second thing to verify is the cross-compilation, we use the same code with `arm-none-eabi-g++ 16.2.0` to compile the firmware according to the warehouse's toolchain file, and the real output of `size` is: blink's text is 4060 bytes, bss is 2100 bytes, perf's text is 5188 bytes, bss is 2108 bytes. The board with 64K Flash and 20K RAM, and the two firmwares are far from the boundary, the measurement device doesn't blow up the firmware.

The third thing to verify is Renode. I removed the platform description that my local Renode 1.17.0 couldn't load (the ones listed above), and both blink and perf firmwares started, the serial port printed `ZerOS demo: blink` and perf's report, the monitor read the RCC control register to get `0x0A020083`, the HSERDY bit 17 was 1 before the firmware started, the fake RCC detector's criterion was verified in the simulation. perf's report min, avg, max are all 0, CYCCNT doesn't run in the simulation, so the simulation can't give cycle numbers, I verified it with the output, not with a guess.

The fourth thing to verify is the boundary of this article, I'll tell you honestly: the two rounds of reports, 489 and 354, the three attribution numbers, about 80, about 30, about 15, all come from the warehouse's `documents/notes/perf_1.md`, which is the number measured by the author on the Blue Pill at the time, using the ST-Link and OpenOCD route. If you don't have a board on hand, you can read this article, every number is there, but you can't reproduce it temporarily. If you have a board on hand, after burning `flash-perf`, we'll wait half a second on serial 115200, and the report will come by itself, but the numbers it gives you won't be exactly the same as the archive, a few cycle drifts in the average are normal, and the archive also records the numbers from that round.

## Next Article

With 354 in hand, our knives haven't been put away yet. The theoretical gain of moving the hot path to RAM is still in the code, and the textbook says and the silicon's performance are at odds. And the next article, "The Textbook Loses to the Prefetch Buffer," uses RAM and Flash to run the four-grid experiment matrix with linked lists and tail pointers, revealing the third round's negative optimization on the spot, and from 489 all the way to 333, and teaches you how to write an honest experiment notebook. We'll see you there.
