# 04-flock-vs-fcntl —— 继承行为对照与总表(E4)

环境:WSL2 内核 6.18.33.2,g++ 16.2.1,数据文件在 ext4(`~/l05_scratch/e4/inherit.bin`)。六个场景全是真机实测(对照 `inheritance.out`);**NFS 一列除外——WSL2 没有 NFS 挂载,如实记录「未测」**,理论口径引 man(表后附出处)。

## 实测六场景

| 场景 | 结果 |
|---|---|
| fork × flock | 子进程在继承 fd 上 `flock(LOCK_EX)` = 0(同一描述,不与父冲突);子进程 `LOCK_UN` 放掉的是父的锁(竞争者立刻拿到);子进程 close 继承 fd 不放锁(父的 fd 还引用着描述) |
| fork × fcntl | 子进程用继承 fd `F_SETLK W[0,100)` = -1/EAGAIN——锁不随 fork 继承,子进程是独立进程;`F_GETLK` 报 `l_pid=<父pid>` |
| dup × flock | close(fd1)(dup 出的 fd2 还在)→ 锁保留;close(fd2)(最后一个引用)→ 锁释放 |
| exec × flock | 持锁子进程 exec 自己(无 O_CLOEXEC):锁活过 exec,窗口内竞争者被挡;exec 后进程退场(最后一个 fd 关闭)才释放 |
| exec × flock × O_CLOEXEC | fd 带 O_CLOEXEC:exec 瞬间 fd 关闭,锁**当场释放**,窗口内竞争者直接拿到 |
| exec × fcntl | 上锁的子进程亲自 exec:锁活过 exec(属于同一进程),窗口内竞争者被挡;进程退出才释放 |

## flock vs fcntl 对照表(文章主表素材)

| 维度 | flock(2) | fcntl(2) 记录锁(F_SETLK 族) |
|---|---|---|
| 作用域 | 整个文件 | 任意字节区间(l_start/l_len,l_len=0 到 EOF) |
| 锁属于谁 | **打开文件描述** | **进程**(F_SETLK/F_SETLKW);F_OFD_SETLK 变体属于打开文件描述(Linux 3.15 起) |
| fork | 子进程共享同一把锁(继承的是同一描述);子进程可替父加/放锁 | 不继承;子进程是独立属主,会与父冲突 |
| exec | fd 不关就活着;O_CLOEXEC 则随 fd 关闭当场释放 | 锁随进程活过 exec(fd 关不关无所谓) |
| **close 语义** | 关掉引用该描述的**最后一个** fd 才释放(引用计数) | **close 该文件的任意一个 fd → 本进程全部记录锁释放**(E2d 陷阱;OFD 锁无此陷阱) |
| 同进程第二次 open | 自冲突,阻塞版自锁死 | 永不冲突(按进程算),后锁覆盖重叠段 |
| 阻塞等待 | 阻塞版无超时;LOCK_NB 非阻塞 | F_SETLKW 阻塞无超时;F_SETLK 非阻塞(EAGAIN) |
| 探测 | 无(只能试) | F_GETLK/F_OFD_GETLK 报出冲突锁的 pid 与区间 |
| tmpfs | 与 ext4 同语义(E7 实测) | 与 ext4 同语义(E7 实测) |
| NFS | ≤2.6.11 不锁;≥2.6.12 客户端**模拟成 fcntl 区间锁**(两族锁在 NFS 上互相作用);2.6.37 起 nfs 挂载选项 local_lock 可退回本地锁 | 客户端记录锁;有丢锁风险(NFSv4 租约默认 90 s、`nfs.recover_lost_locks` 默认 0),远端属主的 l_pid 呈现因版本而异 |
| NFS 实测 | **未测**(WSL2 无 NFS 挂载) | **未测** |

## NFS 一列的出处(理论口径,未实测)

- man 7 flock(2),VERSIONS/NFS:"Up to Linux 2.6.11, flock() does not lock files over NFS";自 2.6.12 起 NFS 客户端把 flock 模拟为「对整个文件的 fcntl 字节区间锁」,因此两族锁在 NFS 上会互相作用,且排他锁要求文件以写方式打开;2.6.37 起 `local_lock=n` 选项(nfs(5))把锁当本地锁处理。
- man 2 fcntl_locking(2):NFS「lost locks」(服务器管理动作/网络分区会丢锁,Linux 3.12 起 NFSv4 下持有的锁丢后 I/O 可能 EIO);NOTES 里 NFSv4 leasetime 默认 90 s、`nfs.recover_lost_locks` 因数据损坏风险默认 0。
- man 2 fcntl_locking(2) 关于继承:"Record locks are not inherited by a child created via fork(2), but are preserved across an execve(2)."(与上表 fork/exec 两行实测一致。)
- man 2 F_OFD_SETLK(2const):open file description locks "available since Linux 3.15"。

## 复现

```sh
mkdir -p ~/l05_scratch/e4
g++ -std=c++20 -Wall -Wextra -Wpedantic -O2 -I ../common inheritance.cpp -o /tmp/e4
/tmp/e4 | tee inheritance.out    # 约 2 s;exec 场景的时间戳从 0 重计是预期(换了映像)
```
