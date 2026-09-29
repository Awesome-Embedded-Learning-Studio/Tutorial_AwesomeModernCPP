---
title: "线程池：worker 循环与优雅关闭"
description: "把阻塞队列、packaged_task 与 stop_token 组装成生产级线程池：worker 循环与 cv_any 三参 wait 的正源，外加一次真实的 terminate 复现"
chapter: 5
order: 3
tags:
  - host
  - cpp-modern
  - advanced
  - 异步编程
  - 并发
  - mutex
difficulty: advanced
platform: host
reading_time_minutes: 26
prerequisites:
  - "promise 与 packaged_task"
  - "线程所有权与 jthread/stop_token"
  - "condition_variable 与阻塞队列"
related:
  - "task<T> 协程"
  - "Actor 模式与邮箱"
  - "SPSC 与 MPSC 队列"
cpp_standard: [11, 17, 20, 23]
---

# 线程池：worker 循环与优雅关闭

咱们在 [packaged_task 那篇](02-promise-and-packaged-task) 的末尾搭过一个单 worker 的 SimpleTaskQueue，一个线程、一条队列、一个返回 future 的 submit，就是它的全部。它倒是能跑，可是提交四个任务也只能排着队一个接一个地跑，跟调用方自己写个循环没有什么本质上的区别。今天咱们把它扩成真的线程池：一组早就建好的 worker，守的是同一个任务队列，任务谁抢到了就归谁执行。

为什么要复用线程？因为创建一个线程要付的成本是一整套的：系统调用的进出、栈的分配、调度器的登记，销毁时又把同一套倒着走了一遍。[OS 线程那一篇](../ch00-concurrency-fundamentals/03-os-threads-and-cost) 用 perf stat 看过开销的明细，数字咱们这里不重复，值得留下的形状是短命线程的创建销毁摊不平，复用才是对路的做法。池子还把并发度收进了咱们手里，同时有几个 worker 在跑由构造参数说了算，而不是由任务到得勤不勤决定。

本篇是两样的正源：worker 循环，还有 cv_any 集成 stop_token 的等待，后面咱们写 [协程的取消](../ch06-async-io-coroutine/03-coroutine-cancellation)、[Actor 的邮箱](../ch07-actor-channel/01-actor-model) 时都要回本篇取这两样料。

咱们的路线只有一条主线。从 C++17 的裸版起步，手写的锁、手写的唤醒、手写的停止标志，把 worker 循环和关闭的时序走通。然后咱们换到 C++20 的轨道上，把三样新件一样一样地换进去：jthread、stop_token，加上 cv_any 的三参 wait，每样顶掉哪一摊老活、又带进来什么新麻烦，等到换轨的时候再细说。笔者在验证析构时序的时候真的翻了一次车，现场咱们保留在后文，值得您亲眼看看它是怎么挂的。

池子不是万能的。任务的总量要是只有几十件，建池拆池的成本可能比省下的还多，直接 std::async 或者裸 jthread 就够了。碰上要等外部 I/O 等到天荒地老的任务，池子的线程数也得跟着膨胀，咱们还不如用后文的协程。池子吃的是量大而短促、彼此也独立的计算型任务，这是它的主场。

## 池子由哪几样零件组成

把池子的零件摆开数一数，其实没几样：装任务的队列，干活的 worker，互斥用的锁，等待用的 cv，报停用的 bool 标志，全都在这儿了。任务队列咱们直接用 [阻塞队列那篇](../ch02-mutex-condition-sync/04-condition-variable-and-bounded-queue) 的那一套，只是换成了无界简化版：容量上限被去掉了，关闭的活由池自己的析构接管。notify 的分工还是老一套：开张的时候一个新任务只需叫醒一个 worker，notify_one 也就够了，轮到 notify_all 登场的是关门，理由咱们在队列篇讲过。

咱们也要交代无界的代价：submit 是永不阻塞的，任务堆多少全看生产者的心情，生产快过消费的时候，跟着遭殃的就是内存了。有界化就是把 [阻塞队列那篇](../ch02-mutex-condition-sync/04-condition-variable-and-bounded-queue) 的 BoundedQueue 原样换进来，队满的时候 submit 就阻塞了，背压就有了。本篇为了讲清 worker 的循环，用的都是无界版。

队列元素的类型为什么是 `function<void()>`？因为队列的职责只有搬运，不该关心任务的返回值。返回值的通道由 packaged_task 与 future 另走一路，队列里躺着的只需是一件能被调用的东西。咱们把这个手法叫类型擦除：千姿百态的任务进了队列，都是同一副 void() 的面孔，worker 取了出来只管调用，别的一概不问。

构造函数也用不了几行，咱们把建 worker 的循环原样贴出来：

```cpp
explicit ThreadPool(std::size_t num_threads)
{
    for (std::size_t i = 0; i < num_threads; ++i) {
        workers_.emplace_back([this] { worker_loop(); });
    }
}
```

每个 worker 跑的都是同一个 worker_loop，线程数就是循环的份数。数量怎么定没有万能的答案：CPU 密集的活大致照着 `hardware_concurrency()` 的返回值给，您也可以为 I/O 密集的活放宽一些，毕竟那样的线程常常挂在等待上。您还得提防那个查询返回 0 的情况，[jthread 那篇](../ch01-thread-lifecycle-raii/03-thread-ownership-and-jthread) 的 parallel_for_each 里咱们兜过底，写法这里就不再重复了。构造要是还想加一道开工的门闩，等全体 worker 都就位了再统一放行，[工具箱那篇](../ch02-mutex-condition-sync/05-sync-primitives-toolkit) 的 latch 就是干这个的。

成员这边咱们一眼就能数完。workers_ 存的是线程，tasks_ 装的是任务，互斥的活儿交给 mutex_，唤醒的活儿交给 cv_，stop_ 是停止的标志。workers_ 在成员表里排的位置是最前面，它的用意要到析构崩溃的现场才揭晓，您到时候回来一看就明白。任务在进队列以前是要被擦成同一个类型的，怎么擦的、返回值怎么拿的，咱们马上说。

## worker 循环的正源：C++17 裸版

全卷后面再讲 worker 循环的地方，都会指回本篇的 canonical 版本。代码咱们只写一份，后面的演进一律在正源代码的基础上做减法，咱们不再整段重抄：

```cpp
void worker_loop()
{
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
            if (stop_ && tasks_.empty()) {
                return;   // drain：停了，而且存货干完了，才退场
            }
            task = std::move(tasks_.front());
            tasks_.pop();
        }                   // 锁内取任务……
        task();             // ……锁外执行
    }
}
```

咱们逐行走读。wait 用的是谓词版，这半句的纪律在 [阻塞队列那篇](../ch02-mutex-condition-sync/04-condition-variable-and-bounded-queue) 里立过：虚假唤醒和丢失唤醒都由持锁求值的谓词挡在外面，两条交错的路径那边也推演过了。谓词的内容您也眼熟，`stop_ || !tasks_.empty()` 跟队列篇的写法是同一副骨架，只是那边的 closed_ 归队列，这边的 stop_ 归池子。

醒来之后的判断就是 drain 语义的全部。stop 是真的、队列也是空的，才走 return 的分支。停止请求来了也不算完，worker 会把队列里的存货接着取出来执行，最后一个干完了才走。丢弃已提交的任务谈不上优雅，那可就是事故了。咱们反过来看：只查 stop 不查空的写法，会让关门瞬间还滞留在队列里的任务再也没人取，调用方手里的 future 永远等不到结果。

stop_ 要不要换成 atomic？咱们不换。它的读写都发生在锁内：worker 在持锁的窗口里求值，析构在持锁的窗口里置位，锁已经给了它足够的保护。就算真换成了 atomic<bool>，那把锁也还是省不掉的，因为谓词读的是队列，而队列的读取本来就离不开锁。所以 bool 配 mutex 就是正解，再叠一层 atomic 就画蛇添足了。

花括号的位置咱们再看一眼，这是 worker 循环里排位最靠前的设计决策：锁的里面取任务，任务在锁的外面执行，所以任务跑多久都牵连不到 mutex_ 的身上。咱们要是拿着锁执行任务，其他 worker 和所有 submit 调用方就全堵在 mutex_ 上了，整台池子退化成串行的单线程，那多开的线程图什么？锁护的只是队列的一取一放，任务的身体是不归它管的。

咱们把 worker 的一辈子数成三个状态：睡在 cv 上等活的时光，持锁取件的瞬间，解锁干活的时段，循环往复地转下去。任务做完了回到循环顶，队列里剩了存货就直接接着取，notify 都省得等了。排查池子类 bug 的时候，咱们的视角也顺着状态走。任务不执行的毛病，咱们多半能在 notify 和谓词身上找到源头。池子卡住关不掉的毛病，基本是关闭时序出了问题，后文复现的那次挂掉就是现场的标本。

```text
  notify_one 或队列里的存货，都会唤醒睡在 cv 上的 worker：

      [睡在 cv 上] --> [持锁取件] --> [锁外干活]
           ^                               |
           +--- 干完回循环顶，接着睡 <------+

  退场只有一条路：醒来时 stop 为真，而且队列已经排干。
```

咱们把一件任务从 submit 到归宿的完整旅程排成五步：

1. submit 持锁入队，notify_one 叫醒一个睡着的 worker。
2. worker 在持锁的窗口里取任务出队，锁随即放下。
3. worker 在锁外调用 task()，里面是 packaged_task 的 operator()。
4. 返回值进了共享状态，异常也会被它接住。
5. 调用方的 future.get() 拿到结果，或者收到重抛的异常。

每一步的同步责任咱们都交代过了：队列的互斥、future 的握手、唤醒的谓词，一样都守住了一段。卷首的 `先同步，再任务` 说的就是这个次序：同步的功夫前面立稳了，咱们才好把任务的这层搭上去。

## submit：在正源之上只写增量

submit 的类型擦除仪式，用的是咱们在 [packaged_task 那篇](02-promise-and-packaged-task) 立过的那一套：返回类型靠 `invoke_result_t` 推出来的，可调用对象装进 `packaged_task<R()>` 的壳，shared_ptr 做的是中转，lambda 捕的是句柄，入队的类型是 `function<void()>`。等池子有了多个 worker，真正要动的地方只有两处：

```cpp
template <typename F, typename... Args>
auto submit(F&& f, Args&&... args)
    -> std::future<std::invoke_result_t<F, Args...>>
{
    using R = std::invoke_result_t<F, Args...>;
    auto task = std::make_shared<std::packaged_task<R()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...));
    std::future<R> fut = task->get_future();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stop_) {   // 增量①：关门期间拒收新任务
            throw std::runtime_error("submit on stopped ThreadPool");
        }
        tasks_.push([task] { (*task)(); });
    }
    cv_.notify_one();  // 增量②：多 worker 下，一个新任务叫醒一个人
    return fut;
}
```

拒收的检查必须发生在持锁的窗口里，跟 stop_ 的置位隔着一道 mutex。submit 看到 stop 为假、放心入队的瞬间，析构恰好把 stop 置位的竞态，被同一把锁串行化掉了。咱们靠的正是这层串行化：少了它，任务可能被推进一条已经没有 worker 的队列里，future 就永远悬在那里了。

轮到解释 shared_ptr 那一层为什么省不掉了，咱们用一句话把因果链接上：队列存的是 `function<void()>`，它要求装进去的东西可拷贝，而 packaged_task 偏偏是 move-only 的。shared_ptr 干的事，是把拷贝本体的动作偷换成了拷贝句柄：lambda 捕的是句柄，task 的本体只有一份。这套仪式在 C++11 年代以后的各种正经实现里反复出现过，您以后在开源代码里再见到它，就该像见到了老朋友。

submit 的参数是怎么进任务的？std::bind 把可调用对象连同参数一起打包成零参的调用体，走的是值的语义。咱们想传引用的时候，包一层的活儿交给 std::ref，不然 bind 存下的是拷贝。move-only 的参数也有讲究，稳妥的路数是让 lambda 直接 move 捕获。这些细节咱们在 [packaged_task 那篇](02-promise-and-packaged-task) 全都过过，这里只提一句：任务入队走的是拷贝、出队走的是 move，中途经手的只有队列。三种传参的姿势咱们摆在一起看：

```cpp
pool.submit(greet, std::string("hello"));          // 拷贝一份进任务，最稳
pool.submit(greet, std::ref(name));                // 传引用：name 得活得比任务久
pool.submit([big = std::move(buf)] { use(big); }); // move 进 lambda，零拷贝
```

### submit 的两个常用简写

咱们日常用池子，九成的 submit 是两个简写。返回 void 的任务，future<void> 咱们照样拿得到，get() 干的只是把异常接住，咱们通常把它扔掉。成员函数的绑定传的是成员指针加对象指针，可读性更好的路数是拿 lambda 包：

```cpp
pool.submit([] { std::puts("fire and forget"); });   // void 任务照常提交
pool.submit(&Service::handle, &svc, request_id);     // 成员函数加对象指针
pool.submit([&] { svc.handle(request_id); });        // lambda 的等价写法：引用捕获
```

### 一个经典的死法：池中池

worker 的任务里要是再调 submit 并且原地 get，咱们就给自己埋了雷。咱们设想四个 worker 手里全有任务，每个任务都在等一个还没被调度的新任务：新任务躺在队列的深处，可全体的 worker 都腾不出手。死锁的形状跟 [死锁那篇](../ch02-mutex-condition-sync/03-deadlock-and-gdb) 讲的循环等待是一副骨架，只不过资源换成了线程本身。

```cpp
// 死法示范：千万别在 worker 里这样写
pool.submit([&pool] {
    auto fut = pool.submit([] { return 1; });
    fut.get();   // 全体 worker 都堵在这儿，新任务永远没人取
});
```

咱们预防的路数也直白：任务里只 submit 不 get，等待的活儿交给外面的调用方。真要等的时候咱们就给 get 加期限。生产级的池子会提供 inline 执行的开关，咱们让提交者顺手把活干了，这已经是 [exercises 的线程池 Lab](../exercises/03-thread-pool) 的地盘了。

### C++23 旁注：move_only_function 拆掉两层壳

这套仪式写起来是别扭的，委员会心里也是有数的。C++23 给了 `std::move_only_function`，头文件用的是 `<functional>`，特性测试宏咱们查 `__cpp_lib_move_only_function`（202110L）。cppreference 的页面示例把新旧并排摆在一起，效果咱们一眼就能看懂：

```cpp
std::packaged_task<int()> task([] { return 42; });
std::future<int> fut = task.get_future();

// std::function<void()> f = [t = std::move(task)]() mutable { t(); };
// ↑ Error：function 要求目标可拷贝，move 捕获的 packaged_task 装不进去

std::move_only_function<void()> f = [t = std::move(task)]() mutable { t(); };  // OK
f();   // future 这边照常 get
```

同一个 lambda 换了身容器就直接编过了，make_shared 和解引用的两层壳都可以拆掉了。有两处咱们按规范说准：提案的编号是 P0288R9，落地的是 C++23，咱们别把它写成 C++20。还有调用空对象的差异也值得咱们记一笔：空着的 std::function 被调用会抛 `bad_function_call`，而 move_only_function 空着调是未定义行为。LEWG 在 Kona 的评审会上以 8/2/1/0/0 表决移除了异常路径，换来的是更瘦的壳。LEWG 是 Library Evolution Working Group 的缩写，也就是标准委员会里管库演进的工作组。委员会的会议习惯拿举办地称呼，Kona 是夏威夷的一处海边小城。咱们把需求往前追，最早能追到 2014 年 LEWG 的一条记录。线程池界把这口气忍了九年才等来正式的替代品，您在旧代码里还会长期见到那套 shared_ptr 仪式，读懂它依然是咱们的基本功。

## 换轨：jthread 与池共享的停止状态

等裸版跑通了，C++20 的三件新家伙就等着接班了。jthread 管的是自动收尾，stop_token 管的是停止信号，第三件是 condition_variable_any 的三参 wait，管的是唤醒。前两件的规范行为咱们在 [jthread 那篇](../ch01-thread-lifecycle-raii/03-thread-ownership-and-jthread) 取过正源，第三件的正源就在本篇的下文。本篇咱们只留一句话用的：jthread 的析构等价于 request_stop 加 join，其余的细节那边全有。

换轨的路不像想的那么直。最省事的写法是把 worker_loop 直接交给 jthread 的自动注入，咱们看第一轮的尝试：

```cpp
workers_.emplace_back([this](std::stop_token st) {
    worker_loop(st);   // token 由 jthread 的构造自动注入
});
```

第一轮的代码咱们编过了，可惜一到停止它就露馅了。每个 jthread 的内部都有一份私有的 stop_source，自动注入的 token 连接的是各自私有的停止状态。咱们想让全体一起停的时候，workers_[i].request_stop() 是做不到的，它连着的只是第 i 个 worker 的私有旗子。组控制的正解形状咱们在 [jthread 那篇](../ch01-thread-lifecycle-raii/03-thread-ownership-and-jthread) 见过：在外部造一个共享的 stop_source，把它的 token 分发给全体成员。池子的第二轮迭代就是这个套路：

```cpp
workers_.emplace_back(
    [this, st = stop_source_.get_token()] { worker_loop(st); });
```

lambda 显式捕获了池持有的 token，jthread 的自动注入就被绕开了：lambda 不收 token 的参数，这一点是咱们刻意安排的。于是池子里同时活着的是两套互不相识的停止状态：

| 停止状态的来源 | 谁持有它 | request_stop 停得到谁 |
|---|---|---|
| jthread 自带的私有 source | 每个 jthread 的内部 | 只有它自己 |
| 池持有的 stop_source_ | ThreadPool 的成员 | 全体 worker（token 已分发） |

析构的时候每个 jthread 照规范对自己的私有 source 举一次旗，可惜 worker 的循环并不看它，等于白举了。真正叫醒 worker 的，是咱们手里这把 stop_source_，咱们一喊全体都听得见。两套 source 的协调成本摊在这儿，这就是新特性带进来的新麻烦。

咱们把三件套的接口也盘一下：stop_source 是开关的持有端，request_stop 的发起权在它手里。stop_token 是只读的票根，探测的活儿归 worker_loop。stop_callback 是挂上去的铃铛。三样的名字各叫各的，公用的却是同一份停止状态。

## cv_any 三参 wait：集成 stop_token 的正源

worker_loop(std::stop_token st) 的等待，咱们该怎么写？等待的 cv_ 得换成 condition_variable_any。这里有个规范事实咱们直接摆出来。接受 stop_token 的三参 wait 重载，只存在于 condition_variable_any 的身上。std::condition_variable 的重载列表里没有它，咱们拿 cv 去调三参版 wait，在编译期就被拦下了，笔者在本机试过（GCC 16.2.1），报错的信息是 no matching function，仅有的两个候选一个收两参、一个只收一参，咱们的三个参数谁也接不住。咱们在规范文本里找不到切分的理由，也就不猜了。cv_any 是泛型的版本，能搭配的锁类型比 cv 多得多，代价则是可能更重的实现。队列篇收尾的时候咱们预告过它，本篇就是那次预告的兑现。

真正的主角是三参 wait 的语义。cppreference 给了等价代码，咱们逐行读：

```cpp
// cv_any_.wait(lock, stoken, pred) 的规范等价代码：
while (!stoken.stop_requested()) {
    if (pred()) return true;
    wait(lock);          // 只带锁的两参版，睡在 cv 上
}
return pred();           // stop 之后醒来，最后再求一次谓词
```

返回值的语义是最容易读错的地方，规范的原话是它指示 pred() 的最新求值结果，与是否发生了 stop 请求无关。咱们把谓词 `[this] { return !tasks_.empty(); }` 代进去，这个说法就落了地。返回 true 的分支好懂：队列是非空的，取了任务接着执行。返回 false 的分支就有讲究了：说明循环是从 while 的条件退出来的，stop 已经请求了，而且最后一次 pred() 求值也是假的，也就是队列空了。两种结局咱们都推完了，wait 拿一个布尔值就说清了。

还有一条机制值得咱们记下：三参 wait 在等待期间会把自己注册到 token 关联的停止状态上，咱们一发起 stop 请求，等在里面的线程就醒了过来。request_stop() 本身就带着敲门的效果，不需要额外的 notify。这个性质咱们在析构的实验里还要用到。

这个注册机制有个看得见的对应物，就是三件套里的第三个成员 std::stop_callback。咱们构造它的时候指定一个 stop 状态，等 stop 请求来了，它注册的回调就会被调用。cv_any 三参 wait 的内部，干的就是咱们手写下面这行的活：

```cpp
// 手写版：cv + stop_token 的桥接（示意）
{
    std::stop_callback cb(st, [this] { cv_.notify_all(); });
    cv_.wait(lock, [this] { return done_ || !tasks_.empty(); });
}   // cb 析构时自动摘钩
```

request_stop 走的也是同一座桥，醒人的活它包了，咱们再显式 notify_all 才算双保险。stop_callback 更常见的舞台是清理，取消的时候关掉打开的文件、回滚写了一半的状态，都是它的活。[jthread 那篇](../ch01-thread-lifecycle-raii/03-thread-ownership-and-jthread) 的三种用法里有一种就是它。

有了这层语义，旧文的写法咱们原样搬来当教具，看看差在了哪里：

```cpp
if (!cv_any_.wait(lock, st, [this] { return !tasks_.empty(); })) {
    if (tasks_.empty()) return;   // 恒真：wait 返回 false 时 pred 必为假，
}                                  //       队列此刻必然是空的
if (tasks_.empty()) continue;      // 不可达：返回 true 意味着 pred 为真
task = std::move(tasks_.front());
tasks_.pop();
```

两层检查为什么是冗余的，咱们拿刚才读过的等价代码一审就明白。wait 返回 false 的前提是 stop_requested 且 pred() 为假，而咱们的 pred 就是 !empty()，所以 false 一到手，队列就必然是空的，头一个 if 是恒真的。第二个 if 守的是另一条路径：wait 返回了 true、队列却空了。可 true 的含义是队列非空，它也就永远走不进去了。读懂了数据流以后，这两行防御性的检查就可以直接删掉了，正源就剩下了：

```cpp
void worker_loop(std::stop_token st)
{
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            // 返回 false：stop 已请求，且队列已空（drain 完成）
            // 返回 true ：队列非空，取任务执行
            if (!cv_any_.wait(lock, st, [this] { return !tasks_.empty(); })) {
                return;
            }
            task = std::move(tasks_.front());
            tasks_.pop();
        }
        task();
    }
}
```

咱们跟 C++17 裸版比一比形状。谓词里的 stop_ 没了，它搬进了 stop_token。醒来后的判断也从 `stop_ && tasks_.empty()` 缩成了一个对返回值的取反。stop 标志的置位、唤醒、drain 判断，三件事全被一个库调用接管了。

三代 worker 循环的对照，咱们也收进一张表里：

| 版本 | 谓词查什么 | 醒后怎么判断 | 停止状态的载体 |
|---|---|---|---|
| C++17 裸版 | stop 或队列非空 | 两个条件都成立才退 | 加锁保护的 bool |
| 旧文 C++20 版 | 队列非空 | wait 返回值外面套两层 if | stop_token |
| 本篇正源 C++20 | 队列非空 | 一个对返回值的取反 | stop_token |

细心的您可能发现了：队列篇的 BoundedQueue 关门靠的是 close 把 closed_ 的旗子竖起来，池子这头靠的是 stop_token 加 drain。两边的目标倒是一致的，落点各有各的安排：队列把关闭做成了容器自己的事，池子把关闭做成了持有者的事。这个分工咱们记下来，后面 Actor 的邮箱关门时，走的还是同一个思路。

化简版的行为咱们在本机验证过（GCC 16.2.1、x86-64 Linux）：4 个 worker、100 个任务的池子反复建拆 300 轮，每轮都核对 100 个任务全部执行、全部线程干净地退出。两条边路咱们也没放过：停止后的拒收、停止前最后一次提交的执行。真实的输出咱们原样贴上：

```text
drain+clean-exit: PASS (300 rounds)
exception-via-future: PASS
submit-after-stop rejected: PASS, drained task ran: PASS
```

TSan 的走查咱们也补了（`-O1 -g -fsanitize=thread`），报告也是干净的。

<!-- 实验回填：TSan 长压与千次建拆的完整输出原文、-O0/-O2 两档对照，回填时附编译命令 -->

## 关闭的时序：三步不能换

裸版的析构函数咱们原样贴出来，三个动作的编号标在注释里：

```cpp
~ThreadPool()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;                // ① 持锁置位
    }
    cv_.notify_all();                // ② 挪到解锁的后面
    for (auto& w : workers_) {       // ③ join 的位置在最末尾
        w.join();
    }
}
```

三个动作各有各的道理，咱们一个一个过。stop_ 的置位发生在持锁的窗口里，这是共享状态修改的纪律，也顺手堵死了与 submit 的竞态：两边隔着的正是同一把 mutex。notify_all 挪到了解锁的后面，是队列篇讲过的常见排法。标准当然允许持锁 notify，但醒了的人睁眼就得抢同一把锁，抢不到就又睡了回去，白醒了一趟。轮到 join 了，它的位置在最末尾。

咱们把顺序换一换，死锁立刻就到了。咱们动嘴推演就够了：析构的线程守在 join 上，等的是 worker 退出。worker 却睡死在 cv 上了，等的是一次不会来的 notify。两边等的都是对方，这场对峙是没有出口的。

咱们再换到 worker 的视角，把析构的全程走一遍：

1. worker 睡在 cv 上，析构方持锁置位 stop_，解锁，notify_all。
2. worker 醒来，重新拿到锁，谓词求值为真：stop 或队列非空。
3. 队列有存货就接着取，锁外执行，做完回循环顶。
4. 队列空了而且 stop 为真，worker 从 while 返回，线程结束。
5. join 在析构方那边逐个收回线程，析构函数返回，全剧终。

您对着五步把两个视角叠起来看，三步时序的每一步都有了着落。

这套时序还带出了一个保证，咱们把推理摆出来：worker 是排干了队列才退场的，join 守着的是全体退场，所以等析构函数返回了，已提交的任务就都执行完了。半途而废的状态，在这套时序下咱们摸不到。

咱们再答一个小问题：析构的执行线程是谁都不要紧，只要它不是池子自己的 worker。在提交任务的线程上跑也好，在无关的第三条线程上跑也好，时序的保证都不依赖调用方。真要在任务里把池子就地析构，咱们马上就能看到下场：析构要 join 的是全体 worker，名单里偏偏就有正在跑析构的线程本身，咱们等于在让它自己 join 自己。glibc 的 pthread_join 对这个形状返回 EDEADLK，libstdc++ 又把它翻成了 system_error 抛出来，异常穿出了线程顶层，等来的就是 terminate，咱们在本机复现过这个死法。唯一的正当要求还是析构期间别再有人 submit，而这一点已经被锁内的拒收检查挡住了。

### 自动 join 挡不住的一次真实崩溃

换到 C++20 的轨道，析构能瘦成什么样？笔者把第一版写成了这样，看着是相当体面的：

```cpp
~ThreadPool()
{
    stop_source_.request_stop();   // 规范：会唤醒三参 wait 的等待者
    cv_any_.notify_all();
}   // workers_ 是 jthread，析构不是自动收尾吗？
```

笔者的算盘是：request_stop 负责唤醒、jthread 的析构负责 join，两行就把收尾办了。咱们拿 drain 测试去跑它：每轮建一个 4 worker 的池子，灌进去的是 100 个任务，咱们一个 get 都不做，直接把池子拆掉了。第一轮就挂了，报错的原文咱们贴在下面：

```text
terminate called after throwing an instance of 'std::future_error'
  what():  std::future_error: No associated state
```

有时的死相是段错误（退出码 139），有时是上面这个异常的穿透（退出码 134）。咱们找根因，最后落在了成员的析构顺序上。request_stop 和 notify_all 都是无辜的：workers_ 声明在成员表的最前面，析构却轮在了最后。C++ 的成员按声明的逆序销毁，所以等到 jthread 的 join 真正发生时，mutex_、cv_any_、tasks_ 早就被拆干净了。而函数体那两行跑完的一瞬间，worker 可能还活得好好的，摸到的队列和锁却是已经死了的对象，未定义行为以 future_error 的面目冒了出来。带 notify_all 的版本和去掉 notify_all 的版本挂得一样快，两个实验互相印证了凶手另有其人。

咱们只用一行修法，把 join 收回到析构函数的体内：

```cpp
~ThreadPool()
{
    stop_source_.request_stop();
    cv_any_.notify_all();   // 廉价的双保险，见下文
    workers_.clear();       // join 必须发生在其余成员死亡之前
}
```

workers_.clear() 做的事就是逐个析构 jthread，request_stop（私有旗是白举的）加 join，全都发生在其余成员尚且活着的窗口里。咱们看修复后的版本：同样的 drain 测试 300 轮全部通过，TSan 的报告也是干净的。

notify_all 的悬案也能结了。request_stop 一个人能不能唤醒全体？规范的回答是能，道理就是刚才讲过的注册机制。实验咱们也做了，把修复版的 notify_all 再去掉，300 轮的常规测试、三轮每轮一千回的压测，外加 TSan 的复查，结果全部是干净的退出。notify_all 确实是冗余的，咱们还是把它留在了代码里：多喊的那一嗓子的代价约等于零，而且它把全员必须醒的意图写在了明处，以后要是有人把 wait 改回两参的版本，它就是保命的那一行。

三代析构的对照，咱们收进一张表里：

| 版本 | 停止怎么发 | 谁负责 join | 踩过的雷 |
|---|---|---|---|
| C++17 裸版 | 持锁置 stop_，解锁后 notify_all | 析构函数体的 for 循环 | 顺序换位就是死锁 |
| C++20 第一版 | request_stop | 指望成员析构顺带 join | 成员逆序销毁，join 轮在了最后 |
| C++20 正源 | request_stop 加 notify_all | workers_.clear() 在函数体内 | 无（300 轮加 TSan 验证过） |

配套代码里咱们还留了一个 stop() 成员，它干的事就是 request_stop。有些时候就是要提前关门的：出了错想赶紧收工的场合，测试里要复现关闭的竞态。它跟析构走的是同一条路，关门的时序咱们不另写一份。

### 长任务拖住的析构

优雅关闭的代价咱们还得交代一笔。worker 正在跑一个长任务的时候，停止的信号对它是没有打断能力的，它把当前的任务跑完、回到循环顶，才看得见停止的旗子，所以析构会阻塞到最长的那个当前任务结束。咱们把它算作协作式取消的代价，而不是 bug，[jthread 那篇](../ch01-thread-lifecycle-raii/03-thread-ownership-and-jthread) 讲取消语义的时候立过这个口径。想躲开它的人会想到 detach 兜底，可咱们见过 [detach 的下场](../ch01-thread-lifecycle-raii/02-thread-arguments-and-lifetime)：带着对池成员引用跑野了的线程，等着它的是未定义行为。练习 2 会请您在限时关闭里跟这个取舍正面交手。

带期限的等待，标准库在这儿是给不出的：std::thread 和 jthread 的成员表里，都只有不带期限的 join。带期限的 try_join_until 是 Boost.Thread 的货，glibc 倒是供了一个 pthread_timedjoin_np，`_np` 就是 non-portable 的意思，可标准的名单上没有它。咱们能等的，是 worker 自己报的退场计数，它的形状长这样：

```cpp
// 思路示意：带期限的等待（练习 2 的起点）
bool shutdown_with_deadline(std::chrono::milliseconds limit)
{
    stop_source_.request_stop();
    cv_any_.notify_all();
    auto deadline = std::chrono::steady_clock::now() + limit;
    std::unique_lock<std::mutex> lock(mutex_);
    return exit_cv_.wait_until(lock, deadline,
                               [this] { return live_workers_ == 0; });
}
```

计数的另一半记在 worker_loop 的退场路径上：构造池子的时候，咱们把 live_workers_ 初始化成 worker 的数目，退场的 return 之前在持锁的窗口里把它减一，咱们再对 exit_cv_ 喊一声 notify_all，数目就对得上了。wait_until 返回 false 的时候，池子就处在半关的状态，没退的线程还在跑它们的活。咱们要么放宽期限再等，要么放弃等待走人，但 detach 的路咱们前面已经堵死了。

### 析构之后再 submit：未定义行为

析构跑完了，workers_ 空了，停止也请求过了，这时的池子就是一具空壳。咱们拿它再去 submit，多数实现上确实会被拒收的检查拦下来，抛出了异常，看着挺安全的。不过严格按标准讲，等析构函数返回了，全体成员的生命周期就都结束了：submit 读的 stop_source_ 是已销毁的对象，tasks_ 和它的队列也是，读个 size 并不比摸别的成员更体面。这一趟走的本就是未定义行为，只是成员占的内存在栈上或堆上都还没被动过，碰巧收得干净而已。咱们的建议干脆：析构过的池子就别再碰，把它当一次性的用品。

### 池子不能拷贝，移动也要三思

拷贝构造和拷贝赋值咱们都 delete 掉了，理由咱们一想就明白：两个池子共享的是同一组 worker，重复的 join 是逃不掉的，灾难就是这么来的。移动构造咱们倒是写得出来，只是没有想的那么顺：thread 能搬，mutex 连移动都被禁了，只能在新对象里原地重造一把，队列和标志再一个个地搬过去。可搬完之后 submit 的调用方还攥着旧地址的话，悬垂就是这么来的。实践里的共识是池子建在哪里就死在哪里，生命周期跟所属的组件对齐，对外传的是引用。

## 异常的去向

task() 跑在 worker 的线程里，任务抛了异常会怎样？这里咱们要勘一个旧说法。旧卷的线程池篇写过一句“异常会被 std::function<void()> 的调用吞掉”，这话咱们得按规范改一改。std::function 的调用不吞异常：任务抛出来的异常会沿着 task() 的调用点往上走，一路穿过 worker_loop、逃出线程的入口函数。而线程函数以异常退出时，标准会替咱们调用 std::terminate。所以裸 enqueue（不走 packaged_task）的池子里，一抛异常搭进去的就是整个进程。真正的凶手是异常逃出了线程的顶层。

> **勘误**：旧文的口径咱们在这里更正。未经 packaged_task 包装的任务抛了异常，异常会一路传播出线程的入口函数、触发 std::terminate。走 packaged_task 的任务，异常被存进了共享状态，调用方的 future.get() 把它重抛了出来，同一通道的验证在 [packaged_task 那篇](02-promise-and-packaged-task) 做过。

某个任务抛了异常，队列里剩下的任务会怎样？答案咱们试过：后面的任务照样跑完。异常被存进了共享状态，worker 的循环根本没看见它，task() 正常地返回了，循环接着取的是下一件。整个池子不会因为一个任务的失败而停摆，这个行为跟 std::async 的任务是一样的。

还有一条消费的纪律值得您记下：packaged_task 关联的 future，析构干的事只是释放共享状态的引用，任务的死活它是不管的。您 submit 了却不 get，任务的异常就被静默地放走了。而 std::async 那边的 future 正相反，那边的析构会阻塞到任务完成，差异的对照咱们在 [async 那篇](01-async-and-future) 做过。所以 submit 拿到的 future，咱们要么 get、要么 wait，把它当成必办的一步。

排查异常类的竞态，TSan 和 ASan 的分工咱们在 [线程参数那篇](../ch01-thread-lifecycle-raii/02-thread-arguments-and-lifetime) 见过：TSan 管的是时序，ASan 管的是内存。池子的代码两边都值得过一遍，本篇的验证就是拿 TSan 跑的。

## 练习

### 练习 1：会插队的队列

咱们用 `std::priority_queue` 换掉 `std::queue`，任务变成带重要度的包装类型。提示：默认的形状是最大堆，咱们在包装类型里补上比较运算，急活就排在了前头。您想一想 drain 语义在堆序下还成立吗，最后一个出队的任务，它的保证是什么。做完了您可以拿 1000 个随机重要度的任务灌进去，咱们核对出队顺序合不合堆序，drain 的收尾有没有把急活全跑完。

### 练习 2：带期限的关闭

咱们给析构加一个期限，到期还有没退的 worker，就放弃了对它的等待。您很快会遇上 detach 的诱惑，[detach 的现场](../ch01-thread-lifecycle-raii/02-thread-arguments-and-lifetime) 是前车之鉴，那些任务还引用着池的成员。提示：让长任务定期地看旗子，把单段任务的时长压下来，这可比 detach 安全多了。咱们提醒一句，wait_until 等的是 steady_clock 上的绝对期限，别图顺手换成循环里反复从当下起算的 wait_for，不然每转一圈期限就跟着往后挪了一截，慢任务能把总的等待拖到没有边。

### 练习 3：工作窃取的雏形

咱们给每个 worker 一条本地的队列，空了再去别人的队列里拿。竞争确实少了，不过取消和关闭的广播得重新设计，停止的信号人人都得及时看到。无锁队列的弹药在 [SPSC 与 MPSC](../ch04-concurrent-data-structures/04-spsc-and-mpsc)，动手量大的版本在 [Lab：生产级线程池](../exercises/03-thread-pool)。动手以前咱们拿纸笔把两件事画清楚：谁睡在谁的队列里，广播的责任归谁。画清了咱们再动手写代码，弯路也就少走一些了。

> 咱们给三个练习留一句统一的提示：都不必奔生产级去，drain 语义守住了就算达标。

## 本篇的落点

三样东西咱们带走了。worker 循环立住了 canonical 的版本，从 C++17 的裸版起步，一路演进到 cv_any 三参 wait 的一行判返回值。关闭走通了三步时序，join 要赶在成员死亡之前的教训也一并留下了。类型擦除的因果链理顺了，从 function 的可拷贝要求，一路到 move_only_function 的落地。往后的协程调度器和 Actor 的邮箱都要回来取这三样料，协程调度器的等待循环，就是拿本篇的 worker 循环改出来的。

咱们给自己留一份验收清单，下面的断言您应该都能亲口说清：

- 为什么谓词里查两个条件，退出却要两个都成立。
- wait 三参版返回 false 的时候，队列和停止状态各是什么。
- 析构里的 join 为什么要写进函数体，而不是交给成员的析构。

清单上的三问您要是答得利索，本篇的功课就算过关了。答得不利索的也不要紧，咱们翻回对应的现场再看一遍，翻书是从来不丢人的。

## 下一步

从线程到任务的路到这里就走完了。async 打的头阵，promise 与 packaged_task 打通的结果通道，线程池把执行流收拢成了能优雅关闭的一池 worker。下一章咱们往协程去，[协程基础](../ch06-async-io-coroutine/01-coroutine-basics) 会把 co_await 的机制立住，[task 协程](../ch06-async-io-coroutine/02-task-coroutine) 则把本篇的 submit 升级成能挂起、能恢复的任务类型，[取消那一篇](../ch06-async-io-coroutine/03-coroutine-cancellation) 还要回来取 cv_any 三参 wait 的料。想动手把池子造厚实的，[Lab：生产级线程池](../exercises/03-thread-pool) 等着您。全卷的走向见 [卷地图](../)。

exercises 的线程池 Lab 还会补上就地执行的开关，工时表上给它排的是十到十四个小时。咱们本篇攒下的正源到那边直接接着往上搭。

> 💡 咱们把完整示例代码放在 [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP)，您可以访问 `code/volumn_codes/vol5/ch05-future-task-threadpool/03_thread_pool.cpp`。C++17 的裸版和 C++20 的完整版都在，编译和 TSan 的命令写在文件头的注释里，您可以对照着跑一遍那 300 轮的 drain 测试。

## 参考资源

- [std::condition_variable_any::wait -- cppreference](https://en.cppreference.com/w/cpp/thread/condition_variable_any/wait)
- [std::condition_variable -- cppreference](https://en.cppreference.com/w/cpp/thread/condition_variable)
- [std::jthread -- cppreference](https://en.cppreference.com/w/cpp/thread/jthread)
- [std::stop_source::request_stop -- cppreference](https://en.cppreference.com/w/cpp/thread/stop_source/request_stop)
- [std::stop_callback -- cppreference](https://en.cppreference.com/w/cpp/thread/stop_callback)
- [std::packaged_task -- cppreference](https://en.cppreference.com/w/cpp/thread/packaged_task)
- [std::function -- cppreference](https://en.cppreference.com/w/cpp/utility/functional/function)
- [std::move_only_function -- cppreference](https://en.cppreference.com/w/cpp/utility/functional/move_only_function)
- [P0288R9: move_only_function -- open-std.org](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2021/p0288r9.html)
- [P0660R10: Stop Token and Joining Thread, Rev 10 -- open-std.org](http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p0660r10.pdf)
- Anthony Williams. *C++ Concurrency in Action*, 2nd ed. Manning, 2019. 第 9 章有线程池的完整叙述，析构与异常的口径跟本篇一致。

