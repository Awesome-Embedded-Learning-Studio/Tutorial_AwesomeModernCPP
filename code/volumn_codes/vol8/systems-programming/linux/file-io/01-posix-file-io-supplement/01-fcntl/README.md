# 01-fcntl —— fcntl 两族:fd 标志 vs 文件状态标志(E1)

环境:WSL2 内核 6.18.33.2,g++ 16.2.1 `-std=c++20 -Wall -Wextra -Wpedantic -O2`,数据文件在 ext4(`~/l01b_scratch/e1/`,路径烧死在源码里,复跑前先 `mkdir -p`)。

## 结论(对照 `fcntl_flags.out`)

两族标志住在 kernel 的不同层级上,这是 fcntl 一切语义的分水岭:

| | F_GETFD / F_SETFD | F_GETFL / F_SETFL |
|---|---|---|
| 管的东西 | fd 标志,实际就一位 `FD_CLOEXEC` | 文件状态标志(`O_APPEND`/`O_NONBLOCK`/`O_ASYNC`…)+ 只读的访问模式 |
| 住在哪 | 进程 fd 表的**表项**上,每个 fd 一份 | **打开文件描述**上,dup 出来的 fd 共享一份 |
| dup/F_DUPFD 复制件 | **不带**(要带得重新 F_SETFD,或用 dup3/F_DUPFD_CLOEXEC) | **共享**(一边改 O_NONBLOCK,另一边 F_GETFL 立刻可见) |
| 常见用途 | open 时忘了 O_CLOEXEC,事后补 | open 之后改 O_APPEND/O_NONBLOCK 的唯一途径 |

各观察点:

| 场景 | 结果 |
|---|---|
| a) F_GETFD 出生值 | 0(open 没带 O_CLOEXEC);F_SETFD(FD_CLOEXEC) 后变 1,F_SETFD(0) 可清 |
| b) F_DUPFD(n) | 与 dup 等价但可指定下限:`fcntl(fd,F_DUPFD,20)`=20、`F_DUPFD_CLOEXEC,20`=21;三个复制件 F_GETFD = 0/0/CLOEXEC |
| c) F_GETFL 解码 | 返回「访问模式 + 状态标志」;访问模式必须 `fl & O_ACCMODE` 抠出来再比 |
| d) F_SETFL 是整体覆盖 | 对带 O_APPEND 的 fd 直接 `F_SETFL(O_NONBLOCK)`,O_APPEND 被冲掉;正确姿势 F_GETFL → 按位或 → F_SETFL |
| e) F_SETFL 改不动访问模式 | 对 O_RDONLY 的 fd 塞 O_RDWR:调用返回 0(成功!),F_GETFL 访问模式纹丝不动,write 照样 -1/EBADF |
| f) 改标志别重开 fd | fd 写了 10 字节偏移在 10;重新 open 的 fd 偏移是 0(新描述);F_SETFL 摘 O_APPEND 后原 fd 偏移还是 10 |
| g) F_SETFL + O_NONBLOCK | FIFO(O_RDWR 自持写端)空时 read 立刻 -1,errno=11(EAGAIN,与 EWOULDBLOCK 同值) |
| h) 归属总对照 | 原件/复制件:O_NONBLOCK 有/有,FD_CLOEXEC 有/无 |

意外发现:F_GETFL 的 raw 值永远比 open 传的 flags 多一位 `0100000`(八进制,0x8000)——内核固定带回 `O_LARGEFILE` 位(64 位 off_t 标记),而 glibc 在 64 位平台把宏 `__O_LARGEFILE` 定义成 0(用户态的 `O_LARGEFILE` 跟着是 0),想按位测都测不着。所以拿 F_GETFL 与 open flags 判相等必错,只能按位与。

E4 说明(不实验):`F_GETOWN`/`F_SETOWN` 设定「谁来收 SIGIO」,与 F_SETFL 的 O_ASYNC 配套构成信号驱动 I/O 的入口,归异步 I/O 线再展开;`F_NOTIFY`(目录变更通知)是 Linux 老接口,已被 inotify 取代(06-inotify 目录有完整实验),此处跳过。

EINTR 重试(fcntl 被信号打断)不重复做:`linux/thinking/02-error-paradigm/02-eintr-retry/` 已有 sys_call 异常版/expected 版 + SA_RESTART 对比的完整实验。

## 复现

```sh
mkdir -p ~/l01b_scratch/e1
g++ -std=c++20 -Wall -Wextra -Wpedantic -O2 -I ../common fcntl_flags.cpp -o /tmp/e1
/tmp/e1 | tee fcntl_flags.out   # .out 是 2026-10-02 WSL2 台机那轮的捕获
```

秒级跑完,无阻塞等待(FIFO 用 O_RDWR 打开,不需要对端)。
