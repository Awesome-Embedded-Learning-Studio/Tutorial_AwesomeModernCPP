---
title: "跨平台异步 I/O 抽象"
description: "四个平台后端的差异摆开之后,C++ 的 concepts 能不能在编译期把一个异步后端约束出来:本篇把 epoll、kqueue、IOCP、io_uring 收进八维差异矩阵(通知语义、兴趣管理、等待成本、普通文件、超时、唤醒通道、线程模型、在途规模,数字全部引用两侧五篇的实测存档,kqueue 整列标注 FreeBSD 手册页的文档口径),给出五行 AsyncBackend concept 与两侧后端骨架(EpollBackend 用 EPOLLONESHOT 把就绪式垫成完成式、适配层亲手 read 搬出 bytes,IocpBackend 的 ReadFile 加 OVERLAPPED 直录、id 藏在扩展结构随包回来),统一 request 的 offset 字段把普通文件那一维收进类型,同一份 drive_one_round 两侧一字不改各跑两轮真实读(管道 5+5 字节走 MOD 再武装,文件偏移 0 与 6 各读 5 字节),负例缺 take 的 LazyBackend 被两侧 GCC 16 在实例化之前拦下、诊断原文 the required expression 'b.take()' is invalid 两侧内容一致,过程里 string_view 过 printf %s 的段错误返工当教学点,收在三个来源拼一个 completion 与 ch07 平台抽象章的接手处"
chapter: 8
order: 3
platform: host
difficulty: advanced
cpp_standard: [20]
reading_time_minutes: 27
prerequisites:
  - "I/O 多路复用:select、poll 与 epoll 的边界与成本"
  - "timerfd 与 eventfd:时间与事件的 fd 化"
  - "io_uring:把等待 I/O 变成收割完成事件"
  - "OVERLAPPED 异步 I/O 与 WaitForMultipleObjects"
  - "IOCP 完成端口"
  - "异步 I/O 与事件循环"
related:
  - "异步 I/O 与事件循环"
  - "OS 资源的 RAII 范式:fd、HANDLE 与映射的同一副骨架"
  - "错误处理范式:从 errno 到 expected"
  - "Reactor 模式:把 epoll 包成事件循环 + 回调,以及它为什么是'同步非阻塞'"
tags:
  - host
  - cpp-modern
  - advanced
  - 系统编程
  - POSIX
  - Win32
  - 异步编程
  - concepts
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# 跨平台异步 I/O 抽象

多路复用的这一章,咱们在两侧的讲述到这里都收了尾。Linux 侧的三篇把 select、poll、epoll 的边界量成了数字,把 timerfd 与 eventfd 化进了同一张表,最后让 io_uring 把等待变成了收割。Windows 侧的两篇从 OVERLAPPED 的事件收割,一路走进了 IOCP 的完成队列。两侧收尾的时候各留了一句交班的话,而且都指到了本篇:两枚环与 IOCP 的逐项对表,Reactor 与 Proactor 的差异矩阵,咱们今天一并兑现。而本篇真正要回答的问题只有一个:四个后端的差异全都摆开之后,C++ 的 concepts 能不能在编译期约束出“一个异步后端”的形状,让同一份用户代码在两侧都跑通?

咱们把出处与称呼一次定下,后文就全按这些名字叫了。Linux 侧的三篇是 [L01](../linux/io-multiplexing/01-select-poll-epoll.md)、[L02](../linux/io-multiplexing/02-timerfd-eventfd.md) 与 [L03](../linux/io-multiplexing/03-io-uring.md),Windows 侧的两篇是 [A01](../windows/async-io/01-overlapped.md) 与 [A02](../windows/async-io/02-iocp.md)。矩阵里的每一个数字都出自这五篇的存档,咱们一个都不重测。咱们自己的实验只排了两组:e1 负责把 concept 的骨架在两侧编译、运行,e2 拿一个残缺的后端去验证编译期的拦截。两组的编号与两侧五篇各自的 e 系互不相干,您翻存档的时候认目录就好,咱们的东西全收在仓库的 `code/volumn_codes/vol8/systems-programming/cross-platform/03-cross-async-io/` 下面,e 打头的文件全是本篇的。输出块的口径也交代一句:e1 的两块是全量引用,e2 的诊断是节选,您对表的时候以存档为准。

咱们把三条边界也交代在前面,免得您等着看不来的东西。事件循环的架构(while 加定时器加等待加分发)与协程的衔接,是 vol5 的[异步 I/O 与事件循环](../../../vol5-concurrency/ch06-async-io-coroutine/04-async-io-and-event-loop.md)的正课,那边点了一条路子:接口抽成统一的,各平台在底下各换各的实现,libuv 与 Asio 走的都是它。咱们在本篇把它做实,但收的只有“后端”的那一层,不重开那边的课。兴趣表、就绪队列、LT 与 ET 这些内核层的差异,归[网络卷的 epoll 篇](../../networking/02-epoll-io-multiplexing.md),咱们沿用链接不展开。句柄的统一表示,错误的统一报告,是 ch07 平台抽象章的正题,本篇的 request.target 拿 uintptr_t 糙着承载,就是留给 ch07 收的口。

环境的口径咱们两侧分开说,后面的输出都要拿它对表。Linux 侧出自笔者的 WSL2,内核跑的是 6.18.33.2-microsoft-standard-WSL2 的构建,g++ 用的是 16.2.1,编译命令给的是 `g++ -std=c++20 -O2 -Wall -Wextra -D_FORTIFY_SOURCE=2`,拿到的警告数是零。Windows 侧出自笔者的 Win11 26200,用的是 MSYS2 UCRT64 的 g++ 16.1.0,从 WSL 里经 interop 调起的,编译的命令是 `/mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra`,拿到的警告数同样为零,interop 链路与产物要补执行位的细节,[Windows 文件 I/O 的首篇](../windows/file-io/01-win32-file-io.md)交代过了,咱们就不复述了。骨架是自包含的:卷首思维基石的 unique_fd、sys_call,连同对侧的 unique_handle 与 check_win32,这回咱们一个都没请,标准库的头加上平台的头就能编,图的是把 concept 的形状摆在明面上。全部输出捕获自 2026-10-04 的同一轮。

## 八个维度,把四个后端摆进一张表

咱们从五篇的实测里挑了八个维度,每个格子里的数字都能翻回出处。kqueue 那一列整个来自 FreeBSD 的 kqueue(2) 手册页,咱们的本机既没有 BSD,也没有 macOS 的机器,实测的数一个都没有,口径咱们在后文专门交代。

| 维度 | epoll(实测) | kqueue(文档) | IOCP(实测) | io_uring(实测) |
| --- | --- | --- | --- | --- |
| 通知语义 | 就绪,醒来报谁能读,动手的还是您 | 就绪,EVFILT_READ 报可读 | 完成,包里自带 bytes,取出即所得 | 完成,CQE 的 res 就是字节数 |
| 兴趣管理 | epoll_ctl 常驻内核,fdinfo 的 tfd 行可见 | changelist 随每次 kevent 递,注册与收割合一 | 没有兴趣表,句柄挂端口一次 | 没有注册,请求自带 fd 与偏移 |
| 等待的成本与批量 | 三档均约 990ns,与 N 无关,就绪批量收回 | 等待与注册同一个调用,两步并作一步 | WFMO 64 枚上限,65 起 WAIT_FAILED 加 87 | submit 一次加 wait 一次,其余收割是用户态 peek |
| 普通文件 | ADD 直接 EPERM,O_NONBLOCK 语义不生效 | vnode 可挂,不在 EOF 即报,与 epoll 相反 | 原生,偏移装在 OVERLAPPED 里 | 原生,READ 自带偏移 |
| 超时怎么进机制 | timerfd 化为 fd 进表 | EVFILT_TIMER 是过滤器,ident 为自定义标识 | GQCS 的限期参数 | 超时本身是请求,与读平起平坐 |
| 唤醒通道 | eventfd 进表 | EVFILT_USER 加 NOTE_TRIGGER | PQCS 往裸端口塞包 | (本卷未测) |
| 线程模型与完成次序 | 工程上单线程事件循环为主 | (手册页未涉及) | 多线程睡同一端口,并发值是上限,完成序跟投喂节奏 | 完成按墙钟,与提交序无关 |
| 在途规模与句柄 | 注册规模受 fd 额度管,WSL2 出厂 1048576 | (手册页未涉及) | 100 发在途 1 枚端口,零扫描 | 环容量自定,entries 给 8 内核配 sq=8 cq=16 |

通知的语义是头一维,也是把四个后端分成两组的那一维。epoll 醒来报的是谁能动,读的活儿还得咱们自己动手,而 io_uring 的 CQE 把 res 直接填成了本次读到的字节数,L03 的 E2 里 64 个 4KiB 读的 res 全是 4096。IOCP 的完成包同样是取出即所得,A02 的 e6 拿 100 发在途做过同场景的对照:事件式的每轮醒来,要把 100 枚事件挨个地问过去,探针记下的就是每轮 100 次,而端口那边的扫描次数是零。L03 开篇点过这两派的名字,等就绪的叫 Reactor(反应器),等完成的叫 Proactor(完成器),矩阵里往后的差别,几乎都是从这一维长出来的。

兴趣表的住处,决定了每轮等待要搬多少东西。select 把递进去的参数当草稿纸改写,位图不重建的话就漏事件,L01 的 E4 里,没重建的位图把后来那枚 fd 的事件漏给了咱们。poll 每轮进出的都是整张表,N=500 的档是 4000 字节,4096 档涨到了 32768(L01 的 E2 与 E3)。epoll 把兴趣表搬进了内核常驻,fdinfo 的 tfd 行摊开就是注册表(L01 E6)。IOCP 干脆省掉了兴趣表,句柄挂端口一次就完了(A02 讲建端口与挂句柄的地方),io_uring 连注册都省了,请求自带的就是 fd、缓冲与偏移,写法见 L03 E2 的 prep_read。

等待调用自身的成本,epoll 用三档平线证明了自己与 N 无关:三档 N=64、500、4096,每轮的读数各是 994、994、985 纳秒(L01 E3),epoll_wait 还把就绪的批量收回,返回了几个,处理的就只有几个(L01 E2)。Windows 侧事件式的硬上限在 WFMO:64 枚是过的,而从 65 枚起,回的才是 WAIT_FAILED 加 87(87=ERROR_INVALID_PARAMETER),带消息队列的 MsgWait 族再让出一个名额,63 枚才是安分的(A01 e4)。GQCS 则把等待与取包合进了同一个调用,批量的收法另有 GQCSEx,A02 提过名字而没测,咱们按文档口径标注。io_uring 的批量在 L03 的 E2 量过:64 个读只花了 submit 一次加 wait 一次,其余 63 个的收割全是用户态的 peek,而不用进内核。

普通文件的待遇,是矩阵里反差最大的一行。epoll_ctl 对普通文件的 ADD 直接回 EPERM,O_NONBLOCK 的标志设得上,语义却是不生效的,read 永远不会拿 EAGAIN 跟您说现在没有(L03 E4)。IOCP 与 io_uring 都是原生的:一个的偏移装在 OVERLAPPED 里(A02 e1 的三发乱序偏移读),另一个的 READ 自带偏移(L03 E2)。kqueue 的 vnode 也能挂,与 epoll 的做法正好相反,咱们到讲 kqueue 的时候再对。本篇 e1 的 Windows 侧还会再做一次,同一份文件的偏移 0 与 6 各读 5 字节。

超时与唤醒的机制,两侧给的答案各有各的形状。timerfd 把定时器化成了 fd 进表,1ms 档的中位 999.3µs、p99 1029µs(L02 E4),L02 还引过同机 Windows 侧的旧实测,Sleep(5) 实睡了约 12.6 毫秒,精度差距本身就是平台的一课。io_uring 把超时做成了请求,独立的 TIMEOUT 按墙钟走,LINK_TIMEOUT 与被超时的对象互撤,res 给的是 -62 与 -125(L03 E6)。GQCS 的限期就在参数里(A02 e1 的 800ms 一场)。唤醒的通道这边,Linux 用的是 eventfd,一次 write(5) 在 LT 加信号量的组合下连醒 5 次,L02 的 E2 管它叫忙通知档,Windows 的对应物是 PostQueuedCompletionStatus(咱们跟着 A02 叫它 PQCS),往裸端口塞个包就完了(A02 e4 的关停哨兵)。io_uring 的唤醒通道咱们本卷没测,矩阵里留了空,数也就不编了。

线程模型与规模的差别,咱们只念实测过的。IOCP 允许咱们让多条线程睡在同一个端口上,并发值是上限而不是配额:间隔投喂的时候,4 条工线程 8 包全进了一条线,一口气投 8 包的时候,并发值 0 的那一档才有四条线同 tick 分包(A02 e3)。完成次序跟的是投喂节奏而不是投递序,投递的次序是 1..6,完成的次序是 6 5 4 3 2 1(A02 e2,与 A01 e5 的同场景互为镜像),io_uring 的完成同样按墙钟(L03 E6 甲场,反序提交的两个 TIMEOUT 按 100 与 350 的时刻到)。epoll 这边的工程主形态是单线程事件循环,咱们沿用 vol5 事件循环篇的口径,多线程等同一个 epfd 的行为本卷未实测,咱们不写。规模上咱们看最后两笔:同样是 100 发在途,事件式被 64 的上限逼成了 64 加 36 的两段,每段醒来还欠一次全量的重扫,而 IOCP 一枚端口零扫描(A02 e6)。epoll 的注册规模最终受 fd 额度管着,L01 的 E1 量过真正查 rlimit 的只有 poll,压到 soft=1024 的时候,1050 条直接给了 EINVAL,WSL2 的出厂值给到 1048576。io_uring 建环的容量自定,L03 的头一枚探针里 entries 给 8,内核配了 sq=8、cq=16。

## 五行 concept,形状定在完成式

vol5 的事件循环篇点过的路子,咱们现在就走。统一的接口要立起来,形状的选定就躲不开了,而四个后端在这里分成了两派:epoll 与 kqueue 是就绪式的,IOCP 与 io_uring 是完成式的,公共的交集只有完成式一个形状。就绪式是可以被适配成完成式的:就绪到来的时候,适配层自己伸手把数据搬了回来,对上层交出去的就是一个完成事件。反过来就不行了:完成式的后端从来不向您报告谁就绪,它直接把活干完了,您手里没有就绪这个中间产物可以往接口上递。所以 concept 定成了完成式,IOCP 与 io_uring 是直录的,而 epoll 要垫一层。libuv 在 Unix 上厚出来的部分里,就有咱们垫的这一层,本篇把这一层做成可编译的证据。

咱们这就看 concept 的本体,五行就写完了:

```cpp
// e1_backend_concept.cpp(节选)
template <typename B>
concept AsyncBackend = requires(B& b, typename B::request r, int timeout_ms) {
    typename B::completion;
    typename B::request;
    { b.submit(r) } -> std::same_as<bool>;                              // 交出请求
    { b.wait(timeout_ms) } -> std::same_as<int>;                        // 等完成进队
    { b.take() } -> std::same_as<std::optional<typename B::completion>>;// 逐个取走
};
```

咱们再配两个类型,连同那个有讲究的 offset 字段:

```cpp
// e1_backend_concept.cpp(节选,注释为行文所加)
struct completion {
    std::uint64_t id;      // 哪一发请求完成了
    std::size_t   bytes;   // 本次搬运的字节数
    int           error;   // errno 或 GetLastError, 0 为成功
};

struct read_request {
    std::uint64_t      id;      // 咱们自己的关联凭证
    std::uintptr_t     target;  // fd 或 HANDLE, 两侧含义不同
    void*              buf;
    std::size_t        len;
    unsigned long long offset;  // 文件语义必填, 流设备忽略
};
```

offset 这个字段值得您单独看一眼。IOCP 的偏移装在 OVERLAPPED 里,io_uring 的 READ 也要求显式的偏移,而 epoll 手里只有流设备,管道与 socket 根本没有偏移的概念。一个字段收下的,是矩阵里普通文件那一维的差别:完成式的两家都吃得下文件,咱们实现的 EpollBackend 吃不下,它的 offset 也就永远闲置。咱们在本篇 e1 的 Windows 侧马上要用到它。

## EpollBackend:垫一层的适配

Linux 侧的后端,submit 干的是咱们武装兴趣的活:

```cpp
// e1_backend_concept.cpp(节选)
bool submit(request r) {
    ::epoll_event ev{};
    ev.events = EPOLLIN | EPOLLONESHOT;
    ev.data.u64 = r.target;
    int op = armed_.count(r.target) ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
    if (::epoll_ctl(epfd_, op, static_cast<int>(r.target), &ev) != 0)
        return false;
    armed_[r.target] = true;
    inflight_[r.target] = r;
    return true;
}
```

EPOLLONESHOT 干的事,是把一次注册对齐成了一次完成:事件报过一回,兴趣就自动解除了,下一发 submit 重投的时候,走的就是 MOD。完成式的语义要的就是一一对应,LT 与 ET 的内核层差异咱们沿用网络卷的引用,骨架里的 ONESHOT 已经把生命周期管住了。

wait 是咱们垫的那一下:

```cpp
int wait(int timeout_ms) {
    ::epoll_event evs[8];
    int n = ::epoll_wait(epfd_, evs, 8, timeout_ms);
    int got = 0;
    for (int i = 0; i < n; ++i) {
        auto it = inflight_.find(evs[i].data.u64);
        if (it == inflight_.end()) continue;  // 已撤单的兴趣,跳过
        request r = it->second;
        completion c{r.id, 0, 0};
        ssize_t nb = ::read(static_cast<int>(r.target), r.buf, r.len);
        if (nb < 0) {
            c.error = errno;
        } else {
            c.bytes = static_cast<std::size_t>(nb);
        }
        done_.push_back(c);
        ++got;
    }
    return got;
}
```

您看这一层垫在了哪儿。epoll_wait 醒来报的只是哪一位能读了,循环里那句 read 是适配层自己伸的手,bytes 是适配层搬出来的,而不是内核给的,error 装的也是这次 read 的 errno。对上层咱们瞒住了就绪这个中间产物,交出去的就是完成。take 则从 done_ 的队列里逐个取,倒是没什么花样。骨架只演示了读,真实的适配层还得接住 EPOLLERR 与 EPOLLHUP(L01 E6 实测内核自动补上),还得管写的一半与非阻塞的重试,咱们不装作它完整。

## IocpBackend:没有翻译的直录

Windows 侧的 submit 不需要咱们垫,它直录的是 ReadFile 加 OVERLAPPED:

```cpp
// e1_backend_concept.cpp(节选)
struct ovx {                    // OVERLAPPED 扩展: 身份随完成包回来
    OVERLAPPED ov;
    std::uint64_t id;
};

bool submit(request r) {
    inflight_.push_back(ovx{});
    ovx& x = inflight_.back();
    x.ov.Offset = static_cast<DWORD>(r.offset & 0xFFFFFFFFull);
    x.ov.OffsetHigh = static_cast<DWORD>(r.offset >> 32);
    x.id = r.id;
    BOOL ok = ::ReadFile(reinterpret_cast<HANDLE>(r.target), r.buf,
                         static_cast<DWORD>(r.len), nullptr, &x.ov);
    if (!ok && ::GetLastError() != ERROR_IO_PENDING) {
        inflight_.pop_back();
        return false;
    }
    return true;
}
```

咱们把请求的 offset 拆进了 Offset 与 OffsetHigh,再把自己的 id 挂在了 OVERLAPPED 的旁边。句柄带 FILE_FLAG_OVERLAPPED 的时候,回的 FALSE 加 997(ERROR_IO_PENDING) 是正常在途的回执,而不是失败,这与 A02 里记的同款回执是一件事。

wait 这边咱们没动过任何手脚,干的就是 GQCS 的活:

```cpp
int wait(int timeout_ms) {
    DWORD bytes = 0;
    ULONG_PTR key = 0;
    LPOVERLAPPED pov = nullptr;
    BOOL ok = ::GetQueuedCompletionStatus(port_, &bytes, &key, &pov,
                                          static_cast<DWORD>(timeout_ms));
    if (pov == nullptr) return 0;  // 超时,没有包
    completion c{0, static_cast<std::size_t>(bytes), 0};
    if (!ok) c.error = static_cast<int>(::GetLastError());
    auto it = find_by_ov(pov);
    if (it != inflight_.end()) {
        c.id = it->id;
        inflight_.erase(it);  // 完成取走,OVERLAPPED 这才可以回收
    }
    done_.push_back(c);
    return 1;
}
```

bytes 是内核填好带回来的,咱们一行搬运的代码都没写,这就是直录与适配的差别。身份走的是 pov:完成包的三件套里,key 是句柄级的,分不出同一把句柄上的哪一发,A02 的 e1 拿指针相认实测过 pov 与 key 的两级分工,所以咱们的 id 藏在扩展结构里,随完成包回来之后咱们再找回。生命周期是另一件要紧事:OVERLAPPED 必须活满在途的全程,在完成取走之前动了它,驱动可能还在往那块内存里写东西(A02 关句柄那一场的劝告),所以 `inflight_` 用的是 `std::list`,插入与删除都不搬元素的地址,vector 扩容可就保不住了。句柄挂端口另有单独的一步 attach,您开句柄的时候记得带上 FILE_FLAG_OVERLAPPED。两侧的类尾巴上各自挂了一条 `static_assert(AsyncBackend<...>)`,编不过的当场就翻脸了,而断言在两侧的编译里都是过的。

## e1:同一份用户代码,两侧各跑两轮

受约束的泛型驱动叫做 drive_one_round,这段代码咱们两侧一个字都没改:

```cpp
// e1_backend_concept.cpp(节选)
template <AsyncBackend B>
bool drive_one_round(B& backend, typename B::request r, std::size_t expect_bytes,
                     std::string_view expect_data, std::string_view tag) {
    if (!backend.submit(r)) {
        std::printf("[%s] submit 失败\n", tag.data());
        return false;
    }
    int n = backend.wait(2000);
    auto c = backend.take();
    if (!c) {
        std::printf("[%s] wait=%d, take: 无完成\n", tag.data(), n);
        return false;
    }
    std::printf("[%s] backend=%s wait=%d take: id=%llu err=%d bytes=%zu data='%.*s'\n",
                tag.data(), B::name, n, static_cast<unsigned long long>(c->id),
                c->error, c->bytes, static_cast<int>(c->bytes),
                static_cast<const char*>(r.buf));
    return c->error == 0 && c->bytes == expect_bytes &&
           std::memcmp(r.buf, expect_data.data(), expect_bytes) == 0;
}
```

编译的命令就是开头交代过的两条,两侧拿到的都是零警告,运行的输出咱们并排贴:

```text
Linux 侧(e1_linux.out):
[轮1] backend=epoll wait=1 take: id=1 err=0 bytes=5 data='hello'
[轮2] backend=epoll wait=1 take: id=2 err=0 bytes=5 data='cross'
PASS: 两轮完成事件与数据全部对上

Windows 侧(e1_windows.out):
[轮1] backend=iocp wait=1 take: id=1 err=0 bytes=5 data='hello'
[轮2] backend=iocp wait=1 take: id=2 err=0 bytes=5 data='cross'
PASS: 两轮完成事件与数据全部对上
```

两份输出差的只有 backend= 一列。Linux 的两轮来自管道的两笔写,轮 2 验的就是 ONESHOT 之后 MOD 再武装的生命周期。Windows 的两轮来自同一份数据文件的两个偏移,文件是程序自己造的,写进 20 字节的 hello cross-platform,再带着 FILE_FLAG_OVERLAPPED 把它重开了一遍,挂上了端口,偏移 0 与 6 的两发各读 5 字节,读回来的正好是 hello 与 cross,offset 字段在文件的语义下到底有没有效,两个完成包就答完了。id、err、bytes、data 四样在两侧全部对上了,咱们同一份用户代码,在两个内核上各自跑通了两轮真实的读。

## e2:缺了 take 的后端,编译期就被拦下

光有正例是不够的,咱们还得配上一个不满足约束的反面例子,不然 concept 就只是个摆设了。LazyBackend 提供了 submit 与 wait,唯独少了 take 那一项:

```cpp
// e2_negative.cpp(节选)
struct LazyBackend {
    using completion = ::completion;
    using request = ::read_request;
    bool submit(request) { return true; }
    int wait(int) { return 0; }
};

static_assert(!AsyncBackend<LazyBackend>,
              "LazyBackend 不许满足 AsyncBackend —— 缺 take 编译期就该现形");

// 与 e1 相同的受约束驱动
template <AsyncBackend B>
int drive(B& backend) {
    return backend.wait(1000) > 0 ? 0 : 1;
}
```

文件里排了两处一正一反的检查。`static_assert(!AsyncBackend<LazyBackend>)` 这一行编过了,concept 如实回答了不满足,它的回答里没有含糊,与本篇 e1 里的正例断言互为镜像。而把 LazyBackend 递给 drive 的那一步,就注定是编不过的,咱们两侧都用 `-c` 只编译不链接,要的就是诊断本身。两侧 GCC 16 的诊断,咱们把要害的几行请出来,Linux 16.2.1 与 MSYS2 UCRT64 16.1.0 给出的内容一致,差的全在字形上:Linux 侧档案里的引号是弯的,Windows 侧是直的,咱们下面的摘录按直引号排的版,而 Linux 的档案末尾,还多一行 depth 提示(全文在存档的两份 .err 里):

```text
e2_negative.cpp: In function 'int main()':
e2_negative.cpp:64:17: error: no matching function for call to 'drive(LazyBackend&)'
  • there is 1 candidate
    • candidate 1: 'template<class B>  requires  AsyncBackend<B> int drive(B&)'
      • template argument deduction/substitution failed:
        • constraints not satisfied
          ...
          • the required expression 'b.take()' is invalid
            e2_negative.cpp:40:13:
               40 |     { b.take() } -> std::same_as<std::optional<typename B::completion>>;
```

诊断把缺的东西点到了名:`the required expression 'b.take()' is invalid`,连 concept 里那一行的原文都带了出来。咱们拿 #ifdef 切平台的老写法对照,类型缺了成员的时候,错处在哪一步暴露是没有定数的,可能编过了,到链接的时候才见分晓,也可能拖到运行期才在某次调用的身上出事。concept 的拦截点是确定的:约束在实例化之前就给了裁决,连缺的是哪一项都点了名。老写法各阶段的暴露咱们不替它下统一的判词,确定的这一侧,咱们要的证据就在上面的诊断里。

## kqueue:整列都是文档的口径

矩阵里 kqueue 那一列的口径,咱们单独交代:这一列咱们一台 BSD 或 macOS 的机器都没有,每个说法都来自 FreeBSD 的 kqueue(2) 手册页,版本的口径是 15.1-RELEASE,查阅的日期是 2026-10-04,咱们一个数都没实测,也就不装作跑过了。

手册页里能跟咱们矩阵直接对上的,有这么几处。kevent 的一次调用同时收 changelist 与 eventlist,注册与收割合在了同一个调用里,man 的原话是 `All changes contained in the changelist are applied before any pending events are read from the queue`,它与 epoll_ctl 加 epoll_wait 的两步,是结构性的差异。咱们再看 struct kevent 的七个字段,`ident`、`filter`、`flags`、`fflags`、`data`、`udata` 与 `ext` 的数组,udata 的值过内核不改,正是 epoll_event.data 的同位物。触发方式在手册里没借 epoll 的词:默认报的是当前状态,挂 EV_CLEAR 的事件在取走之后复位,适合报状态变迁的那类过滤器,咱们拿 epoll 的话对译,就是水平触发与边沿式的复位,EV_ONESHOT 与 epoll 的同名同义,而 EV_DISPATCH 送出即禁用。

过滤器家族的对照也有看头。EVFILT_READ 会按描述符的类型变形:管道的话,data 里直接带回可读的字节数,而 vnode(普通文件)也能挂,文件指针不在 EOF 的时候它就报,与 epoll 对普通文件的 EPERM 拒收正好相反,而套接字上另有 NOTE_LOWAT 能调低水位。EVFILT_TIMER 走的也是过滤器的路,手册的说法是 ident 用自定义标识而不是 fd,不占描述符这一点是咱们照 ident 推的,它与 timerfd 的对照摆在矩阵超时的那一行。EVFILT_USER 加 NOTE_TRIGGER 是用户级的唤醒通道,eventfd 与 PQCS 的同位物。咱们还没提 EVFILT_VNODE,它与 inotify 是同位的,EVFILT_SIGNAL 报的是信号,EVFILT_AIO 的注册,走的是 POSIX AIO 的 sigevent。这些咱们全都没跑过,您以手册页为准。哪天您上了 Mac,拿本篇的 concept 补第三个后端,是现成的练习:它就绪式的脾气与 epoll 同类,垫的那一层会长得很像。

## string_view 过不了 printf 的 %s

骨架定稿之前咱们返工过一处。后端的名字起初写的是 static constexpr std::string_view,而 drive_one_round 里,把它整个递给了 printf 的 %s。varargs 是不认类型的,它认的只有字符指针,string_view 过 %s 就是未定义行为了。编译期的动静只有 -Wformat 一类的警告,而成不了错误,咱们当时没停下来,Linux 侧一跑就当场段错误了,退出码留的是 139。

修法倒是不起眼,名字改成 static constexpr char name[] 就完了,改完了两侧就都干净了。好在同一份代码里咱们还摆了对照,tag 递给 %s 用的是 tag.data(),那才是 string_view 的正确出口,数据指针是它明摆着的成员。把整个对象塞进 varargs 的做法,赌的是它的头一个成员碰巧是指针,而碰巧不等于保证。您写跨平台的骨架,printf 一类的接口两侧都要跑,tag.data() 那样的出口在两侧都得认。

## 三个来源拼一个 completion

咱们把 completion 的三个字段翻过来看,它们在每个后端里的出身各不相同:

| 字段 | EpollBackend(e1 实测) | IocpBackend(e1 实测) | io_uring 若接入(L03 实测) |
| --- | --- | --- | --- |
| id | 适配层自派的编号 | 藏在 OVERLAPPED 扩展里随包回来 | user_data 原样回 |
| bytes | 适配层亲手 read 搬出来的 | GQCS 的包里内核填好 | CQE 的 res |
| error | read 的 errno | GetLastError,随失败包带出 | res 为负时就是错误码 |

id 在 epoll 侧是咱们自己派的流水号,在 IOCP 侧是藏在扩展结构里随完成包回来的,真补上 io_uring 的话,它就是 SQE 的 user_data,L03 的 E1 里 0xC0FFEE 分毫不差地回来过。bytes 的三个来历同样分明:适配层搬的,内核填的,CQE 的 res。error 的那一行,epoll 侧装的是 errno,IOCP 侧装的是 GetLastError,io_uring 干脆拿 res 的负值当了错误码。三个来源拼出来的是同一个形状,concept 收住的就是这一层:差异被压在了后端内部,用户代码认的就只有 completion。

还有一样东西是咱们糙着扛过全篇的:request.target 的 uintptr_t。fd 的本体是 int,而 HANDLE 的本体是指针宽度,拿一个整数的宽度硬装下两者,在骨架里是够用的,工程里它就是留给后续的活。句柄的统一表示、错误的统一报告,思维基石的两篇([RAII 范式](../thinking/01-raii-paradigm.md)与[错误处理范式](../thinking/02-error-paradigm.md))给过零件,收编成正题是 ch07 平台抽象章的事。异步 I/O 的后端层,本篇就写到这儿了,矩阵的数字在两侧五篇的存档里,concept 的五行在正文里,您随时可以写一个自己的后端,看它接不接得住?

<ReferenceCard title="参考资源">
  <ReferenceItem
    :id="1"
    title="kqueue(2)"
    publisher="FreeBSD Manual Pages"
    url="https://man.freebsd.org/cgi/man.cgi?query=kqueue&sektion=2"
  />
  <ReferenceItem
    :id="2"
    title="Constraints and concepts"
    publisher="cppreference.com"
    url="https://en.cppreference.com/w/cpp/language/constraints.html"
  />
  <ReferenceItem
    :id="3"
    title="requires expression"
    publisher="cppreference.com"
    url="https://en.cppreference.com/w/cpp/language/requires.html"
  />
  <ReferenceItem
    :id="4"
    title="std::same_as"
    publisher="cppreference.com"
    url="https://en.cppreference.com/w/cpp/concepts/same_as"
  />
  <ReferenceItem
    :id="5"
    title="libuv"
    publisher="GitHub (libuv/libuv)"
    url="https://github.com/libuv/libuv"
  />
</ReferenceCard>
