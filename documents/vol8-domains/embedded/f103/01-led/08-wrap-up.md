---
title: "从能亮到用得顺：toggle 的讲究与下一站的门"
description: "LED 站收官：读 HAL_GPIO_TogglePin 的实现看单次写 BSRR 双半字怎么规避 ODR 读改写竞争、读与写之间的窗口为什么还留着；用 HAL 源码注释原文坐实 F1 的怪设计——输入上拉下拉竟由 ODR 决定、F4 起 PUPDR 才独立成寄存器；收拢全站三层楼梯的记分牌（宏、HAL、模板与 concept 各拆了哪几颗雷）和攒下的验证工具箱（size、nm、objdump、Renode 采样），给 02-button 站开门：输入世界、机械抖动与时间维度"
chapter: 1
order: 8
tags:
  - stm32f1
  - beginner
  - 嵌入式
  - 寄存器
difficulty: beginner
platform: stm32f1
---

# 从能亮到用得顺：toggle 的讲究与下一站的门

八篇走完，咱们从一声 `Led::on()` 下到了 BSRR 的电线，又爬回类型系统，把配置焊成了编译期的事实。收尾这一篇干三件事：把 `toggle` 这个"看起来最简单"的操作掰开看它藏着的讲究；兑现 04 篇埋的伏笔——F1 输入上下拉由 ODR 决定的怪设计；最后收拢全站的家当，给下一站开门。

## toggle：两次动作一次写

`estdx` 的 `LED::toggle()` 转发到 `Pin::toggle()`，再往下是 `HAL_GPIO_TogglePin`（`stm32f1xx_hal_gpio.c` L487，全文就这么多）：

```c
void HAL_GPIO_TogglePin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin)
{
  uint32_t odr;

  odr = GPIOx->ODR;
  /* Set selected pins that were at low level, and reset ones that were high */
  GPIOx->BSRR = ((odr & GPIO_Pin) << GPIO_NUMBER) | (~odr & GPIO_Pin);
}
```

第一眼平平无奇，细看有真功夫。朴素做法是读 ODR、取反、写回——04 篇拆过的读改写三件套，写 ODR 的那一瞬间可能踢飞并发写入。这里的做法是把"翻转"翻译成**一次 BSRR 写入**：低半字（`~odr & GPIO_Pin`）放"当前是高、需要拉低"的位；高半字（`(odr & GPIO_Pin) << 16`）放"当前是低、需要抬高"的位。一次写，两个动作，03 篇讲的 BSRR 高低半字分工在这里被用成了顺手的设计模式。

但账要算干净：`odr = GPIOx->ODR` 这行读和后面的写之间**还有一个窗口**。如果中断在两步之间翻转了同一个脚，ISR 的翻转会被这一次的写覆盖——丢失更新。要严格免疫，得在读之前关中断、写完再开。本站只有主循环一个角色，够用；等中断正式进场（EXTI 那站），这个窗口就是正餐。工具先收好，问题到时候认领。

## 兑现伏笔：输入的上下拉，旋钮在 ODR 上

04 篇讲上下拉电阻时留了个话头：F1 的输入模式配成上拉/下拉，**选择开关不在某个独立的 PUPDR 寄存器——在 ODR 上**。当时空口无凭，现在咱们有走读 HAL 的能力了，直接看 `HAL_GPIO_Init` 输入分支的原文（L252-L265）：

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

官方注释写得明明白白：`Set the corresponding ODR bit`。**输出数据寄存器，在输入模式下兼职当了上拉/下拉的选择开关**——ODR 对应位写 1 选上拉、写 0 选下拉。为什么会有这种设计？F1 是 ST 较早的 Cortex-M3 产品，寄存器位预算紧张，复用是历史包袱；到 F4，PUPDR 独立成寄存器，输出归输出、上下拉归上下拉，这个包袱 F1 独一份。

对写代码的人意味着什么：**直接操作 ODR 的老习惯，在输入模式下会悄悄改变引脚的偏置**。比如 03 篇那套裸寄存器流程，如果配完输入后又顺手 `ODR = 0`，上拉就变成了下拉——电平reading 整个反掉，而代码里没有一个字提示你刚动了输入的设置。estdx 把这层知识封在 `Gpio` 的模板参数里（`Gpio<GpioPort::A, GPIO_PIN_0, Input, GpioPull::Down>`），HAL 帮你在 `GPIO_Init` 里写对 ODR——但你是从地砖层爬上来的人，再看到 `BSRR = ioposition` 出现在输入配置的代码里，应该能会心一笑：那不是点亮，是选上拉。

## 回望：三层楼梯的记分牌

这一站搭的楼梯，三级各拆了什么雷，一张表收口（雷的清单见 05 篇）：

| | C 宏（05 篇） | HAL（06 篇） | 模板 + concept（07 篇） |
|---|---|---|---|
| 雷 1 无类型 | 裸数字 | 枚举挡拼错 | 类型即配置 |
| 雷 2 时钟外置 | 人肉纪律 | 宏照旧外置 | `if constexpr` 焊死 |
| 雷 3 组合无约束 | 运行时都不炸 | 运行时才不对劲 | 编译期讣告 |
| 雷 4 不可调试 | 无字天书 | 真函数真符号 | 类型可追 |
| 雷 5 复制粘贴 | 加脚全靠抄 | 结构体复用 | 派生不复制 |
| 代价 | 零抽象，全债 | ~1KB 通用机器 | 代码零开销，路径按选择付 |

比结论更值钱的是**验证的手段**，这一路攒下的工具箱才是后面每一站都要用的：

- `arm-none-eabi-size`——三行数字，固件的地基账单；
- `arm-none-eabi-nm --size-sort`——差价归因到函数名，谁大谁小有名有姓；
- `arm-none-eabi-objdump -d`——每个抽象最后都得在这对账，`bl` 多一跳都要看见；
- Renode 采样（`sysbus ReadDoubleWord`）——不开机的看表：断钟写入吞不吞、ODR 翻没翻、复位值是几；
- 故意写错的编译实验——concept 报错从 `template constraint failure` 读到 `evaluated to 'false'`。

还有一条元教训值得单独记账：这一站两次撞见**模拟器的保真度边界**（02 篇 Renode 不模拟时钟门控、03 篇 CRH 复位值与手册不符）。模拟器证明"机制如此"很快，但"真机也如此"的最终裁决权永远在硬件——仿真过了不等于万事大吉，这根弦后面每一站都得绷着。

## 下一站的门：输入的世界

这一站的 LED 是**输出**：咱们写，引脚听。下一站 02-button 翻个方向：引脚是**输入**，世界的电平流进来，咱们读。门的钥匙本篇已经递到手上了：

- 配置的路数全一样（开钟、CRL/CRH 四位、`Gpio<Port, Pin, Input, Pull>`），只是 CNF 配 `10`（上拉/下拉输入）；
- 上拉下拉的旋钮在 ODR——所以"按键一头接地、配输入上拉、读 0 就是按下"这条最经典的电路，你已经知道它每一层为什么；
- 但按键是**机械触点**：按下和松开的瞬间，金属片要弹跳几毫秒到几十毫秒，IDR 上读到的不是干净的一次跳变，是一串毛刺。**时间维度**第一次进场——消抖、采样、状态机，这些 LED 站用不上的手艺，是下一站的正餐。

`Gpio` 的 concept 体系也早就留好了门：`GPIOInputPin` 要求 `level() -> bool`。读一个引脚的电平，在类型层面是和 `set()`/`reset()` 平级的一等公民。楼已经搭好，就等你推门。

## 您来动手

1. 把 `03_led` 的 `main` 从 `on`/`off` 改成 `Led::toggle()`，重新编译后对比 `main` 的反汇编——找到 `HAL_GPIO_TogglePin`，看它读 ODR 加写 BSRR 的两步在固件里怎么呈现；
2. 做丢更新实验：在 Renode 里手工写 ODR 之后再调 `TogglePin`（或跑改版固件），核对翻转结果和你手写的 ODR 是否一致；
3. 通读 `HAL_GPIO_Init` 的输入分支（L238-L266），找出 `GPIO_MODE_IT_RISING` 这些枚举和 `GPIO_MODE_INPUT` 走同一段配置代码的证据——想想为什么输入模式和中断模式是"一家人"；
4. 预习：翻 RM0008 的 IDR（端口输入数据寄存器）一节，看看它和 ODR 的位定义有什么差别——带着答案去下一站。

## 欸欸！自查一下再走

- `HAL_GPIO_TogglePin` 怎么用一次写完成翻转？高半字放的是什么、低半字放的是什么？
- toggle 的读与写之间还有什么窗口？什么场景下必须处理它？
- F1 的输入上拉，ODR 对应位该写 0 还是 1？这个设计 F4 为什么消失了？
- 三层楼梯各拆了哪几颗雷？哪颗雷只有类型系统能拆？
- 这一站攒下的五个验证工具，分别能回答什么问题？哪个工具的结论需要打折扣？

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
