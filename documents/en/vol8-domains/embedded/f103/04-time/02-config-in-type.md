---
title: 'Configuration goes into the type: designated initializers as template arguments'
description: 'TimConfig''s fields are written straight into TimBase''s template parameter slot as designated initializers; folded through the type, configuration becomes immediates in the instruction stream, and typos report at compile time. Set against ST''s official HAL_TIM_Base_Init with its handle null-check, five assert_param calls in a row, and a struct read at runtime, the two routes diverge on when errors surface and what shape configuration takes inside the firmware; the costs — configuration values printed long in error messages, one type per configuration, a period that cannot change at runtime — are each laid out in turn'
chapter: 4
order: 2
tags:
  - stm32f1
  - intermediate
  - 嵌入式
  - 模板
  - constexpr
  - cpp-modern
difficulty: intermediate
platform: stm32f1
cpp_standard: [20, 23]
reading_time_minutes: 13
prerequisites:
  - 'The TIM family and confirmation debouncing: from a 1Hz heartbeat to a 20ms confirmation window'
related:
  - 'The price list of zero overhead: dissecting the 18,200-byte logger firmware'
translation:
  source: documents/vol8-domains/embedded/f103/04-time/02-config-in-type.md
  source_hash: 19c3b98dabc744b9f4fc4b0979d660929344763356153fd33ed0ba33bff36778
  translated_at: '2026-11-01T03:20:13+00:00'
  engine: anthropic
  token_count: 2500
---

# Configuration goes into the type: designated initializers as template arguments

## Introduction: the two lines the family roster glossed over

In the last piece's family roster we flashed `TimConfig` and the `using Heartbeat = TimBase<{...}, OnBeat>` wiring; the trio's division of labor, the solving of PSC and ARR, and the APB1×2 clock rule were all covered there. One thing we merely walked past at the time: what entitles configuration to sit directly in a template's parameter slot, what this way of writing buys us, and what it costs. This piece carries exactly one question; every other axis stays folded.

What is bought and what is paid both deserve weighing, so we need something to compare against. ST's official route keeps configuration in a runtime struct: the caller fills in a `TIM_HandleTypeDef` in RAM and hands its pointer to `HAL_TIM_Base_Init`, and CubeMX-generated projects carry one by default — there are thousands upon thousands of such projects. Let's run `tim.hpp`'s way head-to-head against it, watching only two things: at what moment a misfilled configuration gets stopped, and what configuration turns into inside the finished firmware.

The material still comes from the submodule pinned at commit 8026841. The several small experiments I ran on the dev machine with GCC 16.2.1 and `-std=c++23`, the `09_tim_heartbeat` firmware produced by arm-none-eabi-g++ 16.2.0, and one error probe built with the same cross-compiler plus `-std=gnu++23` were all run on 2026-10-28; the numbers and the error texts are pasted here as they came out.

## ST's official route: the handle, five asserts in a row, and a struct read at runtime

Putting the HAL on the table is not to knock it — within ST's ecosystem it is the orthodox way. The driver source is frozen in the submodule's own `third_party/STM32F1/Drivers/STM32F1xx_HAL_Driver/` (path relative to the libestdx repo root), and every excerpt we quote comes from that one text.

The caller's posture looks like this: a `TIM_HandleTypeDef` sits in RAM, its `Instance` member points at the peripheral's register block, its `Init` member carries a configuration of type `TIM_Base_InitTypeDef`, and alongside hang `State`, `Lock`, and callback pointers. A bundling struct of this shape is what the industry calls a handle. At initialization time we hand the handle's pointer to `HAL_TIM_Base_Init`, and the function does the rest section by section.

The first fourteen lines of the function, quoted as-is:

```c
HAL_StatusTypeDef HAL_TIM_Base_Init(TIM_HandleTypeDef *htim)
{
  /* Check the TIM handle allocation */
  if (htim == NULL)
  {
    return HAL_ERROR;
  }

  /* Check the parameters */
  assert_param(IS_TIM_INSTANCE(htim->Instance));
  assert_param(IS_TIM_COUNTER_MODE(htim->Init.CounterMode));
  assert_param(IS_TIM_CLOCKDIVISION_DIV(htim->Init.ClockDivision));
  assert_param(IS_TIM_PERIOD(htim->Init.Period));
  assert_param(IS_TIM_AUTORELOAD_PRELOAD(htim->Init.AutoReloadPreload));
```

The first block is a real check: when the handle is `NULL`, the function returns `HAL_ERROR` right away. The second block's five-in-a-row `assert_param` calls are what check the configuration content, one item each: instance validity, counter mode, clock division, period, and preload. Whether these checks amount to anything depends entirely on what `assert_param` expands into, so let's paste both of its tiers as written:

```c
#define assert_param(expr) ((expr) ? (void)0U : assert_failed((uint8_t *)__FILE__, __LINE__))
#define assert_param(expr) ((void)0U)
```

The first line is what it looks like when `USE_FULL_ASSERT` is defined: when the expression is false it calls `assert_failed`, and what gets reported carries the source file name and line number; the template file provides only its declaration, leaving the implementation to the user. The second line is the default tier: in projects that don't define `USE_FULL_ASSERT`, the macro becomes a no-op — and this library of ours uses exactly that default tier, so after preprocessing the five-in-a-row checks are simply gone.

Next, the `State` machine section. Between this excerpt and the one above there is nothing but a blank line; joined together, they are the entire function:

```c
  if (htim->State == HAL_TIM_STATE_RESET)
  {
    /* Allocate lock resource and initialize it */
    htim->Lock = HAL_UNLOCKED;

#if (USE_HAL_TIM_REGISTER_CALLBACKS == 1)
    /* Reset interrupt callbacks to legacy weak callbacks */
    TIM_ResetCallback(htim);

    if (htim->Base_MspInitCallback == NULL)
    {
      htim->Base_MspInitCallback = HAL_TIM_Base_MspInit;
    }
    /* Init the low level hardware : GPIO, CLOCK, NVIC */
    htim->Base_MspInitCallback(htim);
#else
    /* Init the low level hardware : GPIO, CLOCK, NVIC */
    HAL_TIM_Base_MspInit(htim);
#endif /* USE_HAL_TIM_REGISTER_CALLBACKS */
  }

  /* Set the TIM state */
  htim->State = HAL_TIM_STATE_BUSY;

  /* Set the Time Base configuration */
  TIM_Base_SetConfig(htim->Instance, &htim->Init);

  /* Initialize the DMA burst operation state */
  htim->DMABurstState = HAL_DMA_BURST_STATE_READY;

  /* Initialize the TIM channels state */
  TIM_CHANNEL_STATE_SET_ALL(htim, HAL_TIM_CHANNEL_STATE_READY);
  TIM_CHANNEL_N_STATE_SET_ALL(htim, HAL_TIM_CHANNEL_STATE_READY);

  /* Initialize the TIM state*/
  htim->State = HAL_TIM_STATE_READY;

  return HAL_OK;
}
```

Let's read it section by section. While `State` is still `RESET`, first-time initialization happens: `Lock` is set to `HAL_UNLOCKED`, and in builds with `USE_HAL_TIM_REGISTER_CALLBACKS` switched off, the direct call is to `HAL_TIM_Base_MspInit`. It carries the `__weak` modifier, so at link time you can displace it with a function of the same name; its job is the low-level hardware layer — clocks, pins, NVIC — and with the switch on, the call goes to the callback pointer hung on the handle instead. Then `State` becomes `BUSY`, `TIM_Base_SetConfig(htim->Instance, &htim->Init)` does the register writing, `DMABurstState` and every channel's state are set to `READY`, `State` becomes `READY`, and finally `HAL_OK` comes back.

As for how the configuration's values land in the registers, here are the two lines where `SetConfig` puts pen to paper; ahead of them sit the moves that read `Structure->CounterMode`, `Structure->ClockDivision`, and the rest into CR1 at runtime:

```c
  /* Set the Autoreload value */
  TIMx->ARR = (uint32_t)Structure->Period ;

  /* Set the Prescaler value */
  TIMx->PSC = Structure->Prescaler;
```

At that moment the configuration still lives in a struct in RAM; the value the register receives was read out at runtime. This is exactly where the HAL's generality comes from: without changing one line of driver code, a differently filled `Init` swaps in a different configuration, and callbacks can be hung on while we're at it. But that generality has a precondition — it only truly pays off when the configuration's values arrive at runtime, say when parameters are read from a communication link or storage after the device is up, and the driver doesn't need recompiling. In CubeMX-generated projects, `Init` holds literals fixed at compile time; changing `Period` means rebuilding the firmware, and on that point its situation is no different from the template route. `tim.hpp` serves only TIM1 through TIM4 on the F103; the two sides want different things, so the ways of writing part company. On to the library's side.

## The library's way: configuration folded into the type, actions left to runtime

`TimConfig`, laid out once more on its own — the last piece quoted it together with `TimPwmConfig`:

```cpp
struct TimConfig {
    TimInstance instance;
    std::uint32_t period_us;
    std::uint32_t timclk_hz = 64'000'000;
};
```

The template's signature and its opening constants, for a look as well:

```cpp
template <TimConfig CFG, irq::IrqHandler Handler>
struct TimBase {
    static constexpr auto kDiv =
        detail::resolve_ticks(detail::ticks_for_period(CFG.timclk_hz, CFG.period_us));
```

What stands in the parameter slot is not `typename`. What stands there is a value of type `TimConfig`. The formal name for this kind of parameter is non-type template parameter (often abbreviated NTTP in the docs); the C++26 draft renames it constant template parameter, cppreference's entry already carries the new name, and this piece sticks to the C++20/23 name. The old face of such parameters is integer literals — `std::array`'s size is one — and class types being allowed to stand there is a C++20 liberalization. We quoted 09's wiring in the last piece; it has the shape `TimBase<{.instance = ..., .period_us = 1'000'000}, OnBeat>`, and the `.field = value` style inside the braces is called a designated initializer, now standing directly in the template-argument position.

I verified this once on the dev machine, with GCC 16.2.1 and `-std=c++23`. The feature-test macro `__cpp_nontype_template_args` reads 201911, which corresponds precisely to that C++20 relaxation<RefLink :id="1" preview="cppreference, Template parameters" /><RefLink :id="4" preview="GCC, C++ Standards Support" />. The smallest positive example takes two lines:

```cpp
struct Cfg { int a; int b; };
template <Cfg c> struct X { static constexpr int sum = c.a + c.b; };
```

We ran `X<{.a = 2, .b = 3}>::sum` through a compile; it folded into a constant and evaluated to 5. The configuration never passed through any runtime variable — it became part of the type.

Class types entering the parameter slot come with an entrance requirement; the standard's word for it is structural type. In plain terms: all of a class's non-static data members must be public, its base classes must be public too, and the members themselves must be literal types evaluable at compile time; the moment a private member appears, the qualification is gone. Tracing the proposal lineage: P0732R2 was the original design that landed, and it was already using the phrase strong structural equality; P1907R1 did the relaxation and consistency revisions, and it is what established the term structural type<RefLink :id="2" preview="Jeff Snyder, Louis Dionne, P0732R2" /><RefLink :id="3" preview="Jens Maurer, P1907R1" />. Compiler progress matters here too: GCC 9 in C++20 mode implemented P0732R2's original design; the P1907R1 revisions were partially picked up from GCC 10 and only complete in GCC 11 — the macro value we measured on our machine matches the documentation. One piece of history while we're here: P1837R0 proposed removing class types from C++20, but the committee declined<RefLink :id="7" preview="Arthur O'Dwyer, P1837R0" />.

With configuration inside the type, the runtime work has not vanished — the library splits the two apart. Starting and retracting the window are runtime actions; here are `TimOneShot`'s two members, quoted as-is:

```cpp
    static void arm() {
        auto* const t = HW::regs();
        t->CR1 |= TIM_CR1_OPM;
        t->EGR = TIM_EGR_UG;
        t->SR = ~TIM_SR_UIF;
        t->CR1 |= TIM_CR1_CEN;
    }

    static void disarm() { HW::regs()->CR1 &= ~(TIM_CR1_OPM | TIM_CR1_CEN); }
```

The 20ms window width is settled at compile time, while opening and closing both happen at runtime — the two ends run on different mechanisms. On the opening side, EXTI's edge calls `Window::arm()` at runtime; we strung that chain in the last piece, and `OnEdge`, the one that does it in example 10, has this function body, quoted as-is: `static void operator()() { Window::arm(); }`. On the closing side, no one intervenes at all: the OPM bit that `arm()` set puts the timer in single-pulse mode, and once it counts to the end the hardware stops by itself — that is what the last piece said about one-pulse mode. We also checked the full text of example 10: `disarm()` is called zero times. It is a door the library keeps for other purposes; retracting the window by hand is its job.

## When errors surface: compile-time messages versus no-op asserts

When the configuration is misfilled, at what moment does each route tell you? We ran one experiment for each.

The first negative example touches a private member. Put a class `Bad` with a private member into the template parameter slot, and the compile walks right back out on the spot; the first two lines of the error, pasted as they came:

```text
error: 'Bad' is not a valid type for a template non-type parameter because it is not structural
note: 'Bad::x' is not public
```

There it is — the error says both things completely: `Bad` doesn't qualify as a template parameter, because `Bad::x` is not a public member; that is exactly where the structural condition bites. The failure stopped at compile time, caught before the firmware even began to build.

The other negative example errs on the type. Following `Bad`'s lead, we set out another template `Z` whose parameter slot holds a different minimal struct also named `Cfg`, its field trimmed to `std::uint32_t` after `TimConfig`'s `period_us`. This time we fed a string literal to the integer field; the error text for `Z<{.period_us = "1ms"}>` reads:

```text
error: could not convert '{"1ms"}' from '<brace-enclosed initializer list>' to 'Cfg'
```

`"1ms"` cannot enter a `std::uint32_t` field, so the template-argument gate turns it away. You also got a look at something worth noting: the error prints the configuration value exactly as it was filled in. Let's write that down — we'll come back to it when we reach the price section.

Now over to the HAL's side, to see how the same class of typo is treated. In the default tier, the five-in-a-row `assert_param` calls are no-ops, gone after preprocessing. The handle null-check is a real check, but it checks the pointer, not the configuration's content. If you want these checks to come alive, you must define the `USE_FULL_ASSERT` macro and write the `assert_failed` implementation yourself — and even then the checks run at runtime, that is, at the moment of initialization on the device. The same misfilled `Period` gets stopped at compile time on the library's side; under the HAL's default configuration it can only maybe be discovered once the device is running, and whether it is discovered at all depends on whether the asserts were switched on.

## Immediates in the instruction stream: how configuration gets baked into the firmware

I ran `arm-none-eabi-nm` over 09's firmware image, then filtered through `grep -c HAL_TIM`, and the count came out 0: the HAL's TIM driver was never linked in at all — the work of handling TIM registers lives entirely in `tim.hpp`, written directly. The HAL wasn't thrown out wholesale, mind you: 09's `main.cpp` still calls `HAL_Init` and `HAL_RCC_OscConfig`; it's just that TIM's registers are not its business.

What does configuration look like once it lands in the firmware? Here is the disassembly of the `TimBase` wiring section from 09's build artifacts, with the arrow lines and the elision line being plain-language annotations — not one instruction byte touched:

```text
800151c: f04f 4380  mov.w r3, #1073741824 @ 0x40000000   ← TIM2 base address
8001520: f240 38e7  movw  r8, #999 @ 0x3e7               ← PSC
8001524: f64f 1eff  movw  lr, #63999 @ 0xf9ff            ← ARR
8001528: f06f 0c01  mvn.w ip, #1                          ← ~UIF
...(the middle is the RCC clock gate being opened and read back — not this piece's material)
8001548: f8c3 8028  str.w r8, [r3, #40] @ 0x28            ← PSC written into TIM2+0x28
```

999 and 63999 are exactly the pair the solver produced in the last piece; we won't retell their origin. In this stretch of instruction stream they are `movw` immediates — constants encoded right into the instructions — loaded into a register and then written out to TIM2+0x28's PSC, with no read-from-RAM of configuration anywhere in between. TIM2's base address 0x40000000 is an immediate too, carried in by `TimInstance::Tim2` taking its value from an address — that convenience was covered in the last piece. The contrast is the ARR assignment quoted on ST's official route, where the register's value has to wait for runtime to be read out of RAM. Having passed through the fold of the type, the configuration went from a struct in RAM to constants in the firmware's code region.

## The price and the boundary

What was bought, the previous two sections laid out: errors stop at compile time, and configuration folds into immediates. The prices to pay — several; let's go through them one by one.

The first price hides in the error message. You have already seen the could-not-convert line from negative example two, and that was still a two-field minimal struct. What does it become with the real library? I probed it once with the real wiring, written as `TimBase<{.instance = TimInstance::Tim2, .period_us = "1ms"}, OnBeat>`, built with arm-none-eabi-g++ 16.2.0 and `-std=gnu++23`, same build options as 09; the main error that came out is one line:

```text
nested.cpp:3:80: error: could not convert '{estdx::stm32f1::TimInstance::Tim2, "1ms"}' from '<brace-enclosed initializer list>' to 'estdx::stm32f1::TimConfig'
```

That one line is the whole real error; what follows are only two consequential lines of `expected type-specifier` and `has not been declared`. Compare the shape of the message against the minimal example's: identical, and the only thing that grew is the configuration value printed inside — the Tim2 entry came out as `estdx::stm32f1::TimInstance::Tim2`, namespaces and all.

The price on the other side is the number of types. One configuration corresponds to one type: the three timers used by 09 and 10 are three independent instantiations, each with its own type, and both compile time and code size scale with the count — conveniently, the checks and the folding each stay in their own lane too. The size story was measured in the last piece; the confirmation build's text came out smaller than the pure-logging example, so we won't measure it twice here.

Configuration cannot change at runtime. `period_us` is written into the type, and changing the period means rebuilding the firmware. The parts that genuinely vary at runtime, the library hands to calls like `arm()` and to OPM's self-stop — opening and closing the window never touches the configuration. If we want to change the period at runtime, there is no road other than rebuilding.

Floats and pointers can enter the template-argument roster too — C++20 let them through. We tried `struct F { float f; int i; }` with `W<{.f = 1.5f, .i = 2}>`, and that compiled as well. The library's two configurations use integer fields throughout, so we mention this only as a boundary: should you really use floats, the entrance requirement is still that same structural condition.

The first dividend of configuration-in-type, the last piece already collected: those two `static_assert`s stop out-of-range periods at compile time and catch the precision misses alongside. The second dividend lives in the range itself — under the same timclk constraint, which periods fit inside 16 bits times 16 bits? Next piece we compute the edge of the range.

## Caveats and common mistakes

| Symptom | Cause | Fix |
|---|---|---|
| `'Bad' is not a valid type ... because it is not structural` | the class has non-public members and fails the structural condition | make the members public, or split the configuration into integer fields |
| `could not convert ... to 'Cfg'` | a designated-initializer value doesn't match the field's type | fill values by field type; `period_us` takes uint32_t |
| You want to change the period at runtime | the period is folded into the type, an immediate in the instruction stream | change the configuration and rebuild; hand runtime changes to `arm()` and `disarm()` |
| Misconfiguration goes unchallenged in a HAL project | the default-tier `assert_param` is a no-op; the five checks aren't there | define `USE_FULL_ASSERT` and implement `assert_failed`; the checks run at runtime |
| Float fields into the template argument | allowed since C++20; the condition is still structural | integer fields suffice — no need for floats; both configurations in the library are all-integer |

## References

<ReferenceCard title="References">
  <ReferenceItem :id="1" author="cppreference" title="Template parameters" url="https://en.cppreference.com/w/cpp/language/template_parameters" chapter="Rules for non-type template parameters and structural types; from C++26 the parameter is renamed constant template parameter" />
  <ReferenceItem :id="2" author="Jeff Snyder, Louis Dionne" title="P0732R2: Class Types in Non-Type Template Parameters" :year="2018" url="https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p0732r2.pdf" chapter="The original design putting class types into non-type template parameters" />
  <ReferenceItem :id="3" author="Jens Maurer" title="P1907R1: Inconsistencies with non-type template parameters" :year="2019" url="https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p1907r1.html" chapter="Where the term structural type comes from; the relaxation and consistency revision" />
  <ReferenceItem :id="4" author="GCC Project" title="C++ Standards Support in GCC" url="https://gcc.gnu.org/projects/cxx-status.html" chapter="Implementation progress for class-type non-type template parameters: GCC 9 lands P0732R2; the P1907R1 revision is partial from GCC 10 and complete in GCC 11" />
  <ReferenceItem :id="5" author="STMicroelectronics" title="stm32f1xx_hal_tim.c and stm32_assert_template.h" chapter="HAL_TIM_Base_Init and TIM_Base_SetConfig, the two tiers of assert_param expansion; text frozen in the libestdx submodule third_party/STM32F1/Drivers/STM32F1xx_HAL_Driver/, with tim.c in the Src/ subdirectory and stm32_assert_template.h in Inc/" />
  <ReferenceItem :id="6" author="Awesome-Embedded-Learning-Studio" title="libestdx (boards/stm32f1/tim.hpp, submodule pinned at commit 8026841)" :year="2026" url="https://github.com/Awesome-Embedded-Learning-Studio/libestdx" chapter="TimConfig, TimBase, TimOneShot, arm/disarm" />
  <ReferenceItem :id="7" author="Arthur O'Dwyer" title="P1837R0: Remove NTTPs of class type from C++20" :year="2019" url="https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p1837r0.html" chapter="The proposal to remove class-type NTTPs from C++20; the committee declined" />
</ReferenceCard>
