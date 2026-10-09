# E1 fork 的语义账本

环境与总口径见[上级 README](../README.md)。编译统一 `g++ -std=c++20 -Wall -Wextra -O2`(或用上级 `run_all.sh` / CMake)。

## e1a_fork_twice —— 一次调用,两次返回

- fork() 父侧返回子进程 pid、子侧返回 0;子进程的世界从 fork 返回那一刻开始,之前的代码没有执行过
- 调用前 `fflush(stdout)` 是关键:子进程继承 stdio 缓冲区副本,不冲刷会把已缓冲的内容打两遍(这本身就是 COW 的一个副产品)
- `.out` 里父子各自打印 pid/ppid/返回值,同一对数字两种视角

## e1b_cow —— 地址不变,物理分家(Pss 证据)

64 MiB 逐页触碰的匿名缓冲,fork 前后读 `/proc/self/smaps_rollup`:

| 时刻 | Rss | Pss | 解读 |
|---|---|---|---|
| fork 前 | 69536 kB | 65813 kB | 独占 |
| 子·刚出生(未写) | 67204 kB | **32915 kB** | 与父共享,Pss 对半分账 |
| 父·fork 后(子未写) | 69600 kB | **32952 kB** | 同上,两边各付一半 |
| 子·写满 64 MiB 后 | 67268 kB | **65684 kB** | COW 触发物理复制,Pss 全额 |
| 父·子退出后再看 | 69600 kB | 65814 kB | 恢复独占 |

同一变量地址两边全程相同(`0x…b0a0`),子进程把它改成 200,父进程读到的还是 100。两边 Pss 之和从 ≈64 MiB 变 ≈128 MiB,物理复制了一份,地址一个字节都没动。看物理页帧号需要 root(`/proc/self/pagemap`),Pss 是免 root 的等价证据。

## e1c_fork_cost —— 1 GiB 父进程的创建成本

每项预热 3 轮 + 计 30 轮,CLOCK_MONOTONIC,单位 µs(min/p50/mean/max 见 `.out`),p50 摘要:

| 创建方式 | 小父进程 | 1 GiB 父进程 | 说明 |
|---|---:|---:|---|
| fork 仅创建 | 232.6 | **31585.5** | 复制页表+记账,成本随已触碰内存涨 |
| vfork 仅创建 | 71.2 | **74.8** | 不复制页表,对 1 GiB 无感 |
| fork+exec /bin/true | — | 32117.8 | 与纯 fork 同量级,exec 不是大头 |
| posix_spawn /bin/true | 486.2 | **480.7** | 一次调用,父进程多大都不影响 |

结论:fork 的成本不在拷页(页根本没拷),在拷页表——1 GiB 触碰后约 32 ms,是小进程的 136 倍;vfork/posix_spawn 走 CLONE_VM 语义,几十 µs 恒定。WSL2 单 NUMA、无 perf,数据是数量级参考。

## e1d_shared_offset —— fork 继承的 fd 共享同一份打开文件描述

父打开 fd→fork→父读 16 字节(偏移到 16)→子用继承的 fd 接着读(拿到 16..31,偏移到 32)→父再读(拿到 32..47,偏移被子的读推到 48)。对照:子自己重新 open 同一文件,读到的是 0..15——偏移各玩各的。与 L01 dup 共享偏移是同一件事,fork 是整表继承。

复现(在本目录):

```sh
mkdir -p ~/lp01_scratch   # e1d/e2b/e2c/e5b 的数据文件路径烧死在这里
g++ -std=c++20 -Wall -Wextra -O2 -o /tmp/e1a e1a_fork_twice.cpp && /tmp/e1a
# 其余同理;e1c 要吃 1 GiB 内存,e1b 要 128 MiB
```
