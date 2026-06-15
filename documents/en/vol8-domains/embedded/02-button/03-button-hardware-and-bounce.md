---
chapter: 16
difficulty: intermediate
order: 3
platform: stm32f1
reading_time_minutes: 10
tags:
- cpp-modern
- intermediate
- stm32f1
title: 'Part 21: Button Circuits and Mechanical Bounce — What Do Real-World Signals
  Look Like?'
translation:
  engine: anthropic
  source: documents/vol8-domains/embedded/02-button/03-button-hardware-and-bounce.md
  source_hash: b9992baf1af2425bd8b83ef661eeb50fa7fc4cd9b2e2294dc61d57248bb93c48
  token_count: 1461
  translated_at: '2026-05-26T12:11:06.404702+00:00'
description: ''
---
# Part 21: Button Circuits and Mechanical Bounce — What Real-World Signals Look Like

> Picking up from the previous article: we've covered the GPIO input path — pull-up input, Schmitt trigger, and the IDR register. Now we put theory into practice: drawing the wiring diagram, calculating current, and confronting a problem that LED tutorials never mention — mechanical bounce.

---

## Our Wiring Scheme

In the LED tutorial, we used the on-board LED on the Blue Pill — connected to PC13, requiring no external wiring. Buttons are different — the Blue Pill has no on-board user button (the reset button is dedicated to the NRST pin and can't serve as a general-purpose button), so you need to wire one up yourself.

The wiring scheme is as follows:

```text
         STM32F103C8T6 内部
         ┌─────────────────────┐
         │                     │
         │    VDD (3.3V)       │
         │      │              │
         │   [R_pullup ~40kΩ]  │
         │      │              │
         │      ├──── PA0 ─────┤─── 排针 PA0
         │      │              │
         │                     │
         │     GND ────────────┤─── 排针 GND
         │                     │
         └─────────────────────┘

         外部接线：
         PA0 排针 ──┤  按钮  ├─── GND 排针

   松开按钮：PA0 通过内部上拉电阻接到 VDD → 读到高电平 (1)
   按下按钮：PA0 直接接到 GND             → 读到低电平 (0)
```

It's that simple — connect the two pins of the button to the PA0 and GND header pins on the Blue Pill. No resistors, no capacitors, no other components needed. The STM32's internal 40kΩ pull-up resistor handles the default logic level for us.

### Current Calculation

When the button is pressed, current flows from VDD (3.3V) through the internal pull-up resistor (approximately 40kΩ) to GND:

```text
I = VDD / R_pullup = 3.3V / 40000Ω = 82.5μA
```

82.5 microamps. This current is extremely small — each STM32 pin can handle up to 25mA, and 82.5μA is only 0.3% of that rating. Moreover, a button press typically lasts a very short time (on the order of hundreds of milliseconds), so the impact on power consumption is negligible. Even in battery-powered projects, this current is a complete non-issue.

### Why PA0

In the previous article, we mentioned the reason for choosing PA0: EXTI0 has its own independent interrupt vector. Here's a practical reason to add — PA0 is easy to find on the Blue Pill header. On the right-side header of the Blue Pill, PA0 is usually near the top, and the adjacent GND pin is very close by, so a short DuPont wire is all you need.

If you only have a 4-pin tactile switch on hand, don't worry — the diagonally opposite pins on a 4-pin switch are internally connected (same contact), while adjacent pins form the switch. Just pick two diagonal pins and connect them to PA0 and GND respectively.

### Alternative: Pull-Down Wiring

For reference, there's also a pull-down wiring scheme:

```text
         STM32F103C8T6 内部
         ┌─────────────────────┐
         │                     │
         │   [R_pulldown ~40kΩ]│
         │      │              │
         │      ├──── PA0 ─────┤─── 排针 PA0
         │      │              │
         │     VDD ────────────┤─── 排针 3.3V
         │                     │
         └─────────────────────┘

         外部接线：
         PA0 排针 ──┤  按钮  ├─── 3.3V 排针

   松开按钮：PA0 通过内部下拉电阻接到 GND → 读到低电平 (0)
   按下按钮：PA0 直接接到 VDD             → 读到高电平 (1)
```

The pull-down scheme is "active high" — released equals low, pressed equals high. This corresponds to `ButtonActiveLevel::High` in code.

We don't use the pull-down scheme for three reasons: (1) in the pull-up scheme, the button connects to GND, which is available everywhere on the board, making wiring more convenient; (2) the vast majority of MCU development resources default to the pull-up scheme, so community resources are more abundant; and (3) if the button wire accidentally breaks or comes loose, the pull-up scheme returns the pin to a high level (a safe state), whereas a floating pin has an indeterminate level that could cause false triggers.

---

## Mechanical Bounce: The Button's "Original Sin"

With the wiring done, a button should theoretically produce an ideal signal: a clean transition from high to low the instant it's pressed, and a clean transition from low to high the instant it's released. Like this:

```text
理想的按钮信号：

高 ───────────┐                 ┌───────────
              │                 │
低            └─────────────────┘
              │← 按下 →│← 松开 →│
```

But in reality, mechanical switches are not ideal devices. At the moment the internal metal contacts close and open, due to spring effects and metal elasticity, they go through a brief "bouncing" process — the contacts repeatedly make and break connection until they finally settle.

Viewed on an oscilloscope, the actual signal looks like this:

```text
实际的按钮信号（按下瞬间）：

高 ───┐  ┌┐ ┌┐  ┌┐  ┌─────────────
      │  ││ ││  ││  │
低    └──┘└─┘└──┘└──┘
      │← 5~20ms →│
       抖动区间
      最终稳定为低电平

实际的按钮信号（松开瞬间）：

低 ─────────────┐  ┌┐ ┌┐  ┌─────
                │  ││ ││  │
高              └──┘└─┘└──┘
                │← 5~20ms →│
                 抖动区间
                最终稳定为高电平
```

The bounce duration depends on the physical characteristics of the switch — cheap tactile switches might bounce for 10-15ms, while higher-quality ones might only bounce for 2-5ms. But virtually no mechanical switch is completely bounce-free.

### Consequences of Not Handling Bounce

If the code doesn't handle bounce and simply reads the pin state in the main loop, what happens?

Suppose the main loop executes once every 1ms (more than fast enough for a 72MHz STM32). During the 10ms bounce period of a button press, the CPU might sample a sequence like this:

```text
采样：  1 1 0 1 0 0 1 0 0 0 0 0 0 0 ...
         ↑       ↑ ↑       ↑
         按下    抖动中的假"释放"和假"按下"
```

What the CPU sees is: high→low→high→low→high→low→low→low→low... It will think the button was pressed three or four times, not once. If your code toggles the LED state on each press, you'll find that pressing the button once might turn the LED on, turn it off, or leave it unchanged — because the multiple toggles cancel each other out.

This isn't theoretical speculation — you can easily verify it. Write a simple polling program with no debounce, quickly press the button once, and use a counter to record the number of "presses" detected. You'll find that a single press gets counted 2-5 times, and occasionally even 7-8 times.

---

## Hardware Debouncing (Optional Approach)

There are two approaches to eliminating bounce: hardware debouncing and software debouncing. Let's start with the hardware approach.

### RC Low-Pass Filtering

The most classic hardware debouncing scheme places a capacitor in parallel with the button, using the low-pass filtering characteristic of an RC circuit to smooth out rapid transitions:

```text
         VDD (3.3V)
           │
        [R_pullup]
           │
  PA0 ─────┤──────── 按钮 ────── GND
           │
        [C = 100nF]
           │
          GND
```

When the button is released, the capacitor slowly charges to VDD (high level) through the pull-up resistor. The instant the button closes, the capacitor rapidly discharges to GND through the button (nearly a short circuit). But during bounce, when the contacts repeatedly open, the capacitor charges through the pull-up resistor — due to the RC time constant τ = R × C, the capacitor voltage doesn't instantly jump back to a high level.

If R = 40kΩ (internal pull-up) and C = 100nF:

```text
τ = 40000 × 0.0000001 = 0.004s = 4ms
```

A 4ms time constant doesn't seem long, but the key is that during bounce, the contacts repeatedly open and close. During each brief opening, the capacitor only charges a tiny amount. Using the charging formula `V = VDD × (1 - e^(-t/τ))`, after 1ms of being open the capacitor charges to `3.3 × (1 - e^(-1/4)) ≈ 0.73V` — well below the Schmitt trigger's rising threshold (approximately 1.6V), so short bounces during opening are indeed filtered out. But if the opening lasts 3ms or more, the capacitor charges to `3.3 × (1 - e^(-3/4)) ≈ 1.88V` — already above the threshold, and the signal "leaks" through.

This exposes the core difficulty of hardware debouncing: the RC parameters must strike a balance between "filtering short bounces" and "not killing genuine long openings," and since bounce times vary greatly between different switches, a single set of parameters rarely works for all of them.

If we use an external resistor (say 10kΩ) with a 100nF capacitor:

```text
τ = 10000 × 0.0000001 = 0.001s = 1ms
```

A 1ms time constant means the capacitor is almost fully charged to VDD after 5ms (5τ). For bounces under 5ms, this RC combination does provide decent filtering. But switches with bounces exceeding 5ms (cheap tactile switches can bounce for 10-15ms) might not be filtered cleanly.

### Limitations of Hardware Debouncing

The problems with hardware debouncing are:

1. **Parameters aren't universal**: Bounce times vary significantly between switches (2ms to 20ms), so it's hard for one set of RC parameters to cover everything.
2. **Extra components**: Requires a capacitor, and sometimes an external resistor, increasing BOM cost and PCB area.
3. **Not fully reliable**: Even with RC filtering, residual bounce can still get through in extreme cases.

So in real-world engineering, hardware debouncing is usually "nice to have" — if space and cost allow, adding a capacitor is certainly better. But **software debouncing is mandatory**, serving as the last line of defense to reliably handle all cases.

---

## Software Debouncing: Our Path

The core idea behind software debouncing is simple: **don't trust the first sample**. After detecting a pin level change, don't immediately assume the state has changed — instead, wait a while and sample again to confirm. Only when multiple consecutive samples agree do we consider the state to have genuinely changed.

There are several specific implementation approaches, and we'll evolve through them step by step:

1. **Blocking delay debouncing** (Part 05): After detecting a change, use `HAL_Delay(20)` to wait, then sample again. Simple but has a cost — the CPU is blocked for 20ms and can't do anything else.

2. **Non-blocking timestamp debouncing** (Part 06): Use `HAL_GetTick()` to record the time of the change, and check on each loop iteration whether enough time has passed. Doesn't block the CPU, but requires manually managing state variables.

3. **State machine debouncing** (Part 07): Uses a 7-state finite state machine to precisely manage the entire debouncing and event detection process. This is our final and most reliable approach.

Each approach is a natural evolution of the previous one — first solve the problem in the simplest way, then use a better approach once you see the limitations. This "dirty first, clean later" learning path is far better than jumping straight to the final solution, because you understand the "why" behind every step.

---

## Our Hardware Preparation Checklist

To summarize, here's the hardware you need:

- **Blue Pill development board** — the same one from the LED tutorial, no need to switch
- **ST-Link V2 debug probe** — same as the LED tutorial
- **One button switch** — the most ordinary tactile switch, either 2-pin or 4-pin
- **One or two DuPont wires** — for connecting the button to the header (PA0 and GND aren't necessarily adjacent on the header, so you'll usually need a DuPont wire to jumper across)

The wiring is just two connections:

- One end of the button → PA0
- The other end of the button → GND

The PC13 on-board LED remains unchanged, with no additional wiring needed.

⚠️ If you really don't have a button switch on hand, you can simulate one with a DuPont wire — plug one end into PA0, briefly touch the other end to GND, then release. The effect is the same as a button, just without the spring rebound, so bounce might be somewhat reduced (but it will still be present).

---

## Looking Back

In this article, we did three things: drew the button wiring diagram (pull-up scheme, button connected to PA0 and GND), calculated the current (82.5μA, completely safe), and explained in detail the "original sin" of buttons — mechanical bounce.

The core takeaway: mechanical switches produce 5-20ms of level oscillation at the moment of pressing and releasing. Without handling, this gets misread as multiple button presses. Hardware debouncing helps but isn't fully reliable, so **software debouncing is mandatory**.

In the next article, we'll start writing code — first using the HAL API to read the pin, and seeing the actual results.
