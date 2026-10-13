# 02-delivery-default-exit —— E2 事件到线程的投递与默认退出码

`e2_delivery.out` 两幕:

**Phase A:handler 线程的生命周期。** 连发两次 CTRL_C:两次 handler 的 tid 分别为 30380、11700,与主线程 26004 三个互不相同——**每次事件一条新线程**(W03 已证「新线程」,这里补全:每次事件各起一条,不复用)。随后 OpenThread 探针:活着的对照线程打开成功(句柄 0xD0),两条 handler tid 都打不开(gle=87, ERROR_INVALID_PARAMETER)——**handler 返回后线程即逝**,不给复用留机会。

**Phase B:无 handler 的默认路径。** 父进程注册 handler(返回 TRUE)后 CreateProcess 一个不注册任何 handler 的炮灰子进程(共享控制台、同组,先自复位忽略位),父发事件:

| 事件 | 炮灰退出码 | 死亡耗时 | 对照:父进程 |
|---|---|---|---|
| CTRL_C_EVENT(0) | **0xC000013A**(-1073741510)= STATUS_CONTROL_C_EXIT | 94ms | handler 被调,活着 |
| CTRL_BREAK_EVENT(1) | **0xC000013A**(同一个码) | 93ms | 同上 |

0xC000013A 即 winnt.h 的 `STATUS_CONTROL_C_EXIT`,这就是「Windows 世界 SIGINT 默认动作」的落点——Linux 默认 SIGINT 是内核收尸 wait status 被 SIGINT 标记,Windows 是默认 handler 调 `ExitProcess(0xC000013A)` 自走,码写在进程退出码里,谁 wait 谁看得见。

`victim_selfsend.cpp` + `victim_selfsend.out`:单独一个「复位忽略位 + 自己给自己发 CTRL_C」的最小程序,用 `cmd /v:on` 的 `!ERRORLEVEL!` 读全码,交叉验证 `ERRORLEVEL=-1073741510`,与父进程 GetExitCodeProcess 读数一致(WSL `$?` 只剩低 8 位,读不出全码)。

注意细节:炮灰必须先 `SetConsoleCtrlHandler(NULL, FALSE)`,否则继承的忽略位让 CTRL_C 根本不来,炮灰死不了——这个「复位才死得了」本身就是免疫机制的又一次展示(E3 展开)。

## 复现

```sh
/mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra e2_delivery.cpp -o e2_delivery.exe
chmod +x e2_delivery.exe && ./e2_delivery.exe
# cmd 全码口径(先拷 victim_selfsend.exe 到 C:\msys64\tmp)
/mnt/c/Windows/System32/cmd.exe /v:on /c "C:\\msys64\\tmp\\victim_selfsend.exe < nul & echo !ERRORLEVEL!"
```
