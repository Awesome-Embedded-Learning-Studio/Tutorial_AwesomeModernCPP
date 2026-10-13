---
title: "io_uring:把等待 I/O 变成收割完成事件"
description: "等 I/O 能不能变成收完成事件:本篇实测裸系统调用建环(编号 425/426/427 摆上明面,三段 mmap 出来的就是 /proc/self/maps 里两条 anon_inode:[io_uring] 映射,手工填 SQE、放尾指针、一次 enter 收回 user_data=0xC0FFEE)、liburing 2.15 的工程姿势(64 个 4KiB 读一次 submit 一次等齐,read(2) 要 64 次系统调用,io_uring 是 submit 1 加 wait 1,其余 63 个收割全是用户态 peek)、链式请求的顺序与失败传播(read、write、fsync 三个 CQE 按序 4096/4096/0,链头坏 fd 变 -EBADF 而下游 -ECANCELED,目标文件停在链 A 写下的前 4KiB)、epoll_ctl 对普通文件直接 EPERM 与 O_NONBLOCK 的不生效(磁盘文件的统一异步只剩 io_uring 的 READ)、批量经济性的诚实答案(16MiB 页缓存热路径上 read(2) 4096 次调用 2.1 到 2.8 毫秒,io_uring 256 一批 2.4 到 2.6 毫秒,墙钟打平而系统调用数差 128 倍)、超时也是请求(TIMEOUT 反序提交完成按墙钟走,LINK_TIMEOUT 到点把等不来的 read 打成 -ECANCELED,数据 100ms 到则 read 正常完成而超时请求被撤,对照 timerfd 的 fd 进表)、SQPOLL 在无特权的 WSL2 上建环成功的探针与 kernel.io_uring_disabled 的环境边界"
chapter: 8
order: 3
platform: host
difficulty: advanced
cpp_standard: [20]
reading_time_minutes: 24
prerequisites:
  - "I/O 多路复用:select、poll 与 epoll 的边界与成本"
  - "timerfd 与 eventfd:时间与事件的 fd 化"
  - "信号(下):实时信号、signalfd 与 pidfd"
related:
  - "epoll:Linux I/O 多路复用,从 poll 的瓶颈到兴趣表与就绪队列"
  - "Reactor 模式:把 epoll 包成事件循环 + 回调,以及它为什么是'同步非阻塞'"
  - "异步 I/O 与事件循环"
tags:
  - host
  - cpp-modern
  - advanced
  - 系统编程
  - POSIX
  - 异步编程
  - 实战
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# io_uring:把等待 I/O 变成收割完成事件

[L02](./02-timerfd-eventfd.md) 收尾的时候,咱们的循环已经相当体面:pipe、eventfd、timerfd 全变成了 fd,挂进了同一张 epoll 表里,一个 epoll_wait 就把收发全打理了。可您往深处追问一句,就会发现表管的只是等。epoll 报的是哪个 fd 能动了,动手的还是咱们自己,read 照样得您亲自去调,而调用本身是同步的,数据没就位的话内核就把您按在 read 上。那咱们把问题推到底:I/O 这个动作本身,能不能整个地交给内核,您只管回头收完成的通知?

这就是 Reactor 与 Proactor 的分野,而且差别落在一个非常具体的地方:等的是就绪,还是等的是完成。Reactor(反应器)等的是就绪,epoll_wait 醒了过来,读的活儿仍由您亲手做。Proactor(完成器)等的是完成:您把读请求连同缓冲的地址一并交出去,内核做完了就往一个地方放完成记录,您再去收割。工程展开的课,网络卷的 [Reactor 篇](../../../networking/03-reactor-pattern.md)与 vol5 的[异步 I/O 与事件循环](../../../../vol5-concurrency/ch06-async-io-coroutine/04-async-io-and-event-loop.md)都已经讲过,咱们这里只对照行为,模式课就不重开了。socket 场景的工程展开,网络卷的路线里还留着它自己的一篇 io_uring,与本篇的文件和管道场景各管一段。

Linux 给咱们的答案叫 io_uring,2019 年 5 月随 5.1 进了主线,作者是块层的维护者 Jens Axboe。名字里的 ring 就是字面的意思:两枚用户态与内核共享的环形缓冲,一枚装的是提交队列(SQ、submission queue),一枚装的是完成队列(CQ、completion queue)。队列里装的一项一项也有自己的名字,提交侧的叫 SQE(submission queue entry),一条提交请求的意思,完成侧的叫 CQE(completion queue entry),一条完成记录的意思。其实在它以前,Linux 就试过两条异步的路。POSIX AIO 在 glibc 那头是用线程池仿出来的,请求并没有真的走进内核的异步路径。原生 AIO(io_setup 与 io_submit 那一套)倒是进了内核,可它的脾气怪,不配 O_DIRECT(读写绕过页缓存、直抵磁盘的模式)的话基本做不出异步的效果,用的人不多,活下来的更少。咱们再看 io_uring 的思路,就跟它们都不一样了:它不再为每个操作单配一条进内核的调用,而是把提交与完成这两件事本身做成共享的内存,等 I/O 就从调用变成了收割。

实验的编号是 E1 到 E6,与仓库存档 `code/volumn_codes/vol8/systems-programming/linux/io-multiplexing/03-io-uring/` 里的 u1 到 u6 六份源码一一对应,代码连同全部的原始输出都收进了存档,您随时能对表。正文里的输出块全是全量引用,咱们连一个字都没改,代码节选里删去的部分用 `...` 标了出来。本篇的 E 只认本篇:[L01](./01-select-poll-epoll.md) 与 [L02](./02-timerfd-eventfd.md) 各有自己的 E 系,同号的它们互不相干,您翻存档的时候认文件名的前缀就好,e 打头的是篇一,t 打头的是篇二,咱们这里的 u 打头。另有两枚探针的小源码也在同一目录里,它们的名字是 probe_uring.c 与 liburing_hello.cpp,不计入 u 的编号,开头的两块输出出自的就是它们。

环境的口径照例交代清楚,后面的数字都要拿它对表:实验出自笔者的台机,CPU 用的是 AMD Ryzen 7 9700X,系统是 WSL2 的环境,内核是 6.18.33.2-microsoft-standard-WSL2 的构建,g++ 用的是 16.2.1,glibc 的版本是 2.44,编译的口径一律 `-std=c++20 -O2 -Wall -Wextra` 加上 `pkg-config --cflags --libs liburing`,拿到的警告数是零。计时用的时钟一律是 CLOCK_MONOTONIC,计时的实验各复跑了一轮,数字有波动而结构一致。数据文件由实验程序自建在 `~/ch04_scratch/` 下面的目录里,路径写死在源码顶部的常量里,复跑以前您得把这个目录建出来。全部 `.out` 出自 2026-10-04 的同一轮,mmap 的地址与毫秒级的时刻每次复跑都会变,咱们引用的是字节数与次序,而不是任何具体的地址。

## 动手以前的两枚探针

io_uring 是相对年轻的设施,别的机器、容器、沙箱上都可能不给咱们用,所以咱们把两枚探针放在了最前面,过不了探针的环境,后面的什么都谈不上。头一枚是什么库都不带的,纯拿裸的系统调用去问内核:

```text
kernel check: io_uring_setup syscall number = 425
io_uring_setup(8, params) = 3  -> OK, ring fd = 3
sq_entries=8 cq_entries=16 flags=0x0 sq_thread_cpu=0 sq_thread_idle=0 features=0x3ffff wq_fd=0
features bits: NODROP=1 SUBMIT_STABLE=1 RW_CUR_POS=1 CUR_PERSONALITY=1 SINGLE_MMAP=1
```

425 这个编号是本机 `<sys/syscall.h>` 里实测打印的,x86-64 上 io_uring 一共占用三个:setup 的编号是 425,enter 的是 426,register 的是 427。您看着眼熟就对了,它们与[信号下篇](../process/05-signal-advanced.md)收尾的编号表里的 434(pidfd_open)、424(pidfd_send_signal)、438(pidfd_getfd)是同一条谱系,都是近年新加的系统调用,而 glibc 对 io_uring 的三个编号到现在也没提供包装,咱们想调就只有裸 syscall 和 liburing 的两条路。三个编号里 setup 与 enter 咱们马上就会用到,register(427) 这一篇用不上:它把一组缓冲区地址或者文件描述符提前注册进内核的表,之后 SQE 里放的就不再是裸的 fd,而是注册表的索引,内核省掉了每次请求的 fd 查找,大批量的热路径才轮得到它,您在大流量的服务里会再遇到。

探针还捎回来了两笔信息。`features=0x3ffff` 是低 18 位的全置位,咱们点过名的 NODROP、SUBMIT_STABLE、RW_CUR_POS、CUR_PERSONALITY、SINGLE_MMAP,五位全都置了位,E1 还会点名其中的 SINGLE_MMAP。NODROP 管的是完成队列满时的行为,有了它内核宁可不返回也不丢完成的事件。请求 8 个 entries 的时候,内核给的是 sq=8、cq=16,完成队列默认配到了提交队列的两倍容量,给收割留出了余量。第二枚探针问的是 liburing 在不在:

```text
liburing version: 2.15
submitted=1 wait=0 cqe res=0 user_data=42
```

答案是肯定的,版本报的是 2.15,一个 NOP 从建环到等待的全链都走通了。于是咱们走双轨:E1 不带库,把机制明明白白地摆在面上,E2 起再换 liburing 讲工程的姿势。用裸 syscall 的理由,与信号下篇那段 `extern "C"` 的交代出自同一层考虑:机制课要看清的是零件,库的封装等看清了再上。

## E1:不用 liburing,把环亲手建起来

liburing 的好用是真的,可它把环的形状全藏了起来。咱们这一篇不带它,只带内核自己的 uapi 头 `<linux/io_uring.h>`,把建环、提交、收割的活儿亲手走一遍。咱们从头一步的 setup 开始,拿到了 ring fd 之后,params 结构体被内核回填了一堆偏移,三段 mmap 靠的就是它们:

```cpp
// u1_bare_ring.cpp(节选)
io_uring_params p{};
int fd = (int)syscall(__NR_io_uring_setup, 4, &p);
...
size_t sq_sz = p.sq_off.array + p.sq_entries * sizeof(unsigned);
size_t cq_sz = p.cq_off.cqes + p.cq_entries * sizeof(io_uring_cqe);
void* sq_map = mmap(nullptr, sq_sz, PROT_READ | PROT_WRITE,
                    MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_SQ_RING);
void* cq_map = mmap(nullptr, cq_sz, PROT_READ | PROT_WRITE,
                    MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_CQ_RING);
void* sqe_map = mmap(nullptr, p.sq_entries * sizeof(io_uring_sqe), PROT_READ | PROT_WRITE,
                     MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_SQES);
```

咱们看三段映射各管的是哪摊活:SQ 环装的是 head、tail、数组下标这些元数据,CQ 环装的是完成队列的元数据,SQE 数组装的是请求本身。mmap 的 flags 里咱们带上了 MAP_POPULATE,它让映射建立的时候页表就填好,免得头一回碰这块内存的时候再吃一次缺页,对环来说这不算必需的东西,共享的页迟早都会被碰到,带了只是少一次冷启动的停顿。跑起来的全量输出长这样:

```text
syscall 编号: __NR_io_uring_setup=425 __NR_io_uring_enter=426 __NR_io_uring_register=427
io_uring_setup(4) = 3, sq_entries=4 cq_entries=8 features=0x3ffff
features: SINGLE_MMAP=1 NODROP=1 SUBMIT_STABLE=1
mmap: SQ 环 208 B @ 0x78d9f1498000, CQ 环 192 B @ 0x78d9f1497000, SQE 数组 256 B @ 0x78d9f1496000
SINGLE_MMAP 置位: SQ/CQ 在同一块区域, 允许一次 mmap 同拿两环
(这里按经典三段式分开映射, 地址自然不同: 0x78d9f1498000 / 0x78d9f1497000; 特性省的是映射次数)
/proc/self/maps 里能看到这两段映射:
  78d9f1496000-78d9f1497000 rw-s 10000000 00:10 6114131                    anon_inode:[io_uring]
  78d9f1498000-78d9f1499000 rw-s 00000000 00:10 6114131                    anon_inode:[io_uring]
环参数: sq_mask=3 cq_mask=7 (容量-1), SQE=64 B, CQE=16 B

塞入 1 个 NOP (user_data=0xC0FFEE), SQ tail 0 -> 1
io_uring_enter(submit=1, wait=1, GETEVENTS) = 1
CQ: head=0 tail=1, 就绪 1 个
  CQE: user_data=0xc0ffee res=0 flags=0x0
收完, CQ head=1 tail=1。提交走 SQ 尾指针, 完成走 CQ: 两个环都是与内核共享的内存。
```

咱们拿 entries=4 把三个尺寸过一遍。SQ 环占的是 208 字节,里面装的是元数据加 4 项数组下标,CQ 环这边占的是 192 字节,里面装的是元数据加 8 项 CQE,SQE 数组占的是 256 字节,正好装下 4 个每只 64 字节的 SQE。`/proc/self/maps` 里您看到的两段 `anon_inode:[io_uring]` 映射就是它们的真身:头一段映射在文件的偏移 0x10000000,装的是 SQE 数组,另一段在偏移 0 的位置,装的是 SQ 环,而 CQ 环的偏移 0x8000000 没在这次的 dump 范围里。您还能看到 SINGLE_MMAP=1:内核的意思是 SQ 与 CQ 两环可以一次 mmap 拿进同一块区域,实验偏按经典的三段式分开映射,地址自然就不同了,这项特性省下的只是映射的次数,咱们离了它也照样建环。

下标的取法还有一处讲究,咱们接着看。落位用的是 tail & mask,mask 的值是容量减一,4 项的环 mask 是 3,tail 涨到 4、5、6 的时候,落位也就回到了 0、1、2,环就这么转了起来。SQ 环里放的其实是下标,SQE 的本尊待在数组里,内核照着下标去取 SQE 数组里的真身。

enter 的四个参数也值得咱们认一遍:ring 的 fd 与要提交的 SQE 个数,想等到的完成个数与 flags。E1 里咱们传的是(1、1、GETEVENTS),一次调用把提交与等待都打包了,而在咱们这样的普通环上不带 GETEVENTS 的话,第三参数内核是不看的,提交完了就立刻返回,完成的等待得再进一次内核(配了 IOPOLL 的环是例外)。

环建好了,咱们提交一个请求要走四步:读尾指针、填 SQE、填数组下标、放尾指针。四步干的全是用户态的活,真正进内核的只有后头那一声 enter,而 enter 本身就是一条系统调用:

```cpp
// u1_bare_ring.cpp(节选,续)
unsigned tail = READ_ONCE(*sq_tail_p);
io_uring_sqe* sqe = &sqes[tail & sq_mask];
std::memset(sqe, 0, sizeof(*sqe));
sqe->opcode = IORING_OP_NOP;
sqe->user_data = 0xC0FFEE;
sq_array[tail & sq_mask] = tail & sq_mask;
WRITE_ONCE(*sq_tail_p, tail + 1);                 // 尾指针一放, 内核就看得见了
...
long r = syscall(__NR_io_uring_enter, fd, 1, 1, IORING_ENTER_GETEVENTS, nullptr);
```

咱们读尾指针、填 SQE、填数组下标、放尾指针,四下就走完了。`WRITE_ONCE` 展开是一次 release 语义的原子写,它保证 SQE 的内容在尾指针生效以前全部就位,内核那边拿 acquire 的语义读尾指针,拿到的就是完整的请求。从放完尾指针到 enter 的这一段里,咱们一次系统调用都没花,请求安安静静地躺在共享内存里,直到 `io_uring_enter` 进内核喊了声:提交 1 个,顺带等的是 1 个完成。到了收割的这边,内核推进 CQ 的 tail,而 head 留给用户推进,所以实验里收完之后 head=tail=1,这就是环的握手方式。

有一个字段值得您现在就混个脸熟:`user_data` 是 64 位的宽度,内容随您填,CQE 会把它原样地带回来。E1 里咱们填了 0xC0FFEE 当招牌,输出里它分毫不差地回来了。它是批量请求的关联凭证:一百个读进了环,完成会乱序地回来,谁是谁全靠的是它,E2 马上就用到它了。

一个读请求的 SQE 长什么样,咱们看字段就能拼出来:opcode 填的 IORING_OP_READ,剩下的 fd、addr、len、off 四格,分别装的是目标文件、缓冲地址、长度与偏移。E2 里 prep_read 折的就是这组格子的填写,您回头对着看一眼就有数了。

还有一句工程上的交代。实验的代码贴着裸 syscall 与 liburing 写,ring fd、pipe fd、文件 fd 在节选里全是裸的 int,工程里它们的归宿是 [RAII 篇](../../thinking/01-raii-paradigm.md)的 unique_fd 与[错误处理篇](../../thinking/02-error-paradigm.md)的 expected,咱们在本篇只引用、不重开课。

## E2:liburing 起手,一次提交 64 个读

裸的 syscall 是机制课,工程里已经没人手填 SQE 了。liburing 的三件套 `get_sqe`、`prep_*`、`submit` 就是给环包的一层薄皮,E1 里的那四步,折进了 `io_uring_prep_read` 与 `io_uring_submit` 一类的小函数里。它其实算不上又一层运行库,封装贴的就是环本身,出了事您照样能用 E1 的知识把环翻出来看。咱们直接上批量:一个 256KiB 的数据文件切成 64 块,每块 4KiB 填了可校验的模式,咱们一次提交 64 个读:

```cpp
// u2_batch_read.cpp(节选)
static unsigned char bufs[kBlocks][kBlockSz];
for (int i = 0; i < kBlocks; ++i) {
    io_uring_sqe* sqe = io_uring_get_sqe(&ring);
    io_uring_prep_read(sqe, fd, bufs[i], kBlockSz, (long long)i * kBlockSz);
    io_uring_sqe_set_data64(sqe, i);              // 块号当凭证
}
int submitted = io_uring_submit(&ring);           // 一次 submit, 64 个请求全部进环
...
while (reaped < kBlocks) {
    io_uring_cqe* cqe = nullptr;
    int r;
    if (reaped == 0) {
        // 批量等待: 这次调用会真正进内核等 64 个完成
        r = io_uring_wait_cqes(&ring, &cqe, kBlocks, nullptr, nullptr);
    } else {
        r = io_uring_peek_cqe(&ring, &cqe);       // 后续直接从共享环里拿, 不进内核
    }
    ...
}
```

有一处与 read(2) 不一样的地方,您看 prep 的参数就明白了:每个读都带着自己的偏移,相当于 pread 的用法,根本不走文件的位置。64 个读打的是同一只 fd,各读各的偏移。全量的输出在这里:

```text
一次 io_uring_submit 提交了 64 个读请求 (各 4 KiB, 各自带偏移)
收割 64 个 CQE (wait 真等 1 次, 其余 63 次是用户态 peek), 校验全部通过 (块号-偏移-内容对上), 共 262144 字节
系统调用次数对照: read(2) 逐块 = 64 次; io_uring = submit 1 次 + wait 1 次 + peek 不进内核
```

咱们把数字摆在一起看。同样的活,read(2) 走的是 64 次系统调用,io_uring 用的是 submit 一次加 wait 一次,进内核的次数就从 64 掉到了 2。`io_uring_wait_cqes` 带 64 的定额,这一次的调用会真正进内核等够 64 个完成,而其余 63 个的收割走的全是 peek,也就是用户态对共享 CQ 环的直接读取,没有内核的份。校验也全过了:块号、偏移、内容三样都对上了,CQE 的 res 是 4096,完成事件带的是真实的字节数。

peek 不进内核的道理,咱们回到环上就明白了:完成事件是由内核写进共享 CQ 环的,`io_uring_peek_cqe` 做的只是读用户态内存,`io_uring_cqe_seen` 做的只是把 head 推一格,把位置让了出来。真正的等待只在环空的时候才发生:一次等待就把一批完成全都等来了,后面的收割全是纯用户态的动作,批量的摊薄就是这么来的。

[L01](./01-select-poll-epoll.md) 的 select、poll、epoll 报的都是就绪,能读了您再读。io_uring 的回报是完成,读完了给您结果。这两句话在 socket 的场景里听着像咬文嚼字,可到了普通文件上,差别就成了有没有路可走,E4 会专门验证的就是这一段。

## E3:链式请求,顺序进内核,失败向下游传播

读到了才能写,写完了才 fsync,这类有依赖的 I/O,用户态的做法是等头一个完成、再提交下一个,一来一回都得咱们自己盯着。io_uring 给了另一条路:同一次提交里的相邻请求用 `IOSQE_IO_LINK` 串成链,顺序的保证交给内核。实验跑了两条链,链 A 是 read 接 write 接 fsync 的三段:

```cpp
// u3_chained.cpp(节选):链A
io_uring_sqe* s1 = io_uring_get_sqe(&ring);
io_uring_prep_read(s1, sfd, buf, 4096, 0);
io_uring_sqe_set_data64(s1, 1);
s1->flags |= IOSQE_IO_LINK;                       // 与下一个绑成链

io_uring_sqe* s2 = io_uring_get_sqe(&ring);
io_uring_prep_write(s2, dfd, buf, 4096, 0);
io_uring_sqe_set_data64(s2, 2);
s2->flags |= IOSQE_IO_LINK;

io_uring_sqe* s3 = io_uring_get_sqe(&ring);
io_uring_prep_fsync(s3, dfd, 0);
io_uring_sqe_set_data64(s3, 3);                   // 链尾不用再挂 LINK
int n = io_uring_submit(&ring);
```

咱们把链 B 头一个 read 的 fd 换成 -1,专等的就是它翻车。全量的输出:

```text
链A 一次提交 3 个 (read->write->fsync 挂链)
  链A 的三个完成, 顺序与提交一致:
  操作1 res=4096 (成功)
  操作2 res=4096 (成功)
  操作3 res=0 (成功)

链B 一次提交 2 个 (read(bad fd)->write)
  链B 的两个完成: 失败向下游传播成取消:
  操作1 res=-9 (坏的 fd)
  操作2 res=-125 (链断了, 被取消)

链A 的 write 确实生效: 前 16 字节 = 00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f ...
链B 的 write 没有执行: 目标文件偏移 4096 处仍是 0 (只写了前 4KiB)
```

咱们看链 A 的三个 CQE 按提交的次序完成,res 报的是 4096、4096、0,write 读到的就是 read 装进 buf 的那 4KiB,前 16 字节 00 到 0f 的模式对得上,fsync 也以 res=0 收了尾。整段的顺序保证都在内核侧,用户态没有一行排序的代码,read 与 write 共用同一个 buf 也不会有竞态的问题,因为链保证 write 开跑的时候 read 已经完成。

链 B 才是这一场的重头戏。链头的 read 拿到的是 res=-9,对应的就是 -EBADF,下游的 write 紧跟着拿到 res=-125,那说的就是 -ECANCELED。write 的执行根本没有发生,目标文件停在链 A 留下的前 4KiB,链 B 要写的 4096 偏移那一段根本就没发生过。失败的传播规则就这样写进了链里:任何一环的失败,后面的整段跟着取消,以 -ECANCELED 的 CQE 逐个报到,不留半执行的中间状态让您收拾。您要是写过手动串联的异步回调,恐怕都记得每一层都得自己判断上一层的成没成,而这里该判断的判断、该跳过的跳过,都归了内核。

咱们顺带再认两个相关的 flag。挂了 IOSQE_IO_DRAIN 旗的请求,要等在途的请求全部完成才开跑,IOSQE_IO_HARDLINK 串的链也一样紧,可前一环失败了后一环照样跑,失败是不传播的。链 A 的活用 HARDLINK 串也是成立的,只是换成链 B 那一次 read 失败的场合,后面的 write 会白跑一趟。

## E4:普通文件,非阻塞不生效,epoll 拒收

[文件 I/O 的头一篇](../file-io/01-posix-file-io.md)与[上一篇](./01-select-poll-epoll.md)的正文里都留过这句话,epoll 是管不了普通文件的。流传的版本笔者听过两个:一个说它永远都是就绪的,另一个说它遭到了静默的忽略。动手以前笔者押的是前者,寻思着 ADD 总归是能成的,wait 会没完没了地报,结果头一步就被拒了。咱们把实测分成两半看,从 O_NONBLOCK 的这半开始:

```text
[普通文件 fd=3]
fcntl F_SETFL O_NONBLOCK = 0 (这个标志本身设得上去)
O_NONBLOCK 下 read = 4096 (有数据直接给, 不会拿 EAGAIN 说"现在没有")

epoll_ctl ADD 普通文件 = -1 errno=1 (Operation not permitted)
man 2 epoll_ctl 对 EPERM 的解释: 目标 fd 不支持 epoll (内核里没有 poll 支持的文件类型)

[对照: 空管道 + O_NONBLOCK] read = -1 errno=11 (Resource temporarily unavailable)
空管道 epoll_wait(0) 返回 0, 管道在其中报就绪: 否

结论: 普通文件两头都关死 -- 非阻塞语义不生效(read 永不 EAGAIN), epoll 直接拒收(EPERM);
对磁盘文件做统一的异步, 只能走 io_uring 的 READ (完成事件带真实字节数, 见 E2)。
```

咱们一段一段对着看。fcntl 把 O_NONBLOCK 设了上去,返回的是 0,标志本身是挂得住的,可语义是不生效的:普通文件上 read 拿到的是 4096,有数据就直接给了,永远轮不到 EAGAIN 的出场。对照组的空管道才是您熟悉的剧本:read 报的是 -1 加 errno=11,而 epoll_wait 也安分,空管道没在就绪的名单里。轮到的下一项是 epoll_ctl ADD 普通文件,返回的是 -1 加 errno=1,报的就是 EPERM。man 2 epoll_ctl 对这个错码的解释只有一句,说的就是目标 fd 不支持 epoll。内核里这半截的机制是咱们补的注:这类文件没有 poll 的实现,注册的这一步自然就过不去。

为什么设计成拒收?普通文件是没有等待可言的。数据要么在页缓存里、要么由内核去盘上给您凑,read 总是能做完的,而它永远是就绪的,就绪的通知对它没有信息量。与其让每一路都报个永远为真的就绪,内核选择了在注册的这一步直接说不。于是普通文件两头都关死了:非阻塞的路子 read 不认,就绪通知的路子 epoll 不收。磁盘文件想要一份统一的异步,剩下的路只有 io_uring 的 READ,而它在 E2 已经验过,完成事件带的还是真实的字节数。而 READ 为什么就走得通?因为它要的只是完成,而不是就绪。请求交了出去之后,页缓存命中的部分内核当场就做完了,做不动的部分按 io_uring(7) 的说法会交给内核内部的工作线程去等,完成了照样回 CQ,咱们站在环外看到的只有完成事件。

## E5:批起来省在哪,页缓存热路径上墙钟不赢

到这儿您可能已经在心里给 io_uring 记了一功。那咱们就把它放到计时的台面上,场景挑的是对它相当有利的一档:16MiB 的文件、4KiB 一块共 4096 块,咱们把文件整读一遍,把页缓存喂热了,然后让 read(2) 的逐块调用与 io_uring 的 256 块一批对着跑,对照侧实测用的是带偏移的 pread,正好与 io_uring 的带偏移读同口径,两边各取 5 轮的中位,再整体复跑了一轮:

```text
16 MiB (4096 x 4KiB), 页缓存已预热, 5 轮取中位:
read(2) 逐块 :      2.1 ms (4096 次系统调用)
io_uring 256 一批:      2.4 ms (submit 调用 16 次 + wait 调用 16 次, 其余收割是用户态 peek)
每块均摊: read 509 ns, io_uring 591 ns

SQPOLL 探针: 建环成功, submit=1 (只写共享环, 不催内核)
SQPOLL: 内核线程 已把 NOP 干完 (res=0)
```

咱们复跑的那一轮:

```text
16 MiB (4096 x 4KiB), 页缓存已预热, 5 轮取中位:
read(2) 逐块 :      2.8 ms (4096 次系统调用)
io_uring 256 一批:      2.6 ms (submit 调用 16 次 + wait 调用 16 次, 其余收割是用户态 peek)
每块均摊: read 692 ns, io_uring 636 ns

SQPOLL 探针: 建环成功, submit=1 (只写共享环, 不催内核)
SQPOLL: 内核线程 已把 NOP 干完 (res=0)
```

把两轮摆开了看,第一轮 read(2) 赢的是 2.1 对 2.4,复跑换 io_uring 赢的是 2.6 对 2.8。数字上是互有胜负的,差值也都在噪声的范围内,咱们诚实的口径就一个字:平。墙钟是打平的,系统调用的数目可是实打实的 4096 对 32,差了 128 倍。32 的来路也摆得上台面:4096 块按 256 的批切下来是 16 批,每批花的都是一次 submit 加一次 wait,乘出来的正好是 32。为什么省下了这么多调用,墙钟却纹丝不动?页缓存命中的时候,数据本来安安稳稳地在内存里,read 的路径只剩系统调用的进出与一次拷贝,每块的均摊是 509 到 692 纳秒。省掉的 128 倍次数,省的是每次进内核的固定门槛,而这个门槛在咱们的机器上本来就便宜,省下的那点又被填 SQE、收割 CQE 的活儿吃回去了。

所以 io_uring 更快这话,您得给它限定场景。在页缓存的热路径上,它赢的是系统调用的数目与完成路径的异步化,而墙钟是赢不动的。墙钟的胜利得去慢设备上找:数据在盘上、在网上、在途中的那段时间里,CPU 也腾出了手去干别的,批量的摊薄才真正兑现,而全命中的 E5 场景是量不出来的。

输出的末尾还带着一枚探针:SQPOLL。建环的时候带上 `IORING_SETUP_SQPOLL`,内核会起一个专属的线程替咱们守着 SQ 环,提交的数据走的就是共享环,省下了每次进内核催的那一趟,探针里的那个 NOP 就是内核线程自己干完的。不过“连 enter 都不用”这话得收窄:内核线程睡着的时候,SQ 环的 flags 里会立起 NEED_WAKEUP 的旗,这一刻 liburing 的 submit 会替咱们发一次 enter(挂上 IORING_ENTER_SQ_WAKEUP 的旗)把线程叫醒,数据的传递仍然只靠共享环。输出里的括号注写着“只写共享环、不催内核”,那是程序里写死的口径串,拿 strace 跟一遍就能看见那次唤醒的 enter,咱们按实情收窄口径。内核线程闲下来的时候,会按 sq_thread_idle 的毫秒数退睡,可活着的每一刻都在轮询,本质是拿 CPU 换提交的延迟,低延迟的场合才划算,像本篇这样偶尔干一票的程序就不值得了。SQPOLL 的待遇有历史门槛:5.12 及以前的内核要的是 CAP_SYS_NICE 或 CAP_SYS_ADMIN,5.13 起才完全免了特权,本机的 6.18 无特权就能建。另一头的边界咱们也交代一下,6.6 起内核加了 `kernel.io_uring_disabled` 这个 sysctl,三档分别是全开的 0、只留特权的 1、全关的 2,个别发行版出厂就设了 2,容器与沙箱的策略也常把三个编号拦掉。您在自己环境里跑不通的时候,记得看的就是这个旋钮,实验代码建环失败的时候会如实把 errno 打出来,那不算实验的失败,而是环境在开口说话。

## E6:超时也是请求,TIMEOUT 与 LINK_TIMEOUT

[L02](./02-timerfd-eventfd.md) 把定时器做成了 timerfd,fd 进了 poll 表,与其他的事件平起平坐。io_uring 的答法更彻底:超时本身变成了一个请求,进了环,它的完成是一个 CQE,连占一个 fd 的活都省了。实验咱们分了三场,甲场把两个独立的 TIMEOUT 反着提交,350 毫秒的排头,100 毫秒的跟后:

```text
甲: 反序提交两个 TIMEOUT (350ms 先提交, 100ms 后提交):
  t+ 100 ms 收到 100ms 的完成 (res=-62, 到点)
  t+ 350 ms 收到 350ms 的完成 (res=-62, 到点)

乙: read(空管道) 挂 LINK_TIMEOUT 300ms, 没人写数据:
  t+ 650 ms 操作2 res=-62 (到点)
  t+ 650 ms 操作1 res=-125 (被超时取消)

丙: 同样的组合, 100ms 时写入 5 字节 (预算 500ms):
  t+ 750 ms 操作1 res=5 (读到了)
  t+ 750 ms 操作2 res=-125 (被撤掉)
```

甲场里完成的次序跟提交的次序反着,跟的是墙钟:t+100 收到的是 100 毫秒的那个,t+350 收到的是 350 毫秒的那个,res 给的都是 -62,也就是咱们说的 -ETIME。您把 100、350、650、750 四个时刻连起来看,到期与完成走的都是墙钟,提交的次序根本不参与排序。prep_timeout 的第三个参数是计数,咱们填 0 就是纯按时间的超时,填了正数它就换脾气,等够那么多个完成的时候也算到期。

乙场是超时的杀手锏用法,咱们给一个等不来数据的 read 配上 `IORING_OP_LINK_TIMEOUT`,两条 set_data64 填的是 1 与 2,输出里的“操作1、操作2”就是它们跟着 user_data 回来的样子:

```cpp
// u6_timeout_requests.cpp(节选):乙
io_uring_sqe* s1 = io_uring_get_sqe(&ring);
unsigned char buf[16];
io_uring_prep_read(s1, pp[0], buf, sizeof buf, 0);
io_uring_sqe_set_data64(s1, 1);
s1->flags |= IOSQE_IO_LINK;
io_uring_sqe* s2 = io_uring_get_sqe(&ring);
__kernel_timespec ts{0, 300000000};
io_uring_prep_link_timeout(s2, &ts, 0);
io_uring_sqe_set_data64(s2, 2);
io_uring_submit(&ring);
```

管道是空的,read 挂在了那儿,300 毫秒到点的时候,操作 2 以 -62 报了到,同一时刻操作 1 的 read 也被取消了,打在它身上的就是 -125 的 -ECANCELED。时间线您可以对一下:乙场是在甲场两个超时收完之后才提交的,t+650 恰好就是 350 加 300 的和。丙场把剧本换了过来,同样的组合预算放宽到 500 毫秒,100 毫秒的时候往管道里写了 5 个字节:read 正常地完成了,res 给的是 5,而超时请求被撤,带着 -125 退了场。成对的两边,一方正常地完成,另一方就自动撤销了,两个 CQE 都到齐了,不给咱们留环里的孤儿请求。

咱们拿超时请求这套做法对照 timerfd,差别就清楚了。timerfd 那边是一个 fd 进了表,到期是表上的可读事件,咱们还是得自己去 read 它拿次数。这边是一个请求进了环,到期报的就是一个完成事件,与一个读的完成、一次 fsync 的完成没有任何地位的差别,收获走的都是同一个 CQ。[信号下篇](../process/05-signal-advanced.md)里优雅关闭的那场服务器实验要是搬到环上,关停超时也就不用单独的表了,每次提交的时候顺手挂上一枚 SQE 就行。

## 另一侧怎么看

Windows 的一侧,异步 I/O 从 NT 时代起就是完成通知的姿势:OVERLAPPED 的结构体随读请求一起交出去,完成的收割交给 I/O 完成端口(IOCP)。您回看 E1 的输出再想 IOCP 的完成队列,会发现两边在行为上是一路的,Proactor 本来就是 Windows 的原生传统。Windows 侧的异步 I/O 两篇已经在盘上,IOCP 的正课在[那一篇](../../windows/async-io/02-iocp.md)。两枚环与 IOCP 的逐项对表,那边的收尾把它交给了跨平台的那一篇,咱们就到那里再收进同一张矩阵。

<ReferenceCard title="参考资源">
  <ReferenceItem
    :id="1"
    title="io_uring_setup(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/io_uring_setup.2.html"
  />
  <ReferenceItem
    :id="2"
    title="io_uring_enter(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/io_uring_enter.2.html"
  />
  <ReferenceItem
    :id="3"
    title="io_uring(7)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man7/io_uring.7.html"
  />
  <ReferenceItem
    :id="4"
    author="Jens Axboe"
    title="liburing"
    publisher="GitHub (axboe/liburing)"
    url="https://github.com/axboe/liburing"
  />
  <ReferenceItem
    :id="5"
    title="Documentation for /proc/sys/kernel"
    publisher="The Linux Kernel documentation"
    url="https://docs.kernel.org/admin-guide/sysctl/kernel.html"
  />
  <ReferenceItem
    :id="6"
    author="Michael Kerrisk"
    title="The Linux Programming Interface"
    publisher="No Starch Press"
    :year="2010"
    url="https://man7.org/tlpi/"
  />
</ReferenceCard>
