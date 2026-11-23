---
title: "File Locks: LockFileEx"
description: "The chapter mirroring Linux file locks on the Windows side: LockFileEx and UnlockFile lock byte ranges, and the measurements hand us a third answer beyond flock (hung on open file descriptions) and fcntl (hung on the process) — conflict detection has no owner exemption, so the same handle asking for its own second exclusive lock still gets 33 (ERROR_LOCK_VIOLATION), and the only exception is the same-handle-exclusive-plus-shared overlap the documentation spells out, which takes two unlocks. Unlock and release permissions recognize the combination of process and file object: handles inherited by a child process can neither add nor drop locks (33/158), DuplicateHandle duplicates can drop, a fresh same-process open cannot, and the moment the lock-holding process exits the system reclaims the locks on the spot. Range locks also genuinely block other handles' ReadFile/WriteFile (we call it semi-mandatory — both POSIX families are advisory). Also measured: length 0 is an empty range (fcntl's l_len=0 locks to EOF, the opposite direction), locking past EOF is not an error, UnlockFile ranges must match exactly (158), two routes of bounded waiting (FAIL_IMMEDIATELY polling, and the native FILE_FLAG_OVERLAPPED-plus-event wait the Linux side lacks), the RAII closure of unique_file_lock, and the same-machine read-pair timing of flock versus LockFileEx: Sleep(10) actually sleeping about 16 ms props up the entire absolute gap, and after normalization both sides land at a serial ratio of about 8:1."
chapter: 8
order: 5
platform: host
difficulty: intermediate
cpp_standard: [20]
reading_time_minutes: 47
prerequisites:
  - "Win32 文件 I/O:句柄、CreateFileW 与同步读写"
  - "OS 资源的 RAII 范式:fd、HANDLE 与映射的同一副骨架"
related:
  - "文件锁:flock 与 fcntl 记录锁"
  - "错误处理范式:从 errno 到 expected"
  - "文件映射:CreateFileMapping 与 MapViewOfFile"
tags:
  - host
  - cpp-modern
  - intermediate
  - 系统编程
  - Win32
  - mutex
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
translation:
  source: documents/vol8-domains/systems-programming/windows/file-io/05-lockfileex.md
  source_hash: b9487908cac2634c2ebddf12c37f2985580126316122380b0ec0b7ac50b2b893
  translated_at: '2026-11-22T15:49:05+00:00'
  engine: glm-5.3
  token_count: 28556
---
# File Locks: LockFileEx

From W01's ReadFile to W02's file mapping and on to the previous chapter's directory enumeration, the file side has been a solo act all along: reading, writing, attaching views, catching exceptions, listing directories — never once a shadow of a second contender. Real programs sooner or later face another process: the same configuration opened by two instances, and just as A is halfway through writing, B happens to come in and read, receiving half a ticket. We met dwShareMode in W01 — the sharing declaration at `CreateFileW`'s door: in the demo program, the handle named keeper hung the file up with sharing mode 0, and a second handle, even one asking only to read, was refused with 32. But what it governs is the static restriction at the moment of opening — the declaration hangs there unchanged for as long as the handle lives — and it cannot express a dynamic mutual exclusion like "while A works on this stretch, B please stay out for now." The one thing on the Windows side that fills this gap is LockFileEx.

The Linux-side answers we already measured in the mirror chapters: two of them — flock's locks hang on the open file description, fcntl's record locks hang on the process — with the full chain of evidence in [File Locks: flock and fcntl Record Locks](../../linux/file-io/05-file-lock.md). The ownership LockFileEx offers is stranger than either, and worth knowing before you lay hands on it: its conflict detection does not look at ownership, and the same handle's second lock on itself is blocked just the same. Unlock permission recognizes the combination of process and file object — a child process holding an inherited handle still cannot drop a lock the parent laid down. And its locks genuinely block other handles' ReadFile and WriteFile, while the two POSIX families are advisory locks that stop only processes that also call locks. In this chapter, on the same machine's NTFS and following the Linux chapters' script, we film the semantics matrix group by group.

The numbering and environment, stated at the top. This chapter's experiments are numbered e1 through e4, matching the four archive directories 01-matrix, 02-try-lock, 03-raii, 04-contention, with code and raw outputs collected in the repository under `code/volumn_codes/vol8/systems-programming/windows/file-io/05-lockfileex/`. In e1's large output, the section labels run E1a through E1h plus one E1w, plus three segments labeled I3 through I5, the labels following the archive's own convention: skipped letters have no corresponding section, E1w sits between E1c and E1d in the output, and the archive's I1 and I2 simply do not exist — when matching against the files, go by the numbers that are actually there. The machine is the local Win11 26200 box — the same one the Linux-side 05 archive used (Ryzen 7 9700X). The compiler is MSYS2 UCRT64 g++ 16.1.0, compiled uniformly with `-std=c++20 -Wall -Wextra` and zero warnings received, outputs captured on 2026-10-02. The data files' path is burned in as `C:/msys64/tmp/l05win/` (the NTFS system drive), and both compiling and running were launched from WSL via interop — the chain's essentials were covered in W01. The multi-process timeline follows one discipline: the timestamp zero point is passed on the command line to every child, each writes its log into its own file, and the main process merges by timestamp at the end — whoever held the lock holds the pen, so the finished order cannot lie. Not one tool is new this time: `unique_handle` is defined in [The RAII Paradigm for OS Resources](../../thinking/01-raii-paradigm.md) and `last_error_code` in [The Error-Handling Paradigm](../../thinking/02-error-paradigm.md). One thing not to misread: FALSE on conflict is a normal answer in this chapter, not an error, so the e series does not wrap check_win32 — it just prints the return value together with GetLastError.

## Getting Started: Six Parameters, Two Flags, One Required Structure

`LockFileEx(HANDLE, dwFlags, dwReserved, lenLow, lenHigh, lpOverlapped)`'s parameter list is not long. We take the weighty ones. The first is the handle: the documentation requires at least one of `GENERIC_READ` or `GENERIC_WRITE` among its permissions. `dwFlags` picks the lock type and the wait style: `LOCKFILE_EXCLUSIVE_LOCK` requests exclusive, and without it the lock is shared. `LOCKFILE_FAIL_IMMEDIATELY` decides the behavior on conflict — with it, the call returns FALSE at once, while on a synchronous handle the call sleeps and waits, in the documentation's words: `"If the file handle was not opened for asynchronous I/O and the lock is not available, this call waits until the lock is granted or an error occurs, unless the LOCKFILE_FAIL_IMMEDIATELY flag is specified"`. Failure's face is FALSE plus GetLastError. On conflict the error code is 33, its name `ERROR_LOCK_VIOLATION`, meaning another process already holds this stretch of the file locked. Set it against the Linux side: flock on conflict returns -1 plus EWOULDBLOCK (11), fcntl record locks return -1 plus EAGAIN, and the Windows shape is FALSE plus 33.

The length travels as a 64-bit value split in two, with `dwReserved` wedged in between kept at 0 per the documentation. The range's starting point is not in the parameter list — it lives in the structure the trailing `lpOverlapped` points to, and the documentation states this parameter is required. **OVERLAPPED** is Win32's little structure describing one I/O request, and this chapter is its first real appearance: the range's start goes in the `Offset` and `OffsetHigh` members, while the asynchronous completion notification goes to `hEvent`, whose moment arrives in the bounded-waiting section. Unlocking's counterpart is `UnlockFile(HANDLE, offLow, offHigh, lenLow, lenHigh)` — five parameters, all plain numbers, the range's start written out in plain sight, no structure needed. In the experiments we wrapped the calls into two small functions:

```cpp
// matrix.cpp(节选):上锁与解锁的封装,区间起点住在 OVERLAPPED 里
LockR lockx(HANDLE h, unsigned long long off, unsigned long long len, DWORD flags)
{
    OVERLAPPED ov{};              // lpOverlapped 是必填项:区间起点住在 Offset/OffsetHigh
    ov.Offset = (DWORD)off;
    ov.OffsetHigh = (DWORD)(off >> 32);
    SetLastError(0);
    BOOL ok = LockFileEx(h, flags, 0, (DWORD)len, (DWORD)(len >> 32), &ov);
    return {ok, ok ? 0 : GetLastError()};
}

LockR unlockx(HANDLE h, unsigned long long off, unsigned long long len)
{
    SetLastError(0);
    BOOL ok = UnlockFile(h, (DWORD)off, (DWORD)(off >> 32),
                         (DWORD)len, (DWORD)(len >> 32));
    return {ok, ok ? 0 : GetLastError()};
}
```

The basic behaviors we film in three opening groups, outputs taken from e1's archive, timestamps in milliseconds relative to program start:

```text
==== E1a  LOCKFILE_EXCLUSIVE_LOCK 互斥:A 拿 → B 阻塞 → A 放 → B 接棒 ====
  [     15 ms] A(pid=20400):LockFileEx([0,1048576) EX,阻塞版) = TRUE,持锁
  [    265 ms] B:LockFileEx(同区间,阻塞版)调用中 …
  [    812 ms] A:UnlockFile [0,1048576) = TRUE → B 的拿锁时刻应紧贴这一行
  [    812 ms] B(pid=8004):阻塞版返回 = TRUE,拿到锁(A 一放就接棒)

==== E1b  共享锁(不带 EXCLUSIVE):两边同时持有 ====
  [    828 ms] 共享A(pid=24772):LockFileEx([0,1048576) 不带 EXCLUSIVE) = TRUE,共享锁到手(与另一边同时持有)
  [   1093 ms] 共享B(pid=25132):LockFileEx([0,1048576) 不带 EXCLUSIVE) = TRUE,共享锁到手(与另一边同时持有)
  [   1234 ms] 探针C:两个共享锁都在场时,EXCLUSIVE|FAIL_IMMEDIATELY 试同区间 = FALSE, GetLastError()=33(ERROR_LOCK_VIOLATION)(共享之上独占进不来)

==== E1c  LOCKFILE_FAIL_IMMEDIATELY:冲突 = FALSE + GetLastError()=33 ====
  [   1703 ms] A:hA: LockFileEx [0,100) EX = TRUE
  [   1703 ms] B:hB: LockFileEx [0,100) EX|FAIL_IMMEDIATELY = FALSE, GetLastError()=33(ERROR_LOCK_VIOLATION)(同区间被占,立刻回来)
  [   1703 ms] B:hB: LockFileEx [100,200) EX|FAIL_IMMEDIATELY = TRUE(不相交就放行)
```

E1a's timing deserves an extra glance: A's unlocking UnlockFile and B's blocking version returning TRUE land in the same 812 ms — no visible seam between the release and the baton pass, the same observation as the Linux side's E1a, and the handoff's price gets settled in the timing section. E1b speaks of sharing: two processes' shared locks can be on stage together, and an exclusive request trying to squeeze in eats 33 — isomorphic with read-write lock rules. E1c pins down the non-blocking convention: a call carrying `LOCKFILE_FAIL_IMMEDIATELY` returns FALSE plus 33 immediately on conflict, and without it the call sleeps in — this is the posture our try-lock probes use uniformly later on.

Range conflicts are judged by byte overlap with zero ambiguity. Straight to E1d's output:

```text
==== E1d  字节区间:A 锁 [0,100),B 试 [50,150) 冲突、[100,200) 成功 ====
  [   1703 ms] A:hA: LockFileEx [0,100) EX = TRUE
  [   1703 ms] B:hB: LockFileEx [50,150) EX|FAIL = FALSE, GetLastError()=33(ERROR_LOCK_VIOLATION)(与 [0,100) 交叠 50 字节)
  [   1703 ms] B:hB: LockFileEx [100,200) EX|FAIL = TRUE(相邻不相交 → 拿到)
  [   1703 ms] B:hB: LockFileEx [99,101) EX|FAIL = FALSE, GetLastError()=33(ERROR_LOCK_VIOLATION)(单字节交叠也算冲突)
```

`[50,150)` and `[0,100)` overlap by 50 bytes — blocked. `[100,200)` and `[0,100)` are adjacent but disjoint — allowed through. The fiercest is `[99,101)`: it borrows just one byte of the other's territory, and 33 is what it gets all the same. Different stretches of one file can be handed to different processes, each minding its own — a sharded usage we see in both LockFileEx and fcntl record locks, while whole-file flock cannot offer it.

## The Same Handle Blocks Itself Too

Now the chapter's first heavyweight. Same process, same handle — what happens when we lock again over a lock we already hold? flock's answer is conversion: a repeated call swaps the old lock for the new. fcntl's answer is rewriting: the later lock takes over the overlapping stretch. LockFileEx's answer — look at the output:

```text
==== E1e  同句柄区间语义:重复加锁、重叠、部分解锁 ====
  [   1703 ms] 本进程:h1: LockFileEx [0,100) EX = TRUE
  [   1703 ms] 本进程:h1: 同句柄再锁 [50,150) EX(带 FAIL_IMMEDIATELY 防挂死)= FALSE, GetLastError()=33(ERROR_LOCK_VIOLATION)
  [   1703 ms] 本进程:   → 对照:fcntl 同进程后锁直接改写重叠段、flock 同 fd 是转换,LockFileEx 连自己名下都挡
  [   1703 ms] 本进程:h1: UnlockFile [0,100) = TRUE(唯一的锁就是它,整段放干净)
  [   1703 ms] 探针:h2 试 [0,50) = TRUE;[50,150) = TRUE(确认场上已无锁)
  [   1718 ms] 本进程:h1: LockFileEx [0,100) EX = TRUE
  [   1718 ms] 本进程:h1: 原地再锁 [0,100) EX|FAIL = FALSE, GetLastError()=33(ERROR_LOCK_VIOLATION)(第二把没立起来)
  [   1718 ms] 本进程:h1: UnlockFile [0,100) 第一次 = TRUE
  [   1718 ms] 探针:h2 试 [0,100) = TRUE(放一次就干净 —— 没有计数、没有第二把)
  [   1718 ms] 本进程:h1: UnlockFile [0,100) 第二次 = FALSE, GetLastError()=158(ERROR_NOT_LOCKED)
  [   1718 ms] 本进程:h1: UnlockFile [10,20)(部分解锁)= FALSE, GetLastError()=158(ERROR_NOT_LOCKED)
  [   1718 ms] 本进程:h1: UnlockFile [0,100)(整段)= TRUE
  [   1718 ms] 本进程:h1: LockFileEx [0,100) EX = TRUE
  [   1718 ms] 本进程:h1: 同句柄再锁 [0,100) 共享(不带 EXCLUSIVE,带 FAIL)= TRUE(文档特例:同句柄可叠)
  [   1718 ms] 本进程:h1: UnlockFile [0,100) 第一次 = TRUE
  [   1718 ms] 探针:EX 试 [0,100) = FALSE, GetLastError()=33(ERROR_LOCK_VIOLATION);共享试 [0,100) = TRUE(看剩下的是哪一把)
  [   1718 ms] 本进程:h1: UnlockFile [0,100) 第二次 = TRUE
  [   1718 ms] 探针:EX 试 [0,100) = TRUE(两把都该放干净了,EX 探针才作数)
```

The same handle's second exclusive lock on itself also returns 33. This is no implementation accident. The documentation's own words write it clean: `"Exclusive locks cannot overlap an existing locked region of a file"` — an exclusive lock cannot press down on an already-locked region, and the sentence carries no owner exemption. Any existing lock counts, including the one we ourselves just laid. Three mechanisms, three temperaments on one small experiment: flock converts on a repeated call through the same description, fcntl's later same-process lock rewrites the overlap, and LockFileEx grants no one's face — conflict detection looks only at whether ranges overlap. The engineering consequence is real: locking twice on one handle in the same stretch of code survives both Linux families but becomes FALSE on Windows, and friends porting code stumble here most easily.

The one excepted road the documentation also spells out: `"A shared lock can overlap an exclusive lock if both locks were created using the same file handle"` — under the same handle's name, a shared lock may in fact stack on an exclusive one. The output's final group is its measurement: stacking a shared on top of EX returns TRUE. The documentation even prescribes the unlock order: `"If the same range is locked with an exclusive and a shared lock, two unlock operations are necessary to unlock the region; the first unlock operation unlocks the exclusive lock, the second unlock operation unlocks the shared lock"`. The first UnlockFile releases the exclusive — we verified with probes: what remains blocked at that moment is the EX probe while the shared probe passes freely, proving the shared lock is the one left on stage, and only the second call truly clears it.

The unlock rules carry one more hard requirement: the range must match exactly. Partially unlocking `[10,20)` returns FALSE plus 158 (`ERROR_NOT_LOCKED`), while releasing the whole stretch in one go returns TRUE. The documentation's words: `"The region to unlock must correspond exactly to an existing locked region"` — and it holds in reverse too: two adjacent locks cannot be merged into one unlock. Compare fcntl's `F_UNLCK`, free to split and merge ranges at will. Windows offers no such freedom: the range recorded at lock time must be replayed to the byte at unlock time.

## Who Owns the Lock: The Process-plus-File-Object Combination

Once ownership is settled, everything about close, inheritance, and exit follows. flock recognizes the open file description, fcntl recognizes the process — what does LockFileEx recognize? The documentation gives no one-sentence direct answer, so we measured it out through the six output segments E1f, E1g, E1h, I3, I4, and I5.

In the first group, one process opens two handles. h1 locks `[0,100)`, and h2 from the same process's second `CreateFileA` tries the same range:

```text
==== E1f  两个句柄:同进程 CreateFileA 两次,互相冲突吗 ====
  [   1718 ms] 本进程:h1: LockFileEx [0,100) EX = TRUE
  [   1718 ms] 本进程:h2(同进程第二次 open):LockFileEx [0,100) EX|FAIL = FALSE, GetLastError()=33(ERROR_LOCK_VIOLATION)
  [   1718 ms] 本进程:   → 同进程两个句柄互相挡:冲突判定没有属主豁免,进程身份救不了
  [   3218 ms] 本进程:1.5 s 过去,对 h2 的阻塞版 LockFileEx 仍未返回(线程 g_woken=0)
  [   3218 ms] 本进程:   → 放锁的人永远不来:同进程两句柄,自己挡死了自己
  [   3531 ms] 本进程:CloseHandle(h1) 之后:g_woken=1 —— 阻塞线程应声拿到并已放掉:它等的正是「另一个句柄」

==== E1g  CloseHandle:该句柄名下所有区间一起放 ====
  [   3531 ms] A:hA: [0,100) = TRUE,[200,300) = TRUE,两把都上身
  [   3531 ms] B:hB 试 [0,100) = FALSE, GetLastError()=33(ERROR_LOCK_VIOLATION);[200,300) = FALSE, GetLastError()=33(ERROR_LOCK_VIOLATION)
  [   3531 ms] B:hA CloseHandle(一次 UnlockFile 都没调)后:试 [0,100) = TRUE;[200,300) = TRUE
```

E1f's three output segments read best strung together. Process identity saves neither handle: h2's try-lock still gets 33. The blocking version's fate is even more absolute — we sent a thread to call LockFileEx without FAIL_IMMEDIATELY on h2, waited 1.5 seconds, and the thread never stirred: the one who could drop h1's lock is us, but our thread is wedged in h2's wait, and the release will never come. This is the same situation flock's E1b showed us: we have blocked ourselves. The 3531 ms line delivers the answer — the moment CloseHandle(h1) landed, the blocked thread woke with a start. What it waited for was never another process, only a lock under another handle's name. E1g adds CloseHandle's semantics: with two range locks on its books, without a single UnlockFile call, the instant the handle closes every range under its name is released together — not even the cleanup is our worry.

The second group watches handle inheritance: `CreateProcess` with `bInheritHandles=TRUE` — the child's inherited handle points at the same file object as the parent's. What can it do?

```text
==== E1h  句柄继承(bInheritHandles):继承来的句柄能干什么 ====
  [   3531 ms] 父:可继承句柄 hL(SA.bInheritHandle=TRUE):LockFileEx [0,100) EX = TRUE
  [   3531 ms] 子(pid=28016):① 自己 CreateFileA 的新句柄:LockFileEx [0,100) EX|FAIL = FALSE, GetLastError()=33(ERROR_LOCK_VIOLATION) —— 「我是它孩子」不算数
  [   3531 ms] 子:② 继承来的句柄值(与父指向同一文件对象):LockFileEx [0,100) EX|FAIL = FALSE, GetLastError()=33(ERROR_LOCK_VIOLATION)
  [   3531 ms] 子:③ UnlockFile(继承句柄,[0,100)) = FALSE, GetLastError()=158(ERROR_NOT_LOCKED)
  [   3547 ms] 父:子进程退场后,父探针试 [0,100) = FALSE, GetLastError()=33(ERROR_LOCK_VIOLATION)
  [   3547 ms] 父:   → ②③ 连起来读:继承句柄既加不进去(冲突判定不豁免任何人)、也放不掉父的锁(解锁认进程)

---- I3 对照:bInheritHandles=FALSE,句柄值照样传过去 ----
  [   3547 ms] 父:普通句柄(不可继承):LockFileEx [0,100) EX = TRUE
  [   3547 ms] 子(pid=17940):LockFileEx(父传来的句柄值) = FALSE, GetLastError()=6(ERROR_INVALID_HANDLE);GetFileType=0(错误 6) —— bInheritHandles=FALSE 时,句柄值没有跨进程的意义
  [   3547 ms] 父:对照组的锁毫发无损:父探针 [0,100) = FALSE, GetLastError()=33(ERROR_LOCK_VIOLATION)(应仍被挡)
```

The child's triple test blocked every road: its own fresh handle trying the lock gets 33 — kinship carries no weight here. The inherited handle trying the lock still gets 33, even though it points at the same file object as the parent handle. Taking the inherited handle to UnlockFile the parent's lock returns 158 (`ERROR_NOT_LOCKED`) — in the system's eyes, that lock is simply not on the child's books. The documentation has a matching sentence: `"If the file handle is inherited by a process created by the locking process, the child process is not granted access to the locked region. If the locking process opens the file a second time, it cannot access the specified region through this second handle until it unlocks the region"`. Set it against flock: a flock child process shares its parent's lock through the inherited fd, and one LOCK_UN call can release it for the parent — none of which works over here. I3 adds a contrast in passing: with `bInheritHandles=FALSE`, the handle value passed over is just a number, and the child's first use of it collects error 6 — meaningless across processes.

The third group asks about a lock's lifespan: the lock-holding process exits — where does the lock go? I4's arrangement: the proxy process locks the range at offset 1000, length 200, on an inheritable handle, the spawned sleeper inherits that handle and sleeps 1200 ms, and the proxy promptly exits without a single UnlockFile ever landing:

```text
---- I4 锁的寿命:持锁进程退出,继承句柄还在别人手里 ----
  [   3562 ms] proxy(pid=27840):可继承句柄上锁 [1000,200) EX = TRUE,接着 spawn sleeper、自己立即退出(全程不 UnlockFile)
  [   3562 ms] sleeper(pid=20468):攥着继承句柄睡 1200 ms,什么都不做
  [   3562 ms] 探针:proxy 进程已退出(全程没 UnlockFile,但 sleeper 攥着同一文件对象的句柄):试 [1000,200) = TRUE
  [   5265 ms] 探针:sleeper 也退场后,再试 [1000,200) = TRUE
```

The lock's master leaves the stage, and the system collects the lock on the spot. The sleeper, clutching the inherited handle to the same file object, is of no help whatsoever — the probe acquired it in the same millisecond. The documentation's words: `"If a process terminates with a portion of a file locked or closes a file that has outstanding locks, the locks are unlocked by the operating system"`, with a reservation appended: `"However, the time it takes for the operating system to unlock these locks depends upon available system resources"` — cleanup time follows available resources, and the documentation further advises unlocking explicitly before exit. On this machine's measurement the cleanup completed within the millisecond, but that is local NTFS — we do not vouch for network file systems.

The fourth group tests the last link of the ownership verdict: one process equips itself with two handles — one from DuplicateHandle, one from a fresh open. Who can drop the lock h1 laid down?

```text
---- I5 归属单位判别:DuplicateHandle、新 open、关句柄次序 ----
  [   5265 ms] 本进程:h1(新 open): LockFileEx [0,100) EX = TRUE
  [   5265 ms] 本进程:DuplicateHandle 复制出 h2(与 h1 指向同一文件对象)
  [   5265 ms] 本进程:h2: LockFileEx [0,100) EX|FAIL = FALSE, GetLastError()=33(ERROR_LOCK_VIOLATION)(同进程同对象,照样加不进)
  [   5265 ms] 本进程:h2: UnlockFile [0,100)(放 h1 立的那把)= TRUE —— 同进程的复制品放得掉
  [   5265 ms] 本进程:h1: 重新 LockFileEx [0,100) EX = TRUE
  [   5265 ms] 本进程:h3(全新 open,同进程): UnlockFile [0,100) = FALSE, GetLastError()=158(ERROR_NOT_LOCKED)
  [   5265 ms] 本进程:   → 解锁认进程+文件对象,新 open 的句柄放不掉
  [   5265 ms] 本进程:CloseHandle(h1) 后(h2 还开着同一对象),新句柄试 [0,100) = FALSE, GetLastError()=33(ERROR_LOCK_VIOLATION)
  [   5265 ms] 本进程:h2/h3 也全关后,再试 = TRUE(清场确认)
```

Lay the six output segments together and the ownership rule reads right out. Unlock permission recognizes the **combination of process and file object**: h2 is a same-process handle obtained via DuplicateHandle pointing at the same file object, so it can drop the lock. h3 is a same-process fresh open pointing at a different file object, so it cannot — 158 comes back. The child's inherited handle has the same object but a different process, and both adding and dropping fall short. CloseHandle's semantics follow the same model: only when that process's handles to that file object are all cleared do the locks release. While h1 is closed but the duplicate h2 stays open, the lock sits firm, and only when everything closes does the field clear. Conflict detection does not even look at ownership — its eyes hold range overlap alone. Take this model back to E1f's self-blocking deadlock and everything lines up: what wedges the blocking call on h2 is the lock under h1's name, and in the system's registry h1 and h2 are two different entries.

## The Lock Genuinely Blocks Other Handles' Reads and Writes

When the Linux chapters covered flock and fcntl, we noted the nature of advisory locks: a process that never calls locks and just writes goes straight through — the kernel does not stop it. LockFileEx is not an advisory lock. We test with ReadFile directly:

```text
==== E1w  锁与读写:别的句柄 read/write 真会被挡吗(咨询锁吗) ====
  [   1703 ms] A:hA: LockFileEx [0,100) EX = TRUE
  [   1703 ms] B:hB: ReadFile@[50,10) = FALSE, GetLastError()=33(ERROR_LOCK_VIOLATION) —— 锁内,被挡
  [   1703 ms] B:hB: ReadFile@[250,10) = TRUE(锁外,畅通)
  [   1703 ms] A:hA(持锁者): ReadFile@[50,10) = TRUE(自己不受影响)
  [   1703 ms] B:hB: 放锁后再读 @[50,10) = TRUE(恢复)
  [   1703 ms] A:hA: LockFileEx [0,100) 共享 = TRUE
  [   1703 ms] B:hB: 共享锁下 ReadFile@[50,10) = TRUE(读放行)
  [   1703 ms] B:hB: 共享锁下 WriteFile@[50,10) = FALSE, GetLastError()=33(ERROR_LOCK_VIOLATION) —— 写被挡
```

B never called any lock at all. It merely read the file with its own handle, the read position fell inside the locked range, and ReadFile returned FALSE plus 33 outright. The same handle moved outside the lock to `[250,10)` passes freely. Lock-holder A reading inside its own lock is unaffected. With a shared lock, other handles' reads pass and writes are blocked. The documentation writes this semantics in two sentences, which we keep as-is: the exclusive one, `"Locking a portion of a file for exclusive access denies all other processes both read and write access to the specified region of the file"`, and the shared one, `"Locking a portion of a file for shared access denies all processes write access to the specified region of the file, including the process that first locks the region"` — even the first locker itself may not write under a shared lock. Our archive has no separate test for that half-sentence, so we pass over it per the documentation's account.

We call this property **semi-mandatory**. We say mandatory because uncooperative I/O really does get bounced back — POSIX advisory locks cannot promise this layer, and only the kernel's long-deprecated mandatory locks have a similar temperament. We say half because what gets blocked is only the ReadFile-and-WriteFile path, and the documentation has another sentence: `"Locking a region of a file does not prevent reading or writing from a mapped file view"`. Per those words, reads and writes through a file mapping view are unrestricted — we did not measure the mapping-view end, and the archive honestly says untested. The engineering reading is ready-made: do not count on the lock to stop every reader, but you can use it to protect quota files, log rotation — scenes where every cooperating writer walks through LockFileEx. The blocks-I/O layer is a benefit that does not exist on Linux.


## A Length-0 Range Is Empty, and Past EOF Is Not an Error

Two boundaries deserve their own lifting out — porting fcntl's habits straight over will end in a wreck. The first: what happens when the length is 0:

```text
  [   1703 ms] A:hA: LockFileEx [200,长度0) EX = TRUE(零长度是「空区间」还是「到 EOF」?往下看)
  [   1703 ms] B:探针试 [900,50)(文件 1000 字节,在 200 之后)= TRUE
  [   1703 ms] B:探针试 [250,10)(紧挨 200)= TRUE
  [   1703 ms] 本进程:   → 长度 0 = 空区间,什么都没锁(对照:fcntl 的 l_len=0 是锁到 EOF,Windows 这里不是)
  [   1703 ms] A:hA: LockFileEx [200,1048576) EX = TRUE(把长度给足,锁过 EOF 也没问题)
  [   1703 ms] B:探针再试 [900,50) = FALSE, GetLastError()=33(ERROR_LOCK_VIOLATION)(这回真挡了)
```

On the fcntl side, `l_len=0` means lock to EOF — we used it in the Linux chapters' matrix table. LockFileEx's length 0 measured as an empty range: not one byte locked, and the probe passes freely even right next to position 200. The documentation says not one word about this value, so measurement is the only answer there is. To lock a range all the way to the file's end, give an explicitly generous length, or the freshly computed one from `GetFileSizeEx` — writing 0 per fcntl's habit gets you nothing locked. The second boundary does agree with fcntl: locking past EOF is not an error, per the documentation's own words: `"Locking a region that goes beyond the current end-of-file position is not an error"`. We gave the length as 1048576 with the file itself only 1000 bytes — the lock held, and writes that later grow past EOF stay inside the circle of protection.

## Bounded Waiting: the Polling Road and the OVERLAPPED Road

LockFileEx has no timeout parameter. The blocking version waits to the end — the same ailment flock and `F_SETLKW` suffer. The Linux chapters' remedy was LOCK_NB plus 1-millisecond mini-polling. On Windows we have two roads.

The first is our usual polling — `LOCKFILE_FAIL_IMMEDIATELY` plus Sleep(10) mini-steps. e2's two outputs:

```text
==== 场景 1:持锁者握 600 ms,waiter 限期 3000 ms ====
  [     15 ms] holder(pid=4360):LockFileEx([0,4096) EX) = TRUE,持锁 600 ms
  [    625 ms] holder:UnlockFile = TRUE,离场
  [    625 ms] waiter:try_lock_for(3000ms) = true:第 32 次尝试拿到(实际等了 500 ms,其余 31 次都是 FALSE+33)

==== 场景 2:持锁者握 1200 ms,waiter 限期 200 ms → 如期超时 ====
  [   1031 ms] holder(pid=10964):LockFileEx([0,4096) EX) = TRUE,持锁 1200 ms
  [   1343 ms] waiter:try_lock_for(200ms) = false:14 次尝试全部 ERROR_LOCK_VIOLATION(33),限期一到就返回,没有死等
  [   2234 ms] holder:UnlockFile = TRUE,离场
```

In scenario 1, waiter's 32nd attempt landed the lock at exactly 500 ms, the previous 31 all FALSE plus 33. Scenario 2 timed out on schedule — 14 attempts exhausted the 200 ms deadline and returned on the mark. Polling's resolution is the Sleep interval, a price we cannot dodge.

The second road is native Windows, something the Linux side does not have, and it gets its own telling. We open the handle for asynchronous I/O with `FILE_FLAG_OVERLAPPED`, then call LockFileEx without FAIL_IMMEDIATELY on it: a conflicting request no longer blocks but returns FALSE plus 997 (`ERROR_IO_PENDING`, overlapped I/O's standard not-yet-complete receipt), the request goes into the system's queue, and at the moment of grant the event in `OVERLAPPED.hEvent` is signaled. The documentation writes the whole chain out: `"The LockFileEx function operates asynchronously if the file handle was opened for asynchronous I/O, unless the LOCKFILE_FAIL_IMMEDIATELY flag is specified. If an exclusive lock is requested for a range of a file that already has a shared or exclusive lock, the function returns the error ERROR_IO_PENDING. The system will signal the event specified in the OVERLAPPED structure after the lock is granted"`. So `WaitForSingleObject(event, timeout)` is naturally a try_lock with a deadline — not one poll required:

```cpp
// try_lock_overlapped.cpp(节选):异步句柄上,冲突改排队,事件置位即锁到手
HANDLE h = open_async(file);                 // CreateFileA 带 FILE_FLAG_OVERLAPPED
OVERLAPPED ov{};
ov.Offset = 0;
HANDLE ev = CreateEventA(nullptr, TRUE, FALSE, nullptr);   // 手动重置事件
ov.hEvent = ev;
BOOL ok = LockFileEx(h, EX, 0, (DWORD)kRange, 0, &ov);     // FALSE + 997:请求在队列里
DWORD w = WaitForSingleObject(ev, 3000);                   // 限时就交给它
if (w == WAIT_OBJECT_0) {
    DWORD nx = 0;
    BOOL g = GetOverlappedResult(h, &ov, &nx, TRUE);       // 锁到手
}
```

The two measurements' outputs are likewise in e2's archive, pasted as they are:

```text
==== 场景 1:持锁者握 600 ms,waiter WaitForSingleObject(事件, 3000 ms) ====
  [      0 ms] holder(pid=32904):LockFileEx([0,4096) EX) = TRUE,持锁 600 ms
  [    109 ms] waiter:LockFileEx(异步句柄,不带 FAIL_IMMEDIATELY)= FALSE, GetLastError()=997(ERROR_IO_PENDING) —— 冲突不再阻塞,改挂账排队
  [    609 ms] holder:UnlockFile = TRUE,离场
  [    609 ms] waiter:事件等了 500 ms 置位,GetOverlappedResult = TRUE —— 锁到手,一次也没轮询
  [    609 ms] waiter:UnlockFileEx = TRUE(显式放锁后关句柄)

==== 场景 2:持锁者握 1200 ms,waiter 限期 200 ms → 超时撤单 ====
  [    953 ms] holder(pid=26828):LockFileEx([0,4096) EX) = TRUE,持锁 1200 ms
  [   1062 ms] waiter:LockFileEx = FALSE, GetLastError()=997(ERROR_IO_PENDING),请求在队列里挂着
  [   1281 ms] waiter:WaitForSingleObject = WAIT_TIMEOUT(0x102),等了 219 ms,限期到
  [   1281 ms] waiter:CancelIoEx(定向撤这一笔) = TRUE
  [   1281 ms] waiter:收尾 GetOverlappedResult = FALSE, GetLastError()=995(ERROR_OPERATION_ABORTED) —— 请求作废,锁没到手
  [   2578 ms] 探针:holder 退场后,新句柄试同区间 = TRUE —— 被撤销的请求没有留下锁
```

The timeout side still wants one closing step: we used `CancelIoEx` to cancel specifically the queued request, and the closing GetOverlappedResult returned FALSE with error 995 (`ERROR_OPERATION_ABORTED`). What truly matters is the probe's last line: after the holder left the stage, a fresh handle acquired the same range immediately, so the canceled request left behind no lock of unknown provenance — the queue came away clean. The async wait's benefits count two: first, the CPU does not poll even once. Second, the wait's resolution lands on the kernel's scheduling granularity, no longer bound to the Sleep setting. Unlocking needs a matching companion: the async handle pairs with `UnlockFileEx`, the range split into parameters and the OVERLAPPED as before. The Linux side has no counterpart — flock's and `F_SETLKW`'s bounded waiting can only poll, and here Windows holds the upper hand.

## unique_file_lock: Binding the Lock's Life to an Object

We have seen enough of the mechanism — time to fold it into a type. On the Linux side we wrote file_lock, binding a flock lock's life to an fd's life. Here on Windows one structural difference must be stated before the hands-on work: there, lock and fd are one body. Here, lock and handle are two things — UnlockFile releases the lock without closing the handle, while CloseHandle releases lock and handle together. So the destructor takes two steps: an explicit UnlockFile leads, and CloseHandle provides the backstop:

```cpp
// unique_file_lock.hpp(节选):构造加锁,析构两步,move-only
class unique_file_lock
{
public:
    explicit unique_file_lock(const char* path, bool exclusive = true,
                              std::uint64_t off = 0, std::uint64_t len = 4096)
        : h_{open_or_die(path)}, off_{off}, len_{len}
    {
        lock_blocking(exclusive);
    }

    ~unique_file_lock()
    {
        if (h_ != INVALID_HANDLE_VALUE) {
            if (locked_) {
                unlock();          // 显式放锁:排查时时间线上有这句可对
            }
            CloseHandle(h_);       // 兜底:句柄一关,名下残余的锁也会被内核放掉
        }
    }

    bool try_lock(bool exclusive = true) noexcept;   // FAIL_IMMEDIATELY 试锁,33 算正常答案

    template <class Rep, class Period>
    bool try_lock_for(std::chrono::duration<Rep, Period> d, bool exclusive = true)
    {
        constexpr DWORD kPollMs = 10;   // 等待分辨率 = 轮询间隔
        const auto deadline = std::chrono::steady_clock::now() + d;
        for (;;) {
            if (try_lock(exclusive)) { return true; }
            if (std::chrono::steady_clock::now() >= deadline) { return false; }
            ::Sleep(kPollMs);
        }
    }

    unique_file_lock(unique_file_lock&& other) noexcept;   // 句柄过户,moved-from 成空壳
    // 其余成员与完整定义见存档 03-raii/unique_file_lock.hpp
};
```

The explicit unlock in the destructor is actually redundant — CloseHandle would release the lock anyway. We keep it so that the release sits at a visible position on the timeline, giving us a line to check against when troubleshooting — the same trade-off as the Linux side's file_lock. `try_lock_for` here walks the polling road, resolution pinned at the 10 ms setting. The two-process timeline runs for you, output from e3's archive: A holds the lock and writes data right after construction, and B tries each of the three postures:

```text
==== 双进程时序:A 拿独占锁写数据,B 有限等待接棒 ====
  [      0 ms] A(pid=3860):构造 unique_file_lock,已写 "ticket=42",持锁 700 ms
  [      0 ms] B(pid=24272):try_lock() = false(锁在 A 手里)
  [    203 ms] B:try_lock_for(200ms) = false(实际等了 200 ms,超时)
  [    703 ms] A:作用域将尽,unique_file_lock 析构在即
  [   703 ms] A:已放锁(显式 UnlockFile)+ CloseHandle,退场
  [   719 ms] B:try_lock_for(3s) = true(等了 522 ms —— A 一放锁,下一轮询就拿到)
  [   719 ms] B:读到 "ticket=42"(临界区数据完好)

==== move 语义:锁跟着新主人走,moved-from 析构不放锁 ====
  [    734 ms] 本进程:lk1 构造(默认独占 [0,4096)):owns_lock()=1
  [    734 ms] 探针:lk1 持锁中:新开句柄试同区间 = FALSE(锁被占)
  [    734 ms] 本进程:move 后:lk1.alive()=0(moved-from 空壳);lk2.owns_lock()=1
  [    734 ms] 探针:lk1 已析构(moved-from,交出了句柄,析构碰不到锁):仍 = FALSE(锁被占)
  [    734 ms] 探针:lk2 reset 后:= TRUE(拿到)
```

The timeline is the same shape as the Linux side: B asks immediately, the answer is false. The 200 ms deadline times out on schedule, and the 3 s deadline acquires only on the polling round after A's release — the gap reads 16 ms, exactly the price of the Sleep(10) setting — and B also read the ticket=42 that A wrote into the critical section. On the move side, when the moved-from lk1 destructs the probe stays blocked: it has already surrendered the handle, and the destructor cannot touch the new owner's lock. The skeleton is isomorphic to the `unique_lock` mental model, so we leave it there.

## The Same Machine, Both Sides Once Each

The price comparison replicates the Linux chapters' E6 script: e4 raised 8 child processes (CreateProcess over here on Windows), each child opening its own handle, each round a three-step take-lock, sleep-10-ms-as-critical-section, release (100 rounds per run). The control group sleeps identically without locking, and another group runs 2000 rounds of pure handoff with an empty critical section. Three runs each, medians taken:

```text
模式        r1            r2            r3
locked     12765.0 ms   12870.7 ms   12947.3 ms  | 中位  12870.7 ms,16088.3 µs/人次
free        1506.8 ms    1596.0 ms    1601.2 ms  | 中位   1596.0 ms,1995.0 µs/人次
        (free 折算:每轮 Sleep(10) 实际 ≈ 15.96 ms —— 并行睡眠的单份成本)

空临界区(纯锁传递开销):8 进程 × 2000 轮 lock/unlock
micro         87.5 ms     110.0 ms     112.6 ms  | 中位    110.0 ms

单句柄无争抢:1000000 对 LockFileEx/UnlockFile → 1.120 µs/一对
Sleep(10) 真实粒度:100 次 → 15.97 ms/次(计时口径的注脚)
```

We set the Linux-side numbers side by side (same machine still: flock ran in WSL2's ext4, LockFileEx on the host's NTFS — same machine, different layers):

| Metric | Linux flock | Windows LockFileEx |
| --- | --- | --- |
| locked median | 8090.5 ms | 12870.7 ms |
| free median | 1009.9 ms | 1596.0 ms |
| locked-to-free ratio | about 8.0 to 1 | about 8.1 to 1 |
| 16000 empty-critical-section handoffs | 255.7 ms, about 16 µs each | 110.0 ms, about 6.9 µs each |
| 1 million uncontended pairs, single handle | 0.675 µs per pair | 1.120 µs per pair |

The first time I set the two columns together, I paused too: Windows' locked runs 60 percent beyond Linux's — is the lock really that expensive? Spread the arithmetic out and the difference lands almost entirely on sleep. Windows' `Sleep(10)` under the default timer granularity actually sleeps about 15.96 ms — the measured 100-sleep footnote on the output's last line is the annotation. So the theoretical serialized total is 800 hops times 15.96 equals 12768 ms, the measured median 12870.7 — only 102.7 ms beyond, amortized to about 128 microseconds per handoff: that is the entire remaining handoff overhead once the sleep itself is subtracted. The free row's result is likewise all sleep: 100 hops times 15.96 gives exactly 1596 ms, matching to the digit. Subtract each side's sleep cost and the structural reading surfaces: mutual exclusion lines the parallel sleeps into one queue, the locked-to-free ratio lands at 8 to 1 on both sides — that is the mechanism's own shape, while the absolute values are the timer granularity's shape. The lesson reaches beyond locks: when timing cross-platform comparisons, the first order of business is measuring each platform's time granularity, or what you measure is really the granularity gap — the mechanism gap hides in the normalized ratio.

For the remaining two rows, we look at the mechanism's true price. The empty-critical-section handoff costs Windows about 6.9 microseconds per pass — more than half cheaper than flock's 16 — so in an eight-process scramble for one empty lock, Windows' throughput actually wins. Yet the uncontended single-handle million pairs cost Windows 1.120 microseconds a pair, dearer than Linux's 0.675. The two readings point in opposite directions. We record each as measured — do not slap a label on an entire mechanism from one row of numbers.

## The Three-Way Comparison: flock, fcntl Record Locks, LockFileEx

We have measured every single-point semantic. Now we set all three into one alignment table. The Windows column comes from e1 and e2's archives (the bounded-waiting row's evidence lives in e2), and the two Linux columns from the Linux chapters' archives. The OFD lock in the table refers to the `F_OFD_SETLK` since Linux 3.15, which moved record locks from the process onto the open file description. The F_SETLK family in the header and abbreviations like F_SETLKW used in the main text can all be looked up in full in the Linux mirror chapter:

| Dimension | flock(2) | fcntl(2) record locks (F_SETLK family) | LockFileEx |
| --- | --- | --- | --- |
| scope | the whole file | arbitrary byte ranges, l_len=0 to EOF | arbitrary byte ranges, length 0 measured as an empty range locking nothing, past EOF not an error |
| conflict-detection unit | open file description | the process — same process never conflicts with itself | no owner exemption, any existing overlapping lock blocks, including the same handle's own previous one |
| non-blocking return on conflict | -1 plus EWOULDBLOCK (11) | -1 plus EAGAIN (same 11) | FALSE plus GetLastError()=33 (ERROR_LOCK_VIOLATION) |
| repeated lock, same handle | conversion, EX and SH swap, non-atomic | replace-and-merge, later lock rewrites the overlap | EX over EX eats 33, the sole exception being SH over EX on the same handle (documented), unlock takes two passes, exclusive first |
| second open, same process | self-conflict, blocking version deadlocks itself | merged under one name, uneventful | self-conflict, blocking version deadlocks itself, E1f holds 1.5 s of forensics |
| unlock | LOCK_UN, releases all | F_UNLCK splits and merges freely | range must match exactly, partial and repeated unlocks both give 158 |
| who may unlock | anyone holding that description, forked children included | the owning process, through any fd | process plus file object combined: DuplicateHandle duplicates can, an inherited child handle cannot, a same-process fresh open cannot either |
| semantics of close | released only when the description's last reference closes | any single fd close releases all — the OFD version lacks this trap | released when that process's handles to that file object reach zero, original closed but duplicate open keeps the lock alive |
| process exit | follows the last reference | owner gone, all released | system reclaims on the spot even if other processes clutch inherited handles, documentation warns of cleanup lag on networked files |
| child processes and inheritance | fork shares the description, child can release the parent's lock | not inherited, child blocked by parent's lock, F_GETLK reports parent pid | inherited handle can neither add nor drop, 33 and 158, with bInheritHandles=FALSE the handle value is just error 6 |
| effect on read and write | advisory, does not block | advisory, does not block | semi-mandatory, other handles' reads/writes inside the range get 33 directly, the lock holder unaffected, shared locks pass reads and block writes, mapping views unrestricted (documented, untested) |
| probing the opponent | none, try it and see | F_GETLK reports pid and the opponent's range, OFD reports l_pid=-1 | no public API, only the FAIL_IMMEDIATELY probe |
| bounded waiting | none native, poll | none native, poll | poll, or FILE_FLAG_OVERLAPPED plus event plus CancelIoEx, no polling |
| lock-list observation | the FLOCK lines of /proc/locks | the POSIX and OFDLCK lines of /proc/locks | no public interface |
| network FS | NFS client emulates with range locks | NFS risks lock loss, leases | SMB 3.0 documented as supported, untested |

The heaviest part of the table is still the ownership block: three mechanisms, three answers to one question, and none of them converts into another. flock's child can release the parent's lock — Windows cannot. fcntl releases through any fd — Windows recognizes the file object. LockFileEx blocks reads and writes that never called locks — neither of the others can. When you design across platforms, these differences admit no splitting of the difference — each side must be written to its own territory.

## How the Other Side Sees It

We set the three answers side by side to close. flock hangs the lock on the open file description, its life following the last reference. fcntl's record lock hangs on the process, buying release-through-any-fd at the price of the close-releases-everything trap. LockFileEx's conflict detection ignores ownership — even its own locks are blocked — unlock permission recognizes the process-plus-file-object combination, process exits are closed out by the system, plus a blocks-I/O layer the advisory locks lack. Observability runs the other way: Linux has /proc/locks laying every lock in the system open to view, and F_GETLK reporting the opponent's pid and range, while Windows has no public counterpart to either — when we want to probe, the only road left is the `FAIL_IMMEDIATELY` hard try. Bounded waiting reverses again: flock's and `F_SETLKW`'s waiting can only poll, while LockFileEx with `FILE_FLAG_OVERLAPPED` and an event has a first-class native scheme. For the other side's complete chain of evidence, please proceed to [File Locks: flock and fcntl Record Locks](../../linux/file-io/05-file-lock.md).

<ReferenceCard title="References">
  <ReferenceItem
    :id="1"
    title="LockFileEx function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-lockfileex"
  />
  <ReferenceItem
    :id="2"
    title="UnlockFile function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-unlockfile"
  />
  <ReferenceItem
    :id="3"
    title="UnlockFileEx function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-unlockfileex"
  />
  <ReferenceItem
    :id="4"
    title="OVERLAPPED structure"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/minwinbase/ns-minwinbase-overlapped"
  />
  <ReferenceItem
    :id="5"
    title="WaitForSingleObject function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitforsingleobject"
  />
  <ReferenceItem
    :id="6"
    title="CancelIoEx function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelioex"
  />
  <ReferenceItem
    :id="7"
    title="DuplicateHandle function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/handleapi/nf-handleapi-duplicatehandle"
  />
  <ReferenceItem
    :id="8"
    title="flock(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/flock.2.html"
  />
  <ReferenceItem
    :id="9"
    title="fcntl_locking(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/fcntl_locking.2.html"
  />
</ReferenceCard>
