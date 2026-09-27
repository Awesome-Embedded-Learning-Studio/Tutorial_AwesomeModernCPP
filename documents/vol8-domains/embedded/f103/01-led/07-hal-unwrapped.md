---
title: "拆开 HAL 的包装纸：GPIO_Init 在替咱们写哪些位"
description: "给 HAL 的 GPIO 层上秤称重拆包：1088 字节差价里 HAL_GPIO_Init 独占 1060，而两份 main 只差 8 字节——初始化付大头、运行路径几乎平手。逐行走读 HAL_GPIO_Init 的逐位循环、Mode 到 CNF/MODE 的 switch 翻译、与 04 篇同款的 (position-8)×4 半段分家算术，再看 MODIFY_REG 宏里裹着的读改写三件套、WritePin 一个 BSRR 两副面孔的高半字技巧、assert_param 默认缺席的真相——最后复查:五个问题 HAL 拆了一个、半拆一个、缓解两个，时钟外置还在"
chapter: 1
order: 7
tags:
  - stm32f1
  - beginner
  - 嵌入式
  - 寄存器
difficulty: beginner
platform: stm32f1
---

# 拆开 HAL 的包装纸：GPIO_Init 在替咱们写哪些位

上一篇咱们把 C 宏的代价摆清了：能跑，不过身上带着五个问题。HAL 是 ST 官方给出的另一条路，走的同样是"把寄存器包起来"，官方的"包装纸"却厚得多、也贵得多。今天咱们的活儿，就是把它拆开、一层一层地标上价，不站队"HAL 好还是不好"："包装纸"里卷了什么，哪些是实打实的干货，哪些是咱们用不到、还躲不掉的，咱们一项一项过秤。

## 上秤：1088 字节的构成

上秤的是咱们认识的两份固件：`01_blinky`（HAL 一手包办）和 `01_register_led`（裸寄存器直书）。咱们把体积摆出来：

```text
$ arm-none-eabi-size blinky register_led
   text    data     bss
   3468      12       4    blinky        (纯 HAL)
   2380      12       4    register_led  (裸寄存器)
   ─────────────────────────────
   差价     1088 字节
```

差价花在哪？咱们让 `arm-none-eabi-nm --size-sort` 把符号按体积排好队，答案自己就跳出来了：

| 符号 | 体积 | 谁带着 |
|------|------|--------|
| `HAL_GPIO_Init` | **1060 字节** | 只有 blinky 有 |
| `HAL_RCC_OscConfig` | 1004 字节 | 两边都有 |
| `HAL_RCC_ClockConfig` | 384 字节 | 两边都有 |
| `HAL_SYSTICK_Config` + NVIC 零头 | ~80 字节 | 两边都有 |
| `main` | **144 vs 136** | 差 8 字节 |

差价的绝对主力，咱们一眼就看得见：`HAL_GPIO_Init` 这个配置机器，一家就占去了 **1060 字节**。更有意思的是，两份 `main` 函数几乎一样大：初始化的开销付过之后，运行路径上 HAL 系和裸寄存器系打了个平手。还有个咱们容易看漏的细节：`register_led` 身上也挂着 `OscConfig` 和 `ClockConfig`，它同样调用了 `HAL_Init` 和 `SystemClock_Config`，表里那行 ~80 字节的 SysTick 零头，也就跟着挂到了两边。所以这 1088 字节是**扣除共同开销之后的净差**，买没买 HAL 的 GPIO 这一层，就是两家唯一的结构性区别。

## HAL_GPIO_Init 逐行拆

咱们花 1060 字节买来的机器，进料口其实是一个结构体：

```cpp
GPIO_InitTypeDef gpio{};
gpio.Pin   = GPIO_PIN_13;
gpio.Mode  = GPIO_MODE_OUTPUT_PP;
gpio.Speed = GPIO_SPEED_FREQ_LOW;
HAL_GPIO_Init(GPIOC, &gpio);
```

对照 06 篇的宏封装，情况好了不少：`13`、`推挽 2MHz` 这些裸数字，现在进了有名字的枚举字段。您要是拼错 `GPIO_MODE_OUTPUT_PP`，编译器当场就翻了脸。问题 1 到这儿就解决了。

接着咱们钻进机器内部（`stm32f1xx_hal_gpio.c` L178 起）：它干的第一件事就是**逐位扫描**：

```c
while (((GPIO_Init->Pin) >> position) != 0x00u)
{
    ioposition = (0x01uL << position);
    iocurrent  = (uint32_t)(GPIO_Init->Pin) & ioposition;
    if (iocurrent == ioposition)
    {
```

`Pin` 其实是掩码，不是脚号：咱们写 `GPIO_PIN_13 | GPIO_PIN_14`，配两个脚也就是一次的事，逐位处理的活儿交给循环。这就是通用性的头一项代价：哪怕咱们只配一个脚，这套循环、判断、逐位迭代的机构，偏偏一个都没有少。往下咱们接着看 `Mode` 是怎么翻成四位配置位的，用的就是一个 switch：

```c
case GPIO_MODE_OUTPUT_PP:
    config = GPIO_Init->Speed + GPIO_CR_CNF_GP_OUTPUT_PP;  // Speed 是 0/1/2/3,加在 CNF 基数上
    break;
```

`Speed + CNF` 这个加法有点门道：目标四位的布局是 `CNF<<2 | MODE`，`Speed` 枚举值本身就是 0-3 的 MODE 位。`GPIO_CR_CNF_GP_OUTPUT_PP` 呢，恰好等于把 `0x0`（推挽输出）左移两位的结果。您把两个数一加，四位正好就拼出来了。再往下就是咱们最眼熟的两行了：

```c
configregister = (iocurrent < GPIO_PIN_8) ? &GPIOx->CRL : &GPIOx->CRH;
registeroffset = (iocurrent < GPIO_PIN_8) ? (position << 2u) : ((position - 8u) << 2u);
```

**半段分家、四位偏移**：04 篇咱们手推过的那套算术，官方原装的同款，连 `(position - 8) << 2` 的写法，都跟咱们的 `(13-8)×4` 一模一样。咱们一路读到这儿，最后真正落到寄存器上的就是这两行：

```c
MODIFY_REG((*configregister), ((GPIO_CRL_MODE0 | GPIO_CRL_CNF0) << registeroffset),
           (config << registeroffset));
```

咱们去 `stm32f1xx.h` L189 看 `MODIFY_REG` 的真身：

```c
#define MODIFY_REG(REG, CLEARMASK, SETMASK) \
    WRITE_REG((REG), (((READ_REG(REG)) & (~(CLEARMASK))) | (SETMASK)))
```

咱们剥掉三层宏皮，看见的就是**读回来、清掩码、或新值、写回去**。02 篇的反汇编里咱们就见过它：读改写三件套，这回只是裹在了宏里。

03 篇的开钟宏里，咱们还见过同一套读改写：`__HAL_RCC_GPIOC_CLK_ENABLE()` 里的 `SET_BIT`，展开以后干的也是这三件事。当时那口多出来的 `tmpreg` 假读，咱们说好拆 HAL 时再细看，现在真身摆在眼前了，讲究就清楚了：`SET_BIT` 管的是把 bit 立起来，`tmpreg` 那一口读了就扔，管的却是时间——手册要求合闸之后隔几个时钟周期再碰外设，读一次寄存器的耗时，刚好把这个缓冲补足了。开钟这步的"包装纸"里，连等待都替咱们算好了。

拆到了这里，"包装纸"的底细已经见了大半：`HAL_GPIO_Init` 没有任何咱们没学过的魔法，它就是把 04 篇的手艺、逐位循环、switch 翻译和 EXTI 分支，装进了同一个函数。EXTI（外部中断）的分支从 L288 往下还有几十行，本站里咱们不去走它，但它占的体积，您已经付过了。

<Anim id="f103-hal-gpio-init" />

## WritePin：一个寄存器，两副面孔

运行路径上的 `HAL_GPIO_WritePin`（L465-L479）短得很，咱们干脆整段复制过来：

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

咱们看两处。头一处是那个 `<< 16`：BSRR 低半字置位、高半字复位（04 篇咱们讲过）。HAL 不用 BRR 寄存器、把同一个 BSRR 的两副面孔都用上了。省下的是对一个外设地址的寻址，代价是复位时多出来的一次移位。

另一处咱们看那个 `if`。`PinState` 是运行时才知道的变量，所以编译器没法砍掉这个分支，每次调用都带着一次比较和一次跳转的开销。开头上秤时咱们看到"main 只差 8 字节"，背后的细节就是它：分支开销在，不过很便宜。原子性倒是没打折，两条路径最终落到的都是**单条写**，中断来了也插不进脚。

## assert_param：默认关着的参数检查

开头两行 `assert_param` 常被当成 HAL 的卖点：参数合法性检查。咱们去 `stm32_assert_template.h` L42-L46 看它的真身：

```c
#define assert_param(expr) ((expr) ? (void)0U : assert_failed((uint8_t *)__FILE__, __LINE__))
// ...
#define assert_param(expr) ((void)0U)      // ← 默认:这行
#endif /* USE_FULL_ASSERT */
```

默认配置（不开 `USE_FULL_ASSERT`）的时候，`assert_param` 展开成的东西就是 `(void)0U`，**编译后一个字节都不剩了**。那个三目运算符版的真检查，要您显式打开宏才有，而且 `assert_failed` 还得您自己实现。所以"HAL 自带参数检查"这句话的正确读法是：**检查逻辑写好了，出厂默认是关着的**。这其实不算坏事，而固件里塞满检查才是坏事。咱们知道这一点，用的时候心里有数就好。

## 复查：五个问题的解决进度

咱们把 06 篇数出的五个问题带过来回访 HAL：

| 问题 | HAL 的应对 |
|----|-----------|
| 1. 无类型 | **拆了**——`GPIO_InitTypeDef` + 枚举，拼错编译失败 |
| 2. 时钟外置 | **还在**——`HAL_GPIO_Init` 不管开钟，还是得您自己念 `__HAL_RCC_GPIOC_CLK_ENABLE()`，忘念照样幽灵 bug |
| 3. 正交维度无约束 | **半拆**——枚举挡住拼错，挡不住非法组合（比如给 PC13 塞 50MHz，运行时才发现不对劲） |
| 4. 不可调试 | **缓解**——真函数、真符号，断点和源码对得上 |
| 5. 复制粘贴 | **缓解**——一个结构体一次调用，多脚复用配置机 |

咱们看最扎眼的问题 2。HAL 的初始化把"配脚"包了，"开钟"却没跟着一起包进来：`GPIO_Init` 的签名里，找不出任何和时钟挂钩的字段，两件事依然靠调用者的人肉纪律同步。这不算 ST 的疏忽，`__HAL_RCC_GPIOC_CLK_ENABLE` 宏也早就等在那儿了。真正卡住咱们的，是 C 语言表达力的上限：**端口和它的电闸，在类型系统里就是没有关系的**。

这 1060 字节买来的通用、可读、可调试到底值不值，咱们得看项目的胃口。但问题 2 和问题 3 的尾巴，得换一种语言层面的思路来收：把"端口、引脚、方向、速度"从函数参数提升成**类型本身**，配错了组合，让编译器在您敲回车那一刻就翻脸。这就是下一篇 `estdx::Gpio<Port, Mask, Dir>` 的活儿，也是整条楼梯的最后一级。

## 您来动手

1. 您拿 `arm-none-eabi-nm --size-sort --print-size` 跑一遍 `blinky`，亲手找到 `HAL_GPIO_Init` 的 1060 字节，再找找 `register_led` 里有没有它。
2. 您把 `blinky` 的 `gpio.Speed` 改成 `GPIO_SPEED_FREQ_HIGH`，再反汇编对比 `HAL_GPIO_Init` 的调用处和 `main`，体会"Speed 只是结构体里一个数"。
3. 您在工程里定义 `USE_FULL_ASSERT` 并实现一个 `assert_failed`（`printf` 或死循环），故意把 `gpio.Pin` 填 `0`，看看参数检查真的开动之后，是什么样子。
4. 您通读 `HAL_GPIO_WritePin` 的反汇编，数一数 `PinState` 分支带来的指令数，和 02 篇 `register_led` 的单条 `str` 对比。

## 欸欸！自查一下再走

- 您说说，1088 字节差价的绝对主力是谁？为什么两份 `main` 几乎一样大？
- `HAL_GPIO_Init` 的 `(position - 8) << 2`，和 04 篇哪个算术是一回事，您还有印象吗？
- `MODIFY_REG` 展开后是什么？和咱们手写的哪两行等价？
- `HAL_GPIO_WritePin` 为什么用 `BSRR << 16` 而不用 BRR？您来数一数，两种写法指令数各几条？
- 五个问题 HAL 解决了几个？没解决的那个，您知道为什么 C 语言治不了吗？

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
