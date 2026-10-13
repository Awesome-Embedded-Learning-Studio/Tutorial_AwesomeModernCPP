# 01-select-poll-epoll 配套实验

《select→poll→epoll 全景》(vol8 systems-programming/linux ch04 L01)的实验代码与原始输出存档。核心问题一句话:**同样一批管道 fd,三个等待 API 的行为边界与成本曲线各在哪里**。epoll 的机制课(兴趣表/就绪队列/等待队列)在网络卷讲透了,本篇只做三兄弟的 API 行为对比与衔接。

与相邻卷的分工:
- 网络卷 02-epoll 篇已讲:poll 的 O(n) 分析、epoll 内核模型、LT/ET 机制与 socket echo 实战。本篇不重复,实验全部用管道 fd。
- vol5 ch06 已讲:阻塞/非阻塞概念、事件循环架构、协程与 epoll 的衔接。本篇不碰协程。

## 环境口径(所有 .out 都来自这一轮)

| 项 | 值 |
|---|---|
| 机器 | 台机,AMD Ryzen 7 9700X 8-Core |
| 系统 | WSL2,内核 6.18.33.2-microsoft-standard-WSL2 |
| 编译器 | g++ (GCC) 16.2.1,`-std=c++20 -O2 -Wall -Wextra -D_FORTIFY_SOURCE=2` |
| 默认 RLIMIT_NOFILE | soft=hard=1048576(WSL2 出厂就很高,实验里用 setrlimit 现场调) |

`-D_FORTIFY_SOURCE=2` 是 E1 必需的:它让 glibc 对 `FD_SET(fd>=FD_SETSIZE)` 的越界写出具守卫,行为可复现(Ubuntu 系 GCC 开优化时常默认带,这里显式写明)。

## 目录与结论对照

| 文件 | 实验 | 一句话结论 |
|---|---|---|
| `e1_select_limit.*` | E1 上限在谁那里 | FD_SETSIZE=1024 是 glibc 的 fd_set 位图尺寸,不是内核的界:带守卫的 FD_SET(1024) 直接 SIGABRT;位图给足后 select(nfds=1026) 对 fd=1025 照常报告就绪,RLIMIT_NOFILE 压到 1024 也不拦 select。真正查 rlimit 的是 poll:soft=1024 时 poll(1050 个条目) 返回 EINVAL。同一批 1050 个 fd,poll(高位 rlimit 下)与 epoll 全部可用 |
| `e2_scan_cost.*` | E2 一次醒来扫多少 | 500 根管道、1 根有数据:select 检查 1002 个 fd(按 fd 号扫到 nfds)、poll 检查 500 条、epoll 只看返回的 1 个。64 根有数据时三者检查数分别是 1002/500/64。select 的位图进出内核各 128 B,poll 每轮 4000 B 进出,epoll 只出就绪项 12 B/个 |
| `e3_copy_growth.*` | E3 每轮成本随 N 的增长 | 只有 1 个 fd 就绪的稳态循环:poll 从 2527(N=64)→12815(500)→104460(4096) ns/轮,线性;epoll 三档都是 ~990 ns,与 N 无关。select 在 500 档 14363 µs,比 poll 慢:它按 fd 号扫描,500 根管道 nfds≈1000 |
| `e4_inout_destroy.*` | E4 select 改写入参 | 事件在 300ms 到来、预算 2s:返回后 timeout 结构体剩 1.699s(Linux 改写入参)。fd_set 同样被消耗:不重建直接再调,300ms 超时返回 0,B 的新事件被漏掉;重建后 0ms 立即就绪 |
| `e5_lt_et_pipe.*` | E5 LT/ET 管道对照 | 60000 字节在管、每醒读 4096:LT 连醒 15 次自然收敛;ET 只醒 1 次,55904 字节无人再报,直到新写入 1 字节触发新边沿,按纪律 14 次读空到 EAGAIN。机制讲解见网络卷 |
| `e6_fdinfo_interest.*` | E6 兴趣表直接可见 | /proc/self/fdinfo/<epfd> 的 tfd 行就是内核兴趣表:ADD 三个 fd 出现三行,DEL 即消失。events 字段是内核加工过的:注册 EPOLLIN 实际记 0x19(自动补 EPOLLERR\|EPOLLHUP)。eventfd 的 counter 也在自己的 fdinfo 里(eventfd-count) |

## E3 计时表(原始数据,单位 ns,2000 轮 x 5 次取中位;.out 表头印的 us/round 是脚本笔误,写手同机重编重跑核实为纳秒,正文引用处已当场更正)

| N | select | poll | epoll |
|---|---|---|---|
| 64 | 2825.96 | 2527.21 | 994.39 |
| 500 | 14363.19 | 12814.89 | 993.85 |
| 4096 | (fd 超界) | 104460.23 | 985.20 |

每轮固定成本含 write/wait/read 三次系统调用与一次线程唤醒,所以绝对值带本机口径;看斜率就好。复跑一轮(见 `e3_rerun.out`)三档结构与中位数一致。select 只测到 500:一根管道吃两个 fd,500 根就把 fd 号顶到 1000 上下,再大 FD_SET 的位图就装不下了。

## 复现

```sh
g++ -std=c++20 -O2 -Wall -Wextra -D_FORTIFY_SOURCE=2 -o e1_select_limit  e1_select_limit.cpp
g++ -std=c++20 -O2 -Wall -Wextra -D_FORTIFY_SOURCE=2 -o e2_scan_cost     e2_scan_cost.cpp
g++ -std=c++20 -O2 -Wall -Wextra -D_FORTIFY_SOURCE=2 -o e3_copy_growth   e3_copy_growth.cpp
g++ -std=c++20 -O2 -Wall -Wextra -D_FORTIFY_SOURCE=2 -o e4_inout_destroy e4_inout_destroy.cpp
g++ -std=c++20 -O2 -Wall -Wextra -D_FORTIFY_SOURCE=2 -o e5_lt_et_pipe    e5_lt_et_pipe.cpp
g++ -std=c++20 -O2 -Wall -Wextra -D_FORTIFY_SOURCE=2 -o e6_fdinfo_interest e6_fdinfo_interest.cpp

./e1_select_limit    # 秒级;子进程会按预期 SIGABRT,主进程收尸后继续
./e2_scan_cost       # 秒级
./e3_copy_growth     # 约 1-2 分钟
./e4_inout_destroy   # 约 2 秒
./e5_lt_et_pipe      # 约 1 秒
./e6_fdinfo_interest # 秒级
```

## 复跑注意

- E1 的子进程 SIGABRT 是实验本体,不是失败;abort 信息走 stderr,`.out` 里混排在开头属正常。
- E3 首轮管道是空的,代码里已预置首字节;若改造代码,注意 wait 超时返回 0 后紧跟的 read 会无限期阻塞(第一版就卡在这里)。
- E6 的 fdinfo 行格式随内核版本变化(6.18 有 eventfd-semaphore 字段),复跑以本机实际输出为准。
- 计时类(E3)复跑时避免同时跑其他吃 CPU 的任务;本仓另两篇的计时实验同理。
