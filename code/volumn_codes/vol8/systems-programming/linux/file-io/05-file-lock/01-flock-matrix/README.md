# 01-flock-matrix —— flock 语义矩阵(E1)

环境:WSL2 内核 6.18.33.2,g++ 16.2.1 `-std=c++20 -Wall -Wextra -Wpedantic -O2`,数据文件在 ext4(`~/l05_scratch/e1/flock.bin`,路径烧死在源码里)。

## 结论(对照 `flock_matrix.out`)

| 场景 | 结果 |
|---|---|
| a) LOCK_EX 互斥 | A 持锁 400 ms 后 `LOCK_UN`,竞争者(自己 open 的新描述)阻塞版在同一毫秒拿到锁。注意:fork 出来的子进程继承同一描述,**测不出互斥**,竞争者必须自己 open |
| b) 同 fd 重复加锁 | 返回 0,flock 在同一描述上是「转换」语义(EX→EX、EX→SH 都成功) |
| b) dup(fd) | 立即成功:dup 复制的是同一描述的引用,不产生第二把锁 |
| b) 重新 open | `LOCK_EX\|LOCK_NB` = -1/EWOULDBLOCK,**同进程也会自冲突**;阻塞版自锁死——子进程对 fd2 阻塞 flock,2 s alarm 到点被 SIGALRM 击杀,从未返回 |
| c) LOCK_NB | 冲突时 -1,errno=11(EWOULDBLOCK,Linux 上与 EAGAIN 同值) |
| d) close 隐式释放 | A `close(fd)`(全程没有 LOCK_UN),竞争者同毫秒拿到 |
| e) fork 继承 | 锁属于打开文件描述:子进程在继承 fd 上 `flock(LOCK_EX)` 直接返回 0;子进程 `LOCK_UN` 放掉的是**父进程的锁**(竞争者立刻拿到);子进程 close 继承 fd **不放锁**(父的 fd 还引用着同一描述);父也 close(最后一个引用)后竞争者才拿到 |

## 复现

```sh
mkdir -p ~/l05_scratch/e1
g++ -std=c++20 -Wall -Wextra -Wpedantic -O2 -I ../common flock_matrix.cpp -o /tmp/e1
/tmp/e1 | tee flock_matrix.out   # .out 是 2026-10-02 台机 9700X 那轮的捕获
```

时长约 2.5 s(b 场景的自锁死取证固定等 2 s)。
