---
title: "IOCP Completion Ports"
description: "Part two of the Windows asynchronous chapter, with a single kernel object as the protagonist: the completion port. It flips the previous article's harvest of one event per pending request — all completions converge into one queue, and each packet taken by GetQueuedCompletionStatus carries its own identity. Seven measured rounds: e1 with the triple acknowledging each submission one by one (three out-of-order offset reads, key=4242, the OVERLAPPED pointer matched to its issue, an empty queue returning 258 after 813ms, a second handle with key=777 each matched to its own), e2 the six-pipe mirror (submission order 1..6, completion order 6 5 4 3 2 1, the previous article's e5 scene with the harvest swapped), e3 the concurrency value as a ceiling (with interval feeding, both the 0 and 1 settings saw all 8 packets collected by the same single thread while idle workers stayed asleep; with 8 packets in one burst, concurrency value 1 still received single-file within the same tick, while the make-up run at concurrency value 0 in the fourth cell had four worker threads share the 8 packets within the same tick), e4 PQCS as a wakeup channel (shutdown sentinels waking worker threads one by one, the triple bytes=111 key=888 ov=0xABCD passed through untouched — the Windows analogue of Linux's eventfd), e5 the fate of in-flight requests when a handle closes (the rough way: CloseHandle delivers the completion packet immediately with 109 ERROR_BROKEN_PIPE; the proper way: CancelIoEx collects 995 first, then close), e5b a Job attached to the port (NEW_PROCESS, EXIT_PROCESS, and ACTIVE_PROCESS_ZERO with all three packets' field landings recorded: bytes=message number, ov=pid, delivering on the processes chapter's death-notice promise), e6 the scale comparison (100 issues in flight: the event style forced by the 64 cap into 64+36 two-segment polling, each wakeup rescanning all 100 events, versus IOCP's single port with zero scans — wall-clock times in the same ballpark, an honest reading that the difference lives in the structure). That a handle attached to a port can no longer be used with ReadFileEx is a documented convention, not measured here, and is flagged as such"
chapter: 8
order: 2
platform: host
difficulty: advanced
cpp_standard: [20]
reading_time_minutes: 22
prerequisites:
  - "OVERLAPPED 异步 I/O 与 WaitForMultipleObjects"
  - "Win32 文件 I/O:句柄、CreateFileW 与同步读写"
  - "文件锁:LockFileEx"
  - "进程与作业:CreateProcessW 与 Job 对象"
  - "控制台事件与 APC"
related:
  - "OVERLAPPED 异步 I/O 与 WaitForMultipleObjects"
  - "进程与作业:CreateProcessW 与 Job 对象"
  - "控制台事件与 APC"
  - "异步 I/O 与事件循环"
tags:
  - host
  - cpp-modern
  - advanced
  - 系统编程
  - Win32
  - 并发
  - 异步编程
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
translation:
  source: documents/vol8-domains/systems-programming/windows/async-io/02-iocp.md
  source_hash: 5715f050500b160fbbd75591111069c2bcbe020ebf9596b87def1cd2fc4d554f
  translated_at: '2026-11-22T15:38:47+00:00'
  engine: glm-5.3
  token_count: 22848
---

# IOCP Completion Ports

When the previous article, [OVERLAPPED Asynchronous I/O and WaitForMultipleObjects](01-overlapped.md), closed, the harvesting tool in our hands was `WaitForMultipleObjects` (WFMO below), and the usage was that every in-flight request had to carry its own manual-reset event. Six reads wore six events, and we verified that the arrangement turns. But that style is born with two awkward spots: WFMO waits on at most 64 objects at a time (the hard ceiling measured in the previous article's e4), and the other is that after wakeup we only know it is our turn to ask — which one finished is still something we must find out by asking the events one by one. In [Processes and Jobs: CreateProcessW and Job Objects](../process/01-createprocess.md) we dropped one line: beyond that, you open threads or move to IOCP. This article pays that sentence back.

IOCP's full name is I/O completion port, and its body is one clean kernel object. What it does can be said in a single sentence: completion notifications no longer hang on each request's event — they all converge into the port's own queue; your threads stand at the queue's exit, handle each taken completion packet on the spot, and go back for more once done. Our question changes from which event lit up to who completed next, and the answer is already there at the dequeue moment — completion packets carry their own identity, no need for us to ask. The network services carrying heavy traffic on Windows mostly rest on it underneath, and on the Linux side nothing of the same completion-style caliber arrived until io_uring. Calling it the pillar of Windows asynchronous I/O is no undeserved praise.

This article runs seven rounds of experiments, from creating a port all the way to a hundred issues in flight: in e1, how the completion packet's triple acknowledges its submission; in e3, four threads guarding one port and how many hands the packets fall into; in e5, the two fates of closing a handle before in-flight requests finish; and in the closing e6, a scale comparison at a hundred issues in flight. There is also one bonus round, e5b, which converts the death-notice delivery the processes chapter (the article on Job objects) owed into three completion packets.

Numbering and environment conventions go up front as usual. This article's experiments are numbered e1 through e6 with one bonus e5b in between; they are independent of the previous article's e-series and of the processes articles' e-series — go by the archive directories. Code and raw outputs live under `code/volumn_codes/vol8/systems-programming/windows/async-io/02-iocp/` in the repository. The machine is the author's Win11 26200 (26H2 line) with 16 logical processors; the compiler is MSYS2 UCRT64 g++ 16.1.0; the compile command is uniformly `-std=c++20 -Wall -Wextra` (zero warnings); only e5b uses the wide-character entry point wmain, with an extra `-municode`. Outputs were captured on 2026-10-04. Timing follows the series convention with GetTickCount64 millisecond stamps at the start of every line; its resolution is only a dozen-odd milliseconds, and the extra sliver by which some timestamps below exceed the set intervals is the noise of stamp quantization. In-flight scenarios all use named pipes, with the writing direction server-to-client (PIPE_ACCESS_OUTBOUND); e1 uses a self-encoded data file: 65536 bytes with each 8-byte block storing its own offset, so reading 3000 tells us we are reading at 3000 — the same file the previous article's e1 used. Cross-system execution follows the WSL interop chain described in [Win32 File I/O](../file-io/01-win32-file-io.md) (W01). The experiment programs are each self-contained; this time we did not even invite the shared utilities on stage. To reproduce any single round, copy the corresponding .cpp and it compiles and runs on its own.

## Creating the Port and Attaching Handles: Completion Packets Carry Their Own Identity

`CreateIoCompletionPort` covers two usages in one function; let us see how from the parameters. Give `FileHandle` as INVALID_HANDLE_VALUE and `ExistingCompletionPort` as NULL, and this trip creates a port, returning a bare kernel object with nothing attached. Give `FileHandle` a handle opened with FILE_FLAG_OVERLAPPED and `ExistingCompletionPort` the just-created port, and this trip does the attaching, returning the same port again. The attaching trip also carries a third parameter, `CompletionKey` — the very thing we keep calling the key below: a custom number registered with the handle, which every completion packet on this handle carries back. In e1 we filled in 4242. The document puts it plainly: a handle can attach to at most one port, and once attached it belongs to that port until we close the handle; multiple handles on one port is exactly the usage it encourages. e1's code looks like this — the two trailing comments were added by us for cross-reference and are not in the archived source:

```cpp
HANDLE port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);   // [1] 造端口
// ...
HANDLE f1 = CreateFileA(kPath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
ULONG_PTR key1 = 4242;
HANDLE ok_assoc = CreateIoCompletionPort(f1, port, key1, 0);                 // [2] 挂接,key=4242
```

The fourth parameter, `NumberOfConcurrentThreads`, governs the concurrency value; we come back to compute it properly in e3. For now, two things to remember: giving 0 means the processor count, and on the attaching trip it is ignored — only the port-creation call has a say. The document's characterization of it is quote-grade: the ceiling on the number of threads the system allows to process completion packets simultaneously, `maximum number of threads that the operating system can allow to concurrently process`. Please underline the word ceiling — e3's measurements will give it a very plain footnote.

e1 attached the file handle (key=4242) and issued three reads at different offsets, deliberately shuffled: 3000, 1000, 2000. All three came back FALSE plus 997 in flight — old friends from the previous article. The harvest side changed protagonists: a `GetQueuedCompletionStatus` (GQCS below) loop takes packets, and the call looks like this:

```cpp
DWORD bytes = 0;
ULONG_PTR key = 0;
LPOVERLAPPED pov = nullptr;
BOOL g = GetQueuedCompletionStatus(port, &bytes, &key, &pov, 1000);
```

We paste the raw output in full:

```text
[     0 ms] [1] 建裸端口
[     0 ms] CreateIoCompletionPort(INVALID_HANDLE_VALUE,...) = 248;并发值给 0 的含义=处理器数(本机 16)
[     0 ms] [2] 挂文件句柄(key=4242),投三发不同偏移的读
[     0 ms] 挂接返回 248(就是那枚端口)
[     0 ms]   投递#1 Offset=3000: ret=0 gle=997(997 在途)
[     0 ms]   投递#2 Offset=1000: ret=0 gle=997(997 在途)
[     0 ms]   投递#3 Offset=2000: ret=0 gle=997(997 在途)
[     0 ms]   GQCS 第1包: ret=1 bytes=8 key=4242 OVERLAPPED* 与投递#1 逐一相认(指针同一) 值=3000
[     0 ms]   GQCS 第2包: ret=1 bytes=8 key=4242 OVERLAPPED* 与投递#2 逐一相认(指针同一) 值=1000
[     0 ms]   GQCS 第3包: ret=1 bytes=8 key=4242 OVERLAPPED* 与投递#3 逐一相认(指针同一) 值=2000
```

248 is the handle value handed out that round; another round gets another value, and we only check that it is non-null. What deserves a pause is GQCS's three output parameters, which we call the completion packet's triple. bytes is the number of bytes this I/O actually completed — we do not even need to ask GetOverlappedResult again. key is the 4242 we filled in ourselves at attach time; which handle completed, one glance at this field answers.

pov is the address of the OVERLAPPED belonging to the request that completed. In e1 we compared it against the three ov arrays one by one; the pointers matched, and the values matched too: the read at offset 3000 brought back 3000. In the previous article we had to tell who finished by which event was lit; now the completion packet announces its own household: key and pov are both written in the packet, identity travels with completion rather than with waiting.

The triple also leaves engineering a customary path: key answers which handle, at the granularity of a connection; pov answers which request, at the granularity of an operation — two levels of identity mapping exactly onto a server's two levels of context. The customary practice is to embed the OVERLAPPED as the first member of a custom struct, hand the whole struct over at issue time, and when the completion packet arrives, the pov in hand converts straight back to the full context, table lookup included for free; the CONTAINING_RECORD macro in winnt.h does exactly this converting. Our experiments in this article do not build that struct, but its root is the pointer acknowledgment e1 verified: what pov returns is exactly the address given at issue time, and if the address is stable, the context is stable too.

In e1's second half we added two small observations. GQCS on an empty queue with an 800ms deadline measured 813ms before returning FALSE, with gle giving 258 (WAIT_TIMEOUT); the extra 13ms is the stamp quantization mentioned at the start. GQCS is itself a wait with a timeout — waiting and taking the packet merge into one call; the event style's two steps of wait plus scan collapse into one step here on the port. The second handle f2 was also attached to the same port, with the key changed to 777; one read was issued, and the completion packet came back carrying 777:

```text
[     0 ms] [3] 空队列超时 + 第二把句柄另一个 key
[   813 ms] GQCS(空队列,800ms): ret=0 历时 813 ms gle=258(258=WAIT_TIMEOUT)
[   813 ms] 第二把句柄(key=777)的一发: GQCS ret=1 key=777 值=4000 —— 完成包带着各自句柄的 key 回来
```

We hang multiple handles off one port and delegate the demultiplexing to the key — exactly the shape a server wants: a hundred sockets attached to one port, one loop collects them all, nothing gets crossed, because every packet carries its own origin. One more document reminder, faithfully noted: once a handle is attached to a port, it can no longer be used with ReadFileEx or WriteFileEx, for the reason that each has its own asynchronous completion mechanism. In the previous article's e2 we just accompanied ReadFileEx, whose completion routine goes through APC; APC's road and the port's road do not merge — once attached, this handle's completion notifications belong to the port. The convention is the document's; we did not measure it here, so take the document as authoritative.

What gets attached is not picky either. The document's term file handle means every endpoint that supports overlapped I/O: files, named pipes, mailslots — sockets can attach too, and the document even provides an example for AcceptEx. Looking back over the experiments' attachment targets: e1 attached a file; e2, e5, and e6, which do in-flight reads, attached nothing but pipes; e3 and e4 use bare ports with not a single device attached; and e5b attaches, of all things, a Job object — it does not even qualify as an I/O device — yet the packet-taking road is the same one. One harvesting structure covers everything that can send completion packets into a port.

## Completion Order: The Port Takes Them as They Complete

e2 uses the same setup as the previous article's e5: six named pipes, the read side issuing in-flight reads in order 1 to 6 with the key set directly to the issue number, the write side running in reverse — feeding one round every 60ms in the order 6 to 1. The previous article harvested with WFMO; this article swaps in a single-threaded GQCS taking six in a row, on the same set of pipes. Let us see whether the completion order changes face when the harvesting tool changes:

```text
[    16 ms] 六发在途读按 1..6 投递,全部挂同一端口(写端将按 6..1 投喂,间隔 60ms)
[    16 ms]   投递#1: ret=0 gle=997
...
[    16 ms] 收割循环:单线程 GQCS 六连取
[    94 ms]   完成第 1 个:是投递#6(GQCS ret=1 bytes=8 key=6 值=6)
[   172 ms]   完成第 2 个:是投递#5(GQCS ret=1 bytes=8 key=5 值=5)
[   234 ms]   完成第 3 个:是投递#4(GQCS ret=1 bytes=8 key=4 值=4)
[   297 ms]   完成第 4 个:是投递#3(GQCS ret=1 bytes=8 key=3 值=3)
[   344 ms]   完成第 5 个:是投递#2(GQCS ret=1 bytes=8 key=2 值=2)
[   406 ms]   完成第 6 个:是投递#1(GQCS ret=1 bytes=8 key=1 值=1)
[   406 ms]  投递序: 1 2 3 4 5 6;完成序: 6 5 4 3 2 1
```

The five lines for issues #2 through #6 all look alike, so we omitted them. The completion order came out 6 5 4 3 2 1 — not one digit off the previous article's WFMO record.

The same scene in the previous article also left a footnote: with multiple issues in flight, the handle signal cannot tell who finished, which is why the event style forces one manual-reset event per issue. On the port side, even the footnote is retired — the taken packet carries the two-level identity of pov and key, one port manages N issues, and the event quota problem is gone at the root.

When a single thread takes packets in a row, the queue dequeues in entry order, and the entry credential is the completion moment: whichever issue's data arrives enters the queue, and the submission order counts for nothing. The document sets up the exception ahead of time; the gist of the original words is that packets enter FIFO but may be taken out of order — out-of-order taking shows up only when multiple threads take together. In e2 only our one thread takes packets, so the out-of-order door never opens. Running the same scene in both articles amounts to a controlled pair: the completion order is decided by the writer's feeding rhythm and has nothing to do with the harvesting tool — the difference is entirely in the harvesting structure. The previous article's round wore six events; this round's six issues occupy one port, and we do not even need to ask which issue completed, for the packet spells it out for us. The submission order is ours to arrange; the completion order is the device's to decide. In synchronous code these two things naturally stick together; in asynchronous code we have to think them apart.

## Four Threads Taking Packets: The Concurrency Value Is a Ceiling

GQCS has one more property the event style cannot offer: it lets multiple threads sleep at the same exit simultaneously, and each completion packet lands in exactly one thread's hands — two threads getting the same issue never happens. The hundred-event approach must either have one thread hug the wait or open several threads, slice the array into segments, and have each wait on its own; one port lets us open as many taking threads as we like, and what the kernel guarantees is hand-to-hand delivery. The stage we built in e3: four worker threads all sleeping hard in GQCS(INFINITE) waits while the main thread posts 8 packets into the port, with the posting rhythm and the port's concurrency value each split into two settings — we look at them cell by cell. The packets did not travel the real I/O road; they are synthetic completion packets made by `PostQueuedCompletionStatus`, the protagonist of e4 — for now, understand it as stuffing packets into the queue. Port A's concurrency value is 0, which resolves to 16 on this machine:

```text
[     0 ms] == 端口A:并发值=0(默认=处理器数,本机 16),4 工线程,8 包间隔 50ms ==
[   312 ms]   工3 拿到包#1 key=1 bytes=100 (tid=31360)
...
[   781 ms]   工3 拿到包#8 key=8 bytes=107 (tid=31360)
[   844 ms]   8 包全部有了主,首包被取走时刻 312 ms
[   906 ms]   工3 收到关停标记,收工 (tid=31360)
[   906 ms]   工2 收到关停标记,收工 (tid=6020)
[   906 ms]   工1 收到关停标记,收工 (tid=5588)
[   906 ms]   工4 收到关停标记,收工 (tid=15480)
```

The six lines for packets #2 through #7 are all worker 3, timestamps running from 375ms to 703ms, plus the end-of-block summary line for the 8-packet duration — all in the archive, omitted here. All 8 packets went into worker 3's pocket alone; all eight lines carry tid 31360, the other three worker threads slept from head to tail, and the first shutdown sentinel also landed in worker 3's hands. Port B changes the concurrency value to 1; with the four-thread, eight-packet configuration unchanged, the same single line still collects — only this time the person is worker 4.

```text
[   906 ms] == 端口B:并发值=1,同样 4 工线程、8 包间隔 50ms ==
[  1219 ms]   工4 拿到包#1 key=1 bytes=100 (tid=23772)
...
[  1672 ms]   工4 拿到包#8 key=8 bytes=107 (tid=23772)
```

The six middle lines are all worker 4; the packet-count summary at block end, the four shutdown-sentinel lines, and the duration line are also in the archive, omitted together. Port C cuts a more extreme figure: the concurrency value stays at 1, but the eight packets are posted in one breath with not a hint of spacing:

```text
[  1797 ms] == 端口C:并发值=1,4 工线程,8 包一口气全投(无间隔) ==
[  2109 ms]   工4 拿到包#1 key=1 bytes=100 (tid=32524)
[  2109 ms]   工4 拿到包#2 key=2 bytes=101 (tid=32524)
[  2109 ms]   工4 拿到包#3 key=3 bytes=102 (tid=32524)
[  2109 ms]   工4 拿到包#4 key=4 bytes=103 (tid=32524)
[  2109 ms]   工4 拿到包#5 key=5 bytes=104 (tid=32524)
[  2109 ms]   工4 拿到包#6 key=6 bytes=105 (tid=32524)
[  2109 ms]   工4 拿到包#7 key=7 bytes=106 (tid=32524)
[  2109 ms]   工4 拿到包#8 key=8 bytes=107 (tid=32524)
```

The fourth cell — concurrency 0 with no spacing — did not make it into e3's original round, so we filled the matrix in: same MSYS2 environment, reusing the same worker and posting skeleton, one fresh process for one more round (timestamps each counting from 0). We paste the raw output in full:

```text
[     0 ms] == 端口D:并发值=0(默认=处理器数,本机 16),4 工线程,8 包一口气全投(无间隔) ==
[   313 ms]   工3 拿到包#1 key=1 bytes=100 (tid=18172)
[   313 ms]   工4 拿到包#3 key=3 bytes=102 (tid=29752)
[   313 ms]   工2 拿到包#5 key=4 bytes=103 (tid=32736)
[   313 ms]   工4 拿到包#6 key=6 bytes=105 (tid=29752)
[   313 ms]   工2 拿到包#7 key=7 bytes=106 (tid=32736)
[   313 ms]   工1 拿到包#2 key=2 bytes=101 (tid=30960)
[   313 ms]   工4 拿到包#8 key=8 bytes=107 (tid=29752)
[   313 ms]   工3 拿到包#4 key=5 bytes=104 (tid=18172)
[   328 ms]   8 包全部有了主,首包被取走时刻 313 ms
[   391 ms]   工3 收到关停标记,收工 (tid=18172)
[   391 ms]   工2 收到关停标记,收工 (tid=32736)
[   391 ms]   工4 收到关停标记,收工 (tid=29752)
[   391 ms]   工1 收到关停标记,收工 (tid=30960)
[   391 ms] 端口D 收完 8 包,历时 391 ms
[   391 ms] iocp-e3d 完
```

With the four cells complete, the port changed its temper, and the watershed is how urgently the packets arrive. In the two interval-fed rounds (ports A and B), concurrency 0 and 1 ended the same way: all 8 packets fell into one thread's hands and the idle workers did not wake. In the two 8-in-one-breath rounds, the concurrency value gets its say: port C still had the same single line collecting back to back within the same tick, while port D let all four sleeping workers out — within the same 313ms timestamp, four lines split the 8 packets with four distinct tids. Let us state the concurrency value's semantics plainly here: it governs a ceiling, not a quota. It takes no part in assigning work; when packets arrive at intervals, one body at the door suffices, and the other threads sleep soundly. What it governs is admission: when packets pile up and the ceiling is wide, several threads working together is allowed. The document's `maximum number of threads that the operating system can allow to concurrently process` says exactly that — at most how many are allowed — and nowhere in the document can we find any promise of how many will work together.

How many hands the 8 packets fell into is the phenomenon this round measured; why the port schedules this way rests on documented convention plus mechanism interpretation, not a measured verdict. The rule for who wakes has ready-made text on the concepts page: blocked threads are released in LIFO order — the original words say `the system releases the last (most recent) thread associated with that port` — and when a new packet enters the queue, the system counts running threads and, short of the concurrency value, admits one waiting thread. The two interval-fed rounds read the same rule: the collecting thread finishes a packet and immediately returns to the GQCS door to queue for the next; when the next packet arrives someone is already at the door, and the kernel hands it over — the other threads never have a reason to be woken, saving one full thread wakeup and, with it, one context switch. The first shutdown sentinel going to worker 3 and worker 4 taking over in port B both read the same rule. The two 8-in-one-breath rounds each have their document backing too: concurrency-1 port C matches a dedicated paragraph on the concepts page — when packets keep waiting in the queue, a running thread calling GQCS again does not block; it just takes the next packet, and not one context switch happens. Concurrency-0 port D takes the admission road: packets flooding in, running threads far short of 16, and the sleeping workers let loose one after another. Our packet handling in the experiments is the small matter of printing one line, per-packet cost nearly nil; the parts the interpretation did not measure — say, whether packets costing several milliseconds each would let more threads out — we do not treat as measured verdicts. If you want to verify, add a time-consuming chore to the worker and run it again; how the tid distribution changes, you will see with your own eyes. What we posted were synthetic packets, but the queue they enter is the same one real I/O completions enter, and queueing and waking travel the same road.

Engineering-wise this leaves us two practical reminders. The thread pool's size must be decided by load and per-packet cost; do not rest a design on concurrency N means N threads working — the port guards only the ceiling. The document's rule-of-thumb value is at least twice the processor count for the pool, because a taking thread can get blocked by other waits, and when blocked, someone must step in. The second reminder goes to use immediately: shutdown packets are posted per thread — we post four for four threads, each waking thread claims its own, and having claimed it, exits. Port A and port D both closed this way; four shutdown sentinels saw all four workers off.

## A Bare Port as a Wakeup Channel: PostQueuedCompletionStatus

In e4 we drop I/O entirely and keep only the port itself. A bare port with nothing attached is, in essence, one queue: `PostQueuedCompletionStatus` (PQCS below) does the stuffing, `GQCS` does the taking, and with no I/O at all it still turns. This widens the port's identity: from the convergence point of completion notifications it moonlights as a custom message queue, and this moonlight has a standard use in thread pools — waking workers asleep in a wait, or handing them the closing notice. e4 is built like this: three worker threads sleeping hard in GQCS(INFINITE) waits while the main thread posts three shutdown sentinels — the same kind of shutdown marker used in e3 — with the key agreed as -2. We paste the worker loop verbatim; the sentinel's way of being recognized hides inside:

```cpp
void worker(HANDLE port, int id)
{
    g_workers_up.fetch_add(1);
    for (;;) {
        DWORD bytes = 0;
        ULONG_PTR key = 0;
        LPOVERLAPPED pov = nullptr;
        BOOL g = GetQueuedCompletionStatus(port, &bytes, &key, &pov, INFINITE);
        std::printf("[%6ld ms]   工%d 醒来: GQCS ret=%d bytes=%lu key=%s ov=0x%llx (tid=%lu)\n",
                    ms_now(), id, (int)g, bytes,
                    key == kShutdownKey ? "关停哨兵" : "普通",
                    (unsigned long long)(UINT_PTR)pov, GetCurrentThreadId());
        std::fflush(stdout);
        if (key == kShutdownKey) {
            std::printf("[%6ld ms]   工%d 认出关停,退场\n", ms_now(), id);
            std::fflush(stdout);
            return;
        }
    }
}
```

Here is what it looks like running:

```text
[     0 ms] 裸端口就绪(一枚句柄,没挂任何 I/O 设备)—— 端口自己就是一条完成队列
[     0 ms] [1] 三条工线程睡进 GQCS,主线程逐个 PQCS 叫醒
[   219 ms] 三条线程都已睡进 GQCS,开始投关停包
[   219 ms]   PQCS 关停包 #1: ret=1
[   219 ms]   PQCS 关停包 #2: ret=1
[   219 ms]   工2 醒来: GQCS ret=1 bytes=0 key=关停哨兵 ov=0xdeadbeef (tid=10548)
[   219 ms]   工2 认出关停,退场
[   219 ms]   PQCS 关停包 #3: ret=1
[   219 ms]   工1 醒来: GQCS ret=1 bytes=0 key=关停哨兵 ov=0xdeadbeef (tid=22732)
[   219 ms]   工1 认出关停,退场
[   219 ms]   工3 醒来: GQCS ret=1 bytes=0 key=关停哨兵 ov=0xdeadbeef (tid=16416)
[   219 ms]   工3 认出关停,退场
[   219 ms] 三条工线程全部退场,join 干净返回
[   219 ms] [2] 三件套原样透传:bytes=111 key=888 ov=0xABCD
[   344 ms]   收到: bytes=111 key=888 ov=0xabcd —— 塞什么,收什么
[   344 ms] [3] 收尾后再投一枚没人读的包,端口不吃亏
[   344 ms]   PQCS: ret=1 —— 端口没有活线程也接得住,包在队列里等着
```

The three posted shutdown packets woke the three threads one by one; each recognized the key and exited, join returned cleanly, all within the same 219ms timestamp. How is the sentinel recognized? The key is our private plot: we agreed with the workers that -2 means shutdown and every other key value means real work. The system knows nothing of this layer of semantics; it rests entirely on our mutual agreement. The shutdown marker in e3 used key=-1 — a different value, the identical principle, which goes to show that the value itself does not matter as long as it is agreed.

PQCS's triple passes through verbatim: what we stuff in is what we take out. We verified this specially in e4: bytes stuffed with 111, key with 888, ov with 0xABCD — received word for word; even the received 0xabcd turning lowercase is printf's %llx at work, not the port's hand. One more observation worth recording: after all three threads exited, posting one more returned TRUE, and the packet sat safely in the queue — if we do not take it, it waits there for us indefinitely.

These properties combined are exactly what a wakeup channel should be, and on the Linux side it has an old acquaintance of ours: eventfd. In this volume's Linux-side [timerfd and eventfd: time and events as fds](../../linux/io-multiplexing/02-timerfd-eventfd.md) article, we hung pipes, eventfd, and timerfd together on epoll and used eventfd to wake a loop sleeping in epoll_wait, so that shutdown and cross-thread chores could slip into the event loop. The same chore on the Windows side wakes the loop sleeping in GQCS with PQCS — we do not even need to attach a device. The self-pipe mirror in [Console Events and APC](../process/02-console-apc.md), where the handler does one SetEvent, upgrades to this as the proper road in the port world. And the comparison the previous article's e2 left behind closes right here: on the completion-routine road, the issuing thread must enter alertable waits periodically before the routines will run, while on the port side the thread sleeps at the GQCS exit, where waiting and taking were one action all along — no detour through the alertable state needed.

## Closing a Handle Before In-Flight Work Finishes: Two Fates

The document's advice on closing handles carries real weight: do not close a handle while in-flight I/O is unfinished, let alone free the OVERLAPPED, because the driver may still be writing its things into that memory. The advice we have noted — but what becomes of the completion notification, only measurement can supply.

e5 uses two pipes that never get fed: after the client connects, the server just keeps sleeping. The client issues one 8-byte in-flight read — the data will never arrive — and then we walk the rough road and the proper road separately. The OVERLAPPED stays globally alive throughout, giving the driver no chance to write into freed memory; we watch only the notification semantics. The rough road: while the read hangs, call `CloseHandle` directly.

```text
[     0 ms] == [1] 野路子:读在途,直接 CloseHandle ==
[     0 ms] 投一发在途读: ret=0 gle=997(ERROR_IO_PENDING),然后直接关句柄
[     0 ms] CloseHandle 已返回(句柄值这会儿已经作废),GQCS 等它的完成包,限期 6000ms
[     0 ms] GQCS: ret=0 历时 0 ms gle=109(ERROR_BROKEN_PIPE) pov=就是那发读 —— 野路子的下场,实测口径
```

The completion packet arrived immediately, duration 0ms; GQCS returned FALSE, gle gave 109 (ERROR_BROKEN_PIPE), and pov still acknowledged the very read. Let us lay out the guessable fates. The first guess was silence: wait out the full 6000ms and collect a 258 timeout — but the packet arrived in 0ms, so the first guess came up empty. The second guess was the cancellation receipt 995; the gle number did not match, so it also came up empty. On Win11 26200's named pipes, the act of closing the handle itself condemned the pipe as broken: the driver completed the in-flight read as a broken pipe, the completion packet lined up in the queue as usual, and the identity was acknowledged as usual. At least on this round and this device type, the rough road loses no packets. The proper road plays the control group: we cancel precisely with `CancelIoEx`, GQCS receives a completion packet carrying 995 (ERROR_OPERATION_ABORTED), pov is acknowledged, and only then do we CloseHandle — a clean finish:

```text
[     0 ms] == [2] 正路子:CancelIoEx 撤单 → 等 995 完成包 → 再关 ==
[     0 ms] 投一发在途读: ret=0 gle=997(ERROR_IO_PENDING)
[     0 ms] CancelIoEx(定向): ret=1
[     0 ms] GQCS: ret=0 历时 0 ms gle=995(ERROR_OPERATION_ABORTED) pov=就是那发读 key=2 —— 撤单也有完成包,995 送到
[     0 ms] 收完 995 再 CloseHandle:干干净净
```

GQCS's two meanings of FALSE are the easiest to mix up when reading output. When pov is NULL, it means we did not take a packet at all, and gle reports the failure of the taking itself — the 258 from e1's empty queue walks the first road. When pov is non-NULL, what was taken is a failed completion packet, and gle reports the cause of death of that I/O — both 109 and 995 walk the second. Mix the two FALSEs together and, when troubleshooting asynchronous errors, we would read a timeout as a broken pipe and a broken pipe as a timeout.

The code 995 is an old acquaintance: in [File Locking: LockFileEx](../file-io/05-lockfileex.md) (W05) it traveled with canceled locks, and in the previous article's e3 with canceled reads. The error codes for in-flight requests are now all gathered. 997 (ERROR_IO_PENDING) is the issue receipt — what ReadFile carries when it returns FALSE, meaning this one is in flight. 996 (ERROR_IO_INCOMPLETE) is the in-flight probe — in the previous article's e3 we asked status with `GetOverlappedResult`, and the answer for in-flight was this. 995 (ERROR_OPERATION_ABORTED) is the cancellation receipt. And 109 carries yet another identity: the device broke, and the request ended in failure. The proper order before closing a device is one sentence: `CancelIoEx` everything, collect the 995s, then close the handle.

## A Job on the Port: Death Notices Delivered as Completion Packets

The other sentence the processes chapter owed us gets repaid at e5b. In [Processes and Jobs: CreateProcessW and Job Objects](../process/01-createprocess.md), while covering the Job object, we said: on Linux, SIGCHLD is the kernel pushing the death notice over; Windows has no such push, and if we want to wait on a crowd of processes asynchronously, the proper road is attaching the Job to a port — process creation and exit arrive asynchronously as completion packets, with the details left to this chapter. The machinery is in place now; let us run it.

The script falls into three steps. Once the Job is created, `SetInformationJobObject` gets the information class `JobObjectAssociateCompletionPortInformation`, and we fill the port and our custom key into the `JOBOBJECT_ASSOCIATE_COMPLETION_PORT` structure — the attachment is done:

```cpp
JOBOBJECT_ASSOCIATE_COMPLETION_PORT acp{};
acp.CompletionKey = (PVOID)(UINT_PTR)0x99;
acp.CompletionPort = port;
SetInformationJobObject(job, JobObjectAssociateCompletionPortInformation, &acp, sizeof acp);
```

In the second step we launch the child with CREATE_SUSPENDED; the child joins the group via `AssignProcessToJobObject`, and the release uses `ResumeThread`.

The born-suspended two-step is a tool the processes article wore smooth, and here it guards against the child jumping the gun: the child runs cmd /c exit 7 and, once released, lives only a few milliseconds; if it exited before the attachment settled, the message would miss it. The document warns of the same thing — best do the attaching while the Job is quiet — and born-suspended covers this layer as well. The program uses the wide-character entry point wmain, compiled with `-municode`, per the environment conventions stated at the start. One word on the message loop: the experiment uses GQCS with a 3000ms deadline to take at most 12 packets, standing down upon recognizing ACTIVE_PROCESS_ZERO; the empty-handed timeout path is insurance for the unlikely, and this round's insurance went unused. We paste the raw output in full:

```text
[     0 ms] Job 挂端口: SetInformationJobObject ret=1(key=0x99)
[     0 ms] 子进程已建(pause 状态,pid=29036),已入 Job,现在放行
[     0 ms] GQCS: ret=1 bytes=6 key=0x99 ov=0x716c → NEW_PROCESS(新进程进组)
[     0 ms]   ↑ lpOverlapped 里装的是 pid=29036(十进制)
[     0 ms] GQCS: ret=1 bytes=7 key=0x99 ov=0x716c → EXIT_PROCESS(组内进程退场)
[     0 ms]   ↑ lpOverlapped 里装的是 pid=29036(十进制)
[     0 ms] GQCS: ret=1 bytes=4 key=0x99 ov=0x0 → ACTIVE_PROCESS_ZERO(组里没有活进程了)
[     0 ms] ACTIVE_PROCESS_ZERO 到了,收队
[     0 ms] 子进程退场码 = 7(建的时候让它 exit 7);已知 pid=29036 —— 拿这两个数对上面的字段落点
[     0 ms] iocp-e5b 完
```

We got the three-packet run in hand, checked it cell by cell against the document's table, and collected the field landings into one table:

| GQCS output parameter | What it carries | The three packets we received |
| --- | --- | --- |
| lpNumberOfBytes | The message number, which class of event it reports | 6, 7, 4 |
| lpOverlapped | The process id attached to the message, or NULL | 0x716c, 0x716c, NULL |
| lpCompletionKey | The custom key we chose at attach time | 0x99 for all three packets |

Let us identify the three message numbers one by one. Number 6, NEW_PROCESS, reports a new process joining the group; number 7, EXIT_PROCESS, reports a member process exiting; number 4's message is ACTIVE_PROCESS_ZERO, saying the group holds no live processes anymore. 0x716c in decimal is 29036 — exactly the pid at creation; the first two packets speak of the same person, and the third, per the document's convention, gives NULL, because this message carries no process id.

One more document detail worth recording: processes already in the group at the moment of attachment also get a make-up NEW_PROCESS message, so the packet-taking window covers the stock that joined before attachment — we will not miss the comings and goings of pre-existing processes either. The document has a lovely turn of phrase for how these packets arrive: as if the Job itself called `PostQueuedCompletionStatus`. We just verified PQCS's triple pass-through in e4; the channel here is the same one, only the fields' semantics are system-fixed, not self-agreed like our shutdown sentinels.

Two things the completion packets do not give, stated separately. The first missing item is the exit code: EXIT_PROCESS reports only the pid, never the exit code; that 7 from cmd was verified separately with WaitForSingleObject on the process handle plus GetExitCodeProcess — the second-to-last line of e5b's output does exactly that. Death-notice delivery belongs to the port, corpse-collecting belongs to the handle — the two dimensions walk separately, matching Linux's division of labor between `SIGCHLD` announcing and `waitpid` collecting. Delivery guarantee is the second missing item: the document's original words mean that, apart from the limit notifications attached via `JobObjectNotificationLimitInformation`, the messages are positioned as notifications only, with delivery not guaranteed — no message received does not mean no event happened. So ACTIVE_PROCESS_ZERO suits a stand-down signal, not a foundation for correctness; for a reliable judgment, waiting on the handle is the hard evidence. The document adds one more reminder: pids get recycled and reused, so to look a process back up by pid, we must keep holding the process handle.

## A Hundred Issues in Flight: Asking One by One versus Take-As-Given

The processes article's line — beyond that, open threads or move to IOCP — we now cash into numbers with e6. We do the same job twice: one named pipe, the read side carrying 100 one-byte in-flight reads, the write side throwing 100 bytes in one breath, five rounds each. The event-style pass is written the proper way per the previous article: 100 manual-reset events, one per issue. The trouble knocks immediately: WFMO waits on at most 64 at a time — the previous article's e4 measured the wall: 64 passes, 65 returns WAIT_FAILED with 87 (87=ERROR_INVALID_PARAMETER) — so 100 issues must split into two segments: 64 plus 36. The IOCP pass needs one port; we loop `GQCS` 100 times. We paste the event-style harvest loop from the archive verbatim; the structural cost is all written in this code:

```cpp
while (collected < kN) {
    // 两段轮等:64 + 36,这是 64 墙逼出来的结构
    for (DWORD base = 0; base < (DWORD)kN; base += kChunk) {
        DWORD cnt = (kN - base) > kChunk ? (DWORD)kChunk : (DWORD)(kN - base);
        WaitForMultipleObjects(cnt, &evs[base], FALSE, 100);
        ++st.wake_rounds;
    }
    // 全量扫描:醒来只说明「某一段里有好的」,谁好还得挨个问
    for (int i = 0; i < kN; ++i) {
        ++st.scans;
        if (WaitForSingleObject(evs[i], 0) == WAIT_OBJECT_0) {
            DWORD got = 0;
            if (!taken[i] && GetOverlappedResult(P.cli, &ovs[i], &got, FALSE)) {
                taken[i] = 1;
                ++collected;
            }
            ResetEvent(evs[i]);
        }
    }
}
```

We paste both sides' raw records:

```text
[     0 ms] 100 发在途 1 字节读,事件式(WFMO 分段 64+36)与 IOCP(单端口循环)各收 5 轮
[     0 ms] 每轮:写端一次性投 100 字节,读端从投满那一刻起计时
[    15 ms] 第1轮: 事件式   0 ms / 扫描 100 次探针 / 收割轮 2 / 完成 100; IOCP  15 ms / 零扫描 / 完成 100 / 句柄 100 事件 vs 1 端口
[    47 ms] 第2轮: 事件式  16 ms / 扫描 100 次探针 / 收割轮 2 / 完成 100; IOCP  16 ms / 零扫描 / 完成 100 / 句柄 100 事件 vs 1 端口
[    78 ms] 第3轮: 事件式  15 ms / 扫描 100 次探针 / 收割轮 2 / 完成 100; IOCP  16 ms / 零扫描 / 完成 100 / 句柄 100 事件 vs 1 端口
[   109 ms] 第4轮: 事件式  15 ms / 扫描 100 次探针 / 收割轮 2 / 完成 100; IOCP  16 ms / 零扫描 / 完成 100 / 句柄 100 事件 vs 1 端口
[   140 ms] 第5轮: 事件式  16 ms / 扫描 100 次探针 / 收割轮 2 / 完成 100; IOCP  15 ms / 零扫描 / 完成 100 / 句柄 100 事件 vs 1 端口
```

The archive's tail carries one more convention note saying exactly this; we omit the original line. The conventions, honestly stated: wall clocks are the same order of magnitude — the event style reads 0 to 16ms, IOCP reads 15 to 16ms. In a load this small, the pipe's I/O dominates, and the little cost of scanning 100 events drowns entirely in the pipe's latency. If you point at the table's wall clock and say IOCP is fast, this round's data does not support the claim. The difference lands in the structure, visible in the table at three spots. First, handles: the event style costs 100 events plus 1 pipe handle; IOCP costs 1 port plus 1 pipe — as the request count grows, the former's handles grow with it while the latter's stay put. Second, scans: each event-style wakeup knows only that a round of asking is due — it waits both segments, then asks all 100 events one by one; the `++st.scans` position in the loop above is exactly where the probe sits, and each of the 5 rounds reads 100 probes, while IOCP's scan count is zero — each taken packet is the completed issue itself. Third, segmentation: 100 issues exceed the 64 cap, so the event style structurally cannot avoid two-segment polling — at 1000 issues that becomes 16 segments of waiting — while IOCP's loop stays the same one.

One more difference is invisible in the table, so we say it separately: what you hold after waking differs. The event style wakes holding who is ready — the data you still must read yourself. On the port side, the taken packet carries even the byte count read — take it and it is yours, no second read needed. The two behaviors each have a name: the completion-style school is called Proactor, and this article's IOCP is its representative; the readiness-style school is called Reactor, and Linux's epoll is its representative. The full comparison matrix of the two schools, along with how epoll and io_uring plug into C++ abstractions, gets top billing in the master-plan article on cross-platform asynchronous abstractions; here we only point at it.

The batch-taking API has one more member, `GetQueuedCompletionStatusEx`, which takes a whole basket of completion packets in one call and often pairs with the port in high-traffic services. This article did not test it; you can find it in the documentation.

Process death notices ride the same queue now, and the two articles' foundations are complete. In the cross-platform abstractions article we will bring this port on stage alongside Linux's epoll and io_uring for a face-off, fold the Reactor-versus-Proactor differences into one matrix, and see how C++ abstractions take both sides in. See you there.

<ReferenceCard title="References">
  <ReferenceItem
    :id="1"
    title="CreateIoCompletionPort function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/FileIO/createiocompletionport"
  />
  <ReferenceItem
    :id="2"
    title="GetQueuedCompletionStatus function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-getqueuedcompletionstatus"
  />
  <ReferenceItem
    :id="3"
    title="PostQueuedCompletionStatus function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/FileIO/postqueuedcompletionstatus"
  />
  <ReferenceItem
    :id="4"
    title="CancelIoEx function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/FileIO/cancelioex"
  />
  <ReferenceItem
    :id="5"
    title="JOBOBJECT_ASSOCIATE_COMPLETION_PORT structure"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-jobobject_associate_completion_port"
  />
  <ReferenceItem
    :id="6"
    title="I/O Completion Ports"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/FileIO/i-o-completion-ports"
  />
  <ReferenceItem
    :id="7"
    title="GetQueuedCompletionStatusEx function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-getqueuedcompletionstatusex"
  />
</ReferenceCard>
