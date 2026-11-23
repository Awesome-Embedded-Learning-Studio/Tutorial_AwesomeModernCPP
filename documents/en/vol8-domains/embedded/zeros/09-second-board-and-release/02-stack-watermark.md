---
title: "Filling the Stack with Magic Numbers: Upgrading Canaries to Watermarks, Determining Stack Depth by Evidence"
description: "The first half of commit fb9072b (feat: stack watermark, tickless) upgraded the stack canary: bury_canary changed from painting just eight canary words to filling the entire stack with 0xEF114514, watermark_free_words scans upward from the bottom to find the remaining empty words, and capacity_words subtracts the eight canary words to report the total capacity. The host white-box tests wrote the inaccurate paths and their respective boundaries into the test cases, and the watermark demo reported after one second: deep used 62 words at the deepest point, shallow used only 30 words, and the extra 32 words exactly matched the number of padding words. I ran 15 host test targets and cross-compiled builds in the /tmp copy, and the watermark demo in Renode produced the two readings above. The 'Wake Everyone Up' case, where eaten=6 was captured, has now been upgraded to a system where readings can be retrieved on demand."
chapter: 9
order: 2
tags:
  - stm32f1
  - intermediate
  - 嵌入式
  - cpp-modern
difficulty: intermediate
platform: stm32f1
cpp_standard: [23]
reading_time_minutes: 22
prerequisites:
  - "Same heartbeat in QEMU: zero-modified second board, fulfilling thin port"
related:
  - "Wake Everyone Up, Reinvestigate the Truth: Event Groups, Direct Notifications, and Full Ring Backpressure"
  - "The Chip's Last Will: HardFault Scene, Stack Canary, and Zero Allocation Log"
current_status:
  title: Stack Depth is Verifiable
  detail: The ZerOS chapter is currently being polished
  variant: polishing
  icon: 📏
translation:
  source: documents/vol8-domains/embedded/zeros/09-second-board-and-release/02-stack-watermark.md
  source_hash: 8e6ff4b6c5128dd2ab156e4a5d0a5220d386abb66cc974dac05e31176042b7bd
  translated_at: '2026-11-22T14:07:58+00:00'
  engine: qwen3:14b
  token_count: 13159
---
# Filling the Stack with Magic Numbers: Upgrading Canaries to Watermarks, Determining Stack Depth by Evidence

At the end of the "Wake Everyone Up" article, we had a live capture: the stack of the event task was compressed to 64 words, and the canary reported `eaten=6`, indicating that it had consumed six canary words. The overflow of six words plus a segment of extra space gave us the new stack depth of 96 words. At the time, I felt a bit proud, but upon reflection, the cost of that measurement was quite high. It required an actual overflow, followed by an immediate shutdown after reporting, and the reading was only a number at the edge of a task's crash. For other tasks, we still had to estimate the stack depth by adding a segment of extra space based on intuition, and we still lacked a table that could be retrieved on demand to know exactly how much was used in normal operation.

Commit `fb9072b` has now filled in the missing table. Its subject is "feat: stack watermark, tickless", and it introduced two features in one commit. I counted its diff, and it changed 13 files, adding 431 lines and removing 7 lines. This article only discusses the first half of the watermark feature; the tickless idle portion is covered in the sister article "Sleep and Slow Down the Heartbeat". The idea of the watermark is straightforward: when a task is created, the kernel fills its entire stack with the same fixed pattern `0xEF114514`. After the task has been running for a while, we scan upward from the bottom of the stack, and stop at the first word that is not the pattern. The number of words that remain untouched is the unused space. The pattern is applied to the entire stack, and the reading is based on evidence, so your stack depth is now determined by the reading, not by intuition.

However, we have already experienced the honesty of the repository in several places. This time, it even wrote its own error margins into the test cases. The watermark reading has ways to be inaccurate: it cannot see the deep holes, and it may optimistically assume one word if the bottom of the deepest frame is not written. Scattered out-of-bound words may make it pessimistic but safe. The test case names are straightforward, and we call them `two ways to be wrong, both bounded and known` (two ways to be wrong, but both have known boundaries). The name lists two ways, and the body is divided into three sections, which we will examine one by one.

## Full Stack Painting: Replacing Eight Canary Words with a Full Pattern

The code's location hasn't changed; it's still in `include/ZerOS/kernel/sched/stack_guard.hpp`, which we read in the article "The Chip's Last Will". Let's look at the changes this commit made to it:

```cpp
namespace ZerOS::task {
constexpr inline std::uint32_t kCanaryWord = 0xEF114514;
constexpr inline std::size_t kCanarySize = 8;

struct TaskGuardHelper {
    static void bury_canary(sched::TCB& t) {
        auto& stack_view = sched::zeros_impl::TCBKeys::stack_view(t);
        for (std::size_t i = 0; i < stack_view.size(); ++i) {
            stack_view[i] = kCanaryWord;
        }
    }
```

This change is small and unobtrusive, but we can only see its significance by placing the old version next to it. The old loop's upper bound was `i < kCanarySize && stack_view.size()`, and it only painted the lowest address's eight canary words. The new version's upper bound is only `stack_view.size()`, and the entire stack is filled with the pattern `0xEF114514`, with no words left. The density of the full stack painting is the foundation of the watermark: the scan counts the words that have never been touched. If the pattern outside the eight canary words is gone, after scanning the eighth word, we will see memory that has never been assigned a value, and the reading becomes meaningless. Here, the magic number is understood as a fixed pattern that was pre-defined, and it is `0xEF114514`, a value that is clearly visible. It was chosen without any deep meaning, but it is useful: the entire stack is uniform, and the pattern breaks where the footprints are.

I will truthfully report one deletion. There was a line of comment above the old loop, which said `word-wise: memset fills BYTES, it would chop the pattern to 0x14`, explaining why we write word by word instead of using `memset` (`memset` fills by bytes, and `0xEF114514` has three `0x14` bytes, resulting in a single stripe, and the pattern is lost if we write word by word). This commit deleted the comment, but the loop still writes word by word, and the old reason still lies in the git history. I have pasted the original text from `git show` for your reference. It deleted one line of comment and the old upper bound line, but the loop body itself was not changed at all:

```diff
-        // word-wise: memset fills BYTES, it would chop the pattern to 0x14
-        for (std::size_t i = 0; i < kCanarySize && i < stack_view.size(); ++i) {
+        for (std::size_t i = 0; i < stack_view.size(); ++i) {
         stack_view[i] = kCanaryWord;
     }
```

The patrol side hasn't changed. `fast_check_stack` still only reads one word at the edge, `eaten_length` still counts the number of canary words eaten after an incident, and the overflow detection method is exactly the same as we read in "The Chip's Last Will". Only the base has changed: before, the canary was under a wasteland, and now the canary is under a full-color pattern. The function hasn't changed, but the reading has become richer.

## Two Readings: How Many Words Are Left, and How Many Words in Total

The new readings have two parts, and we will look at them one by one. The first is the reading of the remaining words, and we look at `watermark_free_words`:

```cpp
    static std::size_t watermark_free_words(sched::TCB& t) {
        auto& stack_view = sched::zeros_impl::TCBKeys::stack_view(t);
        std::size_t free_words = 0;
        for (std::size_t i = kCanarySize; i < stack_view.size() && stack_view[i] == kCanaryWord;
             ++i) {
            ++free_words;
        }
        return free_words;
    }
```

The scanning direction is worth your attention. The task's stack grows from high address to low address, and `view[8]` in the stack view is the low address end adjacent to the canary area. We start at `view[8]` and walk upward along the path. The loop condition is `stack_view[i] == kCanaryWord`, and once it fails, the loop stops immediately. Where it stops, the watermark is set. The words we have walked are the unused space that the task has never pressed. The remaining part after stopping is the deepest point the task has ever reached. The way the condition is written also has a consequence, which we specifically confront in the section on inaccurate readings.

The second reading is the capacity reading, and we look at `capacity_words`:

```cpp
    // usable capacity in words — the canary eats 8 of them (D24: it
    // occupies the front, the budget says so)
    static std::size_t capacity_words(sched::TCB& t) {
        const std::size_t total = sched::zeros_impl::TCBKeys::stack_view(t).size();
        return total > kCanarySize ? total - kCanarySize : 0;
    }
```

The comment clearly states the口径: the canary occupies the first 8 words of the stack view, and the available capacity is the total number of words minus 8. The budget document (D24, which specifies the canary's allocation in the budget) says exactly that. If the total number of words is not greater than 8, the extreme case returns 0, and the capacity is not discussed when the canary itself cannot even fit. You subtract `watermark_free_words` from `capacity_words` to get the number of words used at the deepest point, and the demo's report directly prints it. We will look at the real numbers there.

The timing of the painting doesn't need to change; it's still the old three in `spawn`, painting after burying, then faking the frame and enqueuing. Idle also goes through the same `bury_canary` in `start_scheduler`. Everyone's stack is fully colored, and everyone's reading is available for scanning. This is the coverage we gain after expanding the painting from eight words to the full stack.

## Host Exam: Four Exams

Pure logic scanning is suitable for testing on the host, and the exam is the newly established `test/test_stack_watermark.cpp`, with four `TEST_CASE`s we will go through one by one. The fixture fixes the stack and the card in the test's own storage, with a 32-word stack, 8 words for the canary and 24 words for the available. After construction, it paints on the spot:

```cpp
struct Pinned {
    TCBStorage storage{};
    std::uint32_t stack[32]; // 32 words: 8 canary + 24 usable
    TCB& task;

    Pinned()
        : task(ZerOS::task::named("t")
                   .prio(1)
                   .stack(std::span<std::uint32_t>{stack})
                   .entry(kNoopEntry, nullptr)
                   .spawn_into(storage)) {
        TaskGuardHelper::bury_canary(task);
    }

    std::span<std::uint32_t> view() { return TCBKeys::stack_view(task); }
};
```

The first exam checks the coverage of the painting, and the test case name is `paint covers the whole stack, not just the canary`, which means the painting is full stack, not just the canary. The assertion checks each word in the entire view, and all of them are `kCanaryWord`. The second checks the empty reading, and we look at:

```cpp
TEST_CASE("untouched task reports full capacity free", "[watermark]") {
    Pinned p;
    REQUIRE(TaskGuardHelper::capacity_words(p.task) == 24);
    REQUIRE(TaskGuardHelper::watermark_free_words(p.task) == 24);
}
```

We look at the empty case: when the task has never run, the remaining should be full, 24 matches 24, and the capacity and remaining coincide in the empty case, and the口径 is aligned. The third is the main character, and it checks the watermark's only downward trend. We follow the task through one life, and the code is divided into two parts:

```cpp
TEST_CASE("pushing from the top eats the margin, deepest push wins", "[watermark]") {
    Pinned p;
    auto v = p.view();

    // a few frames near the top: [size-4, size) written
    for (std::size_t i = v.size() - 4; i < v.size(); ++i) {
        v[i] = 0;
    }
    REQUIRE(TaskGuardHelper::watermark_free_words(p.task) == 24 - 4);
```

We go through the plot. The task lightly pressed four words, and the remaining reported 20, which is accurate.

```cpp
    // deeper push later in life: down to word 12 (inclusive) — the
    // watermark only ever goes DOWN, old shallower writes stay counted
    for (std::size_t i = 12; i < v.size(); ++i) {
        v[i] = 1;
    }
    REQUIRE(TaskGuardHelper::watermark_free_words(p.task) == 12 - 8);
    REQUIRE(TaskGuardHelper::capacity_words(p.task) == 24);

    // and the canary zone itself is never part of the margin
    REQUIRE(TaskGuardHelper::fast_check_stack(p.task) == false);
}
```

Later, it lived deeper, pressing up to word 12 (inclusive), and the remaining dropped to 4. The comment writes the temperament dead: watermark only ever goes DOWN (the watermark only ever goes down), the shallow wave won't be erased by a new shallow reading. The final assertion checks the integrity of the canary area, `fast_check_stack` returns false as expected, meaning no overflow, and the canary is still alive. The comment also explains why the canary area is not counted in the remaining. The watermark always answers the question of where it has ever reached the deepest. We also don't mix up the responsibilities of the two functions: checking integrity is `fast_check_stack`, and the extreme value is `eaten_length`, which only counts after an incident.

## The Inaccurate Path, the Repository Wrote Its Own Boundaries

The name of the fourth exam is the title of this section, and we go through the three sections one by one. The first section says the scan has invisible holes:

```cpp
    SECTION("a hole above the deepest write is invisible to the scan") {
        Pinned p;
        auto v = p.view();
        // frames wrote [12, size) but never touched word 13 — the scan
        // stops at 12 (first non-paint) before ever reaching the hole:
        // the reading stays exact for the written-word semantics
        for (std::size_t i = 12; i < v.size(); ++i) {
            v[i] = (i == 13) ? ZerOS::task::kCanaryWord : 1;
        }
        REQUIRE(TaskGuardHelper::watermark_free_words(p.task) == 12 - 8);
    }
```

Frames write through words 12 to the end, but word 13 is untouched, and the pattern is left there. The scan starts at word 8, and stops at word 12 when it hits the first non-pattern word. The hole at word 13 is never seen. The comment's written-word semantics is the口径's answer: the watermark counts the boundary of the written words, and the reading is accurate to this口径. We put it in plain language: its blind spot is the hole that is exactly left with the pattern in the deeper part, which it cannot see, and it doesn't scan it.

The second section marks the only optimism, and we look at:

```cpp
    SECTION("the only flattery: the deepest frame's bottom word unwritten") {
        Pinned p;
        auto v = p.view();
        // the frame REACHED down to word 11 (SP semantics: free == 3)
        // but only words [12, size) were actually stored to — the
        // written-word scan reports 4: one word optimistic, bounded by
        // the frame's own alignment slop, the trade every RTOS takes
        for (std::size_t i = 12; i < v.size(); ++i) {
            v[i] = 1;
        }
        REQUIRE(TaskGuardHelper::watermark_free_words(p.task) == 12 - 8);
    }
```

We set up the scene: the frame's pointer reached word 11, but word 11 was not written at all, and the written part is from word 12 to the end. The remaining by pointer is 3, and the remaining by written words is 4, and the reading overstates by one word. The comment even marks the boundary of the overstatement: the error is limited by the frame's own alignment remaining, and it can be at most one word. The original comment says the trade every RTOS takes (every RTOS signs the same trade), and it just writes the trade on the surface. When we determine the stack depth, leaving one or two words for the remaining is the source of this reason.

The third section is reversed, and we look at it putting the error on the safe side:

```cpp
    SECTION("a lone stray word at the very bottom makes it pessimistic") {
        Pinned p;
        p.view()[8] = 0xDEADBEEF; // out-of-bounds single write below everything
        REQUIRE(TaskGuardHelper::watermark_free_words(p.task) == 0); // safe direction
    }
```

A scattered out-of-bound word falls on word 8, and the scan can't move a step, and the remaining reports 0. The reading changes from overstatement to scare, and the comment gives the direction: the safe direction (safe direction): the scaring reading will only make you increase the stack, not decrease it. The three sections of error direction, the optimistic one has a boundary, and the pessimistic one has a direction. You look back at the test case name, and the third section is the live example of the pessimistic item, and the entire exam has no place that hides its inaccuracy.

## Watermark Demo: Watermark Report After One Second

Our system needs a public reading, and the demo on the board is the place where it reads publicly. The newly registered `watermark` demo starts three tasks, `deep` with real ballast, `shallow` with almost no breathing, and `reporter` sleeps for a second to read the table:

```cpp
void deep(void*) {
    // ballast: a volatile local array welded onto its frame — written
    // and read every round so nothing can optimize it away; the stack
    // genuinely gets pushed down to this depth (and further, every time
    // the sleep call chain runs through it)
    volatile std::uint32_t ballast[32];
    for (std::uint32_t round = 0;; ++round) {
        for (std::uint32_t i = 0; i < 32; ++i) {
            ballast[i] = round + i;
        }
        std::uint32_t sum = 0;
        for (std::uint32_t i = 0; i < 32; ++i) {
            sum += ballast[i];
        }
        (void)sum;
        ThisTask::sleep_for(Milliseconds{100});
    }
}
```

Ballast is a `volatile` array welded onto the frame, and we write and read it every round, so the optimizer can't touch it, and the stack is truly pressed to this depth. The comment also adds a line, saying that each sleep call chain passes through it, pressing deeper. `shallow`'s loop body only has one line of `sleep_for`, and `reporter` reads the table after sleeping for 1000 milliseconds:

```cpp
void report_one(const char* name, TCB* t) {
    const auto cap = TaskGuardHelper::capacity_words(*t);
    const auto free = TaskGuardHelper::watermark_free_words(*t);
    // (the mini formatter speaks plain {} only — no alignment dialect)
    ZerOS::log::print("  {}: {} cap, {} free, {} used at deepest\r\n",
                      name, Dec{cap}, Dec{free}, Dec{cap - free});
}

void reporter(void*) {
    ThisTask::sleep_for(Milliseconds{1000});

    ZerOS::log::print("watermark: read\r\n");
    ZerOS::log::print("  (words, canary's 8 excluded)\r\n");
    report_one("deep", g_deep);     // parked asleep — single core says
    report_one("shallow", g_shallow); // their stacks hold still while we read

    ThisTask::block();
}
```

`report_one` subtracts the remaining from the capacity, and reports the number of words used at the deepest point. The small formatter only recognizes `{}`, and it doesn't have any alignment patterns. We need to be precise at the moment of reading the table: `reporter` runs on a single core, and `deep` and `shallow` are both sleeping in `sleep_for` at this moment. The source code comment writes this reasoning at the end of the line: the stack reading of sleeping tasks is static, and reading them is not peeking, but reading the frozen evidence.

My reproduction chain is the same as before, and I exported the tree to the /tmp copy with `git archive fb9072b`, copied CMSIS into the submodule, and built both host and cross-compiled versions. The original serial port text of the watermark report, we look at:

```text
[INFO] usart1: [host:  1.09s (+0.96s)|virt: 1s (+1s)] watermark: read
[INFO] usart1: [host: 1.09s (+0.39ms)|virt: 1s (+9.8µs)]   (words, canary's 8 excluded)
[INFO] usart1: [host: 1.09s (+0.33ms)|virt: 1s (+0s)]   deep: 88 cap, 26 free, 62 used at deepest
[INFO] usart1: [host: 1.09s (+0.27ms)|virt: 1s (+0s)]   shallow: 56 cap, 26 free, 30 used at deepest
```

We read the numbers together. `deep` uses a 96-word stack, its capacity is 88, and it used 62 at the deepest point. `shallow` uses a 64-word stack, its capacity is 56, and it used 30 at the deepest point. 30 words are its entire fortune, including the birth frame and the sleeping call chain. `deep` pressed 32 words of ballast on the same set of fortune, and the difference between 62 and 30 is exactly 32, and the ballast array appears in full in our report. You look at the capacity column again, 88 matches 56, and the canary's 8 words have been deducted. The origin of the 96 words, the article "Wake Everyone Up" got it by swapping once, and now it's written in the demo's `words(96)`, and the report tells us it's used generously. This is the system where that article's occasional live capture has been upgraded: whoever wants to determine the stack depth of their task, start your own reporter, let it sleep for 1000 milliseconds, and the line it reads is the answer.

The volume is real, and the watermark demo's ELF has a text segment of 4276 bytes, I counted data and bss as 476 and 2124, and the linker reported FLASH as 4752 bytes (7.25% of 64 KB), and RAM as 2604 bytes. The CI's smoke test also added the corresponding mark, and the line added in `scripts/ci.sh` is `[watermark]="watermark: read"`, each demo reports its own line, and we consider it alive if we find this line.

I truthfully report one environmental matter. In the platform description `bluepill.repl` of Renode, i2c1 and i2c2 use the `I2C.STM32F4_I2C` type. My local installation of Renode is 1.17.0, which doesn't recognize this type, and it reports `Error E04: Could not resolve type` when loading the platform. The header comment of repl itself says that our model is derived from the upstream platform of my local Renode 1.16.1, and the version difference is my environment, not the repository's problem. The demo doesn't touch I2C at all, and I comment out the two i2c lines in the /tmp copy and run it, and the output above is the real output after running. When you reproduce, just align the version with your local Renode.

## Acceptance

The verifiable facts of this article are summarized into four items, and we will go through them one by one.

The first item is the host exam. You check out to `fb9072b`, configure it with `cmake -B build-host -DZEROS_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug`, and build it. `ctest` should give a reading of 15 targets all passed, and the watermark test's report is 4 test cases and 41 assertions. The original text I ran in the /tmp copy is as follows:

```text
All tests passed (41 assertions in 4 test cases)
```

The second item is the cross-compile and board report. After the cross-compile is done, the watermark demo's FLASH should report 4752 bytes. When you run Renode, after one second, you should see two lines: `deep: 88 cap, 26 free, 62 used at deepest` and `shallow: 56 cap, 26 free, 30 used at deepest`. The difference between 62 and 30 is 32, which matches the number of words in the ballast array. The two lines about I2C version issues can be handled as explained above.

The third item is the self-reporting of the error direction. The three sections in the host test, the invisible hole, the optimistic one word, and the pessimistic but safe one, each corresponds to the source code's scan condition. You read the loop condition of `watermark_free_words` and the three sections' comments, and the numbers in the hole section and the pessimistic section can be derived from the condition. The boundary of the optimistic item is given in the comment.

The fourth item is what I didn't verify, and I truthfully present it. The one-second report of the demo only reads the watermark of the task's first second. In longer lifetimes, does the watermark still rise, and to what extent? This article doesn't have long-running readings. The generous `words(96)` is only evidence from the first second. The 32-word ballast array and the 62-word reading are perfectly aligned, but the real footprints of the call chain on the board at each station, we can't match them word by word. What we can match are the frames we manually constructed in the host exam. If we really want to determine the stack depth based on evidence, a long-running reading is an experiment you can add yourself, and the depth determined is the depth reached.

## Next Article

The watermark system is now established, and the stack depth has a table that can be retrieved on demand. The other half of the same commit belongs to the sister article "Sleep and Slow Down the Heartbeat": idle sleeps and reprograms SysTick to single-fire to the next due point, clears COUNTFLAG, withdraws pending from ICPR, and manually settles the three-piece race condition, plus 24-bit amplitude control for the installment续杯, and the `-DZEROS_TICKLESS=ON` comparative experiment is waiting for us there. We'll see you there.
