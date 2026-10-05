# 01-overlapped 配套实验

《OVERLAPPED 异步 I/O 与 WaitForMultipleObjects》(vol8 systems-programming/windows ch04 L01)的实验代码与原始输出存档。核心问题一句话:**同一把句柄,同步用法读一次挪一步,异步用法一次能压进去 N 发在途请求,各自从 OVERLAPPED.Offset 起读、各自带着完成信号回来**;收割侧 WaitForMultipleObjects 一次至多等 64 枚,这堵墙在哪、怎么量,也在这里。OVERLAPPED 结构在 file-io/05(锁篇)已经出场过(Offset/OffsetHigh 装区间起点、997/995 两个错误码),本篇是它在读请求上的正主;ReadFileEx 完成例程兑现 process/02 的前向指针。

## 环境口径(所有 .out 都来自这一轮)

| 项 | 值 |
|---|---|
| 系统 | 宿主 Windows 11 26200(26H2 线) |
| 编译器 | MSYS2 UCRT64 g++(Rev 5)16.1.0,`-std=c++20 -Wall -Wextra`(零警告) |
| 跑法 | WSL 侧 `cd /mnt/c/msys64/tmp/wasync && /mnt/c/msys64/ucrt64/bin/g++.exe … && ./xxx.exe` |
| 数据 | `C:/msys64/tmp/wasync/e1data.bin`(e1 自建,65536 字节,8 字节块存自己的偏移);在途场景全部用命名管道(服务端写、客户端异步读) |
| 计时 | GetTickCount64 毫秒级相对时戳,每行 stdout 前缀 `[ N ms]` |

## 目录与结论对照

| 实验 | 一句话结论 |
|---|---|
| `e1_forms` | 三种形态一屏对齐:同步句柄裸读推进文件指针、EOF 回 TRUE+0/TRUE+剩余(file-io/01 口径回放);**同步句柄带 OVERLAPPED 读,调用仍阻塞、永不回 997,但文档写明返回前会同时更新 OVERLAPPED 偏移和文件指针(实测指针从 16 跟到 8008)**;异步句柄上这轮全是 FALSE+997 在途,GetOverlappedResult 收尾,骑 EOF 回 TRUE+部分字节、正对 EOF 回 FALSE+38(ERROR_HANDLE_EOF),裸读回 87,SetFilePointer 推到 8000 后从 Offset=0 读到的还是 0 值(指针被架空) |
| `e2_readfileex_apc` | ReadFileEx 完成例程只在可警告等待里跑:数据 120/160ms 就进了管道,500ms 不可警告等待里例程执行数=0;一进 `WaitForSingleObjectEx(...,TRUE)` 两条例程同 tick 连跑(FIFO),等待以 192(WAIT_IO_COMPLETION)提前返回;hEvent 塞哨兵读回原样(ReadFileEx 不碰它) |
| `e3_cancel` | CancelIoEx 定向撤单收 995;CancelIo 全量(但只撤**调用线程**发的请求:worker 线程发的读,主线程 CancelIo 后 GOR(FALSE) 仍 996 ERROR_IO_INCOMPLETE,CancelIoEx 不点名才撤掉);定向撤 A 放过 B;撤完同句柄再投一发照常完成 |
| `e4_wfmo_limit` | 两堵墙分开量:**WaitForMultipleObjects 的墙在 64/65 之间**(64 枚点名索引 63 照常返回,**65 枚 WAIT_FAILED+87**);**MsgWaitForMultipleObjectsEx 的墙在 63/64 之间**(QS_ALLINPUT 的消息队列自己占一个名额,文档原话 "MAXIMUM_WAIT_OBJECTS minus one",实测 64 枚 87) |
| `e5_scatter_events` | 一把句柄六发在途,投递序 1..6、写端反序投喂,完成序 6 5 4 3 2 1(WFMO 任一模式收割);hEvent=NULL 时句柄自己当信号,单发在途可用,**两发在途时第一包 150ms 到、句柄 157ms 就亮,第二发还在途(分不清谁完成)**——这就是每发请求配一枚手动重置事件的理由 |

e4 与 IOCP 篇 e6 同题呼应(64 墙的工程代价),e5 与 IOCP 篇 e2 同场景换端口收割,两篇互为镜像。

## 复现

```sh
# 在 WSL 侧(源码先放到 C:/msys64/tmp/wasync/ 下)
cd /mnt/c/msys64/tmp/wasync
/mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra e1_forms.cpp -o e1_forms.exe
./e1_forms.exe          # e2/e3/e5 同款三步;e1 自建数据文件,其余实验只用命名管道
```
