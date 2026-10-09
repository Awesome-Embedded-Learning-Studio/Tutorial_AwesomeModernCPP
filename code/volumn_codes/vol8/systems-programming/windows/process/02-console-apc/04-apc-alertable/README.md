# 04-apc-alertable —— E4 APC 队列:排队≠执行,alertable 二态 + FIFO

时序是全部证据(`e4_apc.out`,GetTickCount64 打点):

```
t=91861281  main:QueueUserAPC 两条排完(worker 此刻卡在不可警告的 WaitForSingleObject)
t=91861281  worker 进 SleepEx(600, FALSE) —— 不可警告睡眠
t=91861890  worker 醒:APC 执行数 = 0        ← 排队 609ms,一条没跑
t=91861890  worker 进 SleepEx(5000, TRUE) —— 可警告睡眠
t=91861890  [APC#1] tid=1320 arg=111        ← 开闸瞬间执行
t=91861890  [APC#2] tid=1320 arg=222        ← 紧跟着,FIFO:先排先跑
t=91861890  worker 返回 192(WAIT_IO_COMPLETION),可警告睡眠实际只睡 609ms
```

三个结论:

1. **二态时序**:`bAlertable=FALSE` 的等待里,已排队的 APC 一条不执行(609ms 的非可警告睡眠全程 0 次);同一线程一进 alertable 等待,队列立刻清空式执行,等待同时被打断提前返回。APC 的全部主动权在目标线程手里——「你睡了可告警觉我才开口」。
2. **复用原线程**:两条 APC 的 tid=1320 与 worker 自己的 tid 相同。对照控制台事件每次新起线程(E2),这是两种异步注入的哲学差:控制台事件另起炉灶,APC 借目标线程的躯壳插播。也因此 APC 天然与被插线程的 TLS/锁状态同体,文章展开时这是它「能打断等待」的资格来源。
3. **FIFO**:先排的 APC#1 恒在 APC#2 前(同 tick 内保序)。

## 复现

```sh
/mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra e4_apc.cpp -o e4_apc.exe
chmod +x e4_apc.exe && ./e4_apc.exe
```
