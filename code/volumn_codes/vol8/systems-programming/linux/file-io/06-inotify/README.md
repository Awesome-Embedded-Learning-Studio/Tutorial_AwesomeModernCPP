# 06-inotify 配套实验

《inotify 文件监控》(vol8 systems-programming/linux/file-io L06)的实验代码与原始输出存档。核心问题一句话:**inotify 是一只「绑 inode、按目录粒度报增量」的内核侧事件泵,它不递归、不保证不丢、事件数不能当操作数**;递归要自己搭,可靠性要自己对账,合并行为要认清「四元组全同且相邻才折叠」。

## 环境口径(所有 .out 都来自这一轮)

| 项 | 值 |
|---|---|
| 机器 | 台机,AMD Ryzen 7 9700X 8-Core(16 线程) |
| 系统 | WSL2,内核 6.18.33.2-microsoft-standard-WSL2 |
| 编译器 | g++ (GCC) 16.2.1,`-std=c++20 -Wall -Wextra -Wpedantic -O2`(零警告) |
| 数据盘 | /dev/sdd ext4;E6 的跨设备侧用 /dev/shm(tmpfs) |
| 内存 | 52 GiB |

`/proc/sys/fs/inotify/` 本机实际值(各 .out 开头也打印了):

| 参数 | 值 | 含义 |
|---|---|---|
| `max_user_watches` | 524288 | 每真实用户所有实例合计的 watch 上限 |
| `max_user_instances` | 1024 | 每真实用户的 inotify 实例(fd)数上限 |
| `max_queued_events` | 16384 | 单实例队列深度,超了报 IN_Q_OVERFLOW |

**路径是烧死的**:源码把数据写在 `/home/charliechen/l06_scratch/eN/`(E6 另用 `/dev/shm/l06_e6_C`),`common/` 里是 `errno_code`/`sys_call`/`unique_fd` 三件契约工具,外加 inotify 专属的 sysctl 读取 / mask 位解码 / 事件流读取器。复跑前先 `mkdir -p ~/l06_scratch`,或改各 `.cpp` 开头的路径常量。

## 目录与结论对照

| 目录 | 实验 | 一句话结论 |
|---|---|---|
| `01-event-panorama/` | E1 事件全景 | `sizeof(struct inotify_event)=16`;一次 open(O_CREAT) 挤出 IN_CREATE+IN_OPEN 两条;mv 同目录改名 = IN_MOVED_FROM/IN_MOVED_TO 各一条、**cookie 同值**(实测 8596)配对;mkdir/rmdir 的事件 mask 里叠着状态位 IN_ISDIR(0x40000000);被 watch 的目录自身被删 = IN_DELETE_SELF + IN_IGNORED(name 空、len=0),之后 `inotify_rm_watch` 返回 EINVAL——watch 已被内核自动摘除 |
| `02-dir-vs-file/` | E2 目录 vs 文件 | watch 只挂 A 时 `touch A/B/y` **0 个事件**(不递归实锤);给 B 补挂后同一动作在 B 的 wd 上全量报出(事件归属按被 watch 的目录算);IN_ONLYDIR 挂在文件上 = ENOTDIR(errno 20);文件级 watch 的 name 恒空(len=0);watch 绑 inode 不绑路径——文件改名后 watch 仍跟着走(IN_MOVE_SELF),原路径换新 inode 后写入 **watch 全程沉默**,旧 inode unlink = IN_ATTRIB→IN_DELETE_SELF→IN_IGNORED 三连 |
| `03-recursive-watch/` | E3 递归监控 | walk 全树挂 watch + IN_CREATE\|IN_ISDIR 时动态补挂(必须整棵补:`mkdir -p a/b/c` 的事件到手时孙目录早已存在,只挂一层会漏);7 个目录 7 只 watch,占 524288 的 0.0013%;目录整棵改名时只有被改名的 inode 本身报 IN_MOVE_SELF,**后代的 watch 全静默但 wd 表里的路径已过期**——追路径的 watcher 得在 MOVE_SELF 时改表 |
| `04-queue-merge/` | E4 队列:合并与溢出 | 读者跟上:10 写 = 10 条,零合并;读者不读 + 同文件背靠背 200 写 = **1 条事件**(四元组 wd/mask/cookie/name 全同且相邻 → 内核入队时折叠);读者不读 + 4 个文件轮流 200 写 = **200 条一条不少**(名字不同不算「同一条」);4 写者灌 1.5 s 约 587 万次写,读回 **16385 条 = 16384 条 IN_MODIFY + 1 条 IN_Q_OVERFLOW(wd=-1, 0x4000)恰好垫在队尾**——此后写者仍在写但再无事件入队 |
| `05-epoll-loop/` | E5 inotify × epoll | inotify 实例就是 fd,与 timerfd(500ms)、pipe(300ms)同挂一个水平触发 epoll;`epoll_wait` 的 events[] 里三类 fd 混着返回,一个循环统一调度——「一切皆 fd」在文件监控上的落点,与 ch04 多路复用衔接 |
| `06-cross-dir-move/` | E6 搬移检测 | 同设备跨目录 mv:A 侧 IN_MOVED_FROM(wd=1)与 B 侧 IN_MOVED_TO(wd=2)**cookie 同值**(实测 8787)——同一次搬移在两侧各报一次;跨设备(ext4→tmpfs)`rename(2)` 直接 EXDEV(errno 18),mv 退化为复制+删除:源侧 OPEN/ACCESS/CLOSE_NOWRITE/DELETE、目的侧 CREATE/MODIFY/CLOSE_WRITE/ATTRIB,**cookie 全程为 0,事件流里不存在「这次搬移」** |

## 事件合并的实测口径(E4,易传歪,单独钉死)

1. **合并发生在内核入队时,条件苛刻**:新事件与**队列尾部**事件的 wd、mask、cookie、name 四者全同才折叠成一条。读者跟上(每次写后立刻读)时队列总是空的,根本没有「前一条」可叠 → 零合并。
2. **「同文件连续写合成一条」不是缩水是语义**:200 次写 1 条事件,IN_MODIFY 不能当写次数计数器。
3. **名字一换就断**:轮流写 4 个文件 200 次一条不合并。所谓「IN_MODIFY 会被合并」的准确说法是「完全相同且相邻的事件会被合并」,不是「同类事件都会合并」。
4. **溢出的账没法对**:`max_queued_events=16384` 满后,内核把此后的事件全部丢弃,只补一条 wd=-1 的 IN_Q_OVERFLOW(实测垫在队尾第 16385 条)。溢出前入队的 16384 条读得回;溢出期间丢了多少(实测约 585 万条)没有任何记录——监控端要么读得快,要么应用层对账。

## 复现

```sh
# 单发(在 06-inotify 目录下)
g++ -std=c++20 -Wall -Wextra -Wpedantic -O2 -I common 01-event-panorama/event_panorama.cpp -o /tmp/e1

# 或整套 CMake
cmake -S . -B build && cmake --build build
```

E6 需 `/dev/shm` 可写(WSL2 默认 tmpfs,满足);跨设备结论依赖「A 在 ext4、C 在 tmpfs」,若复跑机器的 /dev/shm 不是独立文件系统,组二会退化成同设备行为。

## 输出档案口径

全部 `.out` 是同一轮(台机 9700X,2026-10-02)的捕获,文章引用的数字与它们逐个对得上。会变的项目:cookie 值、pid、相对毫秒时间戳、E4 写者的完成次数与溢出前后具体是哪个文件的事件(4 写者交错,每轮不同)、E5 的时序抖动。**cookie 是内核全局计数器**(本轮按捕获时间排是 E1→E2→E6→E5,cookie 依次 8596/8619/8787/8809 跨程序递增可见一斑;E6 编号在后但捕获在前,实验编号与运行顺序不同步):配对判定只能在自己 fd 的事件流内做,别拿 cookie 数值本身当信息。

## 实现坑备忘(复刻 watcher 的人会踩)

- **fork 前必须 `fflush(stdout)`**:子进程带着父 stdio 缓冲区副本退出,会把旧账重复写进重定向的文件(E4 第一版踩中,输出整段重复 4 次)。
- **`_exit()` 不冲 stdio**:子进程里 printf 的内容要手动 `fflush`,否则蒸发(E4 写者计数第一轮丢失)。
- **fork + waitpid 前先关命令管道写端**:不关就是父等子退、子等命令的经典死锁(E1 第一版踩中)。
- **`mkdir` 不递归**:scratch 根目录要先建(E2/E3/E5 第一版踩中);父目录不随各实验的 `rm` 清掉,复跑时要在 mkdir 上容忍 EEXIST(E5/E6 第一版踩中)。
- **同步不需要 sleep**:事件由写方 syscall 在返回前同步入队,管道握手 ack 到达即可读,零竞态。
