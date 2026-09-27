---
title: "从能亮到用得顺：toggle 的讲究与下一站的门"
description: "LED 站收官：读 HAL_GPIO_TogglePin 的实现看单次写 BSRR 双半字怎么规避 ODR 读改写竞争、读与写之间的窗口为什么还留着；用 HAL 源码注释原文证实 F1 的怪设计——输入上拉下拉竟由 ODR 决定、F4 起 PUPDR 才独立成寄存器；收拢三层楼梯各自解决了什么（宏、HAL、模板与 concept）和攒下的验证工具箱（size、nm、objdump、Renode 采样），给 02-button 站开门：输入世界、机械抖动与时间维度"
chapter: 1
order: 9
tags:
  - stm32f1
  - beginner
  - 嵌入式
  - 寄存器
difficulty: beginner
platform: stm32f1
---

# 从能亮到用得顺：toggle 的讲究与下一站的门

九篇走完了。咱们从一声 `Led::on()` 出发，一路下到了 BSRR，又爬回了类型系统，把配置变成了编译期的事实。收尾的这一篇，咱们把 `toggle` 这个"看起来最简单"的操作掰开，看看它藏着什么讲究。再顺路把 04 篇埋的那个伏笔挖出来：F1 的输入上下拉，居然是由 ODR 决定的。攒下的家当，咱们也一起收拢，下一站的门，顺手就开了。

## toggle：两次动作一次写

咱们顺着 `estdx` 的 `LED::toggle()` 往下追：它转发到 `Pin::toggle()`，最后落到的是 `HAL_GPIO_TogglePin`（`stm32f1xx_hal_gpio.c` L487）。去掉默认编译为空的参数检查（07 篇拆过它的真身），剩下的主体，咱们一眼就能看完：

```c
void HAL_GPIO_TogglePin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin)
{
  uint32_t odr;

  odr = GPIOx->ODR;
  /* Set selected pins that were at low level, and reset ones that were high */
  GPIOx->BSRR = ((odr & GPIO_Pin) << GPIO_NUMBER) | (~odr & GPIO_Pin);
}
```

咱们第一眼看上去平平无奇，多看了两眼，才看出它的真功夫。最朴素的做法是读 ODR、取反、写回，也就是咱们在 04 篇拆过的读改写三件套：写回 ODR 的那一瞬间，可能把别人的并发写入覆盖掉。这里换了个思路，把"翻转"翻译成**一次 BSRR 写入**：低半字（`~odr & GPIO_Pin`）放"当前是低、需要抬高"的位，高半字（`(odr & GPIO_Pin) << 16`）放"当前是高、需要拉低"的位。咱们只写一次，两个动作就都齐了。04 篇咱们讲过 BSRR 高低半字怎么分工，到了这里，这套分工被 HAL 用成了顺手的设计模式。

<Anim id="f103-toggle-bsrr" />

但有一处咱们得挑明：`odr = GPIOx->ODR` 这行读，和后面的写之间**还有一个窗口**。要是中断在两步之间翻转了同一个脚，ISR 的翻转就会被这一次的写覆盖，丢失更新就这么发生了。想严格免疫的话，咱们得在读操作前把中断关掉，等写完了再开。其实，本站在场的只有主循环一个角色，够用了。等中断正式进了场（第三站的 UART 就是中断驱动的），就轮到这个窗口当正餐了。咱们把这招收好，问题呢，到时候咱们再来认领。

## 挖出伏笔：输入的上下拉，旋钮在 ODR 上

04 篇的组合表里，"上拉/下拉输入"那一行留了句备注：F1 的上拉还是下拉，由 ODR 的对应位决定（"第九篇见"）。05 篇讲上下拉电阻的时候，咱们又从电路那一头看过一遍，不过那会儿，手里的依据只有手册的说法。现在咱们已经能走读 HAL 了，换官方库的源码来当证据，直接看 `HAL_GPIO_Init` 输入分支的原文（L252-L265）：

```c
else if (GPIO_Init->Pull == GPIO_PULLUP)
{
    config = GPIO_CR_MODE_INPUT + GPIO_CR_CNF_INPUT_PU_PD;

    /* Set the corresponding ODR bit */
    GPIOx->BSRR = ioposition;
}
else /* GPIO_PULLDOWN */
{
    config = GPIO_CR_MODE_INPUT + GPIO_CR_CNF_INPUT_PU_PD;

    /* Reset the corresponding ODR bit */
    GPIOx->BRR = ioposition;
}
```

官方的注释写得明明白白：`Set the corresponding ODR bit`。咱们把它翻译过来：**输出数据寄存器在输入模式下，兼职当了上拉/下拉的选择开关**。ODR 的对应位写 1 选上拉，写 0 选的是下拉。为什么会有这样的设计？F1 是 ST 较早的 Cortex-M3 产品，寄存器的位预算紧张，这个复用是历史留下来的包袱。到了 F4，PUPDR 独立成了寄存器，输出和上下拉各管各的，这个包袱就只剩 F1 还背着了。

这对咱们写代码的人来说，意味着什么？**直接操作 ODR 的老习惯，在输入模式下会悄悄改变引脚的偏置**。比如咱们把 04 篇那套裸寄存器流程搬过来配输入，配完又顺手写了 `ODR = 0`：上拉变成了下拉，电平读数整个都反了，而代码里没有一个字提示您刚动了输入的设置。estdx 把这层知识封在 `Gpio` 的模板参数里（`Gpio<GpioPort::A, GPIO_PIN_0, Input, GpioPull::Down>`），HAL 帮您在 `GPIO_Init` 里写对 ODR。但您是从地砖层爬上来的人，再看到 `BSRR = ioposition` 出现在输入配置的代码里，想必您能会心一笑：它选的是上拉，跟点灯没什么关系。

## 回望：三层楼梯各解决了什么

本站搭起来的楼梯，三级各自解决了哪些问题？咱们用一张表来汇总（清单见 06 篇）：

| | C 宏（06 篇） | HAL（07 篇） | 模板 + concept（08 篇） |
|---|---|---|---|
| 问题 1 无类型 | 裸数字 | 枚举挡拼错 | 类型即配置 |
| 问题 2 时钟外置 | 人肉纪律 | 宏照旧外置 | `if constexpr` 自动选电闸 |
| 问题 3 组合无约束 | 运行时都不炸 | 运行时才不对劲 | 编译期报错 |
| 问题 4 不可调试 | 读不懂的宏 | 真函数真符号 | 类型可追 |
| 问题 5 复制粘贴 | 加脚全靠抄 | 结构体复用 | 派生不复制 |
| 代价 | 零抽象，全债 | ~1KB 通用机器 | 代码零开销，路径按选择付 |

不过，比答案更值钱的，是**验证的手段**。咱们这一路攒下的工具箱，后面的每一站都得接着用：

- `arm-none-eabi-size`：咱们看到的三列数字，就是固件的家底清单。
- `arm-none-eabi-nm --size-sort`：咱们用它把尺寸差额归因到函数名，谁大谁小有名有姓。
- `arm-none-eabi-objdump -d`：每个抽象最后都得在这里现出真身，咱们连 `bl` 多一跳都要看见。
- Renode 采样（`sysbus ReadDoubleWord`）：咱们不开机也照样能看，时钟没开时写入吞不吞、ODR 翻没翻、复位值是几。
- 故意写错的编译实验：咱们故意把代码写错，练着读 concept 的报错：从 `template constraint failure` 一路读到 `evaluated to 'false'`。

咱们在 LED 站，已经两次撞见**模拟器的保真度边界**：03 篇的 Renode 不模拟时钟门控，04 篇的 CRH 复位值与手册不符。这两回值得咱们单独记下。用模拟器证明"机制如此"倒是很快，但"实际板子上也如此"得由硬件说了算。仿真过了不等于万事大吉，后面的每一站，咱们都得绷着这根弦。

## 下一站的门：输入的世界

本站的 LED 是**输出**：咱们写什么，引脚就照着做什么。下一站 02-button 把方向倒了过来：引脚成了**输入**，世界的电平流进来，轮到咱们读。门的钥匙，本篇已经递到您手上了：

- 配置的路数您已经走过一遍（开钟、CRL/CRH 四位、`Gpio<Port, Pin, Input, Pull>`），只是 CNF 配成 `10`（上拉/下拉输入）。
- 上拉下拉的旋钮在 ODR。所以"按键一头接地、配输入上拉、读 0 就是按下"这条最经典的电路，您已经知道它每一层为什么。
- 但按键是**机械触点**：按下和松开的瞬间，金属片要弹跳几毫秒到几十毫秒，您在 IDR 上读到的不是干净的一次跳变，是一串毛刺。**时间维度**第一次进场：消抖、采样、状态机，这些 LED 站用不上的手艺，是下一站的正餐。

`Gpio` 的 concept 体系也早就留好了门：`GPIOInputPin` 要求 `level() -> bool`。读一个引脚的电平，在类型层面是和 `set()`/`reset()` 平级的一等公民。楼已经搭好了，就等您推门。

## 您来动手

1. 您把 `03_led` 的 `main` 从 `on`/`off` 改成 `Led::toggle()`，重新编译后对比 `main` 的反汇编，找到 `HAL_GPIO_TogglePin`，看它读 ODR 加写 BSRR 的两步在固件里怎么呈现。
2. 您来做一回丢更新实验：在 Renode 里手工写 ODR 之后再调 `TogglePin`（或跑改版固件），核对翻转结果和您手写的 ODR 是否一致。
3. 您通读 `HAL_GPIO_Init` 的输入分支（L238-L266），找出 `GPIO_MODE_IT_RISING` 这些枚举和 `GPIO_MODE_INPUT` 走同一段配置代码的证据，想想为什么输入模式和中断模式是"一家人"。
4. 您做一回预习：翻 RM0008（就是 ST 的 STM32F1 参考手册，01 篇咱们翻过的）的 IDR（端口输入数据寄存器）一节，看看它和 ODR 的位定义有什么差别，带着答案去下一站。

## 欸欸！自查一下再走

- 您说说，`HAL_GPIO_TogglePin` 怎么用一次写完成翻转？高半字放的是什么，低半字放的是什么？
- toggle 的读与写之间还有什么窗口？什么场景下咱们必须处理它？
- F1 的输入上拉，ODR 对应位该写 0 还是 1？这个设计到了 F4 为什么就消失了，您还有印象吗？
- 三层楼梯各自解决了哪几个问题？哪一个只有类型系统能解决，您能指出来吗？
- 咱们本站攒下的五个验证工具，分别能回答什么问题？哪个工具给出的答案需要打折扣？

<ReferenceCard title="参考文献">
  <ReferenceItem
    :id="1"
    author="STMicroelectronics"
    title="RM0008 — STM32F103 Reference Manual"
    :year="2018"
    url="https://www.st.com/resource/en/reference_manual/cd00171190.pdf"
    chapter="Table 20-25: Port bit configuration table; Section 9.3.4 IDR"
  />
  <ReferenceItem
    :id="2"
    author="STMicroelectronics"
    title="AN4488 — Getting started with STM32F4 GPIO hardware"
    :year="2016"
    url="https://www.st.com/resource/en/application_note/dm00123199.pdf"
    chapter="Section 3: PUPDR — F4 独立上下拉寄存器对照"
  />
  <ReferenceItem
    :id="3"
    author="STMicroelectronics"
    title="stm32f1xx_hal_gpio.c — GPIO HAL Driver Source"
    :year="2016"
    url="https://github.com/STMicroelectronics/stm32f1xx_hal_driver/blob/master/Src/stm32f1xx_hal_gpio.c"
    chapter="L252-L265 Input pull via ODR; L487-L499 TogglePin"
  />
</ReferenceCard>
