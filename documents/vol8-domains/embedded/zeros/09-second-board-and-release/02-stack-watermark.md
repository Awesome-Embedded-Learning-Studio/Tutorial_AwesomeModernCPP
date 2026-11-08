---
title: "把栈刷满魔法数:金丝雀升级成水印,栈深按证据定"
description: "commit fb9072b(feat: stack watermark, tickless)的前半把栈金丝雀升了级:bury_canary 从只涂八个哨字改成把整块栈刷满 0xEF114514,watermark_free_words 自底向上扫出还空着几词,capacity_words 扣掉金丝雀占的八词报出总容量。host 白盒测试把量不准的路子和各自的界写进了用例,watermark demo 一秒后报表,deep 88 词容量、最深处用过 62 词,shallow 只用过 30 词,多出来的 32 词正好是压舱的词数。我在 /tmp 副本跑通了 15 个 host 测试目标与交叉构建,watermark demo 在 Renode 里打出了上面两张读数,《叫醒所有人》那次 eaten=6 的活捉,在这里升格成了随取随读的制度"
chapter: 9
order: 2
tags:
  - stm32f1
  - intermediate
  - 嵌入式
  - cpp-modern
difficulty: intermediate
platform: stm32f1
cpp_standard: [23]
reading_time_minutes: 22
prerequisites:
  - "QEMU 里的同款心跳:第二块板零改,兑现薄 port"
related:
  - "叫醒所有人,各自重查真相:事件组、直接通知与满环背压"
  - "芯片死前立遗嘱:HardFault 现场、栈金丝雀与零分配日志"
current_status:
  title: ZerOS的篇章正在打磨ing
  detail: 栈水印 · 等你验收
  variant: polishing
  icon: 📏
---

# 把栈刷满魔法数:金丝雀升级成水印,栈深按证据定

《叫醒所有人》那一篇收尾的时候,咱们手里有过一次活捉:事件任务的栈被压到了 64 词,金丝雀报告的 `eaten=6` 说它吃掉了六个哨字,溢出的六个词加上一段余量,96 词的新栈深就是这么量出来的。当时我得意了一小会儿,不过您回头再想,那次量法的代价不小,它得真的溢出一次,报告完就地停机,量出来的只是一个任务在崩溃边缘的读数。给别的任务定栈深,咱们还是只能拍脑袋加一段余量,平时到底用了多少,咱们手里还是没有一张随取随读的表。

commit `fb9072b` 把咱们缺的表补上了。它的 subject 写的是 “feat: stack watermark, tickless”,一个 commit 落了两个特性,我数过它的 diff,13 个文件的改动,新增了 431 行,删除了 7 行。本篇只讲前半的水位,tickless 空闲的那一半归的是姊妹篇《睡着就调慢心跳》。水位的思路您一听就懂:任务出生的时候,内核把它的整块栈涂满了同一个固定的花纹 `0xEF114514`。等它活了一阵子之后,咱们从栈底往上扫,扫到了第一个不再是花纹的词就停,数出来的就是从来没被碰过的余量。涂的是全部,读的是证据,您的栈深从此按读数定,不按感觉来定了。

不过仓库的诚实劲儿咱们领教过好几站了,这次它连自己的误差都写成了用例。水位这个读数有量不准的路子,深处的洞它看不见,最深一帧的底词没写到它就乐观了一个词,散落的越界词又让它悲观但安全。测试的用例名字起得直白,咱们的叫法就是 `two ways to be wrong, both bounded and known`(两种错法,而它们的界都是已知的),名字报了两种,身子分了三节,咱们一节一节看。

## 全栈涂刷:八个哨字换成整块花纹底

代码住的地方没搬,还是《芯片死前立遗嘱》里咱们读过的 `include/ZerOS/kernel/sched/stack_guard.hpp`。咱们看这个 commit 对它动的手:

```cpp
namespace ZerOS::task {
constexpr inline std::uint32_t kCanaryWord = 0xEF114514;
constexpr inline std::size_t kCanarySize = 8;

struct TaskGuardHelper {
    static void bury_canary(sched::TCB& t) {
        auto& stack_view = sched::zeros_impl::TCBKeys::stack_view(t);
        for (std::size_t i = 0; i < stack_view.size(); ++i) {
            stack_view[i] = kCanaryWord;
        }
    }
```

这个改动是小得不起眼的,咱们拿旧版放在一处才看得清它的分量。旧版的循环上界是 `i < kCanarySize && stack_view.size()`,涂的只有最低地址的八个哨字。新版的上界只剩了 `stack_view.size()`,整块栈被刷了满版,一个词也没有剩下的,全都是 `0xEF114514` 的花纹。刷满的密度是水位的地基:扫描要数的是没被碰过的词,要是八个哨字之外的花纹不在了,咱们扫过第八个词之后满眼都是没定过值的内存,读数就无从谈起了。这里说的魔法数,您就理解成一个提前定好的固定花纹,它是 `0xEF114514` 这个一望而知的值,选它是没有深意的,选了它才有用:全栈一色,谁的脚印踩过,花纹就断在了哪。

有一处删除我照实交代。旧版的循环上头有一行注释,注释里的原话是 `word-wise: memset fills BYTES, it would chop the pattern to 0x14`,解释的是为什么要逐词写而不用 `memset`(`memset` 是按字节填的,`0xEF114514` 的四个字节里三个是 0x14,填出来的是一条一色的条纹,逐词的花纹就没了)。这个 commit 把注释删了,可循环还是逐词的,旧的理由还在 git 历史里躺着。我把 `git show` 的原文贴给您过目,它删掉的是一行注释和旧的上界那一行,循环体本身是一个字都没动的:

```diff
-        // word-wise: memset fills BYTES, it would chop the pattern to 0x14
-        for (std::size_t i = 0; i < kCanarySize && i < stack_view.size(); ++i) {
+        for (std::size_t i = 0; i < stack_view.size(); ++i) {
         stack_view[i] = kCanaryWord;
     }
```

巡逻的那半边没动。`fast_check_stack` 照旧只读刀口上的 `view[7]` 一个字,`eaten_length` 照旧只在出了事之后数被吃掉的哨字,溢出检测的路数,跟《芯片死前立遗嘱》里咱们读过的完全一致。变的只有底子:从前哨字底下是荒地,现在哨字底下是整片同色的花纹,函数是没换过的,咱们读出来的东西多了一层。

## 两个读数:还空几词,总共几词

新的读数有两笔,咱们一笔一笔看。头一笔是余量的读数,咱们看 `watermark_free_words`:

```cpp
    static std::size_t watermark_free_words(sched::TCB& t) {
        auto& stack_view = sched::zeros_impl::TCBKeys::stack_view(t);
        std::size_t free_words = 0;
        for (std::size_t i = kCanarySize; i < stack_view.size() && stack_view[i] == kCanaryWord;
             ++i) {
            ++free_words;
        }
        return free_words;
    }
```

扫描的方向值得您停一下。任务的栈从高地址往低地址长,栈视图里的 `view[8]` 是紧挨着金丝雀区的低地址端,咱们起步的地方是 `view[8]`,一路朝高处走过来的路线上,循环的条件是 `stack_view[i] == kCanaryWord`,一旦它不成立了,循环当场就停了。停在了哪,水位也就跟着定在了哪:咱们走过的词是任务从来没压到的余量,停下之后剩下的部分,是它一生里压得最深的那一次。条件的写法还有个后果,咱们在量不准的段落里专门跟它对质。

第二笔是容量的读数,咱们看 `capacity_words`:

```cpp
    // usable capacity in words — the canary eats 8 of them (D24: it
    // occupies the front, the budget says so)
    static std::size_t capacity_words(sched::TCB& t) {
        const std::size_t total = sched::zeros_impl::TCBKeys::stack_view(t).size();
        return total > kCanarySize ? total - kCanarySize : 0;
    }
```

注释把口径写明白了:金丝雀占着栈视图前头的 8 个词,咱们可用的容量就是总词数减 8,预算的文档(D24,预算册里给金丝雀占位定了归属的条目)说的就是它。总词数不大于 8 的极端情形返回 0,金丝雀自己都放不下的时候,容量就谈不上了。您拿 `capacity_words` 减去 `watermark_free_words`,得到的就是最深用到过几词,demo 的报表直接打的就是它,咱们到那儿看真数。

涂刷的时机不用改,还是 `spawn` 里的老三样,埋完了涂刷再伪造帧再入队,idle 在 `start_scheduler` 里走的也是同一道 `bury_canary`。谁的栈都是满色的,谁的读数都有的扫,这就是涂刷从八个词放大到了全栈之后,咱们白拿的覆盖面。

## host 考场:四张考卷

纯逻辑的扫描,放在 host 上考是合适的,考卷是新立的 `test/test_stack_watermark.cpp`,四个 `TEST_CASE` 咱们逐张过。夹具把栈和档案卡固定在了测试自己的存储里,32 个词的栈里,8 词归了金丝雀、24 词留给了可用,构造完了就地涂刷:

```cpp
struct Pinned {
    TCBStorage storage{};
    std::uint32_t stack[32]; // 32 words: 8 canary + 24 usable
    TCB& task;

    Pinned()
        : task(ZerOS::task::named("t")
                   .prio(1)
                   .stack(std::span<std::uint32_t>{stack})
                   .entry(kNoopEntry, nullptr)
                   .spawn_into(storage)) {
        TaskGuardHelper::bury_canary(task);
    }

    std::span<std::uint32_t> view() { return TCBKeys::stack_view(task); }
};
```

头一张考卷验涂刷的覆盖面,用例的名字是 `paint covers the whole stack, not just the canary`,说的就是涂面是全栈的,不只是金丝雀的,断言把整个视图逐词验了个遍,验出来的全是 `kCanaryWord`。第二张验的是空载的读数,咱们看:

```cpp
TEST_CASE("untouched task reports full capacity free", "[watermark]") {
    Pinned p;
    REQUIRE(TaskGuardHelper::capacity_words(p.task) == 24);
    REQUIRE(TaskGuardHelper::watermark_free_words(p.task) == 24);
}
```

咱们看空载的情形:任务从没跑过的情况下,余量就该是满的,24 对上了 24,容量与余量在空载时重合了,口径也就咬合上了。第三张是其中的主角,它验的是水位只降不升的脾气,咱们跟着任务活一回,代码咱们分两段贴:

```cpp
TEST_CASE("pushing from the top eats the margin, deepest push wins", "[watermark]") {
    Pinned p;
    auto v = p.view();

    // a few frames near the top: [size-4, size) written
    for (std::size_t i = v.size() - 4; i < v.size(); ++i) {
        v[i] = 0;
    }
    REQUIRE(TaskGuardHelper::watermark_free_words(p.task) == 24 - 4);
```

剧情咱们捋一遍。任务浅浅地压了四个词,余量报了 20,是准的。

```cpp
    // deeper push later in life: down to word 12 (inclusive) — the
    // watermark only ever goes DOWN, old shallower writes stay counted
    for (std::size_t i = 12; i < v.size(); ++i) {
        v[i] = 1;
    }
    REQUIRE(TaskGuardHelper::watermark_free_words(p.task) == 12 - 8);
    REQUIRE(TaskGuardHelper::capacity_words(p.task) == 24);

    // and the canary zone itself is never part of the margin
    REQUIRE(TaskGuardHelper::fast_check_stack(p.task) == false);
}
```

后来它活到了更深的世面,压到了词 12(含),余量跟着降到了 4,注释把脾气写死了:watermark only ever goes DOWN(水位只会往下走),浅过的那一波不会被新的浅读数抹回来。末了的断言查的是金丝雀区的完好,`fast_check_stack` 如约交回了 false,也就是没溢出的意思,金丝雀还好好地活着,注释顺路说明了金丝雀区为什么不计入余量。水位回答的问题从来都是它一生最深到过哪。咱们也别把两个函数的分工混了,查完好性的是 `fast_check_stack`,报极值的 `eaten_length` 得等出了事才数。

## 量不准的路,仓库自己写了界

第四张考卷的名目就是本节的题,三个 SECTION 咱们挨个过。头一节说扫描有看不见的洞:

```cpp
    SECTION("a hole above the deepest write is invisible to the scan") {
        Pinned p;
        auto v = p.view();
        // frames wrote [12, size) but never touched word 13 — the scan
        // stops at 12 (first non-paint) before ever reaching the hole:
        // the reading stays exact for the written-word semantics
        for (std::size_t i = 12; i < v.size(); ++i) {
            v[i] = (i == 13) ? ZerOS::task::kCanaryWord : 1;
        }
        REQUIRE(TaskGuardHelper::watermark_free_words(p.task) == 12 - 8);
    }
```

帧们写遍了词 12 到末尾的地界,唯独词 13 是没被碰过的,花纹恰好留在了那儿。扫描是从词 8 起步的,走到了词 12 就被第一个非花纹词拦下了,词 13 的洞永远轮不到它看。注释里那句 written-word semantics 是口径的答案:水位数的是写过的词的边界,读数对这个口径是准的,洞不构成它的错误。咱们换白话说,它的盲区就是更深处恰好留着花纹的洞,那是它扫不到的,它也不扫了。

第二节标的是唯一的乐观,咱们看:

```cpp
    SECTION("the only flattery: the deepest frame's bottom word unwritten") {
        Pinned p;
        auto v = p.view();
        // the frame REACHED down to word 11 (SP semantics: free == 3)
        // but only words [12, size) were actually stored to — the
        // written-word scan reports 4: one word optimistic, bounded by
        // the frame's own alignment slop, the trade every RTOS takes
        for (std::size_t i = 12; i < v.size(); ++i) {
            v[i] = 1;
        }
        REQUIRE(TaskGuardHelper::watermark_free_words(p.task) == 12 - 8);
    }
```

咱们把场景摆出来:帧的指针到过词 11,可词 11 上是一个字节都没存过的,存下来的是词 12 到末尾的整段。按指针算的余量是 3,按写过的词算的余量是 4,读数就把最深的那一帧夸了一个词。注释连夸的界都标了:误差被帧自己的对齐余量限着,顶多是偏差一个词的,注释里的原话是 the trade every RTOS takes(各家 RTOS 都签了同一个交换),它只是把交换写在了明面上。咱们定栈深的时候给余量留一两个词,这就是理由的出处。

第三节反了过来,咱们看它把误差落在了安全的一侧:

```cpp
    SECTION("a lone stray word at the very bottom makes it pessimistic") {
        Pinned p;
        p.view()[8] = 0xDEADBEEF; // out-of-bounds single write below everything
        REQUIRE(TaskGuardHelper::watermark_free_words(p.task) == 0); // safe direction
    }
```

一个越界的散词落在了词 8 上,扫描一步都走不动了,余量就报了 0。读数从夸变成了吓,注释给的方向是 safe direction(安全的方向):吓着咱们的读数,只会让咱们把栈给大,不会把您的栈给小了。三节的误差方向,乐观的标了界,悲观的标了向,您回头再看用例的名字,第三节就是悲观项的活体,整卷没有一处讳言自己的不准。

## watermark demo:一秒后的水位报表

咱们这套制度得有个当众宣读的场合,板上的 demo 就是当众读一遍的地方。新注册的 `watermark` demo 起了三个任务,deep 带着真实的压舱,shallow 是几乎不喘气的,reporter 睡够了一秒出来读表:

```cpp
void deep(void*) {
    // ballast: a volatile local array welded onto its frame — written
    // and read every round so nothing can optimize it away; the stack
    // genuinely gets pushed down to this depth (and further, every time
    // the sleep call chain runs through it)
    volatile std::uint32_t ballast[32];
    for (std::uint32_t round = 0;; ++round) {
        for (std::uint32_t i = 0; i < 32; ++i) {
            ballast[i] = round + i;
        }
        std::uint32_t sum = 0;
        for (std::uint32_t i = 0; i < 32; ++i) {
            sum += ballast[i];
        }
        (void)sum;
        ThisTask::sleep_for(Milliseconds{100});
    }
}
```

压舱是焊在帧上的 `volatile` 数组,咱们每轮把它写一遍读一遍,优化器动不了它,栈就被真实地压到了这个深度,注释还补了一句,每次睡觉的调用链路过时压得更深。shallow 的循环体只剩一句 `sleep_for`,reporter 睡了 1000 毫秒之后读表:

```cpp
void report_one(const char* name, TCB* t) {
    const auto cap = TaskGuardHelper::capacity_words(*t);
    const auto free = TaskGuardHelper::watermark_free_words(*t);
    // (the mini formatter speaks plain {} only — no alignment dialect)
    ZerOS::log::print("  {}: {} cap, {} free, {} used at deepest\r\n",
                      name, Dec{cap}, Dec{free}, Dec{cap - free});
}

void reporter(void*) {
    ThisTask::sleep_for(Milliseconds{1000});

    ZerOS::log::print("watermark: read\r\n");
    ZerOS::log::print("  (words, canary's 8 excluded)\r\n");
    report_one("deep", g_deep);     // parked asleep — single core says
    report_one("shallow", g_shallow); // their stacks hold still while we read

    ThisTask::block();
}
```

`report_one` 拿容量减去了余量,报出的就是最深用过的词数,小格式化器认的只有 `{}`,对齐的花样它没有。读表的时刻咱们得较真一下:reporter 跑在单核上,deep 与 shallow 此刻都睡在了 `sleep_for` 里,源码注释把这句道理写在了行尾,睡着的任务栈读数是静止的,咱们读它们的栈不算偷看,读出来的是定格的证据。

我的复现链照旧,拿 `git archive fb9072b` 把树导出到了 /tmp 的副本,CMSIS 从子模块拷了进去,host 与交叉各构建了一遍。水位报表的串口原文,咱们看:

```text
[INFO] usart1: [host:  1.09s (+0.96s)|virt: 1s (+1s)] watermark: read
[INFO] usart1: [host: 1.09s (+0.39ms)|virt: 1s (+9.8µs)]   (words, canary's 8 excluded)
[INFO] usart1: [host: 1.09s (+0.33ms)|virt: 1s (+0s)]   deep: 88 cap, 26 free, 62 used at deepest
[INFO] usart1: [host: 1.09s (+0.27ms)|virt: 1s (+0s)]   shallow: 56 cap, 26 free, 30 used at deepest
```

数字咱们一起读。deep 拿的是 96 词的栈,它的容量是 88,最深处用过了 62。shallow 拿的是 64 词,它的容量是 56,最深处用过了 30。30 词是它全部的家底,出生帧加睡觉的调用链。deep 在同一套家底之上压了 32 词的压舱,62 减 30 的差正好是 32,压舱数组在咱们的报表里一个词都不差地现了形。您再看容量那一列,88 对上了 56,金丝雀的 8 个词都已扣除。96 词的来历,《叫醒所有人》那篇是拿一次溢出换来的,如今它写在 demo 的 `words(96)` 里,而报表告诉了咱们它用得宽裕,这就是那篇的偶发活捉在这里升格成的制度:谁想给自己的任务定栈深,起一个咱们自己的 reporter,让它睡满了 1000 毫秒,读出来的行就是答案。

体积是真实的,watermark demo 的 ELF 里,text 段占的是 4276 字节,我数到的 data 与 bss 是 476 与 2124,链接器报的 FLASH 是 4752 字节(占了 64 KB 的 7.25%),RAM 报的是 2604 字节。CI 的冒烟表也添了对应的标记,`scripts/ci.sh` 里添的那行是 `[watermark]="watermark: read"`,每个 demo 报自己的行,咱们巡到了这行就算它活着。

有一处环境的事我照实交代。Renode 的平台描述 `bluepill.repl` 里,i2c1 与 i2c2 用的是 `I2C.STM32F4_I2C` 这个类型,我本机装的 Renode 是 1.17.0,它是不认这个类型的,加载平台的环节当场报了 `Error E04: Could not resolve type`。repl 自己的头注释写着,咱们的模型派生自本机 Renode 1.16.1 的上游平台,版本差异属于我的环境,不属于仓库的问题。demo 里的 I2C 一行都不碰,我在 /tmp 副本里把 i2c 两行注释掉再跑,上面的报表就是跑通之后的真实输出,版本对齐的活儿您复现时按自己机器的 Renode 处理就好。

## 验收

本篇的可查证事实归拢成了四件,咱们一件一件过。

头一件是 host 的考场。您 checkout 到 `fb9072b`,按 `cmake -B build-host -DZEROS_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug` 的配置再构建,`ctest` 应给出 15 个目标全通过的读数,水位测试的成绩单是 4 个用例、41 条断言,我在 /tmp 副本里跑出来的原文如下:

```text
All tests passed (41 assertions in 4 test cases)
```

第二件是交叉构建与板上的报表。交叉构建跑完了之后,watermark demo 的 FLASH 应报 4752 字节,Renode 被您跑起来之后,一秒钟您应看到 `deep: 88 cap, 26 free, 62 used at deepest` 与 `shallow: 56 cap, 26 free, 30 used at deepest` 两行,62 减 30 的 32 对着压舱数组的词数。I2C 的那两行版本问题,按上面交代的方式处理就好。

第三件是误差方向的自己交代。host 测试里的三个 SECTION,洞不可见的、乐观一词的、悲观但安全的,每一条都对着源码的扫描条件,您把 `watermark_free_words` 的循环条件与三节的注释对着读,洞的一节与悲观的一节,数字是能从条件里推出来的,乐观项的界是注释给的。

第四件是我没验的东西,也照实地摆出来。demo 的一秒报表只读到了任务头一秒的水位,更长的生命周期里水位还涨不涨、涨到哪,本篇没有长跑的读数,`words(96)` 的宽裕只是头一秒的证据。压舱数组的 32 词与 62 的读数严丝合缝,可调用链在板上每一站的真实足迹,咱们没法逐词对,能对的是 host 考卷里那些咱们手工构造的帧。真要把栈深按证据定到底的话,长跑读表是您自己手里可以加的一场实验,定到多深就算到多深了。

## 下一篇

水位的制度立住了,栈深有了随取随读的表。同一个 commit 的另一半,归的是姊妹篇《睡着就调慢心跳》:idle 睡前把 SysTick 重装成了对准下一到期点的单发,COUNTFLAG 读清、ICPR 撤 pending、手工结算流逝的三件套竞态,加 24 位钳幅的分期续杯,`-DZEROS_TICKLESS=ON` 的对照实验也在那儿等着咱们。咱们那儿见。
