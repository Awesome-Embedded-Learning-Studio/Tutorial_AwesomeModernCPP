---
title: "Run for Five Seconds, Say Your Line: CI Smoke Testing, Include Firewall, and Size Gate"
description: "d48f5a3 transformed manual verification from station 3 to station 6 into automated checks, integrating host unit tests, include firewall, size ceiling, and Renode smoke testing into a single ./scripts/ci.sh. The same script runs on both the cloud and locally. In this article, we will walk through this flattened PR, mapping the lessons of serial simulation, grep swallowing errors, and orphaned gitlink back to the script. Finally, we will run all four checks on a copy in /tmp and ensure they all pass."
chapter: 7
order: 2
tags:
  - stm32f1
  - intermediate
  - embedded
  - CMake
  - toolchain
difficulty: intermediate
platform: stm32f1
cpp_standard: [23]
reading_time_minutes: 20
prerequisites:
  - "One Room, One Scene: Demo Registry Center, Task Creation Three Paths, and Last Words Become Obligations"
related:
  - "Before the Chip Dies, It Leaves a Will: HardFault Scene, Stack Canary, and Zero Allocation Log"
current_status:
  title: Four Checks Have Been Established
  detail: The ZerOS chapter is currently being polished
  variant: polishing
  icon: 🚦
translation:
  source: documents/vol8-domains/embedded/zeros/07-api-and-ci/02-ci-gates.md
  source_hash: a18ccc6e45297d1fd6d0dd850893eae6ceb7ae32082d068088efc4f4b431a83d
  translated_at: '2026-11-22T14:03:35+00:00'
  engine: qwen3:14b
  token_count: 16460
---
# Run for Five Seconds, Say Your Line: CI Smoke Testing, Include Firewall, and Size Gate

In the previous articles discussing the scheduler, mutex, queue, and event group, the verification sections all described the same kind of work: you checkout to a certain commit, configure our build, run a few commands, and compare the numbers in the serial log with the article. All of this was manual. In "One Room, One Scene: Demo Registry Center, Task Creation Three Paths, and Last Words Become Obligations," I walked through with you the process of splitting a 342-line monolithic example into eight "one room, one scene" demos. After the split, manual verification should retire, because the number of things to manually verify has increased from one log to eight. Commit `d48f5a3` did exactly this: it consolidated four checks into a single script, handing it over to the cloud CI to run, and the same script can also be run locally. In this article, we will walk through it from start to finish.

One point we must honestly disclose: the subject of this commit is "Merge pull request #1 from Charliechen114514/feat/ci_ready". GitHub flattened the PR and merged it into main, leaving only one parent `0054cf8` in the git object, and the original commits on the `feat/ci_ready` branch were not preserved. In my local repository, I tried to find the commits `9dd03a8` and `6aa1e96` based on the commit archive I kept on hand (the archive is in my own work notes, not in the repository, and you won't see it). Git directly reported an unknown revision. Therefore, this article does not have a diff that can be walked step by step. We will guide through the system: the changes implemented are 8 files, 237 lines added, 11 lines deleted. What we need to read is the four checks in `.github/workflows/ci.yml` and `scripts/ci.sh`. Every file I reference is verified against the real content using `git show d48f5a3:<path>`.

## One Script, Two Execution Environments

Let's look at the beginning of the workflow, which is the directory card of the entire CI:

```yaml
name: ci
on:
  pull_request:
  push:
    branches: [main]

jobs:
  build-and-test:
    runs-on: ubuntu-24.04
```

Any pull request or push to main will trigger the same job on the ubuntu-24.04 runner. The end of this job consists of four steps, each of which still calls the same script we mentioned above:

```yaml
      - name: host unit tests
        env:
          CXX: g++-14
        run: ./scripts/ci.sh host

      - name: include hygiene
        run: ./scripts/ci.sh hygiene

      - name: cross build + size limits
        run: ./scripts/ci.sh size

      - name: renode behavior check
        run: ./scripts/ci.sh smoke
```

A single line of comment explains the reason: `# -- one step per check: Actions UI folds each into its own section --`, it remains as is in the workflow's comment. The GitHub Actions page will fold the logs by step, with each check occupying a step. If a check turns green or yellow, you can see it at a glance. If a check blocks the build, the page will directly tell you. When you click into the failure, you don't need to search through the log wall.

The toolchain version is determined based on reality. I have copied the top comment in the workflow as is for you. It is the author's own reminder and also provides a baseline for everyone who views the CI:

```yaml
# ZerOS CI — runs ./scripts/ci.sh on a Linux runner, one step per check.
#
# Toolchain reality check (2026-09-06):
#   xPack has NOT released GCC 16 yet (latest: arm 15.2.1, host 15.2.0).
#   Local dev uses Arch GCC 16.1 — but no 16-specific features are used
#   (D4 floor: GCC 14 for deducing this). CI runs on:
#     host: ubuntu-24.04 apt g++-14 (zero download, always there)
#     arm:  xPack arm-none-eabi-gcc 15.2.1 (pinned tarball)
```

We read these three footnotes, which all say the same thing: the repository itself is fresh, but the external dependencies have not kept up. xPack has not yet released the GCC 16 version. The author uses 16.1 on their local Arch, but does not use any features exclusive to 16, so the entire CI is based on 14 and 15. On the host side, `apt` installs g++-14 with a single command, nothing needs to be downloaded. On the arm side, it uses xPack's 15.2.1, a fixed version tarball. Renode still uses the 1.16.1 portable package. The D4 in the comment is the project's budget number for "supporting deducing this compiler floor", the floor is set at GCC 14, and the CI's choice has not fallen below the floor.

The script that runs on the cloud is the same as the one that runs locally. We can see the usage clearly stated at the beginning of `ci.sh`:

```bash
#!/usr/bin/env bash
# ZerOS CI — runs all checks. Use before every commit:
#   ./scripts/ci.sh            everything
#   ./scripts/ci.sh smoke      just one: host | hygiene | size | smoke
# Any check failing turns the exit code non-zero. The GitHub workflow
# runs this same script on a clean checkout.
```

`set -euo pipefail` stands at the entrance. If any check fails, the exit code becomes non-zero, and the workflow's step turns red. You can also run each check individually. You type `./scripts/ci.sh host`, and you get only the host unit tests running. In the verification section, we will really run the entire script once, and there will be all the real outputs.

## First Check: Unit Tests on the Host

The host-side check is the shortest. Let's look at all its code:

```bash
check_host() {
    banner "1/4 host unit tests"
    cmake -B "$HOST_DIR" -DZEROS_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug >/dev/null
    cmake --build "$HOST_DIR" -j"$NCPU" 2>&1 | grep -E 'error|warning' && fail "host build" || true
    ctest --test-dir "$HOST_DIR" --output-on-failure
}
```

It configures a Debug host build, enables `-DZEROS_BUILD_TESTS=ON`, and then hands the build work over to `ctest`. All the test cases we've seen on the host side, such as bitmap selection, dropped wakeups, mutex promotion, queue full ring, and timer callback suicide, are all on duty in this check. `ctest` comes with its `--output-on-failure`, and when it turns red, the entire output of the failed test case is printed out. The line with `grep -E 'error|warning'` carries one of our lessons, which we will return to in the lessons section.

## Second Check: grep as an Include Firewall

The second check is called hygiene, and it prevents something interesting: the direction of includes. Each of its three checks has its own grep: kernel headers must not recognize any board or vendor, kernel unit tests must not reach for architecture headers, and the root directory of `include/ZerOS/` must remain empty. Directional issues are not reported by compilation, nor by testing, but the architecture is broken. Therefore, it must be guarded by the facts of the text. Let's look at the code:

```bash
check_hygiene() {
    banner "2/4 include hygiene"
    # kernel headers must not know boards or vendors
    if grep -rnE '^\s*#\s*include.*(src/board|third_party|stm32)' include/ZerOS/kernel/; then
        fail "kernel purity: board/vendor include found in include/ZerOS/kernel/"
    fi
    # kernel unit tests must not bind to any architecture
    if grep -rn 'ZerOS/arch/' test/; then
        fail "test purity: kernel tests must stay architecture-free"
    fi
    # the include/ZerOS/ root stays empty — everything files into a subsystem
    if ls include/ZerOS/*.hpp >/dev/null 2>&1; then
        fail "layout: headers found directly in include/ZerOS/ root"
    fi
    echo "clean: kernel purity / test purity / layout"
}
```

The first grep checks every header file under `include/ZerOS/kernel/`. If any `#include` line contains the words `src/board`, `third_party`, or `stm32`, the check will block our build. It guards the direction of dependencies: kernel headers must not recognize any board or vendor. Recall the mutex theater, queue, and timer we've seen. The kernel code never mentions the name of the Blue Pill from start to finish. Board-level things all live in `src/board/`, communicating with the kernel through function pointers and board wiring. Once the direction is reversed, for example, if a kernel header includes a board header for convenience, the kernel can no longer be moved to a second board. Such changes often have no symptoms in compilation. The host can compile it, and the board can compile it, but the architecture is broken. This check doesn't take many words, and it doesn't depend on any compiler options.

The second one manages the purity of host tests: `test/` must not have references to `ZerOS/arch/`. We want the kernel's unit tests to be decoupled from the architecture. The objects it tests are bitmaps, queues, and scheduler logic. The assembly of a certain core is not within its range. Once a test file reaches for the arch header, it can only live on a certain architecture, and the host exam becomes a name only.

The third one manages the directory standard: the root directory of `include/ZerOS/` must remain empty, and any header file must be grouped into a subdirectory. It prevents the careless stacking. New headers are thrown into the root directory, and after three months, no one can say who is in charge. After the three checks are done, the script reports a line `clean: kernel purity / test purity / layout`, which we will see in the verification output.

## Third Check: Size Ceiling

The third check performs a cross-compile, then measures the size of each demo's full image. Let's look at it:

```bash
# regression ceilings (bytes). Not the D12 kernel budget — that needs a
# symbol-level split — these catch WHOLE-IMAGE regressions per demo:
# baseline 2026-09-06 max was ~5.3K flash / ~2.6K ram; headroom ≈ 2x
FLASH_LIMIT=12288
RAM_LIMIT=6144
```

At the beginning of the script, the ceiling's specifications are clearly written. We are worth reading this comment in full. It clarifies the boundaries of two things: D12 is the project's budget number for the kernel size. If you want to check the kernel by budget, you need to do a symbol-level split, which this check can't do. It deals with another matter: the full image regression of each demo. The numbers of flash 12KB and RAM 6KB are also explained in the comment: on September 6, 2026, the largest of the eight demos was about flash 5.3KB and RAM 2.6KB. Leaving about double the margin on this, the ceiling is set at 12288 and 6144 bytes.

Why is a one-time margin left, not just sticking to the baseline? Because its responsibility is to catch regression. The type of check that reviews the kernel item by item according to the budget can't do it. If someone's change causes a demo's image to increase by 200 bytes, it's likely an accidental inline or a forgotten table. The ceiling immediately turns red, and the author must explain where those 200 bytes are spent before merging. If we draw the line at 5.3KB, normal fluctuations will misreport daily, and the alarm will soon be ignored. Leaving a one-time margin, misreporting is zero, and real regression is exposed on the same day.

The method of measuring the size is in the body of the check. Let's look at it:

```bash
    shopt -s nullglob
    elves=(build/src/board/stm32f103_bluepill/example/*/zeros-demo-*.elf)
    [ ${#elves[@]} -gt 0 ] || fail "no demo ELF produced"
    for elf in "${elves[@]}"; do
        read -r text data bss < <(arm-none-eabi-size "$elf" | awk 'NR==2{print $1, $2, $3}')
        flash=$((text + data)); ram=$((data + bss))
        name=$(basename "$elf" .elf)
        printf '%-28s flash=%-6d ram=%-6d\n' "$name" "$flash" "$ram"
        [ "$flash" -le "$FLASH_LIMIT" ] || fail "$name flash $flash > $FLASH_LIMIT"
        [ "$ram" -le "$RAM_LIMIT" ] || fail "$name ram $ram > $RAM_LIMIT"
    done
```

After the cross-compile, it hands each `zeros-demo-*.elf` generated by the registry center to `arm-none-eabi-size`. `awk` extracts its text, data, and bss sections, and then converts them according to the specifications we explained in the first article: flash uses text plus data, and RAM uses data plus bss. Each demo reports one line of its size. If any number exceeds the line, the failure message includes the demo's name and the two numbers together. One line of automatically generated firmware is weighed individually here, and our registry center and size check fit perfectly.

## Fourth Check: Each Demo Owes Us a Line

It's time for the last check, which does Renode smoke testing. Its starting point is written in the banner: `each demo must SAY its line`, each demo owes us a line. The script's line is an associated array, let's look at it:

```bash
    declare -A MARK=(
        [heartbeat]="Hello! ZerOS"
        [event_stream]="got 'a'"
        [inversion]="inheritance paid off"
        [bottom_half]="job #1 done"
        [timers]="one-shot: fired"
        [events_notify]="both keys in"
        [testaments]="alive"
        [round_robin]="rr2 alive"
    )
```

The comment at the top of the script explains the nature of the line: `the one string each demo owes us when it works`, the string each demo owes us when it works. I can match the eight demos' lines back to the previous articles: the greeting of heartbeat is `Hello! ZerOS`, in the inversion theater, H gets `inheritance paid off` after L puts the lock, bottom_half's `job #1 done`, timers' one shot, events_notify's two keys, round_robin's second sound, all of which we have seen in our respective theaters. Behavioral-level verification is thus compressed into a single grep: simulate for five seconds, if a demo doesn't say its line, our check fails.

The `testaments` line is only one sentence `alive`, its comment explains the reason, let's look at it:

```bash
    # the one string each demo owes us when it works; testaments runs calm
    # (its 'x'/'y' death scripts stay manual, see docs/simulation.md)
```

We have seen the testament demo, it is usually quiet. Its two deaths (pressing `x` to execute undefined instructions, pressing `y` to crash its own canary) are left for the Renode section in "Before the Chip Dies, It Leaves a Will" as manual experiments. In CI, we only verify that it is alive, and the death methods are not included in the automatic checks.

There is a detail worth honestly disclosing in the running method, let's look at the script's comment:

```bash
    # simulate sequentially — a CI runner has 2-4 cores, 8 concurrent
    # Renode instances starve each other and virtual time barely advances;
    # 5s per demo × 8 = ~40s total, acceptable for CI
```

The eight demos are run sequentially, one after another. The reason is written in the comment: GitHub Actions' runner has only 2 to 4 cores. If eight Renode instances are opened at the same time, they will starve each other, and the virtual time will almost not move. A five-second simulation will be dragged to timeout and not finish. The cost of sequential running is five seconds per demo, about forty seconds for eight demos. The comment believes we can afford this cost. How this lesson was learned, we will detail in the lessons section.

Where each demo's output goes is also worth a look. The startup parameters used for the smoke test point the logs to individual files:

```bash
            cd "$root" && renode --console --disable-xwt \
                -e "logFile @${tmpdir}/${demo}.log" \
                -e "\$bin=@${elf}" \
                -e "include @${resc}" \
                -e "start" -e "sleep 5" -e "quit" \
                > /dev/null 2>&1
```

`logFile` points to a file named after the demo in the temporary directory. After the run, the script grep each file for its own line. The judgment after the check is also meticulous: if a demo doesn't speak, the failure message includes the expected original sentence, and the last 30 lines of the error log are entirely dumped to standard error. When you click on the red step, the crime scene is right in front of you, and you don't need to go back to the log file. The method of creating an independent log for each demo is the same as the modification in `run-renode.cmake` in the same change. Let's look at it:

```cmake
# 时间戳文件名:每次运行一个独立日志,不做 .1 .2 .3 轮换——事后翻现场
# 时一眼定位是哪一次跑出来的,不是考古一堆同名编号
string(TIMESTAMP _ts "%Y%m%d-%H%M%S")
set(_log "${RENODE_LOG}")
if(_log STREQUAL "")
    set(_log "renode-${_ts}.log")
endif()
```

The `run-<demo>` target, when running Renode locally, has a log filename with a timestamp, and does not do `.1`, `.2` rotation. The comment explains the reason: when looking back at the scene after the fact, we can locate it at a glance to see which run it was. By marking the evidence with a timestamp and dumping the log on failure, this set of checks has clearly put in the effort. The next few lessons we will see are also all revolving around this matter.

## Three Lessons, Mapped Back to the Script

The site's archives record three lessons, none of which are on the clean path, all of which are on the wrong path. The original fix commits were not preserved as the branch was flattened. I can't show you the on-site situation with line-by-line diffs. What I can do is map each one back to the existing script and workflow, and see what shape they have left in today's code.

### Sequential Running Was Forced

The comment in the smoke section has the answer there: the runner has only 2 to 4 cores. Eight concurrent Renode instances starve each other, and the virtual time almost doesn't move. The archive records the process: there were six fix commits in the PR before and after to confirm this fact. Initially, it was probably run concurrently, the virtual time didn't move, the simulation timed out, the author thought Renode was dead, tried various parameters, and finally admitted that it was his own concurrency that was starving. I can't verify the six fix commits, but this statement is supported by the existing code: the pre-build loop comment says `sequential, no lock contention`, and the simulation loop comment says `simulate sequentially`. Both places specifically mark sequential, as if the author was greeting us, reminding us not to go back to the old path of "goodwill" concurrency.

### grep filters logs, and real errors are filtered out together

Now back to the line I left in the host check:

```bash
    cmake --build "$HOST_DIR" -j"$NCPU" 2>&1 | grep -E 'error|warning' && fail "host build" || true
```

There is a similar line in the size check. Its intention is to save logs: the build output has hundreds of lines, filtering them, only leaving the lines with error and warning, usually quiet, and a few lines are enough when something goes wrong. However, this shape is connected to the two points we are about to mention. Let's look at them one by one. After filtering, the main body of the build output is entirely discarded. When the compilation really dies, the context of the error goes down the filter's drain, and we can see at most the few lines that match. The `|| true` also flattens the non-zero exit code of the pipeline. Even if the build dies without a single line of output containing error, this check line still passes, and the real failure only appears when the next line's `ctest` turns red or the size section reports `no demo ELF produced`. The place where it floats out is separated from the place where the incident happened by an entire segment.

We compare them, the failure path in the smoke section is another story: when it fails, it dumps the original 30 lines of logs, not a single word is filtered. The site's lessons record an entry called "grep filtering logs swallowed real errors", which means that the build check suffered from this shape, the error text was filtered out, and the person investigating only had one line `FAILED: host build`. I can't verify the on-site situation outside the archive, and I will honestly say this. The two lines of `grep` in the existing script are still hanging, they save logs on the green path, and rely on the subsequent `ctest` and ELF count to back up on the red path. To change according to the smoke idea, we need to fully retain the build logs, and dump them all out when it fails. This is the improvement it owes. I report according to the current shape of the script, and we don't cover for it.

### Orphaned gitlink: the hole left after sparse

The most special step in the workflow is the one that does the slimming of the submodule. The entire official SDK submodule was buried in the repository when it was opened, and the archive records an approximate number of 131MB. The firmware actually uses only the CMSIS headers. The slimming tool is git's sparse-checkout, which we call sparse checkout in Chinese habit: the repository is cloned in full, but only the paths you specify are checked out in the working tree, and the rest are not checked out. Let's see how it is set up:

```yaml
      - name: sparse the submodule to CMSIS only + fetch the orphaned gitlink
        run: |
          cd third_party/STM32CubeF1
          git sparse-checkout set --no-cone \
            '/Drivers/CMSIS/Include/**' '/Drivers/CMSIS/Device/**' '/Drivers/CMSIS/LICENSE.txt'
          git checkout
```

Once we set up sparse-checkout, only CMSIS's Include, Device, and license are checked out in the repository. The cost of CI cloning is reduced by an order of magnitude, and after slimming, it is about 13MB. For the slimming to stand, we also need to see if the places that use the header files recognize it. In the same change, the example's CMakeLists changed the include path from the `${CMSIS}` variable to the explicit submodule path, writing `Drivers/CMSIS/Include` and `Drivers/CMSIS/Device/ST/STM32F1xx/Include` explicitly, which correspond one-to-one with the two things left by sparse. The narrowing of the path and the narrowing of the checkout are coherent.

However, the name of this step carries another word, `orphaned gitlink`, which is our third lesson. `Drivers/CMSIS/Device/ST/STM32F1xx` is not mapped in the upstream `.gitmodules`, it is an orphaned gitlink that no one recognizes. We can't pull it with `git submodule update --init`, and after sparse, that location becomes a void. When compiling to find CMSIS device headers, it directly fails. The workflow's remedy is to manually pull it back:

```yaml
          git clone --filter=blob:none --no-checkout \
            https://github.com/STMicroelectronics/cmsis-device-f1.git \
            Drivers/CMSIS/Device/ST/STM32F1xx
          git -C Drivers/CMSIS/Device/ST/STM32F1xx fetch --depth 1 origin master
          git -C Drivers/CMSIS/Device/ST/STM32F1xx checkout c8e9a4a4
```

Clone the upstream cmsis-device-f1 repository alone, `--filter=blob:none` does not download the content of the files, `--depth 1` only takes one layer of history, and checks out to `c8e9a4a4`, consistent with the version recorded in third_party/README.md. This set of manual steps is not only written in the workflow, but also written in full in third_party/README.md, whose title is "Orphaned gitlink", and when we reproduce it on our own machines, we just follow the steps. The steps written for AI and the steps written for people are the same step, which is a rare good habit.

## Local Testing: The Same Set of Checks, We Run It Ourselves

After we have guided through the script and workflow, we verify the most important sentence: `./scripts/ci.sh` can indeed pass all four checks locally. My reproduction path is as follows: use `git archive d48f5a3` to export that tree to a copy in /tmp, then copy CMSIS's Include and Device into the copy's third_party (the real ZerOS repository's submodule is a gitfile working tree, which cannot be exported by archiving, this is the old handling we have been using since the first article), and finally comment out the i2c1 and i2c2 nodes in bluepill.repl. The i2c step is also an old handling, my local Renode 1.17.0 does not recognize the `I2C.STM32F4_I2C` type in repl. We have explained this since the first article, and the behavior of the demo is not affected if it does not go through the i2c path.

I need to split the version into two sentences and honestly say. The CI uses arm-none-eabi-gcc 15.2.1 plus Renode 1.16.1, while my local machine uses Arch's arm-none-eabi-gcc 16.2.0 plus Renode 1.17.0. The logic of the checks does not depend on the details of the version, but the numbers below are from my local version of this set of versions, and I have not seen the numbers on the cloud runner. We have not actually run the GitHub Actions cloud behavior in this article, and the workflow part is a guided tour based on the script and archive. We need to clarify this point.

When running the entire script, we run `./scripts/ci.sh` without any parameters, and we get all four checks running. The first result sheet:

```text
100% tests passed out of 14

Total Test time (real) =   0.23 sec
```

All 14 test targets passed. The exam ranges from bitmaps to type tables, all of which we have seen in the host exam, and the total time is 0.23 seconds. The second one is a single line to submit:

```text
clean: kernel purity / test purity / layout
```

The third one reports the weight of the eight demos one by one, which is its real output:

```text
zeros-demo-bottom_half       flash=5068   ram=1076
zeros-demo-events_notify     flash=4744   ram=2540
zeros-demo-event_stream      flash=4588   ram=2516
zeros-demo-heartbeat         flash=3812   ram=2460
zeros-demo-inversion         flash=4596   ram=2452
zeros-demo-round_robin       flash=3536   ram=2444
zeros-demo-testaments        flash=4592   ram=2476
zeros-demo-timers            flash=3428   ram=444
```

We check against the ceiling: the largest flash is bottom_half's 5068 bytes, which is more than one time away from the 12288 line, and the largest RAM is events_notify's 2540 bytes, which is also far from the 6144 line. Compared with the baseline of September 6, 2026 (about 5.3KB flash, 2.6KB RAM), today's numbers are on the same scale as the baseline, and we don't see any signs of significant expansion. However, the baseline comes from CI's GCC 15.2.1, and my numbers come from my local 16.2.0. Cross-compiler comparisons are reserved, and regarding the question of whether this repository has secretly gotten fat, we can only give strong evidence, not a definitive conclusion. The output of the fourth is a row of expected values:

```text
--- run-bottom_half (expect: job #1 done)
--- run-events_notify (expect: both keys in)
--- run-event_stream (expect: got 'a')
--- run-timers (expect: one-shot: fired)
--- run-inversion (expect: inheritance paid off)
--- run-round_robin (expect: rr2 alive)
--- run-heartbeat (expect: Hello! ZerOS)
--- run-testaments (expect: alive)
all 8 demos spoke their lines
```

Eight lines of expected values, we check them line by line, and all eight demos have spoken their lines. The last line of the script is `ALL CHECKS PASSED`, and we have obtained an exit code of zero. From pressing enter to passing all four checks, we only spent a few minutes locally, with the main part being eight five-second simulations. In the verification section, we summarize the verifiable facts we can check.

## Verification

We summarize the three verifiable facts of this article.

The first is the local reproduction. You checkout to `d48f5a3`, prepare CMSIS's Include and Device in third_party, and follow the two sections in `third_party/README.md`. The first section is initialization, giving us submodule init and sparse set. The second is the orphaned gitlink section, giving us the manual pull of the device header warehouse. We have done both, and then ran `./scripts/ci.sh`. The host should give you 14 targets all passed, hygiene should report `clean: kernel purity / test purity / layout`, the size should print the weight of the eight demos, all of which should be within our 12288 and 6144. The ending of the smoke should be `all 8 demos spoke their lines` plus a line `ALL CHECKS PASSED`. What I ran on my local machine with GCC 16.2.0 and Renode 1.17.0 is exactly what is posted above. If you are using Renode 1.16.1, you should not need to comment out the two i2c nodes. However, I honestly say, I have not verified whether the cloud runner ran or whether 1.16.1 recognizes the i2c type. My inference is that the author probably ran it through on 1.16.1, otherwise his own smoke test would not have passed. I wear this hat of inference, and you decide for yourself.

The second is what each check prevents, we can check back to the script. The host check runs `ctest`, preventing logical regression. The three greps of hygiene prevent the direction of includes, also the architecture purity of host tests, and the stacking in the root directory. The size check weighs each demo according to the specifications of `flash=text+data` and `ram=data+bss`, preventing the regression of the full image size. The smoke check runs each demo for five seconds, greps each of their lines, preventing behavioral regression. Each of the four checks guards one side, and when we combine them, we get the complete meaning. The code in our hands must pass through all three layers: host unit tests, cross-compile, and simulation behavior, and only then can this sentence be realized.

The third is what hasn't been verified and what hasn't been checked, I honestly lay it out. I haven't run the execution on GitHub Actions' cloud, and the guided tour of the workflow is based on the text it currently has in the repository. The download of the toolchain, the folding effect of the Actions page, these cloud behaviors you need to push a PR to see. The nine original commits on the PR branch were not preserved after the flattening merge. The lesson of sequential running is only supported by the comments in the script and the records in the archive. I can't verify the six fix commits. The two `grep -E 'error|warning'` filters and `|| true` flattening in the script are still there, and their swallowing of the failure scene can be inferred from the shape of the script itself. However, "which real error was swallowed once" I don't have first-hand evidence, only the records in the archive, and I separate them for you.

## Next Article

With the safety net established, we can have the confidence to make changes. According to the records in the dissection archive, when `e2b7dc0` and `fe270d9` cut the hot path of the scheduler, the records at that time were smoke and size as the criteria for regression. After the change, running `./scripts/ci.sh` once, all four banners passed, and our knife didn't cut off. The performance measurement demo later also entered the CI, and we treated it as a regular citizen. To be honest, it entered the build and size checks, and by looking at `d48f5a3`'s script, it's not in the smoke line's word list, the measurement switch is off by default, and it doesn't drag down our five seconds for everyone. We will look at what was cut, and how every microsecond saved on the hot path is measured out with Renode's virtual time.
