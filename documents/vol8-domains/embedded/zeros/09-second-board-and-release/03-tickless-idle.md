---
title: "睡着就调慢心跳:tickless、读清竞态与 24 位分期续杯"
description: "commit fb9072b 的后半:任务全睡的时候,1kHz 心跳还在空转,咱们让 idle 睡前把 SysTick 重装成对准下一到期点的 one-shot。正文写 next_due_in 怎么问到期队列要剩余时间、布防阈值 2 拍的经济学、24 位 LOAD 钳幅后的分期续杯,以及 COUNTFLAG 读清、ICPR 撤 pending、手工结算流逝的竞态三件套;Renode 里同一段固件关宏跑出 2000 次周期中断,开宏跑出 0 次 periodic 加 12 次 one-shot,四次小睡一拍都不差。CI 只编默认配置,开宏的对照正是留给您的实验"
chapter: 9
order: 3
tags:
  - stm32f1
  - intermediate
  - 嵌入式
  - 优化
  - cpp-modern
difficulty: intermediate
platform: stm32f1
cpp_standard: [23]
reading_time_minutes: 22
prerequisites:
  - "一秒钟的定义权:tick、回绕算术与到期队列"
  - "等三百毫秒,不养一个任务:软件定时器与 fn+void* 硬通货"
related:
  - "把栈刷满魔法数:金丝雀涂满全栈,栈深按证据定"
current_status:
  title: 心跳学会打盹
  detail: ZerOS的篇章正在打磨ing
  variant: polishing
  icon: 🌙
---

# 睡着就调慢心跳:tickless、读清竞态与 24 位分期续杯

上一站收尾的时候,软件定时器已经能在到期队列上按点叫人了,咱们的任务睡五百毫秒,到点就有回调开腔了,等待的成本降到了三个标志位加一个内嵌的等待者。不过您把当时的固件再想深一步,就会发现一个还在空转的部件:任务睡着的这五百毫秒里,SysTick 的周期心跳一次也没有停过,1kHz 的节拍照样每一毫秒打进来一回,咱们数了五百毫秒,要挨的就是五百次的中断,而每一次醒来能接活的只有 idle 自己。有人等的时候,心跳是给任务打拍子的节拍器,没人等的时候,它就成了一台空转的机器。

`fb9072b` 把这件事改掉了。它是一个 commit 落了两个特性,subject 写的是 “feat: stack watermark, tickless”,改动了 13 个文件,新增了 431 行,删掉了 7 行。栈高水位的那一半,归的是上一篇《把栈刷满魔法数》,咱们今天管另一半:tickless idle。做法说穿了很朴素,不过做起来处处是竞态,idle 任务睡前去问了时间内核一句,下一笔等待的到期点在哪儿,然后把 SysTick 从周期模式重装成对准那个到期点的 one-shot(单次触发,响了一回就停),响完了把流逝的拍数一次性结进到期队列,然后再把周期节拍恢复了。没人等的时候,它干脆把计数器停了,连心跳也一并省掉了。您往后读会发现,这段百来行的代码里最值钱的,还是布防与到期互相追赶的时候,作者靠哪三个动作把竞态收平的。

## 空转的心跳,咱们把开销算一下

开销的算法不复杂,咱们拿上一篇思路里的任务摆一遍。任务调了 `ThisTask::sleep_for(Milliseconds{500})` 之后进了阻塞态,把自己挂进了到期队列,调度器发现没人可跑的时候,就把 idle 请了出来。idle 的任务体,咱们在《谁最急,谁上场》里见过它出生的样子,当时的它只有一句 `wfi`:执行到这儿,CPU 停下来等任意的一个中断。中断来了之后,SysTick 的服务程序照常跑,时间内核照常往前推了一拍,查了一遍到期队列,发现队头离到点还有四百多毫秒的工夫,就什么都不做地返回了。PendSV 看了一眼就绪队列,看见的还是只有 idle,于是切回去接着自己的 `wfi`。一个完整的循环,代价是两次异常的进出场,换回来的工作量是零,而这样的循环,在五百毫秒里重复了五百遍。

咱们把开销换成次数再看。上电到 demo 结束的时候,固件要经历的是四次小睡,每一回睡的都是五百毫秒,扛心跳的全是 idle,两秒钟的睡眠就是两千次空转的中断。中断是有代价的,异常帧压了栈,服务程序跑了一遍,出来之后咱们还得把现场再恢复一回,更麻烦的是,它把 CPU 从深睡的状态里一遍遍拽了出来,而 Cortex-M 上的深眠,恰恰要靠 `wfi` 里更久的停留才睡得进去。省下来的中断就是省下来的电,这是 tickless 在每一本 RTOS 教材里出现时的原始动机,ZerOS 也躲不开同样的算式,不过它给出的实现值得咱们一行一行地看,因为竞态全都藏在咱们要读的细节里。

## 问到期队列要剩余时间:next_due_in

idle 睡前要回答的问题只有一个:下一笔等待还有多久到期。这个问题时间内核以前答不了,`fb9072b` 就给 `Kernel<Driver>` 添了一个查询接口,咱们看 `include/ZerOS/kernel/clock/kernel.hpp` 里新增的 15 行:

```cpp
    // How far away the head of the wait queue fires, counted from now —
    // the answer an idle system wants before picking how deep to sleep.
    // false means nothing is waiting: no deadline exists at all.
    bool next_due_in(Ticks::tick_t& out) {
        irq::CriticalGuard guard{lowlevel_driver_};
        auto head = queue_.head();
        if (!head) {
            return false;
        }
        const auto remaining =
            static_cast<Ticks::tick_diff_t>(head->deadline_.tick_ - clock_.current().tick_);
        out = remaining > 0 ? static_cast<Ticks::tick_t>(remaining) : 0;
        return true;
    }
```

注释把它的身份说得很准:等待队列的队头离现在还有多远,这是一个想睡觉的系统在决定睡多深之前要问的答案。返回值的类型是一个布尔,剩余的拍数咱们拿引用接出来,队列空了就交回假,意思是不存在任何的截止时刻,连问都不必问了。差值的算法您应该眼熟,`deadline_ - now` 被转成了 `tick_diff_t` 再判符号,跟《一秒钟的定义权》里 `is_after` 的回绕算术是同一套,所以队头就算刚跨过回绕线,差值的符号照样给得出正确的早晚。已经过期的队头,剩余值被钳成了零交回来,过期不丢弃的老纪律在这里也没有松。

进临界区的理由咱们也照例过一遍:队列和当前时刻是 ISR 与任务两头共享的状态,tick 中断随时都有可能从咱们手上切进来,咱们读队头读到一半被人改了队列,拿回去的数字就是碎的。这个接口被设计成查一次给一个自洽的答案,调用方拿到手就不必自己关中断了,分寸和 `current()` 是一样的。

## 睡前的布防:idle_sleep 的上半场

接口有了,改动的另一半在 `src/arch/arm_cortex_m3/time_kernel_cm3.cpp`。`switch.cpp` 里 idle 的任务体从裸 `wfi` 换成了 `idle_sleep()`,注释给它起的名字是床。这个函数开了两个临界区,一个管睡前的布防,一个管醒来的结算,咱们头一半头一半地看。`ZEROS_TICKLESS` 关着的时候,整个函数就只剩一句裸的 `wfi`,逐位地等价于老行为,咱们下面看的是宏开着的样子。布防的进门只有两个动作:

```cpp
    // ---- arming: one atomic section, races settled by draining ----
    asm volatile("cpsid i");
    drain_pending_expiry(1); // a periodic tick may have expired on our way in
```

进了门就关中断,然后立刻调了一次 `drain_pending_expiry(1)`。这一步看着是多余的,其实是布防里最容易漏的一拍:咱们从上一个 tick 中断返回到走到这一行,路上花的时间可能已经让一个周期拍到期了,计数器已经回绕了,中断也已经挂起了,咱们却还没来得及看它一眼。布防要是跳过了这一步,这笔已到期又未处理的时间就悬在了半空,后面您会看到结算靠什么把它接住。开销上它其实也不亏,传进去的结算量是一拍,真有一拍到期就把它结了,没有的话,咱们也只是多读了一次寄存器。

咱们接着问队头,布防的主分支跟着就是重装的写法:

```cpp
    std::uint32_t due = 0;
    const bool has_deadline = system_time.next_due_in(due);
    if (has_deadline && due > kTicklessThreshold) {
        // clamp to the 24-bit LOAD width; longer spans are split by the
        // idle loop itself — each expiry wakes us once to re-consult
        const std::uint32_t max_per_shot = 0xFFFFFFu / cycles_per_tick_;
        if (due > max_per_shot) {
            due = max_per_shot;
        }
        one_shot_ticks_ = due;
```

拿到了 `due` 之后,前路分成了三个分支,剩余时间大于 `kTicklessThreshold` 的才值得重装,这个阈值的取值是 2,注释把经济学写得明明白白:太近的到期点不值得动 one-shot,周期节拍本身就是 one-shot 拆成一拍一期的极端形态,反正一拍之内就有中断来接咱们的班,重装寄存器的写入开销反而白付。门槛过了还有一道硬件的坎,注释里的那句就是给它的:`clamp to the 24-bit LOAD width`。SysTick 的计数器只有 24 位,重装值最大写到的就是 `0xFFFFFF`,咱们这块板主频 72MHz,一拍要走的是 72000 个周期,咱们拿 `0xFFFFFF` 除一下,得出的上限是 233 拍,折成了时间,就是二百三十三毫秒的工夫。想要的睡眠要是长过这二百三十三毫秒,一发 one-shot 就装不下了,代码给的答案是钳到上限,剩下的跨度不靠硬件,靠的是 idle 循环自己分期:响了一回,醒了一次,重新问了一遍队头,再装上一发新的,注释把这套做法叫成了分期续杯,咱们的标题借的就是注释里的原词。诚实的代价是分期之间多了几回唤醒,不过每回唤醒只做重布这一件事,比每毫秒打一次的周期心跳,还是省了两个数量级的量。

咱们念分支的尾巴,尾巴上是三行寄存器的写,再往下是没被选中的两个分支与 `wfi`:

```cpp
        *kSystRvr = due * cycles_per_tick_ - 1;
        *kSystCvr = 0;
        *kSystCsr = kSystEnable | kSystTickInt | kSystClkSource;
    } else if (!has_deadline) {
        *kSystCsr = kSystClkSource; // stop: nothing ever comes due
    }
    // else: due in 1..threshold ticks — leave the periodic table, wfi
    // wakes within one tick anyway
    asm volatile("cpsie i");

    asm volatile("wfi");
```

重装的动作本身是三行寄存器写:重装值寄存器 RVR 写进 `due * cycles_per_tick_ - 1`,当前值寄存器 CVR 写了个零,控制与状态寄存器 CSR 上写的是使能、中断、时钟源三个位。分支里剩下的两个咱们也念完。没人等的时候走停表的分支,`*kSystCsr = kSystClkSource` 把 ENABLE 的位清掉,计数器整个地停了下来,注释里的说法是时间无债可还,醒来了再恢复周期节拍。夹在中间的情形,一到两拍内就到期的,咱们什么都不动,咱们的周期表也照旧留着,反正 `wfi` 一拍之内就有中断来接咱们。

## 竞态三件套:读清、撤 pending、手工结算

现在咱们把 `drain_pending_expiry` 单独请出来,它是整个 tickless 里最要紧的一段:

```cpp
// Drain a possibly-pending SysTick expiry: withdraw the NVIC pending bit
// and, if the counter really wrapped, settle that elapsed time on the
// ledger by hand. Without this, an expiry racing our (atomic) re-arm
// would be misread: the handler would see one_shot_ticks_ set and settle
// a whole one-shot for what was a single periodic tick.
[[maybe_unused]] void drain_pending_expiry(std::uint32_t settle_ticks) {
    const bool wrapped = (*kSystCsr & kSystCountFlag) != 0; // read-clears
    *kNvicIcpr = kSystickIrqBit;                           // un-pend it
    if (wrapped && settle_ticks != 0) {
        ZerOS::arch::cortex_m3::system_time.on_elapsed(settle_ticks);
    }
}
```

要理解它防的是什么竞态,咱们把案发过程排一遍。布防走到一半的时候,一个周期拍到期了:计数器数到了零,COUNTFLAG 立了起来,NVIC 里 SysTick 的挂起位也置上了。可咱们此刻正握着临界区,中断是进不来的,咱们只能挂着等。布防跟着继续跑它的下半程,`one_shot_ticks_` 被写成了 500,RVR 换成了五百拍的装载值。咱们的临界区一放,挂着的 SysTick 中断进来了,服务程序一看 `one_shot_ticks_` 的值是 500,就会把它当成一发五百拍的 one-shot 到期,一口气给时间内核结算了五百拍。可实际流逝的只是一个周期拍。时间被凭空快进了四百九十九拍,队头的等待提前到期,该睡的任务被提前叫醒,这样的错误在板子上查起来是要命的,它每次都差一个不固定的量,而且只在布防恰好与到期挤在同一个窗口里才发作。

三件套要做的,就是一步一步地把路堵死。头一件是读 CSR 的 bit16,也就是 COUNTFLAG 所在的位。它的语义是 ARMv7-M 架构手册定下的:计数器自上一次读取以来数到过零,咱们读它就交回 1,而且读的动作本身把它清掉了。所以 `wrapped` 这个局部变量接住的是一次性的答案,这一拍到底数没数到零的问题,问过一回咱们就有了准信,证据也不会被别人抢了去。咱们照实说一句口径的边界:读清是手册写死的语义,写 CSR、写 CVR 的动作同样会把它清掉,咱们后面 restore 的次序靠的就是它。至于异常进入的时候动不动它,手册没有把异常进入写成清除它的行为之一,我是按“只有读和写才动它”来理解的,这里是我全篇最想请您在真正的开发板上亲眼看一回的地方,仿真器的 NVIC 模型未必把每条边角都照手册复现。

第二件是撤 pending 的事,咱们接着看。SysTick 在 NVIC 的异常编号里是 15,`kNvicIcpr` 指向的 ICPR 寄存器按位清挂起状态,写 `1u << 15` 就是把已经挂起的那次 SysTick 中断整个撤掉了。为什么要撤?因为这个到期的拍已经不打算走正常的中断路径了,它的后果由第三件手工完成:只要计数器真的回绕过,咱们就把流逝的拍数亲手结进时间内核,`on_elapsed(settle_ticks)` 被咱们调上一回,队头该到期的到期,该叫醒的叫醒,跟它自己走中断路径的结果分毫不差。撤了 pending 又手工结清,中断路径上就什么都不会剩下,后到的服务程序没有存在的理由,误读的入口被封死了。

三件缺了任何一件都不行,咱们各自设想一下缺了的后果。咱们不读 COUNTFLAG,就分不清挂着的中断是一拍还是一整发,撤 pending 就变成了凭空吞掉时间。不撤 pending 的话,手工结完之后挂起的中断还会进来,服务程序照着当下的状态再结一回,时间就被结了两次。不做手工结算的话,撤掉的 pending 就把真实的流逝抹掉了,睡着的任务被睡过头。读清拿的是证据,撤销堵的是路径,结算补的是事实,三个动作合到了一起,才是布防与到期这场比赛的完整判罚。

## 醒来的那半个临界区

`wfi` 把咱们唤醒之后,函数的下半场接手,咱们看原文:

```cpp
    // ---- wake: restore the periodic table, settle what we withdrew ----
    asm volatile("cpsid i");
    if (one_shot_ticks_ != 0) {
        // in flight (a peripheral woke us first, or it never fired):
        // settle honestly if it had fired, then take it off the books
        drain_pending_expiry(one_shot_ticks_);
        one_shot_ticks_ = 0;
    } else {
        // the handler already settled our one-shot — but a fresh
        // periodic tick may be pending since the restore
        drain_pending_expiry(1);
    }
    periodic_table_restore();
    asm volatile("cpsie i");
#endif
```

醒来的原因有两种,函数得分清它们各自的下场。一种是外设把咱们叫醒的,比如 UART 来了一个字节,one-shot 还在飞的路上,`one_shot_ticks_` 非零就是它还在飞的凭证。这时候走 `drain_pending_expiry(one_shot_ticks_)`:one-shot 要是已经数到零了,整段的拍数诚实结算,要是还没数到零的话,至少把这段时间里到期的周期拍结掉,然后 `one_shot_ticks_` 被清了零,在飞的那一发作废。另一种是 one-shot 自己到点把咱们叫醒的,服务程序已经把结算做完了,`one_shot_ticks_` 早就是零了,咱们落到 else 分支。这里再 drain 一回看着也是多余的,注释把理由写了:服务程序结算完跟着就恢复了周期表,恢复到醒来之间的这几微秒里,可能又有一个周期拍到期挂起了,咱们不把它接住,这一拍就丢了。

次序上还有一处值得您多看一眼:两次分支里的 drain 都排在 `periodic_table_restore()` 的前面,这不是随手写的。恢复周期表的动作要写 CSR 和 CVR,而按手册的口径,写 CSR 与 CVR 的动作都会把 COUNTFLAG 清掉。咱们要是恢复了再读,证据在读到它之前就被自己的写入销毁了,结算就永远地查不清了。读清排在会清掉它的写入前面,纪律的依据是架构手册本身,而不是哪个平台的偶然行为。

恢复本身是三行写的镜像动作,咱们把 `periodic_table_restore` 也看完:

```cpp
void periodic_table_restore() {
    if (cycles_per_tick_ == 0) {
        return;
    }
    *kSystRvr = cycles_per_tick_ - 1;
    *kSystCvr = 0; // any VAL write restarts the counter
    *kSystCsr = kSystEnable | kSystTickInt | kSystClkSource;
}
```

咱们看到的三行恢复,把周期表的寄存器带回了 `init_time` 当年写进去的值。注释把一个 Cortex-M 的常识也解释了:写 CVR 任何值都会把计数器重启到重装值从头数,所以 `*kSystCvr = 0` 并没有把计数器清成零,它做的是让节拍从一个干净的起点重新走。1kHz 的心跳就这样回来了,任务从睡梦里被叫醒的时候,世界的时间轴没有丢过一拍。

## SysTick_Handler 的分流

one-shot 到期走的是同一个服务程序的入口,不过入口处多了一个岔路,咱们看:

```cpp
extern "C" void SysTick_Handler() {
    if (one_shot_ticks_ != 0) {
        // one-shot expiry: settle the whole span, back to periodic. No
        // on_tick() — the only runnable thing here is idle (nobody to
        // slice); if the settlement readied someone, PendSV is already
        // pending and the handover happens on our way out
        const auto n = one_shot_ticks_;
        one_shot_ticks_ = 0;
        periodic_table_restore();
        zeros_tick_oneshot = zeros_tick_oneshot + 1;
        ZerOS::arch::cortex_m3::system_time.on_elapsed(n);
        return;
    }
    zeros_tick_periodic = zeros_tick_periodic + 1;
    ZerOS::arch::cortex_m3::system_time.on_elapsed(1); // report the time...
    ZerOS::arch::cortex_m3::system_sched.on_tick();    // ...and the beat (slice)
}
```

周期路径还是老的样子,时间报给了内核,节拍交给了调度器去切任务。one-shot 路径干的事不一样,它把在飞的拍数取了下来、清了零,恢复了周期表,然后把整段的拍数一口气结给时间内核,结完了就返回。周期路径里的那一句 `on_tick()`,它被故意地跳过了,注释把理由写成了三句:此刻唯一能跑的就是 idle,没有谁需要咱们切片。结算要是把某个睡着的任务叫醒了,就绪的位图一动,PendSV 的挂起位已经置上了。切换的交接不用服务程序操心,咱们出去的路上 PendSV 自己接管。这就是计时与调度在 tickless 下的分工:one-shot 是闹钟,只管把时间结清、把睡着的人叫醒,它是不排班的。

两个计数器 `zeros_tick_periodic` 和 `zeros_tick_oneshot` 也是这个 commit 加的,`extern "C"` 把它们挂了出来,任何 demo 都能按裸符号读到它们的值。它们不为内核干任何的活,存在的意义是给旁观者作证,咱们做对照实验,靠的就是它们交答案。

## host 考场与它的边界

commit 同时交了 host 的白盒测试,`test/test_stack_watermark.cpp` 的四个用例、41 条断言,验的是栈水位那一半,细节的事归《把栈刷满魔法数》讲。咱们在本机 checkout 到 `fb9072b` 跑了全量,用的工具链是 GCC 16.2.1,十五个目标全部通过了:

```text
100% tests passed out of 15

Total Test time (real) =   0.30 sec
```

不过 tickless 这一半,host 考场是够不着的,咱们照实划出边界:`idle_sleep` 的全部逻辑长在内联汇编和 MMIO 寄存器上,host 替身里没有 SysTick 的影子,没有 NVIC 的身影,`cpsid i` 也没有对应的物件。这样的代码,考场在仿真器和硅片的身上,接下来的对照实验就是它的验收。

::: warning
one-shot 的结算跑在 SysTick 中断的上下文里,`on_elapsed(n)` 一口气结算下来的是几百拍,队头的回调可能一次连着执行好几个,它们仍然受着《等三百毫秒》里的老约束:回调必须短,重活还是照旧递给 worker 的队列。tickless 改变的是中断的节奏,没有放松回调的纪律,咱们写回调的时候别忘了它。
:::

## Renode 对照:数字当面摆

咱们的实验链路照旧,做法是把 `git archive fb9072b` 导出的树放进 /tmp 的副本,再把 STM32CubeF1 从本机的 third_party 拷进去。用的交叉工具链是 arm-none-eabi-g++ 16.2.0,咱们拿四条 cmake 命令各配各编,开关全集中在同一个 CMake option 的身上:

```shell
cmake -B build-off -DCMAKE_TOOLCHAIN_FILE=cmake/arch/arm-none-eabi.cmake -DCMAKE_BUILD_TYPE=Release
cmake -B build-on  -DCMAKE_TOOLCHAIN_FILE=cmake/arch/arm-none-eabi.cmake -DCMAKE_BUILD_TYPE=Release -DZEROS_TICKLESS=ON
cmake --build build-off --target zeros-demo-tickless
cmake --build build-on  --target zeros-demo-tickless
```

体积的对照是真的,开宏的代价摆在两份 size 报告里,咱们看:

```text
FLASH:        4656 B        64 KB      7.10%      # 默认配置
FLASH:        4864 B        64 KB      7.42%      # ZEROS_TICKLESS=ON
```

208 字节的差价,换来的是布防、结算、恢复的整套机构。咱们觉得这笔开销在两秒钟省掉两千次中断的背景下划算得很,不过划不划算,最终要您拿自己的预算算。

demo 的剧本咱们照源码念一遍:任务 napper 连睡四回,每一回睡的都是五百毫秒,醒来拿时间内核的 `now()` 盖个章,最后把两个计数器的值交出来。仿真器用的还是 Renode 1.17.0,咱们把关着宏的固件跑六秒,串口窗的真实输出如下:

```text
[INFO] usart1: [host: 0.12s (+0.5ms)|virt: 0s (+0s)] ZerOS demo: tickless
[INFO] usart1: [host: 0.59s (+0.48s)|virt: 0.5s (+0.5s)] nap 1 woke @ 500ms
[INFO] usart1: [host: 1.08s (+0.49s)|virt:   1s (+0.5s)] nap 2 woke @ 1000ms
[INFO] usart1: [host: 1.58s (+0.5s)|virt: 1.5s (+0.5s)] nap 3 woke @ 1500ms
[INFO] usart1: [host: 2.08s (+0.5s)|virt:   2s (+0.5s)] nap 4 woke @ 2000ms
[INFO] usart1: [host: 2.08s (+0.3ms)|virt:     2s (+0s)] tickless: witnessed
[INFO] usart1: [host: 2.08s (+0.3ms)|virt:     2s (+0s)] mode: periodic 1kHz (ZEROS_TICKLESS off)
[INFO] usart1: [host: 2.08s (+0.3ms)|virt:     2s (+0s)] periodic ticks: 2000
[INFO] usart1: [host: 2.08s (+0.2ms)|virt:     2s (+0s)] one-shot fires: 0
```

咱们两秒钟数下来,得到的是两千次的周期中断,one-shot 一次也没有响到咱们头上,这就是心跳空转的现场。换上开宏的固件,剧本还是咱们熟悉的剧本,仿真器也还是咱们用过的那个,输出成了咱们下面的样子:

```text
[INFO] usart1: [host: 0.13s (+1.4ms)|virt: 0s (+0s)] ZerOS demo: tickless
[INFO] usart1: [host:  0.6s (+0.47s)|virt: 0.5s (+0.5s)] nap 1 woke @ 500ms
[INFO] usart1: [host:  1.09s (+0.49s)|virt:   1s (+0.5s)] nap 2 woke @ 1000ms
[INFO] usart1: [host:  1.59s (+0.5s)|virt: 1.5s (+0.5s)] nap 3 woke @ 1500ms
[INFO] usart1: [host:  2.09s (+0.5s)|virt:   2s (+0.5s)] nap 4 woke @ 2000ms
[INFO] usart1: [host: 2.09s (+0.5ms)|virt:     2s (+0s)] tickless: witnessed
[INFO] usart1: [host: 2.09s (+0.3ms)|virt:     2s (+0s)] mode: tickless idle (one-shot to next deadline)
[INFO] usart1: [host: 2.09s (+0.4ms)|virt:     2s (+0s)] periodic ticks: 0
[INFO] usart1: [host: 2.09s (+0.2ms)|virt:     2s (+0s)] one-shot fires: 12
```

周期中断从两千掉到了零,one-shot 打了十二发。这个数咱们拆给您看:五百拍一觉,单发的上限是 233 拍,装不下的时候就得分期,头两发各走了 233 拍,咱们再补一发 34 拍的短期,三发凑满了一觉,咱们把四觉加起来,正好凑成了十二发。省掉的是两千次,付出的是十二次,降幅到了百分之九十九还多。更要紧的是上面几行的节奏:四次小睡盖出来的章还是 500、1000、1500、2000ms,跟关宏的固件一拍都不差。睡眠的时间轴没有被分期弄散,结算的诚实在此处兑现,快进四百九十九拍的错误要是真在,这些章早就盖乱了。

咱们转回仿真器这边,照实要交代的事情有两桩。头一件说的是我本机的 Renode 1.17.0 认不出 `bluepill.repl` 里的 `I2C.STM32F4_I2C` 类型,咱们在副本里把 i2c1、i2c2 两个节点连着中断接线注释掉了,demo 不经过 i2c 的路径,行为是不受影响的,这个坎《一秒钟的定义权》的验收里也遇到过,用的是同一副解法。另一件说的是,今天所有的数字都来自 Renode 的模型,真正的开发板上这层对照我还没有跑过,布防与到期挤在同一个窗口里的窄窗竞态,仿真器未必复现得出硅片的时序,这一层我照实标着。

CI 的边界咱们也照实划。`scripts/ci.sh` 加了两条冒烟标记,其中 tickless 的标记是 “tickless: witnessed”,可是 ci.sh 的 configure 从来没有传过 `-DZEROS_TICKLESS=ON`,所以 CI 里跑的 tickless demo 其实是周期模式,宏开着的那一路不在 CI 的覆盖里。这是仓库有意留给您的作业,并非疏忽:您把上面的四条 cmake 命令跑一遍,把两段的串口输出对着看,两千次与十二次的差距就在您手边,数字比咱们转述的任何话都有说服力。

## 验收

本篇的可查证事实归拢为三件,咱们一件一件过。

头一件说的是两套构建与全量的测试。您 checkout 到 `fb9072b`,照上面的四条命令把开关两头的固件都编出来,size 报告应该给出 4656 与 4864 字节的两个数。host 侧的构建拿 `-DZEROS_BUILD_TESTS=ON` 走一遍,十五个目标全部通过了,其中 `test_stack_watermark` 的成绩单是四用例四十一断言。

第二件是 Renode 的对照。咱们还在同一个副本里,关宏的固件跑出 periodic ticks 2000 与 one-shot fires 0,开宏跑出的是 periodic ticks 0 与 one-shot fires 12,四次小睡的章一拍不差。您要是也在 Renode 1.17.0 上遇到 I2C 类型报错,把两个 i2c 节点注释掉了,它就起来了。十二发的构成是 233 加 233 加 34,您拿 `0xFFFFFF / 72000` 自己验一遍就能对上。

第三件是竞态三件套的对源。读清、撤 pending、手工结算的这三步,您对着 `src/arch/arm_cortex_m3/time_kernel_cm3.cpp` 的原文逐行核对:COUNTFLAG 是 CSR 的 bit16,ICPR 清的是异常 15 的挂起位,结算走的是 `on_elapsed`。您要是想亲眼看三件缺一的后果,把布防路径里进门的那次 `drain_pending_expiry(1)` 删掉再跑,悬空的周期拍就开始丢,这类改动的破坏面在 Renode 里看得见。

## 下一篇

心跳会睡觉了,内核的本事也齐了。下一篇《挂牌 v0.1.0》是咱们这一路手搓的收官,`6c0a4f7` 没有改内核的任何一行,干的是出门前的门面工程:README 的结构学、双语与徽章、双许可的边界,还有 v0.1.0 这个 tag 的全链路终验,host 的 ctest 要跑,Renode、QEMU 与真正的开发板上也要各走一遍,四个考场都点到了。终点就在那儿等着咱们,咱们那儿见。
