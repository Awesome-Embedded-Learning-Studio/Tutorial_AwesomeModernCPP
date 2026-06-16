---
chapter: 15
difficulty: beginner
order: 3
platform: stm32f1
reading_time_minutes: 23
tags:
- beginner
- cpp-modern
- stm32f1
title: 'Part 8: Push-Pull, Open-Drain, and PC13 — The Hardware Secrets of Lighting
  an LED'
description: ''
translation:
  source: documents/vol8-domains/embedded/01-led/03-output-modes-and-pc13.md
  source_hash: 040329c62073d8f80cf00d56bae0d428405bad3dd49569ac59d8ad433cc11c57
  translated_at: '2026-06-16T04:09:33.719890+00:00'
  engine: anthropic
  token_count: 2608
---
# Part 8: Push-Pull, Open-Drain, and PC13 — The Hardware Secrets of Lighting an LED

> In the previous part, we turned the four GPIO modes inside out, illustrating the P-MOS and N-MOS in the internal structure diagram. But we left a few key questions unexpanded: What is the real difference between push-pull output and open-drain output? Why should we choose push-pull for LED control? And why is the onboard LED on the Blue Pill board lit by a low level? The answers to these questions are hidden in the hardware circuit. If you don't figure this out, no matter how beautiful the code is, it's just a castle in the air. In this part, we will dismantle these hardware secrets one by one.

---

## Preface: From Modes to Choices

At the end of the last part, we mentioned that GPIO has four basic modes—input floating, input pull-up, input pull-down, analog input—plus push-pull and open-drain in the output direction, totaling eight configurations. The layout of the two MOS tubes, one up and one down, in that structure diagram should still be in your mind. But at that time, we just "knew" about the existence of these modes, and hadn't deeply discussed a very practical question: When you really need to drive an LED, should you choose push-pull or open-drain?

This question looks deceptively simple—LED嘛, output high level on, low level off, just use push-pull. But if you really think so, you've fallen into two pits. The first pit is that the LED on the Blue Pill board is low-level active, the intuition of "high level on" is exactly the opposite here. The second pit is that if you slip up and choose open-drain mode, the LED might not light up at all or be so dim it's invisible, and you'll think the code is wrong, debugging for half a day only to find the output mode was wrong.

Even more subtle is the PC13 pin. It is the GPIO connected to the onboard LED on the Blue Pill, but this pin has a bunch of special limitations in the internal design of the STM32F103C8T6—pull-up/pull-down resistors are not available, drive capability is limited, and speed is also limited. If you don't understand these limitations, when configuring the GPIO, you might pass in some "logically correct but hardware-invalid" parameters, and then stare at an unlit LED doubting life.

So what we need to do now is to clarify the internal circuits of push-pull and open-drain outputs, understand the special restrictions of PC13, and spread out the LED circuit diagram on the Blue Pill board for analysis. Only when you thoroughly understand these hardware principles will every line of your GPIO configuration code be solid.

---

## Push-Pull Output — The Default Choice for LEDs

Let's first draw the internal circuit of push-pull output. Each GPIO pin of the STM32F103 in output mode has two MOSFETs (Metal-Oxide-Semiconductor Field-Effect Transistors) inside, a P-MOS on top and an N-MOS on the bottom, forming a so-called "totem pole" structure:

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

The working principle of this circuit is actually quite intuitive. When the output data register (ODR) writes 1, the control logic turns on the P-MOS and turns off the N-MOS. After the P-MOS turns on, a low-impedance path is formed between VDD and the output pin, and the pin voltage is "pushed" to near VDD's 3.3V—this is outputting a high level. Conversely, when ODR writes 0, P-MOS turns off and N-MOS turns on, a low-impedance path is formed between the output pin and VSS, and the pin voltage is "pulled" to near 0V—this is outputting a low level.

You will find that whether outputting high or low, one MOS tube is always in a conducting state, providing a low-impedance drive path between VDD or VSS and the output pin. This is the source of the name "push-pull"—"Push" is the P-MOS pushing current to the load, direction from VDD through the pin to the outside; "Pull" is the N-MOS pulling current back from the load, direction from the outside through the pin to VSS. The two tubes work alternately, like the two ends of a seesaw, always actively driving the pin level.

This bidirectional active drive brings two key advantages. First is strong drive capability—because the on-resistance of the MOS tube when conducting is very small (typical value is on the order of tens of ohms), push-pull output can provide or sink considerable current. STM32F103's GPIO in push-pull mode can output or sink up to 25mA of current (of course this is the absolute maximum value, in actual use leave some margin). For a load like an LED that needs a few milliamps to ten-plus milliamps of current, push-pull output is more than sufficient.

Second is fast switching speed. The MOS tube takes only a very short time from off to fully on, and because the two tubes drive alternately, both the rising and falling edges of the output signal are very steep. This is crucial for high-frequency signals (like SPI clocks, UART baud rates), because if the edges are too slow, the signal "lingers" between high and low levels for too long, and the receiver might misjudge the logic level.

Now looking back at our code. In `device/led.hpp` (lines 13-15), the LED's constructor is written like this:

```cpp
LED() {
    Base::setup(Base::Mode::OutputPP, Base::PullPush::NoPull, Base::Speed::Low);
}
```

The `Mode::OutputPP` here is telling the HAL library: "I want to configure this pin as push-pull output mode". Looking back at `device/gpio/gpio.hpp` (line 25), this enum value corresponds to the HAL's `GPIO_MODE_OUTPUT_PP` constant. After receiving this configuration, the HAL library will go operate on the GPIOx_CRH or GPIOx_CRL registers, setting the corresponding bits to `00` (General-purpose push-pull output mode, max speed 10MHz—this is the value corresponding to Speed::Low).

Why must LED control choose push-pull? Because the LED needs the pin to output a definite high or low level to control on/off. Push-pull output is actively driven in both directions—when outputting high, P-MOS pulls the pin to 3.3V; when outputting low, N-MOS pulls the pin to 0V. The voltage on the pin is definite and controllable, the voltage difference across the LED is definite, and the current path is clear. If you choose open-drain output (we'll talk about it right below), the situation is completely different.

---

## Open-Drain Output — Another Choice

The internal circuit of open-drain output has one key difference from push-pull: the upper P-MOS tube is disconnected, leaving only the lower N-MOS tube:

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

Note the "Must be provided by external circuit" marked in the diagram—this is the key to understanding open-drain output. In open-drain mode, the internal P-MOS of the chip does not participate in the work, and there is no direct drive path between the pin and VDD. This means that when you make the pin output a "high level", all the chip does is turn off the N-MOS—and then the pin is floating (High-Impedance state), neither pulled towards VDD nor pulled towards VSS, it just floats there, voltage uncertain.

To make the pin truly become high level, you need to add a pull-up resistor externally to the chip, connecting the pin to VDD. When the N-MOS is off, the pull-up resistor slowly pulls the pin towards VDD; when the N-MOS is on, the pin is directly pulled to VSS, at which time current flows from VDD through the pull-up resistor into the N-MOS to ground. The value of the pull-up resistor determines the speed of the rising edge and static power consumption—if the resistance is too small, the current when N-MOS is on is too large, power consumption is high; if the resistance is too large, the rising edge is too slow, signal quality is poor. This is a parameter that needs to be weighed according to the application scenario.

What happens if you use open-drain mode to drive an LED? It depends on the design of the external circuit. Suppose your LED is connected in the classic "pin series resistor to VDD" way (high level on), then when the N-MOS is off (outputting "high level"), the pin floats, and if there is no external pull-up resistor, the anode of the LED may not reach enough voltage to conduct forward. The result is that the LED either doesn't light at all, or the brightness is extremely low, depending on the actual voltage when the pin is floating. And when you output a low level, the N-MOS turns on, the pin is pulled to near 0V, the voltage difference across the LED is instead the largest—this is completely opposite to the behavior in push-pull mode.

⚠️ **Pitfall Warning**: If you mistakenly choose open-drain mode to drive an LED, the LED might not light at all or be extremely dim. This is because open-drain output "high level" actually just lets the pin float, it doesn't actively drive to 3.3V. For LED control that needs definite levels, push-pull is the correct choice. This error is particularly hard to find during debugging, because your code logic is completely correct—`HAL_GPIO_WritePin()` calls are right, timing is right too—but the light just doesn't come on. You'll spend a lot of time checking wiring, checking clock configuration, checking HAL initialization, only to find the Mode was chosen wrong.

So what is open-drain output actually useful for? Its value is reflected in several specific scenarios. The first is the I2C bus. The I2C protocol requires multiple devices to share the same data line (SDA) and clock line (SCL), any device can pull the line low, but cannot actively pull the line high—the line's high level is provided by a unified pull-up resistor on the bus. Open-drain output perfectly matches this need: output 0 turns on N-MOS to pull the line low, output 1 turns off N-MOS to let the line return to high level through the pull-up resistor. If a device outputs high level with push-pull, and another device wants to pull the line low at the same time, it will cause a short circuit, possibly burning the chip.

The second scenario is "Wired-AND" logic. Multiple open-drain outputs are connected together, sharing one pull-up resistor, as long as any one outputs low level (N-MOS on), the whole line is low level. This characteristic is very useful in multi-master buses, interrupt shared lines. The third scenario is level shifting—if your STM32 works at 3.3V, but needs to communicate with a 5V system, open-drain output plus a pull-up resistor pulled up to 5V can achieve 3.3V to 5V level shifting (provided the pin is 5V tolerant, most pins of STM32F103 are).

After understanding the essential difference between push-pull and open-drain, you know why LED control must choose push-pull. LEDs need the pin to output definite high/low levels, need enough drive current, don't need wired-AND logic, and don't need level shifting. Push-pull output actively drives in both directions, it's the simplest and most reliable choice.

---

## Pull-Up and Pull-Down Resistors — Why Choose NoPull Under Push-Pull

Besides the two MOS tubes used for output drive, GPIO pins also have software-configurable pull-up and pull-down resistors inside. In `device/gpio/gpio.hpp` (lines 39-43), we defined three options:

```cpp
enum class PullPush : uint32_t {
    NoPull = GPIO_NOPULL,
    PullUp = GPIO_PULLUP,
    PullDown = GPIO_PULLDOWN,
};
```

The meaning of these three configurations needs to start from the behavior of the pin when there is no external drive.

When configured as `NoPull` (no pull-up/pull-down), the pin is in a "floating" state. If you configure a GPIO pin not connected to any external circuit as input mode and choose NoPull, then measure its voltage with a multimeter, you will find the reading jumps around an uncertain value—it might be affected by electromagnetic interference in the surrounding environment, or changed by electrostatic coupling when your finger approaches. This is the so-called "floating" state, pin level uncertain.

But this is not a problem for output mode. Because in push-pull output mode, the pin is always actively driven by P-MOS or N-MOS—either pulled to VDD, or pulled to VSS. Pull-up/pull-down resistors are basically redundant in output mode, because the drive capability of the MOS tube is far greater than the internal pull-up/pull-down resistors (the typical value of internal pull-up/pull-down resistors is about 40KΩ, while the equivalent resistance of the MOS tube when on is only a few tens of ohms, a difference of three orders of magnitude).

`PullUp` (pull-up) configuration will connect an internal resistor of about 40KΩ between the pin and VDD. When the pin is not driven by an external signal, this resistor will pull the pin level to high level. The most common application scenario is button input: one end of the button is connected to the GPIO pin, the other end is grounded. When the button is not pressed, the internal pull-up resistor maintains the pin at VDD (high level); when the button is pressed, the pin is directly grounded becoming low level. This way you can judge the button being pressed by detecting the falling edge of the pin level.

`PullDown` (pull-down) is the reverse, connecting a resistor of about 40KΩ between the pin and VSS, making the floating pin default to low level. Suitable for scenarios where the other end of the button is connected to VDD—when the button is not pressed the pin is low level, when pressed it becomes high level.

Returning to our LED code, what is passed in the constructor is `PullPush::NoPull`. The reason is simple: the LED pin is configured as push-pull output mode, P-MOS and N-MOS are already actively driving the pin level, the internal pull-up/pull-down resistors here are completely a decoration. Whether you add it or not, the output behavior of the pin won't have any change. So choosing NoPull is the cleanest choice—no redundant configuration, reducing unnecessary static power consumption (although this power consumption is negligible).

But there is a deeper reason here, related to PC13 which we will talk about next. Remember this conclusion first, in a bit you will understand why NoPull is not just the "cleanest choice", but the only reasonable choice on PC13.

---

## Special Restrictions of PC13 — A Pin with a Temper

Here we need to focus the topic on the specific pin PC13 on the Blue Pill board. If you have flipped through the STM32F103C8T6 datasheet (Reference Manual RM0008), you will find an unobtruse but extremely important note in the GPIO chapter, the gist is that the power supply of PC13, PC14, PC15 these three pins is different from other GPIOs, they are powered by the backup domain inside the chip, not by the normal VDD.

Behind this design decision lies clear functional consideration. PC13 can be used as RTC (Real-Time Clock) calibration output or Tamper Detection output; PC14 and PC15 can be used as LSE (Low Speed External) low-speed external crystal oscillator pins OSC32_IN and OSC32_OUT. These functions are all related to RTC and backup registers, belonging to the chip's "backup domain" part, needing to continue working from VBAT battery power after the main power VDD is cut off. So when ST designed the chip, it assigned the power supply of these three pins to the backup domain.

Doing this brings a direct consequence: the drive capability of these three pins is strictly limited. The datasheet clearly states that the maximum current of PC13 in output mode is only 3mA (not 25mA of normal GPIO), and can only work in the lowest speed grade (2MHz). The restrictions on PC14 and PC15 are even stricter—their output speed cannot exceed 2MHz, and they can only drive very small capacitive loads. If you use them as normal GPIOs, driving large current loads might damage the backup domain power supply circuit inside the chip.

Even more critical is the issue of pull-up/pull-down. Because PC13/14/15 are powered from the backup domain, while the internal pull-up/pull-down resistors are connected to the main VDD domain, these two power domains cannot be directly connected at will. So when ST designed it, the internal pull-up/pull-down resistors of these three pins either don't exist or have limited functionality. Specifically, on the STM32F103, when PC13 is configured as general-purpose GPIO output mode, the internal pull-up/pull-down function is **unavailable**—the pull-up/pull-down configuration bits you write into the CRH register will be ignored by hardware.

This means that in our LED code, `PullPush::NoPull` is not just a "clean choice"—it is the only valid option on PC13. You pass in `PullUp` or `PullDown`, the HAL library will faithfully write the configuration into the register, but the hardware will not execute it. For the LED this doesn't matter, because push-pull output itself is actively driving, not needing pull-up/pull-down. But if later you want to do input detection on PC13 (like using it to read a button's state), you must add external pull-up or pull-down resistors—the internal set here can't help you.

⚠️ **Pitfall Warning**: If you plan to use an LED on other pins (like PA0 or PB0), it is possible to enable pull-up/pull-down. But PC13/14/15 cannot. The template system in the code won't stop you from passing wrong configurations—the C++ compiler only checks types, not hardware compatibility. You can completely write `Base::setup(Base::Mode::OutputPP, Base::PullPush::PullUp, Base::Speed::High)`, compilation passes fine, flashing won't report errors, but the PullUp configuration and high speed settings on PC13 won't take effect. This is why understanding hardware principles is important—the compiler can help you check syntax errors, but not "hardware semantic" errors.

There is also a restriction related to PC13 which is speed. We chose `Speed::Low` in the code, which is of course enough for the LED—1Hz flash frequency, any speed grade can handle it. But even if you wanted to choose high speed it's useless, PC13's output speed ceiling is 2MHz, configurations exceeding this limit will also be ignored by hardware. So `Speed::Low` is both a reasonable choice and the highest configuration actually usable on PC13 (`Speed::Low` corresponds to 2MHz on F103, exactly matching PC13's limit).

---

## Blue Pill Onboard LED Circuit — Why Low Level Lights

Now we come to the most critical part. Previously we were always talking about GPIO output modes, pull-up/pull-down, PC13 restrictions, now it's time to string this knowledge together and analyze how the LED connected to PC13 on the Blue Pill board actually works.

On the Blue Pill board's schematic, the connection between PC13 and the LED is like this:

```text
VDD (3.3V)
  |
  [R 限流电阻，约1KΩ]
  |
  [LED 正极 ← 负极]
  |
  PC13 (GPIO引脚)
```

Notice this circuit: the LED's positive pole (anode) is connected to VDD (3.3V) through a current-limiting resistor, and the LED's negative pole (cathode) is directly connected to the PC13 pin. This is exactly opposite to our usual intuition of "pin outputs high level → LED lights". The usual connection is pin to anode, cathode to ground, outputting high level has current flowing from pin to LED to ground. The Blue Pill's connection is VDD to anode, pin to cathode, forming a "Sink Current" drive way.

Let's analyze the current path in two states:

When PC13 outputs **low level** (0V): VDD (3.3V) → current-limiting resistor → LED positive → LED negative → PC13 (0V). There is about 3.3V voltage difference between VDD and PC13, minus the LED's forward conduction drop (red LED is about 1.8-2.2V), the remaining voltage falls on the current-limiting resistor. Assuming LED drop is 2V, then the voltage on the current-limiting resistor is about 1.3V, the current flowing through the LED is about 1.3V/1KΩ = 1.3mA. This current is enough to make the LED emit visible light. So low level lights the LED.

When PC13 outputs **high level** (3.3V): VDD (3.3V) → current-limiting resistor → LED positive → LED negative → PC13 (3.3V). There is almost no voltage difference between VDD and PC13 (both are 3.3V), no current flows through the LED. So high level turns the LED off.

This is so-called "Active Low"—LED lights when the pin outputs low level. This design is very common on embedded development boards, reasons include: one is sink current (current flowing into the pin) usually has slightly stronger drive capability than source current (current flowing out of the pin); two is many MCUs' power-on default state is pin high level or high-impedance, using active low can avoid LED flashing at the moment of power-on. But for beginners, this "counter-intuitive" design is often the most confusing place.

After understanding this circuit, looking back at the `ActiveLevel` enum and `on()` method in our code is completely suddenly clear. In `device/led.hpp` (line 6 and lines 17-20):

```cpp
enum class ActiveLevel { Low, High };

// ...

void on() const {
    Base::set_gpio_pin_state(
        LEVEL == ActiveLevel::Low ? Base::State::UnSet : Base::State::Set);
}
```

`ActiveLevel::Low` means "low level is the active level", that is, the LED lights when low level. So when `LEVEL` is `ActiveLevel::Low`, the `on()` method outputs `Base::State::UnSet`—that is low level (GPIO_PIN_RESET). The `off()` method reverses, outputting `Base::State::Set` (high level, GPIO_PIN_SET).

Then in `main.cpp` (line 11), when we instantiate the LED:

```cpp
device::LED<device::gpio::GpioPort::C, GPIO_PIN_13> led;
```

Note here the third template parameter `ActiveLevel` is not explicitly specified, its default value is `ActiveLevel::Low` (see `device/led.hpp` line 8's template declaration: `ActiveLevel LEVEL = ActiveLevel::Low`). This exactly corresponds to the active low characteristic of the PC13 LED on the Blue Pill board. If your LED connection is "pin → resistor → LED → ground" (high level on), you only need to change the template parameter:

```cpp
device::LED<device::gpio::GpioPort::A, GPIO_PIN_0, device::ActiveLevel::High> led;
```

This way `on()` will output high level to light the LED. The template system abstracts hardware differences into compile-time parameters, you don't need to change any logic code, just need to tell the template "this LED is active high or active low".

---

## Speed Setting — It's Slew Rate, Not Frequency

Finally there is an easily misunderstood configuration item that needs explaining—GPIO speed setting. In `device/gpio/gpio.hpp` (lines 45-49) three speeds are defined:

```cpp
enum class Speed : uint32_t {
    Low = GPIO_SPEED_FREQ_LOW,
    Medium = GPIO_SPEED_FREQ_MEDIUM,
    High = GPIO_SPEED_FREQ_HIGH,
};
```

These three names might cause misunderstanding—"speed" sounds like it refers to how fast the pin can switch high and low levels. But actually, GPIO speed setting controls the **Slew Rate** of the output signal, that is, the steepness of the edge when the voltage jumps from low level to high level (or vice versa).

High slew rate means voltage rises/falls fast, edges are steep; low slew rate means voltage rises/falls slow, edges are gentle. This has no direct relation to the pin's switching frequency—you can use low speed setting to switch the pin at very high frequency, just that each switch's edge isn't that steep.

So why need to control slew rate? The main reason is EMI (Electromagnetic Interference). The steeper the signal edge, the more high-frequency harmonic components it contains, the stronger the electromagnetic interference radiated outward. On high-speed signal lines (like SPI clock lines, USB data lines), you need steep edges to guarantee signal integrity, so choose high speed. But in low-speed scenarios like LEDs, steep edges have no benefit, instead increase unnecessary EMI and power consumption. So choosing low speed is the most reasonable.

On STM32F103, the actual slew rates corresponding to the three speed settings are roughly: Low corresponds to 2MHz bandwidth, Medium corresponds to 10MHz, High corresponds to 50MHz. The "bandwidth" here refers to how fast the output signal can change with that slew rate, not saying the pin can only flip at 2MHz frequency—actual flip frequency depends on your software loop speed.

For an LED flashing at 1Hz frequency, any speed setting's effect is completely the same—the human eye can't distinguish whether the voltage edge is 1 microsecond or 10 nanoseconds. Choosing `Speed::Low` both reduces EMI, and also conforms to PC13 pin's own 2MHz speed limit, it's the most reasonable choice.

If later you do SPI communication (clock frequency can be as high as 18MHz or 36MHz), you will need to use Medium or High to guarantee the SCK signal's edge is steep enough, otherwise the slave device might not correctly sample data. But in the LED scenario, low speed is enough, don't waste that unneeded bandwidth.

---

## Wrap-up: The Closed Loop from Hardware Principle to Code Logic

Here, the hardware principle of lighting an LED is finally completely closed-loop. We talked from the P-MOS/N-MOS dual-tube structure of push-pull output to the single-tube limitation of open-drain output, from the principle of pull-up/pull-down resistors to PC13's backup domain restrictions, from the sink current circuit of Blue Pill onboard LED to the design intent of the `ActiveLevel` enum in the code. Now you look back at `device/led.hpp`'s short thirty lines of code, every line has clear hardware basis—`Mode::OutputPP` corresponds to push-pull dual-tube drive, `PullPush::NoPull` corresponds to PC13's pull-up/pull-down unavailable (and push-pull itself doesn't need pull-up/pull-down), `Speed::Low` corresponds to PC13's 2MHz ceiling and LED's low-speed demand, `ActiveLevel::Low` corresponds to Blue Pill's active low circuit.

After understanding these, your development process is no longer mindless copy-paste. When you need to connect an LED, button, I2C device on another pin, you will know what output mode to choose, whether to add pull-up/pull-down, what to set speed to. These are the judgments hardware principles give you, not just "the tutorial says so".

Next part we enter the world of the HAL library. Until now we've always been using our own template class to wrap GPIO operations, but what exactly do the underlying `HAL_GPIO_Init()` and `HAL_GPIO_WritePin()` do? How do they convert our configuration parameters into register operations? And that `GPIOClock::enable_target_clock()`—why does GPIO need to open the clock first to work? Before answering these questions, we need to first understand STM32's clock tree, this is a big map that makes countless newbies dread. But don't worry, we take it step by step, first get the clock enabling thing clear—without opening the clock, GPIO is just a lump of sleeping silicon.
