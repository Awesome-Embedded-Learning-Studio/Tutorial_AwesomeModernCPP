---
title: "Textbooks Lose to Prefetch Buffers: .ramfunc Reversal and an Honest Experiment Notebook"
description: "Textbooks say RAM fetches zero wait, moving RAM should be fast, but in real development boards, I tested with a four-cell experiment matrix (code location multiplied by queue structure) and found that the RAM version took 377 cycles, slower than the Flash version's 354. The negative optimization turned its back on the improvement, and after rolling back to a macro switch, Flash with a tail pointer reduced yield from 489 all the way to 333 cycles. This article follows the five rounds of testing in the repository documents/notes/perf_1.md, and explains the three honest clauses of this experiment notebook: measurement pollution declaration, integer truncation declaration, and refusal of lateral ranking."
chapter: 8
order: 2
tags:
  - stm32f1
  - advanced
  - embedded
  - optimization
difficulty: advanced
platform: stm32f1
cpp_standard: [23]
reading_time_minutes: 24
prerequisites:
  - "Weld the stopwatch into the silicon"
related:
  - "Pull out the heartbeat for experiments: NO_TICK, zombie tasks, and probe patches with library"
current_status:
  title: Textbooks are refuted by the experiment notebook
  detail: The ZerOS chapter is being polished
  variant: polishing
  icon: 📚
translation:
  source: documents/vol8-domains/embedded/zeros/08-performance-on-silicon/02-ramfunc-rollback-experiment.md
  source_hash: 809947f1203e79b9c1bb5fe0b948dcbe683e7f0ef6512c24ff564e949748271e
  translated_at: '2026-11-22T14:02:08+00:00'
  engine: qwen3:14b
  token_count: 11945
---
# Textbooks Lose to Prefetch Buffers: .ramfunc Reversal and an Honest Experiment Notebook

When we wrapped up the previous stop, the average reading of yield was 489 cycles. After moving the measurement code out of the hot path of the kernel, it dropped to 354. That was a 28% drop, and I was quite pleased at the time. However, 354 was still far from the 1 microsecond budget of D12 (72MHz, 1 microsecond is 72 cycles), so we still had to keep sharpening our knife. The direction for sharpening was already written in the textbook: RAM fetches are zero wait, Flash fetches require wait cycles, so moving the hot path of switching into RAM is a natural choice and should be faster.

The subject of commit `3dc60bf` is "feat: optimize the sched performance", and it shares the same name as the previous commit. I counted through its diff: it changed 2 files, added 184 lines, and deleted 17 lines. However, the actual code that was changed was only one macro definition in `switch.cpp`. The name is that of an optimization, but the body is that of a rollback. Why did an optimization that seemed so justified turn out to be a reversal? Because we placed the project on a real development board, and real silicon does not back the textbook. This article follows the five rounds of testing in the repository `documents/notes/perf_1.md`, walks through the mechanism of the reversal, and then explains another lesson from this experiment notebook: how to record numbers honestly.

One thing I must put at the forefront: every cycle count in the article comes from the real measurement archive `perf_1.md` in the repository. These numbers were only tested on a real development board, the board used is Blue Pill, the chip is STM32F103, and the main frequency is 72MHz. I currently do not have that board, but I will describe the steps as the author did at the time. At that time, we burned the firmware, waited for the report to be returned via the serial port, and then collected it. I will write the steps clearly, but if you want to verify those numbers yourself, you will need to prepare a board and a set of burning environments. The parts I can run for you (host testing, cross-compilation, Renode simulation), there is a section at the end of this article specifically explaining it, and I will paste the output exactly as it is.

## A Promise from the Textbook

Let's present the formula as per the archive's wording. STM32F103 Flash requires 2 wait cycles at 72MHz. According to the manual, the CPU waits for each instruction, waiting for those 2 wait cycles. Each instruction takes a total of 3 cycles. SRAM has no wait, and each fetch only takes 1 cycle. The body of the switching function `context_switch` is about 40 instructions. If each instruction is fetched from Flash, then moving it into RAM would save 2 cycles per instruction, theoretically saving about 80 cycles.

We subtract 80 from 354 and get around 274. This number is tempting, and it's justified: the wait cycles are written in black and white in the hardware manual, and the zero wait for SRAM is also written. Every step of the derivation is valid. Before seeing the real measurement numbers, I expected this move to be a free gain. The formula didn't lose, but the premise that we all read and never thought of was lost. It's hidden in the first assumption of this formula: each instruction has to be fetched on the spot. The problem we've left behind will be addressed in the mechanism section.

## Moving the Hot Path into RAM: .ramfunc and Its Mover

The method of moving is as per the version implemented in commit `e2b7dc0` (the previous commit). It gave three hot path functions the attribute:

```cpp
#define ZEROS_RAMFUNC __attribute__((section(".ramfunc"), noinline))

extern "C" ZEROS_RAMFUNC std::uint32_t* context_switch(std::uint32_t* saved_sp) { ... }

extern "C" ZEROS_RAMFUNC [[gnu::naked]] void SVC_Handler() { ... }
extern "C" ZEROS_RAMFUNC [[gnu::naked]] void PendSV_Handler() { ... }
```

`section(".ramfunc")` is a GCC attribute, which asks the compiler to place the machine code of the function into a segment named `.ramfunc`, rather than the default `.text`. `noinline` prevents other functions from inlining it. If inlined, the code would be scattered into other functions, and the segment attribute would become invalid. The receiving end is the linker script. I checked the original text in `link.ld`:

```ld
    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data*)

        /* RAM 常驻热路径:上下文切换在这里跑,零 Flash 等待周期 */
        . = ALIGN(4);
        _sramfunc = .;
        *(.ramfunc*)
        . = ALIGN(4);
        _eramfunc = .;

        . = ALIGN(4);
        _edata = .;
    } > RAM AT > FLASH
```

We look at the end of the line `> RAM AT > FLASH`: the segment's runtime address (VMA) is in RAM, and the load address (LMA) is in Flash. The machine code is burned into Flash for storage, and after power-on, it needs to be moved into RAM by us. The CPU executes the code in RAM. The mover is the existing copy loop in the boot code. Let's look at the beginning of `startup.cpp` (excerpt):

```cpp
    auto src = reinterpret_cast<std::uintptr_t>(&_sidata);
    auto dst = reinterpret_cast<std::uintptr_t>(&_sdata);
    const auto dend = reinterpret_cast<std::uintptr_t>(&_edata);
```

The main duty of the copy loop is to move the initial values of `.data` from Flash's `_sidata` to `_edata`. And `.ramfunc` lives inside the `.data` block in the linker script, in front of `_edata`, so it rides on the `.data` bus, and the entire copy is included in one copy. The boot code doesn't need to add a single line. This is also the reason why `e2b7dc0` only added 8 lines in `link.ld`: the placement of the segments was left to the linker script, the movement was left to the existing loop, and the hot path functions only needed to attach one attribute to move.

In the third round, the work on the board is as follows: we burn the firmware, and the reporter returns the report after half a second, which is the reading we need. The output of the third round, the original text from the archive is pasted below as is:

```text
=== ZerOS Performance @ 72MHz ===
rounds: 1000

[1] yield full path (external DWT):
    min: 376 cycles (5.22µs)
    avg: 377 cycles (5.24µs)
    max: 579 cycles (8.04µs)
```

354 became 377. The number went up instead of down, an increase of 6%. The author ran it three times, and the numbers were stable in all three runs, and the negative optimization was also stable. The textbook pointed us to a slow path.

## The Four-Cell Matrix: Two Variables Who Can't Be Mixed Together

When the number turned its back, we had to look into the attribution. In the third round of the experiment, there were actually two variables: where the code lived (Flash or RAM), and how the queue inserted people (linked list or tail pointer). The tail pointer is another optimization implemented in the same commit as `e2b7dc0`. `SelfList` went from a single pointer to a double pointer, and the traversal from the head to the tail in `push_back` was replaced with a direct connection at the tail. Static assertions wrote this layout change into a document at compile time (`SelfList = head + tail (tail buys O(1) push_back)` is the original text), and the cost of one pointer was to exchange for insertion without walking the list.

If the two variables are squeezed into the same round, the 6% increase is unclear: is it the RAM move that caused it, or the queue change? So the author completed the matrix, the four cells of position multiplied by the queue, and we filled them one by one. The fourth round is the combination of RAM and tail pointer, and the fifth round removed `.ramfunc`, and the function returned to execute on Flash, while the tail pointer was kept. The outputs of the fourth and fifth rounds, the original text from the archive is as follows:

```text
=== ZerOS Performance @ 72MHz ===
rounds: 1000

[1] yield full path (external DWT):
    min: 349 cycles (4.85µs)
    avg: 349 cycles (4.85µs)
    max: 537 cycles (7.46µs)
```

```text
=== ZerOS Performance @ 72MHz ===
rounds: 1000

[1] yield full path (external DWT):
    min: 333 cycles (4.63µs)
    avg: 333 cycles (4.63µs)
    max: 529 cycles (7.35µs)
```

The four cells are complete, and we arrange them into a table (units are the average cycles of yield throughout, data comes from the real measurement archive in `perf_1.md`):

| Code Location | Ready Queue | Yield Avg | Relative Baseline |
| --- | --- | --- | --- |
| Flash | Linked List O(n) | 354 | Baseline |
| RAM | Linked List O(n) | 377 | +6%, negative optimization |
| RAM | Tail Pointer O(1) | 349 | -1% |
| Flash | Tail Pointer O(1) | 333 | -6%, best overall |

The table is the main body of this article, and the other paragraphs are footnotes for it. The order of reading it also matters. When comparing vertically, RAM is slower than Flash, and the reversal in the third round is confirmed again by the other two cells in the four-cell matrix. When comparing horizontally, the tail pointer gains in both positions, but how much it gains depends on who it's compared to. Let's look at the fourth round alone: 349 is only 5 cycles better than 354, and the actual gain of the tail pointer is far less than our intuition. When paired with Flash, 333 only reveals its true ability. The total in the archive is from 489 to 333, and the three effective methods are ranked by the archive itself. Moving the measurement code out of the hot path is the biggest one, reducing by 28%. PendSV's lock-free scheduling saves the repeated BASEPRI round trips, saving about 15 cycles. The tail pointer insertion in the queue is -6%.

## The Mechanism of Reversal: Why the Prefetch Buffer Wins

Now we go back to the problem we left behind: the first assumption of the formula says that each instruction has to be fetched on the spot. The Flash interface of STM32F103 doesn't work this way; it has a prefetch buffer (prefetch buffer in English): while the CPU is executing the current instruction, the Flash interface reads the following instructions in advance and prepares them. When the CPU comes to fetch the next instruction, the prepared instruction doesn't need to wait for those 2 wait cycles, it is delivered directly. For sequentially executed code, most of the instruction fetches are covered by this buffer, and the formula in the manual that says 3 cycles per instruction is not valid in the real world of sequential execution.

This explains why moving to RAM is slower: the instructions in the hot path were originally well-served by the prefetch buffer in Flash, with almost no real waiting. After moving to RAM, the benefit of the prefetch is completely lost, and RAM now has to serve two mouths at the same time: when the CPU comes to fetch instructions, data access also goes through its bus, and the competition between the two mouths slows down each mouth. The archive also lists two candidate mechanisms: changes in branch layout, changes in the alignment of the veneer (a small segment of transition code in ARM) and the literal pool, the archive's wording is "not completely isolated", I present the four candidates as they are: the mechanism of prefetch failure and bus competition is clear, but the two on layout and alignment, the archive also didn't dare to say they were dead.

One place I have to honestly admit I'm unsure: the specific depth and width of the prefetch buffer on STM32F1 (how many bits are prefetched at a time, how many entries are buffered), I haven't checked the original text in the RM0008 reference manual. This article only used the directional wording "it will prepare the following instructions in advance". If you want to quote the details, you have to check the chapter on Flash memory and interface in the manual yourself. The original wording in the archive's rollback comments in `switch.cpp` says that the Flash prefetch buffer beats the zero-wait RAM by 16 cycles. 16 is the result of the archive comparing the fourth round's 349 with the fifth round's 333, both rounds have tail pointers, only the code location is changed. 377 minus 333 mixed with the queue variable is not the source of 16, I won't help you reconcile it, you have to compare the table yourself.

The applicable domain is clearly marked in the archive, and we have to stick to it. This judgment only holds under the current configuration, that is, the combination of 72MHz and 2 wait cycles. If you change the chip or the frequency, the number of wait cycles changes, and the relative value of the prefetch buffer also changes, and the balance has to be retested. So the rollback didn't delete the code completely, it left a device that can be retested.

## Rollback is also an Output: The Device Left by the Macro Switch

The macro definition that `3dc60bf` changed in `switch.cpp`, we look at the state after the rollback (checked the original text in the commit, the comments are its own):

```cpp
// Switch hot path stays in FLASH (not .ramfunc): real-hardware measurement
// (documents/notes/perf_1.md, round 3 & 5) shows RAM placement is a
// NEGATIVE optimization on STM32F103 @72MHz — the flash prefetch buffer
// beats zero-wait-state RAM by 16 cycles. If a future chip/config changes
// the tradeoff, re-measure before re-enabling.
// #define ZEROS_SWITCH_IN_RAM  (kept for reference; do NOT enable on F103)
#ifdef ZEROS_SWITCH_IN_RAM
#define ZEROS_RAMFUNC __attribute__((section(".ramfunc"), noinline))
#else
#define ZEROS_RAMFUNC
#endif
```

The macro has changed from unconditional definition to a switch: by default, it goes to the `#else` branch, `ZEROS_RAMFUNC` is empty, and the three functions return to Flash. If one day we use a chip with an ineffective prefetch buffer, we just need to define `ZEROS_SWITCH_IN_RAM` again, and the entire chain of moving `.ramfunc` (segment attributes, linker script, boot copy) doesn't need to be rewritten, just one test will tell us where the new balance stands. The comment "do NOT enable on F103" is a hard statement, this hard statement is obtained with the real measurement numbers of two commits, it's not something you can just guess.

We've always seen commits as units of progress, this article is a counterexample: the commit subject says optimize, but the content retracts a change from the previous commit. Rollback is also a type of engineering output, because it's supported by a complete set of measurements, the left switch and comments are a repeatable experimental device. Next time someone wants to optimize on this board, this device will save them the cost of retracing the same steps.

## How to Write an Honest Experiment Notebook

`perf_1.md` in this commit was rewritten from the original 44-line notes into a complete experiment notebook of 203 lines, the number of lines doubled, and most of the added parts are about limiting the numbers. The new numbers didn't increase much, only the number of lines. The way this notebook teaches is summarized into three rules for you.

The issue of measurement pollution is marked the heaviest in the archive, and I read it as the first lesson. In the first round of output, there is a section `[1] PendSV single switch: worst: 210 cycles`, and the reading `[3] worst BASEPRI window: worst: 678 cycles`. The two numbers are marked as non-referable in the archive, because the measurement code (reading the DWT counter) was placed in the hot path of the kernel at the time, reading the register once required a round trip on the bus, which took about 80 cycles, the numbers were soaked in the cost of the measurement tool itself, the original wording of the archive is "using a stopwatch to measure a stopwatch". Polluted numbers are not marked with declarations, and later people (including the author himself three months later) used them as a baseline, the entire optimization chain started from a wrong starting point. Only `[2]` in the first round was truly usable, the min 479, avg 489, max 729 of yield throughout, because its measurement point was in the task, not in the kernel path.

The quality of the numbers, I also read them clearly in the archive. The min and avg in the second, fourth, and fifth rounds are all the same number, 333, there is no coincidence here, only integer division truncation, the average is calculated over 1000 rounds, the decimal is cut off. The min 376 and avg 377 in the third round differ by one cycle, also using the integer division truncation. The max is exactly the opposite, it is about five to six times larger than the min, the archive's judgment is that these delay samples are suspected to be interrupted by SysTick, but this round was not verified, so it clearly wrote that max should not be used as WCET (worst-case execution time, the formal metric for real-time performance). The attribution for this commit stage was not completed, and the archive put the words "not verified" next to the numbers, guesses are guesses, and measurements are measurements.

Numbers that are not up to standard are not allowed to enter the judgment, I read this and I'm convinced. From the second round report, it carries an estimated route `[2] non-assembly remainder: est: 314 cycles`, which is derived by subtracting a rough assembly overhead constant (about 40 cycles) from the yield throughout. The archive's judgment is that it is not an independent measurement result and cannot be used for D12 judgment. D12's budget says "context switching is no more than 1 microsecond", the archive wrote out both interpretations: according to the yield throughout, it reads 4.63 microseconds, and the judgment is FAIL. According to the PendSV body's interpretation, there was no separate measurement method at the time, and the judgment is UNVERIFIED. Both interpretations do not support the standard, and the archive did not choose one interpretation that is favorable to itself to submit the paper. On the other side, the switching numbers reported by other RTOSs have different measurement boundaries, configurations, and compilers, the original wording of the archive is that it is not making a lateral performance ranking, if you want to compare, compare under the same Blue Pill, same compiler, and same measurement code.

The three rules are actually the same attitude: the readers of the experiment notebook are the author himself, who came back after a half year with a different board, and at that time, he had already forgotten the board, the configuration, and the hesitation at that time. If the quality of the numbers is not clearly written next to them, after several months, they would be an old reading with an unclear origin, which is not usable, but also not discardable.

## What I Can Run for You

We cannot reproduce the numbers on the board, but there is still a considerable part of this commit that can be run in reality. I will paste the process and output as they are, they are different from the numbers in the archive, and I will only use them to verify the code itself.

The first thing I ran was the host test. I exported the source tree of `3dc60bf` to /tmp, followed the instructions in the README to configure the host build, the GCC on my machine is 16.2.1, all 14 targets of `ctest` passed, the total time taken was 0.29 seconds:

```text
100% tests passed out of 14

Total Test time (real) =   0.29 sec
```

The two tests that are most relevant to the main text of this article, we look at the results separately. `test_self_list` submitted a result of 8 test cases, 32 assertions all passed, it guards the bidirectional structure itself with a tail pointer. `test_sched` submitted a result of 14 test cases, 70 assertions all passed, it guards the agreement of `pick_next_locked` and lock-free scheduling. We can say that the two data structure changes that earned cycles in the four-cell matrix have passed the behavior verification on the host.

The second thing I ran was the volume comparison of the two versions of the macro switch, this is the part of this article that I am most confident to run for you, because the switch is the device left by this commit. The cross-compiler used is my local arm-none-eabi-g++ 16.2.0, the default configuration has the switch turned off, and the hot path is left in Flash. When `ZEROS_SWITCH_IN_RAM` is turned on, the hot path moves into RAM. `size` reports one version for us each:

```text
# 默认(Flash):
   text	   data	    bss	    dec	    hex	filename
   4704	    484	   2108	   7296	   1c80	build-fw/src/board/stm32f103_bluepill/example/perf/zeros-demo-perf.elf
# 开 ZEROS_SWITCH_IN_RAM(RAM):
   text	   data	    bss	    dec	    hex	filename
   5188	      0	   2108	   7296	   1c80	build-fw-ram/src/board/stm32f103_bluepill/example/perf/zeros-demo-perf.elf
```

The dec column of the two versions is exactly the same, both are 7296 bytes: the total footprint of the firmware has not changed, moving RAM only moves the location where the code lives, the 484 bytes that were reduced from `.text` have moved into the territory of `.data`, they have moved homes in the two versions: the Flash version records it under the name of data, the RAM version is recorded into text by size, `.ramfunc` moves with the `.data` block, the口径 of the column is the size tool's own, we only need to look at the dec column. Whether the relocation has really happened, the symbol table testifies for us:

```text
# RAM 版:nm 里三个热路径符号全在 SRAM 段(0x2000 开头)
200001e4 T context_switch
200002c4 T SVC_Handler
200002dc T PendSV_Handler
# Flash 版:同样的三个符号回到 Flash(0x0800 开头)
08000bf4 T context_switch
08000cd4 T SVC_Handler
08000cec T PendSV_Handler
```

I looked at the two symbol tables, the address of the symbols moved with the switch being on and off, this switch device is really working.

The third thing I ran was Renode. I need to explain two changes in the simulated environment: my local Renode 1.17.0 cannot recognize the two I2C nodes in `bluepill.repl` using the `I2C.STM32F4_I2C` model, I commented out i2c1, i2c2 and their interrupt connections altogether, the perf demo does not use I2C, and its behavior is not affected, consistent with the previous articles. The perf demo runs automatically in the simulation, we just need to start the simulation and wait 15 seconds to collect the logs. It did indeed print out the entire report exactly one second, but the cycle numbers on the report are all zero:

```text
[INFO] usart1: [host: 0.61s (+0.25ms)|virt:   0.5s (+0s)] === ZerOS Performance @ 72MHz ===
[INFO] usart1: [host: 0.61s (+0.1ms)|virt:   0.5s (+0s)] rounds: 1000
[INFO] usart1: [host:  0.61s (+0.17ms)|virt:   0.5s (+0s)] [1] yield full path (external DWT):
[INFO] usart1: [host: 0.61s (+0.31ms)|virt:   0.5s (+0s)]     min: 0 cycles (~0us)
[INFO] usart1: [host: 0.61s (+0.21ms)|virt:   0.5s (+0s)]     avg: 0 cycles (~0us)
[INFO] usart1: [host: 0.61s (+0.21ms)|virt:   0.5s (+0s)]     max: 0 cycles (~0us)
```

All zeros seem to be a witness, it testifies to the argument of this article: the DWT cycle counter increases based on the real beat of the clock tree, the clock in Renode is a fake RCC set (we installed a detector to catch it in the previous article), the instructions are pushed forward by event-driven, the cycle numbers are not even counted. The premise of measurement is real beat, simulation cannot provide it, so all the cycle numbers in this article have only been verified on a real development board, simulation can only prove that the program runs, but cannot prove fast or slow. This is also the reason why the archive recorded both the numbers and the口径 together: the land for measurement is only that one piece, the numbers have to grow on the land to count.

## Acceptance

We summarize the three verifiable facts of this article.

The first is the reversal and matrix in the archive. The third, fourth, and fifth rounds in `documents/notes/perf_1.md` give all the readings of the four cells: 354, 377, 349, 333, plus the baseline of the first round 489 and the total of -32%, each number I quoted verbatim from the archive in the main text. Its domain declaration (only valid under the current configuration) and rollback comments ("do NOT enable on F103") are in the original text of `3dc60bf`'s `switch.cpp`, you can check it by `git show 3dc60bf:src/arch/arm_cortex_m3/switch.cpp`.

The second is the verification chain I ran for you. All 14 targets of the host test passed, `test_sched` submitted a result of 14 test cases and 70 assertions, `test_self_list` submitted a result of 8 test cases and 32 assertions, I cross-compiled both versions of the macro switch configuration, the dec of the two versions is the same at 7296 bytes, three hot path symbols in `nm` moved between `0x2000` and `0x0800` with the switch on and off. If you want to reproduce it, you just need to export the source tree and run cmake twice, and when you enable the RAM version, add `-DZEROS_SWITCH_IN_RAM` to `CMAKE_CXX_FLAGS`.

The third is what this article did not verify, I honestly put it out. I have no board for the four-cell matrix cycle numbers, they were only verified on a real development board, and reproduction requires a Blue Pill and an ST-Link burning environment. In the section on the mechanism of reversal, the口径 of prefetch failure and bus competition is consistent with the archive and manual, the two candidates of buffer depth and width, layout and alignment, the archive itself marked them as not completely isolated, I also did not retest them, the places where you are unsure are the same as the places where I am unsure.

The next article "Pull out the heartbeat for experiments: NO_TICK, zombie tasks, and probe patches with library" will push the measurement further: turn off SysTick, identify the suspect at the end of the max, and dig out two bugs that have been lurking in the NO_TICK path, the measurement probe is peeled off from the mainline and turned into a device that comes with library patches. The UNVERIFIED PendSV body in the archive has a reading of 93 cycles with zero variance, they are added into the archive in the subsequent commits, and the current version of the archive can see them, this article is frozen at the five rounds of `3dc60bf`. We'll see you there.
