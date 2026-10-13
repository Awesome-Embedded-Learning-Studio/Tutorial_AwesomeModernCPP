# 03-shm 配套实验

《共享内存:shm_open 与映射》(vol8 systems-programming/linux/memory 内存章第 3 篇)的实验代码与原始输出存档。核心问题一句话:**共享内存给了两个进程同一块物理页,但"看得见"和"用得对"之间隔着同步**。六个实验:E1 命名对象全生命周期、E2 竞态三版对照、E3 两条共享途径、E4 招牌 SPSC 无锁环形队列、E5 与 pipe 的吞吐基线、E6 memfd_create。

## 环境口径(所有 .out 都来自这一轮)

| 项 | 值 |
|---|---|
| 系统 | WSL2,内核 6.18.33.2-microsoft-standard-WSL2 |
| 编译器 | g++ (GCC) 16.2.1,`-std=c++20 -Wall -Wextra -O2`(E2/E3/E4/E5 另加 `-pthread`) |
| glibc | 2.44——`shm_open`/`sem_*` 符号在 libc(`shm_open@@GLIBC_2.34`),**链接不需要 `-lrt`**(E1 轮已验证:不带 `-lrt` 编译链接通过;`librt.so.1` 里已无 shm_open 符号) |
| CPU | 16 逻辑核(AMD Ryzen 7 9700X 8C/16T,WSL2 视角) |
| scratch | `/home/charliechen/lm03_scratch`(路径烧死在 e1_perm.sh 里,复跑前先 `mkdir -p ~/lm03_scratch` 并把二进制编进去) |
| 权限实验 | `sudo -n` 不可用;跨 uid 用 WSL2 的 `wsl.exe -u root --` 免密 root 会话(01-lifecycle/e1_perm.sh),/dev/shm 是全发行版共享的 tmpfs,root/user 会话看到同一批实体 |

## 目录与结论对照

| 目录 | 实验 | 一句话结论 |
|---|---|---|
| `01-lifecycle/` | E1 全生命周期 | 名字空间就是 /dev/shm 里的目录项:`ls -l` 能看到实体(0600、65536 字节、属主);**未 unlink 再 O_CREAT\|O_EXCL → EEXIST(17)**;unlink 后名字消失但**已映射视图继续可读可写**,readlink /proc/self/fd 显示 `(deleted)`;unlink 后同名重建成功,**新旧两个实体互不相干**(旧映射读到的还是第一代内容) |
| `01-lifecycle/` | E1 附 跨 uid | root 建 0600 对象:user 进程 open → **EACCES**;0666 对象:user 打开成功读到 root 写的内容;反向 user 建 0600,root 照样打开(CAP_DAC_OVERRIDE,特权不是失效)。**mode 还会被进程 umask 截断**:请求 0666,umask 0022 下实际 0644 |
| `02-race/` | E2 竞态 | 两进程各加 1,000,000 次:①无同步,3 轮结果 1,526,047 / 1,628,983 / 1,807,586(**丢失 9.6%~23.7%**,丢失量不稳定——竞态的本来面目);②PROCESS_SHARED 互斥锁,3 轮全 2,000,000,中位 44.7 ms;③进程间信号量,3 轮全 2,000,000,中位 58.1 ms。汇编佐证:nosync 计数是 `addq $0x1,内存` 的**非原子**读改写(无 lock 前缀) |
| `03-two-ways/` | E3 两条途径 | 匿名 MAP_SHARED\|MAP_ANONYMOUS:fork 前映射子写父读 0xC0DE 通;fork 后各自新建的匿名映射互不相通(0x0)——匿名没有名字无从相认。命名对象:server/client 两个**无亲缘**进程靠 `shm_open("/lm03_two")` 相认。附 SCM_RIGHTS 传 fd 一句话演示(fd 传到即实体传到,细讲留 ch03) |
| `04-spsc-queue/` | E4 招牌 SPSC | shm_open 实体里放 head/tail 各占一条 cache line 的无锁环形队列(1024 槽 × 32 B),producer 发 1,000,000 条:**received=1000000,order_err=0,corrupt=0**,两版一致;spin 版中位 6.7 ms(148 百万条/秒),yield 版(队列满时 sched_yield 背压)中位 3.8 ms(264 百万条/秒) |
| `04-spsc-queue/` | E4 附 钉核对照 | yield 比 spin 快 60%+,钉死两核(CPU0/CPU1)仍如此——**同核调度不是主因**;主因是自旋方反复 acquire-load 的正是对方每条消息都要写的 tail 行,cache line 乒乓吃掉吞吐;sched_yield 让生产者安静、消费者成批追进度,吞吐反升(单条延迟另说) |
| `05-ipc-baseline/` | E5 选型基线 | 同一 1 MiB(1024 B × 1024 块,逐块校验):共享内存环形队列中位 **7304 MiB/s**,pipe 分 1024 次写读中位 **2349 MiB/s**,约 **3.1 倍**——pipe 每块都要跨内核(copy in/copy out + 两次系统调用),shm 写完对方直接看得见 |
| `06-memfd/` | E6 memfd | `memfd_create` 不进 /dev/shm(名字只是 `/proc/self/fd` 里的标签,readlink 显示 `/memfd:lm03_memfd (deleted)`);同名再建是两个独立实体;close 归零即消失,**没有 unlink 这一步**;给 SCM_RIGHTS 传 fd 铺路(ch03) |

## E2 三版原始数据

每人 1,000,000 次,期望 2,000,000:

| 版本 | r1 | r2 | r3 | 中位 |
|---|---|---|---|---|
| nosync 结果 | 1,526,047 | 1,628,983 | 1,807,586 | 1,628,983(丢 18.6%) |
| mutex 结果/耗时 | 2,000,000 / 60.4 ms | 2,000,000 / 44.7 ms | 2,000,000 / 39.8 ms | 2,000,000 / 44.7 ms |
| sem 结果/耗时 | 2,000,000 / 57.5 ms | 2,000,000 / 60.7 ms | 2,000,000 / 58.1 ms | 2,000,000 / 58.1 ms |

## E4/E5 吞吐汇总(中位数)

| 实验 | 版本 | r1 | r2 | r3 | 中位 |
|---|---|---|---|---|---|
| E4 SPSC(百万条/秒) | spin | 148.2 | 164.0 | 142.3 | 148.2 |
| E4 SPSC(百万条/秒) | yield | 264.3 | 267.6 | 248.6 | 264.3 |
| E4 钉核(百万条/秒) | spin | 152.5 | 161.7 | 162.7 | 161.7 |
| E4 钉核(百万条/秒) | yield | 256.8 | 276.9 | 266.3 | 266.3 |
| E5(MiB/s) | shm 环形队列 | 7343 | 7304 | 3917 | 7304 |
| E5(MiB/s) | pipe 1024 B 块 | 2349 | 2169 | 2390 | 2349 |

## 复跑

```bash
cd 01-lifecycle && g++ -std=c++20 -Wall -Wextra -O2 e1_lifecycle.cpp -o e1_lifecycle && ./e1_lifecycle
# e1_perm.sh 需要先把 e1_perm 编到 /home/charliechen/lm03_scratch/ 且 WSL2 interop 可用
cd 02-race && g++ -std=c++20 -Wall -Wextra -O2 -pthread e2_race.cpp -o e2_race && ./run_e2.sh
cd 03-two-ways && g++ -std=c++20 -Wall -Wextra -O2 -pthread e3_two_ways.cpp -o e3_two_ways && ./run_e3.sh
cd 04-spsc-queue && g++ -std=c++20 -Wall -Wextra -O2 -pthread e4_spsc.cpp -o e4_spsc && g++ -std=c++20 -Wall -Wextra -O2 -pthread e4_spsc_pinned.cpp -o e4_spsc_pinned && ./run_e4.sh
cd 05-ipc-baseline && g++ -std=c++20 -Wall -Wextra -O2 -pthread e5_ipc.cpp -o e5_ipc && ./run_e5.sh
cd 06-memfd && g++ -std=c++20 -Wall -Wextra -O2 e6_memfd.cpp -o e6_memfd && ./e6_memfd
```

## 踩坑记录(写文章时可引用)

1. **volatile 是给编译器看的,不是给核间同步用的**:E2 若不用 volatile,编译器有权把 load/add/hoist 出循环,单进程视角直接优化成"读一次加 N 存一次",丢更新的戏就演不成了。但 volatile 依然不提供原子性——丢失更新照丢。
2. **fork 后子进程别直接 `_exit` 前靠 printf 输出**:stdio 缓冲不冲,E3 fdpass 首轮子进程的输出就这么丢过(`_exit` 不走 exit handler),要么 `fflush(stdout)` 要么 `write()`。
3. **输出接管道(tee)时 stdout 变全缓冲**:server/client 两个独立进程的输出顺序会乱,`setvbuf(stdout, nullptr, _IOLBF, 0)` 按行冲。
4. **E2 计时口径**:起跑线原子标志统一放行,计时含起跑到两个 waitpid 收完,不含 fork 与初始化。
5. **wsl.exe 的输出带 CR**(CRLF),入册 .out 统一 `sed 's/\r$//'`。
6. **umask 会截断 shm_open 的 mode**:想要精确 0666 得先 `umask(0)`(e1_perm.cpp 就这么干的),否则默认 WSL umask 0022 下 other 没有读写位。
