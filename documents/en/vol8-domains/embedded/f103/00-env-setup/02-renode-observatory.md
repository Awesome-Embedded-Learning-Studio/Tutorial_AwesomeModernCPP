---
title: "Renode observatory: no board, so who gets the final say?"
description: "A minimal install of the four toolchain pieces; how the Renode system-level simulator works and where its functional-level boundary lies; a line-by-line walkthrough of the resc script and the three additions in bluepill.repl's board-level household registry — the main event is a lesson in evidence: 250 ms sampling, the fake standstill of sampling at exactly one period, cross-checking against uwTick, all real output, plus a troubleshooting record of WSLg losing its window to a stale DISPLAY config"
chapter: 0
order: 2
tags:
  - stm32f1
  - beginner
  - 嵌入式
  - renode
difficulty: beginner
platform: stm32f1
reading_time_minutes: 15
related:
  - "Why C++, and by what right?"
  - "A short history of C++: where the bad reputation came from"
translation:
  source: documents/vol8-domains/embedded/f103/00-env-setup/02-renode-observatory.md
  source_hash: 0ba9c3882e836bf44adc6067c0603e8c421ce626f899e50e292b158772bfc602
  translated_at: '2026-09-25T08:23:42+00:00'
  engine: anthropic
  token_count: 7500
---

# "You say the LED is blinking — but where's the board?"

Fair point — where IS the board? Aha, dear reader, that's you not having read the last article carefully: we did say it — Renode! So what kind of entity is this? And why did we invite this old chap out?

The original spark for it came from a chat with @Leon19960120, who asked:

> Hey, this tutorial actually looks decent (this was back in the very early days of TAMCPP and the neighboring imx-forge), but I have a doubt — what about those of us with no board? Are you just turning us away at the door?

True, embedded development without a board really doesn't fly, but we still have to look after everyone just arriving. And even so, we did have grounds to say — fine, can't afford a board, not our problem anymore! (The author utterly disagrees with that position, mind you, but it does shut my mouth quite effectively.)

What actually sealed the decision to explore the Renode simulation route was a different question.

> Prove that your embedded program is logically sound.

The author works in internet client development. UI isn't the easiest thing to test, and it gets pushed around everywhere, but in the end we choose to peel the logic layer away from the interface layer (the GUI-programming jargon like MVP and MVVM, please leave to the GUI track) and test them separately. And here? Can we also take our ARM binaries and run regression and logic tests on them?

Yes — and that is in fact what Renode does: proving that an observed phenomenon is real. It just happens that this also lets us serve friends without a board!

> Front-row disclaimer: every output the author shows was actually run on this machine; if you follow along and type it in, you **should (environment setup is optional material)** be able to reproduce it line by line. If it doesn't reproduce, first suspect the author's environment, then yours, and only then Renode's — please file an Issue~

## Whoa! The tools you made me install are knocking on the door, huh?

YES. Exactly. We can finally set sail. Don't worry — installing the toolchain on Linux / WSL is not complicated at all. While your classmates are still anxiously installing compilers, all you need is a wicked little smirk and this one line:

```bash
sudo apt install gcc-arm-none-eabi cmake ninja-build
```

...and you get to put on sunglasses and become a low-key hacker.

Okay, okay, take the sunglasses off — this is so you don't die socially in front of everyone else when the linking goes wrong later (I trust you're beyond compile errors; lighting an LED isn't hard) or when the light just sits there dead at runtime.

Come back! Joking aside — where's Renode? The answer is we install it ourselves. Your humble servant has laid out the red carpet, this way please: download the `.deb` package from [renode.io](https://renode.io/); for installation details, defer to the [official installation docs](https://renode.readthedocs.io/en/latest/introduction/installing.html) (it depends on the .NET runtime, and the docs state that clearly). For Arch users the author won't write the command — heaven forbid the package name changes one day and this article turns into an accident scene, with you filing an Issue complaining that this Charliechen114514 is a jerk who conned you into a 404, and me tearfully running pacman -Syyu over your issue once again.

In this world, verifying a successful install doesn't only mean begging someone else to open your ancient interface and watch something pop up. Printing version numbers counts too.

```bash
arm-none-eabi-gcc --version && cmake --version && ninja --version && renode --version
```

If all of them print a version number, you're set. Troubleshooting installs that fail or break is out of scope for this piece — environment problems come in a thousand bizarre shapes; interrogating my Issues or asking an AI works better.

## Hey, hey — what even is Renode?

What is Renode? A **system-level simulator**. Another way to put it: what it simulates is not "a chip" but **an entire machine**.

A Cortex-M3-architecture CPU, the GPIO matrix, the Timers on it, the interrupt controller, our Memory, and the Bus linking it all together! A system! An embedded system, brothers!

In the days ahead, all of that — enough material for booklet after booklet — comes down, in Renode's eyes, to two files.

- The `.repl` file writes down "which parts the machine has and what each one's identity is" (Renode calls it a platform description)
- The `.resc` file writes "how this machine runs this time" (the startup script).

Flip back through `examples/01_blinky/` in the libestdx repo — both files are lying right there. The author does not lie~

Then why trust it? We must honestly spell out its boundary: **Renode is functional-level simulation, not cycle-accurate simulation**<RefLink :id="1" preview="Renode official documentation, Introduction" />. Functional-level means "if the program's logic walks right, it's right": whether the LED lit, what bytes the UART spat out, whether the interrupt arrived — on those it gets the final say; but "how many cycles this instruction took, what the frequency is down to the megahertz" — that it does not guarantee. So to any friend who genuinely needs verification to that level, I'm sorry — sumimasen! You really do need to step away from your computer for a bit and go find your old friends the logic analyzer and the oscilloscope.

## What I lit up is an LED! Not a chip

At this point, please open a terminal — or any IDE you find comfortable to work in — and confirm that your tools still cheerfully spit out version numbers when you hit them with --version.

Boot it up!

From the libestdx repo root:

```bash
cmake --build build --target sim
```

Build, launch Renode, load the firmware, start running — one command wraps it all up. Then the monitor parks at its prompt, and the screen starts scrolling the value of GPIOC's output register. The "script" this command follows is `examples/01_blinky/renode.resc`; let's match its main body line by line (the full version is in the repo, along with a `$status` macro that takes diagnostic snapshots — more on that later):

```text
using sysbus

mach create
machine LoadPlatformDescription @sim/stm32f1/bluepill.repl

$bin?=@build/examples/01_blinky/blinky

macro reset
"""
    pause
    sysbus LoadELF $bin
    start
"""

watch "sysbus ReadDoubleWord 0x4001100C" 700
```

Oh my! A distinct whiff of CSharp!

`mach create` conjures up a virtual machine. That's our opening move — picture your IT-support friend hauling a computer over to you and patting you on the shoulder: young lad, plenty of youth left, time to get to work.

`machine LoadPlatformDescription` loads in that "machine household registry" mentioned above. One probe into the machine — oh, so the little one is this STM32F103C8T6.

`sysbus LoadELF $bin` pours the firmware into the virtual Flash. What does that mean? Big bro, what does a virtual machine need to execute your code? Whatever you flash onto the MCU is exactly what it needs; the thing keeping our virtual machine fed meal after meal is precisely our firmware.

Of course, writing this out every single day would be downright silly, so once again we flex our abstraction superpowers and design a macro called `$reset`, with `pause` and `start` tucked in front and behind — so after recompiling the firmware, one `runMacro $reset` in the monitor reloads and resumes, no exit-and-restart needed.

> Two pitfalls deserve a name-and-shame. The first is the `@` paths: they resolve against **the Renode process's current directory**, not the directory the resc file itself lives in. That's why the CMake sim target pins `WORKING_DIRECTORY` at the repo root, so `@sim/...` and `@build/...` both line up; if you start Renode by hand from some other directory and include this script, every path breaks. The second is that question mark in `$bin?=`: a conditional assignment — "assign only if not yet defined" — leaving a hook to override the firmware path from the command line.
> The above came from GLM. After I'd written at length, GLM 5.3 truly could not watch any longer; I released my keyboard with both hands, and GLM told me the script you wrote is a pile of crap, and took the chance to mock me on those two points above, which left me furious. Friends, take note as well.

The bottom `watch` line has the monitor read the output register for us once every 700 milliseconds of idle time. Mind the word "idle": watch is driven only when the monitor is idle — if your script runs commands back to back, it won't make a sound.

This is the real output the author got — yep, it looks like this (one every 700 ms, first eight shown). Eight numbers with honestly zero aesthetic merit in any sense: no little LED bulb, only the jumping values read from GPIO.

```text
0x00000000  0x00000000  0x00002000  0x00000000
0x00002000  0x00000000  0x00002000  0x00000000
```

Let's decode these numbers: `0x4001100C` is the address of GPIOC's ODR (output data register); `0x2000` expanded in binary is bit 13; the value rolls between two states — which reads like a blinking light.

## bluepill.repl: writing the board's household registry

The Renode distribution ships an official chip-level description for the stm32f103, but not for the Blue Pill board. What does that mean? It means this: you look up the chip of the STM32F103 family, and it will say — "kiddo, we've got GPIO A through C, and USART1 through 3".

But it has absolutely no way to say "we have an LED here, on PC13, lit on a low level". Big bro, don't pull the wrong level.

That's exactly why `sim/stm32f1/bluepill.repl` is the board-level household registry we maintain ourselves. The skeleton looks like this:

```text
// flash is mapped both at its real address and at the boot alias 0x0 (on reset, SP/PC are fetched from 0x0)
flash: Memory.MappedMemory @ {
    sysbus 0x08000000;
    sysbus 0x00000000
}
    size: 0x10000

nvic: IRQControllers.NVIC @ sysbus 0xE000E000
    systickFrequency: 64000000

// onboard LED: PC13, lit on a low level
led: Miscellaneous.LED @ gpioPortC 13

// RCC — no official F1 RCC behavioral model; this one uses a PythonPeripheral to fill in the handshake protocol
rcc: Python.PythonPeripheral @ sysbus 0x40021000
    size: 0x100

// peripheral bit-band alias region: HAL enables the PLL via bit-band writes; if the alias region is not mapped, those writes are silently lost
bitBand: Miscellaneous.BitBanding @ sysbus <0x42000000, +0x2000000>
```

Of those three-hundred-some lines, the three additions matter most, and we'll take them one by one. **The dual mapping of flash**: after reset, a Cortex-M fetches the initial stack pointer and the reset vector from address 0; without the 0x0 alias, the very first step of the startup code can't find the door. The ending is: bro, how did you just drop dead, bro.

The second is **systickFrequency aligned to 64 MHz**: the SysTick reference frequency is a construction-time parameter; the skeleton in our previous article pulled HCLK up to 64M, so this must keep up, otherwise every millisecond count is wrong. As for this one, I'm sitting here hoping some big shot will build an integrated Framework. Haha!

**The PythonPeripheral for RCC**: the official F1 clock controller has no behavioral model, only preset register labels; if the firmware wants to switch the clock to the PLL, the status bit read back is forever 0, and what arrives is `HAL_TIMEOUT`. This version uses a stretch of Python script to act the handshake protocol out for real (the ready bit follows the enable bit). And finally, the bit-band alias region — Cortex-M3 hands every single bit of the peripheral registers its own alias address, and that's exactly the style of write HAL uses to flip the RCC switches; leave this region unmapped and those writes are **silently lost** — no error, just no effect.

Flip through the original file and you'll find every comment line explains a why. This repl is itself teaching material: later, when the button station adds buttons or the UART station looks at baud rates, we'll keep coming back to this file, posing as a Thinker, pondering how to make it ever more like a real bluepill.

## Fine, the numbers are changing — so it's really blinking?

Here comes the main event. We stare at those few lines of hex from watch and declare "the LED is blinking"; any rigorous engineer should slam the table on the spot: **you've looked at a handful of numbers — on what basis do you say it's blinking?** Good question (inexplicable applause). Here's the answer.

### Experiment 1: the sampling window must fit the transitions

Each watch read grabs one instantaneous value; whether the LED toggled between two adjacent reads, we honestly don't know. The reliable way is to open a sampling window on **virtual time**: after `pause`, use `RunFor` to advance the machine exactly 250 milliseconds, then read once — one beat is one beat. The firmware's half period is 500 milliseconds, and the 250 ms sampling interval is shorter than the half period; the real output of eight consecutive reads:

```text
0x00000000  0x00000000  0x00002000  0x00002000
0x00000000  0x00000000  0x00002000  0x00002000
```

Both values appear in pairs, and both are rolling. Can we hand down the verdict now? Not so fast — on to experiment two.

### Experiment 2: the fake standstill, an aliasing trap

This time we change the sampling interval: **exactly 1 second** — precisely one period. The real output of five consecutive samples:

```text
0x00002000  0x00002000  0x00002000  0x00002000  0x00002000
```

A constant! For five whole seconds the LED toggled without pause, yet the sampled readings never budged. Every sample lands on the same phase of the period: the signal moves, the sampling points don't. This phenomenon is called **aliasing**. Going by those five lines alone, you'd assert with iron certainty "the LED isn't blinking" — that's the observation method itself lying to you. It's not a simulator specialty either: a logic analyzer with an insufficient sample rate or a scope with a mis-set timebase will put on the same show on real hardware.

### Experiment 3: cross-checking two clocks against each other

There's an even more devious suspicion: what if the toggling "500 milliseconds" itself is fake? We drag in HAL's millisecond counter `uwTick` (incremented once per millisecond by the SysTick interrupt) for a cross-check: let the machine run exactly 1.1 seconds of virtual time first, then read its value. In this firmware `uwTick` lives at `0x2000000C` (one lookup with `arm-none-eabi-nm` tells you). Real output:

```text
0x00000452
```

Let's read the number out: `0x452` is 1106. Virtual time advanced 1100 milliseconds, and the millisecond counter walked 1106; the tiny surplus is the short stretch the machine ran before the script's `pause`. Two independent clocks agree — and only now does the paper value "half period 500 milliseconds" finally touch solid ground.

## The GUI for human eyes, plus one of the author's crashes

In headless mode all output stays in the terminal — great for scripts and CI; when we want to see the light with our own eyes, we launch the GUI from the repo root:

```bash
renode examples/01_blinky/renode.resc
```

For WSL2 users, WSLg should in theory deliver the window straight to the Windows desktop. **In theory**. The author crashed a real car right here: command typed, terminal dead silent, the window refusing to pop no matter what. The troubleshooting session is laid out for you — every WSL2 player will hit this sooner or later:

```text
$ echo $DISPLAY
localhost:0
```

A healthy WSLg value should be `:0`. `localhost:0` means "connect to the Windows-side X server over TCP" — a relic the author left in `.zshrc` from configuring VcXsrv years ago, while VcXsrv wasn't running at all. Verification is simple too: the socket `/tmp/.X11-unix/X0` exists, which means WSLg's X server is alive; nothing is listening on TCP port 6000, which means the route the old config points down is broken. The quick fix is to `export DISPLAY=:0` right there and rerun — the window pops out immediately; the permanent fix is to delete that relic line from `.zshrc` or change it to `:0`. One environment variable ate the author's entire evening of windows, and the story teaches us: **when troubleshooting environment problems, start from "what exactly is the display environment", not from "is the software broken".**

## Breathe out — let's get to work~

Next stop: a warm welcome for our LED!

<ReferenceCard title="References">
  <ReferenceItem
    :id="1"
    author="Renode Project"
    title="Renode Documentation — Introduction"
    :year="2026"
    url="https://renode.readthedocs.io/en/latest/"
    chapter="positioning of the system-level simulator and its functional-level boundary"
  />
  <ReferenceItem
    :id="2"
    author="Renode Project"
    title="Installing Renode"
    :year="2026"
    url="https://renode.readthedocs.io/en/latest/introduction/installing.html"
    chapter="official installation docs (.deb package and the .NET dependency)"
  />
</ReferenceCard>
