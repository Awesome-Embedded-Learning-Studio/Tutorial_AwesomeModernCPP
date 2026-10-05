# E2 signalfd:信号化为 fd

## 结论(对照 signalfd_lab.out)

1. **纪律「先阻塞再创建」**:[a] 不阻塞 SIGUSR2 就创建 signalfd,信号发出后传统 handler 计数=1、`poll(signalfd, 200ms)` 返回 0——信号被传统路(handler/默认动作)当场消费,signalfd 饿死。不阻塞的信号永远到不了 signalfd。
2. **互斥**:[b] 先阻塞再创建,连发 3 次 SIGUSR2(标准信号,pending 1 次),read(signalfd) 拿到 1 条,随后解阻塞 handler 计数仍是 0——**读走即消费,handler 不会再触发**。两条出路抢的是同一条 pending 队列(与 E5 d 的 sigwaitinfo 互吃互证)。
3. **批量读**:[c] 子进程 kill 一个 + 乱序 sigqueue 两个实时信号,一次 `read` 返回 384 字节 = 3 条 `signalfd_siginfo`。read 的返回值恒是 `sizeof(signalfd_siginfo)`(本机 128)的整数倍。
4. **字段**:跨进程发送时 `ssi_pid` 是发送方 pid(39853=子进程);`ssi_code`:kill → `SI_USER`(0),sigqueue → `SI_QUEUE`(-1);出队同样小号优先(12→36→37,与 E1 d3 一致)。
5. **SFD_NONBLOCK 进 poll**:[d] signalfd 与 timerfd(150ms 周期)挂同一个事件循环,子进程在 t≈200/400ms sigqueue 两个信号——时间线 150/201/300/401/450/600 交错,信号与定时器在同一循环统一调度。这是上篇 E5 self-pipe trick 的内核原生版:不再需要 pipe + handler 里的 write。
6. **动态扩掩码**:[e] 对已有 sfd 再调一次 `signalfd(sfd, &mask, flags)` 即可加号,fd 不换;**实测(独立 fcntl 探针四格验证):改掩码那次调用的 flags 动不了 `FD_CLOEXEC` 位,创建时带没带就定了性**(创建带、改时不带,位还在;创建不带、改时带上,也不补上)。man 2 signalfd 对改掩码与 CLOEXEC 的关系无此说,早期版本此处写反过。

## siginfo_t ↔ signalfd_siginfo 字段对照

| siginfo_t(handler/sigwaitinfo 读) | signalfd_siginfo | 本实验的取值 |
|---|---|---|
| `si_signo` | `ssi_signo` | 12/36/37 |
| `si_code` | `ssi_code` | SI_USER=0(kill)/ SI_QUEUE=-1(sigqueue) |
| `si_pid` | `ssi_pid` | 发送方 pid |
| `si_uid` | `ssi_uid` | 1000 |
| `si_value.sival_int` | `ssi_int` | sigqueue 带的 int |
| `si_value.sival_ptr` | `ssi_ptr` | sigqueue 带的指针值 |

(其余 `ssi_errno/ssi_fd/ssi_tid/ssi_band/ssi_overrun/ssi_addr...` 对应定时器/IO/硬件错误等别的来源,本实验不触发。)

## 复现

```sh
g++ -std=c++20 -Wall -Wextra -O2 signalfd_lab.cpp -o /tmp/e2 && /tmp/e2
```

单程序(内部 fork 两个短命子进程),约 0.8s 跑完;[d] 的时间戳数值每次微抖,交错结构稳定。
