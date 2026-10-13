# 02-exit-wait:E2 退出码与等待

文件:`e2_exit_wait.cpp` / `e2_exit_wait.out`(模式:`exitcode`/`abort`/`sleepexit <ms> <code>`;无参=驱动)。

## 三节结论

**[1] 三种退出形态的 GetExitCodeProcess 读数**

| 死法 | 实测退出码 | Linux 对照 |
|---|---|---|
| main return 42 | `42 (0x0000002A)` | exit code 42 |
| `abort()` | `3221226505 (0xC0000409)` = fail-fast(STATUS_STACK_BUFFER_OVERRUN 被复用作 __fastfail 退出码) | waitpid 见 WIFSIGNALED + WTERMSIG=6,根本不是"退出码" |
| 父 TerminateProcess(h, 4660) | `4660 (0x00001234)` 原样透传 | kill -9 后靠 wait 拿信号,退出码维度不存在 |

(d) STILL_ACTIVE 陷阱:孩子没退时查询给 259;真退后 77——**拿退出码判死活会撞 259**,等句柄才是正路。

**[2] 句柄只是观察权**
CloseHandle(hProcess) 后睡 300ms,OpenProcess(pid) 照样成功(进程活着),重开的句柄等到退出、拿到孩子自选的 5。关闭句柄扔掉的是"我们的观察权",内核对象活到最后一只句柄/引用。

**[3] WFMO 三视角**(三孩子 1800/500/1200ms,码 11/22/33)
- (a) 逐个 `WaitForSingleObject(h,0)` 轮询:真实完成序 22(t+547ms)→33(t+1250ms)→11(t+1844ms)
- (b) 一次 `WFMO(bWaitAll=FALSE)`:阻塞到任一信叼,返回 `WAIT_OBJECT_0+1`(最先完成者的索引),t+516ms
- (c) `WFMO(bWaitAll=TRUE)`:一次收完,再按句柄序逐个 GetExitCodeProcess 对账
- 对照点:waitpid(-1) 只说"有个结束了",是谁要再问;WFMO 返回值直接给索引

## 复现

```sh
cd 02-exit-wait
/mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra -municode e2_exit_wait.cpp -o e2_exit_wait.exe
chmod +x e2_exit_wait.exe && ./e2_exit_wait.exe > e2_exit_wait.out 2>&1
```

注意:家长先 `SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX)`(子进程继承 error mode),abort 的 fail-fast 不会弹 WER 挂住跑批。时序毫秒数每轮浮动,量级不变。
