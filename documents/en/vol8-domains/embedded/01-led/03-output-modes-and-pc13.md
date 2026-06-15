---
chapter: 15
difficulty: beginner
order: 3
platform: stm32f1
reading_time_minutes: 24
tags:
- beginner
- cpp-modern
- stm32f1
title: 'Part 8: Push-Pull, Open-Drain, and PC13 — The Hardware Secrets Behind Lighting
  an LED'
translation:
  engine: anthropic
  source: documents/vol8-domains/embedded/01-led/03-output-modes-and-pc13.md
  source_hash: dd1b84c08aff841749581b7323b6c86db93d07c3534e355731acb7c188617f5a
  token_count: 2602
  translated_at: '2026-05-26T12:05:31.367982+00:00'
description: ''
---
# Part 8: Push-Pull, Open-Drain, and PC13 — The Hardware Secrets Behind Lighting an LED

> In the previous part, we dissected the four GPIO modes inside and out, and made the P-MOS and N-MOS in the internal structure diagram crystal clear. But we left a few key questions unanswered: what exactly is the difference between push-pull and open-drain output? Why should we choose push-pull for LED control? And what about the on-board LED on the Blue Pill board—why does it light up at a low level? The answers to these questions lie hidden in the hardware circuitry. Without understanding them, even the most elegant code is just a house of cards. In this part, we will unravel these hardware secrets one by one.

---

## Preface: From Modes to Choices

At the end of the previous part, we mentioned that GPIO has four basic input modes—floating, pull-up, pull-down, and analog—plus push-pull and open-drain for output, making eight configurations in total. The layout of the two MOS transistors, one on top and one on the bottom in that structure diagram, should still be fresh in your mind. But at the time, we merely "knew" these modes existed; we didn't dive into a very practical question: when you actually need to drive an LED, should you choose push-pull or open-drain?

This question seems deceptively simple—an LED, right? High level turns it on, low level turns it off, so push-pull is fine. But if that's what you think, you've fallen into two traps. The first trap is that the LED on the Blue Pill board is active-low—the intuition that "high level means on" is exactly backwards here. The second trap is that if you accidentally select open-drain mode, the LED might not light up at all or be so dim it's practically invisible. You'd think your code was wrong, spend ages debugging, and only then realize you chose the wrong output mode.

Even more subtle is the PC13 pin itself. It's the GPIO connected to the on-board LED on the Blue Pill, but this pin has a host of special limitations in the STM32F103C8T6's internal design—pull-up and pull-down resistors are unavailable, its drive capability is limited, and its speed is restricted. If you don't understand these limitations, you might pass in parameters that are "logically correct but hardware-ineffective" when configuring GPIO, and then stare at an unlit LED in existential despair.

So what we need to do now is thoroughly understand the internal circuits of push-pull and open-drain output, grasp PC13's special limitations, and lay out the Blue Pill's LED schematic for analysis. Only when you fully understand these hardware principles will every line of GPIO configuration code you write be backed by confidence.

---

## Push-Pull Output — The Default Choice for LEDs

Let's first draw out the internal circuit of push-pull output. Each GPIO pin on the STM32F103 in output mode has two MOSFETs (Metal-Oxide-Semiconductor Field-Effect Transistors) internally—a P-MOS on top and an N-MOS on the bottom—forming what's known as a "totem pole" structure:

```text
          VDD (3.3V)
           |
        [P-MOS]  ← 上管（High-Side）
           |
           +──────────── 输出引脚 Pin
           |
        [N-MOS]  ← 下管（Low-Side）
           |
          VSS (GND)
```

The working principle of this circuit is actually quite intuitive. When the output data register (ODR) is written with 1, the control logic turns on the P-MOS and turns off the N-MOS. Once the P-MOS conducts, a low-impedance path forms between VDD and the output pin, and the pin voltage is "pushed" close to VDD's 3.3V—this is a high-level output. Conversely, when the ODR is written with 0, the P-MOS turns off and the N-MOS turns on, forming a low-impedance path between the output pin and VSS, and the pin voltage is "pulled" close to 0V—this is a low-level output.

You'll notice that whether outputting high or low, one MOS transistor is always in a conducting state, providing a low-impedance drive path between VDD or VSS and the output pin. This is where the name "push-pull" comes from—"Push" is the P-MOS pushing current toward the load, with the direction flowing from VDD through the pin to the outside; "Pull" is the N-MOS pulling current back from the load, with the direction flowing from the outside through the pin to VSS. The two transistors work alternately, like the two ends of a seesaw, always actively driving the pin's logic level.

This bidirectional active drive brings two key advantages. The first is strong drive capability—because the on-resistance of a MOS transistor when conducting is very small (typically on the order of tens of ohms), push-pull output can source or sink a considerable amount of current. The GPIO on the STM32F103 in push-pull mode can source or sink up to 25mA (though this is an absolute maximum rating; in practice, you need to leave margin). For loads like LEDs that need a few to a little over ten milliamps of current, push-pull output is more than sufficient.

The second is fast switching speed. A MOS transistor takes only a very short time to go from fully off to fully on, and because the two transistors drive alternately, both the rising and falling edges of the output signal are steep. This is crucial for high-frequency signals (like SPI clocks or UART baud rates), because if the edges are too slow, the signal spends too much time "lingering" between high and low levels, and the receiver might misinterpret the logic level.

Now let's look back at our code. In `device/led.hpp` (lines 13–15), the LED's constructor is written like this:

```cpp
LED() {
    Base::setup(Base::Mode::OutputPP, Base::PullPush::NoPull, Base::Speed::Low);
}
```

The `Mode::OutputPP` here is telling the HAL library: "I want to configure this pin in push-pull output mode." Looking back at `device/gpio/gpio.hpp` (line 25), this enum value corresponds to HAL's `GPIO_MODE_OUTPUT_PP` constant. After receiving this configuration, the HAL library manipulates the GPIOx_CRH or GPIOx_CRL register, setting the corresponding bits to `00` (general-purpose push-pull output mode, maximum speed 10MHz—this is the value corresponding to Speed::Low).

Why must we choose push-pull for LED control? Because an LED needs the pin to output a definite high or low level to control its on/off state. Push-pull output is actively driven in both directions—when outputting high, the P-MOS pulls the pin to 3.3V; when outputting low, the N-MOS pulls the pin to 0V. The voltage on the pin is definite and controllable, the voltage difference across the LED is definite, and the current path is clear. If you chose open-drain output (covered next), the situation would be completely different.

---

## Open-Drain Output — An Alternative Choice

The internal circuit of open-drain output has one key difference from push-pull: the upper P-MOS transistor is disconnected, leaving only the lower N-MOS transistor:

```text
          VDD (3.3V)
           |
        [外部上拉电阻]  ← 必须由外部电路提供！
           |
           +──────────── 输出引脚 Pin
           |
        [N-MOS]  ← 只有下管在工作
           |
          VSS (GND)
```

Note the annotation in the diagram that says "must be provided by external circuitry"—this is the key to understanding open-drain output. In open-drain mode, the chip's internal P-MOS does not participate, and there is no direct drive path between the pin and VDD. This means that when you make the pin output a "high level," the chip's entire action is simply to turn off the N-MOS—and then the pin floats (in a high-impedance state), neither pulled toward VDD nor toward VSS, just hovering there with an indeterminate voltage.

To make the pin actually become a high level, you need to add an external pull-up resistor connecting the pin to VDD. When the N-MOS is off, the pull-up resistor slowly pulls the pin toward VDD; when the N-MOS is on, the pin is directly pulled to VSS, and current flows from VDD through the pull-up resistor into the N-MOS to ground. The resistance value of the pull-up resistor determines the speed of the rising edge and the static power consumption—if the resistor is too small, the current when the N-MOS conducts is too large, leading to high power consumption; if the resistor is too large, the rising edge is too slow, degrading signal quality. This is a parameter that needs to be weighed based on the application scenario.

What happens if you use open-drain mode to drive an LED? It depends on the external circuit design. Suppose your LED uses the classic "pin to series resistor to VDD" wiring (active-high). Then when the N-MOS is off (outputting "high level"), the pin floats. Without an external pull-up resistor, the LED's anode might not reach sufficient voltage for forward conduction. The result is that the LED either doesn't light up at all or is extremely dim, depending on the actual voltage when the pin floats. And when you output a low level, the N-MOS conducts, the pin is pulled close to 0V, and the voltage difference across the LED is actually at its maximum—this is completely reversed behavior compared to push-pull mode.

> ⚠️ **Pitfall Warning**: If you mistakenly choose open-drain mode to drive an LED, the LED might not light up at all or be extremely dim. This is because when open-drain output "outputs high," it actually just lets the pin float—it doesn't actively drive it to 3.3V. For LED control that requires a definite logic level, push-pull is the correct choice. This error is particularly hard to spot during debugging because your code logic is perfectly correct—the `HAL_GPIO_WritePin()` call is fine, the timing is right—but the LED just won't light up. You'll spend a lot of time checking wiring, clock configuration, and HAL initialization, only to finally discover that the Mode was chosen incorrectly.

So what is open-drain output actually good for? Its value shows in a few specific scenarios. The first is the I2C bus. The I2C protocol requires multiple devices to share the same data line (SDA) and clock line (SCL). Any device can pull the line low, but none can actively pull it high—the high level of the line is provided by a shared pull-up resistor on the bus. Open-drain output perfectly matches this need: when outputting 0, the N-MOS conducts and pulls the line low; when outputting 1, the N-MOS turns off and lets the line return to a high level through the pull-up resistor. If one device pushed a high level with push-pull while another device simultaneously tried to pull the line low, it would cause a short circuit that could burn out the chip.

The second scenario is "wired-AND" logic. Multiple open-drain outputs are connected together, sharing a single pull-up resistor. As long as any one of them outputs a low level (N-MOS conducts), the entire line is low. This characteristic is very useful in multi-master buses and shared interrupt lines. The third scenario is level shifting—if your STM32 operates at 3.3V but needs to communicate with a 5V system, an open-drain output with a pull-up resistor to 5V can achieve 3.3V to 5V level shifting (provided the pin is 5V tolerant, which most pins on the STM32F103 are).

Once you understand the essential difference between push-pull and open-drain, you know why LED control must use push-pull. An LED needs the pin to output a definite high/low level, needs sufficient drive current, doesn't need wired-AND logic, and doesn't need level shifting. Push-pull output actively drives in both directions, making it the simplest and most reliable choice.

---

## Pull-Up and Pull-Down Resistors — Why Choose NoPull Under Push-Pull

In addition to the two MOS transistors used for output drive, GPIO pins internally have software-configurable pull-up and pull-down resistors. In `device/gpio/gpio.hpp` (lines 39–43), we defined three options:

```cpp
enum class PullPush : uint32_t {
    NoPull = GPIO_NOPULL,
    PullUp = GPIO_PULLUP,
    PullDown = GPIO_PULLDOWN,
};
```

The meaning of these three configurations needs to be explained from the perspective of a pin's behavior when not externally driven.

When configured as `NoPull` (no pull-up or pull-down), the pin is in a "floating" state. If you configure a GPIO pin that isn't connected to any external circuit as an input mode and select NoPull, then measure its voltage with a multimeter, you'll find the reading jumping around an indeterminate value—it might be affected by electromagnetic interference from the surrounding environment, or changed by electrostatic coupling when your finger gets close. This is the so-called "floating" state, where the pin's logic level is indeterminate.

But this isn't a problem for output mode. Because in push-pull output mode, the pin is always actively driven by either the P-MOS or the N-MOS—either pulled to VDD or pulled to VSS. Pull-up and pull-down resistors are essentially redundant in output mode, because the drive capability of the MOS transistors is far greater than that of the internal pull-up/pull-down resistors (the typical value of internal pull-up/pull-down resistors is about 40KΩ, while the equivalent resistance of a MOS transistor when conducting is only a few tens of ohms—a difference of three orders of magnitude).

The `PullUp` (pull-up) configuration connects an internal resistor of about 40KΩ between the pin and VDD. When the pin isn't driven by an external signal, this resistor pulls the pin's level to a high state. The most common application scenario is button input: one end of the button is connected to the GPIO pin, and the other end is grounded. When the button is not pressed, the internal pull-up resistor holds the pin at VDD (high level); when the button is pressed, the pin is directly grounded and becomes low level. This way, you can detect a button press by checking for a falling edge on the pin's logic level.

`PullDown` (pull-down) does the reverse, connecting a resistor of about 40KΩ between the pin and VSS, making a floating pin default to a low level. This suits scenarios where the other end of the button is connected to VDD—the pin is low when the button is not pressed, and goes high when pressed.

Returning to our LED code, what's passed into the constructor is `PullPush::NoPull`. The reason is simple: the LED pin is configured in push-pull output mode, and the P-MOS and N-MOS are already actively driving the pin's level. The internal pull-up and pull-down resistors are completely ornamental here. Whether you add them or not, the pin's output behavior won't change at all. So choosing NoPull is the cleanest option—no superfluous configuration, reducing unnecessary static power consumption (even though this power consumption is negligible).

But there's a deeper reason here, related to PC13, which we'll discuss next. Keep this conclusion in mind for now; you'll soon understand why NoPull isn't just the "cleanest choice," but the only reasonable choice on PC13.

---

## PC13's Special Limitations — A Pin With an Attitude

At this point, we need to focus our discussion on the specific PC13 pin on the Blue Pill board. If you've flipped through the STM32F103C8T6 datasheet (Reference Manual RM0008), you'll find an unassuming but critically important note in the GPIO chapter, which essentially says that PC13, PC14, and PC15 are powered differently from other GPIOs—they are powered by the chip's internal Backup Domain, not by the regular VDD.

There's a clear functional rationale behind this design decision. PC13 can be used as the RTC (Real-Time Clock) calibration output or tamper detection output; PC14 and PC15 can be used as the LSE (Low Speed External) crystal oscillator pins OSC32_IN and OSC32_OUT. These functions are all related to the RTC and backup registers, belonging to the chip's "Backup Domain" section, which needs to continue working from a VBAT battery even after the main VDD power is cut. So when ST designed the chip, they assigned the power supply for these three pins to the backup domain.

This brings a direct consequence: the drive capability of these three pins is strictly limited. The datasheet explicitly states that PC13 in output mode has a maximum current of only 3mA (not the 25mA of regular GPIOs), and it can only work at the lowest speed grade (2MHz). PC14 and PC15 have even stricter limitations—their output speed cannot exceed 2MHz, and they can only drive very small capacitive loads. If you use them as regular GPIOs to drive high-current loads, you could damage the chip's internal backup domain power supply circuitry.

Even more critical is the issue of pull-ups and pull-downs. Because PC13/14/15 are powered from the backup domain, while the internal pull-up/pull-down resistors are connected to the main VDD domain, these two power domains cannot be directly connected at will. So in ST's design, the internal pull-up and pull-down resistors for these three pins either don't exist or have limited functionality. Specifically, on the STM32F103, when PC13 is configured as a general-purpose GPIO output mode, the internal pull-up and pull-down functionality is **unavailable**—the pull-up/pull-down configuration bits you write to the CRH register are ignored by the hardware.

This means that in our LED code, `PullPush::NoPull` isn't just a "clean choice"—it's the only valid option on PC13. If you pass in `PullUp` or `PullDown`, the HAL library will faithfully write the configuration to the register, but the hardware won't execute it. For the LED, this doesn't matter because push-pull output is already actively driving and doesn't need pull-ups or pull-downs. But if you later want to do input detection on PC13 (like reading a button state), you must use an external pull-up or pull-down resistor—the internal ones won't help you here.

> ⚠️ **Pitfall Warning**: If you plan to use an LED on other pins (like PA0 or PB0), you can enable pull-ups or pull-downs. But not on PC13/14/15. The template system in the code won't stop you from passing in the wrong configuration—the C++ compiler only checks types, not hardware compatibility. You can perfectly well write `Base::setup(Base::Mode::OutputPP, Base::PullPush::PullUp, Base::Speed::High)`, and it will compile without issues and flash without errors, but the PullUp configuration and high-speed setting on PC13 simply won't take effect. This is why understanding hardware principles matters—the compiler can help you check syntax errors, but it can't check "hardware semantic" errors.

There's another PC13-related limitation, and that's speed. We chose `Speed::Low` in our code, which is of course more than enough for an LED—a 1Hz blink frequency is well within the capability of any speed grade. But even if you wanted to choose high speed, it wouldn't matter; PC13's output speed ceiling is 2MHz, and configurations exceeding this limit are likewise ignored by the hardware. So `Speed::Low` is both a reasonable choice and the highest configuration actually usable on PC13 (`Speed::Low` corresponds to 2MHz on the F103, which perfectly matches PC13's limitation).

---

## The Blue Pill On-Board LED Circuit — Why It Lights Up at a Low Level

Now we arrive at the most critical part. We've been talking about GPIO output modes, pull-ups/pull-downs, and PC13's limitations. Now it's time to connect all this knowledge and analyze exactly how the LED connected to PC13 on the Blue Pill board works.

On the Blue Pill board's schematic, the connection between PC13 and the LED looks like this:

```text
VDD (3.3V)
  |
  [R 限流电阻，约1KΩ]
  |
  [LED 正极 ← 负极]
  |
  PC13 (GPIO引脚)
```

Notice this circuit: the LED's positive terminal (anode) is connected to VDD (3.3V) through a current-limiting resistor, and the LED's negative terminal (cathode) is connected directly to the PC13 pin. This is exactly the opposite of the intuitive "pin outputs high level → LED turns on" wiring. In the typical wiring, the pin connects to the anode and the cathode goes to ground, so current flows from the pin through the LED to ground when outputting high. But the Blue Pill's wiring has VDD connected to the anode and the pin connected to the cathode, forming a "sink current" drive method.

Let's analyze the current path in both states:

When PC13 outputs a **low level** (0V): VDD (3.3V) → current-limiting resistor → LED anode → LED cathode → PC13 (0V). There's approximately a 3.3V voltage difference between VDD and PC13. Subtracting the LED's forward voltage drop (about 1.8–2.2V for a red LED), the remaining voltage falls across the current-limiting resistor. Assuming a 2V LED drop, the voltage across the current-limiting resistor is about 1.3V, and the current flowing through the LED is about 1.3V / 1KΩ = 1.3mA. This current is enough to make the LED emit visible light. So the LED lights up at a low level.

When PC13 outputs a **high level** (3.3V): VDD (3.3V) → current-limiting resistor → LED anode → LED cathode → PC13 (3.3V). There's almost no voltage difference between VDD and PC13 (both are at 3.3V), so no current flows through the LED. So the LED turns off at a high level.

This is what's called "active low"—the LED is lit when the pin outputs a low level. This design is very common on embedded development boards for a few reasons: first, sink current (current flowing into the pin) typically has slightly stronger drive capability than source current (current flowing out of the pin); second, many MCUs default to a high or high-impedance state at power-up, and using active-low avoids the LED flashing momentarily during power-up. But for beginners, this "counter-intuitive" design is often the most confusing part.

Once you understand this circuit, looking back at the `ActiveLevel` enum and the `on()` method in our code becomes completely clear. In `device/led.hpp` (line 6 and lines 17–20):

```cpp
enum class ActiveLevel { Low, High };

// ...

void on() const {
    Base::set_gpio_pin_state(
        LEVEL == ActiveLevel::Low ? Base::State::UnSet : Base::State::Set);
}
```

`ActiveLevel::Low` means "low level is the active level," i.e., the LED lights up at a low level. So when `LEVEL` is `ActiveLevel::Low`, the `on()` method outputs `Base::State::UnSet`—which is a low level (GPIO_PIN_RESET). The `off()` method does the reverse, outputting `Base::State::Set` (high level, GPIO_PIN_SET).

Then in `main.cpp` (line 11), when we instantiate the LED:

```cpp
device::LED<device::gpio::GpioPort::C, GPIO_PIN_13> led;
```

Note that the third template parameter `ActiveLevel` isn't explicitly specified here; its default value is `ActiveLevel::Low` (see the template declaration in `device/led.hpp` line 8: `ActiveLevel LEVEL = ActiveLevel::Low`). This happens to match the active-low characteristic of the PC13 LED on the Blue Pill board. If your LED is wired as "pin → resistor → LED → ground" (active-high), you just need to change the template parameter:

```cpp
device::LED<device::gpio::GpioPort::A, GPIO_PIN_0, device::ActiveLevel::High> led;
```

This way, `on()` will output a high level to light up the LED. The template system abstracts hardware differences into compile-time parameters. You don't need to change any logic code; you just tell the template "this LED is active-high or active-low" and that's it.

---

## Speed Settings — It's Slew Rate, Not Frequency

Finally, there's one easily misunderstood configuration item that needs explaining—the GPIO speed setting. Three speed grades are defined in `device/gpio/gpio.hpp` (lines 45–49):

```cpp
enum class Speed : uint32_t {
    Low = GPIO_SPEED_FREQ_LOW,
    Medium = GPIO_SPEED_FREQ_MEDIUM,
    High = GPIO_SPEED_FREQ_HIGH,
};
```

These three names can be misleading—"speed" sounds like it refers to how fast a pin can toggle between high and low levels. But in reality, the GPIO speed setting controls the **slew rate** of the output signal—that is, how steep the edges are when the voltage jumps from low to high (or vice versa).

A high slew rate means the voltage rises/falls quickly, with steep edges; a low slew rate means the voltage rises/falls slowly, with gentle edges. This has no direct relationship to the pin's toggle frequency—you can toggle a pin at a very high frequency with a low-speed setting; it's just that each toggle's edges won't be as steep.

So why do we need to control the slew rate? The main reason is EMI (Electromagnetic Interference). The steeper the signal edges, the more high-frequency harmonic components are contained, and the stronger the electromagnetic interference radiated outward. On high-speed signal lines (like SPI clock lines or USB data lines), you need steep edges to ensure signal integrity, so you choose high speed. But for low-speed scenarios like an LED, steep edges provide no benefit and instead add unnecessary EMI and power consumption. So choosing low speed is the most reasonable approach.

On the STM32F103, the actual slew rates corresponding to the three speed settings are roughly: Low corresponds to a 2MHz bandwidth, Medium to 10MHz, and High to 50MHz. The "bandwidth" here refers to how fast the output signal can change in terms of slew rate, not that the pin can only toggle at 2MHz—the actual toggle frequency depends on your software loop speed.

For an LED blinking at 1Hz, any speed setting produces exactly the same result—the human eye simply cannot distinguish between a voltage edge of 1 microsecond and one of 10 nanoseconds. Choosing `Speed::Low` both reduces EMI and complies with PC13 pin's own 2MHz speed limit, making it the most reasonable choice.

If you later work with SPI communication (where the clock frequency might be as high as 18MHz or 36MHz), you'll need to use Medium or High to ensure the SCK signal's edges are steep enough, otherwise the slave device might not be able to sample the data correctly. But in the LED scenario, low speed is plenty—don't waste bandwidth you don't need.

---

## Wrapping Up: Closing the Loop from Hardware Principles to Code Logic

At this point, the hardware principles behind lighting an LED are finally fully closed-loop. We started from the P-MOS/N-MOS dual-transistor structure of push-pull output, covered the single-transistor limitation of open-drain output, explained the principles of pull-up/pull-down resistors and PC13's backup domain limitations, and analyzed the Blue Pill's sink-current LED circuit to illuminate the design intent behind the `ActiveLevel` enum in our code. Now when you look back at the short thirty lines of `device/led.hpp`, every line has a clear hardware basis—`Mode::OutputPP` corresponds to push-pull dual-transistor drive, `PullPush::NoPull` corresponds to PC13's unavailable pull-ups/pull-downs (and the fact that push-pull doesn't need them anyway), `Speed::Low` corresponds to PC13's 2MHz ceiling and the LED's low-speed requirements, and `ActiveLevel::Low` corresponds to the Blue Pill's active-low circuit.

Once you understand all this, your development workflow is no longer mindless copy-pasting. When you need to connect an LED, a button, or an I2C device on another pin, you'll know which output mode to choose, whether you need pull-ups/pull-downs, and what speed to set. This is the judgment that hardware principles give you, not just "that's what the tutorial says."

In the next part, we enter the world of the HAL library. Up to now, we've been using our own template class to wrap GPIO operations, but what exactly do the underlying `HAL_GPIO_Init()` and `HAL_GPIO_WritePin()` do? How do they convert our configuration parameters into register operations? And what about that `GPIOClock::enable_target_clock()`—why does GPIO need its clock enabled before it can work? Before answering these questions, we need to first understand the STM32's clock tree—a large diagram that makes countless beginners tremble. But don't worry, we'll take it step by step, starting with getting clock enabling straightened out—without enabling the clock, GPIO is just a lump of dead silicon.
