---
title: "量程的边界：16 位乘 16 位能配多长"
description: "16 位 PSC 与 16 位 ARR 在 RM0008 里是两个 15:0 位域，全集 65536 乘 65536 共 2 的 32 次方个 tick，64MHz 下恰好 67.108864s，tick 不超过 65536 时除数取 1 恒精确。咱们把 tim.hpp 的 resolve_ticks、ticks_for_period、relative_error_ppm 三段逐字搬出来，host 复算五行对拍表，arm-none-eabi-g++ 探针证明角点 65535/65535 编得过、67108865µs 两道断言齐炸，mt19937_64 抽样 20 万周期 54.93% 走近似路径、最大误差 15ppm，推导出兜底上限 1/65536，0.1% 门槛真正拦下的是量程出界的配置，对照 HAL 的 IS_TIM_PERIOD 查的是 ARR 槽位宽度"
chapter: 4
order: 3
tags:
  - stm32f1
  - intermediate
  - 嵌入式
  - 寄存器
  - 编译期计算
  - constexpr
  - cpp-modern
difficulty: intermediate
platform: stm32f1
cpp_standard: [20, 23]
reading_time_minutes: 12
prerequisites:
  - "TIM 家族与确认式消抖：从 1Hz 心跳到 20ms 确认窗"
  - "配置进类型：指定初始化器当模板实参"
related:
  - "零开销的价格表：18200 字节日志固件的解剖"
---

# 量程的边界：16 位乘 16 位能配多长

## 引言：上一篇留下的量程问题

[《配置进类型：指定初始化器当模板实参》](02-config-in-type.md)收尾的时候，咱们把下一篇的活儿点到了量程的身上：还是在同一个 timclk 的约束下，16 位乘 16 位的量程到底装得下哪些周期。本篇咱们只算这一件事，边界在哪一头、边上的误差长什么样，咱们一次算清。

素材取自固定在提交 8026841 的子模块，跟 [01 篇](01-tim-family-confirm-debounce.md)、02 篇用的是同一份。开发电脑这边笔者用 GCC 16.2.1 配 `-std=c++23` 把求解器的函数复算了一遍。交叉工具链那边笔者用 arm-none-eabi-g++ 16.2.0 编了几个探针。探针是一段专为让编译器实例化模板而写的小翻译单元，咱们不链接它、也不上电，要看的就是编译这一步，报出来的错误本身就是咱们要的数据。

## 硬件的乘法：两个 16 位位域，2 的 32 次方个 tick

PSC 与 ARR 的位域，咱们去 RM0008（ST 的 STM32F1 参考手册，01 篇的参考卡同源）里核对过。手册是 2026-10-28 从 st.com 下载的 Rev 21 版 PDF，§15 就是讲 General-purpose timers（TIM2 to TIM5）的那一章，TIMx_PSC 的节号是 15.4.11，TIMx_ARR 的节号是 15.4.12，两个位域的记法都是 `15:0`，各占的宽度都是 16 位<RefLink :id="1" preview="STMicroelectronics, RM0008" />。

周期的算法咱们挨个念。预分频器的活是每 `(PSC+1)` 个输入 tick 放行一个，计数器的活是从 0 数到 ARR 再折回起点，所以一个周期吃掉的输入 tick 数目就是 `(PSC+1)×(ARR+1)`。寄存器里记的其实是 N−1，PSC=0 的意思是零分频，ARR=63 的意思是数 64 下，这层换算 01 篇讲求解器的时候交代过，咱们不重讲。

全集咱们一头一头算。PSC 与 ARR 的取值范围都是 0 到 65535，所以 `(PSC+1)` 与 `(ARR+1)` 的范围都是 1 到 65536，乘起来的上限是 `65536×65536 = 4'294'967'296` 个 tick，也就是 2 的 32 次方。64MHz 之下每秒的 tick 数是 64'000'000，咱们拿 2 的 32 次方一除，得到的恰好是 `67'108'864µs`、写成秒则是 67.108864s，咱们连一个零头都找不着。咱们再往小的一头看，周期填的单位是微秒，1µs 折成的 tick 正好是 64 个，PSC=0、ARR=63 的组合就配得下，误差落成了零。

还有一条性质是后面要用的：tick 数只要不超过 65536，除数取 1 的分解就恒精确。取 1 做除数的时候，除法对任何 tick 而言都是整除的，商也就是原本的 tick 数，而商不超过 65536 的时候也就塞得进 ARR。咱们拿 65536 除以 64，得到的是 1024，意思是 64MHz 之下 1024µs 以内的任何周期都有精确分解，近似路径在这里根本没有出场的机会。

## 求解器怎么挑：整除因子从 1 往上找，找不到就兜底

三段函数咱们从子模块固定在 8026841 的 tim.hpp 里逐字搬出来<RefLink :id="2" preview="Awesome-Embedded-Learning-Studio, libestdx" />。头一段找的是除数：

```cpp
consteval TimDivisors resolve_ticks(std::uint64_t ticks) {
    if (ticks < 1 || ticks > 65'536ull * 65'536ull) {
        return {0, 0, false};
    }
    for (std::uint64_t div = 1; div <= 65'536; ++div) {
        if (ticks % div == 0 && ticks / div <= 65'536) {
            return {static_cast<std::uint32_t>(div - 1),
                    static_cast<std::uint32_t>(ticks / div - 1), true};
        }
    }
    const std::uint64_t div = (ticks + 65'535) / 65'536;
    return {static_cast<std::uint32_t>(div - 1), static_cast<std::uint32_t>(ticks / div - 1), true};
}
```

咱们看第二段，把周期折成 tick 数的活计 01 篇念过，折算的公式里加了半个百万再除，落点取的是最近的微秒数：

```cpp
consteval std::uint64_t ticks_for_period(std::uint64_t clk_hz, std::uint64_t period_us) {
    return (clk_hz * period_us + 500'000) / 1'000'000;
}
```

咱们再看第三段，它算的是相对误差，本篇新添的 err 那一列出的是它的结果：

```cpp
consteval std::uint64_t relative_error_ppm(std::uint64_t target, TimDivisors d) {
    const std::uint64_t actual = (std::uint64_t)(d.psc + 1) * (d.arr + 1);
    return actual > target ? (actual - target) * 1'000'000 / target
                           : (target - actual) * 1'000'000 / target;
}
```

ppm 是 parts per million 的缩写，说的就是百万分之一的偏差，15ppm 就是百万分之十五的相对偏差。`resolve_ticks` 的找法 01 篇拆过，从 1 往上找的是能整除、商又塞得进 16 位的因子，第一个命中的 div 就是答案。找不到的时候走的兜底是 div 取 `ceil(ticks/65536)`，代价是带上余数的损失。咱们把这三段函数逐字复刻到开发电脑上，timclk 取的是缺省的 64MHz，跑出来的就是一张五行表，输出咱们原文照贴：

```text
         1 us | ticks=          64 | PSC=    0 ARR=   63 | err=0 ppm
     20000 us | ticks=     1280000 | PSC=   19 ARR=63999 | err=0 ppm
   1000000 us | ticks=    64000000 | PSC=  999 ARR=63999 | err=0 ppm
  67108863 us | ticks=  4294967232 | PSC=65535 ARR=65534 | err=15 ppm
  67108864 us | ticks=  4294967296 | PSC=65535 ARR=65535 | err=0 ppm
```

中间两行咱们认得：20ms 那行是 01 篇 10 号示例的确认窗，1s 那行是 09 的心跳，那对值的来历 01 篇拆解过，这里咱们就不重讲了。咱们看新添的三行。1µs 走的正是 div=1 的精确分解：64 个 tick、ARR=63，跟上一节的推论对上了。最下头两行都贴着量程的顶：67'108'863µs 走的是近似路径，误差落在了 15ppm，而 67'108'864µs 反而是零误差。这两行的待遇为什么不同，下一节咱们到编译器里验。

## 角点与出界：67108864 编得过，再多 1 微秒就被拦下

探针的形状如下，构建旗子跟 09 的构建是同一套：`STM32F103xB`、`USE_HAL_DRIVER`、`-mcpu=cortex-m3`、`-mthumb`，标准用的是 `-std=gnu++23`，工具链用的是 arm-none-eabi-g++ 16.2.0。OnBeat 是个满足 IrqHandler 概念的空壳类型，咱们要的只是模板实例化这一下，行尾那道 `"force"` 断言的用意是逼着编译器把 `kDiv` 算出来：

```cpp
using Probe = estdx::stm32f1::TimBase<
    {.instance = estdx::stm32f1::TimInstance::Tim2, .period_us = 67'108'864}, OnBeat>;
static_assert(Probe::kDiv.arr >= 0, "force");
```

咱们把 `period_us` 填成 `67'108'863`，它编过了，兜底解出来的值是 PSC=65535、ARR=65534，跟表里那行是一致的。换成 `67'108'864` 也编过了，咱们再换一条断言直接问角点的两个值，角点说的就是 PSC 与 ARR 同时顶满 65535 的配法，类型名跟着换成了 Corner：

```cpp
static_assert(Corner::kDiv.psc == 65'535 && Corner::kDiv.arr == 65'535);
```

也编过了。PSC=65535、ARR=65535 的乘积恰好是 2 的 32 次方个 tick，上一节算过的全集上限在这里一分不多一分不少地用上了。它 err=0 的原因咱们看着也直白，65536 本身就是 2 的 32 次方的因子，整除是成立的，精确分解也就成立了。

拦在量程边上的就是 TimBase 里那两道断言，咱们从 tim.hpp 原样搬过来：

```cpp
    static_assert(kDiv.ok, "period below resolution or beyond 16-bit PSC*ARR at this clock");
    static_assert(detail::relative_error_ppm(detail::ticks_for_period(CFG.timclk_hz, CFG.period_us),
                                              kDiv) <= 1'000,
                  "period misses 0.1% precision at this clock");
```

两道的报错文本咱们等下在输出里都能见着。TimOneShot 那边还有两道同款的断言，报错文本里的 period 换成了 timeout，本篇咱们就不展开了。咱们把 `period_us` 再往前多填 1µs、换成 `67'108'865`，两道断言就一起报了，输出咱们原文照贴：

```text
.../tim.hpp:106:24: error: static assertion failed: period below resolution or beyond 16-bit PSC*ARR at this clock
  • '...::kDiv.estdx::stm32f1::detail::TimDivisors::ok' evaluates to false
.../tim.hpp:108:53: error: static assertion failed: period misses 0.1% precision at this clock
  • the comparison reduces to '(999999 <= 1000)'
```

报错路径里的 `...` 是完整命名空间路径的缩写，咱们标注了省略，成稿里的字一个都没改。头一条报的是量程：`67'108'865µs` 要的是 `4'294'967'360` 个 tick，比 2 的 32 次方多出了 64 个，这么大的数目 16 位乘 16 位装不下，`kDiv.ok` 折成了 false。第二条里的 999999 看着有些突兀，咱们把算式复算一遍也就明白了：量程失败的时候 `resolve_ticks` 返回的是 `{0, 0, false}`，`relative_error_ppm` 拿到的 actual 是 `(0+1)×(0+1) = 1` 个 tick，而目标却有四十二亿九千多万个，咱们代进 `(target−1)×1'000'000÷target` 一算，整数除法截断之后正好落成的是 999999。它比 1000 大出了近千倍，第二道断言也就跟着失败了。

出界的另一头咱们也验了一下，咱们把 `period_us` 换成 1、`timclk_hz` 换成 `400'000` 再编一遍。ticks 四舍五入成了 0，tim.hpp:106 那句的 `below resolution` 半边触发，报错的文本一个字都没变。tim.hpp:108 那边变成了 `error: non-constant condition for static assertion`，原因是精度算式里除以了 0，常量求值在那一行就断了。量程断言报错文本里的前半句，就是给这类低时钟的配置准备的。

## 近似路径的误差：抽样 20 万个周期，15ppm 封顶

精确分解覆盖不了所有周期，兜底路径的误差有多大，笔者拿抽样量了一遍。随机源用的是 `mt19937_64`，也就是标准库的 64 位梅森旋转随机数生成器，种子取的是 20261028，笔者在 65'537 到 67'108'864µs 的区间里均匀抽了 20 万个周期，host 上逐个求解用的就是复刻的函数，输出咱们原文照贴：

```text
sampled 200000 periods in (65.536ms, 67.1s): fallback=109867 (54.93%), max_err=15 ppm at 62860349 us
```

没有精确分解的周期占了 54.93%，它们走的只能是近似路径。量到的最大误差是 15ppm、落点是 62'860'349µs。15 这个数不是碰运气的，咱们把它的来历一步步推出来。

第一步咱们看兜底丢了什么。兜底的 div 取的是 `ceil(ticks/65536)`，配出来的实际 tick 数，就是 ticks 除以 div 的整数商再乘回 div，丢掉的余数就是 ticks 对 div 取模的结果。余数的上限是 `div−1`，这是取模的定义给的。

第二步轮到分母的下界。div 的取法既然是向上取整，而 ticks 一定得大于 `(div−1)×65536`，不然的话 div 用不着这么大。分子这一头咱们刚拿到上限 `div−1`，分母则大过了 `(div−1)×65536`，咱们把两个不等式一夹，相对误差的上界就成了 `(div−1)÷((div−1)×65536)`，也就是咱们要找的 `1/65536`。换成 ppm 的读数是 15.26，整数截断之后能留下的至多是 15ppm。抽样量到的封顶值恰好是 15，跟咱们推出的上界对上了。

第三步咱们看那道 0.1% 的精度门槛。1000ppm 的门槛比近似误差的封顶值大了六十多倍，所以兜底路径的误差永远够不着 1000ppm 的门槛，单靠近似误差是触发不了它的。它真正接得住的，是量程出界的配置：上一节那行 `999999 <= 1000` 的报错，就是它把出界的配置一并拦下的样子。这是咱们照着求解器的算式推出来的读法，库作者当时是怎么想的，咱们不代言。

## 对照 HAL：IS_TIM_PERIOD 查的是槽位

同一个量程的问题，在 HAL 的世界里是什么待遇？咱们把 stm32f1xx_hal_tim.h 里管 Period 的宏引出来，原文咱们引的是 L1657 那一行<RefLink :id="3" preview="STMicroelectronics, stm32f1xx_hal_tim.h" />：

```c
#define IS_TIM_PERIOD(__PERIOD__)      (((__PERIOD__) > 0U) && ((__PERIOD__) <= 0xFFFFU))
```

它查的只有一件事：这个数大于 0、又装得进 `0xFFFF`，合起来就是 ARR 寄存器的 16 位槽位。02 篇引过 `TIM_Base_SetConfig` 落笔的那句，`TIMx->ARR` 拿到的就是 `Structure->Period` 的原值，所以宏查的是槽位宽度。而配置在给定时钟下能不能表达您要的周期，宏是没有过问的。PSC 配几、ARR 配几的活，在 HAL 的模型里归工程师自己：量程的知识都写在 RM0008 里，咱们自己查、自己乘、自己算。何况默认档的 assert_param 是空操作，02 篇贴过它的两档展开，这类检查在预处理之后就不在了。

咱们回头看 tim.hpp 这边，同一套乘法搬进了 consteval，量程与精度都成了编译期的断言，02 收尾预告的那第二笔收获，落的就是这个样子。

## 收尾：量程装下了，下一个问题是中断的次序

量程的边界算完了：64MHz 之下 `1µs` 到 `67.108864s` 的周期全都配得下，顶上那一点的误差恰好是零。时间轴上还有一个问题等着咱们。多个定时器都要打断 CPU 的那一刻，谁排在前头、谁的中断又得让路，下一篇咱们拆解的就是 NVIC 的抢占等级。01 篇里咱们其实已经用过它，09 的 `init(3)`、10 号示例里 Touch=2、Window=3、SlowTick=4 的排法，当时咱们只是照着填，它的规则咱们还没拆过。

## 注意事项与常见错误

| 症状 | 原因 | 解法 |
|---|---|---|
| 报错文本是 below resolution 半边 | timclk 太低，ticks 四舍五入成了 0，比如 400kHz 下的 1µs | 核对 `timclk_hz` 填的是不是真实时钟，低时钟下配不了 1µs 量级的周期 |
| 报错文本是 beyond 16-bit PSC\*ARR 半边 | 周期超出 64MHz 下的 67.108864s 量程上限 | 换更慢的 timclk，量程上限跟着反比抬高（timclk 减半到 32MHz，顶上翻成 134.217728s），或把长周期拆到软件里续数 |
| 两道断言一起报，第二条写着 999999 <= 1000 | 量程断言已经拦下，精度算式拿到的是 ok=false 的出界输入 | 修量程那一条，第二条跟着就安静了 |
| 表里 err 是 15ppm，担心精度不够 | 走了近似路径，误差上限 1/65536 ≈ 15.3ppm | 15ppm 对多数定时场景够用，要精确就挑整除得开的周期 |
| 想配满量程的周期 | 67'108'864µs 恰好等于 2 的 32 次方个 tick，角点编得过且零误差 | 再大 1µs 就出界，长于此的周期拆软件续数 |

## 参考资源

<ReferenceCard title="参考文献">
  <ReferenceItem
    :id="1"
    author="STMicroelectronics"
    title="RM0008 Reference Manual — STM32F101/102/103/105/107"
    :year="2021"
    url="https://www.st.com/resource/en/reference_manual/rm0008-stm32f101xx-stm32f102xx-stm32f103xx-stm32f105xx-and-stm32f107xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf"
    chapter="15 General-purpose timers (TIM2 to TIM5); 15.4.11 TIMx_PSC; 15.4.12 TIMx_ARR"
  />
  <ReferenceItem
    :id="2"
    author="Awesome-Embedded-Learning-Studio"
    title="libestdx（include/libestdx/boards/stm32f1/tim.hpp，子模块固定在提交 8026841）"
    :year="2026"
    url="https://github.com/Awesome-Embedded-Learning-Studio/libestdx"
    chapter="resolve_ticks, ticks_for_period, relative_error_ppm 与 TimBase/TimOneShot 的量程断言"
  />
  <ReferenceItem
    :id="3"
    author="STMicroelectronics"
    title="stm32f1xx_hal_tim.h（vendored 源，L1657 的 IS_TIM_PERIOD 宏）"
    chapter="文本冻在 libestdx 子模块 third_party/STM32F1/Drivers/STM32F1xx_HAL_Driver/Inc/"
  />
</ReferenceCard>
