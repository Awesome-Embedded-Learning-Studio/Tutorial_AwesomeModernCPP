---
title: "挂牌 v0.1.0: README is the Gate, Walk Through Four Exams One by One"
description: "commit 6c0a4f7(release: ready to be public) marks the end of the entire handcrafted line: 13 lines for fun! The draft has been replaced with a complete Chinese README, covering why, what, quick start, and a table of examples without missing a single part. README.en.md and five badges have set up the facade, and MIT and Apache-2.0 have clearly defined the boundaries in the license section. Before going public, internal path cleaning was written into the diff. I checked out v0.1.0 in the /tmp copy and walked through the four exams to the places where they could be walked. All 15 tests on the host passed, the heartbeats from Renode and QEMU arrived on time, and the goal of lighting up the development board was honestly submitted."
chapter: 9
order: 4
tags:
  - stm32f1
  - intermediate
  - embedded
  - toolchain
difficulty: intermediate
platform: stm32f1
cpp_standard: [23]
reading_time_minutes: 20
prerequisites:
  - "Slow down the heartbeat when asleep: tickless, read clear race conditions and 24-bit分期续杯"
related:
  - "Run for five seconds, say your line: CI smoke test, include firewall and volume gate"
current_status:
  title: v0.1.0挂牌出门
  detail: The ZerOS chapter is currently being polished
  variant: polishing
  icon: 🎉
translation:
  source: documents/vol8-domains/embedded/zeros/09-second-board-and-release/04-release-v010.md
  source_hash: 4f32239d20029ab70d8f53ed1e6b52d99a99e8a430c4b39adeb3927c2fd91157
  translated_at: '2026-11-22T14:11:08+00:00'
  engine: qwen3:14b
  token_count: 10867
---
# Going Public with v0.1.0: README is the Gate, Walk Through Four Exams One by One

When we wrapped up our previous stop, the experiment we compared just finished. In the two seconds with the macro closed, our number was 2000 cycles of periodic interrupts, and with the macro open, it was zero periodic plus twelve one-shot interrupts, and four small sleeps, each chapter was exactly one beat. The kernel's capabilities were all there, and on the desktop, we had one thing left to do, which we had been talking about since the scheduler landed: we needed to make the repository public. Commit `6c0a4f7` did exactly that, with the subject "release: ready to be public", changing 6 files, adding 269 lines, and deleting 8 lines. Not a single byte of code in `include/ZerOS/` and `src/` was changed. It was the final commit of our entire handcrafted line, and the v0.1.0 tag was placed on it. Today, we walked through it, and we also completed the entire line.

For the facade engineering, we start with the old README's appearance. We took the `README.md` from `6c0a4f7~1` as it was, and counted it, the full text was 13 lines in size:

````markdown
# ZerOS: A toy C++23 written OS

for fun!

 ```shell
# configure (cross toolchain, once)
cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/arch/arm-none-eabi.cmake

# build & run in Renode
cmake --build build --target renode-bluepill
 ```
````

"for fun!" took up three words, followed by a cross-compile command and an old Renode target name. That's how the facade of the self-used repository looked, and it was enough for the person who wrote it to understand. We are at the last stop, and we need to see how the same repository can transform the card that was written for ourselves into a facade for strangers.

## The Structure of README: Each Section Has Its Reason

The new `README.md` has grown to 102 lines. We will go through its paragraphs in order, and for each paragraph, we will give it a reason for why it is placed there.

Right below the title is a banner reference `![banner](documents/assets/banner.svg)`, our hand-drawn picture is at the first glance position at the entrance. Below the banner is a linked declaration, a block quote with one line saying "Co-Apply for Tutorial_AwesomeModernCPP", with a line of Chinese text "linked project" next to it. It is placed so early for a reason: the repository has grown all the way from the beginning of our tutorial line, it is a live textbook of the tutorial, and we need to make sure that people entering can see it at a glance. Below the badge wall is a line for language switching, `[English](README.en.md) | **Simplified Chinese**`, English readers can turn to another README from it.

The badge wall follows, and we will look at its five badges in the next section. Below the badge wall is a line of positioning, we bring the original text to you:

```markdown
> 组件定位的 C++23 裸机 RTOS,面向 Cortex-M3——无堆、无 RTTI、无异常、无 C ABI。
> 一套内核,四处运行:桌面单测、Renode/QEMU 仿真,以及真机。
```

The positioning section answers the two questions that strangers want to ask most: one is what it does, and the other is where its track is. The four rejections (no heap, no rtti, no exceptions, no C ABI) are named in the positioning section. The constraints we have fulfilled with the link-time `ASSERT` and compile-time assertions in the previous stops are given a one-sentence preview by the facade. When you read the source code later, each one matches the scene from before.

Right after is the section "Why it is written this way", with three design principles, we bring the original text to you:

```markdown
- **RTOS 是组件,不是中心。** 应用只面向 `<ZerOS/*>` 头与 BSP 公开面,
  内核不认识任何板级寄存器;换板子不动内核。
- **裸机上能保留的全部 C++23:** concepts 约束接口、类型状态任务工厂、
  应用面 `std::expected`——热路径则保持朴素、静态、可数。
- **一套代码,四个目标:** host 单测 / Renode 仿真 / QEMU mps2-an385 / 真机。
```

There are a total of three principles, and we have all witnessed the scenes where they were fulfilled. The principle of "changing boards without changing the kernel" was fulfilled most loudly, and we use evidence to speak: in "The Same Heartbeat in QEMU", we verified with line-by-line diff, the two `main.cpp` of heartbeat, one for bluepill is 43 lines, and one for mps2 is 45 lines, the difference is only the comment at the beginning and one line of name string. This commit (`8efe93a`) submitted 13 files and 445 lines of new code, and the kernel's changes were zero. The principle of "keeping all C++23" is fulfilled by the type state task factory, `task::named().prio().words()`'s builder chain, which we have used several times in the scheduler's stops. Each principle has something to support it, and each one can be traced back to the corresponding article. You can go back and match them.

The section "What is in the kernel" is a list of capabilities, 32 levels of preemption plus same-level time slice rotation (bitmap plus CLZ) is inside, millisecond-level time service, dynamic tick and synchronization family is inside, work queue, log channel, stack canary and water level are also in the list, and the last word of HardFault is also there. At the end of the list is a sentence about volume, "core kernel in KB, maximum demo ~6KB", with a note in parentheses that it is measured in demo. We said that this statement has a volume gate in the CI to guard it, the ceiling of flash 12KB and RAM 6KB is measured every day, and we have weighed it in the article about CI.

The section "Quick Start" gives four paths, we copy the commands to you. The two on the host are:

```shell
cmake -B build-host -DZEROS_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host -j && ctest --test-dir build-host --output-on-failure
```

The one for cross-compiling into Renode, we look at its comments, the comments write the whole job of the command, compiling, simulation and watching the serial port all in one go:

```shell
cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/arch/arm-none-eabi.cmake
cmake --build build --target run-heartbeat -j
```

One line for QEMU is enough, the command we need to type is `cmake --build build --target run-mps2-heartbeat -j`. One for the development board is `cmake --build build --target flash-heartbeat -j`, going through the ST-Link channel. Each of the four paths corresponds to one exam, the "Quick Start" on the README is the entrance of our tutorial line, and at the final verification section, we have walked through all the ones we can.

The section on examples is a 12-line demo table, its format is one scene per line, each demo has a `run-<name>` target. The details of the table are left to a separate section later, where there are honest explanations. The layout section draws a tree of the directory, `include/ZerOS/` is the public API, the comments in `src/log/` say "formatting lives here, never enters the kernel", the hierarchical discipline we established back then is also given a line on the facade. The license section has only two sentences, its weight is waiting for us in the second half of the next section.

## Bilingual, Badges, and a Hand-Drawn Banner

`README.en.md` is newly established in this commit, it has grown to 110 lines, and is isomorphic with the Chinese version: the linked declaration, badge wall, positioning section, three principles, capability list, four quick starts, example table, layout, license, we have checked each section one by one, and there is no missing section. Bilingual is doing the job of the entrance, readers outside the Chinese circle enter from it, the "One kernel, four homes" in the positioning section and the Chinese "one kernel, four runs" are talking about the same thing. We have checked with `git show 6c0a4f7:README.en.md`, the English version's referenced commands and target names are word-for-word consistent with the Chinese version, none of them are outdated.

We have counted the number of badges according to the original text, and the number we counted is five. The first badge is CI, the link below is to the ci.yml in Actions. The next one is C++23, and another one is Cortex-M3. The freestanding one has the words "no heap · no rtti · no exceptions" printed on it, and the last one is MIT. Each badge has something behind it, the CI badge is backed by the four checks established by d48f5a3, the C++23 badge is backed by the real use of deducing this and constinit, the freestanding badge is backed by the four countermeasures at the link time. The engineering of the badges is most afraid of hanging up the signs that we cannot fulfill, the five we have in our hands can all point to the corresponding articles.

`documents/assets/banner.svg` is also newly established in this commit, it has 39 lines, and is hand-drawn. We will tell you about its construction. At the bottom is a dark rounded rectangle, with a very light grid. The left side of the picture is the large title "ZerOS", followed by a line of small text, the title is accompanied by a green broken line, the annotation says it is the "first demo" that ran at the beginning, according to the annotation, we guess it is the heartbeat curve from the bootstrap example of `6ee1d25`, the tail of the line has a green dot, with the word "alive" on it. The right side of the picture has four capsules (host, renode, qemu mps2, silicon), the first three are outlined in blue, and only the silicon's edge is green. Its lower left corner has a line of red text, the content of the red text is no heap · no rtti · no exceptions · no c abi, all four rejections are written on it. A 39-line picture has collected all the elements of the line: the heartbeat is the first demo, the four capsules are the verification matrix, the four rejections are the bottom line of freestanding, the color scheme is also there, the green edge of silicon, we guess it is the author's preference for "running on a real development board".

## Two Licenses, One Boundary

The `LICENSE` file is newly added in this commit, it has 21 lines, containing the full text of MIT. The year in the copyright line is 2026, and the attribution is Charliechen114514. We will not repeat the terms of MIT, what is worth our attention is the boundary next to it.

The CMSIS device headers under `third_party/` are not the author's, their ownership is ST, and the license is Apache-2.0. We have checked in the submodule on our local machine, the first three lines of `Drivers/CMSIS/LICENSE.txt` are "Apache License, Version 2.0, January 2004". The license section of the new README has only two sentences, which clearly state the boundary. The code of the repository itself uses MIT, our license is the LICENSE file in the repository. The CMSIS device headers under `third_party/` belong to ST, and the license is Apache-2.0, and the way of introduction is git submodule.

The reason for drawing the boundary cleanly is hidden in the shape of the repository: the files from ST have never entered the territory covered by `LICENSE` from the beginning to the end, their residence is submodule, and we use sparse to leave only three things. What is left is Include, Device, and the Apache-2.0 LICENSE.txt. If you fork this repository and modify your own things, MIT follows your changes. If you use the CMSIS headers, the obligation of Apache-2.0 follows the header files. The two licenses each manage their own territory, the two sentences in the README are their land deeds.

## Cleaning Before Going Public: Rewriting Two Internal Paths

The public repository and the self-used repository have a hard difference: in the text written for outsiders, there must be no more paths pointing to the internal structure of the author's computer. One place in the diff of this commit does exactly that, we look at the changes to `third_party/README.md`:

```diff
-孤儿 gitlink(上游 bug,详见 .claude/docs/architecture.md):
+孤儿 gitlink(上游仓库把 Device 目录做成了无 .gitmodules 映射的嵌套 gitlink):
```

The pre-modification writing pushes the responsibility of explanation to `.claude/docs/architecture.md`, it is the internal documentation of the author's local project, the readers of the public repository do not have it in their hands, clicking on it gets an empty page. The post-modification parentheses write the whole thing self-contained: the upstream repository has made the Device directory into a nested gitlink without `.gitmodules` mapping, the manual pull steps are still in place. Readers do not need to know what the author's study room looks like, their answers are all in one line, and we also record the judgment criteria for cleaning: after deleting the internal paths, if the information is not lost, it is a qualified cleaning.

In the same diff, `.gitignore` was also fixed, we record it truthfully. The original pattern was `*.log.*run.log*`, we look at its matching by shape, it hits only when the path contains both `.log.` and `run.log`, and it cannot catch the two types of logs that appear separately, so it is bad. Our modification is to split them into two lines `*.log.*` and `run.log*`, and added a new line `qemu-*.log` to catch the qemu logs that came out after `8efe93a` introduced qemu. Before going public, even the ignored rules in the repository need to be tidied up, the qemu log files will no longer enter the git's view from now on.

## Checkout v0.1.0, Walk Through the Four Exams One by One

The facade is set up, it's time for the acceptance. The `v0.1.0` tag is a lightweight tag (lightweight, the simplest way in git, it is just an alias pointing to a certain commit, without an independent tag object and annotation), `git rev-parse v0.1.0^{commit}` returns exactly `6c0a4f7`. After you get the repository, you check out to the label of v0.1.0, and the tree you get is the one we read today.

Our reproduction chain continues to follow the old path of /tmp copy, using `git archive 6c0a4f7` to export the tree (the third_party is the gitfile of the submodule, and the content cannot be exported by archiving, this processing has been used from the first article to today), and then copy the entire CMSIS in the local submodule into the `third_party/STM32CubeF1/Drivers/` under the copy. I will report the version of the toolchain. arm-none-eabi-g++ is 16.2.0 on my machine. Renode uses 1.17.0. QEMU uses 11.1.2.

The first exam is the host, we look at its score sheet:

```text
100% tests passed out of 15

Total Test time (real) =   0.23 sec
```

All 15 targets passed, from the bitmap, drop wake-up to the white box exam of stack water level, all are in there. The second exam is Renode, we knock down `cmake --build build --target run-heartbeat -j` in one go, the simulation runs for five seconds, the serial port prints out ten pairs of lines as expected, we摘 two lines for you:

```text
[INFO] usart1: [host: 2.07s (+0.5s)|virt:     2s (+0.5s)] Hello! ZerOS
[INFO] usart1: [host: 2.07s (+0.1ms)|virt:     2s (+5.2µs)] A heartbeat example!
```

The third exam is QEMU, we knock down `run-mps2-heartbeat`, our terminal flows out the banner "ZerOS demo: heartbeat (mps2)", followed by ten pairs of lines, timeout arrives and the resident demo is collected, the exit code of the target falls on zero.

The fourth exam I have to honestly report: I don't have a development board in my hands, the `blink` lighting on the real development board, I have never run it. The facts that can be checked in the repository are these: the `blink` demo directory is in `src/board/stm32f103_bluepill/example/`, the registry generates `flash-<name>` targets for each demo, it calls openocd, with two configuration files `interface/stlink.cfg` and `target/stm32f1x.cfg`, the command executed is `program ... verify reset exit`, the comment clearly states that the board must be plugged in, the lighting demo is its user. The quick start in the README lists this command among the four paths. The toolchain is complete, but the lighting action on the silicon, you have to press the board yourself. The empty in this section, I honestly leave it.

## A 12-line Demo Table, Corresponding to the Previous 25 Commits

The demo table has 12 lines, from heartbeat to perf, the records in `git log` before v0.1.0 are exactly 25 commits in quantity, we can say that this table is a map of them. However, the correspondence between the lines in the table and the commits, we do not match by feeling, we rely on the actual measurement of `git log --diff-filter=A`.

I checked the birth commit of the demo directory with `git log --diff-filter=A` one by one, the record is as follows: `0054cf8` generated the eight directories we have today in one go, a large part of the lines in the table came from its blade. Its subject says "seperate the examples", and the meaning of adding new cases is also in the sentence at the end. The 342-line single example was split into eight one-room demo by it. The other birth points: `blink` and `perf` are at `e2b7dc0`, `watermark` and `tickless` are at `fb9072b`, the heartbeat on the mps2 board is added by `8efe93a`.

So the 12 lines in the table correspond to 12 kinds of capabilities, each capability can point back to the station where it was discussed, inversion points back to the theater of priority inheritance, testaments point back to the scene of canary and last words, round_robin points back to the fairness of time slices, watermark and tickless point back to the first two articles of this site "Fill the Stack with Magic Numbers" and "Slow Down the Heartbeat When Asleep", perf points back to the performance line of DWT measurement of switching delay, blink points back to the first light on the earliest board. You can use this table as a directory, if you want to see the origin and development of a demo, just follow the corresponding station number to flip through.

The correspondence is detailed: the eight demo lines (heartbeat to round_robin) are split and registered in the article "One Room per Scene", watermark and tickless are assigned to this site's article 2 and 3, perf is assigned to the performance article of DWT measurement of switching delay, the heartbeat on the mps2 board is assigned to this site's article 1. The blink line, we found its birth commit is `e2b7dc0`, the scene of the lighting itself, we have not written a special article in the entire line, the archive perspective of this site's article 1 is its archive, the flash target is waiting for a real development board.

## Acceptance

The verifiable facts of this article are summarized into four, we will go through them one by one.

The first is the verification of the facade itself. You check out to `6c0a4f7`, the full text of the old README (`6c0a4f7~1` version) is 13 lines, the new `README.md` is 102 lines, the line count of `README.en.md` is 110, `LICENSE` is 21 lines of MIT, `banner.svg` is 39 lines of hand-written SVG, the number of badges is five, the line count of the demo table is 12, these numbers are all counted on the spot by `git show`. The verification of the license boundary also belongs to it: the first three lines of `Drivers/CMSIS/LICENSE.txt` are the text of Apache License 2.0, the two sentences in the license section of the README clearly demarcate the territory of MIT and Apache-2.0.

The second is the verification of the source cleaning. You run `git show 6c0a4f7 -- third_party/README.md` yourself, the internal path before the change and the self-contained writing after the change are both in the diff. The three-line changes to `.gitignore` are also in the diff, the bad pattern split and the addition of `qemu-*.log` can be seen at a glance.

The third is the final verification of the actual run. On the /tmp copy, the host's `ctest` gives 15 all-passed, 0.23 seconds result, Renode's `run-heartbeat` prints out ten pairs of lines, QEMU's `run-mps2-heartbeat` ends normally with timeout, the original text is in the paragraphs above. I will report the version of the toolchain again. arm-none-eabi-g++ uses 16.2.0. Renode uses 1.17.0. QEMU uses 11.1.2. The numbers come from this combination, I have not seen the output of the cloud CI combination (g++-14 plus arm 15.2.1). The old processing of i2c two nodes in Renode's repl is still the same: 1.17.0 can't recognize the type `I2C.STM32F4_I2C`, I have commented out a few lines of i2c in the copy, the demo path does not go through i2c, the behavior is not affected.

The fourth is unverified, I honestly put it. I have not executed the `flash-heartbeat` on the development board, we don't have a board in our hands, the repository checks out the definition of the target, the two configuration files of openocd and the `blink` demo directory, the lighting on the silicon is left to you who have the board. I checked the 39-line source code of `banner.svg`, the rendered image effect, you can see it by looking at the GitHub repository homepage. The lightweight attribute of the tag you can also verify on the spot, `git cat-file -t v0.1.0` returns the type `commit` instead of an independent `tag` object.

## Epilogue: From Super Loop to v0.1.0

The ceremony of the final has been completed, we will look back on the entire line. Our line has a total of 26 commits, the starting point is `6ee1d25`'s "initial feat: backbone setup", our endpoint is `6c0a4f7`, the subject is "ready to be public", our line ends here.

The starting point is an empty frame plus a super loop: `fe5a0e8` ran the bootstrap smoke test, the serial port printed our first line "Hello! ZerOS", at that time it didn't even have the concept of tasks, after printing in main, it was just one line of idle. The next memory stop, we talked about four articles on the topics of bitmap, pool, and guard, the bottom line of no heap was established at that time. The clock stop brought SysTick into the kernel, the definition of one second has now been owned. The scheduler stop was split into four articles, the bitmap of ready selects people, PendSV does the first cut, the race condition of drop wake-up and the story of ThisTask are also in there, two tasks for the first time in our eyes printed each other. The synchronization stop gave us the semaphore and priority inheritance mutex, the context stop gave us the canary, last words, and message queue, the fairness stop gave us the time slice and event group, the API stop split the demo into one room per scene, the CI stop set up four checks, the performance stop went to 72MHz, measured the switching delay, and honestly rolled back the negative optimization, the dual board stop moved the same kernel into QEMU, and also gave the stack water level and tickless.

As we count down, we have not skipped any failed scene: the uncompileable concept proxy, the race condition of drop wake-up, the six canary words eaten, the revoked ramfunc optimization, the i2c type that Renode can't recognize, all are honestly written into the corresponding articles. Every level is something we watched grow from one line of code, the zero change when changing boards, the water level report, the 2000 times vs 12 times, all the numbers are all run by us.

The train platform at the end is where you checked out the tree today. After you typed `git checkout v0.1.0`, the first command in the README ran 15 tests on the host for you, the second one printed the heartbeat in Renode, the third one went into QEMU, and the fourth one waited for a real development board. The entire line has walked from the super loop to here, becoming a family of 12 demos, 4 exams, and 1 tag. The road ahead, the "linked project" words on the facade will continue to lead you. We will stop here today.
