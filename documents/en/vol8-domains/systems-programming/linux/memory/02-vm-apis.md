---
title: "Virtual Memory API Landscape: mprotect/madvise/mlock"
description: "Once you hold a stretch of virtual memory, how do you manage it? mprotect governs permissions, madvise delivers advice, mlock governs residency. This article measures si_addr following the exact faulting byte (correcting L02's page-start rough reading); the W^X three-step where Linux grants RWX and macOS refuses; text plus W going through COW while [vvar] answers EACCES and [vdso] answers EINVAL in the do-not-touch list; the first cold call dying in ld.so's lazy binding after a .data page is demoted (the handler must restore page permissions before it can speak); MADV_RANDOM switching readahead off for exactly 1024 KiB while NORMAL adaptively oscillates between 3088 and 8192 KiB; DONTNEED reading anonymous pages back as zeros with Rss halved; DONTFORK making the whole segment vanish in the child; MADV_REMOVE really punching holes on ext4; mlock's incidental pre-faulting and the ENOMEM edge of RLIMIT_MEMLOCK. The flagship build is guarded_buffer<T>, which uses a tail-aligned guard page to turn silent overflows into si_addr reporting exactly which byte went out of bounds, plus mincore's three notions of residency and a cross-process peek with process_vm_readv, ending with a preview of Windows PAGE_GUARD's one-shot trap"
chapter: 8
order: 2
platform: host
difficulty: advanced
cpp_standard: [20]
reading_time_minutes: 28
prerequisites:
  - "进程内存布局:/proc/pid/maps 全图"
  - "mmap 内存映射:把文件贴进地址空间"
related:
  - "进程内存布局:/proc/pid/maps 全图"
  - "共享内存:shm_open 与映射"
  - "虚拟内存:VirtualAlloc 与 VirtualProtect"
tags:
  - host
  - cpp-modern
  - advanced
  - 系统编程
  - POSIX
  - 内存管理
  - 实战
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
translation:
  source: documents/vol8-domains/systems-programming/linux/memory/02-vm-apis.md
  source_hash: a79f318bd4f0115e69fdf2cf093011f162f47ec551511da84d9091789ee2a977
  translated_at: '2026-11-22T16:06:29+00:00'
  engine: glm-5.3
  token_count: 25522
---
# Virtual Memory API Landscape: mprotect/madvise/mlock

In the [previous article](./01-memory-layout.md) we finished drawing the full map of the process address space: we identified every one of those dozens of segments in maps. Which ones are readable? Which are executable? Where does the heap grow from, and where does the stack top land? With the map drawn, new questions naturally surface. When mmap pastes a run of pages into the address space, the prot parameter hands it an initial set of permissions, but permissions are not carved in stone. Once we hold a stretch of virtual memory, how do we keep managing it?

Linux assigns this job to three APIs, and each governs one layer. `mprotect` governs permissions: we can make a formerly writable range unwritable, or a read-only data page executable; what changes is the protection bits of each page in the page table. `madvise` governs advice: how do you plan to use this memory? A sequential scan, or random jumps? Will you still need it any time soon? We hand the kernel a sentence; whether the kernel listens, and how, we let the numbers speak. `mlock` governs residency: we keep these pages in physical memory and the kernel may not swap them out; real-time audio and key pages care about exactly this layer. In this article we walk each of the three layers, and at the end we assemble them into a practical tool, `guarded_buffer<T>`, a buffer that reports on the spot which byte index went out of bounds. The closing sections also bring along mincore and process_vm_readv, two small observation tools.

We already laid the groundwork in the mprotect section of [L02](../file-io/02-mmap-memory-mapping.md): the effect applies to whole pages, we must align addr to a page boundary ourselves, and an out-of-bounds access delivers SIGSEGV, with si_code splitting into `SEGV_ACCERR` (the mapping exists, permission denied) and `SEGV_MAPERR` (the address is not mapped at all). What we dig into next are all matters beyond that groundwork: how does a guard page work as an overflow detector? Where does W^X's real boundary lie on Linux? Which segments can we not touch? There is also a live scene where demoting permissions backfires on ourselves; we run into it in the E1 continuation.

The experiments are numbered E1 through E6, corresponding one-to-one with the directories 01 through 06 under `code/volumn_codes/vol8/systems-programming/linux/memory/02-vm-apis/` in the repository. The code along with all the raw outputs is kept in that archive, ready for cross-checking at any time. Most output blocks in the text are excerpts; dropped scenario headers and explanation lines are marked with `...`; when you cross-check against the archive, the archive is authoritative. The E numbers in this article refer to this article only; the E series of each file-io article are unrelated to ours, so just follow the directory numbers when browsing the archive. The series' old trio of `unique_fd`, `sys_call`, and `errno_code`, plus a fourth piece `mapped_region`, follow the definitions from the [RAII article](../../thinking/01-raii-paradigm.md) and the [error handling article](../../thinking/02-error-paradigm.md); in this article we only reference them and do not redefine them. The only output channel allowed inside a signal handler is `write_all`/`write_hex`, two small wrappers over raw write; printf is not async-signal-safe, so it must not be touched in a handler. The sigbus section of L02 used the same approach, and the archive collects it into `common/sigout.hpp`. Actions that trigger SIGSEGV also follow L02's discipline: fork a child process to do the work, let the handler print only with write, and let the parent's waitpid reap the child and interpret the result.

As usual, we state the environment up front, since every number below must be checked against it: the experiments ran on the author's WSL2, kernel build 6.18.33.2-microsoft-standard-WSL2, g++ 16.2.1, and compilation is uniformly `-std=c++20 -Wall -Wextra -O2`. Three environment facts directly decide whether a re-run succeeds. The E2 readahead experiment needs a 64 MiB file on ext4, with the path hard-coded as `/home/charliechen/lm02_scratch/`; before re-running you must create `~/lm02_scratch`, and on tmpfs the readahead experiment simply does not hold. The machine's `RLIMIT_MEMLOCK` is 64 MiB with identical soft and hard limits, and E3's boundary numbers are computed against it; in zsh, `ulimit -l` reports 65536 in KiB units, so do not read that as 64 KiB. Addresses in the outputs are all subject to ASLR and change on every run; the regularity we cite is the kind of equation "si_addr equals the faulting byte", never any concrete value.

## E1: mprotect in Depth — si_addr Precision, the W^X Boundary, the Do-Not-Touch List

### On the guard page, si_addr follows byte by byte

We start by auditing a detail L02 left behind. In that article's experiments, the si_addr of s2 and s3 both landed on the page start, but those two accesses had targeted the page-start byte in the first place, so from those two alone we cannot tell whether si_addr reports the page start or the faulting byte. The finer-grained offset behavior is what we fill in here: with a two-page anonymous writable mapping, we demote the second page to `PROT_NONE`. That is a guard page, a page where whoever touches it gets a SIGSEGV; stack-overflow detection and allocator isolation both use it. Then we strike three different bytes of it: byte 0, the last one (byte 4095), and offset 123 in the middle; the first two hits are writes, the third switches to a read.

```text
== (1) guard page: si_addr follows the exact byte ==
data  page = [0x7ad3a9f31000, 0x7ad3a9f32000)  rw-p
guard page = [0x7ad3a9f32000, 0x7ad3a9f33000)  ---p (PROT_NONE)
   maps: 7ad3a9f31000-7ad3a9f32000 rw-p 00000000 00:00 0
   maps: 7ad3a9f32000-7ad3a9f33000 ---p 00000000 00:00 0
data[kPage-1] = 'K' ok (last legal byte)
...
[handler] SIGSEGV in scenario "g1 write guard+0", si_addr = 0x00007ad3a9f32000, si_code = 2 (SEGV_ACCERR)  <- 映射在,权限不许
   OK: child exited 71 as expected
...
[handler] SIGSEGV in scenario "g2 write guard+last", si_addr = 0x00007ad3a9f32fff, si_code = 2 (SEGV_ACCERR)  <- 映射在,权限不许
   OK: child exited 72 as expected
...
[handler] SIGSEGV in scenario "g3 read guard+123", si_addr = 0x00007ad3a9f3207b, si_code = 2 (SEGV_ACCERR)  <- 映射在,权限不许
   OK: child exited 73 as expected
```

The three si_addr values are all distinct: `...2000`, `...2fff`, and `...207b`, and each one exactly equals the address of the byte we accessed, with no page-aligned rounding anywhere. The two accesses in L02 did target the page-start byte, and the two readings happen to coincide at a page start; the real rule is that **si_addr reports the faulting byte itself**. This precision is what lets the E4 handler compute which byte index went out of bounds; it is the foundation of the whole tool. A quick glance at the maps output as well: after mprotect, the original single mapping is torn into two, `rw-p` and `---p` each getting its own line; in the kernel's eyes the guard page is an independent VMA.

### The W^X three-step: Linux grants, macOS refuses

W^X is short for write XOR execute, and we should formally meet the idea that writes and execution are mutually exclusive: it is a security position holding that the same page of memory must be either writable or executable, never both, so an attacker cannot stuff machine code into a writable region and then execute it. It is also the hurdle JIT (just-in-time compilation, placing runtime-generated machine code into memory for execution) cannot get around: the generated code has to be written in, then switched to executable permission, and switched back after modification. The textbook-standard routine is three steps: write code under RW, execute under RX, and back to RW to patch. We follow it once; on x86-64, six bytes are enough for a piece of code:

```cpp
// wx_body() 节选:msg 缓冲与 snprintf 的打印脚手架略
unsigned char* code = static_cast<unsigned char*>(
    ::mmap(nullptr, kPage, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));

const unsigned char kRet42[] = {0xB8, 0x2A, 0x00, 0x00, 0x00, 0xC3}; // mov eax,42; ret
std::memcpy(code, kRet42, sizeof kRet42);                 // step1 [RW] 写码
::mprotect(code, kPage, PROT_READ | PROT_EXEC);
const int r42 = reinterpret_cast<int (*)()>(code)();      // step2 [RX] 执行 -> 42

::mprotect(code, kPage, PROT_READ | PROT_WRITE);          // 回 RW 改码
const unsigned char kRet99[] = {0xB8, 0x63, 0x00, 0x00, 0x00, 0xC3}; // mov eax,99; ret
std::memcpy(code, kRet99, sizeof kRet99);
::mprotect(code, kPage, PROT_READ | PROT_EXEC);
const int r99 = reinterpret_cast<int (*)()>(code)();      // step3 [RW->RX] -> 99
```

```text
...
   step1 [RW ] memcpy mov eax,42; ret
   step2 [RX ] mprotect RX, call -> eax = 42
   step3 [RW->RX] patch mov eax,99; ret (x86-64 取指缓存硬件自洽,无需显式 flush), call -> eax = 99
   step4 mprotect(R|W|X) rc=0  <- W^X 只有 macOS 强制,Linux 放行
```

Steps 1 through 3 passed in one go, 42 and 99 arrived as promised, and we performed no cache flushing: the x86-64 instruction cache keeps itself consistent in hardware here; other architectures (ARM, for instance) genuinely need to think about icache synchronization. The really interesting part is step 4: we ask for all three permissions at once, and mprotect returns rc 0. Linux simply does not enforce W^X; RWX went through in one shot on the local 6.18 kernel. macOS takes a different documented stance: according to Apple's official documentation, since 10.14 pages that are simultaneously writable and executable are restricted, and the same step over there would receive EPERM; we did not measure on macOS, so what is recorded here is the documentation's claim. Permission also brings convenience: after step 4 we write a `0xCC` (x86's int3 instruction) into the page, skip the switching entirely, and call it on the spot —

```text
   step4' write int3 under RWX, call ->
[handler] SIGTRAP in scenario "wx4 exec int3 under RWX", si_addr = 0x0000000000000000, si_code = 128 (SI_KERNEL)  <- 内核送的 trap,不带精确地址(本机 int3 实测)
```

SIGTRAP arrived, and along the way we caught a mismatch between documentation and reality: man 2 sigaction stocks TRAP_BRKPT (breakpoint trap) among the grades prepared for SIGTRAP, but in the local measurement the si_code delivered by int3 is 128 (SI_KERNEL), meaning a trap sent by the kernel, with si_addr 0 and no precise address attached. We record the measured behavior; the TRAP_BRKPT grade never showed up in the local measurements, and we will not guess when it would.

### The do-not-touch list: exactly opposite to intuition

The three-step dance played out on our own anonymous pages, but what is mprotect's attitude toward the segments a process already has? We tune through the representative segments one by one. Intuition says adding W to the text segment ought to be refused, right? How could the code segment be writable just like that? The measured answers are laid out here, line by line:

```text
...
   text (function page) r-x   -> r-x  rc= 0 (ok)
   text (function page) +W    -> rwx  rc= 0 (ok)
   text page really became rwx -- maps says:
   maps: 61f2a5007000-61f2a5008000 rwxp 00001000 08:30 1689032                    /home/charliechen/lm02_scratch/e1
   rodata page           +W   -> rw-  rc= 0 (ok)
   rodata real write under rw: 'R' -> 'X' (COW copy, no signal)
   .data page           ->r   -> r--  rc= 0, restore rw rc= 0
   .data page           rw    -> rw-  rc= 0, restore rw rc= 0
   heap(brk) page       ->--- -> ---  rc= 0, restore rw rc= 0
   heap(brk) page        +x   -> r-x  rc= 0 (ok)
   heap(brk) page       rw    -> rw-  rc= 0, restore rw rc= 0
   [vvar]                     -> rw-  rc=-1 errno=13 (Permission denied)
   [vdso]                     -> rw-  rc=-1 errno=22 (Invalid argument)
   [vsyscall]                 -> 本机 maps 里没有这段映射
...
```

Let us walk through them one by one. The first try is text plus W; rc comes back 0 and the maps output shows that page genuinely became rwxp. We really did fire a write into rodata as well: `'R'` changed to `'X'` with no signal at all. The EACCES of our intuition never showed up on the way to widening these segments. Why? Because these segments come from the executable's `MAP_PRIVATE` mapping; widening permissions changes only the page table, and the moment you actually write, copy-on-write kicks in (copy-on-write: when writing a private file mapping, the kernel produces a private copy, and the modification never reaches the original file). We wrote our own copy, and the file came to no harm. heap plus x was granted too, and just like that an executable heap stands. The NOTES of man 2 mprotect put this on record long ago: Linux is relaxed to the point that nearly anywhere in the process address space can be changed, except the vsyscall area; and [vsyscall] does not even have a mapping on the local WSL2 — we never saw it in the previous article's full map either.

The truly untouchable ones are somebody else: `[vvar]` got EACCES, `[vdso]` got EINVAL. The two of them appeared in the previous article's full map; they are special segments the kernel maps into every process: vdso is a small shared library the kernel prepares, letting calls like gettimeofday skip one trap into the kernel, and vvar is its companion data page. The kernel does not treat them as ordinary pages; the moment mprotect reaches out it is pushed back, and the push-backs are not even the same: one says the permission cannot be granted this way, the other says the parameter itself is invalid. One more note of proportion: rc=0 only says the change was made; the verdict shows only on actual access. In the archive, the three child processes seg5/6/7 each stomped once on the demoted .data, heap, and stack pages, and all delivered SEGV_ACCERR, consistent with L02's division of labor. seg7 has one extra subtlety: the demoted page lives right on the stack, so the handler simply relocated onto another stack prepared by `sigaltstack`. The probe targets the old frame's page, and for us the altstack is insurance, not a necessity.

## E1 continued: demoting .data, stepping on ld.so's lazy binding

If widening is granted this readily, what about narrowing? We demote a .data page from rw to read-only, then do the most ordinary thing imaginable: read errno once. The scene below left the author staring at the screen for a good while the first time it ran.

```text
-- scenario: seg8 cold errno read under RO .data -> SIGSEGV inside ld.so (GOT lazy binding)
    seg8: child mprotect(.data page, PROT_READ) rc=0, now first cold errno read

[handler] SIGSEGV in scenario "seg8 cold errno read under RO .data page", si_addr = 0x000061f2a500c000, si_code = 2 (SEGV_ACCERR)  <- 映射在,权限不许
   OK: child exited 78 as expected
   (&g_data = 0x61f2a500c0d8, si_addr 与它同页:那里住着 .got.plt)
...
```

The cause of death is not in our code, so we must summon the two parties involved. GOT stands for Global Offset Table; it is the table dynamic linking uses to register the real addresses of external symbols. PLT stands for Procedure Linkage Table; it is a small trampoline in front of each external function's entry. Dynamic linking enables lazy binding by default: the first time some external function is called, its GOT slot is still empty, and the PLT leads us into ld.so's resolver; ld.so here means the dynamic linker, the owner of those five ld-linux segments in the previous article's maps. The resolver looks up the address and **writes it into the GOT**, so that the next call goes straight there. The trouble lies exactly in that write: on this machine, `.got.plt` and `.data` are squeezed onto the same page. After we demote that page to read-only, for any libc function this process calls for the first time, the resolver's GOT write on our behalf is caught red-handed by SIGSEGV; the process dies in ld.so's hands, not on our variable. The statement `errno = 0` expands into `*__errno_location() = 0`; the experiment's prewarm stage heated write and strlen up in advance, so this death is pinned precisely onto the resolution of the cold symbol errno, and si_addr points exactly at the page start where the GOT lives.

There is also discipline in how the handler is written, and the code is just two lines, so let us look at it verbatim:

```cpp
// e1.cpp 的 on_signal 节选
void on_signal(int sig, siginfo_t* info, void*)
{
    // 进 handler 第一件事:把 .data 页权恢复。mprotect 异步信号安全,且早已解析。
    // 不然 handler 自己要用的函数谁还没解析过,就要写 GOT——而 GOT 与 .data 同页,
    // handler 会当场二次 SIGSEGV,被默认动作直接带走
    ::mprotect(reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(&g_data) & ~(kPage - 1)),
               kPage, PROT_READ | PROT_WRITE);
    write_all("\n[handler] ");
    // ... 打印 sig / si_addr / si_code 后 _exit(70 + g_which)
}
```

Our handler has to speak, and speaking may pass through functions that have not been resolved yet. So the order of these two lines cannot be reversed: restore the page permission before opening our mouth; mprotect itself is async-signal-safe and long since resolved. This scene has two side branches worth pulling. From the re-run angle: seg8 depends on the cold state where the process has not yet read errno; when re-running, keep the original order from the archive. If you switch the link option to `-Wl,-z,now` for full eager binding, the GOT is filled at startup and this scene can no longer be demonstrated. From the engineering angle: that read-only GNU_RELRO segment hugging the rw one in every file module of the previous article's full map does exactly the same-direction work: once relocation is done, the GOT is turned read-only and nobody may write it again. Our scene demoting the GOT's page to read-only is aligned with RELRO's direction; the one who really opens the hole is somebody else: the handler's first act on entry is to flip that page back to writable, and lazy binding's road reopens. What RELRO wants to block is exactly that door of automatic permission loosening, and with page permissions, every notch you loosen carries that notch's price.

## E2: madvise — handing usage hints to the kernel

mprotect issues commands, and the kernel must comply. madvise offers advice, and whether the kernel listens is entirely up to it. Its signature is one line, `madvise(addr, length, advice)`, with advice chosen from a string of `MADV_` constants. man 2 madvise puts it plainly: ordinary advice affects performance only, and MADV_DONTNEED is the one exception that changes semantics; by the time we reach (B) we will see how drastically it changes them.

### (A) Readahead policy: three numbers, each telling its own story

Let us make readahead explicit here: you touch one page of a file, and the kernel takes it upon itself to read several following pages into the page cache as well, betting you will need them soon. A winning bet saves a pile of page faults; a losing bet burns IO for nothing. The hints we hand the kernel are no more than the three names NORMAL, SEQUENTIAL, and RANDOM. The measurement method is: E2 prepares a 64 MiB file on ext4; each round we squeeze the page cache clean with `posix_fadvise(DONTNEED)`, map the file, touch only the leading 1 MiB (one byte per page), and then use mincore to count how much residency grew (what mincore is, we cover in detail in E5). We lay out the numbers from all three rounds together: the primary run plus two re-runs:

| MADV policy | Primary run | Re-run 1 | Re-run 2 |
| ----------- | ----------- | -------- | -------- |
| NORMAL (adaptive) | 8192 KiB | 8192 KiB | 3088 KiB |
| SEQUENTIAL | 3084 KiB | 3088 KiB | 3080 KiB |
| RANDOM | 1024 KiB | 1024 KiB | 1024 KiB |

The RANDOM column is beautifully steady: we touched 1 MiB, residency is exactly 1 MiB, not one page more; readahead was switched off entirely. Random access was never going to feed readahead anyway, so not feeding it is fine by all means. SEQUENTIAL holds steadily at the 3080 KiB grade; the window is fixed, and granting it buys about two extra times. The biggest surprise to the author is NORMAL: 8192 KiB in the primary run but down to 3088 KiB in re-run 2; the same piece of code produced two different answers. This is not measurement error: the readahead window is adaptive, tuned by the kernel against the history of accesses. Our archive collected three runs including the re-runs precisely so you could see it wobble with your own eyes. So when engineering work cites NORMAL's readahead volume, trust nobody who reports a fixed constant; the same goes for our table here. There is one more that takes no advice and issues commands directly:

```text
   MADV_WILLNEED (8 MiB, no touch):
   before        :    0 KiB resident
   after WILLNEED: 8192 KiB resident (prefetch is async; polled until stable)
```

MADV_WILLNEED: we did not touch a single page, and 8 MiB entered the page cache on its own. Note, though, that the experiment marks it async: prefetching is asynchronous, and we polled mincore until the number stabilized before reading it; when you run it yourself, please wait as well, otherwise what you read is most likely a number that has not settled yet.

### (B) MADV_DONTNEED: anonymous pages read back as zeros, the data is gone

DONTNEED alone is the semantic exception stamped by the man page. We fill 8 MiB of anonymous pages with the `0xC0DE0000+i` pattern, then tell the first 4 MiB DONTNEED:

```text
...
   wrote 8 MiB pattern (0xC0DE0000+i), Rss = 8192 kB
   p[0]=0xc0de0000, p[1MiB]=0xc0e20000, p[5MiB]=0xc0f20000
   madvise(first 4 MiB, MADV_DONTNEED) rc=0, Rss = 4096 kB
   read back: p[0]=0x00000000 (zero page), p[1MiB]=0x00000000, p[5MiB]=0xc0f20000 (intact)
   mincore first 64 KiB: 1000000000000000
   mincore last 64 KiB:  1111111111111111
```

Rss fell from 8192 kB to 4096 kB; the physical pages were truly returned. We read p[0] again: all zeros come back, the pattern is gone. The man page's original words say subsequent accesses will obtain zero-fill-on-demand pages; the kernel has no obligation to keep a copy, and that is exactly how it behaves. The lone 1 in the first cell of the bitmap we explain in E5; it is a zero-page accounting matter. So DONTNEED is not a lossless release: it trades data for memory. Data that can be dropped and recomputed, we can afford to trade; computed results we cannot, so do not hand over that advice. For the symmetric operation on the file-page side, L02 covered the division of labor: shared mappings re-read the file, private mappings drop their COW copies, and E5 will add one more cut to it.

### (C) MADV_DONTFORK: after fork, the segment is gone in the child

We all know the child inherits the entire address space at fork, while MADV_DONTFORK insists on exempting the specified segment. In the experiment the parent prepares two pages: one tagged DONTFORK, the other an ordinary control; we write patterns into both and then fork:

```text
...
   parent: DONTFORK page 0x7e94794f6000 = 0x5a5a5a5a, COW page 0x7e94794f5000 = 0xa5a5a5a5
   child : DONTFORK page in maps? NO (gone) ; COW page in maps? yes
   child : COW page reads 0xa5a5a5a5, child writes 0x11 -> reads 0x00000011

[handler] SIGSEGV reading DONTFORK page, si_addr = 0x00007e94794f6000, si_code = 1 (SEGV_MAPERR)  <- 子进程里地址根本不在映射里
   child exited 83; parent after: DONTFORK page = 0x5a5a5a5a, COW page = 0xa5a5a5a5 (COW 各改各的)
```

In the child's maps, that page's whole segment is gone. Reading it delivers SEGV_MAPERR, the address simply is not in any mapping, neatly distinguished from the SEGV_ACCERR of stomping a PROT_NONE page. The control page behaves entirely as usual: the child reads the value the parent wrote, then writes its own COW copy, and parent and child never disturb each other. Who is this for? The man page points at DMA hardware; DMA stands for direct memory access, the kind of data transfer where a device bypasses the CPU and reads or writes physical memory directly. A COW relocation moves the page to a new physical address, so hardware DMA-ing straight to the old address would find nobody home. DONTFORK makes the child simply unable to see the segment, and the trouble of a changed owner disappears. Friends writing multi-process RDMA (remote DMA) programs — remote direct memory access is exactly this — will meet it again.

### (D) MADV_REMOVE: it really punches holes on ext4 too

Older material calls MADV_REMOVE exclusive to tmpfs/shmem; the current man page has changed its tune, saying that since Linux 3.5 any filesystem supporting fallocate's `FALLOC_FL_PUNCH_HOLE` mode accepts it, and its essence is to swap the mapped range for an fallocate punch-hole. Of course, we have heard the paper claim; next we verify by hand on ext4:

```text
...
   anon private : rc=-1 errno=22 (Invalid argument)
   ext4 shared  : 3 pages written; madvise rc=0 errno=0; pread middle all-zero; st_blocks 24 -> 16, size=12288 (hole punched, KEEP_SIZE)
   tmpfs shared: 3 pages 'a','b','c'; size=12288, st_blocks=24 (512B units), shm f_bfree=6940363
   madvise(middle page, MADV_REMOVE) rc=0 errno=0
   after punch: size=12288 (unchanged); st_blocks 24 -> 16 (page freed), then read hole via mapping -> st_blocks back to 24 (shmem 立了零页)
   map page0='a' page1=0x00 page2='c'; pread middle page reads all-zero (hole)
```

The private anonymous segment gets EINVAL outright; there is nothing behind it to punch holes into. On an ext4 shared mapping we fill three pages with abc; after issuing REMOVE on the middle page, `pread` reads back all zeros, st_blocks drops from 24 to 16 (st_blocks counts in 512-byte units, and one page is exactly 8), while the file size does not budge; the hole is solidly punched. On tmpfs we took one extra look at the aftermath: reading the hole once more through the mapping, shmem erects a zero page for it again and the block count bounces back to 24. So the hole-punching is real, and reading the hole costs money again; why it costs money is the same accounting divergence we will meet in E5.

## E3: mlock — keeping pages in physical memory

The third API governs residency. When memory gets tight, the kernel swaps inactive pages out to swap, and what that buys is millisecond-scale disk latency on the next access; for those of us doing real-time audio, that latency lands in the ear as a glitch. And if a key page gets swapped out, its content ends up in the swap area, which is a leak surface. mlock's promise is written in man 2 mlock: when the call returns successfully, every page covered by that address range is guaranteed to be in RAM, and stays there until unlocked. We split this layer into four observable facets and measure them one by one.

The very first facet comes with a surprise: mlock did all the faulting by itself. We prepare an 8-page anonymous mapping without ever writing a byte, and mincore's bitmap reads `00000000`. One call of `mlock(p, 2 * kPage)` and the bitmap becomes `11000000`, with `VmLck` and smaps' `Locked` both rising to 8 kB:

```text
   before mlock  bitmap: 00000000
   VmLck=0 kB, smaps Locked=0 kB
   mlock(p, 2 pages) rc=0  -- pages still never written by us:
   after mlock   bitmap: 11000000
   VmLck=8 kB, smaps Locked=8 kB  <- mlock 自己把页摸进了内存
   p[0]=0x7f written; VmLck=8 kB (unchanged)
...
```

On reflection, it hardly counts as a side effect: the promise says residency upon successful return, so if it did not pre-fault, how would it honor the promise? mlock simply pays the two page faults in advance. If you would rather it not touch them now, the kernel offers another variant, `mlock2()` with `MLOCK_ONFAULT`: pages already in memory get locked as usual, and pages not yet in get locked the moment they land, never to leave again. The second facet is munlock's sense of proportion: after unlocking, the bitmap still reads `11000000`, not one page has left, and `VmLck` has returned to zero. What unlocking retracts is the no-swap-out promise, not the pages' residency; the eviction timing is decided by the kernel watching memory pressure.

The third facet is about quota. The machine's `RLIMIT_MEMLOCK` is 64 MiB, and we measure right against the line:

```text
...
   mlock(limit + 1 page = 67112960 B): rc=-1 errno=12 (Cannot allocate memory)
   mlock(limit exactly = 67108864 B): rc=0 errno=0 (Success)
   VmLck=65536 kB
   one more page on top of a full limit: rc=-1 errno=12 (Cannot allocate memory)
...
```

The call asking for exactly the limit succeeded. The one asking for one page more got ENOMEM back; errno 12's literal meaning is out of memory, but here the real meaning is that the locking quota is used up. The error text and the ailment do not match, so do not let it lead you astray while debugging. With the lock quota full, trying to add one more page gets the same ENOMEM; the quota is counted as cumulative locked bytes, not per call. The man page further notes that since 2.6.9 the soft limit only binds unprivileged processes; processes holding CAP_IPC_LOCK are unrestricted. The fourth facet is mlockall: a single `mlockall(MCL_CURRENT)` locks the entire current address space; this process's RSS is a mere 4 MB, far within the limit, rc comes back 0, and `VmLck` rises to 7296 kB. Last to mention is `MCL_FUTURE`, which locks future mappings; the man page attaches a warning to it: subsequent mmap and sbrk calls may fail outright due to quota, and if stack growth fails there is not even an error code, just a SIGSEGV. Unless you are sure, do not turn this one on.

We record the local swap situation as it stands: in /proc/swaps the 16 GiB /dev/sdc has not one page in use, with priority and swappiness at -2 and 60 respectively. Locked pages are never swapped out; that is mlock's entire reason for existence, and on this machine that sentence cannot be demonstrated deterministically: forcing the kernel to swap requires root to touch drop_caches, or manufacturing dangerous watermark pressure. In the experiment archive we recorded things as they were; we did not fabricate the scene.

## E4: guarded_buffer\<T\> — reporting the overflow byte index on the spot

We have seen each of the three APIs' tricks; now we assemble them into a handy tool. Lay the problem out: `char d[100]` sits on an ordinary heap, and we write `d[100] = 'X'`, overflowing by exactly one byte. Guess what happens? Most likely nothing at all: the write lands in a neighbor's byte without a sound. ASan (AddressSanitizer, a compiler-instrumented memory error detector) can catch it, but today we hand-build a first-principles version: make the byte d[100] itself not exist. The layout recipe in one sentence: for N bytes, we allocate `ceil(N/page)` pages of data plus 1 page of PROT_NONE guard, and then **align the data's last byte to the guard page's start**.

```cpp
// e4.cpp 的 guarded_buffer 节选:错误分支打印从略
template <class T>
class guarded_buffer {
public:
    explicit guarded_buffer(std::size_t n)
        : n_(n), bytes_(n * sizeof(T)),
          payload_((bytes_ + kPage - 1) / kPage * kPage)
    {
        base_ = static_cast<unsigned char*>(
            ::mmap(nullptr, payload_ + kPage, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        if (base_ == MAP_FAILED) { std::printf("mmap failed\n"); std::abort(); }
        guard_ = base_ + payload_;                       // 尾页整页降权
        if (::mprotect(guard_, kPage, PROT_NONE) != 0) { std::abort(); }
        data_ = reinterpret_cast<T*>(guard_) - n;        // 数据末字节紧贴 guard 页首
    }
    ~guarded_buffer() { ::munmap(base_, payload_ + kPage); }

    guarded_buffer(const guarded_buffer&) = delete;
    guarded_buffer& operator=(const guarded_buffer&) = delete;

    T* data() const noexcept { return data_; }
    std::size_t size() const noexcept { return n_; }
    // guard_begin()/guard_end() 交给 handler 做区间对照,略
private:
    std::size_t n_, bytes_, payload_;
    unsigned char* base_ = nullptr;
    unsigned char* guard_ = nullptr;
    T* data_ = nullptr;
};
```

Why must it be tail-aligned? Lay it the other way round and it becomes clear: if the data were placed aligned to the page start, 3996 legal bytes would remain after the 100, all lying in the same page; writing `d[100]` would be perfectly comfortable, the guard page still far away, and not one byte would report. With tail alignment, `data[N]` is exactly the guard page's first byte; however deep the overflow, it lands at the corresponding depth inside the guard, and E1 just verified for us that si_addr is byte-exact. That gives the handler room to perform: compute `si_addr - guard_begin + 1`, and the overflow byte index reports itself. Here it is running:

```text
== construct guarded_buffer<char>(100) ==
   data  = [0x71d77caf1f9c, 0x71d77caf2000) 100 bytes, 末字节紧贴 guard 页首
   guard = [0x71d77caf2000, 0x71d77caf3000) PROT_NONE
...
== overflow by exactly 1 byte: d[100] = 'X' ==
[handler] SIGSEGV, si_addr = 0x000071d77caf2000, si_code = 2 (SEGV_ACCERR)
[handler] guard = [0x000071d77caf2000, 0x000071d77caf3000), si_addr IN guard, 越界第 1 / 4096 字节; requested address = 0x000071d77caf2000 == si_addr (精确落点)
   caught it, process alive, d[99] still 'Z'
== overflow 1 page deep: d[100+4095] ==
[handler] SIGSEGV, si_addr = 0x000071d77caf2fff, si_code = 2 (SEGV_ACCERR)
[handler] guard = [0x000071d77caf2000, 0x000071d77caf3000), si_addr IN guard, 越界第 4096 / 4096 字节; requested address = 0x000071d77caf2fff == si_addr (精确落点)
   caught at guard's last byte
== overflow READ: c = d[100+3] ==
[handler] SIGSEGV, si_addr = 0x000071d77caf2003, si_code = 2 (SEGV_ACCERR)
[handler] guard = [0x000071d77caf2000, 0x000071d77caf3000), si_addr IN guard, 越界第 4 / 4096 字节; requested address = 0x000071d77caf2003 == si_addr (精确落点)
   reads are guarded too
...
   di[9]=81 ok
[handler] SIGSEGV, si_addr = 0x000071d77caf0000, si_code = 2 (SEGV_ACCERR)
[handler] guard = [0x000071d77caf0000, 0x000071d77caf1000), si_addr IN guard, 越界第 1 / 4096 字节; requested address = 0x000071d77caf0000 == si_addr (精确落点)
   caught: di[10] faults at guard start
== underflow d[-1]: NOT covered by a rear-only guard ==
   no signal -- 前向不设防,这就是本设计的边界(前后双 guard 可补)
...
== control group: same layout WITHOUT guard page ==
   ctrl buffer at 0x71d77caedf9c, ctrl[100] lands at 0x71d77caee000 (page 2, rw-p)
   ctrl[100]='X' -- no signal, value read back: 'X' (silent corruption)
   ctrl[100+4095]='Y' -- still no signal
...
```

Let us read out the most valuable lines of the output. `d[100]` overflows by a single byte, si_addr exactly equals the requested address, and the handler reports overflow byte 1/4096. On the one-page-deep overflow, the handler reports 4096/4096. The read overflow gets intercepted just the same, reported as 4/4096. When the `guarded_buffer<int>` version takes its turn, `di[10]` is a 4-byte int whose first byte is exactly the guard's first byte, and it still gets caught. d[99] meanwhile keeps holding 'Z'; not one legal byte was harmed. We also lay both boundaries in the open: the guard is only 1 page deep, so an overflow beyond 4096 bytes steps across it and is no longer its business. The underflow `d[-1]` lands in the preceding data page, a live mapping, and precisely no signal arrives; a one-way guard cannot cover the negative direction. A double-guard version with one page on each side can patch this, and we leave the change to you as an exercise.

We did not skip the control group either: the same layout without a guard page, overflowed by 1 byte and by 4095 bytes just the same, passed silently with values readable back; that is the fate of silent corruption. One more engineering detail is worth a look: the handler does not let the process die; `sigsetjmp`/`siglongjmp` (savemask=1) wires the flow of execution back to the main path, and the following scenes play out in the same process. Allocator guard bands and the Electric Fence generation of debug allocators use exactly this move: glue a PROT_NONE page onto the end of every allocation, and overflow turns from silent corruption into a guaranteed crash with a location. The pages obtained from mmap, the guard page demoted by mprotect, and the overflow position reported by the handler: the three building blocks of this tool are all in place now.

## E5: mincore — one residency bitmap, three notions of it

E2 and E3 have already used it to take measurements repeatedly; now we lay out its signature: `mincore(addr, length, vec)` leaves one byte in vec for every page in the range. In man 2 mincore's words, when the lowest bit is 1, the page is currently resident in memory, and touching it again will not hit the disk. The sentence looks unremarkable on its face, so we photograph one anonymous mapping's whole life into a series of consecutive snapshots:

```text
== anonymous 8 pages, page by page ==
   1) fresh mmap, untouched:          00000000  (0/8 resident, Rss=20 kB)
   2) wrote pages 0,3,7:              10010001  (3/8 resident, Rss=32 kB)
   read page 1 (never written) -> 0x00000000
   3) after READ of page 1:           11010001  (4/8 resident, Rss=32 kB)
   4) after DONTNEED page 3:          11000001  (3/8 resident, Rss=28 kB)
   5) after READ page 3:              11010001  (4/8 resident, Rss=28 kB)
...
```

Row 3 is the author's favorite anomaly in this article: we only read page 1, never wrote it, its bit turned to 1 on the bitmap, yet Rss did not rise by a single cell. This brings us to the zero page: the kernel keeps one shared read-only all-zero page, and every never-written page of an anonymous mapping gets mapped onto it when read, so nobody needs a real page of memory. mincore counts the present bit of the PTE (page table entry), and the zero page counts as present; but smaps' Rss counts only physical pages actually assigned to this VMA, and the shared zero page does not count. So one single read yields two answers from two instruments; this is the accounting fork. We record both numbers and do not adjudicate which is right. Rows 4 and 5 then echo E2's (B): after DONTNEED the bitmap went back to 0; we read again, and the zero page re-erects the present bit.

File-backed pages have a second notion of it. We prepare a 2-page file mapping, squeeze the page cache clean, prefetch with WILLNEED, and then drop segment by segment:

```text
== file-backed 2 pages (ext4) ==
   1) after fadvise DONTNEED:         00  (0/2 resident, Rss=0 kB)
   2) after MADV_WILLNEED:            11  (2/2 resident, Rss=0 kB)
   page 0 reads 0xaaaaaaaa (now faulted into OUR page table, Rss grows)
   3) after reading page 0:           11  (2/2 resident, Rss=8 kB)
   4) DONTNEED page 1:                11  (2/2 resident, Rss=4 kB)
   5) fadvise DONTNEED again:         10  (1/2 resident, Rss=4 kB)
...
```

Look at row 2: mincore is all 1s while Rss=0. For file mappings, mincore asks the page cache; once the page is in the cache it reports 1, even if the page has not yet faulted into this process's page table. The man page's claim that touching again will not hit the disk holds here; but the parentheses put page fault and disk access side by side, and the measured behavior is that the page still faults once, only that this fault does not touch the disk. Row 4 verifies it once more from the file-page side: when we DONTNEED a file page, what is dropped is only this process's page-table mapping, while the file page remains in the page cache, so mincore still reads 1. To truly evict a file page from the page cache, we need the `posix_fadvise` (file-level) channel, and row 5 shows it arriving, yet it evicts only one page: page 0 is still mapped in this process's page table, protected by its PTE, so the page cache cannot evict it; page 1 has no mapping and gets evicted. So mincore's 1 has at least three origins: PTE present (the zero page included), presence in the page cache, and genuine residency. When you cross-check with it, always keep an smaps Rss standing beside it; otherwise that 1 cannot explain what it is.

## E6: process_vm_readv — a cross-process peek

We dispatch the last API in one paragraph. It is a corner of the whole landscape, but one that often gets missed. When process A wants to read a stretch of process B's memory, the old way was ptrace; since Linux 3.2 there is a dedicated read-only channel, `process_vm_readv`, taking one iovec pair for the local side and one for the remote:

```cpp
// e6.cpp 节选:只留调用三行
struct iovec local_iov { local, sizeof local };
struct iovec remote_iov { g_payload, sizeof g_payload }; // fork 继承,地址同值
const ssize_t n = ::process_vm_readv(pid, &local_iov, 1, &remote_iov, 1, 0);
```

```text
parent read child's memory: 32 bytes ->
   child  has [0xc0de0000 0xc0de0001 .. 0xc0de0007]
   parent has [0x00000000 0x00000001 .. 0x00000007] (untouched, COW)
read unmapped remote addr : rc=-1 errno=14 (Bad address)
read bogus pid            : rc=-1 errno=3 (No such process)
```

After the fork, the child rewrote its global array into the 0xC0DE pattern; the parent's copy stayed untouched under COW protection, and a single process_vm_readv copied those 32 bytes back from the child. The error paths deserve a glance too: an unmapped remote address yields EFAULT (14); a nonexistent pid yields ESRCH (3). The man page states that the permission check uses the same scheme as ptrace (PTRACE_MODE_ATTACH_REALCREDS), so same-uid or privileged callers only. Nor is it atomic: the return may be smaller than requested, and page-granular partial transfers exist, so please check the return value when using it. Crash collectors and debuggers use it strictly for read-only reconnaissance. And when we truly want processes to share data in earnest, what do we rely on? The mainstay is still shared memory, which is the next article's subject.

## How the other side sees it

Windows' mirror of this is VirtualProtect, with PROT_NONE corresponding to PAGE_NOACCESS, and our guarded_buffer ports over as is. But Windows has one extra mode we lack: PAGE_GUARD, a one-shot trap. A page wearing the guard bit raises a STATUS_GUARD_PAGE_VIOLATION exception on its first access, after which the guard bit removes itself, the page becomes an ordinary accessible page, and the next access sails through. If we want another trap, you must call VirtualProtect again to put the bit back. This and PROT_NONE's persistent refusal are two different temperaments: our guard page in this article still SIGSEGVs on the spot at the ten-thousandth access, while PAGE_GUARD lets everything through after one bang. The Windows kernel itself uses it for stack-extension probing. Mapped onto guarded_buffer, the PAGE_NOACCESS version behaves exactly as E4 did, while the PAGE_GUARD version has to handle the loop of re-arming the bit after each exception. The full development of this comparison we leave to the Windows memory article, named [VirtualAlloc and VirtualProtect](../../windows/memory/01-virtualalloc.md). VirtualAlloc's granularity and its reserve/commit two-phase model are waiting for you there, and our measurements of the file-backed SEC_RESERVE variant live in the [file mapping article](../../windows/file-io/02-file-mapping.md).

<ReferenceCard title="Reference Resources">
  <ReferenceItem
    :id="1"
    title="mprotect(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/mprotect.2.html"
  />
  <ReferenceItem
    :id="2"
    title="madvise(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/madvise.2.html"
  />
  <ReferenceItem
    :id="3"
    title="mlock(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/mlock.2.html"
  />
  <ReferenceItem
    :id="4"
    title="mincore(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/mincore.2.html"
  />
  <ReferenceItem
    :id="5"
    title="process_vm_readv(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/process_vm_readv.2.html"
  />
  <ReferenceItem
    :id="6"
    title="sigaction(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/sigaction.2.html"
  />
  <ReferenceItem
    :id="7"
    author="Michael Kerrisk"
    title="The Linux Programming Interface"
    publisher="No Starch Press"
    :year="2010"
    url="https://man7.org/tlpi/"
  />
</ReferenceCard>
