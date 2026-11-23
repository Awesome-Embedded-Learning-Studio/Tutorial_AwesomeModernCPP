---
title: "mmap Memory Mapping: Attaching a File to the Address Space"
description: "mmap attaches a file directly into the address space, so reading a file becomes dereferencing a pointer, and read's copy from the page cache to the user buffer is skipped entirely; this article matches each of the six parameters to its behavioral consequences, manages the mapping with mapped_region under RAII, and walks through experiments covering the unit price of a page fault, /proc/self/maps, SIGBUS, mprotect's two kinds of si_code, cross-process visibility, Dirty and copy-on-write, all the way to a 512 MiB sequential read — where two machines delivered opposite verdicts"
chapter: 8
order: 2
platform: host
difficulty: advanced
cpp_standard: [20, 23]
reading_time_minutes: 32
prerequisites:
  - "POSIX 文件 I/O:open/read/write 与 fd 的一生"
related:
  - "文件映射:CreateFileMapping 与 MapViewOfFile"
tags:
  - host
  - cpp-modern
  - advanced
  - 系统编程
  - POSIX
  - 内存管理
  - 优化
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
translation:
  source: documents/vol8-domains/systems-programming/linux/file-io/02-mmap-memory-mapping.md
  source_hash: a7209a878a5301e1f2e431b1cb976c402cd50c1abf152695cc5f0805c20343cb
  translated_at: '2026-11-22T16:13:00+00:00'
  engine: glm-5.3
  token_count: 34796
---
# mmap Memory Mapping: Attaching a File to the Address Space

The previous article walked an fd through its whole life; this one looks back at where `read`'s cost actually goes. The data you want travels from the file to your variable across two hauls: the kernel brings the file's contents into the **page cache**, and `read` then copies that kernel memory over to you. Think it through: when reading a large file sequentially, the second copy is memory bandwidth burned for nothing, and random access loses even more — every few dozen bytes you touch costs a system call. mmap offers another path: **have the kernel attach the file's pages directly into your address space**. What your pointer dereferences is the page cache itself, and the copying work is skipped entirely.

Better still, the moment `mmap` establishes a mapping, it has **done almost nothing**: not one page of data read, not one byte of memory consumed — just a note registered in the kernel saying "this address range is legal, and the file behind it is that one." The real hauling waits until you first touch a page, and that event is the **page fault**. Lazy loading combined with zero copy feeds the dynamic linker and the databases, and every program that "uses a file as an array" — all standing on the same foundation. In this article we look at the six parameters one by one, hand the release of mappings to RAII, and then line up a series of experiments: how expensive a page fault is, what happens when the file is truncated behind your back, how two processes share the same file, all the way to the mmap-versus-read verdict in a 512 MiB sequential read.

Let's state the experiment environment clearly, since the numbers later all check against it: the first batch of experiments (page faults, SIGBUS, copy-on-write) came from the author's desktop with an AMD Ryzen 7 9700X; that batch's code was not archived, and only the numbers remain in the text. The later additions — mprotect, `/proc/self/maps`, cross-process visibility, and the Dirty counter — plus re-runs of the first three experiments on a laptop, used an i7-13700H machine, with the code and all raw outputs stored in the repository under `code/volumn_codes/vol8/systems-programming/linux/file-io/02-mmap-memory-mapping/`. The 512 MiB benchmark ran once on each machine; which round belongs to which machine is detailed in the benchmark section. Both sides are WSL2 environments, both kernels the same build of 6.18.33.2-microsoft-standard-WSL2, and both g++ the same 16.2.1. Numbers are labeled with their machine wherever they appear. The three tools `unique_fd`, `sys_call`, and `errno_code` follow the definitions from the two thinking-foundation articles ([the RAII article](../../thinking/01-raii-paradigm.md) and [the error-handling article](../../thinking/02-error-paradigm.md)).

## The six parameters of mmap(): each one carries behavioral consequences

```c
void *mmap(void addr[.length], size_t length, int prot, int flags,
           int fd, off_t offset);
int munmap(void addr[.length], size_t length);
```

Counting down from the signature. `addr` is essentially always passed as `nullptr`, letting the kernel pick a page-aligned free address itself — man 2 mmap calls this the most portable approach. If you insist on passing something non-null, it is merely a hint once it reaches the kernel: provided the address is not below `/proc/sys/vm/mmap_min_addr`, the kernel will try to place the mapping at a nearby page boundary; if a mapping already sits there, it picks another, and the address it picks may have nothing to do with your hint.

What promotes `addr` from hint to command is `MAP_FIXED`: the mapping lands exactly at the address you give, and if the target range overlaps an existing mapping, the overlapping part is **discarded outright**, without so much as a notification. The NOTES in the man page admit it themselves: the only safe use of this flag is on an address range reserved beforehand. A multithreaded program that uses it carelessly can tear out libc's mapping or some thread's stack, and nobody will warn you. Since Linux 4.17 there is the far better-behaved `MAP_FIXED_NOREPLACE`: it equally forces the address, but never overwrites an existing mapping — if the range is taken, it fails with errno `EEXIST`. When two threads race for the same range, the winner gets it and the loser receives EEXIST. On old kernels that do not recognize the flag, it degrades to plain hint semantics and returns a different address, so we always compare the returned value against the requested address before trusting it. Let's also nominate a relative: `mremap` can extend, shrink, or even relocate an existing mapping in place; sadly POSIX has no place for it — one glance at man 2 mremap gives you all the details.

`length` is a byte count, and the kernel rounds it up to a page multiple for you. The rounding brings a boundary phenomenon: when the mapping covers the end of the file, the partial page past the end reads as zeros, and writing to it never writes back to the file. The details of this zero-filling, plus one rather counterintuitive exception to it, wait for the mapped_region section below.

`prot` is the chosen tier from `PROT_READ`/`PROT_WRITE`/`PROT_EXEC`/`PROT_NONE`, and it must not clash with the open mode. This restriction, though, only governs `MAP_SHARED`: pair an O_RDONLY fd with `PROT_WRITE` plus `MAP_SHARED`, and the kernel answers with EACCES outright. Switch to `MAP_PRIVATE` and the restriction no longer applies — a read-only fd can still build a writable private mapping; what you write is merely the copy produced by copy-on-write.

Here we must patch what the previous article left unsaid: in man 2 mmap's ERRORS, EACCES does not hang only on `MAP_SHARED`; its full condition is any of four: the fd refers to something other than a regular file, or you want a file mapping but the fd was not opened for reading, or `MAP_SHARED` is combined with `PROT_WRITE` while the fd is not O_RDWR, or the file is append-only and you requested `PROT_WRITE`. The case we care about — "a read-only fd that wants to write" — hits only the third clause, and its live test sits at step 5 of the mprotect section.

`flags` decides the semantics; let's take them one by one. **MAP_SHARED**: your writes land directly on the file's page-cache pages, immediately visible to other processes mapping the same file, with the kernel choosing when to write back. **MAP_PRIVATE** goes the copy-on-write route: your modifications land on a private copy and never enter the file. That is why a read-only fd with `PROT_WRITE` building a private mapping is entirely legal — the relocations the dynamic linker applies to a .so use exactly this move.

The remaining entries we walk faster; just remember the names `MAP_ANONYMOUS` and `MAP_POPULATE` for now. `MAP_ANONYMOUS` is an anonymous mapping with no file attached — its entanglement with malloc waits for the memory-management article. `MAP_POPULATE` (since kernel 2.5.46) prefills the page table and triggers readahead for you, moving the page-fault cost into the moment of the mmap call. `fd` is the one opened in the previous article, and man 2 mmap says it plainly: closing the fd right after the mapping is established does not affect the existing mapping. Still, we keep the `unique_fd` hanging around for peace of mind. `offset` must be a multiple of the page size. And don't hardcode 4096 for the page size either — asking `sysconf(_SC_PAGE_SIZE)` at runtime is the reliable way.

::: warning The failure return value is MAP_FAILED
When mmap fails, it returns `MAP_FAILED` (that is, `(void *) -1`) and sets errno — **not nullptr**. `if (p)` cannot detect the failure; we must write `if (p == MAP_FAILED)`. The `result == -1` check in `sys_call` from [the error-handling article](../../thinking/02-error-paradigm.md) does not even compile here: mmap returns a pointer, and you cannot compare a `void*` directly against the integer `-1`. So we hand-write the check here; you will see it in `mapped_region` below.
:::

## mapped_region: writing munmap into the destructor

A mapping belongs to the same class of resource as an fd: once acquired, it must be released, and a missing `munmap` is a leak of address space. The previous article already demonstrated how `unique_fd` writes close into the destructor; the skeleton here is exactly the same — the move-only approach, the destructor delegating to reset — only the managed object changes from an fd to an address range. The spots that deserve a second look are discussed after the code.

```cpp
class mapped_region {
public:
    mapped_region() noexcept = default;

    mapped_region(const unique_fd& fd, std::size_t length, int prot, int flags,
                  off_t offset = 0)
    {
        void* p = ::mmap(nullptr, length, prot, flags, fd.get(), offset);
        if (p == MAP_FAILED) {
            throw std::system_error{errno_code(), "mmap"}; // 返回 void*,整数 -1 检查编不过,须判 MAP_FAILED
        }
        addr_ = static_cast<unsigned char*>(p);
        const std::size_t page = static_cast<std::size_t>(::sysconf(_SC_PAGE_SIZE));
        length_ = (length + page - 1) / page * page; // 内核按页取整,记录真实映射长度
    }

    mapped_region(mapped_region&& other) noexcept
        : addr_(other.addr_), length_(other.length_)
    {
        other.addr_ = nullptr;
        other.length_ = 0;
    }

    mapped_region& operator=(mapped_region&& other) noexcept
    {
        if (this != &other) {
            reset();
            addr_ = std::exchange(other.addr_, nullptr);
            length_ = std::exchange(other.length_, 0);
        }
        return *this;
    }

    ~mapped_region() { reset(); }

    void reset() noexcept
    {
        if (addr_ != nullptr) {
            ::munmap(addr_, length_);
            addr_ = nullptr;
            length_ = 0;
        }
    }

    unsigned char* data() const noexcept { return addr_; }
    std::size_t size() const noexcept { return length_; } // 向上取整后的映射长度
    explicit operator bool() const noexcept { return addr_ != nullptr; }

    mapped_region(const mapped_region&) = delete;
    mapped_region& operator=(const mapped_region&) = delete;

private:
    unsigned char* addr_ = nullptr;
    std::size_t length_ = 0;
};
```

Please note `size()`: it returns the mapping length **after round-up**, not the length you passed in. munmap wants that same length too, so we simply compute and record it ourselves — that is the purpose of the division line in the constructor. What sits inside the rounded-up tail depends on where the file's end falls, not on the rounding boundary. When the file's length is not a page multiple and the mapping covers the file's end, the stretch from end-of-file to the rounding boundary reads as all zeros, and the file will never know you wrote to it — this is the zero-filling promised above. Flip it around: when length is smaller than the file and only a prefix is mapped (a common pattern on large files), the tail still reads real file data.

Zero-filling comes with an extra caution from the man page's BUGS section that we must append: that half page you wrote past EOF — the data never enters the file, yet it **lingers in the page cache**, and a later mapping of the same file may see what you changed. In some cases, running `msync` ahead of munmap can flush the residue, but on tmpfs it is beyond rescue. If you build cache-visibility assumptions on "writes that go nowhere," it is your own reasoning that will capsize.

That settles the tail; what about reaching out beyond it? Truly stepping past `size()` means leaving the mapping's designated range: step on an unmapped page, and the kernel hands you a SIGSEGV. But if a neighboring mapping happens to sit right next door, you get no signal at all — you read somebody else's data, which is actually worse. Why should an out-of-bounds access produce no signal? We note the question down. SIGBUS governs a different matter: pages of the mapping falling wholly beyond the file's end, or the file being truncated behind your back — we test it live in the SIGBUS section.

## Page faults: at the moment the mapping is established, nothing has happened

The moment mmap returns, the process has gained nothing but the registration of one **VMA** (virtual memory area) — not one page of data has moved. On every memory access, the kernel looks the address up in the registry: if the address falls inside some VMA and the permissions suffice, the corresponding page is hooked into your page table. Only when the address lies in no VMA at all does SIGSEGV get its cue. The question left in the previous section — why stepping out of the mapping's range next to a neighboring mapping produces no signal — is answered right here: the kernel only knows which VMA an address falls in; it does not remember the length you passed to mmap. A pointer walking out of your registered range and into the neighbor's VMA makes an access that is perfectly legal in the kernel's eyes, and what you read is, naturally, somebody else's data.

Page faults themselves fall into two classes, and the sorting criterion is a single question: does this fault touch the disk? One that must wait for I/O to read the page in from disk is a **major fault** — an expensive event. One where the page is already in memory and no disk wait is needed is a **minor fault**, soft fault being its nickname. The kernel counts them per process: the 10th field of `/proc/self/stat` is minflt, with all field numbers laid out in proc_pid_stat(5), and it is exactly what the experiment below reads. The experiment file lives in the page cache, so what we measure is the pure cost of fault handling; the first touch of a cold file on real disk is a major fault, with the disk I/O still to wait for.

Let's measure for real: create a 64 MiB file, and once the mapping is up, touch one byte per page, three passes in a row, reading the minor-fault count straight from `/proc/self/stat`.

```cpp
// fault_cost.cpp(节选):三趟"每页摸一个字节",计时与打印的辅助函数略
mapped_region region(fd, 64u << 20, PROT_READ, MAP_PRIVATE);

fault_counts f0 = self_faults();
unsigned long s1 = touch_pages(region.data(), region.size(), page); // 第一趟,缺页
fault_counts f1 = self_faults();
unsigned long s2 = touch_pages(region.data(), region.size(), page); // 第二趟,页都在
::madvise(region.data(), region.size(), MADV_DONTNEED);             // 丢页,人造第三次首次
unsigned long s3 = touch_pages(region.data(), region.size(), page); // 缺页应声回来
```

```text
$ ./fault_cost
page size     : 4096, mapped 16384 pages
first touch   :   7.20 ms, minor faults  1028, sum 2084688
second touch  :   0.15 ms, minor faults     0, sum 2084688
after DONTNEED:   4.97 ms, minor faults  1024, sum 2084688
```

Lay the three passes out: the first 7.20 ms (about 0.44 µs per page), the second 0.15 ms (about 9 ns per page) — nearly fifty times apart. After `MADV_DONTNEED` dropped the pages, both the time and the fault count came right back. One more number deserves a second look: 16384 pages produced only about 1028 faults. Why so frugal? The kernel has the fault-around optimization: while handling one fault, it goes ahead and maps in the ~16 nearby file pages (64 KiB), pressing the fault count down to about one-sixteenth. That 16 is not a guess either — the `fault_around_bytes` knob in the kernel's mm/memory.c is set to 64 KiB. This also explains what mmap means for random access: **the only thing hauled into memory is the neighborhood you touched**. read, by contrast, either hauls the whole block in ahead of time or reads fragment by fragment with each syscall.

> That output came from the author's desktop. The author later ran the same program several times on the laptop (i7-13700H): first touch 1.42 ms, second 0.25 ms, and the fault counts on the laptop came out 1027/0/1024 every run, while the desktop's first touch was 1028. The absolute durations differ fivefold, but both machines nod to the same fact: the first touch is clearly the expensive one. One more tool worth noting in passing: `mincore(2)` can query which pages of a virtual memory range actually reside in physical memory — a natural instrument when we run fault-versus-residency experiments; we meet it in the memory-management article.

## Seeing the mappings through /proc/self/maps

VMA sounds like the kernel's private business, but it is laid out entirely in one file: `/proc/self/maps`, one line per mapping, readable like any plain text file. We write a program that builds two views of the same file — one `MAP_SHARED` starting at offset 0, the other `MAP_PRIVATE` starting at offset 8 KiB — plus one view of a real-disk ext4 file as the control, then filter by path and print only our own lines:

```cpp
// e3.cpp(节选):read_maps 用 ifstream 逐行读 /proc/self/maps,
// print_matching 按路径子串过滤打印,单线程进程,maps 静止可放心逐行读
mapped_region shared(fd, 2 * page, PROT_READ | PROT_WRITE, MAP_SHARED, 0);
mapped_region priv(fd, 2 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE,
                   static_cast<off_t>(2 * page)); // 偏移 8 KiB

auto before = read_maps();
std::print("maps lines total: {} (before any mmap of the file)\n", before.size());

auto after = read_maps();
std::print("maps lines total: {} (after 3 mmaps, +{})\n", after.size(),
           after.size() - before.size());
print_matching(after, "e3data.bin", "  file-related: ");
```

```text
$ ./e3
maps lines total: 41 (before any mmap of the file)
mapped SHARED 2 pages at 0x77d7b60db000 (offset 0)
mapped PRIVATE 2 pages at 0x77d7b60d9000 (offset 8K)
maps lines total: 44 (after 3 mmaps, +3)
  file-related: 77d7b60d9000-77d7b60db000 rw-p 00002000 00:49 4657                       /tmp/l02_exps/e3_proc_maps/e3data.bin
  file-related: 77d7b60db000-77d7b60dd000 rw-s 00000000 00:49 4657                       /tmp/l02_exps/e3_proc_maps/e3data.bin
  ext4-backed : 77d7b60d7000-77d7b60d9000 r--p 00000000 08:30 660959                     /home/charliechen/l02_scratch/e3ext.bin

[same file, anonymous/bss side of the world]
  heap  : 5d798897e000-5d79889b1000 rw-p 00000000 00:00 0                          [heap]
  stack : 7ffff688f000-7ffff68b1000 rw-p 00000000 00:00 0                          [stack]

after munmap(SHARED): maps lines total: 43 (-1), SHARED line gone, PRIVATE stays
  file-related: 77d7b60d9000-77d7b60db000 rw-p 00002000 00:49 4657                       /tmp/l02_exps/e3_proc_maps/e3data.bin
```

Each line has six columns; take the two file-related lines as specimens: the **address range**, **permissions**, and **offset within the file**, followed by the **device number**, **inode**, and **path**. Both lines correspond to the same file, so both inodes are 4657 and both devices 00:49. Only two spots differ. The offset in column 3: one line reads 00002000 and the other 00000000 — exactly the offsets we passed to mmap. And the last character of column 2: one line ends in p, the other in s. The first three characters of the perms column are r/w/x; the fourth is s for shared or p for private (copy-on-write), corresponding exactly to `MAP_SHARED` and `MAP_PRIVATE`. The device-number column has its own story: the ext4 control line shows 08:30, which is hexadecimal — decimal 8:48, precisely the device number of `/dev/sdd`. tmpfs's 00:49 is its own anonymous block-device number, carrying no block-device meaning. As for the `[heap]` and `[stack]` lines, the device number is plainly 00:00 and the inode 0 — territory with no file attached. Please also check the line-count arithmetic: the total grew from 41 to 44, exactly three lines for three mmaps. munmap ran once, and exactly one line went away. The total itself varies with the runtime libraries and environment variables — when you run it, it most likely will not be 41, so don't treat it as a constant.

> One small observation is left for you: if you delete the underlying file after the mapping is established, the line's path gains a `(deleted)` suffix at the end, while the mapping itself keeps working. Had the SIGBUS section's experiment swapped "truncate" for "delete," this is what maps would have shown.

## SIGBUS: the file got truncated behind your back

The mapping length is what you declared to mmap; the kernel does not watch the file's size for you. After the mapping is established, the file can perfectly well be truncated by someone: log rotation, in-place compression, another process's slip of a ftruncate — all of these have happened in the real world. Touch the out-of-bounds region afterwards, and man 2 mmap's ERRORS section says it plainly: **Attempted access to a page of the buffer that lies beyond the end of the mapped file — the delivery is a SIGBUS**. That out-of-bounds touch yields no return value and no errno; the kernel delivers the SIGBUS signal straight into your process, and its default handling terminates the process on the spot. We install a handler and reproduce it once:

```cpp
// sigbus.cpp(节选):write_all/write_hex 是 write(2) 的小封装,完整程序在 WSL 编译运行
void on_sigbus(int, siginfo_t* info, void*)
{
    write_all("\n[handler] caught SIGBUS, faulting address = ");
    write_hex(reinterpret_cast<std::uintptr_t>(info->si_addr)); // write_hex 自带 0x 前缀
    write_all(info->si_code == BUS_ADRERR ? ", si_code = BUS_ADRERR\n" : "\n");
    write_all("[handler] signal, not a return value; _exit(70)\n");
    ::_exit(70);
}

// main 里:映射两页,摸完第一页后把文件砍到一页,再摸第二页
mapped_region region(fd, 2 * static_cast<std::size_t>(page), PROT_READ, MAP_SHARED);
struct sigaction sa {};
sa.sa_sigaction = on_sigbus;
sa.sa_flags = SA_SIGINFO;
::sigaction(SIGBUS, &sa, nullptr);

volatile unsigned char first = region.data()[0];   // 页内,安全
sys_call("ftruncate", ::ftruncate, fd.get(), page); // 文件砍半,映射毫不知情
volatile unsigned char second = region.data()[page]; // 越界,SIGBUS 在此爆发
```

```text
$ ./sigbus; echo "exit code: $?"
mapped 2 pages (8192 bytes) at 0x79b28a8f8000
touch +0        ... ok
ftruncate -> 4096 bytes, mapping still claims 8192
touch +4096      ... beyond EOF

[handler] caught SIGBUS, faulting address = 0x000079b28a8f9000, si_code = BUS_ADRERR
[handler] signal, not a return value; _exit(70)
exit code: 70
```

One look at the output: 0x79b28a8f9000 is exactly base plus 4096, and in the second half of the program not shown in the excerpt there was one more `NOT REACHED` print, which never got its chance to run. `si_code` gave `BUS_ADRERR` — the kernel saying "the object behind this address is gone."

::: warning SIGBUS is mmap's most famous crash site
Inside the handler, only async-signal-safe functions like `write` and `_exit` will do; `printf` is out entirely — the lock it holds may not be released yet, and one careless move shows you a deadlock. For engineering defenses, we have a few in hand: measure the size accurately with `fstat` before mapping. If you computed the length yourself, stand behind it yourself — a length declared in a file header, for instance, can perfectly well be wrong. And when sharing a file with other processes, everyone agrees that nobody truncates.
:::

## mprotect: after the mapping is built, what changes is the permissions

`prot` is not a one-shot deal. After the mapping is built, `mprotect(addr, len, prot)` can still change it, and man 2 mprotect's wording deserves a careful taste: it modifies the protection of "all pages that overlap the interval [addr, addr+size-1]." The effect lands page-wise: any page that so much as touches the interval gets its whole protection changed. `addr`, however, must be page-aligned or you get EINVAL — this rounding is a chore it does not do for you the way mmap does; you align addr yourself. POSIX only guarantees it for memory obtained from mmap, but Linux is looser: nearly anywhere in the process's address space can be changed, even the code segment can be made writable for you. Access beyond your new permissions afterwards, and the kernel delivers a SIGSEGV, with `si_code` sorting the diagnosis for us: `SEGV_ACCERR` means the mapping is there but the permissions forbid it; `SEGV_MAPERR` means the address is in no mapping at all. We only count each diagnosis after seeing its crime scene once.

How do we photograph the crime scene? Every SIGSEGV-triggering action is performed by a forked child: the child installs an SA_SIGINFO handler, the handler uses only `write` to print `si_addr` and `si_code` and then calls `_exit(70 + scenario number)`, and the parent's `waitpid` collects and interprets the corpse — the same discipline as the SIGBUS experiment. The subject is a three-page anonymous RW mapping whose middle page we demote to `PROT_NONE`: that is a **guard page**, the same device used by stack-overflow probes and allocator isolation zones.

```cpp
// e1.cpp(节选):handler 与 fork/waitpid 的骨架
volatile sig_atomic_t g_which = -1; // 场景号必须走 volatile sig_atomic_t,见下方说明
constexpr const char* kNames[] = {"", "s1 read data page", "s2 guard page read",
                                  "s3 write after RW->R", "s4 write after R->RW",
                                  "s5 nullptr read"};

void on_sigsegv(int, siginfo_t* info, void*)
{
    write_all("\n[handler] SIGSEGV in scenario \"");
    write_all(kNames[g_which]);
    write_all("\", si_addr = ");
    write_hex(reinterpret_cast<std::uintptr_t>(info->si_addr));
    switch (info->si_code) {
    case SEGV_MAPERR: write_all(", si_code = SEGV_MAPERR(1)  <- 地址不在任何映射里\n"); break;
    case SEGV_ACCERR: write_all(", si_code = SEGV_ACCERR(2)  <- 映射在,权限不许\n"); break;
    default:          write_all(", si_code = ?\n"); break;
    }
    ::_exit(70 + g_which); // 70..74 按场景区分
}
```

Why is `g_which` a `volatile sig_atomic_t`? One piece of rework is worth recording verbatim. The first version put the scenario label in a plain `const char*` global, compiled with `-O2`, and the handler went on to print an **empty label**: the compiler had reordered the global's store to after the signal-triggering access, so the handler still read the uninitialized value. The warning in man 7 signal had been sitting right there — inside a handler, only `volatile sig_atomic_t` and `volatile std::atomic` are guaranteed visible — and we verified it with our own hands. Only after switching the scenario label to a number passed via `sig_atomic_t`, with the handler looking it up in a read-only name table, did things settle down.

```text
$ ./e1
anonymous 3 pages at 0x78245aadf000 .. 0x78245aae2000 (page size 4096)
mprotect([++4096, ++8192) -> PROT_NONE  (guard page)
-- scenario: s1 read data page 0            -> no signal, byte travels via exit code
   OK: child exited 81 as expected
-- scenario: s2 read PROT_NONE guard page 1  -> SIGSEGV SEGV_ACCERR

[handler] SIGSEGV in scenario "s2 guard page read", si_addr = 0x000078245aae0000, si_code = SEGV_ACCERR(2)  <- 映射在,权限不许
   OK: child exited 72 as expected
-- scenario: s3 write page 2, RW->R, rewrite -> SIGSEGV SEGV_ACCERR

[handler] SIGSEGV in scenario "s3 write after RW->R", si_addr = 0x000078245aae1000, si_code = SEGV_ACCERR(2)  <- 映射在,权限不许
   OK: child exited 73 as expected
-- scenario: s4 R->RW upgrade, write again   -> no signal, byte = 'U'
   OK: child exited 85 as expected
-- scenario: s5 dereference nullptr          -> SIGSEGV SEGV_MAPERR

[handler] SIGSEGV in scenario "s5 nullptr read", si_addr = 0x0000000000000000, si_code = SEGV_MAPERR(1)  <- 地址不在任何映射里
   OK: child exited 75 as expected
done: guard/readonly violations -> SEGV_ACCERR; unmapped -> SEGV_MAPERR
```

Line up the three handler outputs: s2's si_addr is 0x78245aae0000 — base plus 0x1000, the very start of the guard page. s3's is base plus 0x2000, and s5 lands on a plain 0. The division of labor in si_code is quite stable: reading a PROT_NONE page or writing a read-only page both bring SEGV_ACCERR; dereferencing a null pointer brings SEGV_MAPERR. s1 and s4 produced no signal — so how do we prove the data flowed? We have the child pass the byte it read back as its exit code: 81 is 'Q' and 85 is 'U'. An exit code is only 8 bits wide; using it to ship a byte is demo theater, so don't carry it into engineering. The drift terms also deserve clarity: the base address changes on every run — that is ASLR at work. One narrowing to do on the spot: s2's and s3's si_addr both land on page starts only because those two accesses hit exactly the first byte of a page; si_addr itself is not page-rounded, and what it reports for finer offsets we will check against the mprotect experiments in the memory-management chapter.

mprotect governs file views just as well, letting us turn the parameters section's prose assertion into a live test. The second experiment (e2) takes 16 bytes of AAAABBBBCCCCDDDD, builds a read-write view with an O_RDWR fd and `MAP_SHARED`, opens another O_RDONLY fd dedicated to pread as a bystander, and then walks five steps:

```cpp
// e2.cpp(节选):peek() 是 pread 16 字节的小封装,handler 与 e1 同款
std::memcpy(region.data(), "XXXX", 4);
std::print("1) write 'XXXX' as RW      : ok,  fd2 pread = {}\n", peek(fd2));

sys_call("mprotect->READ", ::mprotect, region.data(), region.size(), PROT_READ);
pid_t pid = ::fork();
if (pid == 0) {                       // 子进程在只读视图上写第一个字节
    struct sigaction sa {};
    sa.sa_sigaction = on_sigsegv;
    sa.sa_flags = SA_SIGINFO;
    ::sigaction(SIGSEGV, &sa, nullptr);
    g_which = 1;                      // 把场景号写进 volatile 变量,不许被重排
    volatile unsigned char* vp = region.data();
    *vp = 'Y';                        // SIGSEGV 应在这行爆发
    ::_exit(0);
}
int st = 0;
::waitpid(pid, &st, 0);
std::print("   parent: child exit status = {} (WIFEXITED={}, code={})\n",
           st, WIFEXITED(st), WEXITSTATUS(st));

sys_call("mprotect->RW", ::mprotect, region.data(), region.size(),
         PROT_READ | PROT_WRITE);
std::memcpy(region.data(), "ZZZZ", 4);
std::print("3) mprotect -> RW again    : ok,  write 'ZZZZ', fd2 pread = {}\n", peek(fd2));

int rc = ::mprotect(region.data() + 8, 4096, PROT_READ); // 地址不按页对齐
std::print("4) mprotect(base+8, ...)   : rc={}, errno={} ({})  <- addr 必须页对齐\n",
           rc, errno, std::strerror(errno));

void* p = ::mmap(nullptr, 16, PROT_WRITE, MAP_SHARED, fd2.get(), 0); // fd2 是只读的
std::print("5) O_RDONLY fd + PROT_WRITE + MAP_SHARED : {}",
           p == MAP_FAILED ? "MAP_FAILED" : "unexpected success");
if (p == MAP_FAILED) std::print(", errno={} ({})\n", errno, std::strerror(errno));
p = ::mmap(nullptr, 16, PROT_WRITE, MAP_PRIVATE, fd2.get(), 0); // 同一个 fd,换私有映射
std::print("   same fd, switch to MAP_PRIVATE         : {}",
           p == MAP_FAILED ? "MAP_FAILED" : "mapped (COW, legal)");
if (p == MAP_FAILED) std::print(", errno={} ({})\n", errno, std::strerror(errno));
else { std::print("\n"); ::munmap(p, 4096); }
```

```text
$ ./e2
mapped 16 bytes (rounded to 4096 B) at 0x787a3c315000, MAP_SHARED RW
1) write 'XXXX' as RW      : ok,  fd2 pread = XXXXBBBBCCCCDDDD
2) mprotect -> PROT_READ   : ok,  view is now read-only

[handler] SIGSEGV, si_addr = 0x0000787a3c315000, si_code = SEGV_ACCERR(2)
   parent: child exit status = 18176 (WIFEXITED=true, code=71)
3) mprotect -> RW again    : ok,  write 'ZZZZ', fd2 pread = ZZZZBBBBCCCCDDDD
4) mprotect(base+8, ...)   : rc=-1, errno=22 (Invalid argument)  <- addr 必须页对齐
5) O_RDONLY fd + PROT_WRITE + MAP_SHARED : MAP_FAILED, errno=13 (Permission denied)
   same fd, switch to MAP_PRIVATE         : mapped (COW, legal)
```

Four spots in this output deserve a look. First: the handler's si_addr is **exactly equal** to the mapping base printed in line 1, because the child wrote to `data()[0]`, and a forked child inherits the parent's mappings unchanged — the address stays put. Second, step 4: errno=22 is EINVAL, proving that mprotect's addr argument must be aligned to `sysconf(_SC_PAGE_SIZE)`; it lacks mmap's good manners of rounding length up for you. Third, step 5: errno=13 is EACCES — the read-only fd plus PROT_WRITE plus MAP_SHARED combination is rejected on the spot, while the same fd switched to MAP_PRIVATE is legal. The prose assertion in the parameters section becomes a live measurement here. Fourth, mind how things are printed: the raw waitpid status word is 18176, the bit layout of 71 shifted left by 8 — served raw it would frighten people — so in our displays we always use the `WEXITSTATUS`-decoded 71. One more man-page caution fits here: adding `PROT_WRITE` to a mapping of a file "opened read-only" also earns EACCES from mprotect. Anonymous mappings are free of this file-level constraint; promoting them to writable is fine. In the outputs of steps 1 and 3, fd2's pread saw the new value immediately; the weight of that glance we will weigh properly in the Dirty section.

## MAP_SHARED: two processes, one page cache

The parameters section said MAP_SHARED's writes are "immediately visible to other processes." On what authority does that stand? On the way the sharing works: a MAP_SHARED write lands **directly on the file's page-cache pages** — the kernel makes no private copy at all. And when another process maps the same range of the same file, its faults deliver the same physical pages. "Immediately visible" really rests on everyone already writing to the same memory; nobody needs to notify anybody. Claims need proof, so here is the experiment: the parent builds its mapping and then forks; the child **opens and mmaps the same file on its own** — an independently opened file description, its address different from the parent's — writes one byte, then notifies the parent through a pipe, sending its own mapping address along. There is no msync anywhere.

```cpp
// e4.cpp(节选):子进程里一行 stdio 都不碰,数据全走管道
pid_t pid = ::fork();
if (pid == 0) {                                   // ---- 子进程 ----
    unique_fd own{sys_call("open", ::open, path, O_RDWR)}; // 自己的打开文件描述
    void* p = ::mmap(nullptr, 16, PROT_READ | PROT_WRITE, MAP_SHARED, own.get(), 0);
    if (p == MAP_FAILED) { ::_exit(9); }
    static_cast<unsigned char*>(p)[4] = 'X';      // 改一个字节,落在页缓存
    struct { char tag; std::uint64_t addr; } msg{'R', reinterpret_cast<std::uint64_t>(p)};
    // 结构体有对齐填充,地址字段按 offsetof 取,别猜布局
    const unsigned off = static_cast<unsigned>(__builtin_offsetof(decltype(msg), addr));
    if (::write(pfd[1], &msg, off + sizeof msg.addr) != static_cast<ssize_t>(off + sizeof msg.addr)) { ::_exit(7); }
    ::_exit(0);                                   // 退出码 0,父进程按口径收尸
}

char buf[32] {};
constexpr unsigned kAddrOff = 8; // tag 之后有 7 字节填充,addr 从第 8 字节起
const unsigned want = kAddrOff + sizeof(std::uint64_t);
for (unsigned got = 0; got < want;) {             // 管道读也要循环,01 篇的教诲
    ssize_t n = sys_call("read-pipe", ::read, pfd[0], buf + got, want - got);
    if (n == 0) { break; }
    got += static_cast<unsigned>(n);
}
std::uint64_t child_addr = 0;
std::memcpy(&child_addr, buf + kAddrOff, sizeof child_addr);
std::print("child  : its OWN mmap at 0x{:x}, wrote 'X' at [4], pinged pipe\n", child_addr);
```

```text
$ ./e4
parent : mmap MAP_SHARED 16 B at 0x710c9d355000, view = AAAABBBBCCCCDDDD
child  : its OWN mmap at 0x710c9d354000, wrote 'X' at [4], pinged pipe
parent : after pipe ping (no msync anywhere):
  own mapping [4]     = X   <- 子进程写的字节,父亲自己的映射直接可见
  pread(fd, 16 B)     = AAAAXBBBCCCCDDDD   <- 独立 syscall 路径问文件,同一个答案
  whole view          = AAAAXBBBCCCCDDDD
waitpid: WIFEXITED=true code=0 (0 = child clean)
```

The parent's mapping sits at 0x710c9d355000 while the child's line reads 0x710c9d354000 — **different**, because the child's mapping is its own mmap, not a fork inheritance — yet what the parent reads in its own mapping is precisely that 'X'. Look also at the pread line: it travels a completely independent system-call path and still answers AAAAXBBBCCCCDDDD. We hold three roads — the parent's mapping, pread, and the child's mapping — and they converge on the same answer inside the page cache. The empirical proof we wanted, "writes land in the page cache and everyone sees them," is right here, and the output pointedly says no msync anywhere: visibility does not need writeback. Two drift terms to be clear about: both mapping addresses change every run, and the child sitting exactly one page below the parent is only a coincidence of this machine's kernel allocating top-down — don't write it up as a law. The child's exit code is 0, and waitpid's decoding is entirely normal. Having the child touch not one line of stdio is deliberate discipline; the feud between fork and stdio buffering gets its full accounting in the process article.

> One more small defensive move in the code is worth learning: the sent-back address is read out by `offsetof`. The first version decoded at tag plus 1, and the struct's 7 bytes of alignment padding turned the child's address into 0x0.

## msync and madvise: visible is one thing, on disk is another

After a MAP_SHARED write, what you have modified are pages in the page cache. Can other processes see them? The previous section just verified that yes, they can. But is the data on disk? We don't know. "Visible" is one fact and "written back" is another, and we separate them face to face with the **Dirty counter** in `/proc/meminfo`: Dirty records the system-wide count of modified page-cache pages not yet written back, in kB.

The experiment (e5) is designed like this: create a 128 MiB file on real-disk ext4 — one discipline to state up front: /tmp is tmpfs territory, its pages stay in memory, writeback never comes, and a data file placed there wastes the test — with a second O_RDONLY fd watching from the side. Once mmap has built the read-write view, we write 1 byte into each page, dirtying all 32768 of them. Before writing, we pread a sparse hole once in advance (it reads 0); right after writing we pread again, then we time `msync(MS_SYNC)`, and finally add a stretch of everyday behavior — writing only 1 MiB with no msync:

```cpp
// e5.cpp(节选):meminfo_kb() 扫 /proc/meminfo 取某字段的 kB 值
report("baseline");                       // Dirty 起点
for (std::size_t i = 0; i < pages; ++i) {
    region.data()[i * page] = static_cast<unsigned char>(i); // 每页摸 1 字节,页页变脏
}
report("after dirtying");
sys_call("pread-1", ::pread, fd2.get(), &seen, 1, static_cast<off_t>(page));       // 页 1
sys_call("pread-2", ::pread, fd2.get(), &seen, 1, static_cast<off_t>(12345 * page)); // 页 12345

auto t2 = std::chrono::steady_clock::now();
sys_call("msync", ::msync, region.data(), region.size(), MS_SYNC);
auto t3 = std::chrono::steady_clock::now();
std::print("msync(MS_SYNC) over 128 MiB       : {:.1f} ms\n",
           std::chrono::duration<double, std::milli>(t3 - t2).count());
report("after msync(MS_SYNC)");

for (std::size_t off = 0; off < kSmall; off += page) {   // 只写 1 MiB,不做 msync
    region.data()[off] = 0xAB;
}
report("after +1 MiB, no msync");
sys_call("pread-3", ::pread, fd2.get(), &seen, 1, 4096);
```

```text
$ sync && sleep 2 && ./e5
baseline                   Dirty =      316 kB, Writeback =      0 kB
pread byte @page 12345 before write : 0 (sparse hole reads as zero)
wrote 1 byte x 32768 pages in 32.7 ms
after dirtying             Dirty =   131388 kB, Writeback =      0 kB
pread byte @page 1 via 2nd fd now : 1 (expected 1, page index & 0xFF)
pread byte @page 12345 now        : 57 (expected 57, i.e. 12345 & 0xFF)
msync(MS_SYNC) over 128 MiB       : 50.6 ms
after msync(MS_SYNC)       Dirty =      316 kB, Writeback =      0 kB
after +1 MiB, no msync     Dirty =     1288 kB, Writeback =      0 kB
pread byte @page 1 now            : 171 (0xAB = 171, visible immediately)
```

316 rose to 131388, a Δ on the order of 131072 kB — exactly 128 MiB — the numbers did their own arithmetic. After msync, Dirty fell back to 316, matching the baseline to the byte. Writing another 1 MiB nudged Dirty back up to 1288. The two pread lines wedged in between are the soul of the show: while Dirty was still hanging high, page 1 read 1 and page 12345 read 57 (12345 & 0xFF — the byte we wrote is the page number's low eight bits), whereas before writing, the same positions read 0. Page 1 reads the new value while Dirty still hangs high: the two facts hold **simultaneously** in this output — visible and written back, separated face to face.

That 50.6 ms of msync is the price of pushing 128 MiB into the VHDX. We later ran the same program twice more — once 68.8 ms, once 48.6 ms — it jitters with disk load. Dirty's fall also deserves a second look: the output above shows 316 back to 316 exactly, but the two later runs landed at 340 and 436, respectively 60 and 24 kB above their own baselines of 280 and 412. Don't read those few dozen kB as unfinished writeback: Dirty is a system-wide counter, other processes on the machine write dirty pages too, and their share mixes into the settling value — so we read the Δ, not the absolute. Truly verifying on-disk contents would require root to drop_caches; this experiment takes MS_SYNC's return plus Dirty's fall as its standard of proof.

> One more environmental factor to note: this machine has `vm.dirty_background_ratio=10`, which with 15 GiB of memory puts the threshold around 1.5 GiB — well above our 128 MiB of dirtied pages — so background writeback does not jump the gun inside the experiment's window. Re-run on a smaller-memory machine and Dirty may start walking down on its own midway; don't panic if you see it.

The three flags of `msync(addr, len, flags)` can now be explained against the experiment. **MS_SYNC** starts a writeback and waits all the way to completion — use it when you want to control the moment data hits the disk; that 50.6 ms above is its work photo. **MS_ASYNC** was meant as "queue it, don't wait," but man 2 msync says outright that since Linux 2.6.19, MS_ASYNC is effectively a no-op: the kernel tracks dirty pages correctly and flushes on demand, leaving the "queue it" step with nothing to do. POSIX still requires you to pass one of MS_SYNC or MS_ASYNC, so don't omit it in portable code; Linux even allows omitting both, which currently behaves like MS_ASYNC. **MS_INVALIDATE** invalidates the file's other mappings so they pick up the freshly written-back values. If any page in the given range is locked by mlock, it answers with EBUSY.

`madvise(addr, len, advice)` is our channel for passing word to the kernel: "here is how I intend to use this mapping," and the kernel picks its readahead and caching strategy accordingly. All the ordinary advices leave program semantics untouched and touch only performance — **the sole exception is `MADV_DONTNEED`, which really does drop data**. Let's run through the common ones: `MADV_SEQUENTIAL` announces sequential access, so the kernel reads ahead aggressively and releases visited pages early. `MADV_RANDOM` takes the opposite stance, telling the kernel "don't read ahead for me" and sparing us a pile of uselessly hauled pages. `MADV_WILLNEED` says access is coming soon, so we may as well read some pages in ahead of time.

`MADV_DONTNEED` deserves a paragraph of its own, because the three kinds of mapping meet different ends. Shared file mappings, shared anonymous mappings, and shmem (System V shared memory, for example) all count as this class: a later access refills from the underlying mapped file's latest contents. Private anonymous mappings receive pages zeroed on demand. As for private file mappings — the man page's enumeration does not name them separately, but a comment in the kernel's mm/madvise.c speaks for them: "these pages can be discarded even when dirty." What gets dropped is precisely the COW'd private copy; the next fault rereads from the page cache under MAP_PRIVATE semantics, and your modifications vanish with the copy. In the fault_cost experiment's third pass back in the page-fault section, the read-only mapping kept the same sum after DONTNEED while the faults promptly returned — that was "rereading the original" at work. Two boundaries to record: on shared mappings, DONTNEED does not promise immediate release of the physical pages — the kernel may wait for a suitable moment — but the calling process's RSS drops immediately. Pages locked by mlock block the road entirely: if any are in the range, the whole call ends in EINVAL at once; dropping only the ordinary pages while keeping the locked ones cannot be done. man 2 madvise's ERRORS states exactly this, the kernel's mm/madvise.c shows the same behavior, and we record it as it is. Since kernel 6.1 there is a new key: MADV_DONTNEED_LOCKED, which drops even mlock'd pages; its definition sits in this machine's `/usr/include/asm-generic/mman-common.h`, ready for you to look up when needed.

madvise addresses a mapped address range; the read side actually has a symmetric channel: `posix_fadvise(fd, offset, size, advice)`. We announce the access pattern by fd plus range — `POSIX_FADV_SEQUENTIAL`, `POSIX_FADV_WILLNEED`, and `POSIX_FADV_DONTNEED` correspond one to one with madvise's same-named advices, except one governs the file and its page cache while the other governs mapped addresses. `POSIX_FADV_DONTNEED` attempts to release the cached pages of that range; when streaming through a large file, we hand it in periodically so as not to squeeze out more useful cache. Dirty pages will not be released, though — to guarantee release, fsync must go ahead of it.

## MAP_PRIVATE: copy-on-write, seen with our own eyes

MAP_PRIVATE's deal is "you change your copy; the file never knows." Claims need proof? We have the same process open two mappings and confront each other face to face: modify 4 bytes on the private mapping, ask the file itself directly with `pread`, and see what a freshly opened shared mapping shows. Finally we modify 4 bytes on the shared mapping, with one `msync(MS_SYNC)` as the control.

```cpp
// cow.cpp(节选):peek_file() 是 pread 16 字节的小封装
mapped_region priv(fd, 16, PROT_READ | PROT_WRITE, MAP_PRIVATE);
std::memcpy(priv.data(), "XXXX", 4); // 写的是 COW 出来的私有副本
std::print("file via pread   : {}\n", peek_file(fd)); // 文件纹丝不动

mapped_region shared(fd, 16, PROT_READ | PROT_WRITE, MAP_SHARED);
std::print("MAP_SHARED view  : {:.16s} (sees the ORIGINAL)\n",
           reinterpret_cast<const char*>(shared.data())); // reinterpret 转成 const char*,截断交给格式串

std::memcpy(shared.data() + 8, "YYYY", 4); // SHARED:直接改页缓存
sys_call("msync", ::msync, shared.data(), shared.size(), MS_SYNC); // 写回并等待
std::print("after msync file : {}\n", peek_file(fd));
```

```text
$ ./cow
MAP_PRIVATE view : XXXX... (just wrote)
file via pread   : AAAABBBBCCCCDDDD
MAP_SHARED view  : AAAABBBBCCCCDDDD (sees the ORIGINAL)
after msync file : AAAABBBBYYYYDDDD
```

The results are laid out right here: the private mapping clearly holds XXXX, yet `pread` still answers AAAA, and the freshly opened shared mapping sees the original too. Your modifications live only in the private copy — they cannot enter the file, and no other process ever sees them. Over on the shared mapping, write followed by `msync(MS_SYNC)`, and YYYY appears in the file immediately. Combined with the previous section's Dirty counter, we can now state the MAP_PRIVATE versus MAP_SHARED difference down to the byte: the former writes to a COW copy and does not so much as stir the Dirty counter; the latter writes the very page-cache pages, Dirty rises in response, and writeback is only a matter of time.

## 512 MiB sequential read: two machines, two answers

Now we run the comparison promised in the opening: a 512 MiB file generated with dd, read looping through a 1 MiB buffer, mmap summing the whole mapping — both computing the same checksum. The file lives in /tmp; both sides sit on tmpfs with the page cache resident, the disk variable is excluded, and what we measure is the pure CPU-side cost. The excerpt below is the skeleton of the contest: the desktop's round ran an earlier version of it, and the laptop's bench was reconstructed from it.

```cpp
// bench.cpp(节选):求和循环 sum_bytes 与计时打印略
if (mode == "read") {
    ::lseek(fd.get(), 0, SEEK_SET); // 前面量长度时 lseek 到了末尾,读之前回到开头
    std::vector<unsigned char> buf(1u << 20); // 1 MiB 缓冲
    for (;;) {
        ssize_t n = ::read(fd.get(), buf.data(), buf.size()); // 页缓存→用户缓冲,一次拷贝
        if (n == 0) {
            break;
        }
        sum += sum_bytes(buf.data(), static_cast<std::size_t>(n));
    }
} else {
    const int extra = (mode == "populate") ? MAP_POPULATE : 0;
    mapped_region region(fd, len, PROT_READ, MAP_PRIVATE | extra);
    sum = sum_bytes(region.data(), region.size()); // 首摸每页缺页,零拷贝
}
```

The desktop's (Ryzen 7 9700X) numbers are preserved as-is — they are measurements of the earlier version, whose code the author did not save at the time:

```text
$ for i in 1 2 3; do ./bench read big.bin; ./bench mmap big.bin; done
read     :   98.1 ms, 5218.9 MiB/s, sum 68449008524, minor faults 413
mmap     :   98.7 ms, 5188.1 MiB/s, sum 68449008524, minor faults 8349
read     :   75.8 ms, 6752.9 MiB/s, sum 68449008524, minor faults 413
mmap     :   89.1 ms, 5749.0 MiB/s, sum 68449008524, minor faults 8348
read     :   77.2 ms, 6627.8 MiB/s, sum 68449008524, minor faults 415
mmap     :   90.3 ms, 5670.2 MiB/s, sum 68449008524, minor faults 8347
$ ./bench populate big.bin
mmap(MAP_POPULATE) itself took 37.0 ms
populate :   92.5 ms, 5534.9 MiB/s, sum 68449008524, minor faults 8350
```

The laptop's (i7-13700H) bench was reconstructed from the excerpt, with the code and all outputs stored in the repository under `code/volumn_codes/vol8/systems-programming/linux/file-io/02-mmap-memory-mapping/09-bench/`:

```text
$ for i in 1 2 3; do ./bench read big.bin; ./bench mmap big.bin; done
read     :   110.4 ms, 4636.9 MiB/s, sum 68450753178, minor faults 446
mmap     :    79.1 ms, 6469.4 MiB/s, sum 68450753178, minor faults 8381
read     :    97.9 ms, 5230.3 MiB/s, sum 68450753178, minor faults 447
mmap     :    75.1 ms, 6819.7 MiB/s, sum 68450753178, minor faults 8380
read     :   107.0 ms, 4783.1 MiB/s, sum 68450753178, minor faults 447
mmap     :    76.7 ms, 6679.2 MiB/s, sum 68450753178, minor faults 8380
$ ./bench populate big.bin
mmap(MAP_POPULATE) itself took 8.8 ms
populate :    73.3 ms, 6988.0 MiB/s, sum 68450753178, minor faults 8514
```

With both machines' outputs side by side, let's read the steady states one machine at a time. We treat the first round as warm-up (frequency ramping, cache warming) and look only at the last two rounds: on the desktop, read holds steady at 76–77 ms while mmap posts 89–90 ms — **read wins the sequential read by about 15%**. On the laptop, it is exactly reversed: read falls at 98–107 ms while mmap takes only 75–77 ms — **mmap wins by about a quarter**. The same code ran to two opposite verdicts; the winner follows the machine.

The fault counts leave us an observable clue. Both machines show the same order of magnitude in shape: a read round takes only four hundred-some minor faults (the program and its buffers' own share), while an mmap round eats eight thousand-some — fault-around hangs ~16 pages per fault, which spreads out to about 8000. Where they truly diverge is the unit price: back at fault_cost's numbers from the page-fault section, for the same thousand-or-so faults, the desktop's first touch cost 7.20 ms while the laptop's cost only 1.42 ms — about five times apart. With equally many faults, the desktop's savings from skipping the 512 MiB copy carried the day, while the laptop's did not. That is as far as the observable explanation of the flip goes; the hardware causes we do not chase. MAP_POPULATE produced no surprises on either machine either: the desktop's 37 ms moved into the mmap call and the total landed at 92.5 ms, slightly worse; the laptop's 8.8 ms moved in and the total landed at 73.3 ms, essentially level with not populating. And don't blame system calls: 512 read calls, at the ~125 ns each measured in [the overview](../../00-overview.md), total on the order of 64 µs — nowhere near a bottleneck.

So when does mmap win? On random access into large files: you touch only the pages you want, while read either wastes readahead or crumbles into tiny syscalls. Multiple processes sharing a mapping of the same file is also mmap's home turf — the dynamic linker loading a .so is the textbook case, one set of physical pages bought for everyone's sharing. Add using a file directly as an in-memory structure, or a write path that wants zero copy. When does read get its turn? Smaller files, or when you want cross-platform-consistent behavior, or when you simply don't want to babysit SIGBUS. As for purely sequential large scans, readahead was tuned for read in the first place — yet we saw with our own eyes today that readahead's edge held on the desktop and failed on the laptop. Which answer your own machine is worth, measure with the bench above — a matter of minutes.

## How the other side sees it

Windows has no mmap; the same job is split into two steps there: `CreateFileMapping` creates a mapping object registering the file's HANDLE and the page protection, and `MapViewOfFile` then attaches it into the current process's address space — the latter is mmap's counterpart. MAP_PRIVATE's mirror is called FILE_MAP_COPY, likewise going the copy-on-write route. `msync(MS_SYNC)` has to be assembled from `FlushViewOfFile` plus `FlushFileBuffers`: the former alone only initiates the dirty-page writeback and does not wait for the disk — it stops halfway between MS_ASYNC and MS_SYNC. The difference to watch most carefully: Linux's SIGBUS script — the file gets truncated, then you touch beyond the end — simply cannot be filmed on Windows; the truncating step itself is intercepted by `SetEndOfFile`, and ERROR_USER_MAPPED_FILE blocks the accident before it happens. Writing to a read-only view delivers an ACCESS_VIOLATION structured exception, and an I/O error underneath a mapped view delivers EXCEPTION_IN_PAGE_ERROR. For the full implementation, see the mirror article: [File Mapping: CreateFileMapping and MapViewOfFile](../../windows/file-io/02-file-mapping.md) — you are welcome to read across.

## Summary

Everything this article confirmed by measurement is collected here for your later reference:

- mmap's six parameters: passing nullptr as `addr` is the most portable; non-null is only a hint; `MAP_FIXED` silently overwrites existing mappings; `MAP_FIXED_NOREPLACE` (since 4.17) reporting EEXIST on collision is the safe posture. `length` gets page-rounded up by the kernel; `prot` must not clash with the open mode (`MAP_SHARED`+`PROT_WRITE` requires a writable fd); `MAP_SHARED`/`MAP_PRIVATE` set the semantics; `offset` must be a multiple of `sysconf(_SC_PAGE_SIZE)`; the `fd` can be closed once mapped
- Failure returns `MAP_FAILED`, not nullptr. `mapped_region` manages it with RAII: move-only, and for the true rounded-up length, the figure `size()` reports is the one to trust
- Establishing a mapping costs nothing; the page fault waits for the first touch: the desktop measured ~0.44 µs/page on first touch and ~9 ns/page on the second (0.086 µs and 15 ns on the laptop — first touch being pricier holds across machines). fault-around lets 16384 pages cost only ~1028 faults. The minor-versus-major divide is whether the disk is touched; the counts live in field 10 of /proc/self/stat
- /proc/self/maps has six columns: one line per mapping, s/p corresponding to SHARED/PRIVATE, device number and inode traceable to the concrete file, and mmap/munmap is simply one line added or removed
- After the file is truncated, touching beyond the end brings the SIGBUS signal, not an error code. Handlers may use only async-signal-safe functions. Defense relies on fstat and standing behind your own length
- mprotect changes permissions after the fact: addr must be page-aligned and the effect lands on whole pages. The si_code division of labor on out-of-permission access: SEGV_ACCERR means the mapping exists but the permission forbids, SEGV_MAPERR means the address is unmapped (measured). A guard page is simply one PROT_NONE page
- MAP_SHARED's writes land directly in the page cache, immediately visible across processes, with no msync needed (measured: three paths, one answer). Visible does not mean written back: the Dirty counter separated the two face to face; msync(MS_SYNC) is on the order of 50 ms, and MS_ASYNC has been a no-op since 2.6.19
- madvise passes the access pattern along; the ordinary advices affect only performance, with MADV_DONTNEED the exception: shared mappings refill from the file, private anonymous mappings get zeroed, and private file mappings lose their COW copy. posix_fadvise is the symmetric channel on the read side
- MAP_PRIVATE's copy-on-write never enters the file (measured: XXXX never got in). The 512 MiB sequential read gave two machines two answers: the desktop's read ~15% faster, the laptop's mmap ~a quarter faster, and the explanation we could observe is the ~fivefold gap in the unit price of a fault. The settings where mmap reliably wins, we record as random access, sharing, and zero copy

<ReferenceCard title="Reference Resources">
  <ReferenceItem
    :id="1"
    title="mmap(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/mmap.2.html"
  />
  <ReferenceItem
    :id="2"
    title="mprotect(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/mprotect.2.html"
  />
  <ReferenceItem
    :id="3"
    title="msync(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/msync.2.html"
  />
  <ReferenceItem
    :id="4"
    title="madvise(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/madvise.2.html"
  />
  <ReferenceItem
    :id="5"
    title="posix_fadvise(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/posix_fadvise.2.html"
  />
  <ReferenceItem
    :id="6"
    title="proc_pid_maps(5)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man5/proc_pid_maps.5.html"
  />
  <ReferenceItem
    :id="7"
    title="proc_pid_stat(5)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man5/proc_pid_stat.5.html"
  />
  <ReferenceItem
    :id="8"
    author="Michael Kerrisk"
    title="The Linux Programming Interface"
    publisher="No Starch Press"
    :year="2010"
    url="https://man7.org/tlpi/"
  />
</ReferenceCard>
