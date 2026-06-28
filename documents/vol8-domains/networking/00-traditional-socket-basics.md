---
title: "传统 socket 编程:服务器五步与 TCP 建链,从 Stevens 学的那套经典写法"
description: 用最朴素的 C 风格 BSD socket API 走通一个 echo server,把 socket/bind/listen/accept/read-write 五步和 TCP 三次握手的内核细节讲透——字节序、listen 的两个队列与 backlog、监听 fd 与连接 fd 的区别,以及为什么"accept 完成握手"是个常见误解。这是几十年没变的网络地基,后面的 Asio/协程再花哨,底层都是这五步
chapter: 8
order: 0
platform: host
difficulty: intermediate
cpp_standard: [20, 23]
reading_time_minutes: 16
related:
  - "现代 socket 封装:RAII 与 std::expected"
  - "epoll:Linux I/O 多路复用"
tags:
  - host
  - cpp-modern
  - intermediate
  - 网络编程
---

# 传统 socket 编程:服务器五步与 TCP 建链,从 Stevens 学的那套经典写法

说实话,写网络编程这一卷的时候,笔者是有点想先把"最土的那套"讲透的冲动。原因很简单:不管后面的 Boost.Asio 多优雅、C++20 协程多顺手、std::execution 多前卫,你在 Linux 上写网络程序,底层兜兜转转都绕不开同一套东西——1983 年定下来的 **BSD socket API**。`epoll` 是对它的加速,`Asio` 是对它的包装,但"一个服务器是怎么起来的"这件事,从 Stevens 的《UNIX 网络编程》到今天的内核,基本没变过。所以这一篇我们不碰任何现代设施,就用最朴素的 C 风格 socket,把这套**几十年没变的地基**一步一步踩实。

这一篇和下一篇是配对的:本篇(00)讲**传统的、C 风格的**写法——裸 fd、手动 `close`、`errno` 加 `perror`,就是 Beej's Guide 和 Stevens 教你的那套;下一篇(01)再用 Modern C++(RAII、`std::expected`)把它的脏活收掉。拆成两篇是有意的——有些读者已经会传统写法、只想看现代封装,可以直接跳到 01;有些读者要复习 socket 基础,本篇就是稳定的参考。BSD socket API 几十年没变,这篇写完基本不用再改。

本篇的代码就是纯 C socket,用 `gcc` 就能编,本机跑出来的终端输出都是真的。我们不掺并发(那是 01 的事),就一个单线程的 echo server,专注把"五步"走干净。

## 一个最小的目标:echo server

我们先定一个最小的目标,把后面所有概念挂上去:写一个 **echo server**——客户端连上来发什么,服务端就把什么原样发回去,然后等下一条。它是网络编程的"Hello World",小到没有业务逻辑,却完整覆盖了一条 TCP 连接从建立到收发的全过程。

这条全生命周期,从服务端的视角看,就是经典的**五步**:`socket → bind → listen → accept → read/write`。接下来我们一步一步走,每一步不光讲"调哪个函数",更要讲**内核那边到底发生了什么**——因为 socket 编程的大部分坑,都来自"你以为这个函数干了 X,其实它干的是 Y"。

## 第一步:socket()——向内核要一个通信端点

```c
int lfd = socket(AF_INET, SOCK_STREAM, 0);
```

`socket()` 干的事其实很简单:**向内核申请一个"通信端点",返回一个文件描述符(fd)来代表它**。三个参数分别回答"用什么网络协议族、什么类型、具体哪个协议":`AF_INET` 是 IPv4,`SOCK_STREAM` 是面向连接的可靠字节流(也就是 TCP),第三个参数填 `0` 表示"协议自动选"(前面定了 TCP,它就选 TCP)。

当然，这里有个绕不开的概念:**fd 到底是什么**。Unix 的名言是"一切皆文件",内核给每个进程维护一张"已打开文件表",fd 就是这张表的一个下标(一个小整数)。`socket()` 返回的 fd,本质上是这张表里的一个新槽位,槽位背后挂着一个内核的 socket 对象。后面你对这个 fd 调 `bind`/`listen`/`accept`/`read`/`write`,内核都是拿 fd 去查这张表、找到背后的 socket 对象再操作。所以 fd 只是个"句柄",真正的状态在内核里。

::: warning fd 失败要判 < 0
`socket` 和后面所有返回 fd 的调用,失败时返回 **-1** 并设置 `errno`,不是返回 0。判 `if (lfd < 0)` 才对。fd 0、1、2 进程启动时就被 stdin/stdout/stderr 占了,正常分配从 3 开始——这也是为什么我们等会儿用 `ss` 能看到监听 socket 的 fd 就是 3。
:::

## 第二步:bind()——把 fd 钉到一个本地地址

光有个 fd 还不能收连接,你得告诉内核"这个 socket 监听哪个地址、哪个端口"。这就是 `bind`:

```c
struct sockaddr_in addr;
memset(&addr, 0, sizeof(addr));
addr.sin_family      = AF_INET;
addr.sin_addr.s_addr = htonl(INADDR_ANY);   /* 监听所有网卡 */
addr.sin_port        = htons(PORT);          /* 13013 */
bind(lfd, (struct sockaddr *)&addr, sizeof(addr));
```

`sockaddr_in` 是"IPv4 地址"的结构体,三个字段要填:协议族(`sin_family = AF_INET`)、IP 地址(`sin_addr`)、端口(`sin_port`)。`INADDR_ANY` 是个特殊值,意思是"本机所有网卡的 IP 都监听"——你的机器可能有 lo(127.0.0.1)、eth0(192.168.x.x)、wlan0 等好几个 IP,`INADDR_ANY` 一次全包;如果只填某一个具体 IP,就只在那张网卡上监听。

这里有个**新人必踩的字节序**问题,值得停下来讲。注意 `htonl(INADDR_ANY)` 和 `htons(PORT)` 这两个调用——它们不是装饰。TCP/IP 规定线上传输多字节数用**大端序(big-endian)**,叫 network byte order;而你的 x86/ARM CPU 是小端序(little-endian)。端口 13013 在小端机器内存里字节排布是 `05 33 00 00`(低字节在前),大端是 `00 00 33 05`——你直接把主机序的端口塞进 `sin_port`,内核和对端都按网络序解析,端口就全错了。`htonl`(host to network long,32 位,给 IP)和 `htons`(host to network short,16 位,给端口)就是干这个字节翻转的,在大端机器上是空操作、小端机器上翻转,所以写上它**无论什么 CPU 都对**。

因此，一个重要的经验油然而生：**凡是塞进 `sockaddr_in` 的多字节整数(IP、端口),一律过一遍 `htonl`/`htons`**,别拿主机序的裸值硬塞。反过来从内核读出来要给人类看,就用 `ntohl`/`ntohs`(network to host)翻回来。

## 第三步:listen()——标记为被动监听,两个队列登场

`bind` 之后这个 socket 还不能收连接,它只是"绑了个地址"。`listen` 才把它变成一个**被动监听**的 socket,告诉内核"开始接受别人往这个地址发起的连接":

```c
listen(lfd, 64);   /* 第二个参数是 backlog */
```

`listen` 真正值得讲的是它第二个参数 **backlog**,以及它背后内核维护的**两个队列**。这是 socket 编程里被误读最多的一个点。

当一个客户端向你的服务器发起 TCP 连接时,先走**三次握手**:客户端发 SYN,你的内核回 SYN-ACK,客户端再回 ACK——握手完成,连接建立。这个过程中,内核为这个监听 socket 维护两个队列:

- **SYN 队列(半连接队列)**:收到客户端 SYN、回了 SYN-ACK、还在等客户端最后那个 ACK 的连接。握手**没完成**。
- **Accept 队列(全连接队列)**:三次握手已经完成、就等你 `accept` 把它取走的连接。握手**完成了**。

而 `listen` 的 backlog 参数,**管的是 Accept 队列(全连接队列)的容量上限**——不是很多人以为的 SYN 队列。`listen(lfd, 64)` 的意思是:最多允许 64 个已完成握手、但还没被 `accept` 取走的连接排队;超过这个数,新完成的连接会被内核丢弃或直接发 RST。SYN 队列的长度是另一回事,由 `/proc/sys/net/ipv4/tcp_max_syn_backlog` 管。

这个区别为什么重要?因为你一旦以为 backlog 管 SYN 队列,排查"连接卡在 SYN_RECV""SYN flood 攻击"时就会找错药——那得调 `tcp_max_syn_backlog` 和 SYN cookies,跟 backlog 没关系。`man 2 listen` 的原文也只说"a limit on the number of sockets in the accept queue",压根没提 SYN 队列。

我们等会儿真跑起来后,会用 `ss` 命令把这个监听 socket 的状态拍出来,你会亲眼看见 backlog 这个值落在内核里的什么位置。

## 第四步:accept()——取出一个已完成握手的连接

```c
int cfd = accept(lfd, NULL, NULL);
```

`accept` 干的事,是从那个 Accept 队列(全连接队列)里**取出一个已经完成握手的连接**,返回一个**全新的 fd** 来代表这条连接。这里有一个新手最容易糊涂的关键点,必须讲透:

**监听 fd(`lfd`)和连接 fd(`cfd`)是两个完全不同的 fd,干两件完全不同的事。** `lfd` 是"门口的迎宾",它唯一的作用是 `accept` 出新的连接;它自己**不收发数据**。`cfd` 才是"某一位已经进来的客人",你在这个 fd 上 `read`/`write` 跟这位客人通话。一个服务器终身只有一个 `lfd`(可能 `accept` 出成千上万个 `cfd`),每来一个连接 `accept` 一次、得到一个新 `cfd`。把这两个 fd 搞混(比如往 `lfd` 上 `write` 数据),是初学者常见的低级错误。

`accept` 的后两个参数填 `NULL`,表示"我不关心这个客户端的地址";如果你想记录客户端的 IP 和端口,可以传一个 `sockaddr_in` 进去让内核填。

还有个**极其常见的误解**要在这里打破:**很多人以为"是 `accept` 完成了三次握手"——这是错的**。三次握手是内核在 `listen` 之后自动做的,客户端发 SYN、内核回 SYN-ACK、客户端回 ACK,这整个过程**完全不需要你的程序参与**,发生在 `accept` 被调用之前。握手完成的连接先进 Accept 队列排着,你的 `accept` 只是把队列里已经就绪的连接"领走"。如果你的程序卡在别的地方很久不调 `accept`,握手照样在内核里完成、连接照样在队列里堆——堆满 backlog 上限才会拒绝新连接。这个时序关系我们下一节展开讲。

## 第五步:read()/write()——在连接 fd 上收发

拿到 `cfd` 之后,它就是个"可以读写的 fd"——和读写文件用的是同一套系统调用:

```c
char buf[4096];
ssize_t n = read(cfd, buf, sizeof(buf));   /* 读对方发来的 */
write(cfd, buf, n);                         /* 原样回写(echo) */
```

`read` 返回**实际读到的字节数**;返回 `0` 表示**对端正常关闭了连接**(TCP 的 FIN 走完了,这是"流结束"的信号,不是错误);返回 `-1` 表示出错。`write` 把数据发回去,它也可能只写一部分(尤其非阻塞或大数据量时),教学版里我们简化处理,下一节你会看到完整循环。

到这里五步就走完了。我们把它们串成一个能跑的 server,真跑一次。

## 建链到底发生了什么:把握手和 accept 的时序理清

在贴完整代码之前,有一个贯穿"建链"的问题值得单独理清——因为它把 `connect`(客户端)、握手(内核)、`accept`(服务端)三者的关系讲明白后,后面所有东西都好理解。

假设客户端要连我们的 server。客户端那边调 `connect(fd, 服务器地址, ...)`——这一调用的本质是**让内核替你发出 SYN**,发起三次握手。然后:

1. 客户端内核发 **SYN** → 到达服务端内核。
2. 服务端内核(因为 `listen` 过)自动回 **SYN-ACK**,同时在**服务端的 SYN 队列**里记下这条"半连接"。
3. 客户端内核收到 SYN-ACK,回最后的 **ACK** → 到达服务端内核。
4. 服务端内核收到 ACK,**三次握手完成**——把这条连接从 SYN 队列挪到 **Accept 队列**。
5. 服务端程序调用 `accept()` → 从 Accept 队列把这条连接取出来,返回新 fd。

注意第 2~4 步**全在内核里,你的服务端程序一个字都没参与**。你的 `accept` 是在第 5 步才登场的,它只是"领人"。这就是为什么"accept 完成握手"是错的——握手在第 4 步就完成了,`accept` 在第 5 步。理解这个时序,你才能理解 backlog 为什么管的是 Accept 队列(第 4 步产物)而不是 SYN 队列(第 2 步产物)。

## 经典 echo server:完整代码与真实运行

把上面五步拼起来,就是一个完整的经典 echo server。我们用最朴素的 C 风格写——裸 fd、手动 `close`、`errno` + `perror`:

```c
/* 传统 C 风格 echo server:裸 fd、手动 close、errno + perror */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT    13013
#define BACKLOG 64
#define BUFSZ   4096

int main(void) {
    signal(SIGPIPE, SIG_IGN);   /* 见下方"两个咬人的细节" */

    int lfd = socket(AF_INET, SOCK_STREAM, 0);          /* 第一步 */
    if (lfd < 0) { perror("socket"); return 1; }

    int yes = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(PORT);
    if (bind(lfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {  /* 第二步 */
        perror("bind"); return 1;
    }
    if (listen(lfd, BACKLOG) < 0) { perror("listen"); return 1; } /* 第三步 */

    printf("classic echo server on 0.0.0.0:%d (pid %d)\n", PORT, getpid());

    for (;;) {
        int cfd = accept(lfd, NULL, NULL);               /* 第四步 */
        if (cfd < 0) { if (errno == EINTR) continue; perror("accept"); continue; }

        char buf[BUFSZ];
        for (;;) {                                        /* 第五步 */
            ssize_t n = read(cfd, buf, BUFSZ);
            if (n <= 0) break;        /* 0=对端关, <0=出错 */
            write(cfd, buf, n);
        }
        close(cfd);                   /* ★手动 close,漏了就泄漏 fd */
    }
}
```

`gcc -O2 classic_server.c -o server` 编出来跑,再写个客户端连上去发两条消息:

```text
$ ./client "hello from classic client"
echo <- 'hello from classic client'
$ ./client "the quick brown fox"
echo <- 'the quick brown fox'
```

echo 通了。现在做一件更有意思的事——趁 server 跑着,用 `ss` 命令把这个监听 socket 从内核里"拍"出来看看:

```text
$ ss -tlnp | grep 13013
LISTEN 0      64           0.0.0.0:13013      0.0.0.0:*    users:(("server",pid=283539,fd=3))
```

这几列信息量很大,我们把第三步讲的那些概念和它对上:`LISTEN` 是 socket 状态;`0.0.0.0:13013` 是我们 `bind` 的地址(`INADDR_ANY` + 端口 13013);`fd=3` 正是前面说的"stdin/stdout/stderr 占了 0/1/2,第一个新 fd 是 3"。最关键的是中间那两个数字 `0` 和 `64`——对 LISTEN 状态的 socket,**第一列是 Accept 队列的当前长度(0,因为刚被 accept 取走、没积压),第二列就是 backlog 上限(64,正是我们 `listen(lfd, 64)` 设的)**。你看,第三步讲的 backlog = Accept 队列上限,在这里是能直接看到的实物,不是抽象概念。

::: tip 自己编译核查
本篇的完整代码在 `code/volumn_codes/vol8/networking/00-traditional-socket/`(`classic_server.c` + `classic_client.c` + `CMakeLists.txt`)。建议你亲手编译跑一遍,别只看:

```bash
cd code/volumn_codes/vol8/networking/00-traditional-socket
# 方式 A:一行 gcc(文章里就是这套)
gcc -O2 -Wall -Wextra classic_server.c -o echo_server
gcc -O2 -Wall -Wextra classic_client.c -o echo_client
# 方式 B:CMake
cmake -S . -B build && cmake --build build
```

然后一个终端 `./echo_server`、另一个 `./echo_client "hello"`,看到 `echo <- 'hello'` 就通了;再开第三个终端 `ss -tlnp | grep 13013`,复核上面那个 `LISTEN 0 64 ... fd=3` 的行。端口 13013 大于 1024,不用 sudo。
:::

## 两个会咬人的细节:SIGPIPE 与 SO_REUSEADDR

这套经典写法能跑,但有两个细节,都是**测试时不出问题、上线或重启才咬人**的那种——它们属于"man page 有但不会主动提醒你"的现场知识,笔者在这里必须强调一下。

**第一个是 SIGPIPE**。TCP 有个经典场景:客户端异常退出了,你的 server 还傻乎乎地往这条连接 `write`。对一个"对端已经关掉"的 socket 写数据,内核会给你发一个 `SIGPIPE` 信号,而 `SIGPIPE` 的默认处理动作是**直接终止进程**——你的 server 没有任何错误日志,就这么悄无声息地死了。这是个能让你排查一整天的坑。解法是 server 启动第一行就 `signal(SIGPIPE, SIG_IGN)` 忽略它,之后 `write` 到已关闭的 fd 会改返回 `-1`、`errno = EPIPE`,你能像处理普通错误一样处理它,而不是被信号谋杀。上面代码 `main` 第一行的就是这个。

**第二个是 SO_REUSEADDR**。你 kill 了 server 想立刻重启,经常撞 `bind: Address already in use`——明明刚才那个进程已经没了。原因是上一条 server 的某些连接还处在 **TIME_WAIT** 状态(主动关闭方会保持约 60 秒,确保对端收到了自己最后那个 FIN 的 ACK),这段时间端口还"占着"。解法是 `bind` 之前 `setsockopt(SO_REUSEADDR)`,允许复用处于 TIME_WAIT 的地址——上面代码 `socket` 之后那行就是这个。开发期频繁重启必备。注意它只解决 TIME_WAIT,不能让两个进程同时 `listen` 同一个端口(那要 `SO_REUSEPORT`,是另一个机制)。

说白了,这两个细节的共同点是:**它们都不会让你的程序"立刻崩给你看"**——SIGPIPE 是静默杀进程,TIME_WAIT 是重启才撞。所以它们特别容易被漏在测试之外,又被特别多地踩在生产里。

## 小结:这套能跑,但脏

这一篇我们用最朴素的 C 风格 socket,把服务器的五步和 TCP 建链从头走了一遍。几条关键的东西收一下:`socket` 要 fd、`bind` 钉地址、`listen` 开监听并定 backlog(管 Accept 队列)、`accept` 取出已完成握手的连接返回新 fd、`read`/`write` 收发;监听 fd 和连接 fd 是两回事;三次握手是内核在 `accept` 之前就做好的,`accept` 只是领人;字节序要 `htonl`/`htons`;SIGPIPE 和 SO_REUSEADDR 是两个 server 标配。

但你也应该嗅到了这套写法的味道——**它能跑,但脏**。裸 `int fd` 到处传,作用域里任何一条提前 `return` 都会跳过末尾的 `close(cfd)`,fd 就泄漏了;错误处理是散落的 `errno` + 返回码,每一步都"返回值 + errno"两件套,到底哪步炸的得你自己记;`perror` 打完日志,错误恢复全靠程序员自觉。这些脏活,BSD socket API 设计于 1983 年,它不管——那是后面语言和库的事。

下一篇(01)我们就用 Modern C++ 把它收掉:用 RAII 的 `unique_fd` 让"漏 close"变成不可能,用 `std::expected` 把错误和值装进类型系统。写完你再看这套传统代码,会发现它每一处"脏", Modern C++ 都有对应的、类型安全的收口。再往后我们还会回到这个 server,问它一个问题:如果客户端多了,"每来一个连接处理一个"扛得住吗?——那是引出 `epoll` 的钥匙。

## 参考资源

- [man 2 socket](https://man7.org/linux/man-pages/man2/socket.2.html) / [bind](https://man7.org/linux/man-pages/man2/bind.2.html) / [listen](https://man7.org/linux/man-pages/man2/listen.2.html) / [accept](https://man7.org/linux/man-pages/man2/accept.2.html) —— 五步 API 的权威定义,`listen` 的 backlog 原文说的就是 accept queue
- [man 7 socket](https://man7.org/linux/man-pages/man7/socket.7.html) / [man 7 tcp](https://man7.org/linux/man-pages/man7/tcp.7.html) —— socket 选项与 TCP 状态机(含 TIME_WAIT)
- [Beej's Guide to Network Programming](https://beej.us/guide/bgnet/) —— 传统 C socket 编程的经典入门,本篇的风格与内容取向深受它影响
- [W. Richard Stevens《UNIX Network Programming, Volume 1》](https://www.pearson.com/en-us/subject-catalog/p/unix-network-programming-volume-1-the-sockets-networking-api/P20000000C9XXX) —— 五步模型与两个队列的原始出处
- [ss(8)](https://man7.org/linux/man-pages/man8/ss.8.html) —— 查看内核 socket 表的工具,本篇用它拍出 LISTEN socket 的 backlog
- [现代 socket 封装:RAII 与 std::expected(下一篇 01)](./01-modern-socket-wrapping.md) —— 用 Modern C++ 收掉本篇的裸 fd 与散落 errno
