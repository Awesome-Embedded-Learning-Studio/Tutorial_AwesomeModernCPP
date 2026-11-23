---
title: "OVERLAPPED Asynchronous I/O and WaitForMultipleObjects"
description: "The first foundation stone of Windows asynchronous I/O: the same ReadFile, going from returning only when full to going in flight once you hand over an OVERLAPPED. Three forms aligned on one screen — an OVERLAPPED call on a synchronous handle still blocks while the file pointer measurably follows from 16 to 8008 (the document's words quoted in place); five back-to-back issues on an asynchronous handle all FALSE+997; a GOR facing EOF returns FALSE+38 while the synchronous side returns TRUE+0. ReadFileEx's completion routine only runs inside alertable waits — routine execution count 0 during a 500ms non-alertable wait, FIFO back-to-back runs within the same tick once alertable, the wait returning early with 192, and an hEvent stuffed with sentinels reading back untouched. Thread ownership in the CancelIo family measured — a read issued by a worker thread cannot be undone by the main thread's CancelIo and GOR still reads 996 (ERROR_IO_INCOMPLETE); only unnamed CancelIoEx collects 995, rounding out three codes with three meanings alongside 997. The WFMO and MsgWait walls measured separately — WFMO passes 64 handles and fails 65 with WAIT_FAILED+87 (index 63 still named), MsgWait passes 63 and fails 64 with 87, the message queue taking up one slot, so the 64-minus-one claim belongs to the MsgWait family. Six issues in flight, submission order 1..6, completion order 6 5 4 3 2 1; with hEvent=NULL, two in-flight handles light up when the first packet lands while the second is still in flight and you cannot tell who finished — the measured reason each issue gets its own manual-reset event. The close hands the event route's two tight spots to the next article's completion port"
chapter: 8
order: 1
platform: host
difficulty: advanced
cpp_standard: [20]
reading_time_minutes: 19
prerequisites:
  - "Win32 文件 I/O:句柄、CreateFileW 与同步读写"
  - "文件锁:LockFileEx"
  - "进程与作业:CreateProcessW 与 Job 对象"
  - "控制台事件与 APC"
related:
  - "控制台事件与 APC"
  - "文件锁:LockFileEx"
  - "进程与作业:CreateProcessW 与 Job 对象"
tags:
  - host
  - cpp-modern
  - advanced
  - 系统编程
  - Win32
  - 异步编程
  - 并发
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
translation:
  source: documents/vol8-domains/systems-programming/windows/async-io/01-overlapped.md
  source_hash: 81c5d35882836193a2e7fd668acc43a1b44432778d81ab64164ffc225b0f5b7a
  translated_at: '2026-11-22T15:34:52+00:00'
  engine: glm-5.3
  token_count: 20025
---

# OVERLAPPED Asynchronous I/O and WaitForMultipleObjects

In [Win32 File I/O](../file-io/01-win32-file-io.md) (we call it W01) we pinned down ReadFile's disposition: on a synchronous handle it returns only when the read is full, and when it steps on EOF it returns TRUE plus 0. Each issue costs one wait — the waiting and the work are crammed into the same call, and if you want one handle to carry several reads at once, it cannot be done. The entry article W01 actually named it: `FILE_FLAG_OVERLAPPED` lives in CreateFileW's dwFlagsAndAttributes. Back then we only learned its face and said a later article in this series would give it a proper stage. Today is that appointment. Flip this flag on and the handle starts following the rules of overlapped I/O (overlapped I/O is Windows' official name for asynchronous I/O): we hand in an OVERLAPPED structure, the call returns immediately, the reading moves into the kernel, and we harvest the completion notification separately.

You have already met the OVERLAPPED structure once, in [File Locking: LockFileEx](../file-io/05-lockfileex.md) (we call it W05): LockFileEx carries the region start in Offset and OffsetHigh, conflicting requests return 997 and queue up, and CancelIoEx collects 995 when it cancels. There the structure was waiting on a lock; in this article we put it back on its regular job of reading data. The layout of the structure and the origin of the 997/995 pair were both covered in W05, so we will not reteach them here. The new things to watch are: how in-flight reads are waited on, harvested, and canceled; where the ceiling sits when there are many of them; and 996, a new code W05 never saw.

We are also holding one old promise that comes due today. At the end of [Console Events and APC](../process/02-console-apc.md) we left word that the completion notifications of functions like ReadFileEx are built on APC underneath, and that bringing them back on stage was reserved for this asynchronous I/O chapter. Section e2 of this article is where that promise lands.

Numbering and environment conventions go up front, as usual. This article's experiments are numbered e1 through e5; the five groups live side by side in the repository under `code/volumn_codes/vol8/systems-programming/windows/async-io/01-overlapped/`: e1_forms, e2_readfileex_apc, e3_cancel, e4_wfmo_limit, e5_scatter_events, each .cpp paired with a verbatim .out of the same name, laid out flat, plus a README.md recording the environment. The e-series numbering is independent from the file-io articles and from the two processes articles, and the completion-port article next door in the same directory numbers its own — go by file names and you will not mistake anyone. The machine is the author's Win11 26200, the compiler is MSYS2 UCRT64 g++ 16.1.0, the command is uniformly `g++ -std=c++20 -Wall -Wextra`, it produced zero warnings, and the outputs were captured on 2026-10-04. Compilation and runs were launched from WSL via interop; the essentials of that chain were covered in W01. Timing uses GetTickCount64, a millisecond-resolution clock, and every output line wears a `[ N ms]` prefix. For in-flight scenarios we used named pipes throughout: the server does the writing, and our client side does the asynchronous reading. The reason is nothing exotic — file data sits in the cache, a ReadFile finishes on the spot, and an in-flight state simply will not hold, whereas a pipe that stays unfinished until fed is a natural stage for in-flight work. The data file for the file experiment (the e1 round) is self-encoded: we chopped 65536 bytes into 8-byte blocks, and block i stores its own offset i*8, so whatever value a read brings back tells you where it landed. This time we invited none of the shared utilities — no `unique_handle`, no `check_win32`; all five experiments are self-contained, compilable with windows.h up front plus a few standard headers. If you want to lift one out to reproduce it, you need not drag along any of the project's common headers.

## Three Forms of Use, Aligned at Once

Our first important question: what do the return shape and the source of positioning look like for the same ReadFile under three forms of use — a synchronous handle, a synchronous handle plus OVERLAPPED, and an asynchronous handle? e1 packs all three into one program, with the self-encoded e1data.bin as the data file.

Segment [a] is the synchronous handle with lpOverlapped set to nullptr — W01's home turf. We replay a few lines as the baseline:

```text
$ ./e1_forms.exe
[     0 ms] == [a] 同步句柄 + lpOverlapped=NULL:file-io/01 的正主,回放三连读 ==
[     0 ms] 第1次裸读: ret=1 got=8 值=0 (期望 0)
[     0 ms] 第2次裸读: ret=1 got=8 值=8 (期望 8)
[     0 ms] 第3次裸读: ret=1 got=8 值=16 (期望 16)
[     0 ms] 同步+EOF(0字节可读): ret=1 got=0 (file-io/01 口径:TRUE 且 got=0)
[     0 ms] 同步+骑EOF(4字节可读): ret=1 got=4 (口径:TRUE 且 got=剩余字节数)
```

Its disposition is one read, one step: the value read back is that block's offset. We replayed the two EOF cases along with it: a read facing EOF returns TRUE plus 0, a read straddling EOF returns TRUE plus 4. That is the recorded convention from W01, and the three async cases later will be checked against them.

In segment [b] the handle is still synchronous, and we hand it an OVERLAPPED loaded with an Offset. The code we write is only a few lines:

```cpp
// e1_forms.cpp(节选):[b] 段的主干与文件指针的探针(中间的 LOG 行略)
ReadFile(h, &val, 8, &got, nullptr);
ReadFile(h, &val, 8, &got, nullptr);

OVERLAPPED ov{};
ov.Offset = 8000;
val = 0;
ok = ReadFile(h, &val, 8, &got, &ov);
ReadFile(h, &val, 8, &got, nullptr);

DWORD file_pointer(HANDLE h)
{
    DWORD ptr = SetFilePointer(h, 0, nullptr, FILE_CURRENT);
    return ptr;
}
```

```text
[     0 ms] == [b] 同步句柄 + lpOverlapped:位置搬进 Offset,调用还是阻塞的 ==
[     0 ms] 预备:两次裸读后文件指针 = 16
[     0 ms] 带 Offset=8000 读: ret=1 got=8 值=8000 gle=0((见 gle)) —— 永远不回 997,同步完成
[     0 ms] 读完文件指针 = 8008(不是 16 了:同步句柄上 OVERLAPPED 读会把指针跟到 Offset+字节数)
[     0 ms] 接着裸读: 值=8008(顺着跟过来的新指针取)—— 同步句柄上两套定位其实共用一条指针
[     0 ms] 同步+OVERLAPPED 骑EOF: ret=1 got=4 gle=0((见 gle))
```

The return is TRUE, gle gives 0, the value read back is 8000 — the position is right. But please note the file pointer: before this read it was clearly 16, and after the read it has become 8008. The document's Synchronization and File Position section says this in so many words, and we quote the whole sentence: `If lpOverlapped is not NULL, the read operation starts at the offset that is specified in the OVERLAPPED structure and ReadFile does not return until the operation is complete. The system updates the OVERLAPPED offset and the file pointer before ReadFile returns.` The second half is exactly the scene before us: the system updates both the OVERLAPPED offset and the file pointer before returning. So an OVERLAPPED on a synchronous handle only swaps the entry point for positioning; the call still blocks, 997 will never come, and the two positioning channels still share one pointer — once we read with an Offset, the landing point of the next bare read gets dragged along too. This observation pays off later: the root reason an asynchronous handle must give each request its own OVERLAPPED with its own Offset is that the shared-pointer road is abandoned.

In segment [c] we grant `FILE_FLAG_OVERLAPPED` and the handle becomes asynchronous. We then fire two reads back to back, and both come back FALSE plus 997:

```text
[     0 ms] == [c] 异步句柄(FILE_FLAG_OVERLAPPED):Offset 独立定位,返回二态 ==
[     0 ms] 读 Offset=2000: ret=0 gle=997(ERROR_IO_PENDING) —— 异步句柄上这轮直接回 997 在途(内核没当场做完)
[     0 ms] 读 Offset=1000: ret=0 gle=997(ERROR_IO_PENDING)
[     0 ms] GOR(ovA): ret=1 got=8 值=2000 —— 一把句柄两处偏移,数据各归各
[     0 ms] GOR(ovB): ret=1 got=8 值=1000 —— 投递序 2000→1000,定位靠 Offset 不靠指针
(满读、骑 EOF、正对 EOF 的三发同为 997,贴在下一块)
```

The GOR we use to harvest in-flight results is short for GetOverlappedResult; that function fetches the result of an in-flight request, and bWait set to TRUE means wait until it is done. The name of 997 is ERROR_IO_PENDING — the request is accepted and not yet finished; it is the asynchronous world's standard receipt, not a failure. The two reads were issued in the order 2000 then 1000, and each came back with its own value — positioning listens entirely to the Offset, and the issuing order plays no part in it here.

We moved the three EOF cases onto the asynchronous handle and ran them again; ReadFile still returned 997 across the board. All five issues of segment [c] are in flight, and the verdict waits for GOR:

```text
[     0 ms] 异步+满读(8字节可读): ReadFile ret=0 gle=997(ERROR_IO_PENDING); GOR ret=1 got=8 gle=0((见 gle))
[     0 ms] 异步+骑EOF(4字节可读): ReadFile ret=0 gle=997(ERROR_IO_PENDING); GOR ret=1 got=4 gle=0((见 gle))
[     0 ms] 异步+正对EOF(0字节可读): ReadFile ret=0 gle=997(ERROR_IO_PENDING); GOR ret=0 got=0 gle=38(ERROR_HANDLE_EOF)
```

The read straddling EOF gets TRUE plus 4 from GOR, matching the same experiment on the synchronous side. The read facing EOF splits the family: the synchronous side returns TRUE plus 0, while the asynchronous side's GOR returns FALSE plus 38, and 38's name is ERROR_HANDLE_EOF. The document writes this pair of behaviors verbatim: `If a read operation on a file begins at or beyond the end of the file, then the read operation fails with the error ERROR_HANDLE_EOF. If a read operation on a file begins before the end of the file, but the read operation extends past the end of the file, then the read operation succeeds, and the number of bytes read is the number of bytes that were read before the end of file was reached.` A read that starts before EOF and ends past it counts as success, with the byte count being what remained up to EOF — the basis for the 4 in the straddling case sits in the second half. A read that starts at or beyond EOF is judged a failure outright in the asynchronous world. If you write code that spans synchronous and asynchronous, the EOF-checking posture has to switch too: the synchronous side looks for TRUE plus got=0, the asynchronous side looks for GOR's FALSE plus 38.

At the tail of segment [c] we also hung two probes, one on a bare read and one on the file pointer:

```text
[     0 ms] 异步句柄裸读(不给 OVERLAPPED): ret=0 gle=87(ERROR_INVALID_PARAMETER) —— 文档口径:必须给
[     0 ms] 指针推到 8000 后从 Offset=0 读: 值=0(不是 8000)—— 异步句柄只认 OVERLAPPED.Offset,文件指针形同虚设
```

Bare-reading an asynchronous handle without an OVERLAPPED gets us 87 (ERROR_INVALID_PARAMETER); the parameter table's original wording is blunt: `A pointer to an OVERLAPPED structure is required if the hFile parameter was opened with FILE_FLAG_OVERLAPPED` — explicitly required. The more telling part is the second line: we pushed the pointer to 8000 with SetFilePointer, then issued a read from Offset=0, and the value read back was 0 — the content of block 0. The pointer push was wasted; the asynchronous handle acknowledges only the Offset. So this pairs exactly with segment [b]: on a synchronous handle the two positioning channels share one pointer; on an asynchronous handle the pointer has simply left the stage, and position lives only inside each request's OVERLAPPED.

One more convention to state honestly. A ReadFile on an asynchronous handle can complete inline: when the data is already ready, the kernel finishes it on the spot and ReadFile returns TRUE right away. The document's Note says verbatim `subsequent calls to functions such as ReadFile using that handle generally return immediately, but can also behave synchronously with respect to blocked execution`, and the page links an old KB-numbered article titled Asynchronous disk I/O appears as synchronous, about the conditions under which disk I/O looks synchronous. In this machine's round, file and pipe reads all took the 997 in-flight form — not a single inline TRUE was caught. The two-state behavior exists; our measured convention is uniformly 997. If you get TRUE on another machine, that is normal too — it is the other end of the two states.

## ReadFileEx's Completion Routine: It Only Runs in Alertable Waits

Now the promise left at the end of [Console Events and APC](../process/02-console-apc.md) comes due. That article quoted the document verbatim: the completion notifications of ReadFileEx, SetWaitableTimer, WriteFileEx and friends `are implemented using an APC as the completion notification callback mechanism` — APC is what pads the floor underneath. The APC queue, the alertable two-state behavior, FIFO, and 192 were all covered in depth there; here we only bring ReadFileEx, the principal user, on stage to see under what conditions its completion routine is willing to run.

The scene we laid out for e2 is two named pipes, with the writers feeding data 120 and 160 milliseconds after release, the main thread issuing two ReadFileEx calls, and the wait split into two stages. The first stage is a 500-millisecond non-alertable wait:

```text
$ ./e2_readfileex_apc.exe
[    16 ms] 阶段0: 投递两个 ReadFileEx(投递序 A→B),放行写入方(120/160 ms 后投喂)
[    16 ms] ReadFileEx 返回: A=1 B=1;此刻例程执行数=0(投递不等于执行)
[    16 ms] 阶段1: 500ms 不可警告等待(FALSE)—— 数据会在这 500ms 里到,例程该一个都不跑
[   516 ms] WaitForSingleObjectEx(...,500,FALSE) 返回 258(258=超时),此刻例程执行数=0 —— 数据在管道里,例程在睡觉
```

We watched the data land in the pipes at 120 and 160 milliseconds, the 500-millisecond wait slept to its timeout, yet the routine execution count is 0. This is APC's ownership rule live: a routine sitting in the queue runs zero times unless the thread enters an alertable wait — the data arriving does not matter. ReadFileEx returning TRUE only means the request was accepted; it does not mean the routine has run.

For the second wait, we step into the alertable state:

```text
[   516 ms] 阶段2: 进入可警告等待(TRUE,限期 3000)—— 两条积压例程该同 tick 连跑
[   516 ms]   例程#1 跑起来了: err=0 bytes=8 (tid=22928)
[   516 ms]   例程#2 跑起来了: err=0 bytes=8 (tid=22928)
[   516 ms] 可警告等待在 516 ms 返回 192(192=WAIT_IO_COMPLETION),等待历时 0 ms,例程执行数=2
[   516 ms] 到达序: A=1 B=2(FIFO);A 例程时刻=516 ms,B 例程时刻=516 ms;vA=120 vB=160
```

Both routines ran back to back within the same tick. Arrival order is A before B, and queue order is execution order — precisely the APC queue's FIFO rule. The wait lasted 0 milliseconds and came back with the code 192, whose name is WAIT_IO_COMPLETION: the wait was interrupted by the queue draining. The routine's tid is the same thread that issued the calls — the evidence that APC reuses the original thread was already filmed in Console Events and APC, so we will not re-stage it. The single-issue round (data arriving at about 250 milliseconds) is also in the archive: the routine woke with the data and the wait likewise returned 192; we will not paste it again.

One sentinel-probe line in the middle of the output deserves its own word. The author stuffed 1, 2, 3 into OVERLAPPED.hEvent as routine numbers and read them back after the routines finished:

```text
[   516 ms] 哨兵探针: ovA.hEvent 读回 1, ovB.hEvent 读回 2 —— ReadFileEx 没碰过它们
```

The values came back untouched — not one byte moved. ReadFileEx's completion report goes through the routine and needs no event at all; the document says it verbatim: `The ReadFileEx function ignores the OVERLAPPED structure's hEvent member. An application is free to use that member for its own purposes in the context of a ReadFileEx call.` In the ReadFileEx context, the hEvent member is yours to command, so using it as a sentinel is safe — that is our basis for stuffing routine numbers into it.

The merit of the completion-routine route is that the callback carries its own context: when the data is in hand, the code is already standing inside the handler. The cost we also saw clearly: the thread must enter alertable waits periodically, the host's shape is a wait loop with TRUE, and the issuing thread is responsible for coming back to sleep. The cost comparison waits for the next article's completion port.

## Canceling: CancelIo Owns Only This Thread, CancelIoEx Owns Everything

When an in-flight request can no longer afford the wait, how do we take it down? In W05 we canceled queued lock requests: CancelIoEx does the targeted naming, GOR collects 995, and read requests are simply another user of the same pair of functions. Beyond re-verifying on read requests, we also measure something W05 never measured — the thread ownership of CancelIo versus CancelIoEx. e3's pipe simply never gets fed; the feeding delay is written as 100 seconds, so the in-flight state stays as long as we want it.

Here are the first two groups:

```text
$ ./e3_cancel.exe
[     0 ms] == [1] 定向撤单:CancelIoEx 指名道姓 ==
[    16 ms] 投读: gle=997(ERROR_IO_PENDING) —— 无数据,在途
[    16 ms] CancelIoEx(定向): ret=1
[    16 ms] GOR: ret=0 gle=995(ERROR_OPERATION_ABORTED) —— 请求被撤,以 995 收场
[    16 ms] == [2] 全量撤单:CancelIo 清光这把句柄上的在途 ==
[    31 ms] 两发在途读: A gle=997 B gle=997
[    31 ms] CancelIo(不带 OVERLAPPED,全量): ret=1
[    31 ms] GOR(A): ret=0 gle=995(ERROR_OPERATION_ABORTED); GOR(B): ret=0 gle=995(ERROR_OPERATION_ABORTED) —— 一勺烩,都是 995
```

The targeted CancelIoEx names one specific request; the bulk CancelIo carries no OVERLAPPED and clears every in-flight request on the handle in one sweep. The name of 995 is ERROR_OPERATION_ABORTED — the closing code of a canceled request. We collected it in W05's lock context; here it is the read request's principal user. Is the targeted cancellation really targeted? Group [3] set up a neighbor as control:

```text
[    31 ms] == [3] 定向撤 A,放过 B:B 照常吃数据完成 ==
[    47 ms] 两发在途读: A gle=997(无事件) B gle=997(带事件)
[    47 ms] CancelIoEx 只点 A
[   219 ms] GOR(A): ret=0 gle=995(ERROR_OPERATION_ABORTED); B 的事件等待 0 后 GOR ret=1 got=8 vB=150 —— 撤单不影响邻居
```

B carried its own event; the data arrived at 150 milliseconds, the event lit, and GOR fetched TRUE plus vB=150. We canceled A and did not touch a hair of B. Next, the [3b] probe deserves its own look, because it asks a question we will need again: for an in-flight read whose OVERLAPPED.hEvent got no event, which signal does GOR(bWait=TRUE) lean on?

```text
[   219 ms] == [3b] 探针:OVERLAPPED.hEvent=NULL 的在途读,GOR(bWait=TRUE) 靠什么等 ==
[   437 ms] GOR(bWait=TRUE) 等了 203 ms 返回: ret=1 gle=0((其他)) —— 单发在途时它落在句柄信号上,真等到了数据
(句柄信号的注记一行在存档,略)
```

GOR waited the full 203 milliseconds and got the data. What it waits on is the handle's own signal bit. With a single issue in flight that works; whether sharing this one signal works with multiple issues is the question mark we note here — the measurement lands in part two of e5. Group [4] added one reassurance: cancellation does not harm the handle.

```text
([4] 组的段头在存档,略)
[   453 ms] 第一发已撤(995),同句柄再投一发
[   578 ms] 第二发: 投递 gle=997(ERROR_IO_PENDING),GOR ret=1 got=8 v2=80 —— 句柄没被取消弄坏
```

After canceling, we issue another read on the same handle and it completes as usual — cancellation leaves no wreckage on the handle. Now we bring out this experiment's headliner: group [5] measures thread ownership. The arrangement is a worker thread issuing the read while our main thread tries to cancel:

```text
[   578 ms] == [5] 线程归属:别的线程发的读,CancelIo(仅本线程)撤不动,CancelIoEx(全线程)才撤得动 ==
[   594 ms] worker(tid=12336)投读: gle=997(ERROR_IO_PENDING)
[   609 ms] 主线程 CancelIo: ret=1;GOR(FALSE): ret=0 gle=996(ERROR_IO_INCOMPLETE(尚未完成,996)) —— 在途纹丝不动,CancelIo 只认调用线程的请求
[   609 ms] 主线程 CancelIoEx(不点名): ret=1;GOR(TRUE): ret=0 gle=995(ERROR_OPERATION_ABORTED) —— 跨线程也照撤
```

The main thread's CancelIo returned TRUE — it looks like the cancellation worked. But probe with GOR(FALSE) and it returns 996: the request sits in the in-flight state, unmoved. Switch to the unnamed form of CancelIoEx and the cancellation lands at once — GOR collects 995. The document spells out the two brothers' division of labor; ReadFile's Remarks has the ready-made quote: `CancelIo: This function only cancels operations issued by the calling thread for the specified file handle. CancelIoEx: This function cancels all operations issued by the threads for the specified file handle.` Owning only the calling thread's requests versus owning every thread's requests on this handle — the difference is one word. If you build a structure where the issuing thread and the harvesting thread are separate and want to cancel the issuer's in-flight requests from the harvester, the tool in hand must be CancelIoEx.

996 is this article's new code; let us line it up with the rest. 997 is the receipt at the moment ReadFile is issued — accepted, not yet done. 996's name is ERROR_IO_INCOMPLETE, the dedicated not-yet-finished that GOR(FALSE) reads on an in-flight request. 995 is the closing code after a cancellation. Three codes follow three actions — when you identify a code, identify the action along with it.

## Two Walls of Limits: WFMO's 64 and MsgWait's 63

The main harvesting tool is WaitForMultipleObjects (WFMO for short — we used it in [Processes and Jobs](../process/01-createprocess.md) while waiting on a crowd of children). There we left a sentence: it can wait on at most `MAXIMUM_WAIT_OBJECTS` objects at a time, and beyond that you open threads or move to IOCP. IOCP is short for I/O Completion Port, the next stop of Windows asynchronous I/O and the next article's headliner — we will keep that appointment at the close. Two numbers in that sentence were never measured: the constant's actual value, and what comes back when you exceed it. We measure this wall with e4, and we also put a widely circulated claim on trial — that you can wait on at most 64 minus 1, i.e. 63, at a time. Whether that holds depends on which family of functions it is talking about.

The experiment opens 130 manual-reset events (once signaled, they stay lit until someone personally calls ResetEvent — why it must be these we explain when we quote the document), all initially unsignaled. We light one signal each at indexes 5 and 70, then walk nCount from 1 up to 130:

```text
$ ./e4_wfmo_limit.exe
[     0 ms] MAXIMUM_WAIT_OBJECTS 编译期常量 = 64
[     0 ms] == WaitForMultipleObjects:nCount 从 63 加到 130 ==
[   203 ms] nCount=  1 → 返回 WAIT_TIMEOUT, gle=0
[   203 ms] nCount= 63 → 返回 WAIT_OBJECT_0+5 (点名索引 5), gle=0
[   203 ms] nCount= 64 → 返回 WAIT_OBJECT_0+5 (点名索引 5), gle=0
[   203 ms] nCount= 65 → 返回 WAIT_FAILED, gle=87(87=ERROR_INVALID_PARAMETER)
[   203 ms] nCount= 66 → 返回 WAIT_FAILED, gle=87(87=ERROR_INVALID_PARAMETER)
(100 与 130 两行同为 WAIT_FAILED+87,在存档,略)
[   203 ms] == 边界复核:nCount=64、把信号放在最末一枚(索引 63) ==
[   203 ms] nCount=64、信号在索引 63 → 返回 WAIT_OBJECT_0+63 (点名索引 63)
```

We measured it: MAXIMUM_WAIT_OBJECTS is 64 as a compile-time constant. nCount at both 63 and 64 returns and names its index normally; from 65 on it returns WAIT_FAILED with 87. The boundary re-check deliberately moved the signal to index 63, and the nCount=64 round still named it — so the 64 slots are real.

With WFMO scanned, we keep the same events and swap the waiting function for `MsgWaitForMultipleObjectsEx` (we follow along and call it MsgWait) for another sweep. This family carries a message wait; we give dwWakeMask QS_ALLINPUT:

```text
[   422 ms] nCount=  1 → 返回 WAIT_TIMEOUT, gle=0
[   422 ms] nCount= 63 → 返回 WAIT_OBJECT_0+5 (点名索引 5), gle=0
[   422 ms] nCount= 64 → 返回 WAIT_FAILED, gle=87(87=ERROR_INVALID_PARAMETER)
(段头与结论注记两行在存档,略)
```

In the same process, in the same round: WFMO's 64 handles work, MsgWait's 64 handles die, and only 63 or fewer behave normally. The difference lands squarely on dwWakeMask: QS_ALLINPUT ropes the message queue into the wait, and the message queue itself takes up one slot. The document's note on nCount says verbatim `The maximum number of object handles is MAXIMUM_WAIT_OBJECTS minus one` — the subtracted one is the message queue's seat. So the 64-minus-one claim has an owner: it speaks of the MsgWait family with message waits. WFMO itself tops out at 64. When you read elsewhere that you can only wait on 63 at a time, do not rush to pin it on WFMO — check which family is being spoken of.

The engineering cost of this wall is recorded in e6 of the IOCP article next door in the same directory: that round straps 100 in-flight reads onto one pipe, 100 events get forced by the 64 cap into 64-plus-36 segmented polling, each wakeup still owing a full rescan, while the completion-port side needs a single queue and zero scans. Two ways past the same limit — both experiment archives are already in the repository.

## Scattered Completions: Six Issues in Flight and Two Ways to Give Events

The most valuable property of asynchronous I/O, in this author's view, is the split between issue order and completion order. e5 builds six pipes: the read side issues in order 1 to 6, the write side feeds in reverse 6 to 1, spaced 60 milliseconds apart, each OVERLAPPED paired with one manual-reset event, harvested with WFMO in any-mode:

```text
$ ./e5_scatter_events.exe
[    15 ms] 六发在途读按投递序 1..6 发出(写端将按 6..1 投喂,间隔 60ms)
[    15 ms]   投递#1: ret=0 gle=997(997=在途)
[    15 ms]   投递#2: ret=0 gle=997(997=在途)
(投递#3 到 #6 同为 997,在存档,略)
[    15 ms] 收割循环:WFMO(六事件,任一,bWaitAll=FALSE)
[    94 ms]   完成第 1 个:是投递#6(GOR ret=1 got=8 值=6)
[   156 ms]   完成第 2 个:是投递#5(GOR ret=1 got=8 值=5)
[   219 ms]   完成第 3 个:是投递#4(GOR ret=1 got=8 值=4)
[   281 ms]   完成第 4 个:是投递#3(GOR ret=1 got=8 值=3)
[   359 ms]   完成第 5 个:是投递#2(GOR ret=1 got=8 值=2)
[   406 ms]   完成第 6 个:是投递#1(GOR ret=1 got=8 值=1)
[   406 ms] 投递序: 1 2 3 4 5 6;完成序:
[   406 ms]  6 5 4 3 2 1 (与写端投喂次序一致,与投递序相反)
```

The completion order we got is 6 5 4 3 2 1, with completion moments at 94, 156, 219, 281, 359, and 406 milliseconds, matching the writer's feeding order one to one. Issue order loses all say here: the earliest-issued #1 happens to finish last. In the synchronous world read order equals issue order — the blocking call guarantees it. Asynchronous cancels that guarantee: whichever read's data arrives early, finishes early. The harvest loop's skeleton is worth a look; the engineering shape of the event route lives in these few lines:

```cpp
// e5_scatter_events.cpp(节选):收割循环(LOG 行略)
while (done < kN) {
    DWORD w = WaitForMultipleObjects(kN, evs.data(), FALSE, 5000);
    if (w == WAIT_FAILED || w == WAIT_TIMEOUT) { LOG("WFMO 异常: %lu gle=%lu", w, GetLastError()); break; }
    int idx = (int)(w - WAIT_OBJECT_0);
    DWORD got = 0;
    BOOL g = GetOverlappedResult(pipes[idx].cli, &ovs[idx], &got, FALSE);
    ++done;
    order.push_back(idx + 1);
    ResetEvent(evs[idx]);   // 手动重置,收完亲手归零
}
```

The IOCP article's e2 swaps the completion port into the same six-pipe scene and harvests the same completion order, 6 5 4 3 2 1 — the two articles are mirror images of each other, and you can check them side by side when you get there.

The other way to give events is to not give one at all. Leave hEvent NULL and the completion signal lands on the handle itself; with a single issue in flight you just wait on the handle — e3's [3b] already showed it waiting out the full 203 milliseconds. With multiple issues in flight? The second part of e5 measures exactly this: two reads both with NULL hEvent, the first packet's data arriving at 150 milliseconds, the second at 450. We wait on the handle:

```text
(第二部分的段头在存档,略)
[   422 ms] 单发在途: ret=0 gle=997,直接等【句柄本身】
[   640 ms] WaitForSingleObject(管道句柄): 218 ms 处返回 0 —— hEvent=NULL 时句柄自己当信号
[   640 ms] GOR: ret=1 got=8 值=77
[   640 ms] 两发在途(都 hEvent=NULL): gle=997/997;第一包 150ms 到,第二包 450ms 到
[   797 ms] WaitForSingleObject(句柄)在 157 ms 返回 0 —— 对齐两包:亮在全部完成还是任一完成,数字说了算
[   797 ms] GOR: 第一发 ret=1 值=1;第二发 ret=0 值=0
```

The answer sits in the last two lines: the handle lit at 157 milliseconds, while the second packet was still more than 290 milliseconds away. The GOR probe testifies too — the first read returns TRUE with value 1, the second returns FALSE, still in flight. So the handle's signal bit is set when any one issue completes; the handle owns exactly one signal, so on wakeup you know something finished but cannot tell which issue. Single-issue scenes find it sufficient; multi-issue scenes cannot name names. That is the measured reason every request gets its own event: with one event per issue, the signal can say which one finished.

The second refinement of events is that they must be manual-reset. The document's OVERLAPPED page carries a warning whose first half says waiting functions reset auto-reset events on the spot — verbatim, `Functions such as GetOverlappedResult and the synchronization wait functions reset auto-reset events to the nonsignaled state.` — and then advises `Therefore, you should use a manual reset event`, with the reason stated as a consequence, verbatim: `if you use an auto-reset event, your application can stop responding if you wait for the operation to complete and then call GetOverlappedResult with the bWait parameter set to TRUE.` By the time you call GOR(TRUE), the event is already dark, and the program hangs at that step. The hands-on ResetEvent in our harvest loop pairs exactly with the manual-reset premise: the event will not put itself out — once harvested, we put it out ourselves.

At this point we have walked the whole event route: one OVERLAPPED and one manual-reset event per request, in-flight identified by 997 and 996, cancellation by 995, EOF by 38. This structure serves fine at the scale of a handful of issues, but it has two born-tight spots, both felt in the experiments. First, on wakeup we know something happened but not who, and must identify by event number; as issue count grows, WFMO's 64 pushes you into segmented polling, each wakeup still owing a full rescan. Second, the host's shape: the ReadFileEx completion-routine route does carry its own context, but it demands the thread enter alertable waits periodically — the loop cannot live without a TRUE-bearing wait. The completion port in the next article takes over both routes together.

The answer we want is the completion port: CreateIoCompletionPort to dock and GetQueuedCompletionStatus to collect are the two principal functions. Completion is no longer an event lighting up for you to recognize — it lines up as packet after packet, each carrying the byte count, the key you attached at issue time, and the pointer to that OVERLAPPED; whichever one you dequeue is the one that finished, and a single queue has no 64 cap. We will re-harvest the same six pipes through the port, run the 64-cap contrast at 100 in-flight issues, and see what happens to in-flight requests when a handle closes — all in the next article, [IOCP Completion Ports](02-iocp.md), same directory, experiment archives already waiting in the repository.

<ReferenceCard title="References">
  <ReferenceItem
    :id="1"
    title="ReadFile function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-readfile"
  />
  <ReferenceItem
    :id="2"
    title="ReadFileEx function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-readfileex"
  />
  <ReferenceItem
    :id="3"
    title="OVERLAPPED structure"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/minwinbase/ns-minwinbase-overlapped"
  />
  <ReferenceItem
    :id="4"
    title="GetOverlappedResult function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-getoverlappedresult"
  />
  <ReferenceItem
    :id="5"
    title="WaitForMultipleObjects function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitformultipleobjects"
  />
  <ReferenceItem
    :id="6"
    title="MsgWaitForMultipleObjectsEx function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-msgwaitformultipleobjectsex"
  />
  <ReferenceItem
    :id="7"
    title="CancelIo function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelio"
  />
  <ReferenceItem
    :id="8"
    title="CancelIoEx function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelioex"
  />
  <ReferenceItem
    :id="9"
    title="Asynchronous disk I/O appears as synchronous on Windows"
    publisher="Microsoft Support (old KB number, linked from the ReadFile docs page)"
    url="https://learn.microsoft.com/en-us/troubleshoot/windows/win32/asynchronous-disk-io-synchronous"
  />
</ReferenceCard>
