---
chapter: 16
difficulty: intermediate
order: 3
platform: stm32f1
reading_time_minutes: 9
tags:
- cpp-modern
- intermediate
- stm32f1
title: 'Part 21: Button Circuits and Mechanical Bounce — What Do Real-World Signals
  Look Like?'
description: ''
translation:
  source: documents/vol8-domains/embedded/02-button/03-button-hardware-and-bounce.md
  source_hash: 9398d3ddeed1a2b9d4640994010a5d6c2bc1b03fe9364af0bd94ec999184dc0f
  translated_at: '2026-06-16T04:10:55.429845+00:00'
  engine: anthropic
  token_count: 1467
---
# Part 21: Button Circuits and Mechanical Bounce — What Real-World Signals Look Like

> Following up on the previous part: We've sorted out the GPIO input path—pull-up input, Schmitt trigger, and the IDR register. In this part, we put theory into practice: we'll draw a wiring diagram, calculate the current, and then tackle a problem you'll never encounter in LED tutorials—mechanical bounce.

---

## Our Wiring Plan

In the LED tutorial, we used the onboard LED on the Blue Pill board—connected to PC13—which required no external wiring. Buttons are different—the Blue Pill doesn't have a dedicated user button (the reset button is dedicated to the NRST pin and can't be used as a general-purpose button), so you need to wire one up yourself.

Here is the wiring plan:

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

It's that simple—just plug the two wires of the button into the PA0 and GND pins on the Blue Pill header. No resistors, capacitors, or other components are needed. The STM32's internal 40kΩ pull-up resistor handles the default logic level for us.

### Current Calculation

When the button is pressed, current flows from VDD (3.3V) through the internal pull-up resistor (approx. 40kΩ) to GND:

```text
I = VDD / R_pullup = 3.3V / 40000Ω = 82.5μA
```

82.5 microamps. This current is very small—an STM32 pin can handle a maximum of 25mA, so 82.5μA is only 0.3% of the rated value. Plus, a button press is usually very short (on the order of hundreds of milliseconds), so the impact on power consumption is negligible. Even in battery-powered projects, this current is completely fine.

### Why PA0?

In the previous part, we mentioned the reason for choosing PA0: EXTI0 has a dedicated interrupt vector. Here is a practical reason—PA0 is easy to find on the Blue Pill headers. On the right-side header of the Blue Pill, PA0 is usually at the very top, and the adjacent GND pin is very close, so you can connect it with a short Dupont wire.

If you only have a 4-pin tactile switch, don't worry—the opposite pins on a 4-pin switch are connected (the same contact), and adjacent pins are the switch. You just need to pick two diagonal pins to connect to PA0 and GND.

### Alternative: Pull-Down Configuration

For reference, here is the pull-down configuration:

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

The pull-down scheme is "Active High"—released = low, pressed = high. This corresponds to `GPIO_MODE_INPUT_PP` (or similar pull-down settings) in the code.

We aren't using the pull-down scheme for three reasons: (1) In the pull-up scheme, the button connects to ground; GND is available everywhere on the board, making wiring easier; (2) The vast majority of MCU documentation defaults to the pull-up scheme, so community resources are more abundant; (3) If the button wire accidentally breaks or disconnects, the pull-up pin returns to a high level (safe state), whereas a floating pin has an indeterminate level, which could cause false triggers.

---

## Mechanical Bounce: The Button's "Original Sin"

With the wiring done, the button should theoretically produce an ideal signal: a clean jump from high to low the instant it is pressed, and a clean jump from low to high the instant it is released. Like this:

```text
理想的按钮信号：

高 ───────────┐                 ┌───────────
              │                 │
低            └─────────────────┘
              │← 按下 →│← 松开 →│
```

But in reality, mechanical switches aren't ideal devices. The metal contacts inside a button experience a brief "bouncing" process upon closing and opening due to spring effects and metal elasticity—the contacts make and break contact repeatedly before finally settling.

If you look at it with an oscilloscope, the actual signal looks like this:

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

The duration of the bounce depends on the physical characteristics of the switch—cheap tactile switches might bounce for 10-15ms, while high-quality ones might only bounce for 2-5ms. However, there is almost no such thing as a mechanical switch without bounce.

### Consequences of Not Handling It

If the code doesn't handle bounce and directly reads the pin state in the main loop, what happens?

Assume the main loop executes every 1ms (more than enough time for a 72MHz STM32). During the 10ms bounce after pressing the button, the CPU might sample a sequence like this:

```text
采样：  1 1 0 1 0 0 1 0 0 0 0 0 0 0 ...
         ↑       ↑ ↑       ↑
         按下    抖动中的假"释放"和假"按下"
```

What the CPU sees is: High→Low→High→Low→High→Low→Low→Low→Low... It will think the button was pressed three or four times, not once. If your code is "toggle LED state on every press," you might find that pressing the button once causes the LED to turn on, turn off, or not react at all—because the multiple toggles cancel each other out.

This isn't theoretical speculation—you can verify it easily. Write a simple polling program without any debouncing, press the button quickly, and use a counter to record the number of "presses" detected. You will find that a single press is counted as 2-5 times, or even occasionally 7-8 times.

---

## Hardware Debouncing (Optional Approach)

There are two ways to eliminate bounce: hardware debouncing and software debouncing. Let's discuss the hardware approach first.

### RC Low-Pass Filtering

The classic hardware debouncing solution is to place a capacitor in parallel with the button, using the low-pass filter characteristics of an RC circuit to smooth out rapid transitions:

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

When the button is open, the capacitor charges slowly to VDD (high level) through the pull-up resistor. The moment the button closes, the capacitor discharges rapidly to GND through the button (almost a short circuit). However, during bouncing, when the contacts repeatedly open, the capacitor charges through the pull-up resistor—due to the RC time constant τ = R × C, the capacitor voltage won't jump back to high immediately.

If R = 40kΩ (internal pull-up) and C = 100nF:

```text
τ = 40000 × 0.0000001 = 0.004s = 4ms
```

A 4ms time constant doesn't seem long, but the problem is that during bouncing, the contacts repeatedly open and close. During every brief open period, the capacitor only charges a tiny amount. Using the charging formula `V = VDD × (1 - e^(-t/τ))`, after being open for 1ms, the capacitor charges to `3.3 × (1 - e^(-1/4)) ≈ 0.73V`—far below the rising threshold of the Schmitt trigger (about 1.6V), so short bounces are indeed filtered out. But if the open lasts for more than 3ms, the capacitor charges to `3.3 × (1 - e^(-3/4)) ≈ 1.88V`—which has already exceeded the threshold, so the signal "leaks" through.

This reveals the core difficulty of hardware debouncing: RC parameters must find a balance between "filtering short bounces" and "not killing real long opens," and bounce times vary greatly between different switches, making it hard for one parameter to fit all.

If we use an external resistor (e.g., 10kΩ) plus a 100nF capacitor:

```text
τ = 10000 × 0.0000001 = 0.001s = 1ms
```

A 1ms time constant means the capacitor is almost fully charged to VDD (5τ) after 5ms. For bounces within 5ms, this RC combination does provide good filtering. However, switches with bounces exceeding 5ms (cheap tactile switches can bounce for 10-15ms) might not be filtered completely.

### Limitations of Hardware Debouncing

The problem with hardware debouncing is:

1. **Parameters aren't universal**: Bounce times vary significantly between switches (2ms to 20ms), so it's hard for one set of RC parameters to cover everything.
2. **Extra components**: Requires a capacitor, and sometimes an external resistor, increasing BOM cost and PCB area.
3. **Not perfectly reliable**: Even with RC filtering, residual bounce might still get through in extreme cases.

Therefore, in actual engineering, hardware debouncing is usually "nice to have"—if space and cost allow, adding a capacitor is certainly better. But **software debouncing is mandatory**; as the last line of defense, it can reliably handle all situations.

---

## Software Debouncing: Our Path

The core idea of software debouncing is simple: **Don't trust the first sample**. After detecting a pin level change, don't immediately assume the state has changed; instead, wait a while and sample again to confirm. Only if multiple consecutive samples are consistent do we acknowledge a real state change.

There are several specific implementation methods, which we will evolve step-by-step:

1. **Blocking Delay Debounce** (Part 05): After detecting a change, `HAL_Delay` waits, then samples again. Simple but costly—the CPU is blocked for 20ms and can't do anything else.

2. **Non-blocking Timestamp Debounce** (Part 06): Use `millis()` or a timer to record the time of the change and check if enough time has passed in each loop. Doesn't block the CPU but requires manual management of state variables.

3. **State Machine Debounce** (Part 07): Uses a finite state machine with 7 states to precisely manage the entire debounce and event detection process. This is our final solution and the most reliable one.

Each method is a natural evolution of the previous one—solving the problem with the simplest method first, then using a better method as issues arise. This "make it work, then make it right" learning path is much better than jumping straight to the final solution, because you understand the "why" behind every step.

---

## Our Hardware Preparation Checklist

To summarize, here is the hardware you need:

- **Blue Pill Development Board** — The same one from the LED tutorial, no need to change.
- **ST-Link V2 Debugger** — Same as the LED tutorial.
- **One Button Switch** — The most common tactile switch, 2-pin or 4-pin are both fine.
- **One or Two Dupont Wires** — To connect the button to the header pins (PA0 and GND aren't necessarily adjacent on the header, usually requiring a jumper wire).

There are only two wires to connect:

- One side of the button → PA0
- The other side of the button → GND

The PC13 onboard LED remains unchanged and requires no extra wiring.

⚠️ If you really don't have a button switch handy, you can simulate it with a Dupont wire—plug one end into PA0 and touch the other end to GND briefly. The effect is the same as a button, just without the spring rebound, so there might be slightly less bounce (but it will still be there).

---

## Looking Back

In this part, we did three things: drew the button wiring diagram (pull-up scheme, button connects PA0 and GND), calculated the current (82.5μA, totally safe), and explained in detail the "original sin" of buttons—mechanical bounce.

**Key Takeaway**: Mechanical switches produce 5-20ms of level oscillation when pressed and released. Without handling, this is misread as multiple key presses. Hardware debouncing helps but isn't fully reliable; **software debouncing is mandatory**.

In the next part, we start writing code—first, we'll use the HAL API to read the pin and see the actual results.
