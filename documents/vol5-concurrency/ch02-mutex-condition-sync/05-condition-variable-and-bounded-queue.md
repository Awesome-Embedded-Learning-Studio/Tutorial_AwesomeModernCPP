---
title: "condition_variable 与阻塞队列"
description: "从轮询的两难出发，建立谓词等待的纪律，再把它组装成可关闭、可超时的有界阻塞队列"
chapter: 2
order: 5
tags:
  - host
  - cpp-modern
  - intermediate
  - mutex
  - 容器
difficulty: intermediate
platform: host
reading_time_minutes: 26
prerequisites:
  - "mutex 与 RAII 守卫"
  - "死锁与现场诊断"
related:
  - "线程池"
  - "Actor 模型与邮箱"
  - "SPSC 与 MPSC 队列"
cpp_standard:
  - 11
  - 14
---

# condition_variable 与阻塞队列

咱们在 [mutex 那篇](01-mutex-and-raii-guards) 里把临界区看住了，又在 [死锁那篇](02-deadlock-and-gdb) 里练了怎么给挂住的程序找凶手。不过有一类需求，咱们拿这两样家伙合力也接不住：线程等的其实是一个条件，而 mutex 对条件一无所知。消费者等的是队列非空、生产者等的是队列不满，mutex 能给您的保证只有互斥，也就是同一时刻碰共享状态的人至多一个。这些条件什么时候成立？它一个字都不会说。

最土的办法咱们都会写：轮询。

```cpp
while (true) {
    {
        std::lock_guard<std::mutex> lock(mtx);
        if (!tasks.empty()) {
            // 取走一个任务，跳出循环
            break;
        }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));  // 睡多久合适？
}
```

跑是能跑的，难受的地方您也感受得到。睡得太短的话，CPU 的大头就花在了加锁、看一眼、解锁的空转上，睡得太长了也不行，条件明明成立了，咱们还得白等一个睡眠周期，响应也就钝了。更麻烦的是那个毫秒数没有正确答案，调小一点只是换了个折中，负载一变咱们又得重新调。卷首咱们立过的三条原则，头一条讲的就是 `先正确性，再性能`，轮询偏偏把两头都做坏了：正确性上它靠碰运气的间隔，性能上它白烧了 CPU。

`std::condition_variable`（下文一律简称 cv）就是标准库为这类等待准备的工具。用法就这一句：**等条件的人睡在 cv 上，改条件的人改完去 cv 上喊一嗓子**。睡着的活由操作系统接管：挂起，不占 CPU 的时间片，被喊了再回来。听着倒是简单，可它的几个语义细节，正是并发 bug 最爱藏身的地方。本篇的活也定下来了：把 cv 的等待语义建立牢靠，然后用它造一个能关闭、能超时的有界阻塞队列。这套队列在咱们手里可不是一次性的教具，后面的线程池和 Actor 邮箱用的都是它。

## wait 到底承诺了什么

cv 的接口小得很：睡的活归 wait，叫人的活归 notify_one 和 notify_all。真正要花力气读的，是 wait 的语义。cppreference 把裸 wait 的行为写成了三段，咱们一段段过：

1. 原子地执行 `lock.unlock()` 并阻塞在 cv 上，“原子地”就是灵魂：解锁和入睡之间没有缝隙，通知不可能恰好掉进缝里被错过，丢失唤醒那一节还要回头用到它。
2. 被 `notify_one()` 或 `notify_all()` 唤醒。标准原文在这里还写了半句：也可能虚假地醒（or spuriously）。这半句的分量不轻，下一节专门伺候它。
3. 醒来后重新拿锁，可能要在锁上排队等一会儿，拿到了才从 wait 返回。

wait 返回的那一刻有个后置条件在那儿等着：`lock.owns_lock()` 为 true，锁回到了调用线程手里。标准还补了一句狠话：这个后置条件要是满足不了怎么办？给出的行为是调用 `std::terminate`。文档脚注里指过一种触发的情形：重新拿锁那一步抛了异常。咱们不用天天惦记它，但得知道 wait 的承诺到哪为止：它管睡和醒，不管程序的死活。

咱们在这里还能顺手回答 [mutex 那篇](01-mutex-and-raii-guards) 留下的一个悬念：wait 的参数为什么必须是 `std::unique_lock<std::mutex>`，换 `lock_guard` 连编译都过不了？标准里写明了 cv 只与 `unique_lock<std::mutex>` 协作，为的是在部分平台上拿到更高的效率。真正的原因就藏在上面的三段里：wait 得能把您交出去的那把锁解开，又得在醒来之后原样地拿回来，而 `lock_guard` 连 `unlock` 都不肯暴露，自然也就接不住这个活了。

边角的事实还有两条：wait 与 notify 系列是允许并发调用的，您就算让好几个线程同时 notify 也不会出事。而踩进未定义行为的写法有三种：传进来的 lock 没锁住、锁不属于调用线程，以及同一个 cv 上混用了两把不同的 mutex。第三种是最容易犯的，咱们的对策就一句话：一把 cv 配一把锁，从一而终地配下去。

## 虚假唤醒：没人叫，它也会醒

轮到那半句 or spuriously 了。虚假唤醒（spurious wakeup）说的是一种任性：没有人 notify，wait 也可能自己就醒了。这可不是实现偷懒出来的 bug：标准白纸黑字允许了它，POSIX 的条件变量允许，Win32 那边的同样放行，Raymond Chen 的博客里也专门写过它（链接在参考资源里），可见它也不是 POSIX 独有的怪癖。为什么允许？咱们笼统地答：这是拿语义上的严格去换实现上的轻。要是要求每次唤醒都和一次 notify 严丝合缝地对应，实现就得给每次通知做上精确的登记与核销，开销也就跟着上去了。标准于是选择了放松要求，把检查条件的责任还给了应用。

咱们直接看一个最能哄人的程序，您甚至可能亲手写过它：worker 拿锁、裸 wait、醒来打印 ready，而主线程睡 50 毫秒后置位并 notify。

```cpp
std::mutex mtx;
std::condition_variable cv;
bool ready = false;

void worker()
{
    std::unique_lock<std::mutex> lock(mtx);
    cv.wait(lock);   // 裸 wait：醒了就当 ready 成立
    std::cout << "worker: ready = " << ready << '\n';
}

int main()
{
    std::thread t(worker);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    {
        std::lock_guard<std::mutex> lock(mtx);
        ready = true;
    }
    cv.notify_one();
    t.join();
}
```

笔者连跑了十遍都过了，打印出来的 ready 全都是 1。能跑和正确之间的距离，就藏在这样的侥幸里。wait 返回了之后，程序里没有任何人查过 ready 的值：notify 也许真的来过，可路过的也可能是一次虚假唤醒，两种来历在返回值上是分不出来的。标准给出的对策直接写进代码：wait 返回了，咱们不能假设有人 notify 过，更不能假设条件已经成立了。教科书的标准姿势是把 wait 套进循环、醒来就重新查条件：

```cpp
std::unique_lock<std::mutex> lock(mtx);
while (!ready) {
    cv.wait(lock);
}
// 走到这里，ready 一定是 true，而且锁在咱们手里
```

咱们把开头的握手程序照这个样子改一遍，worker 里要动的只有一行：把裸 wait 换成谓词版。

```cpp
void worker()
{
    std::unique_lock<std::mutex> lock(mtx);
    cv.wait(lock, [] { return ready; });   // 谓词版：醒来先验货
    std::cout << "worker: ready = " << ready << '\n';
}
```

其余的时序一点没动，您再跑它，底气就完全不同了：无论通知来得早还是晚，还是无缘无故地醒，worker 打印之前都亲眼确认过 ready 的值。这一次的正确不靠运气，靠的是每一次醒来都重新查条件。

库的作者们看出来大家老忘写循环，于是干脆给了个谓词版本的重载，把循环卷进了 wait 的内部。标准对它的定义只有一行：

```cpp
while (!pred()) { wait(lock); }
```

谓词（predicate）这个词您别被它唬住，它其实就是个返回 bool 的可调用对象，回答的是条件现在成立吗。`cv.wait(lock, [] { return ready; })` 跟您手写的那段 while 循环完全等价。这个等价展开里还藏着一个大事实：**`pred()` 是在持有锁的时候被求值的**。循环里的 `pred()` 跑在 wait 外面、锁的里面，每转一圈做的都是拿着锁查条件的事。后面的所有推理，咱们都靠这一行撑着。

还有一个问题值得咱们停一停：谓词看的那个共享变量，如果是 atomic 的，改它的时候还要拿锁吗？不少人的直觉是都 atomic 了就不需要锁了。cppreference 在 cv 的主页上专门写过一句，大意是：**哪怕共享变量是 atomic 的，也必须在持有 mutex 的情况下修改，才能把修改正确地发布给等待的线程**。锁在这里管的不只是互斥，还有等待方与通知方之间的同步关系：通知方的改完并 notify，和等待方的查条件并入睡，靠的正是同一把锁串起来的一条线。内存序的正式定义不在本篇展开，[happens-before 的正源](../ch03-atomic-memory-model/02-atomics-and-happens-before)在 第 3 章，这里您只需要认下持锁修改的规范。

> 咱们再补一个边角的事实：如果等待的是纯事件，notify 本身就是全部的含义，也没有共享的条件可查，那裸 wait 配上外层循环也是合法的写法。本篇从头到尾教的都是谓词版，免得咱们在边角上分岔。

## 丢失唤醒：通知是不等人的

虚假唤醒是没人叫却醒了，丢失唤醒（lost wakeup）刚好把方向倒了过来：叫过了，可听的人还没就位。cv 的通知没有记性，notify 发出去的一瞬间就是它的全部生命，它不会存起来等着补给晚到的线程。标准在条件变量的通用条款里给了咱们一个全序保证，替咱们把一件事敲定了：notify 的效果，连同 wait 的三段原子步骤，落在了同一个全序里，notify_one 是不可能被拖后的，回头去唤醒一个在它之后才开始等待的线程。

那么通知跑在了等待的前面怎么办？答案您手里已经有了：谓词。咱们拿线程交错走一遍就明白。等待的线程拿住锁，头一件事就是求一次 `pred()` 的值，通知的线程要改条件、也得拿同一把锁，于是两件事被串行化了。要么等待的一方赶在前面，`pred()` 看到的是 false，然后就走进了 wait，原子地解锁入睡，之后通知方的修改加 notify 一定会把它叫醒。要么通知的一方赶在前面，改完了条件、发完了 notify、放下了锁，等待的一方这才拿到锁，`pred()` 头一次求值的结果就是 true，根本就不进 wait 的门、直接抬腿走人。两条路都是通的，发出去的 notify 也一次没白费。

裸 wait 偏偏在两条路上各摔一跤：虚假唤醒来的时候它不查条件就往下走，通知提前的时候它错过了通知还接着睡。谓词版把两处都补上了，而且用的是同一个循环。回头再看开篇的那个轮询循环，咱们真正想要的东西就清楚了：检查条件的能力，再加上睡到该醒的效率。cv 两样都给了，代价则是这些语义上的细节，得一条一条地读懂。

咱们还可以亲手把丢失唤醒复现出来，跑过一遍的体感比读十遍文字都牢。咱们让 worker 一进来就睡 200 毫秒，睡醒了再去等一个 ready 标志，主线程在 50 毫秒的时候置位 ready 并 notify。裸 wait 的版本会永远挂在 wait 上：通知发的时候它还在睡懒觉，等它摸到 cv 的跟前，通知早凉了。换成谓词版的写法，同一个时序下程序就正常退出了，因为 worker 拿到锁之后的第一次求值看到的就是 true。练习 2 会让您亲手跑一遍。

笔者实测：WSL2 Arch Linux、内核 6.18、g++ 16.2.1、AMD Ryzen 7 9700X。两个版本各写成一个独立程序（时序就是上面说的：worker 先睡 200 毫秒，主线程 50 毫秒时置位并 notify），裸 wait 版用 `timeout 3` 限着跑：

```text
$ timeout 3 ./lost_wakeup_bare
main: notified
$ echo $?
124
$ ./lost_wakeup_pred
main: notified
worker: ready = 1
main: joined
$ echo $?
0
```

裸 wait 版打完 `main: notified` 就再没动静，3 秒后被 timeout 掐掉、退出码 124——通知在 worker 睡觉的那 150 毫秒里发完就没了，它摸到 cv 跟前时已经无会可赴。谓词版同一个时序，worker 醒来拿到锁、第一次求值就看到 true，连 wait 的门都没进，程序干干净净退出。

## 把 mutex 那篇的黑盒收编

是时候回到主线了。咱们在 [mutex 那篇](01-mutex-and-raii-guards) 里让 ThreadSafeQueue 当过黑盒，用了它、却没讲过 pop 为什么能等人。现在装备齐了，咱们把它收编进来开工。

```cpp
template <typename T>
class ThreadSafeQueue {
public:
    void push(const T& value)
    {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            queue_.push(value);
        }
        cv_.notify_one();   // 锁放下了才去叫人，理由见 notify 要拿着锁去喊吗 一节
    }

    T pop()
    {
        std::unique_lock<std::mutex> lock(mtx_);
        cv_.wait(lock, [this] { return !queue_.empty(); });
        T value = std::move(queue_.front());
        queue_.pop();
        return value;
    }

private:
    std::queue<T> queue_;
    mutable std::mutex mtx_;
    std::condition_variable cv_;
};
```

push 和 pop 用的锁类型不一样，咱们这样写是有讲究的。push 只要在持锁的窗口里改一改容器，`lock_guard` 就够用了，作用域块一结束锁就放下了，notify 留到锁的外面去做。而 pop 得把锁交给 wait 保管：进来时拿着，睡觉的时候交出、醒来之后收回，全程只有 `unique_lock` 干得了。谓词 `[this] { return !queue_.empty(); }` 把虚假唤醒和丢失唤醒一起挡在了门外，wait 返回的时候队列一定非空、锁一定在手上，后面的 front 和 pop 就能放心做。

它在教学上够用了，工程上的短板却是天生的：容量没有上限。生产速度压过消费速度的时候，队列会不断地涨，内存也就跟着涨了，最后消费者等来的搞不好就是 OOM（内存耗尽）。生产者也感知不到任何的压力，push 永远是立刻成功的，下游的堵塞根本传不上来。咱们要给队列装上满了就等的背压，就得让它具备第二个等待的条件。这一步的升级，正好把 cv 的经典用法完整地带出来。

## 有界队列：为什么要两把 cv

升级的第一步是加一个 `capacity_`，等待的条件从一条变成了两条：不满才能 push、不空才能 pop。条件分成了两类、等待者也分两拨，那 cv 要一把还是两把？两版咱们都值得看，从双 cv 的版本看起：

```cpp
template <typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(std::size_t capacity) : capacity_(capacity) {}

    void push(T value)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        not_full_.wait(lock, [this] { return queue_.size() < capacity_; });
        queue_.push(std::move(value));
        not_empty_.notify_one();   // 叫的是消费者那一边
    }

    T pop()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        not_empty_.wait(lock, [this] { return !queue_.empty(); });
        T value = std::move(queue_.front());
        queue_.pop();
        not_full_.notify_one();    // 叫的是生产者那一边
    }

private:
    std::queue<T> queue_;
    std::size_t capacity_;
    std::mutex mutex_;
    std::condition_variable not_full_;   // 生产者在这把上等“不满”
    std::condition_variable not_empty_;  // 消费者在这把上等“不空”
};
```

咱们手上的锁还是原来的一把，管住 `queue_` 和 `capacity_` 的所有访问。cv 则变成了两把，`not_full_` 伺候的是生产者的等待，`not_empty_` 伺候的是消费者的等待。push 做完了只叫 `not_empty_`、pop 做完了只叫 `not_full_`，通知的指向就精确了：腾出空位，叫的是等空位的人。放进新货的时候，叫的是等货的人。

单把 cv 行不行？行倒是行的、正确性一点不差，Williams 的《C++ Concurrency in Action》里那几个 threadsafe_queue 就是单 cv 的形状，咱们上一节的无界版也是。咱们关心的差别在唤醒的精度。单 cv 的时候，push 的 notify_one 叫醒的可能是另一个生产者：它揉着眼睛拿到了锁，发现队列还是满的、谓词不成立，扭头又睡了。程序倒是没坏，就是白叫了一趟。醒过来却发现没自己事的唤醒，教材上给它起了个名字叫无效唤醒。队列两边都有等待者的时候，双 cv 把叫错边的可能直接消掉了。咱们注意用词：说单 cv 不够精确是可以的，说它不正确就冤枉了。谓词和锁兜住的是正确性，cv 的数量只决定叫醒谁。

咱们跑个最小的演示就能看见背压在工作：容量给 10、让生产者连塞 20 个数，消费者慢慢地取。塞满了 10 个之后，生产者第 11 次 push 的谓词就不成立了，它就把自己挂在了 `not_full_` 上，直到消费者取走了一个、回头 notify 它。两个线程被容量逼着你一步我一步地推进，谁想甩开谁都是做不到的。

演示的骨架给您摆在这儿：两个线程、一个队列、一个求和。

```cpp
int main()
{
    BoundedQueue<int> q(10);
    std::thread producer([&q] {
        for (int i = 1; i <= 20; ++i) {
            q.push(i);
        }
    });
    long sum = 0;
    std::thread consumer([&q, &sum] {
        for (int i = 1; i <= 20; ++i) {
            sum += q.pop();
        }
    });
    producer.join();
    consumer.join();
    std::cout << "sum = " << sum << '\n';   // 期望 210
}
```

最后印出来的总数是 210，丢掉的数一个也没有，这就是背压在替咱们守门。生产者塞满之后就在 push 里睡着了，消费者的每次 pop 都会叫它一声，节奏也就完全被容量牵着走了。您把容量改成 5、生产改成 50，它还是稳的，变的只是睡与醒的次数。

<OnlineCompilerDemo
  title="动手验证：背压把 20 个数一个不丢地送到手"
  source-path="code/examples/vol5/38_bounded_queue_backpressure.cpp"
  description="容量 10 的双 cv 有界队列，生产者塞 20 个数、消费者取 20 个数。观察输出：sum = 210（1 加到 20 的期望值）。多跑几遍，总数稳定不变；生产者塞满 10 个后睡在 `not_full_` 上、消费者每 pop 一个叫它一声，这一睡一醒都发生在队列内部，输出里看到的只有最后的总数。"
  run-options="-O2 -std=c++17 -pthread"
  allow-run
/>

三遍全是 210，一个数都没丢。生产者确实在塞满 10 个之后睡过去了，只是这一睡一醒都发生在 push 和 pop 的内部，从输出上看不到，看到的只有最后不缺斤短两的总数。

工具也该上场了。咱们在 [data race 那篇](../ch00-concurrency-fundamentals/02-data-race-and-tsan) 学过 TSan 报告的读法，这里正好复习：把双 cv 版放进多生产者多消费者的压力场景，咱们用 `-fsanitize=thread` 重编一份、多跑几轮，看它到底报不报 data race？谓词和锁都写对的话，报告应当干净：所有对 `queue_` 的读写都在 mutex 的保护下，TSan 追得到其中的每一对。真蹦出了报告，就按 data race 那篇的流程走，十有八九是哪条路径忘了拿锁。

笔者实测：WSL2 Arch Linux、内核 6.18、g++ 16.2.1、AMD Ryzen 7 9700X。咱们把本篇的 close/drain 版 BoundedQueue 放进压力场景：3 个生产者各塞 5000 个数、2 个消费者 drain 到底，容量 64，一轮跑完再连跑三轮，用 `g++ -std=c++20 -fsanitize=thread -g -O2 -pthread` 编译后连跑三次：

```text
$ ./queue_stress_tsan
round 1: consumed = 15000 (expect 15000), sum = 112492500 (expect 112492500)
round 2: consumed = 15000 (expect 15000), sum = 112492500 (expect 112492500)
round 3: consumed = 15000 (expect 15000), sum = 112492500 (expect 112492500)
$ echo $?
0
```

九轮压力（三次 × 三轮）下来，TSan 一个 `WARNING: ThreadSanitizer` 都没吐，退出码 0，报告干干净净——45000 次 push/pop 的每一对读写都在 mutex 的保护下。总数和总和也逐轮对上（112492500 是 0..4999 三份加两段偏移的真实期望值），正确性与无竞争两份证据都齐了。

## close()：给队列一个干净的结束

能跑的队列有了，可它还欠一样东西：一个体面的结束。想象收工的场景：生产者陆续停工，消费者把剩下的存货收干，大家各自收工。现在的 pop 是做不到的：队列一空，消费者就睡死在了 wait 上，等一个永远不来的 push。更狠的还在后面：主线程要是等不及了，直接让队列的对象走析构，睡在 cv 上的线程转眼就站在了一块已经释放的内存上。cppreference 对 cv 析构的安全条件写得很明白：所有等待的线程都得到通知之后，析构才是安全的，而且析构开始的那一刻之后，不能再有人来 wait 了。这样的要求裸写在调用方手里就太容易漏了，咱们把它们做进队列的接口，这就是 close() 的由来了。

close 的语义，咱们用两句话定清楚。关了以后，push 也就被拒了，没有谁会再消费了，塞进去也就是浪费了。关门前已经入队的数据、pop 要能全部取走，这个动作的名字叫 drain（排干），取完以后 pop 报告的就是空且已关，消费者看见它就知道该退场了。drain 是不能省的，少了它的话，关队列就等于把没处理的任务就地掩埋。

结果怎么报告给调用方？咱们用枚举：

```cpp
enum class QueueResult {
    kSuccess,
    kClosed,
    kTimeout,   // 超时版才用得上，下一节见
};
```

接下来咱们把 `closed_` 标志加进队列，谓词的两边都挂上它：

```cpp
template <typename T>
class BoundedQueue {
public:
    void close()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }
        not_full_.notify_all();
        not_empty_.notify_all();
    }

    QueueResult push(T value)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        not_full_.wait(lock, [this] {
            return queue_.size() < capacity_ || closed_;
        });
        if (closed_) {
            return QueueResult::kClosed;
        }
        queue_.push(std::move(value));
        not_empty_.notify_one();
        return QueueResult::kSuccess;
    }

    QueueResult pop(T& value)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        not_empty_.wait(lock, [this] {
            return !queue_.empty() || closed_;
        });
        if (queue_.empty()) {
            return QueueResult::kClosed;   // 空且已关：drain 完成
        }
        value = std::move(queue_.front());
        queue_.pop();
        not_full_.notify_one();
        return QueueResult::kSuccess;
    }

private:
    std::queue<T> queue_;
    std::size_t capacity_;
    bool closed_ = false;
    mutable std::mutex mutex_;
    std::condition_variable not_full_;
    std::condition_variable not_empty_;
};
```

咱们挑几处要紧的看。`closed_` 是谓词的一部分，两边都挂上了 `|| closed_`，于是 close 变成对所有等待者的敲门声：就算队列满着，等空位的生产者也会醒，看到 `closed_` 也就收工了。close 里为什么是 notify_all？因为关门是个全局的事件，两边所有睡着的人都需要醒。用 notify_one 的话一次只能叫一个，指望醒了的人再去叫下一个，这样的链条又脆又慢，中间的哪个环节没接上，后面的人就永远睡过头了。notify_all 是关闭场景的标配。

还有一处不对称的地方，您多半已经瞄到了：push 醒来查 `closed_`，pop 醒来查的却是 empty()。这样的不对称看着别扭，语义其实是自洽的。push 会被拒的原因只有一种，就是关门了。pop 失败的原因也只有一种，就是队列空了：关着门但存货还在的时候，pop 是照样能取的，这正是 drain 的含义。两个后查动作各自对准自己唯一的失败原因，查谁就不言自明了。

消费者的用法也顺势定型了，咱们用 while 循环把 drain 走完：

```cpp
int value = 0;
while (q.pop(value) == QueueResult::kSuccess) {
    handle(value);
}
// 走到这里：队列空了，门也关了，消费者可以安心退场
```

返回式的设计不止一种答案，咱们把工业上几家的选择摆在一起看就更有味道了：

| 出处 | 关闭怎么报告 | 形状 |
|---|---|---|
| Boost.Thread 的 sync_queue | `queue_op_status::closed` 枚举，另备 `sync_queue_is_closed` 异常 | 枚举式与异常式并存，调用方挑着用 |
| asio 的 channel | `experimental::error::channel_closed` 错误码 | 错误码式，惯用法是把它当正常退出信号 |
| 《C++ Concurrency in Action》的 threadsafe_queue | `try_pop` 返回 bool 或空指针，`wait_and_pop` 用出参 | 不用异常报告空 |
| 本篇的 BoundedQueue | `QueueResult` 枚举 | 三态：成功、关闭、超时 |

asio（C++ 的网络库）那一行的口径要交代一下：官方的 reference 页对 close 行为着墨很少，这一行来自社区常见的行为实例与惯用法，咱们按行为实例对待，而不是当文档原文。C++17 之后的 `std::optional<T>` 也是 bool 加出参的现代替身，try_pop 返回 optional 的写法同样干净。几条路都是通的，取舍看调用方想不想区分失败的原因：想区分的话就用枚举或错误码，不想区分的话，bool 或 optional 也就够了。Boost 干脆把两套都给了，可见这在工程里就是个口味的问题。

close 的正确性值得一次像样的压力测试：3 个生产者各 push 100 个数、2 个消费者一直取到 kClosed 为止、生产者全部 join 之后才关门，最后咱们用 atomic 计数核对，看总数是不是正好凑齐了 300 个。顺序要是排反了，赶在前面把门关了再去等生产者，push 就会吃到 kClosed 而提前撤退，丢数就是意料之中了。

<OnlineCompilerDemo
  title="动手验证：join 完生产者再 close，drain 一个不丢"
  source-path="code/examples/vol5/39_bounded_queue_close_drain.cpp"
  description="3 个生产者各塞 100 个数、2 个消费者取到 kClosed 为止，容量 8，生产者全部 join 之后才 close。观察输出：drain consumed = 300 (expect 300)。多跑几遍总数稳定；消费者退场靠的是 pop 返回 kClosed，不是超时或猜测。"
  run-options="-O2 -std=c++17 -pthread"
  allow-run
/>

五遍全是 300，一个不丢。顺序排对的关门前 drain 都这么稳，反例您也就有底气了：真把 close 挪到生产者 join 之前，push 就会吃到 kClosed 提前撤退——这个反例留给您自己跑，数对不上才是它该有的样子。

## 不想死等：try_pop_for 的三态返回

还有一类需求是 close 管不了的。您作为调用方，不想无限地等、只想在期限内试一把。比如网络服务把请求塞进队列的时候，等了 100 毫秒还没位置，丢掉请求反而是它能承受的，吊死接收线程反而是它受不了的。wait_for 就是为这样的等待准备的：

```cpp
template <typename Rep, typename Period>
QueueResult try_pop_for(T& value,
                        const std::chrono::duration<Rep, Period>& timeout)
{
    std::unique_lock<std::mutex> lock(mutex_);
    const bool ok = not_empty_.wait_for(lock, timeout, [this] {
        return !queue_.empty() || closed_;
    });
    if (!ok) {
        return QueueResult::kTimeout;
    }
    if (queue_.empty()) {
        return QueueResult::kClosed;
    }
    value = std::move(queue_.front());
    queue_.pop();
    not_full_.notify_one();
    return QueueResult::kSuccess;
}
```

wait_for 谓词版返回的是 bool，含义精确得很：返回前谓词最后一次求值的结果。ok 为 true、条件成立了，后面的动作照旧是查空、取货。而 ok 为 false 说的则是期限内条件没成立，返回的也就是 kTimeout。这里有个咱们容易漏的细节：裸 wait_for（不带谓词）的返回值，两头都是靠不住的。Notes 里话说得很直白：就算通知是持着锁发出的，裸版因超时而返回的时候，它对谓词的状态也不作任何保证。no_timeout 那一头咱们同样不能多想，它只表示醒了，醒的原因也可能是一次虚假唤醒。所以超时路径要么用谓词版、要么自己在外面包循环查条件，裸的返回值是不能当条件用的。

三态的走向咱们用一个小场景就能验明：空队列、有货、关门各试一次。

```cpp
BoundedQueue<int> q(4);
int value = 0;

auto r1 = q.try_pop_for(value, std::chrono::milliseconds(100));
// 空队列等不到：r1 == QueueResult::kTimeout

q.push(42);
auto r2 = q.try_pop_for(value, std::chrono::milliseconds(100));
// 100 毫秒内有货：r2 == QueueResult::kSuccess，value == 42

q.close();
auto r3 = q.try_pop_for(value, std::chrono::milliseconds(100));
// 关门且取干：r3 == QueueResult::kClosed
```

三条路径走出来的结果各不相同：等不到是 kTimeout、拿到了是 kSuccess、门关了货也取干了是 kClosed，咱们靠一个枚举就能把三种结局安排明白。

时钟上还有个贴心的地方。wait_for 的定义等价于 `wait_until(lock, steady_clock::now() + rel_time)`，计时走的是 steady_clock：墙上的钟被 NTP 拨快拨慢，都不会影响它的计时。wait_until 配 system_clock 才有跳变的问题，真到了需要绝对截止时间的场景，您再回头翻手册。

判断的顺序也算一个坦白：超时查在关闭前面，是本篇的设计选择。反过来排也是成立的，取舍取决于调用方更在意的是没等到还是队列没了，也没有唯一的答案，咱们把话说开就好。

## 满了的另一种态度：把新来的丢掉

不过在咱们手里，对满的应对不止阻塞一种：有的场景里，卡住生产者的代价反而更大。日志聚合、指标上报就是这样的场景：丢一条无所谓，接收线程被吊住才是真正的事故。丢的做法也分两种：丢新来的，就是队列满了以后新元素直接不要了，丢最老的，则是踢掉队头给新元素腾位子、适合只关心最近数据的滑动窗口。丢新来的版本写起来最省事：

```cpp
bool push_or_drop(T value)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_ || queue_.size() >= capacity_) {
        return false;   // 满了就丢，绝不阻塞
    }
    queue_.push(std::move(value));
    not_empty_.notify_one();
    return true;
}
```

您盯着看就会发现 push_or_drop 压根没碰 wait，它是用不着 cv 的。它的动作就四下：加锁、查容量、入队、走人。那么哪些操作才真的需要 cv？只有条件不满足时愿意睡下去的那些。不想睡的，一把锁加一个 if 就到头了。这个对照比任何定义都更能帮咱们划清 cv 的地盘。背压策略的全景：丢、挤、阻塞怎么选，是 [工具箱那篇](06-sync-primitives-toolkit) 的地盘，这里咱们不抢戏。

## notify 要拿着锁去喊吗

到现在为止咱们写的代码都在做同一件事：把锁放下了再 notify。push 里的作用域块一结束就去叫人、close 里也是把解锁排在前面。您可能想问：这算规范吗？notify 到底要不要拿着锁？

```cpp
std::mutex m;
std::condition_variable cv;
std::string data;
bool ready = false;
bool processed = false;

void worker_thread()
{
    std::unique_lock<std::mutex> lk(m);
    cv.wait(lk, [] { return ready; });
    data += " after processing";
    processed = true;
    lk.unlock();
    cv.notify_one();   // 手动解锁放在 notify 前，免得被叫醒的线程睁眼就抢不到锁
}
```

咱们看示例里的 worker 加工完 data、置位 processed，解锁了以后才 notify，等的那个线程醒来的头一件事还是查自己的谓词。一进一出两个方向的条件、共用同一把 mutex 和同一个 cv，替它们分流的正是各自的谓词。

cppreference 给的示例姿势正是锁外通知，示例的注释写得直白：手动解锁放在 notify 前面，免得叫醒的线程一睁眼就去抢一把还攥在别人手里的锁。咱们接着看 notify_one 的 Notes，它说得更不客气：通知方不需要持有等待方的那把锁、持着锁通知，在没做优化的实现里反而是种劣化（pessimization）。被叫醒的线程一睁眼就得抢锁，锁还攥在通知方的手里，醒来的只好接着睡回去，这一趟也就白醒了，行话给它的名字是 hurry up and wait。

不过很多 pthread 实现认出了这样的局面，notify 的时候直接把等待线程从 cv 的队列挪到 mutex 的队列里、压根不真醒它，这就是大名鼎鼎的 wait-morphing，一场白醒被实现悄悄化解了。所以性能方向咱们不写死：两种排法在正确性上是等价的、谓词循环兜底，快慢就交给具体的实现和测量去裁决。

但持锁 notify 也有它必要的场合，Notes 紧接着就给了一个：需要精确调度事件的时候。举的例子非常极端：等待方的条件一满足就退出程序，退出的时候还会把那把 cv 一起析构掉。要是通知方解锁了之后还没轮到 notify，来了一次虚假唤醒，等待方查了谓词、收了工、cv 也跟着被析构，然后通知方的 notify 落在一个已经死掉的对象上。拿着锁 notify 的做法呢，通知就被排到了解锁以前，等待方就不可能抢在前面把对象拆掉了。这个队列走不到这么极端的场景，但咱们把 close() 与析构的时序安排明白，是同一个思路的工程版：全员已被通知，没有谁再来 wait 了，cv 才能够安全地析构。

## 存货的类型，队列也有它的要求

pop 里有一行是值得多看两眼的：`value = std::move(queue_.front())`。如果 T 的移动赋值会抛异常，这一行抛了，元素其实还在队列里，front() 给的是引用，后面的 `queue_`.pop() 还没执行到，下一位消费者取到的还是它。语义上不一定错、但这个边界您得知道。工程上更省心的做法，是咱们把要求立在编译期：

```cpp
static_assert(std::is_nothrow_move_constructible_v<T>,
              "T must be nothrow move constructible");
static_assert(std::is_nothrow_move_assignable_v<T>,
              "T must be nothrow move assignable");
```

int、std::string、std::unique_ptr 这些常用类型的移动操作都是 noexcept 的，日常几乎感觉不到这道闸的存在。真塞进来一个会抛的类型，编译期就把它拦下了，好过运行时在某个奇怪的路径上炸给您看。

wait 自己的异常行为，标准写得比很多人以为的要冷。裸 wait 是不抛异常的。谓词版的异常全部来自 pred 本身：pred 抛了，异常带着锁一起传播了出去，unique_lock 的析构会把锁放下，队列的状态没动过。而重新拿锁那一步要是抛了异常，标准给出的行为是调用 `std::terminate`，前面讲 wait 语义的小节里提过，锁倒是不会泄漏，程序直接就死掉了。这也是为什么咱们只写 `!queue_.empty()` 这一类的谓词，不往里面塞会抛的东西。

## 写 cv 前的核对清单

把本篇的纪律收拢成几句话，往后您每次写 cv、都可以拿它们对一遍：

- 等条件永远用谓词版的 wait，循环让库去转，条件让持锁的谓词去查。
- 共享变量哪怕声明成了 atomic，修改也要放进同一把 mutex 的临界区里，通知方与等待方靠它对上节奏。
- cv 与 mutex 一一配对，同一个 cv 上混用两把锁是未定义行为。
- notify 一般排在解锁之后，正确性上两种排法等价，性能交给具体的实现和测量去裁决。
- 关闭的套路是 `closed_` 进谓词、notify_all 喊醒两边、pop 后查空完成 drain，析构之前要保证人人都被通知过。

队列的完整代码已经归仓，您可以在配套代码目录里找到它，带着 static_assert 与全部的成员。练习咱们放在了下面，您动手练过才算数。

## 练习：轮到您动手了

### 练习 1：CountdownEvent 的等待与归零

实现一个 CountdownEvent：内部计数器的初始值为 N，等待的线程调用 wait() 阻塞到它归零，您再让别的线程调用 signal() 把计数减一。要求您的 wait() 用谓词版，signal() 里您得想清楚该用 notify_one 还是 notify_all，理由您也写进注释。提示：计数从 1 变成 0 的那一刻，所有 waiter 的条件同时成立。

### 练习 2：您亲手复现丢失唤醒

按本篇的时序写一个程序：worker 一进来就睡 200 毫秒，睡醒了再等 ready，主线程在 50 毫秒的时候置位并 notify。头一遍咱们拿裸 wait 去跑，看它是不是真的会永久挂住，第二遍咱们则换谓词版再跑。两种结局您各记一行输出，体会通知没有记性是什么意思。时序上您别手软：worker 在 wait 之前睡足 200 毫秒，主线程等个 50 毫秒就够了，交错就造出来了。

### 练习 3：drain 的压力测试

把 close 版 BoundedQueue 放进 3 生产者各 100 元素、2 消费者取到 kClosed 的场景，咱们用 atomic 计数核对，核对的总数必须是 300、一个都不能少。您试着把 close 挪到生产者 join 之前，观察丢数是怎么发生的。看清楚了再把顺序改回来。再跑一遍 TSan 的版本，确认报告是干净的。

## 下一步

咱们全程没碰过取消。有一个事实您得知道：`condition_variable` 的 wait 只有两个重载、裸版和谓词版，**没有接受 stop_token 的版本**。C++20 把带 stop_token 的 wait 放在了 `condition_variable_any` 上，那是个泛型的版本，什么锁都能配，而代价是可能更重。想让队列的 pop 在外部请求停止的时候立刻醒来，咱们得整体换用 cv_any，那是 [第 5 章 线程池](../ch05-future-task-threadpool/03-thread-pool) 的正源内容，worker 循环加 stop_token 的完整套路在那边展开。本篇的 BoundedQueue 靠 close 就能干净收场，够用了。

本篇造出来的组件，去处都不小气：第 5 章 的线程池拿它当任务队列，[第 7 章 的 Actor](../ch07-actor-channel/01-actor-model) 的邮箱也是同一个形状，那边的差异增量会在本篇的基础上展开。要是您的场景追着吞吐跑，无锁的 SPSC 与 MPSC 队列，在 [第 4 章 的 SPSC 与 MPSC 篇](../ch04-concurrent-data-structures/04-spsc-and-mpsc) 等着您。动手量更大的活儿，在 [exercises 的阻塞队列 Lab](../exercises/01-bounded-queue) 里等着您，它会带着本篇的队列走进多消费者压力与分片锁的取舍里。

> 💡 咱们把完整示例代码放在 [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP)，您可以访问 `code/volumn_codes/vol5/ch02-mutex-condition-sync/`。

## 参考资源

<ReferenceCard title="参考文献">
  <ReferenceItem
    :id="1"
    title="std::condition_variable"
    author="cppreference"
    url="https://en.cppreference.com/w/cpp/thread/condition_variable"
  />
  <ReferenceItem
    :id="2"
    title="std::condition_variable::wait"
    author="cppreference"
    url="https://en.cppreference.com/w/cpp/thread/condition_variable/wait"
  />
  <ReferenceItem
    :id="3"
    title="std::condition_variable::wait_for"
    author="cppreference"
    url="https://en.cppreference.com/w/cpp/thread/condition_variable/wait_for"
  />
  <ReferenceItem
    :id="4"
    title="std::condition_variable::notify_one"
    author="cppreference"
    url="https://en.cppreference.com/w/cpp/thread/condition_variable/notify_one"
  />
  <ReferenceItem
    :id="5"
    title="std::condition_variable::~condition_variable"
    author="cppreference"
    url="https://en.cppreference.com/w/cpp/thread/condition_variable/~condition_variable"
  />
  <ReferenceItem
    :id="6"
    title="Boost.Thread 同步数据结构（sync_queue 与 queue_op_status）"
    url="https://www.boost.org/doc/libs/release/doc/html/thread/sds.html"
  />
  <ReferenceItem
    :id="7"
    title="Spurious wake-ups in Win32 condition variables"
    author="Raymond Chen"
    url="https://devblogs.microsoft.com/oldnewthing/20180201-00/?p=97946"
  />
  <ReferenceItem
    :id="8"
    title="C++ Concurrency in Action, 2nd ed."
    author="Anthony Williams, Manning, 2019"
    :year="2019"
    url="https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition"
  />
</ReferenceCard>
