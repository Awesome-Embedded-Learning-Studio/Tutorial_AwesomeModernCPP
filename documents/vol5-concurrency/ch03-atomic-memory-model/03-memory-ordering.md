---
title: "内存序"
chapter: 3
order: 3
description: "配方篇登场：六种序各自担保什么、发布订阅怎么配对、seq_cst 独管的多标志一致场景，再到 x86 的指令价目表、ARM 对照与 SPSC 队列四个访问点的降级配方"
tags:
  - host
  - cpp-modern
  - advanced
  - atomic
  - memory_order
difficulty: advanced
platform: host
cpp_standard: [11, 20]
reading_time_minutes: 24
prerequisites:
  - "缓存与重排：机器真的会乱序"
  - "原子操作与 happens-before"
related:
  - "原子操作与 happens-before"
  - "缓存与重排：机器真的会乱序"
---

# 内存序

[上一篇](./02-atomics-and-happens-before.md)把公理立完了：sequenced-before 管的是线程内，synchronizes-with 管的是跨线程的交接，接力起来就成了 happens-before。法有了，配方却还没有发到您手里。您写并发代码的时候，手里真正攥着的是六种内存序，每一种能架出什么样的边、又要花什么样的指令代价，就是本篇要配齐的事。您也可以把本篇当一份选购手册读，哪个场景用哪一档、拿不准的时候默认什么，咱们都会给出答案，末了咱们还配了一张速查表。

物证咱们照旧备齐。示例 56 演的是配对形状，发布与订阅的两头怎么合起来才成边。示例 57 与[机器篇](./01-cache-and-reordering.md)的示例 51 成对，把中间那一档补上了：release/acquire 的配对在 SB 形状上照样出 `(0,0)`，它就是发布/获取管不住 StoreLoad 的实证。示例 58 量的是差价，看内存序的钱花在哪个操作上、又在哪个操作上不花。汇编那一路咱们备了两份，一份是代码仓里的现成工程，一份是本机刚生成的全家福对照。

路线咱们也交代清楚。六种序咱们过一遍地图，consume 咱们一句话送走，往后咱们就不理它了。然后咱们给 relaxed、release 与 acquire、acq_rel、seq_cst 逐个开小节，配到 seq_cst 的时候，咱们专门看它独管的场景，就是机器篇里 `(0,0)` 的老家。再往后咱们算价目表，看各种序在 x86 上编成什么指令、差价又有多少，ARM 的对照说明白送是平台给的。末了咱们拿 SPSC 队列的四个访问点练降级配方，收尾的时候给一张速查表。fence 与 atomic wait 不进本篇的正文，它们的正主是[下一篇](./04-fence-wait-and-patterns.md)。

本篇的实测数字全部带着复现命令。实测都在笔者自己的 WSL2 Arch Linux（内核 6.18、g++ 16.2.1、AMD Ryzen 7 9700X）上跑出来，完整的代码放在 `code/volumn_codes/vol5/ch03-atomic-memory-model/`，正文里咱们只贴关键的片段，读法的约定在[卷首](../)立着，咱们照它来。

## 六种序的地图

咱们把六种内存序摆进一张表。衡量的维度有两个：各自担保什么、各自能配在什么操作上。您扫一遍就有底了，后面每一种咱们单独开小节。

| 内存序 | 能配的操作 | 担保什么 |
| --- | --- | --- |
| relaxed | load、store、RMW | 原子性与修改全序内取值，次序一概不管 |
| consume | load | 依赖序，C++26 起弃用 |
| acquire | load、RMW 的读侧 | 排在它后面的访问不许挪到它前面 |
| release | store、RMW 的写侧 | 排在它前面的访问不许挪到它后面 |
| acq_rel | RMW | acquire 与 release 各占一半 |
| seq_cst | 全部 | acquire/release 的全部，外加全体共见的总顺序 S |

consume 咱们一句话送走。它的设想是只顺着数据依赖传播次序，[上一篇](./02-atomics-and-happens-before.md)交代过它的结局：主流的编译器从没有真正实现过它，从 C++11 起兑现的就一直是 acquire 的语义。C++26 的提案 P3475 把这层窗户纸正式捅破了，consume 判了弃用，与它配套的 `[[carries_dependency]]` 属性一并移出了标准，标准也干脆规定了按 acquire 兑现。您在新代码里不该再写它，本篇后面也不再提它了。

档位的读法咱们补一句，免得您把表看成六层楼梯。真正的档位是三级：底下住的是 relaxed，中间住着的是 acquire 与 release，顶上压着的是 seq_cst。acquire 与 release 其实算不得两档，它们是一对方向相反的半边，各自只管自己那一侧的搬运。acq_rel 也不是更高的一档，它是读改写把两个半边合在一条指令里的写法。配方的核心永远是配对：发布的一侧配订阅的一侧，边就架起来了。

为什么默认档挑了最强的 seq_cst？您回 DRF-SC 的担保想一遍：无竞争加默认序的程序整个住进顺序一致的世界，直觉就能当判据用了。默认给弱档的话，省下的指令钱会变成满地的合同陷阱，委员会是没这么选的。

## relaxed：只管这个变量自己的事

relaxed 担保的是单变量自身的原子性。一次 relaxed 的读或写不会被人看见中间状态，读到的值也一定是[上一篇](./02-atomics-and-happens-before.md)修改全序里真实写下去过的某个值，RMW 更是被要求在全序的队尾取最新。次序的事它一个字不提：这次访问与您程序里别的读写谁前谁后，全凭编译器与机器的心情。

它的正主岗位是计数与统计。请求数、错误数、序列号这一类的单变量，您要的只是数字本身对，不在乎它跟周围操作的相对位置，relaxed 就是最省的一档。示例 53 的全局最大值追踪从头到尾 relaxed，一百六十万次抽样跑下来漏掉的大数是零，靠的正是修改全序给 RMW 的队尾读取。咱们把上一篇的口径再念一遍：不背同步职责的单变量，relaxed 是恰好够用的一档。

计数器的样子咱们写成代码，两处的自增都明写着 relaxed：

```cpp
std::atomic<long> g_requests{0};
std::atomic<long> g_errors{0};

void handle_request(bool ok) {
    g_requests.fetch_add(1, std::memory_order_relaxed);  // 只数数，不背次序
    if (!ok) g_errors.fetch_add(1, std::memory_order_relaxed);
}
```

咱们让八条线程同时进来，最终的数也一个不差，因为谁也不需要看见谁的副作用，每次 `fetch_add` 各自落在全序里的一个位置上，加出来的总数就错不了。您要是在这里写 seq_cst，多付的钱是买不到任何东西的。还有一处 API 的细节值得您记：运算符重载是不收内存序实参的，`++counter` 与 `counter += n` 走的永远是 seq_cst，您想给计数降档，就得改写成 `fetch_add` 的显式调用。

反面的教训咱们已经验过两回。[机器篇](./01-cache-and-reordering.md)末尾的病历里，发布标志用的是 relaxed 的 store，x86 上看着总是对的，在合同的口径里它是带着 data race 的。[上一篇](./02-atomics-and-happens-before.md)练习 2 把自旋锁的三处全换成 relaxed，普通构建的输出照样全对，TSan 当场就把 counter 上的竞争报了出来。relaxed 本身其实没有做错什么，错的活是拿它干同步：它只管这个变量自己的事，您让它去担保别的变量的可见性，它是没有那个职权的。

## release 与 acquire：一对配成的发布与订阅

两个半边挡的方向是相反的，咱们把它们分开记。release 配的是写：本线程里排在这次 store 前面的所有访问，不许被排到它的后面。acquire 配的是读：本线程里排在这次 load 后面的所有访问，不许被排到它的前面。两个方向合起来的效果，上一篇的公理已经写好了：release 的写与读到该值的 acquire 读之间，synchronizes-with 就成立了，写方排在发布之前的全部效果，对读方发布之后的访问是全部可见的。

配对的条件咱们抠一遍字眼。边的成立要求读方真的读到写方发布的那个值，读到更晚的值也行，读到更早的值边就断了。所以光在一侧使劲是白费：发布的 store 用了 release，读的一侧还是 relaxed，读到了值也不成边。反过来读的一侧也一样，读的一侧用了 acquire，发布的一侧却是 relaxed 或者干脆是普通写，架起来的边同样一条都没有。配方永远是成对的：一端是 release 的写，另一端是 acquire 的读，中间隔着的正是那个原子变量。

### 消息传递：最小也最正的一对

咱们看形状。生产者的载荷写进普通变量，再用一次 release 的 store 发布标志，您在消费者那头用 acquire 读到标志，然后碰的才是载荷。示例 56 把它跑了五十万轮，还带着一个对照的相位，把发布的那一步换成 relaxed。关键的骨架咱们贴出来：

```cpp
struct Channel {
    alignas(64) std::atomic<long> flag{0};
    alignas(64) long payload = 0;   // 普通变量，安全性全靠配对
};

// 生产者每轮：写完载荷，才发布轮号（对照相位把 release 换成 relaxed）
ch.payload = r;
ch.flag.store(r, std::memory_order_release);
while (ch.flag.load(std::memory_order_acquire) != -r) {}  // 等消费者收卷

// 消费者每轮：acquire 读到轮号 r，才回读载荷
while ((observed = ch.flag.load(std::memory_order_acquire)) != r) {}
if (ch.payload != r) ++stale;
ch.flag.store(-r, std::memory_order_release);             // 收卷的回执
```

骨架里藏着的配对有两对，咱们都指给您看。头一对是正主：`flag` 的 release 发布对 acquire 接收，五十万轮的载荷可见性全押在它身上。第二对管的是外壳：消费者把轮号的负值当回执写回去，用的是 release，生产者等它用的就是 acquire，回合的边界就是拿本篇的正主形状划出来的，与机器篇 litmus 里的 `gate` 与 `done` 同款。您以后读并发代码，只要看见 release 的写与读到它的 acquire 读隔着同一个原子变量，您碰见的就是一次交接。

::: details 跟一轮消息传递走完握手

咱们把生产者与消费者的第 r 轮按时间摆开，四步是这样走的：

1. 生产者写载荷：`ch.payload = r` 落进普通变量。
2. 生产者发布：`flag.store(r, release)`，r 轮的载荷从这一刻起对新读者担保可见。
3. 消费者接收：`flag.load(acquire)` 读到了 r，边在这一步落地。
4. 消费者读载荷、交回执：`payload` 读到的一定是 r，`flag.store(-r, release)` 把收卷的消息送回生产者。

您注意两头各执一半：出货靠的是第 2 步的 release，入货靠的是第 3 步的 acquire。咱们少掉任何一半，中间的普通变量就没人担保了。

:::

骨架里的 `alignas(64)` 咱们也交代一句，它让 `flag` 与 `payload` 隔进了不同的缓存行。`flag` 是两头都要写的变量，它的行注定来回颠簸，`payload` 隔在别的行里就不用跟着挨失效。伪共享的完整实测咱们留给[第 4 章](../ch04-concurrent-data-structures/)。

<OnlineCompilerDemo
  title="动手验证：消息传递的配对发布与不配对的对照"
  source-path="code/examples/vol5/56_message_passing_orders.cpp"
  description="五十万轮消息传递：生产者写普通变量 payload，再发布原子轮号 flag，消费者 acquire 读到轮号后回读 payload，数旧值次数。配对相位用 release 发布，对照相位只把发布换成 relaxed。两行 stale 在 x86 上都是 0（StoreStore 是硬件白送的），读到 0 不等于写法合法，判据在正文。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
/>

笔者在同一台 WSL2 上的实测（`-O2 -std=c++20`）：

```text
rounds per phase: 500000
release/acquire pairing, stale payload: 0
relaxed store (no pairing), stale payload: 0
```

两行输出的都是 0，咱们得把诚实话说明白。配对相位的 0 是公理给的，给它背书的是合同。对照相位的 0 靠的就不是合同了：x86 的 TSO 把 StoreStore 硬件白送，载荷与标志两个写的落地次序，机器替您保住了，旧值在 x86 上是量不出来的。这正是[机器篇](./01-cache-and-reordering.md)立过的那道辨析：平台送的与合同保的是两回事。您把同一份程序搬到 ARM，或者按上一篇练习 2 的流程拿 TSan 编一遍，对照相位的竞争就会现形。本篇再往下您还会看到，量不出差价的这件事本身，也不该影响您选档。

自旋等新值的循环为什么一定能等到？模型里靠的是标准的进度条款，措辞是建议而非担保，硬件上靠的则是一致性的写传播，机器篇讲过写最终是会传播的，所以您在循环里等的新值迟早会到眼前。

配对的关系还能接力。咱们让 A 发布、B 接收，B 处理完了再发布、C 再接收，happens-before 的传递性把 A 的效果一路送到 C，中间人什么额外的活都不用做。示例 56 的回执就是半个接力：生产者对下一轮载荷的写，排在了等到回执之后，消费者这一轮的读载荷又排在回执之前，两头隔着回执配成的边各归各位，所以生产者永远不会覆盖一个还没被读走的载荷。DCLP 的四步配方、引用计数的减法，用的都是同一条传递链，判决在上一篇判过了，咱们不重开庭。

有的场合里，您想在不方便换内存序的操作旁边单独加一道屏障，那是 fence 的活，手法咱们留给[等待与模式篇](./04-fence-wait-and-patterns.md)。

## acq_rel：读改写把两个半边合起来

读改写的操作一身兼二任，读的一侧可以带 acquire，写的一侧可以带 release，acq_rel 的意思就是两个都要。合法的搭配咱们说死：load 认的只有 relaxed、acquire、seq_cst 三档，store 认的只有 relaxed、release、seq_cst 三档，RMW 是五档都认的，拿错档位的行为是未定义的，多数标准库在调试构建里会直接断言拦下您。

用场的例子上一篇判过一案：引用计数的 `fetch_sub` 用 acq_rel，减到零的那次既要看见前面所有持有者的访问，又要把减完的状态排到析构之前。咱们再补一个帮您省钱的反例。示例 54 自旋锁的 `test_and_set` 只用 acquire，为什么不是 acq_rel？因为抢锁这一次的读改写没有什么要发布的，发布的活归放锁人 `clear(release)`。您给它配上 acq_rel，多付的那半个 release 什么边也架不起来。所以 acq_rel 的身份谈不上更高级，它只是更贵的一档，两个半边都有活干的时候才配它。

咱们把引用计数的判决落成代码，减法的两个半边一眼就能看清：

```cpp
void release_ref() {
    // acq_rel：acquire 那一半接住前面持有者的访问，release 那一半交出自己的减完
    if (ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        delete this;   // 减到零的线程看到的对象是完整的
    }
}
```

增计数那一侧为什么 relaxed 就够，上一篇的判决也提过：加一只登记一个新持有者，咱们顺着它看不见任何要发布的访问。

[上一篇](./02-atomics-and-happens-before.md)留了一道题：CAS 的内存序实参有 success 与 failure 两个，该怎么配？现在配法可以给全了。failure 管的是失败路径上的那一次读，所以它带不得 release 与 acq_rel，也不许用强过 success 的档位，违者的行为是未定义的。单实参的版本会自动降档：您传 acq_rel，failure 走的是 acquire，您传 release，failure 走的是 relaxed。惯用的配法咱们摆两种。有载荷要过手的循环里，您给 success 配 acq_rel，failure 配的是 acquire，成了就是一次完整的接力。纯计数没有载荷的场合，两个都写 relaxed 就够了，示例 53 就是这么写的。您守住一条就够：failure 永远不高于 success，其余的按载荷有没有来挑。两种惯用配法的样子，咱们也贴成代码：

```cpp
// 有载荷的 CAS 循环：成了就是一次接力，败了接着读最新的值重试
head.compare_exchange_weak(expected, desired,
                           std::memory_order_acq_rel,   // success
                           std::memory_order_acquire);  // failure

// 纯计数的 CAS：两个实参都 relaxed（示例 53 的写法）
g_max.compare_exchange_weak(observed, candidate,
                            std::memory_order_relaxed,
                            std::memory_order_relaxed);
```

## seq_cst：全体共见的总顺序

seq_cst 在 acquire/release 之上又给咱们添了一条：程序里所有 seq_cst 的操作，全部要排进同一条叫 S 的总顺序，全体线程看到的是同一份。

> 术语卡咱们立一张：**全序 S**。它只收 seq_cst 的操作，排成的是一条全体线程共见的总顺序，每个线程自己的操作在里面保着序。上一篇的族谱里有一类更严的边，它的岗位就是给 S 立约束。S 的分量不在单变量的次序，修改全序早就管了那一层，它管的是把多个变量的看法统一。

哪些场景非它不可？判据一句话就说清了。您要的若只是看见对方的载荷，那 acquire/release 就够了。您要的若是所有线程对几个标志的次序持同一份看法，那就只有 seq_cst 了。双标志互查的 SB 形状是头一个，它也是机器篇里 `(0,0)` 的老家。

### SB 形状：配对管不住 StoreLoad

形状咱们从机器篇搬过来：两个线程各自写自己的标志，写完就去读对方的。示例 57 在示例 51 的两档中间补上了第三档，做的是三相对拍：relaxed、release/acquire 配对、seq_cst，每档跑的都是二十万轮。

外壳的回合边界您在机器篇已经认过了：`gate` 与 `done` 自己是 release/acquire 的配对，只管划清每一轮的起止，轮内被测的两句它是不管的。您想复核就回[机器篇](./01-cache-and-reordering.md)看那套外壳的走读。

您可能要问：release/acquire 不是中档吗，配了真配对，`(0,0)` 不就该清零了？咱们把边摆上桌，笔算给您看。

::: details 把 release/acquire 阶段的边摆开

四个访问点咱们记成 S1 与 L1（线程 1 写 x、读 y），S2 与 L2 的记法同理（线程 2 写 y、读 x）。S1 是 release 的写，L2 是 acquire 的读，L2 读到 1 的话，S1 与 L2 之间就成了一条边。S2 与 L1 的道理相同。您盯着边看就发现问题了：边全是横着的，从一个线程的写跨到另一个线程的读。而 `(0,0)` 的病灶是竖着的，说的是同一个线程里写自己的标志与读对方的标志这两步之间的换序，StoreLoad 管的就是它。横着的边对竖着的换序一次都管不着。

咱们再看谁管得住竖线：只有 S。S1 与 L1 都进了 S，程序序要求 S1 排在 L1 的前面，S2 同理排在 L2 的前面，再添上读到 0 的两条边，四条边就接成了一个圈，矛盾也就藏不住了。咱们在 S 里把它原样复用，您一行都不用改。

:::

<OnlineCompilerDemo
  title="动手验证：SB 形状三相对拍，发布/获取管不住 StoreLoad"
  source-path="code/examples/vol5/57_seq_cst_sb_litmus.cpp"
  description="与示例 51 同款的写完自己的就去读对方的，三相对拍：relaxed、release/acquire 配对、seq_cst。前两相 (0,0) 都非零，seq_cst 恒为 0。非零两行的计数逐轮波动很大，比例当不了常数，前两相非零、末相恒零的模式稳定。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
/>

笔者在同一台 WSL2 上的实测：

```text
rounds per phase: 200000
relaxed          (0,0) outcomes: 59590
release/acquire  (0,0) outcomes: 25383
seq_cst          (0,0) outcomes: 0
```

三行咱们挨着读。relaxed 的非零是意料之中，机器篇已经量过它了。release/acquire 的 25383 才是这一轮的主角：中档配上了真配对，该出的 `(0,0)` 照样出了，发布/获取管不住 StoreLoad 的这件事，撑腰的从推断换成了实测，二十万轮里实打实地出了两万多次。seq_cst 的清零靠的正是 S，竖线被它按住了。口径咱们照旧带上：非零两行的数字逐轮波动得厉害，笔者复跑过多次，从两三万到十九万出头的数都见过，比例是当不了常数的，前两相非零、末相恒零的模式倒是稳的。

### 多标志一致：另一个只有 S 管得住的形状

SB 之外还有一类形状也是 seq_cst 独管的，咱们用四行示意代码看个形状就好，本机跑不出它的坏结局，原因笔者随后就讲：

```text
// 写者 W1: x.store(1)                    写者 W2: y.store(1)
// 读者 R1: r1 = x.load(); r2 = y.load();   // 先 x 后 y
// 读者 R2: r3 = y.load(); r4 = x.load();   // 先 y 后 x
```

坏结局是 R1 看到 x 已经变了而 y 还是 0，R2 同时看到 y 变了而 x 还是 0，两个读者对两个写的次序各执一词。acquire/release 对它是放行的：每个读者与每个写者之间的对都配得上，可没有任何条款要求两个读者的看法一致。每个读者的两次读之间倒是有 acquire 的挡，挡的是自己后面的访问，跨不到另一个读者的身上。seq_cst 的 S 把看法统一了，两个写的次序在 S 里有唯一的一份，全体线程认同的也只能是它。文献里给这个形状起的名字是 IRIW，中文的说法是独立读独立写。它在 x86 上连 relaxed 的版本都跑不出坏结局，因为 TSO 本来就给了所有写一条全局的定序，于是它成了平台赠品的又一个样本：合同允许的分歧，x86 替您挡着，别的机器可就未必挡了。

判据咱们收拢成两句。发布的活配 acquire/release，看法一致的活配 seq_cst。Dekker 互斥那一类依赖双标志互查的算法，它出生的世界就是顺序一致的，机器篇讲过它的来历，您手写同款形状的时候别犹豫，全上 seq_cst 就对了。

## 指令落差：x86 的价目表

配方讲完了，咱们来算钱。同一条语句换上不同的内存序，编出来的指令差多少？答案在 x86 上出奇地偏科：差价几乎全压在一种操作的身上。咱们拿汇编与计时两头验。

### store 的三档：mov、mov、xchg

现成的工程在代码仓 `code/volumn_codes/vol5/ch03-atomic-memory-model/memory_order_store_x86.cpp`，三个函数各做一个档位的 store。命令咱们在仓库根目录跑：

```bash
g++ -std=c++20 -O2 -S \
  -o /tmp/memory_order_store_x86.s \
  code/volumn_codes/vol5/ch03-atomic-memory-model/memory_order_store_x86.cpp
grep -E "^_Z|movl|xchg" /tmp/memory_order_store_x86.s | grep -v cfi
```

笔者机上跑出来的结果如下，指令是逐条照贴的：

```text
_Z13store_relaxedi:
	movl	%edi, x(%rip)
_Z13store_releasei:
	movl	%edi, x(%rip)
_Z13store_seq_csti:
	xchgl	x(%rip), %edi
```

您看输出：relaxed 与 release 生成的是同一条 `movl`，seq_cst 换成了 `xchgl`。xchg 对内存操作数自带隐式的锁定，连 lock 前缀都省了，Intel 的手册里写得分明。实现选哪条指令有它的自由，mov 加一道 mfence 的写法效果等价，GCC 在这里选的是 xchg。它干的正是机器篇讲过的那件事：把 store buffer 排空了，再放行后面的指令，StoreLoad 的窗口就是这么关上的。机器篇留下的那个问题，seq_cst 凭什么把 `(0,0)` 清零，答案的机关就是它。

::: details xchg 排空的时候发生了什么

咱们让一次 seq_cst 的 store 走一遍，看它跟 mov 的差别在哪几步：

1. store buffer 里可能还压着前面几条没落地的写。
2. xchg 要求对目标地址的独占，它得等 buffer 里排在前面的写全部进了缓存系统。
3. 排空完成，xchg 自己的换入才对外可见，后面的指令这才放行。
4. 排在 xchg 后面的 load 从此读不到旧值，StoreLoad 的窗口关上了。

mov 是没有这四步的，写进 buffer 就算完了，所以便宜。您回机器篇那个 `(0,0)` 的时间线对照一遍，排空卡的正是核 A、核 B 各自私有的 buffer。

:::

### 全家福探针：RMW 与 load 的价

咱们把探针扩成全家福：fetch_add 的四档、store 的三档、load 的三档，外加一个用返回值的 fetch_add。源码只有下面的几行，您存成 `/tmp/mo_orders_x86.cpp` 就能复跑：

```cpp
#include <atomic>

std::atomic<long long> x{0};

void fetch_relaxed() { x.fetch_add(1, std::memory_order_relaxed); }
void fetch_release() { x.fetch_add(1, std::memory_order_release); }
void fetch_acq_rel() { x.fetch_add(1, std::memory_order_acq_rel); }
void fetch_seq_cst() { x.fetch_add(1, std::memory_order_seq_cst); }
long long fetch_use_old() { return x.fetch_add(1, std::memory_order_relaxed); }

void store_relaxed(long long v) { x.store(v, std::memory_order_relaxed); }
void store_release(long long v) { x.store(v, std::memory_order_release); }
void store_seq_cst(long long v) { x.store(v, std::memory_order_seq_cst); }

long long load_relaxed() { return x.load(std::memory_order_relaxed); }
long long load_acquire() { return x.load(std::memory_order_acquire); }
long long load_seq_cst() { return x.load(std::memory_order_seq_cst); }
```

编译与提取的命令是这样的，函数名带着编译器的编号，您对着读就行：

```bash
g++ -std=c++20 -O2 -S -o /tmp/mo_orders_x86.s /tmp/mo_orders_x86.cpp
awk '/^_Z/{name=$0} /lock addq|lock xaddq|movq|xchgq/{print name"  "$0}' \
  /tmp/mo_orders_x86.s
```

笔者机上跑出来的结果，左边列的是函数名，右边是它生成的指令：

```text
_Z13fetch_relaxedv:  	lock addq	$1, x(%rip)
_Z13fetch_releasev:  	lock addq	$1, x(%rip)
_Z13fetch_acq_relv:  	lock addq	$1, x(%rip)
_Z13fetch_seq_cstv:  	lock addq	$1, x(%rip)
_Z13fetch_use_oldv:  	lock xaddq	%rax, x(%rip)
_Z13store_relaxedx:  	movq	%rdi, x(%rip)
_Z13store_releasex:  	movq	%rdi, x(%rip)
_Z13store_seq_cstx:  	xchgq	x(%rip), %rdi
_Z12load_relaxedv:   	movq	x(%rip), %rax
_Z12load_acquirev:   	movq	x(%rip), %rax
_Z12load_seq_cstv:   	movq	x(%rip), %rax
```

表咱们竖着读三列。fetch_add 的四档是一个价，编出来的全是 `lock addq`。lock 前缀要求指令独占所在的缓存行，全机同一时刻能动它的只有一个核，这笔钱是 relaxed 也躲不掉的，所以内存序在 RMW 上不加价，原因是 lock 已经把全向的次序都买下来了。用返回值的那一行换成了 `lock xaddq`，指令换了，故事倒是没换，四档的价还是一样的。store 的三档里只有 seq_cst 换了指令，道理前面讲过了。load 的三档全是一条 `movq`，x86 的 load 本来就不越过别的访问，机器篇换序表里 LoadLoad 与 LoadStore 两栏写的都是不换，所以 acquire 与 seq_cst 的读在 x86 上是白给的。贵的永远是落地：写要进了缓存系统才谈得上次序，读的活儿只是取数。

### 代价钻头：拿计时对拍汇编

汇编的说法是差价在 store，咱们再拿计时验一遍。示例 58 分的是两场：八线程各两千万次 fetch_add，再加单线程五千万次的 store，每场都跑 relaxed 与 seq_cst 的两档。

<OnlineCompilerDemo
  title="动手验证：内存序的差价在 store，不在 fetch_add"
  source-path="code/examples/vol5/58_order_cost_drill.cpp"
  description="八线程各两千万次 fetch_add，加五千万次单线程 store。fetch_add 的 relaxed 与 seq_cst 同价（都编成 lock addq），store 的 seq_cst（xchg）比 relaxed（mov）贵一个量级上下。耗时逐次波动，量级稳定。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
/>

笔者在同一台 WSL2 上的实测：

```text
fetch_add relaxed counter: 160000000  (6 ns/add)
fetch_add seq_cst counter: 160000000  (6 ns/add)
store relaxed  store x50000000: 6223999 ns  (0.12448 ns/store)
store seq_cst  store x50000000: 184249285 ns  (3.68499 ns/store)
```

四行咱们对着汇编读。fetch_add 的两档都是 6 ns，RMW 无差价的说法在计时里兑现了，lock 前缀的钱两家一起付。不过别把 6 ns 当成指令的身价，八条线程抢的是同一个缓存行，这个数的大头是独占权的来回，两档是一起挨的，所以照样分不出差价。store 的 relaxed 一条摊下来只有 0.12448 ns、比一个时钟周期还要便宜，这合理吗？合理的，因为写的头一站是 store buffer，五千万次写的又是同一个地址，后面的写把前面的直接覆盖掉，缓冲区还会把同地址的写合并掉，机器码里的 `movq` 放开流水线跑，出来的就是这样的价。seq_cst 的 store 摊到 3.68499 ns，差出了约 30 倍，每一条 `xchgq` 等的都是 buffer 排空，这笔是躲不掉的实价。口径咱们也带上：耗时的数字逐次波动，fetch_add 的两档笔者复跑时在 5 与 6 之间摇摆过，store 的倍数在 30 上下浮过，量级倒是从没变过。

您可能还惦记一件事：全用 seq_cst，程序是不是就慢了？价目表替您答了：在 x86 上真付钱的只有 store，load 与 fetch_add 是白给的，多数程序的写远少于读，全 seq_cst 的代价常常比您想的低。到了 ARM 上代价就真的来了，带档的访存是不同的指令，咱们在 ARM 对照一节把口径给全。

### ARM 对照：白送是平台给的

看完 x86 的价目表，您容易得出一个危险的印象：acquire/release 是白给的。这印象在 x86 上是成立的，出了 x86 它就立不住了。机器篇换序表的 ARM 一列里，四种换法写的都是可能，硬件是不白送的，缺的次序就得由编译器补指令：带 acquire 的读与带 release 的写，在 AArch64 的公开映射里用的是带标签的 ldar 与 stlr，relaxed 用的是普通的 ldr 与 str，它们是不同的指令，全序与围栏的事还有 dmb 伺候。笔者本机没有 ARM，这一段的内容来自编译器的公开文档，不是笔者实测的，您手头有 ARM 设备的话，上面的探针再 `-S` 一遍就能对出各自的指令表。

所以机器篇那句平台送的与合同保的是两回事，在价目表上有了第二层的读法：白送 acquire/release 的是 x86，送礼的是 TSO，语言的条款里可没写这样的承诺。您因为在 x86 上量不出差价，就把该配对的地方写成 relaxed，省下的钱到了 ARM 上会连本带利地赔回去，赔进去的还未必是钱，更可能赔的是正确性。

## 降级配方：拿 SPSC 队列练手

配方齐了，咱们拿一个真实的结构练手。单生产者单消费者的环形队列，实现它是第 4 章的活，练习体系的 Lab 2 也拿它当主菜，本篇只看它的内存序怎么配。结构里的下标有两个：tail 记的是生产者写到哪儿了，head 记的是消费者读到哪儿了，生产者独占 tail 的写，消费者独占 head 的写，跨线程读的只是对方的下标。访问点的数目正好是四个：两个写、两个读。

配方咱们按发布的载荷来配，每一条的判词就写在旁边：

- 生产者写完槽里的数据，再执行 `tail.store(新值, release)`。发布的载荷就是槽里的普通数据，release 担保数据的落地排在下标可见之前。
- 消费者顺着读到的 tail 去碰槽，这一次的 load 用 acquire。边就从读到的值上架，槽里的数据才担保是新的。
- 消费者读完槽，再执行 `head.store(新值, release)`。发布的载荷是槽用完了这件事，它担保读槽的动作排在归还可见之前。
- 生产者顺着读到的 head 判断哪些槽能重用，这一次的 load 用 acquire。边架起来之后，消费者的读才会被排到生产者的重写之前，槽位的重用才不出竞争。

落成代码的时候，咱们把四个访问点与档位一起写成下面几行：

```cpp
// 生产者这一侧：独占 tail 的写
buf[tail % kCap] = item;                                // 普通写：载荷
tail.store(tail + 1, std::memory_order_release);        // 发布槽里的数据

// 消费者这一侧：独占 head 的写
auto t = tail.load(std::memory_order_acquire);          // 顺着读到的值碰槽
auto v = buf[head_local % kCap];                        // 槽里的数据这时是担保新的
head.store(head_local + 1, std::memory_order_release);  // 发布槽用完了

// 生产者回头读 head：判断哪些槽能重用
auto h = head.load(std::memory_order_acquire);          // 边架好，重用才安全
```

咱们顺着代码把四条边对一遍：tail 的写配的是对侧的读，发布的与接收的分别是槽里的数据和空出来的槽位，架边的两次访问都能一一指出来。您以后审别人的降级补丁，拿的就是这一步：每处 relaxed 都要能答出为什么它不背载荷，答不出的就是白省。

消费者对槽的读与生产者的重写，中间的链条咱们把三段写出来：

```text
(C) buf[head] 的普通读 ── sequenced-before ──> (D) head.store(新值, release)
                                                      │
                                                      │ synchronizes-with
                                                      v
(F) buf[tail] 的重写 <── sequenced-before ── (E) 生产者 head.load(acquire) 读到新值
```

咱们把 (C) 到 (F) 接起来：消费者的读 happens-before 生产者的重写，槽位换手就干净了。head 的四个点如此，tail 的四个点把角色对调一遍同理。您审 SPSC 队列的内存序，审的就是 head 与 tail 的两条链。

这套配方的边界咱们也画一下。单生产者单消费者的前提给了咱们独占写、对侧读的干净分工。生产者的数量一多，下标就成了共享的，四个访问点就换成了 CAS 链，配方的形状也随之变了。多条生产者、多条消费者、带阻塞等待的队列，咱们全都留给[第 4 章](../ch04-concurrent-data-structures/)，等待要省 CPU 的活儿归[等待与模式篇](./04-fence-wait-and-patterns.md)。

能降档的地方有两个，咱们照实说。头一处是打量式的轮询。队列空转、满转的时候，您只读下标判断有没有活干，不顺着读到的值去碰任何普通数据，这样的读用 relaxed 就够了。示例 54 自旋锁的外层 `test` 就是同款的姿势：只读地打量用 relaxed，真正要架边的抢与放才用带档的。另一处更省：自己这一侧的下标根本不用反复读原子变量，写它的只有您自己，值在本地存一份就行了，连原子读都省了。DCLP 门内的二查用 relaxed，判据是同源的：边已经被 mutex 架好了，再多花的那道 acquire 就是白花。

总纲咱们收成一句：release 挂在发布普通数据的那次写上，acquire 挂在顺着读到的值去碰普通数据的那次读上，只碰原子变量自己、不背任何载荷的访问，配 relaxed 就够了。四个访问点之外您再遇到别的结构，您拿这一句去套，基本是不会配错的。队列本体的实现与槽位重用的细节，咱们交给[第 4 章](../ch04-concurrent-data-structures/)，动手的活儿在[练习体系](../exercises/)的 Lab 2 等着您。

## 挑选口味速查表

咱们把全篇收拢成一张表，您贴在手边照着挑。

| 场景 | 配方 |
| --- | --- |
| 单变量的计数、统计、序列号 | relaxed |
| 跨线程发布一批数据（消息传递、DCLP 快查） | 写侧 release，读侧 acquire，读到值才算配对 |
| 读改写的两头都有载荷要担保（引用计数减） | acq_rel |
| 多个标志要全体线程看法一致（双标志互查、IRIW） | seq_cst |
| 拿不准 | seq_cst，默认档就是它 |

默认档的来历咱们也交底：您不写内存序实参的时候，atomic 的全部操作都是 seq_cst，这是标准替您挑的安全答案。工程上的口味也就一句：从 seq_cst 起步，量出了原子操作的热点再降档，降档的时候要能说出每条边架在哪里，说不出口的地方就不许降。您在本篇练的正是说出边在哪里的本事，速查表只是把常问的场景提前答了。降档的前提还有一条：TSan 报告干净的程序才有资格谈降档，程序里带着竞争的时候去降档，降了也是白忙。

## 练习

三道题咱们照旧排梯度：标边、下预言、量差价。建议您全做。

### 练习 1：给配对阶段标边，再改一档实测

拿示例 57 的 release/acquire 相位，把一轮里可能成边的那些访问对全部列出来，注明每条边成立需要的条件，也就是读到了谁的值。然后咱们做一步变体，您把模板实参改成 store 用 release，load 用 seq_cst 的组合。等您跑完二十万轮，`(0,0)` 还出不出？预期的答案是照出。判据的两头都通：模型里 release 的 store 进不了 S，竖线还是没人管的，机器上排空的 xchg 挂在 seq_cst 的 store 身上，load 侧升了档编出来的仍是 mov，store buffer 的滞留也照旧。笔者在本机跑过这个变体，两次跑出来的数分别是 4475 与 180898。反过来的组合是把 store 升到 seq_cst、load 降回 relaxed，x86 上是恒零的，您拿 xchg 的排空解释一遍。混合档在标准条款里的边界本篇不细抠，您写代码认全 seq_cst 的口径就不会掉进缝里。

### 练习 2：给 consume 档下汇编预言

咱们给全家福探针再加一个函数，加的是 consume 档的 load。动笔之前您下个预言，猜猜它生成的是哪条指令，编译完了再对答案。预期的答案是与 acquire 同价，编出来的正是 movq。GCC 把它按 acquire 处理的做法从 C++11 年代起就没变过，P3475 判它弃用的提案，不过是把这层现状写进了标准。咱们再进一问，seq_cst 的 load 为什么在 x86 上不用 xchg？您回机器篇的换序表看 LoadLoad 与 LoadStore 两栏，答案就在表的两栏里。

### 练习 3：给代价钻头补两档

示例 58 只跑了 store 的两档，您给它补两档计时：store 的 release，再加一个单线程五千万次 load 的 drill，跑的是 acquire 一档。写代码之前把预期的差价记下来，跑完了再对答案。预期的答案：store 的 release 与 relaxed 同价，汇编里编给两档的就是同一条 movq。load 的 acquire 也同价，靠的也是同一条指令。x86 上中档是白给的，本篇 ARM 一节的口径您再念一遍，您就知道白送的边界在哪里。

## 本篇收进包里的东西

- 六种序的地图：relaxed 在底，acquire 与 release 居中，seq_cst 在顶，acq_rel 是读改写把两个半边合一，consume 已被 C++26 判了弃用。
- relaxed 只管变量自己的事：原子性加修改全序取值，计数与统计的正主，拿它做同步就是机器篇的病历。
- 配对形状：一头 release、一头 acquire，中间一个原子变量，读到值边才成立。接力靠 happens-before 的传递，不另花钱。
- acq_rel 配两头有活的读改写。CAS 的 failure 不得强于 success、不得带 release 侧，单实参的版本会自动降档。
- seq_cst 独管多标志一致：SB 上配好对的 release/acquire 照出 `(0,0)`，seq_cst 恒零，IRIW 一类同样只有 S 能统一看法。
- 指令落差：RMW 四档同价 lock addq，store 只在 seq_cst 换 xchg，load 三档同价 movq。白送是 x86 平台的赠品。
- 降级配方：release 挂发布普通数据的写，acquire 挂顺着值碰普通数据的读，打量式的轮询与自家下标可以省。

## 下一步

配方的活儿本篇交了。fence 这套手动挡与 atomic wait 的等待模式，它们的正主是[等待与模式篇](./04-fence-wait-and-patterns.md)。队列本体与无锁结构的实现归[第 4 章](../ch04-concurrent-data-structures/)，练习体系的 Lab 2 等着您把四个访问点的配方亲手用上去。公理的线您想回看，[原子操作与 happens-before](./02-atomics-and-happens-before.md) 随时都在老地方等您。机器的三种自由，[缓存与重排](./01-cache-and-reordering.md)留了现场。全卷的走向见[卷地图](../)。

> 💡 咱们把完整示例代码放在 [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP)，您可以访问 `code/volumn_codes/vol5/ch03-atomic-memory-model/`。

## 参考资源

<ReferenceCard title="参考文献">
  <ReferenceItem
    :id="1"
    author="C++ 标准草案"
    title="[atomics.order] 内存序条款"
    url="https://eel.is/c++draft/atomics.order"
    chapter="六种序的正式定义、全序 S、load 与 store 各认哪些档"
  />
  <ReferenceItem
    :id="2"
    author="cppreference"
    title="std::memory_order"
    url="https://en.cppreference.com/w/cpp/atomic/memory_order"
    chapter="六种序的展开讲法与 consume 的弃用注记"
  />
  <ReferenceItem
    :id="3"
    author="cppreference"
    title="std::atomic::compare_exchange_weak / strong"
    url="https://en.cppreference.com/w/cpp/atomic/atomic/compare_exchange"
    chapter="success 与 failure 的配法边界，failure 强于 success 是未定义行为"
  />
  <ReferenceItem
    :id="4"
    author="Hans-J. Boehm 等"
    title="P3475: Defang and deprecate memory_order::consume"
    :year="2025"
    url="https://wg21.link/P3475"
    chapter="consume 判弃用与 carries_dependency 移出的提案，并入 C++26 工作稿"
  />
  <ReferenceItem
    :id="5"
    author="Intel"
    title="Intel 64 and IA-32 Architectures Software Developer's Manual（Vol. 2A XCHG 与 Vol. 3A §8.2 Memory Ordering）"
    url="https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html"
    chapter="xchg 对内存操作数的隐式锁定，x86 内存序的官方文本"
  />
  <ReferenceItem
    :id="6"
    author="LLVM"
    title="Atomic Instructions and Concurrency Guide"
    url="https://llvm.org/docs/Atomics.html"
    chapter="各架构的原子代码生成口径，弱序架构要用围栏或带标签指令"
  />
  <ReferenceItem
    :id="7"
    author="Jeff Preshing"
    title="Acquire and Release Semantics"
    :year="2013"
    url="https://preshing.com/20130922/acquire-and-release-semantics/"
    chapter="两个半边各自挡哪个方向的展开讲法"
  />
  <ReferenceItem
    :id="8"
    author="Anthony Williams"
    title="C++ Concurrency in Action, 2nd ed"
    publisher="Manning"
    :year="2019"
    url="https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition"
    chapter="第 5 章内存模型，与本篇同源的展开"
  />
  <ReferenceItem
    :id="9"
    author="Herb Sutter"
    title="atomic&lt;&gt; Weapons, CppCon 2013"
    url="https://www.youtube.com/watch?v=A8e5OjAVHEA"
    chapter="seq_cst 与 acquire/release 分野的两场长讲"
  />
</ReferenceCard>
