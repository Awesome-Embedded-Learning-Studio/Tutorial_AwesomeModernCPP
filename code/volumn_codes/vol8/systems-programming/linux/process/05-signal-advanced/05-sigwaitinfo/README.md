# E5 sigwaitinfo/sigtimedwait:不开 handler,在指定点同步取信号

## 结论(对照 sigwait_lab.out)

1. **基本款**:[a] 阻塞 {SIGUSR1,SIGUSR2},子进程发一个 sigqueue(带值 11)+ 一个 kill,主进程 `sigwaitinfo` 两次取回:si_pid 是发送方、si_value 原样到账、si_code 分别是 SI_QUEUE/SI_USER——全程零 handler,信号被挡在门外,由主流程在指定点亲手取。
2. **超时**:[b] `sigtimedwait` 空等 200ms → 返回 -1/EAGAIN,实测耗时 200ms;timeout 置 nullptr 就是 sigwaitinfo(永久等),置 0 就是「非阻塞清点」(见 [c])。
3. **排队对照**:[c] 阻塞期连发 3 次 SIGRTMIN+2 → sigwaitinfo 连取 3 次按 FIFO;SIGUSR2 连发 3 次 → 非阻塞清点只取到 1 次。与 E1 的 handler 版同款结论,换个消费接口结论不变。
4. **与 signalfd 互吃(同一队列的两个消费者)**:[d]
   - d1:入队两条(21/22),`sigwaitinfo` 先吃掉 21,`read(signalfd)` 只剩 22;
   - d2:反过来入队 31/32,`read(signalfd)` 先吃 31,`sigtimedwait` 拿到 32。
   - pending 队列只有一条(跟着阻塞信号集走),sigwaitinfo 和 signalfd 是它的两个消费接口——谁读了谁消费,不重复、不打架。工程含义:一个信号集别同时开两个消费者,选一个。
5. **三条出路**:handler 异步抢跑(上篇)/ sigwaitinfo 定点同步取(适合「专职信号线程」:主线程全部阻塞,一个线程 sigwaitinfo 循环)/ signalfd 事件循环里当 fd(本篇 E2/E4)。同一阻塞集合,三种消费姿势。

## 复现

```sh
g++ -std=c++20 -Wall -Wextra -O2 sigwait_lab.cpp -o /tmp/e5 && /tmp/e5
```

单程序(一个短命子进程),约 0.2s 跑完(大头是 [b] 的 200ms 超时)。
