# 02-fcntl-matrix —— fcntl 记录锁语义矩阵(E2)

环境:WSL2 内核 6.18.33.2,g++ 16.2.1 `-std=c++20 -Wall -Wextra -Wpedantic -O2`,数据文件在 ext4(`~/l05_scratch/e2/fcntl.bin`,路径烧死在源码里)。持锁者/竞争者/探针全部是独立 fork 的子进程,管道握手,不靠 sleep 赌时序。

## 结论(对照 `fcntl_matrix.out`)

| 场景 | 结果 |
|---|---|
| a) 字节区间 | A 锁 [0,100):B 试 [50,150) → -1/EAGAIN(11),[100,200) → 0;A `_exit(0)` 后 B 立刻拿到 [0,50) |
| b) F_GETLK | 探针 [0,200) → 撞上 `F_WRLCK l_pid=<A的pid>, 区间[0,100)`——**报的是对方锁自己的区间,不是与探针的交集**(部分相交的探针 [50,60) 同样报 [0,100));无交集 → `l_type=F_UNLCK` |
| c) 读写组合 | R+R 共享成功;R+W、W+R、W+W 全部 EAGAIN |
| **d) close 任意 fd 全释放(全篇最大陷阱)** | 本进程 fd1 上锁 [0,100) → 竞争者被挡;**open 第二个 fd(只读都行)再 close(fd2)——fd1 还开着、一次 LOCK_UN 都没调——竞争者立刻拿到**:本进程在该文件上的全部记录锁已被释放。变体二(锁之前就 open 好的 fd0,close 它)同样全释放。man 2 fcntl_locking 原话:如果进程关闭**指向该文件的任何**文件描述符,该进程在此文件上的全部锁都会被释放 |
| d′) OFD 对照 | `F_OFD_SETLK`(Linux 3.15 起)把锁挂在打开文件描述上:close 无关的 fd2 后锁**原样保留**,close 持锁的 fd1 才释放;F_GETLK/F_OFD_GETLK 探它都报 `l_pid=-1`(OFD 锁无属主 pid) |
| e) 替换语义 | 同进程 fd1 W[0,100) 后经 fd2 再 W[0,100) = 0(POSIX 锁按进程算,同进程永不自冲突——与 flock 的「重新 open 自锁死」正好相反);再在 fd1 上 R[50,150) → 重叠段被替换,内核切成 W[0,50) + R[50,150):读探针 [0,50) 撞 W,读探针 [50,150) 无冲突,写探针 [50,150) 撞 R |
| f) 进程退出 | 持锁者直接 `_exit(0)`(不 unlock 不 close),竞争者立刻拿到 |

## 复现

```sh
mkdir -p ~/l05_scratch/e2
g++ -std=c++20 -Wall -Wextra -Wpedantic -O2 -I ../common fcntl_matrix.cpp -o /tmp/e2
/tmp/e2 | tee fcntl_matrix.out
```

秒级跑完。errno 口径:Linux 上 F_SETLK 冲突报 EAGAIN(与 EWOULDBLOCK 同值 11),man 另提 EACCES 是其他实现可能给的。
