---
title: "教科书输给预取缓冲器:.ramfunc 反转与一本诚实的实验册"
description: "教科书说 RAM 取指零等待,搬 RAM 该快,我在真正的开发板上按四格实验矩阵(代码位置乘队列结构)实测:RAM 版的 377 周期反而比 Flash 版的 354 慢,负优化当场翻了脸,回滚成宏开关之后,Flash 加尾指针把 yield 全程从 489 一路压到了 333 周期。本篇跟着仓库 documents/notes/perf_1.md 的五轮实测走完全程,再讲这本实验册的三条诚实条款:测量污染声明、整数截断声明、拒绝横向排名"
chapter: 8
order: 2
tags:
  - stm32f1
  - advanced
  - 嵌入式
  - 优化
difficulty: advanced
platform: stm32f1
cpp_standard: [23]
reading_time_minutes: 24
prerequisites:
  - "把秒表焊进硅片"
related:
  - "拔掉心跳做实验:NO_TICK、僵尸任务与以 patch 随库的探针"
current_status:
  title: 教科书被实验册反驳
  detail: ZerOS的篇章正在打磨ing
  variant: polishing
  icon: 📚
---

# 教科书输给预取缓冲器:.ramfunc 反转与一本诚实的实验册

上一站收尾的时候,咱们手里的 yield 全程均值的读数是 489 周期,把测量代码挪出内核的热路径之后,它就降到了 354。降了 28%,我当时挺得意的,可是 354 离 D12 那个 1 微秒的预算还远得很(72MHz 下,1 微秒就是 72 个周期的量),咱们的刀还得继续动。动刀的方向,教科书早就替咱们写好了:RAM 取指是零等待的,Flash 取指是要带等待周期的,所以把切换的热路径搬进 RAM,天经地义地该快。

`3dc60bf` 的 subject 写的是 “feat: optimize the sched performance”,跟它前一个 commit 的名字是同一个。我把它的 diff 数过一遍:改动了 2 个文件,新增了 184 行,删掉了 17 行,而且代码侧真正动的,只有 `switch.cpp` 里的一处宏定义。名字是优化的名字,身体却是回滚的身体。为什么一个理直气壮的优化,做出来是一场反转呢?因为咱们把项目放到了真正的开发板上,而真硅是不背教科书的书的。本篇跟着仓库 `documents/notes/perf_1.md` 的五轮实测走完全程,看完反转的机理,再看这本实验册教咱们的另一件事:数字怎么记,才算诚实的记录。

有一件事我得摆在最前面:文中每一个周期数,都来自仓库的实测存档 `perf_1.md`,它们只在真正的开发板上验过,板子用的是 Blue Pill,芯片是 STM32F103 的,主频跑的是 72MHz。我现在手上没有那块板子,板上的步骤都按笔者当时的走法转述,当时的走法是:咱们烧录好固件,等报表从串口交回来了再收,复现的路子我会写清楚,可是您要亲手验那些数字,就得自己备一块板和一套烧录的环境。我能替您跑的部分(host 测试、交叉编译、Renode 仿真),本篇的末尾有一节专门交代,跑出来的输出我一字不改地贴。

## 一条来自教科书的许诺

许诺的算式,咱们照存档里的口径摆出来。STM32F103 的 Flash 在 72MHz 下要插 2 个等待周期,照手册的口径,CPU 每取一条指令都是要等上一等的,等的是那 2 个等待周期,一条指令到手总共花的是 3 个周期。SRAM 这边是没有等待的,取一次花的只是 1 个周期的钱。切换函数 `context_switch` 的身子大约有 40 条指令,假如每一条指令都得从 Flash 里现取的话,那么搬进 RAM 之后每条能省 2 个周期,理论上省的就是 80 个周期上下。

咱们把 354 减掉 80,算出来是 274 的样子。这个数听着是让人心动的,而且心动得不冤:等待周期是硬件手册白纸黑字写着的,SRAM 的零等待也是,推出来的每一步都站得住。我在看到实测数字之前,是把这次搬家当成白捡的便宜来期待的。算式是没有输的,输掉的是一条咱们谁都读过、谁都没想到的前提,它就藏在这个算式的第一个假设里:每一条指令都得现取。这里欠下的问题,咱们到机理的部分再回来收。

## 把热路径搬进 RAM:.ramfunc 与它的搬运工

搬家的手法,咱们照 `e2b7dc0`(上一站那个 commit)落地的版本看,它给三个热路径函数都挂了属性:

```cpp
#define ZEROS_RAMFUNC __attribute__((section(".ramfunc"), noinline))

extern "C" ZEROS_RAMFUNC std::uint32_t* context_switch(std::uint32_t* saved_sp) { ... }

extern "C" ZEROS_RAMFUNC [[gnu::naked]] void SVC_Handler() { ... }
extern "C" ZEROS_RAMFUNC [[gnu::naked]] void PendSV_Handler() { ... }
```

`section(".ramfunc")` 是 GCC 的属性,它请编译器把函数的机器码放进取名叫 `.ramfunc` 的段,而不是默认的 `.text`。`noinline` 是不让别的函数把它内联掉,内联了的话,这段代码就散进了别人的函数里,段属性也跟着失效了。接收段的一头是链接脚本,我在 `link.ld` 里核对过它的原文:

```ld
    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data*)

        /* RAM 常驻热路径:上下文切换在这里跑,零 Flash 等待周期 */
        . = ALIGN(4);
        _sramfunc = .;
        *(.ramfunc*)
        . = ALIGN(4);
        _eramfunc = .;

        . = ALIGN(4);
        _edata = .;
    } > RAM AT > FLASH
```

咱们看行尾的 `> RAM AT > FLASH`:段的运行地址(就是 VMA)在 RAM,加载地址(就是 LMA)却在 Flash。机器码是烧进 Flash 里存的,上电之后得靠咱们把它搬进 RAM,CPU 执行的才是 RAM 里的代码。搬它的是启动代码里现成的拷贝循环,咱们看 `startup.cpp` 的开头(节选):

```cpp
    auto src = reinterpret_cast<std::uintptr_t>(&_sidata);
    auto dst = reinterpret_cast<std::uintptr_t>(&_sdata);
    const auto dend = reinterpret_cast<std::uintptr_t>(&_edata);
```

咱们看拷贝循环的本职:它搬的是 `.data` 的初值,从 Flash 的 `_sidata` 一直拷到 `_edata`。而 `.ramfunc` 在链接脚本里就住在 `.data` 块的内部、`_edata` 的前面,所以它搭了 `.data` 的车,一次拷贝全包了,启动代码是一行都不用加的。这也是 `e2b7dc0` 在 `link.ld` 里只加 8 行的原因:段的摆放交给了链接脚本,搬运交给了现成的循环,热路径函数只要挂一个属性就算搬了家。

到了第三轮,板上的活儿是这样的:咱们烧录好固件,reporter 半秒后交回来的报表就是咱们要的读数了。第三轮的输出,存档的原文我按原样贴在下面:

```text
=== ZerOS Performance @ 72MHz ===
rounds: 1000

[1] yield full path (external DWT):
    min: 376 cycles (5.22µs)
    avg: 377 cycles (5.24µs)
    max: 579 cycles (8.04µs)
```

354 变成了 377。数字是不降反涨的,涨了 6%。笔者跑了三遍,三遍的数字是稳定的,负优化也是稳定的。教科书给咱们指了一条慢路。

## 四格矩阵:两个变量谁也别想混在一起

数字翻了脸,咱们就得管归因了。第三轮的实验里其实挤着两个变量:代码住在哪(Flash 还是 RAM),队列怎么插人(走链还是尾指针)。尾指针是 `e2b7dc0` 同一个 commit 里落地的另一项优化,`SelfList` 从单指针涨成了双指针,`push_back` 里从头走到尾的遍历换成了直接在尾部接上。静态断言把这次的布局改动写成了编译期的文书(`SelfList = head + tail (tail buys O(1) push_back)` 是它的原文),一个指针的代价,换来的是插入不用走链。

两个变量挤在同一轮里的话,涨出来的 6% 就说不清了:是搬 RAM 搬坏的,还是换队列换坏的?所以笔者把矩阵补齐了,位置乘队列的四个格子,咱们一格一格地填。第四轮是 RAM 加尾指针的组合,第五轮把 `.ramfunc` 拆掉了,函数回到了 Flash 上执行,而尾指针是留着的。第四轮和第五轮的输出,存档的原文如下:

```text
=== ZerOS Performance @ 72MHz ===
rounds: 1000

[1] yield full path (external DWT):
    min: 349 cycles (4.85µs)
    avg: 349 cycles (4.85µs)
    max: 537 cycles (7.46µs)
```

```text
=== ZerOS Performance @ 72MHz ===
rounds: 1000

[1] yield full path (external DWT):
    min: 333 cycles (4.63µs)
    avg: 333 cycles (4.63µs)
    max: 529 cycles (7.35µs)
```

四个格子凑齐了,咱们把它们摆成一张表(单位都是 yield 全程的平均周期,数据来自 `perf_1.md` 的实测存档):

| 代码位置 | 就绪队列 | yield avg | 相对基线 |
| --- | --- | --- | --- |
| Flash | 走链 O(n) | 354 | 基线 |
| RAM | 走链 O(n) | 377 | +6%,负优化 |
| RAM | 尾指针 O(1) | 349 | -1% |
| Flash | 尾指针 O(1) | 333 | -6%,全场最优 |

表就是本篇的正文,别的段落都是在给它做注脚。读它的次序也有讲究。竖着比的话,RAM 是慢于 Flash 的,第三轮的反转被四格里的另外两格再次确认了。横着比的话,尾指针在两种位置上都赚了,可是它赚的多少,得看它跟的是谁。咱们单独看第四轮,349 比 354 只好了 5 个周期,尾指针的实得收益远小于咱们的直觉。把它跟 Flash 搭在一起的话,333 才把它的本事露了出来。存档里的总计是 489 到 333,一路压下来的是 32%,而三个有效手段的排序是存档自己给的。测量代码移出热路径是最大的一笔,降了 28%。PendSV 的免锁调度省掉了重复的 BASEPRI 往返,省下的往返值约 15 个周期。队列的尾指针插入是 -6%。

## 反转的机理:预取缓冲器为什么赢

咱们现在回去收前面欠下的问题:算式的第一个假设,说的是每条指令都得现取。STM32F103 的 Flash 接口不这么干活,它带一个预取缓冲器(英文名 prefetch buffer):CPU 执行当前指令的时候,Flash 接口会把排在后面的指令提前读出来备着,轮到 CPU 来取下一条的时候,预备好的指令不用再等那 2 个等待周期,直接就交货了。顺序执行的代码,大部分取指都被这个缓冲器兜住了,手册上那个 3 周期一条的算式,在顺序执行的现实里多数时候是不成立的。

这就解释了为什么搬 RAM 反而慢:热路径的指令本来在 Flash 里被预取缓冲器伺候得好好的,几乎没有真实的等待。搬进 RAM 之后的话,预取的好处整个消失了,而且 RAM 从此要同时伺候两张嘴,CPU 来它这里取指令的时候,数据访问也要经过它的总线,两边的竞争把每张嘴都拖慢了。存档里还列了两个候选机理,分支布局的变化、编译器生成的 veneer(ARM 里的一种小段转接代码)与 literal pool 的对齐变化,存档自己的措辞是“尚未完全隔离”,我把四个候选照原样端给您:预取失效与总线竞争的机理是清楚的,布局和对齐的两条,存档也是没敢把它们说死的。

有一处我要照实声明拿不准:预取缓冲器在 STM32F1 上的具体深度和宽度(一次预取多少位、缓冲几个条目),我没有查过 RM0008 参考手册的原文,本文只用了“它会把后面的指令提前备好”这个方向性的口径,细节您要引用的话,自己去翻手册里 Flash 存储器与接口的那一章核实。存档写在 `switch.cpp` 回滚注释里的原话,说的是 Flash 的预取缓冲器以 16 周期之差胜过了零等待 RAM。16 是存档拿第四轮的 349 对第五轮的 333 比出来的,两轮的队列都是尾指针,只换了代码的位置。377 减 333 混着队列的变量,那不是 16 的出处,这个口径我不替它圆,怎么对上的您自己比表。

适用域是存档划得很清楚的,咱们得守住它。这个判语只在当前的配置下成立,也就是 72MHz 加 2 个等待周期的组合。换芯片、换频率的话,等待周期的数目就变了,预取缓冲器的相对身价也跟着变了,权衡就得重测了。所以回滚没有把代码删干净,留的是一个可以复测的装置。

## 回滚也是产出:宏开关留下的装置

`3dc60bf` 在 `switch.cpp` 里动的宏定义,咱们看回滚之后的样子(核对过 commit 里的原文,注释是它自己的):

```cpp
// Switch hot path stays in FLASH (not .ramfunc): real-hardware measurement
// (documents/notes/perf_1.md, round 3 & 5) shows RAM placement is a
// NEGATIVE optimization on STM32F103 @72MHz — the flash prefetch buffer
// beats zero-wait-state RAM by 16 cycles. If a future chip/config changes
// the tradeoff, re-measure before re-enabling.
// #define ZEROS_SWITCH_IN_RAM  (kept for reference; do NOT enable on F103)
#ifdef ZEROS_SWITCH_IN_RAM
#define ZEROS_RAMFUNC __attribute__((section(".ramfunc"), noinline))
#else
#define ZEROS_RAMFUNC
#endif
```

宏从无条件定义变成了开关:默认走 `#else` 分支,`ZEROS_RAMFUNC` 是空的,三个函数回到了 Flash。哪天真换了一块预取缓冲器不灵的芯片,咱们把 `ZEROS_SWITCH_IN_RAM` 定义回去就行,`.ramfunc` 的整条搬运链(段属性、链接脚本、启动拷贝)一样都不用重写,测一遍就知道新的权衡站在哪边。注释里的 “do NOT enable on F103” 说的是硬话,这硬话是用两个 commit 的实测数字换来的,拍脑袋是拍不出来的。

咱们这一路把 commit 当进步的单位看惯了,这一篇是个反例:commit 的 subject 写着 optimize,内容却把上一个 commit 的一处改动撤了。回滚也是工程产出的一种,因为支撑它的是一套完整的测量,留下的开关和注释是可复测的实验装置。下一次有人在这块板子上动优化念头的时候,这个装置会替他省掉重走一遍弯路的代价。

## 诚实的实验册怎么写

`perf_1.md` 在这个 commit 里从 44 行的原始笔记重写成了 203 行的完整实验册,行数翻了一番还多,多出来的部分大半在讲对数字的限定,新数字是没有多出几个的。这本册子教人的写法,我给您归成了三条。

测量污染的事,存档给了最重的标记,我是拿它当第一课读的。第一轮的输出里有 `[1] PendSV single switch: worst: 210 cycles` 一段,还有 `[3] worst BASEPRI window: worst: 678 cycles` 的读数。两个数在存档里被打了不可引用的标记,原因是当时的测量代码(读 DWT 计数器)就塞在内核热路径里,读一次寄存器要走的是一遍总线,一个来回吃掉了约 80 个周期,数字里泡着测量工具自己的开销,存档的原话是“用秒表称秒表”。污染过的数字不标声明,后来的人(包括三个月后的作者自己)拿它当基线,整条优化链就从错了的起点出发了。第一轮真正可用的只有 `[2]`,yield 全程的 min 479、avg 489、max 729,因为它的测量点在任务里,不泡在内核的路径中。

数字的成色,我看存档也一条条说明白了。第二、四、五轮的 min 和 avg 都显示成了同一个数,333 报的就是 333,这里头是没有巧合的,只有整数除法的截断,均值是除了 1000 轮的,小数是被切掉的。第三轮的 min 376 与 avg 377 差了一个周期,走的也是整数除法的截断口径。max 的情况倒是正好相反,它比 min 大出了五六成,存档的判定是这些延迟样本疑似 SysTick 的中断干扰,但是此轮未验证,所以它明确写了 max 不应当作 WCET(最坏情况执行时间,考核实时性的正式指标)来引用。这个 commit 阶段的归因还没做完,存档就把没验证三个字放在了数字的旁边,猜的归猜的,测的归测的。

不够格的数字,存档是不让它进判定的,我读到这里是服气的。从第二轮的报表起,里面带着的估算路线 `[2] non-assembly remainder: est: 314 cycles`,它是 yield 全程减一个拍脑袋的汇编开销常数(约 40 周期)得出的,存档给它的判词是“不是独立测量的结果,不能用于 D12 判定”。D12 预算说的是“上下文切换不大于 1 微秒”,存档把两种口径都写了出来:按 yield 全程的口径算,读出来的是 4.63 微秒,判的是 FAIL。按 PendSV 本体的口径算,当时还没有单独测量的手段,判的是 UNVERIFIED。两种口径都是不支持达标的,存档没有挑一个对自己有利的口径交卷。横向的那一边,别的 RTOS 报的切换数字,测量边界、配置、编译器是各不相同的,存档的原话是暂不作横向性能排名,要比的话,就在同一块 Blue Pill、同一编译器和相同的测量代码下比。

三条归出来其实是同一种态度:实验册的读者,是换了一块板子、隔了半年回来的作者自己,那时的他已经不记得当时的板子、当时的配置、当时的犹疑了。数字旁边不写清成色的话,几个月后它就是一条来路不明的旧读数,用是用不得的,扔也扔不得的。

## 我能替您跑的部分

板上的数字咱们复现不了,可是这个 commit 还有相当一块是能真跑的,我把跑的过程和输出照实贴出来,它们跟存档的数字是两回事,我只拿它们验代码本身。

头一件我跑的是 host 测试。我把 `3dc60bf` 的源码树导出到了 /tmp,照 README 的路子配了 host 构建,本机的 GCC 是 16.2.1,`ctest` 的 14 个目标全部通过,跑完的总耗时是 0.29 秒:

```text
100% tests passed out of 14

Total Test time (real) =   0.29 sec
```

跟本篇正文最贴的两个测试,咱们单独看成绩。`test_self_list` 交出的成绩是 8 个用例、32 条断言全过,它守的是带尾指针的双向结构本身。`test_sched` 交出的成绩是 14 个用例、70 条断言全过,它守着 `pick_next_locked` 与免锁调度的那套约定。咱们可以说,四格矩阵里赚周期的两项数据结构改动,host 上的行为验证是绿的。

第二件我跑的是宏开关两版配置的体积对比,这是本篇我最有底气替您跑的一段,因为开关就是这个 commit 留下来的装置。交叉编译器用的是我本机的 arm-none-eabi-g++ 16.2.0,默认配置里开关是关着的,热路径是留在 Flash 的。开了 `ZEROS_SWITCH_IN_RAM` 的时候,热路径就进了 RAM。`size` 给咱们各报了一版:

```text
# 默认(Flash):
   text	   data	    bss	    dec	    hex	filename
   4704	    484	   2108	   7296	   1c80	build-fw/src/board/stm32f103_bluepill/example/perf/zeros-demo-perf.elf
# 开 ZEROS_SWITCH_IN_RAM(RAM):
   text	   data	    bss	    dec	    hex	filename
   5188	      0	   2108	   7296	   1c80	build-fw-ram/src/board/stm32f103_bluepill/example/perf/zeros-demo-perf.elf
```

dec 一列两版是完全相同的,都是 7296 字节:固件的总脚印没有变,搬 RAM 搬的只是代码住的位置,`.text` 缩掉的 484 个字节进了 `.data` 的领地,它们在两版的分列里换了住处:Flash 版把它记在 data 的名下,RAM 版被 size 记进了 text,`.ramfunc` 是跟着 `.data` 块一起搬的,分列的口径是 size 工具自己的,咱们看 dec 这一列就够了。落位有没有真的发生,符号表替咱们作了证:

```text
# RAM 版:nm 里三个热路径符号全在 SRAM 段(0x2000 开头)
200001e4 T context_switch
200002c4 T SVC_Handler
200002dc T PendSV_Handler
# Flash 版:同样的三个符号回到 Flash(0x0800 开头)
08000bf4 T context_switch
08000cd4 T SVC_Handler
08000cec T PendSV_Handler
```

我盯着两份符号表看,宏一开一关的工夫,符号的住址就跟着搬了家,这个开关的装置是真的在工作。

第三件我跑的是 Renode。仿真的环境我得交代两处改动:本机的 Renode 1.17.0 认不出 `bluepill.repl` 里两个 i2c 节点用的 `I2C.STM32F4_I2C` 模型,我把 i2c1、i2c2 连同它们的中断连线整块注释掉了,perf 这个 demo 不过 i2c,行为是不受影响的,跟前几篇的处理一致。perf demo 在仿真里是全自动跑的,咱们起仿真、等 15 秒收日志就行。它确实一秒不差地把整张报表打了出来,可是报表上的周期数全是 0:

```text
[INFO] usart1: [host: 0.61s (+0.25ms)|virt:   0.5s (+0s)] === ZerOS Performance @ 72MHz ===
[INFO] usart1: [host: 0.61s (+0.1ms)|virt:   0.5s (+0s)] rounds: 1000
[INFO] usart1: [host:  0.61s (+0.17ms)|virt:   0.5s (+0s)] [1] yield full path (external DWT):
[INFO] usart1: [host: 0.61s (+0.31ms)|virt:   0.5s (+0s)]     min: 0 cycles (~0us)
[INFO] usart1: [host: 0.61s (+0.21ms)|virt:   0.5s (+0s)]     avg: 0 cycles (~0us)
[INFO] usart1: [host: 0.61s (+0.21ms)|virt:   0.5s (+0s)]     max: 0 cycles (~0us)
```

全零在我看来倒是个证人,它作证的是本篇的论点:DWT 的周期计数器靠时钟树的真实节拍涨数,Renode 里的时钟是假 RCC 那一套(上一站咱们装过探测器抓它),指令是按事件驱动往前推的,周期数压根就没有在走了。测量的前提是真实节拍,仿真给不了,所以本篇的所有周期数都只在真正的开发板上验过,仿真只能证明的是程序跑得通,证明不了快慢。这也正是存档把数字和口径一并记下来的原因:测量的地皮只有那么一块,数字得在地皮上长着才作数。

## 验收

咱们把本篇可查证的事实归拢成三件。

头一件是存档里的反转与矩阵。`documents/notes/perf_1.md` 的第三、四、五轮给出了四个格子的全部读数:354、377、349、333,外加第一轮的基线 489 与总计的 -32%,每个数我在正文里都是逐字引的存档。它的适用域声明(仅当前配置成立)与回滚注释(“do NOT enable on F103”)在 `3dc60bf` 的 `switch.cpp` 原文里,您 `git show 3dc60bf:src/arch/arm_cortex_m3/switch.cpp` 就能对着看。

第二件是我替您跑的验收链。host 测试的 14 个目标全过了,`test_sched` 交的是 14 用例 70 断言的成绩,`test_self_list` 交的是 8 用例 32 断言的成绩,宏开关的两版配置我都交叉编译过了,dec 两版是同为 7296 字节的,`nm` 里三个热路径符号随开关在 `0x2000` 与 `0x0800` 之间搬了家。您要复现的话,导出源码树、配两次 cmake 就行了,开 RAM 版的时候,把 `-DZEROS_SWITCH_IN_RAM` 加进 `CMAKE_CXX_FLAGS` 就可以了。

第三件是本篇没验的东西,我照实摆。四格矩阵的周期数我一块板也没有,它们只在真正的开发板上验过,复现需要 Blue Pill 加 ST-Link 的烧录环境。反转的机理一段,预取失效与总线竞争的口径跟存档和手册的方向是一致的,缓冲器的深度宽度、布局与对齐的两个候选,存档自己标了尚未完全隔离,我也没有替它补测,您拿不准的地方跟我拿不准的地方是同几处。

下一篇《拔掉心跳做实验:NO_TICK、僵尸任务与以 patch 随库的探针》把测量往前再推一步:关掉 SysTick,把 max 尾巴的嫌疑人当场审出来,还从 NO_TICK 的路径里挖出了两个一直潜伏的 bug,测量探针也从主线剥成了以 patch 随库的装置。存档里那个 UNVERIFIED 的 PendSV 本体,到它那儿有了净 93 周期零方差的读数,它们在后续的 commit 才补进存档,存档的现行版里能看到,本篇冻结在 `3dc60bf` 的五轮上。咱们那儿见。
