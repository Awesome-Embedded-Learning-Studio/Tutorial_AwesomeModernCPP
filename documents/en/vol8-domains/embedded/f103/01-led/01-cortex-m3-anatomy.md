---
title: 'Before the first blink: what the Cortex-M3 machine looks like'
description: 'Before the first blink, get a clear look at the machine: ARM sells blueprints rather than chips, Cortex-M3 is the first member of the family, and the two manuals split the work. What the CPU has to its name — 13 general-purpose registers plus SP/LR/PC, what each of the three pipeline stages (fetch, decode, execute) actually does, and three AHB buses I-Code/D-Code/System plus a PPB; Harvard only as far as the buses, with addressing unified into a single 4GB space. The address partition drawn in ink by architecture manual DDI 0403 Table B3-1 (Code/SRAM/Peripheral/XN/PPB), checked line by line against the initial SP 0x20005000 and the nvic/rcc entries of bluepill.repl. The verdict on the load/store architecture — arithmetic instructions touch registers only, memory access belongs to the ldr/str family alone, self-evidenced by how chapter A4.6 is a category of its own, with the full-firmware census to be cashed in by the disassembly in the next article. One str walked the whole way from fetch to pin, matched line by line against the Renode LogPeripheralAccess routing log, and the PCs in that log cross-checked against the disassembly in the next article — every reading reproduced in a real run'
chapter: 1
order: 1
tags:
  - stm32f1
  - beginner
  - 嵌入式
  - 单片机
difficulty: beginner
platform: stm32f1
translation:
  source: documents/vol8-domains/embedded/f103/01-led/01-cortex-m3-anatomy.md
  source_hash: 6b9e82d21c59b643ae358fd4a22b027e43fd60706c1059c32f028991b5997274
  translated_at: '2026-09-27T05:39:35+00:00'
  engine: anthropic
  token_count: 3600
---

# Before the first blink: what the Cortex-M3 machine looks like

Alright! Welcome to the LED-blinking stop of our journey. I was surfing Zhihu — China's answer to Quora — when I came across a delightful analogy. I never managed to track down where it originated, so let's borrow it a little.

> The first thing when starting out with C is that you write, with your own hands,
>
> ```c
> printf("Hello World");
> ```
>
> and the first thing when starting out with microcontrollers is that we light a lamp, with our own hands.

Yes — blinking an LED is, roughly, step one of the grand expedition along our whole STM32F103 line. It marks the moment we can control hardware at close range with software — really, I'm not exaggerating!

Of course, I don't plan to light the lamp right out of the gate. Among the mainstream 32-bit microcontrollers on the market, nearly every CPU that pulses when the power arrives comes out of the same drawing. Just as each of us has a personality of our own yet remains, in the end, human — right? The lineage of the species called human sets our most basic undertone. A CPU is no different: the lineage that sets its undertone is the celebrated thing called the CPU architecture. The microcontroller for this leg of our journey has a core named Cortex-M3, and the specification it follows is called ARMv7-M: v7 is the running number of ARM's architecture family, and the -M marks the microcontroller branch.

That architecture directly dictates **which instructions we can use**, **how addresses are numbered**, and how one write instruction travels from the CPU to a pin. If we leave all this unexamined, blinking an LED is just copying someone else's code verbatim — and when something actually breaks, all we can do is throw up our hands and cry, Man what can i say.

## ARM draws the blueprint, ST builds the chip

Since some of you may genuinely be unaware of this architecture business — too busy going all-in on building products you love — I think an introduction is in order. Who is ARM? A company, actually. It neither manufactures nor sells chips; what it sells are CPU designs: chip vendors who actually use the architecture pay a license fee, receive a design description ready for tape-out, add their own peripherals on top, and ship under their own brand<RefLink :id="3" preview="Wikipedia, ARM Cortex-M" />. (This business model does feel a little marvelous, somehow.)

The reach of this model is something we can feel with one casual lap of counting: ST's STM32, NXP's LPC, TI's LM3, Microchip's SAM3, Nordic's nRF, and the two M0+ cores inside the Raspberry Pi RP2040 — their hearts are, without exception, Cortex-M family designs. The Wikipedia entry<RefLink :id="3" preview="Wikipedia, ARM Cortex-M" /> tallies this vendor list far more completely; if you want the full count, go count it yourself. So learning this once means learning, in a single stroke, the shared heart of a whole swath of machines.

Cortex-M3, released in 2004, was the family's very first member, and its birth record can be found in the Wikipedia entry<RefLink :id="3" preview="Wikipedia, ARM Cortex-M" />: the target audience was microcontrollers, the instruction set was pared down to lean enough, interrupt latency was pressed to 12 cycles, and interrupt inputs top out at 240. Among the earliest vendors to turn it into silicon, a small company called Luminary Micro snatched first place in 2006, with ST following the very next year — that is the résumé behind our STM32F103, a chip that has now sold for over a decade<RefLink :id="5" preview="Power &amp; Robinson, Embedded.com, 2010" />, and it is exactly what sits on the dozen-yuan-or-so BluePill. From birth it has recognized only the Thumb instruction set (Thumb-1 plus Thumb-2); the old ARM cores' 32-bit fixed-length instructions it does not execute at all — the Wikipedia entry<RefLink :id="3" preview="Wikipedia, ARM Cortex-M" /> spells that out plainly as well. Which is why, in the next article's disassembly, instructions come in fat and thin: 16-bit and 32-bit interleaved — that is what Thumb-2 looks like.

This licensing model also explains something we should have wondered about long ago: why the manuals in our hands come as a pair. How the core does its work (which registers it has, what the instruction set looks like, how the address space is partitioned) is written in ARM's manual. What got added onto the chip (how many GPIO banks there are, how to configure the USART, at which address the RCC lives) is written in ST's reference manual, the one numbered RM0008<RefLink :id="4" preview="STMicroelectronics, RM0008" />: RM is short for Reference Manual, ST numbers all of its documents in this series, and from here on let's be a little lazy and just call it RM0008. The next article's `GPIO_TypeDef` offset table lives in the body of RM0008.

What we cover today lands almost entirely on ARM's side. ARM has one further level of documentation above that: the architecture reference manual numbered DDI 0403. DDI is ARM's own numbering series for technical documents, the same idea as ST's RM. And this manual happens to have the widest jurisdiction of all: RM0008 describes one particular chip, while DDI 0403 writes down the rules every Cortex-M3 must obey — the shape of the instruction set and the partitioning of the 4GB address space are drawn in ink in there too, as we are about to see.

## What the CPU has to its name: a handful of registers, a three-stage pipeline, three buses

Open the Cortex-M3 technical reference manual (DDI 0337 — the name is far too long, so let's take a small lazy shortcut below and abbreviate it to TRM) and go straight to the original text of §1.2.2<RefLink :id="2" preview="Arm Ltd., DDI 0337, §1.2.2" />: on the official configuration sheet for the core, the processor is 32-bit, the instruction set is fixed as Thumb-2, single-cycle 32-bit multiply and hardware divide are both fitted, the pipeline is arranged in three stages, and the processor interface is Harvard-style. The single-cycle multiply and hardware divide on that sheet are of no use to us for blinking an LED, so let's walk the three items we are about to use: the workbench, the pipeline, the buses.

First up is the workbench. Our CPU has 13 32-bit general-purpose registers, numbered R0 all the way to R12. Three more come with dedicated posts: R13 is the stack pointer SP, R14 is the link register LR, and R15 is the program counter PC in person. Standing alongside is one more, xPSR, whose charge is recording the arithmetic flags. The SP's identity is a little special: it actually comes in two banks, the main stack MSP and the process stack PSP — you don't need to worry about that layer right now; when we later turn to the more interesting RTOS (that is, ZerOS, the other tutorial series being continuously updated), we'll come back and give it a proper look~. This small handful of slots is the CPU's entire fortune: whatever data a computation needs, we must carry it onto the table, and carry it back once the arithmetic is done. Carrying in and carrying out correspond to exactly two actions: read and write. This bit of foreshadowing comes into play almost immediately.

Some of you may not be too familiar with how a CPU does its work. That's fine — let's talk it through briefly. It works along a three-stage pipeline: fetch, decode, execute, with each stage taking one job for its own (the manual calls the three stages Fe, De, Ex; just learn to recognize them — translating them in your head while reading the manual gets frankly depressing).

One sentence each on what every stage does:

- The fetch stage brings an instruction back from the code region. Bluntly put: we grab an instruction from wherever instructions live — usually our Flash chip — and stuff it into the CPU
- The decode stage works out what the instruction wants done and, in passing, computes the address it will access. Access what? Think: for the CPU to compute 1+2, someone has to say where those two addends come from, right?
- The execute stage is where the arithmetic really happens. We watch the CPU cutely count on its fingers — oh-ho, it's 3! What genius! Done computing, it hands the address and the data to the bus interface to throw back out, usually writing them back into memory

Three stages is honestly not deep; a desktop PC's CPU runs a dozen or more at the drop of a hat, but ours does not aim for that — the design priorities are **deterministic behavior and fast interrupt entry and exit** (what we call **real-time performance**), and that is precisely what microcontrollers care about most. You only need to hold one fact firm: the address is computed at decode time, while actually touching memory is execute-time business. Later we'll follow one write instruction through its entire journey, and you will see both steps with your own eyes.

The Cortex-M3's processor interface is Harvard-style: **instruction fetch and data access each go their own port, and the two can proceed simultaneously**. But "Harvard" reaches only as far as the buses, so the addresses we programmers see form one complete, unified 4GB. Some of you may be muttering here: "Eh? So do I need some special maneuver? One address encoding to fetch data, another address encoding to fetch instructions?" Nothing of the sort exists here. We simply take everything from one mapped 0-to-4GB space; it is just that the first 512MB is given to the code region for instructions and a later 512MB to SRAM for data — divisions of that kind. There is no such thing as "one numbering for instructions, another for data."

How is that pulled off? Count its outward-facing interfaces and it becomes clear: the Cortex-M3 stretches out three 32-bit AHB buses, plus one private peripheral bus in addition.

Let's call the roll of the ports one by one: I-Code is dedicated to fetching instructions from the code region, D-Code is dedicated to data reads and writes in the code region, System covers instructions, data, and vectors in the remaining space, and the PPB is devoted to the peripherals the core keeps for its own use — hanging off it are the interrupt controller NVIC and the SysTick timer.

It all sounds gloriously well-connected, but the benefit that lands on us boils down to one thing: the roads may be built separately, but the addresses must be numbered together.

This estate deserves a diagram of its own, with the registers, the pipeline, and the buses each in their proper place:

![Cortex-M3 core structure: register bank on the left (R0–R12 general-purpose, SP/LR/PC/xPSR with dedicated posts), the Fe/De/Ex three-stage pipeline in the middle, and the four bus exits I-Code/D-Code/System/PPB on the right](./01-cortex-m3-core.drawio)

## You may be getting impatient: how ARMv7-M actually governs our 4GB address space

Don't rush — let's open the ARMv7-M architecture reference manual (DDI 0403) at section B3.1.

There is one table here worth reading end to end: the architecture carves the 4GB space into several large blocks, and what each block holds, and with which attributes, is all fixed in ink — Table B3-1<RefLink :id="1" preview="Arm Ltd., DDI 0403E.e, Table B3-1" />, excerpted here to the rows that concern us:

| Address range             | Name       | Intended use                                   |
| ------------------------- | ---------- | ---------------------------------------------- |
| `0x00000000`–`0x1FFFFFFF` | Code       | Usually Flash; code and constants              |
| `0x20000000`–`0x3FFFFFFF` | SRAM       | On-chip memory                                 |
| `0x40000000`–`0x5FFFFFFF` | Peripheral | On-chip peripherals; execution forbidden (XN)  |
| `0x60000000`–`0x9FFFFFFF` | RAM        | External memory                                |
| `0xA0000000`–`0xDFFFFFFF` | Device     | External devices; execution likewise forbidden |
| `0xE0000000`–`0xFFFFFFFF` | System     | The first 1MB is the PPB (NVIC, SysTick); the vendor region follows |

The partition table is prescribed by the architecture; whichever vendor's chip lands in our hands, it treats them all alike. It is not that ST uses one scheme and NXP swaps in another. Not happening!

What ST does is fill in the boxes: 64K of Flash starting at `0x08000000`, exactly 20K of SRAM starting at `0x20000000`, and peripherals hung up one by one from `0x40000000`. For how the boxes were filled, consult the memory map in chapter 2 of RM0008 — that is the next article's home turf. The table even flags "the peripheral region must not be executed as code": XN, short for eXecute Never. The architecture's positioning of the circuitry behind those peripheral-region addresses is exactly this: forever read-write only, never jumped into and executed. The architecture seals off an entire class of accidents on our behalf, ahead of time.

Huh? Do we really get any use out of this table? We do. You probably just never noticed while copying the numbers. Back at [the getting-started station](../00-env-setup/03-toolchain-anatomy.md), we had the `file` command sniff the initial SP's position out of a `.bin`: `0x20005000` — which is exactly 0x20000000 plus 20K, landing precisely at the very top of the SRAM region. In our own bluepill.repl platform file, `nvic @ sysbus 0xE000E000` falls into the PPB box, and `rcc @ sysbus 0x40021000` into the Peripheral box. Renode's platform file is written line by line after the architecture's partition table, and the reason everything has lined up so neatly along our way is precisely the partition the architecture manual laid down.

The table pairs perfectly with a strip diagram: on the left, the six boxes the architecture drew in ink; on the right, what ST filled into them — every address we have verified and will be dealing with shortly is labeled on it:

![ARMv7-M 4GB address partition strip: the six blocks from the Code region at 0x00000000 to the System region at 0xFFFFFFFF, with ST's fill-ins annotated on the right (64K Flash, 20K SRAM, RCC, GPIOC, NVIC) and the XN attribute](./01-armv7m-memory-map.drawio)

## The CPU's outward actions: reads and writes, nothing else

Addresses we have; how do we take data, and how do we put it down? Now it is time for two supremely important instructions, which I must formally bring out: load, which handles reads, and store, which handles writes — a third does not exist. ARM is a textbook-grade load/store architecture through and through: every arithmetic instruction's operands are registers, and none of the names in that string — mov, cmp, add, orr, bic, mul, udiv — has the power to point at memory directly. When we want to touch memory or a peripheral, the only family available is ldr/str: the byte versions ldrb/strb, the stack versions push/pop, the move-many-at-once ldm/stm — all variants of that duo. Chapter A4 of the architecture manual sorts the instruction set into categories, and A4.6 is titled Load and store instructions, so there in the table of contents<RefLink :id="1" preview="Arm Ltd., DDI 0403E.e, A4.6" />: a chapter for arithmetic, a chapter for loads and stores — the TOC itself is the answer.

So let's plant this claim right here. Verifying it must be left to the disassembly, the next article's home turf: by then you will have just learned to read instructions, and we will casually run a census over the firmware's full 778 instructions to see whether the 278 that touch memory really do all sit inside this family. Carry that suspense along, and the verdict arrives in the next article.

And this is the root of "accessing peripherals needs no special instructions". Peripherals have addresses in the address table, and load/store is enough to touch them. In the eyes of the CPU and the compiler — in every line of code we write — `0x40011010` and `0x20000000` are a pair of addresses with no essential difference between them. A C pointer can point straight at a peripheral; why the next article's reinterpret_cast is legal and why volatile is necessary — the roots all sit on the same fact: instructions recognize addresses only, and never ask whether the thing pointed at is memory or a peripheral.

## One str, the whole journey: from fetch to pin

Now let's aim the camera at one instruction. The example's loop body contains exactly this line, `GPIOC->BSRR = 1u << 13`; it already made an appearance in the code of article 00 at the getting-started station, inside the 01_register_led example, which we will tear down in full in the next article. The result of 1 shifted left by 13 bits is settled at compile time as 0x2000, and what lands in the machine's hands is a single str. Let's follow it from the PC to the pin, stop by stop. Fetch: the PC is sitting in Flash (the code region, I-Code bus territory), and the str's encoding is brought back. Decode: this stage recognizes a write instruction and has the destination address `0x40011010` worked out (GPIOC base 0x40011000 plus the BSRR offset 0x10 — an equation we will verify with our own hands in the next article). Execute: this stage puts the address and the data 0x2000 out onto the System bus. Routing: the bus matrix receives the access and looks the address up in its table — the `0x40011000` stretch belongs to GPIOC on APB2, and the access is forwarded over. The matrix also keeps a single-entry write buffer on hand: when the bus is busy, it takes the write over for the CPU, so there is no idle waiting (TRM §14.11). Effect: the circuitry behind BSRR inside GPIOC receives the signal and pushes the output of pin 13 up; the pin's level changes, and the LED goes dark.

I turned these five stops into an animation: you can press the step key and walk through it frame by frame — who is doing the work at each stop, and what number they hold in hand, is written on the cards:

<Anim id="f103-str-journey" />

What happened along the whole chain is not something we take on imagination — Renode recorded the entire trip. Turn on GPIOC's peripheral access log and run one full on-off cycle:

```text
sysbus LogPeripheralAccess sysbus.gpioPortC
(after start, pause, then emulation RunFor "1.1"; log excerpt)

gpioPortC: [cpu: 0x8000164] ReadUInt32 from 0x4 (ConfigurationHigh), returned 0x0
gpioPortC: [cpu: 0x800016A] WriteUInt32 to 0x4 (ConfigurationHigh), value 0x0
gpioPortC: [cpu: 0x800016C] ReadUInt32 from 0x4 (ConfigurationHigh), returned 0x0
gpioPortC: [cpu: 0x8000172] WriteUInt32 to 0x4 (ConfigurationHigh), value 0x200000
gpioPortC: [cpu: 0x8000178] WriteUInt32 to 0x10 (BitSetReset), value 0x2000
gpioPortC: [cpu: 0x8000182] WriteUInt32 to 0x14 (BitReset), value 0x2000
gpioPortC: [cpu: 0x8000178] WriteUInt32 to 0x10 (BitSetReset), value 0x2000
```

Each line carries three elements: the PC at which the CPU issued the access, the register name (Renode translates these from the manual's register descriptions), and the value read or written. The first four lines are CRH's read-modify-write: read 0x4, write it back; read again, and write back 0x200000. Why this rhythm, and how many instructions each part settles into — the next article's disassembly will count them out for you. The last three lines are the loop body at work: write 0x10 (BSRR), lamp dark; write 0x14 (BRR), lamp lit; write 0x10 again — stepping along at half a second per stride, rounding out one full cycle. The PCs in the log, `0x8000178` and `0x8000182`, get a formal introduction in the next article, matching to the character.

> Of course, the log we see is what the peripheral model printed on receiving the accesses. The path it forwards along is exactly the road the architecture and the buses prescribe: the model receiving the write to `0x40011010` corresponds to that bus transaction arriving at the GPIO module on a real chip.

Hook the log onto the RCC as well, and we can watch one more address's destination: the read from the `RCC->APB2ENR |= ...` line in main that enables GPIOC's clock is recorded by the log as `rcc: [cpu: 0x800015C] ReadUInt32 from 0x18`. Within this same execution flow of ours, accesses in the `0x40021000` stretch are routed to RCC, and those in the `0x40011000` stretch are handed to GPIOC. The dispatch-by-address logic is precisely the bus matrix's "table lookup" job. The clock machinery gets a whole article of its own — the third one.

## The foundation beneath the foundation, laid

Today we followed one str from fetch all the way to the pin, verified the entire routing log line by line, and even the PCs in the log line up with the next article's disassembly.

The estate we have banked goes straight to work in the next article: peel the CMSIS macros layer by layer and at the bottom sit addresses straight off the architecture's partition table. The instructions that `|=` and `&=` settle into are exactly the read-modify-write we spoke of: address computed at decode, memory touched at execute. What volatile draws is the compiler's right to know that "the contents at this address can change on their own". The machine has been seen clearly; now it is the code's turn.

## Wait, wait! Self-check before you go

- Your turn to talk: which manual did ARM publish, and which did ST? The GPIO offset table and the 4GB partition table — which book does each live in?
- What are `0x20000000`, `0x40000000`, and `0xE0000000` each? While you're at it, think about who decreed them: the architecture, or ST?
- If you want to touch memory or a peripheral, which single family of instructions can do the job? Where do arithmetic instructions keep their operands?
- Which stops does one str pass through from the PC to the pin? In the Renode log, on what grounds do we say "the routing actually happened"?

<ReferenceCard title="References">
  <ReferenceItem
    :id="1"
    author="Arm Ltd."
    title="ARMv7-M Architecture Reference Manual (DDI 0403E.e)"
    :year="2021"
    url="https://developer.arm.com/documentation/ddi0403/latest/"
    chapter="B3.1 The system address map (Table B3-1); A4.6 Load and store instructions"
  />
  <ReferenceItem
    :id="2"
    author="Arm Ltd."
    title="Arm Cortex-M3 Processor Technical Reference Manual (DDI 0337)"
    :year="2024"
    url="https://developer.arm.com/documentation/ddi0337"
    chapter="1.2.2 Processor core; 14.11 Write buffer"
  />
  <ReferenceItem
    :id="3"
    author="Wikipedia"
    title="ARM Cortex-M"
    :year="2026"
    url="https://en.wikipedia.org/wiki/ARM_Cortex-M"
    chapter="Licensing model; Cortex-M3 (2004); MCU vendor list"
  />
  <ReferenceItem
    :id="4"
    author="STMicroelectronics"
    title="RM0008 Reference Manual — STM32F101/102/103/105/107"
    :year="2021"
    url="https://www.st.com/resource/en/reference_manual/rm0008-stm32f101xx-stm32f102xx-stm32f103xx-stm32f105xx-and-stm32f107xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf"
    chapter="2 Memory map"
  />
  <ReferenceItem
    :id="5"
    author="Power, Liam & Robinson, Shane"
    title="The ARM Cortex-M3 and the convergence of the MCU market — Embedded.com"
    :year="2010"
    url="https://www.embedded.com/the-arm-cortex-m3-and-the-convergence-of-the-mcu-market/"
    chapter="Cortex-M3 launched 2004; Luminary Micro first MCU 2006; STM32 2007"
  />
</ReferenceCard>
