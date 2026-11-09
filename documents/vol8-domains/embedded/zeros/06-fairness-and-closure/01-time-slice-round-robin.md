---
title: "数到十就换人:同级时间片与不继承的切片债"
description: "1006648(feat: scheduler finished)给调度器补上公平的这条腿:on_tick 在临界区里给当前任务计拍,kSliceTicks=10 拍满就把当前任务推回同级队尾并挂起一次切换,同级没人等就不做空转切换,pick_next 重置切片计数让新上场者不继承前任剩下的份额。我在 /tmp 副本跑通了三个新单测,切片三用例十四断言全绿,Renode 按 r 孵出两个纯自旋的同级 spinner,串口窗里 rr1 与 rr2 每 500 毫秒交替出声,交错的输出本身就是特性"
chapter: 6
order: 1
tags:
  - stm32f1
  - advanced
  - 嵌入式
  - cpp-modern
difficulty: advanced
platform: stm32f1
cpp_standard: [23]
reading_time_minutes: 20
prerequisites:
  - "谁最急,谁上场:就绪位图、任务三态与选人仲裁"
  - "看 M 闭嘴那三秒:互斥锁、优先级继承与所有权移交"
related:
  - "在栈顶伪造出生证明:PendSV、SVC 与第一次切换"
  - "中断只递条子:环形队列、工作者线程与底半推迟"
current_status:
  title: 数到十就换人
  detail: ZerOS的篇章正在打磨ing
  variant: polishing
  icon: 🤝
---

# 数到十就换人:同级时间片与不继承的切片债

咱们自己把服务面盘了一遍(上一站的 index 把它叫作服务面的经济学),盘出来了两笔旧债等着咱们还。同步家族的那笔债里,咱们的信号量、互斥、队列都只等一个条件,而多方等待与最快的路径还空着,这一块归的是本站的下一篇。调度器的债看起来不该存在:挑人的仲裁面咱们讲完了,切换的机制面咱们也讲完了,您把那两篇的验收回想一遍,挑是挑得对的,切也是切得动的,您似乎挑不出毛病来。缺的东西其实藏在同级里,咱们今天把它挖出来,再看看它是怎么被补上的。

本站还有一笔服务面的旧债其实已经收过了:崩了要看得见死因的活儿,归了《芯片死前立遗嘱:HardFault 现场、栈金丝雀与零分配日志》的遗言报告,而中断里重活的卸货,归了《中断只递条子:环形队列、工作者线程与底半推迟》的工作者。所以本站剩下的两笔债,调度器的一笔归我,同步家族的一笔归下一篇,咱们一篇收一笔。

`1006648` 的 subject,写的是 “feat: scheduler finished”。我数过它的 diff,改动落在了 4 个文件上,新增了 146 行,删掉了 1 行。它的行数不多,口气倒是不小,调度器到了它,才敢在自己的 subject 里自称 finished。为什么优先级抢占都跑了五站之后,调度器还欠着一条腿?咱们把欠条看清楚,再看看它是怎么还上的。

## 欠条:同级里没人管公平

欠的东西,要从仲裁那一篇里的一个测试说起。您还记得 `test_sched.cpp` 里的那个用例,咱们叫它 “same level round-robins via yield-to-tail”:同级的三个人凑在一级里,谁调了 `yield`,谁就推到本级的队尾,下一个就轮到别人了。它当时是绿的,可是咱们把它的剧本再看一眼,轮转的原动力是任务自己调用了 `yield`。

现在咱们假设上场的任务不让。一个纯自旋的任务,它的函数体里找不到一个 `sleep_for`,也找不到一个 `block` 的影子,连一个 `yield` 的字样都没有,它就是一条不回头的死循环。它上场了之后,`pick_next` 谁也请不出来:它是 `Running` 的状态,同级伙伴在 FIFO 里排着长长的队,可调度器没有任何一个可以打断它的时机。位图上别的级的位亮不起来,因为被饿死的伙伴跟它同级。我在 Renode 里、在 `1006648` 之前的调度器上试过这个剧本:两个这样的任务放出去,串口窗里刷屏的名字只有一个,而另一个从头到尾一个字都没有。它是活着的,在队里好好地待着,就是轮不到出场的份。

这就是欠条的内容。优先级是调度器的主轴,数值小的压过数值大的,咱们五站以来建的一切都建立在它上面,而同级内部的分配,当时的调度器只靠任务自己让。让与不让是任务的自觉,内核把公平交给了自觉。自觉是靠不住的,靠得住的是时钟:SysTick 每毫秒打一拍,它有义务在同级之间主持轮换的事。`1006648` 干的就是这件事,咱们看它怎么主持。

## on_tick:拍满数到十,推队尾,请切换

内核接手的办法,落在 `Scheduler` 新添的一个成员函数 `on_tick` 身上。咱们在念它的代码之前,把它的身份与差事立住:SysTick 的节拍每毫秒打一拍,打拍的处理函数每一拍都要把 `on_tick` 请起来一趟,它替当前上场的任务计拍,数满了十拍,就把这个人推回同级的队尾,跟着把切换的请求挂了出去。自觉的轮转靠不住,这是上一节判过的案,从这里起轮转的原动力就换成了时钟,任务让与不让是它自己的事,片到点了,动手的是内核自己。函数的头半段管计拍,咱们看它:

```cpp
    void on_tick() {
        irq::CriticalGuard g{driver_};
        if (!current_ || current_ == &idle_) {
            return;
        }
        // Ticks Served
        ++ticks_served_;

        if (ticks_served_ < kSliceTicks) {
            return; // OK, back
        }
```

函数进门就握住了临界区,咱们对这样的写法已经熟了:`on_tick` 是要从 SysTick 的中断上下文里被调的,它动的 `ticks_served_`、位图、FIFO,跟任务那一侧是共享的,不加锁迟早是要写坏的。进门之后的头一个判断把 idle 放走了:CPU 上要是跑着 idle,说明醒着的真任务一个都没有,轮换就无从谈起了,计拍也不用开始了。`ticks_served_` 是这个 commit 新添的成员,一个 `std::uint32_t` 的整数,它是当前任务在这片里已经用掉的拍数,每来一拍它就涨了一,还没涨到 `kSliceTicks` 的时候,函数什么都不做。

咱们把 `kSliceTicks` 的声明单独看一眼,它其实只有一行:

```cpp
    static constexpr clock::Ticks::tick_t kSliceTicks = 10;
```

常量的单位是拍。demo 里的 `init_time(SystemCoreClock / 1000)` 把节拍定在了 1kHz,一拍走的就是一毫秒,所以十拍是一片,一个时间片在板上就是 10 毫秒的长度。10 这个数写在代码里是不带单位的,是常量的名字 `kSliceTicks` 把单位说了出来,我把这一点看成了命名的一份小示范:数字自己说不清的话,就由名字替它说了。

咱们接着看拍满之后的后半段,公平真正落地的三个动作:

```cpp
        ticks_served_ = 0; // reset, time to sched
        if (fifo_[current_->task_priority_].empty()) {
            return; // Dont dry run self
        }

        push_back(current_);
        current_->state_ = TaskState::Ready;
        driver_.request_switch(); // Ask For a switch, wait PendSV
    }
```

归零的那一行与空队列的判断,它们的用意下一节专门接住,这里咱们把放行的三行念完。`push_back(current_)` 把当前任务推回了它那个级的队尾,它从 `Running` 退成了 `Ready`,刚才用掉的 10 毫秒就是它这一轮的份额,想再跑就得重新排队了。`state_` 改成了 `Ready` 之后,它在 `pick_next` 的眼里就是一个普通的就绪任务,这里头有个值得看的细节,咱们到下一节再展开。`request_switch()` 请求了一次切换,而咱们全线的口径它没有破:中断服务程序里绝不直接切上下文,它只是把 PendSV 的挂起位写一,真正的切换等 PendSV 异常自己发生。

这里咱们把《在栈顶伪造出生证明》那一篇里留过的伏笔接上。咱们回到那一篇的正文:`start_scheduler` 把 PendSV 和 SysTick 在 SHPR3 里排成了同一个 `0xFF`,当时的疑问是时基的心跳凭什么跟切换排一档,答案的一半在那篇的切换过程里,现在咱们补上另一半:`on_tick` 在 SysTick 的处理函数里请求了切换,接活的是 PendSV。两根中断是同级的,PendSV 就不会在 SysTick 还没退场的时候插进来抢,打拍的把拍打完了、退了场,排队的 PendSV 才开始切。要是 PendSV 的级别比 SysTick 急,切片的判断做到一半就有被打断的风险,0xFF 的同级安排,到本篇才算把它的用意用完了。

## 同级没人等,就不做空转的切换

上一节跳过的空队列判断,咱们现在把它接住,它是省下来的每一次切换:

```cpp
        ticks_served_ = 0; // reset, time to sched
        if (fifo_[current_->task_priority_].empty()) {
            return; // Dont dry run self
        }
```

拍满了之后,它问了一句:本级队列里还有人吗?咱们注意问的对象是 `fifo_[current_->task_priority_]`,它是当前任务自己那个级的队列。当前任务此刻是 `Running` 的状态,它是不在队列里的,所以这个队列里有的,全是它的同级伙伴。队列里没有人的话,函数就 `return` 了,注释里写下的那句 “Dont dry run self” 说的是大白话,别做没有意义的空转切换。系统里要是只有它一个同级的伙伴,推出去再选回来的话,选回来的还是它自己,白白付了一次上下文切换的价钱,换回来的什么都没有。切片轮转的目的在同级之间分时间,同级里没有第二个人了,就没有要分的公平,切换就不该发生了。

还有一个容易被看漏的细节,它就藏在 `return` 的前面那一行:拍满之后不管切不切,`ticks_served_` 都归了零。切了,归零是好理解的,新的上场者从零计起。不切的时候也归零,意思是这片用完了就作废,计数就重新来了,单跑的任务每 10 毫秒重开一片,它不会因为计数攒着攒着就莫名地挨一次切换。归零的这一行跟下面的判断贴在同一个入口上,它们各自有各自的职责:空队的判断归第二个用例守。pick_next 的归零归第三个用例守。至于 on_tick 满拍时的归零,它没有专门的测试,它的效果藏在头两个用例的读数里,咱们到测试的段落会看到。

## pick_next 重置计数:切片债不继承

第二处维护计数的地方在 `pick_next` 里,咱们把“切片债”立住了,改动就好念了:任务在当前这片里已经用掉的拍数,就是它的债,债积得越深的话,离换人的时刻就越近。`pick_next` 是所有上场的必经之路,抢占回来的、让出回来的、被唤醒的,全都从它的手里接 CPU。要是它忘了重置计数的话,一个任务用了 8 拍被更高的优先级抢走,它被打断的当口,计数就在别人的手里继续涨着,轮回来的时候剩几拍全看运气,前面抢它的人占了多久的 CPU,通通折进了它的剩余债务里。它明明该有整整的一片,却只拿到一片的零头。更阴险的是这个错误平时看不见:每个任务都还在轮,只是轮得偏了,偏多偏少随着抢占的时机漂,它落到您板子上的样子是偶发的饥饿,查起来没有现成的线索。所以归零就该落在换人的这一步上,咱们看它的改动,它其实只有两行:

```cpp
        current_ = next;
        ticks_served_ = 0; // a fresh pick is a fresh slice: no inheriting
                           // the previous runner's leftover share
        next->state_ = TaskState::Running;
```

注释把用意说完了,咱们把落点接住:a fresh pick is a fresh slice,新选中的人,拿的是新开的一片。计数的语义是“当前任务在这片里用了多少”,而“这片”从某人上场的时候开始,到下场的时候为止,上场的地方只有 `pick_next`,重置就只该在它那里发生了。`on_tick` 里的归零管的是“片用完了作废”,`pick_next` 里的归零管的是“人换了片也换”,两处的归零,一处守边界的干净,一处守换人的干净,合起来的效果,是计数说的才是实话。

## host 考场:咱们的三个用例各守一条不变式

机制讲完了,咱们上考场。`test/test_sched.cpp` 在这个 commit 里新添了 68 行,新添的三个用例,每个用例守上面的一条不变式,咱们逐个看。

咱们看头一个守“拍满才换人”的用例,看它的剧本:

```cpp
TEST_CASE("time slice: same level round-robins by the clock, not by yield", "[sched][slice]") {
    g_switch_requests = 0;
    ZerOS::sched::Scheduler<HostSwitchPort> s;
    Rig a(1, "a"), b(1, "b");

    s.add(&a.tcb);
    s.add(&b.tcb);
    REQUIRE(s.pick_next().get() == &a.tcb); // a runs, b waits at the same level

    for (int i = 0; i < 9; ++i) {
        s.on_tick(); // nine beats: the share is not spent yet
    }
    CHECK(g_switch_requests == 0);

    s.on_tick(); // the tenth beat: the slice is spent
    CHECK(g_switch_requests == 1);        // a switch is pended
    REQUIRE(s.pick_next().get() == &b.tcb); // and b takes the CPU
}
```

a、b 是同级的两个任务,咱们让 a 上场,b 在队里等着的。九拍打了过去,切换请求的计数是零,第十拍打了下去,计数涨到了一,再用 `pick_next` 验出接棒的是 b。边界掐得干干净净的:九拍不行,第十拍才换的人,`kSliceTicks` 的语义就这样被数字本身按在了测试里。

咱们接着看“同级没人等不空切”的用例,只剩 a 一个同级任务了:

```cpp
    for (int i = 0; i < 50; ++i) { // five whole slices with nobody waiting
        s.on_tick();
    }
    CHECK(g_switch_requests == 0); // no dry-run switching
    REQUIRE(s.pick_next().get() == &a.tcb);
```

50 拍打了过去,咱们按 10 毫秒一片算是整整五片,切换请求的计数纹丝不动。要是 `on_tick` 把那个空队列的判断忘了,这片里每满 10 拍就该有一次虚的请求,计数的读数会涨到五,`CHECK` 就当场翻脸了。

第三个用例守的是最微妙的不变式,也就是咱们说的切片债不继承。它的剧本长一些,我照注释给您分作三幕复述。头一幕是布景:0 级的 h 上场,压过了同级的一对伙伴,它睡了之后 a 回到 CPU 上,等在队里的是 b:

```cpp
    // a runs 8 of its 10 beats, gets preempted by someone higher; when it
    // returns it must NOT inherit its own leftover share. b is the same-level
    // companion waiting in the ring (a slice only yields when someone waits)
    s.add(&a.tcb);
    s.add(&b.tcb);
    s.add(&h.tcb);
    REQUIRE(s.pick_next().get() == &h.tcb); // higher level took over
    s.block(&h.tcb);                        // (this pends — wash it off)
    REQUIRE(s.pick_next().get() == &a.tcb); // a is back on stage, b waits
    g_switch_requests = 0;
```

头一幕里有一处舞台指示的细节,我得指给您看。`s.block(&h.tcb)` 的上头那行注释,写的是 “(this pends — wash it off)”。`block` 自己是会请求切换的,h 是当前的任务,它睡了,内核当然要请人接替,host 替身里的 `request_switch` 只是计数器加一,可它把计数弄脏了。测试作者在正戏开始前把 `g_switch_requests` 清了零,才让后面的断言数的是切片的请求。一行注释教了 host 舞台的一课:替身里每个请求都是真的发生过,数数之前把计数清了零。

到了第二幕,回到台上的是 a,咱们给它打了八拍,八拍是不够十拍的,它因为别的原因退场了(测试用 `block` 加 `ready` 模拟自己去排队的剧情),再选人的时候,出来的是 b:

```cpp
    for (int i = 0; i < 8; ++i) {
        s.on_tick();
    }
    CHECK(g_switch_requests == 0); // 8 beats in, share still good
    s.block(&a.tcb);               // ...but now a leaves for other reasons
    s.ready(&a.tcb);               // a rejoins at the TAIL...
    REQUIRE(s.pick_next().get() == &b.tcb); // ...so b takes over (fresh pick)
    g_switch_requests = 0;
```

第三幕是咱们等着的好戏:b 接手了之后,九拍打下去是不该有请求的,第十拍才是该有的:

```cpp
    for (int i = 0; i < 9; ++i) { // 9 beats must NOT trigger: b inherited
        s.on_tick();              // NOTHING of a's leftover 8
    }
    CHECK(g_switch_requests == 0);
    s.on_tick(); // ...the 10th does
    CHECK(g_switch_requests == 1);
    REQUIRE(s.pick_next().get() == &a.tcb); // and the ring turns to a
```

咱们把这幕的用意对一遍:要是 `pick_next` 没有做重置计数这件事的话,a 离场前攒下的 8 拍就留在计数器里,被 b 白白继承了,b 打两拍就得下台了。测试把两条路都堵死了:b 必须享有完整的十拍,而 a 回来的时候(最后一行的 `pick_next` 选回的是 a)同样从零开始。

我拿 GCC 16.2.1 在 /tmp 副本里配了 host 构建,`ctest` 的十二个目标全部过了,跑完的总耗时是 0.24 秒:

```text
100% tests passed out of 12

Total Test time (real) =   0.24 sec
```

调度测试自己的成绩单,咱们跑出来的是 13 个用例、59 条断言,比它头一回交卷的时候多了 4 个用例、17 条断言,多出来的部分正是咱们刚看完的三个切片用例和它们的十四断言。咱们单独跑 `[slice]` 这一组,成绩是这样的:

```text
All tests passed (14 assertions in 3 test cases)
```

## Renode 剧场:两个死不睡觉的任务轮流出声

host 上验的是计数和请求,任务真的被切来切去,咱们还得在仿真里看。`example/main.cpp` 在这个 commit 里加了一个 `r` 键的剧目,咱们把它的两个零件都看了,咱们看的头一个是演员:

```cpp
// 'r' spawns two NEVER-SLEEPING spinners on the SAME level for ~3 seconds.
// Without time slicing one of them would starve the other; the slice forces
// them to take turns — the interleaved output IS the feature.
void spinner(void* tag) {
    const auto* line = static_cast<const char*>(tag);
    const auto deadline = ThisTask::now().tick_ + 3000;
    auto next_report = ThisTask::now().tick_;
    for (;;) {
        const auto now = ThisTask::now().tick_;
        if (now >= deadline) {
            break; // three seconds of proof is enough
        }
        if (now >= next_report) {
            ZerOS::board::print(line);
            next_report += 500;
        }
        // pure spinning, zero sleeps — the scheduler does the fairness
    }
    ThisTask::block(); // self-retire: sleep with nobody to wake me
}
```

注释自己就把戏眼交代了,它写的是 pure spinning 加 zero sleeps。两个这样的演员同级,让与不让的自觉它们一点都没有,公平的事只能由调度器来做,而它们的输出要是交错了,交错的输出本身就是特性。结尾的那句 `ThisTask::block()` 是自退场的惯用法:任务体返回之后,它没有别的去处,睡了就没有人会来叫醒它,就这么退了场。我把它念给您:注释写的 “sleep with nobody to wake me”,是它自己的话。

咱们看孵演员的 `r` 键分支:两个任务都排在了 3 级,栈各给了 64 字,静态的 `TCBStorage` 加静态的栈数组,咱们连一粒堆内存都没有花:

```cpp
                auto& r1 = ZerOS::task::named("rr1")
                               .prio(3)
                               .stack(stack_rr1)
                               .entry(spinner, const_cast<char*>("rr1 alive\r\n"))
                               .spawn_into(tcb_rr1);
                auto& r2 = ZerOS::task::named("rr2")
                               .prio(3)
                               .stack(stack_rr2)
                               .entry(spinner, const_cast<char*>("rr2 alive\r\n"))
                               .spawn_into(tcb_rr2);
```

我自己的验收链路照旧:`git archive 1006648` 导出了源码树,咱们补上 CMSIS 的头,拿 arm-none-eabi-g++ 16.2.0 做了交叉编译,体积是真实的:

```text
   text	   data	    bss	    dec	    hex	filename
   5684	    224	   3256	   9164	   23cc	build/src/board/stm32f103_bluepill/zeros-bluepill.elf
```

Renode 那边是老剧本了,本机的 1.17.0 还是认不出 `bluepill.repl` 里的 `I2C.STM32F4_I2C`,我把 i2c1、i2c2 两个节点连同它们的连线整块注释掉了,demo 是不经过 i2c 的,行为是不受影响的,这跟前面几篇的处理一致。串口分析器是个只读的窗,字符是打不进去的,我把 `sysbus.usart1` 切进了监控器,仿真开跑一秒之后喂了一个 `0x72`(也就是字符 `r` 的编码),再让它跑了六秒,把串口窗的输出收了下来:

```text
[INFO] usart1: [host: 1.45s (+51.75ms)|virt:    1.35s (+48.3ms)] c: spinner pair out, 3s of fairness
[INFO] usart1: [host:  1.45s (+0.31ms)|virt:   1.35s (+17.17µs)] rr1 alive
[INFO] usart1: [host:  1.46s (+9.42ms)|virt:    1.36s (+9.68ms)] rr2 alive
[INFO] usart1: [host: 2.13s (+76.16ms)|virt:      1.85s (+50ms)] rr1 alive
[INFO] usart1: [host:  2.14s (+11.59ms)|virt:      1.86s (+10ms)] rr2 alive
[INFO] usart1: [host: 2.81s (+69.86ms)|virt:   2.35s (+49.99ms)] rr1 alive
[INFO] usart1: [host:  2.83s (+11.98ms)|virt:      2.36s (+10ms)] rr2 alive
[INFO] usart1: [host: 3.41s (+62.26ms)|virt:      2.85s (+50ms)] rr1 alive
[INFO] usart1: [host:  3.42s (+10.91ms)|virt:      2.86s (+10ms)] rr2 alive
[INFO] usart1: [host: 3.86s (+39.67ms)|virt:   3.35s (+50ms)] rr1 alive
[INFO] usart1: [host:  3.87s (+5.72ms)|virt:      3.36s (+10ms)] rr2 alive
[INFO] usart1: [host: 4.25s (+42.43ms)|virt:   3.85s (+50ms)] rr1 alive
[INFO] usart1: [host:   4.26s (+7.7ms)|virt:      3.86s (+10ms)] rr2 alive
```

咱们对着 virt 的时间轴数:rr1 在 1.35、1.85、2.35、2.85、3.35、3.85 秒各出了一声,rr2 在 1.36、1.86、2.36、2.86、3.36、3.86 秒各出了一声,两个演员各出了六声,咱们数下来是一声不多、一声不少,500 毫秒的节奏严丝合缝。两个死不睡觉的自旋任务,三秒里轮流着用了 CPU,谁也没有把谁饿死的惨剧。

更有味道的是 rr2 的晚到。它的每一声都比 rr1 晚了约十毫秒,日志里 virt 一列的间隔是 9.68、10、10、10、10、10 毫秒,十毫秒上下的样子,正是一片的长短。我的读法是这样,我把它念给您:500 毫秒的报点到的时候,CPU 在谁的手里,谁就立刻喊出了一声,另一个得等同级的切片到期(最多再等一片,10 毫秒)接上了 CPU,才轮得到它开口的。所以那个恒定的十毫秒落差不是噪声,它是切片长度的现场读数,两个任务的轮换节奏,被 SysTick 一拍一拍地按在了 10 毫秒的格子上。

## 验收

咱们把本篇可查证的事实归拢成三件。

头一件是 host 的考场。您 checkout 到 `1006648`,配好了 host 构建再跑 `ctest`,十二个目标都会过的,`test_sched` 单跑的成绩是 13 个用例、59 条断言,`[slice]` 这一组的成绩是 3 个用例、14 条断言。三个用例各守的是一条不变式:十拍才请求切换,同级没人等了 50 拍也不请求,换人了之后计数从零起算,每一项都能对回 `on_tick` 和 `pick_next` 的对应行。

第二件是板上的剧场。交叉编译的体积是 text 5684、data 224、bss 3256,Renode 的日志里两个 spinner 各出了六声,rr1 与 rr2 是严格交错着的,rr2 的每一声晚到了约十毫秒。您要复现,记得 Renode 1.17.0 得把 repl 里的两个 i2c 节点注释掉,按 `r` 是在监控器里喂的 `sysbus.usart1 WriteChar 0x72`(仿真开跑一秒之后再喂),起仿真的完整命令行,我按原样贴给您:

```text
renode --console --disable-xwt -e "$bin=@/tmp/zeros-t1006648/build/src/board/stm32f103_bluepill/zeros-bluepill.elf" -e "include @/tmp/zeros-t1006648/src/board/stm32f103_bluepill/sim/renode/bluepill.resc" -e "start" -e "sleep 1" -e "sysbus.usart1 WriteChar 0x72" -e "sleep 6" -e "quit"
```

第三件是边界的交代,我照实摆。host 用例验到的是计数与切换请求,真的上下文切换在 host 替身里是看不见的,真的交错是 Renode 里的串口日志给的,host 与仿真的两头合起来,咱们才算验完。时间片的长度 10 毫秒依赖 1kHz 的节拍,而节拍是 `main` 里 `init_time` 的一行配置定的,您换了节拍,片长就跟着变了,`kSliceTicks` 数的是拍,毫秒是要靠节拍换算的。晚到十毫秒的读法是我的推断,数字是真的,归因要是您有别的解释,欢迎拿日志跟我对。

同步家族那笔债在下一篇等着咱们:多方等待要一个“两把钥匙才开门”的原语,最快路径要一条不给队列、直接往任务手里塞值的路,事件组与直接通知把它们补齐了之后,广播唤醒、醒来各自重查真相会变成横跨全部同步原语的不变式。咱们那儿见。
