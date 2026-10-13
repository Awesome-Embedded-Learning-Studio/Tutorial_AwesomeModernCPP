# 05-apc-interrupt —— E5 温和掐断卡在等待里的线程

正用加对照(`e5_apc_interrupt.out`):

**[1] 正解**:w1 卡在 `WaitForSingleObjectEx(never, INFINITE, TRUE)`(真句柄永不触发,可警告)。main 400ms 后 `QueueUserAPC(wake_w1, 42)`:APC 在 w1 原线程里跑,等待**提前返回 192(WAIT_IO_COMPLETION = 0xC0 = STATUS_USER_APC)**,全程只撑了 407ms,w1 检查 reason、收栈、return 0——干净退出,退出码 0,没人 TerminateThread。这就是「温和终止卡死线程」的正解:前提是线程等在可警告等待上,APC 是把它叫醒的唯一外部手段。

**[2] 对照**:w2 卡在不可警告的 `WaitForSingleObject(never, INFINITE)`。main 排 APC(reason=7)再观察 800ms:w2 状态 258(STILL_WAITING,还卡着),APC 执行数 0——**排不进去执行**。SetEvent 放行后 w2 醒,但它从头到尾没进过可警告点,线程一退,积压的 APC **作废**(reason_w2 终值 0,APC 函数一次没跑)。两个负结果都有:不可警告等待掐不动 + 退出即丢队列。

工程含义:给线程发 APC 之前先确认它会进 alertable 等待(SleepEx/WaitForSingleObjectEx/WaitForMultipleObjectsEx 带 TRUE),否则白排。IOCP 钩子:`GetQueuedCompletionStatus` 的完成投递底层走的就是这套 APC 机制(ch04 展开,此处只留接口:完成端口把「IO 完成回调」以 APC 形态挂到等待线程上,所以线程池工作线程天然在可警告等)。

## 复现

```sh
/mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra e5_apc_interrupt.cpp -o e5_apc_interrupt.exe
chmod +x e5_apc_interrupt.exe && ./e5_apc_interrupt.exe
```
