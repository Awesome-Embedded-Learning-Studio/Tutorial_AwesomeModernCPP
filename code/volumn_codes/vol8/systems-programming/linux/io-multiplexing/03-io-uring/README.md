# 03-io-uring 配套实验

《io_uring》(vol8 systems-programming/linux ch04 L03)的实验代码与原始输出存档。核心问题一句话:**提交与完成两个共享内存环,怎么把等待 I/O 变成收割完成事件**。Proactor(先提交、完成再通知)与 Reactor(先等就绪、再动手)的分野,以及链式请求与超时请求,都在环上展开。

与相邻篇卷的分工:
- Reactor 模式的工程展开(事件循环、连接管理)在网络卷 03-reactor 与 vol5 ch06,本篇不重开课,只拿行为对照。
- 裸系统调用的编号表风格承接 ch03 信号下篇的收尾(434/424/438 那张表),本篇的 425/426/427 是同一条谱系。
- epoll 的边界(普通文件 EPERM)在 01 篇 E4 实测,本篇 u4 从 io_uring 的角度补对照面。

## 两个前置探针(先于一切实验)

| 探针 | 结果 |
|---|---|
| 裸 syscall `io_uring_setup`(编号 425) | 可用。`io_uring_setup(8)` 返回 ring fd,features=0x3ffff(NODROP/SUBMIT_STABLE/RW_CUR_POS/CUR_PERSONALITY/SINGLE_MMAP 全部置位)。见 `probe_uring.*` |
| liburing 在不在 | 在。pkg-config 认 2.15,`/usr/include/liburing.h` 就位,`io_uring_queue_init/submit/wait_cqe` 全链可用。见 `liburing_hello.*` |

两个探针都通过,所以本目录走双轨:E1 用裸系统调用把机制摆在明面上(衔接 ch03 的编号表),E2 起用 liburing 讲用法。

## 环境口径(所有 .out 都来自这一轮)

| 项 | 值 |
|---|---|
| 机器 | 台机,AMD Ryzen 7 9700X 8-Core |
| 系统 | WSL2,内核 6.18.33.2-microsoft-standard-WSL2(io_uring 全功能可用,SQPOLL 也能建) |
| 编译器 | g++ (GCC) 16.2.1,`-std=c++20 -O2 -Wall -Wextra $(pkg-config --cflags --libs liburing)` |
| 数据文件 | 程序自建在 `/home/charliechen/ch04_scratch/`(源码顶部常量,复跑前先建目录或改常量) |
| 计时 | CLOCK_MONOTONIC |

## 目录与结论对照

| 文件 | 实验 | 一句话结论 |
|---|---|---|
| `probe_uring.*` | 前置探针 | 见上表。汇编进 .out 的还有 features 位图与 sq/cq entries 的默认配比(请求 8 得 sq=8/cq=16) |
| `liburing_hello.*` | 前置探针 | liburing 2.15 一个 NOP 走通 submit/wait/data 三件套 |
| `u1_bare_ring.*` | E1 裸系统调用建环 | 三段 mmap(SQ 环 208B/CQ 环 192B/SQE 数组 256B,请求 4 项时)在 /proc/self/maps 里就是两条 `anon_inode:[io_uring]` 映射;手工填 SQE、放 SQ 尾指针、一次 enter(1,1,GETEVENTS)、从 CQ 收到 user_data=0xC0FFEE res=0。SINGLE_MMAP 置位说明 SQ/CQ 同块区域,分开映射是经典三段式 |
| `u2_batch_read.*` | E2 批量读 | 一次 submit 提交 64 个 4KiB 读(各自带偏移与 user_data 块号),一次 wait_cqes(64) 等齐,其余 63 个收割全是用户态 peek;块号-偏移-内容校验全过。read(2) 同样的活要 64 次系统调用,io_uring 是 submit 1 次+wait 1 次 |
| `u3_chained.*` | E3 链式请求 | read→write→fsync 三请求挂 IOSQE_IO_LINK 一次提交,三个 CQE 按序完成(4096/4096/0);链头换成坏 fd,CQE 变 -EBADF/-ECANCELED,下游 write 没有执行(目标文件偏移 4096 处仍是零)。顺序保证与失败传播都在内核侧 |
| `u4_epoll_regular.*` | E4 普通文件的边界 | O_NONBLOCK 在普通文件上设得上去但语义不生效(read 永不 EAGAIN,有数据直接给);epoll_ctl ADD 普通文件返回 EPERM(man 的解释:目标 fd 不支持 epoll)。两头都关死,磁盘文件的统一异步只剩 io_uring 的 READ(E2 已验,完成事件带真实字节数) |
| `u5_batch_economics.*` | E5 批量经济性 | 16MiB 页缓存热的 4KiB 读:read(2) 4096 次系统调用 2.1-2.8ms,io_uring 256 一批(submit 16 次+wait 16 次,其余 peek)2.4-2.6ms,墙钟打平、系统调用数差 128 倍。页缓存命中时 io_uring 不赢墙钟,它赢的是完成路径异步化与慢设备下的批量摊薄。SQPOLL 探针:WSL2 上 IORING_SETUP_SQPOLL 建环成功,submit 只写共享环不进内核,内核线程自己把 NOP 干完 |
| `u6_timeout_requests.*` | E6 超时也是请求 | 两个独立 TIMEOUT 反序提交(350ms 先、100ms 后),完成按墙钟到点(t+100 收 100ms,t+350 收 350ms,res 都是 -ETIME);read(空管道)挂 LINK_TIMEOUT 300ms,到点 read 被 -ECANCELED;同样组合在 100ms 时喂 5 字节,read 正常完成 res=5、超时请求被 -ECANCELED 撤掉。对照 02 篇的 timerfd:那边是 fd 进表,这边是请求进环 |

## E5 计时(两轮对照,16MiB/4KiB/页缓存热)

| 方案 | 第一轮 | 复跑 | 系统调用 |
|---|---|---|---|
| read(2) 逐块 | 2.1 ms | 2.8 ms | 4096 |
| io_uring 256 一批 | 2.4 ms | 2.6 ms | submit 16 + wait 16 |

## 复现

```sh
gcc -O2 -Wall -o probe_uring probe_uring.c
g++ -std=c++20 -O2 -Wall -Wextra -o liburing_hello liburing_hello.cpp $(pkg-config --cflags --libs liburing)
g++ -std=c++20 -O2 -Wall -Wextra -o u1_bare_ring        u1_bare_ring.cpp        $(pkg-config --cflags --libs liburing)
g++ -std=c++20 -O2 -Wall -Wextra -o u2_batch_read        u2_batch_read.cpp       $(pkg-config --cflags --libs liburing)
g++ -std=c++20 -O2 -Wall -Wextra -o u3_chained           u3_chained.cpp          $(pkg-config --cflags --libs liburing)
g++ -std=c++20 -O2 -Wall -Wextra -o u4_epoll_regular     u4_epoll_regular.cpp    $(pkg-config --cflags --libs liburing)
g++ -std=c++20 -O2 -Wall -Wextra -o u5_batch_economics   u5_batch_economics.cpp  $(pkg-config --cflags --libs liburing)
g++ -std=c++20 -O2 -Wall -Wextra -o u6_timeout_requests  u6_timeout_requests.cpp $(pkg-config --cflags --libs liburing)

mkdir -p ~/ch04_scratch          # 数据文件目录(u2/u3/u4/u5 自建数据)
./probe_uring && ./liburing_hello && ./u1_bare_ring     # 秒级
./u2_batch_read && ./u3_chained && ./u4_epoll_regular   # 秒级
./u5_batch_economics                                    # 约 10 秒(含预热)
./u6_timeout_requests                                   # 约 1 秒
```

u1 只用内核 uapi 头(`<linux/io_uring.h>`),不需要 liburing 也能编(链命令里带着无害)。

## 复跑注意

- u5 的两轮数字在噪声内互有胜负,引用时说"打平"别说"谁更快";绝对值带本机口径,慢盘或冷缓存下结论会变。
- u4 的 EPERM 只在 epoll_ctl ADD 普通文件时出现,管道/socket/设备文件不受影响,别把结论扩大。
- u6 的 res=-62 是 -ETIME(超时到点),res=-125 是 -ECANCELED(被取消),.out 里没有符号名,对照这两个数读。
- SQPOLL 在别的环境可能因 sysctl(kernel.io_uring_group)或权限建不起来,u5 里探针失败时会如实打印 errno,不算实验失败。
