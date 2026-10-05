# E1 实时信号:范围、排队、带数据、到达顺序

## 结论(对照 rt_signal.out)

1. **范围**:本机 `SIGRTMIN=34  SIGRTMAX=64  __SIGRTMIN=32`。glibc(NPTL)内部占用 32/33 两个号做线程管理,所以 libc 的 SIGRTMIN 从 34 起——用户可用实时信号 31 个(34..64)。直接用内核号 32/33 发 `kill` 会干扰 NPTL,别碰。
2. **排队**:阻塞期连发 3 次 SIGRTMIN+1(sigqueue 带编号 101/102/103)→ 解阻塞后 handler 跑满 3 次、编号 FIFO;对照 SIGUSR1 连发 3 次 → handler 只跑 1 次。实时信号有队列,标准信号 pending 位图只有一位(接上篇 Lproc04 的结论)。
3. **带数据**:`sigqueue` 的 `sival_int` 在 SA_SIGINFO handler 的 `si_value` 原样读回;`sival_ptr=&payload` 同进程内读回后可解引用(实测指回原变量),跨进程只是个地址数字。注意 `union sigval` 是联合——发 `sival_int=101` 时 handler 打印的 `sival_ptr=0x65` 就是同一字节换种读法,不是 bug。
4. **到达顺序(本机 6.18 实测,三路证据)**:
   - d3 `sigwaitinfo` 同批 pending {12,35,10,34} → 取出顺序 10→12→34→35;
   - d2 `sa_mask`=全屏蔽(handler 期间其他号进不来)同批 → 执行顺序 10→12→34→35;
   - d1 空 `sa_mask` 一次解阻塞 {SIGRTMAX, SIGRTMIN+1×3} → 执行顺序 64 先、然后 35×3(FIFO)。
   - 解读:**内核出队=小号优先**(man 7 signal(7):"they are delivered starting with the lowest-numbered signal. (I.e., low-numbered signals have highest priority.)");空 sa_mask 批量放行时,多个信号帧按出队序叠栈、后叠的先执行,所以 handler 看到的执行顺序是「大号先跑」的倒挂——**不是内核挑大号**。同号 FIFO 在两种情况下都严格成立(每投递一个,该号自身即被挡住,不会嵌套)。
   - 工程结论:跨号顺序别依赖 handler 执行顺序;要顺序消费用 sigwaitinfo/signalfd(它们按出队序小号优先,见 E2/E5)。

## 复现

```sh
g++ -std=c++20 -Wall -Wextra -O2 rt_signal.cpp -o /tmp/e1 && /tmp/e1
```

单进程,无外部依赖,亚毫秒跑完。
