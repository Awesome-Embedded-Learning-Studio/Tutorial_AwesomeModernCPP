---
title: "Renode observatory: no board, so who gets the final say?"
description: "A minimal install of the four toolchain pieces; how the Renode system-level simulator works and where its functional-level boundary lies; a line-by-line matchup with the resc script and the three additions in the bluepill.repl board description — the main event is a lesson in evidence: 250 ms sampling, the fake standstill of sampling at exactly one full period, cross-checking against uwTick, all real output, plus a troubleshooting record of WSLg losing its window to a stale DISPLAY config"
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
  source_hash: c9dd488c3049852663fd4bca6df0cdfb8ba6c894873922e7b9c6a6351b92924c
  translated_at: '2026-09-27T05:30:42+00:00'
  engine: anthropic
  token_count: 7500
---

# "You say the LED is blinking — but where is the board"

Fair point — where IS the board? Aha, dear reader, that's what you get for not reading the last piece carefully: we did say it — Renode! So what manner of being is this? And why did we invite this old chap out?

The original spark came from a chat with @Leon19960120, who asked:

> Hey, this tutorial of yours looks decent enough (back then TAMCPP and the neighboring imx-forge were both in their earliest infancy), but here's my doubt — what if some of us have no board? Do you just turn us away at the door?

True, embedded without a board really doesn't work — but we do have to look after the newcomers all the same. And even then, we have grounds to say: fine, boards cost too much, we'll just do without! (Your author thoroughly disagrees, but that really is enough to shut my mouth.)

What actually settled the decision to explore the Renode simulation route was a different topic.

> Prove that your embedded program is logically sound.

Your author works in internet client development. The UI is never exactly easy to test — it gets pushed around at every turn — but in the end we peel the logic layer away from the UI layer (as for MVP, MVVM, and the rest of the GUI-programming vocabulary, let's leave those to the GUI side track) and test them separately. And here? Can we get our hands on our ARM binaries and run regression and logic tests too?

Yes — and that is literally what Renode is for: proving that the phenomena are real. It just so happens this also serves our friends without boards!

> Front-row disclaimer: every output your author shows was actually run on this machine, and if you type along, you **should (environment setup is optional material)** be able to reproduce it line by line. If something won't reproduce, suspect my environment first, then yours, and only then Renode's — please file an Issue~

## Well, well — so the tools you told me to install are at the door now

YES. Yes indeed. We can finally set sail. Don't worry — installing the toolchain on Linux / WSL is not complicated at all. While your classmates are still anxiously wrestling the compiler into place, all you need is a wicked little smirk and this one line:

```bash
sudo apt install gcc-arm-none-eabi cmake ninja-build
```

… and on go the sunglasses, and you are a low-key hacker.

Okay, okay, take the sunglasses back off — just so you don't die socially in front of everyone when the link fails later (I trust you won't trip a compile error; lighting an LED isn't hard) or when the LED just sits there dead at runtime.

Come back! Joking aside — what about Renode? The answer: we install that one ourselves. Your humble servant has laid out the red carpet, this way please: grab the `.deb` package from [renode.io](https://renode.io/), and defer to the [official installation docs](https://renode.readthedocs.io/en/latest/introduction/installing.html) for the details (it depends on the .NET runtime; the docs spell that out). For Arch users I won't write the command — the day the package name changes, this article becomes an accident scene, you show up in the Issues complaining that this Charliechen114514 bastard tricked you into downloading a 404, and I get to tearfully pacman -Syyu my way through your issue all over again.

In this world, verifying a successful install doesn't only count once somebody else opens that ancient interface of yours and things pop out. Printing versions counts too.

```bash
arm-none-eabi-gcc --version && cmake --version && ninja --version && renode --version
```

If every one of them prints a version number, you're set. Troubleshooting a failed or broken install is out of scope for this piece — environment problems come in a thousand shapes, and interrogating my Issues or asking an AI will serve you better.

## Hey, hey — what exactly is Renode

What is Renode? A **system-level simulator**. Another way to put it: what it simulates is not "a chip" but **an entire machine**.

A Cortex-M3-architecture CPU, the GPIO matrix, the Timers on it, the interrupt controller, our Memory, and the Bus wiring all of it together! A system! An embedded system, brothers!

In the days ahead, all of that — enough material for booklet after booklet — is, as far as Renode is concerned, settled by two files.

- The `.repl` file writes down "which parts the machine has and what each of them is" (Renode calls it a platform description)
- The `.resc` writes "how this machine runs this time" (the startup script).

Flip back through libestdx's `examples/01_blinky/` and both files are right there. Your author is not lying~

So why trust it? We owe you an honest account of its boundary: **Renode is functional-level simulation, not cycle-accurate simulation**<RefLink :id="1" preview="Renode official documentation, Introduction" />. Functional-level means "if the program's logic walks right, it's right": whether the LED lit, what bytes the UART spat out, whether the interrupt arrived — on those it gets the final say; but "how many cycles this instruction took, what the frequency is down to the megahertz" — that it does not guarantee. So to any friend who genuinely needs verification at that level, I'm sorry — sumimasen! You really do need to step away from your computer for a bit and go find your old friends, the logic analyzer and the oscilloscope.

## What I lit is an LED, not a chip

At this point, please open a terminal — or whatever IDE you find comfortable to work in — and confirm that when your tools get hit with `--version`, they still grin and spit out their version numbers.

Game on!

From the libestdx repository root:

```bash
cmake --build build --target sim
```

Build, launch Renode, load the firmware, run — one command covers it all. Then the monitor parks at its prompt and the screen starts scrolling values of the GPIOC output register. The "script" behind this command is `examples/01_blinky/renode.resc`; let's match its main body line by line (the full version is in the repo, plus a `$status` macro that takes diagnostic snapshots — more on that later):

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

Oho! A whiff of C# in the air!

`mach create` conjures up a virtual machine. That's our opening move — picture your IT-support friend hauling a computer over to you, patting you on the shoulder: young fellow, plenty of youth in you, time to get to work.

`machine LoadPlatformDescription` loads in that "machine dossier" we just mentioned. The machine takes one probe — oh, so it's you, little STM32F103C8T6.

`sysbus LoadELF $bin` pours the firmware into the virtual Flash. What does that mean? Big brother, what does a virtual machine need to execute your code? Whatever you flash onto a microcontroller — that's what it needs, and what keeps our virtual machine fed meal after meal here is our firmware.

Of course, writing all this every single day would be insufferably long-winded, so once again we flex our abstraction powers and design a macro called `$reset`, padded with `pause` on one side and `start` on the other — so after the firmware is recompiled, one `runMacro $reset` in the monitor reloads and keeps running, no exit-and-restart needed.

> Two pitfalls deserve to be named. The first is the `@` path: it resolves against **the Renode process's current directory**, not against the directory the resc file itself lives in. That's why the CMake sim target pins `WORKING_DIRECTORY` to the repo root, so that `@sim/...` and `@build/...` both line up; if you start Renode by hand from some other directory and include this script, every path breaks. The second is the question mark in `$bin?=`: a conditional assignment — "assign only if not yet defined" — which leaves an opening to override the firmware path from the command line.
> The above came from GLM: after I'd been writing for ages, GLM 5.3 simply couldn't stand watching anymore; I released my keyboard with both hands, and GLM told me the script I wrote was a pile of crap, threw in some mockery about those two points above while it was at it, and left me fuming. Friends, take note as well.

That `watch` line at the bottom has the monitor read the output register for us once every 700 milliseconds of idleness. Note the word "idle": watch only ticks when the monitor is idle — if your script runs commands back to back without pause, it won't make a sound.

This is the real output your author got — well, it looks like this (one sample per 700 ms, first eight shown). Eight numbers with, frankly, zero aesthetic value: no little LED bulb, just GPIO read values flipping back and forth.

```text
0x00000000  0x00000000  0x00002000  0x00000000
0x00002000  0x00000000  0x00002000  0x00000000
```

Let's decode those numbers: `0x4001100C` is the address of GPIOC's ODR (output data register), `0x2000` in binary is bit 13, and the value rolls between two states — which reads, on paper, like a blinking light.

## bluepill.repl: writing the board down in full

The Renode distribution ships an official chip-level description for the stm32f103, but not for the Blue Pill board. What does that mean? It means: go look up a chip from the STM32F103 family and it will tell you — little friend, we've got GPIO A through C, USART1 through 3.

But it has absolutely no way to say "we have an LED here, on PC13, lights on low level". Big brother, don't pull the wrong level.

That's why our `sim/stm32f1/bluepill.repl` is a board-level description we maintain ourselves; the skeleton looks like this:

```text
// flash is mapped both at its real address and at the boot alias 0x0 (on reset, SP/PC are fetched from 0x0)
flash: Memory.MappedMemory @ {
    sysbus 0x08000000;
    sysbus 0x00000000
}
    size: 0x10000

nvic: IRQControllers.NVIC @ sysbus 0xE000E000
    systickFrequency: 64000000

// on-board LED: PC13, lights on low level
led: Miscellaneous.LED @ gpioPortC 13

// RCC — no official behavioral model for the F1 RCC; this one uses a PythonPeripheral to fill in the handshake protocol
rcc: Python.PythonPeripheral @ sysbus 0x40021000
    size: 0x100

// peripheral bit-band alias region: HAL enables the PLL via bit-band writes; leave the alias region unmapped and those writes vanish silently
bitBand: Miscellaneous.BitBanding @ sysbus <0x42000000, +0x2000000>
```

Of those three-hundred-some lines, three additions matter most, and we'll take them one at a time. **The dual address mapping for flash**: after reset, a Cortex-M fetches the initial stack pointer and the reset vector from address 0; without the 0x0 alias, the very first step of the startup code can't find the door. And the ending is: bro, how did you just drop dead, bro.

The second one is **systickFrequency aligned to 64 MHz**: the SysTick reference frequency is a construction-time parameter; the skeleton in our last piece pulled HCLK up to 64M, and this has to keep up, or every millisecond count comes out wrong. As for this one, I'm sitting here hoping some big shot will build an integrated Framework for it. Haha!

**The RCC PythonPeripheral**: the official F1 clock controller has no behavioral model, only preset register labels — firmware that tries to switch the clock to the PLL reads back a status bit that is forever 0, and what arrives instead is `HAL_TIMEOUT`. This copy uses a stretch of Python to act out the handshake protocol for real (the ready bit follows the enable bit). And last there's the bit-band alias region — the Cortex-M3 hands every bit of the peripheral registers its own alias address, and that is exactly the style of write HAL uses to flip RCC switches. Leave that region unmapped and those writes are **silently dropped**: no error, just no effect.

Flip through the original file and you'll find every comment line is explaining why. This repl is itself teaching material: later, when the button station adds buttons or the UART station looks at baud rates, we will come back to this file again, strike the thinker's pose, and plot how to make it look ever more like a real bluepill.

## OK, the numbers are changing — so is it really blinking

Now for the main event. We stare at those few lines of hex from watch and say "the LED is blinking" — a properly rigorous engineer should slap the table on the spot: **you watched a handful of numbers; on what basis do you claim it's blinking?** Good question (inexplicable applause). Here's the answer.

### Experiment 1: the sampling window must fit the transitions

Each watch read is one instantaneous value; whether the LED toggled between two adjacent reads, we honestly don't know. The dependable approach is to open a sampling window on **virtual time**: after `pause`, use `RunFor` to advance the machine exactly 250 milliseconds and then read once — one beat is one beat. The firmware's half period is 500 milliseconds, and the 250 ms sampling interval is shorter than the half period; the real output of eight consecutive reads:

```text
0x00000000  0x00000000  0x00002000  0x00002000
0x00000000  0x00000000  0x00002000  0x00002000
```

Both values show up in pairs, and both are rolling. Can we hand down the verdict now? Let's not rush — on to experiment two.

### Experiment 2: false stillness, the aliasing trap

This time we swap in a different sampling interval: **exactly 1 second** — exactly one period. The real output of five consecutive samples:

```text
0x00002000  0x00002000  0x00002000  0x00002000  0x00002000
```

A constant! For five full seconds the LED toggled without a moment's rest, yet the sampled readings didn't budge. Every sample lands on the same phase of the period — the signal moves, the sampling points don't. This phenomenon is called **aliasing**. Going by these five lines alone, you would swear flat-out that "the LED isn't blinking" — that's the observation method itself lying to you. And it's not a simulator special: a logic analyzer with an insufficient sample rate or an oscilloscope with the timebase set wrong will stage the same show for you on real hardware.

### Experiment 3: cross-checking two clocks against each other

There's an even sneakier suspicion: what if the toggling "500 milliseconds" is itself fake? We drag in HAL's millisecond counter `uwTick` (incremented once per millisecond by the SysTick interrupt) for a cross-check: let the machine run exactly 1.1 seconds of virtual time first, then read its value. In this firmware `uwTick` lives at `0x2000000C` (one lookup with `arm-none-eabi-nm` tells you); the real output:

```text
0x00000452
```

Let's read the number out: `0x452` is 1106. Virtual time advanced 1100 milliseconds and the millisecond counter walked 1106 — the tiny surplus is the short stretch the machine ran before the script's `pause`. Two independent clocks agree, and only now does the paper value "half period 500 milliseconds" finally touch the ground.

## The GUI for human eyes, plus one of the author's very own wrecks

In headless mode all output stays in the terminal — fine for scripts and CI. When we want to see the light with our own eyes, launch the GUI from the repo root:

```bash
renode examples/01_blinky/renode.resc
```

For WSL2 users, WSLg should in theory deliver the window straight to the Windows desktop. **In theory**. Your author wrecked a real car right here: command entered, the terminal goes quiet, and the window flat-out refuses to appear. Here's the troubleshooting process laid out for you — every WSL2 player runs into it sooner or later:

```text
$ echo $DISPLAY
localhost:0
```

A healthy WSLg value should be `:0`. `localhost:0` means "connect to the Windows-side X server over TCP" — a relic your author left in `.zshrc` years ago while configuring VcXsrv, while VcXsrv wasn't running at all. Verification is simple too: the socket `/tmp/.X11-unix/X0` exists, which means WSLg's X server is alive; nothing is listening on TCP port 6000, which means the route the old config points down is broken. The quick fix is to `export DISPLAY=:0` on the spot and rerun — the window pops out immediately; the permanent fix is to delete that relic line from `.zshrc` or change it to `:0`. One environment variable ate a whole evening of your author's windows, and the story teaches us: **when troubleshooting environment problems, start from "what exactly is the display environment", not from "is the software broken".**

## Breathe out — time to get to work~

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
