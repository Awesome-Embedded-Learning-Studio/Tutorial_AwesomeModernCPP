---
title: "把秒表焊进硅片:时钟树爬上 72MHz,量第一刀切换"
description: "e2b7dc0(feat: optimize the sched performance)把项目头一次放上真硅:clock.hpp 手动爬时钟树(HSE 8MHz 经 PLL×9 到 72MHz,带认出 Renode 假 RCC 的探测器),dwt.hpp 把 Cortex-M3 的 CYCCNT 当秒表,blink 让调度器头一回在真硅上打拍。perf demo 把 DWT 时间戳挪出内核热路径,由让出者写、醒来者读,仓库存档里的 yield 均值从 489 个周期降到 354 个,降幅 28%。我当着您跑通了 host 侧的 14 个测试、交叉编译的体积清单和 Renode 里的探测器对质,而仿真读出的 CYCCNT 全是 0,周期数的唯一出处,只剩仓库里在真正的开发板上量出来的存档"
chapter: 8
order: 1
tags:
  - stm32f1
  - advanced
  - 嵌入式
  - 优化
  - 调试
difficulty: advanced
platform: stm32f1
cpp_standard: [23]
reading_time_minutes: 24
prerequisites:
  - "一幕一房:demo 注册中心、任务创建三条路与遗言成义务"
related:
  - "在栈顶伪造出生证明:PendSV、SVC 与第一次切换"
  - "教科书输给预取缓冲器:.ramfunc 反转与一本诚实的实验册"
current_status:
  title: ZerOS的篇章正在打磨ing
  detail: 时钟树与第一刀切换 · 等你验收
  variant: polishing
  icon: ⏱
---

# 把秒表焊进硅片:时钟树爬上 72MHz,量第一刀切换

《一幕一房》收尾的时候,demo 注册中心已经就位了,不过它当时名下的 demo 只有八个,blink 和 perf 是 `e2b7dc0` 自己注册进去的新一批,咱们用 `zeros_add_demo` 一行就能给它们生成固件目标。不过您把当时的处境盘一遍,就会发现咱们手里缺的东西很要命:调度器在 Renode 里换手换了好几站,可一次切换到底花掉了多少个 CPU 周期,咱们手上的数字是一个都没有的。切换贵不贵、预算达标没达标的事,咱们要是全凭感觉说了算,而感觉是性能的事里最靠不住的证人。

`e2b7dc0` 来还这个问题了。它的 subject 写的是 “feat: optimize the sched performance”,我数过它的 diff,它动了 18 个文件,新增了 548 行,删掉了 14 行。它在板级一口气交了三样新家当:手动爬时钟树的 `clock.hpp`、当秒表使的 `dwt.hpp`、concepts 化的 GPIO 栈加 blink demo,又在内核侧动了刀,把测量代码从热路径里挪了出去。数字落在仓库自己的测量存档里,yield 的均值从 489 个周期降到了 354 个。

而在动笔之前,有一个口径我得跟您摆清楚。本站的周期数,出处只有仓库里的测量存档 `documents/notes/perf_1.md`,它记的是当年在 Blue Pill 上量出来的数。我如今的桌面上没有板子,所以烧录、OpenOCD、LED 的亮灭,我都用 “笔者当时的操作” 的视角引存档的原话,编不出来的也不编。凡是我在您眼前真跑了的,host 测试、交叉编译、Renode 里能跑的部分,我都贴真实输出,而仿真给不出周期数的事,我也当着您验了一遍。

## 时钟树要自己爬:HSE 8MHz 到 PLL×9

上电的那一刻,STM32F103 用的是它自己内部的 8MHz 振荡器,它的名字叫 HSI。咱们要的 72MHz 从天上掉不下来,得咱们把时钟树一格一格接过去:外部的 8MHz 晶振叫 HSE,把它喂给锁相环(PLL 是一个把频率乘上去的电路),乘了 9 倍之后,出来的就是正好 72MHz。仓库新增的 `clock.hpp` 把整棵树写成了一段手动配置,咱们把它分几段看:

```cpp
inline void clock_init() {
    auto* const rcc_cr   = reinterpret_cast<volatile std::uint32_t*>(0x40021000);
    auto* const rcc_cfgr = reinterpret_cast<volatile std::uint32_t*>(0x40021004);
    auto* const flash_acr = reinterpret_cast<volatile std::uint32_t*>(0x40022000);
```

函数开门的三行就是三个寄存器的地址,咱们挨个认一下:头两个是 RCC(Reset and Clock Control)的控制与配置寄存器,而 RCC 是芯片里管所有时钟开关的部门,它的中文名叫复位与时钟控制器,剩下的是 Flash 自己的 ACR 寄存器。咱们的寄存器访问都走 `volatile`,编译器不许把咱们的读写优化掉,因为它们每一个都带着硬件的副作用。

接下来是树的六步,咱们一步一步念。头两步做的事是开晶振、等它稳:

```cpp
    // 1) 开 HSE(外部 8MHz 晶振),等就绪
    *rcc_cr |= (1u << 16); // HSEON
    while ((*rcc_cr & (1u << 17)) == 0) { // HSERDY
    }
```

把控制寄存器的第 16 位写成 1,HSE 就开始起振了,而晶振从通电到稳定要一点时间,硬件会在第 17 位上立一个就绪的标记,咱们的循环守着它,它不立起来咱们就不往下走。硬件什么时候就绪,咱们说了不算,所以只能等。

开完了晶振,接着动的是 Flash,咱们跟着看:

```cpp
    // 2) Flash 等待周期:72MHz 需要 2 wait states
    *flash_acr = (*flash_acr & ~0x7u) | 0x2u;
```

Flash 的读取速度追不上 72MHz 的 CPU,频率上去了,取一条指令的代价就是让 CPU 多等两个时钟周期,咱们管它们叫 2 个等待周期(wait states)。该步骤必须排在提频的前面,顺序反了,CPU 就在用跑不动的高频去读跟不上的 Flash,固件当场死给您看。

紧跟着分的是总线和外设的频,咱们接着看:

```cpp
    // 3) AHB 不分频(72MHz),APB1 ÷2(36MHz,最大),APB2 不分频(72MHz)
    //    CFGR: HPRE=0 (÷1), PPRE1=100 (÷2), PPRE2=0 (÷1)
    *rcc_cfgr = (*rcc_cfgr & ~(0xFu << 4) & ~(0x7u << 8) & ~(0x7u << 11))
              | (0x4u << 8); // PPRE1 = ÷2
```

CPU 的 72MHz 原样供给 AHB 总线,咱们看见挂在 APB1 上的低速外设最高只吃得下 36MHz,所以那边走了 ÷2,而串口 1 住在 APB2 上,跟着跑满了 72MHz。

剩下的三步,咱们把 PLL 请出来、把系统时钟切过去:

```cpp
    // 4) PLL:HSE × 9 = 72MHz
    //    CFGR: PLLSRC=1 (HSE), PLLXTPRE=0 (不÷2), PLLMUL=0111 (×9)
    *rcc_cfgr = (*rcc_cfgr & ~((1u << 16) | (1u << 17) | (0xFu << 18)))
              | (1u << 16)    // PLLSRC = HSE
              | (0x7u << 18); // PLLMUL = ×9

    // 5) 开 PLL,等就绪
    *rcc_cr |= (1u << 24); // PLLON
    while ((*rcc_cr & (1u << 25)) == 0) { // PLLRDY
    }

    // 6) 切系统时钟到 PLL 输出,等切换完成
    *rcc_cfgr = (*rcc_cfgr & ~0x3u) | 0x2u; // SW = PLL
    while ((*rcc_cfgr & (0x3u << 2)) != (0x2u << 2)) { // SWS = PLL
    }
```

PLL 的时钟源选了 HSE,倍频选了 9,算下来的结果是 8 乘 9 等于 72。PLL 也需要起振的时间,所以咱们照旧等它的就绪位,系统时钟的选择字段写 2 表示改用 PLL,而收尾读回来确认的是 SWS(系统时钟切换的状态),切换真完成了标记才变,写和读对上了,树才算爬完了。位段的编号我逐一对过 STM32F1 参考手册里 RCC 章节的口径,`clock.hpp` 的注释自己也写着每一步的位段含义,两边是咬得上的。

要是跳过了它,会怎么样?`clock.hpp` 的文件头注释写得直白:该文件是只在真硅上使的代码,要是咱们不叫 `clock_init`,一切 72MHz 的假设(SysTick、UART 的波特率、延时)都在 8MHz 下跑,全都慢了 9 倍。`demo_setup.hpp` 调它的地方也各有小注,咱们读到的小注 real silicon needs this 是真硅需要它的意思,另一句提醒的是波特率按 72MHz 算,串口的波特率算错了 9 倍,打印出来就是满屏的乱码,您头一次烧固件要是见了乱码,该查的就是树爬没爬。

## Renode 里的 RCC 是假的:一个探测器替咱们认路

树爬通了,可咱们别忘了还有仿真器 Renode 这样一位老朋友。它学的是 Blue Pill 的内存映射,但它对 RCC 的模拟是糊弄事儿的,所以 `clock.hpp` 的注释里有一段探测器,专门替咱们认这件事:

```cpp
    // Simulation detector: Renode's RCC model has ready bits ALWAYS set
    // (P5: HSERDY=1 even when HSEON=0). On real silicon, HSERDY stays 0
    // until HSE is actually running — so seeing it high BEFORE we ask
    // for HSE means we're in a simulator with a fake clock tree. Skip
    // the whole thing; the sim already runs at whatever frequency it wants.
    if ((*rcc_cr & (1u << 17)) != 0) {
        return; // simulation — clock tree is fake, don't touch it
    }
```

注释里的 P5,咱们去仓库的待决清单里能对上号,对上的是第 5 条,讲的就是仿真器的 RCC 是假的这件事。咱们的判据只有一条:咱们还没请求 HSE,就绪位(HSERDY)就已经立着了。真硅上不可能有的事,而晶振没开,就绪的标记就该是 0,仿真器里立的是 1,说明咱们手里的时钟树是假的,往里写六步的配置纯属白费,函数就直接回去了,让仿真按它自己的频率跑。

探测器不是咱们猜出来的,我当着您在 Renode 里对质过一遍。我本机的 Renode 1.17.0 装不出这个 `.repl` 里引用的 I2C 与 EXTI 模型,所以我摘掉的是 i2c1、i2c2 两行,加上 exti 整块、它上面的两路 nvicInput、cpu 的 EventInterrupt 与 ErrorInterrupt 挂线,以及各 GPIO 端口指向 exti 的连线(blink 与 perf 用不到它们,这次的对质不受影响),而平台其余的部分照原样。然后咱们照仓库 `bluepill.resc` 自己写的用法起机,我的完整命令行是:

```shell
renode --console --disable-xwt -e "logFile @/tmp/rcc-probe.log" \
  -e '$bin=@build-fw/src/board/stm32f103_bluepill/example/blink/zeros-demo-blink.elf' \
  -e "include @src/board/stm32f103_bluepill/sim/renode/bluepill.resc" \
  -e "sysbus ReadDoubleWord 0x40021000" -e "quit"
```

咱们在监视器里读到的这一行,是固件还没开跑的时候打出来的:

```text
[21:50:47.0652] [WARNING] sysbus: (tag: 'RCC_CR') ReadDoubleWord from non existing
    peripheral at 0x40021000, returning 0x0A020083.
```

读回来的值是 `0x0A020083`。咱们把它按二进制摊开验:第 17 位立的是 1,而第 16 位(HSEON,咱们请求 HSE 用的位)住的还是 0。固件还没碰过它的寄存器,就绪的标记自己就立起来了,探测器的前提在仿真里当场兑现。按手册和存档的口径,真硅上做同样的读取会读到 0,而板上那一半,手上没有板子的我验不了,归出的只能是存档和手册。

## DWT:硅片自带的秒表

时钟有了,咱们还缺表。量切换的活儿,对表有一条硬的要求,表的刻度得比被量的东西更细才量得着:切换是几百个周期的活,而咱们手里现成的心跳一拍是一毫秒,折成周期是七万二的量,咱们拿它去量切换,读出来的就永远是零。数数的事要是交给软件来干,数的指令自己就得吃周期,数的动作混进被量的对象里,读出来的数也就虚了。咱们最后挑中的表就长在 CPU 里面:DWT(Debug Watchpoint and Trace,调试观察点与跟踪单元)是 Cortex-M3 内置的调试模块,它的 CYCCNT 寄存器是一个每来一个时钟周期就加一的计数器,数数的活硬件替咱们干了,软件只管读它的数。开表、读数的动作,走的都是普通的寄存器读写,而且不占任何的外设。`dwt.hpp` 的全文二十来行,pragma、include 与命名空间的外壳之外,干活的就两个函数,咱们分开来念:

```cpp
inline void dwt_init() {
    auto* const demcr     = reinterpret_cast<volatile std::uint32_t*>(0xE000EDFC);
    auto* const dwt_ctrl  = reinterpret_cast<volatile std::uint32_t*>(0xE0001000);
    auto* const dwt_cyccnt = reinterpret_cast<volatile std::uint32_t*>(0xE0001004);

    *demcr |= (1u << 24);    // TRCENA: unlock the DWT block
    *dwt_cyccnt = 0;          // reset the counter
    *dwt_ctrl |= 1u;          // CYCCNTENA: start counting
}
```

开表的动作有三步,咱们一句一句过。DWT 平时是关着的,得把调试异常与监控寄存器 DEMCR 的第 24 位(TRCENA)立起来,整个跟踪模块才通上了电,然后计数器清了零,再写控制寄存器的第 0 位,表就开始走了。TRCENA 的解锁排在 CYCCNT 的使能之前,我对过 ARMv7-M 体系结构手册里调试章节的口径,`dwt.hpp` 写的顺序跟手册对得上,CYCCNT 在个别早期的硅版上有不跑的毛病,手册里有勘误的说法,而咱们这块 Blue Pill 上表是走起来了,存档里的数字就是证词。

```cpp
[[nodiscard]] inline std::uint32_t dwt_cycles() {
    return *reinterpret_cast<volatile std::uint32_t*>(0xE0001004);
}
```

读数的动作就是读 CYCCNT,咱们拿 72MHz 折算的话,每 72 个计数对应的时长就是一微秒,存档的口径跟这个换算是一致的。

咱们的秒表在仿真里走不走?我又当着您验了一遍。同一个 perf 固件在 Renode 里跑完了一轮测量,报表打出来是这样的:

```text
[1] yield full path (external DWT):
    min: 0 cycles (~0us)
    avg: 0 cycles (~0us)
    max: 0 cycles (~0us)
```

报表里的 min、avg、max 全是 0。Renode 的 Cortex-M 模型没有实现 CYCCNT,而计数器纹丝不动,测出来的差值自然是零。上一站收尾时咱们还指望过的 Renode 虚时间测量,在这儿拿到了它的回信,全 0 的报表就是回信的正文。这一段零我特意贴给您看,因为它把一个边界划清楚了:仿真能替咱们验逻辑、验探测器、验启动,唯独周期数是一个都给不出的,所以后面所有的周期数字,出处就只剩一处了,就是仓库里在真正的开发板上量出来的存档。

## blink:调度器头一回在真硅上打拍

有了时钟,有了 GPIO,咱们迎来了仓库的第一个真硅 demo。头一块新板子到了咱们手里,让灯闪起来是嵌入式的老礼数,不过 blink 的灯闪,图的不是灯本身:咱们前几站验调度器,物证全是仿真器里的串口打印,而 blink 要对质的东西更大,整条链子有时钟树、GPIO、SysTick、PendSV 的份,等它们在真硅上咬合起来了,看的是时间内核还能不能按毫秒打拍。判定咱们拿眼睛就能做,板上的小灯按秒一明一灭,拍子就是调度器亲手打的。朴素的做法是拿 `HAL_Delay` 空转一秒点灯,那样的灯也闪,不过内核在不在场,它都一个样地闪,对质就无从谈起了,文件头的注释自己就把话写得直白。咱们把代码分两段念:

```cpp
// PC13: onboard LED, active low (pull low to light up)
using LedPin =
    ZerOS::board::Gpio<ZerOS::board::GpioPort::C, 1u << 13, ZerOS::gpio::Direction::Output>;
using Led = ZerOS::gpio::LED<LedPin, ZerOS::gpio::Polarity::ActiveLow>;

void blinker(void*) {
    for (;;) {
        Led::toggle();
        ZerOS::ThisTask::sleep_for(ZerOS::Milliseconds{1000});
    }
}
```

咱们看见板载的 LED 挂在 PC13 上,用的是低电平点亮的极性。低电平点亮这件事没有写进任务的代码,它住在 `LED` 模板的极性参数里,而极性的分支是 `if constexpr` 在编译期溶解掉的,板上跑的代码里根本没有那个判断,所以零运行时的代价是白送的。板层提供的 `Gpio` 模板直写寄存器,点亮和熄灭走 BSRR 的原子单写,只有 toggle(翻转)的操作不得不读改写。`gpio_base.hpp` 定义的三个 concept(`GpioPin`、`GpioOutputPin`、`GpioInputPin`)是板无关的脸面,而换一块板,满足概念的实现也都能接上同一盏 `LED`。

任务体的点睛在文件头的注释里,blink 眨的不是 `HAL_Delay` 空转一秒的死循环,您看见的每一下闪烁,都是您自己的调度器在真硅上打的拍子。任务睡了一秒,时间内核的 SysTick 就醒了过来,调度器选了人,PendSV 完成了切换,LED 就翻转了一次,而闪的节奏就是任务节拍本身。

```cpp
int main() {
    ZerOS::demo::setup("blink");
    LedPin::init();
    ZerOS::arch::cortex_m3::launch(
        ZerOS::task::named("blink").prio(1).words(64).entry(blinker, nullptr));
    ZerOS::arch::cortex_m3::start_scheduler();
}
```

咱们看 `main` 排的队。`demo::setup` 走的开机仪式把时钟、串口、日志通道与 1kHz 的节拍都备齐了。`LedPin::init` 负责把灯的脚立起来。`launch` 的活是把 blinker 挂进任务的名册,末了的 `start_scheduler` 一出手,CPU 就交到了调度器的手里,点灯的活从此归任务的节拍管。

烧录的事,笔者当时的操作我是引存档的:配置里加了 `flash-<name>` 的烧录目标,一行 OpenOCD 的命令把 ELF 从 ST-Link 推进 Flash,烧完复位了,PC13 上的小灯按秒一明一灭。灯亮没亮我如今替您看不了,而手上没有板子,不过固件本身我在仿真里跑过,Renode 的串口窗里打出了 blink 的那行启动打印,启动的路子是通的。

## perf:让出者写时间戳,醒来者读

秒表上了手,该量切换了。量法在 perf demo 的头文件注释里写得清楚,咱们照它念:两个同优先级的任务 ping 和 pong 互相对让,让出的人在让出之前读一次计数,存进了一个变量,对面醒来的时候再读一次,而把两个数一减,差出来的就是 “从让出的一瞬间到对方跑起来” 的完整代价。关键的设计是测量完全待在任务体内:

```cpp
// external yield measurement: timestamp stored by the yielder, read by the
// waker — zero kernel involvement, DWT only touched in task bodies
volatile std::uint32_t g_t0 = 0;
volatile bool g_ping_turn = true;
```

为什么这么讲究?因为存档记了头一轮的教训。当年第一版把 DWT 的读取直接塞进了切换函数和临界区里,为的是实时追踪最大值,结果每一次读寄存器都是一次总线访问,来回两次的读加比较加存储,热路径上多背了大约 80 个周期。存档里的原话是 “相当于用秒表称秒表”。咱们在嵌入式上算是对观测者效应有了当场的见识:表自己太重了,称出来的东西就全虚了。新的写法是把表从内核手里拿走,内核的热路径上一个测量钩子都没有,而表只被任务自己摸。

两个任务体的代码都不长,咱们一个一个念。ping 是让出的那一头,它读表、存下出发的时刻、让出去,几步都在循环的体内:

```cpp
void ping(void*) {
    for (std::uint32_t i = 0; i < kRounds; ++i) {
        while (!g_ping_turn) {
        }
        g_t0 = ZerOS::board::dwt_cycles();
        g_ping_turn = false;
        ThisTask::yield();
    }
}
```

咱们看见全局的 `g_ping_turn` 当接力棒,接力棒轮到了谁,跑的就是谁。ping 等接力棒到了手,读一次表记下出发的时刻,把接力棒放下了,跟着就让了出去。

咱们再念 pong,它是接棒的那一头,量出来的数全归它收:

```cpp
void pong(void*) {
    for (std::uint32_t i = 0; i < kRounds; ++i) {
        while (g_ping_turn) {
        }
        const auto elapsed = ZerOS::board::dwt_cycles() - g_t0;
        if (elapsed > g_yield_max) { g_yield_max = elapsed; }
        if (elapsed < g_yield_min) { g_yield_min = elapsed; }
        g_yield_sum += elapsed;
        g_ping_turn = true;
        ThisTask::yield();
    }
}
```

pong 醒来读它的表,减掉 ping 存下的出发时刻,差出来的就是 “从让出的一瞬间到对方跑起来” 的完整代价,归进的是最大、最小、总和的三个格子,再把接力棒交了回去。两组各让了一千轮,咱们拿 `g_yield_sum` 除以 `kRounds`,出来的就是均值。数字的统计全在任务的侧,而谁也不碰内核。

报表归的是第三个任务 reporter,它睡了半秒,等两组跑完了再打出来。它里面有一段估算的路线,值得您多看一眼:

```cpp
    // estimate PendSV-only: yield_path includes our DWT read (~4 cycles)
    // + the spin-loop check (~3 cycles) + full yield/PendSV/context_switch
    // a reasonable floor for the asm+entry overhead is ~40 cycles (fixed)
    constexpr std::uint32_t kAsmOverhead = 40; // stmdb+ldmia+exception entry/exit
```

reporter 想用的算法,是拿 yield 的全程减去 40 个周期,再估一个纯切换的开销出来。而存档后面亲自宣判了该估算的路线:那个 40 是拍的脑袋,估出来的数算不上独立测量,判定的活儿它干不了。代码就留了下来,perf demo 的版本号停在 v0.2,存档自己把它挂成了反面教材,咱们看报表的时候还会碰见它一次。

## 报表:两轮数字,逐字对回存档

现在咱们把存档里的数字请出来。以下两段的报表是 `perf_1.md` 记的原文,出自真正的开发板,我照单全收了,一个数也不改的。头一轮的报表,是测量代码还在内核里的年代:

```text
=== ZerOS Performance @ 72MHz ===
rounds: 1000

[1] PendSV single switch (context_switch only):
    worst: 210 cycles (2.92µs)  budget: 72 = 1µs

[2] yield full path (yield call -> other task runs):
    min: 479 cycles (6.65µs)
    avg: 489 cycles (6.79µs)
    max: 729 cycles (10.13µs)

[3] worst BASEPRI window (longest critical section):
    worst: 678 cycles (9.42µs)

D12 verdict (switch <=1µs): FAIL
```

yield 全程的均值是 489 个周期,折算成了 6.79 微秒。预算线 D12 要的是一次切换不超过一微秒,而一微秒就是 72 个周期,报表当场判了 FAIL。不过咱们读它得带着存档的免责声明:第 [1] 项的 210 和第 [3] 项的 678,里面混着测量代码自己的大约 80 个周期,是当不得真实值引用的,真正能信的是第 [2] 项,它的测量虽然在任务的侧,不过内核当时还背着测量代码跑。

而慢的根子在哪儿?咱们跟着存档归了三条因。头一条是测量的自身,就是上面说的那大约 80 个周期。

第二条的因,是切换函数里做了一层多余的保护:咱们在 C 侧再设了一层 BASEPRI,可 PendSV 的裸汇编进门第一句 `cpsid i` 已经屏蔽了所有可配置优先级的异常,保护的是同一批东西,就属于重复上锁的写法。

第三条的因,咱们看见就绪队列走的是链表,任务让出了就要排回队尾,而链表没记尾巴在哪儿,咱们就得回回从头走到尾,两个任务虽然只多走了一步,加上指针追踪的内存访问,算下来的代价是大约 30 个周期。

`e2b7dc0` 对着三条因各动了一刀,咱们于是有了第二轮:

```text
=== ZerOS Performance @ 72MHz ===
rounds: 1000

[1] yield full path (external DWT):
    min: 354 cycles (4.92µs)
    avg: 354 cycles (4.92µs)
    max: 543 cycles (7.54µs)

[2] non-assembly remainder (rough estimate, not independently measured):
    est: 314 cycles  budget: 72 = 1µs

D12 verdict: see [1] avg — that's the number users feel
```

均值从 489 降到了 354,存档的算法是降幅 28%。您留意 min 和 avg 显示成了同一个数,存档自己声明过了,这是整数除法截断的显示效果,max 上的尾巴有延迟的样本混着,存档声明它当不得最坏情况(WCET)的引用,而 WCET 是英文里最坏执行时间的缩写。第 [2] 项的 est 314,就是上面 `kAsmOverhead` 那段反面教材的产物,而报表里那句英文声明(not independently measured),跟存档的话是一对儿。存档还替 D12 的判定换了口径,该看的是哪项?报表自己答了,看 [1] 的均值,那才是用户感受得到的数。

三条因的成效,按存档自己的改动归因排列是这样的:测量代码移出了热路径,省下的最大一笔,是 28% 的降幅,免锁的调度排在其后,尾指针的 O(1) 队列记的是 -6%。有一件事我要给您说清:大约 15 与大约 30 的两个数,存档给的身份是诊断的数,而逐项实测的收益,要等下一篇的四格实验矩阵才拆得干净,`e2b7dc0` 是一个 commit 把三件事一起落地的,今天的 489 和 354 是整批改动的两头。

## 内核侧动了哪儿:免锁的选人与有了尾巴的链表

三刀里有两刀落在了内核,咱们看代码。头一刀动在了 `scheduler.hpp`,`pick_next` 拆成了两个:

```cpp
    [[nodiscard]] base::BorrowedPtr<TCB> pick_next() {
        irq::CriticalGuard guard{driver_};
        return pick_next_locked();
    }

    // Same as pick_next, but the caller PROMISES interrupts are already
    // masked (PendSV context: cpsid i in the naked asm does it). Skipping
    // the redundant BASEPRI round-trip saves ~15 cycles per switch.
    // always_inline so it folds into the caller's section (.ramfunc when
    // called from the RAM-resident context_switch — zero flash wait states)
    [[gnu::always_inline]] [[nodiscard]] base::BorrowedPtr<TCB> pick_next_locked() {
```

普通的调用者走 `pick_next`,咱们看见它照旧进了临界区再干活,而 PendSV 的路径走 `pick_next_locked`,调用的人承诺中断已经屏蔽过了,裸汇编里的 `cpsid i` 干的就是这件事,所以临界区的进出一趟(BASEPRI 的一设一清)可以整个省掉,按仓库注释自己的算法,省下的大约是 15 个周期。而 always_inline 的属性让它折进了调用者的代码段里,连调用的跳转都不剩。

第二刀动在了 `self_list.hpp`,咱们看见链表添了一个尾巴的指针,入队的动作从走链变成了直接接尾巴:

```cpp
    // O(1) append — no walk, tail is right there. Use when order doesn't
    // matter (round-robin queues); the sorted insert above is for deadlines.
    void push_back(BorrowedPtr<Stuff> new_stuff) {
        new_stuff->next_ = nullptr;
        if (tail_ == nullptr) {
            stuff = new_stuff.get(); // list was empty
        } else {
            tail_->next_ = new_stuff.get();
        }
        tail_ = new_stuff.get();
    }
```

链表从一个指针变成了两个,而这个代价被作者写进了编译期断言的消息里,咱们照面念:

```cpp
static_assert(sizeof(SelfList<zeros_impl::Probe>) == 2 * sizeof(zeros_impl::Probe*),
              "SelfList = head + tail (tail buys O(1) push_back)");
```

断言的消息本身就是一句文档:头加尾的结构,尾巴买来的是 O(1) 的 push_back。谁哪天把结构改了,而编译器会把这句话原样拍进报错里,咱们谁也赖不掉,设计的前提和实现就绑在了一起。存档诊断里走链的代价是大约 30 个周期,省下的就是从头走链的那段路。调度器侧的 `push_back`、`push_front`、`pop_highest` 也都标了 `always_inline`,而它们只被热路径调,内联进去后调用的开销归零。

第三刀押在了代码的摆放位置上,咱们看 `switch.cpp` 给 `context_switch`、`SVC_Handler`、`PendSV_Handler` 标了一个宏:

```cpp
// RAM-RESIDENT: the hot switch path runs with zero flash wait states
// (72MHz + 2WS means every flash fetch is 3 cycles; RAM is 1 cycle)
#define ZEROS_RAMFUNC __attribute__((section(".ramfunc"), noinline))
```

推理看着是很顺的:Flash 在 2 个等待周期下取一条指令要的是三个周期,而 RAM 只要一个,热路径上的四十来条指令,理论上能省下的就是大约 80 个周期。链接脚本配合着改了,`.ramfunc` 段的加载地址留在 Flash、运行地址落在 RAM,启动的时候拷过去。我要提醒您,存档记它是第三轮的押注,而第三轮的实测给它泼了冷水,搬 RAM 反而慢了,那一出反转是下一篇的主角,今天咱们只要知道开关在这儿就行。

## 验收

本篇的可查证事实,我归了四件,咱们一件一件过。

头一件要过的是 host 侧的测试。我把仓库克隆到 `/tmp` 的副本、checkout 到了 `e2b7dc0`,照 README 的路子配了 host 的测试构建,`ctest` 的真实输出是 14 个测试全部通过,总共只花了 0.22 秒。GPIO 的 concepts 栈是纯头文件的代码,而在 host 上编得过,概念的面就立得住。

第二件要过的是交叉编译,咱们拿同一份代码用 `arm-none-eabi-g++ 16.2.0` 按仓库的工具链文件编固件,`size` 的真实输出是:blink 的 text 4060 字节、bss 2100 字节,perf 的 text 5188 字节、bss 2108 字节。64K Flash 加 20K RAM 的板子,而两个固件离边界都很远,测量的装置没有把固件撑爆。

第三件要过的是 Renode。我摘掉了本机 Renode 1.17.0 装不出的平台描述(摘的就是上面列的那几块,而平台其余的部分照原样),blink 与 perf 两个固件都起了机,串口打出了 `ZerOS demo: blink` 与 perf 的报表,监视器读 RCC 的控制寄存器得 `0x0A020083`,第 17 位的 HSERDY 在固件出手之前就是 1,假 RCC 探测器的判据在仿真里当面对质过了。perf 的报表 min、avg、max 全是 0,CYCCNT 在仿真里是不走的,所以仿真给不出周期数的事,我是拿输出验的,而不是拿猜想。

第四件要过的是本篇的边界,我照实说:489 与 354 的两轮报表,大约 80、大约 30、大约 15 的三个归因数,出处全在仓库的 `documents/notes/perf_1.md`,那是笔者当年在 Blue Pill 上,借 ST-Link 加 OpenOCD 的路子,亲手量出来的。您手上要是没有板子,这篇您照读,每个数字都在的,只是暂时复现不了,而手上要有板子,烧了 `flash-perf` 之后,咱们在串口 115200 上等半秒,报表就会自己来了,不过它给您的数字不会跟存档一模一样,均值几个周期的漂移是正常的,存档记的也是它那一轮的数。

## 下一篇

拿到了 354,咱们手里的刀却还没有收完。热路径搬 RAM 的理论收益还押在代码里,而 Flash 与 RAM 到底谁快,教科书的说法和硅片的表现打起了架。而下一篇《教科书输给预取缓冲器》,用 RAM 与 Flash 乘走链与尾指针的四格实验矩阵,把第三轮的负优化当场翻出来,合计从 489 一路走到了 333,再把一本诚实的实验册该怎么写教给您。咱们那儿见。
