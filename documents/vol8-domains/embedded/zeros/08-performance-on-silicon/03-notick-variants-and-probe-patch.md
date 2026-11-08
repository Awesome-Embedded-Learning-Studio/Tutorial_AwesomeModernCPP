---
title: "拔掉心跳做实验:NO_TICK、僵尸任务与以 patch 随库的探针"
description: "fe270d9(feat: rollback perf codings as patches)给性能线收官:demo::setup 拔掉 SysTick 做变异性测试,当场挖出两个潜伏 bug——任务入口返回后落进的 Trap 原地 wfi 霸占 CPU(有 tick 时被时间片遮掩),wait_notify 大跨度的 deadline 无符号回绕成过去(近似 forever 变成立刻醒);PendSV 探针从主线剥离成 scripts/patches/pendsv-probe.patch,忘 apply 就开开关,构建当场响亮失败;我在 /tmp 副本复现了失败与回绕,第六轮的板上读数照存档逐字对回:离群 5 归零,PendSV 本体 93 周期零方差"
chapter: 8
order: 3
tags:
  - stm32f1
  - advanced
  - 嵌入式
  - cpp-modern
  - 优化
difficulty: advanced
platform: stm32f1
cpp_standard: [23]
reading_time_minutes: 22
prerequisites:
  - "教科书输给预取缓冲器:.ramfunc 反转与一本诚实的实验册"
related:
  - "数到十就换人:同级时间片与不继承的切片债"
  - "叫醒所有人,各自重查真相:事件组、直接通知与满环背压"
  - "一秒钟的定义权:tick、回绕算术与到期队列"
---

# 拔掉心跳做实验:NO_TICK、僵尸任务与以 patch 随库的探针

写完了上一篇,咱们手里已经有了一本诚实的实验册,Flash 加尾指针把 yield 全程压到了 333 周期。可是在第五轮的 max 尾巴 529 旁边,写过实验册的您我都知道有一行小字,写的是“max 存在延迟样本,可能是 SysTick 干扰,未验证”。疑似的东西不能过夜,`fe270d9` 就是来给它结案的。我数过它的 diff,改动落在了 8 个文件上,新增了 311 行,删掉了 43 行,subject 写的是 “feat: rollback perf codings as patches”,它没有把 PendSV 的探针留在主线,而是把探针剥成了一个随库的文件 `scripts/patches/pendsv-probe.patch`。

咱们这一篇要做的事叫变异性测试,它的操作办法是故意改掉系统的一个性质,咱们看它崩在哪,崩的地方就住着潜伏的 bug。咱们改掉的性质是心跳本身,咱们把这个 `ZEROS_PERF_NO_TICK` 开关打开,SysTick 就彻底不启动了,时间服务整个就缺席了。咱们把心跳一拔,当场挖出了两个一直潜伏的 bug。头一个是任务入口函数返回之后落进的 Trap,它原地 wfi 霸占着 CPU。另一个是 deadline 的无符号回绕,近似 forever 的愿望落了个空,等来的却是“立刻醒”。bug 挖完了,探针也以 patch 的形式随了库,第六轮的读数把离群归到了零,PendSV 的本体量出来是 93 周期,而且 min、avg、max 三个数一模一样,是零方差的成绩。咱们一样一样来。

## 拔心跳的开关:with_tick

拔心跳的入口长在 demo 的开场仪式上。该 commit 给 `demo_setup.hpp` 加了参数,咱们把改过的函数整段看:

```cpp
// with_tick=false skips init_time entirely: no SysTick, nothing ticks.
// Only callers that depend on no time service may do this (the perf demo
// uses it to measure with tick interference switched off).
inline void setup(const char* name, bool with_tick = true) {
    board::clock_init();  // HSI 8MHz → PLL 72MHz — real silicon needs this
    board::uart1_init();  // after clock_init: BRR assumes 72MHz
    board::register_uart_channel(); // this board talks over UART1
    log::print("\r\nZerOS demo: {}\r\n", name);
    arch::cortex_m3::install_default_testaments(); // obligations, explicitly
    if (with_tick) {
        arch::cortex_m3::init_time(72'000'000 / 1000); // 1kHz tick
    }
}
```

咱们站在 `init_time` 那一行的外面想一圈,`with_tick` 给了 false 之后,1kHz 的 SysTick 就起不来了,内核的到期队列没有人去打,时间片也没有人去数了,整条链路上唯一能自己醒过来的中断消失了。注释里的那句 “Only callers that depend on no time service may do this” 画的是一条使用边界,它的意思是,敢拔心跳的调用者,自己得真的不依赖时间。perf demo 恰好满足了该条件,咱们等一下看它怎么把 reporter 从定时唤醒改成了被通知唤醒。

咱们看 `main.cpp` 那一侧,`main` 的开场按开关分了岔:

```cpp
#ifdef ZEROS_PERF_NO_TICK
    ZerOS::demo::setup("performance (no tick)", false);
#else
    ZerOS::demo::setup("performance");
#endif
```

咱们把开关留着不开,一切都跟上一篇一样地运转,该 demo 还是 CI 里的普通公民。开关开了,它就成了一个没有时间维度的系统,而这样的系统,恰恰是检验潜伏 bug 的放大镜。

## 僵尸任务的住址:Trap 原地 wfi

拔心跳挖出的头一个 bug,住在任务退场的路上。咱们回忆《在栈顶伪造出生证明》里的出生证明,`fabricate_frame` 在新任务栈顶伪造异常帧的时候,`lr` 填的是 Trap 的地址,任务入口函数一旦返回了,执行流就落进了 Trap 里。该 commit 之前的 Trap 长这样:

```cpp
extern "C" [[noreturn]] inline void Trap() {
    while (1) {
        asm volatile("wfi");
    }
}
```

它的意思看着挺体面:退场了,没活干了,睡吧。可是咱们逐字看一下 `wfi` 的睡法,它是 ARMv7-M 的等待中断指令,核心睡了下去,要等来了中断或者异常才醒得过来。能把它叫醒的只有 SysTick。有 tick 的系统里,每毫秒都有 SysTick 的一拍,时间片拍满的时候 `on_tick` 就会请一次切换,《数到十就换人》里咱们逐行读过的那段逻辑,会把 CPU 从该任务的手里夺走,把机会轮给了别人。僵尸霸占 CPU 的这件事,被时间片一轮一轮地遮掩了,所以谁都没有起疑。

这里咱们得停一下,把遮掩的边界也说准,因为 `on_tick` 里有一个空队列的判断,同级队列里没有别人的时候,它不做空转的切换。所以时间片救场的完整条件是,僵尸所在的那一级还有别的任务在排队。perf demo 的 ping、pong、reporter 挤在同几个级上,遮掩是成立的。可只要系统安静到了没有中断、没有别的就绪任务的地步,该任务就真的没人动得了。

NO_TICK 的构建把遮掩整个掀掉了,没有 SysTick 了,`on_tick` 也没了,没人会请切换了,该任务的入口一旦返回了,它就永远占住了 CPU,全场也就死寂了。存档里记着复现的样子,我把原文念给您:“Renode 复现,挂死后 PC 直接落在 Trap 的 wfi 循环里”。我在 Renode 里没有重跑这一幕,机理层面的证据咱们从源码里已经拿足了,它的归属我在验收段会照实分好类。

修复落在了 `switch.cpp` 里,Trap 从头文件里的小函数变成了一个正式的函数,咱们看:

```cpp
// A returned task must LEAVE the cpu, not squat on it: a plain wfi here
// would only ever be pried loose by the next tick's time slice, so a
// system without SysTick wedges outright (caught live by the NO_TICK
// perf build, 2026-09-06). Block self — the scheduler moves on, and if
// nothing else is ready, the idle task's wfi is the honest terminal
// state. A stray wake re-enters the wfi loop below, equally harmless.
extern "C" [[noreturn]] void Trap() {
    system_sched.block(system_sched.current_task());
    while (true) {
        asm volatile("wfi");
    }
}
```

改动其实只有进门的那一行 `block(current)`。退场任务把自己标成了 Blocked,调度器因此有了往下走的路,选下一个该上场的人。注释把退场即让出的语义说定了。要是全场没有别人可跑了,`pick_next` 请出来的就是 idle,而 idle 的任务体本来就是 wfi,《在栈顶伪造出生证明》里咱们见过它的出身,那样的睡着才是正确的终态,因为它表示的是“没有活干”,而僵尸的 wfi 表示的是“占着位子装睡”。还有一句注释的余味值得您看:“A stray wake re-enters the wfi loop below, equally harmless”,万一有个中断把该 block 掉的任务从 wfi 里震了一下,它醒过来了也还在循环里,再睡就是了,所以也无害。

## deadline 的回绕:近似 forever 变成立刻醒

第二个 bug 的住处离时间内核更近。`wait_notify` 带超时的版本本体就住在 `system.cpp` 里,咱们把循环的骨架搬来看:

```cpp
std::expected<std::uint32_t, sync::SyncError>
CortexM3System::wait_notify(clock::Ticks::tick_t span) {
    // re-check loop in the face: probe the mailbox, sleep through THIS
    // object's sleep_for (singletons stay private to the arm .cpp family)
    const auto ddl = system_time.current().tick_ + span;
    for (;;) {
        if (auto letter = system_sched.wait_notify(); letter.has_value()) {
            return letter;
        }
        const auto left =
            static_cast<clock::Ticks::tick_diff_t>(ddl - system_time.current().tick_);
        if (left <= 0) {
            return std::unexpected(sync::SyncError::TimedOut);
        }
        system_sched.sleep_for(system_sched.current_task(), system_time,
                               static_cast<clock::Ticks::tick_t>(left), wake_from_sleep);
    }
}
```

判超时的手法是 deadline 判余量,进门的头一件事是把 `ddl` 定成“现在的读数加 span”,咱们每一圈都拿 `ddl` 减 `now` 算一算还剩多少,余量落到零以下就报超时了。`tick_t` 是无符号的 32 位,`tick_diff_t` 是带符号的 32 位,减法本身是回绕安全的,把差值解释成带符号的数之后,负数的意思就是“ddl 在过去”。这套算术《一秒钟的定义权》里咱们练过,可是问题出在加法的那一步,等 `span` 大到了 `0xFFFFFFFF` 的时候,`now + span` 在 32 位里正好绕回了一整圈,`ddl` 落在了 `now` 的前面一拍。咱们想要的是“近似 forever”,算出来的却是“一拍之前”,余量立刻就变成了负数,函数当场就返回了超时。存档里给它的判词我照念:“想要『近似 forever』,实际『立刻醒』”,而且它被标成了时间内核待决问题(待决 6)的活案例,内核侧加不加饱和或断言的问题,当时留而未决。

该 bug 的机理出在算术上,与时钟的快慢无关,所以咱们在 host 上就能把它跑出来。我把 `system.cpp` 的三行算术原样搬进了一个小程序,是拿 GCC 16.2.1 编的,输出是这样的:

```text
span=600000     ddl=601000 left=600000 -> 正常等待
span=4294967295 ddl=999 left=-1 -> 立刻超时返回
```

`now` 同样取了 1000,10 分钟的跨度余量是整整 60 万拍,`0xFFFFFFFF` 的跨度算出来的 `ddl` 是 999,余量则掉到了 -1,当场就判了负。demo 里的规避手段也写成了 `main.cpp` 里的注释,咱们看它挑的数:

```cpp
// 10 minutes — far past this demo's lifetime. NOT the full 0xFFFFFFFF:
// the kernel arms a deadline as now + span, and that addition wraps
// around to "strictly in the past", firing the timeout at once (caught
// live on silicon + Renode). In ZEROS_PERF_NO_TICK builds the clock never
// advances anyway — the notify from pong is the only way out
constexpr Milliseconds kWaitForPong{600'000};
```

reporter 等 pong 的收工通知,给的跨度是 10 分钟,远远盖过了该 demo 的寿命,又躲开了回绕。注释还交代了 NO_TICK 构建里的另一层事实,时钟的读数永远不动,notify 是唯一的出路,所以 reporter 的等待靠的是《叫醒所有人,各自重查真相》里咱们装好的直接通知原语,定时器在这里是帮不上忙的。回绕这个 bug 是不挑构建的,有 tick 的系统里它一样会发作,只是 perf demo 平时用不着那么大的跨度,所以它才一直没露头。

## 探针为什么住 patch,不住主线

bug 挖完了,该 commit 的另一半工作是给测量设施安排住处。探针是 build-gated 的,生产构建是零开销的,可是热路径里出现了测量钩子,维护的人看着终究碍眼。存档的“测量设施的去留”一节把这个权衡写成了决定,我念给您:“主线代码剥干净,探针以 patch 随库(`scripts/patches/pendsv-probe.patch`),要测量的机器 `git apply` 再开 `ZEROS_MEASURE_PENDSV`,忘了 apply 就开开关 = 链接错误,响亮失败。数据已全部入档,patch 的使命是下次复测即贴即用。”

“响亮失败”是刻意的设计,它把配置依赖变成了构建错误,而不靠文档来提醒。咱们把 patch 的文件头看了,它自己就是一份现成的说明书:

```text
# PendSV-only probe (documents/notes/perf_1.md, round 6)
#
# The mainline keeps the switch hot path measurement-clean; this patch adds
# two branch-free DWT snapshots around the PendSV handler body (~24 cycles
# of fixed probe overhead riding the measured path — subtract it when
# reading, the perf demo already does).
#
# Usage:
#   git apply scripts/patches/pendsv-probe.patch
#   cmake -B build-meas -DCMAKE_TOOLCHAIN_FILE=cmake/arch/arm-none-eabi.cmake \
#         -DZEROS_MEASURE_PENDSV=ON [-DZEROS_PERF_NO_TICK=ON]
#   ... measure ... then:  git apply -R scripts/patches/pendsv-probe.patch
#   (reverse-apply to undo — NOT git checkout, which would also wipe any
#    uncommitted work beside the patch)
#
# Turning ZEROS_MEASURE_PENDSV on WITHOUT applying this patch fails to
# build (the demo references the stamps, nothing defines them) —
# loudly, on purpose.
```

连撤销的操作警告都写在头里,撤 patch 用的是 `git apply -R`,而别用 `git checkout`,因为 checkout 会把您放在工作区里的未提交改动一并擦掉。一份 patch 文件把自己的用法、撤销方式与失败行为全写在了自己的开头,随库躺着的它是文档,贴上去的它才是代码。

开关那侧的接线在 perf 的 `CMakeLists.txt` 里,咱们看两个 option 的默认值,个个的出厂值都是 OFF:

```cmake
option(ZEROS_MEASURE_PENDSV "perf: probe the PendSV handler with DWT stamps" OFF)
option(ZEROS_PERF_NO_TICK "perf: run without SysTick to isolate tick interference" OFF)
if(ZEROS_MEASURE_PENDSV)
    target_compile_definitions(zeros-demo-perf PRIVATE ZEROS_MEASURE_PENDSV)
endif()
if(ZEROS_PERF_NO_TICK)
    target_compile_definitions(zeros-demo-perf PRIVATE ZEROS_PERF_NO_TICK)
endif()
```

注释里写着开探针的前提,写的是“REQUIRES patch applied first”,而违反它的后果,咱们现场验过。

## 响亮失败,我在 /tmp 亲手按响过它

我在 /tmp 里用 `git archive fe270d9` 导出了一份源码树,patch 我没有 apply,`ZEROS_MEASURE_PENDSV` 开到了 ON,构建 perf demo 的命令一发,报错当场就来了,原文贴给您:

```text
/tmp/zeros-tfe270d9/src/board/stm32f103_bluepill/example/perf/main.cpp:102:30:
    error: 'zeros_pendsv_exit' was not declared in this scope
  102 |         const auto handler = zeros_pendsv_exit - zeros_pendsv_enter;
      |                              ^~~~~~~~~~~~~~~~~
/tmp/zeros-tfe270d9/src/board/stm32f103_bluepill/example/perf/main.cpp:102:50:
    error: 'zeros_pendsv_enter' was not declared in this scope
  102 |         const auto handler = zeros_pendsv_exit - zeros_pendsv_enter;
      |                                                  ^~~~~~~~~~~~~~~~~~
make[3]: *** [src/board/stm32f103_bluepill/example/perf/CMakeFiles/zeros-demo-perf.dir/main.cpp.obj] Error 1
```

这里我得照实补一句口径。存档与 patch 头里的说法是“链接错误”,我实测的失败落在了编译期,`main.cpp` 引用了两个戳记符号,而主线里没有任何地方声明它们,编译器当场报出了符号的名字。patch 头部的原文写的是 “fails to build”,构建失败这个行为是特意安排的,响亮、当场、把符号名报了出来。它保证了您没法静悄悄地测出一组带探针的假数据,目的就达到了。方向是对的,阶段是在编译期的,我把两个说法都摆在您面前,您自己掂量。

咱们补上了 patch 再构建,就一路绿了。`git apply scripts/patches/pendsv-probe.patch` 干干净净地贴上,双开关一齐开了,拿 arm-none-eabi-g++ 16.2.0 做了交叉编译,出来的体积是真实的:

```text
   text	   data	    bss	    dec	    hex	filename
   5684	    492	   2132	   8308	   2074	build-probe/src/board/stm32f103_bluepill/example/perf/zeros-demo-perf.elf
```

对了,还有一句要向您交代,源码树里的 CMSIS 厂商头是我照主 checkout 补齐的,子模块目录本身是空的,该步跟前面几篇的处理一致。

## patch 的解剖:两段无分支快照

探针的本体,咱们把 patch 里的核心片段看了:

```c
extern "C" {
volatile std::uint32_t zeros_pendsv_enter = 0;
volatile std::uint32_t zeros_pendsv_exit = 0;
}

#ifdef ZEROS_MEASURE_PENDSV
#    define ZEROS_PENDSV_PROBE_IN           \
        "ldr     r1, =0xE0001004\n"         \
        "ldr     r1, [r1]\n"                \
        "ldr     r2, =zeros_pendsv_enter\n" \
        "str     r1, [r2]\n"
#    define ZEROS_PENDSV_PROBE_OUT         \
        "ldr     r1, =0xE0001004\n"        \
        "ldr     r1, [r1]\n"               \
        "ldr     r2, =zeros_pendsv_exit\n" \
        "str     r1, [r2]\n"
#else
#    define ZEROS_PENDSV_PROBE_IN ""
#    define ZEROS_PENDSV_PROBE_OUT ""
#endif
```

`0xE0001004` 是 DWT 的 CYCCNT 寄存器地址,《`把秒表焊进硅片:时钟树爬上 72MHz,量第一刀切换`》里咱们给它解过锁。四个汇编动作做的是一遍读数一遍存,中间没有任何的分支,所以探针的开销是固定的,约在 24 个周期的量级,而这 24 个周期骑在被测的路径上,咱们读数时要把它减掉,好在 demo 把减法替您做了。两个戳记为什么包着 `extern "C"`?因为裸汇编是按符号的名字寻址的,C++ 的普通符号会被 name mangling 改名,汇编就找不着了,`extern "C"` 把名字保住了。声明的那一半落在 `switch.hpp` 的末尾,裸汇编的写入在 `switch.cpp` 里,声明与定义就分在了两个文件里,这也是忘 apply 必炸的机关所在,因为 demo 只 include 了头文件,头文件的声明只在 patch 里。

插进裸汇编的位置,patch 用的是宏拼接字符串的方式,咱们看改完的 PendSV:

```c
extern "C" ZEROS_RAMFUNC [[gnu::naked]] void PendSV_Handler() {
    asm volatile("cpsid   i\n" ZEROS_PENDSV_PROBE_IN "mrs     r0, psp\n"
                 "cbz     r0, 1f\n"
                 "stmdb   r0!, {r4-r11}\n"
                 "1:\n"
                 "bl      context_switch\n" ZEROS_PENDSV_PROBE_OUT "ldmia   r0!, {r4-r11}\n"
                 "msr     psp, r0\n"
                 "ldr     lr, =0xFFFFFFFD\n"
                 "cpsie   i\n"
                 "bx      lr\n");
}
```

快照的进落在 `cpsid i` 之后、读 PSP 之前,出口落在选完了人之后、弹寄存器之前,夹住的是处理函数的本体。开关不开的话,两个宏就退化成了空串,汇编跟主线一字不差地保留,这就是 build-gated 的“生产构建零开销”在机器层面的意思,您贴着宏展开看,空字符串什么都没添。

## 第六轮的读数:离群归零,93 周期零方差

数字该登场了,咱们把口径摆在前面。下面的读数出自仓库的实测存档 `documents/notes/perf_1.md` 第六轮,是笔者当时的操作,在 Blue Pill 上按第六轮的配置,apply 了探针、开了 `ZEROS_PERF_NO_TICK` 构建,烧进真正的开发板跑出来的。我如今手上没有板子,所以每个数字都只在真正的开发板上验过,您没有板子的话,照前面几篇的做法读数字、对出处就好。

```text
=== ZerOS Performance @ 72MHz ===
rounds: 1000
tick: OFF (no SysTick)

[1] yield full path (external DWT):
    min: 375 cycles (~5us)
    avg: 375 cycles (~5us)
    max: 385 cycles (~5us)
    outliers(>avg+128): 0 of 1000

[2] PendSV handler (probe, 24 cycles subtracted):
    net min: 93 cycles (~1us)
    net avg: 93 cycles (~1us)  budget: 72 = 1us
    net max: 93 cycles (~1us)
    raw avg: 117 cycles

[3] accounting: yield[1] - handler[2] = 282 cycles
    (API + queue + pend + exception entry/exit + waker prologue)
```

存档给它的判读有四条,我一条一条转给您,数字我原样带着。max 的尾巴收敛了,从存档末尾复测表里有 tick 口径的 532(第五轮实测的 529 与它同属一条尾巴)降到了 385,离群从 5 个变成了 0 个,5 毫秒的测量窗在 1kHz 的 tick 下,期望的碰撞次数约是 5 次,与离群 5/1000 的读数吻合,尾巴的来源从猜测变成了对得上的算术,SysTick 干扰的归因到这里有了实验证据的闭合,存档自己的读数把它叫作“归因终审成立”,我照单转述。375 含着 24 个周期的探针开销,减掉之后剩的约是 351,与有 tick 无探针版的 336 在布局抖动的范围内互相咬合,两组测量就这么互相作了证。PendSV 的本体是 93 周期,而且 min、avg、max 三个数一模一样,是零方差的成绩,存档的原话是“零方差本身就是证词:稳态切换是完全确定的路径,测量方法可信”,raw 的 117 减去探针的 24,剩下的正是 93。咱们分开看,375 的总数等于 93 加 282,处理函数的本体只占约四分之一,大头在 API 调用、排队、挂起、异常进出与唤醒者的开头几条指令,想优化 yield 的您,杠杆不在 PendSV 的手里。

预算的判读咱们也得诚实念完,D12 要求的是切换不超过 1 微秒(即 72 周期),93 超了预算约 29%。存档把 D12 的口径裁定列成了待办,按 yield 全程的口径是 FAIL,按处理函数本体的口径该不该放宽预算,留给了后面的 commit。BASEPRI 最长窗口的复测也还挂着,办法存档里写了,探针的 patch 复用即可,它下次复测即贴即用的使命就在那里等着。

## 验收

咱们把本篇可查证的事实归拢成三件,我一件一件来。

头一件是响亮失败的复现。您照本篇的路子,在任意一份 `fe270d9` 的导出树里让 patch 保持未 apply 的状态,您把下面的配置与构建命令跑一遍:

```shell
cmake -B build-meas -DCMAKE_TOOLCHAIN_FILE=cmake/arch/arm-none-eabi.cmake -DZEROS_MEASURE_PENDSV=ON
cmake --build build-meas --target zeros-demo-perf
```

编译器会把 `zeros_pendsv_enter` 与 `zeros_pendsv_exit` 的未声明报出来,报错的原文与本篇贴的一致。把 `git apply scripts/patches/pendsv-probe.patch` 补上了,同一个构建就干净地通过了,我的 /tmp 树的体积是 text 5684、data 492、bss 2132,您跑出来的数字应该跟它们对得上。

第二件是回绕的复现,它的好处是不需要任何硬件,咱们在 host 上就能跑。`ddl = now + span` 与 `left = (int32_t)(ddl - now)` 这几行算术跟 `system.cpp` 的原文一字未改,span 取了 `0xFFFFFFFF` 的话,余量就成了负的,当场就判了超时,输出在本篇的第三节。

第三件是板上的读数,我照实交代边界。第六轮的 93/93/93、raw 117、375、385、离群 0/1000,加上有 tick 版的 max 532 与离群 5/1000,全部出自存档 `perf_1.md` 的第六轮,只在真正的开发板上验过了,我本人此刻复现不了,您没有板子也一样。我能替您验的部分都验出了原文,patch 我贴平了,开关我开动了,失败的报错我按响过,回绕的算术我在 host 上跑通了,它们都落在 /tmp 与 host 的输出里。

心里还挂着的事我也记在这里。Trap 僵尸霸占的现场,我在 Renode 里没有重跑,证据是源码机理加存档记录两层的拼接。D12 的口径裁定、wait_notify 要不要饱和或断言、BASEPRI 窗口的复测,存档自己把它们留成了待办,谁接手了谁去关,去关的那天,探针的 patch 还在 `scripts/patches/` 里等着被贴。
