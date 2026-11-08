---
title: "挂牌 v0.1.0:README 即大门,四个考场挨个走到"
description: "commit 6c0a4f7(release: ready to be public)是整条手搓线的收官:13 行的 for fun! 草稿换成了完整的中文 README,为什么、有什么、快速上手、示例表一段不缺,README.en.md 与五枚徽章把门面立了起来,MIT 与 Apache-2.0 在许可段划清了边界,公开前的内部路径清洗写进了 diff;我 checkout v0.1.0 在 /tmp 副本把四个考场走到了能走的地方,host 的 15 个测试全过,Renode 与 QEMU 的心跳都到了场,开发板的点灯目标我照实交了底"
chapter: 9
order: 4
tags:
  - stm32f1
  - intermediate
  - 嵌入式
  - 工具链
difficulty: intermediate
platform: stm32f1
cpp_standard: [23]
reading_time_minutes: 20
prerequisites:
  - "睡着就调慢心跳:tickless、读清竞态与 24 位分期续杯"
related:
  - "跑五秒,说出你的台词:CI 冒烟、include 防火墙与体积闸门"
---

# 挂牌 v0.1.0:README 即大门,四个考场挨个走到

咱们上一站收尾的时候,对照的实验刚跑完,关着宏的两秒里,咱们的数字是两千次的周期中断,开着宏是零次周期加十二次的 one-shot,四次小睡的章一拍不差。内核该有的本事,到那儿就齐了,桌面上的事,咱们还剩一件没办,它从调度器落地那天起就被咱们念叨着:咱们要把仓库公开出去。commit `6c0a4f7` 干的就是它,subject 写的是 “release: ready to be public”,改动了 6 个文件,新增了 269 行,删了 8 行,`include/ZerOS/` 与 `src/` 底下的代码一个字节都没动。它是咱们整条手搓线的收官 commit,v0.1.0 的 tag 就打在它的身上,咱们今天把它走完,也把整条线收了尾。

门面的工程,咱们从旧 README 的样子说起。咱们把 `6c0a4f7~1` 里的 `README.md` 原样搬来,数了数,全文算下来是 13 行的体量:

````markdown
# ZerOS: A toy C++23 written OS

for fun!

 ```shell
# configure (cross toolchain, once)
cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/arch/arm-none-eabi.cmake

# build & run in Renode
cmake --build build --target renode-bluepill
 ```
````

“for fun!” 就占了三个词的位置,底下是一条交叉构建的命令,外加一个老的 Renode 目标名,自用仓库的门面就是这样,写它的人自己看得懂就够了。咱们到了最后一站,要看的就是同一份仓库,怎么把给自己看的卡片,换成给陌生人看的门面。

## README 的结构学:一段一段,都有它在那里的道理

新的 `README.md` 长到了 102 行,咱们按它的段落顺序走一遍,每一段为什么摆在那儿,咱们都给它一个说法。

标题底下紧跟的是一行 banner 的引用 `![banner](documents/assets/banner.svg)`,咱们手绘的那幅图在门口第一眼的位置。banner 底下是联动的声明,一行的引用块写着 “Co-Apply for Tutorial_AwesomeModernCPP”,旁边配了一行中文的“联动项目”。它排得这么靠前有它的道理:仓库从头到尾是跟着咱们的教程线长起来的,它是教程的活教材,咱们得让进门的人一眼看见它。徽章墙底下还有一行语言的切换,`[English](README.en.md) | **简体中文**`,英文的读者从它拐进另一份 README。

徽章墙跟在了后面,它的五枚咱们下一节细看。徽章底下是一句话的定位,咱们把原文搬来给您:

```markdown
> 组件定位的 C++23 裸机 RTOS,面向 Cortex-M3——无堆、无 RTTI、无异常、无 C ABI。
> 一套内核,四处运行:桌面单测、Renode/QEMU 仿真,以及真机。
```

定位段回答的是陌生人最想问的两个问题,一个是它是干什么的,另一个是它的跑道在哪儿。四条拒绝(no heap、no rtti、no exceptions、no C ABI)在定位段就报了名,咱们前几站用链接期的 `ASSERT` 与编译期的断言兑现过的约束,门面给了它们一句话的预告,您往后读源码,每一条都对得上当年的现场。

紧跟着的是 “为什么这么写” 的一节,三条的设计主张,咱们把原文搬来:

```markdown
- **RTOS 是组件,不是中心。** 应用只面向 `<ZerOS/*>` 头与 BSP 公开面,
  内核不认识任何板级寄存器;换板子不动内核。
- **裸机上能保留的全部 C++23:** concepts 约束接口、类型状态任务工厂、
  应用面 `std::expected`——热路径则保持朴素、静态、可数。
- **一套代码,四个目标:** host 单测 / Renode 仿真 / QEMU mps2-an385 / 真机。
```

主张的条数一共是三条,咱们全都亲眼见过它们兑现的现场。“换板子不动内核”的主张兑现得最响,咱们拿证据说话:《QEMU 里的同款心跳》里,咱们拿逐字的 diff 验过,heartbeat 的两份 `main.cpp`,bluepill 的一份数出来是 43 行,mps2 的一份数出来是 45 行,差出来的只有文件头的注释与一行名字字符串。该 commit(`8efe93a`)一共交了 13 个文件、445 行的新增,内核的改动是零。“能保留的全部 C++23”兑现的是类型状态的任务工厂,`task::named().prio().words()` 的建造者链,咱们在调度器的几站里用过好几回。每一条的主张都有东西撑着,每一张都能指回对应的文章,您翻回去就能对上。

“内核里有什么”的一节是能力的清单,32 级的抢占加同级的时间片轮转(位图加 CLZ)在里面,毫秒级的时间服务、动态的 tick 与同步族在里面,工作队列、日志通道、栈的金丝雀与水位也在清单里,HardFault 的遗言也在。清单的末尾带了一句体积的说法,“核心内核数 KB,最大 demo ~6KB”,括号里注明了它是 demo 实测。咱们说过,该说法有 CI 的体积闸门替它站岗,flash 12KB、RAM 6KB 的天花板每天都在称,咱们在 CI 的那篇文章里对过它的秤。

快速上手的一节给了四条路,咱们把命令抄给您。host 的两条是:

```shell
cmake -B build-host -DZEROS_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host -j && ctest --test-dir build-host --output-on-failure
```

交叉构建进 Renode 的一发,咱们看它的注释,注释把命令的活儿写全了,编、仿真加看串口的活儿一发全包了:

```shell
cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/arch/arm-none-eabi.cmake
cmake --build build --target run-heartbeat -j
```

QEMU 的一条一行就够了,咱们要敲的命令是 `cmake --build build --target run-mps2-heartbeat -j`。开发板的一条是 `cmake --build build --target flash-heartbeat -j`,走的通道是 ST-Link。四条路各对了一个考场,README 上的快速上手就是咱们教程线的入口,到了终验的段落,能走的咱们都走一遍。

示例的一节是 12 行的 demo 总表,它的格式是一场景对应一行,每个 demo 自带了 `run-<名>` 的目标。表的细目咱们留到后面的单独一节,那里有照实交代的东西。布局的一节画了目录的树,`include/ZerOS/` 是公开的 API,`src/log/` 的注释写着“格式化住在这里,永不进内核”,当年立下的分层纪律,门面上也给了它一行。许可的一节只有两句话,它的分量在下一节的下半场等着咱们。

## 双语、徽章与一幅手写的 banner

`README.en.md` 是该 commit 新立的,它长到了 110 行,与中文的版本同构:联动的声明、徽章墙、定位段、三条的主张、能力的清单、四条快速上手、示例表、布局、许可,咱们逐段对过,没有一段是少的。双语做的是入口的活,中文圈之外的读者从它进门,定位段的 “One kernel, four homes” 与中文的“一套内核,四处运行”说的是同一件事。咱们拿 `git show 6c0a4f7:README.en.md` 核过,英文版引用的命令与目标名,与中文版是逐字一致的,没有一份是过期的。

徽章墙的枚数,咱们照原文数过了,数出来的枚数是五。头一枚的牌子是 CI,底下链接的是 Actions 里的 ci.yml。再往下的一枚是 C++23,又一枚给的是 Cortex-M3。freestanding 的一枚印着 no heap · no rtti · no exceptions 的字样,末了的一枚是 MIT。每一枚的背后都有东西撑着,CI 的一枚背后是 d48f5a3 立起来的四道检查,C++23 的一枚背后是 deducing this 与 constinit 的真实使用,freestanding 的一枚背后是链接期的反制四连。徽章的工程最怕挂着自己兑现不了的牌子,咱们手里的五枚都能指到对应的文章。

`documents/assets/banner.svg` 也是该 commit 新立的,它的行数是 39,是手写的。咱们把它的构造念给您。底是一块深色的圆角矩形,衬了一层极淡的网格。图的左边是 “ZerOS” 的大标题,底下缀了一行小字,标题旁边横着一条绿色的折线,注释写明了它的身份,说它就是开场跑过的 “the first demo”,照注释的意思,咱们猜它就是 `6ee1d25` 的 bootstrap 示例打出来的心跳曲线,线的尾巴上还有一枚绿点,配了 “alive” 的字样。图的右边竖着四个胶囊(host、renode、qemu mps2、silicon),头三个描了蓝边,唯独 silicon 的边用了绿色。它的左下角是一行红字,红字的内容是 no heap · no rtti · no exceptions · no c abi,四个拒绝全写在上面了。一幅 39 行的图把整条线的元素都收了进去:心跳是开场的第一个 demo,四个胶囊是验证的矩阵,四拒绝是 freestanding 的底线,配色的心思也在,silicon 的绿色边线,咱们猜是作者留给“跑在真正的开发板上”的偏爱。

## 两张许可证,一条边界

`LICENSE` 文件是本 commit 新增的,它的行数是 21,装的是 MIT 的全文。版权行的年份是 2026,署名写的是 Charliechen114514。MIT 的条款咱们不复述,值得咱们较真的是它旁边的边界。

`third_party/` 底下的 CMSIS 器件头不是作者的东西,它们的归属是 ST,许可走的是 Apache-2.0。咱们在本机的 submodule 里核过,`Drivers/CMSIS/LICENSE.txt` 的头三行就是 “Apache License, Version 2.0, January 2004”。新 README 的许可一节只有两句话,它把边界说清了。仓库自己的代码走的是 MIT,咱们的许可证就是仓库里的 LICENSE 文件。`third_party/` 下的 CMSIS 器件头归 ST,许可走的是 Apache-2.0,引进的方式是 git submodule。

边界划得干净的缘由,藏在仓库的形状里:ST 的文件从头到尾没有进过 `LICENSE` 覆盖的地盘,它们的住处是 submodule,咱们用 sparse 的方式只留三样。留下的是 Include、Device,外加 Apache-2.0 的 LICENSE.txt。您 fork 了该仓库改自己的东西,MIT 跟着您的改动走。您用到 CMSIS 的头,Apache-2.0 的义务就跟着头文件走。两份许可各管各的地皮,README 的两句话就是它们的地契。

## 公开前的清洗:两条内部路径的改写

公开的仓库与自用的仓库有一条硬的差别,写给外人看的文字里,不许再有指向作者电脑内部结构的路径。该 commit 的 diff 里有一处干的就是它,咱们看 `third_party/README.md` 的改动:

```diff
-孤儿 gitlink(上游 bug,详见 .claude/docs/architecture.md):
+孤儿 gitlink(上游仓库把 Device 目录做成了无 .gitmodules 映射的嵌套 gitlink):
```

改前的写法把解释的责任推给了 `.claude/docs/architecture.md`,它是作者本机的项目内部文档,公开仓库的读者手里没有它,点过去得到的是空。改后的括号把话自含地写全了:上游仓库把 Device 目录做成了无 `.gitmodules` 映射的嵌套 gitlink,底下的手动拉取步骤照旧。读者不需要知道作者的书房长什么样,他们要的答案一行都在,清洗的判断标准咱们也记一笔:删掉内部路径之后,信息要是没丢的话,就是合格的清洗。

同一份的 diff 里头,`.gitignore` 也修了一处,咱们照实记。原来挤在一行的 pattern 是 `*.log.*run.log*`,咱们按形状看它的匹配,得同时含 `.log.` 与 `run.log` 的路径才命中,两类日志单独出现的场合它都接不住,所以它是坏的。咱们的改法是把它们拆成 `*.log.*` 与 `run.log*` 两行,又新增了 `qemu-*.log` 的一行,接的是 `8efe93a` 把 QEMU 引进来之后多出来的 qemu 日志。公开前的仓库,连忽略的规则都得拾掇干净,QEMU 的日志文件从今往后进不了 git 的视野。

## checkout v0.1.0,四个考场挨个走到

门面立好了,该验收了。`v0.1.0` 的 tag 是轻量 tag(lightweight,git 里最简的一路,它只是直接指向某个 commit 的别名,不带独立的 tag 对象与附注),`git rev-parse v0.1.0^{commit}` 交回来的正是 `6c0a4f7`。您拿到仓库之后,把 checkout 落到 v0.1.0 的标号上,得到的树就是咱们今天读的树。

咱们的复现链照旧走 /tmp 副本的老路,拿 `git archive 6c0a4f7` 把树导出去(third_party 下是 submodule 的 gitfile,归档导不出它的内容,该处理从首切的文章一直用到今天),再把本机 submodule 里的 CMSIS 整个拷进副本的 `third_party/STM32CubeF1/Drivers/` 底下。工具链的版本我报一下。arm-none-eabi-g++ 是本机的 16.2.0。Renode 用的是 1.17.0。QEMU 用的是 11.1.2。

host 的考场头一个交卷,咱们看它的成绩单:

```text
100% tests passed out of 15

Total Test time (real) =   0.23 sec
```

15 个目标全部过了,从位图、丢唤醒到栈水位的白盒考卷,全都在里面了。第二格的考场是 Renode,咱们把 `cmake --build build --target run-heartbeat -j` 一发敲了下去,仿真跑了五秒,串口如约打出了十对的台词,咱们摘两行给您:

```text
[INFO] usart1: [host: 2.07s (+0.5s)|virt:     2s (+0.5s)] Hello! ZerOS
[INFO] usart1: [host: 2.07s (+0.1ms)|virt:     2s (+5.2µs)] A heartbeat example!
```

第三格的考场是 QEMU,咱们敲下了 `run-mps2-heartbeat`,咱们的终端里流出了横幅 “ZerOS demo: heartbeat (mps2)”,跟着的是十对的台词,timeout 到了点把常驻的 demo 收了场,目标的退出码落在了零上。

第四格咱们得照实交底:我手里没有开发板,`blink` 的点灯在真正的开发板上,我没有跑过。仓库里能核的是这些事实:`blink` 的 demo 目录住在 `src/board/stm32f103_bluepill/example/` 底下,注册中心给每个 demo 生成了 `flash-<名>` 的目标,它调的是 openocd,带着 `interface/stlink.cfg` 与 `target/stm32f1x.cfg` 的两份配置,执行的命令是 `program ... verify reset exit`,注释写明了板子得插着,点灯类的 demo 就是它的用户。README 的快速上手把该命令列进了四条路。工具链的一环是齐的,唯独硅片上的点灯动作,得您拿着板子亲手按,该格的空,我照实留着。

## 12 行的 demo 表,对回前 25 个 commit

示例表的行数是 12,从 heartbeat 排到了 perf,`git log` 里 v0.1.0 之前的记录恰好是 25 个 commit 的分量,咱们可以说该表就是它们的一页地图。不过表的行与 commit 的对应关系,咱们不按感觉配,咱们靠的是 `git log --diff-filter=A` 的实测。

我拿 `git log --diff-filter=A` 逐个查了 demo 目录的出生 commit,记录是这样的:`0054cf8` 一次性生出了咱们今天的 8 个目录,表里一大半的行,就是从它的刀下出来的。它的 subject 里写着 seperate the examples 的字样,加新案例的意思也在句尾。342 行的单体 example,就是被它拆成了八个一幕一房的 demo。其余的出生点:`blink` 与 `perf` 的点在 `e2b7dc0`,`watermark` 与 `tickless` 的点在 `fb9072b`,mps2 板上的 heartbeat 是 `8efe93a` 添的。

所以表的 12 行对着的是 12 种的能力,每种能力都能指回讲过它的站,inversion 指回优先级继承的剧场,testaments 指回金丝雀与遗言的现场,round_robin 指回时间片的公平,watermark 与 tickless 指回本站前两篇的《把栈刷满魔法数》与《睡着就调慢心跳》,perf 指回 DWT 量切换延迟的性能线,blink 指回最早的板子上的第一盏灯。您拿该表当目录用,想看哪个 demo 的来龙去脉,顺着它对应的站号翻就是了。对应关系咱们交代细一层:八行 demo(heartbeat 到 round_robin)的拆分与注册,住在《一幕一房》那篇的篇幅里,watermark 与 tickless 两行归本站的篇 2 与篇 3,perf 一行归 DWT 量切换延迟的性能篇,mps2 板的 heartbeat 归本站的篇 1。blink 的行,咱们查到它的出生 commit 是 `e2b7dc0`,点灯本身的现场,咱们整条线里没有写过专文,本站篇 1 的交底就是它的存档视角,flash 的目标等着的是一块真正的开发板。

## 验收

本篇的可查证事实归拢成四件,咱们一件一件过。

头一件是门面本身的核对。您 checkout 到 `6c0a4f7`,旧 README(`6c0a4f7~1` 里的版本)的全文是 13 行,新的 `README.md` 是 102 行,`README.en.md` 的行数是 110,`LICENSE` 是 21 行的 MIT,`banner.svg` 是 39 行的手写 SVG,徽章的枚数是五,demo 表的行数是 12,这些数字都是 `git show` 当场数得出来的。许可边界的核对也归它:`Drivers/CMSIS/LICENSE.txt` 的头三行是 Apache License 2.0 的文字,README 许可段的两句话把 MIT 与 Apache-2.0 的地皮划清了。

第二件是清洗的对源。`git show 6c0a4f7 -- third_party/README.md` 您自己跑一遍,改前的内部路径与改后的自含写法都在 diff 里。`.gitignore` 的三行改动同样在 diff 里,坏 pattern 的拆分与 `qemu-*.log` 的新增,咱们一眼就能看全。

第三件是终验的实跑。/tmp 的副本上,host 的 `ctest` 给出了 15 个全部通过、0.23 秒的成绩,Renode 的 `run-heartbeat` 打出了十对的台词,QEMU 的 `run-mps2-heartbeat` 以 timeout 正常收了场,输出的原文都在上面的段落里。工具链的版本我再报一遍。arm-none-eabi-g++ 用的是 16.2.0。Renode 用的是 1.17.0。QEMU 用的是 11.1.2。数字出自的是这套的组合,云端 CI 的组合(g++-14 加 arm 15.2.1)的输出,我没有见过。Renode 的 repl 里 i2c 两节点的老处理依旧:1.17.0 认不出 `I2C.STM32F4_I2C` 的类型,我在副本里把 i2c 的几行注释掉了,demo 的路径不经过 i2c,行为是不受影响的。

第四件是没验的,我照实摆。开发板的 `flash-heartbeat` 我没有执行过,咱们手里没有板子,仓库里核到的是目标的定义、openocd 的两份配置文件与 `blink` 的 demo 目录,硅片上的点灯,留给拿着板子的您。banner.svg 我核的是它的 39 行源码,渲染出来的成图效果,您在 GitHub 的仓库首页看一眼就知道了。tag 的轻量属性您也能当场复验,`git cat-file -t v0.1.0` 交回来的类型是 `commit` 而非独立的 `tag` 对象。

## 尾声:从超级循环到 v0.1.0

收官的仪式走完了,咱们把整条的线回头数一遍。咱们的线一共 26 个 commit,起点是 `6ee1d25` 的 “initial feat: backbone setup”,咱们的终点是 `6c0a4f7`,subject 写的是 ready to be public,咱们的线到它为止。

起点是一个空架子加一个超级的循环:`fe5a0e8` 把 bootstrap 的冒烟跑了起来,串口打出了咱们的第一行 “Hello! ZerOS”,那时的它连任务的概念都没有,main 里打印完了就剩一句空转。接着的内存一站,咱们按位图、池、守卫三个主题讲了四篇,无堆的底线就是那时候立下的。时钟的一站把 SysTick 收进了内核,一秒钟的定义权从此有了主人。调度器的一站拆成了四篇,就绪的位图选人,PendSV 干的是首切,丢唤醒的竞态与 ThisTask 的故事也在里头,两个任务头一回在咱们眼前交接地打印。同步的一站给了咱们信号量与优先级继承的互斥,上下文的一站给了金丝雀、遗言与消息的队列,公平的一站给了时间片与事件组,API 的一站把 demo 拆成了一幕一房,CI 的一站立起了四道的检查,性能的一站上了 72MHz、量了切换的延迟,还把负优化诚实地回滚了,双板的一站把同一份内核搬进了 QEMU,又给了栈的水位与 tickless。

咱们一路数下来,没有绕开任何一个失败的现场:编不过的 concept 替身、丢唤醒的竞态、被吃掉的六个金丝雀词、被撤销的 ramfunc 优化、Renode 认不出的 i2c 类型,全都照实地写进了对应的文章。每一层都是咱们看着它从一行代码长起来的,换板的零改、水位的报表、两千次对十二次,全部的数字都是咱们亲手跑出来的。

终点的站台上停着的,就是您今天 checkout 的树。您敲了 `git checkout v0.1.0` 之后,README 的第一条命令在 host 上替您跑起 15 个测试,第二条在 Renode 里给您打出心跳,第三条进的是 QEMU,第四条等的是一块真正的开发板。整条的线从超级循环走到了这里,走成了 12 个 demo、4 个考场与 1 个 tag 的家底,后面的路,门面的“联动项目”字样会接着带您走。咱们今天就走到这里。
