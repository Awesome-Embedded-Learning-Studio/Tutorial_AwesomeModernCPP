---
title: "中断只递条子:环形队列、工作者线程与底半推迟"
description: "咱们拆 3570f27(feat: message queue and worker thread):定容环形缓冲把容量在编译期就定成 2 的幂,查空槽的取模被按位与替换掉,计数信号量当了有数据的信号,工作者线程连着队列带任务控制块,一粒堆内存都没花。ISR 只投递、永不睡,而 worker 的重活里敢睡 200ms,这正是底半推迟的全部意义。我在 /tmp 副本里跑通了七个 host 用例,54 条断言全部通过,又在 Renode 里给 USART1 注入了一个字符 d,看着 worker 报出 got the job,睡满 200ms 再报 job done,而它睡觉的当口,A 一秒一拍、B 七百毫秒一拍,都照常打印"
chapter: 5
order: 2
tags:
  - stm32f1
  - intermediate
  - 嵌入式
  - 循环缓冲区
  - 回调机制
difficulty: intermediate
platform: stm32f1
cpp_standard: [23]
reading_time_minutes: 24
prerequisites:
  - "一秒钟的定义权:tick、回绕算术与到期队列"
  - "谁最急,谁上场:就绪位图、任务三态与选人仲裁"
  - "在栈顶伪造出生证明:PendSV、SVC 与第一次切换"
related:
  - "芯片死前立遗嘱:HardFault 现场、栈金丝雀与零分配日志"
  - "等三百毫秒,不养一个任务:软件定时器与 fn+void* 硬通货"
---

# 中断只递条子:环形队列、工作者线程与底半推迟

上一站收了尾之后,demo 里的收字节链路是咱们验过跑得通的一副样子:USART1 的中断拿到一个字节,把它放进一个全局的 `char`,再发了一个信号量,把睡着的任务叫醒了。不过您把它再看一眼,就会发现骨架里藏着尴尬的事实:邮箱只有一个槽。字节是中断存的,取是任务干的,两个上下文共用的是一个 `char`,要是您在键盘上连着敲两个键,第二个字节就落在第一个还没被取走的槽上,把前面的覆盖了,覆盖得无声无息的,连个报错都不给的。

`3570f27` 把这个尴尬终结了。它的 subject 写的是 “feat: message queue and worker thread”,一口气交了 7 个文件,新增了 365 行,消息队列和工作者线程从它开始进了内核。同一主题的姊妹篇《芯片死前立遗嘱》管的是另一头的上下文,讲芯片在 HardFault 的现场里怎么留下遗言,咱们这篇不碰它的内容,只借它的篇名指个路。咱们这篇要回答的问题只有一个:**代码此刻跑在哪个上下文里**。中断里能做什么、不能做什么,不能做的那些活儿怎么递出去、又递给了谁,拆完 `3570f27` 的 diff,答案就在桌上了。

## 一个槽的委屈,咱们从头数

被替换掉的旧链路值得咱们看一眼,它丢了什么、留了什么,在新代码里都找得到各自的对应。上一版 demo 的家当就三行:

```cpp
constinit ZerOS::sync::Semaphore g_rx_sem{0};
constinit ZerOS::arch::cortex_m3::CortexM3CriticalSection mailbox_lock{};
char g_rx_byte = 0;
```

信号量负责的活是叫人,临界区护着的是里头的字节,`char` 本身就是整个邮箱了。任务侧的姿势咱们也数一下:它 `acquire` 信号量睡过去,字节来了,中断就放了一个单位,它也就醒过来了,锁进临界区把字节抄走了。您看出来了吗,信号量的计数和字节的槽位是两个互不相识的东西:信号量忠实地记着来了几个字节,邮箱却只给了一个格。两个字节前后脚到的话,计数爬到了 2,任务也就醒来了两回,可是它两回取到的是同一个格里的内容,咱们把它的两回捋一下:取回的是后到的那个,而第二回取到的还是它,因为格子早被覆盖了。信号量是没有记错的,是邮箱装不下了。

新代码把两边合成的就是同一个东西了,咱们看 `3570f27` 的 demo,邮箱是下面的样子:

```cpp
// the RX mailbox: a real queue now — a burst of bytes survives intact,
// no slot sharing, no hand-rolled flags
constinit ZerOS::sync::Queue<char, 16> g_rx_queue{};
```

它一共有 16 个槽的容量,每个字节占一个自己的格,谁也覆盖不了谁。注释里的那句 “a burst of bytes survives intact” 并不是光挂在嘴边的口号,host 侧有专门的用例看着它,咱们到考场的段落里当面对质。计数和槽位从此是同一个结构的两个成员,信号量成了队列身上的器官,它怎么接进去的,咱们顺着源码往下看。

## 定容环形缓冲:容量在编译期就被定成了 2 的幂

邮箱从一格扩到了十六格,摆格子的问题也就跟着来了。顺着数组的朴素摆法,是每取走头部的一个字节,就把身后的元素整体往前挪一格,取一个挪一格的代价搁在字节源源不断的串口路径上,是咱们养不起的常态。环形摆法把数组的首尾接了起来,取过的格子转一圈回来接着用,留在格子里的元素从头到尾一动不动,绕回开头靠的只是简单的算术。

`3570f27` 的队列走的就是环形,它的真身住在 `include/ZerOS/kernel/sync/msg_queue.hpp` 里,咱们把它的骨架搬出来:

```cpp
// messages cross the ring by memcpy
template <typename M>
concept PlainMessage = std::is_trivially_copyable_v<M>;

template <ZerOS::system::SystemContext System, PlainMessage Message, std::size_t Capacity>
struct QueueBase {
    static constexpr auto capacity = Capacity;
    constexpr QueueBase() = default;

    static_assert((Capacity & (Capacity - 1)) == 0, "power of two: & beats % on M3");

    Message ring_[Capacity]{};
    std::size_t head_{};
    std::size_t count_{};
};
```

三个成员撑起了整个缓冲:`ring_` 是定容的槽位数组,`head_` 是咱们读的位置,`count_` 记的是此刻压着几个元素。您找一圈就会发现它没有写指针,因为写的位置是算出来的,`head_ + count_` 算出来的就是下一个空槽,专门存它就纯属多余了。`PlainMessage` 这个 concept 要求消息类型是 trivially copyable 的,注释把原因带上了,消息跨过环形缓冲靠的是 `memcpy`,带构造析构、带堆指针的类型进了队列,拷过去的就是一副空壳,所以它把门关在了类型这一层。

`static_assert` 的那一行值得咱们停下来多看两眼。`(Capacity & (Capacity - 1)) == 0` 是判 2 的幂的老手法了,2 的幂减一之后,它的低位全是 1,自己跟自己按位与的结果才是零,6 减一是 5(它的二进制是 101),6 跟 5 与出来的就是 4,断言当场就把编译炸掉了。注释把动机也带了进来,写的正是 “power of two: & beats % on M3” 的原话,咱们翻成白话,就是容量为 2 的幂,按位与跑赢了取模。取模运算在 Cortex-M3 上是一条除法指令的活儿,按位与是一条 `and` 就办了的,中断的路径上每个周期都是钱,容量做成 2 的幂,取模就降级成了与运算,代价是 `Queue<char, 16>` 能用的,而 `Queue<char, 15>` 是连编译都过不去的。这个交换咱们觉得划算,因为定容本来就是咱们的设计意图,容量随便挑的反倒是例外。

写和读的动作都短,咱们一个一个看:

```cpp
    QueueError post(const Message& m) {
        auto& sys = System::self();
        ZerOS::irq::CriticalGuard g{sys};
        if (count_ == capacity) {
            return QueueError::Full;
        }
        ring_[(head_ + count_) & (capacity - 1)] = m;
        ++count_;
        semaphore.release(); // the unit is handed out only AFTER the data landed
        return QueueError::Ok;
    }
```

`post` 进门就握了临界区,咱们在前面几站见过这样的手法了,只要一个函数是有可能被中断调的,共享的状态就得关了中断再动。满员的判断是 `count_ == capacity`,满了就直接交回 `QueueError::Full` 了,一个字节都不写的,老数据也是一个都不动的。这就叫满拒绝:队列满了,新来的就被拒收,绝不会悄悄地覆盖。`Capacity` 被编译期的断言定成了 2 的幂,所以 `capacity - 1` 的低位全是 1,咱们按位与一下之后,`(head_ + count_)` 就老老实实地绕回了数组的开头,取模在这里降级成了一条 `and` 指令。写完之后 `count_` 涨了一,然后才轮得到 `semaphore.release()` 的出场,注释里的那句 “the unit is handed out only AFTER the data landed” 把次序的道理讲明了:单位要等数据落了位才发出去,反过来的话,叫醒的人扑到队列上一抓,抓到的却是没写进去的旧数据,咱们在丢唤醒的一篇里吃过次序的亏,这个亏仓库没有再吃过一遍了。

读的一侧更短,咱们看:

```cpp
    std::expected<Message, QueueError> pop_ring() noexcept {
        auto& system = System::self();
        ZerOS::irq::CriticalGuard g{system};
        Message out = ring_[head_];
        // pop one, we need to move our head!
        head_ = (head_ + 1) & (capacity - 1);
        --count_;
        return out; // explicit make
    }
```

`pop_ring` 的活是从 `head_` 取一个出来,读指针同样用按位与绕回了开头,`count_` 减了一。两个动作都被裹在了临界区里,`post` 和 `pop_ring` 动的是同一组成员,中断里 post、任务里 pop 的交错要是没有这层的保护,是迟早要写出脏数据来的。返回值用的是 `std::expected`,咱们拿到的要么是消息,要么是错误码,调用方用 `has_value()` 问一下就知道了,拿到手了再解引用。

## 计数信号量:有数据的信号,让等待者睡到单位来

环形缓冲自己是不会叫人的,字节压进去了,睡着的任务怎么知道呢?答案是队列身上内嵌的计数信号量,咱们看它的接线:

```cpp
    std::expected<Message, QueueError> receive_one() noexcept {
        semaphore.acquire();
        return pop_ring();
    }
    // when 0, means try receive
    static constexpr auto TRY_RECEIVE_TIME = clock::Milliseconds{0};
    std::expected<Message, QueueError> receive_one(clock::Milliseconds timeout) noexcept {
        if (semaphore.try_acquire(timeout) != SyncError::Ok) {
            return std::unexpected(QueueError::Timeout);
        }
        return pop_ring();
    }
```

计数信号量的道理,咱们用一句白话说就是数数:它有一个计数器,`acquire` 想做的事是把计数减掉一,减不动了(计数是零)就把当前任务挂进等待者的队列睡去,`release` 的动作是把计数加一,还顺手看了一眼有没有人在等,有就叫醒了队头的那个。它不关心数据长什么样,它只关心的是有还是没有,所以拿来当“有数据”的信号正合适。队列出生的时候,信号量的初值是零,咱们看它的成员声明:

```cpp
    SemaphoreBase<System> semaphore{0}; // zero units: the ring starts empty
```

注释把语义也带出来了:环形缓冲出生的时候是空的,单位是一个都没有的,所以谁都别想一上来就取到东西。`receive_one` 的两个重载各管一种姿势,咱们挨个看:无参的版本 `acquire` 之后没有回头的路,拿不到的话就一直睡,拿到了才 `pop_ring`,这就是信箱式收信的姿势。带超时的版本走 `try_acquire`,到了点还等不到的话,就交回了 `QueueError::Timeout`,而且 `TRY_RECEIVE_TIME` 定的是零毫秒,超时给零就是咱们问一嗓子的用法,有就拿了走,没有就立刻走人的,host 用例里的 try 走的全是它。

信号量的实现住在 `semaphore.hpp` 里,`release` 有一段注释值得您原样读一遍:

```cpp
    void release() noexcept {
        auto& system = System::self();
        ZerOS::irq::CriticalGuard g{system};
        ++count_; // the unit ALWAYS lands in the counter — woken sleepers
                  // race for it (loser re-sleeps, that is POSIX-semantics)
        if (auto head = waiters_.pop_head()) {
            // Some one is waiting!
            system.ready(head->task_);
        }
    }
```

计数落位的地方永远是计数器,叫醒只是顺带的动作,两个动作并不绑死:叫醒的和别的任务要是抢这个单位没抢赢,输的那个会重新睡回去,单位还留在计数器里的,谁也不吃亏的。咱们把这段机制记在心里,`post` 里数据落了位才 release 的次序,靠的就是计数器当的缓冲垫,单位的到达和数据的到达永远是一前一后,颠倒不了。

## 工作者线程:连队列带任务控制块,一行堆都没有

队列会装东西了,装进来的重活谁来干呢?`include/ZerOS/kernel/sync/user_worker.hpp` 交出了工作者,咱们给它做个人口普查:

```cpp
template <ZerOS::system::SystemContext System, std::size_t Depth = 8, std::size_t StackWords = 64>
struct WorkBase {
    sched::TCB* task_{};
    QueueBase<System, Work, Depth> queue_{};
    sched::TCBStorage box_{};
    // Local stack storage
    alignas(8) std::uint32_t stack_[StackWords]{};
};
```

咱们数了,成员一共是四个的,每一个都是编译期定容的静态形状:队列深度默认是 8,栈是 64 个字的内嵌数组,任务控制块装在 `TCBStorage` 的盒子里的。咱们数过了,这里找不到一个 `new` 的影子,工作者出生需要的全部内存,都是它自己身上的成员,您声明一个 `Worker`,内存就齐了,声明落在了哪个作用域,寿命就归了哪个作用域。零堆的纪律从内存池的一站延续到了这儿,内核自己带出来的东西,没有一件是去堆上讨过饭的。

工作者的出生走的是任务工厂的链条,咱们看 `setup`:

```cpp
    sched::TCB& setup(sched::TaskPriority_t prio, const char* name) noexcept {
        task_ = &task::named(name).prio(prio).stack(stack_).entry(&Loop, this).spawn_into(box_);
        return *task_; // the pen has spoken: from here on, defer may feed it
    }
```

`named` 加 `prio` 加 `stack` 加 `entry` 加 `spawn_into` 的串法,咱们在调度器的几站里就见过了,工作者的特别之处在 `entry(&Loop, this)`:它的任务体由 `Loop` 来当,`this` 作为参数递了进去,用户连一个循环都不用写的。注释的那句 “the pen has spoken” 是有点文人气的,意思倒是实的:链条走完,任务活起来了,从此 `defer` 才有了东西可喂。您要是跳过 `setup` 直接 `defer`,等着您的是一句检查:

```cpp
    // defer
    bool defer(Work::WorkFunc func, Work::WorkFuncArgs arg) noexcept {
        ZerOS::debug::Check(task_, "defer before setup");
        return queue_.post(Work{func, arg}) == QueueError::Ok;
    }
```

`defer` 干的活就是在队列上 `post` 一个 `Work` 了,`Work` 的形状是一个函数指针加一个 `void*`,开头的 `Check` 守着 setup 之前不许 defer 的次序。队列里流的是活儿本身,投递的全部内容也就这些。`Loop` 是工作者永不停歇的心脏,咱们看:

```cpp
    static void Loop(void* self) {
        auto& work_base_itself = *static_cast<WorkBase*>(self);
        for (;;) // Why different? we write the loop here
        {
            auto user_work = work_base_itself.queue_.receive_one();
            if (user_work) {
                Work::InvokeSelf(*user_work);
            }
        }
    };
```

无限循环里就两步了:无参的 `receive_one()`,没活儿的时候就睡着,单位来了就醒,把活儿取了出来执行,执行完了接着睡。注释里的那句 “Why different? we write the loop here” 是在跟谁对话的呢,文件头还有一段更长的注释,咱们引给您看:

```cpp
/**
 * @brief   What is A worker, tasks forces us to
 *          write something like for(;;) to prevent us not leek
 *          out to the outware, Works dont
 */
```

作者把区别写在了一处错拼的单词旁边(他拼的是 leek,词典义是韭葱的,作者想写的其实是 leak),意思咱们给您翻译过来:普通任务逼着用户自己写 `for(;;)` 的循环体,不然就漏出去了,工作者就不用了,循环长在了 `Loop` 里,用户交进来的只是一段一段的函数。这是工作者这个抽象真正的价值了,活儿是短命的,干活的身体是长生不老的。

## ISR 只投递,永不睡:底半推迟的全部意义

机制铺完了,咱们回到开篇的问题:中断里能干什么?demo 里的中断处理程序给出了答案,咱们看:

```cpp
// RXNE (and ORE, sharing RXNEIE): drop the byte into the queue. post()
// is ISR-safe by construction and never blocks; a full ring drops the
// byte (16 deep is plenty for a human typing)
extern "C" void USART1_IRQHandler() {
    if ((USART1->SR & USART_SR_RXNE) == 0) {
        return;
    }
    const char c = ZerOS::board::uart1_getc();
    if (c == 'd') { // bottom half: the interrupt drops a JOB, not a byte
        static_cast<void>(g_worker.defer(heavy_job, nullptr));
        return;
    }
    static_cast<void>(g_rx_queue.post(c));
}
```

中断干的活,咱们数一数就这么几件:查标志、读字节、往队列里 `post` 一把就撤。它出不了岔子,因为它压根不碰任何会睡的东西:`post` 里只有临界区、一次数组写入和一次 `release`,全是几条指令的活儿,注释里的那句 “ISR-safe by construction and never blocks” 说的是构造上的保证,谁也不用回头去补的。您要在这样的路上堆重活呢,中断的延迟就跟着堆了,整个系统里比它优先级高的、低的,全都得等它忙完的。

按 `d` 的时候,中断往队列里递的就换成了一份工作,咱们看 `heavy_job` 的样子:

```cpp
void heavy_job(void*) {
    ZerOS::board::print("worker: got the job, napping 200ms\r\n");
    ThisTask::sleep_for(Milliseconds{200}); // legal HERE — this is the whole point
    ZerOS::board::print("worker: job done\r\n");
}
```

它一睡就睡上了 200ms 的时间。注释里的那句 “legal HERE — this is the whole point” 咱们一字不改地给您:在这儿是合法的,而这正是全部的意义所在。中断里的睡是死罪,中断的处理程序要是睡了过去,同级和更低的中断全被摁住了,系统就瘫在了半空,任务里的睡倒是天经地义的,睡的只是它自己,别的任务照常跑,调度器手里有的是别人。`heavy_job` 不是在中断里睡的,它是被中断递进了队列、由 worker 在任务的上下文里执行的,所以它敢睡。中断的职责是只递条子,重活留给队列那头的工作者,上半段干的只是把活儿放进队列,真正的执行轮到了下半段,这件事在教科书里的名字是底半(bottom half)推迟,咱们拆完了源码再念这个名词,它就不玄了:被推迟的其实是上下文,活儿本身是一动没动的。

::: warning
`post` 满了会拒收,中断里的 `defer` 也是一样的,`static_cast<void>` 把返回值明着扔了:队列满了,活儿就丢了,也没有人再去重试它了。demo 里的深度是 8,活儿只有按了 d 才会来,是丢不了的,可您要把这套搬去产线,投递的失败就得有个去处,要么计数上报,要么换成带阻塞的投递,总之别学 demo 把失败咽进 void 里了。
:::

## host 考场:七个用例对证队列

行为不是靠嘴说的,`test/test_queue.cpp` 交了七个用例,host 上全跑了。咱们挑三个看,挑出来的恰好各守一条要害。

咱们数的头部是满拒绝。容量 4 的队列,塞四个就成了,塞的是第五个:

```cpp
TEST_CASE("full ring is refused with Full, nothing is overwritten", "[queue]") {
    World w;
    Q q;

    for (char c : {'1', '2', '3', '4'}) {
        REQUIRE(q.post(c) == QueueError::Ok);
    }
    CHECK(q.post('5') == QueueError::Full); // the fifth knocks, ring says no

    // the four that made it in are still the ORIGINAL four, in order:
    for (char c : {'1', '2', '3', '4'}) {
        auto got = q.receive_one(Milliseconds{0});
        REQUIRE(got.has_value());
        CHECK(*got == c);
    }
}
```

第五个被拒了,可是用例真正的断言在后面,咱们看进去的四个是原样出来的,次序是不乱的,内容也没有损了半点。拒收的语义要是写成了覆盖,这段就绿不了了。用例名字里的那句 “nothing is overwritten”,说的就是 demo 注释里 “a burst of bytes survives intact” 那句的考场版,两边验的是同一个承诺,各验了一遍。

第二条管的是环形接缝。环形缓冲最经典的死法就出在接缝上:写指针走到了数组的末尾,回卷的时候要是算错了,数据就从中间劈开了。咱们看用例怎么把它逼出来:

```cpp
TEST_CASE("head wraps around the ring without losing anything", "[queue]") {
    World w;
    Q q;

    for (char c : {'1', '2', '3', '4'}) {
        REQUIRE(q.post(c) == QueueError::Ok);
    }
    REQUIRE(*q.receive_one(Milliseconds{0}) == '1'); // free two slots
    REQUIRE(*q.receive_one(Milliseconds{0}) == '2');
    REQUIRE(q.post('5') == QueueError::Ok); // these land across the seam
    REQUIRE(q.post('6') == QueueError::Ok);

    for (char c : {'3', '4', '5', '6'}) { // and come out in order anyway
        REQUIRE(*q.receive_one(Milliseconds{0}) == c);
    }
    CHECK(q.receive_one(Milliseconds{0}).error() == QueueError::Timeout); // drained
}
```

容量是 4 的队列,咱们把它装满,取走了两个,再放上两个:5 和 6 就落在了数组物理上的开头,跨过了接缝。出来的次序必须是 3、4、5、6,逻辑上的 FIFO 和物理上的绕圈得是同一件事,最后再验一把队列是空的。`head_` 和 `count_` 的组合加上按位与的回卷,在这里被从头到尾走了一遍。

咱们看的第三条,管的是定时收和世界的赛跑。收的时候带 5ms 的超时,任务就睡下去等着了,而测试的世界在它睡着的当口往队列里放了一个 `!`:

```cpp
TEST_CASE("timed receive wakes up when the world delivers while parked", "[queue]") {
    World w;
    Q q;

    FakeSys::during_sleep = [&q] { q.post('!'); }; // the world delivers on time
    auto got = q.receive_one(Milliseconds{5});
    REQUIRE(got.has_value());
    CHECK(*got == '!');
}
```

这个用例验的是次序,它跟运气无关:任务睡着的时候单位到了,`release` 把它叫醒了,醒来的人去 `pop_ring`,取到的必须是刚到的 `!`,而不是超时的空。咱们在信号量一节里记下的“计数永远落进计数器”在这里兑现了,`during_sleep` 这个钩子是 `test_sync` 那批测试一路传下来的 mock 教义,世界在任务睡觉的瞬间运转,竞态的窗口被正对着照了一遍。

剩下的四个用例,空队列的 try 得干净地超时而不睡死、FIFO 的次序、突发的字节一只不损、无限等收到才醒,咱们就不逐个念了,您跑一遍就全见着了。

## 验收

咱们把本篇的可查证事实归拢一下,每一件我是怎么验的,我都照实交代。

我把仓库导到了 /tmp 的副本里(third_party 下的 ZerOS 是个 gitfile 工作树,我拿 `git archive 3570f27` 把那个 commit 的树原样导了出去,还把真实的 STM32CubeF1 拷进了副本的 third_party,它在仓库里只是一段引用的文字,归档里是空的),host 侧按 README 的路子配了测试构建:

```shell
cmake -B build-host -DZEROS_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

我本机的 GCC 是 16.2.1,ctest 的成绩单是真实的:

```text
100% tests passed out of 11

Total Test time (real) =   0.22 sec
```

十一个测试全部通过了,队列自己的成绩单,咱们看一眼:

```text
All tests passed (54 assertions in 7 test cases)
```

七个用例的阵仗,54 条断言的数目,跟咱们上面一个一个看过的对得上座,这一行就是 commit 自己写的验收,我一个断言没动过。

板子的那一侧,我用交叉的工具链(arm-none-eabi-g++ 16.2.0)把固件编了出来,体积是真实的:

```text
   FLASH:        5152 B        64 KB      7.86%
     RAM:        2848 B        20 KB     13.91%
   text	   data	    bss	    dec	    hex
   4960	     192	   2656	   7808	   1e80
```

然后是 Renode 的部分。经验我照实再说一遍:我本机的 Renode 1.17.0 认不出 `bluepill.repl` 里的 `I2C.STM32F4_I2C` 类型,我把 i2c1、i2c2 两个节点连着它们的中断接线整块注释掉了,仿真就起来了,demo 并不经过 i2c 的路径,行为也就不受影响了。

还有一件事我得交代,它是这次验收里我真正折腾过的一个地方:demo 要按 `d` 才递活儿,可 Renode 的串口分析器是个只读的窗,字符是打不进去的。我把 `sysbus.usart1` 切进了监控器的上下文,翻了一遍它的命令清单,找到了 `WriteChar`(它是往接收侧喂字节的命令),于是在仿真开跑了一秒之后,我从监控器里喂了一个 `0x64` 进去,它也就是字符 d 的编码。串口的日志里,出声的除了 A、B 两个从早几站的 demo 一路留下来的打字母任务,还有 M、L、H 几个搭戏的角色,M 是每 100ms 喊一嗓子的监视任务,L 和 H 在演锁的戏,行数太多了,我把它们的行从贴出的片段里抽掉了,原始的日志里它们一行都没少:

```text
[INFO] usart1: [host: 0.16s (+1.57ms)|virt: 0s (+0s)] ZerOS multi-task demo @ Blue Pill
[INFO] usart1: [host: 0.17s (+13.91ms)|virt: 74.21µs (+74.21µs)] A
[INFO] usart1: [host:  0.17s (+0.23ms)|virt:  76.53µs (+2.32µs)] B
[INFO] usart1: [host:    0.81s (+0.1s)|virt:      0.7s (+100ms)] B
[INFO] usart1: [host:    1.11s (+0.1s)|virt:        1s (+100ms)] A
[INFO] usart1: [host: 1.39s (+78.32ms)|virt:    1.27s (+74.6ms)] worker: got the job, napping 200ms
[INFO] usart1: [host: 1.51s (+99.88ms)|virt:      1.4s (+100ms)] B
[INFO] usart1: [host: 1.58s (+73.88ms)|virt:      1.47s (+74ms)] worker: job done
[INFO] usart1: [host:    2.11s (+0.1s)|virt:         2s (+0.1s)] A
```

您看 virt 那一列,咱们要的验收事实全在里面了:`d` 是我在虚拟时间 1.2 秒左右喂进去的,1.27 秒 worker 报了 “got the job, napping 200ms”,1.47 秒它报了 “job done”,中间隔的就是实打实的 200ms。最值钱的是中间那一行:worker 睡觉的当口,1.4 秒的 B 照常打了一个字母。worker 的 200ms 一秒都没耽误别人,它睡的是自己的任务上下文,这就是实证了,中断要是敢在自己肚子里睡上 200ms 的话,咱们看到的就是系统停摆 200ms 了。

## 下一篇

队列会装了,工作者也会干了,可 `heavy_job` 是立刻执行的活儿。要是咱们想要的只是“过 300ms 之后提醒我一声”呢?为这么一声提醒养一个任务、配一整块栈,就太奢侈了。咱们下一篇看的是《`等三百毫秒,不养一个任务:软件定时器与 fn+void* 硬通货`》,拆的 commit 是 `766c462`,软件定时器怎么用回调把等待从任务的身上解下来,还有 `base::Callback`(fn 加 `void*`)为什么是裸机世界的硬通货。咱们那儿见。
