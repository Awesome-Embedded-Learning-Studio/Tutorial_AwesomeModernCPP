---
title: "Shared Memory: Page-File-Backed Named Mapping Objects"
description: "The closing article of the Windows side of the memory chapter: how do two processes share a block of memory? The answer hides in the INVALID_HANDLE_VALUE that W02 half-revealed — the page-file-backed named mapping object takes the lead role here: a second Create under the same name gets err=183 and even the requested size is silently ignored (ask for 1 MiB and you still receive the 64 KiB object); the name dies with the last handle and the object dies with the last reference — a two-stage death with no unlink, holding across processes the same way; the Global\ privilege threshold blocks only sections, not mutexes; the page-file backing is what puts SEC_RESERVE's reserve semantics back in their rightful place. Cross-process views measured with base addresses 502 GiB apart; the offset must align to the 64 KiB granularity while the length rounds only by pages; the parent's pointer value reads VirtualQuery=MEM_FREE in the child, and a peer probe process that dereferences it collects 0xC0000005. The named synchronization trio: lock-free, two processes lose 83411 of 200000 increments; a named mutex guards exactly two hundred thousand; when the holder dies holding the lock, the waiter receives WAIT_ABANDONED and the lock still works. The signature SPSC ring queue delivers one million messages with zero loss and zero reordering; spin hits 288 million msgs/s versus 4.71 million msgs/s for per-message notification (about 60x slower — hard evidence that events belong only in the fallback role); the hybrid reasoning shows why it would rather pay a per-message fence that is 3x slower than risk a missed-wakeup window. A same-machine WSL2 comparison (events at 212 ns/msg versus eventfd at 264 ns/msg; Sleep(5) actually sleeps 12.6 ms) plus an eight-row shm_open comparison table wrap it up; and e6 in one sentence: passing NULL for the security attributes is not the same as having no DACL."
chapter: 8
order: 2
platform: host
difficulty: advanced
cpp_standard: [20]
reading_time_minutes: 47
prerequisites:
  - "虚拟内存:VirtualAlloc 与 VirtualProtect"
  - "文件映射:CreateFileMapping 与 MapViewOfFile"
related:
  - "文件映射:CreateFileMapping 与 MapViewOfFile"
  - "共享内存:shm_open 与映射"
  - "SPSC 与 MPMC 队列"
tags:
  - host
  - cpp-modern
  - advanced
  - 系统编程
  - Win32
  - 内存管理
  - 并发
  - 优化
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
translation:
  source: documents/vol8-domains/systems-programming/windows/memory/02-shared-mem.md
  source_hash: 5e310fb2805cc5ce09d0fcda89e385172108556cdec43e058da614ce82aac9ab
  translated_at: '2026-11-22T15:35:34+00:00'
  engine: glm-5.3
  token_count: 25430
---
# Shared Memory: Page-File-Backed Named Mapping Objects

In the previous article we accompanied VirtualAlloc through the two-phase model of reserve and commit, governing nothing but our own process's address space: how much address space was registered, whether pages were granted — no other process had any right to ask. This article turns the question around: two processes want to share the same block of memory; what is Windows' road for that?

The answer actually half-showed its face in [file mapping](../file-io/02-file-mapping.md) (hereafter W02). While discussing `CreateFileMappingW` we mentioned in passing: pass `INVALID_HANDLE_VALUE` in the file-handle slot and the mapping hangs off no file at all; the documentation's own words are `CreateFileMapping creates a file mapping object of a specified size that is backed by the system paging file instead of by a file in the file system`. The backing store is swapped from a file on disk to the **system paging file** (pagefile.sys is the swap file the system carves out on disk; when physical memory runs tight, pages that no longer fit are stashed there). Back then it only passed through the SEC_RESERVE section once, and the name likewise got only an offhand mention during W02's parameter walk — our experiments never actually used it. This article unfolds it properly, and fills in the name parameter for the first time too.

The name is what makes the cross-process channel. Give the mapping object a name and it becomes a **named object**: the name is registered in the kernel's object namespace, the file system holds no trace of it, and when another process uses the same name, what it opens is the same object. Linux' counterpart is shm_open with /dev/shm; [the Linux-side shared memory article](../../linux/memory/03-shm.md) is already finished, and we line up the two sides' comparison table at the close. This article is also the closing piece of the memory chapter's Windows side: the experiments run from object lifecycle all the way to a cross-process message queue.

Our experiments are numbered e1 through e6, in one-to-one correspondence with the six directories 01 through 06 under `code/volumn_codes/vol8/systems-programming/windows/memory/02-shared-mem/` in the repository; the code and raw outputs are all archived. The tags in program output read [E1] through [E6]; in the body we call them e1 to e6 — match them against file names and you cannot mistake one for another, and they are independent of the e-series of the earlier articles. The fifth run is the exception: the 05 directory holds the Linux-side same-machine comparison, whose two outputs carry the tags [Linux 侧] and [E4-Linux], never once using the [E5] label — just go by the directory. Environment conventions: the local Win11 26200.9457 machine, still the AMD Ryzen 7 9700X we know well (8 cores, 16 threads); the toolchain is MSYS2 UCRT64 g++ 16.1.0; the compile command line is uniformly `-std=c++20 -Wall -Wextra`, except the e4 throughput benchmark adds `-O2`, while e3's lost-update demo deliberately stays at the default -O0, lest the compiler fuse the read-modify-write three-step into one and optimize away exactly the race we want to watch. Numbers were captured on 2026-10-03; the two sides of the two-process experiments are each pinned to one core, on cpu2 and cpu3 of the same CCD (the die the CPU's cores live on; the 9700X has two, each wrapping four cores). The cross-system way of running things is still the WSL interop chain introduced in [Win32 File I/O](../file-io/01-win32-file-io.md) (W01), carried over unchanged.

The common tools keep the old lineup: `unique_handle` and `last_error_code` are defined in [the RAII paradigm](../../thinking/01-raii-paradigm.md) and [error-handling paradigms](../../thinking/02-error-paradigm.md); `check_win32` stays defined in W01 — we reference them without rewriting. This article adds three new pieces in the archive's `common/shm_util.hpp`: `unique_view` manages the view base address's UnmapViewOfFile, its skeleton isomorphic to W02's mapped_view. `qpc_ns` and `qpc_ms` are QueryPerformanceCounter timing wrappers. The most important is `spawn_self`, with which the parent process uses CreateProcessW to pull up another copy of itself under a new command line:

```cpp
// shm_util.hpp:把自己再拉一份(本篇双进程实验的统一起跑方式)
inline bool spawn_self(const wchar_t* args, PROCESS_INFORMATION& pi)
{
    wchar_t path[MAX_PATH * 2];
    if (!GetModuleFileNameW(nullptr, path, MAX_PATH * 2)) { return false; }
    std::wstring cmd = std::wstring(L"\"") + path + L"\" " + args;
    STARTUPINFOW si{};
    si.cb = sizeof si;
    BOOL ok = CreateProcessW(path, cmd.data(), nullptr, nullptr, FALSE,
                             CREATE_UNICODE_ENVIRONMENT, nullptr, nullptr, &si, &pi);
    return ok != FALSE;
}
```

We pass FALSE for bInheritHandles — not a single handle is inherited: all the two-process cooperation in this article rides on opening by the same name; the named object itself is the channel we want, which squares exactly with this article's theme. Two more disciplines are written in shm_util.hpp's header comment: start synchronization always uses a named event, and every printf in both parent and child must be followed by fflush — stdout under a pipe is fully buffered, and without flushing, the two sides' outputs land in the archive out of order. Named objects' names uniformly carry a pid, so that half-dead objects left over from a previous crashed round cannot collide by name.

## Create Means Also Open: Name Collisions, the Silently Ignored Size, and the Two-Stage Death

e1 opens by picking up the thread W02 left hanging: the same name, and we Create three times in a row. The first request, 64 KiB, goes normally — err reads 0. The second time, same name, the object already exists; the handle is valid as usual, but GetLastError returns 183, the familiar ERROR_ALREADY_EXISTS. The third request goes further: it asks for 1 MiB, and what comes back is still 183 plus a valid handle; we measure the actual mapping with VirtualQuery and RegionSize reads 0x10000 — still that 64 KiB object: the requested size was silently ignored.

```cpp
// e1_lifecycle.cpp(节选):同名三连 Create,尺寸静默被忽略
SetLastError(0);
HANDLE h1 = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                               0, 64 * 1024, name.c_str());
SetLastError(0);
HANDLE h2big = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                  0, 1024 * 1024, name.c_str());   // 同名,请求 1MiB
void* vbig = MapViewOfFile(h2big, FILE_MAP_READ, 0, 0, 0);
MEMORY_BASIC_INFORMATION mbi{};
VirtualQuery(vbig, &mbi, sizeof mbi);   // RegionSize 作证:对象到底多大
```

```text
$ ./e1_lifecycle.exe; echo "exit=$?"
[E1] 命名对象生命周期,pid=20084,名字=Local\SysProgShm_E1_20084

[1] CreateFileMappingW 两次同名 + OpenFileMappingW(括号里是进程句柄总数)
  第1次 Create(请求 64KiB)  -> h=00000000000000f0 err=0  (handles 60->61)
  第2次 Create(同名)       -> h=00000000000000f4 err=183  (183=ERROR_ALREADY_EXISTS,句柄照常有效)
  第3次 Create(同名,请求 1MiB) -> h=00000000000000f8 err=183,映射后 RegionSize=0x10000(64KiB) <- 尺寸被忽略,还是 64KiB 那个对象
  OpenFileMappingW(存在)   -> h=00000000000000f8 err=0  (handles 62->63)
  OpenFileMappingW(不存在) -> h=0000000000000000 err=2  (2=ERROR_FILE_NOT_FOUND)
(段间的空行与收尾的 [E1] 完 一行,见下方小观察一段,略)
```

The documentation spells the behavior out; let's set its words on the table: `If the object exists before the function call, the function returns a handle to the existing object (with its current size, not the specified size), and GetLastError returns ERROR_ALREADY_EXISTS`. Give the half-sentence in parentheses an extra look: what you get is the existing object's current size, not the size you specified. So CreateFileMappingW's real semantics are **create-or-open**, and whether the object you hold is freshly built is something you learn from GetLastError's expression. The lesson for protocols is quite practical: two processes agree on a name, one side leads with Create and the other follows with OpenFileMappingW — this is not merely a matter of style. If someone Creates a larger size under a stale name, it hands you the old object without a word; the size is wrong and it never tells you.

The two OpenFileMappingW lines complete the other side: when the name exists the open succeeds; when it does not, what returns is the failure value NULL with err=2. Its documentation's criterion for "existing" is also interesting — we invited the original words out: `If there is an open handle to a file mapping object by this name ... the open operation succeeds`. When you open by name, whether you succeed depends on whether any open handle remains under that name; the criterion hides inside exactly this. That sentence buries the most valuable discovery of this batch of experiments: the name and the object die at different moments.

The second segment of e1 demonstrates the two separately. We hold a string of handles h1, h2, h3, build one full view, and then close the handles one by one: when h1 is closed, h2 is still alive and opening by name still succeeds — handles held by other processes keep the name alive just the same. Once our process's handles are all closed, we open by name again and what returns is immediately err=2: the name is gone. But the view is still alive: we write 4096 bytes into it and read them back, and all 4096 bytes match. The object has entered anonymous life-extension; only when UnmapViewOfFile lets go of the last reference does it truly die.

```text
[2] 生命周期两段论:名字死于最后一个句柄,对象死于最后一个引用
  MapViewOfFile(h2 全景)   -> base=0000021691e30000
  CloseHandle(h1),h2 还拿着句柄 -> 按名打开: h=00000000000000f0 err=0 (有句柄在,名字就在;h4复用了 刚关掉的 h1 那个句柄槽)
  关掉 h2/h3:句柄数回到 60(基线 60),本进程再无该对象的句柄
  句柄全关后写视图 4096 字节再读回:4096/4096 字节相符 —— 视图是另一类引用,对象还活着
  句柄全关后按名再打开      -> h=0000000000000000 err=2 (2=ERROR_FILE_NOT_FOUND!)名字在最后一个句柄关闭那刻就摘了,对象匿名续命
  UnmapViewOfFile:最后一根引用撒手,对象此刻才销毁(没有显式 unlink;名字已亡,销毁无从按名查证)
```

Lay the timeline out and you have the **two-stage death** arrangement: the name follows the last handle — the moment the handle count reaches zero, the name is removed from the namespace on the spot; there is no unlink for us to call. The object follows the last reference — handles and views both count as its references; the view keeps the object alive, the handles keep the name alive. On the Linux side, shm_unlink decides the name's life and death explicitly, independent of how many mappings exist. On the Windows side, the name's disappearance is a side effect of reference counting. It holds across processes the same way, and we will meet it again in e2.

One small observation, also printed in the output: h4 reused the handle slot just vacated by h1 — the two values are identical, and even the author did a double take. A handle is at bottom a slot number in the process's handle table; close it and it is recycled. Two identical handle values in a log prove nothing — do not take them as evidence of the same object. The closing line of the full run also checked a small thing for us: the process's handle count returned to the baseline of 60; not one handle leaked.

::: warning Two silences and one refusal
When you Create again under an existing name, the size is silently ignored — the documentation says so outright, `with its current size`. When all handles close, the name silently disappears: no unlink, no advance warning of any kind. Neither of these reports an error, and a protocol has only two defenses: carry a pid in the name to guard against stale leftovers, and after Create, measure the actual size once with VirtualQuery. In contrast, MapViewOfFile's out-of-bounds requests are refused generously: a 512 KB request against a 256 KB object returns outright NULL plus err=5 — no truncated short view for you; we leave the evidence in e2.
:::

## The Global\ Privilege Threshold, and SEC_RESERVE Finding Its Place

Names also have a prefix subtlety: objects headed Local\ register in the current logon session's namespace — everything this article's experiments use. Objects headed Global\ enter the cross-session global namespace: it is how service processes and user processes recognize each other. e1 tried the Global\ threshold once, and the result surprised us a little: inside one and the same unprivileged process, CreateFileMappingW building a Global\ page-file-backed mapping collects NULL with err=5 (ACCESS_DENIED) — the SeCreateGlobalPrivilege privilege it demands is one we do not hold. Immediately after, CreateMutexW building a named mutex with the same prefix succeeds on the first try.

The documentation draws the boundary clearly; the original words say `this privilege check is limited to the creation of file mapping objects and does not apply to opening existing ones` — the privilege check governs only the creation of sections; it does not even check opening existing ones, let alone the mutex we just built. Another half-sentence we must carry along: `from a session other than session zero` — service processes in session 0 building global objects are not in the checked ranks to begin with. If you memorize "Global\ requires privilege" as a blanket rule, you will not get past the mutex case; the measurements and the documentation stand on the same side.

```text
[3] 名字空间:Local\ 按会话隔离,Global\ 要特权(section 才要)
  CreateFileMappingW(Global\...section) -> h=0000000000000000 err=5 (5=ERROR_ACCESS_DENIED,SeCreateGlobalPrivilege 未持有)
  CreateMutexW     (Global\...mutex)    -> h=00000000000000f8 err=0 (成功!特权门槛只拦 section,不拦互斥体/事件/信号量)

[4] SEC_RESERVE|PAGE_READWRITE(页文件后备,保留语义的正主)
  4MiB SEC_RESERVE 映射 -> base=0000021692140000 State=MEM_RESERVE Protect=0x0 RegionSize=0x400000
  VirtualAlloc(首页 MEM_COMMIT) -> 0000021692140000,State=MEM_COMMIT Protect=0x4
  提交后写读:*(uint64_t*)base=0x5A5A5A5A5A5A5A5A
```

SEC_RESERVE also finds its rightful place here. In W02 we paired SEC_RESERVE with a real file; the documentation called it no effect, and in measurement it silently degraded into an ordinary mapping without so much as an error code — the judgment left behind then was that it belongs with the page-file backing. Section [4] of e1 redeems that sentence: a 4 MiB SEC_RESERVE page-file-backed mapping, with VirtualQuery reading State=MEM_RESERVE and Protect=0 — the address range registered, no pages granted. After VirtualAlloc commits the first page, State flips to MEM_COMMIT; we write a magic number starting 0x5A5A and read it back byte for byte identical. The documentation's conditional reads `If the file mapping object is backed by the operating system paging file` — W02's surprise and this round's reserved redemption part ways on exactly that condition. The two-phase reserve-and-commit model we just walked through in the previous article is precisely the same two-phase model the shared-memory side delivers.

## Two Processes, One Block of Memory: Only Offsets Can Be Passed

In e2 we put the second process properly on stage: the parent Creates the named object, spawn_self pulls up the child, the child opens with OpenFileMappingW by name, and each side maps its own view with MapViewOfFile. The parent writes 0xC0FFEE at offset 0x1000; the child reads the same value back at its own base address. The child writes a verification value back at offset 0x20000, and the parent reads it too. What the two processes share is the object's contents, not any address.

Nobody promises the addresses match. We deliberately had the child map an unrelated 16 MiB anonymous region first and then the target object, and the two base addresses split at once: the parent's 0x1835F6F0000, the child's 0x200F2E60000, differing by 539344961536 bytes — roughly 502 GiB, half a terabyte of magnitude. The two views hang off the same object, but each side's address placement consults its own allocation history, and two sides with different histories naturally land on different bases.

So can we write pointers into shared memory? e2 tried that move for us too. The parent writes its own slot's pointer value 0x1835F6F1000 in as a number; the child reads it out and sets it beside its own same-slot address — they do not match. The child asks VirtualQuery about that value and State reports MEM_FREE: this address was never mapped in the child's space at all. Merely asking did not satisfy us; the parent also pulled up a probe process peer to the child, which actually dereferenced it — the exit code came back 3221225477, which is 0xC0000005 in decimal. The documentation's admonition on this counts word for word: `Do not store pointers in the memory mapped file; store offsets from the base of the file mapping so that the mapping can be used at any address` — translated: store no pointers; what you store are offsets. Our protocol carries nothing but offsets from start to finish — even the slot-array position in the queue head is written as an offset. For full codes we rely on the out-of-process probe: GCC does not accept `__try/__except`, and the in-process way to catch exceptions was covered in [SEH and VEH](../file-io/03-seh-veh.md) (W03). We have more than one place to crash in this article, and the out-of-process approach is the least trouble.

How views are sliced still follows the same requirements W02 covered: MapViewOfFile's offset must align to the allocation granularity, 64 KiB on this machine. We had e2 eat this twice, once in each process: with offset=4096, the page aligns but the granularity falls short, and what returns is straight NULL with err=1132 (ERROR_MAPPED_ALIGNMENT). Parent and child follow the same rulebook: offset=0x10000 with len=4096 succeeds, VirtualQuery measures RegionSize=0x1000 — the length rounds by pages only, while the base still lands on a 64 KB boundary. Granularity governs the offset and the base address, and has nothing to do with length. Over-long requests get equally no room for negotiation; the two err=5 entries in the output are both it.

```text
$ ./e2_cross_view.exe; echo "exit=$?"
[E2] 跨进程视图,父 pid=13288,对象=Local\SysProgShm_E2_13288(256KB)
[父] 全景视图 base=000001835f6f0000  dwAllocationGranularity=65536(0x10000)
[子] pid=26560,先映一块无关 16MiB 区 base=00000200f2fd0000,再映目标对象 base=00000200f2e60000
[父] 子进程基址 = 0x200F2E60000,我的基址 = 0x1835F6F0000,两边不同——这就是只能传偏移的原因
[父] 偏移 0x1000 写 0xC0FFEE;偏移 0x1008 写父侧指针值 0x1835F6F1000
[父] MapViewOfFile(offset=4096, len=8192) -> 0000000000000000 err=1132(offset 不按 64KB 对齐的下场)
[父] MapViewOfFile(offset=0x10000, len=4096) -> 000001835f430000 err=0(64KB 对齐;长度不必对齐)
[父] 4KB 切片视图 VirtualQuery:State=MEM_COMMIT Protect=0x4 RegionSize=0x1000(长度按页取整就是 4KB;基址 000001835f430000 仍落在 64KB 边界——粒度管 offset 与基址,不管长度)
[父] MapViewOfFile(offset=0, len=512KB 超对象) -> 0000000000000000 err=5(不给截断的短视图,整个请求直接拒绝)
[探针-ptr] 读到父指针值 0x1835F6F1000,现在真的去解引用它...
[父] 探针(ptr)退出码=3221225477(0xC0000005) —— STATUS_ACCESS_VIOLATION,和预告的一样
[探针-pastend] 超长视图 base=0000000000000000,摸偏移 0x40000(对象只有 0x40000)...
[探针-pastend] 视图都没给 err=5
[子] 读偏移 0x1000 的值 = 0xC0FFEE(A 写 B 读成立)
[子] 父指针值=0x1835F6F1000  我的同槽地址=0x200F2E61000  相差 +539344961536
[子] VirtualQuery(父指针值) 在我的空间里:State=MEM_FREE —— 这地址在我这儿根本没映射,解引用必 0xC0000005(父进程另派了探针进程实撞)
[子] MapViewOfFile(offset=4096) -> 0000000000000000 err=1132(对齐规则对谁都是同一套)
[子] 经全景视图读偏移 0x10000:0xFEEDFACE
[父] 子回写校验 echo=0xFE2DAEED(期望 0xFE2DAEED) —— 偏移协议全程自洽
(中段探针 pastend 的退出码一行、尾段的跨进程复盘与 exit 行在存档,下一段复述,略)
```

We single out the output's tail segment: e1's two-stage death is pushed across processes here. The parent closes all its mapping handles; the child still holds one, and opening by name still succeeds. Once the child is told to close its handle too, the name disappears immediately, yet the child's view still succeeds writing 0x1234ABCD at offset 0x20000, and the parent reads the value out of its own view. The name is dead, the object lives on — the two-stage death holds just the same between two processes.

## The Named Synchronization Trio: Mutexes, Events, Semaphores

The memory is shared, and so is the trouble. We have two processes doing read-modify-write on the same block of memory; the race is identical to the one between threads, only fiercer: not even the natural ordering inside one process survives. The first round of e3 is the lock-free control: a uint64 counter in shared memory, parent and child each pinned to one core, each performing one hundred thousand increments; the moment the starting event fires, they set off together. The three-step read-modify-write collide, and the final value drops to 116589 against an expected 200000 — 83411 lost updates, more lost than kept by half.

```cpp
for (int i = 0; i < 100000; ++i) {
    if (lock) { WaitForSingleObject(mtx, INFINITE); }
    *counter = *counter + 1;   // 故意非原子:读-改-写三步,没锁就互相踩
    if (lock) { ReleaseMutex(mtx); }
}
```

What guards it is exactly the named mutex we invited. Give CreateMutexW a name and it becomes a named object in the kernel; parent and child each take a handle by name, and what they wait on is the same lock. The second round runs with the lock on and the final value comes out exactly 200000, with zero losses. CreateMutexW's name-collision semantics are the same set as the mapping object's: Create again under an existing name and the handle is still valid, with err still 183. Here lies a structural fork with Linux: pthread mutexes govern within one process by default; going cross-process requires the PTHREAD_PROCESS_SHARED attribute, with the lock's body placed inside shared memory. Windows' locks do not live in shared memory — they live in the kernel, and the name is their channel.

```text
$ ./e3_named_sync.exe mutex; echo "exit=$?"
[E3-mutex-无锁] 两进程各 10 万次共享计数自增
[父] 终值=116589 / 期望 200000 -> 丢更新(差 83411 次)
[worker pid=2248] 完成(互斥体护驾),此刻计数=200000

[E3-mutex-锁] 两进程各 10 万次共享计数自增
[父] 终值=200000 / 期望 200000 -> 一个不丢(差 0 次)
exit=0

$ ./e3_named_sync.exe mutex abandoned; echo "exit=$?"
[abandoner pid=26028] 拿到互斥体(等待结果 0),啥也不干直接 ExitProcess
[E3-abandoned] 持锁暴毙:子进程抱着命名互斥体 ExitProcess
[父] 子进程退出码=1,现在去等那把没人放的互斥体...
[父] WaitForSingleObject 返回 128(WAIT_ABANDONED=0x80) —— 互斥体没坏,拿到手还能继续用
exit=0
(无锁一轮 worker 的完成提示与首次 CreateMutexW 的 err 记录在存档,略)
```

The abandoned round is a show Windows alone puts on: the child takes the mutex, does nothing at all, and calls ExitProcess — dying with the lock in its arms. The parent goes to wait on a lock nobody will ever release, and WaitForSingleObject returns 128, the value of **WAIT_ABANDONED**, 0x80; abandon means forsaking — the holder died without releasing. The documentation's original words hand over what follows: `Ownership of the mutex object is granted to the calling thread and the mutex state is set to nonsignaled` — ownership is awarded outright to the waiter, the lock's state resets too, and carrying on with it is fine. It adds one line: `If the mutex was protecting persistent state information, you should check it for consistency` — the lock is not broken, but the shared data it protected may be half-modified; once you hold the lock, you should inspect the scene. Set against Linux: when a pthread mutex's holder dies, the default reaction is to wait forever — getting EOWNERDEAD requires the extra robust attribute. The named mutex's orphan takeover, the kernel throws in for free.

The trio's second member is the event, CreateEventW. One parameter, bManualReset, decides whether it works as broadcast or as single-wakeup. A manual-reset (TRUE) event, once set, stays signaled — we SetEvent just once and every waiter is released, until someone calls ResetEvent. An auto-reset (FALSE) event's signaled state is exactly enough to release one; the release auto-resets, and the second waiter must wait for the next round. e3 invites two waiting processes each time, and the parent SetEvents only once: in the manual-reset round, both waiters' measured waits land around 59 milliseconds — released simultaneously. In the auto-reset round, the first firing releases only one; the parent fires one more SetEvent, and only then does the other take the stage.

The trio's third member is the semaphore, which governs quota. CreateSemaphoreW opens with an initial value of 2 and a maximum of 2, and we send three processes to queue: the first two pass the gate in 1 microsecond, instantly through; the third waits a full 304982 microseconds, until the parent, 300 milliseconds later, supplies a ReleaseSemaphore(1), and only then does it get its slot. The prev count before the release is 0 — the quota really was exhausted by the first two; this out parameter is the best witness.

This experiment also had a false negative in its first version, worth recording. In the first version, a waiter that arrived early returned its slot with ReleaseSemaphore on the way out, and the \wsl.localhost chain W01 described (formally a UNC path — Windows' \\server\share style network path) pulled processes up half a beat slow; by the time the third, late, one arrived, the slot had already been handed back, it passed the gate in one microsecond, never once tasting what blocking means — the experiment looked fully green while the state it meant to demonstrate never showed. The fixed version has waiters clutch their slots until the parent's closing command; only then does the blocked state appear stably. Timing design for multi-process experiments requires more than thinking about synchronization primitives — process startup latency matters too; the author took a fall on exactly this.

```text
$ ./e3_named_sync.exe event; echo "exit=$?"
[手动复位事件] 两个等待进程就位,父进程只 SetEvent 一次
[父] SetEvent 一次
[父] waiter0 等待时长 59424 微秒(结果 0)
[父] waiter1 等待时长 59418 微秒(结果 0)
[父] 手动复位:一次 SetEvent,两个 waiter 都醒——广播

[自动复位事件] 两个等待进程就位,父进程只 SetEvent 一次
[父] SetEvent 一次
[父] 第一发只放行了 waiter 1;再 SetEvent 一发才轮到另一个
[父] 自动复位:一次 SetEvent 只放行一个;第二个得等下一次
exit=0

$ ./e3_named_sync.exe semaphore; echo "exit=$?"
[sem-waiter 0 pid=12892] 过闸:等待结果=0,排队 1 微秒(名额到手,攥着不放直到父进程放行)
[sem-waiter 1 pid=23288] 过闸:等待结果=0,排队 1 微秒(名额到手,攥着不放直到父进程放行)
[sem-waiter 2 pid=10844] 过闸:等待结果=0,排队 304982 微秒(名额到手,攥着不放直到父进程放行)
[E3-semaphore] 初值 2 的命名信号量,3 个进程排队
[父] 300ms 后 ReleaseSemaphore(1) -> ret=1 prev=0 err=0(0=释放前名额已被两个 waiter 占光)
[父] 前两个瞬时过闸、第三个干等 300ms+——计数信号量跨进程管并发名额
exit=0
```

## The Signature Experiment: An SPSC Ring Queue Moves Into Shared Memory

With synchronization covered, we put every tool this chapter has gathered to work and build something real: a cross-process message queue. The queue's body is an SPSC ring buffer (SPSC abbreviates Single Producer Single Consumer — one producer facing one consumer); vol5's [SPSC and MPMC queues](../../../../vol5-concurrency/cookbook/b-topics/03-lock-free-queues.md) fully covered the lock-free version's theory and memory ordering, so here we do only the engineering: move the ring into a page-file-backed object, with producer and consumer as two separate processes, each pinned to its own core.

The ring's layout lives entirely in shared memory; the opening 4 KiB is the queue head:

```cpp
struct alignas(64) RingHead {                 // 共享内存里的队列头(首 4KB)
    uint64_t magic;
    uint64_t nmsgs;
    uint64_t capacity;
    uint64_t mask;
    std::atomic<uint64_t> tail;               // 生产者写(消费者读)
    char pad0[64 - sizeof(std::atomic<uint64_t>)];
    std::atomic<uint64_t> head;               // 消费者写(生产者读)
    char pad1[64 - sizeof(std::atomic<uint64_t>)];
    std::atomic<uint64_t> sleeping;           // hybrid:消费者要睡了
    // 统计字段(消费者写,生产者收尾读)从略
};
struct Slot { uint64_t value; uint64_t tag; };   // 16B 消息,4096 槽
```

tail and head each occupy a full cache line, and the padding bytes between them are not decoration: the two processes each write their own index, and were they squeezed onto the same line, that line would bounce back and forth between the two cores — before our queue even starts running, this line would already be fighting itself; vol5's name for it is false sharing. We fix messages at 16 bytes: value carries the sequence number, tag a checksum stirred from it, and the consumer verifies message by message — lose one or reorder one, and it cannot hide. The slot array starts at offset 0x1000 — our same old line: the protocol carries only offsets.

The queue head needs no dedicated initialization protocol of ours. The documentation promises this about page-file-backed objects: `The initial contents of the pages in a file mapping object backed by the operating system paging file are 0` — the initial contents are all zero. Zero is already a legal value for lock-free atomics; the producer process uses placement new to rebuild RingHead over the zeroed pages, then fills in capacity and the magic number, so that when the consumer opens the object, the first thing it sees is a complete, legal queue.

Data visibility rides entirely on tail and head's acquire/release; we do not even need CAS (compare-and-swap) — this is vol5's single-writer-single-reader, dual-index ring queue carried over verbatim. The one thing left to design: what does the consumer do upon finding the queue empty? e4 prepared four answers, each a mode of its own to compare:

- spin: on empty, spin in place
- notify: SetEvent for every message
- hybrid: spin primarily; sleep only when sleep will truly hold
- burst: the producer rests 5 milliseconds every ten thousand messages, forcing the consumer to genuinely walk through sleep and wakeup

Now look at hybrid: how the consumer gets to sleep, and how it does not miss the wakeup — this is the protocol (excerpt; the producer's if/else two branches merged into one):

```cpp
// 消费者:空转 2 万次还空,才立牌位睡觉
for (int i = 0; i < 20000 && h == t; ++i) {
    _mm_pause();
    t = rh->tail.load(std::memory_order_acquire);
}
if (h == t) {
    rh->sleeping.store(1, std::memory_order_seq_cst);   // 牌位:我要睡了
    t = rh->tail.load(std::memory_order_acquire);        // 立完再查一遍 tail
    if (h == t) { WaitForSingleObject(data, INFINITE); ++sleeps; }
    rh->sleeping.store(0, std::memory_order_seq_cst);
}
// 生产者:发布 tail 之后,fence 再查牌位
rh->tail.store(tail, std::memory_order_release);
if (!m_spin && !m_notify) {                              // hybrid 路线
    std::atomic_thread_fence(std::memory_order_seq_cst);
    if (rh->sleeping.load(std::memory_order_relaxed)) { SetEvent(data); }
}
```

We pair the two sides' seq_cst operations together; drop either side and a window opens: between the consumer consuming the last message and raising the sleeping flag, the producer happens to finish publishing and glances at the flag, sees 0, and skips the event — a missed wakeup. When the consumer, flag raised, goes back to re-check tail, it happens to read the stale value, goes to sleep, and nobody ever calls it again. The consumer's re-check and the producer's fence, combined, guarantee that at least one side sees the other's move; this pairing is the entirety of hybrid's no-missed-wakeup guarantee.

At the scale of one million messages and 4096 slots, we run spin, hybrid, and notify on the Windows side three rounds each and take the median, and burst one dedicated round; in every round the consumer's verification errors number 0 — value and tag check out message by message, a clean sheet of zero loss and zero reordering. Here is the timing table, laid out together with the WSL2-side isomorphic version:

| Mode | Windows native | WSL2 same machine | Notes |
| --- | --- | --- | --- |
| spin (both sides pure spinning) | 3.47 ms / 288M msgs/s | 5.04 ms / 198M msgs/s | The upper bound set by memory speed; the two sides use different compilers, so the gap includes codegen factors |
| hybrid (spin primary, sleep when empty) | 10.77 ms / 93M msgs/s | 30.38 ms / 33M msgs/s | The per-publish seq_cst fence is the main cost; zero sleeps this round |
| notify (SetEvent per message) | 212.37 ms / 4.71M msgs/s | 264.47 ms / 3.78M msgs/s | One notification plus one wait per message; about 60x slower than spin |
| burst (5 ms rest per 10k) | 1259.80 ms, 197 sleeps | 534.00 ms, 197 sleeps | Proof the fallback channel really was walked; zero loss |

Subtract across the table's four rows and what falls out are four sources of overhead. spin's result is 3.47 nanoseconds per message — the upper bound set by memory speed. notify's overhead sits in the two kernel transitions per message: one SetEvent, one WaitForSingleObject, amortizing to 212 nanoseconds per message, about 60x slower than spin — an event's notification capability, pressed into per-message service, spends everything on those two transitions. That is why events belong only in the fallback role and not in per-message service; the table's numbers are the hard evidence. hybrid runs on atomic indexing alone at 10.77 nanoseconds per message, three times slower than spin, and burst pays the timer's granularity — details below.

Where do the threefold-extra nanoseconds go? Into the seq_cst fence after every publish — while this round's consumer kept up the whole way and never once slept; the fallback channel was never engaged. It is as if we paid the fence on every message and never once used the wakeup it protects. The author did toy with dropping the fence: skip it while the ring is non-empty — the consumer is clearly awake, why raise any flag on every message. Follow the reasoning through and it collapses: a gap still exists between the consumer consuming the last message and finishing its re-check of tail; with the fence absent, the producer's relaxed load may drift ahead of the flag store, read the stale 0, and skip the SetEvent. The re-check then happens to see the stale tail, and sleep-to-death goes through. The missed-wakeup window is not eliminated, only narrowed — narrow enough that nothing goes wrong in the steady state, and only a burst-paced run exposes it. The optimization does not hold; we record it as it is. This fence of the textbook protocol is not one you get to skip.

The burst round is precisely the verification we staged for the fallback channel. The producer's rhythm rests 5 milliseconds via Sleep after every ten thousand writes; the consumer truly fell asleep 197 times and was woken by the event 197 times, one million messages with zero losses, and SetEvent fired 15552 times in all. The fallback wakeup protocol was walked end to end.

```text
$ ./e4_spsc_ring.exe notify; echo "exit=$?"
[E4] 模式=notify 消息数=1000000 槽数=4096(16B/槽) 对象=Local\SysProgShm_E4_21560
[生产者 pid=21560 cpu2] 1000000 条,212.37 ms,吞吐 471 万条/s(212.37 ns/条),遇满自旋 244 次
[消费者 退出码=0] 1000000 条,核对错误 0 条(值+tag 逐条验),212.37 ms,吞吐 471 万条/s,等待 923162 次
[E4] notify:SetEvent 共 1000000 次;100 万条零丢失零乱序
exit=0

$ ./e4_spsc_ring.exe burst; echo "exit=$?"
[E4] 模式=burst 消息数=1000000 槽数=4096(16B/槽) 对象=Local\SysProgShm_E4_22096
[生产者 pid=22096 cpu2] 1000000 条,1259.80 ms,吞吐 79 万条/s(1259.80 ns/条),遇满自旋 246 次
[消费者 退出码=0] 1000000 条,核对错误 0 条(值+tag 逐条验),1259.80 ms,吞吐 79 万条/s,睡眠 197 次
[E4] burst:SetEvent 共 15552 次;100 万条零丢失零乱序
exit=0
(spin 与 hybrid 各三轮的完整输出在存档,中位数字见表)
```

We also ran into a Windows-specific discovery: Sleep(5) actually slept about 12.6 milliseconds. The default system timer resolution comes in 15.6-millisecond ticks; a 5-millisecond request rounds up to the next tick. We deliberately did not call timeBeginPeriod in the experiment and archived it as measured. It is the main reason the burst round took 1259 milliseconds on Windows versus 534 milliseconds for the same experiment under WSL2: one hundred rest stops, each oversleeping by about 7 milliseconds — the difference adds up exactly. As for the queue itself: on the WSL2 side we used eventfd (Linux' way of turning an event into a counted fd) with the same ring to write an isomorphic version, and its shape matches the Windows side: spin clocks 198M msgs/s and the per-message-notification tier 3.78M msgs/s. Looking at the per-message tier alone, Windows' event pair runs 212 nanoseconds per message, slightly faster than WSL2's eventfd pair at 264 nanoseconds — both fight the same battle of kernel transitions; the difference is the cost of entering the kernel.

## Two Answers from One Machine: Comparing Against shm_open

The Linux-side shared memory article is right next door; we ran the comparison's evidence ahead of time on the same machine — that is the fifth run, e5: the gear lives in the archive's 05-linux-compare, and linux_probe does the work. A few lines of output lay out the shm_open side's criteria in full: the name is a real file under /dev/shm, and stat finds it there. On a name collision, O_CREAT|O_EXCL gives EEXIST. Size comes explicitly from ftruncate. mmap's offset aligns by pages — a page plus 8 earns EINVAL. shm_unlink removes the name explicitly, regardless of how many mappings exist, and after removal the old mappings read and write as before.

```text
$ /tmp/linux_probe; echo "exit=$?"
[Linux 侧] pid=77291 页大小=4096 名字=/sysprog_e5_77291(POSIX shm 名字空间)
  shm_open(O_CREAT|O_RDWR|O_EXCL) -> fd=3 errno=0(Success)
  stat(/dev/shm/sysprog_e5_77291) -> 0 size=0 —— 名字就是 /dev/shm 里一个真文件
  shm_open(第二次,带 O_EXCL)  -> fd=-1 errno=17(File exists)
  shm_open(第二次,不带 O_EXCL)-> fd=4 errno=0 —— 同一对象另一个 fd,没有"已存在"信号
  ftruncate(256KiB) 后 stat size=262144 —— 大小是 ftruncate 显式给的(Windows 在 CreateFileMapping 参数里给)
  mmap(MAP_SHARED) -> 0x7293cc4f2000
  mmap(offset=页+8) -> 0xffffffffffffffff errno=22(Invalid argument) —— offset 必须按页对齐(Windows 要 64KB)
  shm_unlink(/sysprog_e5_77291) -> 0 名字显式摘除,与还有多少映射/多少 fd 无关
  unlink 后再 shm_open -> fd=-1 errno=2(No such file or directory) —— 名字没了
  unlink 后旧映射读写:回读 4096/4096 相符,再写 4096 字节也成功 —— 对象活到 munmap
[Linux 侧] 完
exit=0
```

With both answers assembled, we arrange them into eight rows of comparison; every row's evidence traces back to this batch's archived output:

| Dimension | Linux: shm_open + mmap | Windows: CreateFileMappingW page-file-backed | Evidence |
| --- | --- | --- | --- |
| Where the name lives | A real file under /dev/shm, visible to stat | The kernel object namespace, absent from the file system | linux_probe vs. e1 throughout |
| Create or open | Two separate calls; O_CREAT\|O_EXCL collides with EEXIST | One call; Create also opens, freshness read off err=183 | probe's second shm_open vs. e1's 2nd Create |
| Who sets the size | ftruncate, explicitly, after creation | In Create's parameters; ignored on a same-name Create again | probe's ftruncate vs. e1's RegionSize |
| When the name dies | Explicit shm_unlink, independent of mapping count | Implicit: gone when the last handle closes; no unlink API | probe's unlink vs. e1's err=2 |
| When the object dies | When the last reference lets go — fds and mappings both count | When the last reference lets go — handles and views both count | probe's post-unlink old mapping vs. e1's view living on |
| Mapping offset alignment | Page (4 KB); page plus 8 gives EINVAL | Allocation granularity (64 KB); 4096 gives err=1132; length need not align | probe's offset vs. e2's offset |
| Cross-process synchronization | pthread locks are process-internal by default; PROCESS_SHARED moves them into shared memory | Mutex/event/semaphore are kernel named objects, cross-process by nature | Linux-side article vs. e3 |
| Namespace isolation | A single /dev/shm (mount namespaces excepted) | Local\ per session; Global\ cross-session, sections need privilege | probe vs. e1's [3] segment |

Of the eight rows, two deserve a few extra words. The name's life and death is where the two sides diverge most: shm_unlink hands the decision to you — after the unlink, old mappings keep working and a new entity can be built under the same name; name and entity are decoupled. On the CreateFileMappingW side, the name's disappearance is a side effect of reference counting: without a handle in your hand, the name is not yours to hold. A few more words on the synchronization row: Linux makes locks ordinary memory. To go cross-process, we must invite the lock to live inside shared memory; when the holder dies, the lock's state dies in that same memory, and only the robust attribute buys back EOWNERDEAD. Windows makes locks kernel named objects: the lock's residence is not shared memory, the channel is the name, and when the holder drops dead, the kernel awards the lock to the waiter on its behalf. This article's WAIT_ABANDONED and that exact two hundred thousand are both products of the same road.

## A NULL Security Attribute Is Not the Absence of a DACL

One parameter remains unaccounted for: lpSecurityAttributes. We pass NULL throughout for convenience — but NULL does not mean no access control. e6 used GetSecurityInfo to examine the DACL of an object created with NULL (DACL abbreviates Discretionary Access Control List, the roster recording who is allowed to open the object): the result came out present, with three ACEs (access control entries — each line of the DACL roster), one each for NT AUTHORITY\SYSTEM, the current user, and the logon session, all with mask 0xF001F, which together grant exactly SECTION_ALL_ACCESS. Another object created with an explicit SDDL (Security Descriptor Definition Language) string had its DACL equally queryable and matching. The default door is closed — it is only that the default admit list holds the three identities SYSTEM, current user, and logon session. The cross-user denial path is beyond what a single-machine, single-user setup can test; expanding on ACLs goes on our reserve list. Here we plant the flag on the claim: NULL gets you the default DACL; an object with no DACL at all must be requested explicitly.

```text
$ ./e6_acl.exe; echo "exit=$?"
[E6] lpSecurityAttributes:NULL 的默认门禁 vs 显式 SDDL
  NULL 安全属性创建 -> h=00000000000000f8 err=0,查它的 DACL:
  [NULL] DACL:present,AceCount=3(+NT AUTHORITY\SYSTEM(mask=0xF001F),+DESKTOP-65DBAA7\CharlieChen114514(mask=0xF001F),+NT AUTHORITY\LogonSessionId_0_1154262(mask=0xF001F))
  ...(账户名与 SID 每机不同;显式 SDDL 一路的 DACL 同样查得出、对得上,三行 ACE 略)
  理论口径:DACL 没授权的用户 OpenFileMappingW → err=5(单机单用户,实测留给专篇)
exit=0
```

And with that, the two articles of the memory chapter's Windows side are complete. Looking back, we hold a working two-process testbed: spawn_self to pull up processes, named events to fire commands, shared memory to carry data, named mutexes to keep order. The story of memory on a single machine closes with this article.

We have used the word "process" for two articles now without ever discussing the process itself. That CreateProcessW line inside spawn_self has run familiar under our fingers, but what happens in the kernel after a process is created, how handles are inherited, how job objects herd a crowd of processes into one managed flock, how console events serve as the Windows side's signals — all of that sits on the next chapter's calendar. It is the process's turn to take the stage.

<ReferenceCard title="References">
  <ReferenceItem
    :id="1"
    title="CreateFileMappingW function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-createfilemappingw"
  />
  <ReferenceItem
    :id="2"
    title="OpenFileMappingW function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-openfilemappingw"
  />
  <ReferenceItem
    :id="3"
    title="MapViewOfFile function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-mapviewoffile"
  />
  <ReferenceItem
    :id="4"
    title="CreateMutexW function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createmutexw"
  />
  <ReferenceItem
    :id="5"
    title="WaitForSingleObject function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitforsingleobject"
  />
  <ReferenceItem
    :id="6"
    title="CreateEventW function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createeventw"
  />
  <ReferenceItem
    :id="7"
    title="CreateSemaphoreW function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createsemaphorew"
  />
  <ReferenceItem
    :id="8"
    title="Kernel Object Namespaces"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/TermServ/kernel-object-namespaces"
  />
  <ReferenceItem
    :id="9"
    title="shm_open(3)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man3/shm_open.3.html"
  />
</ReferenceCard>
