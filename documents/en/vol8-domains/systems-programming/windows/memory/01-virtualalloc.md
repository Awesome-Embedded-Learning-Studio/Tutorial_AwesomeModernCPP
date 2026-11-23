---
title: "Virtual Memory: VirtualAlloc and VirtualProtect"
description: "First article on the Windows side of memory management, from a pure-memory viewpoint (no files involved): measured walks through VirtualAlloc's two-phase reserve/commit model — MEM_RESERVE of 1 GiB completes in about 2–5 microseconds while the commit charge does not move a byte; committing 72 KiB raises the charge by 80 KiB, and only after 18 pages are touched does the working set grow by 72 KiB (commit is a promise; physical memory is only occupied when touched); MEM_DECOMMIT returns pages to the reserved state and they can be recommitted; MEM_RELEASE only accepts the whole-block base address with dwSize=0, and both the non-base address and the nonzero size fail with error 87 in practice, not the intuitive 487. Allocation granularity runs on two tracks: returned addresses are always 64 KiB aligned while RegionSize rounds by pages (1 byte → 1 page, 0xFFFF → 16 pages, 0x10001 → 17 pages, 0x100001 → 257 pages); the uncommitted tail of a granularity block is FREE, not a hidden reservation; MEM_TOP_DOWN hands out addresses downward from the 0x7FF4 high band. A three-part evidence chain for PAGE_GUARD's one-shot trap (0x104 → 0x80000001 → self-disarm to 0x004 → VirtualProtect re-arms it for one more hit), and the system stack is built exactly on a guard band: a 2 MiB reservation holding a COMMIT section plus a 2–3 page guard band plus RESERVE; after a 768 KiB push the guard band moved down 764 KiB while VEH recorded zero hits during the growth (the kernel grows the stack silently); when the reservation runs out, 0xC00000FD is what you collect. VirtualQuery sweeps the whole 128 TiB in 193 steps (IMAGE 99 / MAPPED 16 / PRIVATE 17 / RESERVE 24 / FREE holes 37), contrasted with /proc/self/maps on row count and backing information, plus three facts about probe semantics (BaseAddress rounds down, RegionSize is the remaining length, the grouping key includes AllocationBase so adjacent allocations never merge). The heap's three tiers and three size thresholds: malloc(32) and HeapAlloc(32) share the same AllocationBase (the CRT sits on the process heap); ≤384 KiB goes through the main heap segment, 416 KiB–1016 KiB opens a new heap segment, ≥1 MiB goes straight to VirtualAlloc and the whole segment is returned on free."
chapter: 8
order: 1
platform: host
difficulty: advanced
cpp_standard: [20]
reading_time_minutes: 24
prerequisites:
  - "文件映射:CreateFileMapping 与 MapViewOfFile"
  - "结构化异常:SEH 与 VEH"
related:
  - "mmap 内存映射:把文件贴进地址空间"
  - "文件映射:CreateFileMapping 与 MapViewOfFile"
  - "OS 资源的 RAII 范式:fd、HANDLE 与映射的同一副骨架"
tags:
  - host
  - cpp-modern
  - advanced
  - 系统编程
  - Win32
  - 内存管理
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
translation:
  source: documents/vol8-domains/systems-programming/windows/memory/01-virtualalloc.md
  source_hash: c92b1a4ade7fbdefd7b8fcb00243654c5ad33956c66efb3195cfabb6a7a038a7
  translated_at: '2026-11-22T15:32:49+00:00'
  engine: glm-5.3
  token_count: 22541
---
# Virtual Memory: VirtualAlloc and VirtualProtect

The chapter on file I/O is behind us; the HANDLEs, mapped views, and exception dispatch chains in our hands have all been put through their paces. This chapter we switch to a new subject: instead of asking how files get into memory, we ask how a process asks the system for memory. When you write `malloc` or `new`, there has to be a layer underneath that talks to the memory manager directly, and the name Windows gives that entry point is `VirtualAlloc`. We have actually met it once before: in [file mapping](../file-io/02-file-mapping.md) (hereafter W02 as usual), the reserved sections under SEC_RESERVE were redeemed range by range with `VirtualAlloc(MEM_COMMIT)`. But W02's protagonist was the file, and it only examined `VirtualProtect` for the authorization boundary of mapped views. In this article we show the file out entirely, take up the pure-memory viewpoint, and walk through the three entry points `VirtualAlloc`, `VirtualProtect`, and `VirtualQuery` from the beginning; the release side, governed by `VirtualFree`, we cover alongside the allocation.

Windows splits the act of asking the system for memory into two explicit steps, and what each step grants is something we check against readings in the reserve-and-commit section below. We already saw a file-backed version of this two-phase model in W02's SEC_RESERVE; this article looks at its native form. Linux takes a different road for the same lazy-allocation problem: anonymous `mmap` relies on **overcommit** by default — the kernel promises far more address space than there is physical memory; the system-wide total of promises is reported as one number in meminfo, and a per-process commit charge is simply unavailable to user mode. Windows turns commitment into an explicit state and quota, and we will keep coming back to this fork in the road.

Numbering and environment conventions, stated up front as usual. The experiments in this article are numbered e1 through e5, following the five archived subdirectories; the code and raw outputs are all collected in the repository under `code/volumn_codes/vol8/systems-programming/windows/memory/01-virtualalloc/`. This numbering is independent of the thinking articles and of the e-series numbering in the file I/O chapter W01–W05; go by the file names and you cannot mistake one for another. The machine is still the local Win11 26200, the compiler is MSYS2 UCRT64 g++ 16.1.0, and the interop chain by which we invoke Windows programs from WSL across systems was introduced in the first article of the Win32 file I/O series (we shorten it to W01 in this series; the naming convention follows W02 and W03). Compilation is uniformly `-std=c++20 -Wall -Wextra`, which came back with zero warnings. The main experiments all run without optimization; when you reproduce e3 and e3b, please also keep `-O0`: the faulting instruction address that VEH (the vectored exception handler — the toolkit we borrowed from [SEH and VEH](../file-io/03-seh-veh.md), referred to as W03 in what follows; VEH is what lets us catch the exception at the instant a page is touched) records is part of the evidence, and once the optimizer moves the faulting write instruction, the scene no longer matches. Every program starts with `setvbuf(stdout, NULL, _IONBF, 0)` so that output emitted before a crash does not rot in the buffer. The `$?` received when running directly from WSL is the low 8 bits of the wait status: the `0xC00000FD` of the e3b overflow gets truncated to 253, and the full code has to be printed by the program itself. No new faces among the tools: the skeleton of `unique_handle` is defined in [The RAII Paradigm for OS Resources](../../thinking/01-raii-paradigm.md), `last_error_code` in [Error-Handling Paradigms](../../thinking/02-error-paradigm.md), and `check_win32` stays in W01; we bring them all along. The outputs were captured on 2026-10-03. All addresses in the output are subject to ASLR (address space layout randomization — every run reshuffles them), so if yours look different on a re-run, that is exactly what should happen.

## Reserve First, Commit Later

Let's learn the two words separately. **Reserve** corresponds to `MEM_RESERVE` of `VirtualAlloc`; the documentation's own words are `"Reserves a range of the process's virtual address space without allocating any actual physical storage in memory or in the paging file on disk"` — what gets registered is an address range, while physical memory and the paging file are not touched by a single byte. **Commit** corresponds to `MEM_COMMIT`, of which the documentation says `"Allocates memory charges (from the overall size of memory and the paging files on disk) for the specified reserved memory pages"` — what it spends is quota in the sense of a promise, and right after comes a sentence that matters: `"Actual physical pages are not allocated unless/until the virtual addresses are actually accessed"`. We happen to hold two ready readings to check this against: the **working set** is the batch of physical pages Windows grants the process — we already met it in W02 while discussing prefetch — and we read it from the `WorkingSetSize` field of `GetProcessMemoryInfo`. The **commit charge** is the total commitment the system accounts per process, and the `PagefileUsage` field in that same call gives exactly that; the commit size in Task Manager reads the same number.

The first move of e1 goes straight at the two-phase model: reserve 1 GiB in one shot, and watch it with three samples — before the reservation, committed but untouched, and after touching. Timing inside the program uses `QueryPerformanceCounter`; the sampling and query wrappers are the same as W02's. Straight to the output:

```text
== 步骤1:MEM_RESERVE 1 GiB(只预约地址,不占提交额度)==
  [采样 预约前 ] WorkingSet=3668 KiB   CommitCharge(PagefileUsage)=536 KiB
  VirtualAlloc(NULL,1GiB,MEM_RESERVE) = 0000024200000000  耗时 2 微秒
  [采样 预约后 ] WorkingSet=3676 KiB   CommitCharge(PagefileUsage)=536 KiB
  [VQ base          ] 0000024200000000..0000024240000000 State=RESERVE Protect=000000 Type=PRIVATE  AllocBase=0000024200000000 RegionSize=0x40000000
```

A 1 GiB range, delivered in 2 microseconds; across several runs of this round the landing point stayed in the 2-to-5-microsecond band. Now look at the two samples: the working set wobbled by just 8 KiB, which is the measurement code's own noise, while the commit charge did not move at all. What `VirtualQuery` (hereafter VQ) reads back is the entire 0x40000000 range wearing `State=RESERVE`. Things get interesting when we hold this next to Linux: over there, an anonymous mmap asking for the same span gets recorded in the kernel's own VMA (virtual memory area, the kernel's management unit that registers address ranges section by section); in user mode we cannot see how many promises were booked or by whom, whereas on Windows the reservation is an explicit, queryable state.

Step two puts the promise on the books. We pick two small spots inside the 1 GiB to commit — 64 KiB at the head of the region plus 8 KiB at the 1 MiB offset, 72 KiB in total — and then touch every page:

```text
== 步骤2:子区间提交 + 触碰 ==
  提交 base..base+64KiB(RW) -> 成功
  提交 base+1MiB..+8KiB(RW) -> 成功
  [采样 提交后未触碰] WorkingSet=3680 KiB   CommitCharge(PagefileUsage)=616 KiB
  [VQ base          ] 0000024200000000..0000024200010000 State=COMMIT  Protect=0x0004 Type=PRIVATE  AllocBase=0000024200000000 RegionSize=0x10000
  [采样 触碰后 ] WorkingSet=3752 KiB   CommitCharge(PagefileUsage)=616 KiB
  (提交 +72KiB 在先,WS 只长了被摸过的 ~18 页 —— 提交是承诺,触碰才占物理)
```

Read the three samples in sequence and the split between promise and physics comes into focus. After committing but before touching, the commit charge rose from 536 to 616 KiB. Of the 80 KiB increase, 72 KiB is what we asked for (0x04 is the raw value of `PAGE_READWRITE`; the 16 pages after rounding up plus 2 pages add up to exactly the 72 KiB committed), and the odd remainder is noise from stdio and the heap in the same process — jitter at this scale will show up again later. The working-set reading still had not moved. Once we had touched all 18 pages, the working set grew by 72 KiB while the commit-charge number did not change by one. So committing only records the promise; physical pages are handed out one by one at the moment we actually touch them. One more sentence from the documentation is worth recording alongside: the `VirtualAlloc` page opens with `"Memory allocated by this function is automatically initialized to zero"` — what we get is ready to use, no zeroing on our part required.

On the release side there are two words, and their semantics are far apart; we try each in turn. What `MEM_DECOMMIT` does is return committed pages to the reserved state; the other one's job is to hand the whole reserved region back as FREE. Step 4 of e1 lays two misuses and one correct use side by side:

```text
== 步骤4:释放语义 ==
  VirtualFree(base+64KiB,64KiB,MEM_RELEASE) -> 失败 GetLastError=87(ERROR_INVALID_PARAMETER)
  VirtualFree(base,4KiB,MEM_RELEASE)(dwSize 非 0) -> 失败 GetLastError=87(ERROR_INVALID_PARAMETER)
  VirtualFree(base,64KiB,MEM_DECOMMIT) -> 成功
  [VQ base(退提交后)] 0000024200000000..0000024200100000 State=RESERVE Protect=000000 Type=PRIVATE  AllocBase=0000024200000000 RegionSize=0x00100000
  [采样 退提交后] WorkingSet=3704 KiB   CommitCharge(PagefileUsage)=752 KiB
  再提交 base..+64KiB(退提交可逆) -> 成功
  VirtualFree(base,0,MEM_RELEASE)(整块归还,含仍在提交的 +1MiB 子区间) -> 成功
```

The two failures deserve a second look. The `MEM_RELEASE` requirements are written in hard language: the address must be the base address returned by the original `VirtualAlloc` reservation, and `dwSize` must be 0 — the documentation's own words, `"The function fails if either of these conditions is not met"`. But which error code you get when the conditions are violated is nowhere to be found in the documentation. Intuition leans toward 487 (`ERROR_INVALID_ADDRESS`) for the wrong-address direction — after all, e2 of W02 just produced a sample of 487: a reserve-flagged allocation into an already-reserved hole returns 487. In practice, though, both the non-base address and the nonzero size return 87 (`ERROR_INVALID_PARAMETER`). We stop guessing and record it as measured: this layer's checks file such misuse under parameter errors, not address errors. `MEM_DECOMMIT` is far more forgiving: the documentation says plainly that ranges not aligned to pages are rounded automatically, and decommitted pages can be recommitted; in the output the region went back to RESERVE and then to COMMIT again — a full round trip without a hitch. Also give the post-decommit sample in the block a second look: the commit charge actually reached 752, 136 KiB more than the 616 after touching. The pages we gave back did not bring the total down, because this number is a process-wide total, and stdio and the heap in the same process keep moving — the second time we have met jitter at that scale, so we read it for stage differences, never point against point. And please note the parenthetical on the last line: `MEM_RELEASE(base, 0)` returns the whole block, and the 1 MiB sub-range still in the committed state goes down with it. There is no entry point for returning in batches; whole-block-in, whole-block-out is the shape the documentation fixes. A library author who wants to wrap chunked release on top of it can only ever produce a set of whole blocks.

Step 3 also buries a fact the VQ section will need, so let's record it now: three back-to-back `MEM_COMMIT` allocations p1, p2, p3 — even when two adjacent ones carry identical attributes, VQ recognizes them as three separate regions, evidenced by the `AllocationBase` in the VQ readings each going its own way (that field records which allocation created this address; it is a different thing from the probe's starting point — details in the VQ section). After we freed p1, p2's reading did not budge; p1 only gave up its own 0x10000. The measured output of these allocations is in the archive's 01 directory. So adjacent allocations are not inherently one body; every `VirtualAlloc` call is an independent reservation.

> Someone may ask: why doesn't this article wrap `VirtualAlloc` in RAII? The author considered it, but the rules of this release layer are themselves the object under observation — `MEM_RELEASE` only accepts the whole block's base address with a zero size, so a wrapper would have to keep both the base and the boundaries on record before it dared step into a destructor. That is a worthwhile thing to do, and we will go into it when we reach the platform-abstraction chapter and fold both sides' wrappers into the toolkit. The thinking articles' `unique_handle` knows HANDLE and `CloseHandle`; VirtualAlloc returns a bare pointer, and the two do not line up — so in this article we honestly use bare calls.

## The Dual Track of Granularity: Addresses Obey 64 KiB, Sizes Obey Pages

`GetSystemInfo` hands us two numbers in one go: the page size, given as 4096, and another number we call the **allocation granularity**, whose value on this tier is 65536. Granularity already taught us a lesson once in W02: the offset of MapViewOfFile does not align to 4 KiB pages but to the 64 KiB granularity, and a page-aligned 4096 still eats error code 1132. This article goes one level deeper: granularity governs addresses while pages govern sizes — two standards in force at the same time, and they do not measure the same thing. The documentation's words, laid out first: on reserve, `"the specified address is rounded down to the nearest multiple of the allocation granularity"`, while `dwSize`, when the address is NULL, is `"rounded up to the next page boundary"`. Addresses lean toward granularity; sizes round up to pages. We check that sentence against six measured lines from e2:

```text
== 步骤1:请求尺寸 vs 实得地址/RegionSize(MEM_RESERVE|MEM_COMMIT)==
  请求       返回地址       低16位 64K对齐  RegionSize(VQ)
  0x1          177ba5c0000        0        是        0x00001000(1 页)
  0x1000       177ba5d0000        0        是        0x00001000(1 页)
  0xffff       177ba5e0000        0        是        0x00010000(16 页)
  0x10000      177ba5f0000        0        是        0x00010000(16 页)
  0x10001      177ba600000        0        是        0x00011000(17 页)
  0x100001     177ba620000        0        是        0x00101000(257 页)
```

The low 16 bits of all six returned addresses are zero — that is granularity's handiwork: no matter how many bytes you ask for, the address handed to us always lands on a 64 KiB boundary. The `RegionSize` column is entirely in the page's currency: ask for 1 byte and you are given exactly 1 page. Ask for 0xFFFF and it does not carry over to a granularity boundary — it rounds up to 16 pages, which happens to be exactly 64 KiB. Ask for 0x10001, and the 1 extra byte pushes us into the 17th page, so `RegionSize` becomes 0x11000. 0x100001 is the same story: a 1 MiB plus 1 byte request is rewarded with exactly 257 pages. If you are porting code over from the mmap world, this is precisely where you are most likely to stumble: Linux has only one 4 KiB ruler, while Windows treats address and size each by its own standard — even questions like "ask for 1 byte, receive 1 page" and "who owns the remaining 60 KiB inside the granularity block" send us back to re-check the tables.

Who owns the tail? We had e2 probe the block-internal offset of that 1-byte allocation:

```text
  探针:上面 1 字节那笔(00000177ba5c0000)块内偏移 +4KiB 处:
  [VQ +4KiB           ] State=FREE    Protect=0x0001 AllocBase=0000000000000000 RegionSize=0x0000f000
  [VQ +64KiB(块外)  ] State=COMMIT  Protect=0x0004 AllocBase=00000177ba5d0000 RegionSize=0x00001000
```

The uncommitted tail inside the granularity block reads FREE — not some hidden reservation. We asked for 1 page and the system granted exactly 1 page; the remaining 60 KiB of the same 64 KiB block is free for anyone to take. That is why consecutive small allocations often come back with adjacent addresses: step 2 of e2 fires four 64 KiB allocations in a row (the measured output of all four is in the archive's 02 directory), spaced by exactly 0x10000. Adjacency does get interrupted by queue-jumpers: between p1 and p2 of e1's step 3 lies a 0x230000 gap, where a new segment stretched out by the heap — printf needs memory too — wedged itself in (how the heap grows new segments gets a close look in the e5 section). The rule of granularity did not change; there are simply more people in the queue.

Where addresses start being handed out is another switch Windows leaves us: `MEM_TOP_DOWN`, of which the documentation says `"Allocates memory at the highest possible address"`, with a reminder that this may be slower. Step 3 of e2 puts the two modes side by side:

```text
== 步骤3:MEM_TOP_DOWN(从高往低发)==
  默认     low =00000177ba770000
  TOP_DOWN td1=00007ff48a850000
  TOP_DOWN td2=00007ff48a750000
  td1-low=0x7e7cd00e0000(跨了半个用户态空间,量级对比)
  td1-td2=0x100000(第二次 TOP_DOWN 更低 —— 高水位往下走,间隔恰 1MiB)
```

The default mode handed out addresses in the low band — this round of e1 landed around the 0x02 segment, and e2's run landed even lower, in the 0x01 segment. With `TOP_DOWN` on, it jumps straight up to the 0x7FF4 high band. The parenthetical on the last line says it spans half the user-mode space; let's do the math properly: `td1-low=0x7e7cd00e0000` works out to about 126.5 TiB — the 128 TiB user-mode space is traversed almost from head to tail, close to ninety-nine percent of the full span, not exactly half. Two consecutive allocations also reveal how the high-water mark walks: each asks for 1 MiB, and the second is issued right below the first's lower edge, spaced by exactly the 1 MiB requested. When would we care? If you want to probe the address layout, or keep some distance from certain high-risk regions, this is a ready-made knob.

## PAGE_GUARD: The Page That Fires Once and Disarms Itself

In the page-protection column we have already used `PAGE_READWRITE`, and those tiers are what `VirtualProtect` usually adjusts. Now let's bring on the most Windows-flavored character of this article: `PAGE_GUARD`. We use it stacked on top of some base protection; it does not occupy a protection tier of its own. The documentation's characterization, quoted verbatim: `"Guard pages thus act as a one-time access alarm"` — a one-time alarm. The instant it is touched, the exception the system raises is `STATUS_GUARD_PAGE_VIOLATION` (0x80000001), and at the same time the GUARD bit erases itself; the documentation's next sentence is `"When an access attempt leads the system to turn off guard page status, the underlying page protection takes over"` — the base protection underneath takes over from then on, and further touches are ordinary reads and writes.

Saying so is of course not proof; with e3 we film the complete three-part chain of evidence. The experiment's approach is direct: take one page of `PAGE_READWRITE|PAGE_GUARD` (VQ reads Protect=0x104; 0x100 is the GUARD bit flag); the VEH toolkit and exit-code reading are W03's, and the handler returns `EXCEPTION_CONTINUE_EXECUTION` the moment it recognizes this page's address:

```text
== 第 1 次写:应当响 ==
  [VQ 写前  ] Protect=0x0104(READWRITE+GUARD) State=COMMIT
  [VEH 命中 1] code=0x80000001 info[0]=1(写) info[1]=0x273639F0000 异常指令=00007ff7009a17d5
     是 guard 页的这一笔 → CONTINUE_EXECUTION,原指令重放
  重放成功,*g='A'
  [VQ 写后  ] Protect=0x0004(READWRITE) State=COMMIT

== 第 2 次写同页同地址:零异常(GUARD 已自灭)==
  *g='B',VEH 新增命中=0(写前 1 → 写后 1)
  [VQ 再写后] Protect=0x0004(READWRITE) State=COMMIT

== 再武装:VirtualProtect 塞回 GUARD 位 ==
  VirtualProtect(...RW|GUARD) -> 1(旧保护=0x4)
  [VQ 再武装后] Protect=0x0104(READWRITE+GUARD) State=COMMIT
  [VEH 命中 2] code=0x80000001 info[0]=1(写) info[1]=0x273639F0000 异常指令=00007ff7009a18d3
  *g='C'(这一笔应当又响一次)
  [VQ 第三次写后] Protect=0x0004(READWRITE) State=COMMIT
```

Let's walk the three segments. On the first write, VEH receives 0x80000001; info field 0 flags a write access, field 1 pinpoints the byte — the conventions of these fields were covered in W03. The handler returned `EXCEPTION_CONTINUE_EXECUTION`, and the replay of the original instruction succeeds precisely because of the self-disarm: while raising the exception, the kernel had already cleared the GUARD bit, so the replay faces a perfectly ordinary READWRITE page and the write goes in. The 0x0004 that VQ reads immediately after writes that fact into the scene. The second time we write the same page at the same address, VEH's hit count does not move — that is the zero-exception control. In the third segment we use `VirtualProtect` to stuff the GUARD bit back in; the out parameter faithfully reports the old protection 0x4; we write again, it fires once more, and afterwards it settles back to 0x04. We watched it fire, disarm, re-arm, and fire again — three segments spliced into one complete round. That is what it means to have seen its one-shot nature.

The contrast is `PAGE_NOACCESS`, a persistent roadblock by temperament: until you change it back, every access receives `0xC0000005`. Linux's guard page happens to belong to the persistent school — the measurements in [the virtual memory API article](../../linux/memory/02-vm-apis.md) say the same: a `PROT_NONE` page delivers `SEGV_ACCERR` on every out-of-bounds access; touch it as many times as you like, it blocks as many times. To imitate this semantics we would have to restore the protection ourselves inside the SIGSEGV handler and re-lay the trap — Linux has no native one-time alarm. The fork between the two sides is not about which is stronger; the alarm is simply put to different uses, and once you read on you will see why Windows needs it to disarm itself.

For the reason it exists, we look to the stack. The Creating Guard Pages documentation confesses it itself: `"there are operating systems that use guard pages to implement automatic stack checking"` — and Windows does exactly this. The layout mode of e3b takes a full-body photograph of the main thread's stack; we sample page by page from the stack top downward with VQ:

```text
$ ./e3b_stack_guard.exe layout; echo "exit=$?"
mode=layout  栈上取样点=00000073f2dff734(主线程,预约总量以 VQ 读数为准)
  -- 压栈前 --
  0073f2df9000..0073f2e00000 COMMIT  0x0004(7页) AllocBase=00000073f2c00000
  0073f2df6000..0073f2df9000 COMMIT  0x0104+GUARD(3页) AllocBase=00000073f2c00000  <-- 栈的 guard 带
  0073f2c00000..0073f2df6000 RESERVE 000000(502页) AllocBase=00000073f2c00000
  guard 带=00000073f2df6000  本栈预约合计=2048 KiB

递归 48 层(每层 16 KiB,共 768 KiB)平安回来,期间 VEH 命中=0

  -- 压栈后 --
  0073f2d3a000..0073f2e00000 COMMIT  0x0004(198页) AllocBase=00000073f2c00000
  0073f2d37000..0073f2d3a000 COMMIT  0x0104+GUARD(3页) AllocBase=00000073f2c00000  <-- 栈的 guard 带
  0073f2c00000..0073f2d37000 RESERVE 000000(311页) AllocBase=00000073f2c00000
  guard 带=00000073f2d37000,较压栈前 下移 764 KiB —— 提交推进到哪,guard 重埋到哪
exit=0
```

The main thread's stack is, at its core, one 2 MiB reservation: the top 7 pages are committed — the stack pages in active use. Below them, 3 pages of 0x0104 form the guard band; please note it is a band of 2 to 3 pages, not the lone single page of the textbooks — that is the shape measured on Win11. Below that, 502 pages are all in the reserved state; the reservation's family fortune lives here. The real show is between the two samples: with 16 KiB of stack per level of recursion, we pushed the stack down 768 KiB; the recursion came back safely, and VEH's hit count was 0. The stack really did grow by 764 KiB — the guard band's downward move matches the push amount, and the 4 KiB shortfall is exactly one page — while the COMMIT section grew from 7 to 198 pages, yet user-mode VEH reported not a single exception. The exception during stack growth did happen; the kernel simply digested it on its own: after the guard band was touched it cleared the bit, committed new pages, re-laid the guard, pushed the context back and replayed — all of it outside user mode's field of view. The self-disarm design clicks into place at this moment: the stack's guard band wants exactly touch-once, yield-once, retreat-one-page; were it a persistent roadblock, the stack could never grow automatically.

Every reservation bottoms out eventually. The overflow mode starts a worker thread with a 256 KiB stack recursing without limit; two conventions deserve your attention here: `CreateThread`'s `dwStackSize` gives the initial commit, so the top 64 pages are COMMIT outright, while the total reservation is inherited from the 2 MiB recorded in the PE header (PE is the Windows executable format; the header stores the default stack reservation the linker chose, and mingw's default is 2 MiB) — VQ still reads a 2048 KiB reservation. The program also calls `SetThreadStackGuarantee` to leave VEH 64 KiB of guaranteed stack, so the printf at the overflow scene lives long enough to emit:

```text
$ ./e3b_stack_guard.exe overflow; echo "exit=$?"
mode=overflow
  工作线程开跑,先拍自己的栈布局:
  -- 工作线程栈(请求预约 256 KiB) --
  00a2751c0000..00a275200000 COMMIT  0x0004(64页) AllocBase=000000a275000000
  00a2751af000..00a2751c0000 COMMIT  0x0104+GUARD(17页) AllocBase=000000a275000000  <-- 栈的 guard 带
  00a275000000..00a2751af000 RESERVE 000000(431页) AllocBase=000000a275000000
  预约合计=2048 KiB —— 实际以 VQ 读数为准(CreateThread 的取整口径见 README)
  开始无限递归...
  [VEH] code=0xC00000FD info[1]=0xA275011280 depth=122
  [VEH] STATUS_STACK_OVERFLOW —— 栈预约耗尽,放行让它收场(进程将崩)
exit=253
```

We recursed to depth 122; the guard band retreated all the way to the bottom of the reservation, with no pages left to yield, and this time VEH finally saw it: code `0xC00000FD`, `STATUS_STACK_OVERFLOW`. One more spot in the output deserves your attention: the worker thread's guard band thickened to 17 pages — an order of magnitude apart from the main thread's 2–3, and just 1 page off the 16-page guaranteed stack we set. The archive records only the shape; we point at the mechanism's causality and stop there — we make no assertions about the kernel's interior. This is not the same kind of code as 0x80000001: the top two bits of an NTSTATUS mark severity — codes beginning with 8 are warnings, those beginning with C are errors. A guard firing is an alarm: once it has sounded, execution can continue. Stack exhaustion is an accident: what it concludes is the whole process. The exit=253 received in the shell is the low-8-bit truncation mentioned at the start; the full code 0xC00000FD has to be printed by the program itself. Look back at the 8 of 0x80000001 and the guard's positioning comes into even clearer view — it was never born to kill processes; it serves use cases of handle-on-touch.

## VirtualQuery: Taking a Census of the 128 TiB Address Space

The last of the three entry points to take the stage governs observation. `VirtualQuery` asks about one region at a time: give it any address, and it measures out the run of contiguous, uniform attributes containing that address and hands us the fields: `BaseAddress` gives the page head after rounding the probed page down; `RegionSize` gives the remaining length from that page to the region's end; `State` is the three-way COMMIT/RESERVE/FREE; `Protect` is the protection tier; `Type` is the class of origin. The first two fields' readings hide three subtleties; we will come back to them specially after the panorama. Let's formally meet the three values of `Type`: `MEM_IMAGE` marks sections mapped from executable images (EXE and DLL); `MEM_MAPPED` marks views mapped from data files or page-file-backed objects; `MEM_PRIVATE` marks private memory — everything we get out of `VirtualAlloc` belongs to this class. `AllocationBase` records which allocation originally created the address; the earlier judgment that adjacent p1 and p2 do not merge rested on exactly this field.

To see the panorama, start from address 0, and with each step jump to the start of the next region, querying all the way up to the user-mode limit. Here is the panorama e4 produced:

```text
$ ./e4_maps_scan.exe; echo "exit=$?"
用户态上限 lpMaximumApplicationAddress=0x7ffffffeffff
扫描步数(VirtualQuery 次数)=193,时间 <1 秒 —— FREE 段一跳就是一个大空洞

== State/Type 组合统计(非 FREE 区段)==
  State/Type           个数           字节
  COMMIT/IMAGE             99         16252928 (0.02 GiB)
  COMMIT/MAPPED            16          1777664 (0.00 GiB)
  COMMIT/PRIVATE           17           385024 (0.00 GiB)
  RESERVE/IMAGE            10           167936 (0.00 GiB)
  RESERVE/MAPPED            2          1032192 (0.00 GiB)
  RESERVE/PRIVATE          12       4340690944 (4.04 GiB)
  FREE(空洞)             37  140733127983104 (128.00 TiB,用户态总空间 128 TiB)
```

The 128 TiB space, fully asked in 193 steps, in under a second — the trick lies in FREE's behavior: holes are contiguous, so one query skips an entire hole; the 37 holes total roughly 128 TiB (the 128.00 the program prints is a rounded reading — subtract the few segments the program itself occupies, and what remains is all holes). There are 156 non-FREE regions, with COMMIT/IMAGE alone holding 99 — that is what the image sections of about 30 DLLs unpack to: each DLL's code, data, and import tables form their own sections, never merged with one another.

Put the table above next to Linux's `/proc/self/maps` and the fork shows at once. The row count differs by an order of magnitude: for a program of the same weight over there, maps usually falls between 25 and 55 rows, each row corresponding to one VMA; what we counted on this side is 156 regions. The information content is split as well: every row of maps carries the path of the backing file, so one glance tells us who mapped which segment. VQ offers only the coarse classification of `Type`; the owner's name it does not give. Of the statistics, the largest entry — that 4 GiB reservation — VQ cannot say whose it is: every Win11 process carries two large PRIVATE reservations; one read 4 GiB plus 128 KiB (a shade over 4 GiB) in our measurements, the other is a solid 32 MiB block. We ran two control groups — one bypassing WSL interop to run under cmd directly, one built with `-static` to strip the mingw runtime — and both reservations were present in both runs, so they have nothing to do with our program; they come from the system DLL chain's reservations. VQ sees the shape but not the owner; to ask about ownership we would have to descend to the NT layer's `NtQueryVirtualMemory` — one level deeper than this article. The numbers that fail to reconcile across the two sides are the same kind of phenomenon: the scan's COMMIT/PRIVATE totals 376 KiB while the process's commit charge is 1004 KiB; the difference is the shared commit of DLL and mapped sections plus the kernel-side accounting conventions — in form, the same class of phenomenon as the gap between VmSize and Private on the Linux side.

Now back to those three subtleties of the readings; let's take them one at a time. The first is the reading of `BaseAddress`: it is the result of rounding the probed page down, not the pointer we passed in verbatim. The second is the most counterintuitive: `RegionSize` measures the remaining length from the probed page to the region's end — the full region's size it does not report. The documentation's example actually puts it plainly: `"if there is a 40 megabyte (MB) region of free memory, and VirtualQuery is called on a page that is 10 MB into the region, the function will obtain a state of MEM_FREE and a size of 30 MB"` — a 40 MiB hole probed at the 10 MiB mark reads as 30 MiB remaining. If our probe does not land at the region's head, the number we read is smaller than the whole region; while e3b probed down along the stack, adjacent queries once read nested numbers, and only on inspection did we find that the probe's position had been moving. The third lies in grouping: `AllocationBase` is part of the regions' merge key — two adjacent, independently allocated runs with the same State and the same Protect never merge into one region; the documentation writes this into the definition of a region: only pages belonging to the same initial allocation count as one region. e1's p1 and p2 and e2's in-block probe keep verifying it over and over.

## Underneath malloc: The Heap's Three Tiers

With the foundation seen, we return to the everyday: what C++ programmers use daily is `malloc` and `new` — how many layers down from them does `VirtualAlloc` sit? e5's answer is three: `malloc`, inside the CRT's implementation (the C runtime, the library layer that underpins C and C++ programs), goes down to the Win32 heap API (`HeapAlloc`/`HeapFree`), and when the heap manager runs short, it goes straight to `VirtualAlloc` for a new segment. Saying so in midair will not stand; we verify the three tiers' kinship by judging each block's ownership with `AllocationBase`:

```text
== 步骤1:小块三家同台(32 字节)==
  malloc(32)       req=0x00000020 p=0000019e4e4ceca0 VQ: COMMIT  RegionSize=0x00004000 AllocBase=0000019e4e4c0000 块偏移=0xeca0 主堆段内
  HeapAlloc(默认堆) req=0x00000020 p=0000019e4e4cee20 VQ: COMMIT  RegionSize=0x00004000 AllocBase=0000019e4e4c0000 块偏移=0xee20 主堆段内
  HeapAlloc(私有堆) req=0x00000020 p=0000019e4e900860 VQ: COMMIT  RegionSize=0x00002000 AllocBase=0000019e4e900000 块偏移=0x0860 新段(专属)
  (malloc 与默认堆 HeapAlloc 同一个 AllocBase=0000019e4e4c0000 —— CRT malloc 坐在进程堆上;私有堆另起炉灶)
```

The pointers of `malloc(32)` and `HeapAlloc(GetProcessHeap(), ..., 32)` land inside the same `AllocationBase`, just 0x180 bytes apart — direct evidence that the CRT sits on the process heap. The UCRT we installed is one generation of the CRT on Windows (Microsoft's Universal C Runtime), and its malloc starts no private heap of its own. The private heap we created with `HeapCreate`, though, receives its own new segment. The heap count also comes into view on the way: `GetProcessHeaps` reports 2 before we build a private heap — the default heap plus one built by the CRT or the startup chain — and 3 after.

Change the block size and the landing place splits into tiers. e5's sizes run from 4 KiB all the way to 2 MiB; at release we look at what each landing place becomes, and the tiers sort into the table below — the boundaries are this machine's readings from this round:

| Tier | Landing place | After release |
| --- | --- | --- |
| ≤384 KiB | Inside the main heap segment, same AllocationBase as the default heap | Segment unaffected; the block returns to the heap's own free management |
| 416 KiB – 1016 KiB | A new heap segment is opened; one new segment can hold multiple blocks | Segment retained; pages decommitted (RESERVE) or shrunk to a 1-page header |
| ≥1 MiB | Standalone region; the heap manager issues VirtualAlloc directly | The whole region FREE; the address space returned entirely |

Of the three tiers' evidence chains, the hardest is the fate after release. The 1 MiB and 2 MiB allocations, after `HeapFree`, read as an entirely FREE region with `AllocationBase` zeroed — meaning they were never heap segments at all: the heap manager went to `VirtualAlloc` directly on our behalf, and release likewise takes the whole-block-return path, fitting e1's step-4 whole-block semantics seamlessly. The middle tier are standard new heap segments: 416 KiB and 480 KiB land inside the same new segment; after the 416 KiB block is freed the segment shrinks to a 1-page header, and after the 480 KiB block is freed the whole segment decommits to RESERVE — the segment stays where it is, the pages go back, and the next big block can still use it. Small blocks inside the main heap segment are the plainest: release merely returns them to the heap's own free management, and the segment's state changes not a word. `malloc` walks the same chain: `malloc(1MiB)` receives an independent new segment, and after free the whole segment becomes FREE. So when you write large buffers on Windows, past the 1 MiB mark it makes no difference whether you go the `malloc` road or the direct `VirtualAlloc` road — underneath, both land on the same layer; the only difference is whether the heap's bookkeeping sits above. One more cold fact related to the error-handling paradigms: HeapAlloc's documentation states plainly that on failure `"it does not call SetLastError"` — at this layer, GetLastError has nothing to offer; without `HEAP_GENERATE_EXCEPTIONS`, a null check is the only failure signal (with it enabled, failure switches to raising an exception like `STATUS_NO_MEMORY`, which e5 did not enable). Alignment, on the other hand, is promised: on 64-bit, returned pointers are aligned to 16 bytes per `MEMORY_ALLOCATION_ALIGNMENT`, while the `malloc` side's alignment is separately guaranteed by the CRT.

## How the Other Side Sees It

Let's line the two sides up. Windows makes lazy allocation an explicit two-step; the promise is recorded in the commit-charge number, and the reserved state is queryable and returnable. Linux hides it in the overcommit default: mmap promises and is done, and a per-process commitment number is unavailable to user mode. The two-phase model even assembles a file-backed version out of W02's SEC_RESERVE. The alignment rulers split as well: this side runs address and size on a dual track, 64 KiB granularity plus 4 KiB pages; that side measures everything with a single 4 KiB ruler — precisely where friends porting mmap code over stumble most. Out-of-bounds alarms differ even further: here `PAGE_GUARD` disarms itself after firing once, and the next hit depends on a re-arm — the stack's automatic growth feeds on exactly this; there `PROT_NONE` is a persistent roadblock, and the virtual memory API article's measurements show it blocking as often as you touch. To imitate the one-time behavior, the re-laying has to be done by hand inside a signal handler. For panorama queries, this side's `VirtualQuery` asks region by region — 193 steps swept the 128 TiB, with Type giving a classification but never the owner; that side's maps gives everything in one breath, an order of magnitude fewer rows, each row carrying the backing file's path. The malloc layers, by contrast, are isomorphic across the two sides: Linux's glibc likewise sends small blocks through the main heap, which grows through the old brk call (the legacy interface that stretches the end of the data segment), while large blocks take the mmap direct route; the thresholds are dynamic there too, matching e5's three tiers — the specific numbers on both sides have to be read off the machine at hand.

One more Windows matter is left unfinished, reserved for the next article's body: if we pass `INVALID_HANDLE_VALUE` as the file handle, the mapping hangs off no file at all — the system's paging file serves as the backing store, and once it gets a name it becomes cross-process shared memory. A page-file-backed, named mapping object is this article's PRIVATE commits extended across processes, and Linux's shm_open is waiting on that side to compare tables. See you in the next article.

<ReferenceCard title="References">
  <ReferenceItem
    :id="1"
    title="VirtualAlloc function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualalloc"
  />
  <ReferenceItem
    :id="2"
    title="VirtualFree function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualfree"
  />
  <ReferenceItem
    :id="3"
    title="VirtualProtect function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualprotect"
  />
  <ReferenceItem
    :id="4"
    title="VirtualQuery function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualquery"
  />
  <ReferenceItem
    :id="5"
    title="Memory Protection Constants (WinNT.h)"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/memory/memory-protection-constants"
  />
  <ReferenceItem
    :id="6"
    title="Creating Guard Pages"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/memory/creating-guard-pages"
  />
  <ReferenceItem
    :id="7"
    title="GetSystemInfo function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-getsysteminfo"
  />
  <ReferenceItem
    :id="8"
    title="HeapAlloc function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/heapapi/nf-heapapi-heapalloc"
  />
  <ReferenceItem
    :id="9"
    title="GetProcessHeaps function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/heapapi/nf-heapapi-getprocessheaps"
  />
  <ReferenceItem
    :id="10"
    title="mmap(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/mmap.2.html"
  />
</ReferenceCard>
