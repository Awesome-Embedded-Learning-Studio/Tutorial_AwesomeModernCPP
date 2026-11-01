---
title: 'Two generations of callbacks: weak symbols and the concept contract'
description: 'The previous article used KeyEvent::irq() as an off-the-shelf part of the EXTI edge chain; this one keeps taking the vector function apart and asks a single question: how does the handler get from user code into the interrupt call site. The HAL keeps two wirings ready in the same F1 driver: weak symbols lean on a __weak empty body plus a same-named strong symbol from the user, the linker overrides the weak one without a word of notice, the compilation-unit boundary becomes the inlining boundary, and the distillation measures text 76 (two units) and 60 (three units, two jumps per interrupt), dropping to 44 once gc-sections is on. Registered pointers write the callback into the PendingCallback slot of EXTI_HandleTypeDef and move the check to run time on the return value of HAL_EXTI_RegisterCallback; the 16-slot pointer table pushes bss up to 68, and the two-level address fetch together with the Thumb state bit in the 0x1 literal is plainly visible in the disassembly. The third wiring in libestdx makes the handler a template parameter, with the IrqHandler concept intercepting ill-formed instantiations at compile time; the gen2 distillation measures text 28, ten instructions without a single call. The check timing of the three wirings and their dispatch burden on a shared vector close out in the comparison table'
chapter: 2
order: 2
tags:
  - stm32f1
  - intermediate
  - 回调机制
  - concepts
  - 链接器
difficulty: intermediate
platform: stm32f1
cpp_standard: [20, 23]
reading_time_minutes: 21
prerequisites:
  - 'One burst of bounce, two chains: polling debounce vs. the EXTI edge chain'
related:
  - 'Putting configuration into types: the Gpio template and compile-time verification'
translation:
  source: documents/vol8-domains/embedded/f103/02-button/02-callback-weak-vs-concept.md
  source_hash: 781cd2da94fb5871b07cf3da90ec6de45e2a0281dc527d95e7f354518b761c3f
  translated_at: '2026-11-01T03:18:31+00:00'
  engine: anthropic
  token_count: 3000
---

# Two generations of callbacks: weak symbols and the concept contract

## The opening: how the handler walks into the interrupt

In the previous article, [One burst of bounce, two chains: polling debounce vs. the EXTI edge chain](01-polling-debounce-vs-async-exti.md), we pressed the EXTI edge chain into service as an off-the-shelf part: one line of `KeyEvent::irq()` in the vector function turned every incoming edge into an entry in the queue. We used that line, but never took it apart. This article carries a single question: how does the handler get from user code into the interrupt call site.

The vector function moving into the user's file is something the previous article already showed us once. One row of the closing pitfalls table, brought back as-is:

| Symptom | Cause | Fix |
|---|---|---|
| The linker complains about a duplicate `EXTI0_IRQHandler` | The vector function is already defined in `main.cpp`, and `it.c` holds another copy | Keep only the comment in `it.c`; the vector belongs to `main.cpp` |

Here lies the root of the linker's complaint. The interrupt call site lives deep inside the startup code and the library, while the handling logic lives in the user's code, and somebody has to decide how the two sides join. The HAL keeps two wirings ready in the same F1 driver: weak symbols and registered pointers are both alive. libestdx hands over a third: the type parameter. The three wirings place the check at different stages, and we take them one by one in the order "overridden at link time, table filled at run time, instantiated at compile time."

## Weak symbols: the linker picks the function on the user's behalf

The HAL's GPIO module keeps a wrapper function ready for EXTI; here is the original text from `stm32f1xx_hal_gpio.c`<RefLink :id="3" preview="STMicroelectronics, stm32f1xx_hal_gpio.c" />:

```c
void HAL_GPIO_EXTI_IRQHandler(uint16_t GPIO_Pin)
{
  /* EXTI line interrupt detected */
  if (__HAL_GPIO_EXTI_GET_IT(GPIO_Pin) != 0x00u)
  {
    __HAL_GPIO_EXTI_CLEAR_IT(GPIO_Pin);
    HAL_GPIO_EXTI_Callback(GPIO_Pin);
  }
}
```

Read what it does once through: on finding the pending bit set it clears it, then hands the pin number over to the callback. The callback that does the work sits in the same file, and its definition carries `__weak`:

```c
__weak void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  /* Prevent unused argument(s) compilation warning */
  UNUSED(GPIO_Pin);
  /* NOTE: This function Should not be modified, when the callback is needed,
           the HAL_GPIO_EXTI_Callback could be implemented in the user file
   */
}
```

The function body is empty, and the point of the whole provision sits in the NOTE comment. Only one line matters, and we copy it verbatim: `the HAL_GPIO_EXTI_Callback could be implemented in the user file`. The meaning is: do not modify this function. When you want the callback, write a same-named one in your own file.

`__weak` is a GCC function attribute. The plain version of a weak symbol: a strong and a weak of the same name exist side by side, and at link time the weak one steps aside for the strong. The original weak passage from the GCC manual's Common Function Attributes section, brought over for comparison<RefLink :id="1" preview="GCC, Common Function Attributes, weak" />:

```text
"This attribute applies to function and variable declarations. The weak
attribute causes a declaration of an external symbol to be emitted as a weak
symbol rather than a global. This is primarily useful in defining library
functions that can be overridden in user code, though it can also be used
with non-function declarations. The overriding symbol must have the same type
as the weak symbol. In addition, if it designates a variable it must also
have the same size and alignment as the weak symbol. Weak symbols are
supported for ELF targets, and also for a.out targets when using the GNU
assembler and linker."
```

The sentence to underline is `The overriding symbol must have the same type as the weak symbol`. When the linker does the overriding it checks the name only — the type agreement goes unexamined — and the overriding itself comes with zero notice: the linker finishes the job without a sound.

The user-side wiring follows the NOTE comment: you write a `HAL_GPIO_EXTI_Callback` of the same name without `__weak` in your own file, it becomes the strong symbol, and at link time it overrides the weak empty body in the library. The entry `switch (GPIO_Pin)` still has to ask where the call came from, because the HAL's callback does not split by line: whatever EXTI line fired, the wrapper funnels every interrupt into the same name, so the function must use the pin number to tell who is calling before each case goes its own way. That is exactly what `gen1a_user.c` distills in the E1 experiment.

One word on what distillation means: the minimal shape of the real sources is extracted into a few small source files compiled separately, we look only at size and disassembly, and only then can the numbers line up. All of E1 was compiled with `arm-none-eabi-gcc 16.2.0`, at `-O2`, targeting `cortex-m3`.

### The compilation-unit boundary is the inlining boundary

A compilation unit is the TU we have been speaking of: the object file one source file compiles into on its own. Set against the callback chain, the unit boundary is at the same time the inlining boundary. E1 provides two contrasts; here are the numbers and how to read them:

- gen1a: two compilation units — one for the HAL, one for the user — text 76. The wrapper function, being in the same unit, was inlined by -O2 into the vector function, so testing the pending bit and clearing it both moved inside the vector function's body. The user callback, across the unit boundary, can only be reached by a bl.
- gen1a3: three compilation units, split after the real sources' shape into it.c, the HAL library, and the user — text 60. The vector function tail-jumps into the wrapper with one b.w, and the wrapper reaches the user's strong symbol with one bl: two jumps per interrupt, both because a compilation unit boundary was crossed.

The term tail call shows up here. In plain words: the hand-off at the end of a function, where the compiler replaces a call with a jump and saves one push-and-pop — the b.w we see is exactly that. Here are gen1a3's two jumps, pasted as-is:

```text
00000000 <EXTI0_IRQHandler>:
   0:	2001      	movs	r0, #1
   2:	f000 b803 	b.w	c <HAL_GPIO_EXTI_IRQHandler>
   6:	bf00      	nop
   8:	4770      	bx	lr
   a:	bf00      	nop

0000000c <HAL_GPIO_EXTI_IRQHandler>:
   c:	f04f 4280 	mov.w	r2, #1073741824	@ 0x40000000
  10:	6951      	ldr	r1, [r2, #20]
  12:	4201      	tst	r1, r0
  14:	d100      	bne.n	18 <HAL_GPIO_EXTI_IRQHandler+0xc>
  16:	4770      	bx	lr
  18:	b508      	push	{r3, lr}
  1a:	6150      	str	r0, [r2, #20]
  1c:	f000 f802 	bl	24 <HAL_GPIO_EXTI_Callback>
  20:	bd08      	pop	{r3, pc}
```

The `bx lr` at address 8 is worth a look too: that is the overridden weak empty body, referenced by no one anymore, yet it still shipped with the firmware. It is there in the two-unit gen1a as well, with its position moved to address 0. The vector function of gen1a folded all three — testing pending, clearing pending, the bl — into its own body, and we can count them out in its disassembly. The user-callback end is worth a look as well: the entry switch got collapsed in the disassembly into a single `cmp r0, #1`. The distillation has only one line, so one comparison settles it. With multiple lines in the picture, the burden of asking who is calling at the door comes back.

### Without gc-sections, the dead weak symbol ships with the firmware

The 4 bytes held by the dead weak symbol have a sequel. E1 added one more contrast: on the same two-unit sources, with `-ffunction-sections` and `--gc-sections` turned on and `EXTI0_IRQHandler` designated as the collection root, text fell from 76 to 44. What got collected was the dead weak definition and the wrapper body nobody calls; what survived is the vector function (testing and clearing pending already inlined inside it) plus the user's strong symbol. Let's be precise about scope: the numbers in hand are measurements of the distilled sources. Whether the real project enables gc-sections, and how much it would reclaim, depends on the project's own shape — we will not extrapolate the 44 to it. By this point, the check timing of the weak-symbol generation is clear: the linker decides who takes the stage, with zero notice at override time. When the signature mismatches, the linker says not a word, and the truth only comes out once the callback is actually invoked. In the end, the whole constraint is nothing but the same name.

## Registered pointers: filling the table at run time

The second wiring lives in the EXTI module itself. `EXTI_HandleTypeDef` in `stm32f1xx_hal_exti.h` is a handle struct; let's look at its fields<RefLink :id="3" preview="STMicroelectronics, stm32f1xx_hal_exti.h" />:

```c
typedef struct
{
  uint32_t Line;                    /*!<  Exti line number */
  void (* PendingCallback)(void);   /*!<  Exti pending callback */
} EXTI_HandleTypeDef;
```

Besides `Line` we find one more field, `PendingCallback`, whose type is a function pointer. A function pointer in plain words: a variable holding a function's address, where calling it means jumping to the stored address and executing there. The callback connection has changed from link-time same-name overriding to run-time address-filling into this field. The identity enum `EXTI_CallbackIDTypeDef` has exactly one option, `HAL_EXTI_COMMON_CB_ID` — that is the only identity the F1 header defines.

### Registration and invocation, checked by the return value

The entry to registration is `HAL_EXTI_RegisterCallback`; here is its body:

```c
    HAL_StatusTypeDef HAL_EXTI_RegisterCallback(EXTI_HandleTypeDef *hexti, EXTI_CallbackIDTypeDef CallbackID, void (*pPendingCbfn)(void))
    {
      HAL_StatusTypeDef status = HAL_OK;

      switch (CallbackID)
      {
        case  HAL_EXTI_COMMON_CB_ID:
          hexti->PendingCallback = pPendingCbfn;
          break;

        default:
          status = HAL_ERROR;
          break;
      }

      return status;
    }
```

Now the inside of registration. Before writing the pointer there is a `switch (CallbackID)` checking the identity: a recognized identity writes the slot and returns `HAL_OK`; an unrecognized one falls to default and returns `HAL_ERROR`. The check has moved from link time to run time, carried entirely by the return value. The body of the call-side `HAL_EXTI_IRQHandler` comes over as well:

```c
    void HAL_EXTI_IRQHandler(EXTI_HandleTypeDef *hexti)
    {
      uint32_t regval;
      uint32_t maskline;

      /* Compute line mask */
      maskline = (1uL << (hexti->Line & EXTI_PIN_MASK));

      /* Get pending bit  */
      regval = (EXTI->PR & maskline);
      if (regval != 0x00u)
      {
        /* Clear pending bit */
        EXTI->PR = maskline;

        /* Call callback */
        if (hexti->PendingCallback != NULL)
        {
          hexti->PendingCallback();
        }
      }
    }
```

Let's read its order once through: compute the mask, test PR, clear PR — after those three steps, only if `if (hexti->PendingCallback != NULL)` passes does the call happen. The real sources carry the NULL check before the call; the distilled gen1b of E1 skips that step, and we mark the difference honestly: the distillation compares the shape of the call site, while the real sources add one null-pointer defense.

### The pointer table in bss

gen1b measures text 64, bss 68. Spread the bss out: the 16-slot pointer table takes 64 bytes, and timestamp takes 4. The table moved into RAM — that is this generation's visible cost. Reading the instructions on both ends off the log: once testing PR and clearing PR are done, `ldr r2, [pc, #8]` fetches the table's base address, `ldr r2, [r2, #4]` fetches the slot, and the jump falls to `bx r2` — only after two levels of address fetching is the callback finally reached. On the registration side, the literal pool holds `0x00000001`, which is the address of `on_line0` ORed with 1: the body's address is 0, and ORing with 1 still yields 1. The lowest bit of an address is not counted as address — it flags the Thumb state. The instruction set the Cortex-M3 executes is called Thumb, and the lowest bit of a function address is borrowed as the state marker, so `bx` jumps carrying it, and what executes after the jump follows the Thumb instruction set — all plainly visible in the disassembly. The GPIO channel and the EXTI handle channel coexist in the same F1 driver; they are two parallel roads, and we are not saying one replaces the other. On the switch front, we speak only of the sources at hand: in the F1 EXTI driver sources, `HAL_EXTI_RegisterCallback` has no separate compile switch — the whole file knows only the module-wide `HAL_EXTI_MODULE_ENABLED`, and we extrapolate nothing beyond that.

## Type parameters: the contract signed at compile time

### The concept itself

For libestdx's wiring, start from the concept itself<RefLink :id="2" preview="Awesome-Embedded-Learning-Studio, libestdx" />. The whole of `irq_base.hpp` is only 11 lines, and the core we want is a few of them:

```cpp
   template <typename H>
   concept IrqHandler = requires {
       H::operator()();
   };
```

A concept, a feature from C++20 onward, is a compile-time true-or-false question: it puts an is-or-isn't question to a type, and the answer is settled at compile time. The only thing `IrqHandler` asks is whether the type H can produce a static `operator()`. The previous article's words were "the handler must satisfy the requirement of `irq::IrqHandler` — providing a static `operator()` is enough to pass." We will not retell where the concept came from; here we only look at where it sits in the wiring.

### irq() and the user side

The `irq()` of `Exti` in `exti.hpp`, lifted verbatim:

```cpp
   static void irq() {
       if (EXTI->PR & Pin::mask) {
           EXTI->PR = Pin::mask;
           Handler::operator()();
       }
   }
```

Reading its order off the source: "test PR, clear PR, call `Handler::operator()()`." `Handler` is a template parameter of `Exti`, so who the callback is was settled at instantiation, and neither table lookup nor asking who is calling appears anywhere inside the interrupt. The user side is those few lines from example 07:

```cpp
struct OnTouch {
    static void operator()() { edge_queue.push(HAL_GetTick()); }
};
using KeyEvent = estdx::stm32f1::Exti<KeyPin, estdx::stm32f1::ExtiEdge::Rising,
                                      OnTouch>;
```

The vector function sits in the same file:

```cpp
extern "C" void EXTI0_IRQHandler() {
    KeyEvent::irq();
}
```

`OnTouch` turned queue-pushing into a type, `KeyEvent` twisted the pin, the edge, and the handler into one line of alias, and the vector function is down to three lines. Last time we looked at them while assembling the station; this time we look again and see something else: the callback connection here relies neither on the linker's same-name overriding nor on a run-time table — it is already part of the type.

### gen2: not a single call in the whole listing

E1's gen2 was compiled with `-std=c++23`, and the text we get is 28. Counting through, not one of the 10 instructions is a call: testing PR, clearing PR, and writing the timestamp all landed inside the single body of the vector function:

```text
00000000 <EXTI0_IRQHandler>:
   0:	f04f 4380 	mov.w	r3, #1073741824	@ 0x40000000
   4:	695a      	ldr	r2, [r3, #20]
   6:	07d2      	lsls	r2, r2, #31
   8:	d505      	bpl.n	16 <EXTI0_IRQHandler+0x16>
   a:	2001      	movs	r0, #1
   c:	f243 0139 	movw	r1, #12345	@ 0x3039
  10:	4a01      	ldr	r2, [pc, #4]	@ (18 <EXTI0_IRQHandler+0x18>)
  12:	6158      	str	r0, [r3, #20]
  14:	6011      	str	r1, [r2, #0]
  16:	4770      	bx	lr
  18:	0000101c 	.word	0x0000101c
```

The static `operator()` is syntax only since C++23, which is why we mark this article's cpp_standard as both 20 and 23: the concept is C++20, the static `operator()` is C++23. When a wiring does not conform, the concept intercepts the instantiation at compile time; when it does, the call site becomes a direct call and even inlines, leaving no hand-off anywhere on the chain.

## The three wirings side by side, and dispatch on a shared vector

| | Weak symbols | Registered pointer | Type parameter |
|---|---|---|---|
| Where the callback lives | A same-named strong symbol in the user file | The PendingCallback slot of the handle | The Handler template parameter of Exti |
| When the check runs | At link time; overriding with zero notice | At run time, via the return value | At compile time; the concept blocks the instantiation |
| Call-site shape | A cross-unit bl, or a tail jump plus a bl | Two address fetches, then bx r2 | A direct call, inlined outright |
| Measured text | 76 (two units), 60 (three units) | 64 | 28 |
| Measured bss | 4 | 68 | 4 |
| Dispatch on a shared vector | The entry switch(GPIO_Pin) asks who is calling | Slots travel with handles, one per line | One instantiation per line, flattened at compile time |

The shared vector deserves its own word. The numbers in the table all come from the same batch of E1 distillations, and the gc contrast of 44 vouches only for the two-unit distilled sources. The per-line mapping table was given in the previous article; here we only look at how it shapes each wiring. Line 0 has its own dedicated vector: binding the vector function straight onto `KeyEvent` is all it takes, and the ask-who-is-calling step disappears entirely. When lines 5 through 9 share one vector, the weak-symbol generation is no longer so carefree: there is only one callback name, and the entry `switch (GPIO_Pin)` must establish who is calling before it can dispatch — gen1a_user's entry switch is exactly that. On the estdx side, each line instantiates its own copy, so dispatch is flattened at compile time; that is why gen2 has no switch, and it is precisely this point that the two distillations corroborate in each other. In the registered-pointer generation, the slot travels with the handle: one line is paired with one handle, and each slot holds its own callback.

## Wrapping up

The second article of the button stop ends here. You have now seen all three wirings: weak symbols hand the check to the linker, registered pointers move it onto run-time return values, and concepts press it down into compile-time instantiation. Next time we take apart `claim_line` — the bitmap machinery behind that `__builtin_trap()` row in the previous article's table.

## References

<ReferenceCard title="References">
  <ReferenceItem
    :id="1"
    author="GCC"
    title="GCC Manual — Common Function Attributes (weak)"
    url="https://gcc.gnu.org/onlinedocs/gcc/Common-Attributes.html"
    chapter="weak attribute: overriding symbol must have the same type as the weak symbol"
  />
  <ReferenceItem
    :id="2"
    author="Awesome-Embedded-Learning-Studio"
    title="libestdx (pin 8026841)"
    :year="2026"
    url="https://github.com/Awesome-Embedded-Learning-Studio/libestdx"
    chapter="irq_base.hpp, exti.hpp, examples/07_button_events"
  />
  <ReferenceItem
    :id="3"
    author="STMicroelectronics"
    title="stm32f1xx_hal_gpio.c and stm32f1xx_hal_exti.c (vendored sources)"
    chapter="the HAL_GPIO_EXTI_Callback weak-symbol channel and the HAL_EXTI_RegisterCallback registered-pointer channel"
  />
</ReferenceCard>
