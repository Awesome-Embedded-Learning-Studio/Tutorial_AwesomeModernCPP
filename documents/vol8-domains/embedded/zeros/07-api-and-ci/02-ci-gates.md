---
title: "跑五秒,说出你的台词:CI 冒烟、include 防火墙与体积闸门"
description: "d48f5a3 把站 3 到站 6 全靠手动的验收变成了自动的检查,host 单测、include 防火墙、体积天花板、Renode 冒烟四道检查收进一个 ./scripts/ci.sh,云端与本地跑的是同一套;咱们照实导览这套压平合入的 PR,把串行仿真、grep 吞错、孤儿 gitlink 三条教材教训对回脚本,末尾在 /tmp 副本把四道检查真实跑到全部通过"
chapter: 7
order: 2
tags:
  - stm32f1
  - intermediate
  - 嵌入式
  - CMake
  - 工具链
difficulty: intermediate
platform: stm32f1
cpp_standard: [23]
reading_time_minutes: 20
prerequisites:
  - "一幕一房:demo 注册中心、任务创建三条路与遗言成义务"
related:
  - "芯片死前立遗嘱:HardFault 现场、栈金丝雀与零分配日志"
---

# 跑五秒,说出你的台词:CI 冒烟、include 防火墙与体积闸门

前面讲调度器、互斥、队列与事件组的文章,验收段落写的都是同一类活:您 checkout 到某个 commit,配一遍咱们的构建,敲上了几条命令,把串口日志里的数字跟文章对上一遍。全是手动的。我在《一幕一房:demo 注册中心、任务创建三条路与遗言成义务》里跟您一起,把 342 行的单体 example 拆成了八个一幕一房的 demo,拆完了之后,手动的验收就该退休了,因为要手动对的东西从一份日志涨到了八份。commit `d48f5a3` 干的就是这件事,它把四道检查收进了一个脚本,交给云端的 CI 去跑,而同一个脚本咱们在本地也能跑,咱们这一篇就把它从头到尾导览一遍。

有一处咱们照实交代:这个 commit 的 subject 写的是 “Merge pull request #1 from Charliechen114514/feat/ci_ready”,GitHub 把 PR 压平之后合进了 main,git 对象里只剩下了 `0054cf8` 一个 parent,`feat/ci_ready` 分支上的原始提交也就没有留下来。我在本机的仓库里,按我手边的 commit 解剖档案记下的 `9dd03a8` 与 `6aa1e96` 找过(档案住在我自己的工作笔记里,仓库里没有它的原件,您也看不到,我这里照实交代),git 直接报了 unknown revision。所以这一篇没有可以逐行走的 diff,咱们按系统导览来讲:落地的改动是 8 个文件、237 行新增、11 行删除,咱们要读的,是 `.github/workflows/ci.yml` 加 `scripts/ci.sh` 里的四道检查,每一处我引用的文件,都是拿 `git show d48f5a3:<路径>` 对着真实内容核过的。

## 一套脚本,两个运行现场

咱们看 workflow 的开头,它是整套 CI 的目录卡:

```yaml
name: ci
on:
  pull_request:
  push:
    branches: [main]

jobs:
  build-and-test:
    runs-on: ubuntu-24.04
```

任何 pull request、任何进 main 的推送,都会让 ubuntu-24.04 的 runner 触发同一个 job。这个 job 的结尾是四步,每步调的还是咱们上面说的同一个脚本:

```yaml
      - name: host unit tests
        env:
          CXX: g++-14
        run: ./scripts/ci.sh host

      - name: include hygiene
        run: ./scripts/ci.sh hygiene

      - name: cross build + size limits
        run: ./scripts/ci.sh size

      - name: renode behavior check
        run: ./scripts/ci.sh smoke
```

一行注释把这么写的理由带了出来:`# -- one step per check: Actions UI folds each into its own section --`,它原样躺在 workflow 的注释里。GitHub Actions 的页面会按 step 折叠日志,一道检查占了一个 step,绿了黄了,您一眼就能看出来。哪道检查把构建拦下了,页面直接告诉您,您点进去看失败的时候,也不用咱们在日志墙里翻。

工具链的版本是照着现实确定下来的。workflow 顶部的注释我原样搬给您,它是作者给自己写的备忘,也给每个来看 CI 的人交了底:

```yaml
# ZerOS CI — runs ./scripts/ci.sh on a Linux runner, one step per check.
#
# Toolchain reality check (2026-09-06):
#   xPack has NOT released GCC 16 yet (latest: arm 15.2.1, host 15.2.0).
#   Local dev uses Arch GCC 16.1 — but no 16-specific features are used
#   (D4 floor: GCC 14 for deducing this). CI runs on:
#     host: ubuntu-24.04 apt g++-14 (zero download, always there)
#     arm:  xPack arm-none-eabi-gcc 15.2.1 (pinned tarball)
```

咱们读这三行脚注,它们说的是同一件事:仓库自己是新鲜的,可外部的依赖没跟上。xPack 还没有发出 GCC 16 的版本,作者本地 Arch 用的 16.1,不过没有用到任何 16 才有的特性,所以 CI 全部落在了 14 与 15 上:host 侧 `apt` 装 g++-14,一条命令就到了位,什么都不用下载。arm 侧拿的是 xPack 的 15.2.1,版本固定下来的 tarball,Renode 用的还是 1.16.1 的 portable 包。注释里的 D4 是本项目给“支持到 deducing this 的编译器地板”定的预算编号,地板画在 GCC 14,CI 的选择没有踩到地板以下。

云端跑的脚本与本地的是同一份,咱们看 `ci.sh` 自己的开头写明了的用法:

```bash
#!/usr/bin/env bash
# ZerOS CI — runs all checks. Use before every commit:
#   ./scripts/ci.sh            everything
#   ./scripts/ci.sh smoke      just one: host | hygiene | size | smoke
# Any check failing turns the exit code non-zero. The GitHub workflow
# runs this same script on a clean checkout.
```

`set -euo pipefail` 站在了门口,任何一道检查失败了,退出码就变成了非零,workflow 的 step 跟着变红。您单独跑每道检查也行,您敲 `./scripts/ci.sh host`,得到的就是只跑 host 单测。咱们在验收的段落会真的把整份脚本跑一遍,那里有全部的真实输出。

## 头一道检查:host 上的单测

host 侧的检查最短,咱们看它的全部代码:

```bash
check_host() {
    banner "1/4 host unit tests"
    cmake -B "$HOST_DIR" -DZEROS_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug >/dev/null
    cmake --build "$HOST_DIR" -j"$NCPU" 2>&1 | grep -E 'error|warning' && fail "host build" || true
    ctest --test-dir "$HOST_DIR" --output-on-failure
}
```

它配了一个 Debug 的 host 构建,开了 `-DZEROS_BUILD_TESTS=ON`,然后把构建的活儿交给了 `ctest`。咱们在 host 考场看过的那些用例,位图选人、丢唤醒、互斥的提升、队列的满环、定时器的回调内自杀,全部在这道检查里给咱们站岗。`ctest` 带着它的 `--output-on-failure`,红了的时候,失败用例的输出会整个打出来。`grep -E 'error|warning'` 的那一行,它身上背着咱们的一节教材,咱们到教训的段落再回来找它。

## 第二道检查:grep 当 include 防火墙

第二道检查的名字叫 hygiene,它防的东西有意思,咱们看代码:

```bash
check_hygiene() {
    banner "2/4 include hygiene"
    # kernel headers must not know boards or vendors
    if grep -rnE '^\s*#\s*include.*(src/board|third_party|stm32)' include/ZerOS/kernel/; then
        fail "kernel purity: board/vendor include found in include/ZerOS/kernel/"
    fi
    # kernel unit tests must not bind to any architecture
    if grep -rn 'ZerOS/arch/' test/; then
        fail "test purity: kernel tests must stay architecture-free"
    fi
    # the include/ZerOS/ root stays empty — everything files into a subsystem
    if ls include/ZerOS/*.hpp >/dev/null 2>&1; then
        fail "layout: headers found directly in include/ZerOS/ root"
    fi
    echo "clean: kernel purity / test purity / layout"
}
```

头一条 grep 查的是 `include/ZerOS/kernel/` 底下的每一个头文件,只要 `#include` 的行里出现了 `src/board`、`third_party` 或者 `stm32` 的字样,检查就把咱们的构建拦下来。它守的是依赖的方向:内核的头不得认识任何一块板卡、任何一家厂商。您回想咱们看过的互斥剧场、队列与定时器,内核的代码从头到尾没出现过 Blue Pill 的名字,板级的东西全部住在 `src/board/` 里,靠的是函数指针与板包接线跟内核对话。方向一旦反了,比如哪个内核头图省事 include 了板上的头,内核就再也搬不到第二块板子上了,而且这样的改动在编译上往往没有症状,host 上编得过它的,板上也编得过它的,坏掉的却是架构。grep 把它变成了文本事实的检查,它花不了咱们几个字,而且不依赖任何编译器的选项。

第二条管的是 host 测试的纯洁性:`test/` 底下不许出现 `ZerOS/arch/` 的引用。咱们要内核的单测跟架构解耦,它测的对象是位图、队列、调度器的逻辑,某颗核的汇编不在它的射程里。测试文件一旦伸手去抓 arch 的头,它就只能活在某一种架构上了,host 考场也就名存实亡了。

第三条管的是目录的规范:`include/ZerOS/` 的根目录必须保持为空,任何头文件都得归进某个子系统的目录里。它防的是图省事的堆放,新写的头往根目录一扔,三个月后咱们谁也说不清它归谁管了。三条检查跑完了,脚本报出了一句 `clean: kernel purity / test purity / layout`,这句咱们在验收的输出里会见到。

## 第三道检查:体积天花板

第三道检查做了交叉构建,再给每个 demo 的整镜像量了体积,咱们看它:

```bash
# regression ceilings (bytes). Not the D12 kernel budget — that needs a
# symbol-level split — these catch WHOLE-IMAGE regressions per demo:
# baseline 2026-09-06 max was ~5.3K flash / ~2.6K ram; headroom ≈ 2x
FLASH_LIMIT=12288
RAM_LIMIT=6144
```

脚本一开场就把天花板的口径写清了,咱们值得把这段注释念完。它划清了两件事的边界:D12 是本项目给内核体积定的预算编号,要按预算查内核的话,得做符号级的拆分,这道检查干不了,它抓的是另一件事,每个 demo 的整镜像回归。flash 12KB、RAM 6KB 的数字是怎么来的,注释也交了底:2026 年 9 月 6 日当天,八个 demo 里最大的一个约是 flash 5.3KB、RAM 2.6KB,在这上面留出大约一倍的余量,天花板就定在了 12288 与 6144 字节上。

余量为什么留一倍,而不是贴着基线卡线?因为它的职责是抓回归,按预算逐项审内核的那类检查,它就管不着了。谁的改动让某个 demo 的镜像涨了二百字节,大概率是一次不小心的内联或者一张忘删的表,天花板立刻就红了,作者在合并之前就得解释这二百字节花在了哪里。咱们要是把线画在 5.3KB 上,正常的波动天天误报,警报很快就没人在听了。留了一倍,误报归了零,真正的回归照样当天现形。

量体积的办法在检查的本体里,咱们看:

```bash
    shopt -s nullglob
    elves=(build/src/board/stm32f103_bluepill/example/*/zeros-demo-*.elf)
    [ ${#elves[@]} -gt 0 ] || fail "no demo ELF produced"
    for elf in "${elves[@]}"; do
        read -r text data bss < <(arm-none-eabi-size "$elf" | awk 'NR==2{print $1, $2, $3}')
        flash=$((text + data)); ram=$((data + bss))
        name=$(basename "$elf" .elf)
        printf '%-28s flash=%-6d ram=%-6d\n' "$name" "$flash" "$ram"
        [ "$flash" -le "$FLASH_LIMIT" ] || fail "$name flash $flash > $FLASH_LIMIT"
        [ "$ram" -le "$RAM_LIMIT" ] || fail "$name ram $ram > $RAM_LIMIT"
    done
```

交叉构建完了,它把注册中心生成的每个 `zeros-demo-*.elf` 挨个交给 `arm-none-eabi-size`,`awk` 取出它的 text、data、bss 三段,再按咱们在首切的文章里交代过的口径换算:flash 用的是 text 加 data,RAM 用的是 data 加 bss。每个 demo 报一行自己的体积,谁的数字越过了线,失败信息里连 demo 的名字带两边的数字一起报出来。一行注册自动生成的固件,在这里被咱们逐个过了秤,咱们的注册中心跟体积检查咬合得刚刚好。

## 第四道检查:每个 demo 欠咱们一句台词

轮到最后一道检查了,它做的是 Renode 冒烟,它的出发点写在了 banner 里,`each demo must SAY its line`,每个 demo 都欠咱们一句台词。台词表是一张关联的数组,咱们来看看:

```bash
    declare -A MARK=(
        [heartbeat]="Hello! ZerOS"
        [event_stream]="got 'a'"
        [inversion]="inheritance paid off"
        [bottom_half]="job #1 done"
        [timers]="one-shot: fired"
        [events_notify]="both keys in"
        [testaments]="alive"
        [round_robin]="rr2 alive"
    )
```

脚本顶上的注释把台词表的性质说了:`the one string each demo owes us when it works`,demo 正常的时候欠咱们的字符串。八个 demo 的台词我都能对回前面的文章:heartbeat 的招呼是 `Hello! ZerOS`,inversion 剧场里 H 在 L 放锁之后拿到的就是那句 `inheritance paid off`,bottom_half 的 `job #1 done`、timers 的一枪、events_notify 的双钥匙、round_robin 的第二声,都是咱们在各自的剧场里亲眼见过的行。行为级的验收就这样被压成了一条 grep:仿真跑五秒,demo 没把自己的台词说出口,咱们的检查就挂了。

`testaments` 的台词只有一句 `alive`,它的注释交代了原因,咱们看:

```bash
    # the one string each demo owes us when it works; testaments runs calm
    # (its 'x'/'y' death scripts stay manual, see docs/simulation.md)
```

遗嘱 demo 咱们见过的,平时是安静的,它的两出死法(按 `x` 执行未定义指令、按 `y` 砸自己的金丝雀)是留给《芯片死前立遗嘱》的 Renode 段落手动做的实验,CI 里只验了它活着,死法就不进自动的检查了。

跑法上有一个细节值得咱们照实交代,看脚本的注释:

```bash
    # simulate sequentially — a CI runner has 2-4 cores, 8 concurrent
    # Renode instances starve each other and virtual time barely advances;
    # 5s per demo × 8 = ~40s total, acceptable for CI
```

八个 demo 是串行跑的,一个跑完了再跑下一个。原因写在注释里:GitHub Actions 的 runner 的核数只有 2 到 4 个,八个 Renode 实例一起开了,它们就会把彼此给饿死了,虚拟时间几乎走不动了,五秒的仿真拖到超时也跑不完。串行的代价是每个 demo 五秒、八个共约四十秒,注释认为咱们付得起这笔钱。这个教训是怎么摸出来的,咱们到教训的段落再细说。

每个 demo 的输出去了哪,也值得咱们看一眼。冒烟用的启动参数把日志指到了独立的文件:

```bash
            cd "$root" && renode --console --disable-xwt \
                -e "logFile @${tmpdir}/${demo}.log" \
                -e "\$bin=@${elf}" \
                -e "include @${resc}" \
                -e "start" -e "sleep 5" -e "quit" \
                > /dev/null 2>&1
```

`logFile` 指到了临时目录里按 demo 命名的文件,跑完了之后,脚本逐个文件 grep 各自的台词。查完的裁决也讲究:哪个 demo 没说话,失败信息带上期待的原句,再把出错的日志末尾 30 行整个倒到标准错误里,您点开红的 step,案发现场就摆在了您的眼前,也不用再去翻日志文件了。给每个 demo 建独立日志的手法,跟同一次改动里 `run-renode.cmake` 的修改是一路货,咱们看它:

```cmake
# 时间戳文件名:每次运行一个独立日志,不做 .1 .2 .3 轮换——事后翻现场
# 时一眼定位是哪一次跑出来的,不是考古一堆同名编号
string(TIMESTAMP _ts "%Y%m%d-%H%M%S")
set(_log "${RENODE_LOG}")
if(_log STREQUAL "")
    set(_log "renode-${_ts}.log")
endif()
```

`run-<demo>` 目标在本地跑 Renode 的时候,日志文件名带了时间戳,不做 `.1`、`.2` 的轮换。注释把理由说完了:事后翻现场,咱们一眼定位是哪一次跑出来的。给证据标上了时间戳,给失败倒了日志,可见这套检查是花了心思的,咱们接着看的几条教训,恰好也都是围着这件事转的。

## 三条教材,对回脚本

本站的档案里记了三条教训,它们不在干净的正道里,全在走错的路上。原始的修复 commit 随着分支压平没有留下来,我没法拿逐行的 diff 给您看现场,能做的是把每条对回现存的脚本与 workflow,看它们在今天的代码里留下了什么形状。

### 串行是逼出来的

冒烟段落的注释把答案写在了那里:runner 的核数只有 2 到 4 个,八个并发的 Renode 互相饿死,虚拟时间几乎走不动了。档案记的经过是,PR 里前前后后六个 fix commit 才把这个事实摸实:一开始大概是并发跑的,虚拟时间不走了,仿真超了时,作者以为 Renode 挂了,试了各种参数,末了才承认是自己的并发在挨饿。六个 fix commit 的现场我核不到,这话您听着得打个折,不过现存的代码支持档案的说法:预构建的循环注释写着 `sequential, no lock contention`,仿真的循环注释写着 `simulate sequentially`,两个地方都特意标注了串行,像是作者跟后来的咱们打了招呼,提醒着咱们,别走了“好心”改回并发去的老路。

### grep 过滤日志,真错误跟着一起被滤掉了

现在回到 host 检查里我留下的那一行:

```bash
    cmake --build "$HOST_DIR" -j"$NCPU" 2>&1 | grep -E 'error|warning' && fail "host build" || true
```

size 检查里还有同款的一行。它的用意是省日志:构建的输出几百行,过滤了一遍,只留下带 error 与 warning 的行,平时是安静的,出事的时候几行就够了。可是这个形状连着咱们要说的两个失手点,咱们一个个看。过滤了之后,构建输出的正文被整份丢掉了,编译真的死掉的时候,报错的上下文进了过滤器的下水道,咱们能看到的最多是匹配上的几行,案发的语境全没了。`|| true` 又把管道的非零退出码抹平了,哪怕构建死得连一行含 error 的输出都没有,这个检查行也照样放行了,真正的失败要等到下一行的 `ctest` 变红、或者 size 段落报出 `no demo ELF produced` 才浮出来,浮出来的地方离案发的地方隔着一整段。

咱们对照着看,冒烟段落的失败路径是另一副样子:失败的时候倒 30 行原始日志,一个字不过滤。本站的教材里记着“grep 过滤日志吞过真错误”的条目,意思应当就是 build 检查吃过这个形状的亏,错误文本被滤掉了,排查的人手里只有一行 `FAILED: host build`。档案之外的现场我核不到,这句我照实说。现存脚本里的两行 `grep` 还挂着,它们对绿路径是省了日志的,红路径靠的是后面的 `ctest` 与 ELF 计数兜底。要按冒烟的思路改,就得把构建日志整份留存、失败的时候整个倒出来,这是它欠着的改进,我按脚本现在的样子报告,咱们不替它圆场。

### 孤儿 gitlink:sparse 之后留下的洞

workflow 里最特殊的一步,做的是给 submodule 瘦身的活,咱们看:

```yaml
      - name: sparse the submodule to CMSIS only + fetch the orphaned gitlink
        run: |
          cd third_party/STM32CubeF1
          git sparse-checkout set --no-cone \
            '/Drivers/CMSIS/Include/**' '/Drivers/CMSIS/Device/**' '/Drivers/CMSIS/LICENSE.txt'
          git checkout
```

开仓库的时候,官方 SDK 的整个 submodule 被埋了进去,档案记的约数是 131MB,固件真正用到的只有 CMSIS 的头,瘦身了之后约剩 13MB。咱们把 sparse-checkout 一设,仓库里只检出 CMSIS 的 Include、Device 与许可证三样,CI 克隆的开销立减了一个数量级。瘦身要立得住的话,还得看用头文件的地方认不认,同一次的改动里,example 的 CMakeLists 把 include 路径从 `${CMSIS}` 变量改成了显式的 submodule 路径,`Drivers/CMSIS/Include` 与 `Drivers/CMSIS/Device/ST/STM32F1xx/Include` 两处给直写了,跟 sparse 留下的两样东西一一对应,路径的收窄与检出的收窄是相干的。

不过这一步的名字里挂着另一个词,`orphaned gitlink`,它就是咱们说的第三条教训。`Drivers/CMSIS/Device/ST/STM32F1xx` 在上游的 `.gitmodules` 里没有映射,是个没人认领的孤儿 gitlink,咱们用 `git submodule update --init` 拉不动它,sparse 了之后,那个位置就成了一个空洞,编译找 CMSIS 设备头的时候直接失败。workflow 的补救是手动把它拉回来:

```yaml
          git clone --filter=blob:none --no-checkout \
            https://github.com/STMicroelectronics/cmsis-device-f1.git \
            Drivers/CMSIS/Device/ST/STM32F1xx
          git -C Drivers/CMSIS/Device/ST/STM32F1xx fetch --depth 1 origin master
          git -C Drivers/CMSIS/Device/ST/STM32F1xx checkout c8e9a4a4
```

单克隆上游的 cmsis-device-f1 仓库,`--filter=blob:none` 不下载文件的内容,`--depth 1` 只取了一层历史,checkout 到了 `c8e9a4a4`,跟 third_party/README.md 记下的版本一致。这套手动步骤不止写在了 workflow 里,`third_party/README.md` 也写了完整的一份,它的标题就是“孤儿 gitlink”,咱们在自己机器上复现的时候照着敲就行。给 AI 写的步骤与给人写的步骤是同一步,这算是稀罕的好习惯。

## 本地实测:同一套检查,咱们自己跑一遍

脚本与 workflow 都导览完了,咱们把最要紧的那句话验掉:`./scripts/ci.sh` 在本地真的能四道全过。我的复现链路是这样的:拿 `git archive d48f5a3` 把那棵树导到 /tmp 的副本里,再把 CMSIS 的 Include 与 Device 两样拷进副本的 third_party(真实的 ZerOS 仓库里 submodule 是个 gitfile 工作树,归档导不出它的内容,这是从首切的文章以来咱们一直在用的老处理),末了把 `bluepill.repl` 里的 i2c1、i2c2 两个节点注释掉。i2c 那一步也是老处理了,本机的 Renode 1.17.0 认不出 repl 里的 `I2C.STM32F4_I2C` 类型,咱们从讲首切的文章起每一篇都交代过,demo 不经过 i2c 的路径,行为是不受影响的。

版本的事我得拆成两句照实说。CI 用的是 arm-none-eabi-gcc 15.2.1 加 Renode 1.16.1,我本机是 Arch 的 arm-none-eabi-gcc 16.2.0 加 Renode 1.17.0。检查的逻辑不依赖版本的细节,可是下面贴的数字是本机这套版本跑出来的,云端 runner 上的数字我没见过。GitHub Actions 的云端行为,咱们这一篇没有真实跑过,workflow 的部分是按脚本与档案做的导览,这点咱们得说清。

跑整份脚本的时候,咱们把 `./scripts/ci.sh` 一个参数也不带,得到的就是四道全跑。头一道的成绩单:

```text
100% tests passed out of 14

Total Test time (real) =   0.23 sec
```

14 个测试目标全部通过了。考卷从位图排到了类型表,咱们在 host 考场见过的全在里面了,总耗时落在了 0.23 秒上。第二道一行就交卷了:

```text
clean: kernel purity / test purity / layout
```

第三道把八个 demo 的体重挨个给咱们报了出来,这是它真实的输出:

```text
zeros-demo-bottom_half       flash=5068   ram=1076
zeros-demo-events_notify     flash=4744   ram=2540
zeros-demo-event_stream      flash=4588   ram=2516
zeros-demo-heartbeat         flash=3812   ram=2460
zeros-demo-inversion         flash=4596   ram=2452
zeros-demo-round_robin       flash=3536   ram=2444
zeros-demo-testaments        flash=4592   ram=2476
zeros-demo-timers            flash=3428   ram=444
```

咱们拿天花板对一下:最大的 flash 是 bottom_half 的 5068 字节,离 12288 的线还有一倍多的余量,最大的 RAM 是 events_notify 的 2540 字节,离 6144 的线也远。跟脚本注释记的 9 月 6 日基线(约 5.3KB flash、2.6KB RAM)再对一下,今天的数字在量级上贴着基线,咱们看不出大幅膨胀的迹象。不过话说回来,基线出自 CI 的 GCC 15.2.1,我的数字出自本机的 16.2.0,跨编译器的对比是有保留的,说到这个仓库有没有悄悄胖过的问题,咱们给的只是强迹象,给不出的是定论。第四道的输出是一排期待值:

```text
--- run-bottom_half (expect: job #1 done)
--- run-events_notify (expect: both keys in)
--- run-event_stream (expect: got 'a')
--- run-timers (expect: one-shot: fired)
--- run-inversion (expect: inheritance paid off)
--- run-round_robin (expect: rr2 alive)
--- run-heartbeat (expect: Hello! ZerOS)
--- run-testaments (expect: alive)
all 8 demos spoke their lines
```

八行的期待值,咱们逐行对的,八个 demo 都把自己的台词说出了口。脚本的最后一行是 `ALL CHECKS PASSED`,咱们的退出码拿到了零。从按了回车到四道全过,咱们在本地也就花了几分钟,大头是八个五秒的仿真。验收的段落里,咱们把能逐条核对的事实归拢一下。

## 验收

咱们把本篇的可查证事实归拢成三件。

头一件是本地的复现。您 checkout 到 `d48f5a3`,给 third_party 备好 CMSIS 的 Include 与 Device 两样,您照 `third_party/README.md` 的两段走,头一段是初始化的,给了咱们 submodule init 加 sparse set,再是孤儿 gitlink 的一段,给的是设备头仓库的手动拉取,咱们两处都走到了,然后跑了 `./scripts/ci.sh`。host 该给您的是 14 个目标全部通过,hygiene 报出来的该是 `clean: kernel purity / test purity / layout`,size 打出来的八个 demo 的体重,都得在咱们的 12288 与 6144 以内,smoke 的收场该是 `all 8 demos spoke their lines` 加一句 `ALL CHECKS PASSED`。我本机 GCC 16.2.0 加 Renode 1.17.0 跑出来的,就是上面贴的这些。i2c 两个节点的注释处理,您要是 Renode 1.16.1 就不需要了才对。不过我照实说,CI 在云端跑没跑过、1.16.1 认不认得 i2c 的类型,两件事我没验过,我的推断是,作者多半在 1.16.1 上跑通过了它,不然他自己的冒烟就过不去了,这个推断的帽子我戴着,您自己取舍。

第二件是每道检查防什么,咱们对回脚本就能查。host 检查跑的是 `ctest`,防的是逻辑回归。hygiene 的三条 grep,防的是 include 的方向,也是 host 测试的架构纯洁性,还有根目录的堆放。size 检查按 `flash=text+data`、`ram=data+bss` 的口径给每个 demo 过秤,防的是整镜像的体积回归。smoke 检查让每个 demo 跑了五秒,grep 各自的台词,防的是行为回归。四道各管了一面,咱们把它们合起来,得到的才是完整的含义。咱们手里的一套代码,得在 host 单测、交叉构建与仿真行为的三层里全部通过,这句话才算落了地。

第三件是没验的与没核到的,我照实摆。GitHub Actions 云端的执行我没跑过,workflow 的导览依据是它今天躺在仓库里的文本,工具链的下载、Actions 页面的折叠效果,这些云端行为您得自己推一次 PR 才见得着。PR 分支的九个原始 commit 在压平合并后没有留下来,串行的教训只有脚本的注释与档案的记载撑着,六个 fix commit 的现场我核不到。脚本里两处 `grep -E 'error|warning'` 的过滤与 `|| true` 的抹平还在,它们对失败现场的吞没,是脚本形状本身能推出来的,不过的话,“曾经吞过哪一次真错误”我没有一手的证据,只有档案的记载,我把两者分开摆给您。

## 下一篇

安全网立起来了,咱们后面动刀才有底气。按解剖档案记的口径,`e2b7dc0` 与 `fe270d9` 两次砍调度热路径的时候,当时的记录是 smoke 与 size 当了回归的判据,改完了跑一遍 `./scripts/ci.sh`,四个 banner 全部通过了,咱们的刀就没砍偏。性能测量的 demo 后来也进了 CI,咱们把它当成了普通公民。照实地说,它进的是构建加体积的检查,拿 `d48f5a3` 的脚本对一眼,冒烟台词表里没有它的名字,测量的开关默认关着,也不拖累咱们每个人的五秒。咱们就去看砍的是什么,热路径上省出来的每一微秒,怎么拿 Renode 的虚拟时间量出来。
