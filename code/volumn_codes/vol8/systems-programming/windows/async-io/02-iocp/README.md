# 02-iocp 配套实验

《IOCP 完成端口》(vol8 systems-programming/windows ch04 L02,Windows 异步支柱)的实验代码与原始输出存档。核心问题一句话:**完成通知不再挂在每发请求的事件上,而是汇进一枚端口,GetQueuedCompletionStatus 每次取出的就是完成的那发(三件套:字节数/key/OVERLAPPED 指针),WaitForMultipleObjects 的 64 枚上限从此不存在**。process/01 末尾点过的两件事——WFMO 超额「换 IOCP」、Job 挂端口收进程死讯——都在这里兑现。

## 环境口径(所有 .out 都来自这一轮)

| 项 | 值 |
|---|---|
| 系统 | 宿主 Windows 11 26200(26H2 线),逻辑处理器 16 |
| 编译器 | MSYS2 UCRT64 g++(Rev 5)16.1.0,`-std=c++20 -Wall -Wextra`(零警告;e5b 另加 `-municode`) |
| 跑法 | WSL 侧 `cd /mnt/c/msys64/tmp/wasync && /mnt/c/msys64/ucrt64/bin/g++.exe … && ./xxx.exe` |
| 数据 | e1 用 `C:/msys64/tmp/wasync/e1data.bin`(偏移自编码文件);其余实验用命名管道 |
| 计时 | GetTickCount64 毫秒级相对时戳,每行 stdout 前缀 `[ N ms]` |

## 目录与结论对照

| 实验 | 一句话结论 |
|---|---|
| `e1_iocp_basics` | 建裸端口(INVALID_HANDLE_VALUE,并发值 0=处理器数);挂文件句柄(key=4242)投三发乱序偏移读,GQCS 三包逐一相认(指针同一、key 原样、bytes 核对无误);空队列 GQCS(800ms)回 FALSE+258;第二把句柄挂 key=777,完成包各认各的 key |
| `e2_completion_order` | 与 OVERLAPPED 篇 e5 完全同一套管道场景,收割换成单线程 GQCS:投递序 1..6、完成序 6 5 4 3 2 1,每个完成包的 OVERLAPPED 指针自报家门 |
| `e3_threads` | 4 工线程并发取同一端口,三场:间隔 50ms 投 8 包时全被**同一条线程**收走,并发值=16 与 =1 行为一致(LIFO 唤醒偏好,空闲线程继续睡);一口气投 8 包时并发值说了算——端口C(=1)仍单线程同 tick 连收,概念页并发值 1 段(单线连取、零切换)背书 |
| `e3_threads_d` | 补跑的第四档:并发值 0 + 八包一口气全投(单独进程,worker/run_round 与 e3_threads 逐字相同)——**四条工线程同 tick 全部醒来分掉 8 包**(四个 tid,复跑三轮分布 4/3/3/2、2/2/4/4、4/2/2/4,四线全参与),与端口C对照正落在概念页的释放规则上(running<并发值就放最近的等待线程);并发值是上限:不派活、只放行 |
| `e4_pqcs_shutdown` | PostQueuedCompletionStatus 让裸端口自己当唤醒通道:三条睡死在 GQCS 的工线程被关停哨兵逐个叫醒退场(Linux 侧 eventfd 唤醒 Reactor 循环的同构物);三件套 bytes/key/ov 原样透传;没有活线程时投包也接得住,包在队列里等 |
| `e5_close_inflight` | 在途读未收尾时直接 CloseHandle(野路子):**Win11 26200 实测完成包立即送达,错误码 109(ERROR_BROKEN_PIPE),OVERLAPPED 指针还是那发**;正路子 CancelIoEx 撤单后 GQCS 收到 995 完成包再关句柄,干净 |
| `e5b_job_port` | Job 挂端口(process/01 前向指针兑现):子进程入组/退场以完成包送达,实测字段落点 **lpNumberOfBytes=消息号(6 NEW_PROCESS/7 EXIT_PROCESS/4 ACTIVE_PROCESS_ZERO)、lpOverlapped=pid、key=挂接时自定值**,与文档表格逐格对上;子进程退场码 7 另由句柄核对 |
| `e6_scale_events_vs_iocp` | 100 发在途 1 字节读 × 5 轮:事件式被 64 墙逼成分段轮询(64+36),醒来必须全量重扫(每轮 100 次事件探针 × 收割轮数);IOCP 单端口循环零扫描,每取一发就是完成的那发;两边墙钟同量级(管道 I/O 主导),差别在结构与句柄数(100 事件 vs 1 端口) |

## 文档口径备忘(写文章直接引)

- CreateIoCompletionPort:NumberOfConcurrentThreads 是「系统允许同时处理完成包的线程数上限」,0 = 处理器数;句柄挂上端口后**不能再用于 ReadFileEx/WriteFileEx**(它们有自己的完成机制)。
- I/O Completion Ports 概念页三句写文章直接引:完成包 FIFO 入队但**出队可以乱序**("while the packets are queued in FIFO order they may be dequeued in a different order");阻塞线程按 **LIFO 释放**("the system releases the last (most recent) thread associated with that port");新包入队先查 running 数,**小于并发值才放一条等待线程**(并发值 1 时在跑线程直接取下一包、零上下文切换)。
- Job 消息的字段落点在 JOBOBJECT_ASSOCIATE_COMPLETION_PORT 页的表格里写得很细(lpNumberOfBytes=消息号,lpOverlapped=进程号或 NULL),本目录 e5b 的实测与之一致;文档同时提醒:除限额类通知外,**消息送达不保证**(只作通知用)。
- MsgWaitForMultipleObjectsEx 的 nCount 上限是 "MAXIMUM_WAIT_OBJECTS minus one"(消息队列占位),与 WFMO 的 64 不同——OVERLAPPED 篇 e4 两侧都量了。

## 复现

```sh
# 在 WSL 侧(源码先放到 C:/msys64/tmp/wasync/ 下)
cd /mnt/c/msys64/tmp/wasync
/mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra e1_iocp_basics.cpp -o e1_iocp_basics.exe
./e1_iocp_basics.exe
# e3_threads_d 是补跑的第四档(并发值 0+无间隔,单独进程),与 e3 同款三步:
/mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra e3_threads_d.cpp -o e3_threads_d.exe
./e3_threads_d.exe
# e5b 用宽字符入口:
/mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra -municode e5b_job_port.cpp -o e5b_job_port.exe
./e5b_job_port.exe
```
