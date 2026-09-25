---
title: "拆开 HAL 的包装纸：GPIO_Init 在替咱们写哪些位"
description: "给 HAL 的 GPIO 层上秤称重拆包：1088 字节差价里 HAL_GPIO_Init 独占 1060，而两份 main 只差 8 字节——初始化付大头、运行路径几乎平手；逐行走读 HAL_GPIO_Init 的逐位循环、Mode 到 CNF/MODE 的 switch 翻译、与 03 篇同款的 (position-8)×4 半段分家算术，再看 MODIFY_REG 宏礼服下的读改写三件套、WritePin 一个 BSRR 两副面孔的高半字技巧、assert_param 默认缺席的真相——最后结账：五颗雷 HAL 拆了两颗，时钟那颗还埋着"
chapter: 1
order: 6
tags:
  - stm32f1
  - beginner
  - 嵌入式
  - 寄存器
difficulty: beginner
platform: stm32f1
---

# 拆开 HAL 的包装纸：GPIO_Init 在替咱们写哪些位

上一篇把 C 宏的账算清了：能跑，但五颗雷。HAL 是 ST 官方给出的另一条路——同样是"把寄存器包起来"，官方的包装纸厚得多，也贵得多。这一篇的活儿不是站队"HAL 好还是不好"，是**拆包标价**：包装纸里卷了什么、哪些是干货、哪些是咱们用不到还躲不掉的，每一项都过秤。

## 先称重：1088 字节的账单

上秤的是咱们认识的两份固件：`01_blinky`（HAL 起家、estdx 收尾）和 `01_register_led`（裸寄存器直书）。先把体积摆出来：

```text
$ arm-none-eabi-size blinky register_led
   text    data     bss
   3468      12       4    blinky        (HAL + estdx)
   2380      12       4    register_led  (裸寄存器)
   ─────────────────────────────
   差价     1088 字节
```

差价花在哪？`arm-none-eabi-nm --size-sort` 把符号按体积排队，答案直接跳出来：

| 符号 | 体积 | 谁带着 |
|------|------|--------|
| `HAL_GPIO_Init` | **1060 字节** | 只有 blinky 有 |
| `HAL_RCC_OscConfig` | 1004 字节 | 两边都有 |
| `HAL_RCC_ClockConfig` | 384 字节 | 两边都有 |
| `HAL_SYSTICK_Config` + NVIC 零头 | ~80 字节 | 只有 blinky 有 |
| `main` | **144 vs 136** | 差 8 字节 |

两个结论。第一，差价的**绝对主力是 `HAL_GPIO_Init` 这台配置机器**，一家占去 1060 字节；第二，更有意思：两份 `main` 几乎一样大——初始化付完钱之后，运行路径上 HAL 系和裸寄存器系打了个平手。还有个容易看漏的细节：`register_led` 身上也挂着 `OscConfig` 和 `ClockConfig`——它同样调用了 `HAL_Init` 和 `SystemClock_Config`，所以这 1088 字节是**扣除共同开销之后的净差**。买没买 `HAL_GPIO_Init`，就是两家唯一的结构性区别。

## HAL_GPIO_Init 逐行拆

1060 字节买来的这台机器，进料口是一个结构体：

```cpp
GPIO_InitTypeDef gpio{};
gpio.Pin   = GPIO_PIN_13;
gpio.Mode  = GPIO_MODE_OUTPUT_PP;
gpio.Speed = GPIO_SPEED_FREQ_LOW;
HAL_GPIO_Init(GPIOC, &gpio);
```

对照 05 篇的宏封装：`13`、`推挽 2MHz` 这些裸数字，现在进了有名字的枚举字段——拼错 `GPIO_MODE_OUTPUT_PP`，编译器当场翻脸。雷 1，拆了。

机器内部（`stm32f1xx_hal_gpio.c` L178 起）干的第一件事，是**逐位扫描**：

```c
while (((GPIO_Init->Pin) >> position) != 0x00u)
{
    ioposition = (0x01uL << position);
    iocurrent  = (uint32_t)(GPIO_Init->Pin) & ioposition;
    if (iocurrent == ioposition)
    {
```

`Pin` 是掩码不是脚号——写 `GPIO_PIN_13 | GPIO_PIN_14` 就能一次配两个脚，循环逐位处理。这就是"通用性"的第一笔成本：哪怕只配一个脚，循环、判断、迭代 machinery 一个不少。接下来是 `Mode` 到四位的翻译，一个 switch：

```c
case GPIO_MODE_OUTPUT_PP:
    config = GPIO_Init->Speed + GPIO_CR_CNF_GP_OUTPUT_PP;  // Speed 是 0/1/2/3,加在 CNF 基数上
    break;
```

`Speed + CNF` 这个加法有点门道：目标四位的布局是 `CNF<<2 | MODE`，`Speed` 枚举值本身就是 0-3 的 MODE 位，`GPIO_CR_CNF_GP_OUTPUT_PP` 恰好等于 `0x0`（推挽输出）左移两位——加法即拼位。然后是咱们最眼熟的两行：

```c
configregister = (iocurrent < GPIO_PIN_8) ? &GPIOx->CRL : &GPIOx->CRH;
registeroffset = (iocurrent < GPIO_PIN_8) ? (position << 2u) : ((position - 8u) << 2u);
```

**半段分家、四位偏移**——03 篇手推的那套算术，官方原装同款，连 `(position - 8) << 2` 的写法都跟咱们的 `(13-8)×4` 一模一样。最后落锤：

```c
MODIFY_REG((*configregister), ((GPIO_CRL_MODE0 | GPIO_CRL_CNF0) << registeroffset),
           (config << registeroffset));
```

`MODIFY_REG` 在 `stm32f1xx.h` L189 的真身：

```c
#define MODIFY_REG(REG, CLEARMASK, SETMASK) \
    WRITE_REG((REG), (((READ_REG(REG)) & (~(CLEARMASK))) | (SETMASK)))
```

剥掉三层宏皮：**读回来、清掩码、或新值、写回去**——01 篇反汇编里见过的三件套，穿了件宏礼服。包装纸到这里已经拆穿大半：`HAL_GPIO_Init` 没有任何咱们没学过的魔法，它是"03 篇的手艺 + 逐位循环 + switch 翻译 + EXTI 分支"的总装线。EXTI（外部中断）那条分支 L288 往下还有几十行，本站不进门——但它占的体积您已经付了。

## WritePin：一个寄存器，两副面孔

运行路径上的 `HAL_GPIO_WritePin`（L465-L479）短得可以全文照抄：

```c
void HAL_GPIO_WritePin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin, GPIO_PinState PinState)
{
  assert_param(IS_GPIO_PIN(GPIO_Pin));
  assert_param(IS_GPIO_PIN_ACTION(PinState));

  if (PinState != GPIO_PIN_RESET)
    GPIOx->BSRR = GPIO_Pin;
  else
    GPIOx->BSRR = (uint32_t)GPIO_Pin << 16u;   // 高半字 = 复位区
}
```

两处可看。一是那个 `<< 16`：BSRR 低半字置位、高半字复位（03 篇的账），HAL 不用 BRR 寄存器，把同一个 BSRR 的两副面孔都用上了——省一个外设地址的寻址，代价是复位时多一次移位。二是那个 `if`：`PinState` 是运行时变量，编译器没法砍掉分支，每次调用都带着一次比较和跳转——这就是上节"main 只差 8 字节"背后的细节：分支开销在，但很便宜。真正的原子性没打折：两条路径最终都是**单条写**，中断来了也插不进脚。

## assert_param：默认缺席的保安

开头两行 `assert_param` 常被当成 HAL 的卖点——参数合法性检查。它的真身在 `stm32_assert_template.h` L42-L46：

```c
#define assert_param(expr) ((expr) ? (void)0U : assert_failed((uint8_t *)__FILE__, __LINE__))
// ...
#define assert_param(expr) ((void)0U)      // ← 默认:这行
#endif /* USE_FULL_ASSERT */
```

默认配置（不开 `USE_FULL_ASSERT`）下，`assert_param` 展开成 `(void)0U`——**编译后一个字节都不剩**。那个三目运算符版的真检查，要显式打开宏才有，而且 `assert_failed` 还得您自己实现。所以"HAL 自带参数检查"这句话的正确读法是：**检查逻辑写好了，出厂默认关着**。不是坏事（固件里塞满检查才是坏事），是消费须知。

## 结账：五颗雷的拆除进度

拿着 05 篇的五颗雷清单回访 HAL：

| 雷 | HAL 的答卷 |
|----|-----------|
| 1. 无类型 | **拆了**——`GPIO_InitTypeDef` + 枚举，拼错编译失败 |
| 2. 时钟外置 | **还在**——`HAL_GPIO_Init` 不管开钟，还是得您自己念 `__HAL_RCC_GPIOC_CLK_ENABLE()`，忘念照样幽灵 bug |
| 3. 正交维度无约束 | **半拆**——枚举挡住拼错，挡不住非法组合（比如给 PC13 塞 50MHz，运行时才发现不对劲） |
| 4. 不可调试 | **缓解**——真函数、真符号，断点和源码对得上 |
| 5. 复制粘贴 | **缓解**——一个结构体一次调用，多脚复用配置机 |

最扎眼的是雷 2：HAL 的初始化把"配脚"包了，却把"开钟"留在门外——`GPIO_Init` 签名里没有任何字段和时钟挂钩，两件事依然靠调用者的人肉纪律同步。这不是 ST 疏忽（`__HAL_RCC_GPIOC_CLK_ENABLE` 宏就等在那），是 C 语言表达力的天花板：**端口和它的电闸，在类型系统里没有关系**。

1060 字节买来通用、可读、可调试，值不值看项目胃口；但雷 2 和雷 3 的余款，得换一种语言层面的思路来结——把"端口、引脚、方向、速度"从函数参数提升成**类型本身**，配错组合让编译器在您敲回车那一刻就翻脸。这就是下一篇 `estdx::Gpio<Port, Pin, Dir>` 的活儿，整条楼梯的最后一级。

## 您来动手

1. `arm-none-eabi-nm --size-sort --print-size` 跑一遍 `blinky`，亲手找到 `HAL_GPIO_Init` 的 1060 字节，再找找 `register_led` 里有没有它；
2. 把 `blinky` 的 `gpio.Speed` 改成 `GPIO_SPEED_FREQ_HIGH`，反汇编对比 `HAL_GPIO_Init` 的调用处和 `main`——体会"Speed 只是结构体里一个数"；
3. 在工程里定义 `USE_FULL_ASSERT` 并实现一个 `assert_failed`（`printf` 或死循环），故意把 `gpio.Pin` 填 `0`——看保安上岗后的样子；
4. 通读 `HAL_GPIO_WritePin` 的反汇编，数一数 `PinState` 分支带来的指令数，和 01 篇 `register_led` 的单条 `str` 对比。

## 欸欸！自查一下再走

- 1088 字节差价的绝对主力是谁？为什么两份 `main` 几乎一样大？
- `HAL_GPIO_Init` 的 `(position - 8) << 2` 和 03 篇哪个算术是一回事？
- `MODIFY_REG` 展开后是什么？和咱们手写的哪两行等价？
- `HAL_GPIO_WritePin` 为什么用 `BSRR << 16` 而不用 BRR？两种写法指令数各几条？
- 五颗雷 HAL 拆了几颗？没拆的那颗为什么 C 语言治不了？

<ReferenceCard title="参考文献">
  <ReferenceItem
    :id="1"
    author="STMicroelectronics"
    title="stm32f1xx_hal_gpio.c — GPIO HAL Driver Source"
    :year="2016"
    url="https://github.com/STMicroelectronics/stm32f1xx_hal_driver/blob/master/Src/stm32f1xx_hal_gpio.c"
    chapter="L178-L284 HAL_GPIO_Init; L465-L479 HAL_GPIO_WritePin"
  />
  <ReferenceItem
    :id="2"
    author="STMicroelectronics"
    title="UM1850 — Description of STM32F1xx HAL drivers"
    :year="2016"
    url="https://www.st.com/resource/en/user_manual/um1850-description-of-stm32f1xx-hal-drivers-stmicroelectronics.pdf"
    chapter="GPIO HAL API; How to use this driver"
  />
  <ReferenceItem
    :id="3"
    author="STMicroelectronics"
    title="stm32f1xx.h / stm32_assert_template.h — CMSIS Device & Assert Template"
    :year="2016"
    url="https://github.com/STMicroelectronics/cmsis_device_f1/blob/master/Include/stm32f1xx.h"
    chapter="L189 MODIFY_REG; assert_param configuration"
  />
  <ReferenceItem
    :id="4"
    author="GNU Binutils"
    title="nm(1) — List Symbols from Object Files"
    :year="2024"
    url="https://sourceware.org/binutils/docs/binutils/nm.html"
    chapter="--size-sort --print-size"
  />
</ReferenceCard>
