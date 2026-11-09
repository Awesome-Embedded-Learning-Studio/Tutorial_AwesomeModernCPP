---
title: "看 M 闭嘴那三秒:互斥锁、优先级继承与所有权移交"
description: "7dbbcdf 给同步家族添了第二件家具:MutexBase 把等待链按优先级排好座次,unlock 不搞重新竞争、所有权当面点交给队头的等待者,单层优先级继承靠调度器新交的 reprioritize 做一场外科手术。我在 /tmp 副本把 host 舞台模型的提升、移交、恢复全弧跑通,25 条断言全绿,Renode 的 L/M/H 剧场里,M 任务(搅局的中优先级)的打印从 2.0 秒安静到 2.3 秒,它闭嘴的窗口就是持锁者戴着高优先级在跑的窗口"
chapter: 4
order: 2
tags:
  - stm32f1
  - advanced
  - 嵌入式
  - mutex
  - cpp-modern
difficulty: advanced
platform: stm32f1
cpp_standard: [23]
reading_time_minutes: 24
prerequisites:
  - "谁最急,谁上场:就绪位图、任务三态与选人仲裁"
  - "睡死那个 bug:丢唤醒、park_for 与让错误用法编不过的建造者链"
related:
  - "事件来得再早也不许丢:计数信号量与八操作的 SystemContext 接缝"
current_status:
  title: M 闭嘴三秒演出成功
  detail: ZerOS的篇章正在打磨ing
  variant: polishing
  icon: 🎭
---

# 看 M 闭嘴那三秒:互斥锁、优先级继承与所有权移交

上一篇讲信号量的文章给咱们留了一个顺理成章的追问:信号量数的是“事情发生过几次”,可是两份任务代码要共用同一个硬件寄存器、同一段缓冲区的场合,光数次数是不够用的,咱们还得回答“现在轮到谁用”。回答这个问题的家具叫互斥锁,它在一个计数之外多管了两件事:所有权归谁,以及归谁的时候谁说了算。咱们这一篇拆的就是它,锚定的 commit 是 `7dbbcdf`,subject 写的是 “feat: mutex”,交了 14 个文件,新增了 438 行,删除了 17 行,主角就是 104 行的 `include/ZerOS/kernel/sync/mutex.hpp`。

它带来的东西,咱们概括成三件:一条按优先级排好座次的等待链,一次不放回竞争的 unlock 当面移交,一层只提升直接持有者的优先级继承。第三件要动调度器的数据,所以同一个 commit 还给调度器添了一把叫 `reprioritize` 的手术刀,给 `SystemContext` 的接缝添了两个动词。咱们今天的故事有个具体到毫秒的画面:Renode 的串口窗里,M 任务本来是每 100 毫秒喊一嗓子的,从虚拟时间的 2.0 秒到 2.3 秒它一个字都没有,那阵安静是持锁者戴着提升后的级别在跑的痕迹。

## 接缝添了两个动词:prio 与 reprioritize

动锁的前一步,咱们看接缝动了哪里。`SystemContext` 是同步原语认系统的要求清单,上一篇咱们数过它的动词,当时的清单里有 `current_task`、`ready`、`block`、`sleep_for`、`yield`、`now`、`self` 加一对 `lock` 和 `unlock`。`7dbbcdf` 在清单上添了两个成员,咱们看 `include/ZerOS/kernel/system_concept.hpp` 的改动:

```cpp
template <typename System>
concept SystemContext = requires(System& s, base::BorrowedPtr<sched::TCB> t,
                                 clock::Ticks::tick_t span,
                                 sched::TaskPriority_t prio) {
    { s.current_task() } -> std::same_as<base::BorrowedPtr<sched::TCB>>;
    { s.ready(t) };
    { s.block(t) };
    { s.sleep_for(t, span) };
    { s.yield() };
    { s.now() } -> std::same_as<clock::Ticks>;
    { s.self() } -> std::same_as<System&>;
    { s.prio(t) } -> std::same_as<sched::TaskPriority_t>;  // PI wants to read
    { s.reprioritize(t, prio) };                            // ...and to lift
    s.lock();
    s.unlock();
};
```

注释把两个动词的分工写得很省,咱们把它翻成白话:继承的判断要问一句“锁的持有者现在排几级”,问的就是 `prio`,判断成立了,咱们还要把持有者的级别改到等待者的级别,干这个的是 `reprioritize`。而读写各归一个动词的安排,让锁的代码自己碰不到 TCB 的字段,写权的钥匙还是留在调度器手里的。板上的实现在 `src/arch/arm_cortex_m3/system.cpp` 里,两个函数都是直通调度器的一行转发,咱们看一眼就放心了:

```cpp
sched::TaskPriority_t CortexM3System::prio(base::BorrowedPtr<sched::TCB> t) {
    return system_sched.prio(t);
}

void CortexM3System::reprioritize(base::BorrowedPtr<sched::TCB> t,
                                  sched::TaskPriority_t new_prio) {
    system_sched.reprioritize(t, new_prio);
}
```

还有一处我得指给您看,同步原语自己不握临界区了,握的是接缝里的 `lock` 和 `unlock`。`CortexM3System` 的实现底下垫的是咱们上一站讲过的 `CortexM3CriticalSection`,它往 BASEPRI 里写的屏蔽级别还是 0x40,数值比 0x40 小的中断照样能进来。这个口径咱们碰过的,上一站的《配平中断优先级,再让中断敲门》那一篇里就有了,NVIC 里给外设中断排的级别必须比它大,不然咱们的临界区会被撕开。锁和中断的级别怎么配平,那一篇已经讲过了,咱们把注意力交还给调度器那把新手术刀。

## reprioritize:改级别是一场手术

您也许会觉得,改个优先级不就是赋个值吗。调度器会用它的结构告诉您,赋值是不够的。就绪任务住在 32 张按级别分的 FIFO 链表里,位图上记着哪些级还有人的位。您要是把一个还挂在队里的任务的 `task_priority_` 直接改小,它就躺在一张位图已经查不到它的链表里了,往后咱们选人永远选不到它,这样的任务等于被活埋了。所以 `reprioritize` 得按任务的三种状态分开动刀,咱们看 `include/ZerOS/kernel/sched/scheduler.hpp` 里新增的这段:

```cpp
    void reprioritize(base::BorrowedPtr<TCB> t, TaskPriority_t new_prio) {
        irq::CriticalGuard guard{driver_};
        if (t->task_priority_ == new_prio) {
            return;
        }
        if (t->state_ == TaskState::Ready) {
            fifo_[t->task_priority_].remove(t);
            if (fifo_[t->task_priority_].empty()) {
                present_.clear(t->task_priority_);
            }
            t->task_priority_ = new_prio;
            push_back(t.get());
        } else {
            t->task_priority_ = new_prio;
        }
    }
```

进门前它照例握住了临界区,级别没变的请求直接打发掉。真正的分岔落在任务的状态上。如果任务还挂在就绪的队列里,咱们得把它从旧级别的链表上摘下来,链表要是空了,位图上对应的位咱们也得清掉,然后才改级别、按新的级别重新入队,位图和链表从里到外换了座次还咬合得严丝合缝。任务不在队列里的时候,它的状态只有 `Running` 或者 `Blocked` 两种,身上没有链表要咱们维护,改个数字就够了,Running 的马上按新级别参与下一次选人,Blocked 的要等 `ready` 唤醒的时候,`push_back` 自然把它放进新级别的队里,位图的那一头由 `push_back` 负责 set,咱们一笔都不用补。

同一个 commit 还给 `Scheduler` 添了一个只读的小函数,咱们也记下它的名字:

```cpp
    // a plain read; diagnostics and the boost-or-not decision both want it
    [[nodiscard]] TaskPriority_t prio(base::BorrowedPtr<TCB> t) const { return t->task_priority_; }
```

注释说的两位顾客,一位是做诊断的,另一位就是锁的“要不要提升”的判断。优先级的约定还是上一站定下的那句:数值越小越急,`0xFF` 留给了 idle。后面的继承逻辑全建立在这个方向上,咱们到锁的代码里会反复用到“大于就是更缓”的读法。

## 等待链:排座次的链表

主角登场之前咱们把等待链的存在理由说清。拿不到锁的任务得去链上排队,而排队就有排法的讲究,互斥锁的交接是一次性的,`unlock` 点交的对象定了,持有者就定了,它没有信号量那样的计数器在中间重新裁决,座次就得在排队的时候排好。排法要是照着信号量的样子排成纯 FIFO,急的任务就可能排在一个慢任务的后面干等,排法要是干脆不排、解锁之后让大伙重新抢,刚被唤醒的急任务更可能眼睁睁看着自己等来的锁被一个恰好路过的慢任务领走,白等了一场。所以等待链的每个节点都得带着自己的级别,链本身按级别把座次排好了。

现在主角登场了,咱们打开 `mutex.hpp`。它开头就定义了等待者的档案和错误码:

```cpp
enum class MutexError : std::uint8_t { Ok, NotYourLock, SelfLocked };

struct MutexWaiter : base::SelfNode<MutexWaiter> {
    constexpr explicit MutexWaiter(sched::TCB* task, sched::TaskPriority_t prio) noexcept
        : task_(task), prio_(prio) {}
    sched::TCB* task_;
    sched::TaskPriority_t prio_;
};
```

每个等锁的任务在这里登记两样东西:它是谁,它来的时候排几级。咱们留意 `prio_` 的身份,它是登记时的快照,别把它当成活的引用,后面移交的判断用的就是登记时的数字。锁的本体咱们看成员,它比信号量多背了几样家当,所有权记在谁的头上、继承借来的级别怎么还,都得在锁的身上放着:

```cpp
template <ZerOS::system::SystemContext System> struct MutexBase {
    constexpr MutexBase() noexcept = default;

  private:
    sched::TCB* owner_{};
    sched::TaskPriority_t saved_prio_{}; // saved
    bool boosted_{false};

    struct ByPrio {
        bool operator()(const MutexWaiter& fresh, const MutexWaiter& parked) const noexcept {
            return fresh.prio_ < parked.prio_;
        }
    };
    base::SelfList<MutexWaiter, ByPrio> waiters_{};
};
```

私有的家当就四样:持有者的指针,提升前留底的原级别,一个正在提升中的标记,还有咱们要排座次的等待链本身。等待链的类型,是 `SelfList` 这个模板配上 `ByPrio` 的比较器,咱们上一站见过的 `SelfList` 不给比较策略的时候是纯 FIFO,给了比较策略它就换了一副脾气,插入的函数我搬来看:

```cpp
    void insert(BorrowedPtr<Stuff> new_stuff) {
        Stuff** slot = &stuff; // Lets walk!

        while (*slot && !less_(*new_stuff, **slot)) {
            slot = &(*slot)->next_;
        }

        new_stuff->next_ = *slot;
        *slot = new_stuff.get();

        // OK, quit here
    }
```

它从链的头一路往下走,新来的要是不比路过的更急,它就接着往下走了,`less_` 比的是 `prio_` 的小于,所以级别数值小的(更急的)插在前面。这个条件还有个 subtle 的好处,咱们拿同级的情况走一遍:两个等待者同级的时候,`fresh < parked` 的比较不成立,新来的会走到同级等待者的后面插进去,同级的次序还是按到达的早晚排,咱们用一条链表加一个比较器,就把急的插队、同级的守序这样的两层语义都排出来了。队头永远是最急的等待者,`pop_head` 弹的是队头,弹出来的就是它,这是后面移交的根基。

## lock:在门口问一次,在循环里等移交

加锁的流程是本文件里最长的函数,咱们分段来读。动代码之前咱们把它要拦的事故数一遍,读的时候心里就有地图了。头一桩要拦的是自己等自己,任务把它已经拿着的锁再来一次 `lock`,门口就把这样的同名请求给拦下了,不然循环里的 `owner_` 等于自己就有了两种意思,是移交点给咱们的,还是咱们本来拿着的,代码就分不清了。第二桩出在移交的认领上,`unlock` 点交完了把等待者叫醒,醒来的人要是不再查一眼 `owner_`,点交的成果就没人认领,所以循环头顶每次都得重查。级别是第三桩事故的来源,持有者顶着数值大的缓级别慢慢跑,门外等着的是数值小的急任务,缺了提升,急任务就得陪着缓任务和不相关的中间任务耗,优先级反转的伤害就从这里进来,所以插进等待链之后还得办一次提升。三样拦法在代码里各有各的位置,咱们一截一截对上去:

```cpp
    MutexError lock() noexcept {
        auto& system = System::self();
        const auto current_task_ = system.current_task();

        {
            // ask ONCE at the door: re-locking what I already hold.
            // Inside the loop, owner==me can only mean "handed to me while parked"
            ZerOS::irq::CriticalGuard g{system};
            if (owner_ == current_task_.get()) {
                return MutexError::SelfLocked;
            }
        }

        MutexWaiter me{current_task_.get(), system.prio(current_task_)};
```

开头的孤立临界区,咱们照着注释念一遍它的用意:自己已经持有的锁再来一次 `lock`,这是自己等自己的死结,门口就把它拦下了,就回 `SelfLocked` 了。为什么只问一次?因为进了循环之后 `owner_` 等于自己只剩一种可能,咱们睡着的时候持有者 unlock 了,移交把所有权点给了咱们,这时候的同名是喜事不是病。作者用两次检查的位置把这个区分讲清楚了,注释里的 “ask ONCE at the door” 画的就是分界线。

```cpp
        while (1) {
            ZerOS::irq::CriticalGuard g{system};

            if (owner_ == current_task_.get()) {
                waiters_.remove(&me);  // pop_head already got me; this is belt and braces
                return MutexError::Ok; // handed over while I parked
            }

            if (!owner_) {
                owner_ = current_task_.get();
                waiters_.remove(&me);
                return MutexError::Ok;
            }
```

循环体每次握住临界区之后干的是四件事,咱们一件一件数过来,头两件就住在上面的代码里。头一件查的是移交,查过了就把自己从等待链上摘下来返回,注释自嘲 `remove` 的这一句是 “belt and braces”,因为移交方的 `pop_head` 通常已经把咱们摘过了,咱们再摘一次算是双保险了。第二件查的是空位,锁要是没了持有者,咱们就直接把锁占下。

```cpp
            waiters_.insert(&me);

            if (system.prio(owner_) > system.prio(current_task_)) {
                if (!boosted_) {
                    // OK, owners self level
                    saved_prio_ = system.prio(owner_);
                    boosted_ = true;
                }

                system.reprioritize(owner_, system.prio(current_task_));
            }

            system.block(current_task_);
        }
    }
```

第三件是今天的正菜:锁已经有了持有者,而且持有者的级别数值比咱们大(也就是更缓),那就把持有者的原级别存进 `saved_prio_`,立起 `boosted_` 的标记,然后调 `reprioritize` 把持有者抬到咱们的级别。`boosted_` 单层二字就落在这个标记的身上,它保证留底的动作只发生一次,持有者已经被抬过一次的时候,再来一个更急的等待者,咱们也只会继续调 `reprioritize` 把它抬得更高,不会把已经抬高的级别当成原级别存第二遍,原级别在咱们这儿永远只有一份。第四件是收尾的 `block`,咱们睡去,醒来之后咱们再从循环头顶重新查一遍。

这里咱们把边界也照实说清:继承只抬锁的直接持有者,持有者要是偏偏又去等另一把锁,这一层的代码不会沿着持有链往上递归,`saved_prio_` 和 `boosted_` 也是每把锁各记各的。教科书里的完整版继承协议还要处理嵌套持有多把锁的恢复次序这些问题,ZerOS 在这个 commit 里做的确实是单层版,咱们不替它扩写,后面验收的段落里边界还会再出现一次。

## unlock:不搞重新竞争,所有权当面点交

`unlock` 一口气要办的事有三件,咱们分段来读:验一下锁归不归您,把继承借来的级别还回去,再把所有权当场点交给队头的等待者。归属为什么不摆回无主让大伙重新抢?重新抢的写法里,刚被唤醒的等待者要和满世界乱跑的任务比眼疾手快,急的任务可能眼睁睁看着自己等来的锁被一个恰好路过的慢任务领走,白等了一场,当面点交把归属定在释放的那一刻,交出去的就是排好的座次:

```cpp
    MutexError unlock() noexcept {
        auto& system = System::self();
        const auto current_task_ = system.current_task();
        ZerOS::irq::CriticalGuard g{system};

        if (owner_ != current_task_.get()) {
            return MutexError::NotYourLock;
        }
        if (boosted_) {
            system.reprioritize(current_task_, saved_prio_);
            boosted_ = false;
        }
```

不是您的锁您来放,`NotYourLock` 就把您拒了,重复释放走到这一步也是一样的,一样被拒了,因为放完之后 `owner_` 已经不指着您了。您是被提升过的,放锁的头一件事就是把级别降回 `saved_prio_` 留的底,继承的代价到这里就结清了。

```cpp
        if (auto head = waiters_.pop_head()) {
            owner_ = head->task_;
            if (auto next = waiters_.head(); // Is the new headers request
                next != nullptr && next->prio_ < system.prio(owner_)) {
                saved_prio_ = system.prio(owner_);
                boosted_ = true;
                system.reprioritize(owner_, next->prio_);
            }
            system.ready(head->task_);
        } else {
            owner_ = nullptr;
        }
        return MutexError::Ok;
    }
```

有人排队的话,`pop_head` 弹出的队头直接写进 `owner_`,最急的等待者拿到继承,归属在释放的那一刻就定了。`owner_` 的值写完之后,咱们的代码还没有撒手,它接着看链上剩下的下一位等待者:要是下一位的级别比新持有者更急,新持有者自己马上就被提升了,`saved_prio_` 记的是它自己的级别,继承在移交的瞬间就换了一棒。移交完了才调 `ready` 把新持有者放回就绪队列,它接下来的竞争只发生在 CPU 上,不再发生在锁上了。没人排队就简单了,咱们把 `owner_` 清成空,锁回到无主的状态。

## host 舞台模型:提升、移交、恢复全弧

机制讲完了,咱们上考场。`test/test_mutex.cpp` 的正文有 210 行,它面对一个真实的困难:host 的替身没有真正的上下文切换,`request_switch` 只是给计数器加了 1 而已,任务根本不会真的“睡着再醒”。它的解法是搭一个舞台,咱们看舞台的搭法:

```cpp
// Mock side of the D22 doctrine, mutex flavor: prio/reprioritize forward to
// the REAL scheduler (PI surgery gets exercised for real); during_sleep runs
// "the world while I park" — the host trick that makes the re-check loop
// converge without a real context switch.
struct FakeSys {
    static inline Scheduler<HostSwitchPort>* sched_;
    static inline Kernel<SleepPort>* time_;
    static inline std::function<void()> during_sleep = nullptr;
```

替身系统把 `prio` 和 `reprioritize` 原样转发给真的调度器,所以继承的手术在 host 上动的是真调度器的结构,纸面上的推演可给不出这个。真正的戏法是 `block`:任务声明阻塞之后,替身接着执行 `during_sleep` 里装的闭包,它扮演的是“我睡着之后世界发生的事”。锁的 `lock` 循环要靠别人放锁的移交才能收敛,舞台就让别人在咱们睡着的瞬间把戏演完,循环醒来的那一刻一看,移交已经发生了。这样的并发是假的,它是一场按剧本走的舞台剧,可是走的是生产代码的路径,断言落在真实调度器的状态上。

全弧的用例咱们整段看,它把提升、移交、恢复的整条弧一口气演完了:

```cpp
TEST_CASE("boost, hand-over and restore: the whole inheritance arc", "[mutex][pi]") {
    World w;
    Mtx m;

    w.become(w.low.tcb); // low (2) takes the lock...
    REQUIRE(m.lock() == MutexError::Ok);

    w.become(w.high.tcb); // ...then high (0) comes for it
    FakeSys::during_sleep = [&] {
        CHECK(TCBKeys::prio(w.low.tcb) == 0);  // holder wears my level now
        w.become(w.low.tcb);                   // world: the holder runs...
        CHECK(m.unlock() == MutexError::Ok);   // ...releases (restore + hand over)
        w.become(w.high.tcb);                  // ...and the waiter takes the CPU back
    };
    REQUIRE(m.lock() == MutexError::Ok); // handed to me while parked

    CHECK(TCBKeys::prio(w.low.tcb) == 2); // restored, not left boosted
    CHECK(m.unlock() == MutexError::Ok);  // and yes, the lock is mine now
}
```

咱们把剧情跟着断言捋一遍。2 级的 low 上台拿到了锁,0 级的 high 来抢,`lock` 走到 `block` 的那一刻,舞台的闭包开演:第一条断言查的是提升,low 的级别此刻必须是 0,它戴着 high 的级别,这就是 `reprioritize` 动过刀的证据。然后 low 被咱们请上台跑,调了 `unlock`,这一步同时干了恢复(降回 2 级)和移交(`owner_` 写成 high)。high 回了台,`lock` 的循环醒来,一眼看见 `owner_` 写的是自己,移交到了手。用例末尾还有两条收尾的断言:low 的级别查回来是 2,提升的级别还了回去,high 拿到锁了之后再放一次,返回的是 `Ok`,因为锁确实是它的,所有权移交落到了实处。

其余的用例各守一个口。门口的 `SelfLocked` 和异手的 `NotYourLock` 合在一个用例里验过了,双重释放也验了,全都给拒了。`reprioritize` 自己有单独的用例,名字起的是“三种形状下都让就绪环保持诚实”,队列里的、正在跑的、睡着的各来一遍,队列里的那个从 3 级提到 0 级之后,咱们拿 `pick_next` 也能立刻选出它来,手术没有把人活埋进查不到的队列。我照实交代一个没验到的角落:多个等待者按紧迫度排座次这件事,host 用例只做了注释里的移交(作者自己的注释把多等待者的排序留给了 Renode 的 demo),而板上的 demo 其实也只安排了一个等待者,排序的路径在 host 和板上都没有真的跑到过,咱们把这句话原样留给您。

整个测试的成绩是这样的:我拿 GCC 16.2.1 配了 host 构建,`ctest` 的九个目标全部通过,总耗时的数字是 0.19 秒,成绩我贴在下面:

```text
100% tests passed out of 9

Total Test time (real) =   0.19 sec
```

mutex 自己的成绩单我单独念一遍,四个用例、二十五条断言都过了:

```text
All tests passed (25 assertions in 4 test cases)
```

## Renode 剧场:M 闭嘴的窗口

host 舞台证明了机制,可是优先级反转肉眼可见的说法,还得在真的调度节奏里看。`example/main.cpp` 这回交了六个任务,咱们只拆剧场的那一组,收字节的那部分归姊妹篇管,我不越界。三个演员的户口是这样的。持锁的 L 叫 `task_holder`,级别排的是 2,它拿锁的节奏是每 2000 毫秒一次,拿到之后是不睡的,干完 300 毫秒的忙等再放。要锁的 H 叫 `task_urgent`,级别排的是 0,它要锁的节奏是每 2100 毫秒一次,到场的时间天生比 L 晚 100 毫秒。捣乱的 M 叫 `task_bully`,级别排的是 1,它跟锁的关系是毫无关系,打 `M: running` 这一行是它的全部戏份,打印的节奏是每 100 毫秒一行。

咱们看 L 的任务体之前,忙等的用意得说清,剧场要演的伤害,靠的就是这段忙等:持锁的人要是拿着锁睡它的大觉,睡觉是不占 CPU 的,H 来了等一会儿也没事,反转就伤害不到任何人了,只有 L 偏偏醒着占住 CPU 干等 300 毫秒,H 才是被真正摁住的那个,M 的捣乱才有戏可演:

```cpp
void task_holder(void*) { // L
    for (;;) {
        ThisTask::sleep_for(Milliseconds{2000});
        if (g_theater.lock() != ZerOS::sync::MutexError::Ok) {
            continue;
        }
        ZerOS::board::print("L: locked, busy 300ms\r\n");
        const auto until = ThisTask::now().tick_ + 300;
        while (ThisTask::now().tick_ < until) { // busy hold: the whole point
        }
        ZerOS::board::print("L: unlocked\r\n");
        g_theater.unlock();
    }
}
```

注释里那句 `busy hold` 指的就是上面说的故意之处,咱们把开演之后的次序走一遍:H 一到场就 `block` 在锁上了,继承就生效了,L 被 `reprioritize` 抬到了 0 级,从此压过 1 级的 M,M 的打印被摁停,L 的忙等一毫秒不拖地跑完,`unlock` 当场降级、当场移交了,H 在三微秒之后就宣布到手了。M 闭嘴的窗口和 L 戴着高级别的窗口是同一个窗口。

我自己在 /tmp 的副本里跑的链路是这样的:`git archive 7dbbcdf` 导出树,补上 CMSIS 的头,我拿 arm-none-eabi-g++ 16.2.0 做了交叉编译,体积是真实的:

```text
   FLASH:        3580 B        64 KB      5.46%
             RAM:        2248 B        20 KB     10.98%
   text	   data	    bss	    dec	    hex
    3388	     192	   2056	   5636	   1604
```

然后咱们上 Renode 跑了 9 秒。本机的 Renode 1.17.0 认不出 `bluepill.repl` 里的 `I2C.STM32F4_I2C` 类型,我把 i2c1、i2c2 两个节点整块注释掉了,demo 是不经过 i2c 的,行为是不受影响的,这个处理跟前面几篇的做法一致。串口窗的真实输出,咱们裁剧场开演的那一段:

```text
[INFO] usart1: [host: 2.14s (+0.24ms)|virt:       2s (+2.27µs)] M: running
[INFO] usart1: [host:  2.14s (+0.33ms)|virt:       2s (+3.08µs)] L: locked, busy 300ms
[INFO] usart1: [host:    2.34s (+0.2s)|virt:    2.1s (+99.99ms)] H: wants the lock
[INFO] usart1: [host:   2.71s (+0.37s)|virt:       2.3s (+0.2s)] L: unlocked
[INFO] usart1: [host:  2.71s (+0.67ms)|virt:        2.3s (+3µs)] H: LOCKED (inheritance paid off)
[INFO] usart1: [host:  2.71s (+0.42ms)|virt:     2.3s (+5.38µs)] B
[INFO] usart1: [host:  2.71s (+0.23ms)|virt:     2.3s (+2.26µs)] M: running
```

咱们对着 virt 的时间轴读。M 的上一声喊在 2.0 秒,照它的节奏算,2.1 秒和 2.2 秒本该各有一声的,可是日志里的 H 在 2.1 秒把要锁的话喊完了,下一行直接跳到了 2.3 秒的 L 放锁那一行,中间是一片安静的,我数过原始日志,2.0 到 2.3 秒的窗口里 M 的行数是零。到了 2.3 秒,L 放锁的一行和 H 到手的一行之间只隔了 3 微秒,移交的量级就是 `pop_head` 加几次指针写,因为继承早就生效了,H 在等待链上等的是被提升了的 L,没有陪着 M 的慢节拍干耗。M 在 2.3 秒就复声了,降级和移交发生在了同一个临界区里,咱们看 M 的感觉,过渡是一点都没有的。4.3 秒的第二轮一模一样地重演,拿锁的还是 L,M 的行数在 4.3 到 4.6 秒之间是零,H 在 L 放锁之后的 3 微秒内到手,剧场两轮之间隔着的是 2.3 秒,H 的 2100 毫秒睡眠加上它被摁住的 300 毫秒,一轮就这么重演一遍。

那句标题里的“三秒”我得给您个交代:M 闭嘴的窗口实测是 300 毫秒,缺声的是 2.1 和 2.2 这连着的两拍,标题取的是这个“三”字的戏剧化说法,数字以咱们的日志为准,您别拿标题去跟测试报告对表。

## 验收

咱们把本篇可查证的事实归拢成三件。

第一件是 host 的考场。您 checkout 到 `7dbbcdf`,配好 host 的构建跑一遍 `ctest`,九个目标全过了。`test_mutex` 单跑的成绩是四个用例、二十五条断言。“提升、移交、恢复”全弧的断言落在真调度器的 `task_priority_` 上,提升的时候查到 0,恢复之后查回来的是 2,两个数字来自源码的行为,我没有做修辞加工。

第二件是板上的剧场。交叉编译的体积是 flash 3580 字节、RAM 2248 字节,Renode 9 秒的日志里,M 在 2.0 到 2.3 秒和 4.3 到 4.6 秒两个窗口各安静了 300 毫秒,H 两次都在 L 放锁之后 3 微秒内拿到了锁。您要复现,记得 Renode 1.17.0 得把 repl 里的两个 i2c 节点注释掉。

第三件是没验的和没做的,我照实摆。多等待者的座次排序,host 的用例和板上的 demo 都只各有一个等待者,排序的路径没有真实跑到过。继承是单层的,它只抬锁的直接持有者,而不沿着持有链往上递归,这是源码的边界,教科书里更完整的协议咱们没在仓库里见到,咱们也就不写它有。`MutexWaiter::prio_` 是登记时的快照,等待期间级别变过的任务,移交判断用的还是旧数字,源码就是这么写的,我按原样报告。

下一篇咱们换个问题问这套内核:芯片死了怎么办。fault 现场的档案、栈上的金丝雀、不占一个字节的日志,都是下一篇的正菜,更远处还留着栈水位与 tickless 的故事。咱们那儿见。
