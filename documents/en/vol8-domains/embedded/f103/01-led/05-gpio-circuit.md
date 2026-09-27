---
title: 'Interlude: a pin is a circuit — push-pull, open-drain, and Schmitt triggers'
description: 'GPIO is not software, it is a piece of circuitry: we dissect the pin into its protection diodes, pull-up/pull-down resistors, Schmitt trigger, and the two stacked MOS transistors, and watch what happens in the circuit as each CNF setting clicks into place; why the push-pull seesaw drives actively in both directions, why open-drain cannot output a high level, and where a floating input actually floats; then the arithmetic of the Blue Pill''s active-low LED — the current-limiting resistor, sink versus source current, and PC13''s backup-domain 3 mA temper; and the piece-04 mishap where ODR refused to budge finally gets its circuit-level explanation'
chapter: 1
order: 5
tags:
  - stm32f1
  - beginner
  - 嵌入式
  - 寄存器
difficulty: beginner
platform: stm32f1
translation:
  source: documents/vol8-domains/embedded/f103/01-led/05-gpio-circuit.md
  source_hash: 35214b903768e787f45db4b546cf32d39aa4d8607ed7ea6648441a3663d8b667
  translated_at: '2026-09-27T05:47:15+00:00'
  engine: anthropic
  token_count: 7600
---

# Interlude: a pin is a circuit — push-pull, open-drain, and Schmitt triggers

In the previous pieces we finished walking the software end of things: addresses, clocks, configuration, toggling — all along we have been standing on the code's side, looking at registers. But a few questions we have been riding on the whole way without ever taking apart to the bottom: why exactly does `CNF=00` get to be called "push-pull"? Our floating input's "float" — where does it actually float? And then that faceplant in piece 04: the mode was never configured as output, BSRR got written, and the output driver played deaf to us — why? In this piece we descend to the circuit layer and take these questions apart one by one. Treat it as an interlude: skipping it does not break the main line's continuity. That said, in the embedded trade, if you want to take fewer tumbles, the intuition for circuits has to be made up sooner or later — we might as well make it up while the iron is hot.

**A GPIO's true body is not "software" — it is a real piece of circuitry.** Every line of code we write does nothing more than "issue an order". The ones actually executing the order are the few dozen transistors inside the pin. When we turn four bits in `GPIOC->CRH = ...`, what we are turning is the circuit's gear.

## Anatomy of the pin

Let's flay a GPIO pin open and look inside; there are these few things to find (a translated rendition of the block diagram in chapter 9 of RM0008 — RM0008 being ST's STM32F1 reference manual, the one we paged through in piece 01):

![Internal structure of a GPIO pin: the VDD rail at the top, the VSS rail at the bottom; on the left, the output leg with the P-MOS upper transistor and the N-MOS lower transistor controlled by ODR/BSRR; hanging on the pin node in the middle are the dashed weak pull-up/pull-down at ~40kΩ (connected by software) and the protection diodes (clamping to VDD/VSS); on the right, the input leg where a Schmitt trigger feeds through to IDR](./05-gpio-anatomy.drawio)

Let's look from the outside in. The outermost ring is a pair of **protection diodes**, clamping the pin's voltage firmly between VDD+0.3V and VSS-0.3V. When static electricity or a surge arrives, they are the first to meet it. Moving further in, there is a pair of weak **pull-up/pull-down resistors** of about 40 kΩ, and whether they are connected is software's call. Further in still, standing on the input path is the **Schmitt trigger**, in charge of judging the pin's continuous voltage into a 0 or a 1. At the far end of the output path stands the **output driver** — the seesaw built from two MOS transistors. Input and output are actually wired in parallel onto the same pin; which leg is on duty is decided by the four `CNF`/`MODE` bits.

## The output side: a seesaw of two transistors

With `CNF=00` (push-pull), we draw the output driver as two states side by side: the conducting transistor is colored in, and the direction of current is marked on the pin as well:

![Push-pull output in two states: on the left, with ODR=1 the P-MOS upper transistor conducts (green path), current is pushed out of the pin from VDD, and the pin sits near 3.3V; on the right, with ODR=0 the N-MOS lower transistor conducts (blue path), current is pulled back into VSS, and the pin sits near 0V](./05-push-pull.drawio)

Look at what happens when ODR is written as 1: the upper transistor conducts, the lower one cuts off, and the pin is "pushed" up to nearly 3.3V. The "push" here refers precisely to the path where current sets out from VDD and flows through the upper transistor into the external load. When ODR is written as 0, the direction flips entirely: upper transistor off, lower transistor on, and current from the outside is "pulled" back through the pin into VSS, pressing the pin down to nearly 0V. And **at any moment, one of the two transistors is actively driving** — that is exactly what the name push-pull is talking about. Seen from either direction, it is a low-impedance output: the level is well-defined and the edges are steep; a few tens of ohms of on-resistance up against the load, and the switching comes out crisp and clean.

Drive capability comes with hard numbers: an ordinary F103 pin tops out at an absolute maximum of 25 mA per pin (the datasheet's electrical characteristics), while everyday LED-lighting spends only a few milliamps — headroom galore. But PC13 happens not to be an ordinary pin; we will keep it for the second half of this piece: it has a 3 mA ceiling.

## Open-drain: the circuit with only the lower transistor

Now look at `CNF=01` (open-drain), which removes the upper transistor outright:

![Open-drain output: the upper transistor's spot is left empty (that emptiness is the “open” in “open-drain”); VDD reaches the pin only through an external pull-up resistor (off-chip), and below the pin only the N-MOS lower transistor connects to VSS; a note on the right explains that writing 0 pulls the line low, while writing 1 leaves the pin floating, hauled high by the external pull-up](./05-open-drain.drawio)

Write 0, and it is the lower transistor that conducts: the pin is pulled to ground, and the path for current is still there. Write 1? The lower transistor cuts off, **and that is the end of the story**: the upper transistor does not exist, the path between the pin and VDD is gone, and the pin floats (high-impedance state). To output a "high", you have to rely on an external pull-up resistor to haul the pin up, slowly. How fast that haul goes, and how much current it leaks at idle, are both its call: choose a small one for speed, a large one for thrift — and you cannot have both ends at once.

So what is open-drain for? Three serious scenes. First on stage is the **I2C bus**. Its protocol demands that multiple devices share a single wire, that every one of them may pull the line low — and that none of them may ever actively push it high. Why? Two push-pull outputs from two devices butting heads is, as an outcome, a short circuit. Open-drain with an external pull-up is born holding up the protocol's requirements (the I2C station, stop five, will put it to the proof). **Wired-AND** counts as another: several open-drain outputs share a single pull-up, and the moment any one of them pulls low, the whole line goes low — an AND built without a single gate. And then there is **level shifting**: hang the pull-up on 5V and the output "high" is 5V, so a 3.3V chip can shake hands with 5V-era equipment (provided the pin is 5V-tolerant).

Taking open-drain to light an LED is asking for suffering: when the output is "high" the pin floats, the LED anode's voltage has nothing to rest on, and the lamp either stays dark or glows most reluctantly. The code looks entirely correct, yet the lamp simply ignores us. So for lighting LEDs we choose push-pull; open-drain's home ground is over on the bus side.

## The input side: Schmitt and the pull resistors

Now it is the input path's turn. First question: how does the continuous voltage on a pin become the 0s and 1s in IDR? Standing in between is the **Schmitt trigger**, and its judgment carries hysteresis: only when the voltage climbs past the high threshold does it count as a 1; only when it falls past the low threshold does it count as a 0; and stuck in between the two thresholds, it keeps whatever it last decided. Why leave that middle band? Because real signals carry noise. With a single 2.0V threshold, a slow signal wobbling around 2.0V would have the output chattering into a string of glitches. With a hysteresis band, no matter how the small noise squirms, it cannot change the output's verdict. Contact bounce on a button, induced pickup on a long wire — the Schmitt trigger is the first thing standing in their way for you.

> The exact threshold values belong to the electrical-characteristics tables of the datasheet; they differ from pin type to pin type, so when we need them we look them up — no need to memorize.

Second question: what does a pin left hanging read back as? **Indeterminate.** That is the literal meaning of "floating" (`CNF=01` with no pull resistor connected): the pin's level is decided by the ambient electromagnetic field, the static on your finger, the shoving among nearby traces — put a multimeter on it, and the reading jumps all over the place. The reset default `0x44444444` leaves every pin a floating input, so what we receive is the "most neutral" factory state: we drive no level of our own, and whatever signal arrives from outside is taken in as-is.

If we want a definite default level, it is the pull resistors' turn on stage: with `CNF=10`, writing 1 to the corresponding ODR bit connects the pull-up and the pin defaults high; writing 0 connects the pull-down and it defaults low. You read that right: **on the F1, the input pull-up/pull-down choice hides inside ODR** — a quirky design the F1 holds alone (from the F4 onward it moved into a dedicated PUPDR).

This resistor pair sits around 40 kΩ — weak in exactly the right way: when the button is pressed, it gets shorted straight to ground and the pin is pulled solidly low; once you let go, it is strong enough to haul the pin back to the default level. The standard posture of button detection (pin with pull-up, button to ground, watch for the falling edge) — that is what we will live on at the next station.

## Back to the Blue Pill: the circuit arithmetic of active-low lighting

Finally, let us compute this lamp's circuit properly. The Blue Pill's onboard LED is wired the "sink-current" way:

![The Blue Pill onboard LED's sink-current wiring, two states side by side: on the left, PC13=0 lights the lamp — the 3.3V rail pushes current down through the current-limiting resistor R (off-chip, dropping 1.3V) and the LED (forward drop about 2V, anode on top, cathode below) into PC13, and from there through the conducting N-MOS lower transistor into VSS, with a blue arrow marking the current's direction; on the right, PC13=1 puts the lamp out — the lower transistor is off, both ends of the leg sit at the 3.3V level, there is no potential difference and no current](./05-blue-pill-led-circuit.drawio)

The LED's anode hangs on 3.3V through the current-limiting resistor; its cathode goes to PC13. When PC13 outputs low (lower transistor conducting), current setting out from 3.3V flows through the resistor and the LED into the pin, and then through the lower transistor to ground — and the lamp lights. "Sink" refers to exactly this direction: the current is poured into the pin. When PC13 outputs high, both ends sit at the 3.3V level, there is no potential difference and no current, and the lamp goes out. **The entire mechanism of "active-low lighting" sits right here**: what decides the lighting is the LED's wiring, not the software's logic. Swap to the "source-current" wiring (LED from the pin to ground) and it becomes the high level that lights it. Both kinds of board are common enough — read the schematic until it makes sense before you commit to a polarity.

Now the current-limiting resistor side: the LED's forward drop is about 2V (typical for red), so the resistor's share is `3.3 - 2.0 = 1.3V`. Work it with the few-kΩ values common on the Blue Pill, and the computed current lands in the few-tenths-to-one-milliamp class — plenty bright for a modern LED. And what if the resistor were gone? The LED's drop stays clamped around 2V, the remaining 1.3V lands entirely on the pin's MOS transistor, the current is decided by the transistor's on-resistance, and it sails past 25 mA with ease. The outcome: a short-lived LED, a heating transistor, and nobody happy. So the current-limiting resistor is not an optional decoration — leave it out, and things really do go wrong.

PC13's temper deserves a word too. It and PC14/15 draw their supply from the **backup domain**: the RTC real-time clock, tamper detection, the LSE low-speed external crystal — the functions that must stay alive through power-down all depend on this domain. The price is that all three pins are clamped harder: output mode is capped at 2 MHz, and the current tops out at 3 mA (spelled out plainly in the electrical characteristics of datasheet DS5319). Our `MODE=10` (2 MHz) rides exactly at the cap: PC13 tops out at 2 MHz, so configuring 50 MHz amounts to writing into the void. Happily, for lighting a one-milliamp-class LED, 3 mA is enough for us.

## Cashing in the mode table

Now, when we look back at the combination table from piece 04, behind every row there is, in fact, a concrete circuit:

| Combination     | What happens in the circuit                                     |
| --------------- | --------------------------------------------------------------- |
| Analog input    | The Schmitt trigger is disconnected (saves power, blocks noise); the signal runs straight to the ADC |
| Floating input  | Only the Schmitt stands guard; the pin's level is left to chance |
| Pull-up/pull-down input | The 40 kΩ weak resistor gives the pin a default level, waiting for the outside world to flip it |
| Push-pull output | The two transistors take turns driving actively; low impedance in both directions |
| Open-drain output | Only the lower transistor remains; the high level is outsourced to the external pull-up |
| Alternate function | The output driver's steering wheel is handed over to an on-chip peripheral (USART/SPI/TIM) |

That faceplant from piece 04 now has its circuit-level explanation. The configuration left in floating input means **the output driver never reported for duty**: the two MOS transistors play deaf to ODR's commands, and however diligently we write BSRR, the pin simply will not budge. Even if ODR does take the write, there is no output path for it to travel (in piece 04's Renode, BSRR accepting writes while the ODR reading stayed frozen was exactly what this circuit layer being off duty looks like). The software view says "the configuration did not take effect"; the circuit view sees: the circuit responsible for execution was never connected in the first place.

And from the next piece on, we start climbing the staircase of abstraction: how C macros wrap up today's addresses and bits — and what hidden wounds the wrapping leaves behind. Climb those stairs with this circuit layer under your feet, and everything you look at is transparent.

## Try it yourself

1. Take the Blue Pill schematic (search the web for "Blue Pill schematic"), find the PC13 leg, confirm the LED's direction and the current-limiting resistor's value, and compute the lighting current.
2. Change `register_led`'s CRH configuration to open-drain (`CNF=01`: `(0x6u << 20)`), load it into Renode, see whether the ODR still toggles as before, and then think about what the LED would do on a real board.
3. Look up the GPIO electrical-characteristics tables in datasheet DS5319, find the original wording of the current limits for ordinary pins and for PC13-15, and check this piece's 25 mA/3 mA against it.
4. Think over one more question: if the LED were rewired from "sink-current" to "source-current" (LED flowing from the pin to ground), which line of the code would have to change?

## Hey, hey! Self-check before you go

- Say it out: in "push-pull", which MOS transistor does the push correspond to, which the pull — and which way does the current flow in each?
- Why can an open-drain output "not output a high level"? Can you spell out the circuit meaning of that sentence?
- Back to the Schmitt trigger: what does its hysteresis band guard against? Without it, what would a slow-edged signal do?
- Look again at the Blue Pill: why does the LED light on a low level? Whose current does the current-limiting resistor limit?
- Which design decision do PC13's 3 mA/2 MHz limits come from? And why is our `MODE=10` choice exactly right?

<ReferenceCard title="References">
  <ReferenceItem
    :id="1"
    author="STMicroelectronics"
    title="RM0008 Reference Manual — STM32F101/102/103/105/107"
    :year="2021"
    url="https://www.st.com/resource/en/reference_manual/rm0008-stm32f101xx-stm32f102xx-stm32f103xx-stm32f105xx-and-stm32f107xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf"
    chapter="9.1 GPIO functional description; 9.1.2-9.1.8 pin circuits and configurations"
  />
  <ReferenceItem
    :id="2"
    author="STMicroelectronics"
    title="STM32F103x8/xB Datasheet (DS5319)"
    :year="2015"
    url="https://www.st.com/resource/en/datasheet/stm32f103c8.pdf"
    chapter="5.3.2 I/O static characteristics; 5.3.22 PC13-PC15 electrical characteristics"
  />
  <ReferenceItem
    :id="3"
    author="STMicroelectronics"
    title="AN1043 — GPIO drive and LED driving on STM32"
    :year="2025"
    url="https://www.st.com/resource/en/application_note/an1043-gpio-drive-and-led-driving-on-stm32-mcus-stmicroelectronics.pdf"
    chapter="Push-pull vs open-drain output driving"
  />
</ReferenceCard>
