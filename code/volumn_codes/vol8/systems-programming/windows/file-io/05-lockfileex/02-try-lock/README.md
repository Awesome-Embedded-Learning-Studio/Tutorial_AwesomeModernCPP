# 02-try-lock —— E2 有限等待:轮询版与 OVERLAPPED 加餐

LockFileEx 的参数表里没有「等多久」——阻塞版一等到底,`LOCKFILE_FAIL_IMMEDIATELY` 只会立刻回绝,这与 Linux 侧 flock/F_SETLKW 完全同病。两条出路都实测:

## try_lock_poll.cpp —— 出路人人都一样的轮询

`FAIL_IMMEDIATELY + Sleep(10ms)` 小步轮询,等待分辨率=轮询间隔(与 Linux 侧 E5 的 1ms usleep 同构,粒度粗一点)。

- 场景 1(holder 握 600ms,限期 3000ms):第 32 次尝试在 500ms 处拿到,其余 31 次全是 FALSE+33。
- 场景 2(holder 握 1200ms,限期 200ms):14 次尝试全数 33,限期一到返回 false,没有死等。

## try_lock_overlapped.cpp —— Windows 独有的正路(加餐)

同步句柄上会卡死的同一句 `LockFileEx`,换 `FILE_FLAG_OVERLAPPED` 打开的异步句柄就不卡了:冲突时立刻返回 FALSE+997(ERROR_IO_PENDING),批准与否落在 `OVERLAPPED.hEvent`(手动重置)上。于是:

- 场景 1:`WaitForSingleObject(hEvent, 3000)` 在 500ms 处 WAIT_OBJECT_0,`GetOverlappedResult`=TRUE——**原生 try_lock_for,一次也没轮询**。
- 场景 2:200ms 限期 → WAIT_TIMEOUT → `CancelIoEx(h, &ov)` 定向撤单 TRUE → 收尾 `GetOverlappedResult`=FALSE+995(ERROR_OPERATION_ABORTED);holder 退场后新句柄探针 TRUE——**被撤销的请求没有留下幽灵锁**。存在「取消与批准赛跑」的窗口(批准恰好先落地),代码里对 TRUE 分支补了解锁,本轮实测未撞上。

文档依据(LockFileEx Remarks):"The LockFileEx function operates asynchronously if the file handle was opened for asynchronous I/O … the function returns the error ERROR_IO_PENDING. The system will signal the event specified in the OVERLAPPED structure after the lock is granted."

注意两条工程细节:异步句柄上的解锁要配 `UnlockFileEx`(带 OVERLAPPED);事件必须手动重置(否则一次置位后状态泄漏)。

## 复现

```sh
cd /mnt/c/msys64/tmp/l05win
/mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra try_lock_poll.cpp -o try_lock_poll.exe
./try_lock_poll.exe demo
/mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra try_lock_overlapped.cpp -o try_lock_overlapped.exe
./try_lock_overlapped.exe demo
```
