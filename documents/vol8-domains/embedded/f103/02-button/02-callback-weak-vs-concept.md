---
title: "回调的两代：弱符号与 concept 合同"
description: "上一篇的 EXTI 边沿链把 KeyEvent::irq() 当现成零件用过了，本篇顺着向量函数往下拆，只问一件事：处理函数是怎么从用户代码走进中断调用点的。HAL 在同一份 F1 驱动里备着两种接法：弱符号靠 __weak 空体加用户同名强符号，链接器顶掉弱者时零提示，编译单元的边界成了内联的边界，蒸馏实测 text 76（两单元）与 60（三单元，一次中断两跳），开 gc-sections 后降到 44。注册指针把回调写进 EXTI_HandleTypeDef 的 PendingCallback 槽，检查挪到运行期靠 HAL_EXTI_RegisterCallback 的返回值，16 槽指针表让 bss 涨到 68，反汇编里两级取址与 0x1 字面量的 Thumb 状态位肉眼可见。libestdx 的第三种把处理器当模板参数，IrqHandler concept 在编译期拦下不合式的实例化，gen2 蒸馏 text 28、10 条指令通篇无 call。三种接法的检查时机与共享向量时的分派负担，收在对照表里"
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
reading_time_minutes: 11
prerequisites:
  - "一串抖动，两条链：轮询消抖与 EXTI 边沿的对拍"
related:
  - "把配置写进类型：Gpio 模板与编译期的验证"
---

# 回调的两代：弱符号与 concept 合同

## 引言：处理函数是怎么走进中断的

咱们在上一篇[《一串抖动，两条链：轮询消抖与 EXTI 边沿的对拍》](01-polling-debounce-vs-async-exti.md)里把 EXTI 边沿链当零件用了一回：向量函数里的一行 `KeyEvent::irq()`，中断进来的沿就变成了队列里的一条边。这一行用是用了，咱们没有往下拆。本篇咱们只扛一个问题：处理函数是怎么从用户代码走进中断调用点的。

向量函数住进了用户文件，这件事上一篇就让咱们见过一回。收尾的注意事项表里有一行，咱们原样搬回来：

| 症状 | 原因 | 解法 |
|---|---|---|
| 链接器抱怨 `EXTI0_IRQHandler` 重复定义 | 向量函数已经写在 `main.cpp`，`it.c` 里又放了一份 | `it.c` 里只留注释，向量归 `main.cpp` |

链接器抱怨的根子就在这儿。中断的调用点住在启动代码与库的深处，处理逻辑住在用户的代码里，两边的接法总得有人定，HAL 在同一份 F1 驱动里备着的接法有两种，弱符号与注册指针都是活着的。libestdx 交的是第三种：类型参数。三种接法把检查放在了不同的阶段，咱们按“链接期顶掉、运行期填表、编译期实例化”的顺序一种一种看。

## 弱符号：链接器替用户选函数

HAL 的 GPIO 模块为 EXTI 备了一个包装函数，咱们把 `stm32f1xx_hal_gpio.c` 里的原文搬过来<RefLink :id="3" preview="STMicroelectronics, stm32f1xx_hal_gpio.c" />：

```c
   ```c
   void HAL_GPIO_EXTI_IRQHandler(uint16_t GPIO_Pin)
   {
     /* EXTI line interrupt detected */
     if (__HAL_GPIO_EXTI_GET_IT(GPIO_Pin) != 0x00u)
     {
       __HAL_GPIO_EXTI_CLEAR_IT(GPIO_Pin);
       HAL_GPIO_EXTI_Callback(GPIO_Pin);
     }
```

它做的事咱们念一遍：查到了挂起位就清掉，然后把引脚号交给了回调。干活的回调在同文件里，它的定义带着 `__weak`：

```c
   ```c
   __weak void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
   {
     /* Prevent unused argument(s) compilation warning */
     UNUSED(GPIO_Pin);
     /* NOTE: This function Should not be modified, when the callback is needed,
              the HAL_GPIO_EXTI_Callback could be implemented in the user file
      */
```

函数体是空的，条文的要点全在 NOTE 注释里。要紧的就一句，咱们原样抄：`the HAL_GPIO_EXTI_Callback could be implemented in the user file`。咱们把英文翻过来，意思是您别改这个函数。您想要回调的时候，就在自己的文件里写一份同名的。

`__weak` 是 GCC 的函数属性。弱符号的白话版：同名的一强一弱同时存在，链接时弱者给强者让了位。GCC 手册 Common Function Attributes 节的 weak 段原文，咱们也搬来对照<RefLink :id="1" preview="GCC, Common Function Attributes, weak" />：

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

咱们要划出来的是 `The overriding symbol must have the same type as the weak symbol` 这一句。链接器顶掉的时候只认名字，类型的约定它不检查，顶掉本身也是零提示的，链接器不吭一声就办完了。

用户侧的接法照着 NOTE 注释做：您在自己的文件里写一个不带 `__weak` 的同名 `HAL_GPIO_EXTI_Callback`，它就成了强符号，链接时把库里的弱空体顶掉。进门的 `switch (GPIO_Pin)` 还得问一句来路，因为 HAL 的回调不分线：任何一条 EXTI 线的中断，走完包装函数汇进的都是同一个名字，函数得拿引脚号分清是谁来的，再各走各的处理。E1 蒸馏实验里的 `gen1a_user.c` 蒸的就是它。
蒸馏的意思咱们交代一句：把真源的最小形状抽成几个小源文件单独编译，看的只有尺寸和反汇编，数字才是对得齐的。整个 E1 都是拿 `arm-none-eabi-gcc 16.2.0` 按 `-O2` 编 `cortex-m3` 的。
### 编译单元的边界就是内联的边界

编译单元就是咱们平常说的 TU：一个源文件独立编译出来的目标文件。放到回调的链上看，单元的边界同时就是内联的边界。E1 给了两份对照，咱们把数字和读法都摆出来：

- gen1a，两个编译单元，hal 一份、用户一份，text 76。同一个单元里的包装函数被 -O2 内联进了向量函数，判挂起与清挂起都进了向量函数体内。跨单元的用户回调只能 bl。
- gen1a3，三个编译单元，照真源的形状分成 it.c、hal 库、用户三份，text 60。向量函数一条 b.w 尾跳进包装函数，包装函数一条 bl 进用户强符号，一次中断两跳，两跳都因为跨了编译单元。

尾调用这个词在这儿出现了。白话的意思是：函数末尾的一次转手，编译器拿跳替代了调用，省下的是一次压栈出栈，咱们看到的 b.w 就是它。咱们把 gen1a3 的两跳原样贴上：

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

地址 8 处的 `bx lr` 也值得看：那是被顶掉的弱空体，已经没人引用了，可它还是随固件出厂了。两单元的 gen1a 里同样有它，位置挪到了 0 地址。gen1a 的向量函数把判挂起、清挂起、bl 三样都收进了自己的体内，咱们在它的反汇编里数得出来。用户回调的那头也值得看：进门的 switch 在反汇编里被收成了一次 `cmp r0, #1`。蒸馏里的线只有一条，一次比较就把它分完了。真到多条线的场合，进门问来路的负担就回来了。

### 不开 gc-sections，死弱随固件出厂

死弱占着的 4 个字节还有下文。E1 补了一组对照：同样的两单元源，咱们加上 `-ffunction-sections` 和 `--gc-sections`，把 `EXTI0_IRQHandler` 定成回收的根，text 从 76 降到了 44。被收走的是死弱定义和没人调用的包装函数体，活下来的是向量函数（判挂起、清挂起已经内联在它体内）加用户强符号。口径咱们说明白：拿到的数字是蒸馏源的实测。真源工程的 gc-sections 开不开、能收回来多少，得看工程自己的形状，咱们不把 44 外推过去。弱符号一代的检查时机，咱们看到这儿就清楚了：链接器把上场的名额定了，顶掉的时候零提示。签名对不上的时候链接器一声也不吭，非得等回调真的被调起来才露馅。全部的约束，到头来就只剩同名了。
## 注册指针：运行期填表

第二种接法在 EXTI 自己的模块里。`stm32f1xx_hal_exti.h` 的 `EXTI_HandleTypeDef` 是个 handle 结构，咱们看它的字段<RefLink :id="3" preview="STMicroelectronics, stm32f1xx_hal_exti.h" />：

```c
   ```c
   {
     uint32_t Line;                    /*!<  Exti line number */
     void (* PendingCallback)(void);   /*!<  Exti pending callback */
```

`Line` 之外咱们多看到了一个 `PendingCallback`，它的类型是函数指针。函数指针的白话：存函数地址的变量，调用它就是跳到存的那个地址去执行。回调的连接从链接期的同名顶掉，变成了运行期往这个字段里填地址。身份的枚举 `EXTI_CallbackIDTypeDef` 只有一档 `HAL_EXTI_COMMON_CB_ID`，F1 的头文件里就这么一个身份。

### 注册与调用，检查靠返回值

注册的入口是 `HAL_EXTI_RegisterCallback`，主体咱们搬过来：

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

咱们接着看注册的内部。写指针之前有一道 `switch (CallbackID)` 验身份：认识的身份写槽、返回 `HAL_OK`，不认识的走 default、返回 `HAL_ERROR`。检查从链接期挪到了运行期，凭的全是返回值。调用侧 `HAL_EXTI_IRQHandler` 的主体也搬来：

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

咱们把它的次序念一遍：算 mask、判 PR、清 PR 的三步走完，然后 `if (hexti->PendingCallback != NULL)` 通过了才调。真源的调用前带着 NULL 检查，E1 的蒸馏版 gen1b 没带这一步，咱们如实标出差别：蒸馏比的是调用点的形态，真源多一道空指针的防御。

### bss 里的指针表

gen1b 的实测是 text 64、bss 68。bss 的构成咱们摊开：16 槽的指针表占了 64 字节，timestamp 占了 4。表住进了 RAM，是这一代看得见的代价。注册与调用两头的指令，咱们按日志念：判 PR、清 PR 的两步走完，取表基址的是 `ldr r2, [pc, #8]`，取槽的是 `ldr r2, [r2, #4]`，跳转的差事落在 `bx r2` 上，两级取址之后才算够到了回调。注册侧的字面量池里装着 `0x00000001`，装的就是 `on_line0` 的地址或上 1 的结果：本体的地址是 0，或上 1 得到的还是 1。地址的最低一位不算是地址，它标的是 Thumb 状态。Cortex-M3 跑的指令集叫 Thumb，函数地址的最低一位被拿来当状态标记，所以 `bx` 带着它跳，跳过去之后按的就是 Thumb 指令集，反汇编里是肉眼可见的。GPIO 通道与 EXTI handle 通道在同一份 F1 驱动里是并存的，它们是平行的两条路，咱们不说谁取代谁。开关的口径也只按眼前的源码说：在 F1 的 EXTI 驱动源里，`HAL_EXTI_RegisterCallback` 没有独立的编译开关，全文件只有整模块的 `HAL_EXTI_MODULE_ENABLED`，别的咱们不外推。

## 类型参数：合同在编译期签好

### 概念本体

libestdx 的接法咱们从概念本体看起<RefLink :id="2" preview="Awesome-Embedded-Learning-Studio, libestdx" />。`irq_base.hpp` 的全文只有 11 行，咱们要的核心就是其中的几行：

```cpp
   template <typename H>
   concept IrqHandler = requires {
       H::operator()();
   };
```

concept 是 C++20 起的编译期判断题，给类型出一道是或不是的题，答案在编译期就算完了。`IrqHandler` 问的只有一件事：H 这个类型能不能交出静态的 `operator()`。上一篇的原话是“处理器要满足 `irq::IrqHandler` 的要求，能提供静态的 `operator()` 就算过”，咱们不重讲它的出身，这里只看它在接法里的位置。

### irq() 与用户侧

`exti.hpp` 里 `Exti` 的 `irq()` 咱们原样搬：

```cpp
   static void irq() {
       if (EXTI->PR & Pin::mask) {
           EXTI->PR = Pin::mask;
           Handler::operator()();
       }
   }
```

咱们照着源码念它的次序：“判 PR、清 PR、调 `Handler::operator()()`”。`Handler` 是 `Exti` 的模板参数，回调是谁在实例化的时候就定下了，查表与问来路的动作在中断里都没有。用户侧就是 07 示例的那几行：

```cpp
   ```cpp
   struct OnTouch {
       static void operator()() { edge_queue.push(HAL_GetTick()); }
   };
   using KeyEvent = estdx::stm32f1::Exti<KeyPin, estdx::stm32f1::ExtiEdge::Rising,
   ```cpp
   extern "C" void EXTI0_IRQHandler() {
       KeyEvent::irq();
```

`OnTouch` 把推队列写成了一个类型，`KeyEvent` 把引脚、边沿、处理器拧在了一行别名里，向量函数就剩下了三行。上一篇咱们看它们是为了装配，本篇咱们再看一遍，看到的是另一件事：回调的连接在这里既不靠链接器的同名顶掉，也不靠运行期的表，它已经是类型的一部分了。

### gen2：通篇无 call

E1 的 gen2 是拿 `-std=c++23` 编出来的，咱们拿到的 text 是 28。咱们数下来 10 条指令里没有一个 call，判 PR、清 PR、写时间戳的活儿全落在了向量函数一个函数体里：

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

静态 `operator()` 是 C++23 起才有的语法，咱们因此把本篇的 cpp_standard 标成 20 与 23 两档，概念是 C++20 的，静态 `operator()` 是 C++23 的。接法不合式的时候 concept 在编译期就把实例化拦下，合式了，调用点就成了直接调用乃至内联，链路上也就没有转手了。

## 三种接法对照与共享向量的分派

| | 弱符号 | 注册指针 | 类型参数 |
|---|---|---|---|
| 回调放在哪 | 用户文件里的同名强符号 | handle 的 PendingCallback 槽 | Exti 的模板参数 Handler |
| 检查时机 | 链接期，顶掉零提示 | 运行期，靠返回值 | 编译期，concept 拦实例化 |
| 调用点形态 | 跨单元 bl，或尾跳加 bl | 两级取址后 bx r2 | 直接调用乃至内联 |
| text 实测 | 76（两单元）、60（三单元） | 64 | 28 |
| bss 实测 | 4 | 68 | 4 |
| 共享向量的分派 | 进门 switch(GPIO_Pin) 问来路 | 槽随 handle 走，一线一份 | 每线一个实例，编译期摊平 |

共享向量的事咱们单独说，表里的数字都出自 E1 的同一批蒸馏，44 的 gc 对照只对两单元的蒸馏源负责。分线的映射表上一篇给过，这里只看它对接法形状的影响。线 0 是独享向量的，向量函数直接绑上 `KeyEvent` 就成了，问来路的环节全省了。5 到 9 共用一个向量的时候，弱符号那代就没这么省心：回调的名字只有一个，进门的 `switch (GPIO_Pin)` 只能问清来路再分派，gen1a_user 的进门 switch 就是它。estdx 这边每条线都各自实例化了一份，分派在编译期就摊平了，所以 gen2 里没有 switch，两份蒸馏互证的就是这一点。注册指针那代的槽跟着 handle 走，一条线配的是一份 handle，各槽装各的回调。

## 收尾

按键站的第二篇到这儿讲完。三种接法您都看过了：弱符号把检查交给了链接器，注册指针把它挪到运行期的返回值上，concept 把它压进编译期的实例化。下一篇咱们拆 `claim_line`，就是上一篇表里 `__builtin_trap()` 那一行背后的位图机关。

## 参考资源

<ReferenceCard title="参考文献">
  <ReferenceItem
    :id="1"
    author="GCC"
    title="GCC Manual — Common Function Attributes（weak）"
    url="https://gcc.gnu.org/onlinedocs/gcc/Common-Attributes.html"
    chapter="weak attribute: overriding symbol must have the same type as the weak symbol"
  />
  <ReferenceItem
    :id="2"
    author="Awesome-Embedded-Learning-Studio"
    title="libestdx（pin 8026841）"
    :year="2026"
    url="https://github.com/Awesome-Embedded-Learning-Studio/libestdx"
    chapter="irq_base.hpp, exti.hpp, examples/07_button_events"
  />
  <ReferenceItem
    :id="3"
    author="STMicroelectronics"
    title="stm32f1xx_hal_gpio.c 与 stm32f1xx_hal_exti.c（vendored 源）"
    chapter="HAL_GPIO_EXTI_Callback 弱符号通道与 HAL_EXTI_RegisterCallback 注册指针通道"
  />
</ReferenceCard>
